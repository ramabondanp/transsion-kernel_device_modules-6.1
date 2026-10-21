/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 MediaTek Inc.
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#pragma once

#define LCM_DSI_CMD_MODE            (0)

#define VREFRESH_144                (144)
#define VREFRESH_120                (120)
#define VREFRESH_90                 (90)
#define VREFRESH_60                 (60)
#define FHD_FRAME_WIDTH             (1280)
#define FHD_FRAME_HEIGHT            (2800)
#define FHD_VSA                     (2)
#define FHD_VBP                     (26)
#define FHD_VFP_144                 (116)
#define FHD_VFP_120                 (124)
#define FHD_VFP_90                  (1108)
#define FHD_VFP_60                  (3076)
#define FHD_HSA                     (4)
#define FHD_HBP                     (20)
#define FHD_HFP_144                 (57)
#define FHD_HFP                     (157)
#define FHD_HTOTAL_144              (FHD_FRAME_WIDTH + FHD_HFP_144 + FHD_HSA + FHD_HBP)
#define FHD_HTOTAL                  (FHD_FRAME_WIDTH + FHD_HFP + FHD_HSA + FHD_HBP)
#define FHD_VTOTAL_144              (FHD_FRAME_HEIGHT + FHD_VFP_144 + FHD_VSA + FHD_VBP)
#define FHD_VTOTAL_120              (FHD_FRAME_HEIGHT + FHD_VFP_120 + FHD_VSA + FHD_VBP)
#define FHD_VTOTAL_90               (FHD_FRAME_HEIGHT + FHD_VFP_90 + FHD_VSA + FHD_VBP)
#define FHD_VTOTAL_60               (FHD_FRAME_HEIGHT + FHD_VFP_60 + FHD_VSA + FHD_VBP)
#define FHD_FRAME_TOTAL_144         (FHD_VTOTAL_144 * FHD_HTOTAL_144)
#define FHD_FRAME_TOTAL_120         (FHD_VTOTAL_120 * FHD_HTOTAL)
#define FHD_FRAME_TOTAL_90          (FHD_VTOTAL_90 * FHD_HTOTAL)
#define FHD_FRAME_TOTAL_60          (FHD_VTOTAL_60 * FHD_HTOTAL)
#define FHD_CLK_144_X10             ((FHD_FRAME_TOTAL_144 * VREFRESH_144) / 100)
#define FHD_CLK_120_X10             ((FHD_FRAME_TOTAL_120 * VREFRESH_120) / 100)
#define FHD_CLK_90_X10              ((FHD_FRAME_TOTAL_90 * VREFRESH_90) / 100)
#define FHD_CLK_60_X10              ((FHD_FRAME_TOTAL_60 * VREFRESH_60) / 100)
#define FHD_CLK_144                 (((FHD_CLK_144_X10 % 10) != 0) ?             \
						(FHD_CLK_144_X10 / 10 + 1) : (FHD_CLK_144_X10 / 10))
#define FHD_CLK_120                 (((FHD_CLK_120_X10 % 10) != 0) ?             \
						(FHD_CLK_120_X10 / 10 + 1) : (FHD_CLK_120_X10 / 10))
#define FHD_CLK_90                  (((FHD_CLK_90_X10 % 10) != 0) ?             \
						(FHD_CLK_90_X10 / 10 + 1) : (FHD_CLK_90_X10 / 10))
#define FHD_CLK_60                  (((FHD_CLK_60_X10 % 10) != 0) ?             \
						(FHD_CLK_60_X10 / 10 + 1) : (FHD_CLK_60_X10 / 10))


#define VFHD_FRAME_WIDTH            (1080)
#define VFHD_FRAME_HEIGHT           (2362)
#define VFHD_HTOTAL_144             (VFHD_FRAME_WIDTH + FHD_HFP_144 + FHD_HSA + FHD_HBP)
#define VFHD_HTOTAL                 (VFHD_FRAME_WIDTH + FHD_HFP + FHD_HSA + FHD_HBP)
#define VFHD_VTOTAL_144             (VFHD_FRAME_HEIGHT + FHD_VFP_144 + FHD_VSA + FHD_VBP)
#define VFHD_VTOTAL_120             (VFHD_FRAME_HEIGHT + FHD_VFP_120 + FHD_VSA + FHD_VBP)
#define VFHD_VTOTAL_90              (VFHD_FRAME_HEIGHT + FHD_VFP_90 + FHD_VSA + FHD_VBP)
#define VFHD_VTOTAL_60              (VFHD_FRAME_HEIGHT + FHD_VFP_60 + FHD_VSA + FHD_VBP)
#define VFHD_FRAME_TOTAL_144        (VFHD_VTOTAL_144 * VFHD_HTOTAL_144)
#define VFHD_FRAME_TOTAL_120        (VFHD_VTOTAL_120 * VFHD_HTOTAL)
#define VFHD_FRAME_TOTAL_90         (VFHD_VTOTAL_90 * VFHD_HTOTAL)
#define VFHD_FRAME_TOTAL_60         (VFHD_VTOTAL_60 * VFHD_HTOTAL)
#define VFHD_CLK_144_X10            ((VFHD_FRAME_TOTAL_144 * VREFRESH_144) / 100)
#define VFHD_CLK_120_X10            ((VFHD_FRAME_TOTAL_120 * VREFRESH_120) / 100)
#define VFHD_CLK_90_X10             ((VFHD_FRAME_TOTAL_90 * VREFRESH_90) / 100)
#define VFHD_CLK_60_X10             ((VFHD_FRAME_TOTAL_60 * VREFRESH_60) / 100)
#define VFHD_CLK_144                (((VFHD_CLK_144_X10 % 10) != 0) ?             \
						(VFHD_CLK_144_X10 / 10 + 1) : (VFHD_CLK_144_X10 / 10))
#define VFHD_CLK_120                (((VFHD_CLK_120_X10 % 10) != 0) ?             \
						(VFHD_CLK_120_X10 / 10 + 1) : (VFHD_CLK_120_X10 / 10))
#define VFHD_CLK_90                 (((VFHD_CLK_90_X10 % 10) != 0) ?             \
						(VFHD_CLK_90_X10 / 10 + 1) : (VFHD_CLK_90_X10 / 10))
#define VFHD_CLK_60                 (((VFHD_CLK_60_X10 % 10) != 0) ?             \
						(VFHD_CLK_60_X10 / 10 + 1) : (VFHD_CLK_60_X10 / 10))

#define PHYSICAL_WIDTH              71
#define PHYSICAL_HEIGHT             157

#define BRIGHTNESS_LCD_MAX          4094

#define REGFLAG_MDELAY              0x0

/* DSC Config */
#define DSC_ENABLE                  1
#define DSC_VER                     18
#define DSC_SLICE_MODE              1
#define DSC_RGB_SWAP                0
#define DSC_DSC_CFG                 40
#define DSC_RCT_ON                  1
#define DSC_BIT_PER_CHANNEL         10
#define DSC_DSC_LINE_BUF_DEPTH      11
#define DSC_BP_ENABLE               1
#define DSC_BIT_PER_PIXEL           128
#define DSC_SLICE_HEIGHT            40
#define DSC_SLICE_WIDTH             640
#define DSC_CHUNK_SIZE              640
#define DSC_XMIT_DELAY              512
#define DSC_DEC_DELAY               604
#define DSC_SCALE_VALUE             32
#define DSC_INCREMENT_INTERVAL      1030
#define DSC_DECREMENT_INTERVAL      8
#define DSC_LINE_BPG_OFFSET         13
#define DSC_NFL_BPG_OFFSET          683
#define DSC_SLICE_BPG_OFFSET        544
#define DSC_INITIAL_OFFSET          6144
#define DSC_FINAL_OFFSET            4320
#define DSC_FLATNESS_MINQP          7
#define DSC_FLATNESS_MAXQP          16
#define DSC_RC_MODEL_SIZE           8192
#define DSC_RC_EDGE_FACTOR          6
#define DSC_RC_QUANT_INCR_LIMIT0    15
#define DSC_RC_QUANT_INCR_LIMIT1    15
#define DSC_RC_TGT_OFFSET_HI        3
#define DSC_RC_TGT_OFFSET_LO        3

#define PLL_CLK                     (648)
#define DATA_RATE                   (PLL_CLK * 2)


#define CUST_ESD_CHECK              1
#define ESD_CHECK_ENABLE            1

#define CONFIG_MTK_RES_SWITCH_ON_AP     (1)

#define INIT_CODE_BACKLIGHT_INDEX (ARRAY_SIZE(init_setting) - 4)

struct LCD_setting_table {
	unsigned char count;
	unsigned char para_list[200];
};
enum MODE_ID {
	FHD_120 = 0,
	FHD_144,
	FHD_90,
	FHD_60,
#ifdef CONFIG_MTK_RES_SWITCH_ON_AP
	REAL_MODE_NUM,
	VFHD_120 = REAL_MODE_NUM,
	VFHD_144,
	VFHD_90,
	VFHD_60,
#endif
	MODE_NUM,
};

static struct LCD_setting_table bl_level[] = {
	{ 0x03, {0x51, 0x0D, 0xBB} },
};

static struct LCD_setting_table init_setting[] = {
	//Setting SPR panel boundary decolor
	{ 0x06, { 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x07 } },
	{ 0x02, { 0xB4, 0xC0 } },
	{ 0x02, { 0x6F, 0x01 } },
	{ 0x04, { 0xB4, 0x60, 0x80, 0x80 } },
	{ 0x02, { 0x6F, 0x04 } },
	{ 0x04, { 0xB4, 0x60, 0x20, 0x80 } },
	{ 0x02, { 0x6F, 0x07 } },
	{ 0x04, { 0xB4, 0x80, 0x80, 0x80 } },
	{ 0x02, { 0x6F, 0x0A } },
	{ 0x04, { 0xB4, 0x80, 0x50, 0x80 } },

	//DVDD Strong to 1v,
	//For other setting refer OP manual Ch 4.9
	{ 0x06, { 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x01 } },
	{ 0x02, { 0x6F, 0x03 } },
	{ 0x02, { 0xC5, 0x00 } },
	{ 0x02, { 0x6F, 0x04 } },
	{ 0x04, { 0xC5, 0x25, 0x25, 0x25 } },

	//Video trim OSC2 to 167Mhz,
	//For other setting refer OP manual Ch 4.3
	{ 0x06, { 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x01 } },
	{ 0x02, { 0x6F, 0x04 } },
	{ 0x02, { 0xC3, 0xFF } },
	{ 0x02, { 0x6F, 0x09 } },
	{ 0x02, { 0xC3, 0xFF } },
	{ 0x02, { 0xEA, 0xC0 } },
	{ 0x02, { 0x6F, 0x11 } },
	{ 0x02, { 0xEA, 0xC0 } },
	{ 0x02, { 0x6F, 0x07 } },
	{ 0x11, { 0xEA, 0x01, 0x02, 0x01, 0x34, 0x01, 0x34, 0x01, 0x34, 0x04, 0xD1 } },
	{ 0x02, { 0x6F, 0x18 } },
	{ 0x11, { 0xEA, 0x01, 0x8A, 0x01, 0xD7, 0x01, 0xD7, 0x01, 0xD7, 0x07, 0x5E } },

	//VGXP by pad cap
	{ 0x05, { 0xFF, 0xAA, 0x55, 0xA5, 0x80 } },
	{ 0x02, { 0x6F, 0x1F } },
	{ 0x02, { 0xF4, 0x0B } },
	{ 0x02, { 0x6F, 0x20 } },
	{ 0x02, { 0xF4, 0x3F } },
	{ 0x02, { 0x6F, 0x08 } },
	{ 0x02, { 0xFC, 0x03 } },
	{ 0x05, { 0xFF, 0xAA, 0x55, 0xA5, 0x80 } },
	{ 0x02, { 0x6F, 0x24 } },
	{ 0x02, { 0xF8, 0xFF } },

	// For Idle enter BIST
	{ 0x06, { 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00 } },
	{ 0x02, { 0xC0, 0x00 } },
	{ 0x06, { 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x01 } },
	{ 0x02, { 0x6F, 0x0B } },
	{ 0x02, { 0xD2, 0x00 } },
	{ 0x05, { 0xFF, 0xAA, 0x55, 0xA5, 0x84 } },
	{ 0x02, { 0x6F, 0x10 } },
	{ 0x02, { 0xF8, 0x02 } },
	{ 0x05, { 0xFF, 0xAA, 0x55, 0xA5, 0x84 } },
	{ 0x02, { 0xF2, 0x15 } },

	//FOD OFF
	{ 0x06, { 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x09 } },
	{ 0x02, { 0x6F, 0x00 } },
	{ 0x02, { 0xB0, 0x01 } },
	{ 0x02, { 0x6F, 0x25 } },
	{ 0x02, { 0xB0, 0x00 } },
	{ 0x02, { 0x6F, 0x01 } },
	{ 0x02, { 0x8B, 0x00 } },
	{ 0x02, { 0x87, 0x00 } },

	//Resolution Setting
	{ 0x05, { 0x2A, 0x00, 0x00, 0x04, 0xFF } },
	{ 0x05, { 0x2B, 0x00, 0x00, 0x0A, 0xEF } },

	//VESA1.2 10 bit 3.75 DSC Setting
	// For other refer OP manual Ch 4.4
	{ 0x02, { 0x90, 0x03 } },
	{ 0x02, { 0x6F, 0x01 } },
	{ 0x02, { 0x90, 0x43 } },
	{ 0x19, { 0x91, 0xAB, 0xA8, 0x00, 0x28, 0xD2, 0x00, 0x02,
		0x5C, 0x04, 0x06, 0x00, 0x08, 0x02, 0xAB, 0x02, 0x20, 0x10, 0xE0 } },

	//Dimming OFF
	{ 0x02, { 0x53, 0x20 } },

	//Video Mode Ext_VFP,VBPF Setting
	{ 0x17, { 0x3B, 0x00, 0x1c, 0x00, 0x74, 0x00, 0x1c, 0x00,
		0x7C, 0x00, 0x1c, 0x04, 0x54, 0x00, 0x1c, 0x00, 0x7C } },
	{ 0x02, { 0x6F, 0x10 } },
	{ 0x05, { 0x3B, 0x00, 0x1C, 0x00, 0x7C } },

	//DPC Temperature Setting
	{ 0x03, { 0x81, 0x01, 0x19 } },

	//FPR1 EN
	{ 0x02, { 0x88, 0x01 } },

	//120Hz Sleep Out
	//For other refer OP manual Ch 5.4.3
	{ 0x02, { 0x2F, 0x01 } },

	//TE ON
	{ 0x02, { 0x35, 0x00 } },

	//Setting Loading Effect x1.0
	{ 0x03, { 0x5F, 0x00, 0x00 } },

	//Switch DBV to 0x0DBB
	{ 0x03, { 0x51, 0x0D, 0xBB } },
	{ 0x01, { 0x11 } }, // Sleep out
	{ REGFLAG_MDELAY, { 120 } },
	{ 0x01, { 0x29 } }, // Display out
};

static struct LCD_setting_table  lcm_suspend_setting[] = {
	{ 0x01, { 0x28 } },
	{ REGFLAG_MDELAY, { 10 } },
	{ 0x01, { 0x10 } },
	{ REGFLAG_MDELAY, { 120 } },
};
