// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#define pr_fmt(fmt)  "[PID_CHG] %s:" fmt, __func__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/power_supply.h>
#include "tc_charger.h"
#include "tc_voter.h"

#define INVALID_TEMP				-1000
#define PID_CHG_EFFICIENCY			90
#define PID_CHG_MEASURE_SYS_POWER_AVG_TIMES	10
#define PID_DELTA_CUR_MAX_UA 500000

enum pid_para {
	KP,
	KI,
	KD,
	PID_MAX,
};

enum pid_delta_temp {
	PID_CHG_POWER_LOW = 0,
	PID_CHG_POWER_MID1,
	PID_CHG_POWER_MID2,
	PID_CHG_POWER_MID3,
	PID_CHG_POWER_HIGH,
	PID_CHG_POWER_MAX,
};

enum mtk_target_temp {
	PID_TARGET_TEMP_LOW = 0,
	PID_TARGET_TEMP_MID1,
	PID_TARGET_TEMP_MID2,
	PID_TARGET_TEMP_MID3,
	PID_TARGET_TEMP_HIGH,
	PID_TARGET_TEMP_MAX,
};

struct pid_chg_info {
	struct platform_device *pdev;
	struct device *dev;
	struct mutex pid_lock;
	struct tran_properties pid_props;
	struct tran_device *pid_dev;
	struct tran_device *tc_lcd;
	struct delayed_work temp_update_work;
	struct notifier_block pid_screen_notifier;

	struct wakeup_source *pid_wakelock;
	wait_queue_head_t  wait_que;
	struct timespec64 prev_time;
	struct timespec64 curr_time;
	bool screen_on;
	bool plug_in;
	bool pid_thread_timeout;
	bool support_ibus_measure;
	int polling_interval;
	int ibus_measure_power_update_time;
	int power_update_time;
	int power_meas_current;
	int power_level[PID_CHG_POWER_MAX];
	int power_delta_temp[PID_CHG_POWER_MAX];
	int ambient_temp[PID_TARGET_TEMP_MAX];
	int target_temp_array[PID_TARGET_TEMP_MAX];
	int pid_array[PID_MAX];
	int prev_chg_cur;
	int curr_chg_cur;
	int prev_temp;
	int curr_temp;
	int target_temp;
	int orignal_target_temp;
	int pterm;
	int iterm;
	int dterm;
	int unsteady_cnt;
	int ignore_next_polling;
	int steady_cycle;
	int max_steady_target_delta_temp;
	int max_steady_delta_temp;
	int normal_steady_delta_temp;
	int max_unsteady_cnt;
	int target_temp_gap;
	int max_target_temp;
	int delta_current_max;
	int fake_orignal_target_temp;
};

struct meas_sys_info {
	int vbus;
	int ibus;
	int vbat;
	int ibat;
	int sys_power;
};

#define PRECISION_ENHANCE	5
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

static int pid_chg_find_closest_index(const unsigned int *array,
		unsigned int num,
		unsigned int value)
{
	unsigned int i;

	for (i = 0; i < num; i++) {
		if ((i == 0 && value <= array[i]) ||
			(i == num - 1 && value >= array[i]))
			return i;
		if (array[i] <= value && array[i + 1] > value) {
			i = ((value - array[i]) <= (array[i + 1] - value)) ? i : (i + 1);
			return i;
		}
	}

	return 0;
}

static ssize_t pid_set_parameter(struct device* dev,
	struct device_attribute *attr, const char* buf, size_t len)
{
	int databuf[3];
	struct pid_chg_info *info = dev_get_drvdata(dev);

	if (buf == NULL || len == 0)
		return len;

    	sscanf(buf, "%d %d %d", &databuf[0], &databuf[1], &databuf[2]);
	info->pid_array[KP] = databuf[0];
	info->pid_array[KI] = databuf[1];
	info->pid_array[KD] = databuf[2];
	
	pr_info("update pid parameter [KP](%d), [KI](%d), [KD](%d)\n",
	       	info->pid_array[KP], info->pid_array[KI], info->pid_array[KD]);

	return len;
}

static ssize_t pid_get_parameter(struct device* dev,
	struct device_attribute *attr, char* buf)
{
	struct pid_chg_info *info = dev_get_drvdata(dev);


	pr_info("get pid parameter [KP](%d), [KI](%d), [KD](%d)\n",
	       	info->pid_array[KP], info->pid_array[KI], info->pid_array[KD]);
	return sprintf(buf, "[KP](%d),[KI](%d),[KD](%d)\n",
	       	info->pid_array[KP], info->pid_array[KI], info->pid_array[KD]);
}

enum debug_node {
	POWER_LEVEL,
	POWER_DELTA_TEMP,
	POWER_UPDATE_TIME,
	POWER_MEAS_CURRENT,
	TARGET_TEMP_GAP,
	STEADY_CYCLE,
	MAX_STEADY_TARGET_DELTA_TEMP,
	MAX_STEADY_DELTA_TEMP,
	NORMAL_STEADY_DELTA_TEMP,
	TARGET_TEMP,
	MAX_UNSTEADY_CNT,
};

static int debugcode = -1;
static ssize_t pid_set_debug_prop(struct device* dev,
	struct device_attribute *attr, const char* buf, size_t len)
{
	int i, databuf[6];
	struct pid_chg_info *info = dev_get_drvdata(dev);

	if (buf == NULL || len == 0)
		return len;

    	sscanf(buf, "%d %d %d %d %d %d", &databuf[0], &databuf[1], &databuf[2], &databuf[3], &databuf[4], &databuf[5]);
	switch (databuf[0]) {
	    case POWER_LEVEL:
		for (i = 0; i < PID_CHG_POWER_MAX; i++)
			info->power_level[i] = databuf[i+1];
		break;
	    case POWER_DELTA_TEMP:
		for (i = 0; i < PID_CHG_POWER_MAX; i++)
			info->power_delta_temp[i] = databuf[i+1];
		break;
	    case POWER_UPDATE_TIME:
		info->power_update_time = databuf[1];
		break;
	    case POWER_MEAS_CURRENT:
		info->power_meas_current = databuf[1];
		break;
	    case TARGET_TEMP_GAP:
		info->target_temp_gap = databuf[1];
		break;
	    case STEADY_CYCLE:
		info->steady_cycle = databuf[1];
		break;
	    case MAX_STEADY_TARGET_DELTA_TEMP:
		info->max_steady_target_delta_temp = databuf[1];
		break;
	    case MAX_STEADY_DELTA_TEMP:
		info->max_steady_delta_temp = databuf[1];
		break;
	    case NORMAL_STEADY_DELTA_TEMP:
		info->normal_steady_delta_temp = databuf[1];
		break;
	    case MAX_UNSTEADY_CNT:
		info->max_unsteady_cnt = databuf[1];
		break;
	    case TARGET_TEMP:
		info->fake_orignal_target_temp = databuf[1];
		break;
	    default:
		pr_err("error input\n");
	}

	debugcode = databuf[0];

	return len;
}

static ssize_t pid_get_debug_prop(struct device* dev,
	struct device_attribute *attr, char* buf)
{
	int len = 0;
	struct pid_chg_info *info = dev_get_drvdata(dev);

	switch(debugcode){
		case TARGET_TEMP:
			len = sprintf(buf, "fake_orignal_target_temp is %d\n", info->fake_orignal_target_temp);
			break;
		default:
			len = sprintf(buf, "not support\n");
	}

	return len;
}

static DEVICE_ATTR(pid_parameter, 0660,
	pid_get_parameter, pid_set_parameter);
static DEVICE_ATTR(debug_node, 0660,
	pid_get_debug_prop, pid_set_debug_prop);

static struct attribute* pid_sysfs_attrs[] = {
	&dev_attr_pid_parameter.attr,
	&dev_attr_debug_node.attr,
	NULL,
};

static const struct attribute_group pid_sysfs_group = {
	.name  = "pid",
	.attrs = pid_sysfs_attrs,
};

static int pid_get_machine_temp(struct pid_chg_info *info, int *temp)
{
	int ret = 0;
	union com_propval machine_temp = {0,};
	struct tran_device *temp_forecast_dev = NULL;

	temp_forecast_dev = tran_get_by_name("temp_forecast");
	if (IS_ERR_OR_NULL(temp_forecast_dev)) {
		pr_err("%s: get temp_forecast_dev fail\n", __func__);
		goto out;
	}

	ret = tran_dev_get_prop(temp_forecast_dev,
			TRAN_PROP_FORECAST_MACHINE_TEMP, &machine_temp);
	if (ret != 0) {
		*temp = 2500;
		pr_info("%s: get machine_temp failed, ret = %d\n", __func__, ret);
		goto out;
	}
	*temp = machine_temp.intval;
out:
	return ret;
}

int pid_set_input_current_override(struct pid_chg_info *info, bool enable, u32 uA)
{
	int ret = 0;
	struct votable *total_aicr_vote = NULL;

	total_aicr_vote = find_votable("total_aicr");
	if (total_aicr_vote == NULL)
		return -ENODEV;
	
	ret = vote_override(total_aicr_vote, PID_VOTER, enable, uA);
	if (ret < 0) {
		pr_err("tc30 set aicr failed, ret = %d\n", ret);
	}

	return ret;
}

static int pid_chg_get_sys_info(struct pid_chg_info *info,
				 struct meas_sys_info *sys_info)
{
	int ret = 0;

	sys_info->ibus = info->support_ibus_measure ? tc_get_ibus() : info->power_meas_current / 1000;
	sys_info->vbus = tc_get_vbus();
	sys_info->vbat = tc_get_battery_voltage();
	sys_info->ibat = tc_get_battery_current();

	pr_info("vbus:%d,ibus:%d,vbat:%d,ibat:%d\n",
		sys_info->vbus, sys_info->ibus, sys_info->vbat, sys_info->ibat);

	return ret;
}

static int pid_chg_cal_sys_power(struct pid_chg_info *info, int *val)
{
	int ret, i;
	int sys_power = 0;
	bool aicr_state = false;
	struct meas_sys_info sys_info, max_sys_info, min_sys_info;
	struct charger_device *chg1_dev = get_charger_by_name("primary_chg");

	memset(&sys_info, 0, sizeof(struct meas_sys_info));
	memset(&max_sys_info, 0, sizeof(struct meas_sys_info));
	memset(&min_sys_info, 0, sizeof(struct meas_sys_info));

	if (!info->support_ibus_measure) {
		pid_set_input_current_override(info, true,
				info->power_meas_current);
		msleep(5);
		ret = charger_dev_get_aicr_state(chg1_dev, &aicr_state);
		if (ret < 0 || !aicr_state) {
			pr_err("aicr_state not support ret(%d), aicr_state(%d)",
					ret, aicr_state);
			ret = -EINVAL;
			goto out;
		}
	}

	for (i = 0; i < PID_CHG_MEASURE_SYS_POWER_AVG_TIMES + 2; i++) {
		ret = pid_chg_get_sys_info(info, &sys_info);
		if (ret < 0) {
			pr_err("pid get sys_info failed(%d)\n", ret);
			goto out;
		}
		sys_info.sys_power = percent((sys_info.vbus * sys_info.ibus / 1000),
			PID_CHG_EFFICIENCY) - sys_info.vbat * sys_info.ibat / 1000;

		if (sys_info.sys_power < 0)
			sys_info.sys_power = 0;

		pr_info("sys_power:%d\n", sys_info.sys_power);

		if (i == 0) {
			memcpy(&max_sys_info, &sys_info,
			       sizeof(struct meas_sys_info));
			memcpy(&min_sys_info, &sys_info,
			       sizeof(struct meas_sys_info));
		} else {
			max_sys_info.sys_power = max(max_sys_info.sys_power,
					sys_info.sys_power);
			min_sys_info.sys_power = min(min_sys_info.sys_power,
					sys_info.sys_power);
		}
		sys_power += sys_info.sys_power;
	}
	sys_power -= (max_sys_info.sys_power + min_sys_info.sys_power);
	sys_power = precise_div(sys_power, PID_CHG_MEASURE_SYS_POWER_AVG_TIMES);
	*val = sys_power;
	pr_info("sys_power_average:%d\n", sys_power);
out:
	if (!info->support_ibus_measure)
		pid_set_input_current_override(info, false,
				info->power_meas_current);
	return ret;
}

static int pid_get_target_temp(struct pid_chg_info *info, bool care_screen_on)
{
	int ret = 0, index = 0;
	int power_delta_temp = 0;
	int sys_power;
	union com_propval ambient_val = {0, };
	struct tran_device *ambient_dev = tran_get_by_name("ambient_detect");

	if (info->orignal_target_temp == INVALID_TEMP) {
		ret = tran_dev_get_prop(ambient_dev,
				TRAN_PROP_AMBIENT_TEMP, &ambient_val);
		if (ret < 0) {
			ambient_val.intval = 250;
			pr_err("fail to get ambient temp,ret = %d\n", ret);
		}

		index = pid_chg_find_closest_index(info->ambient_temp,
				PID_TARGET_TEMP_MAX, ambient_val.intval);

		info->orignal_target_temp = info->target_temp_array[index];
	}

	if (info->fake_orignal_target_temp) {
		info->orignal_target_temp = info->fake_orignal_target_temp;
		pr_info("set fake_orignal_target_temp = %d\n",
				info->fake_orignal_target_temp);
	}

	if (!care_screen_on || !info->screen_on)
		goto out;

	//update power for temp
	ret = pid_chg_cal_sys_power(info, &sys_power);
	if (ret < 0) {
		pr_err("cal sys power failed, ret = %d\n", ret);
		goto out;
	}

	index = pid_chg_find_closest_index(info->power_level, PID_CHG_POWER_MAX, sys_power);

	power_delta_temp = info->power_delta_temp[index];

out:
	info->target_temp = min(info->max_target_temp, (info->orignal_target_temp + power_delta_temp));
	pr_info("target_temp = %d(%d,%d)\n", info->target_temp,
			info->orignal_target_temp, power_delta_temp);

	return ret;
}

static int pid_algo_get_delta_cur(struct pid_chg_info *info, int *delta)
{
	int delta_time_ms;
	int ret = 0;
	int delta_temp, per_delta_temp, target_delta_temp, last_target_delta_temp;
	struct timespec64 delta_time;

	mutex_lock(&info->pid_lock);
	ktime_get_boottime_ts64(&info->curr_time);

	ret = pid_get_machine_temp(info, &info->curr_temp);
	if (ret < 0) {
		*delta = 0;
		pr_err("get machine_temp failed, ERROR!!!\n");
		goto out;
	}

	if (info->prev_time.tv_sec == 0 || info->prev_temp == INVALID_TEMP) {
		pr_info("First enter pid function\n");
		info->prev_time = info->curr_time;
		info->prev_temp = info->curr_temp;
	}

	delta_time = timespec64_sub(info->curr_time, info->prev_time);

	delta_time_ms = (int)(delta_time.tv_sec * 1000 + delta_time.tv_nsec / 1000000);

	/* Limit the minimum time interval for entry */
	if (delta_time_ms < info->polling_interval - 1000 && delta_time_ms != 0) {
		*delta = 0;
		pr_info("%s: delta_time_ms is too short(%d)!\n", __func__, delta_time_ms);
		goto out;
	}

	delta_temp = info->curr_temp - info->prev_temp;
	per_delta_temp = delta_temp * info->polling_interval / delta_time_ms;
	target_delta_temp = (info->target_temp - info->target_temp_gap) - info->curr_temp;
	last_target_delta_temp = (info->target_temp - info->target_temp_gap) - info->prev_temp;

	/* The anti-shake mechanism near the target temperature
	 * prevents the current from jitter back and forth */
	if (abs(target_delta_temp) <= info->max_steady_target_delta_temp &&
		abs(per_delta_temp) <= info->max_steady_delta_temp) {
		if (info->ignore_next_polling > 0) {
			info->ignore_next_polling--;
			*delta = 0;
			pr_info("ignore this polling!\n");
			goto ignore;
		} else if (abs(per_delta_temp) <= info->normal_steady_delta_temp &&
		       	(last_target_delta_temp * per_delta_temp) >= 0) { 
			info->unsteady_cnt = 0;
			*delta = 0;
			pr_info("steady!\n");
			goto steady;
		} else if (info->unsteady_cnt++ < info->max_unsteady_cnt) {
			*delta = 0;
			pr_info("unsteady!\n");
			goto unsteady;
		} else {
			info->ignore_next_polling = info->steady_cycle;
			pr_info("adjust!\n");
		}
	} else {
		info->ignore_next_polling = 0;
	}

	info->pterm = info->target_temp - info->curr_temp;

	if (((info->curr_temp < info->target_temp - info->target_temp_gap) && (info->iterm < 0)) ||
		((info->curr_temp > info->target_temp - info->target_temp_gap) && (info->iterm > 0)))
		info->iterm = 0;
	else
		info->iterm += info->pterm;

	if ((info->curr_temp > info->target_temp) && (info->curr_temp < info->prev_temp))
		info->dterm = 0;
	else
		info->dterm = info->prev_temp - info->curr_temp;
		
	*delta = (info->pterm * info->pid_array[KP]) + (info->iterm * info->pid_array[KI]) + (info->dterm * info->pid_array[KD]);

	/* Align limit to 50mA to avoid redundant calls to chrlmt. */
	*delta = *delta / 50000 * 50000;

	if(abs(*delta) > info->delta_current_max) {
		if(*delta > 0)
			*delta = info->delta_current_max;
		else
			*delta = -info->delta_current_max;
	}

unsteady:
ignore:
steady:

	pr_err("target_temp(%d) curr_temp(%d) prev_temp(%d) pterm(%d) iterm(%d) dterm(%d) delta_current(%d)\n",
		info->target_temp, info->curr_temp, info->prev_temp, info->pterm, info->iterm, info->dterm, *delta);

	info->prev_time = info->curr_time;
	info->prev_temp = info->curr_temp;
	

out:

	mutex_unlock(&info->pid_lock);
	return ret;
}

static void pid_target_temp_update_work(struct work_struct *work)
{
	int polling_time;
	struct pid_chg_info *info = container_of(to_delayed_work(work),
			struct pid_chg_info, temp_update_work);
	if (!info->plug_in)
		return;

	pid_get_target_temp(info, true);

	if (!info->screen_on)
		return;

	polling_time = info->support_ibus_measure ?
			info->ibus_measure_power_update_time :
			info->power_update_time;

	schedule_delayed_work(&info->temp_update_work, polling_time * HZ);

}

static int pid_screen_notifier_callback(struct notifier_block *nb,
                    unsigned long event, void *data)
{
	struct pid_chg_info *info = container_of(nb,
			struct pid_chg_info, pid_screen_notifier);
	
	switch (event) {
	case TRAN_DEV_NOTIFY_SCREEN_OFF:
		pr_info("%s: screen off\n", __func__);
		info->screen_on = false;
		cancel_delayed_work(&info->temp_update_work);
		schedule_delayed_work(&info->temp_update_work, 0);
		break;
	case TRAN_DEV_NOTIFY_SCREEN_ON:
		pr_info("%s: screen on\n", __func__);
		info->screen_on = true;
		cancel_delayed_work(&info->temp_update_work);
		schedule_delayed_work(&info->temp_update_work, 60 * HZ);
		break;
	default:
		break;
	}

	return 0;
}

static int pid_screen_notifier_init(struct pid_chg_info *info)
{
	int ret = 0;


	info->tc_lcd = tran_get_by_name("tc_lcd");
	if (IS_ERR_OR_NULL(info->tc_lcd)) {
		pr_err("%s: get tc_lcd_dev fail\n", __func__);
		ret = -ENODEV;
		goto out;
	}

	info->pid_screen_notifier.notifier_call = pid_screen_notifier_callback;
	ret = register_tran_device_notifier(info->tc_lcd,
				&info->pid_screen_notifier);
	if (ret != 0) {
		pr_err("register lcd notify failed, ret = %d\n", ret);
		goto out;
	}

out:
	return ret;
}

static int pid_chg_get_property(struct tran_device *dev,
				enum tran_common_prop prop,
				union com_propval *val)
{
	struct pid_chg_info *info = tran_get_data(dev);
	int value = 0;
	int ret = 0;

	switch (prop) {
	case TRAN_PROP_GET_PID_PARAM:
	    ret = pid_algo_get_delta_cur(info, &value);
	    if (ret < 0) {
		pr_err("%s: get delta cur failed\n", __func__);
	    }
	    val->intval = value;
	    break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int pid_chg_set_property(struct tran_device *dev,
					   enum tran_common_prop prop,
					   const union com_propval *val)
{
	struct pid_chg_info *info = tran_get_data(dev);
	int ret = 0;

	switch (prop) {
	case TRAN_PROP_POWER_PG_ON:
	case TRAN_PROP_USB_PLUG_IN:
		info->plug_in = true;
		info->prev_temp = INVALID_TEMP;
		info->curr_temp = INVALID_TEMP;
		info->orignal_target_temp = INVALID_TEMP;
		info->unsteady_cnt = 0;
		info->ignore_next_polling = 0;
		info->prev_chg_cur = -1;
		info->curr_chg_cur = -1;
		info->iterm = 0;
		memset(&info->prev_time, 0, sizeof(info->prev_time));
		memset(&info->curr_time, 0, sizeof(info->curr_time));
		pid_get_target_temp(info, false);
		cancel_delayed_work(&info->temp_update_work);
		schedule_delayed_work(&info->temp_update_work, 60 * HZ);
		break;
	case TRAN_PROP_POWER_PG_OFF:
	case TRAN_PROP_USB_PLUG_OUT:
		info->plug_in = false;
		break;
	case TRAN_PROP_SET_PID_TARGET_TEMP:
		info->orignal_target_temp = val->intval;
		if(info->fake_orignal_target_temp) {
			info->orignal_target_temp = info->fake_orignal_target_temp;
			pr_info("set fake_orignal_target_temp = %d\n", info->fake_orignal_target_temp);
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static struct tran_ops pid_chg_algo_ops = {
	.get_prop = pid_chg_get_property,
	.set_prop = pid_chg_set_property,
};

static void pid_chg_parameter_init(struct pid_chg_info *info)
{
	info->prev_temp = INVALID_TEMP;
	info->curr_temp = INVALID_TEMP;
	memset(&info->prev_time, 0, sizeof(info->prev_time));
	memset(&info->curr_time, 0, sizeof(info->curr_time));
}

static int pid_chg_algo_parse_dt(struct pid_chg_info *info,
				struct device *dev)
{
	int ret = 0;
	struct device_node *node = dev->of_node;

	info->support_ibus_measure = of_property_read_bool(node, "support_ibus_measure");

	ret = of_property_read_u32_array(node, "power_level",
			info->power_level, PID_CHG_POWER_MAX);
	if (ret < 0) {
		pr_err("parse power_level failed ret = %d\n", ret);
		goto out;
	}

	ret = of_property_read_u32_array(node, "power_delta_temp",
			info->power_delta_temp, PID_CHG_POWER_MAX);
	if (ret < 0) {
		pr_err("parse power_delta_temp failed ret = %d\n", ret);
		goto out;
	}

	ret = of_property_read_u32_array(node, "ambient_temp",
			info->ambient_temp, PID_TARGET_TEMP_MAX);
	if (ret < 0) {
		pr_err("parse ambient_temp failed ret = %d\n", ret);
		goto out;
	}

	ret = of_property_read_u32_array(node, "target_temp_array",
			info->target_temp_array, PID_TARGET_TEMP_MAX);
	if (ret < 0) {
		pr_err("parse target_temp_array failed ret = %d\n", ret);
		goto out;
	}

	ret = of_property_read_u32_array(node, "pid_array",
			info->pid_array, PID_MAX);
	if (ret < 0) {
		pr_err("parse pid_array failed, ret = %d\n", ret);
		goto out;
	}

	ret = of_property_read_u32(node, "polling_interval",
			&info->polling_interval);
	if (ret < 0) {
		pr_err("parse polling_interval failed, ret = %d\n", ret);
		goto out;
	}

	ret = of_property_read_u32(node, "power_update_time",
			&info->power_update_time);
	if (ret < 0) {
		pr_err("parse power_update_time failed, ret = %d\n", ret);
		goto out;
	}

	ret = of_property_read_u32(node, "ibus_measure_power_update_time",
			&info->ibus_measure_power_update_time);
	if (ret < 0) {
		pr_err("parse ibus_measure_power_update_time failed, ret = %d\n", ret);
		goto out;
	}

	ret = of_property_read_u32(node, "power_meas_current",
			&info->power_meas_current);
	if (ret < 0) {
		pr_err("parse power_meas_current failed, ret = %d\n", ret);
		goto out;
	}

	ret = of_property_read_u32(node, "target_temp_gap",
			&info->target_temp_gap);
	if (ret < 0) {
		pr_err("parse target_temp_gap failed, ret = %d\n", ret);
		goto out;
	}

	ret = of_property_read_u32(node, "steady_cycle", &info->steady_cycle);
	if (ret < 0) {
		pr_err("parse steady_cycle failed, ret = %d\n", ret);
		goto out;
	}

	ret = of_property_read_u32(node, "max_steady_target_delta_temp",
			&info->max_steady_target_delta_temp);
	if (ret < 0) {
		pr_err("parse max_steady_target_delta_temp failed, ret = %d\n", ret);
		goto out;
	}

	ret = of_property_read_u32(node, "max_steady_delta_temp",
			&info->max_steady_delta_temp);
	if (ret < 0) {
		pr_err("parse max_steady_delta_temp failed, ret = %d\n", ret);
		goto out;
	}

	ret = of_property_read_u32(node, "normal_steady_delta_temp",
			&info->normal_steady_delta_temp);
	if (ret < 0) {
		pr_err("parse normal_steady_delta_temp failed, ret = %d\n", ret);
		goto out;
	}

	ret = of_property_read_u32(node, "max_unsteady_cnt",
			&info->max_unsteady_cnt);
	if (ret < 0) {
		pr_err("parse max_unsteady_cnt failed, ret = %d\n", ret);
		goto out;
	}

	ret = of_property_read_u32(node, "max_target_temp", &info->max_target_temp);
	if (ret < 0) {
		pr_err("parse max_target_temp failed, ret = %d\n", ret);
		goto out;
	}

	ret = of_property_read_u32(node, "delta_current_max", &info->delta_current_max);
	if (ret < 0) {
		info->delta_current_max = PID_DELTA_CUR_MAX_UA;
		ret = 0;
		pr_err("parse delta_current_max failed, use default %duA ret = %d\n",
				PID_DELTA_CUR_MAX_UA, ret);
	}

	pr_err("[KP](%d), [KI](%d), [KD](%d), polling_interval(%d)\n",
			info->pid_array[KP], info->pid_array[KI],
			info->pid_array[KD], info->polling_interval);
out:
	return ret;
}

static int pid_chg_algo_probe(struct platform_device *pdev)
{
	struct pid_chg_info *info = NULL;
	int ret;

	pr_err("starts\n");

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	platform_set_drvdata(pdev, info);
	info->dev = &pdev->dev;
	info->pdev = pdev;

	mutex_init(&info->pid_lock);

	init_waitqueue_head(&info->wait_que);
	info->pid_wakelock = wakeup_source_register(&pdev->dev, "pid wakelock");

	ret = pid_chg_algo_parse_dt(info, &pdev->dev);
	if (ret < 0) {
		pr_err("pid paster dt failed, ret = %d\n", ret);
		goto err_parse_dt;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &pid_sysfs_group);

	ret = sysfs_create_link(chg_kobj, &pdev->dev.kobj, "pid_chg");
	if (ret < 0){
		tchr_err("%s : sysfs_create_link failed\n", __func__);
	}

	pid_chg_parameter_init(info);
	INIT_DELAYED_WORK(&info->temp_update_work, pid_target_temp_update_work);

	info->pid_props.alias_name = "pid_chg_algo";
	info->pid_dev = tran_device_register("pid_chg_algo",
						info->dev, info,
						&pid_chg_algo_ops,
						&info->pid_props);
	if (IS_ERR_OR_NULL(info->pid_dev)) {
		pr_info("register device failed\n");
		ret = PTR_ERR(info->pid_dev);
		goto err_register_chg_dev;
	}

	ret = pid_screen_notifier_init(info);
	if (ret != 0) {
		pr_err("%s register screen notify fail!\n", __func__);
		goto err_register_fb;
	}

	return 0;

err_register_fb:
err_register_chg_dev:
err_parse_dt:
	mutex_destroy(&info->pid_lock);
	return ret;
}

static int pid_chg_algo_remove(struct platform_device *dev)
{

	return 0;
}
static int pid_chg_algo_suspend(struct platform_device *dev, pm_message_t state)
{
	struct pid_chg_info *info = platform_get_drvdata(dev);

	mutex_lock(&info->pid_lock);
	return 0;
}
static int pid_chg_algo_resume(struct platform_device *dev)
{
	struct pid_chg_info *info = platform_get_drvdata(dev);

	mutex_unlock(&info->pid_lock);
	return 0;
}
static void pid_chg_algo_shutdown(struct platform_device *dev)
{

}

static const struct of_device_id pid_chg_algo_of_match[] = {
	{ .compatible = "tc,pid_chg_algo", },
	{},
};

MODULE_DEVICE_TABLE(of, pid_chg_algo_of_match);

static struct platform_driver pid_chg_algo_driver = {
	.probe = pid_chg_algo_probe,
	.remove = pid_chg_algo_remove,
	.suspend = pid_chg_algo_suspend,
	.resume = pid_chg_algo_resume,
	.shutdown = pid_chg_algo_shutdown,
	.driver = {
		   .name = "pid_chg_algo",
		   .of_match_table = pid_chg_algo_of_match,
	},
};

static int __init pid_chg_algo_init(void)
{
	return platform_driver_register(&pid_chg_algo_driver);
}
device_initcall_sync(pid_chg_algo_init);

static void __exit reserve_chg_exit(void)
{
	platform_driver_unregister(&pid_chg_algo_driver);
}
module_exit(reserve_chg_exit);

MODULE_AUTHOR("Transsion Inc.");
MODULE_DESCRIPTION("TRANSSION PID Driver");
MODULE_LICENSE("GPL");
