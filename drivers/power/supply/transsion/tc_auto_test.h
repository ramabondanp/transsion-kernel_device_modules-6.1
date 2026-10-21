// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#ifndef __LINUX_TC_AUTO_TEST_H__
#define __LINUX_TC_AUTO_TEST_H__


#define TRAN_TEST_SYSFS_FIELD_RW(_name, _name_set, _name_get, _prop)	\
{									 \
	.attr   = __ATTR(_name, 0644, tran_sysfs_show, tran_sysfs_store),\
	.prop	= _prop,	\
	.set	= _name_set,						\
	.get	= _name_get,						\
}

#define TRAN_TEST_SYSFS_FIELD_RO(_name, _prop)	\
{		\
	.attr   = __ATTR(_name, 0444, tran_sysfs_show, tran_sysfs_store),\
	.prop   = _prop,				  \
	.get	= _name,						\
}

#define TRAN_TEST_SYSFS_FIELD_WO(_name, _prop)	\
{								   \
	.attr	= __ATTR(_name, 0200, tran_sysfs_show, tran_sysfs_store),\
	.prop	= _prop,	\
	.set	= _name,						\
}

#define TRAN_TEST_SYSFS_INFO_FIELD_RW(_name, _prop)	\
{									 \
	.attr   = __ATTR(_name, 0644, tran_sysfs_show, tran_sysfs_store),\
	.prop	= _prop,	\
	.set	= info_set,	\
	.get	= info_get,	\
}

#define TC_GET_ERR   -11    /* get tran_property err*/

enum tran_property {
	TRAN_TEST_PROP_CTRL = 0,

	/* get prop */
	TRAN_TEST_PROP_VBUS = 0,
	TRAN_TEST_PROP_IBUS,
	TRAN_TEST_PROP_VBAT_CP,
	TRAN_TEST_PROP_VBAT_GAUGE,
	TRAN_TEST_PROP_IBAT,
	TRAN_TEST_PROP_VTA_SETTING,
	TRAN_TEST_PROP_ITA_SETTING,
	TRAN_TEST_PROP_VTA_MEASURE,
	TRAN_TEST_PROP_ITA_MEASURE,
	TRAN_TEST_PROP_BAT_TEMP,
	TRAN_TEST_PROP_PCB_TEMP,
	TRAN_TEST_PROP_SW_TEMP,
	TRAN_TEST_PROP_CP_TEMP,
	TRAN_TEST_PROP_USB_TEMP,
	TRAN_TEST_PROP_TA_TEMP,
	TRAN_TEST_PROP_MACHINE_TEMP,
	TRAN_TEST_PROP_AMBIENT_TEMP,
	TRAN_TEST_PROP_AMBIENT_TEMP_UPDATE,
	TRAN_TEST_PROP_AMBIENT_TEMP_UPDATE_TIME,
	TRAN_TEST_PROP_PA_TEMP,
	TRAN_TEST_PROP_CHG_TYPE,
	TRAN_TEST_PROP_CHG_STATUS,
	TRAN_TEST_PROP_SOC,
	TRAN_TEST_PROP_R_CABLE,
	TRAN_TEST_PROP_GAME_MODE,
	TRAN_TEST_PROP_CV,
	TRAN_TEST_PROP_ITERM,
	TRAN_TEST_PROP_MIVR,
	TRAN_TEST_PROP_ILMT,
	TRAN_TEST_PROP_CC1,
	TRAN_TEST_PROP_CC2,
	TRAN_TEST_PROP_FUNC_IMPORT,
	TRAN_TEST_PROP_ALG_RUNNING_VOL,
	TRAN_TEST_PROP_CAPACITY,
	TRAN_TEST_PROP_REAL_SOC,
	TRAN_TEST_PROP_TA_POWER,
	TRAN_TEST_PROP_CHGSPEED,
	TRAN_TEST_PROP_FG_VSOC,
	TRAN_TEST_PROP_FG_CSOC,
	TRAN_TEST_PROP_FG_DO_C,
	TRAN_TEST_PROP_FG_DO_V,
	TRAN_TEST_PROP_FG_UI_SOC,
	TRAN_TEST_PROP_FG_AGING,
	TRAN_TEST_PROP_FG_C_CAR,
	TRAN_TEST_PROP_FG_BAT_CYCLE,
	TRAN_TEST_PROP_FG_QMAX,
	TRAN_TEST_PROP_FG_RM_CAPACITY,
	TRAN_TEST_PROP_BATTERY_ID,
	TRAN_TEST_PROP_FG_OLD_CAR,
	
	/* set prop */
	TRAN_TEST_PROP_FIX_CHG_TYPE,
	TRAN_TEST_PROP_STOP_CHG,
	TRAN_TEST_PROP_QUICK_CHG_BACK,
	TRAN_TEST_PROP_IRQ_TEST,


	 /*third battery prop*/

	TRAN_TEST_PROP_DUAL_BATT_VOL_DIFF,
	TRAN_TEST_PROP_DUAL_BATT_ONLINE_STATUS,
	TRAN_TEST_PROP_MASTER_VBAT,
	TRAN_TEST_PROP_MASTER_IBAT,
	TRAN_TEST_PROP_MASTER_TEMP,
	TRAN_TEST_PROP_MASTER_SOC,
	TRAN_TEST_PROP_MASTER_FCC,
	TRAN_TEST_PROP_MASTER_RC,
	TRAN_TEST_PROP_MASTER_BATT_EN,

	TRAN_TEST_PROP_SLAVE_VBAT,
	TRAN_TEST_PROP_SLAVE_IBAT,
	TRAN_TEST_PROP_SLAVE_TEMP,
	TRAN_TEST_PROP_SLAVE_SOC,
	TRAN_TEST_PROP_SLAVE_FCC,
	TRAN_TEST_PROP_SLAVE_RC,
	TRAN_TEST_PROP_SLAVE_BATT_EN,


	/* auto test */
	TRAN_TEST_PROP_CHG_RUN_PATH,

	TRAN_TEST_PROP_MAX,
};

enum tran_irq_test {
	TRAN_IRQ_NOT_TEST = 0,
	TRAN_IRQ_NOTIFY_VBUS_OVP,
	TRAN_IRQ_NOTIFY_IBUSOCP,
	TRAN_IRQ_NOTIFY_IBUSUCP_FALL,
	TRAN_IRQ_NOTIFY_BAT_OVP,
	TRAN_IRQ_NOTIFY_IBATOCP,
	TRAN_IRQ_NOTIFY_VBATOVP_ALARM,
	TRAN_IRQ_NOTIFY_VBUSOVP_ALARM,
	TRAN_IRQ_NOTIFY_VOUTOVP,
	TRAN_IRQ_NOTIFY_VDROVP,
	TRAN_IRQ_START_TEST = 88,
	TRAN_IRQ_END_TEST = 89,
	TRAN_IRQ_MAX_TEST,
};

enum tran_rfc_run_path_id {
	RFC_DETECT_TA,
	RFC_AUTH_TA,
	RFC_ALGO,
	/* RFC_ALGO_INIT, */
	/* RFC_ALGO_MEASURE_R, */
	/* RFC_ALGO_CC, */
	/* RFC_ALGO_CV, */
	/* RFC_ALGO_STOP, */
};

static const char * const TRAN_RFC_RUN_PATH[] = {
	[RFC_DETECT_TA]      = "rfc_detected_work",
	[RFC_AUTH_TA]        = "pe50_hal_authenticate_ta",
	[RFC_ALGO]           = "pe50_algo_threadfn",
};

enum tran_func_id {
	PID_FUNC = BIT(0),
	SYS_POWER_FUNC = BIT(1),
	BATT_TEMP_FORECAST_FUNC = BIT(2),
	AMBIENT_DET_FUNC = BIT(3),
	TA_OFF_FUNC = BIT(4),
	LONG_BATT_LIFE_FUNC = BIT(5),
	NONLINEAR_UI_FUNC = BIT(6),
	WATER_DET_FUNC = BIT(7),
	THREE_GEAR_CHARGING_FUNC = BIT(8),
};

struct tran_func_info {
	enum tran_func_id id;
	char string[50];
};

static struct tran_func_info tran_func_array[] = {
	{PID_FUNC,			"pid func = "},
	{SYS_POWER_FUNC,		"sys power func = "},
	{BATT_TEMP_FORECAST_FUNC,	"batt temp forecast func = "},
	{AMBIENT_DET_FUNC,		"ambient det func = "},
	{TA_OFF_FUNC,			"ta off func = "},
	{LONG_BATT_LIFE_FUNC,		"long batt life func = "},
	{NONLINEAR_UI_FUNC,		"nonlinear ui func = "},
	{WATER_DET_FUNC,		"water det func = "},
};

struct tran_auto_test {
	struct device *dev;
	struct power_supply *chg_psy;
	struct power_supply *tran_psy;
	struct power_supply_desc tran_psy_d;
	struct charger_device *dv2_chg1;
	struct charger_device *chg_dev;
	struct tran_device *fg_a_dev;
	struct tran_device *fg_b_dev;

	struct tran_test_sysfs_field_info *attr;
	struct mutex ops_lock;

	int mtk_fg_old_car;
	int tran_stop_charging;
	int tran_fix_chg_type;
	int tran_quick_chg_back;
};

struct tran_test_sysfs_field_info {
	struct device_attribute attr;
	enum tran_property prop;
	int (*set)(struct tran_auto_test *tat,
		struct tran_test_sysfs_field_info *attr, int val);
	int (*get)(struct tran_auto_test *tat,
		struct tran_test_sysfs_field_info *attr, int *val);
};

extern int tran_auto_test_set_property(enum tran_property bp, int val);

#endif
