// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#ifndef __TRAN_WIRELESS_MANAGER_H__
#define __TRAN_WIRELESS_MANAGER_H__

#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/power_supply.h>
#include <linux/time.h>
#include <linux/alarmtimer.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/pm_wakeup.h>
#include <linux/list.h>
#include "tc_charger_class.h"
#include "tc_tcpc.h"

#define END_MAX_SOC 	90
#define SHUTDOWN_LPM_SOC 	5

enum wireless_auth_state {
	WIRELESS_AUTH_UNKNOWN,
	WIRELESS_AUTH_SUCCESS,
	WIRELESS_AUTH_FAILED,
};

enum rx_setup_mode {
	FULL_BRIDGE,
	HALF_BRIDGE,
	UNKNOWN_BRIDGE,
};

enum rc_opener {
	RC_CLOSE,
	SETTING,
	ADB,
};

enum BOOST_PROJECT_DESIGN {
	SWITCH_CHARGER,
	CHARGER_PUMP_2P1,
	CHARGER_PUMP_4P1,
	EXTERNAL_BOOST,
};

struct mpp_protocol {
	bool mpp_support;
	u8 mpp_mode;
};

struct config_desc {
        bool low_inductions;
	bool support_magnetism_fan_check;
	bool support_lpm_down;
        u32 sw_vboost;
        u32 sw_iboost;
        u32 done_interval;
	int wireless_init_input_current;
	int wireless_init_charger_current;
	u32 boost_pd;
	u32 bank_soc_support;
	u8 boost_mode;
	u8 tx_power;
	u8 tx_protocol;
};

struct wls_data {
	u8 rd_tx_mode_cnt;
	bool epp_setup;
	bool config_setup;
	bool fast_charger;
	bool config_vol;
	u8 protocol;
};

struct wireless_manager {
	struct platform_device *pdev;
	spinlock_t lock_register;
	struct list_head head;
	struct task_struct *task;
	struct mutex thread_lock;
	struct mutex reverse_lock;
	struct mutex fw_lock;
	struct mutex notify_lock;
	struct kobject *kobj;
	struct config_desc desc;
	void *txc;

	struct charger_device *wireless_chg;
	struct charger_device *primary_charger;
	struct charger_device *dvchg1_dev;
	struct charger_properties chg_props;

	struct power_supply *wl_psy;
	struct power_supply_desc wl_desc;
	struct power_supply_config wl_cfg;

	struct work_struct uevent_work;
	struct delayed_work wake_work;
	struct delayed_work power_bank_work;
	struct delayed_work ldo_on;
	struct delayed_work lpm_work;

	struct alarm alarm_uevent;
	struct notifier_block pd_nb;

	wait_queue_head_t wait_que;
	volatile unsigned long work_state;

	struct tran_device *tc_chg_dev;
	struct tran_device *pid_chg_dev;
	struct device *dev;
	struct tran_device *tc_wireless_dev;
	struct tran_properties tc_wireless_props;

	bool wakeup_thread;
	enum wireless_auth_state private_protocol;
	bool pg_status;
	int reverse_charger;
	int record;
	bool reverse_setup;

	bool plug_in;
	bool plug_out;
	bool wired_otg_en;
	bool epp_status;
	bool max_soc_plugin;
	atomic_t fw_update;
	bool boost;

	u8 power_bank_soc;
	bool pa_auth_done;
	bool tx_det_rx;
	bool power_bank;
	bool chg_done;
	bool tx_magnetism_fan;

	bool probe_done;
	bool wired_plugin;
	bool shutdown;

	void (*state_call_back)(void *data, int event);

	enum rx_setup_mode rx_setup_mode;

#if IS_ENABLED(CONFIG_TRAN_AGING_KOM)
	int fake_rxdetect;
#endif
	struct wls_data wls;
	struct votable *total_aicr_vote;
	struct votable *total_ichg_vote;
	struct votable *chg1_mivr_vote;

	bool pd_connect_hardreset;
	bool pd_connect;
};

#endif
