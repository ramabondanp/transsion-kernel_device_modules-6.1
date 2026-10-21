// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Transsion Inc.
 */

#ifndef __LINUX_TC_COMMON_CLASS_H__
#define __LINUX_TC_COMMON_CLASS_H__

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mutex.h>

#define ERROR_NO_IMPLEMENT		1
#define IS_HVDCP30_A 0
#define IS_HVDCP30_B 1
#define NOT_HVDCP30 -1		
#define SOC_ZERO_DECIMAL 1
#define SOC_TWO_DECIMAL  100

union com_propval {
        int intval;
        const char *strval;
        int64_t int64val;
	void *ptr;
};

// typedef enum tran_chg_misc_set_chg_status {
//         SET_CHG_SET_INPUT_CUR_LMT,
//         SET_CHG_SET_BAT_CUR_LMT,
//         SET_CHG_DISCHARGER,
//         SET_CHG_HIZ
// } TCM_CHG_STATUS;

typedef enum tran_chg_status_controller {
	TRAN_WATER_DETECT = 1 << 0,
	TRAN_AI_CHG = 1 << 1,
	POWER_PATH_CHG = 1 << 2,
	TRAN_ADAPTER_CONTROL = 1 << 3,
	CTRL_MAX,
} TCM_CHG_CONTROLLER_ID;

typedef struct tran_chg_misc_setting {
	TCM_CHG_CONTROLLER_ID controller;
	// TCM_CHG_STATUS chg_status;
	// int batt_status;
	// int input_current_limit;
	// int charger_current_limit;
	bool enable;
} tcm_setting_t;

enum tran_common_prop {
	TRAN_PROP_WATER_DETECT,
	TRAN_PROP_WD_SELECT_SBU_1_2,
	TRAN_PROP_WD_USER_CMD,
	TRAN_PROP_IGNORE_PLUG_IN,
	TRAN_PROP_USB_PRE_PLUG_IN,
	TRAN_PROP_USB_PRE_PLUG_OUT,
	TRAN_PROP_USB_PLUG_IN,
	TRAN_PROP_USB_PLUG_OUT,
	TRAN_PROP_GET_CHARGER_TYPE,
	TRAN_PROP_TC_CHG_TYPE,
	TRAN_PROP_GET_CHARGER_POWER,
	TRAN_PROP_GET_CHARGER_STATUS,
	TRAN_PROP_GET_CHARGER_CURRENT,
	TRAN_PROP_GET_CHARGER_MIN_CURRENT,
	TRAN_PROP_SET_CHARGER_STATUS,
	TRAN_PROP_RESET_CHARGER_STATUS,
	TRAN_PROP_ADAPTER_SWITCH_STATUS,
	TRAN_PROP_USB_CTRL_HVDCP20,
	TRAN_PROP_USB_CTRL_TC30,
	TRAN_PROP_USB_CTRL_HVDCP30,
	TRAN_PROP_USB_CTRL_RFC,
	TRAN_PROP_USB_CTRL_TA_OFF,
	TRAN_PROP_USB_CTRL_RESET,
	TRAN_PROP_USB_CTRL_TC30_TA,
	TRAN_PROP_IS_TC30_TA,
	TRAN_PROP_IS_RFC_TA,
	TRAN_PROP_SET_BATT_RAW_SOC,
	TRAN_PROP_TRAN_CUSTOM_DISCHG,
	TRAN_PROP_SET_BATT_UI_SOC,
	TRAN_PROP_GET_BATT_UI_SOC,
	TRAN_PROP_GET_BATTERY_TEMPERATURE,
	TRAN_PROP_GET_LOW_CV_STATUS,
	TRAN_PROP_AMBIENT_TEMP,
	TRAN_PROP_AMBIENT_TEMP_UPDATE,
	TRAN_PROP_AMBIENT_TEMP_UPDATE_TIME,
	TRAN_PROP_SET_PID_TARGET_TEMP,
	TRAN_PROP_SET_FINAL_CHARGER_CURRENT,
	TRAN_PROP_GET_PID_PARAM,
	TRAN_PROP_FORECAST_BATT_TEMP,
	TRAN_PROP_FORECAST_MACHINE_TEMP,
	TRAN_PROP_ACCURACY_UISOC,
	TRAN_PROP_FORCED_UPDATE_UISOC,
	TRAN_PROP_MTK_GAUGE_CAR_RESET,
	TRAN_PROP_WAKE_UP_CHARGER,
	TRAN_PROP_CHARGING_ANIMATION,
	TRAN_PROP_CHARGER_CURRENT_LIMIT,
	TRAN_PROP_LOG_LEVEL,
	TRAN_PROP_CHG_FULL_STATE,
	TRAN_PROP_BATTERY_ID,
	TRAN_PROP_MONKEY_FLAG,
	TRAN_PROP_FG_HW_CAR,
	TRAN_PROP_FG_HW_RC,
	TRAN_PROP_WATER_CTRL_CHG,
	TRAN_PROP_PORT_BURN_VOTE,
	TRAN_PROP_RUNNING_VOL,
	TRAN_PROP_SET_POLLING_INTERVAL,
	TRAN_PROP_CHG1_CV,
	TRAN_PROP_CHG2_CV,
	TRAN_PROP_CHG_CV,
	TRAN_PROP_CHG_EOC,
	TRAN_PROP_CHG_MASTER_CC,
	TRAN_PROP_CHG_MASTER_CV,
	TRAN_PROP_CHG_MASTER_EOC,
	TRAN_PROP_CHG_SLAVE_CC,
	TRAN_PROP_CHG_SLAVE_CV,
	TRAN_PROP_CHG_SLAVE_EOC,
	TRAN_PROP_IS_FFC_CHR,
	TRAN_PROP_FFC_ALG_ID,
	TRAN_PROP_MAX_CHG_CUR_LMT,

	/* tc gauge for battery */
	TRAN_PROP_PSY_STATUS,
	TRAN_PROP_PSY_HEALTH,
	TRAN_PROP_PSY_PRESENT,
	TRAN_PROP_PSY_TECHNOLOGY,
	TRAN_PROP_PSY_CYCLE_COUNT,
	TRAN_PROP_PSY_CAPACITY,
	TRAN_PROP_PSY_CURRENT_NOW,
	TRAN_PROP_PSY_CURRENT_AVG,
	TRAN_PROP_PSY_CHARGE_FULL,
	TRAN_PROP_PSY_CHARGE_COUNTER,
	TRAN_PROP_PSY_VOLTAGE_NOW,
	TRAN_PROP_PSY_TEMP,
	TRAN_PROP_PSY_CAPACITY_LEVEL,
	TRAN_PROP_PSY_TIME_TO_FULL_NOW,
	TRAN_PROP_PSY_CHARGE_FULL_DESIGN,
	TRAN_PROP_PSY_CONSTANT_CHARGE_VOLTAGE,

	/* battery */
	TRAN_PROP_PRESENT,
	TRAN_PROP_GET_Q_MAX,
	TRAN_PROP_CAPACITY,
	TRAN_PROP_VOLTAGE_NOW,
	TRAN_PROP_TEMP,
	TRAN_PROP_CURRENT_NOW,
	TRAN_PROP_CHARGE_FULL_DESIGN,
	TRAN_PROP_BATTERY_CYCLE,
	TRAN_PROP_BATTERY_RAW_CYCLE,
	TRAN_PROP_BATT_CTRL, 
	TRAN_PROP_BATT_TEMP,		/* Battery Temperature */
	TRAN_PROP_BATT_VOLT,		/* Battery Voltage */
	TRAN_PROP_BATT_AI,		/* Average Current */ 
	TRAN_PROP_BATT_STATUS,	/* BatteryStatus */ 
	TRAN_PROP_BATT_TTE,		/* Time to Empty */
	TRAN_PROP_BATT_TTF,		/* Time to Full */
	TRAN_PROP_BATT_FCC,		/* Full Charge Capacity */
	TRAN_PROP_BATT_RM,		/* Remaining Capacity */
	TRAN_PROP_BATT_CC,		/* Cycle Count */
	TRAN_PROP_BATT_SOC,		/* Relative State of Charge */
	TRAN_PROP_BATT_SOH,		/* State of Health */
	TRAN_PROP_BATT_DC,		/* Design Capacity */
	TRAN_PROP_BATT_ALT_MAC,	/* AltManufactureAccess*/
	TRAN_PROP_BATT_MAC_CHKSUM,	/* MACChecksum */
	TRAN_PROP_BATT_EN,
	TRAN_PROP_MASTER_BATT_VOLT,
	TRAN_PROP_MASTER_BATT_NOW_CURR,
	TRAN_PROP_MASTER_BATT_TEMP,
	TRAN_PROP_MASTER_BATT_EN,
	TRAN_PROP_SLAVE_BATT_VOLT,
	TRAN_PROP_SLAVE_BATT_NOW_CURR,
	TRAN_PROP_SLAVE_BATT_TEMP,
	TRAN_PROP_SLAVE_BATT_EN,
	TRAN_PROP_BATT_CHG_DONE,
	TRAN_PROP_BATT_RUN_MODE,
	TRAN_PROP_BATT_WORKING_STATUS,
	TRAN_PROP_BATT_NOW_CURR,
	TRAN_PROP_BATT_FW_STATUS,
	TRAN_PROP_BATT_TAPER_CUR,
	TRAN_PROP_BATT_INTERNAL_RESISTANCE_LEARN,
	TRAN_PROP_BATT_GI_CHECK_DONE,
	TRAN_PROP_BATT_GI_UPDATE_FAIL,
	TRAN_PROP_BATT_UPDATE_GI,
	TRAN_PROP_BATT_RES_30_PERCENT,
	TRAN_PROP_BATT_RES_40_PERCENT,
	TRAN_PROP_BATT_RES_50_PERCENT,
	TRAN_PROP_BATT_RES_60_PERCENT,
	TRAN_PROP_BATT_RES_70_PERCENT,
	TRAN_PROP_BATT_RES_80_PERCENT,
	TRAN_PROP_BATT_A_WORKING,
	TRAN_PROP_BATT_B_WORKING,
	TRAN_PROP_BATT_SOC_READY,
	TRAN_PROP_BATT_FAST_CHG,
	TRAN_PROP_FG_REAL_SOC,
	TRAN_PROP_FG_VSOC,
	TRAN_PROP_FG_CSOC,
	TRAN_PROP_FG_DO_C,
	TRAN_PROP_FG_DO_V,
	TRAN_PROP_FG_UI_SOC,
	TRAN_PROP_FG_AGING,
	TRAN_PROP_BATT_REPORT_RAWSOC,
	/* pmic */
	TRAN_PROP_VBUS,
	TRAN_PROP_HW_OVP,
	/* otg */
	TRAN_PROP_OTG_CTL_TYPE,
	TRAN_PROP_OTG_PROTECT_FLAG,
	/* bypass chg */
	TRAN_PROP_GET_BYPASS_ENERGY,
	/* set charger vote */
	TRAN_PROP_SET_AICHG_VOTE,
	TRAN_PROP_SET_BYPASS_CHG_VOTE,
	TRAN_PROP_SET_SMTCHG_VOTE,
	/* send uevent */
	TRAN_PROP_SEND_UEVENT,
	/*type-c*/
	TRAN_PROP_TYPE_CC1,
	TRAN_PROP_TYPE_CC2,
	TRAN_PROP_TYPE_CC_SMT,
	/* real soc decimal prop */
	TRAN_PROP_GET_SOC_DECIMAL_SUPPORT,
	TRAN_PROP_SET_SOC_DECIMAL_RATE,
	TRAN_PROP_TRAN_VOTE_REFRESH,

	/*wireless*/ 
	TRAN_PROP_POWER_NOW,
	TRAN_PROP_POWER_PG_ON,
	TRAN_PROP_POWER_PG_OFF,
	/*algo*/
	TRAN_PROP_PROTOCOL_DONE_STATUS,

	TRAN_PROP_GET_VBUS_MEASURE_METHOD,

	TRAN_PROP_USB_PD_VOTE_AICR,
	TRAN_PROP_USB_PD_SET_POWER_PATH,

	TRAN_PROP_MAX,
};

enum tran_common_cmd {
	TRAN_COM_CALL_STATUS,
	TRAN_COM_MAX,
};

enum {
	HVDCP20_NONE = 0,
	HVDCP20_9V,
	HVDCP20_SWITCH_5V,
	HVDCP20_SWITCH_9V,
	HVDCP20_MAX,

	HVDCP30_NONE = 0,
	HVDCP30_HANDSHAKE,
	HVDCP30_DP_PULSE,
	HVDCP30_DM_PULSE,

	TC30_NONE = 0,
	TC30_7V5,
	TC30_SWITCH_5V,
	TC30_SWITCH_7V5,

	PE20_NONE = 0,
	PE20_9V,

	RFC_NONE = 0,
	RFC_HANDSHAKE,
};

/*
enum chg_dev_notifier_events {
	EVENT_FULL,
	EVENT_RECHARGE,
	EVENT_DISCHARGE,
	EVENT_PLUG_IN,
	EVENT_PLUG_OUT,
	EVENT_MAX,
};
*/

struct tran_properties {
	const char *alias_name;
};

/* tran_dev notify */
enum {
	TRAN_DEV_NOTIFY_PLUG_IN,
	TRAN_DEV_NOTIFY_PHY_PLUG_IN,
	TRAN_DEV_NOTIFY_PHY_PLUG_OUT,
	TRAN_DEV_NOTIFY_SCREEN_ON,
	TRAN_DEV_NOTIFY_SCREEN_OFF,
	TRAN_DEV_NOTIFY_BATT_ONLINE_CHANGE,
	TRAN_DEV_NOTIFY_BATT_MASTER_CHG_FULL,
	TRAN_DEV_NOTIFY_BATT_SLAVE_CHG_FULL,
};

struct tran_device {
	struct tran_properties props;
	const struct tran_ops *ops;
	struct mutex ops_lock;
	struct device dev;
	struct srcu_notifier_head evt_nh;
	void	*driver_data;
	bool is_polling_mode;
};

struct tran_ops {
	int (*set_prop)(struct tran_device *dev, enum tran_common_prop prop, const union com_propval *pval);
	int (*get_prop)(struct tran_device *dev, enum tran_common_prop prop, union com_propval *pval);
	int (*parse_cmd)(struct tran_device *dev, char *buf, enum tran_common_cmd cmd);
};

static inline void *tran_dev_get_drvdata(const struct tran_device *tran_dev)
{
	return tran_dev->driver_data;
}

static inline void tran_dev_set_drvdata(struct tran_device *tran_dev, void *data)
{
	tran_dev->driver_data = data;
}

#define to_tran_device(obj) container_of(obj, struct tran_device, dev)

static inline void *tran_get_data(struct tran_device *tran_dev)
{
	return dev_get_drvdata(&tran_dev->dev);
}

#if IS_ENABLED(CONFIG_TC_CLASS_SUPPORT)
extern struct tran_device *tran_device_register(const char *name,
	struct device *parent, void *devdata, const struct tran_ops *ops,
	const struct tran_properties *props);
extern void tran_device_unregister(struct tran_device *tran_dev);
extern int register_tran_device_notifier(
	struct tran_device *tran_dev,struct notifier_block *nb);
extern int unregister_tran_device_notifier(struct tran_device *tran_dev,
	struct notifier_block *nb);
extern int tran_dev_notify(struct tran_device *tran_dev, int event, void *data);
struct tran_device *tran_get_by_name(const char *name);

extern int tran_dev_set_prop(struct tran_device *dev, enum tran_common_prop prop, const union com_propval *pval);
extern int tran_dev_get_prop(struct tran_device *dev, enum tran_common_prop prop, union com_propval *pval);
#else
static inline struct tran_device *tran_device_register(const char *name,
	struct device *parent, void *devdata, const struct tran_ops *ops,
	const struct tran_properties *props)
{
	return NULL;
}

static inline void tran_device_unregister(struct tran_device *tran_dev)
{
	return;
}

static inline int register_tran_device_notifier(
	struct tran_device *tran_dev,struct notifier_block *nb)
{
	return -ENODEV;
}

static inline int unregister_tran_device_notifier(struct tran_device *tran_dev,
	struct notifier_block *nb)
{
	return -ENODEV;
}

static inline int tran_dev_notify(struct tran_device *tran_dev, int event, void *data)
{
	return -ENODEV;
}

static inline struct tran_device *tran_get_by_name(const char *name)
{
	return NULL;
}

static inline int tran_dev_set_prop(struct tran_device *dev, enum tran_common_prop prop, const union com_propval *pval)
{
	return -ERROR_NO_IMPLEMENT;
}

static inline int tran_dev_get_prop(struct tran_device *dev, enum tran_common_prop prop, union com_propval *pval)
{
	return -ERROR_NO_IMPLEMENT;
}
#endif

#endif
