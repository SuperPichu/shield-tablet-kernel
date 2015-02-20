/*
 * arch/arm/mach-tegra/panel-a-1200-1920-8-0.c
 *
 * Copyright (c) 2013-2014, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <mach/dc.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/tegra_pwm_bl.h>
#include <linux/regulator/consumer.h>
#include <linux/pwm_backlight.h>
#include <linux/max8831_backlight.h>
#include <linux/leds.h>
#include <linux/ioport.h>
#include <linux/mfd/palmas.h>
#include <generated/mach-types.h>
#include <video/mipi_display.h>
#include "board.h"
#include "board-panel.h"
#include "devices.h"
#include "gpio-names.h"
#include "tegra11_host1x_devices.h"

#define TEGRA_DSI_GANGED_MODE	0

#define DSI_PANEL_RESET		1

#define DC_CTRL_MODE	(TEGRA_DC_OUT_CONTINUOUS_MODE  |\
			TEGRA_DC_OUT_INITIALIZED_MODE)

#define PRISM_THRESHOLD		50
#define HYST_VAL		25

static bool reg_requested;
static bool gpio_requested;
static struct platform_device *disp_device;
static struct regulator *avdd_lcd_3v3;
static struct regulator *vdd_lcd_bl_en;
static struct regulator *dvdd_lcd_1v8;
static struct device *dc_dev;

static struct tegra_dc_sd_settings dsi_a_1200_1920_8_0_sd_settings = {
	.enable = 0, /* disabled by default. */
	.enable_int = 0, /* disabled by default. */
	.use_auto_pwm = false,
	.hw_update_delay = 0,
	.bin_width = -1,
	.aggressiveness = 3,
	.use_vid_luma = false,
	.phase_in_adjustments = 0,
	.k_limit_enable = true,
	.k_limit = 220,
	.sd_window_enable = false,
	.soft_clipping_enable = true,
	/* Low soft clipping threshold to compensate for aggressive k_limit */
	.soft_clipping_threshold = 128,
	.smooth_k_enable = true,
	.smooth_k_incr = 4,
	/* Default video coefficients */
	.coeff = {5, 9, 2},
	.fc = {0, 0},
	/* Immediate backlight changes */
	.blp = {1024, 255},
	/* Gammas: R: 2.2 G: 2.2 B: 2.2 */
	/* Default BL TF */
	.bltf = {
			{
				{57, 65, 73, 82},
				{92, 103, 114, 125},
				{138, 150, 164, 178},
				{193, 208, 224, 241},
			},
		},
	/* Default LUT */
	.lut = {
			{
				{255, 255, 255},
				{199, 199, 199},
				{153, 153, 153},
				{116, 116, 116},
				{85, 85, 85},
				{59, 59, 59},
				{36, 36, 36},
				{17, 17, 17},
				{0, 0, 0},
			},
		},
	.sd_brightness = &sd_brightness,
	.use_vpulse2 = true,
};

static tegra_dc_bl_output dsi_a_1200_1920_8_0_bl_output_measured = {
	0, 1, 2, 4, 5, 6, 7, 8,
	10, 11, 12, 13, 14, 14, 15, 16,
	17, 18, 18, 19, 20, 21, 22, 23,
	24, 25, 26, 27, 28, 29, 30, 31,
	32, 33, 34, 35, 36, 37, 38, 39,
	40, 41, 42, 43, 44, 45, 46, 47,
	48, 49, 50, 51, 52, 53, 54, 55,
	55, 56, 57, 58, 59, 60, 61, 62,
	63, 64, 65, 66, 67, 68, 69, 70,
	71, 72, 73, 74, 75, 76, 77, 78,
	79, 80, 81, 82, 83, 84, 85, 86,
	87, 88, 89, 90, 91, 92, 93, 94,
	95, 96, 97, 98, 99, 100, 101, 102,
	103, 104, 105, 106, 107, 108, 109, 110,
	111, 112, 113, 114, 115, 116, 117, 118,
	119, 120, 121, 122, 123, 124, 125, 126,
	127, 128, 129, 130, 131, 132, 133, 134,
	135, 136, 137, 138, 139, 140, 141, 142,
	143, 144, 145, 146, 147, 148, 149, 150,
	151, 152, 153, 154, 155, 156, 157, 158,
	159, 160, 161, 162, 163, 164, 165, 166,
	167, 168, 169, 170, 171, 172, 173, 174,
	175, 176, 177, 178, 179, 180, 181, 182,
	183, 184, 185, 186, 187, 188, 189, 190,
	191, 192, 193, 194, 195, 196, 197, 198,
	199, 200, 201, 202, 203, 204, 205, 206,
	207, 208, 209, 210, 211, 212, 213, 214,
	215, 216, 217, 218, 219, 220, 221, 222,
	223, 224, 225, 226, 227, 228, 229, 230,
	231, 232, 233, 234, 235, 236, 237, 238,
	239, 240, 241, 242, 243, 244, 245, 246,
	247, 248, 249, 250, 251, 253, 254, 255,
};

static u8 dsi_a_1200_1920_8_0_bl_nonlinear[256] = {
	0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
	7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12,
	12, 13, 14, 14, 15, 15, 16, 16, 17, 17,
	18, 18, 19, 20, 20, 21, 21, 22, 22, 23,
	23, 24, 24, 25, 25, 26, 27, 27, 28, 28,
	29, 29, 30, 30, 31, 31, 32, 33, 33, 34,
	34, 35, 35, 36, 36, 37, 37, 38, 38, 39,
	40, 40, 41, 41, 42, 42, 43, 43, 44, 44,
	45, 46, 46, 47, 47, 48, 48, 49, 49, 50,
	50, 51, 52, 52, 53, 53, 54, 54, 55, 55,
	56, 56, 57, 57, 58, 59, 59, 60, 60, 61,
	61, 62, 62, 63, 63, 64, 65, 65, 66, 66,
	67, 67, 68, 68, 69, 69, 70, 70, 71, 72,
	72, 73, 73, 74, 74, 75, 75, 76, 76, 77,
	78, 78, 79, 79, 80, 80, 81, 81, 82, 82,
	83, 83, 84, 85, 85, 86, 86, 87, 87, 88,
	88, 89, 89, 90, 92, 94, 96, 97, 99, 101,
	103, 105, 107, 109, 110, 112, 114, 116, 118,
	120, 122, 123, 125, 127, 129, 131, 133, 134,
	136, 138, 140, 142, 144, 146, 147, 149, 151,
	153, 155, 157, 159, 160, 162, 164, 166, 168,
	170, 172, 173, 175, 177, 179, 181, 183, 185,
	186, 188, 190, 192, 194, 196, 198, 199, 201,
	203, 205, 207, 209, 211, 212, 214, 216, 218,
	220, 222, 223, 225, 227, 229, 231, 233, 235,
	236, 238, 240, 242, 244, 246, 248, 249, 251,
	253, 255
};

static struct tegra_dsi_cmd dsi_a_1200_1920_8_0_init_cmd[] = {
    /* no init command required */
};


static struct tegra_dsi_out dsi_a_1200_1920_8_0_pdata = {
	.controller_vs = DSI_VS_1,
	.n_data_lanes = 4,
	.video_burst_mode = TEGRA_DSI_VIDEO_NONE_BURST_MODE,

	.pixel_format = TEGRA_DSI_PIXEL_FORMAT_24BIT_P,
	.refresh_rate = 60,
	.virtual_channel = TEGRA_DSI_VIRTUAL_CHANNEL_0,

	.panel_reset = DSI_PANEL_RESET,
	.power_saving_suspend = true,
	.video_data_type = TEGRA_DSI_VIDEO_TYPE_VIDEO_MODE,
	.video_clock_mode = TEGRA_DSI_VIDEO_CLOCK_TX_ONLY,
	.dsi_init_cmd = dsi_a_1200_1920_8_0_init_cmd,
	.n_init_cmd = ARRAY_SIZE(dsi_a_1200_1920_8_0_init_cmd),
	.boardinfo = {BOARD_P1761, 0, 0, 1},
	.ulpm_not_supported = true,
};

static int dsi_a_1200_1920_8_0_regulator_get(struct device *dev)
{
	int err = 0;

	if (reg_requested)
		return 0;

	avdd_lcd_3v3 = regulator_get(dev, "avdd_lcd");
	if (IS_ERR_OR_NULL(avdd_lcd_3v3)) {
		pr_err("avdd_lcd regulator get failed\n");
		err = PTR_ERR(avdd_lcd_3v3);
		avdd_lcd_3v3 = NULL;
		goto fail;
	}

	dvdd_lcd_1v8 = regulator_get(dev, "dvdd_lcd");
	if (IS_ERR_OR_NULL(dvdd_lcd_1v8)) {
		pr_err("dvdd_lcd_1v8 regulator get failed\n");
		err = PTR_ERR(dvdd_lcd_1v8);
		dvdd_lcd_1v8 = NULL;
		goto fail;
	}

	vdd_lcd_bl_en = regulator_get(dev, "vdd_lcd_bl_en");
	if (IS_ERR_OR_NULL(vdd_lcd_bl_en)) {
		pr_err("vdd_lcd_bl_en regulator get failed\n");
		err = PTR_ERR(vdd_lcd_bl_en);
		vdd_lcd_bl_en = NULL;
		goto fail;
	}

	reg_requested = true;
	return 0;
fail:
	return err;
}

static int dsi_a_1200_1920_8_0_gpio_get(void)
{
	int err = 0;

	if (gpio_requested)
		return 0;

	err = gpio_request(dsi_a_1200_1920_8_0_pdata.dsi_panel_rst_gpio,
		"panel rst");
	if (err < 0) {
		pr_err("panel reset gpio request failed\n");
		goto fail;
	}

	/* free pwm GPIO */
	err = gpio_request(dsi_a_1200_1920_8_0_pdata.dsi_panel_bl_pwm_gpio,
		"panel pwm");
	if (err < 0) {
		pr_err("panel pwm gpio request failed\n");
		goto fail;
	}

	gpio_free(dsi_a_1200_1920_8_0_pdata.dsi_panel_bl_pwm_gpio);
	gpio_requested = true;
	return 0;
fail:
	return err;
}

static int dsi_a_1200_1920_8_0_enable(struct device *dev)
{
	int err = 0;
	struct tegra_dc_out *disp_out =
			((struct tegra_dc_platform_data *)
			(disp_device->dev.platform_data))->default_out;

	err = dsi_a_1200_1920_8_0_regulator_get(dev);
	if (err < 0) {
		pr_err("dsi regulator get failed\n");
		goto fail;
	}

	err = dsi_a_1200_1920_8_0_gpio_get();
	if (err < 0) {
		pr_err("dsi gpio request failed\n");
		goto fail;
	}

	if (avdd_lcd_3v3) {
		err = regulator_enable(avdd_lcd_3v3);
		if (err < 0) {
			pr_err("avdd_lcd regulator enable failed\n");
			goto fail;
		}
	}

	if (dvdd_lcd_1v8) {
		err = regulator_enable(dvdd_lcd_1v8);
		if (err < 0) {
			pr_err("dvdd_lcd regulator enable failed\n");
			goto fail;
		}
	}

	if (vdd_lcd_bl_en) {
		err = regulator_enable(vdd_lcd_bl_en);
		if (err < 0) {
			pr_err("vdd_lcd_bl_en regulator enable failed\n");
			goto fail;
		}
	}

	msleep(100);
#if DSI_PANEL_RESET
	if (!(disp_out->flags & TEGRA_DC_OUT_INITIALIZED_MODE)) {
		gpio_direction_output(
			dsi_a_1200_1920_8_0_pdata.dsi_panel_rst_gpio, 1);
		usleep_range(1000, 5000);
		gpio_set_value(
			dsi_a_1200_1920_8_0_pdata.dsi_panel_rst_gpio, 0);
		msleep(150);
		gpio_set_value(
			dsi_a_1200_1920_8_0_pdata.dsi_panel_rst_gpio, 1);
		msleep(20);
	}
#endif
	dc_dev = dev;
	return 0;
fail:
	return err;
}

static int dsi_a_1200_1920_8_0_disable(void)
{
	if (gpio_is_valid(dsi_a_1200_1920_8_0_pdata.dsi_panel_rst_gpio)) {
		/* Wait for 50ms before triggering panel reset */
		msleep(50);
		gpio_set_value(dsi_a_1200_1920_8_0_pdata.dsi_panel_rst_gpio, 0);
	}

	msleep(120);

	if (vdd_lcd_bl_en)
		regulator_disable(vdd_lcd_bl_en);

	if (avdd_lcd_3v3)
		regulator_disable(avdd_lcd_3v3);

	if (dvdd_lcd_1v8)
		regulator_disable(dvdd_lcd_1v8);

	dc_dev = NULL;
	return 0;
}

static int dsi_a_1200_1920_8_0_postsuspend(void)
{
	return 0;
}

static struct tegra_dc_mode dsi_a_1200_1920_8_0_modes[] = {
	{
		.pclk = 155774400,
		.h_ref_to_sync = 4,
		.v_ref_to_sync = 1,
		.h_sync_width = 10,
		.v_sync_width = 2,
		.h_back_porch = 54,
		.v_back_porch = 30,
		.h_active = 1200,
		.v_active = 1920,
		.h_front_porch = 64,
		.v_front_porch = 3,
	},
};

#ifdef CONFIG_TEGRA_DC_CMU
static struct tegra_dc_cmu dsi_a_1200_1920_8_0_cmu = {
	/* lut1 maps sRGB to linear space. */
	{
		0,  1,  2,  4,  5,  6,  7,  9,
		10,  11,  12,  14,  15,  16,  18,  19,
		21,  23,  25,  27,  29,  31,  33,  35,
		37,  40,  42,  45,  47,  50,  53,  56,
		59,  62,  65,  69,  72,  75,  79,  83,
		87,  90,  94,  99,  103,  107,  111,  116,
		121,  125,  130,  135,  140,  145,  151,  156,
		161,  167,  173,  178,  184,  190,  197,  203,
		209,  216,  222,  229,  236,  243,  250,  257,
		264,  272,  279,  287,  295,  303,  311,  319,
		327,  336,  344,  353,  362,  371,  380,  389,
		398,  408,  417,  427,  437,  447,  457,  467,
		477,  488,  498,  509,  520,  531,  542,  553,
		565,  576,  588,  600,  612,  624,  636,  649,
		661,  674,  687,  699,  713,  726,  739,  753,
		766,  780,  794,  808,  822,  837,  851,  866,
		881,  896,  911,  926,  941,  957,  973,  989,
		1005,  1021,  1037,  1053,  1070,  1087,  1104,  1121,
		1138,  1155,  1173,  1190,  1208,  1226,  1244,  1263,
		1281,  1300,  1318,  1337,  1356,  1376,  1395,  1415,
		1434,  1454,  1474,  1494,  1515,  1535,  1556,  1577,
		1598,  1619,  1640,  1662,  1683,  1705,  1727,  1749,
		1771,  1794,  1816,  1839,  1862,  1885,  1909,  1932,
		1956,  1979,  2003,  2027,  2052,  2076,  2101,  2126,
		2151,  2176,  2201,  2227,  2252,  2278,  2304,  2330,
		2357,  2383,  2410,  2437,  2464,  2491,  2518,  2546,
		2573,  2601,  2629,  2658,  2686,  2715,  2744,  2773,
		2802,  2831,  2860,  2890,  2920,  2950,  2980,  3011,
		3041,  3072,  3103,  3134,  3165,  3197,  3228,  3260,
		3292,  3325,  3357,  3390,  3422,  3455,  3488,  3522,
		3555,  3589,  3623,  3657,  3691,  3725,  3760,  3795,
		3830,  3865,  3900,  3936,  3972,  4008,  4044,  4080,
	},
	/* csc */
	{
		0x0FD, 0x3F1, 0x010,
		0x3F5, 0x104, 0x3E4,
		0x3FC, 0x004, 0x0E4,
	},
	/* lut2 maps linear space to sRGB */
	{
		0,  2,  3,  5,  6,  8,  9,  11,
		12,  14,  15,  16,  17,  17,  17,  18,
		18,  19,  19,  20,  20,  20,  21,  21,
		22,  22,  22,  23,  23,  24,  24,  25,
		25,  25,  26,  26,  27,  27,  28,  28,
		28,  29,  29,  30,  30,  30,  31,  31,
		32,  32,  32,  33,  33,  33,  33,  33,
		34,  34,  34,  34,  35,  35,  35,  35,
		35,  36,  36,  36,  36,  37,  37,  37,
		37,  37,  38,  38,  38,  38,  39,  39,
		39,  39,  39,  40,  40,  40,  40,  41,
		41,  41,  41,  41,  42,  42,  42,  42,
		43,  43,  43,  43,  44,  44,  44,  44,
		44,  45,  45,  45,  45,  46,  46,  46,
		46,  46,  47,  47,  47,  47,  48,  48,
		48,  48,  48,  48,  49,  49,  49,  49,
		49,  49,  50,  50,  50,  50,  50,  50,
		50,  51,  51,  51,  51,  51,  51,  51,
		52,  52,  52,  52,  52,  52,  53,  53,
		53,  53,  53,  53,  53,  54,  54,  54,
		54,  54,  54,  55,  55,  55,  55,  55,
		55,  55,  56,  56,  56,  56,  56,  56,
		57,  57,  57,  57,  57,  57,  57,  58,
		58,  58,  58,  58,  58,  59,  59,  59,
		59,  59,  59,  59,  60,  60,  60,  60,
		60,  60,  60,  61,  61,  61,  61,  61,
		61,  62,  62,  62,  62,  62,  62,  62,
		63,  63,  63,  63,  63,  63,  64,  64,
		64,  64,  64,  64,  64,  64,  65,  65,
		65,  65,  65,  65,  65,  65,  66,  66,
		66,  66,  66,  66,  66,  66,  67,  67,
		67,  67,  67,  67,  67,  67,  67,  68,
		68,  68,  68,  68,  68,  68,  68,  69,
		69,  69,  69,  69,  69,  69,  69,  70,
		70,  70,  70,  70,  70,  70,  70,  70,
		71,  71,  71,  71,  71,  71,  71,  71,
		72,  72,  72,  72,  72,  72,  72,  72,
		73,  73,  73,  73,  73,  73,  73,  73,
		73,  74,  74,  74,  74,  74,  74,  74,
		74,  75,  75,  75,  75,  75,  75,  75,
		75,  76,  76,  76,  76,  76,  76,  76,
		76,  76,  77,  77,  77,  77,  77,  77,
		77,  77,  78,  78,  78,  78,  78,  78,
		78,  78,  79,  79,  79,  79,  79,  79,
		79,  79,  80,  80,  80,  80,  80,  80,
		80,  80,  80,  80,  81,  81,  81,  81,
		81,  81,  81,  81,  81,  81,  81,  81,
		82,  82,  82,  82,  82,  82,  82,  82,
		82,  82,  82,  83,  83,  83,  83,  83,
		83,  83,  83,  83,  83,  83,  84,  84,
		84,  84,  84,  84,  84,  84,  84,  84,
		84,  84,  85,  85,  85,  85,  85,  85,
		85,  85,  85,  85,  85,  86,  86,  86,
		86,  86,  86,  86,  86,  86,  86,  86,
		87,  87,  87,  87,  87,  87,  87,  87,
		87,  87,  87,  87,  88,  88,  88,  88,
		88,  88,  88,  88,  88,  88,  88,  89,
		89,  89,  89,  89,  89,  89,  89,  89,
		89,  89,  89,  90,  90,  90,  90,  90,
		90,  90,  90,  90,  90,  90,  91,  91,
		91,  91,  91,  91,  91,  91,  91,  91,
		91,  92,  92,  92,  92,  92,  92,  92,
		92,  92,  92,  92,  92,  93,  93,  93,
		93,  93,  93,  93,  93,  93,  93,  93,
		94,  95,  95,  96,  97,  97,  98,  99,
		99,  100,  100,  101,  102,  102,  103,  104,
		104,  105,  105,  106,  107,  107,  108,  109,
		109,  110,  111,  111,  112,  112,  113,  113,
		114,  115,  115,  116,  116,  117,  117,  118,
		118,  119,  119,  120,  120,  121,  122,  122,
		123,  123,  124,  124,  125,  125,  126,  126,
		127,  127,  128,  128,  129,  129,  130,  130,
		131,  131,  132,  132,  132,  133,  133,  134,
		134,  135,  135,  136,  136,  136,  137,  137,
		138,  138,  139,  139,  140,  140,  140,  141,
		141,  142,  142,  143,  143,  144,  144,  144,
		145,  145,  146,  146,  147,  147,  148,  148,
		148,  149,  149,  150,  150,  151,  151,  151,
		152,  152,  153,  153,  154,  154,  155,  155,
		155,  156,  156,  157,  157,  158,  158,  158,
		159,  159,  160,  160,  161,  161,  161,  162,
		162,  162,  163,  163,  163,  164,  164,  164,
		165,  165,  165,  166,  166,  166,  167,  167,
		168,  168,  168,  169,  169,  169,  170,  170,
		170,  171,  171,  171,  172,  172,  172,  173,
		173,  173,  174,  174,  175,  175,  175,  176,
		176,  176,  177,  177,  177,  178,  178,  178,
		179,  179,  180,  180,  180,  181,  181,  181,
		182,  182,  182,  183,  183,  184,  184,  184,
		185,  185,  185,  186,  186,  186,  187,  187,
		188,  188,  188,  189,  189,  189,  190,  190,
		191,  191,  191,  192,  192,  192,  193,  193,
		193,  194,  194,  194,  194,  195,  195,  195,
		196,  196,  196,  197,  197,  197,  198,  198,
		198,  199,  199,  199,  199,  200,  200,  200,
		201,  201,  201,  202,  202,  202,  203,  203,
		203,  204,  204,  204,  204,  205,  205,  205,
		206,  206,  206,  207,  207,  207,  208,  208,
		208,  208,  209,  209,  209,  210,  210,  210,
		210,  211,  211,  211,  212,  212,  212,  212,
		213,  213,  213,  213,  214,  214,  214,  215,
		215,  215,  215,  216,  216,  216,  216,  217,
		217,  217,  218,  218,  218,  218,  219,  219,
		219,  220,  220,  220,  220,  221,  221,  221,
		221,  222,  222,  222,  223,  223,  223,  223,
		224,  224,  224,  224,  225,  225,  225,  226,
		226,  226,  226,  227,  227,  227,  227,  228,
		228,  228,  228,  229,  229,  229,  229,  230,
		230,  230,  231,  231,  231,  231,  232,  232,
		232,  232,  233,  233,  233,  233,  234,  234,
		234,  234,  235,  235,  235,  236,  236,  236,
		236,  237,  237,  237,  237,  238,  238,  238,
		238,  239,  239,  239,  239,  240,  240,  240,
		241,  241,  241,  241,  242,  242,  242,  242,
		243,  243,  243,  243,  244,  244,  244,  245,
		245,  245,  245,  246,  246,  246,  246,  247,
		247,  247,  248,  248,  248,  248,  249,  249,
		249,  249,  250,  250,  250,  251,  251,  251,
		251,  252,  252,  252,  252,  253,  253,  253,
		253,  254,  254,  254,  255,  255,  255,  255,
	},
};
#endif

static int dsi_a_1200_1920_8_0_bl_notify(struct device *unused, int brightness)
{
	int cur_sd_brightness;

	/* apply the non-linear curve adjustment */
	brightness = dsi_a_1200_1920_8_0_bl_nonlinear[brightness];
	if (dc_dev) {
		if (brightness <= PRISM_THRESHOLD)
			nvsd_enbl_dsbl_prism(dc_dev, false);
		else if (brightness > PRISM_THRESHOLD + HYST_VAL)
			nvsd_enbl_dsbl_prism(dc_dev, true);
	}
	cur_sd_brightness = atomic_read(&sd_brightness);
	/* SD brightness is a percentage */
	brightness = (brightness * cur_sd_brightness) / 255;

	/* Apply any backlight response curve */
	if (brightness > 255)
		pr_info("Error: Brightness > 255!\n");
	else
		brightness = dsi_a_1200_1920_8_0_bl_output_measured[brightness];

	return brightness;
}

static int dsi_a_1200_1920_8_0_check_fb(struct device *dev,
	struct fb_info *info)
{
	return info->device == &disp_device->dev;
}

static struct platform_pwm_backlight_data dsi_a_1200_1920_8_0_bl_data = {
	.pwm_id		= 1,
	.max_brightness	= 255,
	.dft_brightness	= 191,
	.pwm_period_ns	= 40161,
	.pwm_gpio	= TEGRA_GPIO_INVALID,
	.notify		= dsi_a_1200_1920_8_0_bl_notify,
	/* Only toggle backlight on fb blank notifications for disp1 */
	.check_fb	= dsi_a_1200_1920_8_0_check_fb,
};

static struct platform_device __maybe_unused
		dsi_a_1200_1920_8_0_bl_device = {
	.name	= "pwm-backlight",
	.id	= -1,
	.dev	= {
		.platform_data = &dsi_a_1200_1920_8_0_bl_data,
	},
};

static struct platform_device __maybe_unused
			*dsi_a_1200_1920_8_0_bl_devices[] __initdata = {
	&dsi_a_1200_1920_8_0_bl_device,
};

static int  __init dsi_a_1200_1920_8_0_register_bl_dev(void)
{
	int err = 0;

#ifdef CONFIG_ANDROID
	if (get_androidboot_mode_charger())
		dsi_a_1200_1920_8_0_bl_data.dft_brightness = 112;
#endif

	if (tegra_get_touch_vendor_id() == MAXIM_TOUCH) {
		struct platform_pwm_backlight_data *pfm_dat;
		pfm_dat = dsi_a_1200_1920_8_0_bl_devices[0]->dev.platform_data;
		/* override backlight pwm frequency to 1KHz */
		pfm_dat->pwm_period_ns = 1000000;
	}
	err = platform_add_devices(dsi_a_1200_1920_8_0_bl_devices,
				ARRAY_SIZE(dsi_a_1200_1920_8_0_bl_devices));
	if (err) {
		pr_err("disp1 bl device registration failed");
		return err;
	}
	return err;
}

static void dsi_a_1200_1920_8_0_set_disp_device(
	struct platform_device *display_device)
{
	disp_device = display_device;
}

static void dsi_a_1200_1920_8_0_dc_out_init(struct tegra_dc_out *dc)
{
	dc->dsi = &dsi_a_1200_1920_8_0_pdata;
	dc->parent_clk = "pll_d_out0";
	dc->modes = dsi_a_1200_1920_8_0_modes;
	dc->n_modes = ARRAY_SIZE(dsi_a_1200_1920_8_0_modes);
	dc->enable = dsi_a_1200_1920_8_0_enable;
	dc->disable = dsi_a_1200_1920_8_0_disable;
	dc->postsuspend	= dsi_a_1200_1920_8_0_postsuspend,
	dc->width = 107;
	dc->height = 172;
	dc->flags = DC_CTRL_MODE;
}

static void dsi_a_1200_1920_8_0_fb_data_init(struct tegra_fb_data *fb)
{
	fb->xres = dsi_a_1200_1920_8_0_modes[0].h_active;
	fb->yres = dsi_a_1200_1920_8_0_modes[0].v_active;
}

static void
dsi_a_1200_1920_8_0_sd_settings_init(struct tegra_dc_sd_settings *settings)
{
	*settings = dsi_a_1200_1920_8_0_sd_settings;
	settings->bl_device_name = "pwm-backlight";
}

static void dsi_a_1200_1920_8_0_cmu_init(struct tegra_dc_platform_data *pdata)
{
	pdata->cmu = &dsi_a_1200_1920_8_0_cmu;
}

struct tegra_panel __initdata dsi_a_1200_1920_8_0 = {
	.init_sd_settings = dsi_a_1200_1920_8_0_sd_settings_init,
	.init_dc_out = dsi_a_1200_1920_8_0_dc_out_init,
	.init_fb_data = dsi_a_1200_1920_8_0_fb_data_init,
	.register_bl_dev = dsi_a_1200_1920_8_0_register_bl_dev,
	.init_cmu_data = dsi_a_1200_1920_8_0_cmu_init,
	.set_disp_device = dsi_a_1200_1920_8_0_set_disp_device,
};
EXPORT_SYMBOL(dsi_a_1200_1920_8_0);

