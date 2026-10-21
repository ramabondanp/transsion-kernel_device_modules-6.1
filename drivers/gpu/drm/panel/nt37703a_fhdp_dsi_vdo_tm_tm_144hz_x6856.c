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
#define REGFLAG_DELAY           0xFFFC
#define REGFLAG_UDELAY          0xFFFB
#define REGFLAG_END_OF_TABLE    0xFFFD
#define REGFLAG_RESET_LOW       0xFFFE
#define REGFLAG_RESET_HIGH      0xFFFF
#define PANEL_CLOCK             450000
#define FRAME_WIDTH                 1080
#define FRAME_HEIGHT                2436
#define PHYSICAL_WIDTH              69840
#define PHYSICAL_HEIGHT             157510

#define HSA                         8
#define HBP                         16
#define VSA                         2
#define VBP                         22

#define MODE_0_FPS                  60
#define MODE_0_VFP                  2580
#define MODE_0_HFP                  134
#define MODE_0_DATA_RATE		1080

#define MODE_1_FPS                  120
#define MODE_1_VFP                  60
#define MODE_1_HFP                  134
#define MODE_1_DATA_RATE		1080

#define MODE_2_FPS                  144
#define MODE_2_VFP                  68
#define MODE_2_HFP                  36
#define MODE_2_DATA_RATE		1080

#define DSC_ENABLE                  1
#define DSC_VER                     17
#define DSC_SLICE_MODE              1
#define DSC_RGB_SWAP                0
#define DSC_DSC_CFG                 40
#define DSC_RCT_ON                  1
#define DSC_BIT_PER_CHANNEL         10
#define DSC_DSC_LINE_BUF_DEPTH      11
#define DSC_BP_ENABLE               1
#define DSC_BIT_PER_PIXEL           128
#define DSC_SLICE_HEIGHT            12
#define DSC_SLICE_WIDTH             540
#define DSC_CHUNK_SIZE              540
#define DSC_XMIT_DELAY              512
#define DSC_DEC_DELAY               526
#define DSC_SCALE_VALUE             32
#define DSC_INCREMENT_INTERVAL      287
#define DSC_DECREMENT_INTERVAL      7
#define DSC_LINE_BPG_OFFSET         12
#define DSC_NFL_BPG_OFFSET          2235
#define DSC_SLICE_BPG_OFFSET        2170
#define DSC_INITIAL_OFFSET          6144
#define DSC_FINAL_OFFSET            4336
#define DSC_FLATNESS_MINQP          7
#define DSC_FLATNESS_MAXQP          16
#define DSC_RC_MODEL_SIZE           8192
#define DSC_RC_EDGE_FACTOR          6
#define DSC_RC_QUANT_INCR_LIMIT0    15
#define DSC_RC_QUANT_INCR_LIMIT1    15
#define DSC_RC_TGT_OFFSET_HI        3
#define DSC_RC_TGT_OFFSET_LO        3
static unsigned int rc_buf_thresh[14] = {
	896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000, 8064};
unsigned int range_min_qp[15] = {0, 4, 5, 5, 7, 7, 7, 7, 7, 7, 9, 9, 9, 11, 17};
unsigned int range_max_qp[15] = {8, 8, 9, 10, 11, 11, 11, 12, 13, 14, 15, 16, 17, 17, 19};
int range_bpg_ofs[15] = {2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -12, -12, -12, -12};
#endif
struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct regulator *vddi_regulator;
	struct regulator *vci_regulator;
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
static char bl_tb0[] = {0x51, 0x0C, 0x7D};
static unsigned int last_mapped_level;
static int g_aod_enable;
static int g_hbm_enable;
static int g_lcm_fresh_mode = MODE_1_FPS;
static unsigned int mapped_level;
static char bl_dim[] = {0x53, 0x28};
static unsigned int g_dim_enable;
static unsigned int g_need_dim_enable;

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
		dev_dbg(ctx->dev, "error %d reading dcs seq:(%#x)\n", ret, cmd);
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
		dev_dbg(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
}
struct LCM_setting_table {
	unsigned int cmd;
	unsigned char count;
	unsigned char para_list[120];
};
static struct LCM_setting_table lcm_initialization_setting[] = {
	{0x2F, 1, {0x00}},

	{0xF0, 5, {0x55, 0xAA, 0x52, 0x08, 0x00}},
	{0x6F, 1, {0x0C}},
	{0xC3, 1, {0x04}},
	{0xEA, 1, {0x80}},

	{0x6F, 1, {0x0D}},
	{0xD8, 1, {0x02}},
	{0xF0, 5, {0x55, 0xAA, 0x52, 0x08, 0x01}},
	{0x6F, 1, {0x03}},
	{0xC7, 1, {0xC7}},
	{0x6F, 1, {0x05}},
	{0xC7, 2, {0x27, 0x48}},
	{0x6F, 1, {0x09}},
	{0xC7, 1, {0x24}},
	{0xF0, 5, {0x55, 0xAA, 0x52, 0x08, 0x05}},
	{0xCB, 7, {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33}},
	{0xF0, 5, {0x55, 0xAA, 0x52, 0x08, 0x07}},
	{0xC0, 3, {0x87, 0x01, 0x09}},

	{0x6F, 1, {0x00}},
	{0xC1, 21, {0x34, 0x00, 0x01, 0x69, 0x06, 0x91, 0x06, 0x16, 0x3F, 0xFE, 0xAD, 0x90, 0x00, 0x01, 0x52, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0x6F, 1, {0x15}},
	{0xC1, 16, {0x13, 0x00, 0x16, 0x65, 0x99, 0x70, 0x83, 0x33, 0xC0, 0xED, 0x4F, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0x6F, 1, {0x25}},
	{0xC1, 16, {0x44, 0x22, 0x37, 0x99, 0x70, 0x83, 0x00, 0x00, 0x02, 0x13, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}},

	{0x6F, 1, {0x00}},
	{0xC2, 21, {0x32, 0x0F, 0x01, 0x69, 0x06, 0x91, 0xF9, 0xEA, 0x00, 0x00, 0x73, 0xA2, 0x00, 0x03, 0x33, 0x3A, 0x7F, 0xFF, 0xB9, 0xF0, 0x39}},
	{0x6F, 1, {0x15}},
	{0xC2, 16, {0x13, 0x34, 0xD2, 0x21, 0x99, 0x70, 0x83, 0x33, 0x0F, 0x13, 0x4F, 0xFA, 0x23, 0x00, 0x00, 0x00}},
	{0x6F, 1, {0x25}},
	{0xC2, 16, {0x00, 0x00, 0x15, 0x99, 0x70, 0x83, 0x00, 0x00, 0x02, 0x13, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}},

	{0x6F, 1, {0x00}},
	{0xC3, 21, {0x3C, 0x00, 0x07, 0x90, 0x01, 0xB9, 0x07, 0x38, 0x3F, 0xFB, 0x12, 0xB0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0x6F, 1, {0x15}},
	{0xC3, 16, {0x4E, 0x00, 0x00, 0x15, 0x99, 0x1F, 0x6F, 0x33, 0xC0, 0xB0, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0x6F, 1, {0x25}},
	{0xC3, 16, {0x00, 0x00, 0x15, 0x00, 0x00, 0x13, 0x00, 0x00, 0x02, 0x4E, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}},

	{0x6F, 1, {0x00}},
	{0xC4, 21, {0x3A, 0x0F, 0x08, 0x44, 0x01, 0xB9, 0xF8, 0x74, 0x00, 0x03, 0xBB, 0x2C, 0x00, 0x00, 0x9E, 0x7C, 0x7F, 0xFF, 0xA3, 0x68, 0x40}},
	{0x6F, 1, {0x15}},
	{0xC4, 16, {0x4D, 0x44, 0x22, 0x37, 0x99, 0x1F, 0x6F, 0x33, 0x0F, 0x50, 0x15, 0xF9, 0x70, 0x00, 0x00, 0x00}},
	{0x6F, 1, {0x25}},
	{0xC4, 16, {0x44, 0x22, 0x37, 0x00, 0x00, 0x13, 0x00, 0x00, 0x02, 0x4D, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}},

	{0x6F, 1, {0x00}},
	{0xC5, 21, {0x20, 0x0F, 0x01, 0x69, 0x06, 0x91, 0xF9, 0xEA, 0x3F, 0xFF, 0x21, 0x32, 0x3F, 0xFD, 0xB4, 0x0A, 0x00, 0x00, 0x22, 0x60, 0xC9}},
	{0x6F, 1, {0x15}},
	{0xC5, 16, {0x13, 0x00, 0x16, 0x65, 0x00, 0x00, 0x13, 0x33, 0xF0, 0xED, 0xB1, 0x05, 0xDD, 0x00, 0x00, 0x00}},
	{0x6F, 1, {0x25}},
	{0xC5, 16, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}},

	{0x6F, 1, {0x00}},
	{0xC6, 21, {0x20, 0x0F, 0x08, 0x44, 0x01, 0xB9, 0xF8, 0x74, 0x3F, 0xFD, 0x45, 0x6C, 0x3F, 0xFE, 0xEC, 0x60, 0x00, 0x00, 0x2B, 0x11, 0x00}},
	{0x6F, 1, {0x15}},
	{0xC6, 16, {0x02, 0x00, 0x00, 0x15, 0x00, 0x14, 0x64, 0x33, 0xF0, 0xB0, 0xEB, 0x06, 0x90, 0x00, 0x00, 0x00}},
	{0x6F, 1, {0x25}},
	{0xC6, 16, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}},

	{0x6F, 1, {0x00}},
	{0xC7, 21, {0x26, 0x00, 0x01, 0x69, 0x06, 0x91, 0x06, 0x16, 0x00, 0x00, 0x00, 0x00, 0x3F, 0xFB, 0xD3, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0x6F, 1, {0x15}},
	{0xC7, 16, {0x13, 0x34, 0xD2, 0x21, 0x00, 0x00, 0x13, 0x33, 0x30, 0x13, 0xB1, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0x6F, 1, {0x25}},
	{0xC7, 16, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}},

	{0x6F, 1, {0x00}},
	{0xC8, 21, {0x26, 0x00, 0x08, 0x44, 0x01, 0xB9, 0x07, 0x8C, 0x00, 0x01, 0x5F, 0x6C, 0x3F, 0xFE, 0x4D, 0xE4, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0x6F, 1, {0x15}},
	{0xC8, 16, {0x02, 0x44, 0x22, 0x37, 0x00, 0x14, 0x64, 0x33, 0x30, 0x50, 0xEB, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0x6F, 1, {0x25}},
	{0xC8, 16, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}},

	{0x6F, 1, {0x00}},
	{0xCD, 21, {0x21, 0x00, 0x04, 0x00, 0x04, 0x41, 0x00, 0x00, 0x3F, 0xFE, 0xF8, 0x00, 0x3F, 0xFE, 0xEF, 0xC0, 0x00, 0x00, 0x11, 0x04, 0x00}},
	{0x6F, 1, {0x15}},
	{0xCD, 16, {0x09, 0x12, 0xFA, 0x1B, 0x00, 0x18, 0x38, 0x33, 0xF0, 0xE0, 0xDF, 0x04, 0x20, 0x00, 0x00, 0x00}},
	{0x6F, 1, {0x25}},
	{0xCD, 16, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}},

	{0x6F, 1, {0x00}},
	{0xCE, 21, {0x2D, 0x00, 0x04, 0x41, 0x04, 0x41, 0x00, 0x00, 0x3F, 0xFE, 0xE7, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0x6F, 1, {0x15}},
	{0xCE, 16, {0x17, 0x12, 0xFA, 0x1B, 0x00, 0x39, 0x5A, 0x33, 0xC0, 0xDF, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0x6F, 1, {0x25}},
	{0xCE, 16, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x17, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}},

	{0x6F, 1, {0x00}},
	{0xCF, 21, {0x27, 0x00, 0x04, 0x00, 0x04, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0xFE, 0xEF, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0x6F, 1, {0x15}},
	{0xCF, 16, {0x09, 0x22, 0x1C, 0x3D, 0x00, 0x18, 0x38, 0x33, 0x30, 0x20, 0xDF, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0x6F, 1, {0x25}},
	{0xCF, 16, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}},

	{0x6F, 1, {0x00}},
	{0xD0, 21, {0x2B, 0x00, 0x04, 0x41, 0x04, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0xFF, 0xED, 0xE7, 0x7F}},
	{0x6F, 1, {0x15}},
	{0xD0, 16, {0x17, 0x22, 0x1C, 0x3D, 0x00, 0x39, 0x5A, 0x33, 0x0F, 0x21, 0x21, 0xFB, 0xBF, 0x00, 0x00, 0x00}},
	{0x6F, 1, {0x25}},
	{0xD0, 16, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x17, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}},
	{0xF0, 5, {0x55, 0xAA, 0x52, 0x08, 0x08}},
	{0x6F, 1, {0x0C}},
	{0xBF, 8, {0x10, 0x0C, 0x12, 0x23, 0x2a, 0x30, 0x3C, 0x43}},
	{0x6F, 1, {0x14}},
	{0xBF, 8, {0x04, 0x08, 0x0C, 0x18, 0x18, 0x26, 0x48, 0x30}},
	{0x6F, 1, {0x1C}},
	{0xBF, 8, {0x20, 0x12, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0x6F, 1, {0x24}},
	{0xBF, 8, {0x23, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},

	{0xF0, 5, {0x55, 0xAA, 0x52, 0x08, 0x00}},
	{0xB2, 1, {0x09}},
	{0x6F, 1, {0x05}},
	{0xB2, 2, {0x10, 0x10}},
	{0x88, 5, {0x01, 0x02, 0x1C, 0x08, 0x98}},
	{0xFF, 4, {0xAA, 0x55, 0xA5, 0x80}},
	{0x6F, 1, {0x0B}},
	{0xF5, 1, {0x02}},
	{0x6F, 1, {0x46}},
	{0xF4, 2, {0x07, 0x09}},
	{0x6F, 1, {0x4A}},
	{0xF4, 2, {0x08, 0x0A}},
	{0x6F, 1, {0x56}},
	{0xF4, 2, {0x44, 0x44}},
	{0x6F, 1, {0x18}},
	{0xF4, 1, {0x73}},
	{0x6F, 1, {0x2A}},
	{0xF4, 1, {0x08}},
	{0x6F, 1, {0x32}},
	{0xF2, 1, {0x00}},
	{0xFF, 4, {0xAA, 0x55, 0xA5, 0x81}},
	{0x6F, 1, {0x3C}},
	{0xF5, 1, {0x84}},
	{0x17, 1, {0x21}},
	{0x71, 1, {0x10}},
	{0x8D, 8, {0x00, 0x00, 0x04, 0x37, 0x00, 0x00, 0x06, 0xB7}},
	{0x2A, 4, {0x00, 0x00, 0x04, 0x37}},
	{0x2B, 4, {0x00, 0x00, 0x09, 0x83}},
	{0x90, 1, {0x03}},
	{0x6F, 1, {0x01}},
	{0x90, 1, {0x43}},
	{0x91, 18, {0xAB, 0x28, 0x00, 0x0C, 0xC2, 0x00, 0x02, 0x0E, 0x01, 0x1F, 0x00, 0x07, 0x08, 0xBB, 0x08, 0x7A, 0x10, 0xF0}},
	{0x53, 1, {0x20}},
	{0x3B, 16, {0x00, 0x18, 0x00, 0x3C, 0x00, 0x18, 0x00, 0x44, 0x00, 0x18, 0x0A, 0x14, 0x00, 0x18, 0x00, 0x3C}},
	{0x6F, 1, {0x10}},
	{0x3B, 4, {0x00, 0x18, 0x00, 0x3C}},
	{0x35, 1, {0x00}},
	{0x5F, 2, {0x00, 0x00}},
	{0x51, 2, {0x00, 0x00}},
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

static struct LCM_setting_table hbm_mode_enter_setting[] = {
	{0xF0, 5, {0x55, 0xAA, 0x52, 0x08, 0x08}},
	{0x6F, 1, {0x4A}},
	{0xB8, 6, {0x08, 0x00, 0x0F, 0xFF, 0x08, 0x00}},
	{0x2F, 1, {0x03}},
	{0x87, 1, {0x25}},
};

static struct LCM_setting_table hbm_mode_exit_setting[] = {
	{0x87, 1, {0x20}},
	{0x2F, 1, {0x01}},
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
	msleep(15);
	switch (g_lcm_fresh_mode) {
	case MODE_0_FPS:
		lcm_initialization_setting[0].para_list[0] = 0x02;
		break;
	case MODE_1_FPS:
		lcm_initialization_setting[0].para_list[0] = 0x01;
		break;
	case MODE_2_FPS:
		lcm_initialization_setting[0].para_list[0] = 0x00;
		break;
	}
	push_table(ctx, lcm_initialization_setting,
		sizeof(lcm_initialization_setting) / sizeof(struct LCM_setting_table));
	ctx->error = 0;
	g_dim_enable = 0;
	g_need_dim_enable = 0;
	pr_info("[LCM] %s end\n", __func__);
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

static const struct drm_display_mode switch_mode_60hz = {
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

static const struct drm_display_mode switch_mode_144hz = {
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

#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params_60hz = {
	.cust_esd_check = 0,
	.esd_check_enable = 0,
	.ssc_enable = 0,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.lp_perline_en = 1,

	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable                =  DSC_ENABLE,
		.ver                   =  DSC_VER,
		.slice_mode            =  DSC_SLICE_MODE,
		.rgb_swap              =  DSC_RGB_SWAP,
		.dsc_cfg               =  DSC_DSC_CFG,
		.rct_on                =  DSC_RCT_ON,
		.bit_per_channel       =  DSC_BIT_PER_CHANNEL,
		.dsc_line_buf_depth    =  DSC_DSC_LINE_BUF_DEPTH,
		.bp_enable             =  DSC_BP_ENABLE,
		.bit_per_pixel         =  DSC_BIT_PER_PIXEL,
		.pic_height            =  FRAME_HEIGHT,
		.pic_width             =  FRAME_WIDTH,
		.slice_height          =  DSC_SLICE_HEIGHT,
		.slice_width           =  DSC_SLICE_WIDTH,
		.chunk_size            =  DSC_CHUNK_SIZE,
		.xmit_delay            =  DSC_XMIT_DELAY,
		.dec_delay             =  DSC_DEC_DELAY,
		.scale_value           =  DSC_SCALE_VALUE,
		.increment_interval    =  DSC_INCREMENT_INTERVAL,
		.decrement_interval    =  DSC_DECREMENT_INTERVAL,
		.line_bpg_offset       =  DSC_LINE_BPG_OFFSET,
		.nfl_bpg_offset        =  DSC_NFL_BPG_OFFSET,
		.slice_bpg_offset      =  DSC_SLICE_BPG_OFFSET,
		.initial_offset        =  DSC_INITIAL_OFFSET,
		.final_offset          =  DSC_FINAL_OFFSET,
		.flatness_minqp        =  DSC_FLATNESS_MINQP,
		.flatness_maxqp        =  DSC_FLATNESS_MAXQP,
		.rc_model_size         =  DSC_RC_MODEL_SIZE,
		.rc_edge_factor        =  DSC_RC_EDGE_FACTOR,
		.rc_quant_incr_limit0  =  DSC_RC_QUANT_INCR_LIMIT0,
		.rc_quant_incr_limit1  =  DSC_RC_QUANT_INCR_LIMIT1,
		.rc_tgt_offset_hi      =  DSC_RC_TGT_OFFSET_HI,
		.rc_tgt_offset_lo      =  DSC_RC_TGT_OFFSET_LO,
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
		.dfps_cmd_table[0] = {0, 48, {0xA9, 0x02, 0x08, 0xB7, 0x1C, 0x27, 0x08, 0x00, 0x08, 0x00,
			0x08, 0x00, 0x08, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x08,
			0xB7, 0x28, 0x2D, 0x08, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02,
			0x08, 0xE9, 0x00, 0x07, 0x00, 0x00, 0x00, 0x2E, 0x00, 0x00,
			0x02, 0x18, 0x01, 0x00, 0x2F, 0x00, 0x00, 0x02}},
	},
	.data_rate = MODE_0_DATA_RATE,
	.tran_panel_params = &panel_driver_status,
};

static struct mtk_panel_params ext_params_120hz = {
	.cust_esd_check = 0,
	.esd_check_enable = 0,
	.ssc_enable = 0,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.lp_perline_en = 1,
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
		.dfps_cmd_table[0] = {0, 48, {0xA9, 0x02, 0x08, 0xB7, 0x1C, 0x27, 0x08, 0x00, 0x08, 0x00,
			0x08, 0x00, 0x08, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x08,
			0xB7, 0x28, 0x2D, 0x08, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02,
			0x08, 0xE9, 0x00, 0x07, 0x00, 0x00, 0x00, 0x2E, 0x00, 0x00,
			0x02, 0x18, 0x01, 0x00, 0x2F, 0x00, 0x00, 0x01}},
	},
	.data_rate = MODE_1_DATA_RATE,
	.tran_panel_params = &panel_driver_status,
};

static struct mtk_panel_params ext_params_144hz = {
	.cust_esd_check = 0,
	.esd_check_enable = 0,
	.ssc_enable = 0,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.lp_perline_en = 1,
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
		.dfps_cmd_table[0] = {0, 48, {0xA9, 0x02, 0x08, 0xB7, 0x1C, 0x27, 0x08, 0xF6, 0x08, 0xF6,
			0x08, 0xF6, 0x08, 0xF6, 0x08, 0xF6, 0x08, 0xF6, 0x02, 0x08,
			0xB7, 0x28, 0x2D, 0x08, 0xF6, 0x08, 0xF6, 0x08, 0xF6, 0x02,
			0x08, 0xE9, 0x00, 0x07, 0x00, 0x00, 0x00, 0x33, 0x00, 0x00,
			0x02, 0x18, 0x01, 0x00, 0x2F, 0x00, 0x00, 0x00}},
	},
	.data_rate = MODE_1_DATA_RATE,
	.tran_panel_params = &panel_driver_status,
};

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb,
	void *handle, unsigned int level)
{
	if (level > 511)
		level = 511;
	if (level <= 255) {
		if (panel_driver_status.lcm_low_backlight == 1) {
			if (level == 0) {
				mapped_level = 0;
			} else {
				mapped_level = (123*level*level + 94060*level - 14190)/10000;
				if (mapped_level < 9)
					mapped_level = 9;
			}
		} else {
			mapped_level = level * 3197 / 255;
		}
	} else if (255 < level && level <= 511) {
		mapped_level = 3197 + (level - 256) * (4095-3197) / 255;
	}
	bl_tb0[1] = ((mapped_level >> 8) & 0x0f);
	bl_tb0[2] = (mapped_level & 0xff);
	if (!cb)
		return -1;

	if ((g_dim_enable == 0) && (g_need_dim_enable == 1)) {
		bl_dim[1] = 0x28;
		cb(dsi, handle, bl_dim, ARRAY_SIZE(bl_dim));
		g_dim_enable =  1;
	}
	if (g_aod_enable == 0)
		g_need_dim_enable = 1;
	if ((mapped_level == 0) || (panel_driver_status.dimming_status == 0)) {
		bl_dim[1] = 0x20;
		cb(dsi, handle, bl_dim, ARRAY_SIZE(bl_dim));
		g_dim_enable = 0;
		g_need_dim_enable = 0;
	} else {
		last_mapped_level = mapped_level;
	}
	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));
	pr_info("[LCM] %s level = %d, mapped_level = %d, lcm_low_backlight %d\n", __func__, level, mapped_level, panel_driver_status.lcm_low_backlight);
	return 0;
}

struct drm_display_mode *get_mode_by_id(struct drm_connector *connector,
	unsigned int mode)
{
	struct drm_display_mode *m;
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

	pr_info("[LCM] %s FPS form %d to %d begin\n", __func__, g_lcm_fresh_mode, drm_mode_vrefresh(m));
	if (drm_mode_vrefresh(m) == MODE_0_FPS)
		ext->params = &ext_params_60hz;
	else if (drm_mode_vrefresh(m) == MODE_1_FPS)
		ext->params = &ext_params_120hz;
	else if (drm_mode_vrefresh(m) == MODE_2_FPS)
		ext->params = &ext_params_144hz;
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
		mdelay(12);
	} else {
		ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
		gpiod_set_value(ctx->reset_gpio, 0);
		devm_gpiod_put(ctx->dev, ctx->reset_gpio);
		mdelay(2);
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
	case 4:
		aod_mapped_level = 0x38;
		break;
	case 7:
		aod_mapped_level = 0x58;
		break;
	case 29:
		aod_mapped_level = 0x16B;
		break;
	default:
		aod_mapped_level = 0x16B;
		break;
	}
	pr_info("[LCM] %s level is %u, aod_mapped_level is %d\n", __func__, level, aod_mapped_level);

	bl_tb0[1] = ((aod_mapped_level >> 8) & 0x0f);
	bl_tb0[2] = (aod_mapped_level & 0xff);
	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));

	if (last_mapped_level > aod_mapped_level) {
		g_dim_enable = 0;
		g_need_dim_enable = 1;
		pr_info("[%s] last_mapped_level > aod mapped_level:[0x%X > 0x%X], need enable dim\n", __func__, last_mapped_level, aod_mapped_level);
	}

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

static int panel_hbm_set_cmdq(struct drm_panel *panel, void *dsi,
			dcs_write_gce cb, void *handle, bool en)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!cb)
		return -1;
	if (ctx->hbm_en == en)
		goto done;
	pr_info("[LCM] %s FPS=%dHz en=%d, mapped_level=%d, aod=%d\n", __func__, g_lcm_fresh_mode, en, mapped_level, g_aod_enable);
	if (en) {
		hbm_mode_enter_setting[2].para_list[4] = 0x800 >> 8;
		hbm_mode_enter_setting[2].para_list[5] = 0x800 & 0xFF;
		if ((mapped_level <= 463) || g_aod_enable) {
			hbm_mode_enter_setting[2].para_list[0] = 0x0 >> 8;
			hbm_mode_enter_setting[2].para_list[1] = 0x0 & 0xFF;
		} else if ((mapped_level > 463) && (mapped_level <= 1161)) {
			hbm_mode_enter_setting[2].para_list[0] = ((mapped_level - 463) * 2048 / (1161 - 463)) >> 8;
			hbm_mode_enter_setting[2].para_list[1] = ((mapped_level - 463) * 2048 / (1161 - 463)) & 0xFF;
		} else if ((mapped_level > 1161) && (mapped_level <= 3197)) {
			hbm_mode_enter_setting[2].para_list[0] = 0x800 >> 8;
			hbm_mode_enter_setting[2].para_list[1] = 0x800 & 0xFF;
		} else {
			hbm_mode_enter_setting[2].para_list[0] = (2048 - (mapped_level - 3197) * 2048 / (4095 - 3197)) >> 8;
			hbm_mode_enter_setting[2].para_list[1] = (2048 - (mapped_level - 3197) * 2048 / (4095 - 3197)) & 0xFF;
			hbm_mode_enter_setting[2].para_list[4] = (2048 - (mapped_level - 3197) * 33 / (4095 - 3197)) >> 8;
			hbm_mode_enter_setting[2].para_list[5] = (2048 - (mapped_level - 3197) * 33 / (4095 - 3197)) & 0xFF;
		}
		push_table_cb(dsi, cb, handle, hbm_mode_enter_setting,
			sizeof(hbm_mode_enter_setting) / sizeof(struct LCM_setting_table));
	} else {
		switch (g_lcm_fresh_mode) {
		case MODE_0_FPS:
			hbm_mode_exit_setting[1].para_list[0] = 0x02;
			break;
		case MODE_1_FPS:
			hbm_mode_exit_setting[1].para_list[0] = 0x01;
			break;
		case MODE_2_FPS:
			hbm_mode_exit_setting[1].para_list[0] = 0x00;
			break;
		default:
			break;
		}
		push_table_cb(dsi, cb, handle, hbm_mode_exit_setting,
			sizeof(hbm_mode_exit_setting) / sizeof(struct LCM_setting_table));
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
	pr_info("[LCM] %s FPS=%dHz en=%d, mapped_level=%d, aod=%d\n", __func__, g_lcm_fresh_mode, en, mapped_level, g_aod_enable);
	if (en) {
		hbm_mode_enter_setting[2].para_list[4] = 0x800 >> 8;
		hbm_mode_enter_setting[2].para_list[5] = 0x800 & 0xFF;
		if ((mapped_level <= 463) || g_aod_enable) {
			hbm_mode_enter_setting[2].para_list[0] = 0x0 >> 8;
			hbm_mode_enter_setting[2].para_list[1] = 0x0 & 0xFF;
		} else if ((mapped_level > 463) && (mapped_level <= 1161)) {
			hbm_mode_enter_setting[2].para_list[0] = ((mapped_level - 463) * 2048 / (1161 - 463)) >> 8;
			hbm_mode_enter_setting[2].para_list[1] = ((mapped_level - 463) * 2048 / (1161 - 463)) & 0xFF;
		} else if ((mapped_level > 1161) && (mapped_level <= 3197)) {
			hbm_mode_enter_setting[2].para_list[0] = 0x800 >> 8;
			hbm_mode_enter_setting[2].para_list[1] = 0x800 & 0xFF;
		} else {
			hbm_mode_enter_setting[2].para_list[0] = (2048 - (mapped_level - 3197) * 2048 / (4095 - 3197)) >> 8;
			hbm_mode_enter_setting[2].para_list[1] = (2048 - (mapped_level - 3197) * 2048 / (4095 - 3197)) & 0xFF;
			hbm_mode_enter_setting[2].para_list[4] = (2048 - (mapped_level - 3197) * 33 / (4095 - 3197)) >> 8;
			hbm_mode_enter_setting[2].para_list[5] = (2048 - (mapped_level - 3197) * 33 / (4095 - 3197)) & 0xFF;
		}
		push_table_cb(dsi, cb, handle, hbm_mode_enter_setting,
			sizeof(hbm_mode_enter_setting) / sizeof(struct LCM_setting_table));
	} else {
		switch (g_lcm_fresh_mode) {
		case MODE_0_FPS:
			hbm_mode_exit_setting[1].para_list[0] = 0x02;
			break;
		case MODE_1_FPS:
			hbm_mode_exit_setting[1].para_list[0] = 0x01;
			break;
		case MODE_2_FPS:
			hbm_mode_exit_setting[1].para_list[0] = 0x00;
			break;
		default:
			break;
		}
		push_table_cb(dsi, cb, handle, hbm_mode_exit_setting,
			sizeof(hbm_mode_exit_setting) / sizeof(struct LCM_setting_table));
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
	return 3;
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
	ret = regulator_set_voltage(ctx_work->dvdd_regulator, 1254000, 1254000);
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
			MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_NO_EOT_PACKET |
			MIPI_DSI_CLOCK_NON_CONTINUOUS;
	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);
		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}
	ctx->vci_regulator = devm_regulator_get(ctx->dev, "vci");
	if (IS_ERR(ctx->vci_regulator)) {
		ret = PTR_ERR(ctx->vci_regulator);
		dev_err(ctx->dev, "cannot get vci regulator ret[%d]\n", ret);
		return ret;
	}
	ret = regulator_enable(ctx->vci_regulator);
	if (ret < 0)
		dev_err(ctx->dev, "<%s:%d>enable vci vol error!ret[%d]\n", __func__, __LINE__, ret);

	ctx->vddi_regulator = devm_regulator_get(ctx->dev, "vddi");
	if (IS_ERR(ctx->vddi_regulator)) {
		ret = PTR_ERR(ctx->vddi_regulator);
		dev_err(ctx->dev, "cannot get vddi regulator ret[%d]\n", ret);
		return ret;
	}
	ret = regulator_enable(ctx->vddi_regulator);
	if (ret < 0)
		dev_err(ctx->dev, "<%s:%d>enable vddi vol error!ret[%d]\n", __func__, __LINE__, ret);

	ctx_work = ctx;
	schedule_delayed_work(&lcm_regulator_work, msecs_to_jiffies(6000));

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_dbg(dev, "cannot get reset-gpios %ld\n", PTR_ERR(ctx->reset_gpio));
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
	int ret = 0;
	bool cancelled;
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);

	pr_info("[LCM] %s begin\n", __func__);
	lcm_disable(&ctx->panel);
	if (ctx->prepared) {
		ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
		gpiod_set_value(ctx->reset_gpio, 0);
		devm_gpiod_put(ctx->dev, ctx->reset_gpio);
		mdelay(2);
		if (IS_ERR_OR_NULL(ctx->vci_regulator))
			ctx->vci_regulator = devm_regulator_get(ctx->dev, "vci");

		if (IS_ERR_OR_NULL(ctx->vci_regulator)) {
			ret = PTR_ERR(ctx->vci_regulator);
			dev_err(ctx->dev, "<%s,%d> lcm vci get fail!ret[%d].\n", __func__, __LINE__, ret);
		} else {
			ret = regulator_disable(ctx->vci_regulator);
			if (ret < 0)
				dev_err(ctx->dev, "<%s:%d>disable lcm vci error!ret[%d]\n", __func__, __LINE__, ret);
			if (regulator_is_enabled(ctx->vci_regulator))
				dev_err(ctx->dev, "<%s:%d>after disable, lcm vci still enable!ret[%d]\n", __func__, __LINE__, ret);
			mdelay(2);
		}
	}

	if (IS_ERR_OR_NULL(ctx->dvdd_regulator)) {
		ctx->dvdd_regulator = devm_regulator_get(ctx->dev, "dvdd");
	}
	if (IS_ERR_OR_NULL(ctx->dvdd_regulator)) {
		ret = PTR_ERR(ctx->dvdd_regulator);
		dev_err(ctx->dev, "<%s,%d> lcm dvdd get fail!ret[%d].\n", __func__, __LINE__, ret);
	} else {
		cancelled = cancel_delayed_work_sync(&lcm_regulator_work);
		if (cancelled) {
			pr_err("[LCM] %s work is 6s stop  regulator is disabled\n", __func__);
			ret = regulator_enable(ctx->dvdd_regulator);
			mdelay(2);
			ret = regulator_disable(ctx->dvdd_regulator);
			if (regulator_is_enabled(ctx->dvdd_regulator)) {
				pr_err("[LCM] %s %d regulator is enabled,try to disable\n", __func__, __LINE__);
				ret = regulator_disable(ctx->dvdd_regulator);
			}
		} else {
			if (regulator_is_enabled(ctx->dvdd_regulator)) {
				pr_err("[LCM] %s work is run and  regulator is enabled\n", __func__);
				ret = regulator_disable(ctx->dvdd_regulator);
			}
		}
		if (regulator_is_enabled(ctx->dvdd_regulator)) {
			pr_err("[LCM] %s %d regulator is enabled,try to disable\n", __func__, __LINE__);
			ret = regulator_disable(ctx->dvdd_regulator);
		}
	}
	mdelay(2);
}
static const struct of_device_id lcm_of_match[] = {
	{
		.compatible = "nt37703a,fhdp,dsi,vdo,tm,tm,144hz,x6856",
	},
	{} };
MODULE_DEVICE_TABLE(of, lcm_of_match);
static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.shutdown = lcm_shutdown,
	.driver = {
			.name = "nt37703a_fhdp_dsi_vdo_tm_tm_144hz_x6856",
			.owner = THIS_MODULE,
			.of_match_table = lcm_of_match,
		},
};
module_mipi_dsi_driver(lcm_driver);
MODULE_AUTHOR("Transsion Inc.");
MODULE_DESCRIPTION("transsion, panel driver");
MODULE_LICENSE("GPL v2");
