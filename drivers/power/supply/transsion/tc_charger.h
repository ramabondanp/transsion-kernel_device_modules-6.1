/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Transsion Inc.
 */

#ifndef __TC_CHARGER_H
#define __TC_CHARGER_H

#include <linux/power_supply.h>
#include <linux/alarmtimer.h>
#include <linux/version.h>
#include "tc_charger_class.h"
#include "tc_adapter_class.h"
#include "tc_algorithm_class.h"
#include "tc_common_class.h"
#include "tc_misc_intf.h"
#include "tc_voter.h"
#include "tc_tcpc.h"
#include "tc_gauge.h"

#define CHARGING_INTERVAL 		10
#define CHARGING_FULL_INTERVAL 20
#define CHARGING_ALG_CHECK_INTERVAL 	3
#define CHARGING_WIRELESS_INTERVAL 	6

#define CHRLOG_ERROR_LEVEL	1
#define CHRLOG_INFO_LEVEL		2
#define CHRLOG_DEBUG_LEVEL	3

//Add for pd test
#define USB_CURRENT_MASK 0x80000000
#define UNLIMIT_CURRENT_MASK 0x10000000

extern int tchr_get_debug_level(void);
#define tchr_err(fmt, args...)					\
do {								\
	if (tchr_get_debug_level() >= CHRLOG_ERROR_LEVEL) {	\
		pr_notice("[TC_CHG_THREAD]" fmt, ##args);				\
	}							\
} while (0)

#define tchr_info(fmt, args...)					\
do {								\
	if (tchr_get_debug_level() >= CHRLOG_INFO_LEVEL) {	\
		pr_notice_ratelimited("[TC_CHG_THREAD]" fmt, ##args);		\
	}							\
} while (0)

#define tchr_debug(fmt, args...)					\
do {								\
	if (tchr_get_debug_level() >= CHRLOG_DEBUG_LEVEL) {	\
		pr_notice("[TC_CHG_THREAD]" fmt, ##args);				\
	}							\
} while (0)

struct tc_charger;
struct charger_data;
#define BATTERY_CV 		4450000
#define V_CHARGER_MAX 		6500000 /* 6.5 V */
#define V_CHARGER_MIN 		4600000 /* 4.6 V */
#define DYNAMIC_MIVR_GAP	200000
#define BATTERY_EOC             200000 /* 200mA */

#define SDP_CHARGER_CURRENT			500000 /* 500mA */
#define SDP_INPUT_CURRENT			500000 /* 500mA */
#define CDP_CHARGER_CURRENT			1000000 /* 500mA */
#define CDP_INPUT_CURRENT			1000000 /* 500mA */
#define DCP_CHARGER_CURRENT			2000000
#define DCP_INPUT_CURRENT			2000000
#define NON_STD_AC_CHARGER_CURRENT		1200000
#define NON_STD_AC_INPUT_CURRENT		1200000

/* charging abnormal status */
#define CHG_VBUS_OV_STATUS	(1 << 0)
#define CHG_OC_STATUS			(1 << 2)
#define CHG_BAT_OV_STATUS		(1 << 3)
#define CHG_ST_TMO_STATUS		(1 << 4)
#define CHG_TYPEC_WD_STATUS	(1 << 6)

#define BAT_HT_STEP0_STATUS		(1 << 1)
#define BAT_HT_STEP1_STATUS		(1 << 11)
#define BAT_HT_STEP2_STATUS		(1 << 10)

#define BAT_LT_STEP0_STATUS		(1 << 5)
#define BAT_LT_STEP1_STATUS		(1 << 12)
#define BAT_LT_STEP2_STATUS		(1 << 7)
#define CHG_BAT_HT_EOC_STATUS     (1 << 14)

#define MAX_ALG_NO 10
#define MAX_ENV_LEN	2

#define RP_LEVEL_3A			3000
#define RP_LEVEL_1A5			1500
#define RP_LEVEL_0A5			500

#define BATTERY_HEALTH_FULL             100

#define BATT_TEMP_LIM_H   -10
#define BATT_TEMP_LIM_L   -20
#define BATT_HT_EOC_REC_TEMP   43

#define BATT_HT_SHUTDOWN_REC_TEMP	53
#define BATT_LT_SHUTDOWN_REC_TEMP	-16

enum sw_battery_id {
	BATTERY_ID_0 = 0,
	BATTERY_ID_1,
	BATTERY_ID_2,
	BATTERY_ID_3,
	BATTERY_ID_MAX,
};

enum {
	TRAN_NORMAL_FLAG = 0,
	TRAN_AGING_FLAG = 2,
	TRAN_AGING_HOT_IGNORE,
	TRAN_AGING_KOM,
	TRAN_STOP_CHARGING,
	TRAN_START_CHARGING,
};

enum charger_voltage_max {
	CHARGER_VOLTAGE_SWITCH_BASIC = 0,	//BASIC CHARGER
	CHARGER_VOLTAGE_WIRELESS_BPP = CHARGER_VOLTAGE_SWITCH_BASIC,
	CHARGER_VOLTAGE_SWITCH_FC,	//SWITCH CHARGER
	CHARGER_VOLTAGE_WIRELESS_EPP = CHARGER_VOLTAGE_SWITCH_FC,
	CHARGER_VOLTAGE_CP_FC,	//CP CHARGER
	CHARGER_VOLTAGE_WIRELESS_PE50, //WIRELESS CHARGER
	CHARGER_VOLTAGE_MAX,
};

enum vbus_measure_method_enum {
	MEASURE_BY_PMIC = 0,
	MEASURE_BY_CP,
	MEASURE_BY_SWITCH,
	MEASURE_BY_OTHER,
	MEASURE_METHOD_MAX,
};

enum dynamic_mivr_enum {
	DYNAMIC_MIVR_STEP_0 = 0,
	DYNAMIC_MIVR_STEP_1,
	DYNAMIC_MIVR_STEP_2,
	DYNAMIC_MIVR_STEP_3,
	DYNAMIC_MIVR_MAX,
};

enum batt_notify_ht_dis_chg {
	BATT_HT_DIS_CHG_STEP0 = 0,
	BATT_HT_DIS_CHG_STEP1,
	BATT_HT_DIS_CHG_STEP2,
	BATT_HT_DIS_CHG_MAX,
};

enum batt_notify_lt_dis_chg {
	BATT_LT_DIS_CHG_STEP0 = 0,
	BATT_LT_DIS_CHG_STEP1,
	BATT_LT_DIS_CHG_STEP2,
	BATT_LT_DIS_CHG_MAX,
};

enum batt_notify_ht {
	BATT_HT_STEP0 = 0,
	BATT_HT_STEP1,
	BATT_HT_STEP2,
	BATT_HT_MAX,
};

enum batt_notify_lt {
	BATT_LT_STEP0 = 0,
	BATT_LT_STEP1,
	BATT_LT_STEP2,
	BATT_LT_MAX,
};

enum chg_dev_notifier_events {
	EVENT_FULL,
	EVENT_RECHARGE,
	EVENT_DISCHARGE,
	EVENT_PLUG_IN,
	EVENT_PLUG_OUT,
	EVENT_MAX,
};

enum charger_level {
	TC_LEVEL_BELOW_T1 = 0,
	TC_LEVEL_T1,
	TC_LEVEL_T2,
	TC_LEVEL_T3,
	TC_LEVEL_T4,
	TC_LEVEL_T5,
	TC_LEVEL_T6, //normal jeita temp (15, 45)
	TC_LEVEL_T7,
	TC_LEVEL_T8,
	TC_LEVEL_T9,
	TC_LEVEL_T10,
	TC_LEVEL_T11,
	TC_LEVEL_MAX,
};

enum batt_temp_level {
	BATT_TEMP_LOW = 0,
	BATT_TEMP_HIGH,
	BATT_TEMP_MAX,
};

enum batt_vbat_level {
	BATT_VBAT_LOW = 0,
	BATT_VBAT_MID1,
	BATT_VBAT_MID2,
	BATT_VBAT_MID3,
	BATT_VBAT_MID4,
	BATT_VBAT_HIGH,
	BATT_VBAT_MAX,
};

enum eoc_scheme_enum {
	HW_EOC = 0,
	SW_EOC,
};

enum {
	BAT_HEALTH_LEVEL_EXCELLENT = 0,
	BAT_HEALTH_LEVEL_GOOD,
	BAT_HEALTH_LEVEL_NORMAL,
	BAT_HEALTH_LEVEL_ORDINARY,
	BAT_HEALTH_LEVEL_MAX,
};

enum {
	TC_NORMAL_CHARGING = 0,
	TC_ONLY_POWERPATH,
	TC_NOT_CHARGING,
};

enum chg_enum {
	SW_CHG1 = 0,
	SW_CHG2,
	SW_CHG_MAX,
};

static const char *const chg_name[CHG_MAX] = {
	[SW_CHG1] = "SW_CHG1",
	[SW_CHG2] = "SW_CHG2",
};


enum si_bat_cycle {
	SI_BAT_CYCLE_0 = 0,
	SI_BAT_CYCLE_1,
	SI_BAT_CYCLE_2,
	SI_BAT_CYCLE_MAX,
};

enum charger_type {
	CHARGER_UNKNOWN = 0,
	STANDARD_HOST,		/* USB : 450mA */
	CHARGING_HOST,
	NONSTANDARD_CHARGER,	/* AC : 450mA~1A */
	STANDARD_CHARGER,	/* AC : ~1A */
	APPLE_2_1A_CHARGER, /* 2.1A apple charger */
	APPLE_1_0A_CHARGER, /* 1A apple charger */
	APPLE_0_5A_CHARGER, /* 0.5A apple charger */
	WIRELESS_CHARGER,
	DV2_CHARGER,
	PE_CHARGER,
	USB_OTG,
};

struct tc_charger_algorithm {
	int (*do_algorithm)(struct tc_charger *info);
	int (*do_powerpath)(struct tc_charger *info);
	int (*enable_charging)(struct tc_charger *info, bool en);
	int (*do_chg1_event)(struct notifier_block *nb, unsigned long ev, void *v);
	int (*do_chg2_event)(struct notifier_block *nb, unsigned long ev, void *v);
	int (*do_dvchg1_event)(struct notifier_block *nb, unsigned long ev,void *v);
	int (*do_dvchg2_event)(struct notifier_block *nb, unsigned long ev,void *v);
	int (*do_dvchg3_event)(struct notifier_block *nb, unsigned long ev,void *v);
	int (*change_current_setting)(struct tc_charger *info);
	void *algo_data;
};

enum batt_cycle_cnt {
	BATT_CYCLE_CNT_LOW = 0,
	BATT_CYCLE_CNT_HIGH,
	BATT_CYCLE_CNT_MAX,
};

struct batt_desc {
	int temp[BATT_TEMP_MAX];
	int ffc_status;
	int vbat[BATT_VBAT_MAX];
	int ibat[BATT_VBAT_MAX];
	int eoc;
	int cycle_cnt[BATT_CYCLE_CNT_MAX];
	int rechg_gap;
};

struct charger_data {
	bool chg_en;
	bool hiz;
	bool term_en;
	int eoc;
	int cv;
	int mivr;
	int input_current_limit;
	int charging_current_limit;
};

enum alg_id_enum {
	ALG_NONE = 0,
	PE5_ID,
	PDC_ID,
	PE2_ID,
	ALG_MAX,
};

static char * const alg_name_array[] = {
	[ALG_NONE]   =     "none",
	[PE5_ID]     =     "pe5",
	[PDC_ID]     =     "pd",
	[PE2_ID]     =     "pe20",
};

enum feature_id_enum {
	BYPASS_CHG = 0,
	SMART_CHG,
	FEATURE_MAX,
};

struct feature_status {
	enum feature_id_enum idx;
	int value;
	bool changed;
};

enum tc_chg_speed {
	TRAN_MULTI_SPEED_HIGH = 1,
	TRAN_MULTI_SPEED_MID,
	TRAN_MULTI_SPEED_LOW,
	TRAN_MULTI_SPEED_MAX,
};

enum tc_chg_speed_owner {
	TRAN_MULTI_OWNER_SYS = 1,
	TRAN_MULTI_OWNER_ANI,
	TRAN_MULTI_OWNER_MAX,
};

enum voter_enum {
	VOTER_WATER_DETECT = 0,
	VOTER_AI_CHARGER,
	VOTER_BYPASS_CHARGER,
	VOTER_ADAPTER_CONTROL,
	VOTER_SW_VBUS_OVP,
	VOTER_HW_VBUS_OVP,
	VOTER_BATT_HIGH_TEMP,
	VOTER_BATT_LOW_TEMP,
	VOTER_BATT_HIGH_TEMP_EOC,
	VOTER_SAFETY_TIMER,
	VOTER_MONKEY,
	VOTER_CMD,
	VOTER_PORT_BURN,
	VOTER_TRAN_CUSTOM,
	VOTER_MAX,
};

static const char * const VOTER_TEXT[] = {
	[VOTER_WATER_DETECT]         = "water_detect",
	[VOTER_AI_CHARGER]           = "ai_charger",
	[VOTER_BYPASS_CHARGER]		 = "bypass_charger",
	[VOTER_ADAPTER_CONTROL]      = "adapter_control",
	[VOTER_SW_VBUS_OVP]          = "sw_vbus_ovp",
	[VOTER_HW_VBUS_OVP]          = "hw_vbus_ovp",
	[VOTER_BATT_HIGH_TEMP]       = "batt_high_temp",
	[VOTER_BATT_LOW_TEMP]        = "batt_low_temp",
	[VOTER_BATT_HIGH_TEMP_EOC]   = "batt_high_temp_eoc",
	[VOTER_SAFETY_TIMER]         = "safety_timer",
	[VOTER_MONKEY]               = "monkey",
	[VOTER_CMD]                  = "cmd",
	[VOTER_PORT_BURN]            = "port_burn",
	[VOTER_TRAN_CUSTOM]          = "tran_custom",
};

enum tran_chg_type {
	TRAN_USB_TYPE_UNKNOWN = 0,
	TRAN_USB_TYPE_SDP,
	TRAN_USB_TYPE_DCP,
	TRAN_USB_TYPE_CDP,
	TRAN_USB_TYPE_TC30,
	TRAN_USB_TYPE_PE,
	TRAN_USB_TYPE_PE2,
	TRAN_USB_TYPE_PE4,
	TRAN_USB_TYPE_HVDCP,
	TRAN_USB_TYPE_PDC,
	TRAN_USB_TYPE_PD_PPS,
	TRAN_USB_TYPE_RFC,
	TRAN_USB_TYPE_NONSTAND,
	TRAN_USB_TYPE_WIRELESS,
	TRAN_USB_TYPE_WIRELESS_PE50,
	TRAN_USB_TYPE_MAX,
};

static const char * const TRAN_USB_TYPE_TEXT[] = {
	[TRAN_USB_TYPE_UNKNOWN]		= "Unknown",
	[TRAN_USB_TYPE_SDP]		= "SDP",
	[TRAN_USB_TYPE_DCP]		= "DCP",
	[TRAN_USB_TYPE_CDP]		= "CDP",
	[TRAN_USB_TYPE_TC30]		= "TC30",
	[TRAN_USB_TYPE_PE]	         	= "PE",
	[TRAN_USB_TYPE_PE2]		= "PE2",
	[TRAN_USB_TYPE_PE4]		= "PE4",
	[TRAN_USB_TYPE_HVDCP]		= "HVDCP",
	[TRAN_USB_TYPE_PDC]		= "PD",
	[TRAN_USB_TYPE_PD_PPS]		= "PD_PPS",
	[TRAN_USB_TYPE_RFC]		= "RFC",
	[TRAN_USB_TYPE_NONSTAND]		= "NONSTAND",
	[TRAN_USB_TYPE_WIRELESS]		= "WIRELESS",
	[TRAN_USB_TYPE_WIRELESS_PE50]		= "WIRELESS_PE50",

};

struct tc_charge_vote {
	bool chg_hiz;
	bool discharge;
	bool icon_disappeared;
	enum voter_enum voter;
};

struct dual_chg_table {
	u32 total_ichg;
	u32 secondary_ichg;
};

enum chg_data_idx_enum {
	CHG1_SETTING,
	CHG2_SETTING,
	DVCHG1_SETTING,
	DVCHG2_SETTING,
	CHGS_SETTING_MAX,
};

enum smtchg_state_enum {
	SMTCHG_DISABLE = 0,
	SMTCHG_ENABLE = 1,
};

enum smtchg_energy_enum{
	SMTCHG_ENERGY_CLOSE = 0,
	SMTCHG_ENERGY_30 = 30,
	SMTCHG_ENERGY_50 = 50,
	SMTCHG_ENERGY_100 = 100,
	SMTCHG_ENERGY_MIN = 101
};

enum bypass_setting_enum {
	BYPASS_DISABLE = 0,
	BYPASS_ENABLE = 110,
};

enum bypass_energy_enum{
	BYPASS_ENERGY_CLOSE = 0,
	BYPASS_MIN_ENERGY = 1,
	BYPASS_ENERGY_30 = 30,
	BYPASS_ENERGY_50 = 50,
	BYPASS_ENERGY_100 = 100,
};

enum aichg_state_enum {
	AICHG_DISABLED = 0,
	AICHG_ENABLE = 1,
};

enum tran_custom_state_enum {
	TRAN_CUSTOM_DISCHG = 0,
	TRAN_CUSTOM_CHG = 1,
};

enum ai_chgtime_idx {
	TIME_BEGIN,
	TIME_END,
	TIME_DIFF,
	TIME_MAX
};

enum attach_type {
	ATTACH_TYPE_NONE,
	ATTACH_TYPE_PWR_RDY,
	ATTACH_TYPE_TYPEC,
	ATTACH_TYPE_PD,
	ATTACH_TYPE_PD_SDP,
	ATTACH_TYPE_PD_DCP,
	ATTACH_TYPE_PD_NONSTD,
	ATTACH_TYPE_MAX,
};

#define ONLINE(idx, attach)		((idx & 0xf) << 4 | (attach & 0xf))
#define ONLINE_GET_IDX(online)		((online >> 4) & 0xf)
#define ONLINE_GET_ATTACH(online)	(online & 0xf)

#define BYPASS_CHARGING_DEFAULT_AICR	2000000
#define AI_CHARGING_DEFAULT_AICR	2000000



struct bypass_chg_data {
	bool bypass_en;
	int bypass_energy;
};

struct smart_chg_data {
	struct kobject *smtchg_kobj;
	bool smartchg_en;
	int smartchg_energy;
};

struct ai_chg_data {
	int ai_dischg;
	/*AI bigdate*/
	struct timespec64 charging_time;
	ktime_t aichg_time[TIME_MAX];
	unsigned int upload_tims;
	int last_soc_tid;
	bool pass_80_soc;
	int upload_current;
	int real_upload_current;
};

struct real_soc_decimal_data {
	struct alarm charge_decimal_timeout_timer;
	struct alarm decimal_timer;
	int raw_soc;
	struct tran_device *tran_batt_dev;
	atomic_t wakeup_decimal_thread;
	atomic_t start_decimal_soc;
	wait_queue_head_t decimal_wq;
	struct wakeup_source *decimal_soc_wakelock;
};

struct tc_desc {
	u32 sdp_charger_current;
	u32 sdp_input_current;
	u32 cdp_charger_current;
	u32 cdp_input_current;
	u32 dcp_charger_current;
	u32 dcp_input_current;
	u32 nonstd_charger_current;
	u32 nonstd_input_current;
	u32 min_charger_current;
	u32 wireless_min_charger_current;
	u32 max_charging_current_limit;
	u32 max_input_current_limit;

	u32 min_charger_voltage;
	u32 charger_voltage_ovp[CHARGER_VOLTAGE_MAX];
	u32 vbus_measure_method[MEASURE_METHOD_MAX];
	u32 dynamic_mivr_vol[DYNAMIC_MIVR_MAX];

	bool support_dual_switch;
	struct dual_chg_table *chg_tab;
	u32 chg_tab_len;
	
	const char **support_alg;	/* supported alg name */
	u32 support_alg_cnt;		/* supported ta count */
	bool charger_unlimited;
	bool disable_charger;
	bool enable_sw_jeita;
	bool enable_dynamic_mivr;
	bool enable_hv_charging;
	bool enable_sw_safety_timer;
	bool enable_ir_comp;
	bool support_hardware_ir_comp;
	bool support_dual_battery;
	bool support_reset_eoc;
	bool support_real_soc_decimal;
	bool disable_high_temp_lock_uisoc;
	bool low_temp_err_limit;
	u32 low_temp_err_limit_input;
	u32 dsc_aicr_min;
	u32 dsc_ichg_min;
	u32 r_comp;
	u32 v_comp_max;
	u32 master_r_comp;
	u32 master_v_comp_max;
	u32 slave_r_comp;
	u32 slave_v_comp_max;
	u32 max_charging_time;
	u32 dual_batt_eoc_delay_time;
	u32 smooth_soc_full_vol;
	struct batt_desc *master_batt;
	int master_batt_level;
	struct batt_desc *slave_batt;
	int slave_batt_level;
	struct batt_desc *single_batt;
	int single_batt_level;
	int vbat_gap[BATT_VBAT_MAX];

	int aging_start_uisoc;
	int aging_stop_uisoc;
	int kom_start_uisoc;
	int kom_stop_uisoc; 

	int tpa_temp[TC_LEVEL_MAX];
	int tpa_ichg[TC_LEVEL_MAX];
	u32 tpa_temp_gap;
	
	int tpcb_temp[TC_LEVEL_MAX];
	int tpcb_ichg[TC_LEVEL_MAX];
	u32 tpcb_temp_gap;
	
	int tbat_temp[TC_LEVEL_MAX];
	int tbat_ichg[TC_LEVEL_MAX];
	u32 tbat_temp_gap;

	struct tc_charge_vote batt_ht_vote[BATT_HT_MAX];
	struct tc_charge_vote batt_lt_vote[BATT_LT_MAX];
	int batt_ht_temp[BATT_HT_MAX];
	u32 batt_ht_code[BATT_HT_MAX];
	int batt_ht_rec_temp[BATT_HT_MAX];
	int batt_lt_temp[BATT_LT_MAX];
	u32 batt_lt_code[BATT_LT_MAX];
	int batt_lt_rec_temp[BATT_LT_MAX];

	int batt_ht_temp_dis_chg[BATT_HT_DIS_CHG_MAX];
	u32 batt_ht_code_dis_chg[BATT_HT_DIS_CHG_MAX];
	int batt_lt_temp_dis_chg[BATT_LT_DIS_CHG_MAX];
	u32 batt_lt_code_dis_chg[BATT_LT_DIS_CHG_MAX];
	int batt_over_heat_temp;
	int batt_over_cold_temp;
	int log_level;
	int	ftm_cp_num;

	int sw_eoc_cv_gap;
	int recharger_gap;
	int sw_eoc_cnt_time;
	bool enable_ffc_ir_comp;

	bool support_long_life_recharger;
	u32 long_life_rechg_time;
	u32 bat_health_cycle_base;
	u32 bat_health_dec_times;
	u32 bat_level_def[BAT_HEALTH_LEVEL_MAX];
	u32 bat_level_code[BAT_HEALTH_LEVEL_MAX];

	int smtchg_curr_min;

	u32 decimal_report_freq;
	u32 charge_decimal_timeout_time;
	int swchg_hw_eoc_max;

 	bool support_si_bat;
	u32 si_bat_cycle_def[SI_BAT_CYCLE_MAX];
	u32 si_bat_cycle_vol[SI_BAT_CYCLE_MAX];

	bool battery_health_not_support;
	int base_year;
};

struct date_struct {
	char year;
	char mon;
	short date;
};

struct tc_data {
	struct tran_device *tc_charger_dev;
	struct tran_device *tran_batt_dev;
	struct tran_device *ac_ctl_dev;
	struct tran_device *gauge_dev;
	struct tran_properties tc_charger_props;
	struct tchg_alg_device *running_algo;
	struct charger_device *chg1_dev;
	struct notifier_block chg1_nb;
	struct charger_device *chg2_dev;
	struct notifier_block chg2_nb;
	struct charger_device *dvchg1_dev;
	struct notifier_block dvchg1_nb;
	struct charger_device *dvchg2_dev;
	struct notifier_block dvchg2_nb;
	struct charger_device *dvchg3_dev;
	struct notifier_block dvchg3_nb;
	struct charger_device *wlsc_dev;
	struct tadapter_device *pd_adapter;
	struct notifier_block pd_nb;
	struct power_supply  *chg_psy;
	struct srcu_notifier_head evt_nh;
	struct notifier_block pm_notifier;
	struct notifier_block psy_nb;
	struct delayed_work psy_misc_work;
	struct delayed_work wait_protocol_work;
	struct wakeup_source *charger_wakelock;
	struct mutex notify_lock;
	struct mutex cable_out_lock;
	struct mutex charger_lock;
	struct mutex pd_lock;
	struct mutex wait_protocol_lock;
	struct tc_charger *info;
	struct tchg_alg_device *alg[MAX_ALG_NO];
	struct charger_data chg_data[CHGS_SETTING_MAX];
	struct charger_data total_pdata;
	struct votable *chg1_hiz_vote;
	struct votable *chg2_hiz_vote;
	struct votable *chg1_disable_vote;
	struct votable *chg2_disable_vote;
	struct votable *chg1_mivr_vote;
	struct votable *chg2_mivr_vote;
	struct votable *total_aicr_vote;
	struct votable *chg1_aicr_vote;
	struct votable *chg2_aicr_vote;
	struct votable *total_ichg_vote;
	struct votable *chg1_ichg_vote;
	struct votable *chg2_ichg_vote;
	struct votable *sw_vbus_ovp_vote;
	spinlock_t slock;
	wait_queue_head_t  wait_que;
	u32 polling_interval;

	struct notifier_block chg_alg_nb;
	struct tc_charge_vote charge_vote[VOTER_MAX];
	struct tc_charge_vote vote_result;
	struct timespec64 charging_begin_time;
	struct timespec64 total_time;

	struct feature_status  wait_protocol_array[FEATURE_MAX];

	struct notifier_block gauge_notifier;

	enum charger_level tbat_level;
	enum charger_level tpcb_level;
	enum charger_level tpa_level;

        // NORMAL_BOOT = 0, META_BOOT = 1, RECOVERY_BOOT = 2, SW_REBOOT = 3,
        // FACTORY_BOOT = 4, ADVMETA_BOOT = 5, ATE_FACTORY_BOOT = 6, ALARM_BOOT = 7,
        // KERNEL_POWER_OFF_CHARGING_BOOT = 8, LOW_POWER_OFF_CHARGING_BOOT = 9,
        // DONGLE_BOOT = 10

	u32 bootmode;
	u32 boottype;
	int state;
	int monkey_flag;
	int batt_id;
	int cmd_discharging;
	int max_charger_voltage;
	int cable_out_cnt;
	int memtest_current;
	int battery_temp;
	int chr_type;
	int usb_type;
	int alias_type;
	int pd_type;
	int curr_ratio;
	bool kpoc;
	bool pd_reset;
	bool is_charging;
	bool is_charger_on;
	bool atm_enabled;
	bool plug_in;
	bool support_dual_switch;
	bool wait_protocol_done;
	bool high_temp_mode;
	bool is_low_cv_status;
	/* alarm timer */
	struct alarm charger_timer;
	struct timespec64 endtime;
	bool is_suspend;
	bool is_chg_done;
	bool ffc_eoc_done;
	atomic_t sw_eoc_cnt;
	bool try_dsc;
	ktime_t timer_cb_duration[8];
	/* thread related */
	bool charger_thread_timeout;
	bool charger_thread_polling;
	/* battery warning */
	unsigned int notify_code;
	unsigned int reported_code;
	unsigned int report_code;
	int bat_warning_cmd;

	ktime_t uevent_time_check;

	struct smart_chg_data smtchg_data;
	struct ai_chg_data aichg_data;
	struct bypass_chg_data bypasschg_data;
	struct real_soc_decimal_data *real_soc_data;

	struct alarm long_life_rechg_timer;
	bool long_life_rechg_work_flag;
	bool long_life_rechg_done;
	bool trigger_long_life_rechg_timer;
	bool upload_msg_data_doing;
	struct delayed_work upload_msg_work;

	/* dual batt */
	struct tran_device *fg_a_dev;
	struct tran_device *fg_b_dev;
	struct alarm dual_batt_eoc_timer;
	bool dual_batt_enable_eoc;
	int batt_online_status;
	int fg_a_vbat;
	int fg_a_ibat;
	int fg_a_temp;
	int fg_a_status;
	int fg_b_vbat;
	int fg_b_ibat;
	int fg_b_temp;
	int fg_b_status;

	int master_chg_index;
	int master_batt_level_index;
	int master_batt_vbat[BATT_VBAT_MAX];
	int master_batt_ibat[BATT_VBAT_MAX];
	int master_vbat_cv;
	int master_vbat_cc;
	int master_vbat_eoc;

	int slave_chg_index;
	int slave_batt_level_index;
	int slave_batt_vbat[BATT_VBAT_MAX];
	int slave_batt_ibat[BATT_VBAT_MAX];
	int slave_vbat_cv;
	int slave_vbat_cc;
	int slave_vbat_eoc;

	int battery_cycle;
	int single_chg_index;
	int single_batt_level_index;
	int single_batt_vbat[BATT_VBAT_MAX];
	int single_batt_ibat[BATT_VBAT_MAX];
	int vbat_cv;
	int vbat_cc;
	int vbat_eoc;
	int vbat_cv_term;
	int long_life_rechg_cv_gap;

	struct date_struct mac_date;
	struct date_struct active_date;
	bool bat_active;
	bool bat_active_reset;
	int ic_err;

	int chg_speed;
	int chg_speed_old;
	int speed_owner;
	int speed_owner_old;

	struct class *charger_class;
	struct cdev *charger_cdev;
	int charger_major;
	dev_t charger_devno;


	int is_ffc;
	int ffc_alg_id;
	u32 eoc_scheme;
	int sw_eoc_cur;

	int pe5_rechg_cv_gap;
};

struct tc_charger {
	struct platform_device *pdev;
	struct tc_desc *desc;
	struct tc_data *data;
	/* tc basic function pointer */
	struct tc_charger_algorithm algo;
};


static inline int _uA_to_mA(int uA)
{
	if (uA == -1)
		return -1;
	else
		return uA / 1000;
}


static inline int tc_chg_alg_notify_call(struct tc_charger *info,
					  enum tchg_alg_notifier_events evt,int value)
{
	int i;
	struct tc_data *data = info->data;
	struct tchg_alg_notify notify = {
		.evt = evt,
		.value = value,
	};

	for (i = 0; i < MAX_ALG_NO; i++) {
		if (data->alg[i])
			tchg_alg_notifier_call(data->alg[i], &notify);
	}

	return 0;
}

static inline int tc_chg_alg_plugout_reset(struct tc_charger *info)
{
	int i;
	struct tc_data *data = info->data;

	for (i = 0; i < MAX_ALG_NO; i++) {
		if (IS_ERR_OR_NULL(data->alg[i]))
			continue;
			
		tchg_alg_plugout_reset(data->alg[i]);
	}

	return 0;
}

/* procfs */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#define PROC_FOPS_RW(name)						\
static int tc_chg_##name##_open(struct inode *node, struct file *file)	\
{									\
	return single_open(file, tc_chg_##name##_show, pde_data(node));\
}									\
static const struct proc_ops tc_chg_##name##_fops = {		\
	.proc_open =  tc_chg_##name##_open,			\
	.proc_read = seq_read,						\
	.proc_lseek = seq_lseek,						\
	.proc_release = single_release,					\
	.proc_write = tc_chg_##name##_write,			\
}
#else
#define PROC_FOPS_RW(name)						\
static int tc_chg_##name##_open(struct inode *node, struct file *file)	\
{									\
	return single_open(file, tc_chg_##name##_show, pde_data(node));\
}									\
static const struct file_operations tc_chg_##name##_fops = {	\
	.owner = THIS_MODULE,						\
	.open = tc_chg_##name##_open,					\
	.read = seq_read,						\
	.llseek = seq_lseek,						\
	.release = single_release,					\
	.write = tc_chg_##name##_write,			\
}
#endif

/* functions which framework needs*/
extern int tc_charger_check_psy_ptr(struct power_supply **psy, const char *name);
extern int tc_charger_check_tran_dev_ptr(struct tran_device **dev, const char *name);
extern int tc_basic_charger_init(struct tc_charger *info);
extern int get_ibat(struct tc_charger *info);
extern int get_ibus(struct tc_charger *info);
extern bool is_charger_exist(struct tc_charger *info);
extern int get_charger_temperature(struct tc_charger *info,struct charger_device *chg);
extern int get_charger_charging_current(struct tc_charger *info,struct charger_device *chg);
extern int get_charger_input_current(struct tc_charger *info,struct charger_device *chg);
extern int get_charger_zcv(struct tc_charger *info,struct charger_device *chg);
extern int set_icon_disappeared(struct tc_charger *info, bool enable);
extern void _wake_up_charger(struct tc_charger *info);
/* functions for other */
extern int tc_get_fast_charger_type(struct tc_charger *info);
extern void tc_get_fast_charger_limit(struct tc_charger *info, struct charger_data *pdata);
extern void tc_update_running_algo(struct tc_charger *info,
		struct tchg_alg_device **running_algo);
extern void tc_chg_switch_vbus_ovp(int idx, const char *name, bool enable);
extern int tc_charger_setup_files(struct platform_device *pdev);
extern int tc_chgstat_notify(struct tc_charger *info);
extern int tc_chgstat_pump(struct tc_charger *info);
extern void tc_charger_cmd_vote(struct tc_charger *info, bool discharge);
extern void tc_charger_sw_vbus_ovp_vote(struct tc_charger *info, bool discharge);
extern void tc_charger_batt_lt_vote(struct tc_charger *info, bool discharge);
extern void tc_charger_batt_ht_vote(struct tc_charger *info, bool discharge);
extern bool try_dual_switch_check(struct tc_charger *info);
extern void enable_primary_charger(struct tc_charger *info, bool enable);
extern void enable_secondary_charger(struct tc_charger *info, bool enable);
extern void tc_start_alarm_recharger_timer(struct tc_charger *info);
extern int tc_chgstat_chgspeed(struct tc_charger *info);
extern void tc_charger_dump_key_info(struct tc_charger *info, enum chg_enum idx);
#endif /* __TC_CHARGER_H */
