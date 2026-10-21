// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#ifndef LINUX_TC_MISC_INTF_H
#define LINUX_TC_MISC_INTF_H
#include "tc_algorithm_class.h"

#define TUSB_DIV_RES  100000 
#define TUSB_DIV_VOL	1800 
#define ERROR_NTC_TEMP   -127000

enum alias_type {
	TC_UNKNOWN = 0,
	TC_SDP,
	TC_CDP,
	TC_DCP,
	TC_NON_STD,
	TC_WIRELESS,
	TC_MAX,
};

enum tc_boot_mode_t {
	NORMAL_BOOT = 0,
	META_BOOT = 1,
	RECOVERY_BOOT = 2,
	SW_REBOOT = 3,
	FACTORY_BOOT = 4,
	ADVMETA_BOOT = 5,
	ATE_FACTORY_BOOT = 6,
	ALARM_BOOT = 7,
	KERNEL_POWER_OFF_CHARGING_BOOT = 8,
	LOW_POWER_OFF_CHARGING_BOOT = 9,
	DONGLE_BOOT = 10,
	UNKNOWN_BOOT
};

extern bool tc_get_boot_finish(void);
extern int tc_get_boot_mode(void);
extern int tc_get_boot_type(void);
extern bool tc_get_atm_mode(void);
extern int tc_get_batt_id(void);
extern int tc_get_usb_type(void);
extern int tc_get_charger_type(void);
extern int tc_get_alias_type(void);
extern int tc_get_pd_type(void);
extern int tc_get_accurate_tpcb_temp(void);
extern int tc_get_tpcb_temp(void);
extern int tc_get_tpa_temp_4g(void);
extern int tc_get_tpa_temp_5g(void);
extern int tc_get_tpa_temp_max(void);
extern int tc_get_accurate_tpa_temp_4g(void);
extern int tc_get_accurate_tpa_temp_5g(void);
extern int tc_get_accurate_tpa_temp_max(void);
extern int tc_get_tusb_temp(int *tusb);
extern int tc_get_port_temp(int *port_temp);
extern int tc_get_uisoc(void);
extern int tc_get_qmax(void);
extern int tc_get_bypass_energy(void);
extern bool tc_get_pe50_det_done(void);
extern int tc_get_battery_temperature(void);
extern int tc_get_accurate_battery_temperature(void);
extern int tc_get_battery_current(void);
extern int tc_get_battery_voltage(void);
extern int tc_is_battery_exist(void);
extern int tc_get_vbus(void);
extern int tc_get_ibus(void);
extern int tc_disable_hw_ovp(bool enable);
extern int tc_get_battery_cycle(void);
extern int tc_get_battery_raw_cycle(void);
extern int tc_set_battery_cycle(int battery_cycle);
extern int tc_set_alg_prop(char *alg_name, enum tchg_alg_props s, int value);
extern int tc_get_alg_prop(char *alg_name, enum tchg_alg_props s, int *value);
extern int tc_get_monkey_flag(void);

#endif