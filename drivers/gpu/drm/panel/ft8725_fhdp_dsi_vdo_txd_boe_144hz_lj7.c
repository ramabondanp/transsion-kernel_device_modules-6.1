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
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/of_graph.h>
#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mediatek_v2/mtk_panel_ext.h"
#include "../mediatek/mediatek_v2/mtk_drm_graphics_base.h"
#endif
#include "./tran_drm_i2c_drv/tran_drm_panel_i2c.h"
#define REGFLAG_CMD                0xFFFA
#define REGFLAG_UDELAY             0xFFFB
#define REGFLAG_DELAY              0xFFFC
#define REGFLAG_END_OF_TABLE       0xFFFD
static unsigned int tran_is_lcm_poweroff;
#define LCM_DSI_CMD_MODE                0
#define FRAME_WIDTH                     (1080)
#define FRAME_HEIGHT                    (2460)
#define PHYSICAL_WIDTH                  (69170)
#define PHYSICAL_HEIGHT                 (157560)
#define MIPI_CLOCK 595
#define DATA_RATE 1190

#define VSA 10
#define VBP 20
#define HSA 8
#define HBP 108
#define HFP 92
#define MODE_144_FPS 144
#define MODE_144_HBP 44
#define MODE_144_HFP 50
#define MODE_144_VFP 48
#define MODE_120_FPS 120
#define MODE_120_VFP 48
#define MODE_90_FPS 90
#define MODE_90_VFP 900
#define MODE_60_FPS 60
#define MODE_60_VFP 2596

#define VFP_45HZ 4300
#define DSC_ENABLE                1
#define DSC_VER                   17
#define DSC_SLICE_MODE            1
#define DSC_RGB_SWAP              0
#define DSC_DSC_CFG               34
#define DSC_RCT_ON                1
#define DSC_BIT_PER_CHANNEL       8
#define DSC_DSC_LINE_BUF_DEPTH    9
#define DSC_BP_ENABLE             1
#define DSC_BIT_PER_PIXEL         128
#define DSC_PIC_HEIGHT            2460
#define DSC_PIC_WIDTH             1080
#define DSC_SLICE_HEIGHT          20
#define DSC_SLICE_WIDTH           540
#define DSC_CHUNK_SIZE            540
#define DSC_XMIT_DELAY            512
#define DSC_DEC_DELAY             616
#define DSC_SCALE_VALUE           32
#define DSC_INCREMENT_INTERVAL    488
#define DSC_DECREMENT_INTERVAL    7
#define DSC_LINE_BPG_OFFSET       12
#define DSC_NFL_BPG_OFFSET        1294
#define DSC_SLICE_BPG_OFFSET      1302
#define DSC_INITIAL_OFFSET        6144
#define DSC_FINAL_OFFSET          4336
#define DSC_FLATNESS_MINQP        3
#define DSC_FLATNESS_MAXQP        12
#define DSC_RC_MODEL_SIZE         8192
#define DSC_RC_EDGE_FACTOR        6
#define DSC_RC_QUANT_INCR_LIMIT0  11
#define DSC_RC_QUANT_INCR_LIMIT1  11
#define DSC_RC_TGT_OFFSET_HI      3
#define DSC_RC_TGT_OFFSET_LO      3

static int current_fps = 144;
static unsigned int mapped_level;
static bool g_dimming_enable;
static char bl_dimming_enable[] = {0x53, 0x2C};
static char bl_dimming_disable[] = {0x53, 0x24};
static char bl_tb0[] = {0x51, 0x0C, 0xF3};
struct OCP2131_SETTING_TABLE {
	unsigned char cmd;
	unsigned char data;
};
static struct OCP2131_SETTING_TABLE ocp2131_cmd_data[2] = {
	{ 0x00, 0x0F },
	{ 0x01, 0x0F },
};
struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *bias_pos;
	struct gpio_desc *bias_neg;
	struct regulator *vio18_regulator;
	bool prepared;
	bool enabled;
	int error;
};
tran_lcm_doze_backlight g_lcm_doze_backlight = {
	.doze_backlight_num = 0,
	.doze_backlight_level1 = 0,
	.doze_backlight_level2 = 0,
	.doze_backlight_level3 = 0,
};
struct tran_panel_driver_params panel_driver_status = {0};
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
#define lcm_dcs_write_seq_static(ctx, seq...)                     \
	({                                                            \
		static const u8 d[] = {seq};                              \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                     \
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
		dev_err(ctx->dev, "error %d reading dcs seq:(0x%x)\n", ret, cmd);
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
struct LCM_setting_table {
	unsigned int cmd;
	unsigned char count;
	unsigned char para_list[120];
};
static struct LCM_setting_table lcm_initialization_setting[] = {
	{0x00, 1, {0x00}},
	{0xFF, 3, {0x87, 0x25, 0x01}},
	{0x00, 1, {0x80}},
	{0xFF, 2, {0x87, 0x25}},
	{0x00, 1, {0x00}},
	{0x2A, 4, {0x00, 0x00, 0x04, 0x37}},
	{0x00, 1, {0x00}},
	{0x2B, 4, {0x00, 0x00, 0x09, 0x9B}},
	{0x00, 1, {0xA3}},
	{0xB3, 4, {0x09, 0x9C, 0x00, 0x18}},
	{0x00, 1, {0x80}},
	{0xC0, 6, {0x00, 0x48, 0x00, 0x31, 0x00, 0x0B}},
	{0x00, 1, {0x90}},
	{0xC0, 6, {0x00, 0x48, 0x00, 0x31, 0x00, 0x0B}},
	{0x00, 1, {0xA0}},
	{0xC0, 6, {0x00, 0x4A, 0x00, 0x55, 0x00, 0x0B}},
	{0x00, 1, {0xB0}},
	{0xC0, 5, {0x00, 0xCE, 0x00, 0x31, 0x0B}},
	{0x00, 1, {0xC1}},
	{0xC0, 8, {0x01, 0x0B, 0x00, 0xD0, 0x00, 0xB2, 0x01, 0x38}},
	{0x00, 1, {0x70}},
	{0xC0, 6, {0x00, 0xB5, 0x00, 0x4C, 0x00, 0x0B}},
	{0x00, 1, {0xA3}},
	{0xC1, 6, {0x00, 0x64, 0x00, 0x32, 0x00, 0x02}},
	{0x00, 1, {0xB7}},
	{0xC1, 2, {0x00, 0x69}},
	{0x00, 1, {0x7B}},
	{0xCE, 2, {0xFF, 0xFF}},
	{0x00, 1, {0x80}},
	{0xCE, 16, {0x01, 0x81, 0x1F, 0x1F, 0x01, 0x00, 0x01, 0xB8, 0x00, 0xA0, 0x00, 0xD0, 0x00, 0xA8, 0x00, 0xD0}},
	{0x00, 1, {0x90}},
	{0xCE, 15, {0x00, 0x20, 0x0F, 0xFF, 0x00, 0x01, 0x80, 0xFF, 0xFF, 0x00, 0x0F, 0xA0, 0x23, 0x10, 0x0C}},
	{0x00, 1, {0xA0}},
	{0xCE, 3, {0x00, 0x00, 0x00}},
	{0x00, 1, {0xB0}},
	{0xCE, 3, {0x22, 0x00, 0x00}},
	{0x00, 1, {0xD1}},
	{0xCE, 7, {0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00}},
	{0x00, 1, {0xE1}},
	{0xCE, 11, {0x04, 0x03, 0x14, 0x03, 0x14, 0x03, 0x14, 0x00, 0x00, 0x00, 0x00}},
	{0x00, 1, {0xF1}},
	{0xCE, 9, {0x2D, 0x25, 0x12, 0x00, 0xD9, 0x01, 0x0C, 0x01, 0x3B}},
	{0x00, 1, {0xB0}},
	{0xCF, 4, {0x00, 0x00, 0xF5, 0xF9}},
	{0x00, 1, {0xB5}},
	{0xCF, 4, {0x06, 0x06, 0x1D, 0x21}},
	{0x00, 1, {0xC0}},
	{0xCF, 4, {0x09, 0x09, 0x97, 0x9B}},
	{0x00, 1, {0xC5}},
	{0xCF, 4, {0x09, 0x09, 0x9D, 0xA1}},
	{0x00, 1, {0x60}},
	{0xCF, 8, {0x00, 0x00, 0x9D, 0xA1, 0x05, 0x05, 0x7D, 0x81}},
	{0x00, 1, {0x70}},
	{0xCF, 8, {0x00, 0x00, 0x9B, 0x9F, 0x05, 0x05, 0x7B, 0x7F}},
	{0x00, 1, {0xD1}},
	{0xC1, 12, {0x0B, 0x60, 0x0F, 0xDC, 0x1B, 0x15, 0x05, 0xAF, 0x07, 0xE5, 0x0D, 0x81}},
	{0x00, 1, {0xE1}},
	{0xC1, 2, {0x0F, 0xDC}},
	{0x00, 1, {0xE4}},
	{0xCF, 12, {0x0A, 0x4C, 0x0A, 0x4B, 0x0A, 0x4B, 0x0A, 0x4B, 0x0A, 0x4B, 0x0A, 0x4B}},
	{0x00, 1, {0x90}},
	{0xC1, 1, {0x03}},
	{0x00, 1, {0xF5}},
	{0xCF, 1, {0x00}},
	{0x00, 1, {0xF1}},
	{0xCF, 1, {0x90}},
	{0x00, 1, {0x85}},
	{0xB4, 1, {0x6F}},
	{0x00, 1, {0x8F}},
	{0xC5, 1, {0x20}},
	{0x00, 1, {0x91}},
	{0xC4, 1, {0x88}},
	{0x00, 1, {0x80}},
	{0xC5, 2, {0x87, 0x59}},
	{0x00, 1, {0x87}},
	{0xC5, 2, {0x05, 0x05}},
	{0x00, 1, {0x93}},
	{0xC1, 1, {0x82}},
	{0x00, 1, {0x9E}},
	{0xC5, 1, {0x87}},
	{0x00, 1, {0x88}},
	{0xC4, 1, {0x08}},
	{0x00, 1, {0x80}},
	{0xC2, 12, {0x83, 0x01, 0x01, 0x86, 0x82, 0x01, 0x01, 0x86, 0x8E, 0x02, 0x01, 0x86}},
	{0x00, 1, {0xA0}},
	{0xC2, 15, {0x8A, 0x07, 0x00, 0x01, 0x89, 0x89, 0x08, 0x00, 0x01, 0x89, 0x88, 0x09, 0x00, 0x01, 0x89}},
	{0x00, 1, {0xB0}},
	{0xC2, 5, {0x87, 0x0A, 0x00, 0x01, 0x89}},
	{0x00, 1, {0xE0}},
	{0xC2, 4, {0x33, 0x33, 0x00, 0x00}},
	{0x00, 1, {0xE8}},
	{0xC2, 8, {0x12, 0x00, 0x0A, 0x0A, 0x03, 0x88, 0x00, 0x00}},
	{0x00, 1, {0xD0}},
	{0xC3, 16, {0x35, 0x0A, 0x00, 0x00, 0x35, 0x0A, 0x00, 0x00, 0x35, 0x0A, 0x00, 0x00, 0x35, 0x0A, 0x00, 0x00}},
	{0x00, 1, {0xE0}},
	{0xC3, 16, {0x35, 0x0A, 0x00, 0x00, 0x35, 0x0A, 0x00, 0x00, 0x35, 0x0A, 0x00, 0x00, 0x35, 0x0A, 0x00, 0x00}},
	{0x00, 1, {0xE0}},
	{0xCB, 13, {0x83, 0x83, 0x00, 0x83, 0x83, 0x00, 0x83, 0x83, 0x00, 0x83, 0x83, 0x00, 0x83}},
	{0x00, 1, {0x80}},
	{0xCB, 16, {0x01, 0xCD, 0xCD, 0x01, 0xCD, 0x01, 0x01, 0xCD, 0xCE, 0xCE, 0xCD, 0x01, 0xCE, 0xCD, 0x01, 0x01}},
	{0x00, 1, {0x90}},
	{0xCB, 16, {0x00, 0x00, 0x0C, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0x00, 1, {0xA0}},
	{0xCB, 8, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0x00, 1, {0xB0}},
	{0xCB, 4, {0x50, 0x55, 0xA5, 0x95}},
	{0x00, 1, {0xC0}},
	{0xCB, 4, {0x55, 0x55, 0xA5, 0x95}},
	{0x00, 1, {0xD5}},
	{0xCB, 11, {0x83, 0x00, 0x83, 0x83, 0x00, 0x83, 0x83, 0x00, 0x83, 0x83, 0x00}},
	{0x00, 1, {0xE0}},
	{0xCB, 13, {0x83, 0x83, 0x00, 0x83, 0x83, 0x00, 0x83, 0x83, 0x00, 0x83, 0x83, 0x00, 0x83}},
	{0x00, 1, {0x80}},
	{0xCC, 16, {0x18, 0x17, 0x16, 0x2C, 0x07, 0x06, 0x09, 0x08, 0x2C, 0x25, 0x25, 0x26, 0x26, 0x2C, 0x24, 0x2C}},
	{0x00, 1, {0x90}},
	{0xCC, 8, {0x22, 0x23, 0x23, 0x04, 0x03, 0x02, 0x26, 0x26}},
	{0x00, 1, {0x80}},
	{0xCD, 16, {0x18, 0x17, 0x16, 0x2C, 0x07, 0x06, 0x09, 0x08, 0x2C, 0x25, 0x25, 0x26, 0x26, 0x2C, 0x24, 0x2C}},
	{0x00, 1, {0x90}},
	{0xCD, 8, {0x22, 0x23, 0x23, 0x04, 0x03, 0x02, 0x26, 0x26}},
	{0x00, 1, {0xA0}},
	{0xCC, 16, {0x18, 0x17, 0x16, 0x2C, 0x08, 0x09, 0x06, 0x07, 0x2C, 0x25, 0x25, 0x22, 0x22, 0x2C, 0x24, 0x2C}},
	{0x00, 1, {0xB0}},
	{0xCC, 8, {0x26, 0x23, 0x23, 0x04, 0x02, 0x03, 0x22, 0x22}},
	{0x00, 1, {0xA0}},
	{0xCD, 16, {0x18, 0x17, 0x16, 0x2C, 0x08, 0x09, 0x06, 0x07, 0x2C, 0x25, 0x25, 0x22, 0x22, 0x2C, 0x24, 0x2C}},
	{0x00, 1, {0xB0}},
	{0xCD, 8, {0x26, 0x23, 0x23, 0x04, 0x02, 0x03, 0x22, 0x22}},
	{0x00, 1, {0x86}},
	{0xC0, 8, {0x01, 0x01, 0x01, 0x00, 0x12, 0x12, 0x12, 0x02}},
	{0x00, 1, {0x96}},
	{0xC0, 8, {0x01, 0x02, 0x01, 0x00, 0x13, 0x13, 0x13, 0x03}},
	{0x00, 1, {0xA6}},
	{0xC0, 8, {0x01, 0x02, 0x01, 0x00, 0x12, 0x12, 0x12, 0x02}},
	{0x00, 1, {0xA3}},
	{0xCE, 6, {0x01, 0x01, 0x01, 0x00, 0x12, 0x02}},
	{0x00, 1, {0xB3}},
	{0xCE, 6, {0x00, 0x01, 0x01, 0x00, 0x12, 0x02}},
	{0x00, 1, {0x76}},
	{0xC0, 8, {0x01, 0x02, 0x01, 0x01, 0x31, 0x31, 0x31, 0x05}},
	{0x00, 1, {0x82}},
	{0xA7, 2, {0x20, 0x00}},
	{0x00, 1, {0x8D}},
	{0xA7, 1, {0x02}},
	{0x00, 1, {0x8F}},
	{0xA7, 1, {0x01}},
	{0x00, 1, {0x93}},
	{0xC5, 1, {0x37}},
	{0x00, 1, {0x97}},
	{0xC5, 1, {0x37}},
	{0x00, 1, {0x9A}},
	{0xC5, 1, {0x23}},
	{0x00, 1, {0x9C}},
	{0xC5, 1, {0x23}},
	{0x00, 1, {0xB6}},
	{0xC5, 4, {0x2D, 0x2D, 0x19, 0x19}},
	{0x00, 1, {0x00}},
	{0xD8, 2, {0x2F, 0x2F}},
	{0x00, 1, {0x88}},
	{0xC4, 1, {0x08}},
	{0x00, 1, {0x80}},
	{0xA7, 1, {0x03}},
	{0x00, 1, {0xA0}},
	{0xC3, 16, {0x00, 0x01, 0x23, 0x45, 0x21, 0x03, 0x45, 0x00, 0x00, 0x00, 0x21, 0x03, 0x45, 0x01, 0x23, 0x45}},
	{0x00, 1, {0xB1}},
	{0xF5, 1, {0x1F}},
	{0x00, 1, {0xCB}},
	{0xC0, 1, {0x01}},
	{0x00, 1, {0xCA}},
	{0xC0, 1, {0xA0}},
	{0x00, 1, {0x88}},
	{0xC4, 1, {0x08}},
	{0x00, 1, {0x9A}},
	{0xC4, 1, {0x11}},
	{0x00, 1, {0x82}},
	{0xF5, 1, {0x00}},
	{0x00, 1, {0x93}},
	{0xF5, 1, {0x00}},
	{0x00, 1, {0x9C}},
	{0xF5, 1, {0x00}},
	{0x00, 1, {0x9E}},
	{0xF5, 1, {0x00}},
	{0x00, 1, {0xA0}},
	{0xB0, 7, {0x00, 0x00, 0x00, 0x00, 0x00, 0x1D, 0x01}},
	{0x00, 1, {0x9A}},
	{0xC4, 2, {0x11, 0x08}},
	{0x00, 1, {0x93}},
	{0xE9, 2, {0xFF, 0xFF}},
	{0x00, 1, {0xE8}},
	{0xC0, 1, {0x40}},
	{0x00, 1, {0xE0}},
	{0xCF, 1, {0x34}},
	{0x00, 1, {0x85}},
	{0xA7, 1, {0x00}},
	{0x00, 1, {0x80}},
	{0xB3, 1, {0x22}},
	{0x00, 1, {0xB0}},
	{0xB3, 1, {0x00}},
	{0x00, 1, {0x83}},
	{0xB0, 1, {0x63}},
	{0x00, 1, {0xE0}},
	{0xCE, 1, {0x00}},
	{0x00, 1, {0x90}},
	{0xA7, 1, {0x00}},
	{0x00, 1, {0x87}},
	{0xC4, 2, {0x08, 0x08}},
	{0x00, 1, {0xBE}},
	{0xC5, 2, {0xC0, 0xC0}},
	{0x00, 1, {0x90}},
	{0xE9, 1, {0x50}},
	{0x00, 1, {0x81}},
	{0xA4, 2, {0x23, 0x23}},
	{0x00, 1, {0x87}},
	{0xF5, 1, {0x00}},
	{0x00, 1, {0xB0}},
	{0xB4, 14, {0x00, 0x14, 0x02, 0x00, 0x01, 0xE8, 0x00, 0x07, 0x05, 0x0E, 0x05, 0x16, 0x10, 0xF0}},
	{0x1F, 3, {0x78, 0x00, 0x10}},
	{0x00, 1, {0x0E}},
	{0xF3, 2, {0x80, 0xFF}},
	{0x00, 1, {0x80}},
	{0xCF, 10, {0x14, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00}},
	{0x00, 1, {0x90}},
	{0xCF, 10, {0x14, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00}},
	{0x00, 1, {0xA0}},
	{0xCF, 10, {0x24, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00}},
	{0x00, 1, {0x80}},
	{0xC1, 2, {0x44, 0x44}},
	{0x00, 1, {0x95}},
	{0xE9, 1, {0x80}},
	{0x00, 1, {0xB0}},
	{0xC5, 6, {0x00, 0x4A, 0x01, 0x0F, 0x4A, 0x00}},
	{0x00, 1, {0xC1}},
	{0xC5, 2, {0x55, 0x00}},
	{0x00, 1, {0xC4}},
	{0xC5, 3, {0xFF, 0x5F, 0x5F}},
	{0x00, 1, {0x80}},
	{0xA4, 1, {0xC5}},
	{0x00, 1, {0x84}},
	{0xC5, 1, {0x55}},
	{0x00, 1, {0x84}},
	{0xB0, 1, {0x00}},
	{0x00, 1, {0xF7}},
	{0xCF, 1, {0x51}},
	{0x00, 1, {0xA3}},
	{0xC0, 1, {0x52}},
	{0x00, 1, {0x8D}},
	{0xCE, 1, {0x9C}},
	{0x00, 1, {0x9D}},
	{0xCE, 1, {0x15}},
	{0x00, 1, {0x62}},
	{0xCF, 2, {0x91, 0x95}},
	{0x00, 1, {0x66}},
	{0xCF, 2, {0x71, 0x75}},
	{0x00, 1, {0xB0}},
	{0xCA, 3, {0x00, 0x00, 0x0C}},
	{0x00, 1, {0xB5}},
	{0xCA, 1, {0x04}},
	{0x00, 1, {0x00}},
	{0xFF, 3, {0xFF, 0xFF, 0xFF}},

	{0x51, 2, {0x0, 0x00}},
	{0x53, 1, {0x24}},
	{0x35, 1, {0x00}},

	{0x11, 0, {}},
	{REGFLAG_DELAY, 120, {}},
	{0x29, 0, {}},
	{REGFLAG_DELAY, 40, {}},
};
static struct LCM_setting_table lcm_deep_sleep_mode_in_setting[] = {
	{0x28, 1, {0x00}},
	{REGFLAG_DELAY, 20, {}},
	{0x10, 1, {0x00}},
	{REGFLAG_DELAY, 100, {}},
	{0x00, 1, {0x00}},
	{0xF7, 4, {0x5A, 0xA5, 0x95, 0x27}},
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
static void lcm_panel_init(struct lcm *ctx)
{
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		pr_err("[error]%s: cannot get reset_gpio %ld\n", __func__, PTR_ERR(ctx->reset_gpio));
		return;
	}
	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay(5);
	gpiod_set_value(ctx->reset_gpio, 0);
	mdelay(5);
	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay(11);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	push_table(ctx, lcm_initialization_setting,
		sizeof(lcm_initialization_setting) / sizeof(struct LCM_setting_table));

	if (mapped_level != 0) {
		if (current_fps == 144)
			lcm_dcs_write_seq_static(ctx, 0x1F, 0x90, 0x00, 0x00);
		else
			lcm_dcs_write_seq_static(ctx, 0x1F, 0x78, 0x00, 0x10);

		lcm_dcs_write(ctx, bl_tb0, ARRAY_SIZE(bl_tb0));
	}
}
static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->enabled)
		return 0;

	ctx->enabled = false;
	return 0;
}
static int lcm_unprepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->prepared)
		return 0;
	push_table(ctx, lcm_deep_sleep_mode_in_setting,
		sizeof(lcm_deep_sleep_mode_in_setting) / sizeof(struct LCM_setting_table));

	printk("g_shutdown_flag=%d\n", g_shutdown_flag);
	if (!g_shutdown_flag) {
		ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->reset_gpio)) {
			pr_err("[error]%s: cannot get reset_gpio %ld\n", __func__, PTR_ERR(ctx->reset_gpio));
		}
		gpiod_set_value(ctx->reset_gpio, 0);
		devm_gpiod_put(ctx->dev, ctx->reset_gpio);
		mdelay(5);

		ctx->bias_neg = devm_gpiod_get_index(ctx->dev, "bias", 1, GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_neg)) {
			pr_err("[error]%s: cannot get bias_neg %ld\n", __func__, PTR_ERR(ctx->bias_neg));
			return PTR_ERR(ctx->bias_neg);
		}
		gpiod_set_value(ctx->bias_neg, 0);
		devm_gpiod_put(ctx->dev, ctx->bias_neg);
		mdelay(5);
		ctx->bias_pos = devm_gpiod_get_index(ctx->dev, "bias", 0, GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_pos)) {
			pr_err("[error]%s: cannot get bias_pos %ld\n", __func__, PTR_ERR(ctx->bias_pos));
			return PTR_ERR(ctx->bias_pos);
		}
		gpiod_set_value(ctx->bias_pos, 0);
		devm_gpiod_put(ctx->dev, ctx->bias_pos);
		tran_is_lcm_poweroff = 1;
	}

	ctx->error = 0;
	ctx->prepared = false;
	return 0;
}
static void lcm_shutdown(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);

	if (!tran_is_lcm_poweroff) {
		pr_info("%s  lcm poweroff = %d\n", __func__, tran_is_lcm_poweroff);
		ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->reset_gpio)) {
			pr_err("[error]%s: cannot get reset_gpio %ld\n", __func__, PTR_ERR(ctx->reset_gpio));
		}
		gpiod_set_value(ctx->reset_gpio, 0);
		devm_gpiod_put(ctx->dev, ctx->reset_gpio);
		mdelay(5);
		ctx->bias_neg = devm_gpiod_get_index(ctx->dev, "bias", 1, GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_neg)) {
			pr_err("[error]%s: cannot get bias_neg %ld\n", __func__, PTR_ERR(ctx->bias_neg));
		}
		gpiod_set_value(ctx->bias_neg, 0);
		devm_gpiod_put(ctx->dev, ctx->bias_neg);
		mdelay(3);
		ctx->bias_pos = devm_gpiod_get_index(ctx->dev, "bias", 0, GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_pos)) {
			pr_err("[error]%s: cannot get bias_pos %ld\n", __func__, PTR_ERR(ctx->bias_pos));
		}
		gpiod_set_value(ctx->bias_pos, 0);
		devm_gpiod_put(ctx->dev, ctx->bias_pos);
		tran_is_lcm_poweroff = 1;
		g_shutdown_flag = 0;
	}
}
static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;
	int i = 0;

	if (ctx->prepared)
		return 0;
	if (IS_ERR_OR_NULL(ctx->vio18_regulator)) {
		ctx->vio18_regulator = devm_regulator_get(ctx->dev, "vio18");

		if (IS_ERR_OR_NULL(ctx->vio18_regulator)) {
			ret = PTR_ERR(ctx->vio18_regulator);
			dev_err(ctx->dev, "<%s,%d> lcm vci get fail!ret[%d].\n", __func__, __LINE__, ret);
			return 0;
		}
	}

	if (tran_is_lcm_poweroff) {
		ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->reset_gpio)) {
			pr_err("[error]%s: cannot get reset_gpio %ld\n", __func__, PTR_ERR(ctx->reset_gpio));
		}
		gpiod_set_value(ctx->reset_gpio, 0);
		devm_gpiod_put(ctx->dev, ctx->reset_gpio);
		mdelay(5);
		ctx->bias_pos = devm_gpiod_get_index(ctx->dev, "bias", 0, GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_pos)) {
			pr_err("[error]%s: cannot get bias_pos %ld\n", __func__, PTR_ERR(ctx->bias_pos));
			return PTR_ERR(ctx->bias_pos);
		}
		gpiod_set_value(ctx->bias_pos, 1);
		devm_gpiod_put(ctx->dev, ctx->bias_pos);

		mdelay(5);

		ctx->bias_neg = devm_gpiod_get_index(ctx->dev, "bias", 1, GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_neg)) {
			pr_err("[error]%s: cannot get bias_neg %ld\n", __func__, PTR_ERR(ctx->bias_neg));
			return PTR_ERR(ctx->bias_neg);
		}
		gpiod_set_value(ctx->bias_neg, 1);
		devm_gpiod_put(ctx->dev, ctx->bias_neg);
		mdelay(2);
		for (i = 0; i < ARRAY_SIZE(ocp2131_cmd_data); i++) {
			tran_panel_i2c_write_bytes(ocp2131_cmd_data[i].cmd, ocp2131_cmd_data[i].data);
		}
		mdelay(5);
		tran_is_lcm_poweroff = 0;
	}
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

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb,
	void *handle, unsigned int level)
{
	if (level > 5119)
		level = 5119;

	if (level < 0)
		level = 0;

	if (level == 0) {
		cb(dsi, handle, bl_dimming_disable, ARRAY_SIZE(bl_dimming_disable));
		g_dimming_enable = 0;
	} else if (!g_dimming_enable && mapped_level != 0) {
		cb(dsi, handle, bl_dimming_enable, ARRAY_SIZE(bl_dimming_enable));
		g_dimming_enable = 1;
	}
	if (level <= 4095)
		mapped_level = level * 3276 / 4095;
	else
		mapped_level = 3276 + ((level-4096) * (4095-3276) / 1023);

	bl_tb0[1] = ((mapped_level >> 4) & 0xff);
	bl_tb0[2] = (mapped_level & 0x0f);

	if (!cb)
		return -1;

	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));
	pr_info("[LCM] %s level = %d, mapped_level = %d\n", __func__, level, mapped_level);

	return 0;
}

static int lcm_enable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (ctx->enabled)
		return 0;

	ctx->enabled = true;
	return 0;
}
static const struct drm_display_mode switch_mode_144hz = {
	.clock       = ((FRAME_WIDTH  + MODE_144_HFP + HSA + MODE_144_HBP) * (FRAME_HEIGHT + MODE_144_VFP + VSA + VBP) * MODE_144_FPS / 1000),
	.hdisplay    = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH  + MODE_144_HFP,
	.hsync_end   = FRAME_WIDTH  + MODE_144_HFP + HSA,
	.htotal      = FRAME_WIDTH  + MODE_144_HFP + HSA + MODE_144_HBP,
	.vdisplay    = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + MODE_144_VFP,
	.vsync_end   = FRAME_HEIGHT + MODE_144_VFP + VSA,
	.vtotal      = FRAME_HEIGHT + MODE_144_VFP + VSA + VBP,
};
static const struct drm_display_mode switch_mode_120hz = {
	.clock       = ((FRAME_WIDTH  + HFP + HSA + HBP) * (FRAME_HEIGHT + MODE_120_VFP + VSA + VBP) * MODE_120_FPS / 1000),
	.hdisplay    = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH  + HFP,
	.hsync_end   = FRAME_WIDTH  + HFP + HSA,
	.htotal      = FRAME_WIDTH  + HFP + HSA + HBP,
	.vdisplay    = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + MODE_120_VFP,
	.vsync_end   = FRAME_HEIGHT + MODE_120_VFP + VSA,
	.vtotal      = FRAME_HEIGHT + MODE_120_VFP + VSA + VBP,
};
static const struct drm_display_mode switch_mode_90hz = {
	.clock       = ((FRAME_WIDTH  + HFP + HSA + HBP) * (FRAME_HEIGHT + MODE_90_VFP + VSA + VBP) * MODE_90_FPS / 1000),
	.hdisplay    = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH  + HFP,
	.hsync_end   = FRAME_WIDTH  + HFP + HSA,
	.htotal      = FRAME_WIDTH  + HFP + HSA + HBP,
	.vdisplay    = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + MODE_90_VFP,
	.vsync_end   = FRAME_HEIGHT + MODE_90_VFP + VSA,
	.vtotal      = FRAME_HEIGHT + MODE_90_VFP + VSA + VBP,
};
static const struct drm_display_mode switch_mode_60hz = {
	.clock       = ((FRAME_WIDTH  + HFP + HSA + HBP) * (FRAME_HEIGHT + MODE_60_VFP + VSA + VBP) * MODE_60_FPS / 1000),
	.hdisplay    = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH  + HFP,
	.hsync_end   = FRAME_WIDTH  + HFP + HSA,
	.htotal      = FRAME_WIDTH  + HFP + HSA + HBP,
	.vdisplay    = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + MODE_60_VFP,
	.vsync_end   = FRAME_HEIGHT + MODE_60_VFP + VSA,
	.vtotal      = FRAME_HEIGHT + MODE_60_VFP + VSA + VBP,
};
#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params_144hz = {
	.cust_esd_check = 0,
	.esd_check_enable = 1,
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.ssc_enable = 0,
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
		.pic_height            =  DSC_PIC_HEIGHT,
		.pic_width             =  DSC_PIC_WIDTH,
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
	},
	.data_rate = DATA_RATE,
	.change_fps_by_vfp_send_cmd = 1,
	.dyn_fps = {
		.switch_en = 0,
		.dfps_cmd_table[0] = {0, 4, {0x1F, 0x90, 0x00, 0x00}},
	},
	.tran_panel_params = &panel_driver_status,
};
static struct mtk_panel_params ext_params_120hz = {
	.cust_esd_check = 0,
	.esd_check_enable = 1,
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.ssc_enable = 0,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.vfp_low_power = VFP_45HZ,
	.wait_sof_before_dec_vfp = 1,
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
		.pic_height            =  DSC_PIC_HEIGHT,
		.pic_width             =  DSC_PIC_WIDTH,
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
	},
	.data_rate = DATA_RATE,
	.change_fps_by_vfp_send_cmd = 1,
	.dyn_fps = {
		.switch_en = 0,
		.dfps_cmd_table[0] = {0, 4, {0x1F, 0x78, 0x00, 0x10}},
	},
	.tran_panel_params = &panel_driver_status,
};
static struct mtk_panel_params ext_params_90hz = {
	.cust_esd_check = 0,
	.esd_check_enable = 1,
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.ssc_enable = 0,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.vfp_low_power = VFP_45HZ,
	.wait_sof_before_dec_vfp = 1,
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
		.pic_height            =  DSC_PIC_HEIGHT,
		.pic_width             =  DSC_PIC_WIDTH,
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
	},
	.data_rate = DATA_RATE,
	.change_fps_by_vfp_send_cmd = 1,
	.dyn_fps = {
		.switch_en = 0,
		.dfps_cmd_table[0] = {0, 4, {0x1F, 0x78, 0x00, 0x10}},
	},
	.tran_panel_params = &panel_driver_status,
};
static struct mtk_panel_params ext_params_60hz = {
	.cust_esd_check = 0,
	.esd_check_enable = 1,
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.ssc_enable = 0,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.vfp_low_power = VFP_45HZ,
	.wait_sof_before_dec_vfp = 1,
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
		.pic_height            =  DSC_PIC_HEIGHT,
		.pic_width             =  DSC_PIC_WIDTH,
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
	},
	.data_rate = DATA_RATE,
	.change_fps_by_vfp_send_cmd = 1,
	.dyn_fps = {
		.switch_en = 0,
		.dfps_cmd_table[0] = {0, 4, {0x1F, 0x78, 0x00, 0x10}},
	},
	.tran_panel_params = &panel_driver_status,
};
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

	if (drm_mode_vrefresh(m) == MODE_144_FPS)
		ext->params = &ext_params_144hz;
	else if (drm_mode_vrefresh(m) == MODE_120_FPS)
		ext->params = &ext_params_120hz;
	else if (drm_mode_vrefresh(m) == MODE_90_FPS)
		ext->params = &ext_params_90hz;
	else if (drm_mode_vrefresh(m) == MODE_60_FPS)
		ext->params = &ext_params_60hz;
	else
		ret = 1;
	if (!ret)
		current_fps = drm_mode_vrefresh(m);
	return ret;
}
static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);

	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		pr_err("[error]%s: cannot get reset_gpio %ld\n", __func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, on);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	return 0;
}
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
	pr_info("ATA read data %x %x %x\n", data[0], data[1], data[2]);
	if (data[0] == id[0])
		return 1;
	pr_info("ATA expect read data is %x %x %x\n", id[0], id[1], id[2]);
	return 0;
}

static int panel_doze_enable(struct drm_panel *panel,
		    void *dsi, dcs_write_gce cb, void *handle)
{
	pr_info("[LCM] %s begin\n", __func__);

	pr_info("[LCM] %s end\n", __func__);
	return 0;
}

static int panel_doze_disable(struct drm_panel *panel,
		    void *dsi, dcs_write_gce cb, void *handle)
{
	pr_info("[LCM] %s begin\n", __func__);
	pr_info("[LCM] %s end\n", __func__);
	return 0;
}

#if IS_ENABLED(CONFIG_TRANSSION_DOZE_BRIGHTNESS_SUPPORT)
static int panel_set_aod_light_mode(void *dsi, dcs_write_gce cb,
	void *handle, unsigned int level)
{
	unsigned int aod_mapped_level = 0;

	if (level <= 4095)
		aod_mapped_level = level * 3276 / 4095;
	else
		aod_mapped_level = 3276 + ((level-4096) * (4095-3276) / 1023);
	pr_info("[LCM] %s level is %u,aod_mapped_level is %d\n", __func__, level, aod_mapped_level);

	bl_tb0[1] = ((aod_mapped_level >> 4) & 0xff);
	bl_tb0[2] = (aod_mapped_level & 0x0f);
	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));

	return 0;
}
#endif
static struct mtk_panel_funcs ext_funcs = {
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.reset = panel_ext_reset,
	.ext_param_set = mtk_panel_ext_param_set,
	.ata_check = panel_ata_check,
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

static int lcm_get_modes(struct drm_panel *panel, struct drm_connector *connector)
{
	struct drm_display_mode *mode_144Hz;
	struct drm_display_mode *mode_120Hz;
	struct drm_display_mode *mode_90Hz;
	struct drm_display_mode *mode_60Hz;

	mode_144Hz = drm_mode_duplicate(connector->dev, &switch_mode_144hz);
	if (!mode_144Hz) {
		dev_err(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			switch_mode_144hz.hdisplay, switch_mode_144hz.vdisplay, drm_mode_vrefresh(&switch_mode_144hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode_144Hz);
	mode_144Hz->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode_144Hz);
	mode_120Hz = drm_mode_duplicate(connector->dev, &switch_mode_120hz);
	if (!mode_120Hz) {
		dev_err(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			switch_mode_120hz.hdisplay, switch_mode_120hz.vdisplay, drm_mode_vrefresh(&switch_mode_120hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode_120Hz);
	mode_120Hz->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode_120Hz);
	mode_90Hz = drm_mode_duplicate(connector->dev, &switch_mode_90hz);
	if (!mode_90Hz) {
		dev_err(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			switch_mode_90hz.hdisplay, switch_mode_90hz.vdisplay, drm_mode_vrefresh(&switch_mode_90hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode_90Hz);
	mode_90Hz->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode_90Hz);
	mode_60Hz = drm_mode_duplicate(connector->dev, &switch_mode_60hz);
	if (!mode_60Hz) {
		dev_err(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			switch_mode_60hz.hdisplay, switch_mode_60hz.vdisplay, drm_mode_vrefresh(&switch_mode_60hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode_60Hz);
	mode_60Hz->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode_60Hz);
	connector->display_info.width_mm = 69;
	connector->display_info.height_mm = 157;
	return 4;
}

static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable = lcm_disable,
	.unprepare = lcm_unprepare,
	.prepare = lcm_prepare,
	.enable = lcm_enable,
	.get_modes = lcm_get_modes,
};

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *backlight;
	struct lcm *ctx;
	int ret;
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;
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
	ctx = devm_kzalloc(dev, sizeof(struct lcm), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags =  MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE;

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);
		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	if (IS_ERR_OR_NULL(ctx->vio18_regulator)) {
		ctx->vio18_regulator = devm_regulator_get(ctx->dev, "vio18");

		if (IS_ERR_OR_NULL(ctx->vio18_regulator)) {
			ret = PTR_ERR(ctx->vio18_regulator);
			dev_err(ctx->dev, "<%s,%d> lcm vio18 get fail!ret[%d].\n", __func__, __LINE__, ret);
			return 0;
		}
	}
	ret = regulator_enable(ctx->vio18_regulator);

	ctx->bias_pos = devm_gpiod_get_index(dev, "bias", 0, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_pos)) {
		pr_err("[error]%s: cannot get bias_pos %ld\n", __func__, PTR_ERR(ctx->bias_pos));
		return PTR_ERR(ctx->bias_pos);
	}
	devm_gpiod_put(dev, ctx->bias_pos);

	ctx->bias_neg = devm_gpiod_get_index(dev, "bias", 1, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_neg)) {
		pr_err("[error]%s: cannot get bias_neg %ld\n", __func__, PTR_ERR(ctx->bias_neg));
		return PTR_ERR(ctx->bias_neg);
	}
	devm_gpiod_put(dev, ctx->bias_neg);
	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		pr_err("[error]%s: cannot get reset-gpios %ld\n", __func__, PTR_ERR(ctx->reset_gpio));
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
	ret = mtk_panel_ext_create(dev, &ext_params_120hz, &ext_funcs, &ctx->panel);
	if (ret < 0) {
		pr_err("%s create new panel params fail\n", __func__);
		return ret;
	}
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
	pr_info("[LCM] %s begin\n", __func__);
	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_detach(ext_ctx);
	mtk_panel_remove(ext_ctx);
#endif
}

static const struct of_device_id lcm_of_match[] = {
	{
		.compatible = "ft8725,fhdp,dsi,vdo,txd,boe,144hz,lj7",
	},
	{}
};

MODULE_DEVICE_TABLE(of, lcm_of_match);
static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "ft8725_fhdp_dsi_vdo_txd_boe_144hz_lj7",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
	.shutdown = lcm_shutdown,
};

module_mipi_dsi_driver(lcm_driver);
MODULE_AUTHOR("Transsion Inc.");
MODULE_DESCRIPTION("transsion, panel driver");
MODULE_LICENSE("GPL v2");
