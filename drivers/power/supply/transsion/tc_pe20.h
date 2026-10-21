/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Transsion Inc.
 */

#ifndef __TC_PE20_H
#define __TC_PE20_H

#include "tc_algorithm_class.h"

/* pe20.0 */
#define TA_ICHG_LEAVE_THRESHOLD 1000000 /* uA */
#define TA_START_BATTERY_SOC	0
#define TA_STOP_BATTERY_SOC	90

#define PEOFFTIME 40
#define PEONTIME 90

#define DISABLE_VBAT_THRESHOLD  4350000
#define DEFAULT_ICHG    2000000

#define ECABLEOUT	1	/* cable out */
#define EHAL		2	/* hal operation error */

#define PE20_ERROR_LEVEL     1
#define PE20_INFO_LEVEL	     2
#define PE20_DEBUG_LEVEL     3

extern int pe20_get_debug_level(void);

#define pe20_err(fmt, args...)					\
do {								\
	if (pe20_get_debug_level() >= PE20_ERROR_LEVEL) {	\
		pr_notice("[ALG_PE20]" fmt, ##args);				\
	}							\
} while (0)

#define pe20_info(fmt, args...)					\
do {								\
	if (pe20_get_debug_level() >= PE20_INFO_LEVEL) { \
		pr_notice("[ALG_PE20]" fmt, ##args);			\
	}							\
} while (0)

#define pe20_dbg(fmt, args...)					\
do {								\
	if (pe20_get_debug_level() >= PE20_DEBUG_LEVEL) {	\
		pr_notice("[ALG_PE20]" fmt, ##args);				\
	}							\
} while (0)

struct tc_pe20 {
	struct platform_device *pdev;
	struct tchg_alg_device *alg;

	struct mutex access_lock;
	struct wakeup_source *suspend_lock;
	struct mutex cable_out_lock;
	struct mutex data_lock;
	bool is_cable_out_occur; /* Plug out happened while detect PE20 */
	struct power_supply *bat_psy;

	int vbat_threshold; /* For checking Ready */

	int ta_vchr_target;
	bool is_fast_chr;
	bool support_pe20_by_switch;

	/* pe20 dtsi */
	int ta_ichg_level_threshold;	/* ma */
	int ta_stop_battery_soc;

	enum tchg_alg_state state;
	u32 *chr_volt;
	u32 *chr_volt_mivr;
	u32 *chr_volt_input_cur;
	u32 *chr_volt_chg_cur;
};

extern int pe20_hal_init_hardware(struct tchg_alg_device *alg);
extern int pe20_hal_get_uisoc(struct tchg_alg_device *alg);
extern int pe20_hal_get_charger_type(struct tchg_alg_device *alg);
extern int pe20_hal_set_mivr(struct tchg_alg_device *alg, bool enable, int uV);
extern int pe20_hal_get_charger_cnt(struct tchg_alg_device *alg);
extern int pe20_hal_get_vbus(struct tchg_alg_device *alg);
extern int pe20_hal_get_vbat(struct tchg_alg_device *alg);
extern int pe20_hal_get_ibat(struct tchg_alg_device *alg);
extern int pe20_hal_get_mivr_state(struct tchg_alg_device *alg,enum tchg_idx chgidx, bool *in_loop);
extern int pe20_hal_get_charger_current_limit(struct tchg_alg_device *alg);
extern int pe20_hal_enable_vbus_ovp(struct tchg_alg_device *alg, bool enable);
extern int pe20_hal_get_log_level(struct tchg_alg_device *alg);
extern int ta_dev_set_usb_ctrl_pe20(struct tchg_alg_device *alg, int val);
extern int pe20_hal_get_input_current(struct tchg_alg_device *alg,
	enum tchg_idx chgidx, u32 *uA);
extern int pe20_hal_set_input_current(struct tchg_alg_device *alg, bool enable, u32 uA);

#endif /* __TC_PE20_H */
