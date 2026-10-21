// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) <2024> Transsion Inc.
 */

#include <linux/backlight.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_modes.h>
#include <linux/delay.h>
#include <drm/drm_connector.h>
#include <drm/drm_device.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mediatek_v2/mtk_panel_ext.h"
#include "../mediatek/mediatek_v2/mtk_drm_graphics_base.h"
#endif

#define LCM_MIPISWITCH_TE_DELAY 0
#define REGFLAG_DELAY           0xFFFC
#define REGFLAG_UDELAY          0xFFFB
#define REGFLAG_END_OF_TABLE    0xFFFD
#define FRAME_WIDTH				(1260)
#define FRAME_HEIGHT			(2800)
#define PHYSICAL_WIDTH          (69552)
#define PHYSICAL_HEIGHT         (154560)
#define PLL_CLOCK_144			(710)
#define PLL_CLOCK_120			(710)
#define PLL_CLOCK_60			(710)
#define REAL_MODE_NUM           (3)
#define HFP                     (40)
#define HSA                     (20)
#define HBP                     (40)
#define VFP                     (10)
#define VSA                     (2)
#define VBP                     (16)

#define FHD_FRAME_WIDTH    (1080)
#define FHD_HFP            (40)
#define FHD_HSA            (20)
#define FHD_HBP            (40)
#define FHD_HTOTAL         (FHD_FRAME_WIDTH + FHD_HFP + FHD_HSA + FHD_HBP)
#define FHD_FRAME_HEIGHT   (2400)
#define FHD_VFP            (10)
#define FHD_VSA            (2)
#define FHD_VBP            (16)
#define FHD_VTOTAL         (FHD_FRAME_HEIGHT + FHD_VFP + FHD_VSA + FHD_VBP)
#define FHD_FRAME_TOTAL    (FHD_HTOTAL * FHD_VTOTAL)
#define FHD_PLL_CLOCK      (390)
#define FHD_VREFRESH_DEF   (144)
#define FHD_VREFRESH_60    (60)
#define FHD_VREFRESH_120   (120)
#define FHD_VREFRESH_30    (30)
#define FHD_VREFRESH_24    (24)
#define FHD_VREFRESH_10    (10)
#define FHD_CLK_DEF_X10    ((FHD_FRAME_TOTAL * FHD_VREFRESH_DEF) / 100)
#define FHD_CLK_60_X10     ((FHD_FRAME_TOTAL * FHD_VREFRESH_60) / 100)
#define FHD_CLK_120_X10     ((FHD_FRAME_TOTAL * FHD_VREFRESH_120) / 100)
#define FHD_CLK_DEF		(((FHD_CLK_DEF_X10 % 10) != 0) ?             \
			(FHD_CLK_DEF_X10 / 10 + 1) : (FHD_CLK_DEF_X10 / 10))
#define FHD_CLK_120		(((FHD_CLK_120_X10 % 10) != 0) ?              \
			(FHD_CLK_120_X10 / 10 + 1) : (FHD_CLK_120_X10 / 10))
#define FHD_CLK_60		(((FHD_CLK_60_X10 % 10) != 0) ?              \
			(FHD_CLK_60_X10 / 10 + 1) : (FHD_CLK_60_X10 / 10))

#define MODE_SWITCH_CMDQ_ENABLE 0

struct mtk_mode_switch_cmd cmd_table_144fps[] = {
	{2, {0x2F, 0x05}}
};

struct mtk_mode_switch_cmd cmd_table_120fps[] = {
	{2, {0x2F, 0x00}}
};

struct mtk_mode_switch_cmd cmd_table_60fps[] = {
	{2, {0x2F, 0x06}},
};

static enum RES_SWITCH_TYPE res_switch_type = RES_SWITCH_NO_USE;
static int current_fps = 144;

#if IS_ENABLED(CONFIG_TRANSSION_DOZE_BRIGHTNESS_SUPPORT)
tran_lcm_doze_backlight g_lcm_doze_backlight = {
	.doze_backlight_num = 0,
	.doze_backlight_level1 = 0,
	.doze_backlight_level2 = 0,
	.doze_backlight_level3 = 0,
};
#endif

struct tran_panel_driver_params panel_driver_status = {0};
static int g_aod_enable;
static int g_hbm_enable;
static unsigned int mapped_level = 0, input_brightness = 0, last_mapped_level;
static char bl_dim[] = {0x53, 0x28};
static unsigned int g_dim_enable;
static unsigned int g_need_dim_enable;
static unsigned int last_aod_level;
#if LCM_MIPISWITCH_TE_DELAY
static int g_te_need_delay;
#endif

unsigned int rm692j0_fhdp_dsi_cmd_144hz_dphy_buf_thresh[14] = {
	896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000, 8064};
unsigned int rm692j0_fhdp_dsi_cmd_144hz_dphy_range_min_qp[15] = {
	0, 4, 5, 5, 7, 7, 7, 7, 7, 7, 9, 9, 9, 13, 16};
unsigned int rm692j0_fhdp_dsi_cmd_144hz_dphy_range_max_qp[15] = {
	8, 8, 9, 10, 11, 11, 11, 12, 13, 14, 14, 15, 15, 16, 17};
int rm692j0_fhdp_dsi_cmd_144hz_dphy_range_bpg_ofs[15] = {
	2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -12, -12, -12, -12};

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *vddi_gpio;
	struct regulator *vci_regulator;
	struct regulator *dvdd_regulator;

	int error;
	bool prepared;
	bool enabled;
	bool hbm_en;
	bool hbm_wait;
#if LCM_MIPISWITCH_TE_DELAY
	unsigned int lcm_src_datarate;
	unsigned int lcm_dst_datarate;
#endif
};

#ifdef CONFIG_TRAN_GET_VREFRESH_SUPPORT
int g_tran_get_vrefresh;
EXPORT_SYMBOL_GPL(g_tran_get_vrefresh);

int g_tran_get_vrefresh_main;
EXPORT_SYMBOL_GPL(g_tran_get_vrefresh_main);
#endif

#define lcm_dcs_write_seq(ctx, seq...) \
({\
	const u8 d[] = { seq };\
	BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64, "DCS sequence too big for stack");\
	lcm_dcs_write(ctx, d, ARRAY_SIZE(d));\
})

#define lcm_dcs_write_seq_static(ctx, seq...) \
({\
	static const u8 d[] = { seq };\
	lcm_dcs_write(ctx, d, ARRAY_SIZE(d));\
})

static inline struct lcm *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct lcm, panel);
}

static void lcm_dcs_write(struct lcm *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	char *addr;

	if (ctx->error < 0)
		return;

	addr = (char *)data;
	if ((int)*addr < 0xB0)
		ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	else
		ret = mipi_dsi_generic_write(dsi, data, len);
	if (ret < 0) {
		dev_err(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
}

#ifdef PANEL_SUPPORT_READBACK
static int lcm_dcs_read(struct lcm *ctx, u8 cmd, void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	if (ctx->error < 0)
		return 0;

	ret = mipi_dsi_dcs_read(dsi, cmd, data, len);
	if (ret < 0) {
		dev_err(ctx->dev, "error %zd reading dcs seq:(%#x)\n", ret, cmd);
		ctx->error = ret;
	}

	return ret;
}

static void lcm_panel_get_data(struct lcm *ctx)
{
	u8 buffer[3] = {0};
	static int ret;

	if (ret == 0) {
		ret = lcm_dcs_read(ctx,  0x0A, buffer, 1);
		dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
			 ret, buffer[0] | (buffer[1] << 8));
	}
}

static int lcm_panel_get_ab_data(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	u8 buffer[3] = {0};
	int ret;

	if (!ctx->enabled)
		return 0;
	ret = lcm_dcs_read(ctx,  0xAB, buffer, 1);
	dev_info(ctx->dev, "return %d data(0x%08x) to 0xAB\n",
		 ret, buffer[0] | (buffer[1] << 8));
	ret = lcm_dcs_read(ctx,  0x0A, buffer, 1);
	dev_info(ctx->dev, "return %d data(0x%08x) to 0x0A\n",
		 ret, buffer[0] | (buffer[1] << 8));

	lcm_dcs_write_seq_static(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
	ret = lcm_dcs_read(ctx,  0xC3, buffer, 1);
	dev_info(ctx->dev, "return %d data(0x%08x) to 0xC3\n",
		 ret, buffer[0] | (buffer[1] << 8));
	ret = lcm_dcs_read(ctx,  0xEA, buffer, 1);
	dev_info(ctx->dev, "return %d data(0x%08x) to 0xEA\n",
		 ret, buffer[0] | (buffer[1] << 8));

	return ret;
}
#endif

struct LCM_setting_table {
	unsigned int cmd;
	unsigned char count;
	unsigned char para_list[120];
};

static struct LCM_setting_table aod_mode_enter_setting[] = {
	{0xFE, 1, {0x00}},
	{0x39, 0, {}},
};

static struct LCM_setting_table aod_mode_exit_setting[] = {
	{0xFE, 1, {0x00}},
	{0x38, 0, {}},
	{0xFE, 1, {0x00}},
	{0x51, 2, {0x0D, 0xBB}},
};

static struct LCM_setting_table hbm_mode_enter_setting[] = {
	{0xFE, 1, {0x00}},
	{0x53, 1, {0x20}},
	{0x51, 2, {0x0D, 0xBB}},
	{0x83, 3, {0x01, 0x7F, 0x1F}},
};

static struct LCM_setting_table hbm_mode_exit_setting[] = {
	{0xFE, 1, {0x00}},
	{0x83, 1, {0x00}},
};

static unsigned int brightness_mapped_to_alpha[461][2] = {
	{0x00, 0x00},
	{0x00, 0x00},
	{0x17, 0x1E},
	{0x18, 0x12},
	{0x19, 0x05},
	{0x1A, 0x06},
	{0x1A, 0x1D},
	{0x1B, 0x13},
	{0x1B, 0x13},
	{0x1C, 0x09},
	{0x1D, 0x04},
	{0x1E, 0x12},
	{0x1F, 0x17},
	{0x20, 0x0B},
	{0x21, 0x0B},
	{0x21, 0x0B},
	{0x22, 0x02},
	{0x22, 0x18},
	{0x23, 0x0E},
	{0x24, 0x03},
	{0x24, 0x05},
	{0x24, 0x0F},
	{0x24, 0x0F},
	{0x24, 0x1F},
	{0x25, 0x08},
	{0x25, 0x0F},
	{0x25, 0x15},
	{0x25, 0x1D},
	{0x26, 0x05},
	{0x26, 0x05},
	{0x26, 0x11},
	{0x26, 0x18},
	{0x27, 0x01},
	{0x27, 0x07},
	{0x27, 0x14},
	{0x27, 0x1C},
	{0x27, 0x1C},
	{0x28, 0x06},
	{0x28, 0x15},
	{0x28, 0x1F},
	{0x29, 0x09},
	{0x29, 0x17},
	{0x2A, 0x02},
	{0x2A, 0x02},
	{0x2A, 0x0A},
	{0x2A, 0x0F},
	{0x2A, 0x18},
	{0x2B, 0x02},
	{0x2B, 0x0D},
	{0x2B, 0x1C},
	{0x2B, 0x1C},
	{0x2C, 0x0F},
	{0x2C, 0x18},
	{0x2C, 0x1F},
	{0x2D, 0x08},
	{0x2D, 0x0E},
	{0x2D, 0x19},
	{0x2D, 0x19},
	{0x2E, 0x0C},
	{0x2E, 0x0C},
	{0x2E, 0x1F},
	{0x2F, 0x08},
	{0x2F, 0x16},
	{0x30, 0x05},
	{0x30, 0x05},
	{0x30, 0x15},
	{0x31, 0x05},
	{0x31, 0x12},
	{0x31, 0x1E},
	{0x32, 0x0C},
	{0x32, 0x15},
	{0x32, 0x15},
	{0x33, 0x03},
	{0x33, 0x14},
	{0x33, 0x1D},
	{0x34, 0x08},
	{0x34, 0x12},
	{0x34, 0x1F},
	{0x34, 0x1F},
	{0x35, 0x09},
	{0x35, 0x1C},
	{0x36, 0x09},
	{0x36, 0x11},
	{0x37, 0x05},
	{0x37, 0x0D},
	{0x37, 0x0D},
	{0x37, 0x1D},
	{0x38, 0x0D},
	{0x38, 0x19},
	{0x39, 0x05},
	{0x39, 0x12},
	{0x39, 0x1A},
	{0x39, 0x1A},
	{0x3A, 0x0D},
	{0x3A, 0x1B},
	{0x3B, 0x09},
	{0x3B, 0x17},
	{0x3C, 0x07},
	{0x3C, 0x15},
	{0x3C, 0x15},
	{0x3D, 0x05},
	{0x3D, 0x13},
	{0x3E, 0x02},
	{0x3E, 0x0D},
	{0x3E, 0x1A},
	{0x3F, 0x08},
	{0x3F, 0x08},
	{0x3F, 0x14},
	{0x3F, 0x1E},
	{0x40, 0x12},
	{0x40, 0x1D},
	{0x41, 0x0D},
	{0x41, 0x17},
	{0x41, 0x17},
	{0x42, 0x0D},
	{0x42, 0x12},
	{0x43, 0x03},
	{0x43, 0x18},
	{0x44, 0x07},
	{0x44, 0x17},
	{0x45, 0x02},
	{0x45, 0x02},
	{0x45, 0x14},
	{0x45, 0x1F},
	{0x46, 0x0F},
	{0x47, 0x03},
	{0x47, 0x12},
	{0x48, 0x07},
	{0x48, 0x07},
	{0x48, 0x17},
	{0x49, 0x04},
	{0x49, 0x15},
	{0x4A, 0x01},
	{0x4A, 0x0A},
	{0x4A, 0x14},
	{0x4A, 0x14},
	{0x4A, 0x1D},
	{0x4B, 0x05},
	{0x4B, 0x0A},
	{0x4B, 0x12},
	{0x4B, 0x1A},
	{0x4B, 0x1D},
	{0x4B, 0x1D},
	{0x4C, 0x09},
	{0x4C, 0x0F},
	{0x4C, 0x15},
	{0x4C, 0x1A},
	{0x4C, 0x1F},
	{0x4D, 0x06},
	{0x4D, 0x06},
	{0x4D, 0x0C},
	{0x4D, 0x10},
	{0x4D, 0x17},
	{0x4D, 0x1F},
	{0x4E, 0x09},
	{0x4E, 0x0B},
	{0x4E, 0x0B},
	{0x4E, 0x12},
	{0x4E, 0x1B},
	{0x4F, 0x03},
	{0x4F, 0x0A},
	{0x4F, 0x12},
	{0x4F, 0x17},
	{0x4F, 0x17},
	{0x4F, 0x1F},
	{0x50, 0x05},
	{0x50, 0x0A},
	{0x50, 0x12},
	{0x50, 0x17},
	{0x50, 0x1B},
	{0x50, 0x1B},
	{0x51, 0x02},
	{0x51, 0x0C},
	{0x51, 0x0D},
	{0x51, 0x15},
	{0x52, 0x1D},
	{0x52, 0x04},
	{0x52, 0x04},
	{0x52, 0x0A},
	{0x52, 0x11},
	{0x52, 0x19},
	{0x52, 0x1F},
	{0x53, 0x04},
	{0x53, 0x0D},
	{0x53, 0x0D},
	{0x53, 0x14},
	{0x53, 0x19},
	{0x53, 0x1E},
	{0x54, 0x06},
	{0x54, 0x0A},
	{0x54, 0x19},
	{0x54, 0x19},
	{0x54, 0x1A},
	{0x55, 0x02},
	{0x55, 0x08},
	{0x55, 0x0A},
	{0x55, 0x14},
	{0x55, 0x1B},
	{0x55, 0x1B},
	{0x56, 0x01},
	{0x56, 0x09},
	{0x56, 0x0E},
	{0x56, 0x14},
	{0x56, 0x1A},
	{0x57, 0x04},
	{0x57, 0x04},
	{0x57, 0x09},
	{0x57, 0x12},
	{0x57, 0x19},
	{0x58, 0x02},
	{0x58, 0x09},
	{0x58, 0x0C},
	{0x58, 0x0C},
	{0x58, 0x0D},
	{0x58, 0x12},
	{0x58, 0x1A},
	{0x58, 0x1F},
	{0x59, 0x07},
	{0x59, 0x0D},
	{0x59, 0x0D},
	{0x59, 0x14},
	{0x59, 0x1A},
	{0x59, 0x1F},
	{0x5A, 0x07},
	{0x5A, 0x0B},
	{0x5A, 0x16},
	{0x5A, 0x16},
	{0x5A, 0x1D},
	{0x5B, 0x05},
	{0x5B, 0x0D},
	{0x5B, 0x13},
	{0x5B, 0x1A},
	{0x5C, 0x1F},
	{0x5C, 0x1F},
	{0x5C, 0x07},
	{0x5C, 0x0F},
	{0x5C, 0x13},
	{0x5C, 0x18},
	{0x5C, 0x1E},
	{0x5D, 0x03},
	{0x5D, 0x0A},
	{0x5D, 0x0A},
	{0x5D, 0x11},
	{0x5D, 0x18},
	{0x5D, 0x1E},
	{0x5E, 0x04},
	{0x5E, 0x0A},
	{0x5E, 0x0F},
	{0x5E, 0x0F},
	{0x5E, 0x16},
	{0x5E, 0x1A},
	{0x5E, 0x1F},
	{0x5F, 0x03},
	{0x5F, 0x0D},
	{0x5F, 0x13},
	{0x5F, 0x13},
	{0x5F, 0x19},
	{0x5F, 0x1F},
	{0x60, 0x04},
	{0x60, 0x0F},
	{0x60, 0x17},
	{0x60, 0x1E},
	{0x60, 0x1E},
	{0x61, 0x03},
	{0x61, 0x0D},
	{0x61, 0x14},
	{0x61, 0x1A},
	{0x61, 0x1F},
	{0x62, 0x07},
	{0x62, 0x07},
	{0x62, 0x0E},
	{0x62, 0x17},
	{0x62, 0x1E},
	{0x63, 0x05},
	{0x63, 0x0B},
	{0x63, 0x11},
	{0x63, 0x11},
	{0x63, 0x19},
	{0x63, 0x1F},
	{0x64, 0x05},
	{0x64, 0x0B},
	{0x64, 0x0F},
	{0x64, 0x15},
	{0x64, 0x15},
	{0x64, 0x1B},
	{0x65, 0x01},
	{0x65, 0x08},
	{0x65, 0x0C},
	{0x65, 0x15},
	{0x65, 0x1B},
	{0x65, 0x1B},
	{0x66, 0x02},
	{0x66, 0x08},
	{0x66, 0x0E},
	{0x66, 0x14},
	{0x66, 0x1C},
	{0x67, 0x06},
	{0x67, 0x06},
	{0x67, 0x0E},
	{0x67, 0x14},
	{0x67, 0x1A},
	{0x68, 0x01},
	{0x68, 0x0C},
	{0x68, 0x11},
	{0x68, 0x11},
	{0x68, 0x19},
	{0x68, 0x1E},
	{0x69, 0x08},
	{0x69, 0x0D},
	{0x69, 0x13},
	{0x69, 0x1A},
	{0x69, 0x1A},
	{0x69, 0x1F},
	{0x6A, 0x04},
	{0x6A, 0x0F},
	{0x6A, 0x16},
	{0x6A, 0x1D},
	{0x6B, 0x05},
	{0x6B, 0x05},
	{0x6B, 0x0D},
	{0x6B, 0x15},
	{0x6B, 0x1F},
	{0x6C, 0x05},
	{0x6C, 0x0B},
	{0x6C, 0x14},
	{0x6C, 0x14},
	{0x6C, 0x1B},
	{0x6D, 0x02},
	{0x6D, 0x0A},
	{0x6D, 0x12},
	{0x6D, 0x1B},
	{0x6E, 0x01},
	{0x6E, 0x01},
	{0x6E, 0x09},
	{0x6E, 0x0F},
	{0x6E, 0x15},
	{0x6E, 0x1F},
	{0x6F, 0x05},
	{0x6F, 0x0F},
	{0x6F, 0x0F},
	{0x6F, 0x18},
	{0x6F, 0x1E},
	{0x70, 0x08},
	{0x70, 0x13},
	{0x70, 0x18},
	{0x70, 0x1E},
	{0x70, 0x1E},
	{0x71, 0x05},
	{0x71, 0x07},
	{0x71, 0x0D},
	{0x71, 0x1A},
	{0x71, 0x1F},
	{0x72, 0x04},
	{0x72, 0x0E},
	{0x72, 0x0E},
	{0x72, 0x11},
	{0x72, 0x18},
	{0x72, 0x1F},
	{0x73, 0x03},
	{0x73, 0x08},
	{0x73, 0x0F},
	{0x73, 0x0F},
	{0x73, 0x13},
	{0x73, 0x1A},
	{0x74, 0x02},
	{0x74, 0x07},
	{0x74, 0x0F},
	{0x74, 0x13},
	{0x74, 0x13},
	{0x74, 0x1C},
	{0x75, 0x04},
	{0x75, 0x0A},
	{0x75, 0x0D},
	{0x75, 0x16},
	{0x75, 0x1A},
	{0x75, 0x1A},
	{0x75, 0x1F},
	{0x76, 0x08},
	{0x76, 0x0A},
	{0x76, 0x0D},
	{0x76, 0x15},
	{0x76, 0x19},
	{0x76, 0x19},
	{0x76, 0x1B},
	{0x77, 0x02},
	{0x77, 0x0B},
	{0x77, 0x0D},
	{0x77, 0x11},
	{0x77, 0x17},
	{0x77, 0x17},
	{0x77, 0x1A},
	{0x77, 0x1F},
	{0x78, 0x04},
	{0x78, 0x07},
	{0x78, 0x0D},
	{0x78, 0x10},
	{0x78, 0x10},
	{0x78, 0x13},
	{0x78, 0x16},
	{0x78, 0x1F},
	{0x79, 0x05},
	{0x79, 0x0A},
	{0x79, 0x0F},
	{0x79, 0x0F},
	{0x79, 0x10},
	{0x79, 0x16},
	{0x79, 0x1A},
	{0x79, 0x1C},
	{0x79, 0x1F},
	{0x7A, 0x04},
	{0x7A, 0x04},
	{0x7A, 0x08},
	{0x7A, 0x0C},
	{0x7A, 0x10},
	{0x7A, 0x13},
	{0x7A, 0x19},
	{0x7A, 0x1F},
	{0x7A, 0x1F},
	{0x7B, 0x02},
	{0x7B, 0x0D},
	{0x7B, 0x0E},
	{0x7B, 0x11},
	{0x7B, 0x17},
	{0x7B, 0x19},
	{0x7B, 0x19},
	{0x7B, 0x1C},
	{0x7C, 0x02},
	{0x7C, 0x08},
	{0x7C, 0x0B},
	{0x7C, 0x0F},
	{0x7C, 0x15},
	{0x7C, 0x15},
	{0x7C, 0x17},
	{0x7C, 0x1C},
	{0x7C, 0x1F},
	{0x7D, 0x03},
	{0x7D, 0x05},
	{0x7D, 0x09},
	{0x7D, 0x09},
	{0x7D, 0x0D},
	{0x7D, 0x11},
	{0x7D, 0x13},
	{0x7D, 0x19},
	{0x7D, 0x1C},
	{0x7E, 0x1F},
	{0x7E, 0x1F},
	{0x7E, 0x08},
	{0x7E, 0x0D},
	{0x7E, 0x10},
	{0x7E, 0x17},
	{0x7E, 0x1C},
	{0x7F, 0x00},
	{0x7F, 0x00},
	{0x7F, 0x05},
	{0x7F, 0x0A},
	{0x7F, 0x0E},
	{0x7F, 0x13},
	{0x7F, 0x18},
	{0x7F, 0x1D},
	{0x7F, 0x1D},
	{0x7F, 0x1F},
};

static void push_table_cb(void *dsi, dcs_write_gce cb, void *handle, struct LCM_setting_table *table, unsigned int table_count)
{
	unsigned int i, j;
	unsigned char temp[255] = {0};
	unsigned int cmd;

	for (i = 0; i < table_count; i++) {
		cmd = table[i].cmd;
		memset(temp, 0, sizeof(temp));
		switch (cmd) {
		case REGFLAG_DELAY:
			msleep(table[i].count);
			break;
		case REGFLAG_UDELAY:
			udelay(table[i].count);
			break;
		case REGFLAG_END_OF_TABLE:
			break;
		default:
			temp[0] = cmd;
			for (j = 0; j < table[i].count; j++)
				temp[j+1] = table[i].para_list[j];
			cb(dsi, handle, temp, table[i].count+1);
		}
	}
}

static void lcm_panel_init(struct lcm *ctx)
{
	char bl_tb[] = {0x51, 0x0f, 0xff};

	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return;
	}
	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay(35);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	lcm_dcs_write_seq_static(ctx, 0xFE, 0x6D);
	lcm_dcs_write_seq_static(ctx, 0x30, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x31, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x32, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x33, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x34, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x35, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x36, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x37, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x38, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x70, 0x11);
	lcm_dcs_write_seq_static(ctx, 0x71, 0x6c);
	lcm_dcs_write_seq_static(ctx, 0x72, 0x6c);
	lcm_dcs_write_seq_static(ctx, 0x73, 0x76);
	lcm_dcs_write_seq_static(ctx, 0x74, 0xE9);
	lcm_dcs_write_seq_static(ctx, 0x75, 0xC8);
	lcm_dcs_write_seq_static(ctx, 0x77, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x78, 0x92);
	lcm_dcs_write_seq_static(ctx, 0x79, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x7A, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x7B, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x80, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x81, 0x64);
	lcm_dcs_write_seq_static(ctx, 0x82, 0x64);
	lcm_dcs_write_seq_static(ctx, 0x83, 0x1C);
	lcm_dcs_write_seq_static(ctx, 0x84, 0x98);
	lcm_dcs_write_seq_static(ctx, 0x85, 0x2C);
	lcm_dcs_write_seq_static(ctx, 0x87, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x88, 0x82);
	lcm_dcs_write_seq_static(ctx, 0x89, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x8A, 0x01);
	lcm_dcs_write_seq_static(ctx, 0x8B, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x90, 0x5C);
	lcm_dcs_write_seq_static(ctx, 0x91, 0xFF);
	lcm_dcs_write_seq_static(ctx, 0x92, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x96, 0x03);
	lcm_dcs_write_seq_static(ctx, 0x9A, 0xFF);
	lcm_dcs_write_seq_static(ctx, 0x9B, 0x07);
	lcm_dcs_write_seq_static(ctx, 0x9C, 0xFF);
	lcm_dcs_write_seq_static(ctx, 0x9D, 0x07);
	lcm_dcs_write_seq_static(ctx, 0x9E, 0xFF);
	lcm_dcs_write_seq_static(ctx, 0x9F, 0x07);
	lcm_dcs_write_seq_static(ctx, 0xFE, 0x70);
	lcm_dcs_write_seq_static(ctx, 0x20, 0xC2);
	lcm_dcs_write_seq_static(ctx, 0xFE, 0x40);
	lcm_dcs_write_seq_static(ctx, 0x2A, 0x1F);
	lcm_dcs_write_seq_static(ctx, 0xFE, 0x81);
	lcm_dcs_write_seq_static(ctx, 0x01, 0x42);
	lcm_dcs_write_seq_static(ctx, 0x02, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x03, 0x02);
	lcm_dcs_write_seq_static(ctx, 0x04, 0xEC);
	lcm_dcs_write_seq_static(ctx, 0x05, 0x09);
	lcm_dcs_write_seq_static(ctx, 0x06, 0x73);
	lcm_dcs_write_seq_static(ctx, 0x07, 0x0a);
	lcm_dcs_write_seq_static(ctx, 0x08, 0x5F);
	lcm_dcs_write_seq_static(ctx, 0xfe, 0x81);
	lcm_dcs_write_seq_static(ctx, 0x00, 0x10);
	lcm_dcs_write_seq_static(ctx, 0x74, 0x11);
	lcm_dcs_write_seq_static(ctx, 0x82, 0x09);
	lcm_dcs_write_seq_static(ctx, 0x84, 0x09);
	lcm_dcs_write_seq_static(ctx, 0x86, 0x09);
	lcm_dcs_write_seq_static(ctx, 0x88, 0x01);
	lcm_dcs_write_seq_static(ctx, 0x64, 0x14);
	lcm_dcs_write_seq_static(ctx, 0xfe, 0x81);
	lcm_dcs_write_seq_static(ctx, 0x43, 0x12);
	lcm_dcs_write_seq_static(ctx, 0x44, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x45, 0x43);
	lcm_dcs_write_seq_static(ctx, 0x46, 0x32);
	lcm_dcs_write_seq_static(ctx, 0x47, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x48, 0x41);
	lcm_dcs_write_seq_static(ctx, 0x6a, 0x12);
	lcm_dcs_write_seq_static(ctx, 0x6b, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6c, 0x43);
	lcm_dcs_write_seq_static(ctx, 0x6d, 0x32);
	lcm_dcs_write_seq_static(ctx, 0x6e, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6f, 0x41);
	lcm_dcs_write_seq_static(ctx, 0xfe, 0x6D);
	lcm_dcs_write_seq_static(ctx, 0x90, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xfe, 0x40);
	lcm_dcs_write_seq_static(ctx, 0xbb, 0x1F);
	lcm_dcs_write_seq_static(ctx, 0xfe, 0x16);
	lcm_dcs_write_seq_static(ctx, 0x35, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xfe, 0x16);
	lcm_dcs_write_seq_static(ctx, 0x0d, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x0e, 0x04);
	lcm_dcs_write_seq_static(ctx, 0x0f, 0x05);
	lcm_dcs_write_seq_static(ctx, 0x10, 0x06);
	lcm_dcs_write_seq_static(ctx, 0x11, 0x06);
	lcm_dcs_write_seq_static(ctx, 0x12, 0x07);
	lcm_dcs_write_seq_static(ctx, 0x13, 0x08);
	lcm_dcs_write_seq_static(ctx, 0x14, 0x08);
	lcm_dcs_write_seq_static(ctx, 0x15, 0x09);
	lcm_dcs_write_seq_static(ctx, 0x16, 0x09);
	lcm_dcs_write_seq_static(ctx, 0x17, 0x0a);
	lcm_dcs_write_seq_static(ctx, 0x18, 0x0b);
	lcm_dcs_write_seq_static(ctx, 0x19, 0x0b);
	lcm_dcs_write_seq_static(ctx, 0x1a, 0x0d);
	lcm_dcs_write_seq_static(ctx, 0x1b, 0x0e);
	lcm_dcs_write_seq_static(ctx, 0x1c, 0x0f);
	lcm_dcs_write_seq_static(ctx, 0x1d, 0x10);
	lcm_dcs_write_seq_static(ctx, 0x1e, 0x11);
	lcm_dcs_write_seq_static(ctx, 0x1f, 0x12);
	lcm_dcs_write_seq_static(ctx, 0x20, 0x13);
	lcm_dcs_write_seq_static(ctx, 0x21, 0x14);
	lcm_dcs_write_seq_static(ctx, 0x22, 0x14);
	lcm_dcs_write_seq_static(ctx, 0x23, 0x16);
	lcm_dcs_write_seq_static(ctx, 0x24, 0x1B);
	lcm_dcs_write_seq_static(ctx, 0x25, 0x1f);
	lcm_dcs_write_seq_static(ctx, 0xfe, 0x4f);
	lcm_dcs_write_seq_static(ctx, 0x0C, 0x0a);
	lcm_dcs_write_seq_static(ctx, 0x17, 0x0B);
	lcm_dcs_write_seq_static(ctx, 0xfe, 0x70);
	lcm_dcs_write_seq_static(ctx, 0x1B, 0x83);
	lcm_dcs_write_seq_static(ctx, 0x20, 0xC2);
	lcm_dcs_write_seq_static(ctx, 0x28, 0x58);
	lcm_dcs_write_seq_static(ctx, 0x29, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xfe, 0x42);
	lcm_dcs_write_seq_static(ctx, 0x82, 0x14);
	lcm_dcs_write_seq_static(ctx, 0x83, 0x1c);
	lcm_dcs_write_seq_static(ctx, 0x1B, 0x02);
	lcm_dcs_write_seq_static(ctx, 0xfe, 0x53);
	lcm_dcs_write_seq_static(ctx, 0xD9, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xfe, 0x70);
	lcm_dcs_write_seq_static(ctx, 0x21, 0x0f);
	lcm_dcs_write_seq_static(ctx, 0x23, 0xff);
	lcm_dcs_write_seq_static(ctx, 0xfe, 0x40);
	lcm_dcs_write_seq_static(ctx, 0x3E, 0x3A);
	lcm_dcs_write_seq_static(ctx, 0xfe, 0x93);
	lcm_dcs_write_seq_static(ctx, 0x4c, 0x01);
	lcm_dcs_write_seq_static(ctx, 0x4d, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x4e, 0x40);
	lcm_dcs_write_seq_static(ctx, 0x4f, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x50, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x51, 0x40);
	lcm_dcs_write_seq_static(ctx, 0x52, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x53, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x54, 0x34);
	lcm_dcs_write_seq_static(ctx, 0x55, 0x0c);
	lcm_dcs_write_seq_static(ctx, 0x56, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x58, 0x40);
	lcm_dcs_write_seq_static(ctx, 0x59, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x5a, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x5b, 0x40);
	lcm_dcs_write_seq_static(ctx, 0x5c, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x5d, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x5e, 0x34);
	lcm_dcs_write_seq_static(ctx, 0x5f, 0x0c);
	lcm_dcs_write_seq_static(ctx, 0x60, 0x03);
	lcm_dcs_write_seq_static(ctx, 0x61, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x62, 0x40);
	lcm_dcs_write_seq_static(ctx, 0x63, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x64, 0x0d);
	lcm_dcs_write_seq_static(ctx, 0x65, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x66, 0x40);
	lcm_dcs_write_seq_static(ctx, 0x67, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x68, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x69, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6a, 0x40);
	lcm_dcs_write_seq_static(ctx, 0x6b, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6c, 0x03);
	lcm_dcs_write_seq_static(ctx, 0x6d, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6e, 0x40);
	lcm_dcs_write_seq_static(ctx, 0x6f, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x70, 0x0d);
	lcm_dcs_write_seq_static(ctx, 0x71, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x72, 0x40);
	lcm_dcs_write_seq_static(ctx, 0x73, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x74, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x75, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x76, 0x40);
	lcm_dcs_write_seq_static(ctx, 0x77, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x78, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x79, 0x34);
	lcm_dcs_write_seq_static(ctx, 0x7a, 0x0c);
	lcm_dcs_write_seq_static(ctx, 0x7b, 0x01);
	lcm_dcs_write_seq_static(ctx, 0x81, 0x8a);
	lcm_dcs_write_seq_static(ctx, 0x82, 0x89);
	lcm_dcs_write_seq_static(ctx, 0x83, 0x54);
	lcm_dcs_write_seq_static(ctx, 0x84, 0x7d);
	lcm_dcs_write_seq_static(ctx, 0xfe, 0x2b);
	lcm_dcs_write_seq_static(ctx, 0x00, 0x0f);
	lcm_dcs_write_seq_static(ctx, 0x01, 0x01);
	lcm_dcs_write_seq_static(ctx, 0x02, 0x3b);
	lcm_dcs_write_seq_static(ctx, 0x28, 0x02);
	lcm_dcs_write_seq_static(ctx, 0x29, 0x76);
	lcm_dcs_write_seq_static(ctx, 0x2a, 0x09);
	lcm_dcs_write_seq_static(ctx, 0x2b, 0x80);
	lcm_dcs_write_seq_static(ctx, 0x2d, 0x09);
	lcm_dcs_write_seq_static(ctx, 0x2f, 0xb4);
	lcm_dcs_write_seq_static(ctx, 0x30, 0x0a);
	lcm_dcs_write_seq_static(ctx, 0x31, 0x1d);
	lcm_dcs_write_seq_static(ctx, 0x32, 0x0a);
	lcm_dcs_write_seq_static(ctx, 0x33, 0x52);
	lcm_dcs_write_seq_static(ctx, 0x34, 0x0c);
	lcm_dcs_write_seq_static(ctx, 0x35, 0xff);
	lcm_dcs_write_seq_static(ctx, 0x36, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x37, 0xff);
	lcm_dcs_write_seq_static(ctx, 0x38, 0x33);
	lcm_dcs_write_seq_static(ctx, 0x39, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x3a, 0xff);
	lcm_dcs_write_seq_static(ctx, 0x3b, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x3d, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x3f, 0x66);
	lcm_dcs_write_seq_static(ctx, 0x40, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x41, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x42, 0x66);
	lcm_dcs_write_seq_static(ctx, 0x43, 0x3c);
	lcm_dcs_write_seq_static(ctx, 0x44, 0xaa);
	lcm_dcs_write_seq_static(ctx, 0x45, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x46, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x47, 0xaa);
	lcm_dcs_write_seq_static(ctx, 0x48, 0x0c);
	lcm_dcs_write_seq_static(ctx, 0x49, 0xee);
	lcm_dcs_write_seq_static(ctx, 0x4a, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x4b, 0xee);
	lcm_dcs_write_seq_static(ctx, 0x4c, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x4d, 0x44);
	lcm_dcs_write_seq_static(ctx, 0x4e, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x4f, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x50, 0x44);
	lcm_dcs_write_seq_static(ctx, 0x51, 0x3c);
	lcm_dcs_write_seq_static(ctx, 0x52, 0xcc);
	lcm_dcs_write_seq_static(ctx, 0x53, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x54, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x55, 0xcc);
	lcm_dcs_write_seq_static(ctx, 0x56, 0x0c);
	lcm_dcs_write_seq_static(ctx, 0x58, 0xee);
	lcm_dcs_write_seq_static(ctx, 0x59, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x5a, 0xee);
	lcm_dcs_write_seq_static(ctx, 0x5b, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x5c, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x5d, 0x44);
	lcm_dcs_write_seq_static(ctx, 0x5e, 0x44);
	lcm_dcs_write_seq_static(ctx, 0x5f, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x60, 0xc3);
	lcm_dcs_write_seq_static(ctx, 0x61, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x62, 0xcc);
	lcm_dcs_write_seq_static(ctx, 0x63, 0xcc);
	lcm_dcs_write_seq_static(ctx, 0x64, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x65, 0x3f);
	lcm_dcs_write_seq_static(ctx, 0x66, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x67, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x68, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x69, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6a, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6b, 0x44);
	lcm_dcs_write_seq_static(ctx, 0x6c, 0x44);
	lcm_dcs_write_seq_static(ctx, 0x6d, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6e, 0xc3);
	lcm_dcs_write_seq_static(ctx, 0x6f, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x70, 0xcc);
	lcm_dcs_write_seq_static(ctx, 0x71, 0xcc);
	lcm_dcs_write_seq_static(ctx, 0x72, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x73, 0x33);
	lcm_dcs_write_seq_static(ctx, 0x74, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x75, 0xee);
	lcm_dcs_write_seq_static(ctx, 0x76, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x77, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x78, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x79, 0x88);
	lcm_dcs_write_seq_static(ctx, 0x7a, 0x88);
	lcm_dcs_write_seq_static(ctx, 0x7b, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x7c, 0xc3);
	lcm_dcs_write_seq_static(ctx, 0x7d, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x7e, 0x88);
	lcm_dcs_write_seq_static(ctx, 0x7f, 0x88);
	lcm_dcs_write_seq_static(ctx, 0x80, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x81, 0x33);
	lcm_dcs_write_seq_static(ctx, 0x82, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x83, 0xff);
	lcm_dcs_write_seq_static(ctx, 0x84, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x85, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x86, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x87, 0x99);
	lcm_dcs_write_seq_static(ctx, 0x88, 0x99);
	lcm_dcs_write_seq_static(ctx, 0x89, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x8a, 0xc3);
	lcm_dcs_write_seq_static(ctx, 0x8b, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x8c, 0x77);
	lcm_dcs_write_seq_static(ctx, 0x8d, 0x77);
	lcm_dcs_write_seq_static(ctx, 0x8e, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x8f, 0x33);
	lcm_dcs_write_seq_static(ctx, 0x90, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x91, 0xff);
	lcm_dcs_write_seq_static(ctx, 0x92, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x9a, 0x88);

	lcm_dcs_write_seq_static(ctx, 0xFE, 0x82);
	lcm_dcs_write_seq_static(ctx, 0x00, 0x50);
	lcm_dcs_write_seq_static(ctx, 0xFE, 0xD7);
	lcm_dcs_write_seq_static(ctx, 0x26, 0x64);
	lcm_dcs_write_seq_static(ctx, 0x29, 0x69);

	lcm_dcs_write_seq_static(ctx, 0xFE, 0x4A);
	lcm_dcs_write_seq_static(ctx, 0x00, 0x10);
	lcm_dcs_write_seq_static(ctx, 0x0F, 0x28);
	lcm_dcs_write_seq_static(ctx, 0x10, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x11, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x12, 0x52);
	lcm_dcs_write_seq_static(ctx, 0x13, 0x4A);
	lcm_dcs_write_seq_static(ctx, 0x14, 0x04);
	lcm_dcs_write_seq_static(ctx, 0x52, 0xFE);
	lcm_dcs_write_seq_static(ctx, 0x53, 0x60);
	lcm_dcs_write_seq_static(ctx, 0x54, 0xDA);
	lcm_dcs_write_seq_static(ctx, 0x55, 0x5D);

	lcm_dcs_write_seq_static(ctx, 0xFE, 0x76);
	lcm_dcs_write_seq_static(ctx, 0x36, 0x0C);
	lcm_dcs_write_seq_static(ctx, 0xFE, 0x77);
	lcm_dcs_write_seq_static(ctx, 0x36, 0x18);
	lcm_dcs_write_seq_static(ctx, 0xFE, 0x78);
	lcm_dcs_write_seq_static(ctx, 0x36, 0x24);

	lcm_dcs_write_seq_static(ctx, 0xFE, 0x36);
	lcm_dcs_write_seq_static(ctx, 0xc4, 0x25);
	lcm_dcs_write_seq_static(ctx, 0xc5, 0x25);

	lcm_dcs_write_seq_static(ctx, 0xFE, 0xD8);
	lcm_dcs_write_seq_static(ctx, 0x31, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x32, 0x2D);
	lcm_dcs_write_seq_static(ctx, 0x33, 0xFF);

	lcm_dcs_write_seq_static(ctx, 0xFE, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xFA, 0x01);
	lcm_dcs_write_seq_static(ctx, 0xC2, 0x08);
	lcm_dcs_write_seq_static(ctx, 0x53, 0x20);
	lcm_dcs_write_seq_static(ctx, 0x35, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x51, 0x00, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xFE, 0x00);
	pr_info("%s current_fps:%d\n", __func__, current_fps);
	switch (current_fps) {
	case 144:
		lcm_dcs_write_seq_static(ctx, 0x2F, 0x05);
		break;
	case 120:
		lcm_dcs_write_seq_static(ctx, 0x2F, 0x00);
		break;
	case 60:
		lcm_dcs_write_seq_static(ctx, 0x2F, 0x06);
		break;
	default:
		pr_info("%s current_fps mismatch:%d\n", __func__, current_fps);
		break;
	}

	bl_tb[1] = (mapped_level >> 8) & 0xf;
	bl_tb[2] = mapped_level & 0xFF;
	lcm_dcs_write(ctx, bl_tb, ARRAY_SIZE(bl_tb));

	lcm_dcs_write_seq_static(ctx, 0x11);
	msleep(100);
	lcm_dcs_write_seq_static(ctx, 0x29);

	ctx->error = 0;
	g_dim_enable = 0;
	g_need_dim_enable = 0;
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;
	return 0;
}

static int lcm_unprepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->prepared)
		return 0;
	lcm_dcs_write_seq_static(ctx, 0xFE, 0x9a);
	lcm_dcs_write_seq_static(ctx, 0x00, 0x08);
	lcm_dcs_write_seq_static(ctx, 0xFE, 0x9b);
	lcm_dcs_write_seq_static(ctx, 0x00, 0x3e);
	lcm_dcs_write_seq_static(ctx, 0x01, 0x3e);
	lcm_dcs_write_seq_static(ctx, 0x02, 0x3e);
	lcm_dcs_write_seq_static(ctx, 0x03, 0x3e);
	lcm_dcs_write_seq_static(ctx, 0x04, 0x3e);
	lcm_dcs_write_seq_static(ctx, 0x05, 0x3e);
	msleep(5);
	lcm_dcs_write_seq_static(ctx, 0xFE, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x28);
	msleep(20);
	lcm_dcs_write_seq_static(ctx, 0x10);
	msleep(20);

	ctx->error = 0;
	ctx->prepared = false;
	g_aod_enable = 0;
	g_hbm_enable = 0;

	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;

	if (ctx->prepared)
		return 0;
	lcm_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0)
		lcm_unprepare(panel);

	ctx->prepared = true;

#ifdef PANEL_SUPPORT_READBACK
	lcm_panel_get_data(ctx);
#endif

	return ret;
}

static int lcm_enable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;

	return 0;
}

static const struct drm_display_mode default_mode = {
	.clock = (int)((FRAME_WIDTH + HFP + HSA + HBP) * (FRAME_HEIGHT + VFP + VSA + VBP) * 144 / 1000),
	.hdisplay = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH + HFP,
	.hsync_end = FRAME_WIDTH + HFP + HSA,
	.htotal = FRAME_WIDTH + HFP + HSA + HBP,
	.vdisplay = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + VFP,
	.vsync_end = FRAME_HEIGHT + VFP + VSA,
	.vtotal = FRAME_HEIGHT + VFP + VSA + VBP,
};

static const struct drm_display_mode mode_120 = {
	.clock = (int)((FRAME_WIDTH + HFP + HSA + HBP) * (FRAME_HEIGHT + VFP + VSA + VBP) * 120 / 1000),
	.hdisplay = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH + HFP,
	.hsync_end = FRAME_WIDTH + HFP + HSA,
	.htotal = FRAME_WIDTH + HFP + HSA + HBP,
	.vdisplay = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + VFP,
	.vsync_end = FRAME_HEIGHT + VFP + VSA,
	.vtotal = FRAME_HEIGHT + VFP + VSA + VBP,
};

static const struct drm_display_mode mode_60 = {
	.clock = (int)((FRAME_WIDTH + HFP + HSA + HBP) * (FRAME_HEIGHT + VFP + VSA + VBP) * 60 / 1000),
	.hdisplay = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH + HFP,
	.hsync_end = FRAME_WIDTH + HFP + HSA,
	.htotal = FRAME_WIDTH + HFP + HSA + HBP,
	.vdisplay = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + VFP,
	.vsync_end = FRAME_HEIGHT + VFP + VSA,
	.vtotal = FRAME_HEIGHT + VFP + VSA + VBP,
};

static const struct drm_display_mode fhd_default_mode = {
	.clock = FHD_CLK_DEF,
	.hdisplay = FHD_FRAME_WIDTH,
	.hsync_start = FHD_FRAME_WIDTH + FHD_HFP,
	.hsync_end = FHD_FRAME_WIDTH + FHD_HFP + FHD_HSA,
	.htotal = FHD_FRAME_WIDTH + FHD_HFP + FHD_HSA + FHD_HBP,
	.vdisplay = FHD_FRAME_HEIGHT,
	.vsync_start = FHD_FRAME_HEIGHT + FHD_VFP,
	.vsync_end = FHD_FRAME_HEIGHT + FHD_VFP + FHD_VSA,
	.vtotal = FHD_FRAME_HEIGHT + FHD_VFP + FHD_VSA + FHD_VBP,
};

static const struct drm_display_mode fhd_mode_120 = {
	.clock = FHD_CLK_120,
	.hdisplay = FHD_FRAME_WIDTH,
	.hsync_start = FHD_FRAME_WIDTH + FHD_HFP,
	.hsync_end = FHD_FRAME_WIDTH + FHD_HFP + FHD_HSA,
	.htotal = FHD_FRAME_WIDTH + FHD_HFP + FHD_HSA + FHD_HBP,
	.vdisplay = FHD_FRAME_HEIGHT,
	.vsync_start = FHD_FRAME_HEIGHT + FHD_VFP,
	.vsync_end = FHD_FRAME_HEIGHT + FHD_VFP + FHD_VSA,
	.vtotal = FHD_FRAME_HEIGHT + FHD_VFP + FHD_VSA + FHD_VBP,
};

static const struct drm_display_mode fhd_mode_60 = {
	.clock = FHD_CLK_60,
	.hdisplay = FHD_FRAME_WIDTH,
	.hsync_start = FHD_FRAME_WIDTH + FHD_HFP,
	.hsync_end = FHD_FRAME_WIDTH + FHD_HFP + FHD_HSA,
	.htotal = FHD_FRAME_WIDTH + FHD_HFP + FHD_HSA + FHD_HBP,
	.vdisplay = FHD_FRAME_HEIGHT,
	.vsync_start = FHD_FRAME_HEIGHT + FHD_VFP,
	.vsync_end = FHD_FRAME_HEIGHT + FHD_VFP + FHD_VSA,
	.vtotal = FHD_FRAME_HEIGHT + FHD_VFP + FHD_VSA + FHD_VBP,
};


#if defined(CONFIG_MTK_PANEL_EXT)
static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);

	pr_info("%s value=%d\n", __func__, on);
	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, on);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	return 0;
}

extern unsigned int jiffies_to_msecs(const unsigned long j);
static unsigned long lcm_unprepare_time;
static unsigned long lcm_prepare_time;
static int panel_ext_lcm_power_set(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret = 0;

	pr_info("%s status=%d\n", __func__, on);

	if (IS_ERR_OR_NULL(ctx->dvdd_regulator)) {
		ctx->dvdd_regulator = devm_regulator_get(ctx->dev, "dvdd");
		if (IS_ERR_OR_NULL(ctx->dvdd_regulator)) {
			ret = PTR_ERR(ctx->dvdd_regulator);
			dev_err(ctx->dev, "<%s,%d> lcm dvdd get fail!ret[%d].\n", __func__, __LINE__, ret);
			return 0;
		}
	}

	if (IS_ERR_OR_NULL(ctx->vci_regulator)) {
		ctx->vci_regulator = devm_regulator_get(ctx->dev, "vci");

		if (IS_ERR_OR_NULL(ctx->vci_regulator)) {
			ret = PTR_ERR(ctx->vci_regulator);
			dev_err(ctx->dev, "<%s,%d> lcm vci get fail!ret[%d].\n", __func__, __LINE__, ret);
			return 0;
		}
	}

	if (on) {
		unsigned long time_diff = 0;

		lcm_prepare_time = jiffies_to_msecs(jiffies);
		time_diff = lcm_prepare_time - lcm_unprepare_time;
		if (time_diff < 101)
			mdelay(101 - time_diff);

		ctx->vddi_gpio = devm_gpiod_get(ctx->dev, "vddi", GPIOD_OUT_HIGH);
		gpiod_set_value(ctx->vddi_gpio, 1);
		devm_gpiod_put(ctx->dev, ctx->vddi_gpio);
		mdelay(2);
		ret = regulator_set_voltage(ctx->dvdd_regulator, 1254000, 1254000);
		if (ret < 0)
			dev_err(ctx->dev, "<%s:%d>set lcm dvdd vol error!ret[%d]\n", __func__, __LINE__, ret);
		ret = regulator_enable(ctx->dvdd_regulator);
		if (ret < 0)
			dev_err(ctx->dev, "<%s:%d>enable lcm dvdd error!ret[%d]\n", __func__, __LINE__, ret);
		mdelay(2);
		ret = regulator_set_voltage(ctx->vci_regulator, 3000000, 3000000);
		if (ret < 0)
			dev_err(ctx->dev, "<%s:%d>set lcm vci vol error!ret[%d]\n", __func__, __LINE__, ret);
		ret = regulator_enable(ctx->vci_regulator);
		if (ret < 0)
			dev_err(ctx->dev, "<%s:%d>enable lcm vci error!ret[%d]\n", __func__, __LINE__, ret);
	} else {
		ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
		gpiod_set_value(ctx->reset_gpio, 0);
		devm_gpiod_put(ctx->dev, ctx->reset_gpio);
		mdelay(5);
		ret = regulator_disable(ctx->vci_regulator);
		if (ret < 0)
			dev_err(ctx->dev, "<%s:%d>disable lcm vci error!ret[%d]\n", __func__, __LINE__, ret);
		if (regulator_is_enabled(ctx->vci_regulator))
			dev_err(ctx->dev, "<%s:%d>after disable, lcm vci still enable!ret[%d]\n", __func__, __LINE__, ret);
		mdelay(2);
		ret = regulator_disable(ctx->dvdd_regulator);
		if (ret < 0)
			dev_err(ctx->dev, "<%s:%d>disable lcm dvdd error!ret[%d]\n", __func__, __LINE__, ret);
		if (regulator_is_enabled(ctx->dvdd_regulator))
			dev_err(ctx->dev, "<%s:%d>after disable, lcm dvdd still enable!ret[%d]\n", __func__, __LINE__, ret);
		mdelay(2);
		ctx->vddi_gpio = devm_gpiod_get(ctx->dev, "vddi", GPIOD_OUT_HIGH);
		gpiod_set_value(ctx->vddi_gpio, 0);
		devm_gpiod_put(ctx->dev, ctx->vddi_gpio);
		lcm_unprepare_time = jiffies_to_msecs(jiffies);
	}
	return 0;
}
static int panel_doze_enable(struct drm_panel *panel,
			void *dsi, dcs_write_gce cb, void *handle)
{
	push_table_cb(dsi, cb, handle, aod_mode_enter_setting,
		sizeof(aod_mode_enter_setting) / sizeof(struct LCM_setting_table));
	g_aod_enable = 1;
	return 0;
}
static int panel_doze_disable(struct drm_panel *panel,
			void *dsi, dcs_write_gce cb, void *handle)
{
	unsigned int aod_mapped_level = 0;


		aod_mapped_level = last_aod_level * 3515 / 4095;
	aod_mode_exit_setting[3].para_list[0] = (aod_mapped_level >> 8) & 0xf;
	aod_mode_exit_setting[3].para_list[1] = aod_mapped_level & 0xFF;
	push_table_cb(dsi, cb, handle, aod_mode_exit_setting,
		sizeof(aod_mode_exit_setting) / sizeof(struct LCM_setting_table));
	g_dim_enable = 0;
	g_need_dim_enable = 0;
	g_aod_enable = 0;
	return 0;
}

#if IS_ENABLED(CONFIG_TRANSSION_DOZE_BRIGHTNESS_SUPPORT)
static int panel_set_aod_light_mode(void *dsi, dcs_write_gce cb,
			void *handle, unsigned int level)
{
	char bl_aod_level[] = {0x51, 0x0F, 0xFE};
	unsigned int aod_mapped_level = 0;

	if (level == 8) {
		aod_mapped_level = 0x005;
		last_aod_level = 19;
	} else if (level == 167) {
		aod_mapped_level = 0x748;
		last_aod_level = 184;
	} else if (level == 330) {
		aod_mapped_level = 0xFFE;
		last_aod_level = 342;
	} else {
		aod_mapped_level = 0xFFE;
		last_aod_level = 342;
	}

	pr_info("[LCM] %s level is %u,aod_mapped_level is %d\n", __func__, level, aod_mapped_level);

	bl_aod_level[1] = ((aod_mapped_level >> 8) & 0x0f);
	bl_aod_level[2] = (aod_mapped_level & 0xff);
	cb(dsi, handle, bl_aod_level, ARRAY_SIZE(bl_aod_level));

	return 0;
}
#endif

static int panel_ata_check(struct drm_panel *panel)
{
	struct mtk_panel_ext *ext = find_panel_ext(panel);

	pr_info("ATA read data %x\n", ext->is_connected);

	return ext->is_connected;
}

static int panel_hbm_set_cmdq(struct drm_panel *panel, void *dsi,
			dcs_write_gce cb, void *handle, bool en)
{
	struct lcm *ctx = panel_to_lcm(panel);
	char bl_tb[] = {0x51, 0x0F, 0xff};
	unsigned int aod_mapped_level = 0;

	if (!cb)
		return -1;
	if (ctx->hbm_en == en)
		goto done;
	pr_info("%s FPS=%dHz en=%d, mapped_level=%d, aod=%d\n", __func__, current_fps, en, mapped_level, g_aod_enable);
	if (en) {
		if (g_aod_enable) {
			aod_mapped_level = last_aod_level * 3515 / 4095;
			aod_mode_exit_setting[3].para_list[0] = (aod_mapped_level >> 8) & 0xf;
			aod_mode_exit_setting[3].para_list[1] = aod_mapped_level & 0xFF;
			push_table_cb(dsi, cb, handle, aod_mode_exit_setting, sizeof(aod_mode_exit_setting) / sizeof(struct LCM_setting_table));
			hbm_mode_enter_setting[2].para_list[0] = (aod_mapped_level >> 8) & 0xf;
			hbm_mode_enter_setting[2].para_list[1] = aod_mapped_level & 0xFF;
			hbm_mode_enter_setting[3].para_list[1] = brightness_mapped_to_alpha[last_aod_level][0];
			hbm_mode_enter_setting[3].para_list[2] = brightness_mapped_to_alpha[last_aod_level][1];
		} else {
			hbm_mode_enter_setting[2].para_list[0] = (last_mapped_level >> 8) & 0xf;
			hbm_mode_enter_setting[2].para_list[1] = last_mapped_level & 0xFF;
			if (input_brightness > 459) {
				hbm_mode_enter_setting[3].para_list[1] = brightness_mapped_to_alpha[460][0];
				hbm_mode_enter_setting[3].para_list[2] = brightness_mapped_to_alpha[460][1];
			} else {
				hbm_mode_enter_setting[3].para_list[1] = brightness_mapped_to_alpha[input_brightness][0];
				hbm_mode_enter_setting[3].para_list[2] = brightness_mapped_to_alpha[input_brightness][1];
			}
		}
		push_table_cb(dsi, cb, handle, hbm_mode_enter_setting,
			sizeof(hbm_mode_enter_setting) / sizeof(struct LCM_setting_table));
	} else {
		push_table_cb(dsi, cb, handle, hbm_mode_exit_setting,
			sizeof(hbm_mode_exit_setting) / sizeof(struct LCM_setting_table));
		if (g_aod_enable) {
			push_table_cb(dsi, cb, handle, aod_mode_enter_setting, sizeof(aod_mode_enter_setting) / sizeof(struct LCM_setting_table));
			if (last_aod_level == 19) {
				aod_mapped_level = 0x005;
			} else if (last_aod_level == 184) {
				aod_mapped_level = 0x748;
			} else if (last_aod_level == 342) {
				aod_mapped_level = 0xFFE;
			} else {
				aod_mapped_level = 0xFFE;
			}

			bl_tb[1] = (aod_mapped_level >> 8) & 0x0f;
			bl_tb[2] = aod_mapped_level & 0xff;
			cb(dsi, handle, bl_tb, ARRAY_SIZE(bl_tb));
		}
		g_dim_enable = 0;
		g_need_dim_enable = 0;
	}
	ctx->hbm_en = en;
	ctx->hbm_wait = true;
	g_hbm_enable = ctx->hbm_en;
done:
	return 0;
}


static int panel_hbm_set_cmdq_switch(struct drm_panel *panel, void *dsi,
			dcs_write_gce cb, void *handle, bool en)
{
	if (panel_driver_status.panel_hbm_state == en) {
		pr_info("hbm\n");
		return 0;
	}
	pr_info("%s FPS=%dHz en=%d, mapped_level=%d, aod=%d\n", __func__, current_fps, en, mapped_level, g_aod_enable);
	if (en) {
		hbm_mode_enter_setting[2].para_list[0] = (last_mapped_level >> 8) & 0xf;
		hbm_mode_enter_setting[2].para_list[1] = last_mapped_level & 0xFF;
		if (input_brightness > 459) {
			hbm_mode_enter_setting[3].para_list[1] = brightness_mapped_to_alpha[460][0];
			hbm_mode_enter_setting[3].para_list[2] = brightness_mapped_to_alpha[460][1];
		} else {
			hbm_mode_enter_setting[3].para_list[1] = brightness_mapped_to_alpha[input_brightness][0];
			hbm_mode_enter_setting[3].para_list[2] = brightness_mapped_to_alpha[input_brightness][1];
		}
		push_table_cb(dsi, cb, handle, hbm_mode_enter_setting,
			sizeof(hbm_mode_enter_setting) / sizeof(struct LCM_setting_table));
	} else {
		push_table_cb(dsi, cb, handle, hbm_mode_exit_setting,
			sizeof(hbm_mode_exit_setting) / sizeof(struct LCM_setting_table));
		g_dim_enable = 0;
		g_need_dim_enable = 0;
	}
	panel_driver_status.panel_hbm_state = en;
	g_hbm_enable = en;
	return 0;
}

static void panel_hbm_get_state(struct drm_panel *panel, bool *state)
{
	struct lcm *ctx = panel_to_lcm(panel);
	*state = ctx->hbm_en;
}

static void panel_hbm_get_wait_state(struct drm_panel *panel, bool *wait)
{
	struct lcm *ctx = panel_to_lcm(panel);
	*wait = ctx->hbm_wait;
}

static bool panel_hbm_set_wait_state(struct drm_panel *panel, bool wait)
{
	struct lcm *ctx = panel_to_lcm(panel);
	bool old = ctx->hbm_wait;

	ctx->hbm_wait = wait;
	return old;
}

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb,
	void *handle, unsigned int level)
{
	char bl_tb[] = {0x51, 0x0F, 0xff};

	if (level > 5119)
		level = 5119;

	if (level != 0 && !g_aod_enable)
		input_brightness = level;

	if (level <= 4095) {
		mapped_level = level * 3515 / 4095;
	} else if (4095 < level && level <= 5119) {
		mapped_level = 3515 + (level - 4095) * (4095-3515) / 1024;
	}
	if (level == 1)
		mapped_level = 5;
	bl_tb[1] = ((mapped_level >> 8) & 0x0f);
	bl_tb[2] = (mapped_level & 0xff);

	if (mapped_level != 0 && !g_aod_enable)
		last_mapped_level = mapped_level;

	if (!cb) {
		return -1;
	}

	if ((g_dim_enable == 0) && (g_need_dim_enable == 1)) {
		bl_dim[1] = 0x28;
		cb(dsi, handle, bl_dim, ARRAY_SIZE(bl_dim));
		g_dim_enable = 1;
	}
	g_need_dim_enable = 1;
	if ((mapped_level == 0) || (panel_driver_status.dimming_status == 0)) {
		bl_dim[1] = 0x20;
		cb(dsi, handle, bl_dim, ARRAY_SIZE(bl_dim));
		g_dim_enable = 0;
		g_need_dim_enable = 0;
	}
	cb(dsi, handle, bl_tb, ARRAY_SIZE(bl_tb));
	pr_info("%s level = %d, mapped_level = %d dimming_status = %d\n", __func__, level,
		mapped_level, panel_driver_status.dimming_status);

	return 0;
}

static struct mtk_panel_params ext_params = {
	.pll_clk = PLL_CLOCK_144,
	.cust_esd_check = 0,
	.esd_check_enable = 0,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x1c,
	},
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.lp_perline_en = 0,
	.cmd_null_pkt_en = 1,
	.cmd_null_pkt_len = 0,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_param_load_mode = 2,
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.dsc_params = {
		.enable = 1,
		.ver = 18,
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 40,
		.rct_on = 1,
		.bit_per_channel = 10,
		.dsc_line_buf_depth = 16,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = FRAME_HEIGHT,
		.pic_width = FRAME_WIDTH,
		.slice_height = 70,
		.slice_width = (FRAME_WIDTH/2),
		.chunk_size = 630,
		.xmit_delay = 512,
		.dec_delay = 650,
		.scale_value = 32,
		.increment_interval = 1658,
		.decrement_interval = 8,
		.line_bpg_offset = 15,
		.nfl_bpg_offset = 446,
		.slice_bpg_offset = 319,
		.initial_offset = 6144,
		.final_offset = 4336,
		.flatness_minqp = 7,
		.flatness_maxqp = 16,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 15,
		.rc_quant_incr_limit1 = 15,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,

		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = rm692j0_fhdp_dsi_cmd_144hz_dphy_buf_thresh,
			.range_min_qp = rm692j0_fhdp_dsi_cmd_144hz_dphy_range_min_qp,
			.range_max_qp = rm692j0_fhdp_dsi_cmd_144hz_dphy_range_max_qp,
			.range_bpg_ofs = rm692j0_fhdp_dsi_cmd_144hz_dphy_range_bpg_ofs,
			},
		},
	.data_rate = PLL_CLOCK_144 * 2,
	.dyn = {
		.switch_en = 0,
		.pll_clk = PLL_CLOCK_144 + 1,
	},
	.dyn_fps = {
		.vact_timing_fps = 144,
	},
	.mode_switch_cmdq = MODE_SWITCH_CMDQ_ENABLE,
	.real_te_duration = 6944,
	.tran_panel_params = &panel_driver_status,
};

static struct mtk_panel_params ext_params_120hz = {
	.pll_clk = PLL_CLOCK_120,
	.cust_esd_check = 0,
	.esd_check_enable = 0,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x1c,
	},
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.lp_perline_en = 0,
	.cmd_null_pkt_en = 1,
	.cmd_null_pkt_len = 400,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_param_load_mode = 2,
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.dsc_params = {
		.enable = 1,
		.ver = 18,
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 40,
		.rct_on = 1,
		.bit_per_channel = 10,
		.dsc_line_buf_depth = 16,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = FRAME_HEIGHT,
		.pic_width = FRAME_WIDTH,
		.slice_height = 70,
		.slice_width = (FRAME_WIDTH/2),
		.chunk_size = 630,
		.xmit_delay = 512,
		.dec_delay = 650,
		.scale_value = 32,
		.increment_interval = 1658,
		.decrement_interval = 8,
		.line_bpg_offset = 15,
		.nfl_bpg_offset = 446,
		.slice_bpg_offset = 319,
		.initial_offset = 6144,
		.final_offset = 4336,
		.flatness_minqp = 7,
		.flatness_maxqp = 16,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 15,
		.rc_quant_incr_limit1 = 15,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,

		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = rm692j0_fhdp_dsi_cmd_144hz_dphy_buf_thresh,
			.range_min_qp = rm692j0_fhdp_dsi_cmd_144hz_dphy_range_min_qp,
			.range_max_qp = rm692j0_fhdp_dsi_cmd_144hz_dphy_range_max_qp,
			.range_bpg_ofs = rm692j0_fhdp_dsi_cmd_144hz_dphy_range_bpg_ofs,
			},
		},
	.data_rate = PLL_CLOCK_120 * 2,
	.dyn = {
		.switch_en = 0,
		.pll_clk = PLL_CLOCK_144 + 1,
	},
	.dyn_fps = {
		.vact_timing_fps = 120,
	},
	.mode_switch_cmdq = MODE_SWITCH_CMDQ_ENABLE,
	.real_te_duration = 8333,
	.tran_panel_params = &panel_driver_status,
};

static struct mtk_panel_params ext_params_60hz = {
	.pll_clk = PLL_CLOCK_60,
	.cust_esd_check = 0,
	.esd_check_enable = 0,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x1c,
	},
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.lp_perline_en = 0,
	.cmd_null_pkt_en = 1,
	.cmd_null_pkt_len = 700,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_param_load_mode = 2,
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.dsc_params = {
		.enable = 1,
		.ver = 18,
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 40,
		.rct_on = 1,
		.bit_per_channel = 10,
		.dsc_line_buf_depth = 16,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = FRAME_HEIGHT,
		.pic_width = FRAME_WIDTH,
		.slice_height = 70,
		.slice_width = (FRAME_WIDTH/2),
		.chunk_size = 630,
		.xmit_delay = 512,
		.dec_delay = 650,
		.scale_value = 32,
		.increment_interval = 1658,
		.decrement_interval = 8,
		.line_bpg_offset = 15,
		.nfl_bpg_offset = 446,
		.slice_bpg_offset = 319,
		.initial_offset = 6144,
		.final_offset = 4336,
		.flatness_minqp = 7,
		.flatness_maxqp = 16,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 15,
		.rc_quant_incr_limit1 = 15,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,

		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = rm692j0_fhdp_dsi_cmd_144hz_dphy_buf_thresh,
			.range_min_qp = rm692j0_fhdp_dsi_cmd_144hz_dphy_range_min_qp,
			.range_max_qp = rm692j0_fhdp_dsi_cmd_144hz_dphy_range_max_qp,
			.range_bpg_ofs = rm692j0_fhdp_dsi_cmd_144hz_dphy_range_bpg_ofs,
			},
		},
	.data_rate = PLL_CLOCK_60 * 2,
	.dyn = {
		.switch_en = 0,
		.pll_clk = PLL_CLOCK_144 + 1,
	},
	.dyn_fps = {
		.vact_timing_fps = 60,
	},
	.mode_switch_cmdq = MODE_SWITCH_CMDQ_ENABLE,
	.real_te_duration = 16666,
	.tran_panel_params = &panel_driver_status,
};

struct drm_display_mode *get_mode_by_id(struct drm_connector *connector,
	unsigned int mode)
{
	struct drm_display_mode *m = NULL;
	unsigned int i = 0;

	list_for_each_entry(m, &connector->modes, head) {
		if (i == mode)
			return m;
		i++;
	}
	return NULL;
}


static int mtk_panel_ext_param_set(struct drm_panel *panel,
			struct drm_connector *connector, unsigned int mode)
{
	struct mtk_panel_ext *ext = find_panel_ext(panel);
	#if LCM_MIPISWITCH_TE_DELAY
	struct lcm *ctx = panel_to_lcm(panel);
	#endif
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id(connector, mode);

	pr_info("%s set mode=%d FPS form %d to %d\n", __func__, mode, current_fps, drm_mode_vrefresh(m));
	if (drm_mode_vrefresh(m) == 144) {
		ext_params.skip_vblank = 0;
		ext_params.vblank_off = false;
		ext->params = &ext_params;
	} else if (drm_mode_vrefresh(m) == 120) {
		ext_params_120hz.skip_vblank = 0;
		ext_params_120hz.vblank_off = false;
		ext->params = &ext_params_120hz;
	} else if (drm_mode_vrefresh(m) == 60) {
		ext_params_60hz.skip_vblank = 0;
		ext_params_60hz.vblank_off = false;
		ext->params = &ext_params_60hz;
	} else
		ret = 1;

	#if LCM_MIPISWITCH_TE_DELAY
	ctx->lcm_src_datarate = ctx->lcm_dst_datarate;
	ctx->lcm_dst_datarate = ext->params->data_rate;
	if (ctx->lcm_src_datarate > ctx->lcm_dst_datarate)
		g_te_need_delay = 1;
	#endif

	if (!ret)
		current_fps = drm_mode_vrefresh(m);

	return ret;
}

static int mtk_panel_ext_param_get(struct drm_panel *panel,
	struct drm_connector *connector,
	struct mtk_panel_params **ext_param,
	unsigned int mode)
{
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id(connector, mode);

	pr_info("%s get mode=%d\n", __func__, mode);
	if (drm_mode_vrefresh(m) == 144)
		*ext_param = &ext_params;
	else if (drm_mode_vrefresh(m) == 120)
		*ext_param = &ext_params_120hz;
	else if (drm_mode_vrefresh(m) == 60)
		*ext_param = &ext_params_60hz;
	else
		ret = 1;

	if (!ret)
		current_fps = drm_mode_vrefresh(m);

	return ret;
}

enum RES_SWITCH_TYPE mtk_get_res_switch_type(void)
{
	pr_info("res_switch_type: %d\n", res_switch_type);
	return res_switch_type;
}

int mtk_scaling_mode_mapping(int mode_idx)
{
	return (mode_idx % REAL_MODE_NUM);
}

static void mode_switch_working(struct drm_panel *panel, int fps,
	struct mtk_mode_switch_cmd *mode_switch_cmd, size_t len)
{
#if MODE_SWITCH_CMDQ_ENABLE
	if (fps == 144) {
		memset(&ext_params.mode_switch_cmd, 0,
			sizeof(struct mode_switch_params));
		ext_params.mode_switch_cmd.num_cmd = len;
		memcpy(&ext_params.mode_switch_cmd.ms_table, mode_switch_cmd,
			sizeof(struct mtk_mode_switch_cmd) * len);
	} else if (fps == 120) {
		memset(&ext_params_120hz.mode_switch_cmd, 0,
			sizeof(struct mode_switch_params));
		ext_params_120hz.mode_switch_cmd.num_cmd = len;
		memcpy(&ext_params_120hz.mode_switch_cmd.ms_table, mode_switch_cmd,
			sizeof(struct mtk_mode_switch_cmd) * len);
	} else if (fps == 60) {
		memset(&ext_params_60hz.mode_switch_cmd, 0,
			sizeof(struct mode_switch_params));
		ext_params_60hz.mode_switch_cmd.num_cmd = len;
		memcpy(&ext_params_60hz.mode_switch_cmd.ms_table, mode_switch_cmd,
			sizeof(struct mtk_mode_switch_cmd) * len);
	}
#else
	int i;
	struct lcm *ctx = panel_to_lcm(panel);

	for (i = 0; i < len; i++) {
		lcm_dcs_write(ctx, mode_switch_cmd[i].para_list,
			mode_switch_cmd[i].cmd_num);
	}
#endif
}

static int mode_switch(struct drm_panel *panel,
		struct drm_connector *connector, unsigned int cur_mode,
		unsigned int dst_mode, enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id(connector, dst_mode);

	if (stage == BEFORE_DSI_POWERDOWN) {
		ret = 1;
		return ret;
	}

	pr_info("%s cur_mode = %d dst_mode %d\n", __func__, cur_mode, dst_mode);

	if (drm_mode_vrefresh(m) == 144)
		mode_switch_working(panel, 144, cmd_table_144fps, ARRAY_SIZE(cmd_table_144fps));
	else if (drm_mode_vrefresh(m) == 120)
		mode_switch_working(panel, 120, cmd_table_120fps, ARRAY_SIZE(cmd_table_120fps));
	else if (drm_mode_vrefresh(m) == 60)
		mode_switch_working(panel, 60, cmd_table_60fps, ARRAY_SIZE(cmd_table_60fps));
	else
		ret = 1;

	#if LCM_MIPISWITCH_TE_DELAY
	if (g_te_need_delay) {
		g_te_need_delay = 0;
		if (cur_mode == 0)
			mdelay(1000 / 144);
		else if (cur_mode == 1)
			mdelay(1000 / 120);
	}
	#endif
#ifdef CONFIG_TRAN_GET_VREFRESH_SUPPORT
	g_tran_get_vrefresh = current_fps;
#endif
	return ret;
}

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.ext_param_set = mtk_panel_ext_param_set,
	.ext_param_get = mtk_panel_ext_param_get,
	.get_res_switch_type = mtk_get_res_switch_type,
	.scaling_mode_mapping = mtk_scaling_mode_mapping,
	.mode_switch = mode_switch,
#if IS_ENABLED(CONFIG_TRANSSION_DOZE_BRIGHTNESS_SUPPORT)
	.set_aod_light_mode = panel_set_aod_light_mode,
#endif
	.ata_check = panel_ata_check,
	.lcm_power_set = panel_ext_lcm_power_set,
	.hbm_set_cmdq = panel_hbm_set_cmdq,
	.hbm_set_cmdq_switch = panel_hbm_set_cmdq_switch,
	.hbm_get_state = panel_hbm_get_state,
	.hbm_get_wait_state = panel_hbm_get_wait_state,
	.hbm_set_wait_state = panel_hbm_set_wait_state,
	.doze_enable = panel_doze_enable,
	.doze_disable = panel_doze_disable,
};
#endif

struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int num_modes;

	unsigned int bpc;

	struct {
		unsigned int width;
		unsigned int height;
	} size;

	struct {
		unsigned int prepare;
		unsigned int enable;
		unsigned int disable;
		unsigned int unprepare;
	} delay;
};

static int lcm_get_modes(struct drm_panel *panel, struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct drm_display_mode *mode1;
	struct drm_display_mode *mode2;

	struct drm_display_mode *fhd_mode;
	struct drm_display_mode *fhd_mode1;
	struct drm_display_mode *fhd_mode2;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		dev_err(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	mode1 = drm_mode_duplicate(connector->dev, &mode_120);
	if (!mode1)
		return -ENOMEM;

	drm_mode_set_name(mode1);
	mode1->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode1);

	mode2 = drm_mode_duplicate(connector->dev, &mode_60);
	if (!mode2) {
		dev_err(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			mode_60.hdisplay, mode_60.vdisplay,
			drm_mode_vrefresh(&mode_60));
		return -ENOMEM;
	}

	drm_mode_set_name(mode2);
	mode2->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode2);
	if (res_switch_type == RES_SWITCH_ON_AP) {
		fhd_mode = drm_mode_duplicate(connector->dev, &fhd_default_mode);
		if (!fhd_mode) {
			dev_err(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
				fhd_default_mode.hdisplay, fhd_default_mode.vdisplay,
				drm_mode_vrefresh(&fhd_default_mode));
			return -ENOMEM;
		}

		drm_mode_set_name(fhd_mode);
		fhd_mode->type = DRM_MODE_TYPE_DRIVER;
		drm_mode_probed_add(connector, fhd_mode);

		fhd_mode1 = drm_mode_duplicate(connector->dev, &fhd_mode_120);
		if (!fhd_mode1) {
			dev_err(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
				fhd_mode_120.hdisplay, fhd_mode_120.vdisplay,
				drm_mode_vrefresh(&fhd_mode_120));
			return -ENOMEM;
		}

		drm_mode_set_name(fhd_mode1);
		fhd_mode1->type = DRM_MODE_TYPE_DRIVER;
		drm_mode_probed_add(connector, fhd_mode1);

		fhd_mode2 = drm_mode_duplicate(connector->dev, &fhd_mode_60);
		if (!fhd_mode2) {
			dev_err(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
				fhd_mode_60.hdisplay, fhd_mode_60.vdisplay,
				drm_mode_vrefresh(&fhd_mode_60));
			return -ENOMEM;
		}

		drm_mode_set_name(fhd_mode2);
		fhd_mode2->type = DRM_MODE_TYPE_DRIVER;
		drm_mode_probed_add(connector, fhd_mode2);
	}
	connector->display_info.width_mm = 70;
	connector->display_info.height_mm = 155;
	if (res_switch_type == RES_SWITCH_ON_AP) {
		return 6;
	} else {
		return 3;
	}
}

static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable = lcm_disable,
	.unprepare = lcm_unprepare,
	.prepare = lcm_prepare,
	.enable = lcm_enable,
	.get_modes = lcm_get_modes,
};

static struct lcm *ctx_work;
static void lcm_regulator_power_init(struct work_struct *lcm_work);
static DECLARE_DELAYED_WORK(lcm_regulator_work, lcm_regulator_power_init);
static void lcm_regulator_power_init(struct work_struct *lcm_work)
{
	int ret = 0;

	pr_info("[LCM]%s start\n", __func__);
	ctx_work->dvdd_regulator = devm_regulator_get(ctx_work->dev, "dvdd");
	if (IS_ERR_OR_NULL(ctx_work->dvdd_regulator)) {
		pr_info("<%s,%d> lcm dvdd regulator get fail!\n", __func__, __LINE__);
		schedule_delayed_work(&lcm_regulator_work, msecs_to_jiffies(7000));
		return;
	}
	ret = regulator_set_voltage(ctx_work->dvdd_regulator, 1254000, 1254000);
	if (ret < 0)
		pr_info("<%s,%d> lcm dvdd set vol fail!ret[%d]\n", __func__, __LINE__, ret);
	ret = regulator_enable(ctx_work->dvdd_regulator);
	if (ret < 0)
		pr_info("<%s,%d> lcm dvdd enable error!ret[%d]\n", __func__, __LINE__, ret);
}

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;
	struct lcm *ctx;
	struct device_node *backlight;
	unsigned int res_switch;
	int ret;
	#if IS_ENABLED(CONFIG_TRANSSION_DOZE_BRIGHTNESS_SUPPORT)
	unsigned int doze_backlight[] = {0, 0, 0, 0};
	#endif

	pr_info("%s+\n", __func__);

	dsi_node = of_get_parent(dev->of_node);
	if (dsi_node) {
		endpoint = of_graph_get_next_endpoint(dsi_node, NULL);
		if (endpoint) {
			remote_node = of_graph_get_remote_port_parent(endpoint);
			if (!remote_node) {
				pr_info("No panel connected,skip probe lcm\n");
				return -ENODEV;
			}
			pr_info("device node name:%s\n", remote_node->name);
		}
	}
	if (remote_node != dev->of_node) {
		pr_info("%s+ skip probe due to not current lcm\n", __func__);
		return -ENODEV;
	}

#if IS_ENABLED(CONFIG_TRANSSION_DOZE_BRIGHTNESS_SUPPORT)
	of_property_read_u32_array(remote_node, "doze_backlight", doze_backlight, ARRAY_SIZE(doze_backlight));
	g_lcm_doze_backlight.doze_backlight_num = doze_backlight[0];
	g_lcm_doze_backlight.doze_backlight_level1 = doze_backlight[1];
	g_lcm_doze_backlight.doze_backlight_level2 = doze_backlight[2];
	g_lcm_doze_backlight.doze_backlight_level3 = doze_backlight[3];
	pr_info("[LCM] %s lcm_doze_backlight:%d;%d;%d;%d;\n", __func__, g_lcm_doze_backlight.doze_backlight_num,
		g_lcm_doze_backlight.doze_backlight_level1, g_lcm_doze_backlight.doze_backlight_level2, g_lcm_doze_backlight.doze_backlight_level3);
	panel_driver_status.lcm_doze_backlight = &g_lcm_doze_backlight;
#endif

	panel_driver_status.dimming_status = 1;

	ctx = devm_kzalloc(dev, sizeof(struct lcm), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_NO_EOT_PACKET
			 | MIPI_DSI_CLOCK_NON_CONTINUOUS;

	ret = of_property_read_u32(dev->of_node, "res-switch", &res_switch);
	if (ret < 0)
		res_switch = 0;
	else
		res_switch_type = (enum RES_SWITCH_TYPE)res_switch;

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	ctx->vddi_gpio = devm_gpiod_get(dev, "vddi", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vddi_gpio)) {
		dev_err(dev, "cannot get vddi-gpios %ld\n", PTR_ERR(ctx->vddi_gpio));
		return PTR_ERR(ctx->vddi_gpio);
	}
	devm_gpiod_put(dev, ctx->vddi_gpio);

	ctx_work = ctx;
	schedule_delayed_work(&lcm_regulator_work, msecs_to_jiffies(3000));

	ctx->vci_regulator = devm_regulator_get(ctx->dev, "vci");
	if (IS_ERR(ctx->vci_regulator)) {
		ret = PTR_ERR(ctx->vci_regulator);
		dev_err(ctx->dev, "cannot get vci regulator ret[%d]\n", ret);
		return ret;
	}
	ret = regulator_enable(ctx->vci_regulator);
	if (ret < 0)
		dev_err(ctx->dev, "<%s:%d>enable vci vol error!ret[%d]\n", __func__, __LINE__, ret);

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "%s: cannot get reset-gpios %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	devm_gpiod_put(dev, ctx->reset_gpio);

	ctx->prepared = true;
	ctx->enabled = true;

	drm_panel_init(&ctx->panel, dev, &lcm_drm_funcs, DRM_MODE_CONNECTOR_DSI);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &lcm_drm_funcs;
	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

#if defined(CONFIG_MTK_PANEL_EXT)
	ret = mtk_panel_ext_create(dev, &ext_params, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;
#if LCM_MIPISWITCH_TE_DELAY
	ctx->lcm_dst_datarate = ext_params_120hz.data_rate;
#endif
#endif

	pr_info("%s-\n", __func__);

	return ret;
}

static void lcm_remove(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);
#if defined(CONFIG_MTK_PANEL_EXT)
	struct mtk_panel_ctx *ext_ctx = find_panel_ctx(&ctx->panel);
#endif
	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

#if defined(CONFIG_MTK_PANEL_EXT)
	if (ext_ctx != NULL) {
		mtk_panel_detach(ext_ctx);
		mtk_panel_remove(ext_ctx);
	}
#endif

}

static void lcm_shutdown(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);

	lcm_disable(&ctx->panel);
	if (ctx->prepared) {
		ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
		gpiod_set_value(ctx->reset_gpio, 0);
		devm_gpiod_put(ctx->dev, ctx->reset_gpio);
		mdelay(2);
	}
}

static const struct of_device_id lcm_of_match[] = {
	{ .compatible = "rm692j0,fhdp,dsi,cmd,dsc,boe,boe,144hz,lj9", },
	{ }
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.shutdown = lcm_shutdown,
	.driver = {
		.name = "rm692j0_fhdp_dsi_cmd_dsc_boe_boe_144hz_lj9",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("Transsion Inc.");
MODULE_DESCRIPTION("transsion, panel driver");
MODULE_LICENSE("GPL v2");
