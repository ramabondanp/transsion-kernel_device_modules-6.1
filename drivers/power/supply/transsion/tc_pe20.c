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
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/reboot.h>

#include "tc_pe20.h"
#include "tc_algorithm_class.h"
#include "tc_common_class.h"
#include "tc_misc_intf.h"


static char *pe20_state_to_str(int state)
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
	pe20_err("%s unknown state:%d\n", __func__, state);

	return "PE20_UNKNOWN";
}

static int pe20_reset_ta_none_check_volt(struct tchg_alg_device *alg);
static int pe20_info_level = PE20_DEBUG_LEVEL;

int pe20_get_debug_level(void)
{
	return pe20_info_level;
}

static void pe20_report_charing_animation(struct tchg_alg_device *alg)
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

static int pe20_plugout_reset(struct tchg_alg_device *alg)
{
	int ret = 0, cnt = 0;
	struct tc_pe20 *pe20 = dev_get_drvdata(&alg->dev);
	u32 org_chr_volt_mivr = pe20->chr_volt_mivr[PE20_NONE];

	pe20_info("%s\n", __func__);
	/* set flag to end pe20 thread asap */
	mutex_lock(&pe20->cable_out_lock);
	pe20->is_cable_out_occur = true;
	mutex_unlock(&pe20->cable_out_lock);

	/* wait thread end of run */
	while (mutex_trylock(&pe20->access_lock) == 0 && cnt < 20) {
		pe20_err("%s:pe20 is running state: %s cnt:%d\n",
			__func__, pe20_state_to_str(pe20->state), cnt);
		cnt++;
		msleep(100);
	}
	/* reset cable out */
	mutex_lock(&pe20->cable_out_lock);
	pe20->is_cable_out_occur = false;
	mutex_unlock(&pe20->cable_out_lock);

	/* Reset ta control */
	if (pe20->state == ALG_RUNNING)
		pe20_reset_ta_none_check_volt(alg);

	/* Enable OVP */
	ret = pe20_hal_enable_vbus_ovp(alg, true);
	if (ret < 0)
		pe20_err("%s:enable vbus ovp fail, ret:%d\n",
			__func__, ret);

	/* Disable pe20 mivr */
	ret = pe20_hal_set_mivr(alg, false, org_chr_volt_mivr);
	if (ret < 0)
		pe20_err("%s:set mivr fail, ret:%d\n",
			__func__, ret);

	pe20->state = ALG_READY;
	pe20->is_fast_chr = false;

	pe20_info("%s: OK\n", __func__);

	mutex_unlock(&pe20->access_lock);

	return 0;
}

int pe20_reset_ta_vchr(struct tchg_alg_device *alg)
{
	int ret, chr_volt = 0, ret_value = -1;
	struct tc_pe20 *pe20 = dev_get_drvdata(&alg->dev);
	u32 org_chr_volt = pe20->chr_volt[PE20_NONE];
	u32 org_chr_volt_mivr = pe20->chr_volt_mivr[PE20_NONE];

	pe20_info("%s: starts\n", __func__);

	/* Reset TA's charging voltage */
	ret = ta_dev_set_usb_ctrl_pe20(alg, PE20_NONE);
	if (ret < 0)
		pe20_err("%s:reset TA fail, ret:%d\n",
			__func__, ret);

	/* Check charger's voltage */
	chr_volt = pe20_hal_get_vbus(alg);
	pe20_info("%s: ori_vbus:%d vbus:%d\n", __func__,
		org_chr_volt, chr_volt);

	if (abs(chr_volt - org_chr_volt) <= 1000000) {
		ret_value = 0;
	}

	/* Reset MIVR */
	pe20_hal_set_mivr(alg, false, org_chr_volt_mivr);

	pe20_info("%s: OK\n", __func__);

	return ret;
}

static int pe20_reset_ta_none_check_volt(struct tchg_alg_device *alg)
{
	int ret;
	/* struct tc_pe20 *pe20 = dev_get_drvdata(&alg->dev); */

	pe20_info("%s: starts\n", __func__);

	/* Reset TA's charging voltage */
	ret = ta_dev_set_usb_ctrl_pe20(alg, PE20_NONE);
	if (ret < 0)
		pe20_err("%s:reset TA fail, ret:%d\n",
			__func__, ret);

	pe20_info("%s: OK\n", __func__);

	return ret;
}

static int _pe20_set_ta_vchr(struct tchg_alg_device *alg, int target_volt)
{
	int ret = 0, ret_value = 0;
	struct tc_pe20 *pe20 = dev_get_drvdata(&alg->dev);

	pe20_info("%s: starts\n", __func__);

	/* Not to set chr volt if cable is plugged out */
	if (pe20->is_cable_out_occur && target_volt != PE20_NONE) {
		pe20_err("%s: failed, cable out\n", __func__);
		return -ECABLEOUT;
	}

	pe20->ta_vchr_target = target_volt;
	ret = ta_dev_set_usb_ctrl_pe20(alg, target_volt);
	if (ret < 0) {
		pe20_err("%s: pe20 set target_volt:%d failed, ret = %d\n",
			__func__, pe20->chr_volt[target_volt], ret);
		return -EHAL;
	}

	return ret_value;
}

static int pe20_set_ta_vchr(struct tchg_alg_device *alg, int target_volt)
{
	int ret = 0, ret_value = 0;
	int vchr_before, vchr_after, vchr_delta;
	struct tc_pe20 *pe20 = dev_get_drvdata(&alg->dev);
	u32 chr_volt = pe20->chr_volt[target_volt];
	u32 chr_volt_mivr = pe20->chr_volt_mivr[target_volt];
	u32 org_chr_volt_mivr = pe20->chr_volt_mivr[PE20_NONE];

	pe20_info("%s: starts, target:%d\n", __func__, chr_volt / 1000);

	pe20_hal_set_input_current(alg, true, 1000000);
	pe20_hal_set_mivr(alg, false, org_chr_volt_mivr);

	vchr_before = pe20_hal_get_vbus(alg);
	ret = _pe20_set_ta_vchr(alg, target_volt);
	vchr_after = pe20_hal_get_vbus(alg);

	vchr_delta = abs(vchr_after - chr_volt);
	pe20_err("ret:%d delta:%d %d %d\n",
		 ret, vchr_delta, vchr_after, chr_volt);
	/*
	 * It is successful if VBUS difference to target is
	 * less than 1V.
	 */
	if (vchr_delta < 1000000 && ret == 0) {
		pe20_info("%s: OK, vchr = (%d, %d), vchr_target = %dmV\n", __func__,
			vchr_before / 1000, vchr_after / 1000, chr_volt / 1000);
		/* Set MIVR */
		pe20_hal_set_mivr(alg, true, chr_volt_mivr);
		goto out;
	}


	pe20_info("%s: vchr = (%d, %d), vchr_target = %dmV\n",
		__func__, vchr_before / 1000,
		vchr_after / 1000, chr_volt / 1000);


	if (pe20->is_cable_out_occur)
		ret_value = -ECABLEOUT;
	else
		ret_value = -EHAL;

	pe20_info("%s: failed, vchr_after = %dmV, target_vchr = %dmV, ret_value:%d\n",
		__func__, vchr_after / 1000, chr_volt / 1000, ret_value);

out:
	pe20_hal_set_input_current(alg, false, 1000000);
	return ret_value;
}


static int pe20_detect_ta(struct tchg_alg_device *alg)
{
	int ret = 0;
	/* struct tc_pe20 *pe20 = dev_get_drvdata(&alg->dev); */

	pe20_info("%s: starts\n", __func__);

	/* Disable Normal OVP */
	ret = pe20_hal_enable_vbus_ovp(alg, false);
	if (ret < 0)
		goto err;

	ret = pe20_set_ta_vchr(alg, PE20_9V);
	if (ret < 0) {
		pe20_info("%s: set ta vchr failed, ret:%d\n",
			__func__, ret);
		goto err;
	}

	pe20_info("%s: ret:%d\n", __func__, ret);

	return ret;
err:
	/* Enable Normal OVP */
	pe20_hal_enable_vbus_ovp(alg, true);
	pe20_err("%s: failed, ret = %d\n", __func__, ret);
	return ret;
}

static int __pe20_check_charger(struct tchg_alg_device *alg)
{
	int ret = 0, ret_value = 0;
	struct tc_pe20 *pe20 = dev_get_drvdata(&alg->dev);

	pe20_info("%s\n", __func__);

	if (pe20->is_cable_out_occur)
		goto out;

	ret = pe20_reset_ta_none_check_volt(alg);
	if (ret != 0)
		goto out;

	if (pe20->is_cable_out_occur)
		goto out;

	ret = pe20_detect_ta(alg);
	if (ret < 0)
		goto out;

	pe20->is_fast_chr = true;
	pe20_report_charing_animation(alg);

	pe20_info("%s: OK, state = %d\n",
		__func__, pe20->state);

	return ret;
out:

	if (ret_value == 0)
		ret_value = ALG_TA_NOT_SUPPORT;

	/* Recover Normal state */
	pe20_reset_ta_none_check_volt(alg);

	pe20_info("%s: state:%s, ret:%d, plugout:%d\n", __func__,
		pe20_state_to_str(pe20->state), ret, pe20->is_cable_out_occur);

	return ret_value;
}

static int pe20_leave(struct tchg_alg_device *alg)
{
	int ret = 0;
	struct tc_pe20 *pe20 = dev_get_drvdata(&alg->dev);
	u32 org_chr_volt_mivr = 0;

	pe20_info("%s: starts\n", __func__);

	org_chr_volt_mivr = pe20->chr_volt_mivr[PE20_NONE];

	ret = pe20_set_ta_vchr(alg, PE20_NONE);
	if (ret != 0) {
		pe20_err("%s: failed, state = %d, ret = %d\n",
			__func__, pe20->state, ret);
	}

	ret = pe20_hal_enable_vbus_ovp(alg, true);
	if (ret != 0) {
		pe20_err("%s: enable vbus ovp fai,ret:%d\n",
			__func__, ret);
	}

	pe20_hal_set_mivr(alg, false, org_chr_volt_mivr);
	if (ret != 0) {
		pe20_err("%s:set mivr fail,ret:%d\n",
			__func__, ret);
	}

	pe20_info("%s: OK\n", __func__);
	return ret;
}

static int _pe20_init_algo(struct tchg_alg_device *alg)
{
	int log_level;
	struct tc_pe20 *pe20 = dev_get_drvdata(&alg->dev);

	mutex_lock(&pe20->access_lock);
	if (pe20_hal_init_hardware(alg) != 0) {
		pe20->state = ALG_INIT_FAIL;
		pe20_err("%s:init hw fail\n", __func__);
	} else
		pe20->state = ALG_READY;

	log_level = pe20_hal_get_log_level(alg);
	pe20_info("%s: log_level=%d", __func__, log_level);
	if (log_level > 0)
		pe20_info_level = log_level;
	pe20->ta_vchr_target = PE20_NONE;
	pe20->is_fast_chr = false;

	mutex_unlock(&pe20->access_lock);
	pe20_info("%s config:%d\n", __func__, alg->config);

	return 0;
}

static int _pe20_is_algo_ready(struct tchg_alg_device *alg)
{
	int ret_value, uisoc;
	int alias_type = TC_UNKNOWN;
	int Vbat;
	struct tc_pe20 *pe20 = dev_get_drvdata(&alg->dev);

	pe20 = dev_get_drvdata(&alg->dev);

	mutex_lock(&pe20->access_lock);
	__pm_stay_awake(pe20->suspend_lock);

	alias_type = pe20_hal_get_charger_type(alg);
	if (alias_type == TC_UNKNOWN)
		goto out;

	if (pe20->state != ALG_READY)
		goto out;
		
	Vbat = pe20_hal_get_vbat(alg);
	uisoc = pe20_hal_get_uisoc(alg);
	if (alias_type != TC_DCP) {
		pe20->state = ALG_TA_NOT_SUPPORT;
	} else if (uisoc >= pe20->ta_stop_battery_soc ||
		Vbat > pe20->vbat_threshold) {
		pe20->state = ALG_NOT_READY;
	}
out:
	pe20_info("%s state:%s\n", __func__,
		pe20_state_to_str(pe20->state));

	ret_value = pe20->state;
	__pm_relax(pe20->suspend_lock);
	mutex_unlock(&pe20->access_lock);

	return ret_value;
}

static int __pe20_run(struct tchg_alg_device *alg)
{
	int vchr, uisoc, ichg;
	int ret = 0, ret_value = 0;
	u32 org_chr_volt = 0;
	int ta_vchr_target = 0;
	struct tc_pe20 *pe20 = dev_get_drvdata(&alg->dev);

	ta_vchr_target = pe20->ta_vchr_target;

	org_chr_volt = pe20->chr_volt[ta_vchr_target];

	if (pe20->is_cable_out_occur) {
		ret_value = ALG_TA_NOT_SUPPORT;
		goto out;
	}

	/* PE20 leaves unexpectedly */
	vchr = pe20_hal_get_vbus(alg);
	if (abs(vchr - org_chr_volt) > 1000000) {
		pe20_err("%s: PE20 leave unexpectedly, recheck TA %d %d\n",
			__func__, vchr, org_chr_volt);
		ret = pe20_leave(alg);
		ret_value = ALG_TA_NOT_SUPPORT;
		goto out;
	}

	/* Check SOC & Ichg */
	ichg = pe20_hal_get_ibat(alg);
	uisoc = pe20_hal_get_uisoc(alg);
	if (uisoc > pe20->ta_stop_battery_soc &&
		ichg > 0 && ichg < pe20->ta_ichg_level_threshold) {
		ret = pe20_leave(alg);
		pe20_err("%s: OK, SOC = (%d,%d), ichg:(%d, %d), stop PE20\n", __func__,
			uisoc, pe20->ta_stop_battery_soc,
			ichg, pe20->ta_ichg_level_threshold);
		ret_value = ALG_DONE;
		goto out;
	}

out:

	pe20_info("%s: SOC:%d, Ibat = %d, plugout = %d\n", __func__,
		pe20_hal_get_uisoc(alg), pe20_hal_get_ibat(alg), pe20->is_cable_out_occur);

	pe20_info("%s: ret = %d:%d\n",
		__func__, ret, ret_value);

	return ret_value;
}

static int _pe20_start_algo(struct tchg_alg_device *alg)
{
	int ret = 0, ret_value = 0;
	bool again = false;
	struct tc_pe20 *pe20 = dev_get_drvdata(&alg->dev);

	mutex_lock(&pe20->access_lock);
	__pm_stay_awake(pe20->suspend_lock);

	do {
		pe20_info("%s state:%d %s %d\n", __func__,
			pe20->state,
			pe20_state_to_str(pe20->state),
			again);

		again = false;
		switch (pe20->state) {
		case ALG_READY:
			ret = __pe20_check_charger(alg);
			if (ret == 0) {
				pe20->state = ALG_RUNNING;
				again = true;
			} else if (ret == ALG_TA_CHECKING) {
				pe20->state = ret;
			} else if (ret == ALG_TA_NOT_SUPPORT) {
				pe20->state = ret;
			} else {
				pe20->state = ALG_TA_NOT_SUPPORT;
			}
			break;
		case ALG_RUNNING:
			ret = __pe20_run(alg);
			if (ret == ALG_TA_NOT_SUPPORT) {
				pe20->state = ret;
			} else if (ret == ALG_DONE) {
				pe20->state = ret;
			} /* ignore else. default running */
			break;
		default:
			pe20_err("pe20 unknown state:%d\n", pe20->state);
			pe20->state = ALG_TA_NOT_SUPPORT;
			break;
		}
	} while (again == true);

	ret_value = pe20->state;
	__pm_relax(pe20->suspend_lock);
	mutex_unlock(&pe20->access_lock);

	return ret_value;
}


static bool _pe20_is_algo_running(struct tchg_alg_device *alg)
{
	struct tc_pe20 *pe20 = dev_get_drvdata(&alg->dev);

	if (pe20->state == ALG_RUNNING)
		return true;

	return false;
}

static int _pe20_stop_algo(struct tchg_alg_device *alg)
{
	struct tc_pe20 *pe20 = dev_get_drvdata(&alg->dev);

	mutex_lock(&pe20->access_lock);

	pe20_info("%s %d\n", __func__, pe20->state);
	if (pe20->state == ALG_RUNNING) {
		pe20_leave(alg);
		pe20->state = ALG_READY;
	}
	pe20->is_fast_chr = false;
	mutex_unlock(&pe20->access_lock);

	return 0;
}

static int pe20_full_event(struct tchg_alg_device *alg)
{
	int ret_value = 0;
	struct tc_pe20 *pe20 = dev_get_drvdata(&alg->dev);

	if (pe20->state == ALG_RUNNING) {
		pe20_err("%s evt full\n",  __func__);
		pe20_leave(alg);
		pe20->state = ALG_DONE;
	}

	return ret_value;
}

static int _pe20_notifier_call(struct tchg_alg_device *alg,
			 struct tchg_alg_notify *notify)
{
	int ret_value = 0;
	struct tc_pe20 *pe20 = dev_get_drvdata(&alg->dev);

	pe20_err("%s evt:%d state:%s\n", __func__, notify->evt,
		pe20_state_to_str(pe20->state));

	switch (notify->evt) {
	case EVT_FULL:
		ret_value = pe20_full_event(alg);
		break;
	default:
		ret_value = -EINVAL;
	}
	return ret_value;
}

static int tc_pe20_parse_dt(struct tc_pe20 *pe20,
				struct device *dev)
{
	struct device_node *np = dev->of_node;
	u32 val;
	int length, i;

	/* PE20 */
	pe20->support_pe20_by_switch = of_property_read_bool(np, "support_pe20_by_switch");

	if (of_property_read_u32(np, "ta_ichg_level_threshold", &val) >= 0)
		pe20->ta_ichg_level_threshold = val;
	else {
		pe20_info("use default TA_ICHG_LEAVE_THRESHOLD:%d\n",
			TA_ICHG_LEAVE_THRESHOLD);
		pe20->ta_ichg_level_threshold = TA_ICHG_LEAVE_THRESHOLD;
	}

	if (of_property_read_u32(np, "ta_stop_battery_soc", &val) >= 0)
		pe20->ta_stop_battery_soc = val;
	else {
		pe20_info("use default TA_STOP_BATTERY_SOC:%d\n",
			TA_STOP_BATTERY_SOC);
		pe20->ta_stop_battery_soc = TA_STOP_BATTERY_SOC;
	}

	if (of_property_read_u32(np, "vbat_threshold", &val) >= 0)
		pe20->vbat_threshold = val;
	else {
		pe20_info("turn off vbat_threshold checking:%d\n",
			DISABLE_VBAT_THRESHOLD);
		pe20->vbat_threshold = DISABLE_VBAT_THRESHOLD;
	}

	length = of_property_count_elems_of_size(np, "chr_volt", sizeof(u32));
	if (length <= 0) {
		pe20_err("%s: chr_volt dts undefined, check dtsi config\n", __func__);
		return -EINVAL;
	}

	pe20->chr_volt = devm_kzalloc(dev, length * sizeof(u32), GFP_KERNEL);
	pe20->chr_volt_mivr = devm_kzalloc(dev, length * sizeof(u32), GFP_KERNEL);
	pe20->chr_volt_input_cur = devm_kzalloc(dev, length * sizeof(u32), GFP_KERNEL);
	pe20->chr_volt_chg_cur = devm_kzalloc(dev, length * sizeof(u32), GFP_KERNEL);
	if (IS_ERR_OR_NULL(pe20->chr_volt) ||
		IS_ERR_OR_NULL(pe20->chr_volt_mivr) ||
		IS_ERR_OR_NULL(pe20->chr_volt_input_cur) ||
		IS_ERR_OR_NULL(pe20->chr_volt_chg_cur)) {
		pe20_err("%s: cann't malloc, exit....\n", __func__);
		return -ENOMEM;
	}

	if (of_property_read_u32_array(np, "chr_volt", pe20->chr_volt, length) < 0) {
		pe20_err("%s: failed parse chr_volt\n", __func__);
		return -EINVAL;
	}

	if (of_property_read_u32_array(np, "chr_volt_mivr", pe20->chr_volt_mivr, length)) {
		pe20_err("%s: failed parse chr_volt_mivr\n", __func__);
		return -EINVAL;
	}

	if (of_property_read_u32_array(np, "chr_volt_input_cur", pe20->chr_volt_input_cur, length)) {
		pe20_err("%s: failed parse chr_volt_input_cur\n", __func__);
		return -EINVAL;
	}

	if (of_property_read_u32_array(np, "chr_volt_chg_cur", pe20->chr_volt_chg_cur, length)) {
		pe20_err("%s: failed parse chr_volt_chg_cur\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < length; i++) {
		pe20_info("%s:chr_volt[%d] = %d, "
			"chr_volt_mivr[%d] = %d, "
			"chr_volt_input_cur[%d] = %d, "
			"chr_volt_chg_cur[%d] = %d\n", __func__,
			i, pe20->chr_volt[i],
			i, pe20->chr_volt_mivr[i],
			i, pe20->chr_volt_input_cur[i],
			i, pe20->chr_volt_chg_cur[i]);
	}

	return 0;
}

int _pe20_get_prop(struct tchg_alg_device *alg, enum tchg_alg_props s, int *value)
{
	struct tc_pe20 *pe20 = dev_get_drvdata(&alg->dev);

	pe20_info("%s: %d\n", __func__, s);

	switch (s) {
	case ALG_INPUT_CURRENT: //uA
		*value = pe20->chr_volt_input_cur[pe20->ta_vchr_target];
		break;
	case ALG_CHG_CURRENT: //uA
		*value = pe20->chr_volt_chg_cur[pe20->ta_vchr_target];
		break;
	case ALG_MAX_VBUS: //uV
		*value = pe20->chr_volt[pe20->ta_vchr_target];
		break;
	case ALG_IS_FAST_CHR:
		*value = pe20->is_fast_chr;
		break;
	case ALG_ADAPTER_CAPACITY:
		*value = 18;
		break;
	default:
		break;
	}

	return 0;
}

int _pe20_set_prop(struct tchg_alg_device *alg,
		enum tchg_alg_props s, int value)
{
	/* struct tc_pe20 *pe20 = dev_get_drvdata(&alg->dev); */

	pe20_info("%s %d %d\n", __func__, s, value);

	switch (s) {
	case ALG_LOG_LEVEL:
		pe20_info_level = value;
		break;
	default:
		break;
	}

	return 0;
}

static int _pe20_plugout_reset(struct tchg_alg_device *alg)
{
	pe20_info("%s\n", __func__);
	return pe20_plugout_reset(alg);
}


static struct tchg_alg_ops pe20_alg_ops = {
	.init_algo = _pe20_init_algo,
	.is_algo_ready = _pe20_is_algo_ready,
	.start_algo = _pe20_start_algo,
	.is_algo_running = _pe20_is_algo_running,
	.stop_algo = _pe20_stop_algo,
	.notifier_call = _pe20_notifier_call,
	.get_prop = _pe20_get_prop,
	.set_prop = _pe20_set_prop,
	.plugout_reset = _pe20_plugout_reset,
};

static int tc_pe20_probe(struct platform_device *pdev)
{
	struct tc_pe20 *pe20 = NULL;
	int ret = 0;

	pr_err("%s: starts\n", __func__);

	pe20 = devm_kzalloc(&pdev->dev, sizeof(*pe20), GFP_KERNEL);
	if (!pe20)
		return -ENOMEM;
	platform_set_drvdata(pdev, pe20);
	pe20->pdev = pdev;

	pe20->suspend_lock = wakeup_source_register(NULL, "PE20 suspend wakelock");

	mutex_init(&pe20->access_lock);
	mutex_init(&pe20->cable_out_lock);

	pe20->state = ALG_NOT_READY;
	ret = tc_pe20_parse_dt(pe20, &pdev->dev);
	if (ret < 0) {
		pr_err("%s: parse dt failed, ret:%d\n", __func__, ret);
		return ret;
	}

	pe20->alg = tchg_alg_device_register("pe20", &pdev->dev,
					pe20, &pe20_alg_ops, NULL);

	pr_err("%s: done\n", __func__);

	return 0;
}

static int tc_pe20_remove(struct platform_device *dev)
{
	return 0;
}

static void tc_pe20_shutdown(struct platform_device *dev)
{

}

static const struct of_device_id pe20_of_match[] = {
	{.compatible = "transsion,pe20_charger",},
	{},
};

MODULE_DEVICE_TABLE(of, pe20_of_match);

static struct platform_driver pe20_driver = {
	.probe = tc_pe20_probe,
	.remove = tc_pe20_remove,
	.shutdown = tc_pe20_shutdown,
	.driver = {
		   .name = "pe20",
		   .of_match_table = pe20_of_match,
	},
};
module_platform_driver(pe20_driver);

MODULE_DESCRIPTION("TRANSSION PE20 algorithm Driver");
MODULE_LICENSE("GPL");
