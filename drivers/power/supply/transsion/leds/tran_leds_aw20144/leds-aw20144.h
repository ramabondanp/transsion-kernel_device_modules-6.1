// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#ifndef __AW20144_H__
#define __AW20144_H__

#define OPEN_CFG_IGNORE_MAX 50
/*******************************************************************************
 *
 * struct
 *
 ******************************************************************************/
struct aw20144 {
	struct i2c_client *client;
	struct device *dev;
	struct led_classdev cdev;
	struct mutex lock;
	struct work_struct brightness_work;

	bool effect_bin;

	unsigned char rgb_num;
	unsigned char time_rise_hold;
	unsigned char time_fall_off;
	unsigned char max_rgbblink;
	unsigned char min_rgbblink;
	unsigned char rgbbrightness;

	int enable_gpio;
	int rgb_color;
	unsigned int designeffect;
	unsigned int pwm_current;
	unsigned int sl_current;
	unsigned int imax;
	unsigned int latest_cmd;
	unsigned int test_result;
	unsigned int ignore_open_circuit_test_cfg_size;
	unsigned int ignore_open_circuit_test_cfg_reg[OPEN_CFG_IGNORE_MAX];
	unsigned int ignore_open_circuit_test_cfg_val[OPEN_CFG_IGNORE_MAX];
	struct tc_led_device *led_dev;
};

typedef struct awcfgdata {
	unsigned char *cfg_data;
	unsigned int cfg_size;
} AWCFGDATA;

/*******************************************************************************
 *
 * register list
 *
 ******************************************************************************/

#define REG_GCR			0x00
#define REG_GCCR		0x01
#define REG_DGCR		0x02
#define REG_OSR0		0x03
#define REG_OSR23		0x1A
#define REG_OTCR		0x27
#define REG_SSCR		0x28
#define REG_PCCR		0x29
#define REG_UVCR		0x2A
#define REG_SRCR		0x2B
#define REG_RSTN		0x2F
#define REG_PWMH0		0x30
#define REG_PWMH1		0x31
#define REG_PWMH2		0x32
#define REG_PWML0		0x33
#define REG_PWML1		0x34
#define REG_PWML2		0x35
#define REG_PAT0T0		0x36
#define REG_PAT0T1		0x37
#define REG_PAT0T2		0x38
#define REG_PAT0T3		0x39
#define REG_PAT1T0		0x3A
#define REG_PAT1T1		0x3B
#define REG_PAT1T2		0x3C
#define REG_PAT1T3		0x3D
#define REG_PAT2T0		0x3E
#define REG_PAT2T1		0x3F
#define REG_PAT2T2		0x40
#define REG_PAT2T3		0x41
#define REG_PAT0CFG		0x42
#define REG_PAT1CFG		0x43
#define REG_PAT2CFG		0x44
#define REG_PATGO		0x45
#define REG_MIXCR		0x46
#define REG_SDCR		0x4D
#define REG_PAGE		0xF0

#define REG_SL0			0x00

/*******************************************************************************
 *
 * register detail
 *
 ******************************************************************************/

#define AW20144_CHIPID		0x74
#define AW20144_CHIPID_A2	0x71
#define AWREG_SWRST			0xAE

#define GCR_SWSEL_POS		(4)
#define GCR_SWSEL_MSK		(~(0x0F << GCR_SWSEL_POS))
#define GCR_SWSEL_VAL		(0x07)

#define BIT_CHIPEN_ENABLE	(1<<0)
#define BIT_CHIPEN_DISABLE	(~(1<<0))
#define BIT_SWITCH_ON_LED	(1<<3)
#define BIT_SWITCH_OFF_LED	(~(1<<3))
#define BIT_PATMD_AUTO		(1<<1)
#define BIT_PATMD_MANUAL	(~(1<<1))
#define BIT_PATEN_ENABLE	(1<<0)
#define BIT_PATEN_DISABLE	(~(1<<0))
#define BIT_RUN0_ENABLE		(1<<0)
#define BIT_RUN0_DISABLE	(~(1<<0))

/*******************************************************************************
 *
 * register page
 *
 ******************************************************************************/

#define AWPAGEADDR		0xF0
#define AWPAGE0			0xC0
#define AWPAGE1			0xC1
#define AWPAGE2			0xC2
#define AWPAGE3			0xC3
#define AWPAGE4			0xC4

#define AW20144_LED_NUM		0x8F
#define AW20144_RGB_NUM		0x2F

#define AW20144_CFG_CNT_PAGE1	(0x8F + 2)
#define AW20144_CFG_CNT_PAGE2	(0x8F + 2)
#define AW20144_CFG_CNT_PAGE3	(0x2F + 2)
#define AW20144_CFG_CNT_PAGE4	(0x8F + 2)

/*******************************************************************************
 *
 * register read/write access
 *
 ******************************************************************************/

#define REG_NONE_AEECSS		0
#define REG_RD_ACCESS		(1 << 0)
#define REG_WR_ACCESS		(1 << 1)
#define AW20144_REG_PAGE0_MAX	0xFF
#define AW20144_REG_PAGE1_MAX	0xFF
#define AW20144_REG_PAGE2_MAX	0xFF
#define AW20144_REG_PAGE3_MAX	0xFF

const unsigned char aw20144_reg_page0_access[AW20144_REG_PAGE0_MAX] = {
	[REG_GCR]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_GCCR]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_DGCR]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_OSR0]	= REG_RD_ACCESS,
	[REG_OTCR]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_SSCR]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PCCR]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_UVCR]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_SRCR]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_RSTN]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PWMH0]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PWMH1]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PWMH2]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PWML0]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PWML1]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PWML2]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PAT0T0]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PAT0T1]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PAT0T2]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PAT0T3]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PAT1T0]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PAT1T1]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PAT1T2]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PAT1T3]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PAT2T0]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PAT2T1]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PAT2T2]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PAT2T3]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PAT0CFG]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PAT1CFG]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PAT2CFG]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PATGO]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_MIXCR]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_SDCR]	= REG_RD_ACCESS | REG_WR_ACCESS,
	[REG_PAGE]	= REG_RD_ACCESS | REG_WR_ACCESS,
};

enum factory_mode {
    AW20144_FACTORY_NONE = 0,
    AW20144_FACTORY_SHORT = 1,
    AW20144_FACTORY_OPEN = 2,
};

#endif
