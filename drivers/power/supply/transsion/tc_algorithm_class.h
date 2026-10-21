/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Transsion Inc.
 */

#ifndef __TC_CHARGER_ALGORITHM_CLASS_H__
#define __TC_CHARGER_ALGORITHM_CLASS_H__

#include <linux/module.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mutex.h>

struct tchg_alg_properties {
	const char *alias_name;
};

enum tchg_alg_state {
	ALG_INIT_FAIL,
	ALG_TA_CHECKING,
	ALG_TA_NOT_SUPPORT,
	ALG_NOT_READY,
	ALG_READY,
	ALG_RUNNING,
	ALG_DONE,
};

enum tchg_idx {
	CHG1 = 0,
	CHG2,
	DVCHG1,
	DVCHG2,
	DVCHG3,
	HVCHG,
	CHG_MAX,
};

enum tcharger_configuration {
	SINGLE_CHARGER,
	DUAL_CHARGERS_IN_SERIES,
	DUAL_CHARGERS_IN_PARALLEL,
	DIVIDER_CHARGER,
	DUAL_DIVIDER_CHARGERS,
};

enum support_spec {
	SUPPORT_SPEC_6_2 = 0,
	SUPPORT_SPEC_4_1,
	SUPPORT_SPEC_4_2,
	SUPPORT_SPEC_2_1,
	SUPPORT_SPEC_1_1,
	SUPPORT_SPEC_MAX,
};

enum chgalgo_num {
        PE50_NONE,
        PE50_PPS,
        PE50_UFCS,
};

struct tchg_alg_device {
	struct tchg_alg_properties props;
	const struct tchg_alg_ops *ops;
	enum tcharger_configuration config;
	struct mutex ops_lock;
	struct device dev;
	struct srcu_notifier_head evt_nh;
	void	*driver_data;
	void	*driver_hal_data;
	bool is_polling_mode;
	bool is_disabled;
	int alg_id;
};

enum tchg_alg_notifier_events {
	EVT_PLUG_IN,
	EVT_PLUG_OUT,
	EVT_FULL,
	EVT_RECHARGE,
	EVT_DETACH,
	EVT_HARDRESET,
	EVT_VBUSOVP,
	EVT_IBUSOCP,
	EVT_IBUSUCP_FALL,
	EVT_VBATOVP,
	EVT_IBATOCP,
	EVT_VOUTOVP,
	EVT_VDROVP,
	EVT_VBATOVP_ALARM,
	EVT_VBUSOVP_ALARM,
	EVT_WLS_FULL,
	EVT_ALGO_STOP,
	EVT_MAX,
};

struct tchg_alg_notify {
	enum tchg_alg_notifier_events evt;
	int value;
};

struct tchg_limit_setting {
	int cv;
	int input_current_limit1;
	int input_current_limit2;
	int charging_current_limit1;
	int charging_current_limit2;
	int input_current_limit_dvchg1;
	bool vbat_mon_en;
};

enum tchg_alg_props {
	ALG_MAX_VBUS,
	ALG_LOG_LEVEL,
	ALG_REF_VBAT,
	ALG_IS_FAST_CHR,
	ALG_ADAPTER_CAPACITY,
	ALG_CHG_STATUS,
	ALG_TAPER_DONE,
	ALG_GET_PDP,
	ALG_TA_TEMP,
	ALG_TA_CHECK_STATE,
	ALG_TA_RFC_TA,
	ALG_AUTO_TEST_IRQ,
	ALG_INPUT_CURRENT,
	ALG_CHG_CURRENT,
	ALG_GET_FFC_CV,
	ALG_GET_FFC_EOC,
	ALG_GET_MASTER_FFC_CV,
	ALG_GET_MASTER_FFC_EOC,
	ALG_GET_SLAVE_FFC_CV,
	ALG_GET_SLAVE_FFC_EOC,
	ALG_MASTER_STEP_CC,
	ALG_IS_FFC_CHG,
	ALG_SUPP_LONG_LIFE_RECHG,
	ALG_GET_LONG_LIFE_RECHG_CV_GAP,
	ALG_GET_LONG_LIFE_RECHG_CUR,
	ALG_MULTI_CHG_SPEED,
	ALG_MULTI_CHG_SPEED_OWNER,
	ALG_RUNNING_VOL,
	ALG_SET_CYCLE_RATIO,
	ALG_NUM,
	ALG_PROJECT_POWER,
	ALG_SW_EOC_EN_HW_EOC,
};

struct tchg_alg_ops {
	int (*init_algo)(struct tchg_alg_device *alg);
	int (*is_algo_ready)(struct tchg_alg_device *alg);
	int (*start_algo)(struct tchg_alg_device *alg);
	bool (*is_algo_running)(struct tchg_alg_device *alg);
	bool (*is_ta_ready)(struct tchg_alg_device *alg);
	int (*plugout_reset)(struct tchg_alg_device *alg);
	int (*stop_algo)(struct tchg_alg_device *alg);
	int (*notifier_call)(struct tchg_alg_device *alg,struct tchg_alg_notify *notify);
	int (*get_prop)(struct tchg_alg_device *alg,enum tchg_alg_props s, int *value);
	int (*set_prop)(struct tchg_alg_device *alg,enum tchg_alg_props s, int value);
	int (*set_current_limit)(struct tchg_alg_device *alg_dev,struct tchg_limit_setting *setting);

};

#define to_tchg_alg_dev(obj) container_of(obj, struct tchg_alg_device, dev)

static inline void *tchg_alg_dev_get_drvdata(const struct tchg_alg_device *tchg_alg)
{
	return tchg_alg->driver_data;
}

static inline void tchg_alg_dev_set_drvdata(struct tchg_alg_device *tchg_alg, void *data)
{
	tchg_alg->driver_data = data;
}

static inline void *tchg_alg_dev_get_drv_hal_data(const struct tchg_alg_device *tchg_alg)
{
	return tchg_alg->driver_hal_data;
}

static inline void tchg_alg_dev_set_drv_hal_data(struct tchg_alg_device *tchg_alg, void *data)
{
	tchg_alg->driver_hal_data = data;
}

extern struct tchg_alg_device *get_tchg_alg_by_name(const char *name);
extern struct tchg_alg_device *tchg_alg_device_register(
	const char *name, struct device *parent,
	void *devdata, const struct tchg_alg_ops *ops,
	const struct tchg_alg_properties *props);
extern void tchg_alg_device_unregister(
	struct tchg_alg_device *charger_dev);
extern int register_tchg_alg_notifier(struct tchg_alg_device *alg_dev,struct notifier_block *nb);
extern int unregister_tchg_alg_notifier(struct tchg_alg_device *alg_dev,struct notifier_block *nb);

extern int tchg_alg_init_algo(struct tchg_alg_device *alg_dev);
extern int tchg_alg_is_algo_ready(struct tchg_alg_device *alg_dev);
extern bool tchg_alg_is_ta_ready(struct tchg_alg_device *alg_dev);
extern int tchg_alg_start_algo(struct tchg_alg_device *alg_dev);
extern int tchg_alg_is_algo_running(struct tchg_alg_device *alg_dev);
extern int tchg_alg_stop_algo(struct tchg_alg_device *alg_dev);
extern int tchg_alg_get_prop(struct tchg_alg_device *alg_dev,enum tchg_alg_props s, int *value);
extern int tchg_alg_set_prop(struct tchg_alg_device *alg_dev,enum tchg_alg_props s, int value);
extern int tchg_alg_set_current_limit(struct tchg_alg_device *alg_dev,struct tchg_limit_setting *setting);
extern int tchg_alg_notifier_call(struct tchg_alg_device *alg_dev,struct tchg_alg_notify *notify);
extern char *tchg_alg_state_to_str(int state);
extern const char *const tchg_alg_notify_evt_tostring(enum tchg_alg_notifier_events evt);
extern int tchg_alg_plugout_reset(struct tchg_alg_device *alg);

#endif /* __TC_CHARGER_ALGORITHM_CLASS_H__ */
