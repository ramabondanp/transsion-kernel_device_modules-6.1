// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 Transsion Inc.
 */

#define pr_fmt(fmt)     "[tc_temp_forecast] %s: " fmt, __func__
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
#include "tc_charger.h"
#include "tc_common_class.h"
#include "tc_misc_intf.h"

struct temp_forecast_info {
	struct platform_device *pdev;
	struct device *dev;
	struct tran_device *temp_forecast_dev;
	struct tran_properties temp_forecast_props;
	struct mutex update_rate_lock;
	struct mutex suspend_lock;
	struct timespec64 plug_time;
	int orgin_rate;
	int curr_rate;
	int plug_rate;
	int per_rate_time;
	int max_temp_diff;
	int max_comp_temp;
	int pa_temp_gap;
	int show_param;
	int pre_tusb_temp;
	bool plug_in;
	bool suspend_flag;
};

static void update_batt_temp_rate(struct temp_forecast_info *info)
{
	int rate, delta_rate;
	struct timespec64 cur_update_time;
	struct timespec64 delta_time;

	mutex_lock(&info->update_rate_lock);
	ktime_get_boottime_ts64(&cur_update_time);
	if ((info->curr_rate == info->orgin_rate && info->plug_in) ||
		(info->curr_rate == 100 && !info->plug_in))
		goto out;
		
	if (info->curr_rate == 0) {
		pr_info("first update batt temp rate!\n");
		rate = 100;
		info->plug_time = cur_update_time;
		info->plug_rate = rate;
		goto update;
	}

	delta_time = timespec64_sub(cur_update_time, info->plug_time);
	if (info->plug_in) {
		if (delta_time.tv_sec >= info->per_rate_time * (100 - info->orgin_rate)) {
			rate = info->orgin_rate; 
		} else {
			delta_rate = (int)delta_time.tv_sec / info->per_rate_time;
			rate = info->plug_rate - delta_rate;
		}
	} else {
		if (delta_time.tv_sec >= info->per_rate_time * (100 - info->orgin_rate)) {
			rate = 100; 
		} else {
			delta_rate = (int)delta_time.tv_sec / info->per_rate_time;
			rate = info->plug_rate + delta_rate;
		}
	}
	rate = max(rate, info->orgin_rate);
	rate = min(rate, 100);

update:
	info->curr_rate = rate;
	pr_info("update rate  = %d\n", info->curr_rate);
out:
	mutex_unlock(&info->update_rate_lock);

}

static int get_tusb_temp(struct temp_forecast_info *info, int *tusb)
{
	int ret = 0;

	mutex_lock(&info->suspend_lock);
	if (info->suspend_flag) {
		*tusb = info->pre_tusb_temp;
		pr_err("get tusb while in suspend\n");
		goto out;
	}

	ret = tc_get_tusb_temp(tusb);
	if (ret != 0) {
		pr_err("get tusb failed ret = %d\n", ret);
	}
	info->pre_tusb_temp = *tusb;
out:
	mutex_unlock(&info->suspend_lock);

	return ret;
}

static int forecast_batt_temp(struct temp_forecast_info *info, int batt_ntc_temp)
{
	int ret = 0;
	int diff_temp = 0, tusb_temp = 250;
	int forecast_batt_temp = 0;

	update_batt_temp_rate(info);

	if (info->curr_rate == 100)
		goto direct_batt_ntc_temp;

	ret = get_tusb_temp(info, &tusb_temp);
	if (ret != 0) {
		pr_err("get tusb temp failed(%d), goto err!\n", ret);
		goto error;
	}

	diff_temp = abs(batt_ntc_temp - tusb_temp);
	if (diff_temp > info->max_temp_diff) {
		pr_err("ntc diff too max(%d, %d)\n", diff_temp, info->max_temp_diff);
		goto error;
	}

	forecast_batt_temp = (batt_ntc_temp * info->curr_rate +
				tusb_temp * (100 - info->curr_rate)) / 100;

	diff_temp = forecast_batt_temp - batt_ntc_temp;
	if (abs(diff_temp) > info->max_comp_temp) {
		if (diff_temp > 0)
			forecast_batt_temp = batt_ntc_temp + info->max_comp_temp;
		else
			forecast_batt_temp = batt_ntc_temp - info->max_comp_temp;
	}

	pr_info("forecast_batt_temp = %d, batt_ntc_temp = %d, tusb_temp = %d, curr_rate = %d\n",
		forecast_batt_temp, batt_ntc_temp, tusb_temp, info->curr_rate);

	return forecast_batt_temp;

error:
	pr_info("batt_ntc_temp = %d, tusb_temp = %d, curr_rate = %d\n",
		batt_ntc_temp, tusb_temp, info->curr_rate);
direct_batt_ntc_temp:
	return batt_ntc_temp;
}

static int forecast_machine_temp(struct temp_forecast_info *info, int *machine_temp)
{
	int ret = 0;
	int bat_temp = 2500;
	int pcb_temp = 0;
	int pa_temp = 0;
	int forecast_temp = 0;

	bat_temp = tc_get_accurate_battery_temperature() * 10;

	pcb_temp = tc_get_accurate_tpcb_temp();
	pcb_temp = pcb_temp / 10;

	pa_temp = tc_get_accurate_tpa_temp_max();
	pa_temp = pa_temp / 10;

	pr_info("%s : bat_temp = %d, pcb_temp = %d, pa_temp = %d\n",
	       	__func__, bat_temp, pcb_temp, pa_temp);

	if (pcb_temp == ERROR_NTC_TEMP || pa_temp == ERROR_NTC_TEMP) {
		pr_err("get NTC temp failed!\n");
		goto out;
	}

	forecast_temp = max(bat_temp, pcb_temp);
	forecast_temp = max(forecast_temp, (pa_temp - info->pa_temp_gap));

	*machine_temp = forecast_temp;
	pr_err("%s : machine_temp = %d\n", __func__, *machine_temp);
	return ret;

out:
	*machine_temp = bat_temp;
	return ret;
}

static int temp_forecast_get_property(struct tran_device *dev,
				enum tran_common_prop prop,
				union com_propval *val)
{
	int ret = 0;
	struct temp_forecast_info *info = tran_get_data(dev);

	switch (prop) {
		case TRAN_PROP_FORECAST_BATT_TEMP:
			val->intval = forecast_batt_temp(info, val->intval);
			break;
		case TRAN_PROP_FORECAST_MACHINE_TEMP:
			ret = forecast_machine_temp(info, &val->intval);
			if (ret != 0)
				pr_err("get forecast machine_temp failed\n");
			break;
		default:
			ret = -EINVAL;
			break;
	}

	return ret;
}

static int temp_forecast_set_property(struct tran_device *dev,
				enum tran_common_prop prop,
				const union com_propval *val)
{
	struct temp_forecast_info *info = tran_get_data(dev);
	int ret = 0;

	switch (prop) {
		case TRAN_PROP_USB_PLUG_IN:
			update_batt_temp_rate(info);
			ktime_get_boottime_ts64(&info->plug_time);
			info->plug_rate = info->curr_rate;
			info->plug_in = true;
			break;
		case TRAN_PROP_USB_PLUG_OUT:
			update_batt_temp_rate(info);
			ktime_get_boottime_ts64(&info->plug_time);
			info->plug_rate = info->curr_rate;
			info->plug_in = false;
			break;
		default:
			ret = -EINVAL;
	}

	return ret;
}

static struct tran_ops temp_forecast_ops = {
	.get_prop = temp_forecast_get_property,
	.set_prop = temp_forecast_set_property,
};

static int temp_forecast_prop_init(struct temp_forecast_info *info)
{

        info->temp_forecast_props.alias_name = "temp_forecast";
	info->temp_forecast_dev = tran_device_register("temp_forecast",
						info->dev, info,
						&temp_forecast_ops,
						&info->temp_forecast_props);
	if (IS_ERR_OR_NULL(info->temp_forecast_dev))
		return -ENODEV;

	return 0;
}

static int temp_forecast_parse_dt(struct temp_forecast_info *info,
				struct device *dev)
{
	int ret = 0;
	struct device_node *np = dev->of_node;

	ret = of_property_read_u32(np, "orgin_rate", &info->orgin_rate);
	if (ret < 0) {
		pr_err("parse orgin_rate failed ret = %d",ret);
		goto out;
	}

	ret = of_property_read_u32(np, "max_temp_diff", &info->max_temp_diff);
	if (ret < 0) {
		pr_err("parse max_temp_diff failed ret = %d",ret);
		goto out;
	}

	ret = of_property_read_u32(np, "per_rate_time", &info->per_rate_time);
	if (ret < 0) {
		pr_err("parse per_rate_time failed ret = %d",ret);
		goto out;
	}

	ret = of_property_read_u32(np, "max_comp_temp", &info->max_comp_temp);
	if (ret < 0) {
		pr_err("parse max_comp_temp failed, ret = %d\n", ret);
		goto out;
	}

	ret = of_property_read_u32(np, "pa_temp_gap", &info->pa_temp_gap);
	if (ret < 0) {
		pr_err("parse pa_temp_gap failed, use default, ret = %d\n", ret);
		info->pa_temp_gap = 500;
		ret = 0;
	}
out:
	return ret;
}

enum debug_node {
	DEBUG_ORGIN_RATE = 0,
	DEBUG_MAX_TEMP_DIFF,
	DEBUG_PER_RATE_TIME,
	DEBUG_MAX_COMP_TEMP,
	DEBUG_PA_TEMP_GAP,
};

static ssize_t tran_temp_set_debug_prop(struct device* dev,
	struct device_attribute *attr, const char* buf, size_t len)
{
	int databuf[3];
	int ret = 0;
	struct temp_forecast_info *info = dev_get_drvdata(dev);

	if (buf == NULL || len == 0)
		return len;

    	ret = sscanf(buf, "%d %d", &databuf[0], &databuf[1]);
	if (ret != 2)
		goto out;
	switch (databuf[0]) {
	    case DEBUG_ORGIN_RATE:
		info->orgin_rate = databuf[1];
		break;
	    case DEBUG_MAX_TEMP_DIFF:
		info->max_temp_diff = databuf[1];
		break;
	    case DEBUG_PER_RATE_TIME:
		info->per_rate_time = databuf[1];
		break;
	    case DEBUG_MAX_COMP_TEMP:
		info->max_comp_temp = databuf[1];
		break;
	    case DEBUG_PA_TEMP_GAP:
		info->pa_temp_gap = databuf[1];
		break;
	    default:
		pr_err("error input\n");
	
	}

out:
	info->show_param = databuf[0];

	return len;
}

static ssize_t tran_temp_get_debug_prop(struct device* dev,
	struct device_attribute *attr, char* buf)
{
	struct temp_forecast_info *info = dev_get_drvdata(dev);

	switch (info->show_param) {
	    case DEBUG_ORGIN_RATE:
		return sprintf(buf, "orgin_rate: %d\n", info->orgin_rate);
	    case DEBUG_MAX_TEMP_DIFF:
		return sprintf(buf, "max_temp_diff: %d\n", info->max_temp_diff);
	    case DEBUG_PER_RATE_TIME:
		return sprintf(buf, "per_rate_time: %d\n", info->per_rate_time);
	    case DEBUG_MAX_COMP_TEMP:
		return sprintf(buf, "max_comp_temp: %d\n", info->max_comp_temp);
	    case DEBUG_PA_TEMP_GAP:
		return sprintf(buf, "pa_temp_gap: %d\n", info->pa_temp_gap);
	    default:
		return sprintf(buf, "not support\n");
	
	}
	return sprintf(buf, "not support\n");
}

static DEVICE_ATTR(debug_node, 0660,
	tran_temp_get_debug_prop, tran_temp_set_debug_prop);

static struct attribute* tran_temp_sysfs_attrs[] = {
	&dev_attr_debug_node.attr,
	NULL,
};

static const struct attribute_group tran_temp_sysfs_group = {
	.name  = "tran_temp",
	.attrs = tran_temp_sysfs_attrs,
};

static int temp_forecast_probe(struct platform_device *pdev)
{
	int ret;
	struct temp_forecast_info *info = NULL;

	pr_info("enter\n");
	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);
	info->pdev = pdev;

	mutex_init(&info->update_rate_lock);
	mutex_init(&info->suspend_lock);

	ret = sysfs_create_group(&pdev->dev.kobj, &tran_temp_sysfs_group);

	ret = sysfs_create_link(chg_kobj,
			&pdev->dev.kobj, "temp_forecast");
	if (ret < 0) {
		pr_err("%s : sysfs_create_link failed\n", __func__);
	}

	ret = temp_forecast_parse_dt(info, info->dev);
	if(ret < 0) {
		pr_err("temperature forecast parse dts failed, ret = %d", ret);
	}

	ret = temp_forecast_prop_init(info);
	if (ret < 0) {
		ret = -ENODEV;
		pr_info("register temperature forecast device failed\n");
		goto err_register_dev;
	}

	pr_info("successfully\n");

	return 0;

err_register_dev:
	tran_device_unregister(info->temp_forecast_dev);
	mutex_destroy(&info->update_rate_lock);
	mutex_destroy(&info->suspend_lock);
	return ret;
}

static int temp_forecast_suspend(struct device *dev)
{
	struct temp_forecast_info *info = dev_get_drvdata(dev);

	if (info == NULL) {
		pr_err("%s: info is null\n", __func__);
		return 0;
	}
	mutex_lock(&info->suspend_lock);
	info->suspend_flag = true;
	mutex_unlock(&info->suspend_lock);
	pr_info("%s\n", __func__);

	return 0;
}

static void temp_forecast_resume(struct device *dev)
{
	struct temp_forecast_info *info = dev_get_drvdata(dev);

	if (info == NULL) {
		pr_err("%s: info is null\n", __func__);
		return;
	}
	mutex_lock(&info->suspend_lock);
	info->suspend_flag = false;
	mutex_unlock(&info->suspend_lock);
	pr_info("%s\n", __func__);

	return;
}

static const struct dev_pm_ops temp_forecast_pm_ops = {
	.prepare	= temp_forecast_suspend,
	.complete	= temp_forecast_resume,
};

static int temp_forecast_remove(struct platform_device *pdev)
{
	struct temp_forecast_info *info = platform_get_drvdata(pdev);

	if (info != NULL) {
		mutex_destroy(&info->update_rate_lock);
		mutex_destroy(&info->suspend_lock);
	}

	return 0;
}

static void temp_forecast_shutdown(struct platform_device *dev)
{
	return;
}

static const struct of_device_id temp_forecast_of_match[] = {
	{.compatible = "tc,temp_forecast",},
	{},
};
MODULE_DEVICE_TABLE(of, temp_forecast_of_match);

static struct platform_driver temp_forecast_platdrv = {
	.probe = temp_forecast_probe,
	.remove = temp_forecast_remove,
	.shutdown = temp_forecast_shutdown,
	.driver = {
		.name = "temp_forecast",
		.owner = THIS_MODULE,
		.pm = &temp_forecast_pm_ops,
		.of_match_table = temp_forecast_of_match,
	},
};

static int __init temp_forecast_init(void)
{
	return platform_driver_register(&temp_forecast_platdrv);
}
module_init(temp_forecast_init);

static void __exit temp_forecast_exit(void)
{
	platform_driver_unregister(&temp_forecast_platdrv);
}
module_exit(temp_forecast_exit);

MODULE_DESCRIPTION("Transsion WATER DETECTION FUNCTION");
MODULE_AUTHOR("Duck");
MODULE_VERSION("1.0.0_G");
MODULE_LICENSE("GPL");
