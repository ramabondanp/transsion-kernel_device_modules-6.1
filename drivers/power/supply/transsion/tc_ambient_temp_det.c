// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 Transsion Inc.
 */

#define pr_fmt(fmt)     "[ambient_det] %s: " fmt, __func__
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/power_supply.h>
#include "tc_charger.h"
#include "tc_common_class.h"

#define DEFAULT_AMBIENT_TEMP	250

enum dv2_ambient_temp {
	AMBIENT_POWER_LOW = 0,
	AMBIENT_POWER_MID1,
	AMBIENT_POWER_MID2,
	AMBIENT_POWER_MID3,
	AMBIENT_POWER_HIGH,
	AMBIENT_POWER_MAX,
};

struct ambient_info {
	struct platform_device *pdev;
	struct device *dev;
	struct tran_device *gauge_dev;
	struct tran_device *ambient_dev;
	struct tran_properties ambient_detect_props;
	struct wakeup_source *suspend_lock;
	struct mutex detect_lock;
	struct mutex update_lock;
	struct work_struct ambient_det_work;
	struct notifier_block psy_nb;
	struct timespec64 prev_time;
	struct timespec64 curr_time;
	struct timespec64 ambient_update_time;
	struct timespec64 offset_time;
	struct timespec64 plug_out_time;
	struct timespec64 plug_in_time;
	struct timespec64 suspend_time;
	struct timespec64 resume_time;
	struct timespec64 endtime;
	struct alarm ambient_alarm;
	int qmax;
	int prev_vbat; 
	int curr_vbat;
	int prev_soc; 
	int curr_soc;
	int prev_tusb_temp;
	int curr_tusb_temp;
	int prev_tpcb_temp;
	int curr_tpcb_temp;
	int prev_elec;
	int curr_elec;
	int ambient_temp;
	int ambient_max_power;
	int ambient_max_temp;
	int ambient_min_temp;
	int power_level[AMBIENT_POWER_MAX];
	int power_delta_temp[AMBIENT_POWER_MAX];
	int ambient_max_update_time;
	int ambient_max_temp_rate;
	int ambient_max_diff_pcb_temp;
	int polling_interval;
	int min_detection_duration;
	int ambient_low_power_update_time;
	bool plug_in;
	bool force_update;

};
static void ambient_det_start_timer(struct ambient_info *info);

static int ambient_find_closest_index(const unsigned int *array,
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

static void ambient_temp_update(struct ambient_info *info, int temp, int index)
{

	mutex_lock(&info->update_lock);
	info->ambient_temp = temp - info->power_delta_temp[index];
	info->ambient_temp = min(info->ambient_max_temp, max(info->ambient_min_temp, info->ambient_temp));
	ktime_get_boottime_ts64(&info->ambient_update_time);
	memset(&info->offset_time, 0, sizeof(struct timespec64));
	pr_info("ambient update: %d\n", info->ambient_temp);
	mutex_unlock(&info->update_lock);
}

static int ambient_obtain_tusb(struct ambient_info *info, int *tusb)
{
	int ret = 0;

	ret = tc_get_tusb_temp(tusb);
	if (ret != 0) {
		pr_err("obtain tusb failed ret = %d\n", ret);
	}

	return ret;
}

static int ambient_obtain_soc(struct ambient_info *info)
{
	int ret = 0;
	int soc = 5000;
	union com_propval tran_val = {0, };

	if (!info->gauge_dev)
		info->gauge_dev = tran_get_by_name("tc_gauge");

	ret = tran_dev_get_prop(info->gauge_dev, TRAN_PROP_ACCURACY_UISOC, &tran_val);
	if (ret < 0) {
		pr_err("obtain accuracy uisoc failed(%d)\n", ret);
		goto out;
	}

	soc = tran_val.intval;
	pr_info("obtain soc: %d\n", soc);
out:
	return soc;
}

static int ambient_obtain_electric(struct ambient_info *info)
{
	int ret = 0;
	int electric = 0;
	union com_propval tran_val = {0, };

	if (!info->gauge_dev)
		info->gauge_dev = tran_get_by_name("tc_gauge");

#if IS_ENABLED(CONFIG_TC_BATTERY)
	ret = tran_dev_get_prop(info->gauge_dev, TRAN_PROP_BATT_RM, &tran_val);
#else
	ret = tran_dev_get_prop(info->gauge_dev, TRAN_PROP_FG_HW_CAR, &tran_val);
#endif
	if (ret < 0) {
		pr_err("obtain hw elec failed(%d)\n", ret);
		goto out;
	}

	electric = tran_val.intval;
	pr_info("obtain electric: %d\n", electric);
out:
	return electric;
}

static int ambient_obtain_tpcb(void)
{

	return tc_get_accurate_tpcb_temp() / 100;
}

static int ambient_obtain_vbat(void)
{
	return tc_get_battery_voltage() / 1000;
}

static void ambient_detect_work(struct work_struct *work)
{
	int ret = 0;
	int index = 0;
        /* int diff_t = 0; */
	int diff_tpcb_temp = 0, diff_tusb_temp = 0;
	int diff_soc = 0, diff_elec = 0;
	struct timespec64 time, diff_time;
	int tpcb = 0, vbat = 0, tusb = 0, soc = 0, average_power = 0, electric = 0;
	struct ambient_info *info = container_of(work, 
				struct ambient_info, ambient_det_work);

	if (!info->suspend_lock->active)
		__pm_stay_awake(info->suspend_lock);

	mutex_lock(&info->detect_lock);
	/*Only discharge and first insertion status can be detected*/
	if (info->plug_in) {
		goto out;
	}

	tpcb = ambient_obtain_tpcb();

	vbat = ambient_obtain_vbat();

	soc = ambient_obtain_soc(info);

	ktime_get_boottime_ts64(&time);

	ret = ambient_obtain_tusb(info, &tusb);
	if (ret != 0) {
		pr_err("get tusb temp failed %d\n", ret);
		goto timer_start;
	}

	electric = ambient_obtain_electric(info);

	/*You need to reset the parameters after first insertion or charging*/
	if (info->prev_soc == -1 && info->curr_soc == -1) {
		info->prev_vbat = vbat;
		info->prev_soc = soc;
		info->prev_time = time;
		info->prev_tusb_temp = tusb;
		info->prev_tpcb_temp = tpcb;
		info->prev_elec = electric;
		info->curr_vbat = vbat;
		info->curr_soc = soc;
		info->curr_time	= time;
		info->curr_tusb_temp = tusb;
		info->curr_tpcb_temp = tpcb;
		info->curr_elec = electric;
		pr_err("first init or charger plug in ambient detect reset parameters!");
		goto timer_start;
	}

	/*If not plugged into the charger requires a charge difference of more than 1*/
	if (info->curr_soc < soc) {
		pr_err("soc(%d,%d) error occur, Reset!\n", info->curr_soc, soc);
		info->prev_soc = -1;
		info->curr_soc = -1;
		goto timer_start;
	}

	//check min time
	if (timespec64_sub(time, info->curr_time).tv_sec <
			info->min_detection_duration) {

		pr_info("min_detection_duration not support");
		if (timespec64_compare(&time, &info->endtime) >= 0 &&
			info->endtime.tv_sec != 0 &&
			info->endtime.tv_nsec != 0) {
			pr_info("No alarm in the queue");
			goto timer_start;
		} else {
			goto out;
		}
	}

	info->prev_vbat = info->curr_vbat;
	info->prev_soc = info->curr_soc;
	info->prev_time = info->curr_time;
	info->prev_tusb_temp = info->curr_tusb_temp;
	info->prev_tpcb_temp = info->curr_tpcb_temp;
	info->prev_elec = info->curr_elec;
	info->curr_vbat = vbat;
	info->curr_soc = soc;
	info->curr_time	= time;
	info->curr_tusb_temp = tusb;
	info->curr_tpcb_temp = tpcb;
	info->curr_elec = electric;

	diff_soc = abs(info->prev_soc - info->curr_soc);
	diff_time = timespec64_sub(info->curr_time, info->prev_time);
	diff_tusb_temp = abs(info->curr_tusb_temp - info->prev_tusb_temp);
	diff_tpcb_temp = abs(info->curr_tpcb_temp - info->prev_tpcb_temp);
	diff_elec = abs(info->curr_elec - info->prev_elec);

	pr_info("curr_tpcb_temp:%d, prev_tpcb_temp:%d ,abs:%d\n",
		info->curr_tpcb_temp, info->prev_tpcb_temp, diff_tpcb_temp);
	pr_info("curr_tusb_temp:%d, prev_tusb_temp:%d ,abs:%d\n",
		info->curr_tusb_temp, info->prev_tusb_temp, diff_tusb_temp);
	pr_info("curr_elec:%d, prev_elec:%d ,abs:%d\n",
		info->curr_elec, info->prev_elec, diff_elec);
	pr_info("diff_soc:%d, diff_time:%lld\n", diff_soc, diff_time.tv_sec);
	
	if (info->force_update) {
		pr_info("force update ambient temp!\n");
		goto ambient_force_update;
	}
	/*lf the difference between the two pcb temperatures is too large, it does not match*/
	if (diff_tpcb_temp >= info->ambient_max_diff_pcb_temp) { 
		pr_err("diff tpcb temp too high(%d, %d)\n", 
			diff_tpcb_temp, info->ambient_max_diff_pcb_temp);
		goto timer_start;
	}

	/*If the time to drop a point is less than 15 minutes, it does not match*/
	if (diff_tusb_temp != 0 && 
		(diff_time.tv_sec * 10 / diff_tusb_temp) <=
		info->ambient_max_temp_rate) { 
		pr_err("diff tusb temp rate too high(%lld, %d)\n",
			(long long)diff_time.tv_sec * 10 / diff_tusb_temp, info->ambient_max_temp_rate);
		goto timer_start;
	}

	/*If the average power consumption exceeds the maximum power consumption, it does not meet*/
	if (diff_time.tv_sec != 0)
		average_power = (diff_elec * 3600 / diff_time.tv_sec) *
			 (info->prev_vbat + info->curr_vbat) / 2000;

	if (average_power > info->ambient_max_power) {
		pr_err("average power too high(%d, %d)\n",
				average_power, info->ambient_max_power);
		goto timer_start;
	}
	/*Compensate according to the average power consumption*/
	index = ambient_find_closest_index(info->power_level,
			AMBIENT_POWER_MAX, average_power);
	pr_err("average_power:%d, index:%d\n", average_power, index);

ambient_force_update:
	ambient_temp_update(info, tusb, index);

timer_start:
	ambient_det_start_timer(info);
	
out:
	if (info->plug_in) {
		info->prev_soc = -1;
		info->curr_soc = -1;
	}

	info->force_update = false;

	mutex_unlock(&info->detect_lock);
	__pm_relax(info->suspend_lock);
	return;
}

static void ambient_info_init(struct ambient_info *info)
{
	info->prev_soc = -1;
	info->curr_soc = -1;
	info->ambient_temp = DEFAULT_AMBIENT_TEMP;
	info->plug_out_time.tv_sec = -1;
	info->plug_in_time.tv_sec = -1;
}

static enum alarmtimer_restart
	ambient_alarm_timer_func(struct alarm *alarm, ktime_t now)
{
	struct ambient_info *info =
		container_of(alarm, struct ambient_info, ambient_alarm);

	__pm_stay_awake(info->suspend_lock);
	schedule_work(&info->ambient_det_work);


	return ALARMTIMER_NORESTART;
}

static void ambient_init_alarm_timer(struct ambient_info *info)
{
	alarm_init(&info->ambient_alarm, ALARM_BOOTTIME,
		ambient_alarm_timer_func);
	ambient_det_start_timer(info);
}

static void ambient_det_cancel_timer(struct ambient_info *info)
{
	ktime_get_boottime_ts64(&info->endtime);
	alarm_cancel(&info->ambient_alarm);
}

static void ambient_det_start_timer(struct ambient_info *info)
{
	struct timespec64 end_time, time_now;
	ktime_t ktime, ktime_now;
	int ret = 0;

	/* If the timer was already set, cancel it */
	ret = alarm_try_to_cancel(&info->ambient_alarm);
	if (ret < 0) {
		pr_err("%s: callback was running\n", __func__);
	}

	ktime_now = ktime_get_boottime();
	time_now = ktime_to_timespec64(ktime_now);
	end_time.tv_sec = time_now.tv_sec + info->polling_interval;
	end_time.tv_nsec = time_now.tv_nsec + 0;
	info->endtime = end_time;
	ktime = ktime_set(info->endtime.tv_sec, info->endtime.tv_nsec);

	pr_err("%s: alarm timer start:%d, %lld %ld\n", __func__, ret,
		(long long)info->endtime.tv_sec, info->endtime.tv_nsec);

	alarm_start(&info->ambient_alarm, ktime);
}

static int ambient_get_property(struct tran_device *dev,
				enum tran_common_prop prop,
				union com_propval *val)
{
	int ret = 0, tusb = 0;
	struct timespec64 time, diff_time;
	struct ambient_info *info = tran_get_data(dev);

	switch (prop) {
		case TRAN_PROP_AMBIENT_TEMP:
			mutex_lock(&info->detect_lock);
			ambient_obtain_tusb(info, &tusb);
			ktime_get_boottime_ts64(&time);
			if (info->plug_in) {
				diff_time = timespec64_sub(info->plug_in_time,
						info->ambient_update_time);
				diff_time = timespec64_sub(diff_time,
						info->offset_time);
			} else {
				diff_time = timespec64_sub(time,
					info->ambient_update_time);
				diff_time = timespec64_sub(diff_time,
						info->offset_time);
			}

			if (diff_time.tv_sec > info->ambient_max_update_time) {
				pr_err("The ambient temperature no update too long, use default \n");
				val->intval = DEFAULT_AMBIENT_TEMP;
				//ambient_temp_update(info, DEFAULT_AMBIENT_TEMP, 0);
				ret = -EINVAL;
			} else if (tusb <= info->ambient_temp - 30) {
				pr_err("The ambient temperature mistake, use default \n");
				//ambient_temp_update(info, DEFAULT_AMBIENT_TEMP, 0);
				val->intval = DEFAULT_AMBIENT_TEMP;
				ret = -EINVAL;
			}
			val->intval = info->ambient_temp;
			pr_err("The ambient temperature (%d) \n", info->ambient_temp);
			mutex_unlock(&info->detect_lock);
			break;
		case TRAN_PROP_AMBIENT_TEMP_UPDATE:
			val->intval = info->ambient_temp;
			break;
		case TRAN_PROP_AMBIENT_TEMP_UPDATE_TIME:
			ktime_get_boottime_ts64(&time);
			diff_time = timespec64_sub(time, info->ambient_update_time);
			val->intval = diff_time.tv_sec;
			break;
		default:
			ret = -EINVAL;
	}

	return ret;
}

static int ambient_set_property(struct tran_device *dev,
					   enum tran_common_prop prop,
					   const union com_propval *val)
{
	int ret = 0;
	struct timespec64 diff_time;
	struct ambient_info *info = tran_get_data(dev);

	switch (prop) {
		case TRAN_PROP_USB_PLUG_IN:
			pr_info("ambient plug_in call\n");
			ambient_detect_work(&info->ambient_det_work);

			mutex_lock(&info->detect_lock);
			ktime_get_boottime_ts64(&info->plug_in_time);
			info->plug_in = true;
			mutex_unlock(&info->detect_lock);
			break;
		case TRAN_PROP_USB_PLUG_OUT:
			mutex_lock(&info->detect_lock);
			ktime_get_boottime_ts64(&info->plug_out_time);
			diff_time = timespec64_sub(info->plug_out_time,
					info->plug_in_time);
			info->offset_time = timespec64_add(info->offset_time,
					diff_time);
			info->plug_in = false;
			mutex_unlock(&info->detect_lock);

			pr_info("ambient plug_out call\n");
			ambient_detect_work(&info->ambient_det_work);
			break;
		case TRAN_PROP_AMBIENT_TEMP_UPDATE:
			break;
		case TRAN_PROP_MTK_GAUGE_CAR_RESET:
			mutex_lock(&info->detect_lock);
			info->curr_elec = info->curr_elec - ambient_obtain_electric(info);
			mutex_unlock(&info->detect_lock);
			pr_info("mtk gauge car reset, update curr_elec(%d)\n",
					info->curr_elec);
			break;
		default:
			ret = -EINVAL;
	}

	return ret;
}

static struct tran_ops ambient_detect_ops = {
	.get_prop = ambient_get_property,
	.set_prop = ambient_set_property,
};

static int ambient_prop_init(struct ambient_info *info)
{

        info->ambient_detect_props.alias_name = "ambient_detect";
	info->ambient_dev = tran_device_register("ambient_detect",
						info->dev, info,
						&ambient_detect_ops,
						&info->ambient_detect_props);
	if (IS_ERR_OR_NULL(info->ambient_dev))
		return -ENODEV;

	return 0;
}


static ssize_t ambient_temp_set(struct device* dev,
	struct device_attribute *attr, const char* buf, size_t len)
{
	int data;
	struct ambient_info *info = dev_get_drvdata(dev);

	if (buf == NULL || len == 0)
		return len;

    	sscanf(buf, "%d", &data);

	ambient_temp_update(info, data, 0);
	
	pr_info("debug set ambient_temp(%d)\n", data);

	return len;
}

static ssize_t ambient_temp_get(struct device* dev,
	struct device_attribute *attr, char* buf)
{
	struct ambient_info *info = dev_get_drvdata(dev);

	pr_info("debug get ambient_temp(%d)\n", info->ambient_temp);
	return sprintf(buf, "ambient_temp(%d)\n", info->ambient_temp);
}


static DEVICE_ATTR(ambient_temp, 0660,
	ambient_temp_get, ambient_temp_set);

static struct attribute* ambient_sysfs_attrs[] = {
	&dev_attr_ambient_temp.attr,
	NULL,
};

static const struct attribute_group ambient_sysfs_group = {
	.name  = "ambient",
	.attrs = ambient_sysfs_attrs,
};

static int ambient_detect_parse_dt(struct ambient_info *info,
				struct device *dev)
{
	int ret = 0;
	struct device_node *np = dev->of_node;

	ret = of_property_read_s32(np, "ambient_max_temp", &info->ambient_max_temp);
	if (ret < 0) {
		pr_err("parse ambient_max_temp failed ret = %d",ret);
		goto out;
	}

	ret = of_property_read_s32(np, "ambient_min_temp", &info->ambient_min_temp);
	if (ret < 0) {
		pr_err("parse ambient_min_temp failed ret = %d",ret);
		goto out;
	}

	ret = of_property_read_u32(np, "ambient_max_power", &info->ambient_max_power);
	if (ret < 0) {
		pr_err("parse ambient_max_power failed ret = %d",ret);
		goto out;
	}

	ret = of_property_read_u32(np, "ambient_max_update_time", &info->ambient_max_update_time);
	if (ret < 0) {
		pr_err("parse ambient_max_update_time failed ret = %d",ret);
		goto out;
	}

	ret = of_property_read_u32(np, "ambient_max_temp_rate", &info->ambient_max_temp_rate);
	if (ret < 0) {
		pr_err("parse ambient_max_temp_rate failed ret = %d",ret);
		goto out;
	}

	ret = of_property_read_u32(np, "ambient_max_diff_pcb_temp", &info->ambient_max_diff_pcb_temp);
	if (ret < 0) {
		pr_err("parse ambient_max_diff_pcb_temp failed ret = %d",ret);
		goto out;
	}

	ret = of_property_read_u32(np, "polling_interval", &info->polling_interval);
	if (ret < 0) {
		pr_err("parse polling_interval failed, ret = %d\n", ret);
		goto out;
	}

	ret = of_property_read_u32(np, "min_detection_duration", &info->min_detection_duration);
	if (ret < 0) {
		pr_err("parse min_detection_duration failed, ret = %d\n", ret);
		goto out;
	}

	ret = of_property_read_u32(np, "ambient_low_power_update_time", &info->ambient_low_power_update_time);
	if (ret < 0) {
		pr_err("parse ambient_low_power_update_time failed ret = %d",ret);
		goto out;
	}

	ret = of_property_read_u32_array(np, "power_level", info->power_level, AMBIENT_POWER_MAX);
	if (ret < 0) {
		pr_err("parse power_level failed ret = %d",ret);
		goto out;
	}

	ret = of_property_read_u32_array(np, "power_delta_temp", info->power_delta_temp, AMBIENT_POWER_MAX);
	if (ret < 0) {
		pr_err("parse power_delta_temp failed ret = %d",ret);
		goto out;
	}

out:
	return ret;
}

static int ambient_detect_probe(struct platform_device *pdev)
{
	int ret;
	struct ambient_info *info = NULL;

	pr_info("enter\n");
	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);
	info->pdev = pdev;

	mutex_init(&info->detect_lock);
	mutex_init(&info->update_lock);


	ret = ambient_detect_parse_dt(info, info->dev);
	if(ret < 0) {
		pr_err("water detect parse dts failed, ret = %d", ret);
	}

	ret = ambient_prop_init(info);
	if (ret < 0) {
		ret = -ENODEV;
		pr_info("register ambient device failed\n");
		goto err_register_dev;
	}

	info->suspend_lock =
		wakeup_source_register(NULL, "ambient detect");

	ambient_info_init(info);

	INIT_WORK(&info->ambient_det_work, ambient_detect_work);
	ambient_init_alarm_timer(info);
	ret = sysfs_create_group(&pdev->dev.kobj, &ambient_sysfs_group);
	ret = sysfs_create_link(chg_kobj, &pdev->dev.kobj, "tc_ambient");
	if (ret < 0){
		tchr_err("%s : sysfs_create_link failed\n", __func__);
	}


	pr_info("successfully\n");

	return 0;

err_register_dev:
	tran_device_unregister(info->ambient_dev);
	mutex_destroy(&info->detect_lock);
	mutex_destroy(&info->update_lock);
	return ret;
}

static int ambient_detect_prepare_suspend(struct device *dev)
{
	struct ambient_info *info = dev_get_drvdata(dev);

	if (info == NULL) {
		pr_err("%s: info is null\n", __func__);
		return 0;
	}
	pr_info("%s\n", __func__);

	ambient_det_cancel_timer(info);
	ktime_get_boottime_ts64(&info->suspend_time);
	mutex_lock(&info->detect_lock);

	return 0;
}

static void ambient_detect_complete_resume(struct device *dev)
{
	struct timespec64 diff_time;
	struct ambient_info *info = dev_get_drvdata(dev);

	if (info == NULL) {
		pr_err("%s: info is null\n", __func__);
		return;
	}
	pr_info("%s\n", __func__);

	mutex_unlock(&info->detect_lock);

	ktime_get_boottime_ts64(&info->resume_time);
	diff_time = timespec64_sub(info->resume_time, info->suspend_time);
	if (info->suspend_time.tv_sec != 0 &&
		diff_time.tv_sec > info->ambient_low_power_update_time) {
		mutex_lock(&info->detect_lock);
		info->force_update = true;
		mutex_unlock(&info->detect_lock);
	}

	if (!info->suspend_lock->active)
		__pm_stay_awake(info->suspend_lock);
	schedule_work(&info->ambient_det_work);
}


static const struct dev_pm_ops ambient_detect_pm_ops = {
	.prepare	= ambient_detect_prepare_suspend,
	.complete       = ambient_detect_complete_resume,
};

static int ambient_detect_remove(struct platform_device *pdev)
{
	return 0;
}

static void ambient_detect_shutdown(struct platform_device *dev)
{
	return;
}

static const struct of_device_id ambient_detect_of_match[] = {
	{.compatible = "tc,ambient_detect",},
	{},
};
MODULE_DEVICE_TABLE(of, ambient_detect_of_match);

static struct platform_driver ambient_det_platdrv = {
	.probe = ambient_detect_probe,
	.remove = ambient_detect_remove,
	.shutdown = ambient_detect_shutdown,
	.driver = {
		.name = "ambient_detect",
		.owner = THIS_MODULE,
		.pm = &ambient_detect_pm_ops,
		.of_match_table = ambient_detect_of_match,
	},
};

static int __init ambient_temp_det_init(void)
{
	return platform_driver_register(&ambient_det_platdrv);
}
late_initcall(ambient_temp_det_init);

static void __exit ambient_temp_det_exit(void)
{
	platform_driver_unregister(&ambient_det_platdrv);
}
module_exit(ambient_temp_det_exit);

MODULE_DESCRIPTION("Transsion AMBIENT DETECT FUNCTION");
MODULE_AUTHOR("Duck");
MODULE_VERSION("1.0.0_G");
MODULE_LICENSE("GPL");

