// SPDX-License-Identifier: GPL-2.0-only-or-later
/*
 * leds-aw210xx.c
 *
 * Copyright (c) 2021 Shanghai Awinic Technology Co., Ltd. All Rights Reserved
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/leds.h>
#include "tc_led_class.h"
#include "leds_aw210xx.h"
#include "leds_aw210xx_reg.h"
#include "aw210xx_reg_cfg.h"
#include "aw_breath_algorithm.h"
#include "aw_lamp_interface.h"

/******************************************************
 *
 * Marco
 *
 ******************************************************/
#define AW210XX_DRIVER_VERSION "V1.0.0"
#define AW_I2C_RETRIES 5
#define AW_I2C_RETRY_DELAY 1
#define AW_READ_CHIPID_RETRIES 2
#define AW_READ_CHIPID_RETRY_DELAY 1
#define AW210XX_CFG_NAME_MAX	64

/******************************************************
 *
 * aw210xx led parameter
 *
 ******************************************************/
struct aw210xx_cfg aw210xx_cfg_array[] = {
	{aw210xx_group_cfg_led_off, sizeof(aw210xx_group_cfg_led_off)},
	{aw21018_group_all_leds_on, sizeof(aw21018_group_all_leds_on)},
	{aw21018_group_red_leds_on, sizeof(aw21018_group_red_leds_on)},
	{aw21018_group_green_leds_on, sizeof(aw21018_group_green_leds_on)},
	{aw21018_group_blue_leds_on, sizeof(aw21018_group_blue_leds_on)},
	{aw21018_group_breath_leds_on, sizeof(aw21018_group_breath_leds_on)},
	{aw21012_group_all_leds_on, sizeof(aw21012_group_all_leds_on)},
	{aw21012_group_red_leds_on, sizeof(aw21012_group_red_leds_on)},
	{aw21012_group_green_leds_on, sizeof(aw21012_group_green_leds_on)},
	{aw21012_group_blue_leds_on, sizeof(aw21012_group_blue_leds_on)},
	{aw21012_group_breath_leds_on, sizeof(aw21012_group_breath_leds_on)},
	{aw21009_group_all_leds_on, sizeof(aw21009_group_all_leds_on)},
	{aw21009_group_red_leds_on, sizeof(aw21009_group_red_leds_on)},
	{aw21009_group_green_leds_on, sizeof(aw21009_group_green_leds_on)},
	{aw21009_group_blue_leds_on, sizeof(aw21009_group_blue_leds_on)},
	{aw21009_group_breath_leds_on, sizeof(aw21009_group_breath_leds_on)}
};
static char aw210xx_cfg_name[][AW210XX_CFG_NAME_MAX] = {
	{"aw210xx_group_cfg_led_off"},
	{"aw21018_group_all_leds_on"},
	{"aw21018_group_red_leds_on"},
	{"aw21018_group_green_leds_on"},
	{"aw21018_group_blue_leds_on"},
	{"aw21018_group_breath_leds_on"},
	{"aw21012_group_all_leds_on"},
	{"aw21012_group_red_leds_on"},
	{"aw21012_group_green_leds_on"},
	{"aw21012_group_blue_leds_on"},
	{"aw21012_group_breath_leds_on"},
	{"aw21009_group_all_leds_on"},
	{"aw21009_group_red_leds_on"},
	{"aw21009_group_green_leds_on"},
	{"aw21009_group_blue_leds_on"},
	{"aw21009_group_breath_leds_on"},
};

enum aw21xxx_effect {
	TRANSSION_ALL_OFF = 0,
	TRANSSION_ALL_ON,

	TRANSSION_GAMESINGLEKILL,
	TRANSSION_GAMEDOUBLEKILL,
	TRANSSION_GAMEFIRSTBLOOD,
	TRANSSION_GAMESTART,
	
	TRANSSION_RECORD,
	TRANSSION_PHOTO_3S,
	TRANSSION_PHOTO_5S,
	TRANSSION_PHOTO_10S,

	TRANSSION_CHARGE1,
	TRANSSION_CHARGE2,
	TRANSSION_CHARGE3,
	TRANSSION_CHARGE4,
	TRANSSION_CHARGE5,
	TRANSSION_CHARGE6,
	TRANSSION_CHARGE7,
	TRANSSION_CHARGE8,
	TRANSSION_ON_1,
	TRANSSION_ON_2,
	TRANSSION_ON_3,
	TRANSSION_ON_4,
	TRANSSION_ON_5,
	TRANSSION_ON_6,
	TRANSSION_ON_7,
	TRANSSION_ON_8,
	TRANSSION_CHARGE_SETTING,
	
	TRANSSION_CALL,
	TRANSSION_NOTICE,

	TRANSSION_AWAKE,
	TRANSSION_ANALYSYS,
	TRANSSION_ANSWER,

	TRANSSION_CALL_PREVIEW,
	TRANSSION_CHARGEFULL,
	TRANSSION_VOICE,
};

static AW21XXX_CFG aw21xxx_cfg_array[] = {
	{transsion_all_off_data,sizeof(transsion_all_off_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_all_on_data,sizeof(transsion_all_on_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},

	{transsion_gamesinglekill_data,sizeof(transsion_gamesinglekill_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_gamedoublekill_data,sizeof(transsion_gamedoublekill_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_photo_gamefirstblood_data,sizeof(transsion_photo_gamefirstblood_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_gamestart_data,sizeof(transsion_gamestart_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},

	{transsion_record_data,sizeof(transsion_record_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_photo_3s_data,sizeof(transsion_photo_3s_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_photo_5s_data,sizeof(transsion_photo_5s_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_photo_10s_data,sizeof(transsion_photo_10s_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},

	{transsion_charge1_data,sizeof(transsion_charge1_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_charge2_data,sizeof(transsion_charge2_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_charge3_data,sizeof(transsion_charge3_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_charge4_data,sizeof(transsion_charge4_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_charge5_data,sizeof(transsion_charge5_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_charge6_data,sizeof(transsion_charge6_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_charge7_data,sizeof(transsion_charge7_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_charge8_data,sizeof(transsion_charge8_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_on_1_data,sizeof(transsion_on_1_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_on_2_data,sizeof(transsion_on_2_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_on_3_data,sizeof(transsion_on_3_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_on_4_data,sizeof(transsion_on_4_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_on_5_data,sizeof(transsion_on_5_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_on_6_data,sizeof(transsion_on_6_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_on_7_data,sizeof(transsion_on_7_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_on_8_data,sizeof(transsion_on_8_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_charge_setting_data,sizeof(transsion_charge_setting_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},

	{transsion_call_data,sizeof(transsion_call_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_notice_data,sizeof(transsion_notice_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},

	{transsion_awake_data,sizeof(transsion_awake_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_analysys_data,sizeof(transsion_analysys_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_answer_data,sizeof(transsion_answer_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_call_preview_data,sizeof(transsion_call_preview_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_charge_full_data,sizeof(transsion_charge_full_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
	{transsion_voice_data,sizeof(transsion_voice_data)/sizeof(AW_MULTI_BREATH_DATA_STRUCT)},
};

/******************************************************
 *
 * aw210xx i2c write/read
 *
 ******************************************************/

static int aw210xx_i2c_writes(struct aw210xx *aw210xx,
			unsigned char reg_addr, unsigned char *buf,unsigned int len)
{
	unsigned char *reg_data = NULL;
	int ret = -1;
	reg_data = kmalloc(len+1,GFP_KERNEL);

	reg_data[0] = reg_addr;		

	memcpy(&reg_data[1],buf,len);
		
	ret = i2c_master_send(aw210xx->i2c,reg_data,len+1);

	if (ret < 0) {
		pr_err("%s: i2c write error=%d\n",__func__, ret);
	} 		

	kfree(reg_data);

	return ret;
				

}

static int
aw210xx_i2c_write(struct aw210xx *aw210xx, unsigned char reg_addr, unsigned char reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_smbus_write_byte_data(aw210xx->i2c, reg_addr, reg_data);
		if (ret < 0)
			AW_ERR("i2c_write cnt=%d ret=%d\n", cnt, ret);
		else
			break;
		cnt++;
		usleep_range(1000, 2000);
	}

	return ret;
}

static int
aw210xx_i2c_read(struct aw210xx *aw210xx, unsigned char reg_addr, unsigned char *reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_smbus_read_byte_data(aw210xx->i2c, reg_addr);
		if (ret < 0) {
			AW_ERR("i2c_read cnt=%d ret=%d\n", cnt, ret);
		} else {
			*reg_data = ret;
			break;
		}
		cnt++;
		usleep_range(1000, 2000);
	}

	return ret;
}

static int aw210xx_i2c_write_bits(struct aw210xx *aw210xx,
		unsigned char reg_addr, unsigned int mask,
		unsigned char reg_data)
{
	unsigned char reg_val;

	aw210xx_i2c_read(aw210xx, reg_addr, &reg_val);
	reg_val &= mask;
	reg_val |= (reg_data & (~mask));
	aw210xx_i2c_write(aw210xx, reg_addr, reg_val);

	return 0;
}

/*****************************************************
 * led Interface: set effect
 *****************************************************/
static void
aw210xx_update_cfg_array(struct aw210xx *aw210xx, uint8_t *p_cfg_data, uint32_t cfg_size)
{
	unsigned int i = 0;

	for (i = 0; i < cfg_size; i += 2)
		aw210xx_i2c_write(aw210xx, p_cfg_data[i], p_cfg_data[i + 1]);
}

void aw210xx_cfg_update(struct aw210xx *aw210xx)
{
	AW_LOG("aw210xx->effect = %d", aw210xx->effect);

	aw210xx_update_cfg_array(aw210xx,
			aw210xx_cfg_array[aw210xx->effect].p,
			aw210xx_cfg_array[aw210xx->effect].count);
}

void aw210xx_uvlo_set(struct aw210xx *aw210xx, bool flag)
{
	if (flag) {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_UVCR,
				AW210XX_BIT_UVPD_MASK,
				AW210XX_BIT_UVPD_DISENA);
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_UVCR,
				AW210XX_BIT_UVDIS_MASK,
				AW210XX_BIT_UVDIS_DISENA);
	} else {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_UVCR,
				AW210XX_BIT_UVPD_MASK,
				AW210XX_BIT_UVPD_ENABLE);
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_UVCR,
				AW210XX_BIT_UVDIS_MASK,
				AW210XX_BIT_UVDIS_ENABLE);
	}
}

void aw210xx_sbmd_set(struct aw210xx *aw210xx, bool flag)
{
	if (flag) {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR2,
				AW210XX_BIT_SBMD_MASK,
				AW210XX_BIT_SBMD_ENABLE);
		aw210xx->sdmd_flag = 1;
	} else {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR2,
				AW210XX_BIT_SBMD_MASK,
				AW210XX_BIT_SBMD_DISENA);
		aw210xx->sdmd_flag = 0;
	}
}

void aw210xx_rgbmd_set(struct aw210xx *aw210xx, bool flag)
{
	if (flag) {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR2,
				AW210XX_BIT_RGBMD_MASK,
				AW210XX_BIT_RGBMD_ENABLE);
		aw210xx->rgbmd_flag = 1;
	} else {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR2,
				AW210XX_BIT_RGBMD_MASK,
				AW210XX_BIT_RGBMD_DISENA);
		aw210xx->rgbmd_flag = 0;
	}
}

void aw210xx_apse_set(struct aw210xx *aw210xx, bool flag)
{
	if (flag) {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_APSE_MASK,
				AW210XX_BIT_APSE_ENABLE);
	} else {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_APSE_MASK,
				AW210XX_BIT_APSE_DISENA);
	}
}

/*****************************************************
 * aw210xx led function set
 *****************************************************/
int32_t aw210xx_osc_pwm_set(struct aw210xx *aw210xx)
{
	switch (aw210xx->osc_clk) {
	case CLK_FRQ_16M:
		AW_LOG("osc is 16MHz!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_CLKFRQ_MASK,
				AW210XX_BIT_CLKFRQ_16MHz);
		break;
	case CLK_FRQ_8M:
		AW_LOG("osc is 8MHz!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_CLKFRQ_MASK,
				AW210XX_BIT_CLKFRQ_8MHz);
		break;
	case CLK_FRQ_1M:
		AW_LOG("osc is 1MHz!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_CLKFRQ_MASK,
				AW210XX_BIT_CLKFRQ_1MHz);
		break;
	case CLK_FRQ_512k:
		AW_LOG("osc is 512KHz!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_CLKFRQ_MASK,
				AW210XX_BIT_CLKFRQ_512kHz);
		break;
	case CLK_FRQ_256k:
		AW_LOG("osc is 256KHz!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_CLKFRQ_MASK,
				AW210XX_BIT_CLKFRQ_256kHz);
		break;
	case CLK_FRQ_125K:
		AW_LOG("osc is 125KHz!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_CLKFRQ_MASK,
				AW210XX_BIT_CLKFRQ_125kHz);
		break;
	case CLK_FRQ_62_5K:
		AW_LOG("osc is 62.5KHz!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_CLKFRQ_MASK,
				AW210XX_BIT_CLKFRQ_62_5kHz);
		break;
	case CLK_FRQ_31_25K:
		AW_LOG("osc is 31.25KHz!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_CLKFRQ_MASK,
				AW210XX_BIT_CLKFRQ_31_25kHz);
		break;
	default:
		AW_LOG("this clk_pwm is unsupported!\n");
		return -AW210XX_CLK_MODE_UNSUPPORT;
	}

	return 0;
}

int32_t aw210xx_br_res_set(struct aw210xx *aw210xx)
{
	switch (aw210xx->br_res) {
	case BR_RESOLUTION_8BIT:
		AW_LOG("br resolution select 8bit!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_PWMRES_MASK,
				AW210XX_BIT_PWMRES_8BIT);
		break;
	case BR_RESOLUTION_9BIT:
		AW_LOG("br resolution select 9bit!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_PWMRES_MASK,
				AW210XX_BIT_PWMRES_9BIT);
		break;
	case BR_RESOLUTION_12BIT:
		AW_LOG("br resolution select 12bit!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_PWMRES_MASK,
				AW210XX_BIT_PWMRES_12BIT);
		break;
	case BR_RESOLUTION_9_AND_3_BIT:
		AW_LOG("br resolution select 9+3bit!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_PWMRES_MASK,
				AW210XX_BIT_PWMRES_9_AND_3_BIT);
		break;
	default:
		AW_LOG("this br_res is unsupported!\n");
		return -AW210XX_CLK_MODE_UNSUPPORT;
	}

	return 0;
}

/*****************************************************
 * aw210xx debug interface set
 *****************************************************/
static void aw210xx_update(struct aw210xx *aw210xx)
{
	aw210xx_i2c_write(aw210xx, AW210XX_REG_UPDATE, AW210XX_UPDATE_BR_SL);
}

void aw210xx_global_set(struct aw210xx *aw210xx)
{
	aw210xx_i2c_write(aw210xx, AW210XX_REG_GCCR, aw210xx->glo_current);
}
void aw210xx_current_set(struct aw210xx *aw210xx)
{
	aw210xx_i2c_write(aw210xx, AW210XX_REG_GCCR, aw210xx->set_current);
}

/*****************************************************
 *
 * aw210xx led cfg
 *
 *****************************************************/
static void aw210xx_brightness_work(struct work_struct *work)
{
	struct aw210xx *aw210xx = container_of(work, struct aw210xx, brightness_work);

	if (aw210xx->cdev.brightness > aw210xx->cdev.max_brightness)
		aw210xx->cdev.brightness = aw210xx->cdev.max_brightness;

	aw210xx_i2c_write(aw210xx, AW210XX_REG_GCCR, aw210xx->cdev.brightness);
}

static void aw210xx_set_brightness(struct led_classdev *cdev, enum led_brightness brightness)
{
	struct aw210xx *aw210xx = container_of(cdev, struct aw210xx, cdev);

	aw210xx->cdev.brightness = brightness;

	schedule_work(&aw210xx->brightness_work);
}

/*****************************************************
 * aw210xx basic function set
 *****************************************************/
void aw210xx_chipen_set(struct aw210xx *aw210xx, bool flag)
{
	if (flag) {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_CHIPEN_MASK,
				AW210XX_BIT_CHIPEN_ENABLE);
	} else {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_CHIPEN_MASK,
				AW210XX_BIT_CHIPEN_DISENA);
	}
}

static int aw210xx_hw_enable(struct aw210xx *aw210xx, bool flag)
{
	if (aw210xx && gpio_is_valid(aw210xx->enable_gpio)) {
		if (flag) {
			gpio_set_value_cansleep(aw210xx->enable_gpio, 0);
			usleep_range(2000, 2500);
			gpio_set_value_cansleep(aw210xx->enable_gpio, 1);
			usleep_range(2000, 2500);
		} else {
			gpio_set_value_cansleep(aw210xx->enable_gpio, 0);
		}
	} else {
		AW_ERR("failed\n");
	}

	return 0;
}

static int aw210xx_led_init(struct aw210xx *aw210xx)
{
	aw210xx->sdmd_flag = 0;
	aw210xx->rgbmd_flag = 0;
	/* chip enable */
	aw210xx_chipen_set(aw210xx, true);
	/* sbmd enable */
	aw210xx_sbmd_set(aw210xx, true);
	/* rgbmd enable */
	aw210xx_rgbmd_set(aw210xx, false);
	/* clk_pwm selsect */
	aw210xx_osc_pwm_set(aw210xx);
	/* br_res select */
	aw210xx_br_res_set(aw210xx);
	/* global set */
	aw210xx_global_set(aw210xx);
	/* under voltage lock out */
	aw210xx_uvlo_set(aw210xx, true);
	/* apse enable */
	aw210xx_apse_set(aw210xx, true);

	return 0;
}

static int32_t aw210xx_group_gcfg_set(struct aw210xx *aw210xx, bool flag)
{
	if (flag) {
		switch (aw210xx->chipid) {
		case AW21018_CHIPID:
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GCFG, AW21018_GROUP_ENABLE);
			return 0;
		case AW21012_CHIPID:
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GCFG, AW21012_GROUP_ENABLE);
			return 0;
		case AW21009_CHIPID:
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GCFG, AW21009_GROUP_ENABLE);
			return 0;
		default:
			AW_LOG("%s: chip is unsupported device!\n", __func__);
			return -AW210XX_CHIPID_FAILD;
		}
	} else {
		switch (aw210xx->chipid) {
		case AW21018_CHIPID:
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GCFG, AW21018_GROUP_DISABLE);
			return 0;
		case AW21012_CHIPID:
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GCFG, AW21012_GROUP_DISABLE);
			return 0;
		case AW21009_CHIPID:
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GCFG, AW21009_GROUP_DISABLE);
			return 0;
		default:
			AW_LOG("%s: chip is unsupported device!\n", __func__);
			return -AW210XX_CHIPID_FAILD;
		}
	}
}

void
aw210xx_singleled_set(struct aw210xx *aw210xx, uint32_t rgb_reg, uint32_t rgb_sl, uint32_t rgb_br)
{
	/* chip enable */
	aw210xx_chipen_set(aw210xx, true);
	/* global set */
	aw210xx->set_current = rgb_br;
	aw210xx_current_set(aw210xx);
	/* group set disable */
	aw210xx_group_gcfg_set(aw210xx, false);

	aw210xx_sbmd_set(aw210xx, true);
	aw210xx_rgbmd_set(aw210xx, false);
	aw210xx_uvlo_set(aw210xx, true);

	/* set sl */
	aw210xx->rgbcolor = rgb_sl & 0xff;
	aw210xx_i2c_write(aw210xx, AW210XX_REG_SL00 + rgb_reg, aw210xx->rgbcolor);

	/* br set */
	aw210xx_i2c_write(aw210xx, AW210XX_REG_BR00L + rgb_reg, rgb_br);
	if (aw210xx->sdmd_flag == 0)
		aw210xx_i2c_write(aw210xx, AW210XX_REG_BR00H + rgb_reg, rgb_br);
	/* update */
	aw210xx_update(aw210xx);
}

/*****************************************************
 * open short detect
 *****************************************************/
void aw210xx_open_detect_cfg(struct aw210xx *aw210xx)
{
	/*enable open detect*/
	aw210xx_i2c_write(aw210xx, AW210XX_REG_OSDCR, AW210XX_OPEN_DETECT_EN);
	/*set DCPWM = 1*/
	aw210xx_i2c_write_bits(aw210xx, AW210XX_REG_SSCR,
							AW210XX_DCPWM_SET_MASK,
							AW210XX_DCPWM_SET);
	/*set Open threshold = 0.2v*/
	aw210xx_i2c_write_bits(aw210xx,
							AW210XX_REG_OSDCR,
							AW210XX_OPEN_THRESHOLD_SET_MASK,
							AW210XX_OPEN_THRESHOLD_SET);
}

void aw210xx_short_detect_cfg(struct aw210xx *aw210xx)
{
	/*enable short detect*/
	aw210xx_i2c_write(aw210xx, AW210XX_REG_OSDCR, AW210XX_SHORT_DETECT_EN);
	/*set DCPWM = 1*/
	aw210xx_i2c_write_bits(aw210xx, AW210XX_REG_SSCR,
							AW210XX_DCPWM_SET_MASK,
							AW210XX_DCPWM_SET);
	/*set Short threshold = 1v*/
	aw210xx_i2c_write_bits(aw210xx,
							AW210XX_REG_OSDCR,
							AW210XX_SHORT_THRESHOLD_SET_MASK,
							AW210XX_SHORT_THRESHOLD_SET);
}

void aw210xx_open_short_dis(struct aw210xx *aw210xx)
{
	aw210xx_i2c_write(aw210xx, AW210XX_REG_OSDCR, AW210XX_OPEN_SHORT_DIS);
	/*SET DCPWM = 0*/
	aw210xx_i2c_write_bits(aw210xx, AW210XX_REG_SSCR, AW210XX_DCPWM_SET_MASK,
							AW210XX_DCPWM_CLEAN);
}
void aw210xx_open_short_detect(struct aw210xx *aw210xx, int32_t detect_flg, u8 *reg_val)
{
	/*config for open shor detect*/
	if (detect_flg == AW210XX_OPEN_DETECT)
		aw210xx_open_detect_cfg(aw210xx);
	else if (detect_flg == AW210XX_SHORT_DETECT)
		aw210xx_short_detect_cfg(aw210xx);
	/*read detect result*/
	aw210xx_i2c_read(aw210xx, AW210XX_REG_OSST0, &reg_val[0]);
	aw210xx_i2c_read(aw210xx, AW210XX_REG_OSST1, &reg_val[1]);
	aw210xx_i2c_read(aw210xx, AW210XX_REG_OSST2, &reg_val[2]);
	/*close for open short detect*/
	aw210xx_open_short_dis(aw210xx);
}

/******************************************************
 *
 * effect update
 *
******************************************************/
static int aw210xx_effect_close(struct aw210xx *aw210xx)
{

	memset(aw210xx_col_data, 0,sizeof(aw210xx_col_data));
	aw210xx_i2c_writes(aw210xx, AW210XX_REG_SL00,aw210xx_col_data, LED_NUM);
	return 0;
}

void aw210xx_rgb_multi_breath_init( AW_MULTI_BREATH_DATA_STRUCT *data)
{
	unsigned char i;

	aw210xx_interface.getBrightnessfunc = aw_get_breath_brightness_algo_func(BREATH_ALGO_GAMMA_CORRECTION);
	algo_data.cur_frame = 0;
	algo_data.total_frames = 20;
	algo_data.data_start = 0;
	algo_data.data_end = 0;
	aw210xx_interface.p_algo_data = &algo_data;

	for (i = 0; i < LED_NUM; i++) {
		colorful_cur_frame[i] = 0;
		colorful_total_frames[i] = 20;
		colorful_cur_color_index[i] = 0;
		colorful_cur_phase[i] = 0;
		colorful_phase_nums[i] = 5;
		breath_cur_phase[i] = 0;
		if(data[i].effect == 12){
			breath_phase_nums[i] = 7;
		}else{
			breath_phase_nums[i] = 6;
		}

		if(data[i].effect == 1 || data[i].effect == 5 || data[i].effect == 7 || data[i].effect == 9 || data[i].effect == 11 || data[i].effect == 12){
			aw210xx_algo_data[i].cur_frame = 1;
		}else{
			aw210xx_algo_data[i].cur_frame = 0;
		}
		aw210xx_algo_data[i].total_frames = (data[i].time[0] + 19) / 20 + 1;
		if(data[i].effect == 9 || data[i].effect == 12){
			aw210xx_algo_data[i].data_start = 0;
			aw210xx_algo_data[i].data_end = 0;
		}else{
			aw210xx_algo_data[i].data_start = data[i].fadel;
			aw210xx_algo_data[i].data_end = data[i].fadel;
		}
		source_color[i] = 0x00;
		destination_color[i] = 0x00;
		dim_data[i] =0;
		fade_data[i] = 0;
		loop_end[i] = 0;
		breath_cur_loop[i] = 0;
	}
}


bool aw210xx_check_idx_valid(int idx, int max_idx, int loop, int phase, int frame)
{
	if (idx < 0 || idx >= max_idx) {
		pr_err("idx %d out of range %d, loop %d, phase %d, frame %d\n", idx, max_idx, loop, phase, frame);
		return false;
	}
	return true;
}

void aw210xx_update_frame_idx( AW_MULTI_BREATH_DATA_STRUCT *data)
{
	unsigned char i;
	int update_frame_idx = 0;
	int idx = 0;
	for (i = 0; i < LED_NUM; i++) {
		update_frame_idx = 1;
		if (loop_end[i] == 1)
			continue;

		source_color[i] = data[i].rgb_color_list[0];

		aw210xx_algo_data[i].cur_frame++;
		if (aw210xx_algo_data[i].cur_frame >= aw210xx_algo_data[i].total_frames) {
			
			if(data[i].effect == 1 || data[i].effect == 5 || data[i].effect == 7 || data[i].effect == 9 || data[i].effect == 11 || data[i].effect == 12){
				aw210xx_algo_data[i].cur_frame = 1;
			}else{
				aw210xx_algo_data[i].cur_frame = 0;
			}

			breath_cur_phase[i]++;	
			if(breath_cur_phase[i] == 5){
				if(data[i].effect == 0 || data[i].effect == 4 || data[i].effect == 5 || data[i].effect == 7 || data[i].effect == 8 || data[i].effect == 9 || data[i].effect == 10 || data[i].effect == 11 || data[i].effect == 12){
					if(data[i].repeat_nums == 0){
						breath_cur_phase[i] = 1;
						breath_cur_loop[i] = 0;
					}
					else if(breath_cur_loop[i] < data[i].repeat_nums - 1){
						breath_cur_phase[i] = 1;
						breath_cur_loop[i]++;
					}	
				}else{
					if(data[i].repeat_nums == 0){
						breath_cur_phase[i] = 0;
						breath_cur_loop[i] = (breath_cur_loop[i] + 1) %2;
					
					}
					else if(breath_cur_loop[i] < data[i].repeat_nums - 1){
						breath_cur_phase[i] = 0;
						breath_cur_loop[i]++;

					}
				}
			}
			if(breath_cur_phase[i] >= breath_phase_nums[i]){
				breath_cur_phase[i] = 0;
				update_frame_idx = 0;
			}
			if (update_frame_idx) {
				if(data[i].effect == 3 || data[i].effect == 6){
					
					if(breath_cur_loop[i]%2==1&&(breath_cur_phase[i]==0 || breath_cur_phase[i]==4) ){
						aw210xx_algo_data[i].total_frames=(240-data[i].time[breath_cur_phase[i]])/20+1;
					}else{
						aw210xx_algo_data[i].total_frames =(data[i].time[breath_cur_phase[i]])/20 + 1;
					}
					
					if(data[i].effect == 6 &&  breath_cur_loop[i] >= data[i].repeat_nums - 1 &&( breath_cur_phase[i] == 3 || breath_cur_phase[i] == 4) ){
						aw210xx_algo_data[i].total_frames =	 1;
					}else if(data[i].effect == 6 && breath_cur_loop[i] >= data[i].repeat_nums - 1 && breath_cur_phase[i] == 2){
						aw210xx_algo_data[i].total_frames =(160+data[i].time[0])/20 + 1;		
					}
				}else if(data[i].effect == 4){
					idx = breath_cur_loop[i]*4 + breath_cur_phase[i]-1;
					switch(i){
						case 0:
							if (!aw210xx_check_idx_valid(idx, sizeof(time1) / sizeof(time1[0]), breath_cur_loop[i], breath_cur_phase[i], aw210xx_algo_data[i].cur_frame)) {
								break;
							}
							aw210xx_algo_data[0].total_frames = time1[breath_cur_loop[0]*4+breath_cur_phase[0]-1]/20+1;
							break;
						case 1:
							if (!aw210xx_check_idx_valid(idx, sizeof(time2) / sizeof(time2[0]), breath_cur_loop[i], breath_cur_phase[i], aw210xx_algo_data[i].cur_frame)) {
								break;
							}
							aw210xx_algo_data[1].total_frames = time2[breath_cur_loop[1]*4+breath_cur_phase[1]-1]/20+1;
							break;
						case 2:
							if (!aw210xx_check_idx_valid(idx, sizeof(time3) / sizeof(time3[0]), breath_cur_loop[i], breath_cur_phase[i], aw210xx_algo_data[i].cur_frame)) {
								break;
							}
							aw210xx_algo_data[2].total_frames = time3[breath_cur_loop[2]*4+breath_cur_phase[2]-1]/20+1;
							break;
						case 3:
							if (!aw210xx_check_idx_valid(idx, sizeof(time4) / sizeof(time4[0]), breath_cur_loop[i], breath_cur_phase[i], aw210xx_algo_data[i].cur_frame)) {
								break;
							}
							aw210xx_algo_data[3].total_frames =	time4[breath_cur_loop[3]*4+breath_cur_phase[3]-1]/20+1;
							break;
						case 4:
							if (!aw210xx_check_idx_valid(idx, sizeof(time5) / sizeof(time5[0]), breath_cur_loop[i], breath_cur_phase[i], aw210xx_algo_data[i].cur_frame)) {
								break;
							}
							aw210xx_algo_data[4].total_frames =	time5[breath_cur_loop[4]*4+breath_cur_phase[4]-1]/20+1;
							break;
						case 5:
							if (!aw210xx_check_idx_valid(idx, sizeof(time6) / sizeof(time6[0]), breath_cur_loop[i], breath_cur_phase[i], aw210xx_algo_data[i].cur_frame)) {
								break;
							}
							aw210xx_algo_data[5].total_frames =	time6[breath_cur_loop[5]*4+breath_cur_phase[5]-1]/20+1;
							break;
						case 6:
							if (!aw210xx_check_idx_valid(idx, sizeof(time7) / sizeof(time7[0]), breath_cur_loop[i], breath_cur_phase[i], aw210xx_algo_data[i].cur_frame)) {
								break;
							}
							aw210xx_algo_data[6].total_frames =	time7[breath_cur_loop[6]*4+breath_cur_phase[6]-1]/20+1;
							break;
						case 7:
							if (!aw210xx_check_idx_valid(idx, sizeof(time8) / sizeof(time8[0]), breath_cur_loop[i], breath_cur_phase[i], aw210xx_algo_data[i].cur_frame)) {
								break;
							}
							aw210xx_algo_data[7].total_frames =	time8[breath_cur_loop[7]*4+breath_cur_phase[7]-1]/20+1;
							break;
						default:
							break;
					}
				}else if(data[i].effect == 8){
					switch(i){
						idx = breath_cur_loop[i]*4 + breath_cur_phase[i]-1;
						case 0:
							if (!aw210xx_check_idx_valid(idx, sizeof(game_double_time1) / sizeof(game_double_time1[0]), breath_cur_loop[i], breath_cur_phase[i], aw210xx_algo_data[i].cur_frame)) {
								break;
							}
							aw210xx_algo_data[0].total_frames = game_double_time1[breath_cur_loop[0]*4+breath_cur_phase[0]-1]/20+1;
							break;
						case 1:
							if (!aw210xx_check_idx_valid(idx, sizeof(game_double_time2) / sizeof(game_double_time2[0]), breath_cur_loop[i], breath_cur_phase[i], aw210xx_algo_data[i].cur_frame)) {
								break;
							}
							aw210xx_algo_data[1].total_frames = game_double_time2[breath_cur_loop[1]*4+breath_cur_phase[1]-1]/20+1;
							break;
						case 2:
							if (!aw210xx_check_idx_valid(idx, sizeof(game_double_time3) / sizeof(game_double_time3[0]), breath_cur_loop[i], breath_cur_phase[i], aw210xx_algo_data[i].cur_frame)) {
								break;
							}
							aw210xx_algo_data[2].total_frames = game_double_time3[breath_cur_loop[2]*4+breath_cur_phase[2]-1]/20+1;
							break;
						case 3:
							if (!aw210xx_check_idx_valid(idx, sizeof(game_double_time4) / sizeof(game_double_time4[0]), breath_cur_loop[i], breath_cur_phase[i], aw210xx_algo_data[i].cur_frame)) {
								break;
							}
							aw210xx_algo_data[3].total_frames = game_double_time4[breath_cur_loop[3]*4+breath_cur_phase[3]-1]/20+1;
							break;
						case 4:
							if (!aw210xx_check_idx_valid(idx, sizeof(game_double_time5) / sizeof(game_double_time5[0]), breath_cur_loop[i], breath_cur_phase[i], aw210xx_algo_data[i].cur_frame)) {
								break;
							}
							aw210xx_algo_data[4].total_frames = game_double_time5[breath_cur_loop[4]*4+breath_cur_phase[4]-1]/20+1;
							break;
						case 5:
							if (!aw210xx_check_idx_valid(idx, sizeof(game_double_time6) / sizeof(game_double_time6[0]), breath_cur_loop[i], breath_cur_phase[i], aw210xx_algo_data[i].cur_frame)) {
								break;
							}
							aw210xx_algo_data[5].total_frames = game_double_time6[breath_cur_loop[5]*4+breath_cur_phase[5]-1]/20+1;
							break;
						case 6:
							if (!aw210xx_check_idx_valid(idx, sizeof(game_double_time7) / sizeof(game_double_time7[0]), breath_cur_loop[i], breath_cur_phase[i], aw210xx_algo_data[i].cur_frame)) {
								break;
							}
							aw210xx_algo_data[6].total_frames = game_double_time7[breath_cur_loop[6]*4+breath_cur_phase[6]-1]/20+1;
							break;
						case 7:
							if (!aw210xx_check_idx_valid(idx, sizeof(game_double_time8) / sizeof(game_double_time8[0]), breath_cur_loop[i], breath_cur_phase[i], aw210xx_algo_data[i].cur_frame)) {
								break;
							}
							aw210xx_algo_data[7].total_frames = game_double_time8[breath_cur_loop[7]*4+breath_cur_phase[7]-1]/20+1;
							break;
						default:
							break;
					}

				}else if(data[i].effect == 5 && breath_cur_loop[i] >= data[i].repeat_nums - 1 && breath_cur_phase[i] == 2){
					aw210xx_algo_data[i].total_frames = (560 + 140 - data[i].time[0])/20 + 1;
				}else if(data[i].effect == 5 && breath_cur_loop[i] >= data[i].repeat_nums - 1 && breath_cur_phase[i] == 3){
					aw210xx_algo_data[i].total_frames =500/20 + 1;
				}else if(data[i].effect == 7 && breath_cur_loop[i] >= data[i].repeat_nums - 1 && breath_cur_phase[i] == 2){
					aw210xx_algo_data[i].total_frames = (700 + 140 - data[i].time[0])/20 + 1;
				}else if(data[i].effect == 7 && breath_cur_loop[i] >= data[i].repeat_nums - 1 && breath_cur_phase[i] == 3){
					aw210xx_algo_data[i].total_frames =500/20 + 1;
				}else if(data[i].effect == 7 && breath_cur_loop[i] >= data[i].repeat_nums - 1 && breath_cur_phase[i] == 4){
					aw210xx_algo_data[i].total_frames =1;
				}else if(data[i].effect == 12 && breath_cur_loop[i] >= data[i].repeat_nums - 1 && breath_cur_phase[i] == 2){
					aw210xx_algo_data[i].total_frames =(700- data[i].time[0]+500)/20 + 1;
				}else if(data[i].effect == 12 && breath_cur_loop[i] >= data[i].repeat_nums - 1 && breath_cur_phase[i] == 3){
					aw210xx_algo_data[i].total_frames =40/20 + 1;
				}else if(data[i].effect == 12 && breath_cur_loop[i] >= data[i].repeat_nums - 1 && breath_cur_phase[i] == 4){
					aw210xx_algo_data[i].total_frames =260/20 + 1;
				}else if(data[i].effect == 12 && breath_cur_loop[i] >= data[i].repeat_nums - 1 && breath_cur_phase[i] == 5){
					aw210xx_algo_data[i].total_frames =940/20 + 1;
				}else if(data[i].effect == 12 && breath_cur_loop[i] >= data[i].repeat_nums - 1 && breath_cur_phase[i] == 6){
					aw210xx_algo_data[i].total_frames =160/20 + 1;
				}else{
					aw210xx_algo_data[i].total_frames=(data[i].time[breath_cur_phase[i]])/20 + 1;
				}
				if(aw210xx_algo_data[i].total_frames == 1){
					continue;
				}
				if (breath_cur_phase[i] == 1) {
					if(data[i].effect == 12 && breath_cur_loop[i] >= data[i].repeat_nums - 1 && i>3)
					{
						aw210xx_algo_data[i].data_start = data[i].fadel;
						aw210xx_algo_data[i].data_end = data[i].fadel;
					}else{
						aw210xx_algo_data[i].data_start = data[i].fadel;
						aw210xx_algo_data[i].data_end = data[i].fadeh;
					}
				} else if (breath_cur_phase[i] == 2) {
					if(data[i].effect == 12 && breath_cur_loop[i] >= data[i].repeat_nums - 1 && i>3){
						aw210xx_algo_data[i].data_start = data[i].fadel;
						aw210xx_algo_data[i].data_end = data[i].fadel;
					}else if(data[i].effect == 12 && breath_cur_loop[i] >= data[i].repeat_nums - 1 && i<=3){
						aw210xx_algo_data[i].data_start = data[i].fadeh;
						aw210xx_algo_data[i].data_end = data[i].fadel;
					}
					else{
						aw210xx_algo_data[i].data_start = data[i].fadeh;
						aw210xx_algo_data[i].data_end = data[i].fadeh;
					}
				}else if(breath_cur_phase[i] == 3){
					if(data[i].effect == 12 && breath_cur_loop[i] >= data[i].repeat_nums - 1 && i>3){
						aw210xx_algo_data[i].data_start = data[i].fadel;
						aw210xx_algo_data[i].data_end = 0;
					}else if(data[i].effect == 12 && breath_cur_loop[i] >= data[i].repeat_nums - 1 && i<=3){
						aw210xx_algo_data[i].data_start = data[i].fadel;
						aw210xx_algo_data[i].data_end = 0;
					}else if(data[i].effect == 10 && breath_cur_loop[i] >= data[i].repeat_nums - 1){
						aw210xx_algo_data[i].data_start = data[i].fadeh;
						aw210xx_algo_data[i].data_end = 0;
					}else{
						aw210xx_algo_data[i].data_start = data[i].fadeh;
						aw210xx_algo_data[i].data_end = data[i].fadel;
					}
				}else if(breath_cur_phase[i] == 4){
					if(data[i].effect == 12 && breath_cur_loop[i] >= data[i].repeat_nums - 1 ){
						aw210xx_algo_data[i].data_start = 0;
						aw210xx_algo_data[i].data_end = data[i].fadeh;
					}
					else if(data[i].effect == 2){
						aw210xx_algo_data[i].data_start = data[i].fadel;
						aw210xx_algo_data[i].data_end = data[i].fadeh;
					}else{
						aw210xx_algo_data[i].data_start = data[i].fadel;
						aw210xx_algo_data[i].data_end = data[i].fadel;
					}
				} else if (breath_cur_phase[i] == 5){
					if(data[i].effect == 12 && breath_cur_loop[i] >= data[i].repeat_nums - 1 ){
						aw210xx_algo_data[i].data_start = data[i].fadeh;
						aw210xx_algo_data[i].data_end =data[i].fadeh;
					}
					else if(data[i].effect == 1){
						aw210xx_algo_data[i].data_start = data[i].fadel;
						aw210xx_algo_data[i].data_end = data[i].fadeh;
					}else if(data[i].effect == 2 || data[i].effect == 3){
						aw210xx_algo_data[i].data_start = data[i].fadeh;
						aw210xx_algo_data[i].data_end = data[i].fadel;
					}else if(data[i].effect == 6){
						aw210xx_algo_data[i].data_start = data[i].fadeh;
						aw210xx_algo_data[i].data_end = 20;
					}else{
						aw210xx_algo_data[i].data_start = data[i].fadel;
						aw210xx_algo_data[i].data_end = data[i].fadel;
					}
				}else if(breath_cur_phase[i] == 6){
					aw210xx_algo_data[i].data_start = data[i].fadeh;
					aw210xx_algo_data[i].data_end = 0;
				}else{
					aw210xx_algo_data[i].data_start = data[i].fadel;
					aw210xx_algo_data[i].data_end = data[i].fadel;
				}
				/* breath_cur_phase[i]++; */
			} else {
				aw210xx_algo_data[i].cur_frame = 0;
				aw210xx_algo_data[i].total_frames = 1;
				aw210xx_algo_data[i].data_start = 0;
				aw210xx_algo_data[i].data_end = 0;
				loop_end[i] = 1;
			}
		}
		
		destination_color[i] = data[i].rgb_color_list[0];

	}
}

void aw210xx_frame_display(void)
{
	unsigned char i = 0;
	unsigned char brightness = 0;

	for (i = 0; i < LED_NUM; i++) {
		aw210xx_interface.p_color_1 = source_color[i];
		aw210xx_interface.p_color_2 = destination_color[i];
		aw210xx_interface.cur_frame= aw210xx_algo_data[i].cur_frame;
		aw210xx_interface.total_frames = aw210xx_algo_data[i].total_frames;
		if(aw210xx_interface.total_frames > 1){
			aw_set_colorful_rgb_data(i, dim_data, &aw210xx_interface);
			brightness = aw210xx_interface.getBrightnessfunc(&aw210xx_algo_data[i]);

			if (breath_cur_phase[i] == 0)
				brightness = aw210xx_algo_data[i].data_start;
			aw_set_rgb_brightness(i, fade_data, brightness);
		}
	}
}
void aw210xx_update_effect(struct aw210xx *aw210xx)
{
	unsigned char i = 0;

	for (i = 0; i < LED_NUM; i++) {
		aw210xx_col_data[aw210xx_reg_map[i * 2 + 0] - 0x46] = dim_data[i];
		aw210xx_br_data[aw210xx_reg_map[i * 2 + 1] - 0x21] = fade_data[i];
	}
	aw210xx_i2c_writes(aw210xx, AW210XX_REG_BR00L,aw210xx_br_data, LED_NUM);
	aw210xx_i2c_writes(aw210xx, AW210XX_REG_SL00,aw210xx_col_data, LED_NUM);
	aw210xx_i2c_write(aw210xx, AW210XX_REG_UPDATE, 0x00);

}

static void aw21xxx_rgb_multi_breath(struct aw210xx *aw210xx)
{
	aw210xx->num = 0;
	aw210xx_rgb_multi_breath_init(aw210xx->effect_data);
	aw210xx_frame_display();
	aw210xx_update_effect(aw210xx);

}
 static int aw_is_get_stop(void)
{
	unsigned int is_stop = 0;
	unsigned int i = 0;
	for(i=0; i<LED_NUM; i++){
		if(loop_end[i] == 1){
			is_stop++;
		}else{
			break;
		}
	}
	if(is_stop >= LED_NUM){
		return 1;
	}else{
		return 0;
	}
}

static int aw210xx_start_next_effect( struct aw210xx *aw210xx,int size)
{

	if( aw_is_get_stop() ==1 && aw210xx->num < size){

		aw210xx->num ++;
		if(aw210xx->num >= size){
			aw210xx->num = 0;
			return -1;
		}

		aw210xx->effect_data += LED_NUM;	

		aw210xx_rgb_multi_breath_init(aw210xx->effect_data);

		aw210xx_frame_display();

		//aw210xx_update();
	}
	return 0;
}

static void aw210xx_rgb_multi_breath_work(struct work_struct *work)
{

	struct aw210xx *aw210xx = container_of(work, struct aw210xx,
							cfg_work);	
	unsigned char ret = 0;
	mutex_lock(&aw210xx->cfg_lock);
	
	if(aw210xx->cfg < sizeof(aw21xxx_cfg_array)/sizeof( AW21XXX_CFG)){
		hrtimer_cancel(&aw210xx->timer);
		aw210xx_update_frame_idx(aw210xx -> effect_data);
		aw210xx_frame_display();
		aw210xx_update_effect(aw210xx);
		ret = aw210xx_start_next_effect(aw210xx,aw21xxx_cfg_array[aw210xx->cfg].count/LED_NUM);
		if( ret == 0){
			hrtimer_start(&aw210xx->timer,ktime_set(aw210xx->ms/1000,(aw210xx->ms%1000)*1000000),HRTIMER_MODE_REL);
		}else{
			pr_info("cfg effect perform compelete %s\n", __func__);
		}
		
	}else{
		pr_info("timer cancel %s\n", __func__);
	}
	mutex_unlock(&aw210xx->cfg_lock);

}



static enum hrtimer_restart aw210xx_timer_func(struct hrtimer *timer)
{
	struct aw210xx *aw210xx = container_of(timer, struct aw210xx,
							timer);
	schedule_work(&aw210xx->cfg_work);

	return HRTIMER_NORESTART;
}


/******************************************************
 *
 * sys group attribute: reg
 *
 ******************************************************/
static ssize_t
reg_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	uint32_t databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		if (aw210xx_reg_access[(uint8_t)databuf[0]] & REG_WR_ACCESS)
			aw210xx_i2c_write(aw210xx, (uint8_t)databuf[0], (uint8_t)databuf[1]);
	}

	return len;
}

static ssize_t reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	ssize_t len = 0;
	unsigned int i = 0;
	unsigned char reg_val = 0;
	uint8_t br_max = 0;
	uint8_t sl_val = 0;

	aw210xx_i2c_read(aw210xx, AW210XX_REG_GCR, &reg_val);
	len += snprintf(buf + len, PAGE_SIZE - len,
			"reg:0x%02x=0x%02x\n", AW210XX_REG_GCR, reg_val);
	switch (aw210xx->chipid) {
	case AW21018_CHIPID:
		br_max = AW210XX_REG_BR17H;
		sl_val = AW210XX_REG_SL17;
		break;
	case AW21012_CHIPID:
		br_max = AW210XX_REG_BR11H;
		sl_val = AW210XX_REG_SL11;
		break;
	case AW21009_CHIPID:
		br_max = AW210XX_REG_BR08H;
		sl_val = AW210XX_REG_SL08;
		break;
	default:
		AW_LOG("chip is unsupported device!\n");
		return len;
	}

	for (i = AW210XX_REG_BR00L; i <= br_max; i++) {
		if (!(aw210xx_reg_access[i] & REG_RD_ACCESS))
			continue;
		aw210xx_i2c_read(aw210xx, i, &reg_val);
		len += snprintf(buf + len, PAGE_SIZE - len, "reg:0x%02x=0x%02x\n", i, reg_val);
	}
	for (i = AW210XX_REG_SL00; i <= sl_val; i++) {
		if (!(aw210xx_reg_access[i] & REG_RD_ACCESS))
			continue;
		aw210xx_i2c_read(aw210xx, i, &reg_val);
		len += snprintf(buf + len, PAGE_SIZE - len, "reg:0x%02x=0x%02x\n", i, reg_val);
	}
	for (i = AW210XX_REG_GCCR; i <= AW210XX_REG_GCFG; i++) {
		if (!(aw210xx_reg_access[i] & REG_RD_ACCESS))
			continue;
		aw210xx_i2c_read(aw210xx, i, &reg_val);
		len += snprintf(buf + len, PAGE_SIZE - len, "reg:0x%02x=0x%02x\n", i, reg_val);
	}

	return len;
}

static ssize_t
hwen_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	int rc;
	unsigned int val = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	if (val > 0)
		aw210xx_hw_enable(aw210xx, true);
	else
		aw210xx_hw_enable(aw210xx, false);

	return len;
}

static ssize_t
hwen_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "hwen=%d\n",
			gpio_get_value(aw210xx->enable_gpio));
	return len;
}
static ssize_t cfg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	ssize_t len = 0;
	unsigned int i;

	for (i=0; i < sizeof(aw21xxx_cfg_array)/sizeof( AW21XXX_CFG);i++){
		len += snprintf(buf + len, PAGE_SIZE - len,
				"effect[%d]: %ps\n", i, aw21xxx_cfg_array[i].p);
	}
	len += snprintf(buf + len, PAGE_SIZE - len,
				"current effect[%d]: %ps\n", aw210xx->cfg, aw21xxx_cfg_array[aw210xx->cfg].p);
	return len;
}

static ssize_t
cfg_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	int rc;
	unsigned int val = 0;
	rc = kstrtouint(buf, 0, &val);
	aw210xx->cfg = val;
	if (rc < 0)
		return rc;
	if (val < (sizeof(aw21xxx_cfg_array) / sizeof(AW21XXX_CFG) )) {
		aw210xx->effect_data = aw21xxx_cfg_array[aw210xx->cfg].p;
		aw210xx_led_init(aw210xx);
		hrtimer_cancel(&aw210xx->timer);
		aw21xxx_rgb_multi_breath(aw210xx);
		schedule_work(&aw210xx->cfg_work);	
	}else{
		mutex_lock(&aw210xx->cfg_lock);
		hrtimer_cancel(&aw210xx->timer);
		aw210xx_effect_close(aw210xx);
		mutex_unlock(&aw210xx->cfg_lock);
	}
	return len;
}
static ssize_t
effect_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	ssize_t len = 0;
	unsigned int i;

	for (i = 0; i < (sizeof(aw210xx_cfg_array) / sizeof(struct aw210xx_cfg)); i++) {
		len += snprintf(buf + len, PAGE_SIZE - len, "effect[%x]: %s\n",
				i, aw210xx_cfg_name[i]);
	}

	len += snprintf(buf + len, PAGE_SIZE - len, "current effect[%d]: %s\n",
			aw210xx->effect, aw210xx_cfg_name[aw210xx->effect]);
	return len;
}

static ssize_t
effect_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	int rc;
	unsigned int val = 0;

	rc = kstrtouint(buf, 10, &val);
	if (rc < 0)
		return rc;
	if ((val >= (sizeof(aw210xx_cfg_array) / sizeof(struct aw210xx_cfg))) || (val < 0)) {
		pr_err("%s, store effect num error.\n", __func__);
		return -EINVAL;
	}

	aw210xx->effect = val;

	aw210xx_cfg_update(aw210xx);

	return len;
}

static ssize_t
rgbcolor_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	uint32_t rgb_num = 0;
	uint32_t rgb_data = 0;

	if (sscanf(buf, "%x %x", &rgb_num, &rgb_data) == 2) {
		aw210xx_chipen_set(aw210xx, true);
		aw210xx_sbmd_set(aw210xx, true);
		aw210xx_rgbmd_set(aw210xx, true);
		aw210xx_global_set(aw210xx);
		aw210xx_uvlo_set(aw210xx, true);

		/* set sl */
		aw210xx->rgbcolor = (rgb_data & 0xff0000) >> 16;
		aw210xx_i2c_write(aw210xx,
				AW210XX_REG_SL00 + (uint8_t)rgb_num * 3,
				aw210xx->rgbcolor);

		aw210xx->rgbcolor = (rgb_data & 0x00ff00) >> 8;
		aw210xx_i2c_write(aw210xx,
				AW210XX_REG_SL00 + (uint8_t)rgb_num * 3 + 1,
				aw210xx->rgbcolor);

		aw210xx->rgbcolor = (rgb_data & 0x0000ff);
		aw210xx_i2c_write(aw210xx,
				AW210XX_REG_SL00 + (uint8_t)rgb_num * 3 + 2,
				aw210xx->rgbcolor);

		/* br set */
		aw210xx_i2c_write(aw210xx,
				AW210XX_REG_BR00L + (uint8_t)rgb_num,
				AW210XX_GLOBAL_DEFAULT_SET);

		/* update */
		aw210xx_update(aw210xx);
	}

	return len;
}

static ssize_t
singleled_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	uint32_t led_num = 0;
	uint32_t rgb_data = 0;
	uint32_t rgb_brightness = 0;

	if (sscanf(buf, "%x %x %x", &led_num, &rgb_data, &rgb_brightness) == 3) {
		if (aw210xx->chipid == AW21018_CHIPID) {
			if (led_num > AW21018_LED_NUM)
				led_num = AW21018_LED_NUM;
		} else if (aw210xx->chipid == AW21012_CHIPID) {
			if (led_num > AW21012_LED_NUM)
				led_num = AW21012_LED_NUM;
		} else {
			if (led_num > AW21009_LED_NUM)
				led_num = AW21009_LED_NUM;
		}
		aw210xx_singleled_set(aw210xx, led_num, rgb_data, rgb_brightness);
	}

	return len;
}

static ssize_t
opdetect_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	ssize_t len = 0;
	int i = 0;
	uint8_t reg_val[3] = {0};

	aw210xx_open_short_detect(aw210xx, AW210XX_OPEN_DETECT, reg_val);
	for (i = 0; i < sizeof(reg_val); i++)
		len += snprintf(buf + len, PAGE_SIZE - len, "OSST%d:%#x\n", i, reg_val[i]);

	return len;
}

static ssize_t
stdetect_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	ssize_t len = 0;
	int i = 0;
	uint8_t reg_val[3] = {0};

	aw210xx_open_short_detect(aw210xx, AW210XX_SHORT_DETECT, reg_val);
	for (i = 0; i < sizeof(reg_val); i++)
		len += snprintf(buf + len, PAGE_SIZE - len, "OSST%d:%#x\n", i, reg_val[i]);
	return len;
}

static int aw210xx_factory_test(struct aw210xx *aw210xx, enum factory_mode mode)
{
	int i = 0;
	uint8_t reg_val[OPEN_TEST_REG_NUM_MAX] = {0};

	aw210xx_hw_enable(aw210xx, true);
  	aw210xx->test_result = 0;

	switch(aw210xx->chipid){
		case AW21009_CHIPID:
			aw210xx->effect = AW21009_GROUP_ALL_LEDS_ON;
			break;
		case AW21012_CHIPID:
			aw210xx->effect = AW21012_GROUP_ALL_LEDS_ON;
			break;
		case AW21018_CHIPID:
			aw210xx->effect = AW21018_GROUP_ALL_LEDS_ON;
			break;
		default:
			return -1;
	};
	aw210xx_cfg_update(aw210xx);
	msleep(250);
	switch(mode){
		case AW210xx_FACTORY_SHORT:
			aw210xx_open_short_detect(aw210xx, AW210XX_SHORT_DETECT, reg_val);
			for (i = 0; i < sizeof(reg_val); i++){
				if(reg_val[i])
					aw210xx->test_result = 1;
				dev_err(aw210xx->dev,"%s OSST%d = %x\n", __func__,i,reg_val[i]);
			}
			break;
		case AW210xx_FACTORY_OPEN:
			aw210xx_open_short_detect(aw210xx, AW210XX_OPEN_DETECT, reg_val);
			for (i = 0; i < sizeof(reg_val); i++){
				if(aw210xx->open_circuit_test_val[i] != reg_val[i])
					aw210xx->test_result = 1;
				dev_err(aw210xx->dev,"%s OSST%d = %x\n", __func__,i,reg_val[i]);
			}
			break;
		default:
			return -1;
	};
	aw210xx_hw_enable(aw210xx, false);
	dev_err(aw210xx->dev,"%s mode = %d result = %d\n", __func__,mode,aw210xx->test_result);
	return 0;
}

static void awinic_effect_mode(struct aw210xx *aw210xx, int effect)
{
	if (effect < (sizeof(aw21xxx_cfg_array) / sizeof(AW21XXX_CFG) )) {
		hrtimer_cancel(&aw210xx->timer);
		mutex_lock(&aw210xx->cfg_lock);
		aw210xx->cfg = effect;
		aw210xx->effect_data = aw21xxx_cfg_array[effect].p;
		aw210xx_led_init(aw210xx);
		aw21xxx_rgb_multi_breath(aw210xx);
		mutex_unlock(&aw210xx->cfg_lock);
		schedule_work(&aw210xx->cfg_work);
	} else {
		mutex_lock(&aw210xx->cfg_lock);
		aw210xx->cfg = effect;
		hrtimer_cancel(&aw210xx->timer);
		aw210xx_effect_close(aw210xx);
		mutex_unlock(&aw210xx->cfg_lock);
	}
}

static void awinic_effect_charge(struct aw210xx *aw210xx, int effect, int charging_mode)
{
	int effect_charing_offset;

	if(charging_mode == 1) {
		effect_charing_offset = TRANSSION_CHARGE1;
	} else if(charging_mode == 0) {
		effect_charing_offset = TRANSSION_ON_1;
	} else {
		dev_err(aw210xx->dev,"%s:charging_mode value is invaild:%d!\n", __func__, charging_mode);
		return;
	}

	effect = (effect - 0x21) + effect_charing_offset;
	awinic_effect_mode(aw210xx, effect);

	dev_err(aw210xx->dev,"%s:%d\n", __func__, effect);
	return;
}

static int aw210xx_store_tran_led_cmd(struct tc_led_device *dev, const char *cmd)
{
	struct aw210xx *aw210xx = dev->dev.driver_data;
	int num[10];

	memset(num, 0, 10);
	if (sscanf(cmd, "%x %x %x %x %x %x", &num[0], &num[1], &num[2], &num[3],&num[4],&num[5]) == 6) {
		pr_info("%s 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",__func__,num[0], num[1], num[2], num[3],num[4],num[5]);
		aw210xx->latest_cmd = num[1];
		aw210xx_hw_enable(aw210xx, true);
		switch(num[0]) {//type
			case 0x00:
				switch(num[1]) {//mode
					case 0x00://close
						mutex_lock(&aw210xx->cfg_lock);
						hrtimer_cancel(&aw210xx->timer);
						aw210xx_effect_close(aw210xx);
						mutex_unlock(&aw210xx->cfg_lock);
						break;
					case 0x01://open
					case 0x09://midtest
						awinic_effect_mode(aw210xx, TRANSSION_ALL_ON);
						break;
					case 0x02://Preview
						awinic_effect_mode(aw210xx, TRANSSION_CALL);
						break;
					case 0x03://incoming
						awinic_effect_mode(aw210xx, TRANSSION_CALL);
						break;
					case 0x04://notify
						awinic_effect_mode(aw210xx, TRANSSION_NOTICE);
						break;
					case 0x05://call preview
						awinic_effect_mode(aw210xx, TRANSSION_CALL_PREVIEW);
						break;
					case 0x20://charing preview
						awinic_effect_mode(aw210xx, TRANSSION_CHARGE_SETTING);
						break;
					case 0x21://charing
					case 0x22:
					case 0x23:
					case 0x24:
					case 0x25:
					case 0x26:
					case 0x27:
					case 0x28:
						awinic_effect_charge(aw210xx, num[1], num[2]);
						break;
					case 0x29://charge full
						awinic_effect_mode(aw210xx, TRANSSION_CHARGEFULL);
						break;
					case 0x51://3s time-lapse
						awinic_effect_mode(aw210xx, TRANSSION_PHOTO_3S);
						break;
					case 0x52:
						awinic_effect_mode(aw210xx, TRANSSION_PHOTO_5S);
						break;
					case 0x53:
						awinic_effect_mode(aw210xx, TRANSSION_PHOTO_10S);
						break;
					case 0x54://video record
						awinic_effect_mode(aw210xx, TRANSSION_RECORD);
						break;
					case 0x61://game
						awinic_effect_mode(aw210xx, TRANSSION_GAMESTART);
						break;
					case 0x62:
						awinic_effect_mode(aw210xx, TRANSSION_GAMEFIRSTBLOOD);
						break;
					case 0x63:
						awinic_effect_mode(aw210xx, TRANSSION_GAMESINGLEKILL);
						break;
					case 0x64:
						awinic_effect_mode(aw210xx, TRANSSION_GAMEDOUBLEKILL);
						break;
					case 0x81://Floax
						awinic_effect_mode(aw210xx, TRANSSION_AWAKE);
						break;
					case 0x82:
						awinic_effect_mode(aw210xx, TRANSSION_ANALYSYS);
						break;
					case 0x83:
						awinic_effect_mode(aw210xx, TRANSSION_ANSWER);
						break;
					case 0x84:
						awinic_effect_mode(aw210xx, TRANSSION_VOICE);
						break;
					default:
						aw210xx->latest_cmd = 0;
						dev_err(aw210xx->dev,"%s(no para) %d\n", __func__,__LINE__);
						break;
				}
				break;
			default:
				dev_err(aw210xx->dev,"%s(no para) %d\n", __func__,__LINE__);
				break;
		}
	}

	return 0;
}

static int aw210xx_show_tran_led_cmd(struct tc_led_device *dev, char *buf)
{
	struct aw210xx *aw210xx = dev->dev.driver_data;
	return sprintf(buf, "%d\n",  aw210xx->latest_cmd);
}

static int aw210xx_store_tran_led_check(struct tc_led_device *dev, const char *cmd)
{
	struct aw210xx *aw210xx = dev->dev.driver_data;
	int num[10];
	
	memset(num, 0, 10);
	if (sscanf(cmd, "%d", &num[0]) == 1) {
		dev_err(aw210xx->dev,"%s %d\n",__func__,num[0]);
		aw210xx->latest_cmd = num[0];
		switch(num[0]){//type
			case 1:
				aw210xx_factory_test(aw210xx, AW210xx_FACTORY_SHORT);
				break;
			case 2:
				aw210xx_factory_test(aw210xx, AW210xx_FACTORY_OPEN);
				break;
			default:
				dev_err(aw210xx->dev,"%s(no para) %d\n", __func__,__LINE__);
				break;
		}
	}
	return 0;
}

static int aw210xx_show_tran_led_check(struct tc_led_device *dev, char *buf)
{
	struct aw210xx *aw210xx = dev->dev.driver_data;

	return sprintf(buf, "%d\n",  aw210xx->test_result);
}

#if 0
static DEVICE_ATTR(tran_led_cmd, 0664,
			show_tran_led_cmd, store_tran_led_cmd);
static DEVICE_ATTR(tran_led_check, 0664,
			show_tran_led_check, store_tran_led_check);]
#endif

static DEVICE_ATTR_RW(reg);
static DEVICE_ATTR_RW(hwen);
static DEVICE_ATTR_RW(effect);
static DEVICE_ATTR_RW(cfg);
static DEVICE_ATTR_WO(rgbcolor);
static DEVICE_ATTR_WO(singleled);
static DEVICE_ATTR_RO(opdetect);
static DEVICE_ATTR_RO(stdetect);

static struct attribute *aw210xx_attributes[] = {
	&dev_attr_reg.attr,
	&dev_attr_hwen.attr,
	&dev_attr_effect.attr,
	&dev_attr_cfg.attr,
	&dev_attr_rgbcolor.attr,
	&dev_attr_singleled.attr,
	&dev_attr_opdetect.attr,
	&dev_attr_stdetect.attr,
	NULL,
};

static struct attribute_group aw210xx_attribute_group = {
	.attrs = aw210xx_attributes
};
/******************************************************
 *
 * led class dev
 ******************************************************/

static int aw210xx_parse_led_cdev(struct aw210xx *aw210xx, struct device_node *np)
{
	int ret = -1;
	struct device_node *temp;

	for_each_child_of_node(np, temp) {
		ret = of_property_read_string(temp, "aw210xx,name", &aw210xx->cdev.name);
		if (ret < 0) {
			dev_err(aw210xx->dev, "Failure reading led name, ret = %d\n", ret);
			goto free_pdata;
		}
		ret = of_property_read_u32(temp, "aw210xx,imax", &aw210xx->imax);
		if (ret < 0) {
			dev_err(aw210xx->dev, "Failure reading imax, ret = %d\n", ret);
			goto free_pdata;
		}
		ret = of_property_read_u32(temp, "aw210xx,brightness", &aw210xx->cdev.brightness);
		if (ret < 0) {
			dev_err(aw210xx->dev, "Failure reading brightness, ret = %d\n", ret);
			goto free_pdata;
		}
		ret = of_property_read_u32(temp, "aw210xx,max_brightness",
				&aw210xx->cdev.max_brightness);
		if (ret < 0) {
			dev_err(aw210xx->dev, "Failure reading max brightness, ret = %d\n", ret);
			goto free_pdata;
		}

		ret = of_property_read_u32_array(temp, "open_circuit_test_val",
			(u32 *)aw210xx->open_circuit_test_val,
			OPEN_TEST_REG_NUM_MAX);
		if (ret < 0) {
			dev_err(aw210xx->dev,
				"%s: parse open_circuit_test_val err, ret = %d\n", __func__, ret);
		}
	}
	hrtimer_init(&aw210xx->timer,CLOCK_MONOTONIC,HRTIMER_MODE_REL);
	aw210xx->ms = AW210XX_TIME_REFRESH;
	aw210xx->timer.function = aw210xx_timer_func;
	aw210xx->cdev.name = "aw210xx_led";
	INIT_WORK(&aw210xx->brightness_work, aw210xx_brightness_work);
	INIT_WORK(&aw210xx->cfg_work,aw210xx_rgb_multi_breath_work);
	aw210xx->cdev.brightness_set = aw210xx_set_brightness;

	ret = led_classdev_register(aw210xx->dev, &aw210xx->cdev);
	if (ret) {
		AW_ERR("unable to register led ret=%d\n", ret);
		goto free_pdata;
	}

	ret = sysfs_create_group(&aw210xx->cdev.dev->kobj, &aw210xx_attribute_group);
	if (ret) {
		AW_ERR("led sysfs ret: %d\n", ret);
		goto free_class;
	}
	return 0;

free_class:
	led_classdev_unregister(&aw210xx->cdev);
free_pdata:
	return ret;
}

/*****************************************************
 *
 * check chip id and version
 *
 *****************************************************/
static int aw210xx_read_chipid(struct aw210xx *aw210xx)
{
	int ret = -1;
	unsigned char cnt = 0;
	unsigned char chipid = 0;

	while (cnt < AW_READ_CHIPID_RETRIES) {
		ret = aw210xx_i2c_read(aw210xx, AW210XX_REG_RESET, &chipid);
		if (ret < 0) {
			AW_ERR("failed to read chipid: %d\n", ret);
		} else {
			aw210xx->chipid = chipid;
			switch (aw210xx->chipid) {
			case AW21018_CHIPID:
				AW_LOG("AW21018, read chipid = 0x%02x!!\n", chipid);
				return 0;
			case AW21012_CHIPID:
				AW_LOG("AW21012, read chipid = 0x%02x!!\n", chipid);
				return 0;
			case AW21009_CHIPID:
				AW_LOG("AW21009, read chipid = 0x%02x!!\n", chipid);
				return 0;
			default:
				AW_LOG("chip is unsupported device id = %x\n", chipid);
				break;
			}
		}
		cnt++;
		usleep_range(1000, 2000);
	}

	return -EINVAL;
}

/*****************************************************
 *
 * device tree
 *
 *****************************************************/
static int aw210xx_parse_dt(struct device *dev, struct aw210xx *aw210xx, struct device_node *np)
{
	int ret = -EINVAL;

	aw210xx->enable_gpio = of_get_named_gpio(np, "enable-gpio", 0);

	ret = of_property_read_u32(np, "osc_clk", &aw210xx->osc_clk);
	if (ret < 0) {
		AW_ERR("no osc_clk provided, osc clk unsupported\n");
		return ret;
	}

	ret = of_property_read_u32(np, "br_res", &aw210xx->br_res);
	if (ret < 0) {
		AW_ERR("brightness resolution unsupported\n");
		return ret;
	}

	ret = of_property_read_u32(np, "global_current", &aw210xx->glo_current);
	if (ret < 0) {
		AW_ERR("global current resolution unsupported\n");
		return ret;
	}

	return 0;
}

static const struct tc_led_ops leds_aw210xx_ops = {
	.store_tran_led_cmd = aw210xx_store_tran_led_cmd,
	.show_tran_led_cmd = aw210xx_show_tran_led_cmd,
	.store_tran_led_check = aw210xx_store_tran_led_check,
	.show_tran_led_check = aw210xx_show_tran_led_check,
};

/******************************************************
 *
 * i2c driver
 *
 ******************************************************/
static int aw210xx_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct aw210xx *aw210xx;
	struct device_node *np = i2c->dev.of_node;
	int ret;

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C)) {
		pr_err("check_functionality failed\n");
		return -EIO;
	}

	aw210xx = devm_kzalloc(&i2c->dev, sizeof(struct aw210xx), GFP_KERNEL);
	if (aw210xx == NULL)
		return -ENOMEM;

	aw210xx->dev = &i2c->dev;
	aw210xx->i2c = i2c;
	i2c_set_clientdata(i2c, aw210xx);
	mutex_init(&aw210xx->cfg_lock);
	/* aw210xx parse device tree */
	if (np) {
		ret = aw210xx_parse_dt(&i2c->dev, aw210xx, np);
		if (ret) {
			pr_err("failed to parse device tree node\n");
			goto err_parse_dt;
		}
	}

	if (gpio_is_valid(aw210xx->enable_gpio)) {
		ret = devm_gpio_request_one(&i2c->dev, aw210xx->enable_gpio,
				GPIOF_OUT_INIT_LOW, "aw210xx_en");
		if (ret) {
			pr_err("enable gpio request failed\n");
			goto err_gpio_request;
		}
	}

	/* hardware enable */
	aw210xx_hw_enable(aw210xx, true);

	/* aw210xx identify */
	ret = aw210xx_read_chipid(aw210xx);
	if (ret < 0) {
		pr_err("aw210xx_read_chipid failed ret=%d\n", ret);
		goto err_id;
	}

	dev_set_drvdata(&i2c->dev, aw210xx);
	aw210xx_parse_led_cdev(aw210xx, np);
	if (ret < 0) {
		pr_err("error creating led class dev\n");
		goto err_sysfs;
	}

#if 0
	kobj = kobject_create_and_add("led", NULL);
	if (!kobj) {
		dev_err(&i2c->dev, "%s:sysfs_create_group fail",__func__);
		goto err_sysfs;
	}

	ret = sysfs_create_link(kobj,&i2c->dev.kobj,"led");
	if(ret<0){
		dev_err(&i2c->dev, "%s : sysfs_create_link failed\n", __func__);
		goto err_sysfs;
	}
#endif

	aw210xx->led_dev = tc_led_device_register("aw210xx", aw210xx->dev, aw210xx, &leds_aw210xx_ops, NULL);

	aw210xx_led_init(aw210xx);

	AW_LOG("probe completed!\n");

	return 0;

err_sysfs:
err_id:
err_gpio_request:
err_parse_dt:
	devm_kfree(&i2c->dev, aw210xx);
	aw210xx = NULL;
	return ret;
}

static void aw210xx_i2c_remove(struct i2c_client *i2c)
{
	struct aw210xx *aw210xx = i2c_get_clientdata(i2c);

	sysfs_remove_group(&aw210xx->cdev.dev->kobj, &aw210xx_attribute_group);
	led_classdev_unregister(&aw210xx->cdev);
	devm_kfree(&i2c->dev, aw210xx);
	aw210xx = NULL;
}

static const struct i2c_device_id aw210xx_i2c_id[] = {
	{AW210XX_I2C_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, aw210xx_i2c_id);

static const struct of_device_id aw210xx_dt_match[] = {
	{.compatible = "awinic,aw210xx_led"},
	{}
};

static struct i2c_driver aw210xx_i2c_driver = {
	.driver = {
		.name = AW210XX_I2C_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(aw210xx_dt_match),
		},
	.probe = aw210xx_i2c_probe,
	.remove = aw210xx_i2c_remove,
	.id_table = aw210xx_i2c_id,
};

static int __init aw210xx_i2c_init(void)
{
	int ret = 0;

	pr_info("enter, aw210xx driver version %s\n", AW210XX_DRIVER_VERSION);

	ret = i2c_add_driver(&aw210xx_i2c_driver);
	if (ret) {
		pr_err("failed to register aw210xx driver!\n");
		return ret;
	}

	return 0;
}
module_init(aw210xx_i2c_init);

static void __exit aw210xx_i2c_exit(void)
{
	i2c_del_driver(&aw210xx_i2c_driver);
}
module_exit(aw210xx_i2c_exit);

MODULE_DESCRIPTION("AW210XX LED Driver");
MODULE_LICENSE("GPL v2");
