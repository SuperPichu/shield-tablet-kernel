/*
 * drivers/video/tegra/nvmap/nvmap_pp.c
 *
 * Manage page pools to speed up page allocation.
 *
 * Copyright (c) 2009-2014, NVIDIA CORPORATION. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/moduleparam.h>
#include <linux/shrinker.h>
#include <linux/kthread.h>

#include "nvmap_priv.h"

#define NVMAP_TEST_PAGE_POOL_SHRINKER     1
#define PENDING_PAGES_SIZE                (SZ_1M / PAGE_SIZE)
#define MIN_AVAILABLE_MB                  128

static bool enable_pp = 1;
static int pool_size;

static struct task_struct *background_allocator;
static struct page *pending_pages[PENDING_PAGES_SIZE];
static atomic_t bg_pages_to_fill;
static atomic_t pp_dirty;

#ifdef CONFIG_NVMAP_PAGE_POOL_DEBUG
static inline void __pp_dbg_var_add(u64 *dbg_var, u32 nr)
{
	*dbg_var += nr;
}
#else
#define __pp_dbg_var_add(dbg_var, nr)
#endif

#define pp_alloc_add(pool, nr) __pp_dbg_var_add(&(pool)->allocs, nr)
#define pp_fill_add(pool, nr)  __pp_dbg_var_add(&(pool)->fills, nr)
#define pp_hit_add(pool, nr)   __pp_dbg_var_add(&(pool)->hits, nr)
#define pp_miss_add(pool, nr)  __pp_dbg_var_add(&(pool)->misses, nr)

static void pp_clean_cache(void)
{
	if (atomic_read(&pp_dirty)) {
		/*
		 * Make sure any data in the caches is cleaned out before
		 * passing these pages to userspace. otherwise, It can lead to
		 * corruption in pages that get mapped as something
		 * other than WB in userspace and leaked kernel data.
		 */
		inner_clean_cache_all();
		outer_clean_all();
		atomic_set(&pp_dirty, 0);
	}
}

/*
 * Allocate n pages one by one. Not the most efficient allocation scheme ever;
 * however, it will make it easier later on to handle single or small number of
 * page allocations from the page pool being individually freed.
 */
static int __nvmap_pp_alloc_n_pages(struct page **pages, int n, gfp_t flags)
{
	int i;

	for (i = 0; i < n; i++) {
		pages[i] = alloc_page(flags);
		if (!pages[i])
			goto no_mem;
	}

	return 0;

no_mem:
	for (i -= 1; i >= 0; i--)
		__free_page(pages[i]);
	return -ENOMEM;
}

/*
 * Fill a bunch of pages into the page pool. This will fill as many as it can
 * and return the number of pages filled. Pages are used from the start of the
 * passed page pointer array in a linear fashion.
 *
 * You must lock the page pool before using this.
 */
int __nvmap_page_pool_fill_lots_locked(struct nvmap_page_pool *pool,
				       struct page **pages, u32 nr)
{
	u32 real_nr;
	u32 ind = 0;

	real_nr = min_t(u32, pool->length - pool->count, nr);
	if (real_nr == 0)
		return 0;

	while (real_nr--) {
		if (IS_ENABLED(CONFIG_NVMAP_PAGE_POOL_DEBUG)) {
			BUG_ON(pp_full(pool));
			BUG_ON(pool->page_array[pool->fill]);
			atomic_inc(&pages[ind]->_count);
			BUG_ON(atomic_read(&pages[ind]->_count) != 2);
		}
		pool->page_array[pool->fill] = pages[ind++];
		nvmap_pp_fill_inc(pool);
	}

	pool->count += ind;
	pp_fill_add(pool, ind);

	return ind;
}

/*
 * Actually do the fill. This requires a few steps:
 *
 *  1. Allocate a bunch of pages.
 *
 *  2. Fill the page pool with the allocated pages. We don't want to hold the
 *     PP lock for too long so this is the only time we hold the PP lock.
 *
 *  3. Rinse and repeat until we have allocated all the pages we think we need
 *     or the page pool is full. Since we are not holding the lock for the
 *     entire fill it is possible that other pages were filled into the pool.
 *
 *  4. Free any left over pages if the pool is filled before we finish.
 */
static void nvmap_pp_do_background_fill(struct nvmap_page_pool *pool)
{
	int err;
	struct sysinfo info;
	u32 pages = 0, nr, i;
	gfp_t gfp = GFP_NVMAP | __GFP_NOMEMALLOC |
		    __GFP_NORETRY | __GFP_NO_KSWAPD;

	pages = (u32)atomic_xchg(&bg_pages_to_fill, pages);

	if (!pages || !enable_pp)
		return;

	/* If this param is set, force zero page allocation. */
	if (zero_memory)
		gfp |= __GFP_ZERO;

	do {
		si_meminfo(&info);
		if (info.freeram <= (MIN_AVAILABLE_MB << (20 - PAGE_SHIFT)))
			return;
		nr = min_t(u32, PENDING_PAGES_SIZE, pages);
		err = __nvmap_pp_alloc_n_pages(pending_pages, nr, gfp);
		if (err) {
			pr_info("Failed to alloc %u pages for PP!\n", pages);
			return;
		}

		nvmap_page_pool_lock(pool);
		atomic_set(&pp_dirty, 1);
		i = __nvmap_page_pool_fill_lots_locked(pool, pending_pages, nr);
		nvmap_page_pool_unlock(pool);
		pages -= nr;
	} while (pages && i == nr);

	for (; i < nr; i++)
		__free_page(pending_pages[i]);
	/* clean cache in the background so that allocations immediately
	 * after fill don't suffer the cache clean overhead.
	 */
	pp_clean_cache();
}

/*
 * This thread fills the page pools with zeroed pages. We avoid releasing the
 * pages directly back into the page pools since we would then have to zero
 * them ourselves. Instead it is easier to just reallocate zeroed pages. This
 * happens in the background so that the overhead of allocating zeroed pages is
 * not directly seen by userspace. Of course if the page pools are empty user
 * space will suffer.
 */
static int nvmap_background_zero_allocator(void *arg)
{
	pr_info("PP alloc thread starting.\n");

	while (1) {
		if (kthread_should_stop())
			break;

		nvmap_pp_do_background_fill(&nvmap_dev->pool);

		/* Pending work is done - go to sleep. */
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}

	return 0;
}

/*
 * Call this if the background allocator should possibly wake up. This function
 * will check to make sure its actually a good idea for that to happen before
 * waking the allocator up.
 */
static inline void nvmap_pp_wake_up_allocator(void)
{
	struct nvmap_page_pool *pool = &nvmap_dev->pool;
	struct sysinfo info;
	u32 free_pages, tmp;

	if (!enable_pp)
		return;

	/* Hueristic: if we don't need to prefill explicitly zero'ed memory then
	 * lots of memory can be placed back in the pools by possible frees.
	 * Therefor don't fill the pool unless we really need to as we may get
	 * more memory without needing to alloc pages.
	 */
	if (!zero_memory && pool->count > NVMAP_PP_ZERO_MEM_FILL_MIN)
		return;

	if (pool->length - pool->count < NVMAP_PP_DEF_FILL_THRESH)
		return;

	si_meminfo(&info);
	free_pages = (info.freeram * info.mem_unit) >> PAGE_SHIFT;

	tmp = free_pages - (MIN_AVAILABLE_MB << (20 - PAGE_SHIFT));
	if (tmp <= 0)
		return;

	/* Let the background thread know how much memory to fill. */
	atomic_set(&bg_pages_to_fill, min(tmp, pool->length - pool->count));
	wake_up_process(background_allocator);
}

/*
 * This removes a page from the page pool. If ignore_disable is set, then
 * the enable_pp flag is ignored.
 */

static struct page *nvmap_page_pool_alloc_locked(struct nvmap_page_pool *pool)
{
	struct page *page;

	if (pp_empty(pool)) {
		pp_miss_add(pool, 1);
		nvmap_pp_wake_up_allocator();
		return NULL;
	}

	if (IS_ENABLED(CONFIG_NVMAP_PAGE_POOL_DEBUG))
		BUG_ON(pool->count == 0);

	pp_clean_cache();
	page = pool->page_array[pool->alloc];
	pool->page_array[pool->alloc] = NULL;
	nvmap_pp_alloc_inc(pool);
	pool->count--;

	/* Sanity check. */
	if (IS_ENABLED(CONFIG_NVMAP_PAGE_POOL_DEBUG)) {
		atomic_dec(&page->_count);
		BUG_ON(atomic_read(&page->_count) != 1);
	}

	pp_alloc_add(pool, 1);
	pp_hit_add(pool, 1);
	nvmap_pp_wake_up_allocator();

	return page;
}

struct page *nvmap_page_pool_alloc(struct nvmap_page_pool *pool)
{
	struct page *page = NULL;

	if (pool) {
		nvmap_page_pool_lock(pool);
		page = nvmap_page_pool_alloc_locked(pool);
		nvmap_page_pool_unlock(pool);
	}
	return page;
}

/*
 * Alloc a bunch of pages from the page pool. This will alloc as many as it can
 * and return the number of pages allocated. Pages are placed into the passed
 * array in a linear fashion starting from index 0.
 *
 * You must lock the page pool before using this.
 */
int __nvmap_page_pool_alloc_lots_locked(struct nvmap_page_pool *pool,
					struct page **pages, u32 nr)
{
	u32 real_nr;
	u32 ind = 0;

	pp_clean_cache();

	real_nr = min_t(u32, nr, pool->count);

	while (real_nr--) {
		if (IS_ENABLED(CONFIG_NVMAP_PAGE_POOL_DEBUG)) {
			BUG_ON(pp_empty(pool));
			BUG_ON(!pool->page_array[pool->alloc]);
		}
		pages[ind++] = pool->page_array[pool->alloc];
		pool->page_array[pool->alloc] = NULL;
		nvmap_pp_alloc_inc(pool);
		if (IS_ENABLED(CONFIG_NVMAP_PAGE_POOL_DEBUG)) {
			atomic_dec(&pages[ind - 1]->_count);
			BUG_ON(atomic_read(&pages[ind - 1]->_count) != 1);
		}
	}

	pool->count -= ind;
	pp_alloc_add(pool, ind);
	pp_hit_add(pool, ind);
	pp_miss_add(pool, nr - ind);
	nvmap_pp_wake_up_allocator();

	return ind;
}

/*
 * This adds a page to the pool. Returns true iff the passed page is added.
 * That means if the pool is full this operation will fail.
 */
static bool nvmap_page_pool_fill_locked(struct nvmap_page_pool *pool,
					struct page *page)
{
	if (pp_full(pool))
		return false;

	/* Sanity check. */
	if (IS_ENABLED(CONFIG_NVMAP_PAGE_POOL_DEBUG)) {
		atomic_inc(&page->_count);
		BUG_ON(atomic_read(&page->_count) != 2);
		BUG_ON(pool->count > pool->length);
		BUG_ON(pool->page_array[pool->fill] != NULL);
	}

	atomic_set(&pp_dirty, 1);

	pool->page_array[pool->fill] = page;
	nvmap_pp_fill_inc(pool);
	pool->count++;
	pp_fill_add(pool, 1);

	return true;
}

bool nvmap_page_pool_fill(struct nvmap_page_pool *pool, struct page *page)
{
	bool ret = false;

	if (pool) {
		nvmap_page_pool_lock(pool);
		ret = nvmap_page_pool_fill_locked(pool, page);
		nvmap_page_pool_unlock(pool);
	}

	return ret;
}

static int nvmap_page_pool_get_available_count(struct nvmap_page_pool *pool)
{
	return pool->count;
}

static int nvmap_page_pool_free(struct nvmap_page_pool *pool, int nr_free)
{
	int i = nr_free;
	struct page *page;

	if (!nr_free)
		return nr_free;

	nvmap_page_pool_lock(pool);
	while (i) {
		page = nvmap_page_pool_alloc_locked(pool);
		if (!page)
			break;
		__free_page(page);
		i--;
	}
	nvmap_page_pool_unlock(pool);

	return i;
}

ulong nvmap_page_pool_get_unused_pages(void)
{
	int total = 0;

	if (!nvmap_dev)
		return 0;

	total = nvmap_page_pool_get_available_count(&nvmap_dev->pool);

	return total;
}

/*
 * Remove and free to the system all the pages currently in the page
 * pool. This operation will happen even if the page pools are disabled.
 */
int nvmap_page_pool_clear(void)
{
	struct page *page;
	struct nvmap_page_pool *pool = &nvmap_dev->pool;

	if (!pool->page_array)
		return 0;

	nvmap_page_pool_lock(pool);

	while ((page = nvmap_page_pool_alloc_locked(pool)) != NULL)
		__free_page(page);

	/* For some reason, if an error occured... */
	if (!pp_empty(pool)) {
		nvmap_page_pool_unlock(pool);
		return -ENOMEM;
	}

	nvmap_page_pool_unlock(pool);
	nvmap_pp_wake_up_allocator();

	return 0;
}

/*
 * Resizes the page pool to the passed size. If the passed size is 0 then
 * all associated resources are released back to the system. This operation
 * will only occur if the page pools are enabled.
 */
static void nvmap_page_pool_resize(struct nvmap_page_pool *pool, int size)
{
	int ind;
	struct page **page_array = NULL;

	if (size == pool->length)
		return;

	nvmap_page_pool_lock(pool);
	if (size == 0) {
		/* TODO: fix this! */
		vfree(pool->page_array);
		pool->page_array = NULL;
		goto out;
	}

	page_array = vzalloc(sizeof(struct page *) * size);
	if (!page_array)
		goto fail;

	/*
	 * Reuse what pages we can.
	 */
	ind = __nvmap_page_pool_alloc_lots_locked(pool, page_array, size);

	/*
	 * And free anything that might be left over.
	 */
	while (!pp_empty(pool))
		__free_page(nvmap_page_pool_alloc_locked(pool));

	swap(page_array, pool->page_array);
	pool->alloc = 0;
	pool->fill = (ind == size ? 0 : ind);
	pool->count = ind;
	pool->length = size;

	vfree(page_array);

out:
	pr_debug("page pool resized to %d from %d pages\n", size, pool->length);
	pool->length = size;
	goto exit;
fail:
	vfree(page_array);
	pr_err("page pool resize failed\n");
exit:
	nvmap_page_pool_unlock(pool);
}

static int nvmap_page_pool_shrink(struct shrinker *shrinker,
				  struct shrink_control *sc)
{
	int shrink_pages = sc->nr_to_scan;

	if (!shrink_pages)
		goto out;

	pr_debug("sh_pages=%d", shrink_pages);

	shrink_pages = nvmap_page_pool_free(&nvmap_dev->pool, shrink_pages);
out:
	return nvmap_page_pool_get_unused_pages();
}

static struct shrinker nvmap_page_pool_shrinker = {
	.shrink = nvmap_page_pool_shrink,
	.seeks = 1,
};

static void shrink_page_pools(int *total_pages, int *available_pages)
{
	struct shrink_control sc;

	if (*total_pages == 0) {
		sc.gfp_mask = GFP_KERNEL;
		sc.nr_to_scan = 0;
		*total_pages = nvmap_page_pool_shrink(NULL, &sc);
	}
	sc.nr_to_scan = *total_pages;
	*available_pages = nvmap_page_pool_shrink(NULL, &sc);
}

#if NVMAP_TEST_PAGE_POOL_SHRINKER
static int shrink_pp;
static int shrink_set(const char *arg, const struct kernel_param *kp)
{
	int cpu = smp_processor_id();
	unsigned long long t1, t2;
	int total_pages, available_pages;

	param_set_int(arg, kp);

	if (shrink_pp) {
		total_pages = shrink_pp;
		t1 = cpu_clock(cpu);
		shrink_page_pools(&total_pages, &available_pages);
		t2 = cpu_clock(cpu);
		pr_debug("shrink page pools: time=%lldns, "
			"total_pages_released=%d, free_pages_available=%d",
			t2-t1, total_pages, available_pages);
	}
	return 0;
}

static int shrink_get(char *buff, const struct kernel_param *kp)
{
	return param_get_int(buff, kp);
}

static struct kernel_param_ops shrink_ops = {
	.get = shrink_get,
	.set = shrink_set,
};

module_param_cb(shrink_page_pools, &shrink_ops, &shrink_pp, 0644);
#endif

static int enable_pp_set(const char *arg, const struct kernel_param *kp)
{
	int total_pages, available_pages, ret;

	ret = param_set_bool(arg, kp);
	if (ret)
		return ret;

	if (!enable_pp) {
		total_pages = 0;
		shrink_page_pools(&total_pages, &available_pages);
		pr_info("disabled page pools and released pages, "
			"total_pages_released=%d, free_pages_available=%d",
			total_pages, available_pages);
	}
	return 0;
}

static int enable_pp_get(char *buff, const struct kernel_param *kp)
{
	return param_get_int(buff, kp);
}

static struct kernel_param_ops enable_pp_ops = {
	.get = enable_pp_get,
	.set = enable_pp_set,
};

module_param_cb(enable_page_pools, &enable_pp_ops, &enable_pp, 0644);

static int pool_size_set(const char *arg, const struct kernel_param *kp)
{
	param_set_int(arg, kp);
	nvmap_page_pool_resize(&nvmap_dev->pool, pool_size);
	return 0;
}

static int pool_size_get(char *buff, const struct kernel_param *kp)
{
	return param_get_int(buff, kp);
}

static struct kernel_param_ops pool_size_ops = {
	.get = pool_size_get,
	.set = pool_size_set,
};

module_param_cb(pool_size, &pool_size_ops, &pool_size, 0644);

int nvmap_page_pool_init(struct nvmap_device *dev)
{
	static int reg = 1;
	unsigned long totalram_mb;
	struct sysinfo info;
	struct nvmap_page_pool *pool = &dev->pool;
	struct sched_param param = {};
#ifdef CONFIG_NVMAP_PAGE_POOLS_INIT_FILLUP
	int i;
	struct page *page;
	int pages_to_fill;
	int highmem_pages = 0;
#endif

	memset(pool, 0x0, sizeof(*pool));
	mutex_init(&pool->lock);

	si_meminfo(&info);
	totalram_mb = (info.totalram * info.mem_unit) >> 20;
	pr_info("Total MB RAM: %lu\n", totalram_mb);

	if (!CONFIG_NVMAP_PAGE_POOL_SIZE)
		/* The ratio is KB to MB so this ends up being mem in KB which
		 * when >> 2 -> total pages in the pool. */
		pool->length = (totalram_mb * NVMAP_PP_POOL_SIZE) >> 2;
	else
		pool->length = CONFIG_NVMAP_PAGE_POOL_SIZE;

	if (pool->length >= info.totalram)
		goto fail;
	pool_size = pool->length;

	pr_info("nvmap page pool size: %u pages (%u MB)\n", pool->length,
		pool->length >> 8);
	pool->page_array = vzalloc(sizeof(struct page *) * pool->length);
	if (!pool->page_array)
		goto fail;

	if (reg) {
		reg = 0;
		register_shrinker(&nvmap_page_pool_shrinker);
	}

	background_allocator = kthread_create(nvmap_background_zero_allocator,
					    NULL, "nvmap-bz");
	if (IS_ERR_OR_NULL(background_allocator))
		goto fail;

	/* set nvmap-bz to very low priority */
	param.sched_priority = background_allocator->rt_priority;
	if (sched_setscheduler(background_allocator, SCHED_IDLE, &param))
		goto fail;

#ifdef CONFIG_NVMAP_PAGE_POOLS_INIT_FILLUP
	pages_to_fill = CONFIG_NVMAP_PAGE_POOLS_INIT_FILLUP_SIZE * SZ_1M /
			PAGE_SIZE;
	pages_to_fill = pages_to_fill ? : pool->length;

	nvmap_page_pool_lock(pool);
	for (i = 0; i < pages_to_fill; i++) {
		page = alloc_page(GFP_NVMAP);
		if (!page)
			goto done;
		if (!nvmap_page_pool_fill_locked(pool, page)) {
			__free_page(page);
			goto done;
		}
		if (PageHighMem(page))
			highmem_pages++;
	}
	si_meminfo(&info);
	pr_info("highmem=%d, pool_size=%d,"
		"totalram=%lu, freeram=%lu, totalhigh=%lu, freehigh=%lu\n",
		highmem_pages, pool->length,
		info.totalram, info.freeram, info.totalhigh, info.freehigh);
done:
	nvmap_page_pool_unlock(pool);
#endif
	return 0;
fail:
	nvmap_page_pool_fini(dev);
	return -ENOMEM;
}

int nvmap_page_pool_fini(struct nvmap_device *dev)
{
	struct nvmap_page_pool *pool = &dev->pool;

	if (!IS_ERR_OR_NULL(background_allocator))
		kthread_stop(background_allocator);
	pool->length = 0;
	vfree(pool->page_array);

	return 0;
}
