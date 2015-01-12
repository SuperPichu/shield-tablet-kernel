/*
 * arch/arm/mach-tegra/board-vcm30_t124-panel.c
 *
 * Copyright (c) 2013-2014, NVIDIA CORPORATION.  All rights reserved.
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
#include <linux/ioport.h>
#include <linux/fb.h>
#include <linux/nvmap.h>
#include <linux/nvhost.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/dma-mapping.h>

#include <mach/irqs.h>
#include <mach/dc.h>

#include "board.h"
#include "devices.h"
#include "gpio-names.h"
#include "board-vcm30_t124.h"
#include "board-panel.h"
#include "common.h"
#include "iomap.h"
#include "tegra12_host1x_devices.h"


struct platform_device * __init vcm30_t124_host1x_init(void)
{
	struct platform_device *pdev = NULL;

#ifdef CONFIG_TEGRA_GRHOST
	if (!of_have_populated_dt())
		pdev = tegra12_register_host1x_devices();
	else
		pdev = to_platform_device(bus_find_device_by_name(
			&platform_bus_type, NULL, "host1x"));

	if (!pdev) {
		pr_err("host1x devices registration failed\n");
		return NULL;
	}
#endif
	return pdev;
}

#ifndef CONFIG_TEGRA_HDMI_PRIMARY
/* XXX: EDP is not functionally tested yet */
static struct resource vcm30_t124_disp1_resources[] = {
	{
		.name	= "irq",
		.start	= INT_DISPLAY_GENERAL,
		.end	= INT_DISPLAY_GENERAL,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "regs",
		.start	= TEGRA_DISPLAY_BASE,
		.end	= TEGRA_DISPLAY_BASE + TEGRA_DISPLAY_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "fbmem",
		.start	= 0, /* Filled in by vcm30_t124_panel_init() */
		.end	= 0, /* Filled in by vcm30_t124_panel_init() */
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "mipi_cal",
		.start	= TEGRA_MIPI_CAL_BASE,
		.end	= TEGRA_MIPI_CAL_BASE + TEGRA_MIPI_CAL_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name   = "sor",
		.start  = TEGRA_SOR_BASE,
		.end    = TEGRA_SOR_BASE + TEGRA_SOR_SIZE - 1,
		.flags  = IORESOURCE_MEM,
	},
	{
		.name   = "dpaux",
		.start  = TEGRA_DPAUX_BASE,
		.end    = TEGRA_DPAUX_BASE + TEGRA_DPAUX_SIZE - 1,
		.flags  = IORESOURCE_MEM,
	},
	{
		.name	= "irq_dp",
		.start	= INT_DPAUX,
		.end	= INT_DPAUX,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct tegra_fb_data vcm30_t124_disp1_fb_data = {
	.win		= 0,
	.bits_per_pixel = 32,
};

static struct tegra_dc_out vcm30_t124_disp1_out = {
	.type		= TEGRA_DC_OUT_DP,
};

static struct tegra_dc_platform_data vcm30_t124_disp1_pdata = {
	.flags		= TEGRA_DC_FLAG_ENABLED,
	.default_out	= &vcm30_t124_disp1_out,
	.fb		= &vcm30_t124_disp1_fb_data,
	.emc_clk_rate	= 204000000,
#ifdef CONFIG_TEGRA_DC_CMU
	.cmu_enable	= 1,
#endif
};

static struct platform_device vcm30_t124_disp1_device = {
	.name		= "tegradc",
	.id		= 0,
	.resource	= vcm30_t124_disp1_resources,
	.num_resources	= ARRAY_SIZE(vcm30_t124_disp1_resources),
	.dev = {
		.platform_data = &vcm30_t124_disp1_pdata,
	},
};
#endif

static struct resource vcm30_t124_disp2_resources[] = {
	{
		.name	= "irq",
#ifndef CONFIG_TEGRA_HDMI_PRIMARY
		.start	= INT_DISPLAY_B_GENERAL,
		.end	= INT_DISPLAY_B_GENERAL,
#else
		.start	= INT_DISPLAY_GENERAL,
		.end	= INT_DISPLAY_GENERAL,
#endif
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "regs",
#ifndef CONFIG_TEGRA_HDMI_PRIMARY
		.start	= TEGRA_DISPLAY2_BASE,
		.end	= TEGRA_DISPLAY2_BASE + TEGRA_DISPLAY2_SIZE - 1,
#else
		.start	= TEGRA_DISPLAY_BASE,
		.end	= TEGRA_DISPLAY_BASE + TEGRA_DISPLAY_SIZE - 1,
#endif
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "fbmem",
		.start	= 0, /* Filled in by tegra_init_hdmi() */
		.end	= 0, /* Filled in by tegra_init_hdmi() */
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "hdmi_regs",
		.start	= TEGRA_HDMI_BASE,
		.end	= TEGRA_HDMI_BASE + TEGRA_HDMI_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static int vcm30_t124_hdmi_enable(struct device *dev)
{
	return 0;
}

static int vcm30_t124_hdmi_disable(void)
{
	return 0;
}

static int vcm30_t124_hdmi_postsuspend(void)
{
	return 0;
}

static int vcm30_t124_hdmi_hotplug_init(struct device *dev)
{
	return 0;
}

/* XXX: These values are taken from ardbeg. To be fixed after VCM char */
struct tmds_config vcm30_t124_tmds_config[] = {
	{ /* 480p/576p / 25.2MHz/27MHz modes */
	.pclk = 27000000,
	.pll0 = 0x01003110,
	.pll1 = 0x00300F00,
	.pe_current = 0x08080808,
	.drive_current = 0x2e2e2e2e,
	.peak_current = 0x00000000,
	},
	{ /* 720p / 74.25MHz modes */
	.pclk = 74250000,
	.pll0 =  0x01003310,
	.pll1 = 0x10300F00,
	.pe_current = 0x08080808,
	.drive_current = 0x20202020,
	.peak_current = 0x00000000,
	},
	{ /* 1080p / 148.5MHz modes */
	.pclk = 148500000,
	.pll0 = 0x01003310,
	.pll1 = 0x10300F00,
	.pe_current = 0x08080808,
	.drive_current = 0x20202020,
	.peak_current = 0x00000000,
	},
	{
	.pclk = INT_MAX,
	.pll0 = 0x01003310,
	.pll1 = 0x10300F00,
	.pe_current = 0x08080808,
	.drive_current = 0x3A353536, /* lane3 needs a slightly lower current */
	.peak_current = 0x00000000,
	},
};

struct tegra_hdmi_out vcm30_t124_hdmi_out = {
	.tmds_config = vcm30_t124_tmds_config,
	.n_tmds_config = ARRAY_SIZE(vcm30_t124_tmds_config),
};

#ifdef CONFIG_TEGRA_HDMI_PRIMARY
static struct tegra_dc_mode hdmi_panel_modes[] = {
	{
		.pclk =			148500000,
		.h_ref_to_sync =	1,
		.v_ref_to_sync =	1,
		.h_sync_width =		44,	/* hsync_len */
		.v_sync_width =		5,	/* vsync_len */
		.h_back_porch =		148,	/* left_margin */
		.v_back_porch =		36,	/* upper_margin */
		.h_active =			1280,	/* xres */
		.v_active =			720,	/* yres */
		.h_front_porch =	88,	/* right_margin */
		.v_front_porch =	4,	/* lower_margin */
	},
};
#endif

#define VCM30_T124_HDMI_HPD  TEGRA_GPIO_PN7
static struct tegra_dc_out vcm30_t124_disp2_out = {
	.type		= TEGRA_DC_OUT_HDMI,
	.flags		= TEGRA_DC_OUT_HOTPLUG_LOW |
			  TEGRA_DC_OUT_NVHDCP_POLICY_ON_DEMAND,
	.parent_clk	= "pll_d2",

	.ddc_bus	= 3,
	.hotplug_gpio	= VCM30_T124_HDMI_HPD,
	.hdmi_out	= &vcm30_t124_hdmi_out,

	/* TODO: update max pclk to POR */
	.max_pixclock	= KHZ2PICOS(297000),
#if defined(CONFIG_TEGRA_HDMI_PRIMARY)
	.modes = hdmi_panel_modes,
	.n_modes = ARRAY_SIZE(hdmi_panel_modes),
	.depth = 24,
#endif /* CONFIG_FRAMEBUFFER_CONSOLE */

	.align		= TEGRA_DC_ALIGN_MSB,
	.order		= TEGRA_DC_ORDER_RED_BLUE,

	.enable		= vcm30_t124_hdmi_enable,
	.disable	= vcm30_t124_hdmi_disable,
	.postsuspend	= vcm30_t124_hdmi_postsuspend,
	.hotplug_init	= vcm30_t124_hdmi_hotplug_init,
};

static struct tegra_fb_data vcm30_t124_disp2_fb_data = {
	.win		= 0,
	.xres		= 1280,
	.yres		= 720,
	.bits_per_pixel = 32,
};

static struct tegra_dc_platform_data vcm30_t124_disp2_pdata = {
	.default_out	= &vcm30_t124_disp2_out,
	.fb		= &vcm30_t124_disp2_fb_data,
	.emc_clk_rate	= 300000000,
};

static struct platform_device vcm30_t124_disp2_device = {
	.name		= "tegradc",
#ifndef CONFIG_TEGRA_HDMI_PRIMARY
	.id		= 1,
#else
	.id		= 0,
#endif
	.resource	= vcm30_t124_disp2_resources,
	.num_resources	= ARRAY_SIZE(vcm30_t124_disp2_resources),
	.dev = {
		.platform_data = &vcm30_t124_disp2_pdata,
	},
};

static struct nvmap_platform_carveout vcm30_t124_carveouts[] = {
	[0] = {
		.name		= "iram",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_IRAM,
		.base		= TEGRA_IRAM_BASE + TEGRA_RESET_HANDLER_SIZE,
		.size		= TEGRA_IRAM_SIZE - TEGRA_RESET_HANDLER_SIZE,
		.dma_dev	= &tegra_iram_dev,
	},
	[1] = {
		.name		= "generic-0",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_GENERIC,
		.base		= 0, /* Filled in by vcm30_t124_panel_init() */
		.size		= 0, /* Filled in by vcm30_t124_panel_init() */
		.dma_dev	= &tegra_generic_dev,
	},
	[2] = {
		.name		= "vpr",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_VPR,
		.base		= 0, /* Filled in by vcm30_t124_panel_init() */
		.size		= 0, /* Filled in by vcm30_t124_panel_init() */
		.dma_dev	= &tegra_vpr_dev,
	},
};

static struct nvmap_platform_data vcm30_t124_nvmap_data = {
	.carveouts	= vcm30_t124_carveouts,
	.nr_carveouts	= ARRAY_SIZE(vcm30_t124_carveouts),
};
static struct platform_device vcm30_t124_nvmap_device  = {
	.name	= "tegra-nvmap",
	.id	= -1,
	.dev	= {
		.platform_data = &vcm30_t124_nvmap_data,
	},
};

int __init vcm30_t124_panel_init(void)
{
	int err = 0;
	struct resource *res;
	struct platform_device *phost1x = NULL;
#ifdef CONFIG_NVMAP_USE_CMA_FOR_CARVEOUT
	struct dma_declare_info vpr_dma_info;
	struct dma_declare_info generic_dma_info;

	vcm30_t124_carveouts[1].base = tegra_carveout_start;
	vcm30_t124_carveouts[1].size = tegra_carveout_size;

	vcm30_t124_carveouts[2].base = tegra_vpr_start;
	vcm30_t124_carveouts[2].size = tegra_vpr_size;

#ifdef CONFIG_NVMAP_USE_CMA_FOR_CARVEOUT
	generic_dma_info.name = "generic";
	generic_dma_info.base = tegra_carveout_start;
	generic_dma_info.size = tegra_carveout_size;
	generic_dma_info.resize = false;
	generic_dma_info.cma_dev = NULL;

	vpr_dma_info.name = "vpr";
	vpr_dma_info.base = tegra_vpr_start;
	vpr_dma_info.size = tegra_vpr_size;
	vpr_dma_info.resize = false;
	vpr_dma_info.cma_dev = NULL;
	vcm30_t124_carveouts[1].cma_dev = &tegra_generic_cma_dev;
	vcm30_t124_carveouts[1].resize = false;
	vcm30_t124_carveouts[2].cma_dev = &tegra_vpr_cma_dev;
	vcm30_t124_carveouts[2].resize = true;

	vpr_dma_info.size = SZ_32M;
	vpr_dma_info.resize = true;
	vpr_dma_info.cma_dev = &tegra_vpr_cma_dev;
	vpr_dma_info.notifier.ops = &vpr_dev_ops;

	if (tegra_carveout_size) {
		err = dma_declare_coherent_resizable_cma_memory(
				&tegra_generic_dev, &generic_dma_info);
		if (err) {
			pr_err("Generic coherent memory declaration failed\n");
			return err;
		}
	}
	if (tegra_vpr_size) {
		err = dma_declare_coherent_resizable_cma_memory(
				&tegra_vpr_dev, &vpr_dma_info);
		if (err) {
			pr_err("VPR coherent memory declaration failed\n");
			return err;
		}
	}
#endif

	err = platform_device_register(&vcm30_t124_nvmap_device);
	if (err) {
		pr_err("nvmap device registration failed\n");
		return err;
	}
#endif

	phost1x = vcm30_t124_host1x_init();
	if (!phost1x) {
		pr_err("host1x devices registration failed\n");
		return -EINVAL;
	}

#ifndef CONFIG_TEGRA_HDMI_PRIMARY
	res = platform_get_resource_byname(&vcm30_t124_disp1_device,
					 IORESOURCE_MEM, "fbmem");
#else
	res = platform_get_resource_byname(&vcm30_t124_disp2_device,
					 IORESOURCE_MEM, "fbmem");
#endif
	res->start = tegra_fb_start;
	res->end = tegra_fb_start + tegra_fb_size - 1;

	/* clear FB for both DC and copy the bootloader FB */
	__tegra_clear_framebuffer(&vcm30_t124_nvmap_device,
		tegra_fb_start, tegra_fb_size);
	if (tegra_bootloader_fb_size)
		__tegra_move_framebuffer(&vcm30_t124_nvmap_device,
			tegra_fb_start, tegra_bootloader_fb_start,
			min(tegra_fb_size, tegra_bootloader_fb_size));
	if (tegra_fb2_size) {
		__tegra_clear_framebuffer(&vcm30_t124_nvmap_device,
			tegra_fb2_start, tegra_fb2_size);
		if (tegra_bootloader_fb2_size)
			__tegra_move_framebuffer(&vcm30_t124_nvmap_device,
				tegra_fb2_start, tegra_bootloader_fb2_start,
				min(tegra_fb2_size,
					 tegra_bootloader_fb2_size));
	}

#ifndef CONFIG_TEGRA_HDMI_PRIMARY
	vcm30_t124_disp1_device.dev.parent = &phost1x->dev;
	err = platform_device_register(&vcm30_t124_disp1_device);
	if (err) {
		pr_err("disp1 device registration failed\n");
		return err;
	}
#endif

	err = tegra_init_hdmi(&vcm30_t124_disp2_device, phost1x);
	if (err)
		return err;

#ifdef CONFIG_TEGRA_NVAVP
	nvavp_device.dev.parent = &phost1x->dev;
	err = platform_device_register(&nvavp_device);
	if (err) {
		pr_err("nvavp device registration failed\n");
		return err;
	}
#endif
	return err;
}
