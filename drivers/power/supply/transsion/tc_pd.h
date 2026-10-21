
// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 Transsion Inc.
 */

#ifndef __TC_PD_H
#define __TC_PD_H

#include "tc_algorithm_class.h"


#define V_CHARGER_MIN 4600000 /* 4.6 V */
#define CHARGER_CURRENT_LIMIT	3000000 /* 3.0A */
#define TA_ICHG_LEAVE_THRESHOLD 1000000 /* 1.0A */

/* pd */
#define PD_VBUS_UPPER_BOUND		10000000	/* uv */
#define PD_VBUS_LOW_BOUND		5000000	/* uv */

#define PD_STOP_BATTERY_SOC 80

#define DISABLE_VBAT_THRESHOLD  4350000
#define PD_HV_MAX_AICR			2000000 /* 2.0A */
#define PD_MAX_WATT_SUPPORT		18000000  /* normal 18W */

#define PD_ERROR_LEVEL	1
#define PD_INFO_LEVEL	2
#define PD_DEBUG_LEVEL	3

extern int pd_get_debug_level(void);
#define pd_err(fmt, args...)					\
do {								\
	if (pd_get_debug_level() >= PD_ERROR_LEVEL) {	\
		pr_notice("[ALG_PD]" fmt, ##args);				\
	}							\
} while (0)

#define pd_info(fmt, args...)					\
do {								\
	if (pd_get_debug_level() >= PD_INFO_LEVEL) { \
		pr_notice("[ALG_PD]" fmt, ##args);				\
	}							\
} while (0)

#define pd_dbg(fmt, args...)					\
do {								\
	if (pd_get_debug_level() >= PD_DEBUG_LEVEL) {	\
		pr_notice("[ALG_PD]" fmt, ##args);				\
	}							\
} while (0)

#define PD_CAP_MAX_NR 10

struct pd_power_cap {
	uint8_t selected_cap_idx;
	uint8_t nr;
	uint8_t pdp;
	uint8_t pwr_limit[PD_CAP_MAX_NR];
	int max_mv[PD_CAP_MAX_NR];
	int min_mv[PD_CAP_MAX_NR];
	int ma[PD_CAP_MAX_NR];
	int maxwatt[PD_CAP_MAX_NR];
	int minwatt[PD_CAP_MAX_NR];
	uint8_t type[PD_CAP_MAX_NR];
	int info[PD_CAP_MAX_NR];
};

struct tc_pd {
	struct platform_device *pdev;
	struct tchg_alg_device *alg;
	struct mutex access_lock;
	struct wakeup_source *suspend_lock;

	int vbat_threshold; /* For checking Ready */
	int check_ta_cnt;
	int state;

	/* dtsi setting */
	int vbus_l;
	int vbus_h;
	int min_charger_voltage;
	int charger_current_limit;
	int ta_ichg_level_threshold;
	int pd_stop_battery_soc;
	int pd_cap_max_watt;
	int pd_hv_max_aicr;

	struct pd_power_cap cap;
	int pd_idx;
	int pd_reset_idx;
	int pd_boost_idx;
	int pd_buck_idx;
	bool is_cable_out_occur;
	bool is_fast_chr;
	bool plug_in;
	int old_cap_nr;
	int mivr;
	int aicr;
};

extern int pd_hal_get_vbat(struct tchg_alg_device *alg);
extern int pd_hal_get_ibat(struct tchg_alg_device *alg);
extern int pd_hal_init_hardware(struct tchg_alg_device *alg);
extern int pd_hal_is_pd_adapter_ready(struct tchg_alg_device *alg);
extern int pd_hal_get_adapter_cap(struct tchg_alg_device *alg,
	struct pd_power_cap *cap);
extern int pd_hal_get_vbus(struct tchg_alg_device *alg);
extern int pd_hal_get_mivr_state(struct tchg_alg_device *alg,
	enum tchg_idx chgidx, bool *in_loop);
extern int pd_hal_get_mivr(struct tchg_alg_device *alg,
	enum tchg_idx chgidx, int *mivr1);
extern int pd_hal_set_mivr(struct tchg_alg_device *alg, bool enable, int uV);
extern int pd_hal_enable_vbus_ovp(struct tchg_alg_device *alg, bool enable);
extern int pd_hal_get_input_current(struct tchg_alg_device *alg,
	enum tchg_idx chgidx, u32 *ua);
extern int pd_hal_set_input_current(struct tchg_alg_device *alg,
	bool enable, u32 uA);
extern int pd_hal_set_adapter_cap(struct tchg_alg_device *alg,
	int mV, int mA);
extern int pd_hal_do_charger_notify(struct tchg_alg_device *alg,
	int event);
extern int pd_hal_get_uisoc(struct tchg_alg_device *alg);
extern int pd_hal_get_log_level(struct tchg_alg_device *alg);

#endif /* __TC_PD_H */
