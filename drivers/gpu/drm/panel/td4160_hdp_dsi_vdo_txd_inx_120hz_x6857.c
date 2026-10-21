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
#define FRAME_WIDTH                     (720)
#define FRAME_HEIGHT                    (1600)
#define PHYSICAL_WIDTH                  (69550)
#define PHYSICAL_HEIGHT                 (154560)

#define MIPI_CLOCK 595
#define DATA_RATE 1190
#define VSA 4
#define VBP 30
#define HSA 4
#define HBP 53
#define HFP 54
#define MODE_0_FPS 120
#define MODE_0_VFP 180
#define MODE_1_FPS 90
#define MODE_1_VFP 784
#define MODE_2_FPS 60
#define MODE_2_VFP 1994

struct CW8762_SETTING_TABLE {
	unsigned char cmd;
	unsigned char data;
};

static struct CW8762_SETTING_TABLE cw8762_cmd_data[2] = {
	{ 0x00, 0x14 },
	{ 0x01, 0x14 },
};


struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *bias_pos;
	struct gpio_desc *bias_neg;
	struct regulator *vio18_regulator;
	struct gpio_desc *tp_reset;
	bool prepared;
	bool enabled;
	int error;
};
struct tran_panel_driver_params panel_driver_status = {0};
tran_lcm_doze_backlight g_lcm_doze_backlight = {
	.doze_backlight_num = 0,
	.doze_backlight_level1 = 0,
	.doze_backlight_level2 = 0,
	.doze_backlight_level3 = 0,
};

static char bl_dim[] = {0x53, 0x24};
static char bl_tb0[] = {0x51, 0x0C, 0xF3};
static char bl_dim_tb0[] = {0x13};
static unsigned int g_aod_enable;


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
	unsigned char para_list[256];
};

static struct LCM_setting_table lcm_initialization_setting[] = {
	{0xB0, 1, {0x04}},
	{0xD6, 1, {0x00}},
	{0xB6, 6, {0x30, 0x6b, 0x00, 0x86, 0xc3, 0x03}},
	{0xB7, 4, {0x31, 0x00, 0x00, 0x00}},
	{0xB8, 6, {0x00, 0x78, 0x64, 0x10, 0x64, 0xb4}},
	{0xB9, 6, {0x00, 0x78, 0x64, 0x10, 0x64, 0xb4}},
	{0xBA, 6, {0x00, 0x78, 0x64, 0x10, 0x64, 0xb4}},
	{0xBB, 3, {0x00, 0xb4, 0xa0}},
	{0xBC, 3, {0x00, 0xb4, 0xa0}},
	{0xBD, 3, {0x00, 0xb4, 0xa0}},
	{0xBE, 13, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0xC0, 24, {0x00, 0x41, 0x22, 0x06, 0x40, 0x00, 0x10, 0x06, 0x94, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0xC1, 39, {0x30, 0x81, 0x50, 0xfa, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x40, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0xC2, 244, {0x03, 0xc0, 0x42, 0x14, 0x07, 0x10, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x20, 0x3a, 0x0c, 0x0c, 0x07, 0x00, 0x41, 0x00, 0x66, 0x5b, 0x03, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x20, 0x3a, 0x0a, 0x0a, 0x07, 0x00, 0x01, 0x65, 0xc6, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0xC3, 105, {0x00, 0x00, 0x00, 0xc3, 0x01, 0x80, 0xf8, 0x3b, 0xf8, 0x18, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0xC4, 84, {0x00, 0x07, 0x07, 0x2B, 0x23, 0x25, 0x27, 0x29, 0x5D, 0x61, 0x13, 0x15, 0x17, 0x19, 0x1B, 0x1D, 0x03, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x04, 0x1E, 0x1C, 0x1A, 0x18, 0x16, 0x14, 0x61, 0x5D, 0x2A, 0x28, 0x26, 0x24, 0x2C, 0x07, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55}},
	{0xC5, 5, {0x08, 0x00, 0x00, 0x00, 0x00}},
	{0xC6, 76, {0x00, 0x00, 0x00, 0x00, 0x01, 0x22, 0x04, 0x22, 0x01, 0x00, 0x5d, 0x00, 0x00, 0x00, 0x01, 0x00, 0x5d, 0x00, 0x01, 0x05, 0x01, 0x0b, 0x01, 0x35, 0xff, 0x8f, 0x06, 0x15, 0x01, 0xc0, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x20, 0x00, 0x00, 0x00, 0xc0, 0x11, 0x00, 0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x50, 0x00, 0x33, 0x03, 0x00, 0x00}},
	{0xCB, 30, {0x02, 0xd0, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff}},
	{0xCE, 24, {0x5d, 0x40, 0x43, 0x49, 0x55, 0x62, 0x71, 0x82, 0x94, 0xa8, 0xb9, 0xcb, 0xdb, 0xe9, 0xf5, 0xfc, 0xff, 0x08, 0x6a, 0x04, 0x04, 0x00, 0x04, 0x8c}},
	{0xCF, 1, {0x00}},
	{0xD0, 20, {0xc1, 0x46, 0x81, 0x66, 0x09, 0x90, 0x00, 0xcc, 0xf2, 0xff, 0x11, 0x46, 0x06, 0x7e, 0x09, 0x08, 0xcc, 0x1b, 0xf0, 0x06}},
	{0xD1, 37, {0xd4, 0xd4, 0x1b, 0x33, 0x33, 0x17, 0x07, 0xbb, 0x55, 0x55, 0x55, 0x55, 0x00, 0x3b, 0x77, 0x07, 0x3b, 0x30, 0x06, 0x72, 0x33, 0x13, 0x00, 0xd7, 0x0c, 0x55, 0x02, 0x00, 0x18, 0x70, 0x18, 0x77, 0x11, 0x11, 0x11, 0x20, 0x20}},
	{0xD2, 3, {0x00, 0x00, 0x00}},
	{0xD3, 153, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xf7, 0xff}},
	{0xD4, 23, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0xD7, 46, {0x29, 0x00, 0x12, 0x12, 0x00, 0x41, 0x00, 0x18, 0x00, 0x41, 0x00, 0x18, 0x03, 0x83, 0x80, 0x85, 0x85, 0x85, 0x87, 0x84, 0x45, 0x86, 0x87, 0x80, 0x88, 0x86, 0x89, 0x83, 0x83, 0x87, 0x84, 0x88, 0x8a, 0x0c, 0x0b, 0x0a, 0x0a, 0x0a, 0x07, 0x07, 0x06, 0x06, 0x00, 0x08, 0x0a, 0x0a}},
	{0xD8, 22, {0x40, 0x99, 0x26, 0xed, 0x16, 0x6c, 0x16, 0x6c, 0x16, 0x6c, 0x00, 0x14, 0x00, 0x14, 0x00, 0x14, 0x01, 0x0c, 0x00, 0x00, 0x01, 0x00}},
	{0xD9, 40, {0x01, 0x02, 0x5e, 0x18, 0x00, 0x20, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0xDD, 4, {0x30, 0x06, 0x23, 0x65}},
	{0xDE, 14, {0x00, 0x00, 0x00, 0x0f, 0xff, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00}},
	{0xE6, 8, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0xEA, 35, {0x02, 0x0e, 0x0e, 0x06, 0x80, 0x07, 0x40, 0x74, 0x00, 0x00, 0x00, 0x03, 0x33, 0x07, 0x0a, 0x00, 0x5e, 0x00, 0x5e, 0x00, 0x5e, 0x01, 0x28, 0x01, 0x28, 0x00, 0x5e, 0x00, 0x5e, 0x00, 0x5e, 0x00, 0x0c, 0x02, 0x00}},
	{0xEB, 8, {0x09, 0xd0, 0x9d, 0x00, 0x01, 0x00, 0x01, 0x01}},
	{0xEC, 23, {0x05, 0xf0, 0x01, 0x70, 0x6b, 0x07, 0x40, 0x74, 0x00, 0x00, 0x00, 0x02, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0xED, 33, {0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x67, 0x37, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xb0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x10, 0x00}},
	{0xEE, 121, {0x05, 0x00, 0x55, 0x00, 0xc0, 0x5f, 0x00, 0xc0, 0x5f, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0xc0, 0x5f, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x5f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0xc0, 0x5f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02}},
	{0xB0, 1, {0x03}},
	{0x55, 1, {0x00}},
	{0x53, 1, {0x24}},
	{0x51, 2, {0x00, 0x00}},
	{0x35, 1, {0x00}},

	{0x11, 0, {}},
	{REGFLAG_DELAY, 100, {}},
	{0x29, 0, {}},
	{REGFLAG_DELAY, 20, {}},
};

static struct LCM_setting_table lcm_deep_sleep_mode_in_setting[] = {
	{0x28, 0, {}},
	{REGFLAG_DELAY, 20, {}},
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
	mdelay(2);
	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay(22);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	push_table(ctx, lcm_initialization_setting,
		sizeof(lcm_initialization_setting) / sizeof(struct LCM_setting_table));
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->enabled)
		return 0;
/* For OLED backlight set
	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}
*/
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
		ctx->reset_gpio =
			devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->reset_gpio)) {
			pr_err("[error]%s: cannot get reset_gpio %ld\n", __func__, PTR_ERR(ctx->reset_gpio));
			return PTR_ERR(ctx->reset_gpio);
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
		mdelay(1);
		ctx->tp_reset = devm_gpiod_get(ctx->dev, "tp", GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->tp_reset)) {
			pr_err("[error]%s: cannot get tp_reset %ld\n", __func__, PTR_ERR(ctx->tp_reset));
		} else {
			gpiod_set_value(ctx->tp_reset, 0);
			devm_gpiod_put(ctx->dev, ctx->tp_reset);
			pr_info("%s : pull down tp reset done.", __func__);
		}
		mdelay(5);

		ctx->bias_neg = devm_gpiod_get_index(ctx->dev, "bias", 1, GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_neg)) {
			pr_err("[error]%s: cannot get bias_neg %ld\n", __func__, PTR_ERR(ctx->bias_neg));
		}
		gpiod_set_value(ctx->bias_neg, 0);
		devm_gpiod_put(ctx->dev, ctx->bias_neg);

		mdelay(5);

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

	if (tran_is_lcm_poweroff) {
		ctx->bias_pos = devm_gpiod_get_index(ctx->dev, "bias", 0, GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_pos)) {
			pr_err("[error]%s: cannot get bias_pos %ld\n", __func__, PTR_ERR(ctx->bias_pos));
			return PTR_ERR(ctx->bias_pos);
		}
		gpiod_set_value(ctx->bias_pos, 1);
		devm_gpiod_put(ctx->dev, ctx->bias_pos);

		mdelay(2);

		ctx->bias_neg = devm_gpiod_get_index(ctx->dev, "bias", 1, GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_neg)) {
			pr_err("[error]%s: cannot get bias_neg %ld\n", __func__, PTR_ERR(ctx->bias_neg));
			return PTR_ERR(ctx->bias_neg);
		}
		gpiod_set_value(ctx->bias_neg, 1);
		devm_gpiod_put(ctx->dev, ctx->bias_neg);
		mdelay(2);
		for (i = 0; i < ARRAY_SIZE(cw8762_cmd_data); i++) {
			tran_panel_i2c_write_bytes(cw8762_cmd_data[i].cmd, cw8762_cmd_data[i].data);
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
static unsigned int g_hbm_state;
static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb,
	void *handle, unsigned int level)
{
	unsigned int mapped_level;

	if (level > 5119)
		level = 5119;

	if (level < 0)
		level = 0;

	if (level <= 4095)
		mapped_level = level * 3276 / 4095;
	else
		mapped_level = 3276 + ((level-4096) * (4095-3276) / 1023);

	if ((g_hbm_state == 0) && (level > 4095)) {
		g_hbm_state = 1;
		mdelay(32);
		bl_dim[1] = 0x2c;
		cb(dsi, handle, bl_dim, ARRAY_SIZE(bl_dim));
	}

	bl_tb0[1] = ((mapped_level >> 8) & 0x0f);
	bl_tb0[2] = (mapped_level & 0xff);

	if (!cb)
		return -1;

	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));
	cb(dsi, handle, bl_dim_tb0, ARRAY_SIZE(bl_dim_tb0));

	if ((g_hbm_state == 1) && (level <= 4094)) {
		g_hbm_state = 0;
		mdelay(32);
		bl_dim[1] = 0x24;
		cb(dsi, handle, bl_dim, ARRAY_SIZE(bl_dim));
		pr_info("[LCM] g_hbm_state:%d,and dim is closed\n", g_hbm_state);
	}

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

static const struct drm_display_mode switch_mode_120hz = {
	.clock       = ((FRAME_WIDTH  + HFP + HSA + HBP) * (FRAME_HEIGHT + MODE_0_VFP + VSA + VBP) * MODE_0_FPS / 1000),
	.hdisplay    = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH  + HFP,
	.hsync_end   = FRAME_WIDTH  + HFP + HSA,
	.htotal      = FRAME_WIDTH  + HFP + HSA + HBP,
	.vdisplay    = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + MODE_0_VFP,
	.vsync_end   = FRAME_HEIGHT + MODE_0_VFP + VSA,
	.vtotal      = FRAME_HEIGHT + MODE_0_VFP + VSA + VBP,
};

static const struct drm_display_mode switch_mode_90hz = {
	.clock       = ((FRAME_WIDTH  + HFP + HSA + HBP) * (FRAME_HEIGHT + MODE_1_VFP + VSA + VBP) * MODE_1_FPS / 1000),
	.hdisplay    = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH  + HFP,
	.hsync_end   = FRAME_WIDTH  + HFP + HSA,
	.htotal      = FRAME_WIDTH  + HFP + HSA + HBP,
	.vdisplay    = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + MODE_1_VFP,
	.vsync_end   = FRAME_HEIGHT + MODE_1_VFP + VSA,
	.vtotal      = FRAME_HEIGHT + MODE_1_VFP + VSA + VBP,
};

static const struct drm_display_mode switch_mode_60hz = {
	.clock       = ((FRAME_WIDTH  + HFP + HSA + HBP) * (FRAME_HEIGHT + MODE_2_VFP + VSA + VBP) * MODE_2_FPS / 1000),
	.hdisplay    = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH  + HFP,
	.hsync_end   = FRAME_WIDTH  + HFP + HSA,
	.htotal      = FRAME_WIDTH  + HFP + HSA + HBP,
	.vdisplay    = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + MODE_2_VFP,
	.vsync_end   = FRAME_HEIGHT + MODE_2_VFP + VSA,
	.vtotal      = FRAME_HEIGHT + MODE_2_VFP + VSA + VBP,
};

#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params_120hz = {
	.esd_check_enable = 0,
	.cust_esd_check = 0,
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.data_rate = DATA_RATE,
	.tran_panel_params = &panel_driver_status,
	.dyn = {
		.switch_en = 1,
		.pll_clk = 576,
		.hbp = 42,
		.hfp = 40,
	},
};

static struct mtk_panel_params ext_params_90hz = {
	.esd_check_enable = 0,
	.cust_esd_check = 0,
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.data_rate = DATA_RATE,
	.tran_panel_params = &panel_driver_status,
	.dyn = {
		.switch_en = 1,
		.pll_clk = 576,
		.hbp = 42,
		.hfp = 40,
	},
};

static struct mtk_panel_params ext_params_60hz = {
	.esd_check_enable = 0,
	.cust_esd_check = 0,

	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.data_rate = DATA_RATE,
	.tran_panel_params = &panel_driver_status,
	.dyn = {
		.switch_en = 1,
		.pll_clk = 576,
		.hbp = 42,
		.hfp = 40,
	},
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
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id(connector, mode);

	if (drm_mode_vrefresh(m) == MODE_0_FPS)
		ext->params = &ext_params_120hz;
	else if (drm_mode_vrefresh(m) == MODE_1_FPS)
		ext->params = &ext_params_90hz;
	else if (drm_mode_vrefresh(m) == MODE_2_FPS)
		ext->params = &ext_params_60hz;
	else
		ret = 1;

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

	if (level <= 4095)
		aod_mapped_level = level * 3276 / 4095;
	else
		aod_mapped_level = 3276 + ((level-4096) * (4095-3276) / 1023);
	pr_info("[LCM] %s level is %u,aod_mapped_level is %d\n", __func__, level, aod_mapped_level);

	bl_tb0[1] = ((aod_mapped_level >> 8) & 0x0f);
	bl_tb0[2] = (aod_mapped_level & 0xff);
	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));
	cb(dsi, handle, bl_dim_tb0, ARRAY_SIZE(bl_dim_tb0));

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
	struct drm_display_mode *mode_120Hz;
	struct drm_display_mode *mode_90Hz;
	struct drm_display_mode *mode_60Hz;

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
	mode_90Hz->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
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
	connector->display_info.height_mm = 154;

	return 3;
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
	ret = regulator_set_voltage(ctx->vio18_regulator, 1800000, 1800000);
	if (ret < 0)
		dev_err(ctx->dev, "<%s:%d>set lcm vio18 vol error!ret[%d]\n", __func__, __LINE__, ret);
	ret = regulator_enable(ctx->vio18_regulator);
	if (ret < 0)
		dev_err(ctx->dev, "<%s:%d>enable lcm vio18 error!ret[%d]\n", __func__, __LINE__, ret);
		mdelay(12);
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
		.compatible = "td4160,hdp,dsi,vdo,txd,inx,120hz,x6857",
	},
	{}
};
MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
			.name = "td4160_hdp_dsi_vdo_txd_inx_120hz_x6857",
			.owner = THIS_MODULE,
			.of_match_table = lcm_of_match,
		},
	.shutdown = lcm_shutdown,
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("Transsion Inc.");
MODULE_DESCRIPTION("transsion, panel driver");
MODULE_LICENSE("GPL v2");
