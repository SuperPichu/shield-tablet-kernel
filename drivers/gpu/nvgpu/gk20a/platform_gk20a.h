/*
 * drivers/video/tegra/host/gk20a/soc/platform_gk20a.h
 *
 * GK20A Platform (SoC) Interface
 *
 * Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _GK20A_PLATFORM_H_
#define _GK20A_PLATFORM_H_

#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/dma-attrs.h>

struct gk20a;
struct channel_gk20a;
struct gr_ctx_buffer_desc;
struct gk20a_scale_profile;

struct secure_page_buffer {
	void (*destroy)(struct platform_device *, struct secure_page_buffer *);
	size_t size;
	u64 iova;
	struct dma_attrs attrs;
};

struct gk20a_platform {
#ifdef CONFIG_TEGRA_GK20A
	u32 syncpt_base;
#endif
	/* Populated by the gk20a driver before probing the platform. */
	struct gk20a *g;

	/* Should be populated at probe. */
	bool can_railgate;

	/* Should be populated at probe. */
	bool has_syncpoints;

	/* Should be populated by probe. */
	struct dentry *debugfs;

	/* Clock configuration is stored here. Platform probe is responsible
	 * for filling this data. */
	struct clk *clk[3];
	int num_clks;

	/* Delay before rail gated */
	int railgate_delay;

	/* Delay before clock gated */
	int clockgate_delay;

	/* Initialize the platform interface of the gk20a driver.
	 *
	 * The platform implementation of this function must
	 *   - set the power and clocks of the gk20a device to a known
	 *     state, and
	 *   - populate the gk20a_platform structure (a pointer to the
	 *     structure can be obtained by calling gk20a_get_platform).
	 *
	 * After this function is finished, the driver will initialise
	 * pm runtime and genpd based on the platform configuration.
	 */
	int (*probe)(struct platform_device *dev);

	/* Second stage initialisation - called once all power management
	 * initialisations are done.
	 */
	int (*late_probe)(struct platform_device *dev);

	/* This function is called to allocate secure memory (memory that the
	 * CPU cannot see). The function should fill the context buffer
	 * descriptor (especially fields destroy, sgt, size).
	 */
	int (*secure_alloc)(struct platform_device *dev,
			    struct gr_ctx_buffer_desc *desc,
			    size_t size);

	/* Function to allocate a secure buffer of PAGE_SIZE at probe time.
	 * This is also helpful to trigger secure memory resizing
	 * while GPU is off
	 */
	int (*secure_page_alloc)(struct platform_device *dev);
	struct secure_page_buffer secure_buffer;
	bool secure_alloc_ready;

	/* Device is going to be suspended */
	int (*suspend)(struct device *);

	/* Called to turn off the device */
	int (*railgate)(struct platform_device *dev);

	/* Called to turn on the device */
	int (*unrailgate)(struct platform_device *dev);
	struct mutex railgate_lock;

	/* Called to check state of device */
	bool (*is_railgated)(struct platform_device *dev);

	/* Postscale callback is called after frequency change */
	void (*postscale)(struct platform_device *pdev,
			  unsigned long freq);

	/* Pre callback is called before frequency change */
	void (*prescale)(struct platform_device *pdev);

	/* Devfreq governor name. If scaling is enabled, we request
	 * this governor to be used in scaling */
	const char *devfreq_governor;

	/* Quality of service id. If this is set, the scaling routines
	 * will register a callback to id. Each time we receive a new value,
	 * the postscale callback gets called.  */
	int qos_id;

	/* Called as part of debug dump. If the gpu gets hung, this function
	 * is responsible for delivering all necessary debug data of other
	 * hw units which may interact with the gpu without direct supervision
	 * of the CPU.
	 */
	void (*dump_platform_dependencies)(struct platform_device *dev);
};

static inline struct gk20a_platform *gk20a_get_platform(
		struct platform_device *dev)
{
	return (struct gk20a_platform *)platform_get_drvdata(dev);
}

extern struct gk20a_platform gk20a_generic_platform;
#ifdef CONFIG_TEGRA_GK20A
extern struct gk20a_platform gk20a_tegra_platform;
#endif

static inline bool gk20a_platform_has_syncpoints(struct platform_device *dev)
{
	struct gk20a_platform *p = gk20a_get_platform(dev);
	return p->has_syncpoints;
}

#endif
