
// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 Transsion Inc.
 */

#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/reboot.h>

#include "tc_pd.h"
#include "tc_common_class.h"
#include "tc_misc_intf.h"
#include "tc_charger_class.h"
static int pd_dbg_level = PD_DEBUG_LEVEL;
#define PD_VBUS_IR_DROP_THRESHOLD 1500

static char *pd_state_to_str(int state)
{
	switch (state) {
	case ALG_INIT_FAIL:
		return "ALG_INIT_FAIL";
	case ALG_TA_CHECKING:
		return "ALG_TA_CHECKING";
	case ALG_TA_NOT_SUPPORT:
		return "ALG_TA_NOT_SUPPORT";
	case ALG_NOT_READY:
		return "ALG_NOT_READY";
	case ALG_READY:
		return "ALG_READY";
	case ALG_RUNNING:
		return "ALG_RUNNING";
	case ALG_DONE:
		return "ALG_DONE";
	default:
		break;
	}
	pd_err("%s unknown state:%d\n", __func__, state);

	return "PD_UNKNOWN";
}

int pd_get_debug_level(void)
{
	return pd_dbg_level;
}

static int pd_init_algo(struct tchg_alg_device *alg)
{
	struct tc_pd *pd;
	int log_level;

	pd = dev_get_drvdata(&alg->dev);
	pd_dbg("%s\n", __func__);

	mutex_lock(&pd->access_lock);
	if (pd_hal_init_hardware(alg) != 0) {
		pd->state = ALG_INIT_FAIL;
		pd_err("%s:init hw fail\n", __func__);
	} else
		pd->state = ALG_READY;

	log_level = pd_hal_get_log_level(alg);
	pr_notice("%s: log_level=%d", __func__, log_level);
	if (log_level > 0)
		pd_dbg_level = log_level;

	pd->pd_idx = -1;
	pd->pd_reset_idx = -1;
	pd->pd_boost_idx = 0;
	pd->pd_buck_idx = 0;
	pd->old_cap_nr = 0;
	pd->is_fast_chr = false;
	pd->pd_cap_max_watt = -1;

	mutex_unlock(&pd->access_lock);
	return 0;
}

static int pd_is_algo_ready(struct tchg_alg_device *alg)
{
	int uisoc;
	int ret_value;
	int Vbat;
	int max_check_cnt = 3;
	int alias_type = tc_get_alias_type();
	struct tc_pd *pd = dev_get_drvdata(&alg->dev);

	mutex_lock(&pd->access_lock);
	__pm_stay_awake(pd->suspend_lock);

	if (alias_type == TC_SDP)
		max_check_cnt = 10;

	if (pd->state != ALG_READY &&
		pd->state != ALG_TA_CHECKING)
		goto out;

	ret_value = pd_hal_is_pd_adapter_ready(alg);
	switch (ret_value) {
	case ALG_READY:
		uisoc = pd_hal_get_uisoc(alg);
		Vbat = pd_hal_get_vbat(alg);
	
		if (uisoc >= pd->pd_stop_battery_soc ||
			Vbat > pd->vbat_threshold) {
			ret_value = ALG_NOT_READY;
			pd_err("High cap or vol, soc(%d,%d), vbat(%d,%d)\n",
				uisoc, pd->pd_stop_battery_soc,
				Vbat, pd->vbat_threshold);
		}
		break;
	case ALG_TA_CHECKING:
		if (++pd->check_ta_cnt > max_check_cnt) {
			ret_value = ALG_TA_NOT_SUPPORT; 
		}
		break;
	}

	pd->state = ret_value;
out:
	pd_info("%s state: %s\n", __func__, pd_state_to_str(pd->state));

	__pm_relax(pd->suspend_lock);
	mutex_unlock(&pd->access_lock);

	return pd->state;
}

void __tc_pdc_init_table(struct tchg_alg_device *alg)
{
	struct tc_pd *pd = dev_get_drvdata(&alg->dev);

	pd->cap.nr = 0;
	pd->cap.selected_cap_idx = -1;

	if (pd_hal_is_pd_adapter_ready(alg) == ALG_READY)
		pd_hal_get_adapter_cap(alg, &pd->cap);
	else
		pd_err("pdc_ready is fail\n");

	if(pd->old_cap_nr != pd->cap.nr)
		pd->pd_idx = -1;
	pd->old_cap_nr = pd->cap.nr;

	pd_err("[%s] nr:%d default:%d,old cap nr:%d\n", __func__, pd->cap.nr,
	pd->cap.selected_cap_idx,pd->old_cap_nr);
}

void __tc_pdc_get_reset_idx(struct tchg_alg_device *alg)
{
	struct tc_pd *pd = dev_get_drvdata(&alg->dev);
	struct pd_power_cap *cap;
	int i = 0;
	int idx = 0;

	cap = &pd->cap;

	if (pd->pd_reset_idx == -1) {
		for (i = 0; i < cap->nr; i++) {

			if (cap->min_mv[i] < pd->vbus_l ||
			    cap->min_mv[i] > pd->vbus_l ||
			    cap->max_mv[i] < pd->vbus_l ||
			    cap->max_mv[i] > pd->vbus_l) {
				continue;
			}
			idx = i;
		}
		pd->pd_reset_idx = idx;
		pd_err("[%s]reset idx:%d vbus:%d %d\n", __func__,
			idx, cap->min_mv[idx], cap->max_mv[idx]);
	}
}

static void pdc_report_charing_animation(struct tchg_alg_device *alg)
{
	struct tran_device *dev = NULL;
	union com_propval prop = {.intval = 0};

	dev = tran_get_by_name("tc_charger");
	if (IS_ERR_OR_NULL(dev)) {
		pr_err("get tc_charger dev fail\n");
		return;
	}

	tran_dev_set_prop(dev, TRAN_PROP_CHARGING_ANIMATION, &prop);

}

void __tc_pdc_get_cap_max_watt(struct tchg_alg_device *alg)
{
	struct tc_pd *pd = dev_get_drvdata(&alg->dev);
	struct pd_power_cap *cap;
	int i = 0;
	int idx = 0;

	cap = &pd->cap;

	if (pd->pd_cap_max_watt == -1) {
		for (i = 0; i < cap->nr; i++) {
			if (cap->min_mv[i] <= pd->vbus_h &&
				cap->min_mv[i] >= pd->vbus_l &&
				cap->max_mv[i] <= pd->vbus_h &&
				cap->max_mv[i] >= pd->vbus_l) {

				if (cap->maxwatt[i] > pd->pd_cap_max_watt) {
					pd->pd_cap_max_watt = cap->maxwatt[i];
					idx = i;
				}
				pd_err("%d %d %d %d %d %d\n",
					cap->min_mv[i],
					cap->max_mv[i],
					pd->vbus_h,
					pd->vbus_l,
					cap->maxwatt[i],
					pd->pd_cap_max_watt);
				continue;
			}
		}

		if (pd->pd_cap_max_watt >= PD_MAX_WATT_SUPPORT)
			pdc_report_charing_animation(alg);

		pd_err("[%s]idx:%d vbus:%d %d maxwatt:%d\n", __func__,
			idx, cap->min_mv[idx], cap->max_mv[idx],
			pd->pd_cap_max_watt);
	}
}

int __tc_pdc_get_idx(struct tchg_alg_device *alg, int selected_idx,
	int *boost_idx, int *buck_idx)
{
	struct tc_pd *pd = dev_get_drvdata(&alg->dev);
	struct pd_power_cap *cap;
	int i = 0;
	int idx = 0;

	cap = &pd->cap;
	idx = selected_idx;

	if (idx < 0) {
		pd_err("[%s] invalid idx:%d\n", __func__, idx);
		*boost_idx = 0;
		*buck_idx = 0;
		return -1;
	}

	/* get boost_idx */
	for (i = 0; i < cap->nr; i++) {

		if (cap->min_mv[i] < pd->vbus_l ||
			cap->max_mv[i] < pd->vbus_l) {
			pd_err("min_mv error:%d %d %d\n",
					cap->min_mv[i],
					cap->max_mv[i],
					pd->vbus_l);
			continue;
		}

		if (cap->min_mv[i] > pd->vbus_h ||
			cap->max_mv[i] > pd->vbus_h) {
			pd_err("max_mv error:%d %d %d\n",
					cap->min_mv[i],
					cap->max_mv[i],
					pd->vbus_h);
			continue;
		}

		if (idx == selected_idx) {
			if (cap->maxwatt[i] > cap->maxwatt[idx])
				idx = i;
		} else {
			if (cap->maxwatt[i] < cap->maxwatt[idx] &&
				cap->maxwatt[i] > cap->maxwatt[selected_idx])
				idx = i;
		}
	}
	*boost_idx = idx;
	idx = selected_idx;

	/* get buck_idx */
	for (i = 0; i < cap->nr; i++) {

		if (cap->min_mv[i] < pd->vbus_l ||
			cap->max_mv[i] < pd->vbus_l) {
			pd_err("min_mv error:%d %d %d\n",
					cap->min_mv[i],
					cap->max_mv[i],
					pd->vbus_l);
			continue;
		}

		if (cap->min_mv[i] > pd->vbus_h ||
			cap->max_mv[i] > pd->vbus_h) {
			pd_err("max_mv error:%d %d %d\n",
					cap->min_mv[i],
					cap->max_mv[i],
					pd->vbus_h);
			continue;
		}

		if (idx == selected_idx) {
			if (cap->maxwatt[i] < cap->maxwatt[idx])
				idx = i;
		} else {
			if (cap->maxwatt[i] > cap->maxwatt[idx] &&
				cap->maxwatt[i] < cap->maxwatt[selected_idx])
				idx = i;
		}
	}
	*buck_idx = idx;

	return 0;
}

int __tc_pdc_setup(struct tchg_alg_device *alg, int idx)
{
	int ret = 0;
	struct tc_pd *pd = dev_get_drvdata(&alg->dev);
	unsigned int mivr;
	unsigned int aicr = 500000;

	if (pd->pd_idx != idx) {
		if (pd->cap.max_mv[idx] > 5000) {
			pd_hal_enable_vbus_ovp(alg, false);
			aicr = min(pd->pd_hv_max_aicr, pd->cap.ma[idx] * 1000);
		} else {
			pd_hal_enable_vbus_ovp(alg, true);
			aicr = pd->cap.ma[idx] * 1000;
		}

		pd_hal_set_mivr(alg, false, pd->mivr);
		pd_hal_set_input_current(alg, true, 500000);
		msleep(100);

		if (pd->cap.ma[idx] <= 100) {
			pd_err("skip this pd_cap!\n");
			goto out;
		}

		ret = pd_hal_set_adapter_cap(alg, pd->cap.max_mv[idx],
			pd->cap.ma[idx]);

		if (ret == 0) {
			mivr = max(pd->cap.max_mv[idx] - PD_VBUS_IR_DROP_THRESHOLD,
					pd->min_charger_voltage / 1000);
			pd_hal_set_mivr(alg, true, mivr * 1000);
			pd_hal_set_input_current(alg, true, aicr);
			pd->pd_idx = idx;
		} else {
			pd_hal_set_mivr(alg, false, pd->mivr);
			pd_hal_set_input_current(alg, false, pd->aicr);
		}

		__tc_pdc_get_idx(alg, idx,
			&pd->pd_boost_idx, &pd->pd_buck_idx);
	}

	pd_err("[%s]idx:%d:%d:%d:%d vbus:%d cur:%d ret:%d\n", __func__,
		pd->pd_idx, idx, pd->pd_boost_idx, pd->pd_buck_idx,
		pd->cap.max_mv[idx], pd->cap.ma[idx], ret);
out:
	return ret;
}

void tc_pdc_reset(struct tchg_alg_device *alg)
{
	struct tc_pd *pd = dev_get_drvdata(&alg->dev);

	pd_err("%s: reset to default profile\n", __func__);
	__tc_pdc_init_table(alg);
	__tc_pdc_get_reset_idx(alg);
	__tc_pdc_setup(alg, pd->pd_reset_idx);
	pd_hal_set_mivr(alg, false, pd->min_charger_voltage);
	pd_hal_set_input_current(alg, false, 0);
}

int __tc_pdc_get_setting(struct tchg_alg_device *alg, int *newvbus, int *newcur,
			int *newidx)
{
	int idx;
	int ichg, uisoc;
	struct tc_pd *pd = dev_get_drvdata(&alg->dev);
	struct pd_power_cap *cap;
	int vbus;
	int mivr1 = 0;
	bool chg1_mivr = false;

	__tc_pdc_init_table(alg);
	__tc_pdc_get_reset_idx(alg);
	__tc_pdc_get_cap_max_watt(alg);
	uisoc = pd_hal_get_uisoc(alg);
	ichg = pd_hal_get_ibat(alg);

	cap = &pd->cap;

	if (cap->nr == 0)
		goto reset;

	if (uisoc >= pd->pd_stop_battery_soc &&
		ichg <= pd->ta_ichg_level_threshold) {

		pd_err("%s: SOC = (%d,%d), ichg:(%d, %d), stop pd fix\n",
			__func__,uisoc, pd->pd_stop_battery_soc,
			ichg, pd->ta_ichg_level_threshold);
		pd->state = ALG_DONE;
		goto reset;
	
	}

	pd_hal_get_mivr_state(alg, CHG1, &chg1_mivr);
	pd_hal_get_mivr(alg, CHG1, &mivr1);
	vbus = pd_hal_get_vbus(alg);

	idx = cap->selected_cap_idx;

	if (idx < 0 || idx >= PD_CAP_MAX_NR)
		idx = 0;

	pd_err("cur idx:%d %dmV(%dmV %dmV) %dmA %dwatt chg1_mivr_state:%d %dmivr1 %dvbus\n", idx,
		cap->max_mv[idx],
		pd->vbus_h, pd->vbus_l,
		cap->ma[idx],
		cap->maxwatt[idx], chg1_mivr,
		mivr1, vbus);

	if (chg1_mivr && ((vbus / 1000) < mivr1 / 1000 - 500))
		goto reset;

	*newidx = pd->pd_boost_idx;
	*newvbus = cap->max_mv[*newidx];
	*newcur = cap->ma[*newidx];

	if (*newidx != pd->pd_idx) {
		pd_hal_do_charger_notify(alg, CHARGER_DEV_NOTIFY_PWR_CHANGED);
		pd_err("new idx:%d %dmV(%dmV %dmV) %dmA %dwatt\n", *newidx,
			cap->max_mv[*newidx],
			pd->vbus_h, pd->vbus_l,
			cap->ma[*newidx],
			cap->maxwatt[*newidx]);
	}

	return 0;

reset:
	tc_pdc_reset(alg);
	*newidx = pd->pd_reset_idx;
	*newvbus = cap->max_mv[*newidx];
	*newcur = cap->ma[*newidx];

	return -1;
}

static int __pd_run(struct tchg_alg_device *alg)
{
	int ret = 0;
	int vbus = 0;
	int cur, idx;
	int ret_value = ALG_RUNNING;
	/* struct tc_pd *pd = dev_get_drvdata(&alg->dev); */

	ret = __tc_pdc_get_setting(alg, &vbus, &cur, &idx);
	if (ret < 0) {
		ret_value = ALG_TA_NOT_SUPPORT;
		goto out;
	}

	if (idx == -1) {
		goto out;
	}

	ret = __tc_pdc_setup(alg, idx);
	if (ret < 0) {
		ret_value = ALG_TA_NOT_SUPPORT;
		goto out;
	}

out:
	return ret_value;
}

static int pd_start_algo(struct tchg_alg_device *alg)
{
	int ret = 0;
	int ret_value = 0;
	struct tc_pd *pd = dev_get_drvdata(&alg->dev);
	bool again = false;

	mutex_lock(&pd->access_lock);

	do {
		pd_info("%s state: %s again:%d\n", __func__,
			pd_state_to_str(pd->state),
			again);
		again = false;

		switch (pd->state) {
		case ALG_READY:
			pd->state = ALG_RUNNING;
			pd->is_fast_chr = true;
			again = true;
			break;
		case ALG_RUNNING:
			ret = __pd_run(alg);
			if (ret == ALG_TA_NOT_SUPPORT) {
				pd->state = ret;
			} else if (ret == ALG_DONE) {
				pd->state = ret;
			} /* ignore else. default running */
			break;
		default:
			pd_err("pd unknown state: %s\n", pd_state_to_str(pd->state));
			pd->state = ALG_TA_NOT_SUPPORT;
			break;
		}
	} while (again == true);

	mutex_unlock(&pd->access_lock);

	return ret_value;
}

static bool pd_is_algo_running(struct tchg_alg_device *alg)
{
	struct tc_pd *pd;

	pd_dbg("%s\n", __func__);
	pd = dev_get_drvdata(&alg->dev);

	if (pd->state == ALG_RUNNING)
		return true;

	return false;
}

static int pd_stop_algo(struct tchg_alg_device *alg)
{
	int ret_value = 0;
	struct tc_pd *pd = dev_get_drvdata(&alg->dev);

	mutex_lock(&pd->access_lock);

	pd_info("%s state: %s\n", __func__, pd_state_to_str(pd->state));

	if (pd->state == ALG_RUNNING) {
		tc_pdc_reset(alg);
		pd->state = ALG_READY;
	}

	mutex_unlock(&pd->access_lock);

	return ret_value;
}

static int pd_full_evt(struct tchg_alg_device *alg)
{
	int ret_value = 0;
	struct tc_pd *pd = dev_get_drvdata(&alg->dev);

	if (pd->state == ALG_RUNNING) {
		tc_pdc_reset(alg);
		pd->state = ALG_DONE;
	}
	return ret_value;
}

static int pd_plugout_reset(struct tchg_alg_device *alg)
{
	int cnt = 0;
	struct tc_pd *pd = dev_get_drvdata(&alg->dev);

	while (mutex_trylock(&pd->access_lock) == 0 && cnt < 20) {
		pd_err("%s:pd is running state: %s cnt:%d\n",
			__func__, pd_state_to_str(pd->state), cnt);
		cnt++;
		msleep(100);
	}
	pd_hal_set_mivr(alg, false, pd->min_charger_voltage);
	pd_hal_set_input_current(alg, false, 0);
	pd_hal_enable_vbus_ovp(alg, true);
	pd->state = ALG_READY;
	pd->pd_idx = -1;
	pd->old_cap_nr = 0;
	pd->pd_reset_idx = -1;
	pd->pd_boost_idx = 0;
	pd->pd_buck_idx = 0;
	pd->is_fast_chr = false;
	pd->pd_cap_max_watt = -1;
	pd->aicr = 500000;
	pd->mivr = pd->min_charger_voltage;
	mutex_unlock(&pd->access_lock);

	pd_info("%s: OK\n", __func__);

	return 0;
}

static int pd_notifier_call(struct tchg_alg_device *alg,
			 struct tchg_alg_notify *notify)
{
	struct tc_pd *pd;
	int ret_value = 0;

	pd = dev_get_drvdata(&alg->dev);
	pd_err("%s evt:%d\n", __func__, notify->evt);

	switch (notify->evt) {
	case EVT_PLUG_IN:
		pd->plug_in = true;
		break;	
	case EVT_PLUG_OUT:
		pd->plug_in = false;
		pd->check_ta_cnt = 0;
		break;
	case EVT_FULL:
		ret_value = pd_full_evt(alg);
		break;
	case EVT_HARDRESET:
		pd->pd_idx = -1;
		pd->old_cap_nr = 0;
		pd->check_ta_cnt = 0;
		if(pd->state == ALG_RUNNING)
			pd->state = ALG_TA_CHECKING;
		ret_value = 0;
		break;
	default:
		ret_value = -EINVAL;
	}

	return ret_value;
}

static void tc_pd_parse_dt(struct tc_pd *pd,
				struct device *dev)
{
	struct device_node *np = dev->of_node;
	u32 val = 0;

	if (of_property_read_u32(np, "ta_ichg_level_threshold", &val) >= 0)
		pd->ta_ichg_level_threshold = val;
	else {
		pd_info("use default TA_ICHG_LEAVE_THRESHOLD:%d\n",
			TA_ICHG_LEAVE_THRESHOLD);
		pd->ta_ichg_level_threshold = TA_ICHG_LEAVE_THRESHOLD;
	}

	if (of_property_read_u32(np, "min_charger_voltage", &val) >= 0)
		pd->min_charger_voltage = val;
	else {
		pd_err("use default V_CHARGER_MIN:%d\n", V_CHARGER_MIN);
		pd->min_charger_voltage = V_CHARGER_MIN;
	}

	if (of_property_read_u32(np, "charger_current_limit", &val) >= 0)
		pd->charger_current_limit = val;
	else {
		pd_err("use default charger_current_limit:%d\n", CHARGER_CURRENT_LIMIT);
		pd->charger_current_limit = CHARGER_CURRENT_LIMIT;
	}

	/* PD */
	if (of_property_read_u32(np, "pd_vbus_upper_bound", &val) >= 0) {
		pd->vbus_h = val / 1000;
	} else {
		pd_err("use default pd_vbus_upper_bound:%d\n",
			PD_VBUS_UPPER_BOUND);
		pd->vbus_h = PD_VBUS_UPPER_BOUND / 1000;
	}

	if (of_property_read_u32(np, "pd_vbus_low_bound", &val) >= 0) {
		pd->vbus_l = val / 1000;
	} else {
		pd_err("use default pd_vbus_low_bound:%d\n",
			PD_VBUS_LOW_BOUND);
		pd->vbus_l = PD_VBUS_LOW_BOUND / 1000;
	}

	if (of_property_read_u32(np, "pd_stop_battery_soc", &val) >= 0)
		pd->pd_stop_battery_soc = val;
	else {
		pd_err("use default pd_stop_battery_soc:%d\n",
			PD_STOP_BATTERY_SOC);
		pd->pd_stop_battery_soc = PD_STOP_BATTERY_SOC;
	}

	if (of_property_read_u32(np, "vbat_threshold", &val) >= 0)
		pd->vbat_threshold = val;
	else {
		pr_notice("turn off vbat_threshold checking:%d\n",
			DISABLE_VBAT_THRESHOLD);
		pd->vbat_threshold = DISABLE_VBAT_THRESHOLD;
	}

	if (of_property_read_u32(np, "pd_hv_max_aicr", &val) >= 0)
		pd->pd_hv_max_aicr = val;
	else {
		pr_notice("use default pd_hv_max_aicr:%d\n",
			PD_HV_MAX_AICR);
		pd->pd_hv_max_aicr = PD_HV_MAX_AICR;
	}

}

int pd_get_prop(struct tchg_alg_device *alg,
		enum tchg_alg_props s, int *value)
{
	struct tc_pd *pd = dev_get_drvdata(&alg->dev);
	struct pd_power_cap *cap;

	cap = &pd->cap;

	pd_info("%s: %d\n", __func__, s);

	switch (s) {
	case ALG_INPUT_CURRENT: //uA
		if (pd->pd_idx >= 0)
			*value = cap->ma[pd->pd_idx] * 1000;
		else
			*value = 2000000;
		break;
	case ALG_CHG_CURRENT: //uA
		*value = pd->charger_current_limit;
		break;
	case ALG_MAX_VBUS: //uV
		if (pd->pd_idx >= 0)
			*value = cap->max_mv[pd->pd_idx] * 1000;
		else
			*value = 5000000;
		break;
	case ALG_IS_FAST_CHR:
		*value = pd->is_fast_chr && pd->pd_cap_max_watt >= PD_MAX_WATT_SUPPORT;
		break;
	case ALG_ADAPTER_CAPACITY:
		*value = pd->pd_cap_max_watt / 1000000;
		break;
	default:
		break;
	}
	return 0;
}

int pd_set_prop(struct tchg_alg_device *alg,
		enum tchg_alg_props s, int value)
{
	struct tc_pd *pd;

	pr_info("%s %d %d\n", __func__, s, value);

	pd = dev_get_drvdata(&alg->dev);

	switch (s) {
	case ALG_LOG_LEVEL:
		pd_dbg_level = value;
		break;
	default:
		break;
	}

	return 0;
}

static struct tchg_alg_ops pd_alg_ops = {
	.init_algo = pd_init_algo,
	.is_algo_ready = pd_is_algo_ready,
	.start_algo = pd_start_algo,
	.is_algo_running = pd_is_algo_running,
	.stop_algo = pd_stop_algo,
	.notifier_call = pd_notifier_call,
	.get_prop = pd_get_prop,
	.set_prop = pd_set_prop,
	.plugout_reset = pd_plugout_reset,
};

static int tc_pd_probe(struct platform_device *pdev)
{
	struct tc_pd *pd = NULL;

	pr_notice("%s: starts\n", __func__);

	pd = devm_kzalloc(&pdev->dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;
	platform_set_drvdata(pdev, pd);
	pd->pdev = pdev;

	mutex_init(&pd->access_lock);
	pd->state = ALG_NOT_READY;
	pd->suspend_lock = wakeup_source_register(NULL, "PD suspend wakelock");
	tc_pd_parse_dt(pd, &pdev->dev);
	pd->alg = tchg_alg_device_register("pd", &pdev->dev,
					pd, &pd_alg_ops, NULL);
	return 0;
}

static int tc_pd_remove(struct platform_device *dev)
{
	return 0;
}

static void tc_pd_shutdown(struct platform_device *dev)
{
}

static const struct of_device_id tc_pd_of_match[] = {
	{.compatible = "transsion,pd_charger",},
	{},
};

MODULE_DEVICE_TABLE(of, tc_pd_of_match);

struct platform_device pd_device = {
	.name = "pd",
	.id = -1,
};

static struct platform_driver pd_driver = {
	.probe = tc_pd_probe,
	.remove = tc_pd_remove,
	.shutdown = tc_pd_shutdown,
	.driver = {
		   .name = "pd",
		   .of_match_table = tc_pd_of_match,
	},
};

static int __init tc_pd_init(void)
{
	return platform_driver_register(&pd_driver);
}
module_init(tc_pd_init);

static void __exit tc_pd_exit(void)
{
	platform_driver_unregister(&pd_driver);
}
module_exit(tc_pd_exit);

MODULE_DESCRIPTION("TRANSSION PD algorithm Driver");
MODULE_LICENSE("GPL");
