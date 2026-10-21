
// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#include <linux/alarmtimer.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include "tc_pe5.h"

void pe50_ab_ta_otp_stop_algo(struct pe50_algo_info *info,int ab_dev);
static int log_level = PE50_INFO_LEVEL;
module_param(log_level, int, 0644);

int pe50_get_log_level(void)
{
	return log_level;
}
EXPORT_SYMBOL_GPL(pe50_get_log_level);

#define MS_TO_NS(msec)		((msec) * (NSEC_PER_MSEC))

/* If there's no property in dts, these values will be applied */
static const struct pe50_algo_desc algo_desc_defval = {
	.polling_interval = 500,
	.ta_cv_ss_repeat_tmin = 25,
	.vbat_cv = 4450,
	.start_vbat_max = 4300,
	.idvchg_term = 500,
	.ita_level = {3000, 2700, 2400, 2000},
	.rcable_level = {250, 278, 313, 375},
	.idvchg_ss_init = 500,
	.idvchg_ss_step = 250,
	.idvchg_ss_step1 = 100,
	.idvchg_ss_step2 = 50,
	.idvchg_ss_step1_vbat = 4000,
	.idvchg_ss_step2_vbat = 4200,
	.ta_blanking = 500,
	.chg_time_max = 18000,
	.pe50_ab_retry_cnt=0,
	.idvchg_pps_term = 1000,
	.start_vbat_min =3400,
	.ifod_threshold = 200,
	.rechg_tbat_high = 43,
	.rechg_tbat_low = 17,
	.rsw_min = 20,
	.ircmp_rbat = 40,
	.ircmp_vclamp = 0,
	.default_spec = SUPPORT_SPEC_2_1,
	.support_spec = { 0, 0, 0, 0, 0 },
	.vta_cap_min = { 6800, 6800, 6800, 6800, 4000},
	.vta_cap_max = { 30500, 20500, 20500, 10500, 7000 },
	.ita_cap_min = { 1000, 1000, 1000, 1000, 1000 },
	.vbus_max_ovp = { 30500, 20500, 20500, 10500, 7000 },
	.support_switch_spec = false,
	.allow_not_check_ta_status = true,
	.project_power = 45,
	.project_pwr_ratio = 70,
	.ita_lmt_gap = 0,
	.min_ita_gap = 10,
	.support_dual_battery = false,
	.idvchg_level_multi = {3000, 6000, 9000, 12000},
	.pe5_ffc_gap = {20, 20, 20, 20, 20, 20},
	.pe50_vta_init = PE50_VTA_INIT,
	.pe50_ita_init = PE50_ITA_INIT,
	.pe50_default_vta_step = 20,
        .tta_level_def               =     {0     ,0     ,0     ,5     ,25    ,75    ,75    ,75    ,75    ,75    ,75    ,75    ,75    ,75    ,80    ,85    ,90},
        .tta_curlmt                  =     {-1    ,-1    ,-1    ,300   ,0     ,300   ,300   ,300   ,300   ,300   ,300   ,300   ,300   ,300   ,600   ,900   ,-1},
        .tta_recovery_area = 1,
        .tdvchg_level_def            =     {0     ,0     ,0     ,5     ,25    ,70    ,70    ,70    ,70    ,70    ,70    ,70    ,70    ,70    ,75    ,80    ,85},
        .tdvchg_curlmt               =     {-1    ,-1    ,-1    ,300   ,0     ,300   ,300   ,300   ,300   ,300   ,300   ,300   ,300   ,300   ,600   ,900   ,-1},
        .tdvchg_recovery_area = 1,
        .tbat_level_def              =     {15    ,15    ,15    ,22    ,25    ,35    ,35    ,35    ,35    ,35    ,35    ,35    ,35    ,35    ,40    ,43    ,45},
        .tbat_curlmt                 =     {-1    ,-1    ,0     ,0     ,0     ,2000  ,2000  ,2000  ,2000  ,2000  ,2000  ,2000  ,2000  ,2000  ,4000  ,5000  ,-1},
        .tbat_recovery_area = 1,
        .tpcb_level_def              =     {0     ,0     ,0     ,22    ,25    ,35    ,35    ,35    ,35    ,35    ,35    ,35    ,35    ,35    ,40    ,43    ,45},
        .tpcb_curlmt                 =     {-1    ,-1    ,0     ,0     ,0     ,2000  ,2000  ,2000  ,2000  ,2000  ,2000  ,2000  ,2000  ,2000  ,4000  ,5000  ,-1},
        .tpcb_recovery_area = 1,
        .tpa_level_def               =     {0     ,0     ,0     ,22    ,25    ,38    ,38    ,38    ,38    ,38    ,38    ,38    ,38    ,38    ,40    ,43    ,45},
        .tpa_curlmt                  =     {-1    ,-1    ,0     ,0     ,0     ,2000  ,2000  ,2000  ,2000  ,2000  ,2000  ,2000  ,2000  ,2000  ,4000  ,5000  ,-1},
        .tpa_recovery_area = 1,
	.sys_power_level_def         =     {0     ,0     ,400   ,800   ,1200  ,1200  ,1600  ,2000  ,2400  ,2800  ,3200  ,3600  ,4000  ,4400  ,4800  ,5200  ,50000},
	.sys_power_curlmt            =     {-1    ,-1    ,0     ,0     ,3300  ,3300  ,2800  ,2500  ,2300  ,2100  ,1900  ,1700  ,1500  ,1300  ,1300  ,1300  ,-1},
	.sys_power_recovery_area = 400,
	.supprot_multi_level_charging = false,
};

/* Check if there is error notification coming from H/W */
static bool pe50_is_hwerr_notified(struct pe50_algo_info *info)
{
	struct pe50_algo_data *data = info->data;
	bool err = false;
	u32 hwerr = PE50_HWERR_NOTIFY;

	mutex_lock(&data->notify_lock);
	if (data->ignore_ibusucpf)
		hwerr &= ~BIT(EVT_IBUSUCP_FALL);
	err = !!(data->notify & hwerr);
	if (err)
		PE50_ERR("H/W error(0x%08X)", hwerr);

	if(data->notify & BIT(EVT_IBUSUCP_FALL))
		pe50_ab_ibusucp_stop_algo(info,PE50_IBUS_UCP);

	mutex_unlock(&data->notify_lock);

	return err;
}

static void pe50_report_charing_animation(struct pe50_algo_info *info)
{
	struct tran_device *dev = NULL;
	union com_propval prop = {.intval = 0};

	dev = tran_get_by_name("tc_charger");
	if (IS_ERR_OR_NULL(dev)) {
		PE50_ERR("get tc_charger dev fail\n");
		return;
	}
	tran_dev_set_prop(dev, TRAN_PROP_CHARGING_ANIMATION, &prop);
}

static void pe50_report_ffc_status(struct pe50_algo_info *info, bool status)
{
	struct tran_device *dev = NULL;
	union com_propval prop = {.intval = 0};

	dev = tran_get_by_name("tc_charger");
	if (IS_ERR_OR_NULL(dev)) {
		pr_err("get tc_charger dev fail\n");
		return;
	}

	prop.intval = status ? 1 : 0;
	tran_dev_set_prop(dev, TRAN_PROP_IS_FFC_CHR, &prop);

	prop.intval = status ? PE5_ID : ALG_NONE;
	tran_dev_set_prop(dev, TRAN_PROP_FFC_ALG_ID, &prop);
}

int pe50_get_adc(struct pe50_algo_info *info, enum pe50_adc_channel chan,
			int *val)
{
	struct pe50_algo_data *data = info->data;
	int ret, i, ibus;
	struct pe50_stop_info sinfo = {
		.reset_ta = true,
		.hardreset_ta = false,
	};

	if (atomic_read(&data->stop_algo)) {
		PE50_INFO("stop algo\n");
		goto stop;
	}
	*val = 0;
	if (chan == PE50_ADCCHAN_IBUS) {
		for (i = PE50_DVCHG_MASTER; i < PE50_DVCHG_MAX; i++) {
			if (!data->is_dvchg_en[i])
				continue;
			ret = pe50_hal_get_adc(info->alg, to_chgidx(i),
					       PE50_ADCCHAN_IBUS, &ibus);
			if (ret < 0) {
				PE50_ERR("get dvchg ibus fail(%d)\n", ret);
				return ret;
			}
			*val += ibus;
		}
		if (data->is_swchg_en) {
			ret = pe50_hal_get_adc(info->alg, CHG1,
					       PE50_ADCCHAN_IBUS, &ibus);
			if (ret < 0) {
				PE50_ERR("get swchg ibus fail(%d)\n", ret);
				return ret;
			}
			*val += ibus;
		}
		return 0;
	}

	if (chan == PE50_ADCCHAN_IBAT) {
		*val = tc_get_battery_current();
		return 0;
	}

#if IS_ENABLED(CONFIG_TC_BATTERY)
	if (chan == PE50_ADCCHAN_VBAT) {
		*val = tc_get_battery_voltage();
		return 0;
	}
#endif

	return pe50_hal_get_adc(info->alg, DVCHG1, chan, val);
stop:
	pe50_stop(info, &sinfo);
	return -EIO;
}

/* Calculate power limited ita according to TA's power limitation */
u32 pe50_get_ita_pwr_lmt_by_vta(struct pe50_algo_info *info, u32 vta)
{
	struct pe50_algo_data *data = info->data;
	struct pe50_ta_auth_data *auth_data = &data->ta_auth_data[data->running_spec];
	u32 ita_pwr_lmt;

	if (!auth_data->pwr_lmt)
		return data->ita_lmt;

	ita_pwr_lmt = precise_div(percent(auth_data->pdp * 1000000, data->pwr_ratio), vta);

	PE50_INFO("full_power_flag:%d, ita_pwr_lmt:%d, pwr_ratio:%d\n",
		data->full_power_flag, ita_pwr_lmt, data->pwr_ratio);

	return min(ita_pwr_lmt, data->ita_lmt);
}

int pe50_set_ta_cap_cv(struct pe50_algo_info *info, u32 vta,
				     u32 ita)
{
	int ret, ita_meas_pre, ita_meas_post, vta_meas;
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	struct pe50_ta_auth_data *auth_data = &data->ta_auth_data[data->running_spec];
	u32 vstep_cnt, ita_gap, vta_gap;
	struct pe50_stop_info sinfo = {
		.reset_ta = true,
		.hardreset_ta = false,
	};

	if (data->vta_setting == vta && data->ita_setting == ita)
		return 0;
	while (true) {
		if (pe50_is_hwerr_notified(info)) {
			PE50_ERR("H/W error notified\n");
			goto stop;
		}

		if (atomic_read(&data->stop_algo)) {
			PE50_INFO("stop algo\n");
			goto stop;
		}

		if (vta > auth_data->vcap_max) {
			PE50_ERR("vta(%d) over capability(%d)\n", vta,
				 auth_data->vcap_max);
			goto stop;
		}

		if (vta < auth_data->vcap_min) {
			PE50_ERR("vta(%d) under capability(%d)\n", vta,
				 auth_data->vcap_min);
			goto stop;
		}

		if (ita < auth_data->ita_min) {
			PE50_INFO("ita(%d) under ita_min(%d)\n", ita,
				  auth_data->ita_min);
			ita = auth_data->ita_min;
		}
		vta_gap = abs(data->vta_setting - vta);

		/* Get ta cap before setting */
		ret = pe50_get_ta_cap_by_supportive(info, &vta_meas,
						    &ita_meas_pre);
		if (ret < 0) {
			PE50_ERR("get ta cap by supportive fail(%d)\n", ret);
			return ret;
		}

		/* Not to increase vta if it exceeds pwr_lmt */
		data->ita_pwr_lmt = pe50_get_ita_pwr_lmt_by_vta(info, vta);
		if (vta > data->vta_setting &&
		    (data->ita_pwr_lmt <
		     ita_meas_pre + data->ita_gap_per_vstep)) {
			PE50_INFO("ita_meas(%d) + ita_gap(%d) > pwr_lmt(%d)\n",
				  ita_meas_pre, data->ita_gap_per_vstep,
				  data->ita_pwr_lmt);
			return 0;
		}

		/* Set ta cap */
		ret = pe50_hal_set_ta_cap(info->alg, vta, ita);
		if (ret < 0) {
			PE50_ERR("set ta cap fail(%d)\n", ret);
			return ret;
		}
		if (vta_gap > auth_data->vta_step ||
		    data->state != PE50_ALGO_SS_DVCHG)
			msleep(desc->ta_blanking);

		/* Get ta cap after setting */
		ret = pe50_get_ta_cap_by_supportive(info, &vta_meas,
						    &ita_meas_post);
		if (ret < 0) {
			PE50_ERR("get ta cap by supportive fail(%d)\n", ret);
			return ret;
		}

		if (data->is_dvchg_en[PE50_DVCHG_MASTER] &&
		    (ita_meas_post > ita_meas_pre) &&
		    (vta > data->vta_setting)) {
			vstep_cnt = precise_div(max(vta, (u32)vta_meas) -
						data->vta_setting,
						auth_data->vta_step);
			ita_gap = precise_div(ita_meas_post - ita_meas_pre,
					      vstep_cnt);
			pe50_update_ita_gap(info, ita_gap);
			PE50_INFO("ita gap(now,updated)=(%d,%d)\n",
				  ita_gap, data->ita_gap_per_vstep);
		}

		if ((data->is_dvchg_en[PE50_DVCHG_MASTER]) &&
			(vta > data->vta_setting)) {
			PE50_INFO("up v\n");
			if ((ita_meas_post <= ita_meas_pre) ||
				(ita_meas_post - ita_meas_pre) < desc->min_ita_gap) {
				PE50_INFO("i stable or descend\n");
				data->vta_up_ita_stable_cnt++;
			} else {
				PE50_INFO("reset cnt param\n");
				data->vta_up_ita_stable_cnt = 0;
			}

			if (data->vta_up_ita_stable_cnt >= data->ita_stable_cnt) {
				data->ita_meas_lmt = percent(ita_meas_post, 95);
				data->vta_up_ita_stable_cnt = 0;
				PE50_INFO("update data->ita_meas_lmt:%dmA\n", data->ita_meas_lmt);
			}
		}

		data->ita_gap_per_vstep = 0;

		data->vta_setting = vta;
		data->ita_setting = ita;

		if (ita_meas_post <= pe50_get_ita_tracking_max(ita))
			break;

		vta -= auth_data->vta_step;
		PE50_INFO("ita_meas %dmA over setting %dmA, keep tracking...\n",
			  ita_meas_post, ita);
	}

	data->vta_measure = vta_meas;
	data->ita_measure = ita_meas_post;
	PE50_INFO("vta(set,meas):(%d,%d),ita(set,meas):(%d,%d)\n",
		 data->vta_setting, data->vta_measure, data->ita_setting,
		 data->ita_measure);
	return 0;
stop:
	pe50_stop(info, &sinfo);
	return -EIO;
}

static int pe50_check_cable_capacity_by_hw_mark(struct pe50_algo_info *info)
{
	int capability = 0;

	if (tc_tcpc_detect_dual_rp_cable()) {
		PE50_INFO("detected dual rp cable\n");
		capability = PE50_DUAL_RP_CAPACITY;
		goto out;
	}
	if (tc_tcpc_detect_rp_ra_cable()) {
		PE50_INFO("detected rp ra cable\n");
		capability = PE50_RP_RA_CAPACITY;
		goto out;
	}
out:
	return capability;
}
static int pe50_check_cable_capacity(struct pe50_algo_info *info)
{
	int capability = 0;
	int hw_mark_capability;
	struct pe50_algo_data *data = info->data;

	data->ignore_measure_r = false;

	hw_mark_capability = pe50_check_cable_capacity_by_hw_mark(info);
	if (hw_mark_capability > 0) {
		PE50_INFO("detected reliable hw mark capability %d\n", hw_mark_capability);
		data->ignore_measure_r = true;
		capability = hw_mark_capability;
		goto out;
	}
out:
	return capability;
}

static inline void pe50_calculate_vbat_ircmp(struct pe50_algo_info *info)
{
	int ret, ibat;
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	u32 ircmp;

	if (!data->is_dvchg_en[PE50_DVCHG_MASTER]) {
		data->vbat_ircmp = 0;
		return;
	}

	ret = pe50_get_adc(info, PE50_ADCCHAN_IBAT, &ibat);
	if (ret < 0) {
		PE50_ERR("get ibat fail(%d)\n", ret);
		return;
	}
	ircmp = max(div1000(ibat * data->r_bat), desc->ircmp_vclamp);
	/*
	 * For safety,
	 * if state is CC_CV, ircmp can only be smaller than previous one
	 */
	if (data->state == PE50_ALGO_CC_CV)
		ircmp = min(data->vbat_ircmp, ircmp);
	data->vbat_ircmp = min(desc->ircmp_vclamp, ircmp);
	PE50_INFO("vbat_ircmp(vclamp,ibat,rbat)=%d(%d,%d,%d)\n",
		 data->vbat_ircmp, desc->ircmp_vclamp, ibat, data->r_bat);
}

static void pe50_update_dual_battery_info(struct pe50_algo_info *info)
{
	int ret = 0;
	union com_propval temp_val = {.intval = 0};
	struct pe50_algo_data *data = info->data;

	ret = pe50_check_tran_dev_ptr(&data->tc_gauge, "tc_gauge");
	if (ret < 0) {
		pr_info("%s Couldn't get gauge_dev\n", __func__);
		return;
	}

	ret = tran_dev_get_prop(data->tc_gauge, TRAN_PROP_MASTER_BATT_VOLT, &temp_val);
	if (!ret)
		data->fg_a_vbat = temp_val.intval / 1000;

	ret = tran_dev_get_prop(data->tc_gauge, TRAN_PROP_MASTER_BATT_NOW_CURR, &temp_val);
	if (!ret)
		data->fg_a_ibat = temp_val.intval / 1000;
	
	ret = tran_dev_get_prop(data->tc_gauge, TRAN_PROP_MASTER_BATT_TEMP, &temp_val);
	if (!ret)
		data->fg_a_temp = temp_val.intval / 10;
	
	ret = tran_dev_get_prop(data->tc_gauge, TRAN_PROP_MASTER_BATT_EN, &temp_val);
	if (!ret)
		data->fg_a_status = temp_val.intval;

	ret = tran_dev_get_prop(data->tc_gauge, TRAN_PROP_SLAVE_BATT_VOLT, &temp_val);
	if (!ret)
		data->fg_b_vbat = temp_val.intval / 1000;

	ret = tran_dev_get_prop(data->tc_gauge, TRAN_PROP_SLAVE_BATT_NOW_CURR, &temp_val);
	if (!ret)
		data->fg_b_ibat = temp_val.intval / 1000;
	
	ret = tran_dev_get_prop(data->tc_gauge, TRAN_PROP_SLAVE_BATT_TEMP, &temp_val);
	if (!ret)
		data->fg_b_temp = temp_val.intval / 10;

	ret = tran_dev_get_prop(data->tc_gauge, TRAN_PROP_SLAVE_BATT_EN, &temp_val);
	if (!ret)
		data->fg_b_status = temp_val.intval;	
}

static int pe50_update_battery_info(struct pe50_algo_info *info)
{
	struct pe50_algo_data *data = info->data;
	int ret = 0;

	/* ibus */
	ret = pe50_get_adc(info, PE50_ADCCHAN_IBUS, &data->ibus);
	if (ret < 0) {
		PE50_ERR("get ibus fail, ret:%d\n", ret);
		goto out;
	}

	/* tbat */
	ret = pe50_get_adc(info, PE50_ADCCHAN_TBAT, &data->tbat);
	if (ret < 0) {
		PE50_ERR("get tbat fail, ret:%d\n", ret);
		goto out;
	}

	/* vbat */
	ret = pe50_get_adc(info, PE50_ADCCHAN_VBAT, &data->vbat);
	if (ret < 0) {
		PE50_ERR("get vbat fail, ret:%d\n", ret);
		goto out;
	}

out:
	return ret;
}

static int pe50_update_dual_battery_step_charger_fcc_limit(struct pe50_algo_info *info)
{
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	int level;
	int i;
	int ret = 0;
	u32 idvchg_term_master, idvchg_term_slave, idvchg_term;
	bool step_ffc_level_change = false;

	/* get ADC data */
	pe50_update_dual_battery_info(info);

	/* batt step ffc param Calculate */
	if (data->fg_a_temp > desc->step_master_ffc[desc->step_master_ffc_level -1].ffc_temp[PE50_TEMP_HIGH] || 
		data->fg_a_temp <= desc->step_master_ffc[0].ffc_temp[PE50_TEMP_LOW]) {

		memcpy(data->step_master_ffc_vbat, desc->step_master_ffc[PE50_TEMP_LOW].ffc_vbat, sizeof(data->step_master_ffc_vbat));
		memcpy(data->step_master_ffc_ibat, desc->step_master_ffc[PE50_TEMP_LOW].ffc_ibat, sizeof(data->step_master_ffc_ibat));
		idvchg_term_master = desc->step_master_ffc[PE50_TEMP_LOW].ffc_term / data->conversion_ratio;
		data->bat_master_ffc_eoc = desc->step_master_ffc[PE50_TEMP_LOW].ffc_term;
		data->bat_master_ffc_cv = desc->step_master_ffc[PE50_TEMP_LOW].ffc_vbat[PE50_VBAT_FFC_HIGH];
	}

	PE50_INFO("%s: fg_a_temp:%d, fg_b_temp:%d, master_temp:%d. slave_temp:%d\n",
		__func__, data->fg_a_temp, data->fg_b_temp, desc->step_master_ffc[0].ffc_temp[PE50_TEMP_LOW], desc->step_master_ffc[0].ffc_temp[PE50_TEMP_HIGH]);
	/* debounce */
	for (level = 0; level < desc->step_master_ffc_level; level++) {

		if (data->fg_a_temp > desc->step_master_ffc[level].ffc_temp[PE50_TEMP_LOW] &&
		   data->fg_a_temp <= desc->step_master_ffc[level].ffc_temp[PE50_TEMP_HIGH]) {

			if (data->step_master_ffc_level == level)
				break;
			memcpy(data->step_master_ffc_vbat, desc->step_master_ffc[level].ffc_vbat, sizeof(data->step_master_ffc_vbat));
			memcpy(data->step_master_ffc_ibat, desc->step_master_ffc[level].ffc_ibat, sizeof(data->step_master_ffc_ibat));
			idvchg_term_master = desc->step_master_ffc[level].ffc_term / data->conversion_ratio;
			data->bat_master_ffc_eoc = desc->step_master_ffc[level].ffc_term;
			data->bat_master_ffc_cv = desc->step_master_ffc[level].ffc_vbat[PE50_VBAT_FFC_HIGH];
			data->step_master_ffc_level = level;
			step_ffc_level_change = true;
			PE50_INFO("master bat_master_ffc_cv:%d, bat_master_ffc_eoc:%d, idvchg_term_master:%d, temp_level:%d\n",
			       	data->bat_master_ffc_cv, data->bat_master_ffc_eoc, idvchg_term_master, data->step_master_ffc_level);
			break;
		}
	}

	for (i = PE50_VBAT_FFC_LOW; i < PE50_VBAT_FFC_HIGH; i++) {

		PE50_INFO("data->fg_a_vbat = %d, data->step_master_ffc_vbat[i] = %d, desc->pe5_ffc_gap[i] = %d, data->step_master_ffc_vbat[i + 1] = %d\n",
			data->fg_a_vbat, data->step_master_ffc_vbat[i], desc->pe5_ffc_gap[i], data->step_master_ffc_vbat[i + 1]);

		if (data->fg_a_vbat > (data->step_master_ffc_vbat[i] - desc->pe5_ffc_gap[i]) &&
			data->fg_a_vbat <= (data->step_master_ffc_vbat[i + 1] - desc->pe5_ffc_gap[i])) {
	
			data->step_master_chg_index = i + 1;
			break;
		}
	}

	if ((data->step_master_ffc_ibat[data->step_master_chg_index] >= data->fg_a_ibat &&
		data->vbat_master_step_cv != data->step_master_ffc_vbat[data->step_master_chg_index]) ||
		step_ffc_level_change == true) {
		
		step_ffc_level_change = false;

		data->vbat_master_step_cv = data->step_master_ffc_vbat[data->step_master_chg_index];
		data->vbat_master_step_cc = data->step_master_ffc_ibat[data->step_master_chg_index];

		PE50_ERR("%s:master vbat_master_step_cv switch to %d", __func__, data->vbat_master_step_cv);
	}

	/* slave batt step ffc param Calculate */
	if (data->fg_b_temp > desc->step_slave_ffc[desc->step_slave_ffc_level -1].ffc_temp[PE50_TEMP_HIGH] || 
		data->fg_b_temp <= desc->step_slave_ffc[0].ffc_temp[PE50_TEMP_LOW]) {

		memcpy(data->step_slave_ffc_vbat, desc->step_slave_ffc[PE50_TEMP_LOW].ffc_vbat, sizeof(data->step_slave_ffc_vbat));
		memcpy(data->step_slave_ffc_ibat, desc->step_slave_ffc[PE50_TEMP_LOW].ffc_ibat, sizeof(data->step_slave_ffc_ibat));
		idvchg_term_slave = desc->step_slave_ffc[PE50_TEMP_LOW].ffc_term / data->conversion_ratio;
		data->bat_slave_ffc_eoc = desc->step_slave_ffc[PE50_TEMP_LOW].ffc_term;
		data->bat_slave_ffc_cv = desc->step_slave_ffc[PE50_TEMP_LOW].ffc_vbat[PE50_VBAT_FFC_HIGH];
	}

	/* debounce */
	for (level = 0; level < desc->step_slave_ffc_level; level++) {

		if (data->fg_b_temp > desc->step_slave_ffc[level].ffc_temp[PE50_TEMP_LOW] &&
		   data->fg_b_temp <= desc->step_slave_ffc[level].ffc_temp[PE50_TEMP_HIGH]) {

			if (data->step_slave_ffc_level == level)
				break;
			memcpy(data->step_slave_ffc_vbat, desc->step_slave_ffc[level].ffc_vbat, sizeof(data->step_slave_ffc_vbat));
			memcpy(data->step_slave_ffc_ibat, desc->step_slave_ffc[level].ffc_ibat, sizeof(data->step_slave_ffc_ibat));
			idvchg_term_slave = desc->step_slave_ffc[level].ffc_term / data->conversion_ratio;
			data->bat_slave_ffc_eoc = desc->step_slave_ffc[level].ffc_term;
			data->bat_slave_ffc_cv = desc->step_slave_ffc[level].ffc_vbat[PE50_VBAT_FFC_HIGH];
			data->step_slave_ffc_level = level;
			step_ffc_level_change = true;
			PE50_INFO("slave bat_slave_ffc_cv:%d, bat_slave_ffc_eoc:%d, idvchg_term_slave:%d, temp_level:%d\n",
			       	data->bat_slave_ffc_cv, data->bat_slave_ffc_eoc, idvchg_term_slave, data->step_slave_ffc_level);
			break;
		}
	}

	for (i = PE50_VBAT_FFC_LOW; i < PE50_VBAT_FFC_HIGH; i++) {

		PE50_INFO("data->fg_b_vbat = %d, data->step_slave_ffc_vbat[i] = %d, desc->pe5_ffc_gap[i] = %d, data->step_slave_ffc_vbat[i + 1] = %d\n",
			data->fg_b_vbat, data->step_slave_ffc_vbat[i], desc->pe5_ffc_gap[i], data->step_slave_ffc_vbat[i + 1]);

		if (data->fg_b_vbat > (data->step_slave_ffc_vbat[i] - desc->pe5_ffc_gap[i]) &&
			data->fg_b_vbat <= (data->step_slave_ffc_vbat[i + 1] - desc->pe5_ffc_gap[i])) {
	
			data->step_slave_chg_index = i + 1;
			break;
		}
	}

	if ((data->step_slave_ffc_ibat[data->step_slave_chg_index] >= data->fg_b_ibat &&
		data->vbat_slave_step_cv != data->step_slave_ffc_vbat[data->step_slave_chg_index]) ||
		step_ffc_level_change == true) {
		
		step_ffc_level_change = false;

		data->vbat_slave_step_cv = data->step_slave_ffc_vbat[data->step_slave_chg_index];
		data->vbat_slave_step_cc = data->step_slave_ffc_ibat[data->step_slave_chg_index];

		PE50_ERR("%s:slave vbat_slave_step_cv switch to %d", __func__, data->vbat_slave_step_cv);
	}

	idvchg_term_master = desc->step_master_ffc[data->step_master_ffc_level].ffc_term / data->conversion_ratio;
	idvchg_term_slave = desc->step_slave_ffc[data->step_slave_ffc_level].ffc_term / data->conversion_ratio;

	if (data->fg_a_status && data->fg_b_status) {
		data->vbat_step_cv = max(data->vbat_master_step_cv, data->vbat_slave_step_cv);
		data->vbat_step_cc = data->vbat_master_step_cc + data->vbat_slave_step_cc;
		idvchg_term = idvchg_term_slave;
	} else {
		data->vbat_step_cv = data->fg_a_status ? data->vbat_master_step_cv : data->vbat_slave_step_cv;
		data->vbat_step_cc = data->fg_a_status ? data->vbat_master_step_cc : data->vbat_slave_step_cc;
		idvchg_term = data->fg_a_status ? idvchg_term_master : idvchg_term_slave;
	}

	switch (data->algo_num) {
		case PE50_PPS:
			data->idvchg_term = max_t(u32, idvchg_term, desc->idvchg_term);
			break;
		case PE50_UFCS:
			break;
		default:
			data->idvchg_term = 1000;
			PE50_ERR("algo number failed \n");
			break;
	}
	PE50_INFO("[step_ffc] %s: vbat_master_step_cv:%d, vbat_master_step_cc:%d, step_chg_index:%d, step_master_ffc_level:%d\n",
		__func__, data->vbat_master_step_cv ,data->vbat_master_step_cc, data->step_master_chg_index, data->step_master_ffc_level);

	PE50_INFO("[step_ffc] %s: vbat_slave_step_cv:%d, vbat_slave_step_cc:%d, step_chg_index:%d, step_slave_ffc_level:%d\n",
		__func__, data->vbat_slave_step_cv ,data->vbat_slave_step_cc, data->step_slave_chg_index, data->step_slave_ffc_level);

	PE50_INFO("[PE5_dual_gauge_result] %s: vbat_step_cv:%d, vbat_step_cc:%d\n",
		__func__, data->vbat_step_cv ,data->vbat_step_cc);

	return ret;
}

static int pe50_update_step_charger_fcc_limit(struct pe50_algo_info *info)
{
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	int level;
	int i;
	int ret = 0;
	u32 idvchg_term;
	bool step_ffc_level_change = false;

	/* get ADC data */
	ret = pe50_update_battery_info(info);
	if (ret < 0) {
		PE50_ERR("update batt info failed(%d)\n", ret);
		goto out;
	}

	/* batt step ffc param Calculate */
	if (data->tbat > desc->step_ffc[desc->step_ffc_level -1].ffc_temp[PE50_TEMP_HIGH] || 
		data->tbat <= desc->step_ffc[0].ffc_temp[PE50_TEMP_LOW]) {

		memcpy(data->step_ffc_vbat, desc->step_ffc[PE50_TEMP_LOW].ffc_vbat, sizeof(data->step_ffc_vbat));
		memcpy(data->step_ffc_ibat, desc->step_ffc[PE50_TEMP_LOW].ffc_ibat, sizeof(data->step_ffc_ibat));
		idvchg_term = desc->step_ffc[PE50_TEMP_LOW].ffc_term / data->conversion_ratio;
		data->bat_ffc_eoc = desc->step_ffc[PE50_TEMP_LOW].ffc_term;
		data->bat_ffc_cv = desc->step_ffc[PE50_TEMP_LOW].ffc_vbat[PE50_VBAT_FFC_HIGH];
	}

	/* debounce */
	for (level = 0; level < desc->step_ffc_level; level++) {

		if (data->tbat > desc->step_ffc[level].ffc_temp[PE50_TEMP_LOW] &&
		   data->tbat <= desc->step_ffc[level].ffc_temp[PE50_TEMP_HIGH]) {
			if (!((desc->step_ffc[level].cycle_cnt[PE50_CYCLE_CNT_LOW] < 0) ||
			(data->battery_cycle >= desc->step_ffc[level].cycle_cnt[PE50_CYCLE_CNT_LOW] &&
			(data->battery_cycle < desc->step_ffc[level].cycle_cnt[PE50_CYCLE_CNT_HIGH] ||
			 desc->step_ffc[level].cycle_cnt[PE50_CYCLE_CNT_HIGH] < 0))))
				continue;

			if (data->step_ffc_level == level)
				break;
			memcpy(data->step_ffc_vbat, desc->step_ffc[level].ffc_vbat, sizeof(data->step_ffc_vbat));
			memcpy(data->step_ffc_ibat, desc->step_ffc[level].ffc_ibat, sizeof(data->step_ffc_ibat));
			idvchg_term = desc->step_ffc[level].ffc_term / data->conversion_ratio;
			data->bat_ffc_eoc = desc->step_ffc[level].ffc_term;
			data->bat_ffc_cv = desc->step_ffc[level].ffc_vbat[PE50_VBAT_FFC_HIGH];
			data->long_life_rechg_cv_gap = desc->step_ffc[level].rechg_gap;
			data->long_life_rechg_cur = desc->step_ffc[level].rechg_cur;
			data->step_ffc_level = level;
			step_ffc_level_change = true;
			PE50_INFO("bat_ffc_cv:%d, bat_ffc_eoc:%d, idvchg_term:%d, temp_level:%d\n",
			       	data->bat_ffc_cv, data->bat_ffc_eoc, idvchg_term, data->step_ffc_level);
			break;
		}
	}

	for (i = PE50_VBAT_FFC_LOW; i < PE50_VBAT_FFC_HIGH; i++) {

		PE50_INFO("data->vbat = %d, data-step_ffc_vbat[i] = %d, desc->pe5_ffc_gap[i] = %d, data->step_ffc_vbat[i + 1] = %d\n",
			data->vbat, data->step_ffc_vbat[i], desc->pe5_ffc_gap[i], data->step_ffc_vbat[i + 1]);
		if (data->vbat > (data->step_ffc_vbat[i] - desc->pe5_ffc_gap[i]) &&
			data->vbat <= (data->step_ffc_vbat[i + 1] - desc->pe5_ffc_gap[i])) {
	
			data->step_chg_index = i + 1;
			break;
		}
	}

	idvchg_term = desc->step_ffc[data->step_ffc_level].ffc_term / data->conversion_ratio;

	if ((data->step_ffc_ibat[data->step_chg_index] >= data->ibus * data->conversion_ratio &&
		data->vbat_step_cv != data->step_ffc_vbat[data->step_chg_index]) ||
		step_ffc_level_change == true) {
		
		step_ffc_level_change = false;

		data->vbat_step_cv = data->step_ffc_vbat[data->step_chg_index];
		data->vbat_step_cc = data->step_ffc_ibat[data->step_chg_index];
		switch (data->algo_num) {
			case PE50_PPS:
				data->idvchg_term = max_t(u32, idvchg_term, desc->idvchg_term);
				break;
			case PE50_UFCS:
				break;
			default:
				data->idvchg_term = 1000;
				PE50_ERR("algo number failed \n");
				break;
		}

		PE50_ERR("%s: vbat_step_cv switch to %d", __func__, data->vbat_step_cv);
	}

	PE50_INFO("[step_ffc] %s: vbat_step_cv:%d, vbat_step_cc:%d, idvchg_term = %d, step_chg_index:%d\n",
		__func__, data->vbat_step_cv ,data->vbat_step_cc, data->idvchg_term, data->step_chg_index);

out:
	return ret;
}

int pe50_select_vbat_cv(struct pe50_algo_info *info)
{
	int ret = 0;
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	u32 cv = data->vbat_cv;
	u32 lower_temp;

	mutex_lock(&data->ext_lock);

	if (desc->support_dual_battery)
		ret = pe50_update_dual_battery_step_charger_fcc_limit(info);
	else
		ret = pe50_update_step_charger_fcc_limit(info);
	if (ret < 0) {
		PE50_ERR("update step ffc failed(%d)\n", ret);
		goto out;
	}

	cv = data->vbat_step_cv + data->vbat_ircmp;

	if (cv == data->vbat_cv)
		goto out;

	/* VBATOVP ALARM */
	ret = pe50_hal_set_vbatovp_alarm(info->alg, DVCHG1, cv);
	if (ret < 0) {
		PE50_ERR("set vbatovp alarm fail(%d)\n", ret);
		ret = 0;
		goto out;
	}
	data->vbat_cv = cv;

	if (desc->support_dual_battery) {
		if (data->fg_a_status && data->fg_b_status)
			lower_temp = max(data->bat_master_ffc_cv, data->bat_slave_ffc_cv);
		else
			lower_temp = data->fg_a_status ? data->bat_master_ffc_cv : data->bat_slave_ffc_cv;
		data->cv_lower_bound = lower_temp + data->vbat_ircmp - PE50_CV_LOWER_BOUND_GAP;
	} else {
		data->cv_lower_bound = data->step_ffc_vbat[PE50_VBAT_FFC_HIGH] + data->vbat_ircmp - PE50_CV_LOWER_BOUND_GAP;
	}

out:
	PE50_INFO("vbat_cv(vbat_step_cv, vbat_ircmp, cv_lower_bound)=%d(%d, %d, %d)\n",
		  data->vbat_cv, data->vbat_step_cv, data->vbat_ircmp, data->cv_lower_bound);

	mutex_unlock(&data->ext_lock);
	return ret;
}

/* Calculate VBUSOV S/W level */
static u32 pe50_get_dvchg_vbusovp(struct pe50_algo_info *info, u32 ita)
{
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	u32 vout, ibat;

	ibat = pe50_cal_ibat(info, ita);
	vout = desc->vbat_cv + div1000(ibat * data->r_sw);
	return min(percent(pe50_vout2vbus(info, vout), PE50_VBUSOVP_RATIO),
		   data->vbusovp);
}

/* Calculate VBATOV S/W level */
static u32 pe50_get_vbatovp(struct pe50_algo_info *info)
{
	struct pe50_algo_desc *desc = info->desc;

	return percent(desc->vbat_cv + desc->ircmp_vclamp, PE50_VBATOVP_RATIO);
}

int pe50_set_dvchg_protection(struct pe50_algo_info *info, bool dual)
{
	int ret;
	u32 idvchg_lmt;
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	struct pe50_ta_auth_data *auth_data = &data->ta_auth_data[data->running_spec];
	u32 vbusovp, ibusocp, vbatovp, ibatocp;

	/* VBATOVP ALARM */
	ret = pe50_hal_set_vbatovp_alarm(info->alg, DVCHG1, desc->vbat_cv);
	if (ret < 0) {
		PE50_ERR("set vbatovp alarm fail(%d)\n", ret);
		return ret;
	}

	/* VBUSOVP */
	vbusovp = desc->vbus_max_ovp[data->running_spec];

	ret = pe50_hal_set_vbusovp(info->alg, DVCHG1, vbusovp);
	if (ret < 0) {
		PE50_ERR("set vbusovp fail(%d)\n", ret);
		return ret;
	}
	data->vbusovp = vbusovp;
	/* For TA CV mode, vbusovp alarm is not required */
	ret = pe50_hal_set_vbusovp_alarm(info->alg, DVCHG1, vbusovp);
	if (ret < 0) {
		PE50_ERR("set vbusovp alarm fail(%d)\n", ret);
		return ret;
	}

	/* IBUSOCP */
	idvchg_lmt = min(data->idvchg_cc, (u32)auth_data->ita_max);

	ibusocp = percent(idvchg_lmt, PE50_IBUSOCP_RATIO);

	if (data->is_dvchg_exist[PE50_DVCHG_SLAVE] && dual) {
		ret = pe50_hal_set_ibusocp(info->alg, DVCHG2, ibusocp);
		if (ret < 0) {
			PE50_ERR("set slave ibusocp fail(%d)\n", ret);
			return ret;
		}
	}

	ret = pe50_hal_set_ibusocp(info->alg, DVCHG1, ibusocp);
	if (ret < 0) {
		PE50_ERR("set ibusocp fail(%d)\n", ret);
		return ret;
	}

	/* VBATOVP */
	vbatovp = percent(desc->vbat_cv + desc->ircmp_vclamp,
			  PE50_VBATOVP_RATIO);
	ret = pe50_hal_set_vbatovp(info->alg, DVCHG1, vbatovp);
	if (ret < 0) {
		PE50_ERR("set vbatovp fail(%d)\n", ret);
		return ret;
	}

	/* IBATOCP */
	ibatocp = percent(data->conversion_ratio * data->idvchg_cc,
			  PE50_IBATOCP_RATIO);
	ret = pe50_hal_set_ibatocp(info->alg, DVCHG1, ibatocp);
	if (ret < 0) {
		PE50_ERR("set ibatocp fail(%d)\n", ret);
		return ret;
	}
	PE50_INFO("vbusovp,ibusocp,vbatovp,ibatocp = (%d,%d,%d,%d)\n",
		 vbusovp, ibusocp, vbatovp, ibatocp);
	return 0;
}

int pe50_enable_dvchg_charging(struct pe50_algo_info *info,
				enum pe50_dvchg_role role, bool en)
{
	int ret;
	struct pe50_algo_data *data = info->data;

	if (!data->is_dvchg_exist[role])
		return -ENODEV;
	if (data->is_dvchg_en[role] == en)
		return 0;
	PE50_INFO("en[%s] = %d\n", pe50_dvchg_role_name[role], en);
	ret = pe50_hal_enable_charging(info->alg, to_chgidx(role), en);
	if (ret < 0) {
		PE50_ERR("en chg fail(%d)\n", ret);
		return ret;
	}
	data->is_dvchg_en[role] = en;

	return 0;
}

/*
 * Set protection parameters, disable swchg and  enable divider charger
 *
 * @en: enable/disable
 */
int pe50_set_dvchg_charging(struct pe50_algo_info *info, bool en)
{
	int ret;
	struct pe50_algo_data *data = info->data;

	if (!data->is_dvchg_exist[PE50_DVCHG_MASTER])
		return -ENODEV;

	PE50_INFO("en = %d\n", en);

	if (en) {
		ret = pe50_hal_enable_hz(info->alg, true);
		if (ret < 0) {
			PE50_ERR("set swchg hz fail(%d)\n", ret);
			return ret;
		}

		ret = pe50_set_dvchg_protection(info, false);
		if (ret < 0) {
			PE50_ERR("set protection fail(%d)\n", ret);
			return ret;
		}
	}
	ret = pe50_enable_dvchg_charging(info, PE50_DVCHG_MASTER, en);
	if (ret < 0)
		return ret;

	if (!en) {
		ret = pe50_hal_enable_hz(info->alg, false);
		if (ret < 0) {
			PE50_ERR("disable swchg hz fail(%d)\n", ret);
			return ret;
		}
	}

	return 0;
}

/*
 * Enable charging of switching charger
 * For divide by two algorithm, according to swchg_ichg to decide enable or not
 *
 * @en: enable/disable
 */
int pe50_enable_swchg_charging(struct pe50_algo_info *info, bool en)
{
	int ret;
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;

	PE50_INFO("en = %d\n", en);
	if (en) {
		ret = pe50_hal_enable_charging(info->alg, CHG1, true);
		if (ret < 0) {
			PE50_ERR("en swchg fail(%d)\n", ret);
			return ret;
		}
		ret = pe50_hal_enable_hz(info->alg, false);
		if (ret < 0) {
			PE50_ERR("disable hz fail(%d)\n", ret);
			return ret;
		}
	} else {
		ret = pe50_hal_enable_hz(info->alg, true);
		if (ret < 0) {
			PE50_ERR("disable hz fail(%d)\n", ret);
			return ret;
		}
		ret = pe50_hal_enable_charging(info->alg, CHG1, false);
		if (ret < 0) {
			PE50_ERR("en swchg fail(%d)\n", ret);
			return ret;
		}
	}
	data->is_swchg_en = en;
	ret = pe50_hal_set_vbatovp_alarm(info->alg, DVCHG1, desc->vbat_cv);
	if (ret < 0) {
		PE50_ERR("set vbatovp alarm fail(%d)\n", ret);
		return ret;
	}
	return 0;
}

int pe50_algo_multi_dvchg_update(struct pe50_algo_info *info)
{
	int ret = 0, i;
	int cp_num = 0; 
	struct pe50_algo_data *data = info->data;

	/* Calculate the number of CP's that need to be opened */
	for (i = PE50_DVCHG_MASTER; i < PE50_DVCHG_MAX; i++) {
		if (data->ita_measure >= (data->run_cp_cur[i] - 
			    (data->is_dvchg_en[i] ? data->run_cp_recovery_gap[i] : 0))) {
			cp_num++;
		} else {
			break;
		}
	}

	/* Open CP based on the number of CP's that need to be opened */
	for (i = PE50_DVCHG_MASTER; i < PE50_DVCHG_MAX; i++) {
		if (!data->is_dvchg_exist[i])
			continue;
		ret = pe50_enable_dvchg_charging(info, i, (i < cp_num) ? true : false);
		if (ret < 0) {
			PE50_ERR("(%s) enable dvchg fail(%d)\n",
				pe50_dvchg_role_name[i], ret);
		}
	}

	return ret;
}

/*
 * Enable TA by algo
 *
 * @en: enable/disable
 * @mV: requested output voltage
 * @mA: requested output current
 */
int pe50_enable_ta_charging(struct pe50_algo_info *info, bool en, int mV,
				   int mA)
{
	int ret;
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	u32 wdt = max(desc->polling_interval * 2, (u32)PE50_TA_WDT_MIN);

	PE50_INFO("en = %d\n", en);
	if (en) {
		ret = pe50_hal_set_ta_wdt(info->alg, wdt);
		if (ret < 0) {
			PE50_ERR("set ta wdt fail(%d)\n", ret);
			return ret;
		}

		ret = pe50_hal_enable_ta_wdt(info->alg, true);
		if (ret < 0) {
			PE50_ERR("en ta wdt fail(%d)\n", ret);
			return ret;
		}
	}

	ret = pe50_hal_enable_ta_charging(info->alg, en, mV, mA);
	if (ret < 0) {
		PE50_ERR("en ta charging fail(%d)\n", ret);
		return ret;
	}

	if (!en) {
		ret = pe50_hal_enable_ta_wdt(info->alg, false);
		if (ret < 0)
			PE50_ERR("disable ta wdt fail(%d)\n", ret);
	}

	data->vta_setting = mV;
	data->ita_setting = mA;

	return ret;
}

static int pe50_send_notification(struct pe50_algo_info *info,
				  unsigned long val,
				  struct tchg_alg_notify *notify)
{
	return srcu_notifier_call_chain(&info->alg->evt_nh, val, notify);
}

/* Stop PE5.0 charging and reset parameter */
int pe50_stop(struct pe50_algo_info *info, struct pe50_stop_info *sinfo)
{
	int i;
	union com_propval prop = {.intval = 0};
	struct pe50_algo_data *data = info->data;
	struct tchg_alg_notify notify = {
		.evt = EVT_ALGO_STOP,
	};
	int ret = 0;

	if (data->state == PE50_ALGO_STOP) {
		/*
		 * Always clear stop_algo,
		 * in case it is called from pe50_stop_algo
		 */
		atomic_set(&data->stop_algo, 0);
		PE50_DBG("already stop\n");
		return 0;
	}

	PE50_INFO("reset ta(%d), hardreset ta(%d):%d\n", sinfo->reset_ta,
		 sinfo->hardreset_ta, data->notify);
	data->state = PE50_ALGO_STOP;
	atomic_set(&data->stop_algo, 0);
	alarm_cancel(&data->timer);

	if (data->is_swchg_en)
		pe50_enable_swchg_charging(info, false);

	for (i = PE50_DVCHG_SLAVE; i < PE50_DVCHG_MAX; i++) {
		if (!data->is_dvchg_exist[i])
			continue;
		
		ret = pe50_enable_dvchg_charging(info, i, false);
		if (ret < 0) {
			PE50_ERR("(%s) enable dvchg fail(%d)\n",
				pe50_dvchg_role_name[i], ret);
		}
	}

	pe50_set_dvchg_charging(info, false);

	if (!(data->notify & PE50_RESET_NOTIFY)) {
		if (sinfo->hardreset_ta)
			ret = pe50_hal_send_ta_hardreset(info->alg);
	}

	if (sinfo->reset_ta) {
		ret = pe50_hal_set_ta_cap(info->alg, data->pe50_vta_init,
			    		data->pe50_ita_init);
		if(data->running_spec <= SUPPORT_SPEC_4_2){
			prop.intval = true;
			msleep(200);
			ret = pe50_hal_set_tran_dev_prop(info,PE50_TRAN_TC_CHG_DEV,
						TRAN_PROP_TRAN_VOTE_REFRESH,&prop);
			}
		ret = pe50_enable_ta_charging(info, false, data->pe50_vta_init,
								data->pe50_ita_init);
	}
	
	for (i = PE50_DVCHG_SLAVE; i < PE50_DVCHG_MAX; i++) {
		if (!data->is_dvchg_exist[i])
			continue;
		ret = pe50_hal_init_adc(info->alg, to_chgidx(i), false);
		if (ret < 0) {
			PE50_ERR("(%s) init adc fail(%d)\n",
				pe50_dvchg_role_name[i], ret);
		}
	}

	if (!data->pe50_taper_done)
		pe50_report_ffc_status(info, false);

	pe50_hal_set_aicr(info->alg, false, 0);
	pe50_hal_set_ichg(info->alg, false, 0);

	pe50_send_notification(info, EVT_ALGO_STOP, &notify);

	return 0;
}

static inline void pe50_init_algo_data(struct pe50_algo_info *info)
{
	struct pe50_algo_data *data = info->data;

	switch (data->algo_num) {
	case PE50_PPS:
		pe50_init_pps_algo_data(info);
		break;
	case PE50_UFCS:
		break;
	default:
		break;
	}
}

int pe50_earily_restart(struct pe50_algo_info *info)
{
	int i, ret;
	struct pe50_algo_data *data = info->data;

	for (i = PE50_DVCHG_MASTER; i < PE50_DVCHG_MAX; i++) {
		if (!data->is_dvchg_exist[i])
			continue;
		ret = pe50_enable_dvchg_charging(info, i, false);
		if (ret < 0) {
			PE50_ERR("(%s) enable dvchg fail(%d)\n",
				pe50_dvchg_role_name[i], ret);\
			return ret;
		}
	}
	ret = pe50_enable_ta_charging(info, false, data->pe50_vta_init,
				      data->pe50_ita_init);
	if (ret < 0) {
		PE50_ERR("disable ta charging fail(%d)\n", ret);
		return ret;
	}
	pe50_init_algo_data(info);
	return 0;
}

/*
 * Start pe50 timer and run algo
 * It cannot start algo again if algo has been started once before
 * Run once flag will be reset after plugging out TA
 */
static inline int pe50_start(struct pe50_algo_info *info)
{
	int ret,i;
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	ktime_t ktime = ktime_set(0, MS_TO_NS(PE50_INIT_POLLING_INTERVAL));

	PE50_DBG("++\n");

	if (data->run_once) {
		PE50_ERR("already run PE5.0 once\n");
		return -EINVAL;
	}

	data->idvchg_ss_init = desc->idvchg_ss_init;

	/* Check DVCHG registers stat first */
	for (i = PE50_DVCHG_MASTER; i < PE50_DVCHG_MAX; i++) {
		if (!data->is_dvchg_exist[i])
			continue;
		ret = pe50_hal_init_chip(info->alg, to_chgidx(i));
		if (ret < 0) {
			PE50_ERR("(%s) init chip fail(%d)\n",
				pe50_dvchg_role_name[i], ret);
			return ret;
		}
	}

	/* disable sw vbusovp after init chip(ADC init) */
	pe50_hal_enable_sw_vbusovp(info->alg, false);

	/* Parameters that only reset by restarting from outside */
	mutex_lock(&data->ext_lock);
	data->input_current_limit = -1;
	data->cv_limit = -1;
	mutex_unlock(&data->ext_lock);

	pe50_init_algo_data(info);
	pe50_select_vbat_cv(info);
	data->state = PE50_ALGO_INIT;

	pe50_report_ffc_status(info, true);
	alarm_start_relative(&data->timer, ktime);

	return 0;
}

struct meas_r_info {
	u32 vbus;
	u32 ibus;
	u32 vbat;
	u32 ibat;
	u32 vout;
	u32 vta;
	u32 ita;
	u32 r_cable;
	u32 r_bat;
	u32 r_sw;
};

static int pe50_algo_get_r_info(struct pe50_algo_info *info,
				struct meas_r_info *r_info,
				struct pe50_stop_info *sinfo)
{
	int ret;
	struct pe50_algo_data *data = info->data;
	struct pe50_ta_auth_data *auth_data = &data->ta_auth_data[data->running_spec];

	memset(r_info, 0, sizeof(struct meas_r_info));
	if (auth_data->support_meas_cap) {
		ret = pe50_get_ta_cap(info);
		if (ret < 0) {
			PE50_ERR("get ta cap fail(%d)\n", ret);
			sinfo->hardreset_ta = true;
			return ret;
		}
		r_info->ita = data->ita_measure;
		r_info->vta = data->vta_measure;
	}

	ret = pe50_get_adc(info, PE50_ADCCHAN_VBUS, &r_info->vbus);
	if (ret < 0) {
		PE50_ERR("get vbus fail(%d)\n", ret);
		return ret;
	}
	ret = pe50_get_adc(info, PE50_ADCCHAN_IBUS, &r_info->ibus);
	if (ret < 0) {
		PE50_ERR("get ibus fail(%d)\n", ret);
		return ret;
	}
	ret = pe50_get_adc(info, PE50_ADCCHAN_VOUT, &r_info->vout);
	if (ret < 0) {
		PE50_ERR("get vout fail(%d)\n", ret);
		return ret;
	}
	ret = pe50_get_adc(info, PE50_ADCCHAN_VBAT, &r_info->vbat);
	if (ret < 0) {
		PE50_ERR("get vbat fail(%d)\n", ret);
		return ret;
	}

	PE50_INFO("vta:%d,ita:%d,vbus:%d,ibus:%d,vout:%d,vbat:%d,ibat:%d\n",
		 r_info->vta, r_info->ita, r_info->vbus, r_info->ibus,
		 r_info->vout, r_info->vbat, r_info->ibat);

	return 0;
}

int pe50_algo_cal_r_info_with_ta_cap(struct pe50_algo_info *info,
					struct pe50_stop_info *sinfo)
{
	int ret, i;
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	struct meas_r_info r_info, max_r_info, min_r_info;
	struct pe50_stop_info _sinfo = {
		.reset_ta = true,
		.hardreset_ta = false,
	};

	memset(&max_r_info, 0, sizeof(struct meas_r_info));
	memset(&min_r_info, 0, sizeof(struct meas_r_info));
	data->r_bat = data->r_sw = data->r_cable = 0;
	for (i = 0; i < PE50_MEASURE_R_AVG_TIMES + 2; i++) {
		if (atomic_read(&data->stop_algo)) {
			PE50_INFO("stop algo\n");
			goto stop;
		}
		ret = pe50_algo_get_r_info(info, &r_info, sinfo);
		if (ret < 0) {
			PE50_ERR("get r info fail(%d)\n", ret);
			return ret;
		}

		if (r_info.ita == 0) {
			PE50_ERR("ita == 0 fail\n");
			sinfo->hardreset_ta = true;
			return -EINVAL;
		}

		if (r_info.ita < data->idvchg_term &&
		    r_info.vbat >= data->vbat_cv) {
			PE50_INFO("finish PE5.0 charging\n");
			return -EINVAL;
		}

		/* Use absolute instead of relative calculation */
		if(r_info.ibat != 0){
			r_info.r_bat = precise_div(abs(r_info.vbat - data->zcv) * 1000,
						   abs(r_info.ibat));
			if (r_info.r_bat > desc->ircmp_rbat)
				r_info.r_bat = desc->ircmp_rbat;

			r_info.r_sw = precise_div(abs(r_info.vout - r_info.vbat) * 1000,
						  abs(r_info.ibat));
		}else{
			r_info.r_sw = desc->rsw_min;
		}

		if (r_info.r_sw < desc->rsw_min)
			r_info.r_sw = desc->rsw_min;

		r_info.r_cable = precise_div(abs(r_info.vta - data->vbus_cali -
					     r_info.vbus) * 1000,
					     abs(r_info.ita));

		PE50_INFO("r_sw:%d, r_bat:%d, r_cable:%d\n", r_info.r_sw,
			  r_info.r_bat, r_info.r_cable);

		if (i == 0) {
			memcpy(&max_r_info, &r_info,
			       sizeof(struct meas_r_info));
			memcpy(&min_r_info, &r_info,
			       sizeof(struct meas_r_info));
		} else {
			max_r_info.r_bat = max(max_r_info.r_bat, r_info.r_bat);
			max_r_info.r_sw = max(max_r_info.r_sw, r_info.r_sw);
			max_r_info.r_cable = max(max_r_info.r_cable,
						 r_info.r_cable);
			min_r_info.r_bat = min(min_r_info.r_bat, r_info.r_bat);
			min_r_info.r_sw = min(min_r_info.r_sw, r_info.r_sw);
			min_r_info.r_cable = min(min_r_info.r_cable,
						 r_info.r_cable);
		}
		data->r_bat += r_info.r_bat;
		data->r_sw += r_info.r_sw;
		data->r_cable += r_info.r_cable;
	}
	data->r_bat -= (max_r_info.r_bat + min_r_info.r_bat);
	data->r_sw -= (max_r_info.r_sw + min_r_info.r_sw);
	data->r_cable -= (max_r_info.r_cable + min_r_info.r_cable);
	data->r_bat = precise_div(data->r_bat, PE50_MEASURE_R_AVG_TIMES);
	data->r_sw = precise_div(data->r_sw, PE50_MEASURE_R_AVG_TIMES);
	data->r_cable = precise_div(data->r_cable,
				    PE50_MEASURE_R_AVG_TIMES);
	data->r_total = data->r_bat + data->r_sw + data->r_cable;
	PE50_INFO("end:r_sw:%d, r_bat:%d, r_cable:%d r_total:%d\n", data->r_sw,
			  data->r_bat, data->r_cable,data->r_total);
	return 0;
stop:
	pe50_stop(info, &_sinfo);
	return -EIO;
}

/*
 * PE5.0 algorithm initial state
 * It does Foreign Object Detection(FOD)
 */
static int pe50_algo_init(struct pe50_algo_info *info)
{
	struct pe50_algo_data *data = info->data;

	switch (data->algo_num) {
	case PE50_PPS:
		pe50_pps_algo_init(info);
		break;
	case PE50_UFCS:
		break;
	default:
		break;
	}
	return 0;
}

/* Measure resistance of cable/battery/sw and get corressponding ita limit */
static int pe50_algo_measure_r(struct pe50_algo_info *info)
{
	struct pe50_algo_data *data = info->data;

	switch (data->algo_num) {
	case PE50_PPS:
		pe50_pps_algo_measure_r(info);
		break;
	case PE50_UFCS:
		break;
	default:
		break;
	}
	return 0;
}

/* Soft start of divider charger */
static int pe50_algo_ss_dvchg(struct pe50_algo_info *info)
{
	struct pe50_algo_data *data = info->data;
	
	switch (data->algo_num) {
	case PE50_PPS:
		pe50_pps_algo_ss_dvchg(info);
		break;
	case PE50_UFCS:
		break;
	default:
		break;
	}
	return 0;
}

static int pe50_algo_cc_cv(struct pe50_algo_info *info)
{
	struct pe50_algo_data *data = info->data;

	switch (data->algo_num) {
	case PE50_PPS:
		pe50_pps_algo_cc_cv(info);
		break;
	case PE50_UFCS:
		break;
	default:
		break;
	}
	return 0;
}

/*
 * Check TA's status
 * Get status from TA and check temperature, OCP, OTP, and OVP, etc...
 *
 * return true if TA is normal and false if it is abnormal
 */
static bool pe50_check_ta_status(struct pe50_algo_info *info,
				 struct pe50_stop_info *sinfo)
{
	int ret;
	struct pe50_ta_status status;
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	struct pe50_ta_auth_data *auth_data = &data->ta_auth_data[data->running_spec];

	if (!auth_data->support_status)
		return desc->allow_not_check_ta_status;
	ret = pe50_hal_get_ta_status(info->alg, &status);
	if (ret < 0) {
		PE50_ERR("get ta status fail(%d)\n", ret);
		goto err;
	}

	PE50_INFO("temp = %d, (OVP,OCP,OTP) = (%d,%d,%d)\n",
		  status.temperature, status.ovp, status.ocp, status.otp);
	if (status.ocp || status.otp || status.ovp)
		goto err;
	return true;
err:
	if (status.otp && !status.ocp && !status.ovp) {
		pe50_ab_ta_otp_stop_algo(info, PE50_TA_OTP);
	}
	sinfo->hardreset_ta = true;
	return false;
}

/*
 * Check charge run spec status
 *
 * Switch the running mode according to the conditions
 *
 * return false if need switch running mode otherwise return true
 */
static bool pe50_check_switch_spec(struct pe50_algo_info *info,
				 struct pe50_stop_info *sinfo)
{
	/* int ret; */
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	int i;

	if (!desc->support_switch_spec)
		return true;

	if (data->ta_support_spec_cnt <= 1)
		return true;

	if (data->force_spec != -1) {
		PE50_INFO("%s: already switch spec, sepc:%d, ta:%d\n",
			__func__, data->force_spec, data->running_ta);
		return true;
	}

	if (data->state == PE50_ALGO_CC_CV) {
		for (i = 0; i < SUPPORT_SPEC_MAX; i++) {
			if (data->ta_auth_support_spec[i] == true && data->running_spec != i) {
				PE50_INFO("%s: switch to new spec:%d\n", __func__, i);
				break;
			}
		}
		data->force_spec = i;
		data->run_once = false;
		data->ta_ready = false;
		return false;
	}

	return true;
}

/*
 * Check VBUS voltage of divider charger
 * return false if VBUS is over voltage otherwise return true
 */
static bool pe50_check_dvchg_vbusovp(struct pe50_algo_info *info,
				     struct pe50_stop_info *sinfo)
{
	int ret, vbus, i;
	struct pe50_algo_data *data = info->data;
	u32 vbusovp;

	vbusovp = pe50_get_dvchg_vbusovp(info, data->ita_setting);
	for (i = PE50_DVCHG_MASTER; i < PE50_DVCHG_MAX; i++) {
		if (!data->is_dvchg_en[i])
			continue;
		ret = pe50_hal_get_adc(info->alg, to_chgidx(i),
				       PE50_ADCCHAN_VBUS, &vbus);
		if (ret < 0) {
			PE50_ERR("get vbus fail(%d)\n", ret);
			return false;
		}
		PE50_INFO("(%s)vbus(%dmV), vbusovp(%dmV)\n",
			  pe50_dvchg_role_name[i], vbus, vbusovp);
		if (vbus > vbusovp) {
			PE50_ERR("(%s)vbus(%dmV) > vbusovp(%dmV)\n",
				 pe50_dvchg_role_name[i], vbus, vbusovp);
			return false;
		}
	}
	return true;
}

static bool pe50_check_vbatovp(struct pe50_algo_info *info,
			       struct pe50_stop_info *sinfo)
{
	int ret, vbat;
	u32 vbatovp;

	vbatovp = pe50_get_vbatovp(info);
	ret = pe50_get_adc(info, PE50_ADCCHAN_VBAT, &vbat);
	if (ret < 0) {
		PE50_ERR("get vbat fail(%d)\n", ret);
		return false;
	}
	PE50_INFO("vbat(%dmV), vbatovp(%dmV)\n", vbat, vbatovp);
	if (vbat > vbatovp) {
		PE50_ERR("vbat(%dmV) > vbatovp(%dmV)\n", vbat, vbatovp);
		return false;
	}
	return true;
}

struct pe50_thermal_data {
	const char *name;
	int temp;
	enum pe50_thermal_level *temp_level;
	int *temp_level_def;
	int *curlmt;
	int recovery_area;
};

void pe50_ab_tbat_stop_algo(struct pe50_algo_info *info,
				struct pe50_thermal_data *tdata)
{
	struct pe50_algo_desc *desc = info->desc;

	if (!strcmp(tdata->name, "tbat"))
		desc->pe50_ab_dev[PE50_TBAT] = PE50_STOP_BY_AB;

	PE50_INFO("PE50_TBAT = %d", desc->pe50_ab_dev[PE50_TBAT]);

}

static bool pe50_ab_tbat_start_algo(struct pe50_algo_info *info)
{
	struct pe50_algo_desc *desc = info->desc;
	struct pe50_algo_data *data = info->data;
	int tbat = 0;
	int ret;

	if(desc->pe50_ab_dev[PE50_TBAT] != PE50_STOP_BY_AB)
		return 0;

	ret = pe50_get_adc(info, PE50_ADCCHAN_TBAT, &tbat);
	if (ret < 0) {
		PE50_ERR("get tbat fail(%d)\n", ret);
		return false;
	}

	if ((tbat < desc->rechg_tbat_high) &&
		(tbat > desc->rechg_tbat_low)) {
		data->run_once = false;
		data->ta_ready = false;
		desc->pe50_ab_dev[PE50_TBAT] = PE50_REALSE_AB;
		PE50_INFO("PE50_REALSE_AB\n");
	}
	PE50_DBG("PE50_TBAT=%d run_once=%d",
		desc->pe50_ab_dev[PE50_TBAT], data->run_once);

	return desc->pe50_ab_dev[PE50_TBAT];
}

void pe50_ab_ibusucp_stop_algo(struct pe50_algo_info *info,int ab_dev)
{
	struct pe50_algo_desc *desc = info->desc;

	if (ab_dev == PE50_IBUS_UCP &&
		desc->pe50_ab_retry_cnt < PE50_AB_RETRY_MAX) {
		desc->pe50_ab_dev[PE50_IBUS_UCP] = PE50_STOP_BY_AB;
	}

	PE50_ERR("PE50_IBUS_UCP=%d, pe50_ab_retry_cnt=%d",
		desc->pe50_ab_dev[PE50_IBUS_UCP], desc->pe50_ab_retry_cnt);
}

static bool pe50_ab_ibusucp_start_algo(struct pe50_algo_info *info)
{
	struct pe50_algo_desc *desc = info->desc;
	struct pe50_algo_data *data = info->data;

	if (desc->pe50_ab_dev[PE50_IBUS_UCP] == PE50_STOP_BY_AB) {
		data->run_once = false;
		data->ta_ready = false;
		desc->pe50_ab_dev[PE50_IBUS_UCP] = PE50_REALSE_AB;
		desc->pe50_ab_retry_cnt++;
	}

	PE50_ERR("PE50_IBUS_UCP=%d run_once=%d",
		desc->pe50_ab_dev[PE50_IBUS_UCP], data->run_once);

	return desc->pe50_ab_dev[PE50_IBUS_UCP];
}

void pe50_ab_ta_otp_stop_algo(struct pe50_algo_info *info,int ab_dev)
{
	struct pe50_algo_desc *desc = info->desc;

	if (ab_dev == PE50_TA_OTP) {
		desc->pe50_ab_dev[PE50_TA_OTP] = PE50_STOP_BY_AB;
	}

	PE50_ERR("PE50_TA_OTP = %d\n", desc->pe50_ab_dev[PE50_TA_OTP]);
}

static bool pe50_ab_ta_otp_start_algo(struct pe50_algo_info *info)
{
	int ret = 0;
	struct pe50_algo_desc *desc = info->desc;
	struct pe50_algo_data *data = info->data;
	struct pe50_ta_status status;
	struct pe50_ta_auth_data *auth_data = &data->ta_auth_data[data->running_spec];

	if (!auth_data->support_status)
		return 0;

	if(desc->pe50_ab_dev[PE50_TA_OTP] != PE50_STOP_BY_AB)
		return 0;

	ret = pe50_hal_get_ta_status(info->alg, &status);
	if (ret < 0) {
		PE50_ERR("get ta status fail(%d)\n", ret);
		return 0;
	}

	PE50_INFO("recovery temp = %d, (OVP,OCP,OTP) = (%d,%d,%d)\n",
		  status.temperature, status.ovp, status.ocp, status.otp);

	if (desc->pe50_ab_dev[PE50_TA_OTP] == PE50_STOP_BY_AB &&
		!status.ocp && !status.otp && !status.ovp) {
		data->run_once = false;
		desc->pe50_ab_dev[PE50_TA_OTP] = PE50_REALSE_AB;
	}

	PE50_ERR("PE50_TA_OTP = %d\n", desc->pe50_ab_dev[PE50_TA_OTP]);

	return desc->pe50_ab_dev[PE50_TA_OTP];
}

static bool
(*pe50_abnormal_check_fn[])(struct pe50_algo_info *info) = {
	pe50_ab_tbat_start_algo,
	pe50_ab_ibusucp_start_algo,
	pe50_ab_ta_otp_start_algo,
};

static bool pe50_algo_abnormal_check(struct pe50_algo_info *info)
{
	int i;

	PE50_DBG("++\n");
	for (i = 0; i < ARRAY_SIZE(pe50_abnormal_check_fn); i++) {
		if (pe50_abnormal_check_fn[i](info))
			goto err;
	}
	return true;

err:
	PE50_INFO("stop in the abnormal state(%d)\n", i);
	return false;
}

static bool pe50_check_thermal_level(struct pe50_algo_info *info,
				      struct pe50_thermal_data *tdata)
{
	enum pe50_thermal_level higher_level;
	enum pe50_thermal_level lower_level;

	if (tdata->temp >= tdata->temp_level_def[PE50_THERMAL_VERY_HOT]) {
		if (tdata->curlmt[PE50_THERMAL_VERY_HOT] == 0)
			return true;
		PE50_ERR("%s(%d) is over max(%d)\n", tdata->name, tdata->temp,
			tdata->temp_level_def[PE50_THERMAL_VERY_HOT]);
		pe50_ab_tbat_stop_algo(info,tdata);
		return false;
	}
	if (tdata->temp <= tdata->temp_level_def[PE50_THERMAL_VERY_COLD]) {
		if (tdata->curlmt[PE50_THERMAL_VERY_COLD] == 0)
			return true;
		PE50_ERR("%s(%d) is under min(%d)\n", tdata->name, tdata->temp,
			tdata->temp_level_def[PE50_THERMAL_VERY_COLD]);
		pe50_ab_tbat_stop_algo(info,tdata);
		return false;
	}

	higher_level = (*tdata->temp_level == PE50_THERMAL_VERY_HOT) ? *tdata->temp_level : (*tdata->temp_level + 1);
	lower_level = (*tdata->temp_level == PE50_THERMAL_VERY_COLD) ? *tdata->temp_level : (*tdata->temp_level - 1);

	if (tdata->temp >= tdata->temp_level_def[higher_level] &&
			 tdata->curlmt[higher_level] >= 0) {
		*tdata->temp_level = higher_level;
	} else if (tdata->temp <= tdata->temp_level_def[*tdata->temp_level] -
				tdata->recovery_area && tdata->curlmt[lower_level] >= 0) {
		*tdata->temp_level = lower_level;
	}

	PE50_INFO("%s(%d,%d)\n", tdata->name, tdata->temp, *tdata->temp_level);
	return true;
}

/*
 * Check and adjust battery's temperature level
 * return false if battery's temperature is over maximum or under minimum
 * otherwise return true
 */
static bool pe50_check_tbat_level(struct pe50_algo_info *info,
				  struct pe50_stop_info *sinfo)
{
	int ret, tbat;
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	struct pe50_thermal_data tdata = {
		.name = "tbat",
		.temp_level_def = data->tbat_level_def,
		.curlmt = data->tbat_curlmt,
		.temp_level = &data->tbat_level,
		.recovery_area = desc->tbat_recovery_area,
	};

	ret = pe50_get_adc(info, PE50_ADCCHAN_TBAT, &tbat);
	if (ret < 0) {
		PE50_ERR("get tbat fail(%d)\n", ret);
		return false;
	}
	tdata.temp = tbat;

	return pe50_check_thermal_level(info, &tdata);
}

/*
 * Check and adjust TA's temperature level
 * return false if TA's temperature is over maximum
 * otherwise return true
 */
static bool pe50_check_tta_level(struct pe50_algo_info *info,
				 struct pe50_stop_info *sinfo)
{
	int ret;
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	struct pe50_ta_auth_data *auth_data = &data->ta_auth_data[data->running_spec];
	struct pe50_ta_status status;
	struct pe50_thermal_data tdata = {
		.name = "tta",
		.temp_level_def = desc->tta_level_def,
		.curlmt = desc->tta_curlmt,
		.temp_level = &data->tta_level,
		.recovery_area = desc->tta_recovery_area,
	};

	if (!auth_data->support_status)
		return desc->allow_not_check_ta_status;

	if (data->algo_num == PE50_PPS)
		return true;

	ret = pe50_hal_get_ta_status(info->alg, &status);
	if (ret < 0) {
		PE50_ERR("get tta fail(%d)\n", ret);
		sinfo->hardreset_ta = true;
		return false;
	}

	tdata.temp = status.temperature;
	return pe50_check_thermal_level(info, &tdata);
}

/*
 * Limit the maximum power running time of the charger
 **/
static bool pe50_check_charging_duration(struct pe50_algo_info *info,
				 struct pe50_stop_info *sinfo)
{
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	struct pe50_ta_auth_data *auth_data = &data->ta_auth_data[data->running_spec];

	 if (data->dtime.tv_sec > auth_data->full_power_run_time &&
		data->pe50_over_power && !data->full_power_flag) {

		data->full_power_flag = true;

		if (auth_data->pdp < desc->project_power) {
			data->pwr_ratio = 70;
		} else if (auth_data->pdp == desc->project_power) {
			data->pwr_ratio = desc->project_pwr_ratio;
		} else {
			data->pwr_ratio = 100;
		}

		data->ita_pwr_lmt = pe50_get_ita_pwr_lmt_by_vta(info, data->vta_setting);
		PE50_INFO("check run time, now run time:%d > full_power_run_time:%d\n",
			(int)data->dtime.tv_sec, auth_data->full_power_run_time);
	}

	return true;
}

/*
 * Check and adjust divider charger's temperature level
 * return false if divider charger's temperature is over maximum
 * otherwise return true
 */
static bool pe50_check_tdvchg_level(struct pe50_algo_info *info,
				    struct pe50_stop_info *sinfo)
{
	int ret, i, tdvchg;
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	char buf[14];
	struct pe50_thermal_data tdata = {
		.temp_level_def = desc->tdvchg_level_def,
		.curlmt = desc->tdvchg_curlmt,
		.temp_level = &data->tdvchg_level,
		.recovery_area = desc->tdvchg_recovery_area,
	};

	for (i = PE50_DVCHG_MASTER; i < PE50_DVCHG_MAX; i++) {
		if (!data->is_dvchg_en[i])
			continue;
		ret = pe50_hal_get_adc(info->alg, to_chgidx(i),
				       PE50_ADCCHAN_TCHG, &tdvchg);
		if (ret < 0) {
			PE50_ERR("get tdvchg fail(%d)\n", ret);
			return false;
		}
		snprintf(buf, 8 + strlen(pe50_dvchg_role_name[i]), "tdvchg_%s",
			 pe50_dvchg_role_name[i]);
		tdata.name = buf;
		tdata.temp = tdvchg;
		if (!pe50_check_thermal_level(info, &tdata))
			return false;
	}

	return true;
}

static bool pe50_check_tpcb_level(struct pe50_algo_info *info,
                                   struct pe50_stop_info *sinfo)
{
	int tpcb;
	int ret = 0;
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	struct pe50_thermal_data tdata = {
		.name = "tpcb",
		.temp_level_def = data->tpcb_level_def,
		.curlmt = data->tpcb_curlmt,
		.temp_level = &data->tpcb_level,
		.recovery_area = desc->tpcb_recovery_area,
	};

	ret = pe50_get_adc(info, PE50_ADCCHAN_TPCB, &tpcb);
	if (ret < 0) {
		PE50_ERR("get tpcb fail(%d)\n", ret);
		return false;
	}
	tdata.temp = tpcb;
	PE50_ERR("tpcb %d\n", tdata.temp);

	return pe50_check_thermal_level(info, &tdata);
}

static bool pe50_check_tpa_level(struct pe50_algo_info *info,
                                   struct pe50_stop_info *sinfo)
{
	int tpa;
	int ret = 0;
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	struct pe50_thermal_data tdata = {
		.name = "tpa",
		.temp_level_def = data->tpa_level_def,
		.curlmt = data->tpa_curlmt,
		.temp_level = &data->tpa_level,
		.recovery_area = desc->tpa_recovery_area,
	};

	ret = pe50_get_adc(info, PE50_ADCCHAN_TPA, &tpa);
	if (ret < 0) {
		PE50_ERR("get tpa fail(%d)\n", ret);
		return false;
	}
	tdata.temp = tpa;
	PE50_ERR("tpa %d\n", tdata.temp);

	return pe50_check_thermal_level(info, &tdata);
}

static bool pe50_check_sys_power_level(struct pe50_algo_info *info,
				   struct pe50_stop_info *sinfo)
{
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	struct pe50_thermal_data tdata = {
		.name = "sys_power",
		.temp_level_def = desc->sys_power_level_def,
		.curlmt = desc->sys_power_curlmt,
		.temp_level = &data->sys_power_level,
		.recovery_area = desc->sys_power_recovery_area,
	};

	if (data->state != PE50_ALGO_CC_CV)
		return true;

	tdata.temp = data->dynamic_sys_power;

	return pe50_check_thermal_level(info, &tdata);
}

static bool
(*pe50_safety_check_fn[])(struct pe50_algo_info *info,
			  struct pe50_stop_info *sinfo) = {
	pe50_check_ta_status,
	pe50_check_dvchg_vbusovp,
	pe50_check_tbat_level,
	pe50_check_vbatovp,
	pe50_check_tta_level,
	pe50_check_tdvchg_level,
	pe50_check_tpcb_level,
	pe50_check_tpa_level,
	pe50_check_sys_power_level,
	pe50_check_charging_duration,
	pe50_check_switch_spec,
};

static bool pe50_algo_safety_check(struct pe50_algo_info *info)
{
	int i;
	struct pe50_stop_info sinfo = {
		.reset_ta = true,
		.hardreset_ta = false,
	};

	PE50_DBG("++\n");
	for (i = 0; i < ARRAY_SIZE(pe50_safety_check_fn); i++) {
		if (!pe50_safety_check_fn[i](info, &sinfo))
			goto err;
	}
	return true;

err:
	pe50_stop(info, &sinfo);
	return false;
}

static void pe50_check_algo_name(struct pe50_algo_info *info)
{
	struct pe50_algo_data *data = info->data;

	if (!strcmp(data->adapter_name, "pd_adapter")) {
		data->algo_num = PE50_PPS;
	} else {
		data->algo_num = PE50_NONE;
	}
}

static int pe50_force_protocol_ratio(struct pe50_algo_info *info)
{
	struct pe50_algo_data *data = info->data;
	struct pe50_ta_auth_data *auth_data = &data->ta_auth_data[data->force_spec];
	int ret;
	int i;

	for (i = 0; i < SUPPORT_SPEC_MAX; i++) {

		if (i != data->force_spec) {
			PE50_INFO("no force ratio, continue\n");
			data->ta_auth_support_spec[i] = false;
			continue;
		}

		ret = pe50_hal_authenticate_ta(info->alg, auth_data, data->running_ta);
		if (ret == ALG_READY) {
			auth_data->vcap_min = max(auth_data->vta_min,
				auth_data->vcap_min);
			auth_data->vcap_max = min(auth_data->vta_max,
				auth_data->vcap_max);

			data->ta_auth_support_spec[i] = true;
			PE50_INFO("%s: authenticate success, support spec:%d\n",
					__func__, i);
			continue;
		} else if (ret == ALG_TA_CHECKING) {
			return ret;
		} else if (ret == ALG_TA_NOT_SUPPORT) {
			data->ta_auth_support_spec[i] = false;
			PE50_DBG("%s:ta auth not support sepc:%d\n", __func__, i);
			auth_data->pdp = 0;
			continue;
		}
	}

	if (data->ta_auth_support_spec[data->force_spec] != true) {
		PE50_INFO("%s: force ratio auth fail\n", __func__);
		return ALG_TA_NOT_SUPPORT;
	}

	PE50_INFO("%s: froce spec auth succ:%d\n", __func__, data->force_spec);
	auth_data = &data->ta_auth_data[data->force_spec];
	data->ta_ready = true;
	data->running_spec = data->force_spec;

	data->adapter_name = pe50_hal_get_adapter_name(info->alg);
	if (IS_ERR_OR_NULL(data->adapter_name)) {
		PE50_ERR("%s: NULL adapter\n", __func__);
		return ALG_INIT_FAIL;
	}
	
	pe50_check_algo_name(info);
	
	pe50_report_charing_animation(info);

	auth_data->cable_capability = pe50_check_cable_capacity(info);

	return ALG_READY;
}

static int pe50_is_ta_rdy(struct pe50_algo_info *info)
{
	int ret;
	int i, j;
	struct pe50_algo_desc *desc = info->desc;
	struct pe50_algo_data *data = info->data;
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(info->alg);
	struct pe50_ta_auth_data *auth_data = NULL;
	bool ta_auth_succ = false;

	if (data->ta_ready) {
		PE50_INFO("%s: ta already authenticate, return\n", __func__);
		return ALG_READY;
	}

	if (data->force_spec != -1) {
		ret = pe50_force_protocol_ratio(info);
		PE50_INFO("pe50 force protocol done:%d\n", ret);
		return ret;
	}

	for (i = 0; i < hal->support_ta_cnt; i++) {
		if (!hal->adapters[i])
			continue;

		ta_auth_succ = false;

		if (strcmp(hal->adapters[i]->dev.kobj.name, "pd_adapter") == 0) {
			ret = pe50_hal_is_pd_adapter_ready(info->alg);
			if (ret == ALG_READY) {
				PE50_INFO("pd type ready!, go to auth\n");
			} else if (ret == ALG_TA_CHECKING) {
				PE50_INFO("pd type not ready, waiting\n");
				return ret;
			} else {
				PE50_DBG("pd type unknown, continue\n");
				continue;
			}
		}

		for (j = 0; j < SUPPORT_SPEC_MAX; j++) {

			auth_data = &data->ta_auth_data[j];

			if (desc->support_spec[j] == 0) {
				data->ta_auth_support_spec[j] = false;
				PE50_DBG("%s: not support sepc:%d\n", __func__, j);
				continue;
			}

			auth_data->vcap_max = desc->vta_cap_max[j];
			auth_data->vcap_min = desc->vta_cap_min[j];
			auth_data->icap_min = desc->ita_cap_min[j];
	
			ret = pe50_hal_authenticate_ta(info->alg, auth_data, i);
			if (ret == ALG_READY) {
				auth_data->vcap_min = max(auth_data->vta_min,
					auth_data->vcap_min);
				auth_data->vcap_max = min(auth_data->vta_max,
					auth_data->vcap_max);
	
				data->ta_auth_support_spec[j] = true;
				data->ta_support_spec_cnt++;
				ta_auth_succ = true;
				PE50_INFO("%s: authenticate success, support spec:%d\n",
						__func__, j);
				continue;
			} else if (ret == ALG_TA_CHECKING) {
				return ret;
			} else if (ret == ALG_TA_NOT_SUPPORT) {
				data->ta_auth_support_spec[j] = false;
				PE50_DBG("%s:ta auth not support sepc:%d\n", __func__, j);
				auth_data->pdp = 0;
				continue;
			}
		}
		if (ta_auth_succ) {
			data->running_ta = i;
			PE50_INFO("%s: adapter:%s auth succ\n",
				__func__, hal->adapters[i]->dev.kobj.name);
			break;
		} else {
			PE50_DBG("%s: adapter:%s auth fail, exit protocol\n",
				__func__, hal->adapters[i]->dev.kobj.name);
			tadapter_dev_exit_protocol(hal->adapters[i]);
		}
	}

	for (i = 0; i < SUPPORT_SPEC_MAX; i++) {
		if (data->ta_auth_support_spec[i] == true) {
			PE50_INFO("%s: ta_auth_support_spec:%d\n", __func__, i);
			auth_data = &data->ta_auth_data[i];
			data->ta_ready = true;
			data->running_spec = i;
			break;
		}
	}

	if (i == SUPPORT_SPEC_MAX) {
		PE50_INFO("%s: all support sepc auth fail\n", __func__);
		return ALG_TA_NOT_SUPPORT;
	}

	data->adapter_name = pe50_hal_get_adapter_name(info->alg);
	if (IS_ERR_OR_NULL(data->adapter_name)) {
		PE50_ERR("%s: NULL adapter\n", __func__);
		return ALG_INIT_FAIL;
	}
    
	pe50_check_algo_name(info);
	
	pe50_report_charing_animation(info);

	auth_data->cable_capability = pe50_check_cable_capacity(info);

	return ALG_READY;
}

static inline void pe50_wakeup_algo_thread(struct pe50_algo_data *data)
{
	PE50_DBG("++\n");

	atomic_set(&data->wakeup_thread, 1);
	wake_up_interruptible(&data->wq);
}

static enum alarmtimer_restart pe50_algo_timer_cb(struct alarm *alarm, ktime_t now)
{
	struct pe50_algo_data *data =
		container_of(alarm, struct pe50_algo_data, timer);

	PE50_DBG("++\n");
	pe50_wakeup_algo_thread(data);
	return ALARMTIMER_NORESTART;
}

/*
 * Check charging time of pe5.0 algorithm
 * return false if timeout otherwise return true
 */
static bool pe50_algo_check_charging_time(struct pe50_algo_info *info)
{
	struct pe50_algo_data *data = info->data;
	ktime_t etime, time_diff;
	struct pe50_stop_info sinfo = {
		.reset_ta = true,
		.hardreset_ta = false,
	};

	etime = ktime_get_boottime();
	time_diff = ktime_sub(etime, data->stime);
	data->dtime = ktime_to_timespec64(time_diff);
	if (data->dtime.tv_sec >= data->chg_time_max) {
		PE50_ERR("PE5.0 algo timeout(%d, %d)\n", (int)data->dtime.tv_sec,
			 data->chg_time_max);
		pe50_stop(info, &sinfo);
		return false;
	}
	return true;
}

static inline int __pe50_plugout_reset(struct pe50_algo_info *info,
				       struct pe50_stop_info *sinfo)
{
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;

	PE50_INFO("++\n");
	data->ta_ready = false;
	data->run_once = false;
	data->ta_auth_data[data->running_spec].pdp = 0;
	desc->pe50_ab_retry_cnt = 0;
	data->pe50_taper_done = false;
	memset(desc->pe50_ab_dev, 0, PE50_ABMMORMAL_MAX);
	memset(data->ta_auth_support_spec, 0, sizeof(data->ta_auth_support_spec));
	data->ta_support_spec_cnt = 0;
	data->force_spec = -1;
	data->running_ta = -1;
	pe50_hal_ta_reset(info->alg);
	pe50_stop(info, sinfo);

	return 0;
}

static int pe50_notify_hardreset_hdlr(struct pe50_algo_info *info)
{
	struct pe50_algo_data *data = info->data;
	struct pe50_stop_info sinfo = {
		.reset_ta = false,
		.hardreset_ta = false,
	};

	PE50_INFO("++\n");

	if (data->state != PE50_ALGO_STOP && data->algo_num == PE50_PPS)
		return __pe50_plugout_reset(info, &sinfo);

	return 0;
}

static int pe50_notify_detach_hdlr(struct pe50_algo_info *info)
{
	struct pe50_stop_info sinfo = {
		.reset_ta = false,
		.hardreset_ta = false,
	};

	PE50_INFO("++\n");
	return __pe50_plugout_reset(info, &sinfo);
}

static int pe50_notify_hwerr_hdlr(struct pe50_algo_info *info)
{
	struct pe50_stop_info sinfo = {
		.reset_ta = true,
		.hardreset_ta = false,
	};

	PE50_INFO("++\n");
	return pe50_stop(info, &sinfo);
}

static int pe50_notify_ibusucpf_hdlr(struct pe50_algo_info *info)
{
	int ret, ibus;
	struct pe50_algo_data *data = info->data;

	if (data->ignore_ibusucpf) {
		PE50_INFO("ignore ibusucpf\n");
		data->ignore_ibusucpf = false;
		return 0;
	}
	if (!data->is_dvchg_en[PE50_DVCHG_MASTER]) {
		PE50_INFO("master dvchg is off\n");
		return 0;
	}

	/* Last chance */
	ret = pe50_get_adc(info, PE50_ADCCHAN_IBUS, &ibus);
	if (ret < 0) {
		PE50_ERR("get dvchg ibus fail(%d)\n", ret);
		goto out;
	}

	pe50_ab_ibusucp_stop_algo(info,PE50_IBUS_UCP);
	if (ibus < PE50_IBUSUCPF_RECHECK || data->pe50_auto_test_ibusucp) {
		PE50_ERR("ibus(%d) < recheck(%d) or pe50_auto_test_ibusucp is true(%d)\n",
			ibus, PE50_IBUSUCPF_RECHECK, data->pe50_auto_test_ibusucp);
		goto out;
	}

	PE50_INFO("recheck ibus and it is not ucp\n");
	return 0;
out:
	return pe50_notify_hwerr_hdlr(info);
}

static int pe50_notify_vbatovp_alarm_hdlr(struct pe50_algo_info *info)
{
	int ret;
	struct pe50_algo_data *data = info->data;
	struct pe50_stop_info sinfo = {
		.reset_ta = true,
		.hardreset_ta = false,
	};

	if (data->state == PE50_ALGO_STOP)
		return 0;

	PE50_INFO("++\n");
	ret = pe50_hal_reset_vbatovp_alarm(info->alg, DVCHG1);
	if (ret < 0) {
		PE50_ERR("reset vbatovp alarm fail(%d)\n", ret);
		return pe50_stop(info, &sinfo);
	}

	return 0;
}

static int pe50_notify_vbusovp_alarm_hdlr(struct pe50_algo_info *info)
{
	int ret;
	struct pe50_algo_data *data = info->data;
	struct pe50_stop_info sinfo = {
		.reset_ta = true,
		.hardreset_ta = false,
	};

	if (data->state == PE50_ALGO_STOP)
		return 0;

	PE50_INFO("++\n");
	ret = pe50_hal_reset_vbusovp_alarm(info->alg, DVCHG1);
	if (ret < 0) {
		PE50_ERR("reset vbusovp alarm fail(%d)\n", ret);
		return pe50_stop(info, &sinfo);
	}

	return 0;
}

static int
(*pe50_notify_pre_hdlr[EVT_MAX])(struct pe50_algo_info *info) = {
	[EVT_DETACH] = pe50_notify_detach_hdlr,
	[EVT_HARDRESET] = pe50_notify_hardreset_hdlr,
	[EVT_VBUSOVP] = pe50_notify_hwerr_hdlr,
	[EVT_IBUSOCP] = pe50_notify_hwerr_hdlr,
	[EVT_IBUSUCP_FALL] = pe50_notify_ibusucpf_hdlr,
	[EVT_VBATOVP] = pe50_notify_hwerr_hdlr,
	[EVT_IBATOCP] = pe50_notify_hwerr_hdlr,
	[EVT_VOUTOVP] = pe50_notify_hwerr_hdlr,
	[EVT_VDROVP] = pe50_notify_hwerr_hdlr,
	[EVT_VBATOVP_ALARM] = pe50_notify_vbatovp_alarm_hdlr,
};

static int
(*pe50_notify_post_hdlr[EVT_MAX])(struct pe50_algo_info *info) = {
	[EVT_DETACH] = pe50_notify_detach_hdlr,
	[EVT_HARDRESET] = pe50_notify_hardreset_hdlr,
	[EVT_VBUSOVP] = pe50_notify_hwerr_hdlr,
	[EVT_IBUSOCP] = pe50_notify_hwerr_hdlr,
	[EVT_IBUSUCP_FALL] = pe50_notify_ibusucpf_hdlr,
	[EVT_VBATOVP] = pe50_notify_hwerr_hdlr,
	[EVT_IBATOCP] = pe50_notify_hwerr_hdlr,
	[EVT_VOUTOVP] = pe50_notify_hwerr_hdlr,
	[EVT_VDROVP] = pe50_notify_hwerr_hdlr,
	[EVT_VBATOVP_ALARM] = pe50_notify_vbatovp_alarm_hdlr,
	[EVT_VBUSOVP_ALARM] = pe50_notify_vbusovp_alarm_hdlr,
};

static int pe50_pre_handle_notify_evt(struct pe50_algo_info *info)
{
	int i;
	struct pe50_algo_data *data = info->data;

	mutex_lock(&data->notify_lock);
	PE50_DBG("0x%08X\n", data->notify);
	for (i = 0; i < EVT_MAX; i++) {
		if ((data->notify & BIT(i)) && pe50_notify_pre_hdlr[i]) {
			data->notify &= ~BIT(i);
			mutex_unlock(&data->notify_lock);
			pe50_notify_pre_hdlr[i](info);
			mutex_lock(&data->notify_lock);
		}
	}
	mutex_unlock(&data->notify_lock);

	return 0;
}

static int pe50_post_handle_notify_evt(struct pe50_algo_info *info)
{
	int i;
	struct pe50_algo_data *data = info->data;

	mutex_lock(&data->notify_lock);
	PE50_DBG("0x%08X\n", data->notify);
	for (i = 0; i < EVT_MAX; i++) {
		if ((data->notify & BIT(i)) && pe50_notify_post_hdlr[i]) {
			data->notify &= ~BIT(i);
			mutex_unlock(&data->notify_lock);
			pe50_notify_post_hdlr[i](info);
			mutex_lock(&data->notify_lock);
		}
	}
	mutex_unlock(&data->notify_lock);

	return 0;
}

static int pe50_dump_charging_info(struct pe50_algo_info *info)
{
	int ret, i;
	int vbus, ibus[PE50_DVCHG_MAX] = {0};
	int ibus_swchg = 0, vbat, ibat;
	int vout[PE50_DVCHG_MAX] = {0};
	int ibus_total = 0, vsys, tbat;
	struct pe50_algo_data *data = info->data;
	u32 soc;

	/* vbus */
	ret = pe50_get_adc(info, PE50_ADCCHAN_VBUS, &vbus);
	if (ret < 0)
		PE50_ERR("get vbus fail(%d)\n", ret);
	/* ibus */
	for (i = 0; i < PE50_DVCHG_MAX; i++) {
		if (!data->is_dvchg_en[i])
			continue;
		ret = pe50_hal_get_adc(info->alg, to_chgidx(i),
				       PE50_ADCCHAN_IBUS, &ibus[i]);
		if (ret < 0) {
			PE50_ERR("get %s ibus fail\n", pe50_dvchg_role_name[i]);
			continue;
		}
		ibus_total += ibus[i];
	}
	data->ibus_total = ibus_total;

	if (data->is_swchg_en) {
		ret = pe50_hal_get_adc(info->alg, CHG1, PE50_ADCCHAN_IBUS,
				       &ibus_swchg);
		if (ret < 0)
			PE50_ERR("get swchg ibus fail\n");
	}
	/* vbat */
	ret = pe50_get_adc(info, PE50_ADCCHAN_VBAT, &vbat);
	if (ret < 0)
		PE50_ERR("get vbat fail\n");
	/* ibat */
	ret = pe50_get_adc(info, PE50_ADCCHAN_IBAT, &ibat);
	if (ret < 0)
		PE50_ERR("get ibat fail\n");

	ret = pe50_get_ta_cap_by_supportive(info, &data->vta_measure,
					     &data->ita_measure);
	if (ret < 0)
		PE50_ERR("get ta measure cap fail(%d)\n", ret);

	/* vout */
	for (i = PE50_DVCHG_MASTER; i < PE50_DVCHG_MAX; i++) {
		if (!data->is_dvchg_en[i])
			continue;
		ret = pe50_hal_get_adc(info->alg, to_chgidx(i), PE50_ADCCHAN_VOUT,
					   &vout[i]);
		if (ret < 0) {
			PE50_ERR("get %s ibus fail\n", pe50_dvchg_role_name[i]);
			continue;
		}
	}

	ret = pe50_hal_get_adc(info->alg, CHG1, PE50_ADCCHAN_VSYS,
				   &vsys);
	if (ret < 0) {
		PE50_ERR("get vsys from swchg fail\n");
	}

	ret = pe50_get_adc(info, PE50_ADCCHAN_TBAT, &tbat);

	ret = pe50_hal_get_soc(info->alg, &soc);
	if (ret < 0) {
		PE50_ERR("get soc fail\n");
	}

	PE50_INFO("vbus,ibus(master,slave,sw),vbat,ibat=%d,(%d,%d,%d,%d),%d,%d\n",
		 vbus, ibus[PE50_DVCHG_MASTER], ibus[PE50_DVCHG_SLAVE], ibus[PE50_DVCHG_THIRD],
		 ibus_swchg, vbat, ibat);
	PE50_INFO("vta,ita(set,meas)=(%d,%d),(%d,%d),force_cv=%d\n",
		 data->vta_setting, data->vta_measure, data->ita_setting,
		 data->ita_measure, data->force_ta_cv);
	PE50_INFO("vout(master,slave,third)=(%d,%d,%d)\n",
		 vout[PE50_DVCHG_MASTER], vout[PE50_DVCHG_SLAVE], vout[PE50_DVCHG_THIRD]);
	PE50_INFO("[PE5] %d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
		 vbus, ibus[PE50_DVCHG_MASTER], ibus[PE50_DVCHG_SLAVE],ibus_total, vbat, ibat,
		 tbat, vsys, soc,
		 vout[PE50_DVCHG_MASTER], vout[PE50_DVCHG_SLAVE]);

	return 0;
}

#define MIN_SYS_POWER    1

struct meas_info {
	int ibus;
	int ibat;
	int isys;
	int vout;
	int sys_power;
};

static int pe50_algo_get_meas_info(struct pe50_algo_info *info,
				 struct meas_info *meas_info)
{
	struct pe50_algo_data *data = info->data;
	int ret;
	memset(meas_info, 0, sizeof(struct meas_info));

	ret = pe50_get_adc(info, PE50_ADCCHAN_IBUS, &meas_info->ibus);
	if (ret < 0) {
		PE50_ERR("get ibus fail(%d)\n", ret);
		return ret;
	}
	ret = pe50_get_adc(info, PE50_ADCCHAN_IBAT, &meas_info->ibat);
	if (ret < 0) {
		PE50_ERR("get ibat fail(%d)\n", ret);
		return ret;
	}
	ret = pe50_get_adc(info, PE50_ADCCHAN_VOUT, &meas_info->vout);
	if (ret < 0) {
		PE50_ERR("get vout fail(%d)\n", ret);
		return ret;
	}

	meas_info->isys = data->conversion_ratio * meas_info->ibus - meas_info->ibat;

	PE50_INFO("ibus:%d, ibat:%d, isys:%d, vout:%d\n",
		meas_info->ibus, meas_info->ibat, meas_info->isys, meas_info->vout);
	return 0;
}

static int pe50_algo_cal_meas_info_with_ta_cap(struct pe50_algo_info *info)
{
	int ret, i;
	int dynamic_sys_power = 0;
	struct pe50_algo_data *data = info->data;
	struct meas_info meas_info, max_meas_info, min_meas_info;

	memset(&max_meas_info, 0, sizeof(struct meas_info));
	memset(&min_meas_info, 0, sizeof(struct meas_info));
	for (i = 0; i < PE50_MEASURE_R_AVG_TIMES + 2; i++) {
		if (atomic_read(&data->stop_algo)) {
			PE50_INFO("stop algo\n");
			return 0;
		}

		ret = pe50_algo_get_meas_info(info, &meas_info);
		if (ret < 0) {
			PE50_ERR("get r info fail(%d)\n", ret);
			return ret;
		}

		meas_info.sys_power = meas_info.isys * meas_info.vout / 1000;
		meas_info.sys_power = max(meas_info.sys_power, MIN_SYS_POWER);

		PE50_INFO("sys_power:%d\n", meas_info.sys_power);

		if (i == 0) {
			memcpy(&max_meas_info, &meas_info,
			       sizeof(struct meas_info));
			memcpy(&min_meas_info, &meas_info,
			       sizeof(struct meas_info));
		} else {
			max_meas_info.sys_power = max(max_meas_info.sys_power,
						 meas_info.sys_power);
			min_meas_info.sys_power = min(min_meas_info.sys_power,
						 meas_info.sys_power);
		}
		dynamic_sys_power += meas_info.sys_power;
	}
	dynamic_sys_power -= (max_meas_info.sys_power + min_meas_info.sys_power);
	dynamic_sys_power = precise_div(dynamic_sys_power,
					PE50_MEASURE_R_AVG_TIMES);
	data->dynamic_sys_power = dynamic_sys_power;
	return 0;
}

static bool pe50_dynamic_cal_info(struct pe50_algo_info *info)
{
	struct pe50_algo_data *data = info->data;
	int cal_freq, ret;
	static int count = 0;

	if (data->state != PE50_ALGO_CC_CV ||
		(data->bootmode == KERNEL_POWER_OFF_CHARGING_BOOT ||
		data->bootmode == LOW_POWER_OFF_CHARGING_BOOT))
		return true;

	cal_freq = data->screen_on ? PE50_HIGH_CAL_FREQ : PE50_LOW_CAL_FREQ;

	if (++count % cal_freq != 0)
		return true;
	else
		count = 0;

	ret = pe50_algo_cal_meas_info_with_ta_cap(info);
	if (ret != 0) {
		PE50_ERR("cal meas info failed:%d\n", ret);
		return false;
	}

	PE50_INFO("dynamic_sys_power:%d\n", data->dynamic_sys_power);
	return true;
}

static bool pe50_check_gauge_fw_updating(struct pe50_algo_info *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};
	struct pe50_algo_data *data = info->data;
	
	ret = pe50_check_tran_dev_ptr(&data->tc_gauge, "tc_gauge");
	if (ret < 0) {
		pr_info("%s Couldn't get gauge_dev\n", __func__);
		return false;
	}

	tran_dev_get_prop(data->tc_gauge, TRAN_PROP_BATT_FW_STATUS, &prop);

	pr_info("%s: %d\n", __func__, prop.intval);

	return !!prop.intval;
}

static bool pe50_check_dual_batt_run_mode(struct pe50_algo_info *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	
	if (!desc->support_dual_battery)
		return true;

	ret = pe50_check_tran_dev_ptr(&data->tc_gauge, "tc_gauge");
	if (ret < 0) {
		pr_info("%s Couldn't get gauge_dev\n", __func__);
		return false;
	}

	tran_dev_get_prop(data->tc_gauge, TRAN_PROP_BATT_RUN_MODE, &prop);
	if (prop.intval == GAUGE_DUAL_BATT_MODE)
		return true;

	pr_info("%s: %d\n", __func__, prop.intval);

	return false;
}

static int pe50_algo_threadfn(void *param)
{
	struct pe50_algo_info *info = param;
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	u32 sec, ms, polling_interval;
	ktime_t ktime;
	struct pe50_stop_info sinfo = {
		.reset_ta = true,
		.hardreset_ta = false,
	};

	while (!kthread_should_stop()) {
		wait_event_interruptible(data->wq,
					 atomic_read(&data->wakeup_thread));
		pm_stay_awake(info->dev);
		if (atomic_read(&data->stop_thread)) {
			pm_relax(info->dev);
			break;
		}
		atomic_set(&data->wakeup_thread, 0);
		mutex_lock(&data->lock);
		PE50_INFO("state = %s\n", pe50_algo_state_name[data->state]);
		if (atomic_read(&data->stop_algo))
			pe50_stop(info, &sinfo);
		pe50_pre_handle_notify_evt(info);
		if (data->state != PE50_ALGO_STOP) {
			pe50_algo_check_charging_time(info);
			pe50_calculate_vbat_ircmp(info);
			pe50_select_vbat_cv(info);
			pe50_dynamic_cal_info(info);
			pe50_dump_charging_info(info);
			if (!pe50_algo_safety_check(info))
				goto cont;
		}
		switch (data->state) {
		case PE50_ALGO_INIT:
			pe50_algo_init(info);
			break;
		case PE50_ALGO_MEASURE_R:
			pe50_algo_measure_r(info);
			break;
		case PE50_ALGO_SS_DVCHG:
			pe50_algo_ss_dvchg(info);
			break;
		case PE50_ALGO_CC_CV:
			pe50_algo_cc_cv(info);
			break;
		case PE50_ALGO_STOP:
			PE50_INFO("PE5.0 STOP\n");
			break;
		default:
			PE50_ERR("NO SUCH STATE\n");
			break;
		}
		pe50_post_handle_notify_evt(info);
		if (data->state != PE50_ALGO_STOP) {
			pe50_dump_charging_info(info);
			if (data->state == PE50_ALGO_CC_CV)
				polling_interval = desc->polling_interval;
			else
				polling_interval =
					PE50_INIT_POLLING_INTERVAL;
			sec = polling_interval / 1000;
			ms = polling_interval % 1000;
			ktime = ktime_set(sec, MS_TO_NS(ms));
			alarm_start_relative(&data->timer, ktime);
		}
cont:
		mutex_unlock(&data->lock);
		pm_relax(info->dev);
	}
	return 0;
}

/* =================================================================== */
/* PE5.0 Algo OPS                                                        */
/* =================================================================== */
static int pe50_init_algo(struct tchg_alg_device *alg)
{
	int ret = 0;
	struct pe50_algo_info *info = tchg_alg_dev_get_drvdata(alg);
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;

	mutex_lock(&data->lock);
	PE50_DBG("++\n");

	if (data->inited) {
		PE50_INFO("already inited\n");
		goto out;
	}
	if (pe50_hal_init_hardware(info->alg, desc->support_ta,
				   desc->support_ta_cnt)) {
		PE50_ERR("%s:init hw fail\n", __func__);
		goto out;
	}
	data->inited = true;
	PE50_INFO("successfully\n");
out:
	mutex_unlock(&data->lock);
	return ret;
}

static bool pe50_is_algo_running(struct tchg_alg_device *alg)
{
	struct pe50_algo_info *info = tchg_alg_dev_get_drvdata(alg);
	struct pe50_algo_data *data = info->data;
	bool running = false;

	if (!data->inited) {
		running = false;
		goto out_unlock;
	}
	running = !(data->state == PE50_ALGO_STOP);
	PE50_DBG("running = %d\n", running);
out_unlock:
	return running;
}

static bool pe50_is_ta_ready(struct tchg_alg_device *alg)
{
	int ret = 0;
	bool is_rdy = false;
	struct pe50_algo_info *info = tchg_alg_dev_get_drvdata(alg);
	
	ret = pe50_is_ta_rdy(info);
	if (ret == ALG_READY)
		is_rdy = true;

	PE50_INFO("%s -- is_rdy = %d\n",__func__,is_rdy);
	
	return is_rdy;
}

static int pe50_is_algo_ready(struct tchg_alg_device *alg)
{
	int vbat = 0;
	int ret = 0;
	struct pe50_algo_info *info = tchg_alg_dev_get_drvdata(alg);
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;

	if (pe50_is_algo_running(info->alg))
		return ALG_RUNNING;

	mutex_lock(&data->lock);

	PE50_INFO("++\n");

	if (!data->inited) {
		ret = ALG_INIT_FAIL;
		goto out;
	}

	if (!pe50_algo_abnormal_check(info)) {
		ret = ALG_TA_CHECKING;
		goto out;
	}

	if (pe50_check_gauge_fw_updating(info)) {
		PE50_ERR("fg update fw, wait...\n");
		ret = ALG_TA_CHECKING;
		goto out;
	}

	if (!pe50_check_dual_batt_run_mode(info)) {
		PE50_ERR("dual batt project, but batt err\n");
		ret = ALG_TA_NOT_SUPPORT;
		goto out;
	}

	PE50_INFO("run once(%d)\n", data->run_once);
	if (data->run_once) {
		if (!(data->notify & PE50_RESET_NOTIFY) && !(data->notify & BIT(EVT_WLS_FULL))) {
			PE50_INFO("run once notify(0x%x)\n", data->notify);
			ret = ALG_NOT_READY;
			goto out;
		}
		mutex_lock(&data->notify_lock);
		PE50_INFO("run once but detach/hardreset happened\n");
		data->notify &= ~PE50_RESET_NOTIFY;
		data->run_once = false;
		data->ta_ready = false;
		mutex_unlock(&data->notify_lock);
	}

	ret = pe50_is_ta_rdy(info);
	if (ret != ALG_READY) {
		goto out;
	}

	pe50_hal_init_adc(info->alg, to_chgidx(PE50_DVCHG_MASTER), true);
	ret = pe50_get_adc(info, PE50_ADCCHAN_VBAT, &vbat);
	if (ret < 0 || vbat == 0) {
		PE50_DBG("get vbat fail(%d, %d)\n", ret, vbat);
		pe50_hal_dump_register(info->alg, DVCHG1);
		ret = ALG_NOT_READY;
		goto out;
	}

	if (vbat < desc->start_vbat_min || vbat > desc->start_vbat_max) {
		PE50_INFO("vbat(%d) not in range(%d~%d)\n", vbat,
			desc->start_vbat_min, desc->start_vbat_max);
		ret = ALG_NOT_READY;
		goto out;
	}

	ret = ALG_READY;

out:
	if (ret == ALG_TA_NOT_SUPPORT)
		pe50_hal_init_adc(info->alg, to_chgidx(PE50_DVCHG_MASTER), false);

	mutex_unlock(&data->lock);
	return ret;
}

static int pe50_start_algo(struct tchg_alg_device *alg)
{
	int ret = 0;
	struct pe50_algo_info *info = tchg_alg_dev_get_drvdata(alg);
	struct pe50_algo_data *data = info->data;

	if (pe50_is_algo_running(alg))
		return ALG_RUNNING;

	mutex_lock(&data->lock);
	PE50_DBG("++\n");
	if (!data->inited || !data->ta_ready) {
		ret = ALG_INIT_FAIL;
		goto out;
	}

	ret = pe50_start(info);
	if (ret < 0) {
		PE50_ERR("start PE5.0 algo fail\n");
		ret = ALG_INIT_FAIL;
	}
out:
	mutex_unlock(&data->lock);
	return ret;
}

static int pe50_plugout_reset(struct tchg_alg_device *alg)
{
	int ret = 0;
	struct pe50_algo_info *info = tchg_alg_dev_get_drvdata(alg);
	struct pe50_algo_data *data = info->data;
	struct pe50_stop_info sinfo = {
		.reset_ta = false,
		.hardreset_ta = false,
	};

	atomic_set(&data->stop_algo, 1);
	mutex_lock(&data->lock);
	PE50_ERR("++\n");
	if (!data->inited)
		goto out;

	__pe50_plugout_reset(info, &sinfo);
	pe50_hal_enable_sw_vbusovp(info->alg,true);

	ret = pe50_hal_enable_hz(info->alg, false);
	if (ret < 0) {
		PE50_ERR("set swchg hz fail(%d)\n", ret);
		goto out;
	}

out:
	mutex_unlock(&data->lock);
	return ret;
}

static int pe50_stop_algo(struct tchg_alg_device *alg)
{
	int ret = 0;
	struct pe50_algo_info *info = tchg_alg_dev_get_drvdata(alg);
	struct pe50_algo_data *data = info->data;
	struct pe50_stop_info sinfo = {
		.reset_ta = true,
		.hardreset_ta = false,
	};

	atomic_set(&data->stop_algo, 1);
	mutex_lock(&data->lock);
	if (!data->inited)
		goto out;
	ret = pe50_stop(info, &sinfo);
	data->run_once = false;
out:
	mutex_unlock(&data->lock);
	return ret;
}

static int pe50_notifier_call(struct tchg_alg_device *alg,
			      struct tchg_alg_notify *notify)
{
	int ret = 0;
	struct pe50_algo_info *info = tchg_alg_dev_get_drvdata(alg);
	struct pe50_algo_data *data = info->data;

	mutex_lock(&data->notify_lock);
	if (notify->evt == EVT_FULL) {
		PE50_INFO("battery full, reset related param\n");
		data->pe50_taper_done = false;
	}
	if (data->state == PE50_ALGO_STOP) {
		if ((notify->evt == EVT_DETACH ||
		     notify->evt == EVT_HARDRESET) && data->run_once) {
			PE50_INFO("detach/hardreset && run once after stop\n");
			data->notify |= BIT(notify->evt);
		}

		if(notify->evt != EVT_WLS_FULL)
			goto out;
	}
	PE50_INFO("%s\n", tchg_alg_notify_evt_tostring(notify->evt));
	switch (notify->evt) {
	case EVT_DETACH:
	case EVT_HARDRESET:
	case EVT_VBUSOVP:
	case EVT_IBUSOCP:
	case EVT_IBUSUCP_FALL:
	case EVT_VBATOVP:
	case EVT_IBATOCP:
	case EVT_VOUTOVP:
	case EVT_VDROVP:
	case EVT_VBATOVP_ALARM:
	case EVT_VBUSOVP_ALARM:
	case EVT_WLS_FULL:
		data->notify |= BIT(notify->evt);
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	pe50_wakeup_algo_thread(data);
out:
	mutex_unlock(&data->notify_lock);
	return ret;
}

static int pe50_set_current_limit(struct tchg_alg_device *alg,
				  struct tchg_limit_setting *setting)
{
	struct pe50_algo_info *info = tchg_alg_dev_get_drvdata(alg);
	struct pe50_algo_data *data = info->data;
	int cv = micro_to_milli(setting->cv);
	int ic = micro_to_milli(setting->input_current_limit_dvchg1);

	mutex_lock(&data->ext_lock);
	if (data->cv_limit != cv || data->input_current_limit != ic) {
		data->cv_limit = cv;
		data->input_current_limit = ic;
		PE50_INFO("ic = %d, cv = %d\n", ic, cv);
		pe50_wakeup_algo_thread(data);
	}
	mutex_unlock(&data->ext_lock);
	return 0;
}

void pe50_set_muiti_chg_algo_data(struct pe50_algo_info *info)
{
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	int chg_speed_temp = data->chg_speed;

	if(!desc->supprot_multi_level_charging)
		return;

	if(IS_ERR_OR_NULL(info->alg)){
		PE50_INFO("%s: info alg is null\n", __func__);
		return;
	}

	data->adapter_name = pe50_hal_get_adapter_name(info->alg);

	if((data->ta_auth_data[data->running_spec].pdp < desc->project_power) && data->ta_ready) {
		PE50_ERR("%s:adapter_capacity < project_power,chg_speed use default:TRAN_MULTI_SPEED_MID\n", __func__);
		chg_speed_temp = TRAN_MULTI_SPEED_MID;
	}

	mutex_lock(&data->chgspeed_lock);
	memcpy(data->tbat_level_def, desc->tc_pe50_multi_level[chg_speed_temp-1].tbat_level_def,
			sizeof(data->tbat_level_def));
	memcpy(data->tpcb_level_def, desc->tc_pe50_multi_level[chg_speed_temp-1].tpcb_level_def,
			sizeof(data->tpcb_level_def));
	memcpy(data->tpa_level_def, desc->tc_pe50_multi_level[chg_speed_temp-1].tpa_level_def,
			sizeof(data->tpa_level_def));
	memcpy(data->tbat_curlmt, desc->tc_pe50_multi_level[chg_speed_temp-1].tbat_curlmt,
			sizeof(data->tbat_curlmt));
	memcpy(data->tpcb_curlmt, desc->tc_pe50_multi_level[chg_speed_temp-1].tpcb_curlmt,
			sizeof(data->tpcb_curlmt));
	memcpy(data->tpa_curlmt, desc->tc_pe50_multi_level[chg_speed_temp-1].tpa_curlmt,
			sizeof(data->tpa_curlmt));
	mutex_unlock(&data->chgspeed_lock);

	PE50_INFO("%s:enter\n", __func__);
}

static void pe50_multi_chg_init(struct pe50_algo_info *info)
{
	struct pe50_algo_data *data = info->data;

	data->speed_owner = TRAN_MULTI_OWNER_SYS;
	data->chg_speed = TRAN_MULTI_SPEED_MID;
}

static void pe50_set_cycle_level_ratio(struct pe50_algo_info *info, int value)
{
	struct pe50_algo_desc *desc = info->desc;
	int i, j;

	for (i = 0; i < desc->step_ffc_level; i++) {
		for (j = 0; j < PE50_CYCLE_CNT_MAX; j++) {
			if (desc->step_ffc[i].cycle_cnt[j] <= 0)
				continue;
			PE50_INFO("%s: pre_cycle_cnt:%d, post_cycle_cnt:%d, value:%d\n", __func__,
				desc->step_ffc[i].cycle_cnt[j], desc->step_ffc[i].cycle_cnt[j] / value, value);
			desc->step_ffc[i].cycle_cnt[j] = desc->step_ffc[i].cycle_cnt[j] / value;
		}
	}
}

int pe50_get_prop(struct tchg_alg_device *alg,
		enum tchg_alg_props s, int *value)
{
	struct pe50_algo_info *info = tchg_alg_dev_get_drvdata(alg);
	struct pe50_algo_data *data = info->data;
	struct pe50_algo_desc *desc = info->desc;
	struct pe50_ta_status status;
	int ret = 0;

	switch (s) {
	case ALG_CHG_STATUS:
		*value = pe50_is_algo_running(alg) ? 1 : 0;
		break;
	case ALG_TAPER_DONE:
		*value = data->pe50_taper_done ? 1 : 0;
		break;
	case ALG_IS_FAST_CHR:
		*value = data->ta_ready ? 1 : 0;
		break;
	case ALG_ADAPTER_CAPACITY:
		if (data->ta_auth_data[data->running_spec].pdp > U8_TO_INT_MAX)
			*value = U8_TO_INT_ERR;
		else{
			*value = (int)data->ta_auth_data[data->running_spec].pdp;
		}
		break;
	case ALG_TA_TEMP:
		ret = pe50_hal_get_ta_status(info->alg, &status);
		if (ret < 0) {
			PE50_ERR("get ta status fail(%d)\n", ret);
			*value = -1;
			break;
		}
		*value = status.temperature;
		break;
	case ALG_IS_FFC_CHG:
		if (pe50_is_algo_running(alg) || data->pe50_taper_done)
			*value = 1;
		else
			*value = 0;
		break;
	case ALG_GET_FFC_CV:
		if(data->bat_ffc_cv > U32_TO_INT_MAX)
			*value = U32_TO_INT_ERR;
		else
			*value = (int)data->bat_ffc_cv;
		break;
	case ALG_GET_FFC_EOC:
		if(data->bat_ffc_eoc > U32_TO_INT_MAX)
			*value = U32_TO_INT_ERR;
		else
			*value = (int)data->bat_ffc_eoc;
		break;
	case ALG_GET_LONG_LIFE_RECHG_CV_GAP:
		if(data->long_life_rechg_cv_gap > U32_TO_INT_MAX)
			*value = U32_TO_INT_ERR;
		else
			*value = (int)data->long_life_rechg_cv_gap;
		break;
	case ALG_GET_LONG_LIFE_RECHG_CUR:
		if(data->long_life_rechg_cur > U32_TO_INT_MAX)
			*value = U32_TO_INT_ERR;
		else
			*value = (int)data->long_life_rechg_cur;
		break;
	case ALG_NUM:
		*value = data->algo_num;
		break;
	case ALG_MULTI_CHG_SPEED:
		*value = data->chg_speed;
		break;
	/* dual battery */
	case ALG_GET_MASTER_FFC_CV:
		if(data->bat_master_ffc_cv > U32_TO_INT_MAX)
			*value = U32_TO_INT_ERR;
		else		
			*value = (int)data->bat_master_ffc_cv;
		break;
	case ALG_GET_MASTER_FFC_EOC:
		if(data->bat_master_ffc_eoc > U32_TO_INT_MAX)
			*value = U32_TO_INT_ERR;
		else
			*value = (int)data->bat_master_ffc_eoc;
		break;
	case ALG_GET_SLAVE_FFC_CV:
		if(data->bat_slave_ffc_cv > U32_TO_INT_MAX)
			*value = U32_TO_INT_ERR;
		else
			*value = (int)data->bat_slave_ffc_cv;
		break;
	case ALG_GET_SLAVE_FFC_EOC:
		if(data->bat_slave_ffc_eoc > U32_TO_INT_MAX)
			*value = U32_TO_INT_ERR;
		else
			*value = (int)data->bat_slave_ffc_eoc;
		break;
	case ALG_MASTER_STEP_CC:
		if(data->vbat_master_step_cc > U32_TO_INT_MAX)
			*value = U32_TO_INT_ERR;
		else
			*value = (int)data->vbat_master_step_cc;
		break;
	case ALG_PROJECT_POWER:
		if(desc->project_power > U32_TO_INT_MAX)
			*value = U32_TO_INT_ERR;
		else
			*value = (int)desc->project_power;
		break;
	default:
		break;
	}

	return 0;
}

int pe50_set_prop(struct tchg_alg_device *alg,
		enum tchg_alg_props s, int value)
{
	struct pe50_algo_info *info = tchg_alg_dev_get_drvdata(alg);
	struct pe50_algo_data *data = info->data;

	PE50_INFO("%s %d %d\n", __func__, s, value);

	switch (s) {
	case ALG_LOG_LEVEL:
		log_level = value;
		break;
	case ALG_REF_VBAT:
		data->ref_vbat = value;
		break;
	case ALG_AUTO_TEST_IRQ:
		data->pe50_auto_test_ibusucp = value;
		break;
	case ALG_MULTI_CHG_SPEED:
		data->chg_speed = value;
		pe50_set_muiti_chg_algo_data(info);
		PE50_INFO("%s data->chg_speed:%d\n", __func__, data->chg_speed);
		break;
	case ALG_MULTI_CHG_SPEED_OWNER:
		data->speed_owner = value;
		PE50_INFO("%s data->speed_owner:%d\n", __func__, data->speed_owner);
		break;
	case ALG_SET_CYCLE_RATIO:
		pe50_set_cycle_level_ratio(info, value);
		break;
	default:
		break;
	}

	return 0;
}

static struct tchg_alg_ops pe50_ops = {
	.init_algo = pe50_init_algo,
	.is_algo_ready = pe50_is_algo_ready,
	.is_ta_ready = pe50_is_ta_ready,
	.start_algo = pe50_start_algo,
	.is_algo_running = pe50_is_algo_running,
	.plugout_reset = pe50_plugout_reset,
	.stop_algo = pe50_stop_algo,
	.notifier_call = pe50_notifier_call,
	.set_current_limit = pe50_set_current_limit,
	.set_prop = pe50_set_prop,
	.get_prop = pe50_get_prop,
};

#define PE50_DT_VALPROP_ARR(name, sz) \
	{#name, offsetof(struct pe50_algo_desc, name), sz}

#define PE50_DT_VALPROP(name) \
	PE50_DT_VALPROP_ARR(name, 1)

struct pe50_dtprop {
	const char *name;
	size_t offset;
	size_t sz;
};

static inline void pe50_parse_dt_bool(struct device_node *np, void *desc,
				    const struct pe50_dtprop *props,
				    int prop_cnt)
{
	int i;

	for (i = 0; i < prop_cnt; i++) {
		if (unlikely(!props[i].name))
			continue;
		*(bool *)(desc + props[i].offset) = of_property_read_bool(np, props[i].name);
	}
}

static inline void pe50_parse_dt_u32(struct device_node *np, void *desc,
				     const struct pe50_dtprop *props,
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

static inline void pe50_parse_dt_s32(struct device_node *np, void *desc,
				    const struct pe50_dtprop *props,
				    int prop_cnt)
{
	int i;

	for (i = 0; i < prop_cnt; i++) {
		if (unlikely(!props[i].name))
			continue;
		__of_property_read_s32(np, props[i].name, desc + props[i].offset);
	}
}

static inline void pe50_parse_dt_u32_arr(struct device_node *np, void *desc,
					 const struct pe50_dtprop *props,
					 int prop_cnt)
{
	int i;

	for (i = 0; i < prop_cnt; i++) {
		if (unlikely(!props[i].name))
			continue;
		of_property_read_u32_array(np, props[i].name,
					   desc + props[i].offset, props[i].sz);
	}
}

static inline int __of_property_read_s32_array(const struct device_node *np,
					       const char *propname,
					       s32 *out_values, size_t sz)
{
	return of_property_read_u32_array(np, propname, (u32 *)out_values, sz);
}

static inline void pe50_parse_dt_s32_arr(struct device_node *np, void *desc,
					 const struct pe50_dtprop *props,
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

static const struct pe50_dtprop pe50_dtprops_bool[] = {
	PE50_DT_VALPROP(support_dual_battery),
	PE50_DT_VALPROP(support_switch_spec),
};

static const struct pe50_dtprop pe50_dtprops_u32[] = {
	PE50_DT_VALPROP(polling_interval),
	PE50_DT_VALPROP(ta_cv_ss_repeat_tmin),
	PE50_DT_VALPROP(vbat_cv),
	PE50_DT_VALPROP(start_vbat_min),
	PE50_DT_VALPROP(start_vbat_max),
	PE50_DT_VALPROP(idvchg_term),
	PE50_DT_VALPROP(idvchg_ss_init),
	PE50_DT_VALPROP(idvchg_ss_step),
	PE50_DT_VALPROP(idvchg_ss_step1),
	PE50_DT_VALPROP(idvchg_ss_step2),
	PE50_DT_VALPROP(idvchg_ss_step1_vbat),
	PE50_DT_VALPROP(idvchg_ss_step2_vbat),
	PE50_DT_VALPROP(ta_blanking),
	PE50_DT_VALPROP(chg_time_max),
	PE50_DT_VALPROP(tta_recovery_area),
	PE50_DT_VALPROP(tbat_recovery_area),
	PE50_DT_VALPROP(tpa_recovery_area),
	PE50_DT_VALPROP(sys_power_recovery_area),
	PE50_DT_VALPROP(tdvchg_recovery_area),
	PE50_DT_VALPROP(tpcb_recovery_area),
	PE50_DT_VALPROP(ifod_threshold),
	PE50_DT_VALPROP(rsw_min),
	PE50_DT_VALPROP(ircmp_rbat),
	PE50_DT_VALPROP(ircmp_vclamp),
	PE50_DT_VALPROP(project_power),
	PE50_DT_VALPROP(project_pwr_ratio),
	PE50_DT_VALPROP(min_ita_gap),
	PE50_DT_VALPROP(ita_lmt_gap),
	PE50_DT_VALPROP(idvchg_pps_term),
	PE50_DT_VALPROP(pe50_vta_init),
	PE50_DT_VALPROP(pe50_ita_init),
	PE50_DT_VALPROP(vol_diff_gap),
	PE50_DT_VALPROP(default_spec),
	PE50_DT_VALPROP(pe50_default_vta_step),
};

static const struct pe50_dtprop pe50_dtprops_s32[] = {
	PE50_DT_VALPROP(rechg_tbat_high),
	PE50_DT_VALPROP(rechg_tbat_low),
};

static const struct pe50_dtprop pe50_dtprops_u32_array[] = {
	PE50_DT_VALPROP_ARR(ita_level, PE50_RCABLE_MAX),
	PE50_DT_VALPROP_ARR(rcable_level, PE50_RCABLE_MAX),
	PE50_DT_VALPROP_ARR(support_spec, SUPPORT_SPEC_MAX),
	PE50_DT_VALPROP_ARR(vta_cap_min, SUPPORT_SPEC_MAX),
	PE50_DT_VALPROP_ARR(vta_cap_max, SUPPORT_SPEC_MAX),
	PE50_DT_VALPROP_ARR(ita_cap_min, SUPPORT_SPEC_MAX),
	PE50_DT_VALPROP_ARR(vbus_max_ovp, SUPPORT_SPEC_MAX),
	PE50_DT_VALPROP_ARR(run_cp_cur, PE50_DVCHG_MAX),
	PE50_DT_VALPROP_ARR(run_cp_recovery_gap, PE50_DVCHG_MAX),
	PE50_DT_VALPROP_ARR(idvchg_level_multi, PE50_DVCHG_MAX),
	PE50_DT_VALPROP_ARR(pe5_ffc_gap, PE50_VBAT_FFC_MAX),
	PE50_DT_VALPROP_ARR(cable_capability_level, PE50_CABLE_CAPABILTY_MAX),
	PE50_DT_VALPROP_ARR(vol_diff_level, PE50_CABLE_CAPABILTY_MAX),
};

static const struct pe50_dtprop pe50_dtprops_s32_array[] = {
	PE50_DT_VALPROP_ARR(tta_level_def, PE50_THERMAL_MAX),
	PE50_DT_VALPROP_ARR(tta_curlmt, PE50_THERMAL_MAX),
	PE50_DT_VALPROP_ARR(tbat_level_def, PE50_THERMAL_MAX),
	PE50_DT_VALPROP_ARR(tbat_curlmt, PE50_THERMAL_MAX),
	PE50_DT_VALPROP_ARR(tpcb_level_def, PE50_THERMAL_MAX),
	PE50_DT_VALPROP_ARR(tpcb_curlmt, PE50_THERMAL_MAX),
	PE50_DT_VALPROP_ARR(tpa_level_def, PE50_THERMAL_MAX),
	PE50_DT_VALPROP_ARR(tpa_curlmt, PE50_THERMAL_MAX),
	PE50_DT_VALPROP_ARR(sys_power_level_def, PE50_THERMAL_MAX),
	PE50_DT_VALPROP_ARR(sys_power_curlmt, PE50_THERMAL_MAX),
	PE50_DT_VALPROP_ARR(tdvchg_level_def, PE50_THERMAL_MAX),
	PE50_DT_VALPROP_ARR(tdvchg_curlmt, PE50_THERMAL_MAX),
};

static int pe50_parse_dual_batt_ffc_dt(struct pe50_algo_info *info)
{
	struct pe50_algo_desc *desc = info->desc;
	struct device_node *np = info->dev->of_node;
	int value = 0;
	char buf[30] = {0};
	int length;
	int rc;
	int i, j;

	rc = of_property_read_u32(np, "step_cc_gap", &value);
	if (rc < 0) {
		desc->step_cc_gap = 100;
		pr_err("%s: step_cc_gap property missing, use default val: %d\n",
			__func__, desc->step_cc_gap);
	} else {
		desc->step_cc_gap = value;
		pr_info("%s: dts config desc->step_cc_gap: %d\n",
			__func__, desc->step_cc_gap);
	}

	/* master battery ffc param */
	rc = of_property_read_u32(np, "step_master_ffc_level", &value);
	if (rc < 0) {
		PE50_ERR("%s: step_master_ffc_level property missing\n",
			__func__);
		return -EINVAL;
	} else {
		desc->step_master_ffc_level = value;
		PE50_INFO("%s: dts config desc->step_master_ffc_level: %d\n",
			__func__, desc->step_master_ffc_level);
	}

	desc->step_master_ffc = devm_kzalloc(info->dev,
		sizeof(step_ffc_t) * desc->step_master_ffc_level, GFP_KERNEL);
	if (!desc->step_master_ffc) {
		PE50_ERR("%s: cann't malloc, exit...\n", __func__);
		return -ENOMEM;
	}

	for (i = 0; i < desc->step_master_ffc_level; i++) {
		sprintf(buf, "step_master_ffc_level_%d", i);
		length = of_property_count_elems_of_size(np, buf, sizeof(u32));
		rc = __of_property_read_s32_array(np, buf,
			(u32 *)(&desc->step_master_ffc[i]), length);
		if (rc < 0) {
			PE50_ERR("%s: %s property missing, use default config\n",
				__func__, buf);
		}

		for (j = 0; j < PE50_TEMP_MAX; j++)
			PE50_DBG("%s: desc->step_master_ffc[%d]->ffc_temp[%d] = [%d]", __func__, i, j,desc->step_master_ffc[i].ffc_temp[j]);
		for (j = 0; j < PE50_VBAT_FFC_MAX; j++)
			PE50_DBG("%s: %s desc->step_master_ffc[%d]->ffc_vbat[%d] = [%d],"
				"desc->step_master_ffc[%d]->ffc_ibat[%d] = [%d],"
				"desc->step_master_ffc[%d]->ffc_term[%d] = %d\n",
				__func__, buf,
				i, j, desc->step_master_ffc[i].ffc_vbat[j],
				i, j, desc->step_master_ffc[i].ffc_ibat[j],
				i, j, desc->step_master_ffc[i].ffc_term);	
		for (j = 0; j < PE50_CYCLE_CNT_MAX; j++)
			PE50_DBG("%s: desc->step_master_ffc[%d]->cycle_cnt[%d] = [%d], rechg_gap = %d",
				__func__, i, j,desc->step_master_ffc[i].cycle_cnt[j], desc->step_master_ffc[i].rechg_gap);
	}

	/* slave battery ffc param */
	rc = of_property_read_u32(np, "step_slave_ffc_level", &value);
	if (rc < 0) {
		PE50_ERR("%s: step_slave_ffc_level property missing\n",
			__func__);
		return -EINVAL;
	} else {
		desc->step_slave_ffc_level = value;
		PE50_INFO("%s: dts config desc->step_slave_ffc_level: %d\n",
			__func__, desc->step_slave_ffc_level);
	}

	desc->step_slave_ffc = devm_kzalloc(info->dev,
		sizeof(step_ffc_t) * desc->step_slave_ffc_level, GFP_KERNEL);
	if (!desc->step_slave_ffc) {
		PE50_ERR("%s: cann't malloc, exit...\n", __func__);
		return -ENOMEM;
	}

	for (i = 0; i < desc->step_slave_ffc_level; i++) {
		sprintf(buf, "step_slave_ffc_level_%d", i);
		length = of_property_count_elems_of_size(np, buf, sizeof(u32));
		rc = __of_property_read_s32_array(np, buf,
			(u32 *)(&desc->step_slave_ffc[i]), length);
		if (rc < 0) {
			PE50_ERR("%s: %s property missing, use default config\n",
				__func__, buf);
		}

		for (j = 0; j < PE50_TEMP_MAX; j++)
			PE50_DBG("%s: desc->step_slave_ffc[%d]->ffc_temp[%d] = [%d]", __func__, i, j,desc->step_slave_ffc[i].ffc_temp[j]);
		for (j = 0; j < PE50_VBAT_FFC_MAX; j++)
			PE50_DBG("%s: %s desc->step_slave_ffc[%d]->ffc_vbat[%d] = [%d],"
				"desc->step_slave_ffc[%d]->ffc_ibat[%d] = [%d],"
				"desc->step_slave_ffc[%d]->ffc_term[%d] = %d\n",
				__func__, buf,
				i, j, desc->step_slave_ffc[i].ffc_vbat[j],
				i, j, desc->step_slave_ffc[i].ffc_ibat[j],
				i, j, desc->step_slave_ffc[i].ffc_term);	
		for (j = 0; j < PE50_CYCLE_CNT_MAX; j++)
			PE50_DBG("%s: desc->step_slave_ffc[%d]->cycle_cnt[%d] = [%d], rechg_gap = %d",
				__func__, i, j,desc->step_slave_ffc[i].cycle_cnt[j], desc->step_slave_ffc[i].rechg_gap);
	}

	return 0;
}

static int pe50_parse_vbat_ffc_dt(struct pe50_algo_info *info)
{
	struct pe50_algo_desc *desc = info->desc;
	struct device_node *np = info->dev->of_node;
	int value = 0;
	char buf[30] = {0};
	int length;
	int rc;
	int i, j;

	rc = of_property_read_u32(np, "step_ffc_level", &value);
	if (rc < 0) {
		PE50_ERR("%s: step_ffc_level property missing\n",
			__func__);
		return -EINVAL;
	} else {
		desc->step_ffc_level = value;
		PE50_INFO("%s: dts config desc->step_ffc_level: %d\n",
			__func__, desc->step_ffc_level);
	}

	desc->step_ffc = devm_kzalloc(info->dev,
		sizeof(step_ffc_t) * desc->step_ffc_level, GFP_KERNEL);
	if (!desc->step_ffc) {
		PE50_ERR("%s: cann't malloc, exit...\n", __func__);
		return -ENOMEM;
	}

	for (i = 0; i < desc->step_ffc_level; i++) {
		sprintf(buf, "step_ffc_level_%d", i);
		length = of_property_count_elems_of_size(np, buf, sizeof(u32));
		rc = __of_property_read_s32_array(np, buf,
			(u32 *)(&desc->step_ffc[i]), length);
		if (rc < 0) {
			PE50_ERR("%s: %s property missing, use default config\n",
				__func__, buf);
		}

		for (j = 0; j < PE50_TEMP_MAX; j++)
			PE50_DBG("%s: desc->step_ffc[%d]->ffc_temp[%d] = [%d]", __func__, i, j,desc->step_ffc[i].ffc_temp[j]);
		for (j = 0; j < PE50_VBAT_FFC_MAX; j++)
			PE50_DBG("%s: %s desc->step_ffc[%d]->ffc_vbat[%d] = [%d],"
				"desc->step_ffc[%d]->ffc_ibat[%d] = [%d],"
				"desc->step_ffc[%d]->ffc_term[%d] = %d\n",
				__func__, buf,
				i, j, desc->step_ffc[i].ffc_vbat[j],
				i, j, desc->step_ffc[i].ffc_ibat[j],
				i, j, desc->step_ffc[i].ffc_term);	
		for (j = 0; j < PE50_CYCLE_CNT_MAX; j++)
			PE50_DBG("%s: desc->step_ffc[%d]->cycle_cnt[%d] = [%d], rechg_gap = %d, rechg_cur = %d",
				__func__, i, j,desc->step_ffc[i].cycle_cnt[j], desc->step_ffc[i].rechg_gap, desc->step_ffc[i].rechg_cur);
	}

	return 0;
}

static void pe50_parse_multi_chg_dt(struct pe50_algo_info *info)
{
	struct pe50_algo_desc *desc = info->desc;
	struct device_node *np = info->dev->of_node;
	int i,ret;
	char buf[30] = {0};

	desc->supprot_multi_level_charging =
		of_property_read_bool(np, "supprot_multi_level_charging");
	for (i = 0; i < TRAN_MULTI_SPEED_MAX-1; i++) {
		sprintf(buf, "tbat_level_def_%d", i);
		ret = __of_property_read_s32_array(np, buf,
				desc->tc_pe50_multi_level[i].tbat_level_def, PE50_THERMAL_MAX);
		if (ret != 0)
			PE50_ERR("Parse multi tbat level[%d] define array failed, ret = %d\n", i, ret);

		sprintf(buf, "tbat_curlmt_%d", i);
		ret = __of_property_read_s32_array(np, buf,
				desc->tc_pe50_multi_level[i].tbat_curlmt, PE50_THERMAL_MAX);
		if (ret != 0)
			PE50_ERR("Parse multi tbat curlmt level[%d] define array failed, ret = %d\n", i, ret);

		sprintf(buf, "tpcb_level_def_%d", i);
		ret = __of_property_read_s32_array(np, buf,
				desc->tc_pe50_multi_level[i].tpcb_level_def, PE50_THERMAL_MAX);
		if (ret != 0)
			PE50_ERR("Parse multi tpcb level[%d] define array failed, ret = %d\n", i, ret);

		sprintf(buf, "tpcb_curlmt_%d", i);
		ret = __of_property_read_s32_array(np, buf,
				desc->tc_pe50_multi_level[i].tpcb_curlmt, PE50_THERMAL_MAX);
		if (ret != 0)
			PE50_ERR("Parse multi tpcb curlmt level[%d] define array failed, ret = %d\n", i, ret);

		sprintf(buf, "tpa_level_def_%d", i);
		ret = __of_property_read_s32_array(np, buf,
				desc->tc_pe50_multi_level[i].tpa_level_def, PE50_THERMAL_MAX);
		if (ret != 0)
			PE50_ERR("Parse multi tpa level[%d] define array failed, ret = %d\n", i, ret);

		sprintf(buf, "tpa_curlmt_%d", i);
		ret = __of_property_read_s32_array(np, buf,
				desc->tc_pe50_multi_level[i].tpa_curlmt, PE50_THERMAL_MAX);
		if (ret != 0)
			PE50_ERR("Parse multi tpa curlmt level[%d] define array failed, ret = %d\n", i, ret);
	}
}

static int pe50_parse_dt(struct pe50_algo_info *info)
{
	int i, ret;
	struct pe50_algo_desc *desc;
	struct pe50_algo_data *data;
	struct device_node *np = info->dev->of_node;

	desc = devm_kzalloc(info->dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	info->desc = desc;
	data = info->data;
	memcpy(desc, &algo_desc_defval, sizeof(*desc));

	ret = of_property_count_strings(np, "support_ta");
	if (ret < 0)
		return ret;
	desc->support_ta_cnt = ret;
	desc->support_ta = devm_kzalloc(info->dev, ret * sizeof(char *),
					GFP_KERNEL);
	if (!desc->support_ta)
		return -ENOMEM;
	for (i = 0; i < desc->support_ta_cnt; i++) {
		ret = of_property_read_string_index(np, "support_ta", i,
						    &desc->support_ta[i]);
		if (ret < 0)
			return ret;
		PE50_INFO("support ta(%s)\n", desc->support_ta[i]);
	}

	desc->allow_not_check_ta_status =
		of_property_read_bool(np, "allow_not_check_ta_status");

	pe50_parse_dt_bool(np, (void *)desc, pe50_dtprops_bool,
			     ARRAY_SIZE(pe50_dtprops_bool));
	pe50_parse_dt_u32(np, (void *)desc, pe50_dtprops_u32,
			  ARRAY_SIZE(pe50_dtprops_u32));
	pe50_parse_dt_s32(np, (void *)desc, pe50_dtprops_s32,
			  ARRAY_SIZE(pe50_dtprops_s32));
	pe50_parse_dt_u32_arr(np, (void *)desc, pe50_dtprops_u32_array,
			      ARRAY_SIZE(pe50_dtprops_u32_array));
	pe50_parse_dt_s32_arr(np, (void *)desc, pe50_dtprops_s32_array,
			      ARRAY_SIZE(pe50_dtprops_s32_array));

	pe50_parse_multi_chg_dt(info);

	if (desc->support_dual_battery)
		ret = pe50_parse_dual_batt_ffc_dt(info);
	else
		ret = pe50_parse_vbat_ffc_dt(info);
	if (ret < 0) {
		PE50_ERR("parse %s fail, ret:%d\n", __func__, ret);
		return ret;
	}

	return 0;
}

static ssize_t show_pe50_thermal(struct device *dev,struct device_attribute *attr,char *buf)
{
	struct pe50_algo_info *info=dev->driver_data;
	struct pe50_algo_desc *desc = info->desc;
	struct pe50_algo_data *data = info->data;
	int i, len = 0;
	char str[200] = {0};

	switch (data->debug_code) {
		case TTA_LEVEL:
			strcat(str, "tta level: ");
			for (i = PE50_THERMAL_WARM; i < PE50_THERMAL_VERY_HOT; i++) {
				sprintf(buf, "%d, ", desc->tta_level_def[i]);
				strcat(str, buf);
			}
			strcat(str, "\n \rtta limit: ");
			for (i = PE50_THERMAL_WARM; i < PE50_THERMAL_VERY_HOT; i++) {
				sprintf(buf, "%d, ", desc->tta_curlmt[i]);
				strcat(str, buf);
			}
			strcat(str, "\n");
			len = sprintf(buf, "%s", str);
			PE50_INFO("%s", str);
			break;
		case TDVCHG_LEVEL:
			strcat(str, "tdvchg level: ");
			for (i = PE50_THERMAL_WARM; i < PE50_THERMAL_VERY_HOT; i++) {
				sprintf(buf, "%d, ", desc->tdvchg_level_def[i]);
				strcat(str, buf);
			}
			strcat(str, "\n \rtdvchg limit: ");
			for (i = PE50_THERMAL_WARM; i < PE50_THERMAL_VERY_HOT; i++) {
				sprintf(buf, "%d, ", desc->tdvchg_curlmt[i]);
				strcat(str, buf);
			}
			strcat(str, "\n");
			len = sprintf(buf, "%s", str);
			PE50_INFO("%s", str);
			break;
		case TBAT_LEVEL:
			strcat(str, "tbat level: ");
			for (i = PE50_THERMAL_WARM; i < PE50_THERMAL_VERY_HOT; i++) {
				if(desc->supprot_multi_level_charging)
					sprintf(buf, "%d, ",desc->tc_pe50_multi_level[TRAN_MULTI_SPEED_MID].tbat_level_def[i]);
				else
					sprintf(buf, "%d, ", desc->tbat_level_def[i]);

				strcat(str, buf);
			}
			strcat(str, "\n \rtbat limit: ");
			for (i = PE50_THERMAL_WARM; i < PE50_THERMAL_VERY_HOT; i++) {
				if(desc->supprot_multi_level_charging)
					sprintf(buf, "%d, ", desc->tc_pe50_multi_level[TRAN_MULTI_SPEED_MID].tbat_curlmt[i]);
				else
					sprintf(buf, "%d, ", desc->tbat_curlmt[i]);

				strcat(str, buf);
			}
			strcat(str, "\n");
			len = sprintf(buf, "%s", str);
			PE50_INFO("%s", str);
			break;
		case TPCB_LEVEL:
			strcat(str, "tpcb level: ");
			for (i = PE50_THERMAL_WARM; i < PE50_THERMAL_VERY_HOT; i++) {
				if(desc->supprot_multi_level_charging)
					sprintf(buf, "%d, ", desc->tc_pe50_multi_level[TRAN_MULTI_SPEED_MID].tpcb_level_def[i]);
				else
					sprintf(buf, "%d, ", desc->tpcb_level_def[i]);

				strcat(str, buf);
			}
			strcat(str, "\n \rtpcb limit: ");
			for (i = PE50_THERMAL_WARM; i < PE50_THERMAL_VERY_HOT; i++) {
				if(desc->supprot_multi_level_charging)
					sprintf(buf, "%d, ", desc->tc_pe50_multi_level[TRAN_MULTI_SPEED_MID].tpcb_curlmt[i]);
				else
					sprintf(buf, "%d, ", desc->tpcb_curlmt[i]);

				strcat(str, buf);
			}
			strcat(str, "\n");
			len = sprintf(buf, "%s", str);
			PE50_INFO("%s", str);
			break;
		case TPA_LEVEL:
			strcat(str, "tpa level: ");
			for (i = PE50_THERMAL_WARM; i < PE50_THERMAL_VERY_HOT; i++) {
				if(desc->supprot_multi_level_charging)
					sprintf(buf, "%d, ", desc->tc_pe50_multi_level[TRAN_MULTI_SPEED_MID].tpa_level_def[i]);
				else
					sprintf(buf, "%d, ", desc->tpa_level_def[i]);

				strcat(str, buf);
			}
			strcat(str, "\n \rtpa limit: ");
			for (i = PE50_THERMAL_WARM; i < PE50_THERMAL_VERY_HOT; i++) {
				if(desc->supprot_multi_level_charging)
					sprintf(buf, "%d, ", desc->tc_pe50_multi_level[TRAN_MULTI_SPEED_MID].tpa_curlmt[i]);
				else
					sprintf(buf, "%d, ", desc->tpa_curlmt[i]);

				strcat(str, buf);
			}
			strcat(str, "\n");
			len = sprintf(buf, "%s", str);
			PE50_INFO("%s", str);
			break;
		case SYS_POWER_LEVEL:
			strcat(str, "sys_power level: ");
			for (i = PE50_THERMAL_WARM; i < PE50_THERMAL_VERY_HOT; i++) {
				sprintf(buf, "%d, ", desc->sys_power_level_def[i]);
				strcat(str, buf);
			}
			strcat(str, "\n \rsys_power limit: ");
			for (i = PE50_THERMAL_WARM; i < PE50_THERMAL_VERY_HOT; i++) {
				sprintf(buf, "%d, ", desc->sys_power_curlmt[i]);
				strcat(str, buf);
			}
			strcat(str, "\n");
			len = sprintf(buf, "%s", str);
			PE50_INFO("%s", str);
			break;
		default:
			len = sprintf(buf,"no para\n");
			break;
	}

	return len;
}

static ssize_t store_pe50_thermal(struct device *dev,struct device_attribute *attr,
	const char *buf, size_t size)
{
	struct pe50_algo_info *info=dev->driver_data;
	struct pe50_algo_desc *desc = info->desc;
	struct pe50_algo_data *data = info->data;
	int i, databuf[25] = {0};
	int j = 0;

	if (!size)
		return 0;

	if (sscanf(buf, "%d [%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d] [%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d]",
		    &databuf[0], &databuf[1], &databuf[2], &databuf[3], &databuf[4], &databuf[5],
		    &databuf[6], &databuf[7], &databuf[8], &databuf[9], &databuf[10], &databuf[11],
		    &databuf[12], &databuf[13], &databuf[14], &databuf[15], &databuf[16], &databuf[17],
		    &databuf[18], &databuf[19], &databuf[20], &databuf[21], &databuf[22]) == 23) {

		switch (databuf[0]) {
			case TTA_LEVEL:
				for (i = PE50_THERMAL_WARM; i < PE50_THERMAL_VERY_HOT; i++) {
					desc->tta_level_def[i] =
							databuf[i - PE50_THERMAL_WARM + 1];
					desc->tta_curlmt[i] =
							databuf[i - 2 * PE50_THERMAL_WARM + PE50_THERMAL_VERY_HOT + 1];
				}
				break;
			case TDVCHG_LEVEL:
				for (i = PE50_THERMAL_WARM; i < PE50_THERMAL_VERY_HOT; i++) {
					desc->tdvchg_level_def[i] =
							databuf[i - PE50_THERMAL_WARM + 1];
					desc->tdvchg_curlmt[i] =
							databuf[i - 2 * PE50_THERMAL_WARM + PE50_THERMAL_VERY_HOT + 1];
				}
				break;
			case TBAT_LEVEL:
				if(desc->supprot_multi_level_charging) {
					for(j = 0; j < TRAN_MULTI_SPEED_MAX-1; j++) {
						for (i = PE50_THERMAL_WARM; i < PE50_THERMAL_VERY_HOT; i++) {
							desc->tc_pe50_multi_level[j].tbat_level_def[i] =
									databuf[i - PE50_THERMAL_WARM + 1];
							desc->tc_pe50_multi_level[j].tbat_curlmt[i] =
									databuf[i - 2 * PE50_THERMAL_WARM + PE50_THERMAL_VERY_HOT + 1];
						}
					}
				} else {
					for (i = PE50_THERMAL_WARM; i < PE50_THERMAL_VERY_HOT; i++) {
						desc->tbat_level_def[i] =
								databuf[i - PE50_THERMAL_WARM + 1];
						desc->tbat_curlmt[i] =
								databuf[i - 2 * PE50_THERMAL_WARM + PE50_THERMAL_VERY_HOT + 1];
					}
				}
				break;
			case TPCB_LEVEL:
				if(desc->supprot_multi_level_charging) {
					for(j = 0; j < TRAN_MULTI_SPEED_MAX-1; j++) {
						for (i = PE50_THERMAL_WARM; i < PE50_THERMAL_VERY_HOT; i++) {
							desc->tc_pe50_multi_level[j].tpcb_level_def[i] =
									databuf[i - PE50_THERMAL_WARM + 1];
							desc->tc_pe50_multi_level[j].tpcb_curlmt[i] =
									databuf[i - 2 * PE50_THERMAL_WARM + PE50_THERMAL_VERY_HOT + 1];
						}
					}
				} else {
					for (i = PE50_THERMAL_WARM; i < PE50_THERMAL_VERY_HOT; i++) {
						desc->tpcb_level_def[i] =
								databuf[i - PE50_THERMAL_WARM + 1];
						desc->tpcb_curlmt[i] =
								databuf[i - 2 * PE50_THERMAL_WARM + PE50_THERMAL_VERY_HOT + 1];
					}
				}
				break;
			case TPA_LEVEL:
				if(desc->supprot_multi_level_charging) {
					for(j = 0; j < TRAN_MULTI_SPEED_MAX-1; j++) {
						for (i = PE50_THERMAL_WARM; i < PE50_THERMAL_VERY_HOT; i++) {
							desc->tc_pe50_multi_level[j].tpa_level_def[i] =
									databuf[i - PE50_THERMAL_WARM + 1];
							desc->tc_pe50_multi_level[j].tpa_curlmt[i] =
									databuf[i - 2 * PE50_THERMAL_WARM + PE50_THERMAL_VERY_HOT + 1];
						}
					}
				} else {
					for (i = PE50_THERMAL_WARM; i < PE50_THERMAL_VERY_HOT; i++) {
						desc->tpa_level_def[i] =
								databuf[i - PE50_THERMAL_WARM + 1];
						desc->tpa_curlmt[i] =
								databuf[i - 2 * PE50_THERMAL_WARM + PE50_THERMAL_VERY_HOT + 1];
					}
				}
				break;
			case SYS_POWER_LEVEL:
				for (i = PE50_THERMAL_WARM; i < PE50_THERMAL_VERY_HOT; i++) {
					desc->sys_power_level_def[i] =
							databuf[i - PE50_THERMAL_WARM + 1];
					desc->sys_power_curlmt[i] =
							databuf[i - 2 * PE50_THERMAL_WARM + PE50_THERMAL_VERY_HOT + 1];
				}
				break;
		}
	}
	data->debug_code = databuf[0];
	return size;
}
static DEVICE_ATTR(pe50_debug, 0664, show_pe50_thermal, store_pe50_thermal);

static ssize_t show_pe50_ffc(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pe50_algo_info *info = dev->driver_data;
	struct pe50_algo_desc *desc = info->desc;
	int i, j;
	char *str;
	size_t str_size = DUMP_FFC_BUF_SIZE;
	ssize_t ret;

	str = kmalloc(str_size, GFP_KERNEL);
	if (!str) {
		return -ENOMEM;
	}
	memset(str, 0, str_size);

	if (desc->support_dual_battery) {
		for (i = 0; i < desc->step_master_ffc_level; i++) {
			for (j = 0; j < PE50_CYCLE_CNT_MAX; j++) {
				PE50_INFO("desc->step_master_ffc[%d]->cycle_cnt[%d] = [%d], rechg_gap = [%d]\n",
					i, j, desc->step_master_ffc[i].cycle_cnt[j], desc->step_master_ffc[i].rechg_gap);
				sprintf(buf, "desc->step_master_ffc[%d]->cycle_cnt[%d] = [%d], rechg_gap = [%d]\n",
					i, j, desc->step_master_ffc[i].cycle_cnt[j], desc->step_master_ffc[i].rechg_gap);
				strcat(str, buf);
			}
		}

		strcat(str, "\r\n");

		for (i = 0; i < desc->step_slave_ffc_level; i++) {
			for (j = 0; j < PE50_CYCLE_CNT_MAX; j++) {
				PE50_INFO("desc->step_slave_ffc[%d]->cycle_cnt[%d] = [%d], rechg_gap = [%d]\n",
					i, j, desc->step_slave_ffc[i].cycle_cnt[j], desc->step_slave_ffc[i].rechg_gap);
				sprintf(buf, "desc->step_slave_ffc[%d]->cycle_cnt[%d] = [%d], rechg_gap = [%d]\n",
					i, j, desc->step_slave_ffc[i].cycle_cnt[j], desc->step_slave_ffc[i].rechg_gap);
				strcat(str, buf);
			}
		}
	} else {
		for (i = 0; i < desc->step_ffc_level; i++) {
			for (j = 0; j < PE50_CYCLE_CNT_MAX; j++) {
				PE50_INFO("desc->step_ffc[%d]->cycle_cnt[%d] = [%d], rechg_gap = [%d]\n",
					i, j, desc->step_ffc[i].cycle_cnt[j], desc->step_ffc[i].rechg_gap);
				sprintf(buf, "desc->step_ffc[%d]->cycle_cnt[%d] = [%d], rechg_gap = [%d]\n",
					i, j, desc->step_ffc[i].cycle_cnt[j], desc->step_ffc[i].rechg_gap);
				strcat(str, buf);
			}
		}
	}

	PE50_INFO("%s\n", str);

	ret = sprintf(buf, "%s", str);

	kfree(str);

	return ret;
}

static ssize_t store_pe50_ffc(struct device *dev,struct device_attribute *attr,
	const char *buf, size_t size)
{
	return size;
}
static DEVICE_ATTR(pe50_ffc, 0664, show_pe50_ffc, store_pe50_ffc);

static int pe50_screen_notifier_callback(struct notifier_block *nb,
                    unsigned long event, void *s)
{
	struct pe50_algo_data *data = container_of(nb,
			struct pe50_algo_data, pe50_screen_notifier);
	
	switch (event) {
	case TRAN_DEV_NOTIFY_SCREEN_OFF:
		PE50_INFO("%s: screen off\n", __func__);
		data->screen_on = false;
		break;
	case TRAN_DEV_NOTIFY_SCREEN_ON:
		PE50_INFO("%s: screen on\n", __func__);
		data->screen_on = true;
		break;
	default:
		break;
	}

	return 0;
}

static int pe50_screen_notifier_init(struct pe50_algo_info *info)
{
	int ret = 0;
	struct pe50_algo_data *data = info->data;

	data->tc_lcd = tran_get_by_name("tc_lcd");
	if (IS_ERR_OR_NULL(data->tc_lcd)) {
		PE50_ERR("%s: get tc_lcd_dev fail\n", __func__);
		ret = -ENODEV;
		goto out;
	}

	data->pe50_screen_notifier.notifier_call = pe50_screen_notifier_callback;
	ret = register_tran_device_notifier(data->tc_lcd,
				&data->pe50_screen_notifier);
	if (ret != 0) {
		PE50_ERR("register lcd notify failed, ret = %d\n", ret);
		goto out;
	}

out:
	return ret;
}

static int pe50_probe(struct platform_device *pdev)
{
	int ret;
	struct pe50_algo_info *info;
	struct pe50_algo_data *data;

	dev_info(&pdev->dev, "%s\n", __func__);

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	info->data = data;
	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);

	ret = pe50_parse_dt(info);
	if (ret < 0) {
		PE50_ERR("%s parse dt fail(%d)\n", __func__, ret);
		return ret;
	}

	mutex_init(&data->notify_lock);
	mutex_init(&data->lock);
	mutex_init(&data->ext_lock);
	mutex_init(&data->chgspeed_lock);
	init_waitqueue_head(&data->wq);
	atomic_set(&data->wakeup_thread, 0);
	atomic_set(&data->stop_thread, 0);
	data->state = PE50_ALGO_STOP;
	data->bootmode = tc_get_boot_mode();
	data->force_spec = -1;
	data->running_ta = -1;
	alarm_init(&data->timer, ALARM_REALTIME, pe50_algo_timer_cb);
	data->task = kthread_run(pe50_algo_threadfn, info, "pe50_algo_task");
	if (IS_ERR(data->task)) {
		ret = PTR_ERR(data->task);
		PE50_ERR("%s run task fail(%d)\n", __func__, ret);
		goto err;
	}

	device_init_wakeup(info->dev, true);
        device_create_file(&(pdev->dev), &dev_attr_pe50_debug);
        device_create_file(&(pdev->dev), &dev_attr_pe50_ffc);
	info->alg = tchg_alg_device_register("pe5", info->dev, info, &pe50_ops,
					    NULL);
	if (IS_ERR_OR_NULL(info->alg)) {
		PE50_ERR("%s reg pe5 algo fail(%d)\n", __func__, ret);
		ret = PTR_ERR(info->alg);
		goto err;
	}
	tchg_alg_dev_set_drvdata(info->alg, info);

	ret = pe50_screen_notifier_init(info);
	if (ret != 0) {
		PE50_ERR("%s register screen notify fail!\n", __func__);
	}

	pe50_multi_chg_init(info);

	dev_info(info->dev, "%s successfully\n", __func__);
	return 0;
err:
	mutex_destroy(&data->chgspeed_lock);
	mutex_destroy(&data->ext_lock);
	mutex_destroy(&data->lock);
	mutex_destroy(&data->notify_lock);
	tchg_alg_device_unregister(info->alg);

	return ret;
}

static int pe50_remove(struct platform_device *pdev)
{
	struct pe50_algo_info *info = platform_get_drvdata(pdev);
	struct pe50_algo_data *data;

	if (info) {
		data = info->data;
		atomic_set(&data->stop_thread, 1);
		pe50_wakeup_algo_thread(data);
		kthread_stop(data->task);
		mutex_destroy(&data->chgspeed_lock);
		mutex_destroy(&data->ext_lock);
		mutex_destroy(&data->lock);
		mutex_destroy(&data->notify_lock);
		tchg_alg_device_unregister(info->alg);
	}

	return 0;
}

static int __maybe_unused pe50_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pe50_algo_info *info = platform_get_drvdata(pdev);
	struct pe50_algo_data *data = info->data;

	dev_info(dev, "%s\n", __func__);
	mutex_lock(&data->lock);

	return 0;
}

static int __maybe_unused pe50_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pe50_algo_info *info = platform_get_drvdata(pdev);
	struct pe50_algo_data *data = info->data;

	dev_info(dev, "%s\n", __func__);
	mutex_unlock(&data->lock);

	return 0;
}

static SIMPLE_DEV_PM_OPS(pe50_pm_ops, pe50_suspend, pe50_resume);

static const struct of_device_id tc_pe50_of_match[] = {
	{ .compatible = "tc,pe5", },
	{},
};
MODULE_DEVICE_TABLE(of, tc_pe50_of_match);

static void pe50_shutdown(struct platform_device *pdev)
{
	struct pe50_algo_info *info = platform_get_drvdata(pdev);
	struct pe50_algo_data *data=info->data;
	struct pe50_stop_info sinfo = {
		.reset_ta = true,
		.hardreset_ta = false,
	};
	if (!info)
		return;
	dev_info(&pdev->dev, "%s\n", __func__);
	mutex_unlock(&data->lock);
	if (data->state != PE50_ALGO_STOP)
		pe50_stop(info, &sinfo);
}

static struct platform_driver pe50_platdrv = {
	.probe = pe50_probe,
	.remove = pe50_remove,
	.shutdown = pe50_shutdown,
	.driver = {
		.name = "pe5",
		.owner = THIS_MODULE,
		.pm = &pe50_pm_ops,
		.of_match_table = tc_pe50_of_match,
	},
};

static int __init pe50_init(void)
{
	return platform_driver_register(&pe50_platdrv);
}

static void __exit pe50_exit(void)
{
	platform_driver_unregister(&pe50_platdrv);
}
module_init(pe50_init);
module_exit(pe50_exit);

MODULE_AUTHOR("Transsion Inc.");
MODULE_DESCRIPTION("MTK Pump Express 5 Algorithm");
MODULE_LICENSE("GPL");
