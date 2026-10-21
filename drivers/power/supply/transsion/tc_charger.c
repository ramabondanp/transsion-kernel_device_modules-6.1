// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Transsion Inc.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeup.h>
#include <linux/rtc.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/reboot.h>

#include <asm/setup.h>
#include "tc_charger.h"

#define MS_TO_NS(msec)		((msec) * (NSEC_PER_MSEC))
#define IS_MULTI_BIT(x) (((x) != 0) && (((x) & ((x) - 1)) != 0))

static struct tc_charger *pinfo;
static bool charger_init_algo(struct tc_charger *info);
extern void tc_start_dual_batt_eoc_timer(struct tc_charger *info);

struct tc_desc default_info = {
	.charger_unlimited = false,
	.enable_sw_safety_timer = false,
	.enable_sw_jeita = true,
	.enable_dynamic_mivr = false,
	.enable_hv_charging = true,
	.enable_ir_comp = false,
	.support_dual_battery = false,
	.support_hardware_ir_comp = false,
	.support_dual_switch = false,
	.support_reset_eoc = false,
	.support_long_life_recharger = false,
	.support_si_bat = false,
	.low_temp_err_limit = false,
	.low_temp_err_limit_input = 100000,
	.dsc_aicr_min = 1000000,
	.dsc_ichg_min = 1400000,
	.sw_eoc_cv_gap = 15000, // 15mV
	.recharger_gap = 100000, // 100mV
	.sw_eoc_cnt_time = 2,
	.r_comp = 30,
	.v_comp_max = 0,
	.master_r_comp = 30,
	.master_v_comp_max = 0,
	.slave_r_comp = 30,
	.slave_v_comp_max = 0,
	.log_level = CHRLOG_DEBUG_LEVEL,
	.smtchg_curr_min = 500000,
	.min_charger_voltage = 4400000,
	.min_charger_current = 1000000,
	.wireless_min_charger_current = 800000,
	.max_charging_current_limit = 3000000,
	.max_input_current_limit = 3000000,
	.dual_batt_eoc_delay_time = 15,

	.ftm_cp_num = 1,

	.tbat_temp = {-100, -40, -30, -20, -10, 0, 15, 45, 55, 60, 65, 70},
	.tbat_ichg = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	.tbat_temp_gap = 1,

	.tpcb_temp = {-100, 15, 30, 35, 37, 39, 41, 43, 44, 45, 55, 60},
	.tpcb_ichg = {0, 0, 0, 200000, 500000, 700000, 1000000, 1500000, 1800000, 2000000, 2000000, 2000000},
	.tpcb_temp_gap = 1,

	.tpa_temp = {-100, 15, 30, 35, 37, 39, 41, 45, 55, 60, 65, 70},
	.tpa_ichg = {0, 0, 0, 200000, 300000, 500000, 700000, 900000, 1300000, 1500000, 2000000, 2000000},
	.tpa_temp_gap = 1,

	.batt_ht_temp = {53, 55, 58},
	.batt_ht_code = {BAT_HT_STEP0_STATUS, BAT_HT_STEP1_STATUS, BAT_HT_STEP2_STATUS},
	.batt_lt_temp = {2, 0, -18},
	.batt_lt_code = {BAT_LT_STEP0_STATUS, BAT_LT_STEP1_STATUS, BAT_LT_STEP2_STATUS},
	.batt_ht_temp_dis_chg = {58, 58, 58},
	.batt_ht_code_dis_chg = {BAT_HT_STEP2_STATUS, BAT_HT_STEP2_STATUS, BAT_HT_STEP2_STATUS},
	.batt_lt_temp_dis_chg = {-18, -18, -18},
	.batt_lt_code_dis_chg = {BAT_LT_STEP2_STATUS, BAT_LT_STEP2_STATUS, BAT_LT_STEP2_STATUS},
	.sdp_charger_current = SDP_CHARGER_CURRENT,
	.sdp_input_current = SDP_INPUT_CURRENT,
	.cdp_charger_current = CDP_CHARGER_CURRENT,
	.cdp_input_current = CDP_INPUT_CURRENT,
	.dcp_charger_current = DCP_CHARGER_CURRENT,
	.dcp_input_current = DCP_INPUT_CURRENT,
	.nonstd_charger_current = NON_STD_AC_CHARGER_CURRENT,
	.nonstd_input_current = NON_STD_AC_INPUT_CURRENT,
	.dynamic_mivr_vol = {4400000, 4400000, 4400000, 4600000},
	.charger_voltage_ovp = {6500000, 10500000, 11000000, 11000000},
	.vbus_measure_method = {MEASURE_BY_PMIC, MEASURE_BY_PMIC, MEASURE_BY_PMIC, MEASURE_BY_CP},
	.max_charging_time = 72000,
	.aging_start_uisoc = 70,
	.aging_stop_uisoc = 75,
	.kom_start_uisoc = 40,
	.kom_stop_uisoc = 70,
	.batt_over_heat_temp = 60,
	.batt_over_cold_temp = -20,
	.long_life_rechg_time = 180,
	.bat_health_cycle_base = 300,
	.bat_health_dec_times = 60,
	.charge_decimal_timeout_time = 20,
	.decimal_report_freq = 200,
	.smooth_soc_full_vol = 4450000,
	.bat_level_def = {400, 700, 1000, 99999},
	.bat_level_code = {4, 3, 2, 1},
	.master_batt_level = 2,
	.slave_batt_level = 2,
	.single_batt_level = 2,
	.vbat_gap = {40000, 40000, 40000, 40000, 40000, 40000},
	.swchg_hw_eoc_max = 800000,
	.si_bat_cycle_def = {100, 200, 99999},
	.si_bat_cycle_vol = {3000, 3100, 3200},
};

#define TC_DT_VALPROP_ARR(name, sz) \
	{#name, offsetof(struct tc_desc, name), sz}

#define TC_DT_VALPROP(name) \
	TC_DT_VALPROP_ARR(name, 1)

struct tc_dtprop {
	const char *name;
	size_t offset;
	size_t sz;
};

static inline void tc_parse_dt_bool(struct device_node *np, void *desc,
				    const struct tc_dtprop *props,
				    int prop_cnt)
{
	int i;

	for (i = 0; i < prop_cnt; i++) {
		if (unlikely(!props[i].name))
			continue;
		*(bool *)(desc + props[i].offset) = of_property_read_bool(np, props[i].name);
	}
}

static inline void tc_parse_dt_u32(struct device_node *np, void *desc,
				    const struct tc_dtprop *props,
				    int prop_cnt)
{
	int i;

	for (i = 0; i < prop_cnt; i++) {
		if (unlikely(!props[i].name))
			continue;
		of_property_read_u32(np, props[i].name, desc + props[i].offset);
	}
}

static inline int __of_property_read_s32(const struct device_node *np,
					       const char *propname,
					       s32 *out_values)
{
	return of_property_read_u32(np, propname, (u32 *)out_values);
}

static inline void tc_parse_dt_s32(struct device_node *np, void *desc,
				    const struct tc_dtprop *props,
				    int prop_cnt)
{
	int i;

	for (i = 0; i < prop_cnt; i++) {
		if (unlikely(!props[i].name))
			continue;
		__of_property_read_s32(np, props[i].name, desc + props[i].offset);
	}
}

static inline void tc_parse_dt_u32_arr(struct device_node *np, void *desc,
					const struct tc_dtprop *props,
					int prop_cnt)
{
	int i;

	for (i = 0; i < prop_cnt; i++) {
		if (unlikely(!props[i].name))
			continue;
		of_property_read_u32_array(np, props[i].name,
					   desc + props[i].offset,
					   props[i].sz);
	}
}

static inline int __of_property_read_s32_array(const struct device_node *np,
					       const char *propname,
					       s32 *out_values, size_t sz)
{
	return of_property_read_u32_array(np, propname, (u32 *)out_values, sz);
}

static inline void tc_parse_dt_s32_arr(struct device_node *np, void *desc,
					   const struct tc_dtprop *props,
					   int prop_cnt)
{
	int i;

	for (i = 0; i < prop_cnt; i++) {
		if (unlikely(!props[i].name))
			continue;
		__of_property_read_s32_array(np, props[i].name,
					     desc + props[i].offset,
					     props[i].sz);
	}
}

static const struct tc_dtprop tc_dtprops_bool[] = {
	TC_DT_VALPROP(charger_unlimited),
	TC_DT_VALPROP(enable_sw_safety_timer),
	TC_DT_VALPROP(enable_sw_jeita),
	TC_DT_VALPROP(enable_dynamic_mivr),
	TC_DT_VALPROP(enable_hv_charging),
	TC_DT_VALPROP(enable_ir_comp),
	TC_DT_VALPROP(support_hardware_ir_comp),
	TC_DT_VALPROP(support_dual_battery),
	TC_DT_VALPROP(support_reset_eoc),
	TC_DT_VALPROP(support_long_life_recharger),
	TC_DT_VALPROP(support_real_soc_decimal),
	TC_DT_VALPROP(disable_high_temp_lock_uisoc),
	TC_DT_VALPROP(low_temp_err_limit),
	TC_DT_VALPROP(support_si_bat),
	TC_DT_VALPROP(battery_health_not_support),
};

static const struct tc_dtprop tc_dtprops_u32[] = {
	TC_DT_VALPROP(min_charger_voltage),
	TC_DT_VALPROP(min_charger_current),
	TC_DT_VALPROP(wireless_min_charger_current),
	TC_DT_VALPROP(sdp_charger_current),
	TC_DT_VALPROP(sdp_input_current),
	TC_DT_VALPROP(cdp_charger_current),
	TC_DT_VALPROP(cdp_input_current),
	TC_DT_VALPROP(dcp_charger_current),
	TC_DT_VALPROP(dcp_input_current),
	TC_DT_VALPROP(nonstd_charger_current),
	TC_DT_VALPROP(nonstd_input_current),
	TC_DT_VALPROP(max_charging_current_limit),
	TC_DT_VALPROP(max_input_current_limit),
	TC_DT_VALPROP(tpa_temp_gap),
	TC_DT_VALPROP(tpcb_temp_gap),
	TC_DT_VALPROP(tbat_temp_gap),
	TC_DT_VALPROP(r_comp),
	TC_DT_VALPROP(v_comp_max),
	TC_DT_VALPROP(master_r_comp),
	TC_DT_VALPROP(master_v_comp_max),
	TC_DT_VALPROP(slave_r_comp),
	TC_DT_VALPROP(slave_v_comp_max),
	TC_DT_VALPROP(log_level),
	TC_DT_VALPROP(ftm_cp_num),
	TC_DT_VALPROP(dual_batt_eoc_delay_time),
	TC_DT_VALPROP(max_charging_time),
	TC_DT_VALPROP(dsc_aicr_min),
	TC_DT_VALPROP(dsc_ichg_min),
	TC_DT_VALPROP(aging_start_uisoc),
	TC_DT_VALPROP(aging_stop_uisoc),
	TC_DT_VALPROP(kom_start_uisoc),
	TC_DT_VALPROP(kom_stop_uisoc),
	TC_DT_VALPROP(batt_over_heat_temp),
	TC_DT_VALPROP(batt_over_cold_temp),
	TC_DT_VALPROP(long_life_rechg_time),
	TC_DT_VALPROP(bat_health_cycle_base),
	TC_DT_VALPROP(bat_health_dec_times),
	TC_DT_VALPROP(charge_decimal_timeout_time),
	TC_DT_VALPROP(decimal_report_freq),
	TC_DT_VALPROP(smooth_soc_full_vol),
	TC_DT_VALPROP(low_temp_err_limit_input),
	TC_DT_VALPROP(swchg_hw_eoc_max),
};

static const struct tc_dtprop tc_dtprops_s32[] = {
	TC_DT_VALPROP(sw_eoc_cv_gap),
	TC_DT_VALPROP(recharger_gap),
	TC_DT_VALPROP(sw_eoc_cnt_time),
};

static const struct tc_dtprop tc_dtprops_u32_array[] = {
	TC_DT_VALPROP_ARR(charger_voltage_ovp, CHARGER_VOLTAGE_MAX),
	TC_DT_VALPROP_ARR(vbus_measure_method, MEASURE_METHOD_MAX),
	TC_DT_VALPROP_ARR(dynamic_mivr_vol, DYNAMIC_MIVR_MAX),
	TC_DT_VALPROP_ARR(bat_level_def, BAT_HEALTH_LEVEL_MAX),
	TC_DT_VALPROP_ARR(bat_level_code, BAT_HEALTH_LEVEL_MAX),
	TC_DT_VALPROP_ARR(vbat_gap, BATT_VBAT_MAX),
	TC_DT_VALPROP_ARR(si_bat_cycle_def, SI_BAT_CYCLE_MAX),
	TC_DT_VALPROP_ARR(si_bat_cycle_vol, SI_BAT_CYCLE_MAX),
};

static const struct tc_dtprop tc_dtprops_s32_array[] = {
	TC_DT_VALPROP_ARR(tpa_temp, TC_LEVEL_MAX),
	TC_DT_VALPROP_ARR(tpa_ichg, TC_LEVEL_MAX),
	TC_DT_VALPROP_ARR(tpcb_temp, TC_LEVEL_MAX),
	TC_DT_VALPROP_ARR(tpcb_ichg, TC_LEVEL_MAX),
	TC_DT_VALPROP_ARR(tbat_temp, TC_LEVEL_MAX),
	TC_DT_VALPROP_ARR(tbat_ichg, TC_LEVEL_MAX),
	TC_DT_VALPROP_ARR(batt_ht_temp_dis_chg, BATT_HT_DIS_CHG_MAX),
	TC_DT_VALPROP_ARR(batt_ht_code_dis_chg, BATT_HT_DIS_CHG_MAX),
	TC_DT_VALPROP_ARR(batt_lt_temp_dis_chg, BATT_LT_DIS_CHG_MAX),
	TC_DT_VALPROP_ARR(batt_lt_code_dis_chg, BATT_LT_DIS_CHG_MAX),
};

void tc_upload_battery_msg(struct ai_chg_data *aichg, struct tc_charger *info)
{
	union com_propval set_val;
	int qmax = 0;
	char *env[2] = { "AICHG_BIGDATA=1", NULL };

	qmax = tc_get_qmax();
	tchr_info("upload_battery_msg imp=%d,cap=%d,eoc=%d,cur=%d,soc=%d,upload_tims=%d \n",3460,qmax,info->data->vbat_eoc,
		aichg->upload_current,tc_get_uisoc(),aichg->upload_tims);

	set_val.intval = ARRAY_SIZE(env);
	set_val.ptr = &env[0];
	tran_dev_set_prop(info->data->tc_charger_dev, TRAN_PROP_SEND_UEVENT, &set_val);

	aichg->pass_80_soc = false;
	aichg->aichg_time[TIME_BEGIN] = ktime_get_boottime();
}

static void tc_upload_battery_health_percent_msg(struct tc_charger *info)
{
	struct tc_desc *desc = info->desc;
	int bat_cycle;
	int bat_health_percent;

	bat_cycle = tc_get_battery_cycle();

	if (bat_cycle > desc->bat_health_cycle_base)
	    bat_health_percent = BATTERY_HEALTH_FULL - (bat_cycle - desc->bat_health_cycle_base) / desc->bat_health_dec_times;
	else
		bat_health_percent = BATTERY_HEALTH_FULL;

	tchr_info("%s:bat_cycle = %d, health percent = %d\n",__func__, bat_cycle, bat_health_percent);
}

static void tc_chg_upload_msg_data_work(struct work_struct *work)
{
	struct tc_data *data = container_of(to_delayed_work(work),
			struct tc_data, upload_msg_work);
	struct tc_charger *info = data->info;
	struct tc_desc *desc = info->desc;

	//tchr_info("%s: enter\n", __func__);
	if(!desc->battery_health_not_support)
		tc_upload_battery_health_percent_msg(info);

	return;
}

static void tc_aichg_data_upload(struct tc_charger *info, bool is_charger_on)
{
	struct tc_data *data = info->data;
	struct ai_chg_data *aichg = &data->aichg_data;
	if((aichg->last_soc_tid == 78) && (tc_get_uisoc() == 79)){
			aichg->pass_80_soc = true;
			aichg->upload_current = tc_get_battery_current();
			aichg->aichg_time[TIME_BEGIN] = ktime_get_boottime();
	}

	if (!aichg->ai_dischg) {
		if(aichg->pass_80_soc){
			aichg->aichg_time[TIME_END] = ktime_get_boottime();
			aichg->aichg_time[TIME_DIFF] = ktime_sub(aichg->aichg_time[TIME_END], aichg->aichg_time[TIME_BEGIN]);
			aichg->charging_time = ktime_to_timespec64(aichg->aichg_time[TIME_DIFF]);
			aichg->upload_tims = aichg->charging_time.tv_sec;
			if(is_charger_on) {
				tc_upload_battery_msg(aichg, info);
			}else{
				if((aichg->last_soc_tid == 99) && (tc_get_uisoc() == 100)){//charge to full,upload 80% to 100% duration
					tc_upload_battery_msg(aichg, info);
				}
			}
		} else {
			aichg->real_upload_current =  tc_get_battery_current();
		}
	}
	aichg->last_soc_tid = tc_get_uisoc();
}

static bool is_all_code_reported(unsigned int reported_code, unsigned int notify_code)
{
	return (reported_code != 0
		&& notify_code == (notify_code & reported_code));
}

void bat_warning_cmd_clear(struct tc_charger *info)
{
	struct tc_data *data = info->data;

	if (data->bat_warning_cmd == 0)
		return;

	/* -16~53 */
	if (data->battery_temp >= BATT_LT_SHUTDOWN_REC_TEMP &&
		data->battery_temp <= BATT_HT_SHUTDOWN_REC_TEMP) {
		data->bat_warning_cmd = 0;
	}
}

static bool tc_check_report_condition(struct tc_charger *info, unsigned int notify_code)
{
	struct tc_data *data = info->data;

	/* Only check not reported notify code */
	notify_code &= (~data->reported_code);
	/* Check If is high&low batt temp stop chg notify code. */
	if (tc_get_charger_type() != POWER_SUPPLY_TYPE_UNKNOWN &&
		(notify_code & CHG_BAT_HT_EOC_STATUS ||
		notify_code & BAT_HT_STEP1_STATUS ||
		notify_code & BAT_LT_STEP1_STATUS)) {

		/* Add others condition here */
		return !(data->charge_vote[VOTER_TRAN_CUSTOM].discharge ||
				data->charge_vote[VOTER_WATER_DETECT].discharge ||
				data->charge_vote[VOTER_PORT_BURN].discharge);
	}

	if (notify_code & BAT_HT_STEP2_STATUS ||
		notify_code & BAT_LT_STEP2_STATUS) {
		return !(data->bat_warning_cmd);
	}

	return true;
}

unsigned int tc_get_report_code(struct tc_charger *info, unsigned int *code_array, int size)
{
	int i = 0;
	struct tc_data *data = info->data;
	unsigned int notify_code = 0;
	unsigned int report_code = 0;

	for (i = size - 1; i >= 0; i--) {
		notify_code = code_array[i] & data->notify_code;
		if (notify_code != 0
			&& !is_all_code_reported(data->reported_code, notify_code)
			&& tc_check_report_condition(info, notify_code)) {
			report_code |= notify_code;
			break;
		} else if (!(data->notify_code & notify_code)) {
			data->reported_code &= ~code_array[i];
		}
	}

	return report_code;
}

int tc_chgstat_notify(struct tc_charger *info)
{
	char *env[2] = { "CHGSTAT=1", NULL };
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	unsigned int report_code = 0;
	int ret = 0;
	unsigned int misc_notify_code[] = {
		CHG_BAT_HT_EOC_STATUS,
		CHG_VBUS_OV_STATUS,
		CHG_TYPEC_WD_STATUS,
	};

	report_code = tc_get_report_code(info, misc_notify_code, ARRAY_SIZE(misc_notify_code));
	if (report_code)
		goto out;

	report_code = tc_get_report_code(info, desc->batt_lt_code, BATT_LT_MAX);
	if (report_code)
		goto out;

	report_code = tc_get_report_code(info, desc->batt_ht_code, BATT_HT_MAX);
	if (report_code)
		goto out;
out:
	// clear already reported code bit
	if (report_code)
		report_code &= (~data->reported_code);

	/* If is multi bit,multiple report,exp:0x1080*/
	if (IS_MULTI_BIT(report_code)) {
		//handle high bit first
		report_code &= report_code - 1;//0x1000
	}

	data->report_code = report_code;

	if (data->report_code || data->notify_code == 0) {
		data->reported_code |= data->report_code;
		ret = kobject_uevent_env(&info->pdev->dev.kobj, KOBJ_CHANGE, env);
		if (ret)
			tchr_err("%s: kobject_uevent_fail, ret=%d", __func__, ret);
		tchr_err("%s: notify_code:0x%x,report_code:0x%x,reported_code:0x%x\n", __func__, data->notify_code, data->report_code, data->reported_code);
	}
	return ret;
}

int tc_chgstat_pump(struct tc_charger *info)
{
	int ret = 0;
	struct tc_data *data = NULL;
	char *env[2] = { "PUMPSTAT=1", NULL };

	if(IS_ERR(info))
		return ret;

	data = info->data;
	if (data->monkey_flag == TRAN_AGING_KOM) {
		tchr_err("%s:in aging KOM mode, not show fast chg animation\n", __func__);
		return ret;
	}

	tchr_err("%s: enter\n", __func__);
	ret = kobject_uevent_env(&info->pdev->dev.kobj, KOBJ_CHANGE, env);
	if (ret)
		tchr_err("%s: kobject_uevent_fail, ret=%d", __func__, ret);

	return ret;
}

int tc_chgstat_chgspeed(struct tc_charger *info)
{
	int ret = 0;
	char *env[2] = { "SPEED=1", NULL };

	tchr_err("%s: enter\n", __func__);
	ret = kobject_uevent_env(&info->pdev->dev.kobj, KOBJ_CHANGE, env);
	if (ret)
		tchr_err("%s: kobject_uevent_fail, ret=%d", __func__, ret);

	return ret;
}

#define AGING_MODE_SETUP "tran_aging_mode=1"
void tc_detect_aging_mode(struct tc_charger *info)
{
	struct tc_data *data = info->data;
 	struct device_node *of_chosen = NULL;
 	char *bootargs = NULL;
 
#if IS_ENABLED(CONFIG_TRAN_AGING_KOM)
  	data->monkey_flag = TRAN_AGING_KOM;
	goto out;
#endif
  	of_chosen = of_find_node_by_path("/chosen");
  	if (!of_chosen) {
		tchr_err("%s failed to of_chosen\n", __func__);
		goto out;
	}
  	bootargs = (char *)of_get_property(of_chosen,
  			"bootargs", NULL);
  	if (!bootargs) {
		tchr_err("%s failed to get bootargs\n",__func__);
		goto out;
	}

  	if (strstr(bootargs, AGING_MODE_SETUP))
  		data->monkey_flag = TRAN_AGING_FLAG;

out:
	tchr_err("%s boot monkey_flag = %d\n", __func__, data->monkey_flag);

}

int tchr_get_debug_level(void)
{
	if (IS_ERR_OR_NULL(pinfo) || IS_ERR_OR_NULL(pinfo->desc))
		return CHRLOG_DEBUG_LEVEL; 

	return pinfo->desc->log_level;
}
EXPORT_SYMBOL(tchr_get_debug_level);

void _wake_up_charger(struct tc_charger *info)
{
	unsigned long flags;
	struct tc_data *data = info->data;

	if (info == NULL) {
		tchr_err("chg info is null, exit!\n");
		return;
	}
	data->timer_cb_duration[2] = ktime_get_boottime();
	spin_lock_irqsave(&data->slock, flags);
	data->timer_cb_duration[3] = ktime_get_boottime();
	if (!data->charger_wakelock->active)
		__pm_stay_awake(data->charger_wakelock);
	data->timer_cb_duration[4] = ktime_get_boottime();
	spin_unlock_irqrestore(&data->slock, flags);
	data->charger_thread_timeout = true;
	data->timer_cb_duration[5] = ktime_get_boottime();
	wake_up_interruptible(&data->wait_que);
}
EXPORT_SYMBOL(_wake_up_charger);

bool try_dual_switch_check(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	int vote_ichg = 0, vote_aicr = 0;
	int ibat = 0;

	if (!data->support_dual_switch)
		return false;

	if (data->is_chg_done)
		return false;

	ibat = tc_get_battery_current();
	if (data->vbat_cv >= data->vbat_cv_term && ibat * 1000 < desc->dsc_ichg_min)
		return false;

	vote_ichg = get_effective_result_locked(data->total_ichg_vote);
	vote_aicr = get_effective_result_locked(data->total_aicr_vote);

	if (vote_ichg < desc->dsc_aicr_min || vote_aicr < desc->dsc_ichg_min)
		return false;

	if (!data->running_algo)
		return false;

	if (data->running_algo->alg_id != PDC_ID &&
		data->running_algo->alg_id != PE2_ID)
		return false;

	return true;
}

void enable_primary_charger(struct tc_charger *info, bool enable)
{
	struct tc_data *data = info->data;

	vote(data->chg1_disable_vote, CHG_THREAD_VOTER, !enable, !enable);
}

void enable_secondary_charger(struct tc_charger *info, bool enable)
{
	struct tc_data *data = info->data;

	vote(data->chg2_disable_vote, CHG_THREAD_VOTER, !enable, !enable);
}

int tc_enable_charging(struct tc_charger *info, bool en)
{
	struct tc_data *data = info->data;

	tchr_err("tc charger %s\n", en ? "enable" : "disable");

	if (en)
		ktime_get_boottime_ts64(&data->charging_begin_time);
	else
		memset(&data->charging_begin_time, 0, sizeof(data->charging_begin_time));

	if (info->algo.enable_charging != NULL)
		return info->algo.enable_charging(info, en);

	return false;
}

int tc_get_fast_charger_type(struct tc_charger *info)
{
	int i;
	struct tchg_alg_device *alg;
	struct tc_data *data = info->data;

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = data->alg[i];
		if (alg == NULL)
			continue;

		if (tchg_alg_is_algo_running(alg))
			return alg->alg_id;

	}

	return ALG_NONE;
}

void tc_update_running_algo(struct tc_charger *info,
		struct tchg_alg_device **running_algo)
{
	int i;
	struct tchg_alg_device *alg;
	struct tc_data *data = info->data;

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = data->alg[i];
		if (alg == NULL)
			continue;

		if (tchg_alg_is_algo_running(alg)) {
			*running_algo = data->alg[i];
			return;
		}

	}
}

void tc_get_fast_charger_limit(struct tc_charger *info, struct charger_data *pdata)
{
	struct tc_data *data = info->data;
	struct tchg_alg_device *alg = data->running_algo;

	if (alg == NULL)
		return;

	if (tchg_alg_is_algo_running(alg)) {
		tchg_alg_get_prop(alg,
			ALG_INPUT_CURRENT, &pdata->input_current_limit);
		tchg_alg_get_prop(alg,
			ALG_CHG_CURRENT, &pdata->charging_current_limit);
	}
}

static int tc_get_chg_type(struct tc_charger *info)
{
	int ret = 0,chg_type = TRAN_USB_TYPE_UNKNOWN;
	union power_supply_propval pval_usb_type = {0, };
	union power_supply_propval pval_type = {0, };
	struct tchg_alg_device *alg = NULL;
	struct tc_data *data = info->data;
	int algo_num;

	ret = tc_charger_check_psy_ptr(&data->chg_psy, "charger");
	if (ret < 0) {
		tchr_info("%s Couldn't get chg_psy\n", __func__);
		return chg_type;
	}

	if (IS_ERR_OR_NULL(data->chg_psy)) {
		tchr_err("%s Couldn't get chg_psy\n", __func__);
	}

	ret = power_supply_get_property(data->chg_psy,
		POWER_SUPPLY_PROP_USB_TYPE, &pval_usb_type);
	if (ret < 0) {
		pr_err("get chg type failed, ret = %d\n", ret);
	}
	ret = power_supply_get_property(data->chg_psy,
		POWER_SUPPLY_PROP_TYPE, &pval_type);

	if (pval_usb_type.intval == POWER_SUPPLY_USB_TYPE_UNKNOWN)
		chg_type = TRAN_USB_TYPE_UNKNOWN;
	else if (pval_usb_type.intval == POWER_SUPPLY_USB_TYPE_SDP)
		chg_type = TRAN_USB_TYPE_SDP;
	else if (pval_usb_type.intval == POWER_SUPPLY_USB_TYPE_CDP)
		chg_type = TRAN_USB_TYPE_CDP;
	else if (pval_usb_type.intval == POWER_SUPPLY_USB_TYPE_DCP) {
		chg_type = TRAN_USB_TYPE_DCP;
		if (pval_type.intval == POWER_SUPPLY_TYPE_WIRELESS)
			chg_type = TRAN_USB_TYPE_WIRELESS;
		else if (pval_type.intval == POWER_SUPPLY_TYPE_USB)
			chg_type = TRAN_USB_TYPE_NONSTAND;
	} else
		chg_type = TRAN_USB_TYPE_NONSTAND;

	alg = data->running_algo;
	if (alg == NULL || chg_type == TRAN_USB_TYPE_UNKNOWN)
		goto out;

	pr_info("%s: running_algo:%s \n", __func__,
		dev_name(&alg->dev));

	switch (alg->alg_id) {
	case PE2_ID:
		chg_type = TRAN_USB_TYPE_PE2;
		break;
	case PDC_ID:
		chg_type = TRAN_USB_TYPE_PDC;
		break;
	case PE5_ID:
		tchg_alg_get_prop(alg, ALG_NUM, &algo_num);
		if (algo_num == PE50_PPS)
			chg_type = TRAN_USB_TYPE_PD_PPS;
		break;
	default:
		break;
	}
out:
	pr_info("%s:chg_type=%d\n", __func__,chg_type);
	return chg_type;
}

static int tc_get_alg_running_vol( struct tc_charger *info)
{
	int i, ret = 0;
	int value = 0;
	struct tchg_alg_device *alg;
	struct tc_data *data = info->data;
	union power_supply_propval pval_usb_type = {0, };
	union power_supply_propval pval_type = {0, };

	ret = tc_charger_check_psy_ptr(&data->chg_psy, "charger");
	if (ret < 0) {
		tchr_info("%s Couldn't get chg_psy\n", __func__);
		return value;
	}

	if (IS_ERR_OR_NULL(data->chg_psy)) {
		tchr_err("%s Couldn't get chg_psy\n", __func__);
	}

	power_supply_get_property(data->chg_psy,
		POWER_SUPPLY_PROP_USB_TYPE, &pval_usb_type);
	power_supply_get_property(data->chg_psy,
		POWER_SUPPLY_PROP_TYPE, &pval_type);

	if (pval_usb_type.intval != POWER_SUPPLY_USB_TYPE_DCP ||
		pval_type.intval == POWER_SUPPLY_TYPE_USB) {
		return value;
	}

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = data->alg[i];
		if (alg == NULL)
			continue;

		ret = tchg_alg_is_algo_running(alg);
		if (ret == false || ret == -EOPNOTSUPP) {
			//pr_info("%s: alg:%s ret:%d\n", __func__,
			//	dev_name(&alg->dev), ret);
			continue;
		}

		ret = tchg_alg_get_prop(alg, ALG_RUNNING_VOL, &value);
		if (ret < 0) {
			tchr_err("get ALG_RUNNING_VOL fail\n");
			return value;
		}
		break;
	}
	return value;
}

static void tc_parse_temp_code(struct tc_charger *info,
				struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct tc_desc *desc = info->desc;
	struct tc_data *data = info->data;
	int i;
	char name[50];

	for (i = 0; i < BATT_HT_MAX; i++) {
		sprintf(name, "%sbatt_ht_temp", data->kpoc ? "kpoc_" : "");
		of_property_read_u32_index(np, name, i, (u32 *)&desc->batt_ht_temp[i]);

		sprintf(name, "%sbatt_ht_rec_temp", data->kpoc ? "kpoc_" : "");
		of_property_read_u32_index(np, name, i, (u32 *)&desc->batt_ht_rec_temp[i]);

		sprintf(name, "%sbatt_ht_code", data->kpoc ? "kpoc_" : "");
		of_property_read_u32_index(np, name, i, (u32 *)&desc->batt_ht_code[i]);

		sprintf(name, "%sbatt_ht_vote_hiz", data->kpoc ? "kpoc_" : "");
		of_property_read_u32_index(np, name, i, (u32 *)&desc->batt_ht_vote[i].chg_hiz);

		sprintf(name, "%sbatt_ht_vote_discharge", data->kpoc ? "kpoc_" : "");
		of_property_read_u32_index(np, name, i, (u32 *)&desc->batt_ht_vote[i].discharge);

		sprintf(name, "%sbatt_ht_vote_icon", data->kpoc ? "kpoc_" : "");
		of_property_read_u32_index(np, name, i, (u32 *)&desc->batt_ht_vote[i].icon_disappeared);

		desc->batt_ht_vote[i].voter = VOTER_BATT_HIGH_TEMP; 
	}

	for (i = 0; i < BATT_LT_MAX; i++) {
		sprintf(name, "%sbatt_lt_temp", data->kpoc ? "kpoc_" : "");
		of_property_read_u32_index(np, name, i, (u32 *)&desc->batt_lt_temp[i]);

		sprintf(name, "%sbatt_lt_rec_temp", data->kpoc ? "kpoc_" : "");
		of_property_read_u32_index(np, name, i, (u32 *)&desc->batt_lt_rec_temp[i]);

		sprintf(name, "%sbatt_lt_code", data->kpoc ? "kpoc_" : "");
		of_property_read_u32_index(np, name, i, (u32 *)&desc->batt_lt_code[i]);

		sprintf(name, "%sbatt_lt_vote_hiz", data->kpoc ? "kpoc_" : "");
		of_property_read_u32_index(np, name, i, (u32 *)&desc->batt_lt_vote[i].chg_hiz);

		sprintf(name, "%sbatt_lt_vote_discharge", data->kpoc ? "kpoc_" : "");
		of_property_read_u32_index(np, name, i, (u32 *)&desc->batt_lt_vote[i].discharge);

		sprintf(name, "%sbatt_lt_vote_icon", data->kpoc ? "kpoc_" : "");
		of_property_read_u32_index(np, name, i, (u32 *)&desc->batt_lt_vote[i].icon_disappeared);

		desc->batt_lt_vote[i].voter = VOTER_BATT_LOW_TEMP; 
	}

}

static int tc_parse_dual_batt_jeita(struct tc_charger *info,
				struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct tc_desc *desc = info->desc;
	int i, j, value, length = 0;
	int ret = 0;
	char name[50];

	/* master battery ffc param */
	ret = of_property_read_u32(np, "master_batt_level", &value);
	if (ret < 0) {
		pr_err("%s: master_batt_level property missing\n",
			__func__);
		return -EINVAL;
	} else {
		desc->master_batt_level = value;
		pr_info("%s: dts config desc->master_batt_level: %d\n",
			__func__, desc->master_batt_level);
	}

	desc->master_batt = devm_kzalloc(&info->pdev->dev,
		sizeof(struct batt_desc) * desc->master_batt_level, GFP_KERNEL);
	if (!desc->master_batt) {
		pr_err("%s: cann't malloc, exit...\n", __func__);
		return -ENOMEM;
	}

	for (i = 0; i < desc->master_batt_level; i++) {
		sprintf(name, "master_batt_level_%d", i);
		length = of_property_count_elems_of_size(np, name, sizeof(u32));
		ret = __of_property_read_s32_array(np, name,
			(u32 *)(&desc->master_batt[i]), length);
		if (ret < 0) {
			pr_err("%s: %s property missing, use default config\n",
				__func__, name);
		}

		for (j = 0; j < BATT_TEMP_MAX; j++)
			pr_info("%s: desc->master_batt[%d]->temp[%d] = [%d]", __func__, i, j,desc->master_batt[i].temp[j]);
		for (j = 0; j < BATT_VBAT_MAX; j++)
			pr_info("%s: %s desc->master_batt[%d]->vbat[%d] = [%d],"
				"desc->master_batt[%d]->ibat[%d] = [%d],"
				"desc->master_batt[%d]->eoc[%d] = %d\n",
				__func__, name,
				i, j, desc->master_batt[i].vbat[j],
				i, j, desc->master_batt[i].ibat[j],
				i, j, desc->master_batt[i].eoc);	
	}

	/* slave battery ffc param */
	ret = of_property_read_u32(np, "slave_batt_level", &value);
	if (ret < 0) {
		pr_err("%s: slave_batt_level property missing\n",
			__func__);
		return -EINVAL;
	} else {
		desc->slave_batt_level = value;
		pr_info("%s: dts config desc->slave_batt_level: %d\n",
			__func__, desc->slave_batt_level);
	}

	desc->slave_batt = devm_kzalloc(&info->pdev->dev,
		sizeof(struct batt_desc) * desc->slave_batt_level, GFP_KERNEL);
	if (!desc->slave_batt) {
		pr_err("%s: cann't malloc, exit...\n", __func__);
		return -ENOMEM;
	}

	for (i = 0; i < desc->slave_batt_level; i++) {
		sprintf(name, "slave_batt_level_%d", i);
		length = of_property_count_elems_of_size(np, name, sizeof(u32));
		ret = __of_property_read_s32_array(np, name,
			(u32 *)(&desc->slave_batt[i]), length);
		if (ret < 0) {
			pr_err("%s: %s property missing, use default config\n",
				__func__, name);
		}

		for (j = 0; j < BATT_TEMP_MAX; j++)
			pr_info("%s: desc->slave_batt[%d]->temp[%d] = [%d]", __func__, i, j,desc->slave_batt[i].temp[j]);
		for (j = 0; j < BATT_VBAT_MAX; j++)
			pr_info("%s: %s desc->slave_batt[%d]->vbat[%d] = [%d],"
				"desc->slave_batt[%d]->ibat[%d] = [%d],"
				"desc->slave_batt[%d]->eoc[%d] = %d\n",
				__func__, name,
				i, j, desc->slave_batt[i].vbat[j],
				i, j, desc->slave_batt[i].ibat[j],
				i, j, desc->slave_batt[i].eoc);	
	}

	return 0;
}

static int tc_parse_single_batt_jeita(struct tc_charger *info,
				struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct tc_desc *desc = info->desc;
	int i, j, value, length = 0;
	int ret = 0;
	char name[50];

	ret = of_property_read_u32(np, "single_batt_level", &value);
	if (ret < 0) {
		pr_err("%s: single_batt_level property missing\n",
			__func__);
		return -EINVAL;
	} else {
		desc->single_batt_level = value;
		pr_info("%s: dts config desc->single_batt_level: %d\n",
			__func__, desc->single_batt_level);
	}

	desc->single_batt = devm_kzalloc(&info->pdev->dev,
		sizeof(struct batt_desc) * desc->single_batt_level, GFP_KERNEL);
	if (!desc->single_batt) {
		pr_err("%s: cann't malloc, exit...\n", __func__);
		return -ENOMEM;
	}

	for (i = 0; i < desc->single_batt_level; i++) {
		sprintf(name, "single_batt_level_%d", i);
		length = of_property_count_elems_of_size(np, name, sizeof(u32));
		ret = __of_property_read_s32_array(np, name,
			(u32 *)(&desc->single_batt[i]), length);
		if (ret < 0) {
			pr_err("%s: %s property missing, use default config\n",
				__func__, name);
		}

		for (j = 0; j < BATT_TEMP_MAX; j++)
			pr_info("%s: desc->single_batt[%d]->temp[%d] = [%d]", __func__, i, j,desc->single_batt[i].temp[j]);
		for (j = 0; j < BATT_VBAT_MAX; j++)
			pr_info("%s: %s desc->single_batt[%d]->vbat[%d] = [%d],"
				"desc->single_batt[%d]->ibat[%d] = [%d],"
				"desc->single_batt[%d]->eoc[%d] = %d\n",
				__func__, name,
				i, j, desc->single_batt[i].vbat[j],
				i, j, desc->single_batt[i].ibat[j],
				i, j, desc->single_batt[i].eoc);	
	}

	return 0;
}

static void tc_charger_parse_dt(struct tc_charger *info,
				struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct tc_desc *desc = info->desc;
	struct tc_data *data = info->data;
	int i, length = 0, ret = 0;

	memcpy(desc, &default_info, sizeof(*desc));

	tc_parse_dt_bool(np, (void *)desc, tc_dtprops_bool,
			     ARRAY_SIZE(tc_dtprops_bool));
	tc_parse_dt_u32(np, (void *)desc, tc_dtprops_u32,
			 ARRAY_SIZE(tc_dtprops_u32));
	tc_parse_dt_s32(np, (void *)desc, tc_dtprops_s32,
			 ARRAY_SIZE(tc_dtprops_s32));
	tc_parse_dt_u32_arr(np, (void *)desc, tc_dtprops_u32_array,
			     ARRAY_SIZE(tc_dtprops_u32_array));
	tc_parse_dt_s32_arr(np, (void *)desc, tc_dtprops_s32_array,
			     ARRAY_SIZE(tc_dtprops_s32_array));

	ret = of_property_count_strings(np, "support_alg");
	if (ret < 0) {
		ret = 0;
		tchr_err("not found support_alg");
	}

	desc->support_alg_cnt = ret;
	desc->support_alg = devm_kzalloc(&info->pdev->dev, ret * sizeof(char *),
					GFP_KERNEL);
	if (!desc->support_alg)
		return;

	for (i = 0; i < desc->support_alg_cnt; i++) {
		ret = of_property_read_string_index(np, "support_alg", i,
						    &desc->support_alg[i]);
		if (ret >= 0)
			tchr_err("support alg(%s)\n", desc->support_alg[i]);
	}

	/* 8 = KERNEL_POWER_OFF_CHARGING_BOOT */
	/* 9 = LOW_POWER_OFF_CHARGING_BOOT */
	if (data->bootmode == 8 || data->bootmode == 9) {
		data->kpoc = true;
	}

	desc->support_dual_switch = of_property_read_bool(np, "support_dual_switch");
	if (desc->support_dual_switch) {
		length = of_property_count_elems_of_size(np, "chg_tab", sizeof(u32));
		desc->chg_tab = devm_kzalloc(&info->pdev->dev, (length + 1) * sizeof(u32),
					GFP_KERNEL);
		ret = of_property_read_u32_array(np, "chg_tab", (u32 *)desc->chg_tab, length);
		if (ret < 0) {
			tchr_err("property missing, please check!\n");
		}
	}

	desc->chg_tab_len = length / (sizeof(struct dual_chg_table) / sizeof(u32));

	tc_parse_temp_code(info, dev);

	if (desc->support_dual_battery)
		tc_parse_dual_batt_jeita(info, dev);
	else
		tc_parse_single_batt_jeita(info, dev);

	ret = of_property_read_u32(np, "base_year", &desc->base_year);
	if (ret || desc->base_year < 2000) {
		desc->base_year = 2020;
		tchr_err("no config base year\n");
	}

	tchr_err("%s: parse dts success\n", __func__);

}

static void tc_charger_start_timer(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	struct timespec64 end_time, time_now;
	ktime_t ktime, ktime_now;
	int ret = 0;

	/* If the timer was already set, cancel it */
	ret = alarm_try_to_cancel(&data->charger_timer);
	if (ret < 0) {
		tchr_err("%s: callback was running, skip timer\n", __func__);
		return;
	}

	ktime_now = ktime_get_boottime();
	time_now = ktime_to_timespec64(ktime_now);
	end_time.tv_sec = time_now.tv_sec + data->polling_interval;
	end_time.tv_nsec = time_now.tv_nsec + 0;
	data->endtime = end_time;
	ktime = ktime_set(data->endtime.tv_sec, data->endtime.tv_nsec);

	tchr_err("%s: alarm timer start:%d, %lld %09ld\n", __func__, ret,
		(long long)data->endtime.tv_sec, data->endtime.tv_nsec);
	alarm_start(&data->charger_timer, ktime);
}

static void check_battery_exist(struct tc_charger *info)
{
	unsigned int i = 0;
	int count = 0;
	//int boot_mode = data->bootmode;

	for (i = 0; i < 3; i++) {
		if (tc_is_battery_exist() == false)
			count++;
	}

#ifdef FIXME
	if (count >= 3) {
		if (boot_mode == META_BOOT || boot_mode == ADVMETA_BOOT ||
		    boot_mode == ATE_FACTORY_BOOT)
			tchr_info("boot_mode = %d, bypass battery check\n",boot_mode);
		else {
			tchr_err("battery doesn't exist, shutdown\n");
			orderly_poweroff(true);
		}
	}
#endif
}

static void tc_charger_control_vote(struct tc_charger *info, struct tc_charge_vote tc_vote, bool wake_up)
{
	struct tc_data *data = info->data;

	if (!memcmp(&data->charge_vote[tc_vote.voter], &tc_vote, sizeof(tc_vote)))
		return;

	tchr_info("Voter:%s Result:%d,%d,%d\n", VOTER_TEXT[tc_vote.voter],
		tc_vote.chg_hiz, tc_vote.discharge, tc_vote.icon_disappeared);

	data->charge_vote[tc_vote.voter] = tc_vote;
	if (wake_up)
		_wake_up_charger(info);
}

static void tc_charger_control_check(struct tc_charger *info)
{
	int i;
	bool chg_hiz = false;
	bool discharge = false;
	bool icon_disappeared = false;
	char str[256] = {0};
	struct tc_data *data = info->data;

	strcat(str, "Hiz_result: ");
	for (i = 0; i < VOTER_MAX; i++) {
		chg_hiz |= data->charge_vote[i].chg_hiz;
		if (data->charge_vote[i].chg_hiz) {
			strcat(str, VOTER_TEXT[data->charge_vote[i].voter]);
			strcat(str, " ");
		}
	}

	strcat(str, "     Dischg_result: ");
	for (i = 0; i < VOTER_MAX; i++) {
		discharge |= data->charge_vote[i].discharge;
		if (data->charge_vote[i].discharge) {
			strcat(str, VOTER_TEXT[data->charge_vote[i].voter]);
			strcat(str, " ");
		}
	}

	strcat(str, "    Icon_result: ");
	for (i = 0; i < VOTER_MAX; i++) {
		icon_disappeared |= data->charge_vote[i].icon_disappeared;
		if (data->charge_vote[i].icon_disappeared) {
			strcat(str, VOTER_TEXT[data->charge_vote[i].voter]);
			strcat(str, " ");
		}
	}
	strcat(str, "\n");

	if (chg_hiz || discharge || icon_disappeared)
		pr_info("%s", str);

	if (data->vote_result.discharge != discharge) {
		data->vote_result.discharge = discharge;
		tc_enable_charging(info, !discharge);
	}

	if (data->vote_result.icon_disappeared != icon_disappeared) {
		data->vote_result.icon_disappeared = icon_disappeared;
		set_icon_disappeared(info, icon_disappeared);
	}

	if (data->vote_result.chg_hiz != chg_hiz) {
		data->vote_result.chg_hiz = chg_hiz;
		vote(data->chg1_hiz_vote, CHG_THREAD_VOTER, chg_hiz, chg_hiz);
		vote(data->chg2_hiz_vote, CHG_THREAD_VOTER, chg_hiz, chg_hiz);
	}

	if (!discharge && !chg_hiz)
		data->state = TC_NORMAL_CHARGING;
	else if (discharge && !chg_hiz)
		data->state = TC_ONLY_POWERPATH;
	else
		data->state = TC_NOT_CHARGING;

}

static void check_dynamic_mivr(struct tc_charger *info)
{
	int i = 0;
	int vbat = 0;
	bool is_fast_charge = false;
	struct tchg_alg_device *alg = NULL;
	struct tc_desc *desc = info->desc;
	struct tc_data *data = info->data;

	if (!desc->enable_dynamic_mivr)
		return;

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = data->alg[i];
		if (alg == NULL)
			continue;

		is_fast_charge = tchg_alg_is_algo_running(alg);
		if (is_fast_charge)
			return;
	}

	vbat = tc_get_battery_voltage()*1000;

	for (i = 0; i < DYNAMIC_MIVR_MAX; i++) {
		if (vbat < desc->dynamic_mivr_vol[i] -
					DYNAMIC_MIVR_GAP) {
			vote(data->chg1_mivr_vote, CHG_THREAD_VOTER,
					true, desc->dynamic_mivr_vol[i]);
			vote(data->chg2_mivr_vote, CHG_THREAD_VOTER,
					true, desc->dynamic_mivr_vol[i]);
			tchr_info("Vbat:%d, dynamic_mivr:%d\n",
				vbat, desc->dynamic_mivr_vol[i]);
			break;
		}
	}
}

void tc_chg_switch_vbus_ovp(int idx, const char *name, bool enable)
{
	struct tc_data *data = pinfo->data;
	struct tc_desc *desc = pinfo->desc;

	vote(data->sw_vbus_ovp_vote, name, enable, desc->charger_voltage_ovp[idx]);
}
EXPORT_SYMBOL(tc_chg_switch_vbus_ovp);

struct tc_level_data {
	const char *name;
	int temp;
	enum charger_level *temp_level;
	int *temp_level_def;
	int *curlmt;
	int recovery_area;
};

/* sw jeita */
static void tc_check_temp_level(struct tc_charger *info,
					struct tc_level_data *tdata)
{
	int i;
	enum charger_level temp_level = 0;
	int prev_temp_level = *tdata->temp_level;

	if (tdata->temp < tdata->temp_level_def[TC_LEVEL_T1] - tdata->recovery_area) {
		temp_level = TC_LEVEL_BELOW_T1;
		goto out;
	}

	for (i = TC_LEVEL_MAX - 1; i > TC_LEVEL_BELOW_T1; i--) {

		if (tdata->temp >= tdata->temp_level_def[i]) {

			if (prev_temp_level <= i ||
				tdata->temp <= (tdata->temp_level_def[prev_temp_level] - tdata->recovery_area))
				temp_level = i;
			else
				temp_level = prev_temp_level;
			break;
		}
	}

out:
	*tdata->temp_level = temp_level;
	tchr_err("%s(%d,%d)\n", tdata->name, tdata->temp, *tdata->temp_level);

}

static void tc_charger_update_dual_battery_info(struct tc_charger *info)
{
	int ret = 0;
	union com_propval temp_val = {.intval = 0};
	struct tc_data *data = info->data;

	if (IS_ERR_OR_NULL(data->gauge_dev))
		data->gauge_dev = tran_get_by_name("tc_gauge");

	ret = tran_dev_get_prop(data->gauge_dev, TRAN_PROP_MASTER_BATT_VOLT, &temp_val);
	if (!ret)
		data->fg_a_vbat = temp_val.intval / 1000;

	ret = tran_dev_get_prop(data->gauge_dev, TRAN_PROP_MASTER_BATT_NOW_CURR, &temp_val);
	if (!ret)
		data->fg_a_ibat = temp_val.intval / 1000;
	
	ret = tran_dev_get_prop(data->gauge_dev, TRAN_PROP_MASTER_BATT_TEMP, &temp_val);
	if (!ret)
		data->fg_a_temp = temp_val.intval / 10;
	
	ret = tran_dev_get_prop(data->gauge_dev, TRAN_PROP_MASTER_BATT_EN, &temp_val);
	if (!ret)
		data->fg_a_status = temp_val.intval;

	ret = tran_dev_get_prop(data->gauge_dev, TRAN_PROP_MASTER_BATT_VOLT, &temp_val);
	if (!ret)
		data->fg_b_vbat = temp_val.intval / 1000;

	ret = tran_dev_get_prop(data->gauge_dev, TRAN_PROP_MASTER_BATT_NOW_CURR, &temp_val);
	if (!ret)
		data->fg_b_ibat = temp_val.intval / 1000;
	
	ret = tran_dev_get_prop(data->gauge_dev, TRAN_PROP_MASTER_BATT_TEMP, &temp_val);
	if (!ret)
		data->fg_b_temp = temp_val.intval / 10;

	ret = tran_dev_get_prop(data->gauge_dev, TRAN_PROP_MASTER_BATT_EN, &temp_val);
	if (!ret)
		data->fg_b_status = temp_val.intval;	
}

static void tc_chg_update_dual_batt_jeita(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	int level;
	int i;
	bool master_batt_level_change = false;
	bool slave_batt_level_change = false;

	/* get ADC data */
	tc_charger_update_dual_battery_info(info);

	/* batt step ffc param Calculate */
	if (data->fg_a_temp > desc->master_batt[desc->master_batt_level -1].temp[BATT_TEMP_HIGH] || 
		data->fg_a_temp <= desc->master_batt[0].temp[BATT_TEMP_LOW]) {

		memcpy(data->master_batt_vbat, desc->master_batt[BATT_TEMP_LOW].vbat, sizeof(data->master_batt_vbat));
		memcpy(data->master_batt_ibat, desc->master_batt[BATT_TEMP_LOW].ibat, sizeof(data->master_batt_ibat));
		data->master_batt_level_index = 0;
	}

	/* debounce */
	for (level = 0; level < desc->master_batt_level; level++) {

		if (data->fg_a_temp > desc->master_batt[level].temp[BATT_TEMP_LOW] &&
		   data->fg_a_temp <= desc->master_batt[level].temp[BATT_TEMP_HIGH] &&
		   (desc->master_batt[level].ffc_status < 0 ||
		    desc->master_batt[level].ffc_status == data->is_ffc)) {

			if (data->master_batt_level_index == level)
				break;

			memcpy(data->master_batt_vbat, desc->master_batt[level].vbat, sizeof(data->master_batt_vbat));
			memcpy(data->master_batt_ibat, desc->master_batt[level].ibat, sizeof(data->master_batt_ibat));
			data->master_batt_level_index = level;
			master_batt_level_change = true;
			tchr_info("master_batt_level:%d\n", data->master_batt_level_index);
			break;
		}
	}

	for (i = BATT_VBAT_HIGH - 1; i >= 0; i--) {
		if (data->fg_a_vbat >= (data->master_batt_vbat[i] - desc->vbat_gap[i])) {
	
			data->master_chg_index = i + 1;
			break;
		}

		if (i == 0)
			data->master_chg_index = 0;
	}

	if ((data->master_batt_ibat[data->master_chg_index] >= data->fg_a_ibat &&
		data->master_vbat_cv != data->master_batt_vbat[data->master_chg_index]) ||
		master_batt_level_change == true) {
		
		master_batt_level_change = false;

		data->master_vbat_cv = data->master_batt_vbat[data->master_chg_index];
		data->master_vbat_cc = data->master_batt_ibat[data->master_chg_index];
		data->master_vbat_eoc = desc->master_batt[data->master_batt_level_index].eoc;

		tchr_err("%s:master batt jeita switch to cv:%d, cc:%d, eoc:%d\n",
			__func__, data->master_vbat_cv, data->master_vbat_cc, data->master_vbat_eoc);
	}

	/* slave batt step ffc param Calculate */
	if (data->fg_b_temp > desc->slave_batt[desc->slave_batt_level -1].temp[BATT_TEMP_HIGH] || 
		data->fg_b_temp <= desc->slave_batt[0].temp[BATT_TEMP_LOW]) {

		memcpy(data->slave_batt_vbat, desc->slave_batt[BATT_TEMP_LOW].vbat, sizeof(data->slave_batt_vbat));
		memcpy(data->slave_batt_ibat, desc->slave_batt[BATT_TEMP_LOW].ibat, sizeof(data->slave_batt_ibat));
		data->slave_batt_level_index = 0;
	}

	/* debounce */
	for (level = 0; level < desc->slave_batt_level; level++) {

		if (data->fg_b_temp > desc->slave_batt[level].temp[BATT_TEMP_LOW] &&
		   data->fg_b_temp <= desc->slave_batt[level].temp[BATT_TEMP_HIGH] &&
		   (desc->slave_batt[level].ffc_status < 0 ||
		    desc->slave_batt[level].ffc_status == data->is_ffc)) {

			if (data->slave_batt_level_index == level)
				break;

			memcpy(data->slave_batt_vbat, desc->slave_batt[level].vbat, sizeof(data->slave_batt_vbat));
			memcpy(data->slave_batt_ibat, desc->slave_batt[level].ibat, sizeof(data->slave_batt_ibat));
			data->slave_batt_level_index = level;
			slave_batt_level_change = true;
			tchr_info("slave_batt_level:%d\n", data->slave_batt_level_index);
			break;
		}
	}

	for (i = BATT_VBAT_HIGH - 1; i >= 0; i--) {
		if (data->fg_b_vbat >= (data->slave_batt_vbat[i] - desc->vbat_gap[i])) {
	
			data->slave_chg_index = i + 1;
			break;
		}

		if (i == 0)
			data->slave_chg_index = 0;
	}

	if ((data->slave_batt_ibat[data->slave_chg_index] >= data->fg_b_ibat &&
		data->slave_vbat_cv != data->slave_batt_vbat[data->slave_chg_index]) ||
		slave_batt_level_change == true) {
		
		slave_batt_level_change = false;

		data->slave_vbat_cv = data->slave_batt_vbat[data->slave_chg_index];
		data->slave_vbat_cc = data->slave_batt_ibat[data->slave_chg_index];
		data->slave_vbat_eoc = desc->slave_batt[data->slave_batt_level_index].eoc;

		tchr_err("%s:slave batt jeita switch to cv:%d, cc:%d, eoc:%d\n",
			__func__, data->slave_vbat_cv, data->slave_vbat_cc, data->slave_vbat_eoc);
	}

	if (data->fg_a_status && data->fg_b_status) {
		data->vbat_cv = min(data->master_vbat_cv, data->slave_vbat_cv);
		data->vbat_cc = data->master_vbat_cc + data->slave_vbat_cc;
		data->vbat_eoc = data->master_vbat_eoc + data->slave_vbat_eoc;
	} else {
		data->vbat_cv = data->fg_a_status ? data->master_vbat_cv : data->slave_vbat_cv;
		data->vbat_cc = data->fg_a_status ? data->master_vbat_cc : data->slave_vbat_cc;
		data->vbat_eoc = data->fg_a_status ? data->master_vbat_eoc : data->slave_vbat_eoc;
	}

	data->vbat_cv_term = data->master_batt_vbat[BATT_VBAT_MAX - 1];
	data->is_low_cv_status = data->vbat_cv < desc->smooth_soc_full_vol;

	tchr_info("[MASTER] %s: master_cv:%d, master_cc:%d, master_eoc:%d, chg_index:%d\n",
		__func__, data->master_vbat_cv ,data->master_vbat_cc,
		data->master_vbat_eoc, data->master_chg_index);

	tchr_info("[SLAVE] %s: slave_cv:%d, slave_cc:%d, slave_eoc:%d, chg_index:%d\n",
		__func__, data->slave_vbat_cv ,data->slave_vbat_cc,
		data->slave_vbat_eoc, data->slave_chg_index);

	tchr_info("[DUAL_BATT] %s: cv:%d,%d cc:%d, eoc:%d, is_low_cv:%d\n",
		__func__, data->vbat_cv_term, data->vbat_cv,
		data->vbat_cc, data->vbat_eoc, data->is_low_cv_status);

}

static void tc_chg_update_single_batt_jeita(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	int level;
	int i;
	int tbat, ibat, vbat;
	bool batt_level_change = false;

	/* get ADC data */
	tbat = tc_get_battery_temperature();
	ibat = tc_get_battery_current() * 1000;
	vbat = tc_get_battery_voltage() * 1000;

	/* batt step ffc param Calculate */
	if (tbat > desc->single_batt[desc->single_batt_level -1].temp[BATT_TEMP_HIGH] || 
		tbat <= desc->single_batt[0].temp[BATT_TEMP_LOW]) {

		memcpy(data->single_batt_vbat, desc->single_batt[BATT_TEMP_LOW].vbat, sizeof(data->single_batt_vbat));
		memcpy(data->single_batt_ibat, desc->single_batt[BATT_TEMP_LOW].ibat, sizeof(data->single_batt_ibat));
		data->single_batt_level_index = 0;
	}

	/* debounce */
	for (level = 0; level < desc->single_batt_level; level++) {

		if (tbat > desc->single_batt[level].temp[BATT_TEMP_LOW] &&
		   tbat <= desc->single_batt[level].temp[BATT_TEMP_HIGH] && 
		   (desc->single_batt[level].ffc_status < 0 ||
		    desc->single_batt[level].ffc_status == data->is_ffc)) {

			if ((desc->single_batt[level].cycle_cnt[BATT_TEMP_LOW] >= 0) &&
			(data->battery_cycle < desc->single_batt[level].cycle_cnt[BATT_TEMP_LOW] ||
			(data->battery_cycle >= desc->single_batt[level].cycle_cnt[BATT_TEMP_HIGH] &&
			 desc->single_batt[level].cycle_cnt[BATT_TEMP_HIGH] >= 0)))
				continue;

			if (data->single_batt_level_index == level)
				break;

			memcpy(data->single_batt_vbat, desc->single_batt[level].vbat, sizeof(data->single_batt_vbat));
			memcpy(data->single_batt_ibat, desc->single_batt[level].ibat, sizeof(data->single_batt_ibat));
			data->single_batt_level_index = level;
			batt_level_change = true;
			tchr_info("single_batt_level:%d\n", data->single_batt_level_index);
			break;
		}
	}

	for (i = BATT_VBAT_HIGH - 1; i >= 0; i--) {
		if (vbat >= (data->single_batt_vbat[i] - desc->vbat_gap[i])) {
	
			data->single_chg_index = i + 1;
			break;
		}

		if (i == 0)
			data->single_chg_index = 0;
	}

	if ((data->single_batt_ibat[data->single_chg_index] >= ibat &&
		data->vbat_cv != data->single_batt_vbat[data->single_chg_index]) ||
		batt_level_change == true) {
		
		batt_level_change = false;

		data->vbat_cv = data->single_batt_vbat[data->single_chg_index];
		data->vbat_cc = data->single_batt_ibat[data->single_chg_index];
		data->vbat_eoc = desc->single_batt[data->single_batt_level_index].eoc;
		data->long_life_rechg_cv_gap = desc->single_batt[level].rechg_gap;

		tchr_err("%s:single batt jeita switch to cv:%d, cc:%d, eoc:%d cv_gap:%d\n",
			__func__, data->vbat_cv, data->vbat_cc, data->vbat_eoc,
			data->long_life_rechg_cv_gap);
	}

	data->vbat_cv_term = data->single_batt_vbat[BATT_VBAT_MAX - 1];
	data->is_low_cv_status = data->vbat_cv < desc->smooth_soc_full_vol;

	tchr_info("[SINGLE_BATT] %s: cv:%d,%d, cc:%d, eoc:%d,is_low_cv:%d\n",
		__func__, data->vbat_cv_term, data->vbat_cv,
		data->vbat_cc, data->vbat_eoc,data->is_low_cv_status);
}

static void tc_check_batt_jeita(struct tc_charger *info)
{
	struct tc_desc *desc = info->desc;

	if (desc->support_dual_battery)
		tc_chg_update_dual_batt_jeita(info);
	else
		tc_chg_update_single_batt_jeita(info);
}

static void tc_check_tbat_level(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	struct tc_level_data tdata = {
		.name = "tbat",
		.temp_level_def = desc->tbat_temp,
		.temp_level = &data->tbat_level,
		.recovery_area = desc->tbat_temp_gap,
	};

	tdata.temp = data->battery_temp;

	tc_check_temp_level(info, &tdata);
}

static void tc_check_tpcb_level(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	struct tc_level_data tdata = {
		.name = "tpcb",
		.temp_level_def = desc->tpcb_temp,
		.temp_level = &data->tpcb_level,
		.recovery_area = desc->tpcb_temp_gap,
	};

	tdata.temp = tc_get_tpcb_temp();

	tc_check_temp_level(info, &tdata);
}

static void tc_check_tpa_level(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	struct tc_level_data tdata = {
		.name = "tpa",
		.temp_level_def = desc->tpa_temp,
		.temp_level = &data->tpa_level,
		.recovery_area = desc->tpa_temp_gap,
	};

	tdata.temp = tc_get_tpa_temp_max();

	tc_check_temp_level(info, &tdata);
}

static void check_state_machine_level(struct tc_charger *info)
{
	struct tc_data *data = info->data;

	if (!data->plug_in)
		return;

	tc_check_batt_jeita(info);
	tc_check_tbat_level(info);
	tc_check_tpcb_level(info);
	tc_check_tpa_level(info);
}

void tc_charger_sw_vbus_ovp_vote(struct tc_charger *info, bool discharge)
{
	struct tc_charge_vote tc_vote = {
		.voter = VOTER_SW_VBUS_OVP,
		.chg_hiz = discharge,
		.discharge = discharge,
		.icon_disappeared = discharge,
	};

	tc_charger_control_vote(info, tc_vote, false);
}

void tc_charger_safety_time_vote(struct tc_charger *info, bool discharge)
{
	struct tc_charge_vote tc_vote = {
		.voter = VOTER_SAFETY_TIMER,
		.chg_hiz = false,
		.discharge = discharge,
		.icon_disappeared = discharge,
	};
	
	tc_charger_control_vote(info, tc_vote, false);
}

void tc_charger_monkey_vote(struct tc_charger *info, bool chg_hiz,
			bool discharge, bool icon_disappeared)
{
	struct tc_charge_vote tc_vote = {
		.voter = VOTER_MONKEY,
		.chg_hiz = chg_hiz,
		.discharge = discharge,
		.icon_disappeared = icon_disappeared,
	};
	
	tc_charger_control_vote(info, tc_vote, false);
}

void tc_charger_cmd_vote(struct tc_charger *info, bool discharge)
{
	struct tc_charge_vote tc_vote = {
		.voter = VOTER_CMD,
		.chg_hiz = false,
		.discharge = discharge,
		.icon_disappeared = discharge,
	};
	
	tc_charger_control_vote(info, tc_vote, true);
}

void tc_charger_aidischg_vote(struct tc_charger *info, bool discharge)
{
	struct tc_charge_vote tc_vote = {
		.voter = VOTER_AI_CHARGER,
		.chg_hiz = false,
		.discharge = discharge,
		.icon_disappeared = false,
	};

	tc_charger_control_vote(info, tc_vote, true);
}

void tc_charger_bypass_dischg_vote(struct tc_charger *info, bool discharge)
{
	struct tc_charge_vote tc_vote = {
		.voter = VOTER_BYPASS_CHARGER,
		.chg_hiz = false,
		.discharge = discharge,
		.icon_disappeared = false,
	};

	tc_charger_control_vote(info, tc_vote, true);
}

int tc_charger_send_uevent(struct tc_charger *info, void *ptr, int len)
{
	int ret = 0,i = 0;
	char **pchar = ptr;
	char *env[MAX_ENV_LEN] = { NULL };

	if(IS_ERR(info) || IS_ERR(pchar))
		return -EINVAL;

	for (i = 0; i < len && i < MAX_ENV_LEN; i++) {
		env[i] = *(pchar + i);
		tchr_info("%s: env[%d]%s,length(%d)\n",__func__, i, env[i], len);
	}

	ret = kobject_uevent_env(&info->pdev->dev.kobj, KOBJ_CHANGE, env);
	if (ret)
		tchr_err("%s: kobject_uevent_fail, ret=%d", __func__, ret);

	return ret;
}

static void batt_ht_notify_check(struct tc_charger *info)
{
	int i;
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	static int notify_code_all = -1;
	struct tc_charge_vote tc_vote = {
		.voter = VOTER_BATT_HIGH_TEMP,
		.chg_hiz = false,
		.discharge = false,
		.icon_disappeared = false,
	};

	if (notify_code_all < 0) {
		notify_code_all = 0;
		for (i = BATT_HT_MAX - 1; i >= 0; i--) {
			notify_code_all |= desc->batt_ht_code[i];
		}
	}

	mutex_lock(&data->notify_lock);
	if (data->monkey_flag == TRAN_AGING_HOT_IGNORE) {
		tchr_info("MONKEY_HOT_IGNORE, Skip batt_ht notify");
		data->notify_code &= ~notify_code_all;
		goto out;
	}
	for (i = BATT_HT_MAX - 1; i >= 0; i--) {
		if (data->battery_temp >=
			((desc->batt_ht_code[i] && (data->notify_code & desc->batt_ht_code[i])) ?
			desc->batt_ht_rec_temp[i] : desc->batt_ht_temp[i])) {
			tc_vote = desc->batt_ht_vote[i];
			data->notify_code &= ~notify_code_all;
			data->notify_code |= desc->batt_ht_code[i];
			break;
		} else {
			data->notify_code &= ~desc->batt_ht_code[i];
		}
	}

out:
	mutex_unlock(&data->notify_lock);

	tc_charger_control_vote(info, tc_vote, false);
}

static void batt_lt_notify_check(struct tc_charger *info)
{
	int i;
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
    static int notify_code_all = -1;
	struct tc_charge_vote tc_vote = {
		.voter = VOTER_BATT_LOW_TEMP,
		.chg_hiz = false,
		.discharge = false,
		.icon_disappeared = false,
	};

	if (notify_code_all < 0) {
		notify_code_all = 0;
		for (i = BATT_LT_MAX - 1; i >= 0; i--) {
			notify_code_all |= desc->batt_lt_code[i];
		}
	}

	mutex_lock(&data->notify_lock);

	for (i = BATT_LT_MAX - 1; i >= 0; i--) {
		if (data->battery_temp <=
			((desc->batt_lt_code[i] && (data->notify_code & desc->batt_lt_code[i])) ?
			desc->batt_lt_rec_temp[i] : desc->batt_lt_temp[i])) {
			tc_vote = desc->batt_lt_vote[i];
			data->notify_code &= ~notify_code_all;
			data->notify_code |= desc->batt_lt_code[i];
			break;
		} else {
			data->notify_code &= ~desc->batt_lt_code[i];
		}
	}

	mutex_unlock(&data->notify_lock);

	tc_charger_control_vote(info, tc_vote, false);
}

void tc_report_lock_charging(struct tc_charger *info, bool discharge)
{
	struct tc_data *data = info->data;
	struct tc_charge_vote tc_vote = {
		.voter = VOTER_BATT_HIGH_TEMP_EOC,
		.chg_hiz = false,
		.discharge = discharge,
		.icon_disappeared = discharge,
	};
	if (discharge) {
		tchr_info("high temp charger full, report uevent\n");
		data->high_temp_mode = true;
		data->notify_code |= CHG_BAT_HT_EOC_STATUS;
	} else {
		tchr_info("high temp recharger, report uevent\n");
		data->high_temp_mode = false;
		data->notify_code &= ~(CHG_BAT_HT_EOC_STATUS);
	}
	tc_charger_control_vote(info, tc_vote, false);
}

void tc_high_temp_lock_uisoc_check(struct tc_charger *info)
{
	int uisoc;
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;

	if (desc->disable_high_temp_lock_uisoc)
		return;

	uisoc = tc_get_uisoc();

	if (data->is_chg_done && data->is_low_cv_status
		&& uisoc < 100 && data->high_temp_mode == false)
		tc_report_lock_charging(info, true);
	else if(!data->is_low_cv_status && data->high_temp_mode
		&& data->battery_temp <= BATT_HT_EOC_REC_TEMP)
		tc_report_lock_charging(info, false);
}

static void water_detect_dischg_vote(struct tc_charger *info, bool discharge)
{
	struct tc_charge_vote tc_vote = {
		.voter = VOTER_WATER_DETECT,
		.chg_hiz = discharge,
		.discharge = discharge,
		.icon_disappeared = discharge,
	};

	tc_charger_control_vote(info, tc_vote, true);
}

static void port_burn_dischg_vote(struct tc_charger *info, bool discharge)
{
	struct tc_charge_vote tc_vote = {
		.voter = VOTER_PORT_BURN,
		.chg_hiz = discharge,
		.discharge = discharge,
		.icon_disappeared = discharge,
	};

	tc_charger_control_vote(info, tc_vote, true);
}

static void tran_custom_dischg_vote(struct tc_charger *info, bool discharge)
{
	struct tc_charge_vote tc_vote = {
		.voter = VOTER_TRAN_CUSTOM,
		.chg_hiz = false,
		.discharge = discharge,
		.icon_disappeared = discharge,
	};

	tc_charger_control_vote(info, tc_vote, true);
}

static void vbus_ovp_notify_check(struct tc_charger *info)
{
	int vchr = 0;
	struct tc_data *data = info->data;
	bool vote_discharge = false;
	vchr = tc_get_vbus() * 1000; /* uV */
	mutex_lock(&data->notify_lock);
	if (vchr < data->max_charger_voltage) {
		/* triggerd hardware vbus_ovp but pd_vbus is not 0V */
		if (vchr < 2500000 && !data->pd_reset) {
			if((data->chr_type != POWER_SUPPLY_TYPE_UNKNOWN)&&(data->chr_type != POWER_SUPPLY_TYPE_WIRELESS)) {
				data->notify_code &= ~CHG_VBUS_OV_STATUS;
				vote_discharge = true;
			}
		} else {
			data->notify_code &= ~CHG_VBUS_OV_STATUS;
			vote_discharge = false;
		}
	} else {
		data->notify_code |= CHG_VBUS_OV_STATUS;
		vote_discharge = true;
		tchr_err("[BATTERY] charger_vol(%d mV) > %d mV\n",
			vchr / 1000, data->max_charger_voltage / 1000);
	}

	tc_charger_sw_vbus_ovp_vote(info, vote_discharge);
	mutex_unlock(&data->notify_lock);
}

static void safety_time_check(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	struct timespec64 time_now, total_time;

	if (!desc->enable_sw_safety_timer)
		goto out;

	if (data->is_chg_done)
		goto out;

	if(data->charging_begin_time.tv_sec == 0)
		goto out;

	ktime_get_boottime_ts64(&time_now);

	total_time = timespec64_sub(time_now, data->charging_begin_time);

	if (total_time.tv_sec >= desc->max_charging_time) {
		tchr_err("safety time trigger(%lld >= %u)\n",
			(long long)total_time.tv_sec, desc->max_charging_time);
		tc_charger_safety_time_vote(info, true);
		return;
	}
out:
	tc_charger_safety_time_vote(info, false);
	
}

static void monkey_flag_check(struct tc_charger *info)
{
	int uisoc;
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;

	uisoc = tc_get_uisoc();

	switch (data->monkey_flag) {
	case TRAN_NORMAL_FLAG:
	case TRAN_AGING_HOT_IGNORE:
		tc_charger_monkey_vote(info, false, false, false);
		break;
	case TRAN_AGING_FLAG:
		if (uisoc >= desc->aging_stop_uisoc) {
			tc_charger_monkey_vote(info, true, true, false);
		} else if (uisoc <= desc->aging_start_uisoc) {
			tc_charger_monkey_vote(info, false, false, false);
		}
		tchr_info("monkey_aging ctrl, soc(strat,stop) = %d(%d,%d)!\n",
			uisoc, desc->aging_start_uisoc, desc->aging_stop_uisoc);
		break;
	case TRAN_AGING_KOM:
		if (uisoc >= desc->kom_stop_uisoc) {
			tc_charger_monkey_vote(info, true, true, false);
		} else if (uisoc <= desc->kom_start_uisoc) {
			tc_charger_monkey_vote(info, false, false, false);
		}
		tchr_info("monkey_kom ctrl, soc(strat,stop) = %d(%d,%d)!\n",
			uisoc, desc->kom_start_uisoc, desc->kom_stop_uisoc);
		break;
	case TRAN_START_CHARGING:
		tc_charger_monkey_vote(info, false, false, false);
		tchr_info("monkey_start_charging ctrl!\n");
		break;
	case TRAN_STOP_CHARGING:
		tc_charger_monkey_vote(info, true, true, false);
		tchr_info("monkey_stop_charging ctrl!\n");
		break;	       
	default:
		break;
	}
}

static void charger_check_status(struct tc_charger *info)
{
	struct tc_data *data = info->data;

	if (tc_get_charger_type() == POWER_SUPPLY_TYPE_UNKNOWN){
		tchr_err("chg type null\n");
		return;
	}

	charger_dev_kick_wdt(data->chg1_dev);
	charger_dev_kick_wdt(data->chg2_dev);
	tc_high_temp_lock_uisoc_check(info);
	batt_ht_notify_check(info);
	batt_lt_notify_check(info);
	vbus_ovp_notify_check(info);
	bat_warning_cmd_clear(info);
	if (data->notify_code != data->reported_code)
		tc_chgstat_notify(info);

	if(data->wait_protocol_done)
		safety_time_check(info);
	monkey_flag_check(info);

	tc_charger_control_check(info);

	tchr_err("tmp:%d notify_code:%d chg:%d %d\n",
		data->battery_temp, data->notify_code,
		data->state, data->is_charging);
}

static int tchg_alg_id(const char *alg_name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(alg_name_array); i++) {
		if (!strcmp(alg_name, alg_name_array[i])) {
			return i;
		}
	}

	return -EINVAL;
}

static void tc_chg_dual_switch_check(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	int ret = 0;

	if (desc->support_dual_switch) {
		ret = charger_dev_kick_wdt(data->chg2_dev);
		if (ret >= 0) {
			data->support_dual_switch = true;
			return;
		}
	}
	
	data->support_dual_switch = false;
}

static void tc_chg_power_path_recover(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	bool enabled = false;
	charger_dev_is_powerpath_enabled(data->chg1_dev, &enabled);
	if (!enabled)
		charger_dev_enable_powerpath(data->chg1_dev, true);
}

static void tc_chg_vote_refresh(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	tchr_info("chg vote refresh for disable charger");
	rerun_election(data->chg1_hiz_vote);
	rerun_election(data->chg2_hiz_vote);
	rerun_election(data->chg1_disable_vote);
	rerun_election(data->chg2_disable_vote);
	rerun_election(data->chg1_mivr_vote);
	rerun_election(data->chg2_mivr_vote);
	rerun_election(data->chg1_aicr_vote);
	rerun_election(data->chg2_aicr_vote);
	rerun_election(data->chg1_ichg_vote);
	rerun_election(data->chg2_ichg_vote);
	rerun_election(data->sw_vbus_ovp_vote);
}

void tc_chg_vote_reset(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;

	tchr_info("chg vote reset for plug out");
	memset(data->charge_vote, 0, sizeof(data->charge_vote));
	tc_charger_control_check(info);

	vote(data->chg1_hiz_vote, CHG_THREAD_VOTER, false, 0);
	vote(data->chg2_hiz_vote, CHG_THREAD_VOTER, false, 0);
	vote(data->total_aicr_vote, PD_CHG_VOTER, false, 0);
	vote(data->chg2_disable_vote, CHG_THREAD_VOTER, true, true);
	vote(data->total_ichg_vote, CHG_THREAD_VOTER, true, 100000);
	vote(data->total_aicr_vote, CHG_THREAD_VOTER, true, 100000);
	vote(data->chg1_mivr_vote, CHG_THREAD_VOTER,
			true, desc->min_charger_voltage);
	vote(data->chg2_mivr_vote, CHG_THREAD_VOTER,
			true, desc->min_charger_voltage);
}

static void tc_multi_chg_para_init(struct tc_charger *info)
{
	struct tc_data *data = info->data;

	data->speed_owner = TRAN_MULTI_OWNER_SYS;
	data->speed_owner_old = TRAN_MULTI_OWNER_SYS;
	data->chg_speed = TRAN_MULTI_SPEED_MID;
	data->chg_speed_old = TRAN_MULTI_SPEED_MID;
}

int tc_send_up_nv_cycle_count(struct tc_charger *info)
{
	char buf[64];
	char *env[2] = { NULL, NULL };
	int bat_cycle = tc_get_battery_raw_cycle();

	snprintf(buf, sizeof(buf),"BAT_CYCLE:%d",bat_cycle);
	env[0] = buf;
	kobject_uevent_env(&info->pdev->dev.kobj, KOBJ_CHANGE, env);

	tchr_info("%s: bat_raw_cycle = %d\n",__func__, bat_cycle);

	return 0;
}

static void tc_multi_chg_plug_out_reset(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	struct tchg_alg_device *alg;

	tchr_info("%s:use old muliti chg_speed and speed_owner value\n", __func__);
	data->speed_owner = TRAN_MULTI_OWNER_SYS;
	data->chg_speed = data->chg_speed_old;

	alg = get_tchg_alg_by_name("pe5");
	if (alg) {
		tchg_alg_set_prop(alg, ALG_MULTI_CHG_SPEED, data->chg_speed);
		tchg_alg_set_prop(alg, ALG_MULTI_CHG_SPEED_OWNER, data->speed_owner);
	}
}

static void tc_chg_data_reset(struct tc_charger *info)
{
	struct tc_data *data = info->data;

	/*reset smtchg flags*/
	data->smtchg_data.smartchg_en = false;
	data->smtchg_data.smartchg_energy = SMTCHG_ENERGY_100;

	/*reset bypass flags*/
	data->bypasschg_data.bypass_en = false;
	data->bypasschg_data.bypass_energy = BYPASS_ENERGY_100;

	/*reset AI flags*/
	data->aichg_data.ai_dischg = false;
}

static void tc_chg_notify_code_reset(struct tc_charger *info)
{
	int i;
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	int notify_code = 0; 

	pr_info("%s: reset notify code:%d\n",
		__func__, data->notify_code);

	if (data->plug_in) {
		/* high temp notify_code plug in reset */
		for (i = BATT_HT_MAX - 1; i >= 0; i--) {
			notify_code |= desc->batt_ht_code_dis_chg[i];
		}
		
		/* low temp notify_code plug in reset */
		for (i = BATT_LT_MAX - 1; i >= 0; i--) {
			notify_code |= desc->batt_lt_code_dis_chg[i];
		}
	} else {
		/* high temp notify_code plug out reset */
		for (i = BATT_HT_MAX - 1; i >= 0; i--) {
			notify_code |= desc->batt_ht_code[i];
		}
		
		/* low temp notify_code plug out reset */
		for (i = BATT_LT_MAX - 1; i >= 0; i--) {
			notify_code |= desc->batt_lt_code[i];
		}
	}

	/* sw_ovp notify_code plug out reset */
	notify_code |= CHG_VBUS_OV_STATUS;
	notify_code |= CHG_BAT_HT_EOC_STATUS;

	mutex_lock(&data->notify_lock);
	data->notify_code &= ~notify_code;
	data->reported_code &= ~notify_code;
	tc_charger_sw_vbus_ovp_vote(info, false);
	mutex_unlock(&data->notify_lock);
}

static int tc_charger_plug_out(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	/* struct tc_desc *desc = info->desc; */

	tchr_err("%s\n", __func__);
	data->chr_type = POWER_SUPPLY_TYPE_UNKNOWN;
	data->charger_thread_polling = false;
	data->plug_in = false;
	data->wait_protocol_done = false;
	data->pd_reset = false;
	data->notify_code = 0;
	data->is_charging = false;
	data->tbat_level = 0;
	data->tpcb_level = 0;
	data->tpa_level = 0;
	data->is_chg_done = false;
	data->try_dsc = false;
	data->high_temp_mode = false;
	data->is_ffc = 0;
	data->ffc_alg_id = ALG_NONE;
	data->eoc_scheme = HW_EOC;
	data->sw_eoc_cur = 0;
	data->pe5_rechg_cv_gap = 0;
	data->running_algo = NULL;
	memset(&data->total_time, 0, sizeof(data->total_time));
	alarm_cancel(&data->long_life_rechg_timer);
	data->trigger_long_life_rechg_timer = false;
	data->long_life_rechg_work_flag = false;
	data->long_life_rechg_done = false;
	data->polling_interval = CHARGING_INTERVAL;
	data->upload_msg_data_doing = false;
	/* data->max_charger_voltage = */
	/*         desc->charger_voltage_ovp[CHARGER_VOLTAGE_SWITCH_BASIC]; */

	tc_chg_data_reset(info);
	tc_chg_alg_notify_call(info, EVT_PLUG_OUT, 0);
	tc_chg_alg_plugout_reset(info);
	tran_dev_notify(data->tc_charger_dev, EVENT_PLUG_OUT, info);
	enable_secondary_charger(info, false);
	charger_dev_plug_out(data->chg1_dev);
	charger_dev_plug_out(data->chg2_dev);
	charger_dev_plug_out(data->dvchg1_dev);
	charger_dev_plug_out(data->dvchg2_dev);
	charger_dev_plug_out(data->wlsc_dev);
	tc_multi_chg_plug_out_reset(info);
	tc_chg_notify_code_reset(info);
	tc_chg_vote_reset(info);
	tc_send_up_nv_cycle_count(info);
	tc_chgstat_notify(info);

	return 0;
}

static int tc_charger_plug_in(struct tc_charger *info,
				int chr_type)
{
	struct tc_data *data = info->data;
	/* struct tc_desc *desc = info->desc; */
	struct charger_data *total_pdata = &data->total_pdata;

	tchr_debug("%s\n",__func__);

	data->chr_type = chr_type;
	data->usb_type = tc_get_usb_type();
	data->charger_thread_polling = true;
	data->plug_in = true;

	data->master_chg_index = 0;
	data->master_batt_level_index = -1;
	data->master_vbat_cv = 0;
	data->master_vbat_cc = 0;
	data->master_vbat_eoc = 0;

	data->slave_chg_index = 0;
	data->slave_batt_level_index = -1;
	data->slave_vbat_cv = 0;
	data->slave_vbat_cc = 0;
	data->slave_vbat_eoc = 0;

	data->single_chg_index = 0;
	data->single_batt_level_index = -1;
	data->vbat_cv = 0;
	data->vbat_cc = 0;
	data->vbat_eoc = 0;
	data->vbat_cv_term = 0;

	total_pdata->charging_current_limit = 2000000;
	total_pdata->input_current_limit = 500000;
	data->running_algo = NULL;
	data->battery_cycle = tc_get_battery_cycle();
	tc_chg_dual_switch_check(info);
	tc_chg_power_path_recover(info);
	tc_chg_vote_refresh(info);

	tchr_err("tc_is_charger_on plug in, type:%d\n", chr_type);

	ktime_get_boottime_ts64(&data->charging_begin_time);

	tc_chg_alg_notify_call(info, EVT_PLUG_IN, 0);
	tran_dev_notify(data->tc_charger_dev, EVENT_PLUG_IN, info);

	charger_dev_plug_in(data->chg1_dev);
	charger_dev_plug_in(data->dvchg1_dev);
	tc_chg_notify_code_reset(info);

	return 0;
}

static bool tc_is_charger_on(struct tc_charger *info)
{
	int chr_type;
	struct tc_data *data = info->data;

	chr_type = tc_get_charger_type();
	data->alias_type = tc_get_alias_type();
	if (chr_type == POWER_SUPPLY_TYPE_UNKNOWN) {
		if (data->chr_type != POWER_SUPPLY_TYPE_UNKNOWN) {
			tc_charger_plug_out(info);
			mutex_lock(&data->cable_out_lock);
			data->cable_out_cnt = 0;
			mutex_unlock(&data->cable_out_lock);
		}
	} else {
		if (data->chr_type == POWER_SUPPLY_TYPE_UNKNOWN)
			tc_charger_plug_in(info, chr_type);
		else {
			data->chr_type = chr_type;
			data->usb_type = tc_get_usb_type();
		}

		if (data->cable_out_cnt > 0) {
			tc_charger_plug_out(info);
			tc_charger_plug_in(info, chr_type);
			mutex_lock(&data->cable_out_lock);
			data->cable_out_cnt = 0;
			mutex_unlock(&data->cable_out_lock);
		}
	}

	if (chr_type == POWER_SUPPLY_TYPE_UNKNOWN)
		return false;

	return true;
}

static void charger_send_kpoc_uevent(struct tc_charger *info)
{
	static bool first_time = true;
	struct tc_data *data = info->data;
	ktime_t ktime_now;

	if (first_time) {
		data->uevent_time_check = ktime_get();
		first_time = false;
	} else {
		ktime_now = ktime_get();
		if ((ktime_ms_delta(ktime_now, data->uevent_time_check) / 1000) >= 60) {
			tc_chgstat_notify(info);
			data->uevent_time_check = ktime_now;
		}
	}
}

static void kpoc_power_off_check(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	int vbus = 0;
	int counter = 0;

	if (data->kpoc) {
		vbus = tc_get_vbus();
		if (vbus >= 0 && vbus < 2500 && !tc_is_charger_on(info) && !data->pd_reset) {
			tchr_err("Unplug Charger/USB in KPOC mode, vbus=%d, shutdown\n", vbus);
			charger_send_kpoc_uevent(info);
			while (1) {
				if (counter >= 20000) {
					tchr_err("%s, wait too long\n", __func__);
					if (pm_suspend_target_state != PM_SUSPEND_ON) {
						tchr_err("skip shutdown during pm suspend state %d", pm_suspend_target_state);
					} else {
						ksys_sync_helper();
						kernel_power_off();
					}

					break;
				}
				if (data->is_suspend == false) {
					//tchr_err("%s, not in suspend, shutdown\n", __func__);
					if (pm_suspend_target_state != PM_SUSPEND_ON) {
						tchr_err("skip shutdown during pm suspend state %d", pm_suspend_target_state);
					} else {
						ksys_sync_helper();
						//kernel_power_off();
					}

				} else {
					tchr_err("%s, suspend! cannot shutdown\n", __func__);
					msleep(20);
				}
				counter++;
			}
		}
		//charger_send_kpoc_uevent(info);
	}
}

static void charger_status_check(struct tc_charger *info)
{
	union power_supply_propval online, status;
	struct tc_data *data = info->data;
	int ret;
	bool charging = true;

	ret = tc_charger_check_psy_ptr(&data->chg_psy, "charger");
	if (ret < 0) {
		tchr_info("%s Couldn't get chg_psy\n", __func__);
		goto out;
	}

	if (IS_ERR_OR_NULL(data->chg_psy)) {
		tchr_err("%s Couldn't get chg_psy\n", __func__);
	}

	ret = power_supply_get_property(data->chg_psy,
		POWER_SUPPLY_PROP_ONLINE, &online);

	ret = power_supply_get_property(data->chg_psy,
		POWER_SUPPLY_PROP_STATUS, &status);

	if (!online.intval)
		charging = false;
	else if (status.intval == POWER_SUPPLY_STATUS_NOT_CHARGING)
		charging = false;
out:
	data->is_charging = charging;
}

static char *dump_charger_type(int chg_type, int usb_type)
{
	switch (chg_type) {
	case POWER_SUPPLY_TYPE_UNKNOWN:
		return "none";
	case POWER_SUPPLY_TYPE_USB:
		if (usb_type == POWER_SUPPLY_USB_TYPE_SDP)
			return "usb";
		else
			return "nonstd";
	case POWER_SUPPLY_TYPE_WIRELESS:
		return "wireless";
	case POWER_SUPPLY_TYPE_USB_CDP:
		return "usb-h";
	case POWER_SUPPLY_TYPE_USB_DCP:
		return "std";
	default:
		return "unknown";
	}
}

struct charger_device *to_chg_dev(struct tc_charger *info, enum chg_enum idx)
{
	struct tc_data *data = info->data;

	switch (idx) {
	case SW_CHG1:
		return data->chg1_dev;
	case SW_CHG2:
		return data->chg2_dev;
	default:
		return NULL;
	}

	return NULL;
}

static bool key_info_dbg = true;

void tc_charger_dump_key_info(struct tc_charger *info, enum chg_enum idx)
{
	struct charger_device *chg_dev = to_chg_dev(info, idx);
	int ichg = -1, cv = 0, ilim = 0, mivr = 0;
	bool chg_en = false, hiz_state = false;
	bool mivr_state = false, ilim_state = false, is_done = false;

	if (!key_info_dbg)
		return;
	charger_dev_get_charging_current(chg_dev, &ichg);
	charger_dev_get_constant_voltage(chg_dev, &cv);
	charger_dev_get_input_current(chg_dev, &ilim);
	charger_dev_get_mivr(chg_dev, &mivr);

	charger_dev_is_enabled(chg_dev, &chg_en);
	charger_dev_is_hz(chg_dev, &hiz_state);
	charger_dev_get_mivr_state(chg_dev, &mivr_state);
	charger_dev_get_ilim_state(chg_dev, &ilim_state);
	charger_dev_is_charging_done(chg_dev, &is_done);

	tchr_info("%s: ichg:%d, cv:%d, ilim:%d, mivr:%d, "
		"chg_en:%d, hiz_state:%d, "
		"ilim_state:%d, mivr_state:%d, "
		"is_done:%d", chg_name[idx],
		_uA_to_mA(ichg), _uA_to_mA(cv),
		_uA_to_mA(ilim), _uA_to_mA(mivr),
		_uA_to_mA(chg_en), _uA_to_mA(hiz_state),
		_uA_to_mA(ilim_state), _uA_to_mA(mivr_state),
		_uA_to_mA(is_done));
}

static int charger_routine_thread(void *arg)
{
	struct tc_charger *info = arg;
	struct tc_data *data = info->data;
	static bool is_module_init_done = false;
	unsigned long flags;
	bool is_charger_on;
	int ret;
	int vbat_min, vbat_max;
	u32 chg_cv = 0;
	unsigned int init_times = 3;

	while (1) {
		ret = wait_event_interruptible(data->wait_que,
			(data->charger_thread_timeout == true));
		if (ret < 0) {
			tchr_err("%s: wait event been interrupted(%d)\n", __func__, ret);
			continue;
		}

		while (is_module_init_done == false) {
			if (charger_init_algo(info) == true) {
				is_module_init_done = true;
				tchr_err("%s: charger init success\n", __func__);
			}
			else {
				if (init_times > 0) {
					tchr_err("%s: retry to init charger\n", __func__);
					init_times = init_times - 1;
					msleep(10000);
				} else {
					tchr_err("%s: holding to init charger\n", __func__);
					msleep(60000);
				}
			}
		}

		mutex_lock(&data->charger_lock);
		spin_lock_irqsave(&data->slock, flags);
		if (!data->charger_wakelock->active)
			__pm_stay_awake(data->charger_wakelock);
		spin_unlock_irqrestore(&data->slock, flags);
		data->charger_thread_timeout = false;

		tc_chg_vote_refresh(info);
		data->battery_temp = tc_get_battery_temperature();
		ret = charger_dev_get_adc(data->chg1_dev,
			ADC_CHANNEL_VBAT, &vbat_min, &vbat_max);
		ret = charger_dev_get_constant_voltage(data->chg1_dev, &chg_cv);

		if (vbat_min != 0)
			vbat_min = vbat_min / 1000;

		tchr_err("Vbat=%d vbats=%d vbus:%d ibus:%d I=%d T=%d uisoc:%d type:%s>%s pd:%d swchg_ibat:%d cv:%d\n",
			tc_get_battery_voltage(),
			vbat_min,
			tc_get_vbus(),
			tc_get_ibus(),
			tc_get_battery_current(),
			data->battery_temp,
			tc_get_uisoc(),
			dump_charger_type(data->chr_type, data->usb_type),
			dump_charger_type(tc_get_charger_type(), tc_get_usb_type()),
			data->pd_type, get_ibat(info), chg_cv);

		kpoc_power_off_check(info);

		is_charger_on = tc_is_charger_on(info);
		if (!is_charger_on) {
			tchr_info("chargering off, goto out\n");
			goto out;
		}

		if (data->charger_thread_polling == true)
			tc_charger_start_timer(info);

		check_battery_exist(info);
		check_dynamic_mivr(info);
		check_state_machine_level(info);
		charger_check_status(info);

		switch (data->state) {
		case TC_NORMAL_CHARGING:
			info->algo.do_algorithm(info);
			break;

		case TC_ONLY_POWERPATH:
			info->algo.do_powerpath(info);
			break;

		case TC_NOT_CHARGING:
			break;
		}

		charger_status_check(info);
		if(!data->upload_msg_data_doing) {
			schedule_delayed_work(&data->upload_msg_work, 1000);
			data->upload_msg_data_doing = true;
		}
out:
		tc_aichg_data_upload(info, is_charger_on);
		spin_lock_irqsave(&data->slock, flags);
		__pm_relax(data->charger_wakelock);
		spin_unlock_irqrestore(&data->slock, flags);
		tchr_debug("%s end , %d\n",
			__func__, data->charger_thread_timeout);
		mutex_unlock(&data->charger_lock);
	}

	return 0;
}

static int charger_pm_event(struct notifier_block *notifier,
			unsigned long pm_event, void *unused)
{
	ktime_t ktime_now;
	struct timespec64 now;
	struct tc_data *data = container_of(notifier,
		struct tc_data, pm_notifier);
	struct tc_charger *info = data->info;

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		data->is_suspend = true;
		tchr_debug("%s: enter PM_SUSPEND_PREPARE\n", __func__);
		break;
	case PM_POST_SUSPEND:
		data->is_suspend = false;
		tchr_debug("%s: enter PM_POST_SUSPEND\n", __func__);
		ktime_now = ktime_get_boottime();
		now = ktime_to_timespec64(ktime_now);

		if (timespec64_compare(&now, &data->endtime) >= 0 &&
			data->endtime.tv_sec != 0 &&
			data->endtime.tv_nsec != 0) {
			tchr_err("%s: alarm timeout, wake up charger\n",
				__func__);
			__pm_relax(data->charger_wakelock);
			data->endtime.tv_sec = 0;
			data->endtime.tv_nsec = 0;
			_wake_up_charger(info);
		}
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

static enum alarmtimer_restart
	tc_charger_alarm_timer_func(struct alarm *alarm, ktime_t now)
{
	struct tc_data *data =
		container_of(alarm, struct tc_data, charger_timer);
	struct tc_charger *info = data->info;
	ktime_t *time_p = data->timer_cb_duration;

	data->timer_cb_duration[0] = ktime_get_boottime();
	if (data->is_suspend == false) {
		tchr_debug("%s: not suspend, wake up charger\n", __func__);
		data->timer_cb_duration[1] = ktime_get_boottime();
		_wake_up_charger(info);
		data->timer_cb_duration[6] = ktime_get_boottime();
	} else {
		tchr_debug("%s: alarm timer timeout\n", __func__);
		__pm_stay_awake(data->charger_wakelock);
	}

	data->timer_cb_duration[7] = ktime_get_boottime();

	if ((long long)ktime_us_delta(time_p[7], time_p[0]) > 5000)
		tchr_err("%s: delta_t: %lld %lld %lld %lld %lld %lld %lld (%lld)\n",
			__func__,
			(long long)ktime_us_delta(time_p[1], time_p[0]),
			(long long)ktime_us_delta(time_p[2], time_p[1]),
			(long long)ktime_us_delta(time_p[3], time_p[2]),
			(long long)ktime_us_delta(time_p[4], time_p[3]),
			(long long)ktime_us_delta(time_p[5], time_p[4]),
			(long long)ktime_us_delta(time_p[6], time_p[5]),
			(long long)ktime_us_delta(time_p[7], time_p[6]),
			(long long)ktime_us_delta(time_p[7], time_p[0]));

	return ALARMTIMER_NORESTART;
}

static void tc_charger_init_timer(struct tc_charger *info)
{
	struct tc_data *data = info->data;

	alarm_init(&data->charger_timer, ALARM_BOOTTIME,
			tc_charger_alarm_timer_func);
	tc_charger_start_timer(info);

}

int tc_chg_tcpc_notifier_call(struct notifier_block *notifier,
			unsigned long evt, void *val)
{
	struct tc_tcpc_noti *noti = val;
	struct tc_data *data = container_of(notifier,
			struct tc_data, pd_nb);
	struct tc_charger *info = data->info;

	tchr_err("%s %lu\n", __func__, evt);

	switch (evt) {
	case TC_PD_TYPE:
		mutex_lock(&data->pd_lock);
		tchr_err("pd type = %s\n", tc_pd_type_tostring(noti->pd_type));
		data->pd_type = noti->pd_type;
		data->pd_reset = false;
		mutex_unlock(&data->pd_lock);
		_wake_up_charger(info);
		break;

	case TC_PD_CONNECT_HARD_RESET:
		mutex_lock(&data->pd_lock);
		tchr_err("PD Notify HardReset\n");
		data->pd_type = TC_PD_CONNECT_NONE;
		data->pd_reset = true;
		mutex_unlock(&data->pd_lock);
		tc_chg_alg_notify_call(info, EVT_HARDRESET, 0);
		_wake_up_charger(info);
		/* reset PE40 */
		break;
	}

	return NOTIFY_DONE;
}

static void batt_over_temp_check(struct tc_charger *info)
{
	struct tc_desc *desc = info->desc;
	struct tc_data *data = info->data;
	int batt_temp = data->battery_temp;

	if (data->kpoc)
		return;

	if (batt_temp >= desc->batt_over_heat_temp ||
		batt_temp <= desc->batt_over_cold_temp) {
		tchr_err("batt temp over alert %d(%d,%d), shutdown\n", batt_temp,
			desc->batt_over_heat_temp, desc->batt_over_cold_temp);
		if (pm_suspend_target_state != PM_SUSPEND_ON) {
			tchr_err("skip shutdown during pm suspend state %d", pm_suspend_target_state);
		} else {
			ksys_sync_helper();
			kernel_power_off();
		}
	}
}

static void batt_notify_check(struct tc_charger *info)
{
	struct tc_desc *desc = info->desc;
	struct tc_data *data = info->data;
	int batt_temp = data->battery_temp;
	int i, notify_code = 0;

	if (data->plug_in)
		return;

	tchr_info("%s: enter!", __func__);
	for (i = BATT_HT_DIS_CHG_MAX - 1; i >= 0; i--) {
		notify_code |= desc->batt_ht_code_dis_chg[i];
	}
	
	for (i = BATT_LT_DIS_CHG_MAX - 1; i >= 0; i--) {
		notify_code |= desc->batt_lt_code_dis_chg[i];
	}
	
	mutex_lock(&data->notify_lock);
	data->notify_code &= ~notify_code;

	for (i = BATT_HT_DIS_CHG_MAX - 1; i >= 0; i--) {
		if (batt_temp >= desc->batt_ht_temp_dis_chg[i]) {
			data->notify_code |= desc->batt_ht_code_dis_chg[i];
			break;
		}
	}

	for (i = BATT_LT_DIS_CHG_MAX - 1; i >= 0; i--) {
		if (batt_temp <= desc->batt_lt_temp_dis_chg[i]) {
			data->notify_code |= desc->batt_lt_code_dis_chg[i];
			break;
		}
	}

	if (data->notify_code == 0)
		bat_warning_cmd_clear(info);

	if (data->notify_code != data->reported_code)
		tc_chgstat_notify(info);

	mutex_unlock(&data->notify_lock);
}

static void psy_misc_work_handler(struct work_struct *work)
{
	struct tc_data *data = container_of(to_delayed_work(work),
			struct tc_data, psy_misc_work);
	struct tc_charger *info = data->info;

	if (data->monkey_flag == TRAN_AGING_HOT_IGNORE) {
		tchr_info("MONKEY_HOT_IGNORE, Skip Temp det");
		return;
	}

	data->battery_temp =
		tc_get_battery_temperature();
	batt_over_temp_check(info);
	batt_notify_check(info);
}

static int tc_psy_misc_event(
	struct notifier_block *nb, unsigned long event, void *v)
{
	struct power_supply *psy = v;
	struct tc_data *data = container_of(nb, struct tc_data, psy_nb);

	if (strcmp(psy->desc->name, "battery"))
		goto out;

	schedule_delayed_work(&data->psy_misc_work, 500);

out:
        return NOTIFY_DONE;

}

void tc_psy_misc_init(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	INIT_DELAYED_WORK(&data->psy_misc_work, psy_misc_work_handler);
	data->psy_nb.notifier_call = tc_psy_misc_event;
	power_supply_reg_notifier(&data->psy_nb);
}

static int tc_chg1_disable_vote_callback(struct votable *votable, void *arg, int value, const char *client)
{
	int ret = 0;
	bool enable = !value;
	struct tc_charger *info = arg; 
	struct tc_data *data = info->data;
	struct charger_data *pdata = &info->data->chg_data[CHG1_SETTING];

	tchr_info("vote chg1_en = %d\n", enable);

	charger_dev_enable(data->chg1_dev, enable);
	pdata->chg_en = enable;

	return ret;
}

static int tc_chg2_disable_vote_callback(struct votable *votable, void *arg, int value, const char *client)
{
	int ret = 0;
	bool enable = !value;
	struct tc_charger *info = arg; 
	struct tc_data *data = info->data;
	struct charger_data *pdata2 = &info->data->chg_data[CHG2_SETTING];

	tchr_info("vote chg2_en = %d\n", enable);

	charger_dev_enable(data->chg2_dev, enable);
	pdata2->chg_en = enable;

	/* chg2 change requires a redistribution of current */
	rerun_election(data->total_ichg_vote);
	rerun_election(data->total_aicr_vote);
	return ret;
}

static int tc_chg1_hiz_vote_callback(struct votable *votable, void *arg, int value, const char *client)
{
	int ret = 0;
	bool enable = !!value;
	struct tc_charger *info = arg; 
	struct tc_data *data = info->data;
	struct charger_data *pdata = &info->data->chg_data[CHG1_SETTING];

	tchr_info("vote chg1_hiz = %d\n", enable);

	charger_dev_enable_hz(data->chg1_dev, enable);
	pdata->hiz = enable;

	return ret;
}

static int tc_chg2_hiz_vote_callback(struct votable *votable, void *arg, int value, const char *client)
{
	int ret = 0;
	bool enable = !!value;
	struct tc_charger *info = arg; 
	struct tc_data *data = info->data;
	struct charger_data *pdata2 = &info->data->chg_data[CHG2_SETTING];

	tchr_info("vote chg2_hiz = %d\n", enable);

	charger_dev_enable_hz(data->chg2_dev, enable);
	pdata2->hiz = enable;

	return ret;
}

static int tc_chg1_mivr_vote_callback(struct votable *votable, void *arg, int value, const char *client)
{
	int ret = 0;
	struct tc_charger *info = arg; 
	struct tc_data *data = info->data;
	struct charger_data *pdata = &info->data->chg_data[CHG1_SETTING];

	tchr_info("vote chg1_mivr = %d\n", value);

	charger_dev_set_mivr(data->chg1_dev, value);
	pdata->mivr = value;

	return ret;
}

static int tc_chg2_mivr_vote_callback(struct votable *votable, void *arg, int value, const char *client)
{
	int ret = 0;
	struct tc_charger *info = arg; 
	struct tc_data *data = info->data;
	struct charger_data *pdata2 = &info->data->chg_data[CHG2_SETTING];

	tchr_info("vote chg2_mivr = %d\n", value);

	charger_dev_set_mivr(data->chg2_dev, value);
	pdata2->mivr = value;

	return ret;
}

static int tc_total_aicr_vote_callback(struct votable *votable, void *arg, int value, const char *client)
{
	int ret = 0;
	struct tc_charger *info = arg; 
	struct tc_data *data = info->data;
	struct charger_data chg1_pdata;
	struct charger_data chg2_pdata;
	struct charger_data *pdata2 = &info->data->chg_data[CHG2_SETTING];

	tchr_info("vote aicr = %d\n", value);

	if (pdata2->chg_en) {
		chg2_pdata.input_current_limit = (long)value * data->curr_ratio / 1000 / 100000 * 100000;
		chg2_pdata.input_current_limit = max(chg2_pdata.input_current_limit, 100000);
		chg1_pdata.input_current_limit = value - chg2_pdata.input_current_limit;
	} else {
		chg1_pdata.input_current_limit = value;
		chg2_pdata.input_current_limit = 0;
	}

	vote(data->chg1_aicr_vote, TOTAL_SET_VOTER, true,
			chg1_pdata.input_current_limit);
	vote(data->chg2_aicr_vote, TOTAL_SET_VOTER, true,
			chg2_pdata.input_current_limit);

	return ret;
}

static int tc_chg1_aicr_vote_callback(struct votable *votable, void *arg, int value, const char *client)
{
	int ret = 0;
	struct tc_charger *info = arg; 
	struct tc_data *data = info->data;
	struct charger_data *pdata = &info->data->chg_data[CHG1_SETTING];

	tchr_info("vote chg1_aicr = %d\n", value);

	charger_dev_set_input_current(data->chg1_dev, value);
	pdata->input_current_limit = value;

	return ret;
}

static int tc_chg2_aicr_vote_callback(struct votable *votable, void *arg, int value, const char *client)
{
	int ret = 0;
	struct tc_charger *info = arg; 
	struct tc_data *data = info->data;
	struct charger_data *pdata2 = &info->data->chg_data[CHG2_SETTING];

	tchr_info("vote chg2_aicr = %d\n", value);

	charger_dev_set_input_current(data->chg2_dev, value);
	pdata2->input_current_limit = value;

	return ret;
}

static int tc_total_ichg_vote_callback(struct votable *votable, void *arg, int value, const char *client)
{
	int i, ret = 0;
	struct tc_charger *info = arg; 
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	struct charger_data chg1_pdata;
	struct charger_data chg2_pdata;
	struct charger_data *pdata2 = &info->data->chg_data[CHG2_SETTING];

	tchr_info("vote ichg = %d\n", value);

	if (pdata2->chg_en) {
		for (i = 0; i < desc->chg_tab_len; i++) {
			if (value < desc->chg_tab[i].total_ichg)
				continue;
	
			chg2_pdata.charging_current_limit = desc->chg_tab[i].secondary_ichg;
			chg1_pdata.charging_current_limit = value - chg2_pdata.charging_current_limit;
			data->curr_ratio = (long)chg2_pdata.charging_current_limit * 1000 / value;
			break;
		}
	} else {
		chg1_pdata.charging_current_limit = value;
		chg2_pdata.charging_current_limit = 0;
	}

	vote(data->chg1_ichg_vote, TOTAL_SET_VOTER, true,
			chg1_pdata.charging_current_limit);
	vote(data->chg2_ichg_vote, TOTAL_SET_VOTER, true,
			chg2_pdata.charging_current_limit);

	return ret;
}

static int tc_chg1_ichg_vote_callback(struct votable *votable, void *arg, int value, const char *client)
{
	int ret = 0;
	struct tc_charger *info = arg; 
	struct tc_data *data = info->data;
	struct charger_data *pdata = &info->data->chg_data[CHG1_SETTING];

	tchr_info("vote chg1_ichg = %d\n", value);

	charger_dev_set_charging_current(data->chg1_dev, value);
	pdata->charging_current_limit = value;

	return ret;
}

static int tc_chg2_ichg_vote_callback(struct votable *votable, void *arg, int value, const char *client)
{
	int ret = 0;
	struct tc_charger *info = arg; 
	struct tc_data *data = info->data;
	struct charger_data *pdata2 = &info->data->chg_data[CHG2_SETTING];

	tchr_info("vote chg2_ichg = %d\n", value);

	charger_dev_set_charging_current(data->chg2_dev, value);
	pdata2->charging_current_limit = value;

	return ret;
}

static int tc_sw_vbus_ovp_vote_callback(struct votable *votable, void *arg, int value, const char *client)
{
	struct tc_charger *info = arg;
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	int hw_ovp_limit = desc->charger_voltage_ovp[CHARGER_VOLTAGE_SWITCH_BASIC];

	tchr_info("vote sw vbus ovp = %d\n", value);

	data->max_charger_voltage = value;

	tc_disable_hw_ovp(value > hw_ovp_limit ? true : false);

	return 0;
}

void tc_chg_vote_init(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;

	/* hiz vote */
	data->chg1_hiz_vote = create_votable("chg1_hiz", VOTE_SET_ANY,
			tc_chg1_hiz_vote_callback, info);

	data->chg2_hiz_vote = create_votable("chg2_hiz", VOTE_SET_ANY,
			tc_chg2_hiz_vote_callback, info);

	/* chg en vote */
	data->chg1_disable_vote = create_votable("chg1_disable", VOTE_SET_ANY,
			tc_chg1_disable_vote_callback, info);

	data->chg2_disable_vote = create_votable("chg2_disable", VOTE_SET_ANY,
			tc_chg2_disable_vote_callback, info);

	/* mivr vote */
	data->chg1_mivr_vote = create_votable("chg1_mivr", VOTE_MAX,
			tc_chg1_mivr_vote_callback, info);

	data->chg2_mivr_vote = create_votable("chg2_mivr", VOTE_MAX,
			tc_chg2_mivr_vote_callback, info);

	/* aicr vote */
	data->total_aicr_vote = create_votable("total_aicr", VOTE_MIN,
		   tc_total_aicr_vote_callback, info);

	data->chg1_aicr_vote = create_votable("chg1_aicr", VOTE_MIN,
		   tc_chg1_aicr_vote_callback, info);

	data->chg2_aicr_vote = create_votable("chg2_aicr", VOTE_MIN,
		   tc_chg2_aicr_vote_callback, info);

	/* ichg vote */
	data->total_ichg_vote = create_votable("total_ichg", VOTE_MIN,
			tc_total_ichg_vote_callback, info);

	data->chg1_ichg_vote = create_votable("chg1_ichg", VOTE_MIN,
			tc_chg1_ichg_vote_callback, info);

	data->chg2_ichg_vote = create_votable("chg2_ichg", VOTE_MIN,
			tc_chg2_ichg_vote_callback, info);

	/* SW VBUS OVP vote */
	data->sw_vbus_ovp_vote = create_votable("sw_vbus_ovp", VOTE_MAX,
			tc_sw_vbus_ovp_vote_callback, info);

	/* init param */
	vote(data->chg1_hiz_vote, CHG_THREAD_VOTER, false, 0);

	vote(data->chg2_hiz_vote, CHG_THREAD_VOTER, false, 0);

	vote(data->chg1_disable_vote, CHG_THREAD_VOTER, false, 0);

	vote(data->chg2_disable_vote, CHG_THREAD_VOTER, true, 0);

	vote(data->total_ichg_vote, CHG_THREAD_VOTER, true, 2000000);

	vote(data->total_aicr_vote, CHG_THREAD_VOTER, true, 500000);

	vote(data->chg1_mivr_vote, CHG_THREAD_VOTER,
			true, desc->min_charger_voltage);

	vote(data->chg2_mivr_vote, CHG_THREAD_VOTER,
			true, desc->min_charger_voltage);

	vote(data->sw_vbus_ovp_vote, CHG_THREAD_VOTER, true,
			desc->charger_voltage_ovp[CHARGER_VOLTAGE_SWITCH_BASIC]);

}

int tchg_alg_event(struct notifier_block *notifier,
			unsigned long event, void *arg)
{
	tchr_err("%s: evt:%lu\n", __func__, event);

	return NOTIFY_DONE;
}

void tc_check_batt_working_status(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	union com_propval prop = {.intval = 0};

	if (!desc->support_dual_battery) {
		data->batt_online_status = GAUGE_SINGLE_MASTER_BATT_ONLINE;
		return;
	}

	tran_dev_get_prop(data->gauge_dev, TRAN_PROP_BATT_WORKING_STATUS, &prop);

	data->batt_online_status = prop.intval;

	tchr_info("%s: batt online status = %d\n",
			__func__, data->batt_online_status);
}

static int gauge_notifier_callback(struct notifier_block *nb,
                    unsigned long event, void *v)
{
	struct tc_data *data = container_of(nb,
			struct tc_data, gauge_notifier);
	struct tc_charger *info = data->info;
	
	tchr_info("gauge notify event = %lu\n", event);

	switch (event) {
	case TRAN_DEV_NOTIFY_BATT_ONLINE_CHANGE:
		tc_check_batt_working_status(info);
		if (data->batt_online_status == GAUGE_DUAL_BATT_ONLINE) {
			alarm_cancel(&data->dual_batt_eoc_timer);
			data->dual_batt_enable_eoc = false;
		}
		_wake_up_charger(info);
		break;
	case TRAN_DEV_NOTIFY_BATT_MASTER_CHG_FULL:
	case TRAN_DEV_NOTIFY_BATT_SLAVE_CHG_FULL:
		tc_start_dual_batt_eoc_timer(info);
		break;
	default:
		break;
	}

	return 0;
}

static bool charger_init_algo(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	struct tchg_alg_device *alg;
	int i, idx = 0;
	int ret = 0;

	data->chg_psy = power_supply_get_by_name("charger");
	if (IS_ERR_OR_NULL(data->chg_psy)) {
		tchr_err("%s: devm power fail to get chg_psy\n", __func__);
		return false;
	}

	/* primary_chg */
	data->chg1_dev = get_charger_by_name("primary_chg");
	if (!data->chg1_dev) {
		tchr_err("%s, *** Error : can't find primary charger ***\n"
			, __func__);
		return false;
	}

	if (data->chg1_dev != NULL && info->algo.do_chg1_event != NULL) {
		data->chg1_nb.notifier_call = info->algo.do_chg1_event;
		register_charger_device_notifier(data->chg1_dev,
						&data->chg1_nb);
		charger_dev_set_drvdata(data->chg1_dev, info);
		tchr_err("register chg1 notifier done\n");
	}

	/* secondary_chg */
	data->chg2_dev = get_charger_by_name("secondary_chg");
	if (!data->chg2_dev) {
		tchr_err("can't find secondary charger ***\n");
	}

	if (data->chg2_dev != NULL && info->algo.do_chg2_event != NULL) {
		data->chg2_nb.notifier_call = info->algo.do_chg2_event;
		register_charger_device_notifier(data->chg2_dev,
						&data->chg2_nb);
		charger_dev_set_drvdata(data->chg2_dev, info);
		tchr_err("register chg2 notifier done\n");
	}

	/* primary_dvchg */
	data->dvchg1_dev = get_charger_by_name("primary_dvchg");
	if (!data->dvchg1_dev) {
		tchr_err("can't find primary divider charger ***\n");
	}

	if (data->dvchg1_dev != NULL && info->algo.do_dvchg1_event != NULL) {
		data->dvchg1_nb.notifier_call = info->algo.do_dvchg1_event;
		register_charger_device_notifier(data->dvchg1_dev,
						&data->dvchg1_nb);
		charger_dev_set_drvdata(data->dvchg1_dev, info);
		tchr_err("register dvchg chg1 notifier done\n");
	}

	/* secondary_dvchg */
	data->dvchg2_dev = get_charger_by_name("secondary_dvchg");
	if (!data->dvchg2_dev) {
		tchr_err("can't find secondary divider charger ***\n");
	}

	if (data->dvchg2_dev != NULL && info->algo.do_dvchg2_event != NULL) {
		data->dvchg2_nb.notifier_call = info->algo.do_dvchg2_event;
		register_charger_device_notifier(data->dvchg2_dev,
						 &data->dvchg2_nb);
		charger_dev_set_drvdata(data->dvchg2_dev, info);
		tchr_err("register dvchg chg2 notifier done\n");
	}

	/* third_divider_chg */
	data->dvchg3_dev = get_charger_by_name("third_divider_chg");
	if (!data->dvchg3_dev) {
		tchr_err("can't find third_divider_chg divider charger ***\n");
	}

	if (data->dvchg3_dev != NULL && info->algo.do_dvchg3_event != NULL) {
		data->dvchg3_nb.notifier_call = info->algo.do_dvchg3_event;
		register_charger_device_notifier(data->dvchg3_dev,
						 &data->dvchg3_nb);
		charger_dev_set_drvdata(data->dvchg3_dev, info);
		tchr_err("register dvchg chg2 notifier done\n");
	}

	/* wireless charger */
	data->wlsc_dev = get_charger_by_name("wireless_manager");
	if (!data->wlsc_dev) {
		tchr_err("can't find wireless charger device ***\n");
	}

	data->pd_nb.notifier_call = tc_chg_tcpc_notifier_call;
	register_tc_tcpc_notifier(&data->pd_nb);

	data->chg_alg_nb.notifier_call = tchg_alg_event;

	for (i = 0; i < desc->support_alg_cnt; i++) {
		alg = get_tchg_alg_by_name(desc->support_alg[i]);
		data->alg[idx] = alg;
		if (IS_ERR_OR_NULL(alg)) {
			tchr_err("get %s fail\n", desc->support_alg[i]);
			continue;
		}

		tchr_err("get %s success\n", desc->support_alg[i]);
		alg->alg_id = tchg_alg_id(desc->support_alg[i]);
		tchg_alg_init_algo(alg);
		register_tchg_alg_notifier(alg, &data->chg_alg_nb);
		idx++;
	}

	if (idx < desc->support_alg_cnt) {
		tchr_err("the found algo num is not match support_alg_cnt\n");
		return false;
	}

	data->gauge_dev = tran_get_by_name("tc_gauge");
	if (IS_ERR_OR_NULL(data->gauge_dev)) {
		pr_err("%s: get gauge_dev fail\n", __func__);
		return false;
	}

	data->gauge_notifier.notifier_call = gauge_notifier_callback;
	ret = register_tran_device_notifier(data->gauge_dev,
				&data->gauge_notifier);
	if (ret != 0) {
		pr_err("register gauge notify failed, ret = %d\n", ret);
	}
	tc_check_batt_working_status(info);
	return true;
}

static int tc_get_charger_enegry(struct tc_charger *info)
{
	int enegry = 100;
	struct tc_data *data = info->data;
	struct smart_chg_data *smtchg_data = &data->smtchg_data;
	struct bypass_chg_data *bypasschg_data = &data->bypasschg_data;

	if (smtchg_data->smartchg_en &&
		smtchg_data->smartchg_energy != SMTCHG_ENERGY_MIN) {

		enegry = min(enegry, smtchg_data->smartchg_energy);
	}

	if (bypasschg_data->bypass_en &&
		bypasschg_data->bypass_energy != BYPASS_ENERGY_CLOSE) {

		enegry = min(enegry, bypasschg_data->bypass_energy);
	}

	return enegry;
}

static int tc_get_vbus_measure_method(struct tc_charger *info)
{
	int i = 0, index = 0;
	int method = MEASURE_BY_PMIC;
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	int vbus_ovp_voltage = data->max_charger_voltage;

	for (i = 0; i < CHARGER_VOLTAGE_MAX; i++) {
		if (vbus_ovp_voltage == desc->charger_voltage_ovp[i])
			break;
	}
	index = i < CHARGER_VOLTAGE_MAX ? i : 0;
	method = desc->vbus_measure_method[index];

	pr_info("%s :vbus_ovp_voltage = %d, index = %d, method = %d\n", __func__, vbus_ovp_voltage, index, method);

	return method;
}

static int tc_charger_get_property(struct tran_device *dev,
				enum tran_common_prop prop,
				union com_propval *val)
{
	int ret = 0;
	struct tc_charger *info = tran_get_data(dev);
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;

	switch (prop) {
	case TRAN_PROP_CHARGER_CURRENT_LIMIT:
		val->intval = data->total_pdata.charging_current_limit;
		break;
	case TRAN_PROP_LOG_LEVEL:
		val->intval = desc->log_level;
		break;
	case TRAN_PROP_MONKEY_FLAG:
		val->intval = data->monkey_flag;
		break;
	case TRAN_PROP_GET_BYPASS_ENERGY:
		val->intval = tc_get_charger_enegry(info);
		break;
	case TRAN_PROP_TC_CHG_TYPE:
		val->intval = tc_get_chg_type(info);
		break;
	case TRAN_PROP_RUNNING_VOL:
		val->intval = tc_get_alg_running_vol(info);
		break;
	case TRAN_PROP_CHG_CV:
		val->intval = data->vbat_cv;
		break;
	case TRAN_PROP_CHG_EOC:
		val->intval = data->vbat_eoc;
		break;
	case TRAN_PROP_CHG_MASTER_CC:
		val->intval = data->master_vbat_cc;
		break;
	case TRAN_PROP_CHG_MASTER_CV:
		val->intval = data->master_batt_vbat[BATT_VBAT_HIGH];
		break;
	case TRAN_PROP_CHG_MASTER_EOC:
		val->intval = data->master_vbat_eoc;
		break;
	case TRAN_PROP_CHG_SLAVE_CC:
		val->intval = data->slave_vbat_cc;
		break;
	case TRAN_PROP_CHG_SLAVE_CV:
		val->intval = data->slave_batt_vbat[BATT_VBAT_HIGH];
		break;
	case TRAN_PROP_CHG_SLAVE_EOC:
		val->intval = data->slave_vbat_eoc;
		break;
	case TRAN_PROP_GET_SOC_DECIMAL_SUPPORT:
		val->intval = desc->support_real_soc_decimal;
		break;
	case TRAN_PROP_GET_LOW_CV_STATUS:
		val->intval = data->is_low_cv_status;
		break;
	case TRAN_PROP_GET_BATTERY_TEMPERATURE:
		val->intval = tc_get_battery_temperature();
		break;
	case TRAN_PROP_GET_CHARGER_CURRENT:
		val->intval = tc_get_battery_current();
		break;
	case TRAN_PROP_IS_FFC_CHR:
		val->intval = data->is_ffc;
		break;
	case TRAN_PROP_FFC_ALG_ID:
		val->intval = data->ffc_alg_id;
		break;
	case TRAN_PROP_MAX_CHG_CUR_LMT:
		val->intval = desc->max_charging_current_limit;
		break;
	case TRAN_PROP_PROTOCOL_DONE_STATUS:
		val->intval = data->wait_protocol_done;
		break;
	case TRAN_PROP_GET_VBUS_MEASURE_METHOD:
		val->intval = tc_get_vbus_measure_method(info);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int tc_charger_set_property(struct tran_device *dev,
					   enum tran_common_prop prop,
					   const union com_propval *val)
{
	int ret = 0;
	struct tc_charger *info = tran_get_data(dev);
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;

	switch (prop) {
	case TRAN_PROP_WAKE_UP_CHARGER:
		_wake_up_charger(info);
		break;
	case TRAN_PROP_SET_AICHG_VOTE:
		tc_charger_aidischg_vote(info, val->intval);
		break;
	case TRAN_PROP_SET_BYPASS_CHG_VOTE:
		tc_charger_bypass_dischg_vote(info, val->intval);
		break;
	case TRAN_PROP_SEND_UEVENT:
		ret = tc_charger_send_uevent(info, val->ptr, val->intval);
		break;
	case TRAN_PROP_CHARGING_ANIMATION:
		tc_chgstat_pump(info);
		break;
	case TRAN_PROP_WATER_CTRL_CHG:
		water_detect_dischg_vote(info, val->intval ? true : false);
		break;
	case TRAN_PROP_PORT_BURN_VOTE:
		port_burn_dischg_vote(info, val->intval ? true : false);
		break;		
	case TRAN_PROP_SET_POLLING_INTERVAL:
		data->polling_interval = val->intval;
		break;
	case TRAN_PROP_SET_BATT_RAW_SOC:
		if (desc->support_real_soc_decimal) {
			data->real_soc_data->raw_soc = val->intval;
			pr_info("debug raw soc:%d\n", val->intval);
		}
		break;
	case TRAN_PROP_TRAN_CUSTOM_DISCHG:
		tran_custom_dischg_vote(info, val->intval ? true : false);
		break;
	case TRAN_PROP_TRAN_VOTE_REFRESH:
		if(val->intval)	
			tc_chg_vote_refresh(info);
		else
			tc_chg_vote_reset(info);
		break;
	case TRAN_PROP_IS_FFC_CHR:
		data->is_ffc = val->intval;
		break;
	case TRAN_PROP_FFC_ALG_ID:
		data->ffc_alg_id = val->intval;
		break;
	/* Add for pd test */
	case TRAN_PROP_USB_PD_VOTE_AICR:
		if (val->intval >= 0) {
			vote(data->total_aicr_vote, PD_CHG_VOTER, true, val->intval);
			pr_info("PD set AICR = %duA\n", val->intval);
		} else {
			vote(data->total_aicr_vote, PD_CHG_VOTER, false, 0);
			pr_info("reset PD AICR vote!\n");
		}
		_wake_up_charger(info);
		break;
	case TRAN_PROP_USB_PD_SET_POWER_PATH:
		/*ignore power_path operation if wireless type*/
		if (data->alias_type == TC_WIRELESS)
			return ret;
		ret = charger_dev_enable_powerpath(data->chg1_dev, val->intval);
		pr_info("set power_path = %d\n", val->intval);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static struct tran_ops tc_charger_ops = {
	.get_prop = tc_charger_get_property,
	.set_prop = tc_charger_set_property,
};

static int tc_charger_prop_init(struct tc_charger *info)
{
	struct tc_data *data = info->data;

	data->tc_charger_props.alias_name = "tc_charger";
	data->tc_charger_dev = tran_device_register("tc_charger",
						&info->pdev->dev, info,
						&tc_charger_ops,
						&data->tc_charger_props);
	if (IS_ERR_OR_NULL(data->tc_charger_dev))
		return -ENODEV;

	return 0;
}

static enum alarmtimer_restart tc_alarm_timer_long_life_rechg_func(struct alarm *alarm, ktime_t now)
{
	struct tc_data *data = container_of(alarm, struct tc_data, long_life_rechg_timer);
	struct tc_charger *info = data->info;

	data->long_life_rechg_work_flag = true;

	_wake_up_charger(info);

	tchr_info("%s: alarm recharger timer func run succ\n", __func__);

	return ALARMTIMER_NORESTART;
}

void tc_start_alarm_recharger_timer(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	struct timespec64 time, time_now;
	ktime_t temp_time;
	ktime_t ktime;
	int ret = 0;

	/* If the timer was already set, cancel it */
	ret = alarm_try_to_cancel(&data->long_life_rechg_timer);
	if (ret < 0) {
		tchr_err("%s: callback was running, skip timer\n", __func__);
		return;
	}

	temp_time = ktime_get_boottime();
	time_now = ktime_to_timespec64(temp_time);

	time.tv_sec = time_now.tv_sec + desc->long_life_rechg_time; // default 180s
	time.tv_nsec = 0;

	ktime = ktime_set(time.tv_sec, time.tv_nsec);

	alarm_start(&data->long_life_rechg_timer, ktime);

	tchr_err("%s: alarm long life rechg timer start:%lld %lld\n",
		__func__, (long long)time.tv_sec, (long long)time.tv_nsec);
}

static void tc_init_long_life_recharger_timer(struct tc_charger *info)
{
	struct tc_data *data = info->data;

	alarm_init(&data->long_life_rechg_timer, ALARM_BOOTTIME,
		tc_alarm_timer_long_life_rechg_func);

	tchr_err("%s: alarm recharger timer init\n", __func__);
}

static enum alarmtimer_restart
	tc_alarm_timer_eoc_func(struct alarm *alarm, ktime_t now)
{
	struct tc_data *data = container_of(alarm,
		struct tc_data, dual_batt_eoc_timer);
	struct tc_charger *info = data->info;

	data->dual_batt_enable_eoc = true;

	_wake_up_charger(info);

	tchr_info("%s: alarm dual battery enable eoc func run succ\n", __func__);

	return ALARMTIMER_NORESTART;
}

void tc_start_dual_batt_eoc_timer(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	struct timespec64 time, time_now;
	ktime_t temp_time;
	ktime_t ktime;
	int ret;

	/* If the timer was already set, cancel it */
	ret = alarm_try_to_cancel(&data->dual_batt_eoc_timer);
	if (ret < 0) {
		pr_err("%s: callback was running, skip timer\n", __func__);
		return;
	}

	temp_time = ktime_get_boottime();
	time_now = ktime_to_timespec64(temp_time);

	time.tv_sec = time_now.tv_sec + desc->dual_batt_eoc_delay_time;
	time.tv_nsec = 0;

	ktime = ktime_set(time.tv_sec, time.tv_nsec);

	alarm_start(&data->dual_batt_eoc_timer, ktime);

	pr_info("%s: alarm hweoc timer start:%d, %lld %lld\n", __func__,
		ret, (long long)time.tv_sec, (long long)time.tv_nsec);
}

static void tc_init_dual_batt_eoc_timer(struct tc_charger *info)
{
	struct tc_data *data = info->data;

	alarm_init(&data->dual_batt_eoc_timer, ALARM_BOOTTIME,
		tc_alarm_timer_eoc_func);

	pr_info("%s: alarm dual batt eoc timer init\n", __func__);
}

void tc_wakeup_decimal_soc_report_thread(struct real_soc_decimal_data *data)
{
	atomic_set(&data->wakeup_decimal_thread, 1);
	wake_up_interruptible(&data->decimal_wq);
}

static enum alarmtimer_restart tc_decimal_soc_report_timer_cb(struct alarm *alarm, ktime_t now)
{
	struct real_soc_decimal_data *data = container_of(alarm,
		struct real_soc_decimal_data, decimal_timer);

	tc_wakeup_decimal_soc_report_thread(data);

	return ALARMTIMER_NORESTART;
}

static int tc_decimal_soc_report_thread(void *param)
{
	struct tc_charger *info = param;
	struct tc_desc *desc = info->desc;
	struct real_soc_decimal_data *data = info->data->real_soc_data;
	u32 sec, ms;
	ktime_t ktime;
	char prop_buf[32] = {0};
	char *envp[2] = {NULL, NULL};

	/* init event param */
	snprintf(prop_buf, sizeof(prop_buf), "DEC_SOC");
	envp[0] = prop_buf;

	while (!kthread_should_stop()) {
		wait_event_interruptible(data->decimal_wq,
					atomic_read(&data->wakeup_decimal_thread) || kthread_should_stop());

		__pm_stay_awake(data->decimal_soc_wakelock);
		if (kthread_should_stop()) {
			__pm_relax(data->decimal_soc_wakelock);
			goto out;
		}

		atomic_set(&data->wakeup_decimal_thread, 0);

		if (!atomic_read(&data->start_decimal_soc)) {
			pr_info("%s: stop decimal soc report thread\n", __func__);
			goto stop;
		}

		// send uevent
		kobject_uevent_env(&info->pdev->dev.kobj, KOBJ_CHANGE, envp);

		sec = desc->decimal_report_freq / 1000;
		ms = desc->decimal_report_freq % 1000;
		ktime = ktime_set(sec, MS_TO_NS(ms));
		alarm_start_relative(&data->decimal_timer, ktime);
stop:
		__pm_relax(data->decimal_soc_wakelock);
	}
out:
	return 0;
}

static enum alarmtimer_restart tc_alarm_charge_decimal_timeout_func(struct alarm *alarm, ktime_t now)
{
	struct tc_data *tdata = NULL;
	union com_propval val = {0, };
	struct real_soc_decimal_data *data = container_of(alarm, struct real_soc_decimal_data, charge_decimal_timeout_timer);
	if (IS_ERR_OR_NULL(data))
		return ALARMTIMER_NORESTART;
	tdata =  container_of(&data, struct tc_data, real_soc_data);

	if (IS_ERR_OR_NULL(tdata->tran_batt_dev))
		tdata->tran_batt_dev = tran_get_by_name("tran_batt");

	// switch soc update freq
	val.intval = SOC_ZERO_DECIMAL;
	tran_dev_set_prop(tdata->tran_batt_dev, TRAN_PROP_SET_SOC_DECIMAL_RATE, &val);

	atomic_set(&data->start_decimal_soc, 0);

	tchr_info("%s: run succ\n", __func__);

	return ALARMTIMER_NORESTART;
}

void tc_start_charge_decimal_timeout_alarm_timer(struct real_soc_decimal_data *data, u32 time_out)
{
	struct timespec64 time, time_now;
	ktime_t temp_time;
	ktime_t ktime;
	int ret;

	/* If the timer was already set, cancel it */
	ret = alarm_try_to_cancel(&data->charge_decimal_timeout_timer);
	if (ret < 0) {
		pr_err("%s: callback was running, skip timer\n", __func__);
		return;
	}

	temp_time = ktime_get_boottime();
	time_now = ktime_to_timespec64(temp_time);

	time.tv_sec = time_now.tv_sec + time_out;
	time.tv_nsec = 0;

	ktime = ktime_set(time.tv_sec, time.tv_nsec);

	alarm_start(&data->charge_decimal_timeout_timer, ktime);

	pr_err("%s: alarm charge decimal timeout timer start:%lld %09ld\n", __func__,
		(long long)time.tv_sec, time.tv_nsec);
}

static void tc_init_charge_decimal_timeout_timer(struct real_soc_decimal_data *data)
{
	alarm_init(&data->charge_decimal_timeout_timer, ALARM_BOOTTIME,
		tc_alarm_charge_decimal_timeout_func);

	alarm_init(&data->decimal_timer, ALARM_REALTIME,
		tc_decimal_soc_report_timer_cb);

	tchr_info("%s: alarm charge decimal timeout timer & report timer init\n", __func__);
}

static void tc_bypass_charger_hdlr(struct tc_charger *info)
{
	union com_propval vote_val = {0};
	struct tc_data *data = info->data;
	struct bypass_chg_data *bypasschg_data = &data->bypasschg_data;
	int val = data->wait_protocol_array[BYPASS_CHG].value;

	if (val == BYPASS_ENABLE) {
		bypasschg_data->bypass_en = true;
		bypasschg_data->bypass_energy = BYPASS_ENERGY_CLOSE;
	} else if(val == BYPASS_DISABLE) {
		bypasschg_data->bypass_en = false;
		bypasschg_data->bypass_energy = BYPASS_ENERGY_100;
	} else if (val >= BYPASS_MIN_ENERGY && val <= BYPASS_ENERGY_100) {
		bypasschg_data->bypass_en = true;
		bypasschg_data->bypass_energy = val;
	} else {
		pr_err("error argument!\n");
		return;
	}

	if (bypasschg_data->bypass_en &&
		bypasschg_data->bypass_energy == BYPASS_ENERGY_CLOSE) {

		vote_val.intval = true;
		tran_dev_set_prop(data->tc_charger_dev,
			TRAN_PROP_SET_BYPASS_CHG_VOTE, &vote_val);

	} else {
		vote_val.intval = false;
		tran_dev_set_prop(data->tc_charger_dev,
			TRAN_PROP_SET_BYPASS_CHG_VOTE, &vote_val);
	}

}

static void tc_smart_charger_hdlr(struct tc_charger *info)
{
	struct tc_desc *desc = info->desc;
	struct tc_data *data = info->data;
	struct smart_chg_data *smtchg_data = &data->smtchg_data;
	int val = data->wait_protocol_array[SMART_CHG].value;

	if (val == SMTCHG_DISABLE) {
		smtchg_data->smartchg_en = false;
		smtchg_data->smartchg_energy = SMTCHG_ENERGY_100;
	} else if (val == SMTCHG_ENERGY_MIN) {
		smtchg_data->smartchg_en = true;
		smtchg_data->smartchg_energy = SMTCHG_ENERGY_MIN;
	} else if (val >= 1 && val <= 100) {
		smtchg_data->smartchg_en = true;
		smtchg_data->smartchg_energy = val;
	} else {
		pr_err("error argument!\n");
		return;
	}

	if (smtchg_data->smartchg_en &&
		smtchg_data->smartchg_energy == SMTCHG_ENERGY_MIN) {
		/*adjust current for min current*/
		vote(data->total_ichg_vote, SMTCHG_VOTER,
			true, desc->smtchg_curr_min);
	} else {
		vote(data->total_ichg_vote, SMTCHG_VOTER,
			false, desc->smtchg_curr_min);
	}

	_wake_up_charger(info);
}

static void
(*tc_wait_protocol_hdlr[FEATURE_MAX])(struct tc_charger *info) = {
	[BYPASS_CHG] = tc_bypass_charger_hdlr,
	[SMART_CHG] = tc_smart_charger_hdlr,
};

static void tc_wait_protocol_work(struct work_struct *work)
{
	struct tc_data *data = container_of(to_delayed_work(work),
			struct tc_data, wait_protocol_work);
	struct tc_charger *info = data->info;
	int i;

	mutex_lock(&data->wait_protocol_lock);
	if (data->plug_in && !data->wait_protocol_done &&
		data->total_time.tv_sec <= 20) {
		schedule_delayed_work(&data->wait_protocol_work, 100);
		goto out;
	}

	for (i = 0; i < FEATURE_MAX; i++) {
		if (!data->wait_protocol_array[i].changed)
			continue;
		tc_wait_protocol_hdlr[i](info);
		data->wait_protocol_array[i].changed = false;
	}
out:
	mutex_unlock(&data->wait_protocol_lock);

}

static int tc_charger_probe(struct platform_device *pdev)
{
	struct tc_charger *info = NULL;
	struct tc_data *data = NULL;
	struct tc_desc *desc = NULL;
	struct real_soc_decimal_data *real_soc_data = NULL;
	int ret = 0;
	char *name = NULL;

	tchr_err("%s: starts\n", __func__);

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	desc = devm_kzalloc(&pdev->dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	info->pdev = pdev;
	info->data = data;
	info->desc = desc;
	data->info = info;
	pinfo = info;
	platform_set_drvdata(pdev, info);

	data->bootmode = tc_get_boot_mode();
	data->boottype = tc_get_boot_type();
	data->atm_enabled = tc_get_atm_mode();
	data->batt_id = tc_get_batt_id();
	data->polling_interval = CHARGING_INTERVAL;
	tc_charger_parse_dt(info, &pdev->dev);
	tc_detect_aging_mode(info);

	tc_basic_charger_init(info);

	mutex_init(&data->cable_out_lock);
	mutex_init(&data->charger_lock);
	mutex_init(&data->pd_lock);
	mutex_init(&data->notify_lock);
	mutex_init(&data->wait_protocol_lock);

	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s", "charger suspend wakelock");
	data->charger_wakelock = wakeup_source_register(NULL, name);
	spin_lock_init(&data->slock);

	init_waitqueue_head(&data->wait_que);
	tc_charger_init_timer(info);
	data->pm_notifier.notifier_call = charger_pm_event;
	if (register_pm_notifier(&data->pm_notifier)) {
		tchr_err("%s: register pm failed\n", __func__);
		return -ENODEV;
	}
	tc_psy_misc_init(info);
	tc_chg_vote_init(info);
	tc_charger_setup_files(pdev);
	tc_init_long_life_recharger_timer(info);
	tc_init_dual_batt_eoc_timer(info);

	data->is_charging = false;
	tc_multi_chg_para_init(info);

	kthread_run(charger_routine_thread, info, "charger_thread");

	INIT_DELAYED_WORK(&data->wait_protocol_work, tc_wait_protocol_work);
	INIT_DELAYED_WORK(&data->upload_msg_work, tc_chg_upload_msg_data_work);

	ret = tc_charger_prop_init(info);
	if (ret < 0) {
		tchr_info("register tc charger device failed\n");
	}

	if (desc->support_real_soc_decimal) {
		data->real_soc_data = devm_kzalloc(&pdev->dev, sizeof(*real_soc_data), GFP_KERNEL);
		if (IS_ERR_OR_NULL(data->real_soc_data)) {
			tchr_err("alloc memory failed\n");
			return -ENOMEM;
		}
		real_soc_data = data->real_soc_data;
		real_soc_data->decimal_soc_wakelock =
			wakeup_source_register(NULL, "decimal soc report wakelock");
		tc_init_charge_decimal_timeout_timer(real_soc_data);
		init_waitqueue_head(&real_soc_data->decimal_wq);
		kthread_run(tc_decimal_soc_report_thread, info, "decimal_soc_thread");
	}

	tchr_err("%s: done\n", __func__);
	return 0;
}

static int tc_charger_remove(struct platform_device *dev)
{
	return 0;
}

static void tc_charger_shutdown(struct platform_device *dev)
{
	struct tc_charger *info = platform_get_drvdata(dev);
	struct tc_data *data = info->data;
	int i;

	for (i = 0; i < MAX_ALG_NO; i++) {
		if (data->alg[i] == NULL)
			continue;
		tchg_alg_stop_algo(data->alg[i]);
	}
}

static const struct of_device_id tc_charger_of_match[] = {
	{.compatible = "tc,charger",},
	{},
};

MODULE_DEVICE_TABLE(of, tc_charger_of_match);

struct platform_device tc_charger_device = {
	.name = "charger",
	.id = -1,
};

static struct platform_driver tc_charger_driver = {
	.probe = tc_charger_probe,
	.remove = tc_charger_remove,
	.shutdown = tc_charger_shutdown,
	.driver = {
		   .name = "charger",
		   .of_match_table = tc_charger_of_match,
	},
};

static int __init tc_charger_init(void)
{
	return platform_driver_register(&tc_charger_driver);
}

late_initcall(tc_charger_init);

static void __exit tc_charger_exit(void)
{
	platform_driver_unregister(&tc_charger_driver);
}
module_exit(tc_charger_exit);

MODULE_DESCRIPTION("TC Driver");
MODULE_LICENSE("GPL");
