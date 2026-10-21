
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
#include "tc_charger_class.h"
#include "tc_misc_intf.h"
#include "tc_adapter_class.h"
#include "tc_charger.h"

struct pd_hal {
	struct charger_device *chg1_dev;
	struct charger_device *chg2_dev;
	struct tadapter_device *adapter;
};

int pd_get_chg_dev_info(struct tchg_alg_device *alg,
		enum tran_common_prop prop, int *value)
{
	int ret = 0;
	union com_propval val = {0, };
	struct tran_device *tc_chg_dev = NULL;

	tc_chg_dev = tran_get_by_name("tc_charger");
	ret = tran_dev_get_prop(tc_chg_dev, prop, &val);
	if (ret < 0) {
		pd_err("%s: get prop(%d) failed(%d)\n", __func__, prop, ret);
		return ret;
	}
	*value = val.intval;
	return ret;

}

int pd_hal_init_hardware(struct tchg_alg_device *alg)
{
	struct tc_pd *pd;
	struct pd_hal *hal;


	pd_dbg("%s\n", __func__);
	if (alg == NULL) {
		pd_err("%s: alg is null\n", __func__);
		return -EINVAL;
	}

	pd = dev_get_drvdata(&alg->dev);
	hal = tchg_alg_dev_get_drv_hal_data(alg);
	if (hal == NULL) {
		hal = devm_kzalloc(&pd->pdev->dev, sizeof(*hal), GFP_KERNEL);
		if (!hal)
			return -ENOMEM;
		tchg_alg_dev_set_drv_hal_data(alg, hal);
	}

	hal->chg1_dev = get_charger_by_name("primary_chg");
	if (hal->chg1_dev)
		pd_dbg("%s: Found primary charger\n", __func__);
	else {
		pd_err("%s: Error : can't find primary charger\n",
			__func__);
		return -ENODEV;
	}

	hal->chg2_dev = get_charger_by_name("secondary_chg");
	if (hal->chg2_dev)
		pd_dbg("%s: Found secondary charger\n", __func__);
	else
		pd_err("%s: Error : can't find secondary charger\n",
			__func__);

	hal->adapter = get_tadapter_by_name("pd_adapter");
	if (hal->adapter)
		pd_dbg("%s: Found pd adapter\n", __func__);
	else {
		pd_err("%s: Error : can't find pd adapter\n",
			__func__);
		return -ENODEV;
	}

	return 0;
}

int pd_hal_is_pd_adapter_ready(struct tchg_alg_device *alg)
{
	struct tc_pd *pd;
	struct pd_hal *hal;
	int type, time;
	int alias_type = tc_get_alias_type();
	if (alg == NULL) {
		pd_err("%s: alg is null\n", __func__);
		return -EINVAL;
	}

	pd = dev_get_drvdata(&alg->dev);
	hal = tchg_alg_dev_get_drv_hal_data(alg);
	type = tadapter_dev_get_property(hal->adapter, PD_TYPE_TIMEOUT);
	time = tadapter_dev_get_property(hal->adapter, PD_ATTACHED_TIME);
	pr_info("%s type:%d  time:%d\n", __func__, type, time);

	if (alias_type == TC_SDP && time <= 3)
		return ALG_TA_CHECKING;
	if (type == TC_PD_CONNECT_PE_READY_SNK ||
		type == TC_PD_CONNECT_PE_READY_SNK_PD30 ||
		type == TC_PD_CONNECT_PE_READY_SNK_APDO)
		return ALG_READY;
	else if (type == TC_PD_CONNECT_NONE)
		return ALG_TA_CHECKING;
	else 
		return ALG_TA_NOT_SUPPORT;
}

int pd_hal_get_adapter_cap(struct tchg_alg_device *alg, struct pd_power_cap *cap)
{
	struct tc_pd *pd;
	struct pd_hal *hal;
	struct tadapter_power_cap acap = {0};
	int i, ret;

	if (alg == NULL) {
		pd_err("%s: alg is null\n", __func__);
		return -EINVAL;
	}

	pd = dev_get_drvdata(&alg->dev);
	hal = tchg_alg_dev_get_drv_hal_data(alg);

	ret = tadapter_dev_get_cap(hal->adapter, TC_PD, &acap);
	cap->selected_cap_idx = acap.selected_cap_idx;
	cap->nr = acap.nr;
	cap->pdp = acap.pdp;
	for (i = 0; i < 10; i++) {
		cap->pwr_limit[i] = acap.pwr_limit[i];
		cap->min_mv[i] = acap.min_mv[i];
		cap->max_mv[i] = acap.max_mv[i];
		cap->ma[i] = acap.ma[i];
		cap->maxwatt[i] = acap.maxwatt[i];
		cap->minwatt[i] = acap.minwatt[i];
		cap->type[i] = acap.type[i];
		cap->info[i] = acap.info[i];
	}
	return ret;
}

int pd_hal_get_vbus(struct tchg_alg_device *alg)
{
	return tc_get_vbus() * 1000;
}

int pd_hal_get_ibat(struct tchg_alg_device *alg)
{
	return tc_get_battery_current() * 1000;
}

int pd_hal_get_mivr_state(struct tchg_alg_device *alg,
	enum tchg_idx chgidx, bool *in_loop)
{
	struct pd_hal *hal;

	if (alg == NULL)
		return -EINVAL;

	hal = tchg_alg_dev_get_drv_hal_data(alg);
	if (chgidx == CHG1)
		charger_dev_get_mivr_state(hal->chg1_dev, in_loop);
	else if (chgidx == CHG2)
		charger_dev_get_mivr_state(hal->chg2_dev, in_loop);
	return 0;
}

int pd_hal_get_mivr(struct tchg_alg_device *alg,
	enum tchg_idx chgidx, int *mivr1)
{
	struct pd_hal *hal;

	if (alg == NULL)
		return -EINVAL;

	hal = tchg_alg_dev_get_drv_hal_data(alg);

	charger_dev_get_mivr(hal->chg1_dev, mivr1);

	if (chgidx == CHG1)
		charger_dev_get_mivr(hal->chg1_dev, mivr1);
	else if (chgidx == CHG2)
		charger_dev_get_mivr(hal->chg2_dev, mivr1);

	return 0;
}

int pd_hal_enable_vbus_ovp(struct tchg_alg_device *alg, bool enable)
{
	enum charger_voltage_max idx;

	idx = CHARGER_VOLTAGE_SWITCH_FC;

	tc_chg_switch_vbus_ovp(idx, PDC_VOTER, !enable);

	return 0;
}

int pd_hal_set_mivr(struct tchg_alg_device *alg, bool enable, int uV)
{
	int ret = 0;
	struct votable *chg1_mivr_vote = NULL;
	struct tc_pd *pd;
	
	pd = dev_get_drvdata(&alg->dev);
	chg1_mivr_vote = find_votable("chg1_mivr");
	if (chg1_mivr_vote == NULL)
		return -ENODEV;
	
	ret = vote(chg1_mivr_vote, PDC_VOTER, enable, uV);
	if (ret < 0) {
		pd_err("pd set mivr failed, ret = %d\n", ret);
		return ret;
	}
	if (enable)
		pd->mivr = uV;

	return ret;
}

int pd_hal_get_input_current(struct tchg_alg_device *alg,
	enum tchg_idx chgidx, u32 *ua)
{
	struct pd_hal *hal;

	if (alg == NULL)
		return -EINVAL;

	hal = tchg_alg_dev_get_drv_hal_data(alg);
	if (chgidx == CHG1)
		charger_dev_get_input_current(hal->chg1_dev,
		ua);
	else if (chgidx == CHG2)
		charger_dev_get_input_current(hal->chg2_dev,
		ua);

	return 0;
}

int pd_hal_set_input_current(struct tchg_alg_device *alg,
	bool enable, u32 uA)
{
	int ret = 0;
	struct votable *total_aicr_vote = NULL;
	struct tc_pd *pd;
	
	pd = dev_get_drvdata(&alg->dev);
	total_aicr_vote = find_votable("total_aicr");
	if (total_aicr_vote == NULL)
		return -ENODEV;
	
	ret = vote(total_aicr_vote, PDC_VOTER, enable, uA);
	if (ret < 0) {
		pd_err("pdc set aicr failed, ret = %d\n", ret);
		return ret;
	}
	if (enable)
		pd->aicr = uA;

	return ret;
}

int pd_hal_get_vbat(struct tchg_alg_device *alg)
{
	return tc_get_battery_voltage() * 1000;
}

int pd_hal_set_adapter_cap(struct tchg_alg_device *alg,
	int mV, int mA)
{
	struct pd_hal *hal;

	if (alg == NULL)
		return -EINVAL;

	pd_dbg("%s %d %d\n", __func__, mV, mA);
	hal = tchg_alg_dev_get_drv_hal_data(alg);
	return tadapter_dev_set_cap(hal->adapter, TC_PD, mV, mA);
}

int pd_hal_do_charger_notify(struct tchg_alg_device *alg,
	int event)
{
	struct pd_hal *hal;
	if(alg == NULL)
		return -EINVAL;

	hal = tchg_alg_dev_get_drv_hal_data(alg);
	return charger_dev_notify(hal->chg1_dev, event);
}

int pd_hal_get_uisoc(struct tchg_alg_device *alg)
{
	return tc_get_uisoc();
}

int pd_hal_get_log_level(struct tchg_alg_device *alg)
{
	int value = PD_DEBUG_LEVEL;

	pd_get_chg_dev_info(alg, TRAN_PROP_LOG_LEVEL, &value);

	return value;
}

