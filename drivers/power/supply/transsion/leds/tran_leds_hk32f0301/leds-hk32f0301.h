// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#ifndef __HK32F0301_H__
#define __HK32F0301_H__

/*******************************************************************************
 *
 * struct
 *
 ******************************************************************************/
struct hk32fxxxx {
	struct i2c_client *i2c;
	struct device *dev;
	struct led_classdev cdev;
	struct delayed_work wait_fw_update_work;
	struct mutex i2c_lock;
	struct mutex fw_lock;
	struct hrtimer timer;
	uint8_t chipid;
	int enable_gpio;
	unsigned int imax;
	unsigned int max_brightness;
	unsigned int latest_cmd;
	struct tc_led_device *led_dev;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pinctrl_i2c_clk_gpio;
	struct pinctrl_state *pinctrl_i2c_sda_gpio;

	u8 *fwver_data;
	u32 fwver_length;
	u32 fwver_crc;
	u8 fwver_version;
};
#endif
