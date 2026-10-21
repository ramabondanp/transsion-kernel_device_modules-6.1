
// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Transsion Inc.
 */

#ifndef __TC_PE5_EVO_H
#define __TC_PE5_EVO_H

#include <linux/power_supply.h>
#include <linux/delay.h>
#include "tc_algorithm_class.h"
#include "tc_common_class.h"
#include "tc_charger.h"
#include "tc_voter.h"
#include "tc_tcpc.h"
#include "tc_gauge.h"

#define PE50_ERR_LEVEL	1
#define PE50_INFO_LEVEL	2
#define PE50_DBG_LEVEL	3
#define PE50_ITA_GAP_WINDOW_SIZE	50
#define PRECISION_ENHANCE	5

/* Parameters */
#define PE50_VTA_INIT		5300	/* mV */
#define PE50_ITA_INIT		3000	/* mA */
#define PE50_TA_WDT_MIN		10000	/* ms */
#define PE50_VTA_GAP_MIN	200	/* mV */
#define PE50_VTA_VAR_MIN	103	/* % */
#define PE50_ITA_TRACKING_GAP	150	/* mA */
#define PE50_DVCHG_VBUSALM_GAP	100	/* mV */
#define PE50_VBUSOVP_RATIO	110
#define PE50_IBUSOCP_RATIO	116
#define PE50_VBATOVP_RATIO	110
#define PE50_IBATOCP_RATIO	130
#define PE50_ITAOCP_RATIO	130
#define PE50_IBUSUCPF_RECHECK		250	/* mA */
#define PE50_VBUS_CALI_THRESHOLD	500 // 150	/* mV */
#define PE50_CV_LOWER_BOUND_GAP		50	/* mV */
#define PE50_INIT_POLLING_INTERVAL	200	/*default500 ms */
#define PE50_INIT_RETRY_MAX	0
#define PE50_MEASURE_R_RETRY_MAX	3
#define PE50_MEASURE_R_AVG_TIMES	10
#define PE50_VSYS_UPPER_BOUND            4700    /* mV */
#define PE50_VSYS_UPPER_BOUND_GAP        40      /* mV */

#define PE50_HIGH_CAL_FREQ      3
#define PE50_LOW_CAL_FREQ       10
#define PE50_AB_RETRY_MAX	(5)     /*stop PE50*/

#define	PE50_DEFAULT_CAPACITY		4500
#define	PE50_EMARK_CAPACITY_5A		5000
#define	PE50_EMARK_CAPACITY_9A		9100
#define	PE50_DUAL_RP_CAPACITY		7000
#define	PE50_RP_RA_CAPACITY		7000

#define PE50_DEFAULT_VOL_DIFF		1000

#define DUMP_FFC_BUF_SIZE               3000 // byte
#define U8_TO_INT_MAX             255 // byte
#define U8_TO_INT_ERR			  -10
#define U32_TO_INT_MAX			0x7FFFFFFF
#define U32_TO_INT_ERR			-9	

#define PE50_HWERR_NOTIFY \
	(BIT(EVT_VBUSOVP) | BIT(EVT_IBUSOCP) | BIT(EVT_VBATOVP) | \
	 BIT(EVT_IBATOCP) | BIT(EVT_VOUTOVP) | BIT(EVT_VDROVP) | \
	 BIT(EVT_IBUSUCP_FALL))

#define PE50_RESET_NOTIFY \
	(BIT(EVT_DETACH) | BIT(EVT_HARDRESET))


extern int pe50_get_log_level(void);
#define PE50_DBG(fmt, ...) \
	do { \
		if (pe50_get_log_level() >= PE50_DBG_LEVEL) \
			pr_info("[PE50]%s " fmt, __func__, ##__VA_ARGS__); \
	} while (0)

#define PE50_INFO(fmt, ...) \
	do { \
		if (pe50_get_log_level() >= PE50_INFO_LEVEL) \
			pr_info("[PE50]%s " fmt, __func__, ##__VA_ARGS__); \
	} while (0)

#define PE50_ERR(fmt, ...) \
	do { \
		if (pe50_get_log_level() >= PE50_ERR_LEVEL) \
			pr_info("[PE50]%s " fmt, __func__, ##__VA_ARGS__); \
	} while (0)

#define PE50_STOP_BY_AB  (1)     /*stop PE50*/
#define PE50_REALSE_AB   (0)     /*re-enter PE50*/

enum {
	TTA_LEVEL = 0,
	TDVCHG_LEVEL,
	TBAT_LEVEL,
	TPCB_LEVEL,
	TPA_LEVEL,
	SYS_POWER_LEVEL,

};

enum pe50_adc_channel {
	PE50_ADCCHAN_VBUS = 0,
	PE50_ADCCHAN_IBUS,
	PE50_ADCCHAN_VBAT,
	PE50_ADCCHAN_IBAT,
	PE50_ADCCHAN_TBAT,
	PE50_ADCCHAN_TPCB,
	PE50_ADCCHAN_TPA,
	PE50_ADCCHAN_TCHG,
	PE50_ADCCHAN_VOUT,
	PE50_ADCCHAN_VSYS,
	PE50_ADCCHAN_MAX,
};

enum pe50_algo_state {
	PE50_ALGO_INIT = 0,
	PE50_ALGO_MEASURE_R,
	PE50_ALGO_SS_DVCHG,
	PE50_ALGO_CC_CV,
	PE50_ALGO_STOP,
	PE50_ALGO_STATE_MAX,
};

enum pe50_thermal_level {
	PE50_THERMAL_VERY_COLD = 0,
	PE50_THERMAL_COLD,
	PE50_THERMAL_VERY_COOL,
	PE50_THERMAL_COOL,
	PE50_THERMAL_NORMAL,
	PE50_THERMAL_WARM,
	PE50_THERMAL_WARM1,
	PE50_THERMAL_WARM2,
	PE50_THERMAL_WARM3,
	PE50_THERMAL_WARM4,
	PE50_THERMAL_WARM5,
	PE50_THERMAL_WARM6,
	PE50_THERMAL_WARM7,
	PE50_THERMAL_WARM8,
	PE50_THERMAL_VERY_WARM,
	PE50_THERMAL_HOT,
	PE50_THERMAL_VERY_HOT,
	PE50_THERMAL_MAX,
};

enum pe50_rcable_level {
	PE50_RCABLE_NORMAL = 0,
	PE50_RCABLE_BAD1,
	PE50_RCABLE_BAD2,
	PE50_RCABLE_BAD3,
	PE50_RCABLE_MAX,
};

enum pe50_dvchg_role {
	PE50_DVCHG_MASTER = 0,
	PE50_DVCHG_SLAVE,
	PE50_DVCHG_THIRD,
	PE50_DVCHG_FOURTH,
	PE50_DVCHG_MAX,
};

enum dv2_special_dvchg_role {
	DV2_SPECIAL_DVCHG_MASTER,
	DV2_SPECIAL_DVCHG_MAX,
};

enum pe50_abnormal_dev {
	PE50_TBAT = 0,
	PE50_TTA,
	PE50_TDVCHG,
	PE50_TSWCHG,
	PE50_CC_OVP,
	PE50_IBUS_UCP,
	PE50_TA_OTP,
	PE50_ABMMORMAL_MAX,
};

enum pe50_ffc_temp {
	PE50_TEMP_LOW = 0,
	PE50_TEMP_HIGH,
	PE50_TEMP_MAX,
};

enum pe50_cycle_cnt {
	PE50_CYCLE_CNT_LOW = 0,
	PE50_CYCLE_CNT_HIGH,
	PE50_CYCLE_CNT_MAX,
};

enum pe50_vbat_ffc_level {
	PE50_VBAT_FFC_LOW = 0,
	PE50_VBAT_FFC_MID1,
	PE50_VBAT_FFC_MID2,
	PE50_VBAT_FFC_MID3,
	PE50_VBAT_FFC_MID4,
	PE50_VBAT_FFC_HIGH,
	PE50_VBAT_FFC_MAX,
};

enum pe50_cable_capabilty {
	EMARK_CABLE_3A = 0,
	EMARK_CABLE_5A,
	EMARK_CABLE_9A,
	EMARK_CABLE_MAX,
};

enum pe50_cable_capabilty_level {
	PE50_CABLE_CAPABILTY_LOW,
	PE50_CABLE_CAPABILTY_MID1,
	PE50_CABLE_CAPABILTY_MID2,
	PE50_CABLE_CAPABILTY_HIGH,
	PE50_CABLE_CAPABILTY_MAX,
};

enum pe50_tran_dev {
	PE50_TRAN_TC_CHG_DEV = 0,
	PE50_TRAN_DEV_MAX,

};

static const char *const pe50_dvchg_role_name[PE50_DVCHG_MAX] = {
	"master", "slave", "third", "fourth",
};

static const char *const pe50_algo_state_name[PE50_ALGO_STATE_MAX] = {
	"INIT", "MEASURE_R", "SS_DVCHG", "CC_CV", "STOP",
};

typedef struct step_ffc {
	int ffc_temp[PE50_TEMP_MAX];
	int ffc_vbat[PE50_VBAT_FFC_MAX];
	int ffc_ibat[PE50_VBAT_FFC_MAX];
	int ffc_term;
	int cycle_cnt[PE50_CYCLE_CNT_MAX];
	int rechg_gap;
	int rechg_cur;
} step_ffc_t;


struct pe50_multi_desc {
	int tbat_level_def[PE50_THERMAL_MAX];
	int tbat_curlmt[PE50_THERMAL_MAX];
	int tpcb_level_def[PE50_THERMAL_MAX];
	int tpcb_curlmt[PE50_THERMAL_MAX];
	int tpa_level_def[PE50_THERMAL_MAX];
	int tpa_curlmt[PE50_THERMAL_MAX];
};

struct pe50_stop_info {
	bool hardreset_ta;
	bool reset_ta;
};

struct pe50_ta_status {
	int temperature;
	u8 cable_capability;
	bool ocp;
	bool otp;
	bool ovp;
};

struct pe50_ta_auth_data {
	int vcap_min;
	int vcap_max;
	int icap_min;
	int vta_min;
	int vta_max;
	int ita_max;
	int ita_min;
	bool pwr_lmt;
	u8 pdp;
	bool support_meas_cap;
	bool support_status;
	u32 vta_step;
	u32 ita_step;
	u32 ita_gap_per_vstep;
	u32 ita_gap_cv_mode;
	u32 full_power_run_time;
	int cable_capability;
};

struct pe50_algo_data {
	bool is_dvchg_exist[PE50_DVCHG_MAX];

	/* Thread & Timer */
	struct alarm timer;
	struct task_struct *task;
	struct mutex lock;
	struct mutex ext_lock;
	struct tran_device *tc_lcd;
	struct tran_device *tc_gauge;
	struct tran_device *fg_a_dev;
	struct tran_device *fg_b_dev;
	struct notifier_block pe50_screen_notifier;
	wait_queue_head_t wq;
	atomic_t wakeup_thread;
	atomic_t stop_thread;
	atomic_t stop_algo;

	int fg_a_vbat;
	int fg_a_ibat;
	int fg_a_temp;
	int fg_a_status;
	int fg_b_vbat;
	int fg_b_ibat;
	int fg_b_temp;
	int fg_b_status;

	/* Notify */
	struct mutex notify_lock;
	u32 notify;

	/* Algorithm */
	bool screen_on;
	bool inited;
	bool ta_ready;
	bool run_once;
	bool is_swchg_en;
	bool is_dvchg_en[PE50_DVCHG_MAX];
	bool ignore_ibusucpf;
	bool force_ta_cv;
	bool ignore_measure_r;
	bool suspect_ta_cc;
	struct pe50_ta_auth_data ta_auth_data[SUPPORT_SPEC_MAX];
	bool ta_auth_support_spec[SUPPORT_SPEC_MAX]; /* ta auth support spec */
	int ta_support_spec_cnt;
	u32 vta_setting;
	u32 ita_setting;
	u32 vta_measure;
	u32 ita_measure;
	u32 ita_gap_per_vstep;
	u32 ita_gap_window_idx;
	u32 ita_gaps[PE50_ITA_GAP_WINDOW_SIZE];
	u32 ichg_setting;
	u32 aicr_setting;
	u32 aicr_lmt;
	u32 aicr_init_lmt;
	u32 idvchg_cc;
	u32 idvchg_ss_init;
	u32 idvchg_term;
	int vbus_cali;
	u32 r_sw;
	u32 r_cable;
	u32 r_cable_by_swchg;
	u32 ita_lmt_rcable;
	u32 r_bat;
	u32 r_total;
	u32 ita_lmt;
	u32 ita_pwr_lmt;
	u32 cv_lower_bound;
	u32 err_retry_cnt;
	u32 vbusovp;
	u32 zcv;
	u32 vbat_cv_no_ircmp;
	u32 vbat_cv;
	u32 vbat_ircmp;
	u32 dynamic_sys_power;
	int vta_comp;
	int vbat_threshold; /* For checking Ready */
	int ref_vbat; /* Vbat with cable in */
	bool is_vbat_over_cv;
	ktime_t stime;
	enum pe50_algo_state state;
	enum pe50_thermal_level tbat_level;
	enum pe50_thermal_level tta_level;
	enum pe50_thermal_level tpcb_level;
	enum pe50_thermal_level tpa_level;
	enum pe50_thermal_level sys_power_level;
	enum pe50_thermal_level tdvchg_level;
	enum pe50_thermal_level tswchg_level;
	int input_current_limit;
	int cv_limit;
	int pe50_vta_init;
	int pe50_ita_init;
	int pe50_over_power;
	struct timespec64 dtime;
	bool full_power_flag;
	int pwr_ratio;
	u32 vta_up_ita_stable_cnt;
	u32 ita_meas_lmt;
	int pe50_taper_done;
	bool pe50_plugout;
	int ibus_total;
	bool fb_on;
	int fb_on_ita;
	u32 chg_time_max;		/* max charging time */
	int tbat_level_def[PE50_THERMAL_MAX];
	int tbat_curlmt[PE50_THERMAL_MAX];
	int tpcb_level_def[PE50_THERMAL_MAX];
	int tpcb_curlmt[PE50_THERMAL_MAX];
	int tpa_level_def[PE50_THERMAL_MAX];
	int tpa_curlmt[PE50_THERMAL_MAX];
	u32 ita_stable_cnt;

	struct tran_device *bms_dev;
	int step_chg_index;
	int vbat_step_cv;
	int vbat_step_cc;
	int step_ffc_vbat[PE50_VBAT_FFC_MAX];
	int step_ffc_ibat[PE50_VBAT_FFC_MAX];
	int step_ffc_level;
	u32 bat_ffc_cv;
	u32 bat_ffc_eoc;

	int step_master_chg_index;
	int vbat_master_step_cv;
	int vbat_master_step_cc;
	int step_master_ffc_vbat[PE50_VBAT_FFC_MAX];
	int step_master_ffc_ibat[PE50_VBAT_FFC_MAX];
	int step_master_ffc_level;
	u32 bat_master_ffc_cv;
	u32 bat_master_ffc_eoc;

	int step_slave_chg_index;
	int vbat_slave_step_cv;
	int vbat_slave_step_cc;
	int step_slave_ffc_vbat[PE50_VBAT_FFC_MAX];
	int step_slave_ffc_ibat[PE50_VBAT_FFC_MAX];
	int step_slave_ffc_level;
	u32 bat_slave_ffc_cv;
	u32 bat_slave_ffc_eoc;

	u32 long_life_rechg_cv_gap;
	u32 long_life_rechg_cur;

	int pe50_auto_test_ibusucp;

	const char *adapter_name;
	int run_cp_cur[PE50_DVCHG_MAX];
	int run_cp_recovery_gap[PE50_DVCHG_MAX];

	u32 running_spec;
	u32 conversion_ratio;

	int ibus;
	int tbat;
	int vbat;

	int algo_num;
	int debug_code;

	int battery_cycle;
	int vol_diff_max;

	struct mutex chgspeed_lock;
	int chg_speed;
	int speed_owner;

	int bootmode;

	int force_spec;
	int running_ta;
};

/* Setting from dtsi */
struct pe50_algo_desc {
	u32 polling_interval;		/* polling interval */
	u32 ta_cv_ss_repeat_tmin;	/* min repeat time of ss for TA CV */
	u32 vbat_cv;			/* vbat constant voltage */
	u32 start_vbat_min;		/* algo start bat low bound */
	u32 start_vbat_max;		/* algo start bat upper bound */
	u32 idvchg_term;		/* terminated current */
	u32 ita_level[PE50_RCABLE_MAX];	/* input current */
	u32 rcable_level[PE50_RCABLE_MAX];	/* cable impedance level */
	u32 idvchg_ss_init;		/* SS state init input current */
	u32 idvchg_ss_step;		/* SS state input current step */
	u32 idvchg_ss_step1;		/* SS state input current step2 */
	u32 idvchg_ss_step2;		/* SS state input current step3 */
	u32 idvchg_ss_step1_vbat;	/* vbat threshold for ic_ss_step2 */
	u32 idvchg_ss_step2_vbat;	/* vbat threshold for ic_ss_step3 */
	u32 ta_blanking;		/* wait TA stable */
	u32 chg_time_max;		/* max charging time */
	u32 idvchg_pps_term;		/*pps terminated current */
	u32 ita_lmt_gap;
	int pe50_ab_dev[PE50_ABMMORMAL_MAX];
	int pe50_ab_retry_cnt;

	int tta_level_def[PE50_THERMAL_MAX];	/* TA temp level */
	int tta_curlmt[PE50_THERMAL_MAX];	/* TA temp current limit */
	int tbat_level_def[PE50_THERMAL_MAX];	/* BAT temp level */
	int tbat_curlmt[PE50_THERMAL_MAX];	/* BAT temp current limit */
	int tdvchg_level_def[PE50_THERMAL_MAX];	/* DVCHG temp level */
	int tdvchg_curlmt[PE50_THERMAL_MAX];	/* DVCHG temp current limit */
	int tpcb_level_def[PE50_THERMAL_MAX];    /* PCB temp level */
	int tpcb_curlmt[PE50_THERMAL_MAX];       /* PCB temp current limit */
	int tpa_level_def[PE50_THERMAL_MAX];    /* PCB temp level */
	int tpa_curlmt[PE50_THERMAL_MAX];       /* PCB temp current limit */
	int sys_power_level_def[PE50_THERMAL_MAX];    /* PCB temp level */
	int sys_power_curlmt[PE50_THERMAL_MAX];       /* PCB temp current limit */
	bool supprot_multi_level_charging;
	struct pe50_multi_desc tc_pe50_multi_level[TRAN_MULTI_SPEED_MAX-1];
	u32 tpcb_recovery_area;
	u32 tta_recovery_area;
	u32 tbat_recovery_area;
	u32 tpa_recovery_area;
	u32 sys_power_recovery_area;
	u32 tdvchg_recovery_area;
	u32 ifod_threshold;		/* FOD current threshold */
	u32 rsw_min;			/* min rsw */
	u32 ircmp_rbat;			/* IR compensation's rbat */
	u32 ircmp_vclamp;		/* IR compensation's vclamp */
	u32 default_spec;
	u32 support_spec[SUPPORT_SPEC_MAX]; /* support charge mode */
	u32 vta_cap_min[SUPPORT_SPEC_MAX]; /* min ta voltage capability */
	u32 vta_cap_max[SUPPORT_SPEC_MAX]; /* max ta voltage capability */
	u32 ita_cap_min[SUPPORT_SPEC_MAX]; /* min ta current capability */
	u32 vbus_max_ovp[SUPPORT_SPEC_MAX];
	bool support_switch_spec;
	const char **support_ta;	/* supported ta name */
	u32 support_ta_cnt;		/* supported ta count */
	bool allow_not_check_ta_status;	/* allow not to check ta status */

	// u32 full_power_run_time;
	u32 project_power;
	int project_pwr_ratio;
	u32 min_ita_gap;

	int step_cc_gap;
	step_ffc_t *step_ffc;
	int step_ffc_level;

	step_ffc_t *step_master_ffc;
	int step_master_ffc_level;

	step_ffc_t *step_slave_ffc;
	int step_slave_ffc_level;

	int pe5_ffc_gap[PE50_VBAT_FFC_MAX];
	bool support_dual_battery;
	u32 pe50_vta_init;
	u32 pe50_ita_init;
	u32 pe50_default_vta_step;

	int rechg_tbat_high;
	int rechg_tbat_low;
	int run_cp_cur[PE50_DVCHG_MAX];
	int run_cp_recovery_gap[PE50_DVCHG_MAX];

	u32 idvchg_level_multi[PE50_DVCHG_MAX];

	u32 vol_diff_gap;
	u32 cable_capability_level[PE50_CABLE_CAPABILTY_MAX];
	u32 vol_diff_level[PE50_CABLE_CAPABILTY_MAX];
};

struct pe50_algo_info {
	struct device *dev;
	struct tchg_alg_device *alg;
	struct pe50_algo_desc *desc;
	struct pe50_algo_data *data;
};

enum mtk_chg_type {
	MTK_CHGTYP_SWCHG = 0,
	MTK_CHGTYP_DVCHG,
	MTK_CHGTYP_DVCHG_SLAVE,
	MTK_CHGTYP_DVCHG_THIRD,
	MTK_CHGTYP_HV_DVCHG,
	MTK_CHGTYP_MAX,
};

struct mtk_chgdev_desc {
	enum mtk_chg_type type;
	const char *name;
	bool must_exist;
};

static struct mtk_chgdev_desc mtk_chgdev_desc_tbl[MTK_CHGTYP_MAX] = {
	{
		.type = MTK_CHGTYP_SWCHG,
		.name = "primary_chg",
		.must_exist = true,
	},
	{
		.type = MTK_CHGTYP_DVCHG,
		.name = "primary_dvchg",
		.must_exist = true,
	},
	{
		.type = MTK_CHGTYP_DVCHG_SLAVE,
		.name = "secondary_dvchg",
		.must_exist = false,
	},
	{
		.type = MTK_CHGTYP_DVCHG_THIRD,
		.name = "third_divider_chg",
		.must_exist = false,
	},
	{
		.type = MTK_CHGTYP_HV_DVCHG,
		.name = "hv_divider_charger",
		.must_exist = false,
	},
};

struct pe50_hal {
	struct device *dev;
	struct charger_device *chgdevs[MTK_CHGTYP_MAX];
	struct tadapter_device **adapters;
	struct tadapter_device *adapter;
	const char **support_ta;
	struct power_supply *bat_psy;
	int support_ta_cnt;
};

extern int pe50_hal_get_ta_fw(struct tchg_alg_device *alg, u8 *code, u32 length);
extern int pe50_hal_get_ta_output(struct tchg_alg_device *alg, int *mV, int *mA);
extern int pe50_hal_get_ta_status(struct tchg_alg_device *alg,
				  struct pe50_ta_status *status);
extern int pe50_hal_set_ta_cap(struct tchg_alg_device *alg, int mV, int mA);
extern int pe50_hal_is_ta_cc(struct tchg_alg_device *alg, bool *is_cc);
extern int pe50_hal_set_ta_wdt(struct tchg_alg_device *alg, u32 ms);
extern int pe50_hal_enable_ta_wdt(struct tchg_alg_device *alg, bool en);
extern int pe50_hal_enable_ta_charging(struct tchg_alg_device *alg, bool en,
				       int mV, int mA);
extern int pe50_hal_sync_ta_volt(struct tchg_alg_device *alg, u32 mV);
extern int pe50_hal_authenticate_ta(struct tchg_alg_device *alg,
				    struct pe50_ta_auth_data *data,
				    int adapter_num);
extern int pe50_hal_send_ta_hardreset(struct tchg_alg_device *alg);
extern int pe50_hal_init_hardware(struct tchg_alg_device *alg,
				  const char **support_ta, int support_ta_cnt);
extern int pe50_hal_enable_sw_vbusovp(struct tchg_alg_device *alg, bool en);
extern int pe50_hal_enable_charging(struct tchg_alg_device *alg,
				    enum tchg_idx chgidx, bool en);
extern int pe50_hal_enable_chip(struct tchg_alg_device *alg, enum tchg_idx chgidx,
				bool en);
extern int pe50_hal_enable_hz(struct tchg_alg_device *alg, bool en);
extern int pe50_hal_set_vbusovp(struct tchg_alg_device *alg, enum tchg_idx chgidx,
				u32 mV);
extern int pe50_hal_set_ibusocp(struct tchg_alg_device *alg, enum tchg_idx chgidx,
				u32 mA);
extern int pe50_hal_set_vbatovp(struct tchg_alg_device *alg, enum tchg_idx chgidx,
				u32 mV);
extern int pe50_hal_set_ibatocp(struct tchg_alg_device *alg, enum tchg_idx chgidx,
				u32 mA);
extern int pe50_hal_set_vbatovp_alarm(struct tchg_alg_device *alg,
				      enum tchg_idx chgidx, u32 mV);
extern int pe50_hal_reset_vbatovp_alarm(struct tchg_alg_device *alg,
					enum tchg_idx chgidx);
extern int pe50_hal_set_vbusovp_alarm(struct tchg_alg_device *alg,
				      enum tchg_idx chgidx, u32 mV);
extern int pe50_hal_reset_vbusovp_alarm(struct tchg_alg_device *alg,
					enum tchg_idx chgidx);
extern int pe50_hal_get_adc(struct tchg_alg_device *alg, enum tchg_idx chgidx,
			    enum pe50_adc_channel chan, int *val);
extern int pe50_hal_get_soc(struct tchg_alg_device *alg, u32 *soc);
extern int pe50_hal_is_pd_adapter_ready(struct tchg_alg_device *alg);
extern int pe50_hal_set_ichg(struct tchg_alg_device *alg, bool enable,
			     u32 mA);
extern int pe50_hal_set_aicr(struct tchg_alg_device *alg, bool enable,
			     u32 mA);
extern int pe50_hal_get_ichg(struct tchg_alg_device *alg, enum tchg_idx chgidx,
			     u32 *mA);
extern int pe50_hal_get_aicr(struct tchg_alg_device *alg, enum tchg_idx chgidx,
			     u32 *mA);
extern int pe50_hal_is_vbuslowerr(struct tchg_alg_device *alg,
				  enum tchg_idx chgidx, bool *err);
extern int pe50_hal_get_adc_accuracy(struct tchg_alg_device *alg,
				     enum tchg_idx chgidx,
				     enum pe50_adc_channel chan, int *val);
extern int pe50_hal_get_vac1_status(struct tchg_alg_device *alg, enum tchg_idx chgidx, bool *online);
extern int pe50_hal_set_ibusucp_enable(struct tchg_alg_device *alg, enum tchg_idx chgidx,
			 bool enable);
extern int pe50_hal_init_chip(struct tchg_alg_device *alg, enum tchg_idx chgidx);
extern int pe50_hal_set_run_spec(struct tchg_alg_device *alg, enum tchg_idx chgidx, enum support_spec run_sepc);
extern int pe50_hal_get_run_spec(struct tchg_alg_device *alg, enum tchg_idx chgidx, enum support_spec run_sepc);
extern const char * pe50_hal_get_adapter_name(struct tchg_alg_device *alg);
extern int pe50_hal_is_chip_enabled(struct tchg_alg_device *alg, enum tchg_idx chgidx,bool *err);
extern int pe50_enable_special_function(struct tchg_alg_device *alg, enum tchg_idx chgidx, int function);

extern int pe50_hal_dump_register(struct tchg_alg_device *alg, enum tchg_idx chgidx);
extern int pe50_hal_init_adc(struct tchg_alg_device *alg, enum tchg_idx chgidx, bool en);
extern int pe5_hal_get_bypass_energy(struct pe50_algo_info *info);
extern int pe50_hal_set_tran_dev_prop(struct pe50_algo_info *info,enum pe50_tran_dev dev_name,enum tran_common_prop prop,const union com_propval *val);
extern void pe50_hal_ta_reset(struct tchg_alg_device *alg);



/* algo intf */
extern int pe50_select_vbat_cv(struct pe50_algo_info *info);
extern void pe50_ab_ibusucp_stop_algo(struct pe50_algo_info *info,int ab_dev);
extern int pe50_stop(struct pe50_algo_info *info, struct pe50_stop_info *sinfo);
extern int pe50_get_adc(struct pe50_algo_info *info, enum pe50_adc_channel chan,
			int *val);
extern u32 pe50_get_ita_pwr_lmt_by_vta(struct pe50_algo_info *info, u32 vta);
extern int pe50_set_ta_cap_cv(struct pe50_algo_info *info, u32 vta,
				     u32 ita);
extern int pe50_set_dvchg_charging(struct pe50_algo_info *info, bool en);
extern int pe50_set_dvchg_protection(struct pe50_algo_info *info, bool dual);
extern int pe50_enable_dvchg_charging(struct pe50_algo_info *info,
				      enum pe50_dvchg_role role, bool en);
extern int pe50_enable_swchg_charging(struct pe50_algo_info *info, bool en);
extern int pe50_algo_multi_dvchg_update(struct pe50_algo_info *info);
extern int pe50_enable_ta_charging(struct pe50_algo_info *info, bool en, int mV,
				   int mA);
extern int pe50_earily_restart(struct pe50_algo_info *info);
extern int pe50_algo_cal_r_info_with_ta_cap(struct pe50_algo_info *info,
					struct pe50_stop_info *sinfo);
extern void pe50_init_pps_algo_data(struct pe50_algo_info *info);
extern int pe50_pps_algo_init(struct pe50_algo_info *info);
extern int pe50_pps_algo_measure_r(struct pe50_algo_info *info);
extern int pe50_pps_algo_ss_dvchg(struct pe50_algo_info *info);
extern int pe50_pps_algo_cc_cv(struct pe50_algo_info *info);

static inline u32 precise_div(u64 dividend, u64 divisor)
{
	u64 _val = div64_u64(dividend << PRECISION_ENHANCE, divisor);

	return (u32)((_val + (1 << (PRECISION_ENHANCE - 1))) >>
		PRECISION_ENHANCE);
}

static inline u32 percent(u32 val, u32 percent)
{
	return precise_div((u64)val * percent, 100);
}

static inline u32 div1000(u32 val)
{
	return precise_div(val, 1000);
}

static inline u32 milli_to_micro(u32 val)
{
	return val * 1000;
}

static inline int micro_to_milli(int val)
{
	return (val < 0) ? -1 : div1000(val);
}

static inline enum tchg_idx to_chgidx(enum pe50_dvchg_role role)
{
	switch (role) {
	case PE50_DVCHG_MASTER:
		return DVCHG1;
	case PE50_DVCHG_SLAVE:
		return DVCHG2;
	case PE50_DVCHG_THIRD:
		return DVCHG3;
	default:
		PE50_ERR("error dvchg role:%d\n", role);
		return -ENODEV;
	}
}

static inline int pe50_check_tran_dev_ptr(struct tran_device **dev, const char *name)
{
	if (IS_ERR_OR_NULL(*dev)) {
		*dev = tran_get_by_name(name);
		if (IS_ERR_OR_NULL(*dev)) {
			pr_err("%s Couldn't get dev(%s)\n", __func__, name);
			return -EINVAL;
		}
	}

	return 0;
}

static inline u32 pe50_get_ita_tracking_max(u32 ita)
{
	return min(percent(ita, PE50_ITAOCP_RATIO),
		   (u32)(ita + PE50_ITA_TRACKING_GAP));
}

static inline void pe50_update_ita_gap(struct pe50_algo_info *info, u32 ita_gap)
{
	int i;
	u32 val = 0, avg_cnt = PE50_ITA_GAP_WINDOW_SIZE;
	struct pe50_algo_data *data = info->data;

	if (ita_gap < data->ita_gap_per_vstep)
		return;
	data->ita_gap_window_idx = (data->ita_gap_window_idx + 1) %
				   PE50_ITA_GAP_WINDOW_SIZE;
	data->ita_gaps[data->ita_gap_window_idx] = ita_gap;

	for (i = 0; i < PE50_ITA_GAP_WINDOW_SIZE; i++) {
		if (data->ita_gaps[i] == 0)
			avg_cnt--;
		else
			val += data->ita_gaps[i];
	}
	data->ita_gap_per_vstep = avg_cnt != 0 ? precise_div(val, avg_cnt) : 0;
}

/*
 * Calculate VBUS for divider charger
 * If divider charger is charging, the VBUS only needs to be 2 times of VOUT.
 */
static inline u32 pe50_vout2vbus(struct pe50_algo_info *info, u32 vout)
{
	struct pe50_algo_data *data = info->data;

	return (percent(vout, data->conversion_ratio * 100) + 400);
}

/*
 * Get output current and voltage measured by TA
 * and updates measured data
 */
static inline int pe50_get_ta_cap(struct pe50_algo_info *info)
{
	struct pe50_algo_data *data = info->data;
 
	return pe50_hal_get_ta_output(info->alg, &data->vta_measure,
				      &data->ita_measure);
}

/*
 * Get output current and voltage measured by TA
 * and updates measured data
 * If ta does not support measure capability, dvchg's ADC is used instead
 */
static inline int pe50_get_ta_cap_by_supportive(struct pe50_algo_info *info,
						 int *vta, int *ita)
{
	int ret;
	struct pe50_algo_data *data = info->data;
	struct pe50_ta_auth_data *auth_data = &data->ta_auth_data[data->running_spec];

	if (auth_data->support_meas_cap) {
		ret = pe50_get_ta_cap(info);
		if (ret < 0) {
			PE50_ERR("get ta cap fail(%d)\n", ret);
			return ret;
		}
		*vta = data->vta_measure;
		*ita = data->ita_measure;
		return 0;
	}
	ret = pe50_get_adc(info, PE50_ADCCHAN_VBUS, vta);
	if (ret < 0) {
		PE50_ERR("get vbus fail(%d)\n", ret);
		return ret;
	}
	return pe50_get_adc(info, PE50_ADCCHAN_IBUS, ita);
}

/* Calculate ibat from ita */
static inline u32 pe50_cal_ibat(struct pe50_algo_info *info, u32 ita)
{
	struct pe50_algo_data *data = info->data;

	return 2 * (data->is_swchg_en ? (ita - data->aicr_setting) : ita);
}

static inline int pe50_dvchg_online_cnt(struct pe50_algo_info *info)
{
	struct pe50_algo_data *data = info->data;
	int i;
	int cnt = 0;
	
	for (i = PE50_DVCHG_MASTER; i < PE50_DVCHG_MAX; i++) {
	        if (data->is_dvchg_exist[i] && data->is_dvchg_en[i])
	                cnt++;
	}
	
	return cnt;
}

#endif /* __TC_PE5_EVO_H */
