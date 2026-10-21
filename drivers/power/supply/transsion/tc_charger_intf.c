// SPDX-License-Identifier: GPL-2.0-only
#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
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
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/reboot.h>
#include <linux/mfd/mt6397/core.h>/* PMIC MFD core header */
#include <linux/regmap.h>
#include <linux/of_platform.h>

#include "tc_charger.h"

int tc_charger_check_psy_ptr(struct power_supply **psy, const char *name)
{
	if (IS_ERR_OR_NULL(*psy)) {
		*psy = power_supply_get_by_name(name);
		if (IS_ERR_OR_NULL(*psy)) {
			tchr_err("%s Couldn't get psy(%s)\n", __func__, name);
			return -EINVAL;
		}

	}

	return 0;
}

int tc_charger_check_tran_dev_ptr(struct tran_device **dev, const char *name)
{
	if (IS_ERR_OR_NULL(*dev)) {
		*dev = tran_get_by_name(name);
		if (IS_ERR_OR_NULL(*dev)) {
			tchr_err("%s Couldn't get dev(%s)\n", __func__, name);
			return -EINVAL;
		}
	}

	return 0;
}

int get_ibat(struct tc_charger *info)
{
	int ret = 0;
	int ibat = 0;
	struct tc_data *data = info->data;

	if (info == NULL)
		return -EINVAL;
	ret = charger_dev_get_ibat(data->chg1_dev, &ibat);
	if (ret < 0)
		tchr_err("%s: get ibat failed: %d\n", __func__, ret);

	return ibat / 1000;
}

int get_ibus(struct tc_charger *info)
{
	int ret = 0;
	int ibus = 0;
	struct tc_data *data = info->data;

	if (info == NULL)
		return -EINVAL;
	ret = charger_dev_get_ibus(data->chg1_dev, &ibus);
	if (ret < 0)
		tchr_err("%s: get ibus failed: %d\n", __func__, ret);

	return ibus / 1000;
}

bool is_charger_exist(struct tc_charger *info)
{
	union power_supply_propval prop;
	struct tc_data *data = info->data;
	int ret = 0;
	bool is_exist = false;

	ret = tc_charger_check_psy_ptr(&data->chg_psy, "charger");
	if (ret < 0) {
		tchr_info("%s Couldn't get chg_psy\n", __func__);
		goto out;
	}

	ret = power_supply_get_property(data->chg_psy,
			POWER_SUPPLY_PROP_ONLINE, &prop);
	if (ret < 0) {
		tchr_err("get chg online failed\n");
		goto out;
	}
	is_exist = !!prop.intval;

out:
	tchr_info("%s:%d\n", __func__,ret);
	return is_exist;
}

int get_charger_temperature(struct tc_charger *info,
	struct charger_device *chg)
{
	int ret = 0;
	int tchg_min = 0, tchg_max = 0;

	if (info == NULL)
		return 0;

	ret = charger_dev_get_temperature(chg, &tchg_min, &tchg_max);
	if (ret < 0)
		tchr_err("%s: get temperature failed: %d\n", __func__, ret);
	else
		ret = (tchg_max + tchg_min) / 2;

	tchr_debug("%s:%d\n", __func__,ret);
	return ret;
}

int get_charger_charging_current(struct tc_charger *info,
	struct charger_device *chg)
{
	int ret = 0;
	int olduA = 0;

	if (info == NULL)
		return 0;
	ret = charger_dev_get_charging_current(chg, &olduA);
	if (ret < 0)
		tchr_err("%s: get charging current failed: %d\n", __func__, ret);
	else
		ret = olduA;

	tchr_debug("%s:%d\n", __func__,ret);
	return ret;
}

int get_charger_input_current(struct tc_charger *info,
	struct charger_device *chg)
{
	int ret = 0;
	int olduA = 0;

	if (info == NULL)
		return 0;
	ret = charger_dev_get_input_current(chg, &olduA);
	if (ret < 0)
		tchr_err("%s: get input current failed: %d\n", __func__, ret);
	else
		ret = olduA;

	tchr_debug("%s:%d\n", __func__,ret);
	return ret;
}

int get_charger_zcv(struct tc_charger *info,
	struct charger_device *chg)
{
	int ret = 0;
	int zcv = 0;

	if (info == NULL)
		return 0;

	ret = charger_dev_get_zcv(chg, &zcv);
	if (ret < 0)
		tchr_err("%s: get charger zcv failed: %d\n", __func__, ret);
	else
		ret = zcv;
	tchr_debug("%s:%d\n", __func__,ret);
	return ret;
}

int set_icon_disappeared(struct tc_charger *info, bool enable)
{
	int ret = 0;
	union power_supply_propval status;
	struct tc_data *data = info->data;

	ret = tc_charger_check_psy_ptr(&data->chg_psy, "charger");
	if (ret < 0) {
		tchr_info("%s Couldn't get chg_psy\n", __func__);
		goto out;
	}

	status.intval = enable ?
			POWER_SUPPLY_STATUS_NOT_CHARGING : -1;
	ret = power_supply_set_property(data->chg_psy,
			POWER_SUPPLY_PROP_STATUS, &status);
	if (ret < 0) {
		tchr_err("%s: failed(%d)\n", __func__, ret);
	}

out:
	tchr_info("%s:%d\n", __func__,ret);

	return ret;
}
