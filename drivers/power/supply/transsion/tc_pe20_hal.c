// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Transsion Inc.
 */
#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/power_supply.h>
#include <linux/time.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include "tc_pe20.h"
#include "tc_charger_class.h"
#include "tc_charger.h"

struct pe20_hal {
	struct charger_device *chg1_dev;
	struct charger_device *chg2_dev;
	struct tran_device *usb_control_dev;
};

int pe20_get_chg_dev_info(struct tchg_alg_device *alg,
		enum tran_common_prop prop, int *value)
{
	int ret = 0;
	union com_propval val = {0, };
	struct tran_device *tc_chg_dev = NULL;

	tc_chg_dev = tran_get_by_name("tc_charger");
	ret = tran_dev_get_prop(tc_chg_dev, prop, &val);
	if (ret < 0) {
		pe20_err("%s: get prop(%d) failed(%d)\n", __func__, prop, ret);
		return ret;
	}
	*value = val.intval;
	return ret;

}

int pe20_hal_init_hardware(struct tchg_alg_device *alg)
{
	struct tc_pe20 *pe20;
	struct pe20_hal *hal;

	pe20_dbg("%s\n", __func__);
	if (alg == NULL) {
		pe20_err("%s: alg is null\n", __func__);
		return -EINVAL;
	}

	pe20 = dev_get_drvdata(&alg->dev);
	hal = tchg_alg_dev_get_drv_hal_data(alg);
	if (hal == NULL) {
		hal = devm_kzalloc(&pe20->pdev->dev, sizeof(*hal), GFP_KERNEL);
		if (!hal)
			return -ENOMEM;
		tchg_alg_dev_set_drv_hal_data(alg, hal);
	}

	hal->chg1_dev = get_charger_by_name("primary_chg");
	if (hal->chg1_dev)
		pe20_dbg("%s: Found primary charger\n", __func__);
	else {
		pe20_err("%s: Error : can't find primary charger\n",
			__func__);
		return -ENODEV;
	}

	hal->chg2_dev = get_charger_by_name("secondary_chg");
	if (hal->chg2_dev)
		pe20_dbg("%s: Found secondary charger\n", __func__);
	else
		pe20_err("%s: Error : can't find secondary charger\n",
			__func__);

	return 0;
}

int pe20_hal_get_uisoc(struct tchg_alg_device *alg)
{
	return tc_get_uisoc();
}

int pe20_hal_get_charger_type(struct tchg_alg_device *alg)
{
	return tc_get_alias_type();
}

int pe20_hal_set_mivr(struct tchg_alg_device *alg, bool enable, int uV)
{
	int ret = 0;
	struct votable *chg1_mivr_vote = NULL;

	chg1_mivr_vote = find_votable("chg1_mivr");
	if (chg1_mivr_vote == NULL)
		return -ENODEV;
	
	ret = vote(chg1_mivr_vote, PE20_VOTER, enable, uV);
	if (ret < 0) {
		pe20_err("pe20 set mivr failed, ret = %d\n", ret);
	}

	return ret;
}

int pe20_hal_get_charger_cnt(struct tchg_alg_device *alg)
{
	struct pe20_hal *hal;
	int cnt = 0;

	if (alg == NULL)
		return -EINVAL;

	hal = tchg_alg_dev_get_drv_hal_data(alg);
	if (hal->chg1_dev != NULL)
		cnt++;
	if (hal->chg2_dev != NULL)
		cnt++;

	return cnt;
}

int pe20_hal_get_vbus(struct tchg_alg_device *alg)
{
	return tc_get_vbus() * 1000;
}

int pe20_hal_get_vbat(struct tchg_alg_device *alg)
{
	return tc_get_battery_voltage() * 1000;
}

int pe20_hal_get_ibat(struct tchg_alg_device *alg)
{
	return tc_get_battery_current() * 1000;
}

int pe20_hal_get_mivr_state(struct tchg_alg_device *alg,
	enum tchg_idx chgidx, bool *in_loop)
{
	struct pe20_hal *hal;

	if (alg == NULL)
		return -EINVAL;

	hal = tchg_alg_dev_get_drv_hal_data(alg);
	if (chgidx == CHG1 && hal->chg1_dev != NULL)
		charger_dev_get_mivr_state(hal->chg1_dev, in_loop);
	else if (chgidx == CHG2 && hal->chg2_dev != NULL)
		charger_dev_get_mivr_state(hal->chg2_dev, in_loop);

	return 0;
}

int pe20_hal_get_charger_current_limit(struct tchg_alg_device *alg)
{
	int value = DEFAULT_ICHG;

	pe20_get_chg_dev_info(alg, TRAN_PROP_CHARGER_CURRENT_LIMIT, &value);

	return value;
}

int pe20_hal_set_input_current(struct tchg_alg_device *alg, bool enable, u32 uA)
{
	int ret = 0;
	struct votable *total_aicr_vote = NULL;

	total_aicr_vote = find_votable("total_aicr");
	if (total_aicr_vote == NULL)
		return -ENODEV;
	
	ret = vote(total_aicr_vote, PE20_VOTER, enable, uA);
	if (ret < 0) {
		pe20_err("pe20 set aicr failed, ret = %d\n", ret);
	}

	return ret;
}

int pe20_hal_get_input_current(struct tchg_alg_device *alg,
	enum tchg_idx chgidx, u32 *uA)
{
	struct pe20_hal *hal;
	int ret;

	if (alg == NULL)
		return -EINVAL;

	hal = tchg_alg_dev_get_drv_hal_data(alg);
	if (chgidx == CHG1 && hal->chg1_dev != NULL)
		ret = charger_dev_get_input_current(hal->chg1_dev, uA);
	if (chgidx == CHG2 && hal->chg2_dev != NULL)
		ret = charger_dev_get_input_current(hal->chg2_dev, uA);
	pr_notice("%s idx:%d %d\n", __func__, chgidx, *uA);
	return 0;
}

int pe20_hal_enable_vbus_ovp(struct tchg_alg_device *alg, bool enable)
{
	enum charger_voltage_max idx;

	idx = CHARGER_VOLTAGE_SWITCH_FC;

	tc_chg_switch_vbus_ovp(idx, PE20_VOTER, !enable);

	return 0;
}

int pe20_hal_get_log_level(struct tchg_alg_device *alg)
{
	int value = PE20_DEBUG_LEVEL;

	pe20_get_chg_dev_info(alg, TRAN_PROP_LOG_LEVEL, &value);

	return value;
}

struct timespec64 ptime[13];
static int cptime[13][2];

static int dtime(int i)
{
    struct timespec64 time;

    time = timespec64_sub(ptime[i], ptime[i-1]);
    return time.tv_nsec/1000000;
}

int pe20_set_current_pattern(struct tchg_alg_device *alg, int uV)
{
	int value;
	int i, j = 0;
	int flag;
	int retry_cnt = 0;
	int retry_cnt_max = 3;
	int ret = 0;
	int chg_type;
	ktime_t ktime_now;
	
retry:
	ret = 0;
	j = 0;
	mdelay(20);
	value = (uV - 5500000) / 500000;
	
	pe20_hal_set_input_current(alg, true, 0);
	mdelay(150);
	
	ktime_now = ktime_get_boottime();
	ptime[j++] = ktime_to_timespec64(ktime_now);
	for (i = 4; i >= 0; i--) {
		chg_type = tc_get_charger_type();
		if (chg_type == POWER_SUPPLY_TYPE_UNKNOWN) {
			pe20_err("pe20 chg unknown, return!");
			ret = -EINVAL;
			goto out;
		}
		flag = value & (1 << i);
		if (flag == 0) {
			pe20_hal_set_input_current(alg, true, 800000);
			mdelay(PEOFFTIME);
			ktime_now = ktime_get_boottime();
			ptime[j] = ktime_to_timespec64(ktime_now);
			cptime[j][0] = PEOFFTIME;
			cptime[j][1] = dtime(j);
			if (cptime[j][1] < 30 || cptime[j][1] > 65) {
				ret = -EINVAL;
				goto out_retry;
	    		}
			j++;
			pe20_hal_set_input_current(alg, true, 0);
	        	mdelay(PEONTIME);
	        	ktime_now = ktime_get_boottime();
	        	ptime[j] = ktime_to_timespec64(ktime_now);
	        	cptime[j][0] = PEONTIME;
	        	cptime[j][1] = dtime(j);
	        	if (cptime[j][1] < 90 || cptime[j][1] > 115) {
				ret = -EINVAL;
				goto out_retry;
	        	}
	        	j++;
		} else {
			pe20_hal_set_input_current(alg, true, 800000);
			mdelay(PEONTIME);
			ktime_now = ktime_get_boottime();
			ptime[j] = ktime_to_timespec64(ktime_now);
			cptime[j][0] = PEONTIME;
			cptime[j][1] = dtime(j);
			if (cptime[j][1] < 90 || cptime[j][1] > 115) {
				ret = -EINVAL;
				goto out_retry;
			}
			j++;
			pe20_hal_set_input_current(alg, true, 0);
			mdelay(PEOFFTIME);
			ktime_now = ktime_get_boottime();
			ptime[j] = ktime_to_timespec64(ktime_now);
			cptime[j][0] = PEOFFTIME;
			cptime[j][1] = dtime(j);
			if (cptime[j][1] < 30 || cptime[j][1] > 65) {
				ret = -EINVAL;
				goto out_retry;
			}
			j++;
		}
	}
	
	pe20_hal_set_input_current(alg, true, 800000);
	mdelay(200);
	ktime_now = ktime_get_boottime();
	ptime[j] = ktime_to_timespec64(ktime_now);
	cptime[j][0] = 200;
	cptime[j][1] = dtime(j);
	if (cptime[j][1] < 180 || cptime[j][1] > 240) {
		ret = -EINVAL;
		goto out_retry;
	}
	j++;
	
	chg_type = tc_get_charger_type();
	if (chg_type == POWER_SUPPLY_TYPE_UNKNOWN) {
		pe20_err("pe20 chg unknown, return!");
		ret = -EINVAL;
		goto out;
	}
	pe20_hal_set_input_current(alg, true, 0);
	mdelay(150);
	pe20_hal_set_input_current(alg, false, 0);
	mdelay(150);

out_retry:
	if (ret< 0 && ++retry_cnt < retry_cnt_max)
		goto retry;
out:
	return ret;
}

int pe20_reset_ta(struct tchg_alg_device *alg)
{
	pe20_err("enter!\n");

	pe20_hal_set_input_current(alg, true, 0);
	msleep(250);//250ms
	pe20_hal_set_input_current(alg, false, 0);

	return 0;
}

int ta_dev_set_usb_ctrl_pe20(struct tchg_alg_device *alg, int value)
{
	int ret = 0;
	int uV;
	struct tc_pe20 *pe20;
        struct pe20_hal *hal;
	struct votable *chg1_aicr_vote = NULL;

        if (alg == NULL)
                return -EINVAL;

	pe20 = dev_get_drvdata(&alg->dev);
        hal = tchg_alg_dev_get_drv_hal_data(alg);

	uV = pe20->chr_volt[value];

	if (value == PE20_NONE) {
		if (pe20->support_pe20_by_switch) {
			ret = charger_dev_reset_ta(hal->chg1_dev);
		} else {
			ret = pe20_reset_ta(alg);
		}
		goto out;
	}

	if (pe20->support_pe20_by_switch) {
		ret = charger_dev_send_ta20_current_pattern(
					hal->chg1_dev, uV);
	} else {
		ret = pe20_set_current_pattern(alg, uV);
	}
out:
	if (pe20->support_pe20_by_switch) {
		chg1_aicr_vote = find_votable("chg1_aicr");
		if (chg1_aicr_vote == NULL)
			return -ENODEV;
		rerun_election(chg1_aicr_vote);
	}
	return ret;
}

