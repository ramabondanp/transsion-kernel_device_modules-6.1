// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#ifndef __LINUX_THIRD_GAUGE_MANAGER_H__
#define __LINUX_THIRD_GAUGE_MANAGER_H__

#include <generated/autoconf.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/kthread.h>
#include <linux/device.h>
#include <linux/pm_wakeup.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/writeback.h>
#include <linux/seq_file.h>
#include <linux/nvmem-consumer.h>
#include <linux/power_supply.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/reboot.h>
#include <linux/of.h>
#include <linux/alarmtimer.h>
#include "tc_common_class.h"
#include "tc_charger.h"

#define HIGH_UPDATE_FREQ     60
#define LOW_UPDATE_FREQ      600
#define FULL_CAPACITY        10000
#define PER_CAPACITY         100
#define CHG_INTERVAL	 	 1           
#define CHG_DEINTERVAL	 	 5 
enum batt_status{
	BATTERY_STATUS_DISCAHRGING = 1,
	BATTERY_STATUS_CHARGING = 0,
	BATTERY_STATUS_UNKNOWN = -1,
};

enum {
	NON_BATT_MODE = 0,
	SINGLE_MASTER_BATT_MODE,
	SINGLE_SLAVE_BATT_MODE,
	DUAL_BATT_MODE,
};

enum batt_enum {
	BATTERY_MASTER = 0,
	BATTERY_SLAVE,
	BATTERY_MAX,
};

static const char *const batt_name[BATTERY_MAX] = {
	[BATTERY_MASTER] = "batt_master",
	[BATTERY_SLAVE] = "batt_slave",
};

struct battery_info {
	int temperature;
	int time_to_empty;
	int time_to_empty_avg;
	int time_to_full;
	int charge_full;
	int cycle_count;
	int energy;
	int flags;
	int power_avg;
	int health;
	int rc;
	int charge_design_full;
	int raw_soc;
	int batt_soc;
	int smooth_soc;
	int ui_soc;
	int voltage;
	int batt_cur;
	bool present;
};

struct battery_manager {
	struct device *dev;
	struct power_supply *bms_psy;
	struct power_supply_desc bms_psy_desc;
	struct power_supply_config bms_psy_cfg;
	struct power_supply *chg_psy;
	struct charger_device *chg1_dev;
	struct timespec64  soc_update_time;
	struct tran_device *fg_a_dev;
	struct tran_device *fg_b_dev;
	struct tran_device *tc_chg_dev;
	struct task_struct *bms_monitor;
	struct battery_info data;
	struct battery_info *fg_a_data;
	struct battery_info *fg_b_data;
	struct alarm alarm_timer;
	struct tran_device *bms_dev;
	struct tran_properties batt_prop;
	struct wakeup_source *suspend_lock;
	struct mutex ops_lock;
	struct mutex thread_lock;
	wait_queue_head_t  wait_que;

	bool is_batt_en[BATTERY_MAX];

	int first_full_soc;
	int monitor_flag;
	int boot_point_gap;
	int hidden_point_gap_accelerate;
	int update_per_soc_time;
	int update_per_soc_time_min;
	int update_per_soc_time_max;
	int smooth_soc_full_vol;
	int last_smooth_soc;
	int last_raw_soc;
	int charging_interval;
	int discharging_interval;
	int chg_status;
	int dual_fg;
	int run_mode;
	int pull_full_soc_min;
	int pull_full_polling_interval;
	int pull_full_soc_gap;
	int hidden_point_gap;
	int resume_polling_inerval;
	const char *fg_a_name;
	const char *fg_b_name;
	bool soc_ready;
	struct charger_device *lmt_chg_dev;
	int chg_lmt_curr;
	int master_sw_cv_gap;
	int slave_sw_cv_gap;
	int max_voltage_diff;
	int start_balance_vol_diff;
	int balance_step;
	int balance_step_cur;
	int min_lmt_chg_cur;
};

#endif
