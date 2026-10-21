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
#include <linux/workqueue.h>

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mediatek_v2/mtk_panel_ext.h"
#include "../mediatek/mediatek_v2/mtk_log.h"
#include "../mediatek/mediatek_v2/mtk_drm_graphics_base.h"
#endif

#ifdef CONFIG_MTK_ROUND_CORNER_SUPPORT
#include "../mediatek/mediatek_v2/mtk_corner_pattern/mtk_data_hw_roundedpattern.h"
#endif

#define __IS_10BIT__		1
#define LCM_MIPISWITCH_TE_DELAY 1
#define NT37900_MODE_NUM	6


#define REGFLAG_CMD			 0xFFFA
#define REGFLAG_DELAY		   0xFFFC
#define REGFLAG_UDELAY		  0xFFFB
#define REGFLAG_END_OF_TABLE	0xFFFD
#define REGFLAG_RESET_LOW	   0xFFFE
#define REGFLAG_RESET_HIGH	  0xFFFF

#define MODE_0_DATA_RATE (1448)
#define MODE_1_DATA_RATE (1156)
#define MODE_2_DATA_RATE (1000)

#define FRAME_WIDTH			 2296
#define FRAME_HEIGHT			2000

#define PHYSICAL_WIDTH		  150296
#define PHYSICAL_HEIGHT		 130920

#define DATA_RATE			   822
#define HSA					 0
#define HBP					 20
#define VSA					 0
#define VBP					 9

#define MODE_0_FPS			  120
#define MODE_0_VFP			  23
#define MODE_0_HFP			  20

#define MODE_1_FPS				  90
#define MODE_2_FPS				  60
#define MODE_3_FPS				  45
#define MODE_4_FPS				  30
#define MODE_5_FPS				  10

#if !__IS_10BIT__
#define DSC_ENABLE				  1
#define DSC_VER					 17
#define DSC_SLICE_MODE			  1
#define DSC_RGB_SWAP				0
#define DSC_DSC_CFG				 34
#define DSC_RCT_ON				   1
#define DSC_BIT_PER_CHANNEL		 8
#define DSC_DSC_LINE_BUF_DEPTH	  9
#define DSC_BP_ENABLE			   1
#define DSC_BIT_PER_PIXEL		   128
#define DSC_SLICE_HEIGHT			16
#define DSC_SLICE_WIDTH			 1148
#define DSC_CHUNK_SIZE			  1148
#define DSC_XMIT_DELAY			  512
#define DSC_DEC_DELAY			   831
#define DSC_SCALE_VALUE			 32
#define DSC_INCREMENT_INTERVAL	  526
#define DSC_DECREMENT_INTERVAL	  15
#define DSC_LINE_BPG_OFFSET		 12
#define DSC_NFL_BPG_OFFSET		  1639
#define DSC_SLICE_BPG_OFFSET		754
#define DSC_INITIAL_OFFSET		  6144
#define DSC_FINAL_OFFSET			4304
#define DSC_FLATNESS_MINQP		  3
#define DSC_FLATNESS_MAXQP		  12
#define DSC_RC_MODEL_SIZE		   8192
#define DSC_RC_EDGE_FACTOR		  6
#define DSC_RC_QUANT_INCR_LIMIT0	11
#define DSC_RC_QUANT_INCR_LIMIT1	11
#define DSC_RC_TGT_OFFSET_HI		3
#define DSC_RC_TGT_OFFSET_LO		3
#else
#define DSC_ENABLE				  1
#define DSC_VER					 17
#define DSC_SLICE_MODE			  1
#define DSC_RGB_SWAP				0
#define DSC_DSC_CFG				 40
#define DSC_RCT_ON				  1
#define DSC_BIT_PER_CHANNEL		 10
#define DSC_DSC_LINE_BUF_DEPTH	  11
#define DSC_BP_ENABLE			   1
#define DSC_BIT_PER_PIXEL		   128
#define DSC_SLICE_HEIGHT			16
#define DSC_SLICE_WIDTH			 1148
#define DSC_CHUNK_SIZE			  1148
#define DSC_XMIT_DELAY			  512
#define DSC_DEC_DELAY			   831
#define DSC_SCALE_VALUE			 32
#define DSC_INCREMENT_INTERVAL	  462
#define DSC_DECREMENT_INTERVAL	  15
#define DSC_LINE_BPG_OFFSET		 12
#define DSC_NFL_BPG_OFFSET		  1639
#define DSC_SLICE_BPG_OFFSET		771
#define DSC_INITIAL_OFFSET		  6144
#define DSC_FINAL_OFFSET			4352
#define DSC_FLATNESS_MINQP		  7
#define DSC_FLATNESS_MAXQP		  16
#define DSC_RC_MODEL_SIZE		   8192
#define DSC_RC_EDGE_FACTOR		  6
#define DSC_RC_QUANT_INCR_LIMIT0	15
#define DSC_RC_QUANT_INCR_LIMIT1	15
#define DSC_RC_TGT_OFFSET_HI		3
#define DSC_RC_TGT_OFFSET_LO		3

#define DSC_RANGE_MIN_QP_0		  0
#define DSC_RANGE_MIN_QP_1		  4
#define DSC_RANGE_MIN_QP_2		  5
#define DSC_RANGE_MIN_QP_3		  5
#define DSC_RANGE_MIN_QP_4		  7
#define DSC_RANGE_MIN_QP_5		  7
#define DSC_RANGE_MIN_QP_6		  7
#define DSC_RANGE_MIN_QP_7		  7
#define DSC_RANGE_MIN_QP_8		  7
#define DSC_RANGE_MIN_QP_9		  7
#define DSC_RANGE_MIN_QP_10		 9
#define DSC_RANGE_MIN_QP_11		 9
#define DSC_RANGE_MIN_QP_12		 9
#define DSC_RANGE_MIN_QP_13		 11
#define DSC_RANGE_MIN_QP_14		 17
#define DSC_RANGE_MAX_QP_0		  8
#define DSC_RANGE_MAX_QP_1		  8
#define DSC_RANGE_MAX_QP_2		  9
#define DSC_RANGE_MAX_QP_3		  10
#define DSC_RANGE_MAX_QP_4		  11
#define DSC_RANGE_MAX_QP_5		  11
#define DSC_RANGE_MAX_QP_6		  11
#define DSC_RANGE_MAX_QP_7		  12
#define DSC_RANGE_MAX_QP_8		  13
#define DSC_RANGE_MAX_QP_9		  14
#define DSC_RANGE_MAX_QP_10		 15
#define DSC_RANGE_MAX_QP_11		 16
#define DSC_RANGE_MAX_QP_12		 17
#define DSC_RANGE_MAX_QP_13		 17
#define DSC_RANGE_MAX_QP_14		 19
#define DSC_RANGE_BPG_OFFSET_0	  2
#define DSC_RANGE_BPG_OFFSET_1	  0
#define DSC_RANGE_BPG_OFFSET_2	  0
#define DSC_RANGE_BPG_OFFSET_3	  -2
#define DSC_RANGE_BPG_OFFSET_4	  -4
#define DSC_RANGE_BPG_OFFSET_5	  -6
#define DSC_RANGE_BPG_OFFSET_6	  -8
#define DSC_RANGE_BPG_OFFSET_7	  -8
#define DSC_RANGE_BPG_OFFSET_8	  -8
#define DSC_RANGE_BPG_OFFSET_9	  -10
#define DSC_RANGE_BPG_OFFSET_10	 -10
#define DSC_RANGE_BPG_OFFSET_11	 -12
#define DSC_RANGE_BPG_OFFSET_12	 -12
#define DSC_RANGE_BPG_OFFSET_13	 -12
#define DSC_RANGE_BPG_OFFSET_14	 -12
#endif

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *bias_pos, *bias_neg;
	struct regulator *vci;
	struct regulator *dvdd;
	struct regulator *vddi;

	bool prepared;
	bool enabled;

	int lcm_fresh_mode;
	struct list_head probed_modes;
#if LCM_MIPISWITCH_TE_DELAY
	unsigned int lcm_src_datarate;
	unsigned int lcm_dst_datarate;
#endif

	unsigned int reset_port;
	int error;
};

static char bl_tb0[] = {0x51, 0x03, 0xFF};
static int g_aod_enable;
u32 g_lcm_main_baclight_value;
EXPORT_SYMBOL(g_lcm_main_baclight_value);
#ifdef CONFIG_TRAN_GET_VREFRESH_SUPPORT
int g_tran_get_vrefresh_main;
EXPORT_SYMBOL_GPL(g_tran_get_vrefresh_main);
#endif
int g_tran_lcm_get_vfresh_main;
EXPORT_SYMBOL(g_tran_lcm_get_vfresh_main);
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
		dev_info(ctx->dev, "main error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
}
#define PANEL_SUPPORT_READBACK
#ifdef PANEL_SUPPORT_READBACK
#if !IS_ENABLED(CONFIG_TRAN_GET_LCM_CUSTOM_INFO)
static int lcm_dcs_read(struct lcm *ctx, u8 cmd, void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	if (ctx->error < 0)
		return 0;

	ret = mipi_dsi_dcs_read(dsi, cmd, data, len);
	if (ret < 0) {
		dev_err(ctx->dev, "main error %ld reading dcs seq:(%#x)\n", ret, cmd);
		ctx->error = ret;
	}

	return ret;
}
#endif

struct LCM_setting_table {
	unsigned int cmd;
	unsigned char count;
	unsigned char para_list[64];
};

static void push_table_cb(void *dsi, dcs_write_gce cb, void *handle, struct LCM_setting_table *table)
{
	unsigned int i = 0;

	while (table[i].cmd != REGFLAG_END_OF_TABLE) {
		switch (table[i].cmd) {
		case REGFLAG_DELAY:
			msleep(table[i].count);
			break;
		case REGFLAG_UDELAY:
			udelay(table[i].count);
			break;
		case REGFLAG_END_OF_TABLE:
			break;
		default:
			cb(dsi, handle, table[i].para_list, table[i].count);
		}
		i++;
	}
}

#if IS_ENABLED(CONFIG_TRAN_GET_LCM_CUSTOM_INFO)
extern u32 g_lcm_custom_info;
#endif
static u8 lcm_panel_check_batch(struct lcm *ctx)
{
	static u8 lcm_batch_num = 0x01;
	#if !IS_ENABLED(CONFIG_TRAN_GET_LCM_CUSTOM_INFO)
	int ret = 0;
	u8 buffer[3] = {0};

	if (lcm_batch_num == 0) {
		lcm_dcs_write_seq_static(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
		ret = lcm_dcs_read(ctx,  0xBA, buffer, 2);
		dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
			 ret, buffer[0] | (buffer[1] << 8));

		switch (buffer[1]) {
		case 0xA5:
			lcm_batch_num = 1;
			break;
		case 0xBB:
			lcm_batch_num = 2;
			break;
		default:
			lcm_batch_num = 2;
			break;
		}
	}
	#else
	lcm_batch_num = (u8)((g_lcm_custom_info & 0xFF00) >> 8);
	#endif
	dev_info(ctx->dev, "main nt37900 lcm_panel_check_batch num(%d)\n", lcm_batch_num);

	return lcm_batch_num;
}
#endif

static void transsion_lcm0_panel_init(struct lcm *ctx)
{
	pr_info("main %s\n", __func__);

	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(ctx->reset_gpio)) {
		dev_info(ctx->dev, "main %s: cannot get reset_gpio %ld\n", __func__, PTR_ERR(ctx->reset_gpio));
		return;
	}

	gpiod_set_value(ctx->reset_gpio, 1);
	udelay(5 * 1000);
	gpiod_set_value(ctx->reset_gpio, 0);
	udelay(5 * 1000);
	gpiod_set_value(ctx->reset_gpio, 1);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	mdelay(20);

	if (lcm_panel_check_batch(ctx) >= 4) {
		lcm_dcs_write_seq_static(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
		lcm_dcs_write_seq_static(ctx, 0xE1, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x6F, 0x01);
		lcm_dcs_write_seq_static(ctx, 0xE1, 0x10, 0x18, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00);
	}
	lcm_dcs_write_seq_static(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);

	lcm_dcs_write_seq_static(ctx, 0x6F, 0x1C);
	lcm_dcs_write_seq_static(ctx, 0xBA, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6F, 0x2C);
	lcm_dcs_write_seq_static(ctx, 0xBA, 0x00, 0x01, 0x01, 0x01, 0x00, 0x01, 0x01, 0x01, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6F, 0x3C);
	lcm_dcs_write_seq_static(ctx, 0xBA, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x03, 0x02, 0x00, 0x00, 0x03, 0x02, 0x00, 0x00, 0x00, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6F, 0x4C);
	lcm_dcs_write_seq_static(ctx, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6F, 0x5C);
	lcm_dcs_write_seq_static(ctx, 0xBA, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01);
	lcm_dcs_write_seq_static(ctx, 0x6F, 0x6C);
	lcm_dcs_write_seq_static(ctx, 0xBA, 0x01, 0x03, 0x04, 0x0B, 0x01, 0x05, 0x77, 0x01, 0x02, 0x08, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6F, 0x7C);
	lcm_dcs_write_seq_static(ctx, 0xBA, 0x01, 0x03, 0x04, 0x0B, 0x01, 0x05, 0x77, 0x01, 0x02, 0x08, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6F, 0x8C);
	lcm_dcs_write_seq_static(ctx, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6F, 0x9C);
	lcm_dcs_write_seq_static(ctx, 0xBA, 0x11, 0x11, 0x30, 0x33, 0x00, 0x33, 0x00, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6F, 0xA4);
	lcm_dcs_write_seq_static(ctx, 0xBA, 0x94, 0x25, 0x3D, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6F, 0xA8);
	lcm_dcs_write_seq_static(ctx, 0xBA, 0x00, 0x00, 0x11, 0x20, 0x22, 0x02, 0x00, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6F, 0xB0);
	lcm_dcs_write_seq_static(ctx, 0xBA, 0x00, 0x00, 0x11, 0x20, 0x22, 0x02, 0x00, 0x00);

	lcm_dcs_write_seq_static(ctx, 0xFF, 0xAA, 0x55, 0xA5, 0x80);
	lcm_dcs_write_seq_static(ctx, 0x6F, 0x02);
	lcm_dcs_write_seq_static(ctx, 0xF7, 0x60);

	lcm_dcs_write_seq_static(ctx, 0xFF, 0xAA, 0x55, 0xA5, 0x81);
	lcm_dcs_write_seq_static(ctx, 0x6F, 0x0D);
	lcm_dcs_write_seq_static(ctx, 0xFB, 0x80);
	lcm_dcs_write_seq_static(ctx, 0x6F, 0X10);
	lcm_dcs_write_seq_static(ctx, 0xFB, 0x04);

	lcm_dcs_write_seq_static(ctx, 0x6F, 0x04);
	lcm_dcs_write_seq_static(ctx, 0xFE, 0x34, 0x14);


	lcm_dcs_write_seq_static(ctx, 0x2A, 0x00, 0x00, 0x08, 0xF7);
	lcm_dcs_write_seq_static(ctx, 0x2B, 0x00, 0x00, 0x07, 0xCF);
	lcm_dcs_write_seq_static(ctx, 0x35, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x51, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x53, 0x20);
	lcm_dcs_write_seq_static(ctx, 0x90, 0x03, 0x43);
	#if __IS_10BIT__
	lcm_dcs_write_seq_static(ctx, 0x91, 0xAB, 0x28, 0x00, 0x10, 0xC2, 0x00, 0x03, 0x3F, 0x01, 0xCE, 0x00, 0x0F, 0x06, 0x67, 0x03, 0x03, 0x11, 0x00);
	#else
	lcm_dcs_write_seq_static(ctx, 0x91, 0x89, 0x28, 0x00, 0x10, 0xC2, 0x00, 0x03, 0x3F, 0x02, 0x0E, 0x00, 0x0F, 0x06, 0x67, 0x02, 0xF2, 0x10, 0xD0);
	#endif

	lcm_dcs_write_seq_static(ctx, 0x2F, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x5F, 0x00);


	lcm_dcs_write_seq_static(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6F, 0x2A);
	lcm_dcs_write_seq_static(ctx, 0xB2, 0x00);

	lcm_dcs_write_seq_static(ctx, 0x53, 0x28);
	lcm_dcs_write_seq_static(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xB2, 0x09);
	lcm_dcs_write_seq_static(ctx, 0x6F, 0x05);
	lcm_dcs_write_seq_static(ctx, 0xB2, 0x20, 0x20);

	lcm_dcs_write_seq_static(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x05);
	lcm_dcs_write_seq_static(ctx, 0xCB, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33);
	lcm_dcs_write_seq_static(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6F, 0x0A);
	lcm_dcs_write_seq_static(ctx, 0xB5, 0x4F);

	lcm_dcs_write_seq_static(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6F, 0xC5);
	lcm_dcs_write_seq_static(ctx, 0xBA, 0x10);
	lcm_dcs_write_seq_static(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6F, 0x09);
	lcm_dcs_write_seq_static(ctx, 0xC8, 0x13, 0x13, 0x13, 0x13, 0x0D, 0x13);

	lcm_dcs_write_seq_static(ctx, 0x11);
	msleep(120);
	lcm_dcs_write_seq_static(ctx, 0x29);
	mdelay(1);

	g_tran_lcm_get_vfresh_main = MODE_0_FPS;
	if (g_lcm_main_baclight_value != 0) {
		bl_tb0[1] = ((g_lcm_main_baclight_value & 0xF00) >> 8) & 0xF;
		bl_tb0[2] = g_lcm_main_baclight_value & 0xFF;
		lcm_dcs_write(ctx, bl_tb0, ARRAY_SIZE(bl_tb0));
	}
	ctx->error = 0;
}

static int lcm_panel_ext_ldo_ctrl(struct lcm *ctx, bool en)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(ctx->vddi)) {
		ctx->vddi = regulator_get(ctx->dev, "vddi");
		if (IS_ERR_OR_NULL(ctx->vddi)) {
			ret = PTR_ERR(ctx->vddi);
			dev_err(ctx->dev, "main %s cannot get vddi regulator!ret[%d]\n", __func__, ret);
			return ret;
		}
	}

	if (IS_ERR_OR_NULL(ctx->dvdd)) {
		ctx->dvdd = regulator_get(ctx->dev, "dvdd");
		if (IS_ERR_OR_NULL(ctx->dvdd)) {
			ret = PTR_ERR(ctx->dvdd);
			dev_err(ctx->dev, "main %s cannot get dvdd regulator!ret[%d]\n", __func__, ret);
			return ret;
		}
	}

	if (IS_ERR_OR_NULL(ctx->vci)) {
		ctx->vci = regulator_get(ctx->dev, "vci");
		if (IS_ERR_OR_NULL(ctx->vci)) {
			ret = PTR_ERR(ctx->vci);
			dev_err(ctx->dev, "main %s cannot get vci regulator!ret[%d]\n", __func__, ret);
			return ret;
		}
	}

	if (en) {
		if (regulator_is_enabled(ctx->vddi)) {
			ret = regulator_disable(ctx->vddi);
			if (ret < 0)
				dev_err(ctx->dev, "main %s ddi disable error!ret[%d]\n", __func__, ret);
		}

		ret = regulator_set_voltage(ctx->vddi, 1820000, 1820000);
		if (ret < 0)
			dev_err(ctx->dev, "main %s vddi set vol fail!ret[%d]\n", __func__, ret);
		ret = regulator_enable(ctx->vddi);
		if (ret < 0)
			dev_err(ctx->dev, "main %s vddi enable error!ret[%d]\n", __func__, ret);
		udelay(10);

		ret = regulator_set_voltage(ctx->dvdd, 1352000, 1352000);
		if (ret < 0)
			dev_err(ctx->dev, "main %s dvdd set vol fail!ret[%d]\n", __func__, ret);
		ret = regulator_enable(ctx->dvdd);
		if (ret < 0)
			dev_err(ctx->dev, "main %s dvdd enable error!ret[%d]\n", __func__, ret);
		udelay(10);

		ret = regulator_set_voltage(ctx->vci, 3004000, 3004000);
		if (ret < 0)
			dev_err(ctx->dev, "main%s vci set vol fail!ret[%d]\n", __func__, ret);
		ret = regulator_enable(ctx->vci);
		if (ret < 0)
			dev_err(ctx->dev, "main %s vci enable error!ret[%d]\n", __func__, ret);
		udelay(10);
	} else {
		ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
		if (IS_ERR_OR_NULL(ctx->reset_gpio)) {
			dev_err(ctx->dev, "%s: cannot get reset_gpio %ld\n", __func__, PTR_ERR(ctx->reset_gpio));
			return PTR_ERR(ctx->reset_gpio);
		}
		gpiod_set_value(ctx->reset_gpio, 0);
		devm_gpiod_put(ctx->dev, ctx->reset_gpio);
		udelay(10);

		ret = regulator_disable(ctx->vci);
		if (regulator_is_enabled(ctx->vci)) {
			ret = regulator_disable(ctx->vci);
			if (regulator_is_enabled(ctx->vci))
				dev_err(ctx->dev, "main %s disable vci error!ret[%d]\n", __func__, ret);
		}
		udelay(10);

		ret = regulator_disable(ctx->dvdd);
		if (regulator_is_enabled(ctx->dvdd)) {
			ret = regulator_disable(ctx->dvdd);
			if (regulator_is_enabled(ctx->dvdd))
				dev_err(ctx->dev, "main %s disable dvdd error!ret[%d]\n", __func__, ret);
		}

		udelay(10);
		ret = regulator_disable(ctx->vddi);
		if (regulator_is_enabled(ctx->vddi)) {
			ret = regulator_disable(ctx->vddi);
			if (regulator_is_enabled(ctx->vddi))
				dev_err(ctx->dev, "main %s disable vddi error!ret[%d]\n", __func__, ret);
		}
	}

	return 0;
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	pr_info("main %s, status=%d\n", __func__, ctx->enabled);
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

	pr_info("main %s begin, status=%d\n", __func__, ctx->prepared);
	if (g_aod_enable == 1) {
		lcm_dcs_write_seq_static(ctx, 0x38);
		g_aod_enable = 0;
		msleep(70);
	}
	lcm_dcs_write_seq_static(ctx, 0x28);
	mdelay(10);
	lcm_dcs_write_seq_static(ctx, 0x10);
	mdelay(110);


	ctx->error = 0;
	ctx->prepared = false;
	pr_info("main %s end\n", __func__);

	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret = 0;

	pr_info("main %s begin, status=%d\n", __func__, ctx->prepared);
	if (ctx->prepared)
		return 0;

	mdelay(10);
	transsion_lcm0_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0)
		lcm_unprepare(panel);

	ctx->prepared = true;
	pr_info("main %s end\n", __func__);

	return ret;
}

static int lcm_enable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	pr_info("main %s, status=%d\n", __func__, ctx->enabled);
	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;

	return 0;
}

static int panel_ext_lcm_power_set(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);

	pr_info("main %s begain\n", __func__);
	lcm_panel_ext_ldo_ctrl(ctx, on);
	pr_info("main %s end\n", __func__);
	return 0;
}

static struct drm_display_mode switch_mode[NT37900_MODE_NUM] = {
	{
		.clock = (int)((FRAME_WIDTH + MODE_0_HFP + HSA + HBP) * (FRAME_HEIGHT + MODE_0_VFP + VSA + VBP) * MODE_0_FPS / 1000),
		.hdisplay = FRAME_WIDTH,
		.hsync_start = FRAME_WIDTH + MODE_0_HFP,
		.hsync_end = FRAME_WIDTH + MODE_0_HFP + HSA,
		.htotal = FRAME_WIDTH + MODE_0_HFP + HSA + HBP,
		.vdisplay = FRAME_HEIGHT,
		.vsync_start = FRAME_HEIGHT + MODE_0_VFP,
		.vsync_end = FRAME_HEIGHT + MODE_0_VFP + VSA,
		.vtotal = FRAME_HEIGHT + MODE_0_VFP + VSA + VBP,
	},
	{
		.clock = (int)((FRAME_WIDTH + MODE_0_HFP + HSA + HBP) * (FRAME_HEIGHT + MODE_0_VFP + VSA + VBP) * MODE_1_FPS / 1000),
		.hdisplay = FRAME_WIDTH,
		.hsync_start = FRAME_WIDTH + MODE_0_HFP,
		.hsync_end = FRAME_WIDTH + MODE_0_HFP + HSA,
		.htotal = FRAME_WIDTH + MODE_0_HFP + HSA + HBP,
		.vdisplay = FRAME_HEIGHT,
		.vsync_start = FRAME_HEIGHT + MODE_0_VFP,
		.vsync_end = FRAME_HEIGHT + MODE_0_VFP + VSA,
		.vtotal = FRAME_HEIGHT + MODE_0_VFP + VSA + VBP,
	},
	{
		.clock = (int)((FRAME_WIDTH + MODE_0_HFP + HSA + HBP) * (FRAME_HEIGHT + MODE_0_VFP + VSA + VBP) * MODE_2_FPS / 1000),
		.hdisplay = FRAME_WIDTH,
		.hsync_start = FRAME_WIDTH + MODE_0_HFP,
		.hsync_end = FRAME_WIDTH + MODE_0_HFP + HSA,
		.htotal = FRAME_WIDTH + MODE_0_HFP + HSA + HBP,
		.vdisplay = FRAME_HEIGHT,
		.vsync_start = FRAME_HEIGHT + MODE_0_VFP,
		.vsync_end = FRAME_HEIGHT + MODE_0_VFP + VSA,
		.vtotal = FRAME_HEIGHT + MODE_0_VFP + VSA + VBP,
	},
	{
		.clock = (int)((FRAME_WIDTH + MODE_0_HFP + HSA + HBP) * (FRAME_HEIGHT + MODE_0_VFP + VSA + VBP) * MODE_3_FPS / 1000),
		.hdisplay = FRAME_WIDTH,
		.hsync_start = FRAME_WIDTH + MODE_0_HFP,
		.hsync_end = FRAME_WIDTH + MODE_0_HFP + HSA,
		.htotal = FRAME_WIDTH + MODE_0_HFP + HSA + HBP,
		.vdisplay = FRAME_HEIGHT,
		.vsync_start = FRAME_HEIGHT + MODE_0_VFP,
		.vsync_end = FRAME_HEIGHT + MODE_0_VFP + VSA,
		.vtotal = FRAME_HEIGHT + MODE_0_VFP + VSA + VBP,
	},
	{
		.clock = (int)((FRAME_WIDTH + MODE_0_HFP + HSA + HBP) * (FRAME_HEIGHT + MODE_0_VFP + VSA + VBP) * MODE_4_FPS / 1000),
		.hdisplay = FRAME_WIDTH,
		.hsync_start = FRAME_WIDTH + MODE_0_HFP,
		.hsync_end = FRAME_WIDTH + MODE_0_HFP + HSA,
		.htotal = FRAME_WIDTH + MODE_0_HFP + HSA + HBP,
		.vdisplay = FRAME_HEIGHT,
		.vsync_start = FRAME_HEIGHT + MODE_0_VFP,
		.vsync_end = FRAME_HEIGHT + MODE_0_VFP + VSA,
		.vtotal = FRAME_HEIGHT + MODE_0_VFP + VSA + VBP,
	},
	{
		.clock = (int)((FRAME_WIDTH + MODE_0_HFP + HSA + HBP) * (FRAME_HEIGHT + MODE_0_VFP + VSA + VBP) * MODE_5_FPS / 1000),
		.hdisplay = FRAME_WIDTH,
		.hsync_start = FRAME_WIDTH + MODE_0_HFP,
		.hsync_end = FRAME_WIDTH + MODE_0_HFP + HSA,
		.htotal = FRAME_WIDTH + MODE_0_HFP + HSA + HBP,
		.vdisplay = FRAME_HEIGHT,
		.vsync_start = FRAME_HEIGHT + MODE_0_VFP,
		.vsync_end = FRAME_HEIGHT + MODE_0_VFP + VSA,
		.vtotal = FRAME_HEIGHT + MODE_0_VFP + VSA + VBP,
	},
};

#if defined(CONFIG_MTK_PANEL_EXT)
static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);

	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(ctx->reset_gpio)) {
		dev_info(ctx->dev, "main %s: cannot get reset_gpio %ld\n", __func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}

	gpiod_set_value(ctx->reset_gpio, on);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	return 0;
}

static int panel_ata_check(struct drm_panel *panel)
{
	#if !IS_ENABLED(CONFIG_TRAN_GET_LCM_CUSTOM_INFO)
	struct lcm *ctx = panel_to_lcm(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	unsigned char data[3];
	unsigned char id[3] = {0x9c, 0x00, 0x00};
	ssize_t ret;

	pr_info("main %s enter\n", __func__);

	ret = mipi_dsi_dcs_read(dsi, 0xa, data, 3);
	if (ret < 0)
		pr_info("main %s error\n", __func__);

	pr_info("main ATA read data %x %x %x\n", data[0], data[1], data[2]);

	if (data[0] == id[0] &&
			data[1] == id[1] &&
			data[2] == id[2])
		return 1;

	pr_info("main ATA expect read data is %x %x %x\n",
			id[0], id[1], id[2]);
	return 0;
	#else
	unsigned char ret = 0;

	pr_info("main %s enter g_lcm_custom_info[%x]\n", __func__, g_lcm_custom_info);

	if ((u8)((g_lcm_custom_info & 0xFF00) >> 8) != 0)
		ret |= 0x02;
	else
		ret &= 0x0D;

	if ((u8)(g_lcm_custom_info & 0xFF))
		ret |= 0x01;
	else
		ret &= 0x0E;
	ret |= 0xF0;
	pr_info("main %s  ret[%x]\n", __func__, ret);
	return ret;
	#endif

}

static struct LCM_setting_table first_up_dim[] = {
	{REGFLAG_CMD, 0x06, {0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00}},
	{REGFLAG_CMD, 0x02, {0x6F, 0x05}},
	{REGFLAG_CMD, 0x03, {0xB2, 0x08, 0x08}},
	{REGFLAG_END_OF_TABLE, 0x01, {0x00}},
};
static struct LCM_setting_table nor_dim[] = {
	{REGFLAG_CMD, 0x06, {0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00}},
	{REGFLAG_CMD, 0x02, {0x6F, 0x05}},
	{REGFLAG_CMD, 0x03, {0xB2, 0x20, 0x20}},
	{REGFLAG_END_OF_TABLE, 0x01, {0x00}},
};
static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb,
	void *handle, unsigned int level)
{
	char bl_dim_on[] = {0x53, 0x28};
	char bl_dim_off[] = {0x53, 0x20};
	static unsigned int last_level = 4095;
	static unsigned int dim_changed;
	unsigned int mapped_level = level;

	if (level > 4095) {
		level = 4095;
		mapped_level = 4095;
	}

	if ((level / 8) <= 22)
		mapped_level = (level / 8) * 16;
	else if ((level / 8) <= 66)
		mapped_level = ((level / 8) - 22) * 4 + 352;
	bl_tb0[1] = ((mapped_level & 0xF00) >> 8) & 0xF;
	bl_tb0[2] = mapped_level & 0xFF;

	pr_info("main %s level is %u,mapped_level is %d\n", __func__, level, mapped_level);
	if (!cb)
		return -1;
	g_lcm_main_baclight_value = level;
	if (last_level == 0) {
		pr_info("main %s first_up\n", __func__);
		push_table_cb(dsi, cb, handle, first_up_dim);
		dim_changed = 1;
	} else {
		if (dim_changed == 1)
			push_table_cb(dsi, cb, handle, nor_dim);
		dim_changed = 0;
	}
	last_level = level;
	if (level == 0)
		cb(dsi, handle, bl_dim_off, ARRAY_SIZE(bl_dim_off));
	else
		cb(dsi, handle, bl_dim_on, ARRAY_SIZE(bl_dim_on));
	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));

	return 0;
}
static struct mtk_panel_params ext_params = {
	.lcm_index = 0,
	.cust_esd_check = 0,
	.esd_check_enable = 0,
	.cmd_null_pkt_en = 0,
	.cmd_null_pkt_len = 400,
	.lp_perline_en = 1,
	.lcm_esd_check_table[0] = {
			.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
		},
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable			 =  DSC_ENABLE,
		.ver				   =  DSC_VER,
		.slice_mode		 =  DSC_SLICE_MODE,
		.rgb_swap			 =  DSC_RGB_SWAP,
		.dsc_cfg			   =  DSC_DSC_CFG,
		.rct_on			 =  DSC_RCT_ON,
		.bit_per_channel	   =  DSC_BIT_PER_CHANNEL,
		.dsc_line_buf_depth =  DSC_DSC_LINE_BUF_DEPTH,
		.bp_enable		   =  DSC_BP_ENABLE,
		.bit_per_pixel	   =  DSC_BIT_PER_PIXEL,
		.pic_height		 =  FRAME_HEIGHT,
		.pic_width		   =  FRAME_WIDTH,
		.slice_height		 =  DSC_SLICE_HEIGHT,
		.slice_width		   =  DSC_SLICE_WIDTH,
		.chunk_size		 =  DSC_CHUNK_SIZE,
		.xmit_delay		 =  DSC_XMIT_DELAY,
		.dec_delay		   =  DSC_DEC_DELAY,
		.scale_value		   =  DSC_SCALE_VALUE,
		.increment_interval =  DSC_INCREMENT_INTERVAL,
		.decrement_interval =  DSC_DECREMENT_INTERVAL,
		.line_bpg_offset	   =  DSC_LINE_BPG_OFFSET,
		.nfl_bpg_offset	 =  DSC_NFL_BPG_OFFSET,
		.slice_bpg_offset	 =  DSC_SLICE_BPG_OFFSET,
		.initial_offset	 =  DSC_INITIAL_OFFSET,
		.final_offset		 =  DSC_FINAL_OFFSET,
		.flatness_minqp	 =  DSC_FLATNESS_MINQP,
		.flatness_maxqp	 =  DSC_FLATNESS_MAXQP,
		.rc_model_size	   =  DSC_RC_MODEL_SIZE,
		.rc_edge_factor	 =  DSC_RC_EDGE_FACTOR,
		.rc_quant_incr_limit0  =  DSC_RC_QUANT_INCR_LIMIT0,
		.rc_quant_incr_limit1  =  DSC_RC_QUANT_INCR_LIMIT1,
		.rc_tgt_offset_hi	 =  DSC_RC_TGT_OFFSET_HI,
		.rc_tgt_offset_lo	 =  DSC_RC_TGT_OFFSET_LO,
	},

	.dyn_fps = {
		.switch_en = 0,
	},
	.data_rate = MODE_0_DATA_RATE,
};

static struct mtk_panel_params ext_params_90fps = {
	.lcm_index = 0,
	.data_rate = MODE_1_DATA_RATE,
	.cust_esd_check = 0,
	.esd_check_enable = 0,
	.cmd_null_pkt_en = 0,
	.cmd_null_pkt_len = 500,
	.lp_perline_en = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
	},
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable			 =  DSC_ENABLE,
		.ver				   =  DSC_VER,
		.slice_mode		 =  DSC_SLICE_MODE,
		.rgb_swap			 =  DSC_RGB_SWAP,
		.dsc_cfg			   =  DSC_DSC_CFG,
		.rct_on			 =  DSC_RCT_ON,
		.bit_per_channel	   =  DSC_BIT_PER_CHANNEL,
		.dsc_line_buf_depth =  DSC_DSC_LINE_BUF_DEPTH,
		.bp_enable		   =  DSC_BP_ENABLE,
		.bit_per_pixel	   =  DSC_BIT_PER_PIXEL,
		.pic_height		 =  FRAME_HEIGHT,
		.pic_width		   =  FRAME_WIDTH,
		.slice_height		 =  DSC_SLICE_HEIGHT,
		.slice_width		   =  DSC_SLICE_WIDTH,
		.chunk_size		 =  DSC_CHUNK_SIZE,
		.xmit_delay		 =  DSC_XMIT_DELAY,
		.dec_delay		   =  DSC_DEC_DELAY,
		.scale_value		   =  DSC_SCALE_VALUE,
		.increment_interval =  DSC_INCREMENT_INTERVAL,
		.decrement_interval =  DSC_DECREMENT_INTERVAL,
		.line_bpg_offset	   =  DSC_LINE_BPG_OFFSET,
		.nfl_bpg_offset	 =  DSC_NFL_BPG_OFFSET,
		.slice_bpg_offset	 =  DSC_SLICE_BPG_OFFSET,
		.initial_offset	 =  DSC_INITIAL_OFFSET,
		.final_offset		 =  DSC_FINAL_OFFSET,
		.flatness_minqp	 =  DSC_FLATNESS_MINQP,
		.flatness_maxqp	 =  DSC_FLATNESS_MAXQP,
		.rc_model_size	   =  DSC_RC_MODEL_SIZE,
		.rc_edge_factor	 =  DSC_RC_EDGE_FACTOR,
		.rc_quant_incr_limit0  =  DSC_RC_QUANT_INCR_LIMIT0,
		.rc_quant_incr_limit1  =  DSC_RC_QUANT_INCR_LIMIT1,
		.rc_tgt_offset_hi	 =  DSC_RC_TGT_OFFSET_HI,
		.rc_tgt_offset_lo	 =  DSC_RC_TGT_OFFSET_LO,
	},
	.dyn_fps = {
		.switch_en = 0,
	},
};

static struct mtk_panel_params ext_params_60fps = {
	.lcm_index = 0,
	.data_rate = MODE_2_DATA_RATE,
	.cust_esd_check = 0,
	.esd_check_enable = 0,
	.cmd_null_pkt_en = 0,
	.cmd_null_pkt_len = 500,
	.lp_perline_en = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
	},
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable			 =  DSC_ENABLE,
		.ver				   =  DSC_VER,
		.slice_mode		 =  DSC_SLICE_MODE,
		.rgb_swap			 =  DSC_RGB_SWAP,
		.dsc_cfg			   =  DSC_DSC_CFG,
		.rct_on			 =  DSC_RCT_ON,
		.bit_per_channel	   =  DSC_BIT_PER_CHANNEL,
		.dsc_line_buf_depth =  DSC_DSC_LINE_BUF_DEPTH,
		.bp_enable		   =  DSC_BP_ENABLE,
		.bit_per_pixel	   =  DSC_BIT_PER_PIXEL,
		.pic_height		 =  FRAME_HEIGHT,
		.pic_width		   =  FRAME_WIDTH,
		.slice_height		 =  DSC_SLICE_HEIGHT,
		.slice_width		   =  DSC_SLICE_WIDTH,
		.chunk_size		 =  DSC_CHUNK_SIZE,
		.xmit_delay		 =  DSC_XMIT_DELAY,
		.dec_delay		   =  DSC_DEC_DELAY,
		.scale_value		   =  DSC_SCALE_VALUE,
		.increment_interval =  DSC_INCREMENT_INTERVAL,
		.decrement_interval =  DSC_DECREMENT_INTERVAL,
		.line_bpg_offset	   =  DSC_LINE_BPG_OFFSET,
		.nfl_bpg_offset	 =  DSC_NFL_BPG_OFFSET,
		.slice_bpg_offset	 =  DSC_SLICE_BPG_OFFSET,
		.initial_offset	 =  DSC_INITIAL_OFFSET,
		.final_offset		 =  DSC_FINAL_OFFSET,
		.flatness_minqp	 =  DSC_FLATNESS_MINQP,
		.flatness_maxqp	 =  DSC_FLATNESS_MAXQP,
		.rc_model_size	   =  DSC_RC_MODEL_SIZE,
		.rc_edge_factor	 =  DSC_RC_EDGE_FACTOR,
		.rc_quant_incr_limit0  =  DSC_RC_QUANT_INCR_LIMIT0,
		.rc_quant_incr_limit1  =  DSC_RC_QUANT_INCR_LIMIT1,
		.rc_tgt_offset_hi	 =  DSC_RC_TGT_OFFSET_HI,
		.rc_tgt_offset_lo	 =  DSC_RC_TGT_OFFSET_LO,
	},
	.dyn_fps = {
		.switch_en = 0,
	},
};

static struct drm_display_mode *get_mode_by_id(struct drm_connector *connector,
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
	#if LCM_MIPISWITCH_TE_DELAY
	struct lcm *ctx = panel_to_lcm(panel);
	#endif
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id(connector, mode);

	switch (drm_mode_vrefresh(m)) {
	case MODE_0_FPS:
		ext->params = &ext_params;
		break;
	case MODE_1_FPS:
	case MODE_3_FPS:
	case MODE_4_FPS:
	case MODE_5_FPS:
		ext->params = &ext_params_90fps;
		break;
	case MODE_2_FPS:
		ext->params = &ext_params_60fps;
		break;
	default:
		ret = 1;
		break;
	}

	if (ext->params) {
		#if LCM_MIPISWITCH_TE_DELAY
		if (ctx->lcm_dst_datarate == 0)
			ctx->lcm_dst_datarate = MODE_0_DATA_RATE;
		ctx->lcm_src_datarate = ctx->lcm_dst_datarate;
		ctx->lcm_dst_datarate = ext->params->data_rate;
		pr_info("main data_rate change form %d to %d\n", ctx->lcm_src_datarate, ctx->lcm_dst_datarate);
		#else
		pr_info("main data_rate %d\n", ext->params->data_rate);
		#endif
	} else
		pr_info("main ext_param is NULL;\n");
	return ret;
}

static int mtk_panel_ext_param_get(struct drm_panel *panel,
	struct drm_connector *connector,
	struct mtk_panel_params **ext_param,
	unsigned int mode)
{
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id(connector, mode);

	switch (drm_mode_vrefresh(m)) {
	case MODE_0_FPS:
		*ext_param = &ext_params;
		break;
	case MODE_1_FPS:
	case MODE_3_FPS:
	case MODE_4_FPS:
	case MODE_5_FPS:
		*ext_param = &ext_params_90fps;
		break;
	case MODE_2_FPS:
		*ext_param = &ext_params_60fps;
		break;
	default:
		ret = 1;
		break;
	}

	if (*ext_param)
		pr_info("main data_rate:%d\n", (*ext_param)->data_rate);
	else
		pr_info("main ext_param is NULL;\n");

	return ret;
}

static void mode_switch_to_mode_0(struct drm_panel *panel,
			enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (stage == AFTER_DSI_POWERON) {
		pr_info("main %s mode_switch_to_120\n", __func__);
		lcm_dcs_write_seq_static(ctx, 0x2F, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x5F, 0x00);
	}
}

static void mode_switch_to_mode_1(struct drm_panel *panel,
			enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (stage == AFTER_DSI_POWERON) {
		pr_info("main %s mode_switch_to_90\n", __func__);
		lcm_dcs_write_seq_static(ctx, 0x2F, 0x02);
		lcm_dcs_write_seq_static(ctx, 0x5F, 0x00);
	}
}

static void mode_switch_to_mode_2(struct drm_panel *panel,
			enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (stage == AFTER_DSI_POWERON) {
		pr_info("main %s mode_switch_to_60\n", __func__);
		lcm_dcs_write_seq_static(ctx, 0x2F, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x5F, 0x00);

	}
}

static void mode_switch_to_mode_3(struct drm_panel *panel,
			enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (stage == AFTER_DSI_POWERON) {
		pr_info("main %s mode_switch_to_45\n", __func__);
		lcm_dcs_write_seq_static(ctx, 0x2F, 0x30);
		lcm_dcs_write_seq_static(ctx, 0x5A, 0x01, 0x01);

		lcm_dcs_write_seq_static(ctx, 0x6D, 0x07, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x5F, 0x00);
	}
}

static void mode_switch_to_mode_4(struct drm_panel *panel,
			enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (stage == AFTER_DSI_POWERON) {
		pr_info("main %s mode_switch_to_30\n", __func__);
		lcm_dcs_write_seq_static(ctx, 0x2F, 0x30);
		lcm_dcs_write_seq_static(ctx, 0x5A, 0x01, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x6D, 0x08, 0x00);
		lcm_dcs_write_seq_static(ctx, 0x5F, 0x00);
	}
}

static void mode_switch_to_mode_5(struct drm_panel *panel,
			enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (stage == AFTER_DSI_POWERON) {
		pr_info("main %s mode_switch_to_10\n", __func__);
		lcm_dcs_write_seq_static(ctx, 0x2F, 0x30);
		lcm_dcs_write_seq_static(ctx, 0x5A, 0x01, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x6D, 0x09, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x5F, 0x00);
	}
}


static int mode_switch(struct drm_panel *panel,
		struct drm_connector *connector, unsigned int cur_mode,
		unsigned int dst_mode, enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	int ret = 0;
	struct lcm *ctx = panel_to_lcm(panel);
	struct drm_display_mode *m = get_mode_by_id(connector, dst_mode);
	static unsigned char base_fps = 120;

	if (cur_mode == dst_mode)
		return ret;

	if (g_aod_enable == 0) {
		switch (drm_mode_vrefresh(m)) {
		case MODE_0_FPS:
			mode_switch_to_mode_0(panel, stage);
			break;
		case MODE_1_FPS:
			mode_switch_to_mode_1(panel, stage);
			break;
		case MODE_2_FPS:
			mode_switch_to_mode_2(panel, stage);
			break;
		case MODE_3_FPS:
			mode_switch_to_mode_3(panel, stage);
			break;
		case MODE_4_FPS:
			mode_switch_to_mode_4(panel, stage);
			break;
		case MODE_5_FPS:
			mode_switch_to_mode_5(panel, stage);
			break;
		default:
			ret = 1;
		}
	}

	if (stage == AFTER_DSI_POWERON) {
		#if LCM_MIPISWITCH_TE_DELAY
		if (ctx->lcm_src_datarate > ctx->lcm_dst_datarate) {
			pr_info("main data_rate change form %d to %d\n", ctx->lcm_src_datarate, ctx->lcm_dst_datarate);
			if (base_fps == 0)
				base_fps = 120;
			mdelay(1000 / base_fps);
		}
		#endif
		ctx->lcm_fresh_mode = drm_mode_vrefresh(m);
		g_tran_lcm_get_vfresh_main = ctx->lcm_fresh_mode;
#ifdef CONFIG_TRAN_GET_VREFRESH_SUPPORT
		g_tran_get_vrefresh_main = g_tran_lcm_get_vfresh_main;
#endif
		switch (ctx->lcm_fresh_mode) {
		case MODE_0_FPS:
			base_fps = 120;
			break;
		case MODE_1_FPS:
			base_fps = 90;
			break;
		case MODE_2_FPS:
			base_fps = 60;
			break;
		case MODE_3_FPS:
			base_fps = 90;
			break;
		case MODE_4_FPS:
			base_fps = 90;
			break;
		case MODE_5_FPS:
			base_fps = 90;
			break;
		default:
			base_fps = 90;
			break;
		}
	}

	return ret;
}

static struct LCM_setting_table enter_aod[] = {
	{REGFLAG_CMD, 0x02, {0x2F, 0x00}},
	{REGFLAG_CMD, 0x01, {0x39}},
	{REGFLAG_END_OF_TABLE, 0x01, {0x00}},
};
static struct LCM_setting_table aod_dim[] = {
	{REGFLAG_CMD, 0x06, {0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00}},
	{REGFLAG_CMD, 0x02, {0x6F, 0x05}},
	{REGFLAG_CMD, 0x03, {0xB2, 0x08, 0x08}},
	{REGFLAG_END_OF_TABLE, 0x01, {0x00}},
};
static int panel_doze_enable(struct drm_panel *panel,
	void *dsi, dcs_write_gce cb, void *handle)
{
	pr_info("main %s begain\n", __func__);
	push_table_cb(dsi, cb, handle, aod_dim);
	push_table_cb(dsi, cb, handle, enter_aod);
	g_aod_enable = 1;
	return 0;
}

static struct LCM_setting_table exit_aod[] = {
	{REGFLAG_CMD, 0x02, {0x2F, 0x00}},
	{REGFLAG_CMD, 0x01, {0x38}},
	{REGFLAG_END_OF_TABLE, 0x01, {0x00}},
};

static struct LCM_setting_table fps_120_code[] = {
	{REGFLAG_CMD, 0x02, {0x2F, 0x00}},
	{REGFLAG_CMD, 0x02, {0x5F, 0x00}},
	{REGFLAG_END_OF_TABLE, 0x01, {0x00}},
};
static struct LCM_setting_table fps_90_code[] = {
	{REGFLAG_CMD, 0x02, {0x2F, 0x02}},
	{REGFLAG_CMD, 0x02, {0x5F, 0x00}},
	{REGFLAG_END_OF_TABLE, 0x01, {0x00}},
};
static struct LCM_setting_table fps_60_code[] = {
	{REGFLAG_CMD, 0x02, {0x2F, 0x01}},
	{REGFLAG_CMD, 0x02, {0x5F, 0x00}},
	{REGFLAG_END_OF_TABLE, 0x01, {0x00}},
};
static struct LCM_setting_table fps_45_code[] = {
	{REGFLAG_CMD, 0x02, {0x2F, 0x30}},
	{REGFLAG_CMD, 0x03, {0x5A, 0x01, 0x01}},
	{REGFLAG_CMD, 0x03, {0x6D, 0x07, 0x00}},
	{REGFLAG_CMD, 0x02, {0x5F, 0x00}},
	{REGFLAG_END_OF_TABLE, 0x01, {0x00}},
};
static struct LCM_setting_table fps_30_code[] = {
	{REGFLAG_CMD, 0x02, {0x2F, 0x30}},
	{REGFLAG_CMD, 0x03, {0x5A, 0x01, 0x01}},
	{REGFLAG_CMD, 0x03, {0x6D, 0x08, 0x00}},
	{REGFLAG_CMD, 0x02, {0x5F, 0x00}},
	{REGFLAG_END_OF_TABLE, 0x01, {0x00}},
};
static struct LCM_setting_table fps_10_code[] = {
	{REGFLAG_CMD, 0x02, {0x2F, 0x30}},
	{REGFLAG_CMD, 0x03, {0x5A, 0x01, 0x01}},
	{REGFLAG_CMD, 0x03, {0x6D, 0x09, 0x00}},
	{REGFLAG_CMD, 0x02, {0x5F, 0x00}},
	{REGFLAG_END_OF_TABLE, 0x01, {0x00}},
};
static int panel_doze_disable(struct drm_panel *panel,
	void *dsi, dcs_write_gce cb, void *handle)
{
	struct lcm *ctx = panel_to_lcm(panel);

	pr_info("main %s begain\n", __func__);
	push_table_cb(dsi, cb, handle, nor_dim);
	push_table_cb(dsi, cb, handle, exit_aod);

	switch (ctx->lcm_fresh_mode) {
	case MODE_0_FPS:
		push_table_cb(dsi, cb, handle, fps_120_code);
		break;
	case MODE_1_FPS:
		push_table_cb(dsi, cb, handle, fps_90_code);
		break;
	case MODE_2_FPS:
		push_table_cb(dsi, cb, handle, fps_60_code);
		break;
	case MODE_3_FPS:
		push_table_cb(dsi, cb, handle, fps_45_code);
		break;
	case MODE_4_FPS:
		push_table_cb(dsi, cb, handle, fps_30_code);
		break;
	case MODE_5_FPS:
		push_table_cb(dsi, cb, handle, fps_10_code);
		break;
	default:
		break;
	}
	pr_info("main %s resume fps:%d\n", __func__, ctx->lcm_fresh_mode);

	if (g_lcm_main_baclight_value != 0) {
		bl_tb0[1] = ((g_lcm_main_baclight_value & 0xF00) >> 8) & 0xF;
		bl_tb0[2] = g_lcm_main_baclight_value & 0xFF;
		cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));
	}

	g_aod_enable = 0;
	return 0;
}



static int get_lcm_virtual_heigh(void)
{
	return FRAME_HEIGHT;
}
static int get_lcm_virtual_width(void)
{
	return FRAME_WIDTH;
}
static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.ata_check = panel_ata_check,
	.lcm_power_set = panel_ext_lcm_power_set,
	.ext_param_set = mtk_panel_ext_param_set,
	.ext_param_get = mtk_panel_ext_param_get,
	.mode_switch = mode_switch,
	.get_virtual_heigh = get_lcm_virtual_heigh,
	.get_virtual_width = get_lcm_virtual_width,

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

static int lcm_get_modes(struct drm_panel *panel,
			struct drm_connector *connector)
{
	struct drm_display_mode *mode[NT37900_MODE_NUM];
	int i;
	struct lcm *ctx = panel_to_lcm(panel);
	u8 mode_num = NT37900_MODE_NUM;

	for (i = 0; i < mode_num; i++) {
		dev_info(connector->dev->dev, "main [%d] add mode %ux%ux@%u\n", i,
				switch_mode[i].hdisplay, switch_mode[i].vdisplay,
				drm_mode_vrefresh(&switch_mode[i]));
		mode[i] = drm_mode_duplicate(connector->dev, &switch_mode[i]);
		if (!mode[i]) {
			dev_info(connector->dev->dev, "main failed to add mode %ux%ux@%u\n",
				switch_mode[i].hdisplay, switch_mode[i].vdisplay,
				drm_mode_vrefresh(&switch_mode[i]));
			return -ENOMEM;
		}

		drm_mode_set_name(mode[i]);
		mode[i]->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
		drm_mode_probed_add(connector, mode[i]);

	}
	ctx->lcm_fresh_mode = 120;
	connector->display_info.width_mm = 150;
	connector->display_info.height_mm = 130;

	return i;
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

	pr_info("main lcm_regulator_power_init\n");
	ctx_work->vddi = regulator_get(ctx_work->dev, "vddi");
	if (IS_ERR_OR_NULL(ctx_work->vddi)) {
		ret = PTR_ERR(ctx_work->vddi);
		dev_err(ctx_work->dev, "main %s cannot get vddi regulator!ret=%d\n", __func__, ret);
		return;
	}

	ctx_work->dvdd = regulator_get(ctx_work->dev, "dvdd");
	if (IS_ERR_OR_NULL(ctx_work->dvdd)) {
		ret = PTR_ERR(ctx_work->dvdd);
		dev_err(ctx_work->dev, "main %s cannot get dvdd regulator!ret=%d\n", __func__, ret);
		return;
	}

	ctx_work->vci = regulator_get(ctx_work->dev, "vci");
	if (IS_ERR_OR_NULL(ctx_work->vci)) {
		ret = PTR_ERR(ctx_work->vci);
		dev_err(ctx_work->dev, "main %s cannot get vci regulator!ret=%d\n", __func__, ret);
		return;
	}

	ret = regulator_set_voltage(ctx_work->vddi, 1804000, 1804000);
	if (ret < 0)
		dev_err(ctx_work->dev, "main %s vddi set vol fail!ret=%d\n", __func__, ret);
	ret = regulator_enable(ctx_work->vddi);
	if (ret < 0)
		dev_err(ctx_work->dev, "main %s vddi enable error!ret=%d\n", __func__, ret);

	ret = regulator_set_voltage(ctx_work->dvdd, 1352000, 1352000);
	if (ret < 0)
		dev_err(ctx_work->dev, "main %s dvdd set vol fail!ret=%d\n", __func__, ret);
	ret = regulator_enable(ctx_work->dvdd);
	if (ret < 0)
		dev_err(ctx_work->dev, "main %s dvdd enable error!ret=%d\n", __func__, ret);

	ret = regulator_set_voltage(ctx_work->vci, 3004000, 3004000);
	if (ret < 0)
		dev_err(ctx_work->dev, "main %s vci set vol fail!ret=%d\n", __func__, ret);
	ret = regulator_enable(ctx_work->vci);
	if (ret < 0)
		dev_err(ctx_work->dev, "main %s vci enable error!ret=%d\n", __func__, ret);
}
static DECLARE_DELAYED_WORK(lcm_regulator_work, lcm_regulator_power_init);

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct lcm *ctx;
	struct device_node *backlight;
	int ret;
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;

	pr_info("main %s+\n", __func__);

	dsi_node = of_get_parent(dev->of_node);
	if (dsi_node) {
		endpoint = of_graph_get_next_endpoint(dsi_node, NULL);
		if (endpoint) {
			remote_node = of_graph_get_remote_port_parent(endpoint);
			if (!remote_node) {
				pr_info("main No panel connected,skip probe lcm\n");
				return -ENODEV;
			}
			pr_info("main device node name:%s\n", remote_node->name);
		}
	}
	if (remote_node != dev->of_node) {
		pr_info("main %s skip probe due to not current lcm\n", __func__);
		return -ENODEV;
	}

	ctx = devm_kzalloc(dev, sizeof(struct lcm), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx_work = ctx;
	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_NO_EOT_PACKET
			 | MIPI_DSI_CLOCK_NON_CONTINUOUS;

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	schedule_delayed_work(&lcm_regulator_work, msecs_to_jiffies(10000));

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(ctx->reset_gpio)) {
		dev_info(dev, "main %s: cannot get reset-gpios %ld\n", __func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	devm_gpiod_put(dev, ctx->reset_gpio);

#ifndef CONFIG_MTK_DISP_NO_LK
	ctx->prepared = true;
	ctx->enabled = true;
#endif

	drm_panel_init(&ctx->panel, dev, &lcm_drm_funcs, DRM_MODE_CONNECTOR_DSI);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &lcm_drm_funcs;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		pr_info("main %s, Line-%d\n", __func__, __LINE__);
	}

#if defined(CONFIG_MTK_PANEL_EXT)
	ret = mtk_panel_ext_create(dev, &ext_params, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;
#endif

	INIT_LIST_HEAD(&ctx->probed_modes);

	pr_info("main %s-\n", __func__);

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
	mtk_panel_detach(ext_ctx);
	mtk_panel_remove(ext_ctx);
#endif
}

static const struct of_device_id lcm_of_match[] = {
	{ .compatible = "nt37900,wqhd,dsi,cmd,csot,120hz,d9300", },
	{ }
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "nt37900_wqhd_dsi_cmd_csot_120hz_d9300",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("Transsion Inc.");
MODULE_DESCRIPTION("transsion, panel driver");
MODULE_LICENSE("GPL v2");
