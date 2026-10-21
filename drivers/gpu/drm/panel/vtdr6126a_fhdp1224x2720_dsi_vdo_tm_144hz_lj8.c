// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) <2024> Transsion Inc.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_modes.h>
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
#include <linux/gpio/consumer.h>
#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mediatek_v2/mtk_panel_ext.h"
#include "../mediatek/mediatek_v2/mtk_drm_graphics_base.h"
#define REGFLAG_DELAY               0xFFFC
#define REGFLAG_UDELAY              0xFFFB
#define REGFLAG_END_OF_TABLE        0xFFFD
#define REGFLAG_RESET_LOW           0xFFFE
#define REGFLAG_RESET_HIGH          0xFFFF

#define FRAME_WIDTH                 1224
#define FRAME_HEIGHT                2720
#define PHYSICAL_WIDTH              70670
#define PHYSICAL_HEIGHT             157050

#define VSA                         2
#define VBP                         18
#define HSA                         4
#define HBP                         16

#define MODE_0_FPS                  144
#define MODE_0_VFP                  76
#define MODE_0_HFP                  32
#define MODE_0_DATA_RATE            1124

#define MODE_1_FPS                  120
#define MODE_1_VFP                  76
#define MODE_1_HFP                  124
#define MODE_1_DATA_RATE            1124

#define MODE_2_FPS                  90
#define MODE_2_VFP                  1016
#define MODE_2_HFP                  124
#define MODE_2_DATA_RATE            1124

#define MODE_3_FPS                  60
#define MODE_3_VFP                  2892
#define MODE_3_HFP                  124
#define MODE_3_DATA_RATE            1124

#define DSC_ENABLE                  1
#define DSC_VER                     18
#define DSC_SLICE_MODE              1
#define DSC_RGB_SWAP                0
#define DSC_DSC_CFG                 34
#define DSC_RCT_ON                  1
#define DSC_BIT_PER_CHANNEL         8
#define DSC_DSC_LINE_BUF_DEPTH      9
#define DSC_BP_ENABLE               1
#define DSC_BIT_PER_PIXEL           128
#define DSC_SLICE_HEIGHT            20
#define DSC_SLICE_WIDTH             612
#define DSC_CHUNK_SIZE              612
#define DSC_XMIT_DELAY              512
#define DSC_DEC_DELAY               588
#define DSC_SCALE_VALUE             32
#define DSC_INCREMENT_INTERVAL      497
#define DSC_DECREMENT_INTERVAL      8
#define DSC_LINE_BPG_OFFSET         13
#define DSC_NFL_BPG_OFFSET          1402
#define DSC_SLICE_BPG_OFFSET        1149
#define DSC_INITIAL_OFFSET          6144
#define DSC_FINAL_OFFSET            4336
#define DSC_FLATNESS_MINQP          3
#define DSC_FLATNESS_MAXQP          12
#define DSC_RC_MODEL_SIZE           8192
#define DSC_RC_EDGE_FACTOR          6
#define DSC_RC_QUANT_INCR_LIMIT0    11
#define DSC_RC_QUANT_INCR_LIMIT1    11
#define DSC_RC_TGT_OFFSET_HI        3
#define DSC_RC_TGT_OFFSET_LO        3

static unsigned int rc_buf_thresh[14] = {
896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000, 8064};
unsigned int range_min_qp[15] = {0, 0, 1, 1, 3, 3, 3, 3, 3, 3, 5, 5, 5, 9, 12};
unsigned int range_max_qp[15] = {4, 4, 5, 6, 7, 7, 7, 8, 9, 10, 10, 11, 11, 12, 13};
int range_bpg_ofs[15] = {2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -12, -12, -12, -12};
#endif

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *vddi_gpio;
	struct gpio_desc *vci_gpio;
	struct regulator *dvdd_regulator;
	bool prepared;
	bool enabled;
	int error;
	bool hbm_en;
	bool hbm_wait;
};

tran_lcm_doze_backlight g_lcm_doze_backlight = {
	.doze_backlight_num = 0,
	.doze_backlight_level1 = 0,
	.doze_backlight_level2 = 0,
	.doze_backlight_level3 = 0,
};

struct tran_panel_driver_params panel_driver_status = {0};

static char bl_tb0[] = {0x51, 0x0D, 0xBB};
static unsigned int last_mapped_level;
static int g_aod_enable;
static int g_hbm_enable;
static int g_lcm_fresh_mode = MODE_0_FPS;
static unsigned int mapped_level;
static char bl_dim[] = {0x53, 0x28};
static unsigned int g_dim_state;
static unsigned int g_need_dim_enable;
static unsigned int g_aod_setbacklight;

#define lcm_dcs_write_seq(ctx, seq...)                                     \
	({                                                                     \
		const u8 d[] = {seq};                                          \
		BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64,                           \
				 "DCS sequence too big for stack");            \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                      \
	})
#define lcm_dcs_write_seq_static(ctx, seq...)                              \
	({                                                                     \
		static const u8 d[] = {seq};                                   \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                      \
	})

static inline struct lcm *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct lcm, panel);
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
		dev_err(ctx->dev, "error %d reading dcs seq:(%#x)\n", ret, cmd);
		ctx->error = ret;
	}
	return ret;
}

static void lcm_panel_get_data(struct lcm *ctx)
{
	u8 buffer[3] = {0};
	static int ret;

	if (ret == 0) {
		ret = lcm_dcs_read(ctx, 0x0A, buffer, 1);
		dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
			 ret, buffer[0] | (buffer[1] << 8));
	}
}
#endif

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

struct LCM_setting_table {
	unsigned int cmd;
	unsigned char count;
	unsigned char para_list[120];
};

static struct LCM_setting_table lcm_initialization_setting[] = {
	{0x6C, 1, {0x00}},

	{0xF0, 2, {0xAA, 0x16}},
	{0xD1, 3, {0x00, 0x00, 0x00}},
	{0x65, 1, {0x03}},
	{0xD1, 3, {0x00, 0x00, 0x00}},
	{0x65, 1, {0x06}},
	{0xD1, 3, {0x00, 0x00, 0x00}},
	{0x65, 1, {0x09}},
	{0xD1, 3, {0x00, 0x00, 0x00}},
	{0x65, 1, {0x0C}},
	{0xD1, 3, {0x00, 0x00, 0x00}},

	{0xFF, 2, {0x5A, 0x81}},
	{0x65, 1, {0x02}},
	{0xFB, 2, {0x73, 0x73}},

	{0xFF, 2, {0x5A, 0x81}},
	{0x65, 1, {0x03}},
	{0xF3, 1, {0x61}},
	{0x65, 1, {0x0B}},
	{0xF3, 1, {0x78}},

	{0xFF, 2, {0x5A, 0x80}},
	{0xF9, 1, {0x10}},
	{0x65, 1, {0x0a}},
	{0xF9, 1, {0x1e}},
	{0x65, 1, {0x25}},
	{0xFD, 1, {0x01}},

	{0xFF, 2, {0x5A, 0x83}},
	{0x65, 1, {0x0B}},
	{0xF7, 1, {0x03}},

	{0x03, 1, {0x01}},
	{0x35, 1, {0x00}},
	{0x53, 1, {0x20}},
	{0x51, 2, {0x00, 0x00}},
	{0x59, 1, {0x09}},
	{0x72, 1, {0x00}},
	{0x6D, 1, {0x00}},
	{0x6F, 1, {0x01}},

	{0xF0, 2, {0xAA, 0x18}},
	{0xB0, 1, {0x80}},
	{0xB2, 1, {0x00}},

	{0x70, 94, {0x12, 0x00, 0x00, 0x89, 0x30, 0x80, 0x0A, 0xA0, 0x04, 0xC8,
			  0x00, 0x14, 0x02, 0x64, 0x02, 0x64, 0x02, 0x00, 0x02, 0x4C,
			  0x00, 0x20, 0x01, 0xF1, 0x00, 0x08, 0x00, 0x0D, 0x05, 0x7A,
			  0x04, 0x7D, 0x18, 0x00, 0x10, 0xF0, 0x03, 0x0C, 0x20, 0x00,
			  0x06, 0x0B, 0x0B, 0x33, 0x0E, 0x1C, 0x2A, 0x38, 0x46, 0x54,
			  0x62, 0x69, 0x70, 0x77, 0x79, 0x7B, 0x7D, 0x7E, 0x01, 0x02,
			  0x01, 0x00, 0x09, 0x40, 0x09, 0xBE, 0x19, 0xFC, 0x19, 0xFA,
			  0x19, 0xF8, 0x1A, 0x38, 0x1A, 0x78, 0x1A, 0xB6, 0x2A, 0xB6,
			  0x2A, 0xF4, 0x2A, 0xF4, 0x4B, 0x34, 0x63, 0x74, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00}},

	{0xF0, 2, {0xAA, 0x1C}},
	{0xC0, 18, {0x02, 0x46, 0x02, 0x18, 0x02, 0xB8, 0x02, 0x46, 0x02, 0x18,
			  0x02, 0xB8, 0x02, 0x46, 0x02, 0x18, 0x02, 0xB8}},

	{0xF0, 2, {0xAA, 0x10}},
	{0x65, 1, {0x01}},
	{0xD0, 1, {0xE8}},
	{0xF0, 2, {0xAA, 0x13}},
	{0xC8, 1, {0x18}},
	{0xD0, 1, {0x18}},
	{0xE0, 1, {0x18}},
	{0xE8, 1, {0x18}},

	{0xF0, 2, {0xAA, 0x15}},
	{0x65, 1, {0x02}},
	{0xB5, 1, {0x22}},

	{0xF0, 2, {0xAA, 0x1C}},
	{0xB0, 11, {0x01, 0x00, 0x0F, 0xFF, 0x0D, 0x11, 0x12, 0x2C, 0x2D, 0x2E, 0x2F}},
	{0xB1, 24, {0x03, 0xE9, 0x03, 0xE0, 0x04, 0x05, 0x03, 0xA8, 0x03, 0xA8, 0x03, 0xD0, 0x03, 0xb0, 0x03, 0xA6, 0x03, 0xC0, 0x04, 0x00, 0x04, 0x00, 0x04, 0x00}},
	{0xB2, 24, {0x03, 0xE9, 0x03, 0xE0, 0x04, 0x05, 0x03, 0xA8, 0x03, 0xA8, 0x03, 0xD0, 0x03, 0xb0, 0x03, 0xA6, 0x03, 0xC0, 0x04, 0x00, 0x04, 0x00, 0x04, 0x00}},
	{0xB3, 24, {0x03, 0xE9, 0x03, 0xE0, 0x04, 0x05, 0x03, 0xA8, 0x03, 0xA8, 0x03, 0xD0, 0x03, 0xb0, 0x03, 0xA6, 0x03, 0xC0, 0x04, 0x00, 0x04, 0x00, 0x04, 0x00}},
	{0xB4, 24, {0x03, 0xE9, 0x03, 0xE0, 0x04, 0x05, 0x03, 0xA8, 0x03, 0xA8, 0x03, 0xD0, 0x03, 0xb0, 0x03, 0xA6, 0x03, 0xC0, 0x04, 0x00, 0x04, 0x00, 0x04, 0x00}},

	{0xF0, 2, {0xAA, 0x12}},
	{0x65, 1, {0x0B}},
	{0xE9, 1, {0x00}},
	{0xFF, 2, {0x5A, 0x83}},
	{0x65, 1, {0x09}},
	{0xF7, 2, {0x10, 0x10}},
	{0xF0, 2, {0xAA, 0x12}},
	{0x65, 1, {0x07}},
	{0xB9, 7, {0xC0, 0xC0, 0xC0, 0xC0, 0x20, 0x20, 0x20}},
	{0x65, 1, {0x2E}},
	{0xB9, 4, {0x22, 0x22, 0x11, 0x01}},

	{0x11, 0, {}},
	{REGFLAG_DELAY, 120, {}},
	{0x29, 0, {}},
	{REGFLAG_DELAY, 20, {}},
};

static struct LCM_setting_table lcm_suspend_setting[] = {
	{0x28, 0, {}},
	{REGFLAG_DELAY, 10, {}},
	{0x10, 0, {}},
	{REGFLAG_DELAY, 100, {}},
};

static void push_table(struct lcm *ctx, struct LCM_setting_table *table, unsigned int count)
{
	unsigned int i, j;
	unsigned char temp[255] = {0};
	unsigned int cmd;

	for (i = 0; i < count; i++) {
		cmd = table[i].cmd;
		memset(temp, 0, sizeof(temp));
		switch (cmd) {
		case REGFLAG_DELAY:
			if (table[i].count <= 10)
				msleep(table[i].count);
			else
				msleep(table[i].count);
			break;
		case REGFLAG_END_OF_TABLE:
			break;
		default:
			temp[0] = cmd;
			for (j = 0; j < table[i].count; j++)
				temp[j+1] = table[i].para_list[j];
			lcm_dcs_write(ctx, temp, table[i].count+1);
		}
	}
}

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
	pr_info("[LCM] %s begin\n", __func__);
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay(2);
	gpiod_set_value(ctx->reset_gpio, 0);
	mdelay(2);
	gpiod_set_value(ctx->reset_gpio, 1);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	mdelay(25);
	switch (g_lcm_fresh_mode) {
	case MODE_0_FPS:
		lcm_initialization_setting[0].para_list[0] = 0x00;
		break;
	case MODE_1_FPS:
		lcm_initialization_setting[0].para_list[0] = 0x01;
		break;
	case MODE_2_FPS:
		lcm_initialization_setting[0].para_list[0] = 0x02;
		break;
	case MODE_3_FPS:
		lcm_initialization_setting[0].para_list[0] = 0x03;
		break;
	}
	push_table(ctx, lcm_initialization_setting,
		sizeof(lcm_initialization_setting) / sizeof(struct LCM_setting_table));

	if (mapped_level) {
		pr_info("[LCM] %s esd recovery, need set curr_bl=%d\n", __func__, mapped_level);
		lcm_dcs_write(ctx, bl_tb0, ARRAY_SIZE(bl_tb0));
	}

	ctx->error = 0;
	g_dim_state = 0;
	g_need_dim_enable = 0;
	pr_info("[LCM] %s end\n", __func__);
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	pr_info("[LCM] %s begin\n", __func__);

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
	pr_info("[LCM] %s begin\n", __func__);
	g_aod_enable = 0;
	push_table(ctx, lcm_suspend_setting,
		sizeof(lcm_suspend_setting) / sizeof(struct LCM_setting_table));
	ctx->error = 0;
	ctx->prepared = false;
	g_hbm_enable = 0;
	pr_info("[LCM] %s end\n", __func__);
	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;

	pr_info("[LCM] %s begin\n", __func__);
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
	pr_info("[LCM] %s end\n", __func__);
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

static const struct drm_display_mode switch_mode_144hz = {
	.clock = (int)((FRAME_WIDTH + MODE_0_HFP + HSA + HBP) * (FRAME_HEIGHT + MODE_0_VFP + VSA + VBP) * MODE_0_FPS / 1000),
	.hdisplay = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH + MODE_0_HFP,
	.hsync_end = FRAME_WIDTH + MODE_0_HFP + HSA,
	.htotal = FRAME_WIDTH + MODE_0_HFP + HSA + HBP,
	.vdisplay = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + MODE_0_VFP,
	.vsync_end = FRAME_HEIGHT + MODE_0_VFP + VSA,
	.vtotal = FRAME_HEIGHT + MODE_0_VFP + VSA + VBP,
};

static const struct drm_display_mode switch_mode_120hz = {
	.clock = (int)((FRAME_WIDTH + MODE_1_HFP + HSA + HBP) * (FRAME_HEIGHT + MODE_1_VFP + VSA + VBP) * MODE_1_FPS / 1000),
	.hdisplay = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH + MODE_1_HFP,
	.hsync_end = FRAME_WIDTH + MODE_1_HFP + HSA,
	.htotal = FRAME_WIDTH + MODE_1_HFP + HSA + HBP,
	.vdisplay = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + MODE_1_VFP,
	.vsync_end = FRAME_HEIGHT + MODE_1_VFP + VSA,
	.vtotal = FRAME_HEIGHT + MODE_1_VFP + VSA + VBP,
};

static const struct drm_display_mode switch_mode_90hz = {
	.clock = (int)((FRAME_WIDTH + MODE_2_HFP + HSA + HBP) * (FRAME_HEIGHT + MODE_2_VFP + VSA + VBP) * MODE_2_FPS / 1000),
	.hdisplay = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH + MODE_2_HFP,
	.hsync_end = FRAME_WIDTH + MODE_2_HFP + HSA,
	.htotal = FRAME_WIDTH + MODE_2_HFP + HSA + HBP,
	.vdisplay = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + MODE_2_VFP,
	.vsync_end = FRAME_HEIGHT + MODE_2_VFP + VSA,
	.vtotal = FRAME_HEIGHT + MODE_2_VFP + VSA + VBP,
};

static const struct drm_display_mode switch_mode_60hz = {
	.clock = (int)((FRAME_WIDTH + MODE_3_HFP + HSA + HBP) * (FRAME_HEIGHT + MODE_3_VFP + VSA + VBP) * MODE_3_FPS / 1000),
	.hdisplay = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH + MODE_3_HFP,
	.hsync_end = FRAME_WIDTH + MODE_3_HFP + HSA,
	.htotal = FRAME_WIDTH + MODE_3_HFP + HSA + HBP,
	.vdisplay = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + MODE_3_VFP,
	.vsync_end = FRAME_HEIGHT + MODE_3_VFP + VSA,
	.vtotal = FRAME_HEIGHT + MODE_3_VFP + VSA + VBP,
};

#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params_144hz = {
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.ssc_enable = 0,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.lcm_esd_check_table[1] = {
		.cmd = 0x66, .count = 2, .para_list[0] = 0x00, .para_list[1] = 0x00,
	},
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.vdo_per_frame_lp_enable = 1,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable                   = DSC_ENABLE,
		.ver                      = DSC_VER,
		.slice_mode               = DSC_SLICE_MODE,
		.rgb_swap                 = DSC_RGB_SWAP,
		.dsc_cfg                  = DSC_DSC_CFG,
		.rct_on                   = DSC_RCT_ON,
		.bit_per_channel          = DSC_BIT_PER_CHANNEL,
		.dsc_line_buf_depth       = DSC_DSC_LINE_BUF_DEPTH,
		.bp_enable                = DSC_BP_ENABLE,
		.bit_per_pixel            = DSC_BIT_PER_PIXEL,
		.pic_height               = FRAME_HEIGHT,
		.pic_width                = FRAME_WIDTH,
		.slice_height             = DSC_SLICE_HEIGHT,
		.slice_width              = DSC_SLICE_WIDTH,
		.chunk_size               = DSC_CHUNK_SIZE,
		.xmit_delay               = DSC_XMIT_DELAY,
		.dec_delay                = DSC_DEC_DELAY,
		.scale_value              = DSC_SCALE_VALUE,
		.increment_interval       = DSC_INCREMENT_INTERVAL,
		.decrement_interval       = DSC_DECREMENT_INTERVAL,
		.line_bpg_offset          = DSC_LINE_BPG_OFFSET,
		.nfl_bpg_offset           = DSC_NFL_BPG_OFFSET,
		.slice_bpg_offset         = DSC_SLICE_BPG_OFFSET,
		.initial_offset           = DSC_INITIAL_OFFSET,
		.final_offset             = DSC_FINAL_OFFSET,
		.flatness_minqp           = DSC_FLATNESS_MINQP,
		.flatness_maxqp           = DSC_FLATNESS_MAXQP,
		.rc_model_size            = DSC_RC_MODEL_SIZE,
		.rc_edge_factor           = DSC_RC_EDGE_FACTOR,
		.rc_quant_incr_limit0     = DSC_RC_QUANT_INCR_LIMIT0,
		.rc_quant_incr_limit1     = DSC_RC_QUANT_INCR_LIMIT1,
		.rc_tgt_offset_hi         = DSC_RC_TGT_OFFSET_HI,
		.rc_tgt_offset_lo         = DSC_RC_TGT_OFFSET_LO,
		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = rc_buf_thresh,
			.range_min_qp = range_min_qp,
			.range_max_qp = range_max_qp,
			.range_bpg_ofs = range_bpg_ofs,
			},
	},
	.change_fps_by_vfp_send_cmd = 1,
	.dyn_fps = {
		.switch_en = 0,
		.dfps_cmd_table[0] = {0, 2, {0x6C, 0x00}},
	},
	.data_rate = MODE_0_DATA_RATE,
	.tran_panel_params = &panel_driver_status,
};

static struct mtk_panel_params ext_params_120hz = {
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.ssc_enable = 0,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.lcm_esd_check_table[1] = {
		.cmd = 0x66, .count = 2, .para_list[0] = 0x00, .para_list[1] = 0x00,
	},
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.vdo_per_frame_lp_enable = 1,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable                   = DSC_ENABLE,
		.ver                      = DSC_VER,
		.slice_mode               = DSC_SLICE_MODE,
		.rgb_swap                 = DSC_RGB_SWAP,
		.dsc_cfg                  = DSC_DSC_CFG,
		.rct_on                   = DSC_RCT_ON,
		.bit_per_channel          = DSC_BIT_PER_CHANNEL,
		.dsc_line_buf_depth       = DSC_DSC_LINE_BUF_DEPTH,
		.bp_enable                = DSC_BP_ENABLE,
		.bit_per_pixel            = DSC_BIT_PER_PIXEL,
		.pic_height               = FRAME_HEIGHT,
		.pic_width                = FRAME_WIDTH,
		.slice_height             = DSC_SLICE_HEIGHT,
		.slice_width              = DSC_SLICE_WIDTH,
		.chunk_size               = DSC_CHUNK_SIZE,
		.xmit_delay               = DSC_XMIT_DELAY,
		.dec_delay                = DSC_DEC_DELAY,
		.scale_value              = DSC_SCALE_VALUE,
		.increment_interval       = DSC_INCREMENT_INTERVAL,
		.decrement_interval       = DSC_DECREMENT_INTERVAL,
		.line_bpg_offset          = DSC_LINE_BPG_OFFSET,
		.nfl_bpg_offset           = DSC_NFL_BPG_OFFSET,
		.slice_bpg_offset         = DSC_SLICE_BPG_OFFSET,
		.initial_offset           = DSC_INITIAL_OFFSET,
		.final_offset             = DSC_FINAL_OFFSET,
		.flatness_minqp           = DSC_FLATNESS_MINQP,
		.flatness_maxqp           = DSC_FLATNESS_MAXQP,
		.rc_model_size            = DSC_RC_MODEL_SIZE,
		.rc_edge_factor           = DSC_RC_EDGE_FACTOR,
		.rc_quant_incr_limit0     = DSC_RC_QUANT_INCR_LIMIT0,
		.rc_quant_incr_limit1     = DSC_RC_QUANT_INCR_LIMIT1,
		.rc_tgt_offset_hi         = DSC_RC_TGT_OFFSET_HI,
		.rc_tgt_offset_lo         = DSC_RC_TGT_OFFSET_LO,
		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = rc_buf_thresh,
			.range_min_qp = range_min_qp,
			.range_max_qp = range_max_qp,
			.range_bpg_ofs = range_bpg_ofs,
		},
	},
	.change_fps_by_vfp_send_cmd = 1,
	.dyn_fps = {
		.switch_en = 0,
		.dfps_cmd_table[0] = {0, 2, {0x6C, 0x01}},
	},
	.data_rate = MODE_1_DATA_RATE,
	.tran_panel_params = &panel_driver_status,
};

static struct mtk_panel_params ext_params_90hz = {
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.ssc_enable = 0,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.lcm_esd_check_table[1] = {
		.cmd = 0x66, .count = 2, .para_list[0] = 0x00, .para_list[1] = 0x00,
	},
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.vdo_per_frame_lp_enable = 1,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable                   = DSC_ENABLE,
		.ver                      = DSC_VER,
		.slice_mode               = DSC_SLICE_MODE,
		.rgb_swap                 = DSC_RGB_SWAP,
		.dsc_cfg                  = DSC_DSC_CFG,
		.rct_on                   = DSC_RCT_ON,
		.bit_per_channel          = DSC_BIT_PER_CHANNEL,
		.dsc_line_buf_depth       = DSC_DSC_LINE_BUF_DEPTH,
		.bp_enable                = DSC_BP_ENABLE,
		.bit_per_pixel            = DSC_BIT_PER_PIXEL,
		.pic_height               = FRAME_HEIGHT,
		.pic_width                = FRAME_WIDTH,
		.slice_height             = DSC_SLICE_HEIGHT,
		.slice_width              = DSC_SLICE_WIDTH,
		.chunk_size               = DSC_CHUNK_SIZE,
		.xmit_delay               = DSC_XMIT_DELAY,
		.dec_delay                = DSC_DEC_DELAY,
		.scale_value              = DSC_SCALE_VALUE,
		.increment_interval       = DSC_INCREMENT_INTERVAL,
		.decrement_interval       = DSC_DECREMENT_INTERVAL,
		.line_bpg_offset          = DSC_LINE_BPG_OFFSET,
		.nfl_bpg_offset           = DSC_NFL_BPG_OFFSET,
		.slice_bpg_offset         = DSC_SLICE_BPG_OFFSET,
		.initial_offset           = DSC_INITIAL_OFFSET,
		.final_offset             = DSC_FINAL_OFFSET,
		.flatness_minqp           = DSC_FLATNESS_MINQP,
		.flatness_maxqp           = DSC_FLATNESS_MAXQP,
		.rc_model_size            = DSC_RC_MODEL_SIZE,
		.rc_edge_factor           = DSC_RC_EDGE_FACTOR,
		.rc_quant_incr_limit0     = DSC_RC_QUANT_INCR_LIMIT0,
		.rc_quant_incr_limit1     = DSC_RC_QUANT_INCR_LIMIT1,
		.rc_tgt_offset_hi         = DSC_RC_TGT_OFFSET_HI,
		.rc_tgt_offset_lo         = DSC_RC_TGT_OFFSET_LO,
		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = rc_buf_thresh,
			.range_min_qp = range_min_qp,
			.range_max_qp = range_max_qp,
			.range_bpg_ofs = range_bpg_ofs,
		},
	},
	.change_fps_by_vfp_send_cmd = 1,
	.dyn_fps = {
		.switch_en = 0,
		.dfps_cmd_table[0] = {0, 2, {0x6C, 0x02}},
	},
	.data_rate = MODE_2_DATA_RATE,
	.tran_panel_params = &panel_driver_status,
};

static struct mtk_panel_params ext_params_60hz = {
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.ssc_enable = 0,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.lcm_esd_check_table[1] = {
		.cmd = 0x66, .count = 2, .para_list[0] = 0x00, .para_list[1] = 0x00,
	},
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.vdo_per_frame_lp_enable = 1,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable                   = DSC_ENABLE,
		.ver                      = DSC_VER,
		.slice_mode               = DSC_SLICE_MODE,
		.rgb_swap                 = DSC_RGB_SWAP,
		.dsc_cfg                  = DSC_DSC_CFG,
		.rct_on                   = DSC_RCT_ON,
		.bit_per_channel          = DSC_BIT_PER_CHANNEL,
		.dsc_line_buf_depth       = DSC_DSC_LINE_BUF_DEPTH,
		.bp_enable                = DSC_BP_ENABLE,
		.bit_per_pixel            = DSC_BIT_PER_PIXEL,
		.pic_height               = FRAME_HEIGHT,
		.pic_width                = FRAME_WIDTH,
		.slice_height             = DSC_SLICE_HEIGHT,
		.slice_width              = DSC_SLICE_WIDTH,
		.chunk_size               = DSC_CHUNK_SIZE,
		.xmit_delay               = DSC_XMIT_DELAY,
		.dec_delay                = DSC_DEC_DELAY,
		.scale_value              = DSC_SCALE_VALUE,
		.increment_interval       = DSC_INCREMENT_INTERVAL,
		.decrement_interval       = DSC_DECREMENT_INTERVAL,
		.line_bpg_offset          = DSC_LINE_BPG_OFFSET,
		.nfl_bpg_offset           = DSC_NFL_BPG_OFFSET,
		.slice_bpg_offset         = DSC_SLICE_BPG_OFFSET,
		.initial_offset           = DSC_INITIAL_OFFSET,
		.final_offset             = DSC_FINAL_OFFSET,
		.flatness_minqp           = DSC_FLATNESS_MINQP,
		.flatness_maxqp           = DSC_FLATNESS_MAXQP,
		.rc_model_size            = DSC_RC_MODEL_SIZE,
		.rc_edge_factor           = DSC_RC_EDGE_FACTOR,
		.rc_quant_incr_limit0     = DSC_RC_QUANT_INCR_LIMIT0,
		.rc_quant_incr_limit1     = DSC_RC_QUANT_INCR_LIMIT1,
		.rc_tgt_offset_hi         = DSC_RC_TGT_OFFSET_HI,
		.rc_tgt_offset_lo         = DSC_RC_TGT_OFFSET_LO,
		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = rc_buf_thresh,
			.range_min_qp = range_min_qp,
			.range_max_qp = range_max_qp,
			.range_bpg_ofs = range_bpg_ofs,
			},
	},
	.change_fps_by_vfp_send_cmd = 1,
	.dyn_fps = {
		.switch_en = 0,
		.dfps_cmd_table[0] = {0, 2, {0x6C, 0x03}},
	},
	.data_rate = MODE_3_DATA_RATE,
	.tran_panel_params = &panel_driver_status,
};

static unsigned int ghbm_state;
static unsigned int dim_speed_change;
static struct LCM_setting_table dim_frame_speed[] = {
	{0xF0, 2, {0xAA, 0x13}},
	{0xC8, 1, {0x08}},
	{0xD0, 1, {0x08}},
	{0xE0, 1, {0x08}},
	{0xE8, 1, {0x08}},
};
static unsigned int vrefn2_en;
static struct LCM_setting_table vrefn2_set[] = {
	{0xF0, 2, {0xAA, 0x12}},
	{0xBE, 2, {0x01, 0x05}},
};
static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb,
	void *handle, unsigned int level)
{
	if (!cb) {
		pr_info("[LCM] %s cb is null\n", __func__);
		return -1;
	}

	if (level > 511)
		level = 511;

	if (level <= 255) {
		mapped_level = level * 3515 / 255;
		if (ghbm_state) {
			ghbm_state = ghbm_state - 1;
		}
	} else if (255 < level && level <= 511) {
		mapped_level = 3515 + (level - 256) * (4095-3515) / 255;
		ghbm_state = 2;
	}

	if ((ghbm_state == 2) && (dim_speed_change == 0)) {
		pr_info("%s ghbm_state=%d, dim_speed_change=%d set 0x08.\n", __func__, ghbm_state, dim_speed_change);
		dim_frame_speed[1].para_list[0] = 0x08;
		dim_frame_speed[2].para_list[0] = 0x08;
		dim_frame_speed[3].para_list[0] = 0x08;
		dim_frame_speed[4].para_list[0] = 0x08;
		push_table_cb(dsi, cb, handle, dim_frame_speed, sizeof(dim_frame_speed) / sizeof(struct LCM_setting_table));
		dim_speed_change = 1;
	} else if ((ghbm_state == 0) && (dim_speed_change == 1) && (mapped_level > 0x12F)) {
		pr_info("%s ghbm_state=%d, dim_speed_change=%d set 0x18.\n", __func__, ghbm_state, dim_speed_change);
		dim_frame_speed[1].para_list[0] = 0x18;
		dim_frame_speed[2].para_list[0] = 0x18;
		dim_frame_speed[3].para_list[0] = 0x18;
		dim_frame_speed[4].para_list[0] = 0x18;
		push_table_cb(dsi, cb, handle, dim_frame_speed, sizeof(dim_frame_speed) / sizeof(struct LCM_setting_table));
		dim_speed_change = 0;
	} else if ((ghbm_state == 0) && (dim_speed_change == 0) && (mapped_level < 0x12F) && (mapped_level > 0)) {
		pr_info("%s ghbm_state=%d, dim_speed_change=%d set 0x28.\n", __func__, ghbm_state, dim_speed_change);
		dim_frame_speed[1].para_list[0] = 0x28;
		dim_frame_speed[2].para_list[0] = 0x28;
		dim_frame_speed[3].para_list[0] = 0x28;
		dim_frame_speed[4].para_list[0] = 0x28;
		push_table_cb(dsi, cb, handle, dim_frame_speed, sizeof(dim_frame_speed) / sizeof(struct LCM_setting_table));
		dim_speed_change = 1;
	}

	bl_tb0[1] = ((mapped_level >> 8) & 0x0f);
	bl_tb0[2] = (mapped_level & 0xff);

	if ((g_dim_state == 0) && (g_need_dim_enable == 1)) {
		bl_dim[1] = 0x28;
		cb(dsi, handle, bl_dim, ARRAY_SIZE(bl_dim));
		g_dim_state =  1;
	}

	if (g_aod_setbacklight == 1) {
		g_aod_setbacklight = 0;
	} else {
		g_need_dim_enable = 1;
	}

	if ((mapped_level == 0) || (panel_driver_status.dimming_status == 0)) {
		bl_dim[1] = 0x20;
		cb(dsi, handle, bl_dim, ARRAY_SIZE(bl_dim));
		g_dim_state = 0;
		g_need_dim_enable = 0;
	} else {
		last_mapped_level = mapped_level;
	}

	switch (mapped_level) {
	case 0:
		if (vrefn2_en) {
			pr_info("[LCM] %s vrefn2_en=%d mapped_level=%d\n", __func__, vrefn2_en, mapped_level);
			push_table_cb(dsi, cb, handle, vrefn2_set, sizeof(vrefn2_set) / sizeof(struct LCM_setting_table));
		}
		break;
	case 0xFFE:
	case 0xFFF:
		vrefn2_en = 1;
		break;
	default:
		vrefn2_en = 0;
		break;
	}

	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));
	pr_info("[LCM] %s level = %d, mapped_level = %d, dim_state = %d\n", __func__, level, mapped_level, g_dim_state);

	return 0;
}

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
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id(connector, mode);

	pr_info("[LCM] %s FPS form %d to %d\n", __func__, g_lcm_fresh_mode, drm_mode_vrefresh(m));

	if (drm_mode_vrefresh(m) == MODE_0_FPS)
		ext->params = &ext_params_144hz;
	else if (drm_mode_vrefresh(m) == MODE_1_FPS)
		ext->params = &ext_params_120hz;
	else if (drm_mode_vrefresh(m) == MODE_2_FPS)
		ext->params = &ext_params_90hz;
	else if (drm_mode_vrefresh(m) == MODE_3_FPS)
		ext->params = &ext_params_60hz;
	else
		ret = 1;

	g_lcm_fresh_mode = drm_mode_vrefresh(m);
	return ret;
}

static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);

	pr_info("[LCM] %s begin\n", __func__);
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, on);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	pr_info("[LCM] %s end\n", __func__);
	return 0;
}

extern unsigned int jiffies_to_msecs(const unsigned long j);
static unsigned long lcm_unprepare_time;
static unsigned long lcm_prepare_time;
static int panel_ext_lcm_power_set(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret = 0;

	pr_info("[LCM] %s status=%d begin\n", __func__, on);

	if (IS_ERR_OR_NULL(ctx->dvdd_regulator)) {
		ctx->dvdd_regulator = devm_regulator_get(ctx->dev, "dvdd");
		if (IS_ERR_OR_NULL(ctx->dvdd_regulator)) {
			ret = PTR_ERR(ctx->dvdd_regulator);
			dev_err(ctx->dev, "<%s,%d> lcm dvdd get fail!ret[%d].\n", __func__, __LINE__, ret);
			return 0;
		}
	}

	if (on) {
		unsigned long time_diff = 0;

		lcm_prepare_time = jiffies_to_msecs(jiffies);
		time_diff = lcm_prepare_time - lcm_unprepare_time;
		if (time_diff < 120)
			mdelay(120 - time_diff);


		ctx->vci_gpio = devm_gpiod_get(ctx->dev, "vci", GPIOD_OUT_HIGH);
		gpiod_set_value(ctx->vci_gpio, 1);
		devm_gpiod_put(ctx->dev, ctx->vci_gpio);
		mdelay(2);

		ret = regulator_set_voltage(ctx->dvdd_regulator, 1248000, 1248000);
		if (ret < 0)
			dev_err(ctx->dev, "<%s:%d>set lcm dvdd vol error!ret[%d]\n", __func__, __LINE__, ret);
		ret = regulator_enable(ctx->dvdd_regulator);
		if (ret < 0)
			dev_err(ctx->dev, "<%s:%d>enable lcm dvdd error!ret[%d]\n", __func__, __LINE__, ret);
		mdelay(12);
	} else {
		ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
		gpiod_set_value(ctx->reset_gpio, 0);
		devm_gpiod_put(ctx->dev, ctx->reset_gpio);
		mdelay(2);

		ret = regulator_disable(ctx->dvdd_regulator);
		if (ret < 0)
			dev_err(ctx->dev, "<%s:%d>disable lcm dvdd error!ret[%d]\n", __func__, __LINE__, ret);
		if (regulator_is_enabled(ctx->dvdd_regulator))
			dev_err(ctx->dev, "<%s:%d>after disable, lcm dvdd still enable!ret[%d]\n", __func__, __LINE__, ret);
		mdelay(2);

		ctx->vci_gpio = devm_gpiod_get(ctx->dev, "vci", GPIOD_OUT_HIGH);
		gpiod_set_value(ctx->vci_gpio, 0);
		devm_gpiod_put(ctx->dev, ctx->vci_gpio);
		mdelay(2);

		lcm_unprepare_time = jiffies_to_msecs(jiffies);
	}
	pr_info("[LCM] %s end\n", __func__);
	return 0;
}

static int panel_doze_enable(struct drm_panel *panel,
			void *dsi, dcs_write_gce cb, void *handle)
{
	pr_info("[LCM] %s begin\n", __func__);
	g_aod_enable = 1;
	g_aod_setbacklight = 1;
	pr_info("[LCM] %s end\n", __func__);
	return 0;
}

static int panel_doze_disable(struct drm_panel *panel,
			void *dsi, dcs_write_gce cb, void *handle)
{
	pr_info("[LCM] %s begin\n", __func__);
	g_aod_enable = 0;
	pr_info("[LCM] %s end\n", __func__);
	return 0;
}

#if IS_ENABLED(CONFIG_TRANSSION_DOZE_BRIGHTNESS_SUPPORT)
static int panel_set_aod_light_mode(void *dsi, dcs_write_gce cb,
	void *handle, unsigned int level)
{
	unsigned int aod_mapped_level = 0;

	switch (level) {
	case 2:
		aod_mapped_level = 0x30;
		break;
	case 11:
		aod_mapped_level = 0x49;
		break;
	case 23:
		aod_mapped_level = 0x12D;
		break;
	default:
		aod_mapped_level = 0x12D;
		break;
	}

	pr_info("[LCM] %s level is %u,aod_mapped_level is %d\n", __func__, level, aod_mapped_level);

	bl_tb0[1] = ((aod_mapped_level >> 8) & 0x0f);
	bl_tb0[2] = (aod_mapped_level & 0xff);
	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));

	g_aod_setbacklight = 1;
	mapped_level = aod_mapped_level;

	return 0;
}
#endif

static int panel_ata_check(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	unsigned char data[3] = {0x00, 0x00, 0x00};
	unsigned char id[3] = {0x9c, 0x02, 0x00};
	ssize_t ret;

	ret = mipi_dsi_dcs_read(dsi, 0x0a, data, 3);
	if (ret < 0) {
		pr_err("%s error\n", __func__);
		return 0;
	}
	printk("ATA read data %x %x %x\n", data[0], data[1], data[2]);
	if (data[0] == id[0])
		return 1;
	printk("ATA expect read data is %x %x %x\n",
			id[0], id[1], id[2]);
	return 0;
}


static unsigned int alpha_gain[401][2] = {
	{0x00, 0x0C}, {0x01, 0x4D}, {0x02, 0x22}, {0x02, 0x52}, {0x02, 0x76}, {0x02, 0x9F}, {0x02, 0xBF}, {0x02, 0xE3}, {0x02, 0xFB}, {0x03, 0x13},
	{0x03, 0x27}, {0x03, 0x3F}, {0x03, 0x57}, {0x03, 0x73}, {0x03, 0x88}, {0x03, 0xA0}, {0x03, 0xC0}, {0x03, 0xD8}, {0x03, 0xFC}, {0x04, 0x18},
	{0x04, 0x38}, {0x04, 0x54}, {0x04, 0x74}, {0x04, 0x89}, {0x04, 0x95}, {0x04, 0xA5}, {0x04, 0xB1}, {0x04, 0xC5}, {0x04, 0xD1}, {0x04, 0xDD},
	{0x04, 0xED}, {0x04, 0xF9}, {0x05, 0x0D}, {0x05, 0x19}, {0x05, 0x29}, {0x05, 0x3D}, {0x05, 0x49}, {0x05, 0x5D}, {0x05, 0x69}, {0x05, 0x7D},
	{0x05, 0x8A}, {0x05, 0x96}, {0x05, 0xAE}, {0x05, 0xBA}, {0x05, 0xCE}, {0x05, 0xDE}, {0x05, 0xEE}, {0x06, 0x02}, {0x06, 0x12}, {0x06, 0x26},
	{0x06, 0x32}, {0x06, 0x46}, {0x06, 0x56}, {0x06, 0x66}, {0x06, 0x7A}, {0x06, 0x8B}, {0x06, 0xA3}, {0x06, 0xB3}, {0x06, 0xC3}, {0x06, 0xCF},
	{0x06, 0xD7}, {0x06, 0xE3}, {0x06, 0xEB}, {0x06, 0xF7}, {0x06, 0xFF}, {0x07, 0x07}, {0x07, 0x13}, {0x07, 0x1B}, {0x07, 0x27}, {0x07, 0x2F},
	{0x07, 0x37}, {0x07, 0x43}, {0x07, 0x4B}, {0x07, 0x57}, {0x07, 0x5B}, {0x07, 0x67}, {0x07, 0x73}, {0x07, 0x7B}, {0x07, 0x84}, {0x07, 0x90},
	{0x07, 0x98}, {0x07, 0xA8}, {0x07, 0xAC}, {0x07, 0xB8}, {0x07, 0xC4}, {0x07, 0xCC}, {0x07, 0xD4}, {0x07, 0xDC}, {0x07, 0xE8}, {0x07, 0xF0},
	{0x07, 0xFC}, {0x08, 0x04}, {0x08, 0x10}, {0x08, 0x20}, {0x08, 0x24}, {0x08, 0x30}, {0x08, 0x38}, {0x08, 0x40}, {0x08, 0x4C}, {0x08, 0x54},
	{0x08, 0x64}, {0x08, 0x6C}, {0x08, 0x70}, {0x08, 0x81}, {0x08, 0x89}, {0x08, 0x91}, {0x08, 0x9D}, {0x08, 0xA5}, {0x08, 0xB1}, {0x08, 0xB5},
	{0x08, 0xC1}, {0x08, 0xC9}, {0x08, 0xD5}, {0x08, 0xDD}, {0x08, 0xE5}, {0x08, 0xF1}, {0x08, 0xFD}, {0x09, 0x05}, {0x09, 0x0D}, {0x09, 0x15},
	{0x09, 0x21}, {0x09, 0x29}, {0x09, 0x35}, {0x09, 0x41}, {0x09, 0x49}, {0x09, 0x51}, {0x09, 0x5D}, {0x09, 0x69}, {0x09, 0x71}, {0x09, 0x75},
	{0x09, 0x86}, {0x09, 0x8A}, {0x09, 0x9A}, {0x09, 0xA2}, {0x09, 0xAA}, {0x09, 0xB6}, {0x09, 0xC2}, {0x09, 0xCA}, {0x09, 0xD2}, {0x09, 0xDE},
	{0x09, 0xEA}, {0x09, 0xF2}, {0x09, 0xFE}, {0x0A, 0x10}, {0x0A, 0x18}, {0x0A, 0x1F}, {0x0A, 0x24}, {0x0A, 0x2C}, {0x0A, 0x34}, {0x0A, 0x3A},
	{0x0A, 0x42}, {0x0A, 0x46}, {0x0A, 0x4A}, {0x0A, 0x55}, {0x0A, 0x5A}, {0x0A, 0x5F}, {0x0A, 0x66}, {0x0A, 0x6E}, {0x0A, 0x6E}, {0x0A, 0x7A},
	{0x0A, 0x7E}, {0x0A, 0x87}, {0x0A, 0x8F}, {0x0A, 0x93}, {0x0A, 0x9B}, {0x0A, 0x9F}, {0x0A, 0xA7}, {0x0A, 0xAF}, {0x0A, 0xB3}, {0x0A, 0xBB},
	{0x0A, 0xBF}, {0x0A, 0xC7}, {0x0A, 0xCF}, {0x0A, 0xD3}, {0x0A, 0xDB}, {0x0A, 0xDF}, {0x0A, 0xE7}, {0x0A, 0xEB}, {0x0A, 0xF3}, {0x0A, 0xFB},
	{0x0A, 0xFF}, {0x0B, 0x09}, {0x0B, 0x0B}, {0x0B, 0x17}, {0x0B, 0x1F}, {0x0B, 0x23}, {0x0B, 0x27}, {0x0B, 0x2B}, {0x0B, 0x33}, {0x0B, 0x3F},
	{0x0B, 0x45}, {0x0B, 0x4D}, {0x0B, 0x54}, {0x0B, 0x57}, {0x0B, 0x5A}, {0x0B, 0x5F}, {0x0B, 0x67}, {0x0B, 0x6B}, {0x0B, 0x79}, {0x0B, 0x7E},
	{0x0B, 0x84}, {0x0B, 0x88}, {0x0B, 0x8C}, {0x0B, 0x94}, {0x0B, 0x98}, {0x0B, 0xA0}, {0x0B, 0xA8}, {0x0B, 0xB4}, {0x0B, 0xB9}, {0x0B, 0xBD},
	{0x0B, 0xC4}, {0x0B, 0xC5}, {0x0B, 0xC8}, {0x0B, 0xD8}, {0x0B, 0xDD}, {0x0B, 0xE4}, {0x0B, 0xE7}, {0x0B, 0xF2}, {0x0B, 0xF8}, {0x0B, 0xFC},
	{0x0C, 0x02}, {0x0C, 0x06}, {0x0C, 0x10}, {0x0C, 0x17}, {0x0C, 0x1E}, {0x0C, 0x21}, {0x0C, 0x24}, {0x0C, 0x2C}, {0x0C, 0x33}, {0x0C, 0x3B},
	{0x0C, 0x43}, {0x0C, 0x47}, {0x0C, 0x49}, {0x0C, 0x4C}, {0x0C, 0x56}, {0x0C, 0x5E}, {0x0C, 0x60}, {0x0C, 0x68}, {0x0C, 0x6C}, {0x0C, 0x70},
	{0x0C, 0x7D}, {0x0C, 0x86}, {0x0C, 0x89}, {0x0C, 0x8A}, {0x0C, 0x93}, {0x0C, 0x9A}, {0x0C, 0x9F}, {0x0C, 0xA6}, {0x0C, 0xAB}, {0x0C, 0xAE},
	{0x0C, 0xB7}, {0x0C, 0xBA}, {0x0C, 0xC5}, {0x0C, 0xCA}, {0x0C, 0xD5}, {0x0C, 0xD6}, {0x0C, 0xDA}, {0x0C, 0xE0}, {0x0C, 0xE2}, {0x0C, 0xE9},
	{0x0C, 0xF0}, {0x0C, 0xF5}, {0x0C, 0xF9}, {0x0C, 0xFE}, {0x0D, 0x07}, {0x0D, 0x0E}, {0x0D, 0x18}, {0x0D, 0x1F}, {0x0D, 0x22}, {0x0D, 0x27},
	{0x0D, 0x2C}, {0x0D, 0x36}, {0x0D, 0x3C}, {0x0D, 0x3E}, {0x0D, 0x41}, {0x0D, 0x52}, {0x0D, 0x57}, {0x0D, 0x59}, {0x0D, 0x5F}, {0x0D, 0x66},
	{0x0D, 0x69}, {0x0D, 0x6F}, {0x0D, 0x76}, {0x0D, 0x78}, {0x0D, 0x7E}, {0x0D, 0x83}, {0x0D, 0x8A}, {0x0D, 0x94}, {0x0D, 0x98}, {0x0D, 0x9A},
	{0x0D, 0x9E}, {0x0D, 0xB2}, {0x0D, 0xB8}, {0x0D, 0xBA}, {0x0D, 0xBC}, {0x0D, 0xBE}, {0x0D, 0xC6}, {0x0D, 0xCB}, {0x0D, 0xD2}, {0x0D, 0xD6},
	{0x0D, 0xDA}, {0x0D, 0xE2}, {0x0D, 0xE8}, {0x0D, 0xF0}, {0x0D, 0xF5}, {0x0D, 0xFA}, {0x0D, 0xFE}, {0x0E, 0x02}, {0x0E, 0x0A}, {0x0E, 0x0D},
	{0x0E, 0x12}, {0x0E, 0x1A}, {0x0E, 0x21}, {0x0E, 0x25}, {0x0E, 0x28}, {0x0E, 0x30}, {0x0E, 0x32}, {0x0E, 0x3C}, {0x0E, 0x45}, {0x0E, 0x4C},
	{0x0E, 0x54}, {0x0E, 0x5A}, {0x0E, 0x5D}, {0x0E, 0x5E}, {0x0E, 0x61}, {0x0E, 0x6B}, {0x0E, 0x6F}, {0x0E, 0x7C}, {0x0E, 0x80}, {0x0E, 0x8A},
	{0x0E, 0x8C}, {0x0E, 0x8E}, {0x0E, 0x98}, {0x0E, 0x9D}, {0x0E, 0xA2}, {0x0E, 0xA5}, {0x0E, 0xA8}, {0x0E, 0xAF}, {0x0E, 0xB7}, {0x0E, 0xBD},
	{0x0E, 0xC4}, {0x0E, 0xC8}, {0x0E, 0xCB}, {0x0E, 0xCF}, {0x0E, 0xDA}, {0x0E, 0xDD}, {0x0E, 0xE4}, {0x0E, 0xE9}, {0x0E, 0xED}, {0x0E, 0xF4},
	{0x0E, 0xFA}, {0x0E, 0xFD}, {0x0E, 0xFF}, {0x0F, 0x05}, {0x0F, 0x0D}, {0x0F, 0x14}, {0x0F, 0x1D}, {0x0F, 0x20}, {0x0F, 0x28}, {0x0F, 0x2F},
	{0x0F, 0x33}, {0x0F, 0x37}, {0x0F, 0x3D}, {0x0F, 0x40}, {0x0F, 0x48}, {0x0F, 0x4F}, {0x0F, 0x55}, {0x0F, 0x5F}, {0x0F, 0x63}, {0x0F, 0x67},
	{0x0F, 0x6B}, {0x0F, 0x74}, {0x0F, 0x77}, {0x0F, 0x7D}, {0x0F, 0x82}, {0x0F, 0x88}, {0x0F, 0x8C}, {0x0F, 0x92}, {0x0F, 0x9C}, {0x0F, 0xA2},
	{0x0F, 0xA5}, {0x0F, 0xA8}, {0x0F, 0xAA}, {0x0F, 0xB0}, {0x0F, 0xB4}, {0x0F, 0xB6}, {0x0F, 0xBA}, {0x0F, 0xC0}, {0x0F, 0xCE}, {0x0F, 0xD4},
	{0x0F, 0xD7}, {0x0F, 0xDD}, {0x0F, 0xE0}, {0x0F, 0xE5}, {0x0F, 0xE9}, {0x0F, 0xF2}, {0x0F, 0xF7}, {0x0F, 0xF9}, {0x0F, 0xFB}, {0x0F, 0xFD},
	{0x10, 0x00}
};

static struct LCM_setting_table hbm_mode_enter_setting[] = {
	{0xF0, 2, {0xAA, 0x13}},
	{0xC6, 1, {0x01}},
	{0x63, 4, {0x10, 0x00, 0x0D, 0xBB}},
	{0x62, 1, {0x03}},
};

static struct LCM_setting_table hbm_mode_exit_setting[] = {
	{0x62, 1, {0x00}},
};

static int panel_hbm_set_cmdq(struct drm_panel *panel, void *dsi,
			dcs_write_gce cb, void *handle, bool en)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!cb) {
		pr_info("[LCM] %s cb is null\n", __func__);
		return -1;
	}

	if (ctx->hbm_en == en) {
		pr_info("[LCM] %s FPS=%dHz, already en=%d skip, mapped_level=%d, aod=%d\n", __func__, g_lcm_fresh_mode, en, mapped_level, g_aod_enable);
		goto done;
	}

	pr_info("[LCM] %s FPS=%dHz en=%d, mapped_level=%d, aod=%d\n", __func__, g_lcm_fresh_mode, en, mapped_level, g_aod_enable);

	if (en) {
		if (mapped_level <= 0x190) {
			hbm_mode_enter_setting[2].para_list[0] = alpha_gain[mapped_level][0];
			hbm_mode_enter_setting[2].para_list[1] = alpha_gain[mapped_level][1];
			hbm_mode_enter_setting[2].para_list[2] = 0x01;
			hbm_mode_enter_setting[2].para_list[3] = 0x91;
		} else {
			hbm_mode_enter_setting[2].para_list[0] = 0x10;
			hbm_mode_enter_setting[2].para_list[1] = 0x00;
			hbm_mode_enter_setting[2].para_list[2] = ((mapped_level >> 8) & 0x0f);
			hbm_mode_enter_setting[2].para_list[3] = (mapped_level & 0xff);
		}
		push_table_cb(dsi, cb, handle, hbm_mode_enter_setting,
			sizeof(hbm_mode_enter_setting) / sizeof(struct LCM_setting_table));
	} else {
		push_table_cb(dsi, cb, handle, hbm_mode_exit_setting,
			sizeof(hbm_mode_exit_setting) / sizeof(struct LCM_setting_table));
	}

	pr_info("[LCM] %s HBM_CMD=0x%x, HBM_PARA=0x%x,0x%x,0x%x,0x%x\n", __func__,
		    hbm_mode_enter_setting[2].cmd,
			hbm_mode_enter_setting[2].para_list[0],
			hbm_mode_enter_setting[2].para_list[1],
			hbm_mode_enter_setting[2].para_list[2],
			hbm_mode_enter_setting[2].para_list[3]);

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
		pr_info("[LCM] %s FPS=%dHz, already hbm\n", __func__, g_lcm_fresh_mode);
		return 0;
	}

	pr_info("[LCM] %s FPS=%dHz en=%d, mapped_level=%d, aod=%d\n", __func__, g_lcm_fresh_mode, en, mapped_level, g_aod_enable);

	if (en) {
		if (mapped_level <= 0x190) {
			hbm_mode_enter_setting[2].para_list[0] = alpha_gain[mapped_level][0];
			hbm_mode_enter_setting[2].para_list[1] = alpha_gain[mapped_level][1];
			hbm_mode_enter_setting[2].para_list[2] = 0x01;
			hbm_mode_enter_setting[2].para_list[3] = 0x91;
		} else {
			hbm_mode_enter_setting[2].para_list[0] = 0x10;
			hbm_mode_enter_setting[2].para_list[1] = 0x00;
			hbm_mode_enter_setting[2].para_list[2] = ((mapped_level >> 8) & 0x0f);
			hbm_mode_enter_setting[2].para_list[3] = (mapped_level & 0xff);
		}
		push_table_cb(dsi, cb, handle, hbm_mode_enter_setting,
			sizeof(hbm_mode_enter_setting) / sizeof(struct LCM_setting_table));
	} else {
		push_table_cb(dsi, cb, handle, hbm_mode_exit_setting,
			sizeof(hbm_mode_exit_setting) / sizeof(struct LCM_setting_table));
	}

	pr_info("[LCM] %s HBM_CMD=0x%x, HBM_PARA=0x%x,0x%x,0x%x,0x%x\n", __func__,
		    hbm_mode_enter_setting[2].cmd,
			hbm_mode_enter_setting[2].para_list[0],
			hbm_mode_enter_setting[2].para_list[1],
			hbm_mode_enter_setting[2].para_list[2],
			hbm_mode_enter_setting[2].para_list[3]);

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

static struct mtk_panel_funcs ext_funcs = {
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.ext_param_set = mtk_panel_ext_param_set,
	.ata_check = panel_ata_check,
	.reset = panel_ext_reset,
	.lcm_power_set = panel_ext_lcm_power_set,
	.hbm_set_cmdq = panel_hbm_set_cmdq,
	.hbm_set_cmdq_switch = panel_hbm_set_cmdq_switch,
	.hbm_get_state = panel_hbm_get_state,
	.hbm_get_wait_state = panel_hbm_get_wait_state,
	.hbm_set_wait_state = panel_hbm_set_wait_state,
	.doze_enable = panel_doze_enable,
	.doze_disable = panel_doze_disable,
#if IS_ENABLED(CONFIG_TRANSSION_DOZE_BRIGHTNESS_SUPPORT)
	.set_aod_light_mode = panel_set_aod_light_mode,
#endif
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

static int lcm_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct drm_display_mode *mode_60hz;
	struct drm_display_mode *mode_90hz;
	struct drm_display_mode *mode_120hz;
	struct drm_display_mode *mode_144hz;

	pr_info("[LCM] %s begin\n", __func__);

	mode_144hz = drm_mode_duplicate(connector->dev, &switch_mode_144hz);
	if (!mode_144hz) {
		dev_dbg(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			switch_mode_144hz.hdisplay,
			switch_mode_144hz.vdisplay,
			drm_mode_vrefresh(&switch_mode_144hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode_144hz);
	mode_144hz->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode_144hz);

	mode_120hz = drm_mode_duplicate(connector->dev, &switch_mode_120hz);
	if (!mode_120hz) {
		dev_dbg(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			switch_mode_120hz.hdisplay,
			switch_mode_120hz.vdisplay,
			drm_mode_vrefresh(&switch_mode_120hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode_120hz);
	mode_120hz->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode_120hz);

	mode_90hz = drm_mode_duplicate(connector->dev, &switch_mode_90hz);
	if (!mode_90hz) {
		dev_dbg(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			switch_mode_90hz.hdisplay,
			switch_mode_90hz.vdisplay,
			drm_mode_vrefresh(&switch_mode_90hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode_90hz);
	mode_90hz->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode_90hz);

	mode_60hz = drm_mode_duplicate(connector->dev, &switch_mode_60hz);
	if (!mode_60hz) {
		dev_dbg(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			switch_mode_60hz.hdisplay,
			switch_mode_60hz.vdisplay,
			drm_mode_vrefresh(&switch_mode_60hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode_60hz);
	mode_60hz->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode_60hz);

	connector->display_info.width_mm = 70;
	connector->display_info.height_mm = 158;
	pr_info("[LCM] %s end\n", __func__);
	return 4;
}
static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable = lcm_disable,
	.unprepare = lcm_unprepare,
	.prepare = lcm_prepare,
	.enable = lcm_enable,
	.get_modes = lcm_get_modes,
};

static struct lcm *ctx_work;
static void lcm_regulator_power_init(struct work_struct *lcm_work)
{
	int ret = 0;

	pr_info("[LCM]%s start\n", __func__);
	ctx_work->dvdd_regulator = devm_regulator_get(ctx_work->dev, "dvdd");
	if (IS_ERR_OR_NULL(ctx_work->dvdd_regulator)) {
		pr_info("<%s,%d> lcm dvdd regulator get fail!\n", __func__, __LINE__);
		return;
	}
	ret = regulator_set_voltage(ctx_work->dvdd_regulator, 1248000, 1248000);
	if (ret < 0)
		pr_info("<%s,%d> lcm dvdd set vol fail!ret[%d]\n", __func__, __LINE__, ret);
	ret = regulator_enable(ctx_work->dvdd_regulator);
	if (ret < 0)
		pr_info("<%s,%d> lcm dvdd enable error!ret[%d]\n", __func__, __LINE__, ret);
}
static DECLARE_DELAYED_WORK(lcm_regulator_work, lcm_regulator_power_init);

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct lcm *ctx;
	struct device_node *backlight;
	int ret;
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;
	#if IS_ENABLED(CONFIG_TRANSSION_DOZE_BRIGHTNESS_SUPPORT)
	unsigned int doze_backlight[] = {0, 0, 0, 0};
	#endif

	pr_info("[LCM] %s begin\n", __func__);

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
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			MIPI_DSI_MODE_NO_EOT_PACKET;
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

	ctx->vci_gpio = devm_gpiod_get(dev, "vci", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vci_gpio)) {
		dev_dbg(dev, "cannot get vci-gpios %ld\n", PTR_ERR(ctx->vci_gpio));
		return PTR_ERR(ctx->vci_gpio);
	}
	devm_gpiod_put(dev, ctx->vci_gpio);

	ctx_work = ctx;
	schedule_delayed_work(&lcm_regulator_work, msecs_to_jiffies(10000));

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "cannot get reset-gpios %ld\n", PTR_ERR(ctx->reset_gpio));
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
	ret = mtk_panel_ext_create(dev, &ext_params_144hz, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;
#endif

	pr_info("[LCM] %s end\n", __func__);
	return ret;
}

static void lcm_remove(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);
#if defined(CONFIG_MTK_PANEL_EXT)
	struct mtk_panel_ctx *ext_ctx = find_panel_ctx(&ctx->panel);
#endif
	pr_info("[LCM] %s begin\n", __func__);
	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_detach(ext_ctx);
	mtk_panel_remove(ext_ctx);
#endif
	return;
}

static void lcm_shutdown(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);

	pr_info("[LCM] %s ctx->prepared=%d, begin\n", __func__, ctx->prepared);

	if (ctx->prepared) {
		lcm_dcs_write_seq_static(ctx, 0xF0, 0xAA, 0x10);
		lcm_dcs_write_seq_static(ctx, 0x65, 0x01);
		lcm_dcs_write_seq_static(ctx, 0xD0, 0x68);
		lcm_dcs_write_seq_static(ctx, 0x53, 0x20);
		mdelay(18);
		lcm_dcs_write_seq_static(ctx, 0x51, 0x00, 0x00);
		mdelay(18);
	}
}

static const struct of_device_id lcm_of_match[] = {
	{
		.compatible = "vtdr6126a,fhdp1224x2720,dsi,vdo,tm,144hz,lj8",
	},
	{} };
MODULE_DEVICE_TABLE(of, lcm_of_match);
static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.shutdown = lcm_shutdown,
	.driver = {
			.name = "vtdr6126a_fhdp_dsi_vdo_tm_144hz_lj8",
			.owner = THIS_MODULE,
			.of_match_table = lcm_of_match,
		},
};
module_mipi_dsi_driver(lcm_driver);
MODULE_AUTHOR("Transsion Inc.");
MODULE_DESCRIPTION("transsion, panel driver");
MODULE_LICENSE("GPL v2");
