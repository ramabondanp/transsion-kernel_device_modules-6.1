// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#ifndef __TRAN_SC868X_H__
#define __TRAN_SC868X_H__

#include <linux/i2c.h>
#include <linux/regmap.h>

#define SC858X_DRV_VERSION              "1.1.0_G"

#define SC858X_DEVICE_ID                0x81
#define SC8566_DEVICE_ID                0x2D

#define SC858X_REG17                    0x17
#define SC858X_REGMAX                   0x7F

#define SC858X_REG7C                    0x7C
#define SC858X_PRIVATE_CODE             0x01

#define REG0E_FORWARD_4_1_CHARGER_MODE      0
#define REG0E_FORWARD_2_1_CHARGER_MODE      1 
#define REG0E_FORWARD_1_1_CHARGER_MODE      2
#define REG0E_FORWARD_1_1_CHARGER_MODE1     3
#define REG0E_REVERSE_1_4_CONVERTER_MODE    4
#define REG0E_REVERSE_1_2_CONVERTER_MODE    5
#define REG0E_REVERSE_1_1_CONVERTER_MODE    6 
#define REG0E_REVERSE_1_1_CONVERTER_MODE1   7

#define REG0D_SS_TIMEOUT_DISABLE            0
#define REG0D_SS_TIMEOUT_320MS              3
#define REG0D_SS_TIMEOUT_1280MS             4
#define REG0D_SS_TIMEOUT_5120MS             5
#define REG0D_SS_TIMEOUT_20480MS            6
#define REG0D_SS_TIMEOUT_81920MS            7

#define IBUS_UCP_256MS	3
#define IBUS_UCP_8US	1

enum {
    SC858X_STANDALONG = 0,
    SC858X_MASTER,
    SC858X_SLAVE,
};

enum sc858x_reg_range {
	SC858X_VBAT_OVP,
	SC858X_IBAT_OCP,
	SC858X_VBUS_OVP,
	SC858X_IBUS_OCP,
};

enum {
	ADC_IBUS,
	ADC_VBUS,
	ADC_VUSB,
	ADC_VWPC,
	ADC_VOUT,
	ADC_VBAT,
	ADC_IBAT,
	RESERVED,
	ADC_TDIE,
	ADC_MAX_NUM,
}SC_858X_ADC_CH;

struct reg_range {
	u32 min;
	u32 max;
	u32 step;
	u32 offset;
	const u32 *table;
	u16 num_table;
	bool round_up;
};

#define SC858X_CHG_RANGE(_min, _max, _step, _offset, _ru) \
{ \
	.min = _min, \
	.max = _max, \
	.step = _step, \
	.offset = _offset, \
	.round_up = _ru, \
}

#define SC858X_CHG_RANGE_T(_table, _ru) \
    { .table = _table, .num_table = ARRAY_SIZE(_table), .round_up = _ru, }

enum sc858x_fields {
	DEVICE_VER,
	VBAT_OVP_DIS, VBAT_OVP,
	IBAT_OCP_DIS, IBAT_OCP,
	VUSB_OVP,
	VWPC_OVP,
	VBUS_OVP, VOUT_OVP,
	IBUS_OCP_DIS, IBUS_OCP,
	IBUS_UCP_DIS, IBUS_UCP_FALL_DG_SET,
	PMID2OUT_OVP_DIS, PMID2OUT_OVP,
	PMID2OUT_UVP_DIS, PMID2OUT_UVP,
	CP_SWITCHING_STAT,VBUS_ERRORHI_STAT,VBUS_ERRORLO_STAT, PIN_DIAG_FAIL,
	CP_EN, QB_EN, ACDRV_MANUAL_EN, WPCGATE_EN, OVPGATE_EN, VBUS_PD_EN, VWPC_PD_EN, VUSB_PD_EN,
	FSW_SET, FREQ_DITHER, ACDRV_HI_EN,
	VBUS_INRANGE_DET_DIS, SS_TIMEOUT, WD_TIMEOUT,
	VBAT_OVP_DG_SET, SET_IBAT_SNS_RES, REG_RST, MODE,
	TSHUT_DIS, VWPC_OVP_DIS, VUSB_OVP_DIS, VBUS_OVP_DIS, VOUT_OVP_DIS,
	VUSB_INSERT_STAT,
	ADC_EN, ADC_RATE,
	DEVICE_ID,
	SECRET_KEY,
	F_MAX_FIELDS,
};

enum sc858x_notify {
	SC858X_NOTIFY_VBUSSTAT = 0,
	SC858X_NOTIFY_VWPCSTAT,
	SC858X_NOTIFY_OTHER,
	SC858X_NOTIFY_IBUSOCP,
	SC858X_NOTIFY_VBUSOVP,
	SC858X_NOTIFY_IBATOCP,
	SC858X_NOTIFY_VBATOVP,
	SC858X_NOTIFY_VOUTOVP,
};

enum sc858x_error_stata {
	ERROR_VBUS_HIGH = 0,
	ERROR_VBUS_LOW,
	ERROR_VBUS_OVP,
	ERROR_IBUS_OCP,
	ERROR_VBAT_OVP,
	ERROR_IBAT_OCP,
};

struct flag_bit {
	int notify;
	int mask;
	char *name;
};

struct intr_flag {
	int reg;
	int len;
	struct flag_bit bit[8];
};

struct sc858x_cfg_e {
	int vbat_ovp_dis;
	int vbat_ovp;
	int ibat_ocp_dis;
	int ibat_ocp;
	int vusb_ovp_dis;
	int vusb_ovp;
	int vwpc_ovp_dis;
	int vwpc_ovp;
	int vbus_ovp_dis;
	int vbus_ovp;
	int vout_ovp_dis;
	int vout_ovp;
	int ibus_ocp_dis;
	int ibus_ocp;
	int ibus_ucp_fall_dis;
	int ibus_ucp_fall;
	int pmid2out_uvp_dis;
	int pmid2out_uvp;
	int pmid2out_ovp_dis;
	int pmid2out_ovp;
	int fsw_set;
	int ss_timeout;
	int wd_timeout;
	int ibat_sns_r;
	int mode;
	int tshut_dis;
};

struct sc858x_chip {
	struct device *dev;
	struct i2c_client *client;
	struct regmap *regmap;
	struct regmap_field *rmap_fields[F_MAX_FIELDS];

	struct sc858x_cfg_e cfg;
	struct charger_properties chg_prop;
	int irq_gpio;
	struct gpio_desc *lpm_gpio;
	int irq;

	int mode;
	int work_mode;

	bool charge_enabled;
	int usb_present;
	int vbus_volt;
	int ibus_curr;
	int vbat_volt;
	int ibat_curr;
	int die_temp;
	struct charger_device *chg_dev;
	const char *chg_dev_name;

	struct power_supply_desc psy_desc;
	struct power_supply_config psy_cfg;
	struct power_supply *psy;

#if IS_ENABLED(CONFIG_WIRELESS_MANAGER)
	struct wireless_charger *wl_chg;
#endif
};

#endif
