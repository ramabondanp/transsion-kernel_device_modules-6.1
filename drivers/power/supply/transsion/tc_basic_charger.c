// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Transsion Inc.
 */
#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/reboot.h>

#include "tc_charger.h"
#include "wireless_class.h"
#include "wireless_manager.h"

static int tc_select_ffc_cv_props(struct tc_charger *info)
{
	enum tchg_alg_props props;
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;

	if (desc->support_dual_battery) {
		switch (data->batt_online_status) {
		case GAUGE_DUAL_BATT_ONLINE:
		case GAUGE_SINGLE_SLAVE_BATT_ONLINE:
			props = ALG_GET_SLAVE_FFC_CV;
			break;
		case GAUGE_SINGLE_MASTER_BATT_ONLINE:
		case GAUGE_NONE_BATT_ONLINE:
			props = ALG_GET_MASTER_FFC_CV;
			break;
		default:
			props = ALG_GET_MASTER_FFC_CV;
			break;
	
		}
	} else {
		props = ALG_GET_FFC_CV;
	}

	return props;
}

static int tc_select_cv(struct tc_charger *info)
{
	int cv = -1;
	struct tc_data *data = info->data;
	enum tchg_alg_props cv_props = tc_select_ffc_cv_props(info);

	if (data->is_ffc && data->ffc_alg_id == PE5_ID) {
		tc_get_alg_prop(alg_name_array[PE5_ID], cv_props, &cv);
		cv = cv * 1000;
	} else {
		cv = data->vbat_cv;
	} 

	return cv;
}

static int tc_select_ffc_eoc_props(struct tc_charger *info)
{
	enum tchg_alg_props props;
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;

	if (desc->support_dual_battery) {
		switch (data->batt_online_status) {
		case GAUGE_DUAL_BATT_ONLINE:
		case GAUGE_SINGLE_SLAVE_BATT_ONLINE:
			props = ALG_GET_SLAVE_FFC_EOC;
			break;
		case GAUGE_SINGLE_MASTER_BATT_ONLINE:
		case GAUGE_NONE_BATT_ONLINE:
			props = ALG_GET_MASTER_FFC_EOC;
			break;
		default:
			props = ALG_GET_MASTER_FFC_EOC;
			break;
	
		}
	} else {
		props = ALG_GET_FFC_EOC;
	}

	return props;
}

static int tc_select_eoc(struct tc_charger *info)
{
	int eoc = -1;
	enum tchg_alg_props eoc_props = tc_select_ffc_eoc_props(info);
	struct tc_data *data = info->data;

	if (data->is_ffc && data->ffc_alg_id == PE5_ID) {
		tc_get_alg_prop(alg_name_array[PE5_ID], eoc_props, &eoc);
		eoc = eoc * 1000;
	} else {
		eoc = data->vbat_eoc;
	} 
	return eoc;
}

static void tc_select_ir_comp(struct tc_charger *info, int *r_comp, int *v_comp_max)
{
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;

	if (desc->support_dual_battery) {
		switch (data->batt_online_status) {
		case GAUGE_DUAL_BATT_ONLINE:
		case GAUGE_SINGLE_MASTER_BATT_ONLINE:
			*r_comp = desc->master_r_comp;
			*v_comp_max = desc->master_v_comp_max;
			break;
		case GAUGE_SINGLE_SLAVE_BATT_ONLINE:
		case GAUGE_NONE_BATT_ONLINE:
			*r_comp = desc->slave_r_comp;
			*v_comp_max = desc->slave_v_comp_max;
			break;
		default:
			*r_comp = desc->master_r_comp;
			*v_comp_max = desc->master_v_comp_max;
			break;

		}
	} else {
		*r_comp = desc->r_comp;
		*v_comp_max = desc->v_comp_max;
	}

}

int tc_get_ir_comp(struct tc_charger *info)
{
	u32 v_comp = 0;//uV
	int ibat = 0;//mA
	struct tc_desc *desc = info->desc;
	struct tc_data *data = info->data;
	int r_comp = 0;
	int v_comp_max = 0;

	if (!desc->enable_ir_comp)
		goto out;

	/* select r and v for dual batt */
	tc_select_ir_comp(info, &r_comp, &v_comp_max);

	if (desc->support_hardware_ir_comp) {
		charger_dev_set_ircmp(data->chg1_dev, r_comp * 1000);
		charger_dev_set_ivcmp(data->chg1_dev, v_comp_max * 1000);
		goto out;
	}
	
	ibat = tc_get_battery_current(); 
	if (ibat < 0)
		goto out;

	v_comp = ibat * r_comp;

	v_comp = min_t(u32, v_comp, v_comp_max * 1000);
	v_comp = max_t(u32, v_comp, 0);

out:
	tchr_info("%s supp_hw_ir:%d, ibat:%d r_comp:%d v_comp:%d\n",
		__func__, desc->support_hardware_ir_comp,
		ibat, desc->r_comp, v_comp);

	return v_comp;
}

static int tc_get_cv_gap(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	int cv_gap = 0;

	if (data->long_life_rechg_work_flag || data->long_life_rechg_done) {
		if (data->pe5_rechg_cv_gap != 0) {
			cv_gap = data->pe5_rechg_cv_gap;
		} else {
			cv_gap = data->long_life_rechg_cv_gap;
		}
	}

	cv_gap = cv_gap * 1000;

	return cv_gap;
}

static int tc_get_long_life_rechg_cur(struct tc_charger *info, int *val)
{
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	int rechg_cur = 0;

	if (!desc->support_long_life_recharger)
		return -EOPNOTSUPP;

	if (data->long_life_rechg_work_flag) {
		tc_get_alg_prop(alg_name_array[PE5_ID],
			ALG_GET_LONG_LIFE_RECHG_CUR, &rechg_cur);
		*val = rechg_cur * 1000;
	}

	tchr_info("%s: long life rechg_cur = %d\n", __func__, rechg_cur > 0 ? *val : -1);

	return rechg_cur > 0 ? 0 : -1;
}

static void tc_reset_eoc(struct tc_charger *info)
{
	struct tc_desc *desc = info->desc;
	struct tc_data *data = info->data;

	tchr_info("%s: do tc_reset_eoc\n", __func__);

	if (desc->support_reset_eoc) {
		charger_dev_reset_eoc_state(data->chg1_dev);
	} else {
		charger_dev_enable_termination(data->chg1_dev, false);
		vote(data->chg1_disable_vote, CHG_THREAD_VOTER, true, true);
		msleep(200);
		vote(data->chg1_disable_vote, CHG_THREAD_VOTER, false, false);
		charger_dev_enable_termination(data->chg1_dev, true);
	}
}

static void select_constant_voltage(struct tc_charger *info)
{
	struct tc_desc *desc = info->desc;
	struct tc_data *data = info->data;
	int v_comp = 0;
	int chg1_cv = BATTERY_CV;
	int chg2_cv = 0;
	int cv_gap = 0;
	static int pre_cv_gap;
	struct charger_data *pdata = &info->data->chg_data[CHG1_SETTING];
	struct charger_data *pdata2 = &info->data->chg_data[CHG2_SETTING];

	if (!desc->enable_sw_jeita) {
		goto out;
	}

	cv_gap = tc_get_cv_gap(info);

	v_comp = tc_get_ir_comp(info);

	chg1_cv = tc_select_cv(info);
	chg1_cv -= cv_gap;
	chg1_cv += v_comp;
	chg2_cv = chg1_cv;

out:
	pdata->cv = chg1_cv;
	pdata2->cv = chg2_cv;
	tchr_info("setting cv:%d %d v_comp:%d, cv_gap:%d, is_ffc:%d\n",
		chg1_cv, chg2_cv, v_comp, cv_gap, data->is_ffc);
	charger_dev_set_constant_voltage(data->chg1_dev, chg1_cv);
	if (pdata2->chg_en)
		charger_dev_set_constant_voltage(data->chg2_dev, chg2_cv);

	if (cv_gap != pre_cv_gap && cv_gap != 0)
		tc_reset_eoc(info);

	pre_cv_gap = cv_gap;
}

static void tc_set_chg1_eoc_current(struct tc_charger *info, int chg1_eoc)
{
	struct tc_data *data = info->data;
	struct charger_data *pdata = &info->data->chg_data[CHG1_SETTING];

	if (chg1_eoc > 0) {
		charger_dev_set_eoc_current(data->chg1_dev, chg1_eoc);
		charger_dev_enable_termination(data->chg1_dev, true);
	} else {
		charger_dev_enable_termination(data->chg1_dev, false);
	}

	pdata->eoc = chg1_eoc;
}

static void tc_set_chg2_eoc_current(struct tc_charger *info, int chg2_eoc)
{
	struct tc_data *data = info->data;
	struct charger_data *pdata2 = &info->data->chg_data[CHG2_SETTING];

	if (chg2_eoc > 0) {
		charger_dev_set_eoc_current(data->chg2_dev, chg2_eoc);
		charger_dev_enable_termination(data->chg2_dev, true);
	} else {
		charger_dev_enable_termination(data->chg2_dev, false);
	}

	pdata2->eoc = chg2_eoc;
}
	

static void select_battery_eoc(struct tc_charger *info)
{
	struct tc_desc *desc = info->desc;
	struct tc_data *data = info->data;
	int chg1_eoc = 0, chg2_eoc = 0;
	int eoc = 0;

	/* If dual batt en, close hardware eoc */
	if (desc->support_dual_battery) {
		if (data->batt_online_status == GAUGE_DUAL_BATT_ONLINE ||
			!data->dual_batt_enable_eoc)
			goto out;
	}

	if (data->vbat_cv < data->vbat_cv_term) {
		chg1_eoc = 0;
		chg2_eoc = 0;
		goto out;
	}

	/* DUAL SWITCH EOC */
	if (data->try_dsc) {
		chg1_eoc = 0;
		chg2_eoc = 0;
		goto out;
	}


	/* EOC */
	eoc = tc_select_eoc(info);
	if (eoc > desc->swchg_hw_eoc_max) {
		chg1_eoc = 0;
		chg2_eoc = 0;
		data->eoc_scheme = SW_EOC;
		data->sw_eoc_cur = eoc;
	} else {
		chg1_eoc = eoc;
		chg2_eoc = 0;
		data->eoc_scheme = HW_EOC;
		data->sw_eoc_cur = 0;
	}

out:
	tc_set_chg1_eoc_current(info, chg1_eoc);
	tc_set_chg2_eoc_current(info, chg2_eoc);

	tchr_info("setting eoc chg1:%d chg2:%d, eoc_scheme:%d\n",
			chg1_eoc, chg2_eoc, data->eoc_scheme);
}

static void select_charger_type_current_limit(struct tc_charger *info, struct charger_data *chg_data)
{
	int alias_type = TC_UNKNOWN;
	struct tc_desc *desc = info->desc;
	struct tc_data *data = info->data;

	chg_data->input_current_limit = -1;
	chg_data->charging_current_limit = -1;

	alias_type = tc_get_alias_type();

	switch (alias_type) {
	case TC_SDP:
		chg_data->charging_current_limit = desc->sdp_charger_current;
		chg_data->input_current_limit = desc->sdp_input_current;
		break;
	case TC_CDP:
		chg_data->charging_current_limit = desc->cdp_charger_current;
		chg_data->input_current_limit = desc->cdp_input_current;
		break;
	case TC_DCP:
		chg_data->charging_current_limit = desc->dcp_charger_current;
		chg_data->input_current_limit = desc->dcp_input_current;
		break;
	case TC_NON_STD:
		chg_data->charging_current_limit = desc->nonstd_charger_current;
		chg_data->input_current_limit = desc->nonstd_input_current;
		break;
	case TC_WIRELESS:
		wireless_select_current_limit(data->wlsc_dev);
		chg_data->charging_current_limit = get_client_vote(data->total_ichg_vote, WIRELESS_VOTER);
		chg_data->input_current_limit = get_client_vote(data->total_aicr_vote, WIRELESS_VOTER);
		break;
	default:
		chg_data->charging_current_limit = desc->sdp_charger_current;
		chg_data->input_current_limit = desc->sdp_input_current;
		break;
	}

	tc_get_fast_charger_limit(info, chg_data);

/* out: */
	tchr_info("%s: original iindpm:%d, ichg:%d\n",
		__func__, chg_data->input_current_limit, chg_data->charging_current_limit);
	return;

}

static int charging_core_process(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	struct charger_data *pdata = &info->data->chg_data[CHG1_SETTING];
	struct charger_data *pdata2 = &info->data->chg_data[CHG2_SETTING];

	if (IS_ERR_OR_NULL(data->chg1_dev)) {
		tchr_info("primary charger not found\n");
		return -EINVAL;
	}

	enable_primary_charger(info, true);
	if (data->support_dual_switch) {
		enable_secondary_charger(info, data->try_dsc);
	}

	tchr_err("support_dual_switch:%d,%d,"
		" chg_en:%d %d, hiz:%d %d, ichg:%d,%d, aicr:%d,%d,"
		" mivr:%d, %d\n",
		data->support_dual_switch, data->try_dsc,
		pdata->chg_en, pdata2->chg_en,
		pdata->hiz, pdata2->hiz,
		pdata->charging_current_limit, pdata2->charging_current_limit,
		pdata->input_current_limit, pdata2->input_current_limit,
		pdata->mivr, pdata2->mivr);

	return 0;
}

static int __maybe_unused get_pid_charger_current_limit(struct tc_charger *info, int *val)
{
	int ret = 0;
	union com_propval delta_current = {0,};
	struct tran_device *pid_dev = NULL;
	struct tc_data *data = info->data;
	struct charger_data *total_pdata = &data->total_pdata;
	
	pid_dev = tran_get_by_name("pid_chg_algo");
	if (IS_ERR_OR_NULL(pid_dev)) {
	        pr_info("get pid_dev failed\n");
	        ret = -EINVAL;
	        goto out;
	}
	
	ret = tran_dev_get_prop(pid_dev, TRAN_PROP_GET_PID_PARAM, &delta_current);
	if (ret != 0) {
	        pr_info("get pid current failed\n");
	        goto out;
	}
	
	*val = total_pdata->charging_current_limit + delta_current.intval;
out:
	return ret;     
}

static void select_charging_current_limit(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	struct charger_data pdata;
	struct charger_data *total_pdata = &data->total_pdata;
	static struct tchg_alg_device *prev_algo = NULL;
	int charging_current, input_current;
	int limit_current, pid_chg_cur, bypass_energy;
	int ret = 0;
	int tbat;
	int alias_type = TC_UNKNOWN;
	int limit_min_current;

	if (data->running_algo &&
		data->running_algo->alg_id == PE5_ID)
		return;

	/* memtest as the first priority */
	if (data->memtest_current > 0) {
		charging_current = data->memtest_current;
		input_current = data->memtest_current;
		goto out;
	}

	bypass_energy = tc_get_bypass_energy();
	if (bypass_energy > 0)
		charging_current = desc->max_charging_current_limit * bypass_energy / 100;
	else
		charging_current = desc->max_charging_current_limit;

	/* chg type current */
	select_charger_type_current_limit(info, &pdata);
	charging_current = min_t(int, pdata.charging_current_limit,
				charging_current);
	input_current = min_t(int, pdata.input_current_limit,
				desc->max_input_current_limit);

	if (desc->charger_unlimited)
		goto out;

	/* jeita current */
	if (desc->enable_sw_jeita) {
		charging_current = min_t(int, charging_current, data->vbat_cc);
	}
	
	if(desc->low_temp_err_limit){
		tbat = tc_get_battery_temperature();
		if(tbat <= BATT_TEMP_LIM_H && tbat >= BATT_TEMP_LIM_L)
		input_current = min_t(int, input_current,desc->low_temp_err_limit_input);
	}
	
	alias_type = tc_get_alias_type();
	if (alias_type == TC_WIRELESS) 
		limit_min_current = desc->wireless_min_charger_current;
	else
		limit_min_current = desc->min_charger_current;

	ret = get_pid_charger_current_limit(info, &pid_chg_cur);
	if (ret == 0) { 
		/* Not take effect when the protocol is switched,
		 * ensuring fast up current */
		if (prev_algo == data->running_algo) {
			limit_current = max_t(int, limit_min_current, pid_chg_cur);
			charging_current = min_t(int, charging_current, limit_current);
		}
	} else {
		/* tbat limit current */
		limit_current = max_t(int, limit_min_current,
				desc->max_charging_current_limit - desc->tbat_ichg[data->tbat_level]);
		charging_current = min_t(int, charging_current, limit_current);
	
		/* tpa limit current */
		limit_current = max_t(int, limit_min_current,
				desc->max_charging_current_limit - desc->tpa_ichg[data->tpa_level]);
		charging_current = min_t(int, charging_current, limit_current);
	
		/* tpcb limit current */
		if (!data->kpoc) {
			limit_current = max_t(int, limit_min_current,
					desc->max_charging_current_limit - desc->tpcb_ichg[data->tpcb_level]);
			charging_current = min_t(int, charging_current, limit_current);
		}
	}

	ret = tc_get_long_life_rechg_cur(info ,&limit_current);
	if (ret == 0) {
		charging_current = min_t(int, charging_current, limit_current);
	}

out:
	prev_algo = data->running_algo;
	total_pdata->charging_current_limit = charging_current;
	total_pdata->input_current_limit = input_current;

	vote(data->total_ichg_vote, CHG_THREAD_VOTER, true, total_pdata->charging_current_limit);
	vote(data->total_aicr_vote, CHG_THREAD_VOTER, true, total_pdata->input_current_limit);

	tchr_err("unlimited:%d enable_sw_jeita:%d memtest_lmt:%d max_lmt:%d,%d type_lmt:%d,%d jeita_lmt:%d tbat_lmt:%d tpcb_lmt:%d tpa_lmt:%d, pid_chg_cur:%d, chg_speed:%d chg_owner:%d total:%d,%d min_current:%d bypass_energy:%d\n",
		desc->charger_unlimited, desc->enable_sw_jeita, _uA_to_mA(data->memtest_current),
		_uA_to_mA(desc->max_input_current_limit), _uA_to_mA(desc->max_charging_current_limit),
		_uA_to_mA(pdata.input_current_limit), _uA_to_mA(pdata.charging_current_limit),
		_uA_to_mA(data->vbat_cc), _uA_to_mA(desc->tbat_ichg[data->tbat_level]),
		_uA_to_mA(desc->tpcb_ichg[data->tpcb_level]), _uA_to_mA(desc->tpa_ichg[data->tpa_level]),
		_uA_to_mA(pid_chg_cur), data->chg_speed, data->speed_owner,
		_uA_to_mA(total_pdata->input_current_limit), _uA_to_mA(total_pdata->charging_current_limit),_uA_to_mA(limit_min_current), bypass_energy);
}

static void tc_charger_full_inform_related_module(struct tc_charger *info, bool chg_done)
{
	int alias_type = TC_UNKNOWN;
	union com_propval tran_val = {0, };
	struct tc_data *data = info->data;

	if (IS_ERR_OR_NULL(data->ac_ctl_dev))
		data->ac_ctl_dev = tran_get_by_name("adapter_control");

	tran_val.intval = chg_done;
	tran_dev_set_prop(data->ac_ctl_dev, TRAN_PROP_CHG_FULL_STATE, &tran_val);

	if (chg_done) {
		charger_dev_do_event(data->wlsc_dev, EVENT_FULL, 0);
	} else {
		charger_dev_do_event(data->wlsc_dev, EVENT_RECHARGE, 0);
	}

	alias_type = tc_get_alias_type();
	if ((alias_type == TC_WIRELESS) && chg_done) {
		vote(data->total_aicr_vote, WIRELESS_VOTER, true, 200000);
	}
}

static void tc_check_long_life_recharger(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	int cv_gap = 0;

	if (!desc->support_long_life_recharger)
		return;

	if (data->trigger_long_life_rechg_timer) {
		if (data->long_life_rechg_work_flag) {
			tchr_info("%s: long life rechg done\n", __func__);
			data->long_life_rechg_work_flag = false;
			data->long_life_rechg_done = true;
		}
		return;
	}

	if (data->is_ffc && data->ffc_alg_id == PE5_ID) {

		if (data->pe5_rechg_cv_gap == 0) {
			tc_get_alg_prop(alg_name_array[PE5_ID],
				ALG_GET_LONG_LIFE_RECHG_CV_GAP, &cv_gap);
			data->pe5_rechg_cv_gap = cv_gap;
		}

		cv_gap = data->pe5_rechg_cv_gap;
	} else {
		cv_gap = data->long_life_rechg_cv_gap;
	}

	if (cv_gap == 0) {
		pr_info("%s:do without recharger:%d\n",
			__func__, cv_gap);
		return;
	}

	tchr_info("%s: recharger cv gap:%d\n",
		__func__, cv_gap);

	data->trigger_long_life_rechg_timer = true;
	tc_start_alarm_recharger_timer(info);
}

static void tc_check_hw_eoc(struct tc_charger *info, bool *chg_done)
{
	struct tc_data *data = info->data;
	bool chg1_done = false;
	bool chg2_done = false;
	struct charger_data *pdata2 = &info->data->chg_data[CHG2_SETTING];

	charger_dev_is_charging_done(data->chg1_dev, &chg1_done);
	charger_dev_is_charging_done(data->chg2_dev, &chg2_done);
	
	tchr_err("%s chg1_done:%d, chg2_done:%d\n", __func__, chg1_done, chg2_done);
	if (chg1_done == true && !pdata2->chg_en) {
		*chg_done = true;
	}
}

static void tc_check_sw_eoc(struct tc_charger *info, bool *chg_done)
{
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;
	int vbat = 0;
	int ibat = 0;
	int pe50_done = 0;
	struct charger_data *pdata = &info->data->chg_data[CHG1_SETTING];

	if (data->ffc_alg_id == PE5_ID) {
		tc_get_alg_prop(alg_name_array[PE5_ID],
				ALG_TAPER_DONE, (int *)&pe50_done);
		if (pe50_done) {
			tchr_err("%s:pe50 chg done\n", __func__);
			atomic_set(&data->sw_eoc_cnt, 0);
			*chg_done = true;
			enable_secondary_charger(info, false);
			enable_primary_charger(info, false);
		}
		return;
	}

	vbat = tc_get_battery_voltage();
	vbat = vbat * 1000;
	ibat = tc_get_battery_current();

	tchr_err("vbat:%d, ibat:%d, chg1_cv:%d\n",
		vbat, ibat, pdata->cv);	

	if (vbat >= pdata->cv - desc->sw_eoc_cv_gap && ibat > 0 &&
	    ibat <= data->sw_eoc_cur && !data->is_chg_done) {
		if (atomic_read(&data->sw_eoc_cnt) == desc->sw_eoc_cnt_time) {
			tchr_err("%s:sw eoc done chg1_cv:%d, vbat:%d, ibat:%d, sw_eoc_cv_gap:%d",
				__func__, pdata->cv, vbat, ibat, desc->sw_eoc_cv_gap);
			atomic_set(&data->sw_eoc_cnt, 0);
			*chg_done = true;
			enable_secondary_charger(info, false);
			enable_primary_charger(info, false);
			return;
		} else {
			atomic_inc(&data->sw_eoc_cnt);
		}
	} else {
		atomic_set(&data->sw_eoc_cnt, 0);
	}

	/* recharger check */
	if (data->is_chg_done) {
		*chg_done = true;
		if (pdata->cv - vbat > desc->recharger_gap) {
			tchr_err("%s:start sw eoc recharger chg1_cv:%d, vbat:%d, recharger_gap:%d",
				__func__, pdata->cv, vbat, desc->recharger_gap);
			*chg_done = false;
		} else if (data->long_life_rechg_work_flag) {
			tchr_err("%s:long life recharger chg1_cv:%d, vbat:%d",
				__func__, pdata->cv, vbat);
			*chg_done = false;
		}
	}
}

static void check_charging_done(struct tc_charger *info, bool *chg_done)
{
	struct tc_data *data = info->data;
	int pe50_done = 1;

	if (data->ffc_alg_id == PE5_ID) {
		tc_get_alg_prop(alg_name_array[PE5_ID],
				ALG_TAPER_DONE, (int *)&pe50_done);
		if (!pe50_done) {
			*chg_done = false;
			goto out;
		}
	}

	switch (data->eoc_scheme) {
	case HW_EOC:
		tc_check_hw_eoc(info, chg_done);
		break;
	case SW_EOC:
		tc_check_sw_eoc(info, chg_done);
		break;
	default:
		charger_dev_is_charging_done(data->chg1_dev, chg_done);
	}

out:
	tchr_err("%s: chg_done:%d, pe50_done:%d", __func__, *chg_done, pe50_done);
	return;
}

static void tc_check_charging_done(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	bool chg_done = false;
	u32 event;

	check_charging_done(info, &chg_done);

	if (data->is_chg_done == chg_done)
		return;

	if (chg_done) {
		tc_check_long_life_recharger(info);
		data->is_ffc = 0;
		data->ffc_alg_id = ALG_NONE;
		data->sw_eoc_cur = 0;
	}

	if (!chg_done) {
		data->long_life_rechg_done = false;
		ktime_get_boottime_ts64(&data->charging_begin_time);
	}
	/* notify chg type */
	event = chg_done ? EVENT_FULL : EVENT_RECHARGE;
	tran_dev_notify(data->tc_charger_dev, event, info);

	/* notify fast charge protocol */
	event = chg_done ? EVT_FULL : EVT_RECHARGE;
	tc_chg_alg_notify_call(info, event, 0);

	tc_charger_full_inform_related_module(info, chg_done);

	data->is_chg_done = chg_done;

	tchr_err("%s battery %s\n", __func__, chg_done ? "full" : "recharge");
}

static bool tc_algo_protocol_process(struct tc_charger *info)
{
	int i;
	bool is_basic = true;
	int alg_state = 0;
	struct tc_data *data = info->data;
	struct tchg_alg_device *alg;
	struct tchg_alg_device *prev_algo = data->running_algo;
	int alias_type = tc_get_alias_type();

	if (data->monkey_flag == TRAN_AGING_FLAG ||
		(data->monkey_flag == TRAN_AGING_KOM && data->wait_protocol_done) ||
		data->smtchg_data.smartchg_energy == SMTCHG_ENERGY_MIN) {
		tchg_alg_stop_algo(data->running_algo);
		goto out;
	}

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = data->alg[i];
		if (alg == NULL || alg->is_disabled)
			continue;

		if(data->running_algo != NULL &&
		   data->running_algo->alg_id != data->alg[i]->alg_id)
			continue;

		if (alias_type == TC_NON_STD &&
			alg->alg_id != PE5_ID &&
			alg->alg_id != PDC_ID) {
			alg_state = ALG_TA_CHECKING;
			continue;
		}

		alg_state = tchg_alg_is_algo_ready(alg);

		tchr_err("%s %s alg_state:%s\n", __func__,
			dev_name(&alg->dev),
			tchg_alg_state_to_str(alg_state));

		if (alg_state == ALG_INIT_FAIL || alg_state == ALG_TA_NOT_SUPPORT ||
			alg_state == ALG_NOT_READY) {
			/* try next algorithm */
			continue;
		} else if (alg_state == ALG_TA_CHECKING || alg_state == ALG_DONE) {
			/* wait checking , use basic first */
			break;
		} else if (alg_state == ALG_READY || alg_state == ALG_RUNNING) {
			/* recovery polling interal to normal */
			if(data->polling_interval == CHARGING_ALG_CHECK_INTERVAL)
				data->polling_interval = CHARGING_INTERVAL;
			if (tchg_alg_start_algo(alg) == ALG_TA_NOT_SUPPORT)
			        continue;

			is_basic = false;
			data->running_algo = data->alg[i];
			break;
		} else {
			tchr_err("algorithm alg_state is error\n");
		}

	}

out:
	tchr_err("%s is_basic:%d\n", __func__, is_basic);
	if (is_basic)
		data->running_algo = NULL;

	if (alg_state != ALG_TA_CHECKING)
		data->wait_protocol_done = true;
	else
		data->polling_interval = CHARGING_ALG_CHECK_INTERVAL;

	if (prev_algo != data->running_algo) {
		tchr_info("algo change, refresh current\n");
		select_charging_current_limit(info);
	}

	return is_basic;

}

static int do_powerpath(struct tc_charger *info)
{
	/* struct tc_data *data = info->data; */
	/* struct tc_desc *desc = info->desc; */

	select_charging_current_limit(info);

	return 0;
}

static int do_algorithm(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	struct tc_desc *desc = info->desc;

	tc_check_charging_done(info);
	select_charging_current_limit(info);

	if (!desc->enable_hv_charging) {
		data->wait_protocol_done = true;
		goto skip_algo;
	}

	tc_algo_protocol_process(info);

	data->try_dsc = try_dual_switch_check(info);

skip_algo:
	select_constant_voltage(info);

	if (data->is_chg_done) {
		data->polling_interval = CHARGING_FULL_INTERVAL;
		goto battery_full;
	}

	select_battery_eoc(info);
	charging_core_process(info);

battery_full:
	if (data->chg1_dev != NULL) {
		tc_charger_dump_key_info(info, SW_CHG1);
		charger_dev_dump_registers(data->chg1_dev);
	}
	if (data->chg2_dev != NULL) {
		tc_charger_dump_key_info(info, SW_CHG2);
		charger_dev_dump_registers(data->chg2_dev);
	}
	return 0;
}

static int enable_charging(struct tc_charger *info,bool en)
{
	int i;
	struct tchg_alg_device *alg;
	struct tc_data *data = info->data;
	struct charger_data *pdata2 = &data->chg_data[CHG2_SETTING];

	tchr_err("%s %d\n", __func__, en);

	if (en == false) {
		for (i = 0; i < MAX_ALG_NO; i++) {
			alg = data->alg[i];
			if (alg == NULL)
				continue;
			tchg_alg_stop_algo(alg);
		}

		if (data->support_dual_switch && pdata2->chg_en) {
			enable_secondary_charger(info, false);
			charger_dev_do_event(data->chg2_dev, EVENT_DISCHARGE, 0);
			msleep(200);
		}

		enable_primary_charger(info, false);
		charger_dev_do_event(data->chg1_dev, EVENT_DISCHARGE, 0);
		charger_dev_do_event(data->wlsc_dev, EVENT_DISCHARGE, 0);
	} else {
		enable_primary_charger(info, true);
		charger_dev_do_event(data->chg1_dev, EVENT_RECHARGE, 0);
		charger_dev_do_event(data->wlsc_dev, EVENT_RECHARGE, 0);
	}

	return 0;
}

static int chg1_dev_event(struct notifier_block *nb, unsigned long event,void *v)
{
	struct tc_data *data = container_of(nb,
				struct tc_data, chg1_nb);
	struct tc_charger *info = data->info;

	tchr_err("%s %lu\n", __func__, event);

	switch (event) {
	case CHARGER_DEV_NOTIFY_EOC:
		tchr_info("%s: primary chg done\n", __func__);
		_wake_up_charger(info);
		break;
	case CHARGER_DEV_NOTIFY_RECHG:
		tchr_info("%s: recharge\n", __func__);
		_wake_up_charger(info);
		break;
	case CHARGER_DEV_NOTIFY_PWR_CHANGED:
		tchr_info("%s: pwr changed!\n", __func__);
		_wake_up_charger(info);
		break;
	default:
		break;
	}
	
	return NOTIFY_DONE;
}

static int chg2_dev_event(struct notifier_block *nb, unsigned long event,void *v)
{
	struct tc_data *data = container_of(nb,
				struct tc_data, chg2_nb);
	struct tc_charger *info = data->info;

	tchr_err("%s %lu\n", __func__, event);

	switch (event) {
	case CHARGER_DEV_NOTIFY_EOC:
		tchr_info("%s: primary chg done\n", __func__);
		_wake_up_charger(info);
		break;
	case CHARGER_DEV_NOTIFY_RECHG:
		tchr_info("%s: recharge\n", __func__);
		_wake_up_charger(info);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static int to_alg_notify_evt(unsigned long evt)
{
	switch (evt) {
	case CHARGER_DEV_NOTIFY_VBUS_OVP:
		return EVT_VBUSOVP;
	case CHARGER_DEV_NOTIFY_IBUSOCP:
		return EVT_IBUSOCP;
	case CHARGER_DEV_NOTIFY_IBUSUCP_FALL:
		return EVT_IBUSUCP_FALL;
	case CHARGER_DEV_NOTIFY_BAT_OVP:
		return EVT_VBATOVP;
	case CHARGER_DEV_NOTIFY_IBATOCP:
		return EVT_IBATOCP;
	case CHARGER_DEV_NOTIFY_VBATOVP_ALARM:
		return EVT_VBATOVP_ALARM;
	case CHARGER_DEV_NOTIFY_VBUSOVP_ALARM:
		return EVT_VBUSOVP_ALARM;
	case CHARGER_DEV_NOTIFY_VOUTOVP:
		return EVT_VOUTOVP;
	case CHARGER_DEV_NOTIFY_VDROVP:
		return EVT_VDROVP;
	default:
		return -EINVAL;
	}
}

static int dvchg1_dev_event(struct notifier_block *nb,
				unsigned long event, void *notify_data)
{
	struct tc_data *data = container_of(nb,
				struct tc_data, dvchg1_nb);
	struct tc_charger *info = data->info;
	int alg_evt = to_alg_notify_evt(event);

	tchr_info("%s %ld", __func__, event);
	if (alg_evt < 0)
		return NOTIFY_DONE;
	tc_chg_alg_notify_call(info, alg_evt, 0);
	return NOTIFY_OK;
}

static int dvchg2_dev_event(struct notifier_block *nb,
				unsigned long event, void *notify_data)
{
	struct tc_data *data = container_of(nb,
				struct tc_data, dvchg2_nb);
	struct tc_charger *info = data->info;
	int alg_evt = to_alg_notify_evt(event);

	tchr_info("%s %ld", __func__, event);
	if (alg_evt < 0)
		return NOTIFY_DONE;
	tc_chg_alg_notify_call(info, alg_evt, 0);
	return NOTIFY_OK;
}

static int dvchg3_dev_event(struct notifier_block *nb,
				unsigned long event, void *notify_data)
{
	struct tc_data *data = container_of(nb,
				struct tc_data, dvchg3_nb);
	struct tc_charger *info = data->info;
	int alg_evt = to_alg_notify_evt(event);

	tchr_info("%s %ld", __func__, event);
	if (alg_evt < 0)
		return NOTIFY_DONE;
	tc_chg_alg_notify_call(info, alg_evt, 0);
	return NOTIFY_OK;
}

int tc_basic_charger_init(struct tc_charger *info)
{
	info->algo.do_algorithm = do_algorithm;
	info->algo.do_powerpath = do_powerpath;
	info->algo.enable_charging = enable_charging;
	info->algo.do_chg1_event = chg1_dev_event;
	info->algo.do_chg2_event = chg2_dev_event;
	info->algo.do_dvchg1_event = dvchg1_dev_event;
	info->algo.do_dvchg2_event = dvchg2_dev_event;
	info->algo.do_dvchg3_event = dvchg3_dev_event;
	return 0;
}
