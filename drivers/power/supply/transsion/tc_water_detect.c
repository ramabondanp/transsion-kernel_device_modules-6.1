// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 Transsion Inc.
 */

#define pr_fmt(fmt)     "[WATER_DETECT] %s: " fmt, __func__
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
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include "tc_charger.h"
#include "tc_misc_intf.h"
#include "tc_water_detect.h"


static bool g_fake_flooding_water;
module_param(g_fake_flooding_water, bool, 0644);

extern void tran_ignore_water(void);

struct water_detect_info {
	struct platform_device *pdev;
	struct device *dev;
	struct tran_device *wd_dev;
	struct tran_device *tc_audio;
	struct tran_device *tc_chg;
	struct tran_properties water_detect_props;
	struct charger_device *swchg_dev;
	bool is_water;
	int water_pl_bound; // pull low bound
	int water_ph_lbound; // pull high low bound
	int water_ph_hbound; // pull high high bound

	wait_queue_head_t  wait_que;
	int monitor_flag;
	int polling_interval;
	struct hrtimer wd_hrtimer;
	struct task_struct *wd_monitor;
	struct wakeup_source *suspend_lock;
	struct mutex thread_lock;

	int plug_in_state;
	int user_cmd;
	bool wd_kpoc;
	bool suspend_flag;

	/* scheme gpio */
	int scheme;
	int gpio_x;
	int gpio_y;
	int gpio_z;

	bool is_doing_flag;
	bool is_shutdown_flag;
};

static void wd_send_data(struct water_detect_info *info, int msg_type)
{
	(void)info;
	pr_info("msg_type:%d\n", msg_type);
}

static int tran_water_ctrl_charge(struct water_detect_info *info, bool state)
{
	union com_propval com_val = {0, };

	/* true = discharging */
	com_val.intval = state;
	tran_dev_set_prop(info->tc_chg, TRAN_PROP_WATER_CTRL_CHG, &com_val);

	return 0;
}

static int tran_water_send_uevent_env(struct water_detect_info *info, bool state)
{
        char name_buf[50] = {0};
        char state_buf[50] = {0};
        char *prop_buf = NULL;
        char *envp[3] = {NULL};
        int env_offset = 0;
        int length = 0;

	tran_water_ctrl_charge(info, state);

        prop_buf = (char *)get_zeroed_page(GFP_ATOMIC);
        if (!prop_buf) {
                dev_err(&info->pdev->dev, "out of memory in extcon_set_state\n");
                return -ENOMEM;
        }

        length = sprintf(prop_buf, "%s\n", info->pdev->name);
        if (length > 0) {
                if (prop_buf[length - 1] == '\n')
                        prop_buf[length - 1] = 0;
                snprintf(name_buf, sizeof(name_buf), "UEVENT_NAME=%s", prop_buf);
                envp[env_offset++] = name_buf;
        }

        length = sprintf(prop_buf, "%d\n", state ? 1 : 0);
        if (length > 0) {
                if (prop_buf[length - 1] == '\n')
                        prop_buf[length - 1] = 0;
                snprintf(state_buf, sizeof(state_buf), "WATER_STATE=%s", prop_buf);
                envp[env_offset++] = state_buf;
        }

        envp[env_offset] = NULL;

        dev_err(&info->pdev->dev, "name_buf:%s\n", name_buf);
        dev_err(&info->pdev->dev, "state_buf:%s\n", state_buf);

        kobject_uevent_env(&info->pdev->dev.kobj, KOBJ_CHANGE, envp);

        free_page((unsigned long)prop_buf);

        return 0;
}

int tran_water_plug_out_reset(struct water_detect_info *info)
{
	hrtimer_cancel(&info->wd_hrtimer);
	info->is_water = false;
	info->user_cmd = WD_USER_CMD_UNKNOWN;

	pr_info("tran_water_plug_out_reset\n");
	tran_water_send_uevent_env(info, false);

        return 0;
}

static bool wd_gpio_is_water_detected(struct water_detect_info *info)
{
	int gpio_status = 0;

	gpio_set_value(info->gpio_y, 1);

	gpio_set_value(info->gpio_x, 1);

	msleep(50);

	gpio_status = gpio_get_value(info->gpio_y);
	if (gpio_status == 1) {
		pr_err("SBUx the earthing short circuit, gpio_status:%d\n", gpio_status);
		return true;
	}
	pr_info("debug 1, gpio_status:%d\n", gpio_status);

	gpio_set_value(info->gpio_x, 0);

	msleep(50);

	gpio_status = gpio_get_value(info->gpio_y);
	if (gpio_status == 0) {
		pr_err("SBUx the vbus is short-circuited, gpio_status:%d\n", gpio_status);
		return true;
	}
	pr_info("debug 2, gpio_status:%d\n", gpio_status);

	return false;
}

static void usb_id_switch_gpio_control(struct water_detect_info *info,bool en)
{
	if(en)
		gpio_set_value(info->gpio_z, 1);
	else
		gpio_set_value(info->gpio_z, 0);

	pr_err("%s:water switch gpio en =%d\n",__func__,en);
	return;
}

static void wd_check_sbu1_sbu2(struct water_detect_info *info)
{
	bool sbu1_val = false;
	bool sbu2_val = false;
	union com_propval tran_val = {0, };

	if (info->is_doing_flag == true) {
		pr_err("water_detected is_doing skip\n");
		return;
	}

	if (IS_ERR_OR_NULL(info->tc_audio)){
		info->tc_audio = tran_get_by_name("tc_audio");
		if (IS_ERR_OR_NULL(info->tc_audio)) {
			pr_err("get tc_audio fail\n");
			return;
		}
	}

	info->is_doing_flag = true;

	//select sbu1
	tran_val.intval = true;
	tran_dev_set_prop(info->tc_audio,TRAN_PROP_WD_SELECT_SBU_1_2, &tran_val);
	//set_sw_gpio_switch_sbu(1);
	msleep(30);
	if(info->scheme == SCHEME_USB_ID_AND_GPIO_AND_SBU_1_2)
		charger_dev_is_water_detected(info->swchg_dev, &sbu1_val);
	else if(info->scheme == SCHEME_GPIO_AND_SBU_1_2)
	{
	}

	//select sbu2
	tran_val.intval = false;
	tran_dev_set_prop(info->tc_audio,TRAN_PROP_WD_SELECT_SBU_1_2, &tran_val);
	//set_sw_gpio_switch_sbu(0);
	msleep(30);
	if(info->scheme == SCHEME_USB_ID_AND_GPIO_AND_SBU_1_2)
		charger_dev_is_water_detected(info->swchg_dev, &sbu2_val);
	else if(info->scheme == SCHEME_GPIO_AND_SBU_1_2)
   {
   }

	info->is_water = sbu1_val | sbu2_val;

	info->is_doing_flag = false;
}

static void tran_water_detect(struct water_detect_info *info)
{
	union com_propval tran_val = {0, };

	if(info->scheme == SCHEME_USB_SWITCH) {
		if (IS_ERR_OR_NULL(info->tc_audio)){
			info->tc_audio = tran_get_by_name("tc_audio");
			if (IS_ERR_OR_NULL(info->tc_audio)) {
				pr_err("get tc_audio fail\n");
				return;
			}
		}
	}

	if (g_fake_flooding_water) {
		pr_info("trigger fake water\n");
		info->is_water = true;
		return;
	}

	switch (info->scheme){
	case SCHEME_GPIO:
		info->is_water = wd_gpio_is_water_detected(info);
		break;
	case SCHEME_GPIO_AND_SBU_1_2:
		if(info->plug_in_state)
			wd_check_sbu1_sbu2(info);
		break;
	case SCHEME_USB_SWITCH:
		tran_dev_get_prop(info->tc_audio,TRAN_PROP_WATER_DETECT, &tran_val);
		info->is_water = tran_val.intval;
		break;
	case SCHEME_SUBPMIC:
	case SCHEME_USB_ID_AND_GPIO:
		if(info->plug_in_state)
			charger_dev_is_water_detected(info->swchg_dev, &info->is_water);
		break;
	case SCHEME_USB_ID_AND_GPIO_AND_SBU_1_2:
		if(info->plug_in_state)
			wd_check_sbu1_sbu2(info);
		break;
	default:
		break;
	}
	pr_err("%s:scheme is:%d,is_water:%d\n",__func__,info->scheme,info->is_water);
}

static int wd_update_status(struct water_detect_info *info);
static int water_detect_get_property(struct tran_device *dev,
				enum tran_common_prop prop,
				union com_propval *val)
{
	return 0;
}

static int water_detect_set_property(struct tran_device *dev,
					   enum tran_common_prop prop,
					   const union com_propval *val)
{
	struct water_detect_info *info = tran_get_data(dev);
	int ret = 0;

	switch (prop) {
	case TRAN_PROP_WD_USER_CMD:
		info->user_cmd = val->intval;
		if (info->user_cmd == WD_USER_FORCE_CHARGING) {
			tran_water_ctrl_charge(info, false);
			wd_send_data(info,WD_USER_SELECT_FORCE_CHARGING);
		} else if (info->user_cmd == WD_USER_DISCHARGING) {
			tran_water_ctrl_charge(info, true);
			wd_send_data(info,WD_USER_SELECT_DISCHARGING);
		}

		pr_err("user_cmd:%d\n", info->user_cmd);
		break;
	case TRAN_PROP_USB_PLUG_IN:
		pr_info("TRAN_PROP_USB_PLUG_IN\n");
		info->plug_in_state = true;

		if (info->scheme == SCHEME_USB_ID_AND_GPIO)
			usb_id_switch_gpio_control(info,true);
		else if (info->scheme == SCHEME_USB_ID_AND_GPIO_AND_SBU_1_2)
			usb_id_switch_gpio_control(info,true);

		if (info->user_cmd == WD_USER_CMD_UNKNOWN)
			wd_update_status(info);
		break;
	case TRAN_PROP_USB_PLUG_OUT:
		pr_info("TRAN_PROP_USB_PLUG_OUT\n");
		info->plug_in_state = false;
		if(info->scheme == SCHEME_USB_ID_AND_GPIO)
			usb_id_switch_gpio_control(info,false);
		else if(info->scheme == SCHEME_USB_ID_AND_GPIO_AND_SBU_1_2)
			usb_id_switch_gpio_control(info,false);
		tran_water_plug_out_reset(info);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static struct tran_ops water_detect_ops = {
	.get_prop = water_detect_get_property,
	.set_prop = water_detect_set_property,
};

static int water_detect_prop_init(struct water_detect_info *info)
{

        info->water_detect_props.alias_name = "water_detect";
	info->wd_dev = tran_device_register("water_detect",
						info->dev, info,
						&water_detect_ops,
						&info->water_detect_props);
	if (IS_ERR_OR_NULL(info->wd_dev))
		return -ENODEV;

	return 0;
}

static int water_detect_parse_dt(struct water_detect_info *info,
				struct device *dev)
{
	int ret = 0;
	struct device_node *np = dev->of_node;

	ret = of_property_read_u32(np, "polling_interval", &info->polling_interval);
	if (ret < 0) {
		pr_err("parse polling_interval failed ret = %d",ret);
		info->polling_interval = 5;
	}

	ret = of_property_read_u32(np, "scheme", &info->scheme);
	if (ret < 0) {
		pr_err("parse scheme failed,use default 0, ret:%d\n", ret);
		info->scheme = SCHEME_SUBPMIC;
	}

	if (info->scheme == SCHEME_SUBPMIC)
		goto out;

	if ((info->scheme == SCHEME_USB_ID_AND_GPIO) || (info->scheme == SCHEME_USB_ID_AND_GPIO_AND_SBU_1_2)){
		info->gpio_z = of_get_named_gpio(np, "gpio_z", 0);
		if (info->gpio_z < 0 || info->gpio_z == U32_MAX) {
			pr_err("parse gpio_z failed, gpio_z:%d\n", info->gpio_z);
			goto out;
		}
		ret = gpio_request(info->gpio_z, "gpio_z");
		if (ret < 0) {
			pr_err("gpio_z request failed, ret:%d\n", ret);
			goto out;
		}
		ret = gpio_direction_output(info->gpio_z, 0);
		if (ret < 0) {
			pr_err("fail to set gpio_z dir\n");
			goto out;
		}
		goto out;
	}

	info->gpio_x = of_get_named_gpio(np, "gpio_x", 0);
	if (info->gpio_x < 0 || info->gpio_x == U32_MAX) {
		pr_err("parse gpio_x failed, gpio_x:%d\n", info->gpio_x);
		goto out;
	}

	ret = gpio_request(info->gpio_x, "gpio_x");
	if (ret < 0) {
		pr_err("gpio_x request failed, ret:%d\n", ret);
		goto out;
	}

	info->gpio_y = of_get_named_gpio(np, "gpio_y", 0);
	if (info->gpio_y < 0 || info->gpio_y == U32_MAX) {
		pr_err("parse gpio_y failed, gpio_y:%d\n", info->gpio_y);
		goto out;
	}

	ret = gpio_request(info->gpio_y, "gpio_y");
	if (ret < 0) {
		pr_err("gpio_y request failed, ret:%d\n", ret);
		goto out;
	}

out:
	return ret;
}

static int wd_cloud_enable(struct water_detect_info *info)
{
	(void)info;
	return 1;
}

static int wd_update_status(struct water_detect_info *info)
{
	ktime_t ktime = ktime_set(info->polling_interval, 0);

	if(info->wd_kpoc){
		return 0;
	}

	if (!wd_cloud_enable(info)) {
		info->is_water = false;
		return 0;
	}
	if (info->suspend_flag)
		return 0;

	if (!info->is_water && info->plug_in_state)
		tran_water_detect(info);

	/*
	 * water detect is time-consuming work
	 * Mybe usb is already been plug out, after water detect
	*/
	if (!info->plug_in_state)
		info->is_water = false;

	if (info->is_water && tc_get_boot_finish()) {
		tran_water_send_uevent_env(info, info->is_water);
		wd_send_data(info,WD_TRIGGER);
		pr_info("trigger is water = %d\n", info->is_water);
		return 0;
	}

	pr_info("is_water = %d\n", info->is_water);

	if (info->plug_in_state)
		hrtimer_start(&info->wd_hrtimer, ktime, HRTIMER_MODE_REL);
	return 0;
}

static int water_detect_monitor(void *data)
{
	struct water_detect_info *info = data;

	while (!kthread_should_stop()) {
		wait_event_interruptible(info->wait_que,
			info->monitor_flag > 0 || kthread_should_stop());

		if (kthread_should_stop())
			goto out;

		if (info->is_shutdown_flag == true)
			goto out;

		__pm_stay_awake(info->suspend_lock);
		mutex_lock(&info->thread_lock);

		if (info->monitor_flag > 0 && !info->wd_kpoc) {
			info->monitor_flag = 0;
			wd_update_status(info);
		}

		mutex_unlock(&info->thread_lock);
		__pm_relax(info->suspend_lock);
	}
out:
	return 0;
}

static void water_detect_monitor_wakeup(struct water_detect_info *info)
{
	info->monitor_flag = 1;
	wake_up(&info->wait_que);
}

static enum hrtimer_restart wd_hrtimer_func(struct hrtimer *timer)
{
	struct water_detect_info *info = container_of(timer, struct water_detect_info, wd_hrtimer);

	water_detect_monitor_wakeup(info);

	return HRTIMER_NORESTART;
}

static void wd_thread_hrtimer_init(struct water_detect_info *info)
{
	pr_info("%s\n", __func__);

	hrtimer_init(&info->wd_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	info->wd_hrtimer.function = wd_hrtimer_func;
}

static void wd_gpio_init(struct water_detect_info *info)
{
	int ret;

	ret = gpio_direction_output(info->gpio_x, 0);
	if (ret < 0) {
		pr_err("fail to set gpio_x dir\n");
		return;
	}

	ret = gpio_direction_output(info->gpio_y, 0);
	if (ret < 0) {
		pr_err("fail to set gpio_y dir\n");
		return;
	}
}

static int water_detect_probe(struct platform_device *pdev)
{
	int ret;
	struct water_detect_info *info;
	int boot_mode = 11; // UNKNOWN_BOOT

	pr_info("enter\n");

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);
	info->pdev = pdev;

	mutex_init(&info->thread_lock);

	info->is_doing_flag = false;
	info->is_shutdown_flag = false;
	info->is_water = false;
	info->scheme = -1;

	ret = water_detect_parse_dt(info, info->dev);
	if(ret < 0) {
		pr_err("water detect parse dts failed, ret = %d", ret);
	}

	if (info->scheme == SCHEME_GPIO)
		wd_gpio_init(info);

	info->swchg_dev = get_charger_by_name("primary_chg");
	if (!info->swchg_dev) {
		ret = -EPROBE_DEFER;
		pr_err("*** Error : can't find primary switch charger ***\n");
		goto err_get_swchg;
	}

	info->tc_chg = tran_get_by_name("tc_charger");
	if (IS_ERR_OR_NULL(info->tc_chg)) {
		ret = -EPROBE_DEFER;
		pr_err("*** Error : can't find tc charger ***\n");
		goto err_get_swchg;
	}

	ret = water_detect_prop_init(info);
	if (ret < 0) {
		ret = -ENODEV;
		pr_info("register water detect device failed\n");
		goto err_register_dev;
	}

	boot_mode = tc_get_boot_mode();

	/* 8 = KERNEL_POWER_OFF_CHARGING_BOOT */
	/* 9 = LOW_POWER_OFF_CHARGING_BOOT */
	if (boot_mode == 8 || boot_mode == 9)
		info->wd_kpoc = true;

	info->suspend_lock =
		wakeup_source_register(NULL, "water detect seriver");

	init_waitqueue_head(&info->wait_que);
	info->wd_monitor = kthread_run(water_detect_monitor, info, "water_detect_serivce");
	if (IS_ERR(info->wd_monitor)) {
		ret = PTR_ERR(info->wd_monitor);
		pr_err("%s: fail to register water_detect_serivce kthread, ret:%d\n",
			__func__, ret);
		goto err_kthread_run;
	}
	wd_thread_hrtimer_init(info);

	pr_info("successfully, boot_mode = %d\n", boot_mode);

	return 0;

err_kthread_run:
err_register_dev:
	tran_device_unregister(info->wd_dev);
err_get_swchg:
	mutex_destroy(&info->thread_lock);
	devm_kfree(&pdev->dev, info);
	return ret;
}

static int wd_suspend(struct device *dev)
{
	struct water_detect_info *info = dev_get_drvdata(dev);
	if (info == NULL) {
		pr_err("%s: info is null\n", __func__);
		return 0;
	}
	pr_info("%s\n", __func__);

	mutex_lock(&info->thread_lock);
	info->suspend_flag = true;
	hrtimer_cancel(&info->wd_hrtimer);
	mutex_unlock(&info->thread_lock);
	return 0;
}

static int wd_resume(struct device *dev)
{
	struct water_detect_info *info = dev_get_drvdata(dev);
	ktime_t ktime = ktime_set(0, 0);

	if (info == NULL) {
		pr_err("%s: info is null\n", __func__);
		return 0;
	}
	pr_info("%s\n", __func__);

	mutex_lock(&info->thread_lock);
	if (info->plug_in_state && !info->wd_kpoc && !info->is_water)
		hrtimer_start(&info->wd_hrtimer, ktime, HRTIMER_MODE_REL);
	info->suspend_flag = false;
	mutex_unlock(&info->thread_lock);

	return 0;
}

static const struct dev_pm_ops water_detect_pm_ops = {
	.resume		= wd_resume,
	.suspend	= wd_suspend,
};

static int water_detect_remove(struct platform_device *pdev)
{
	struct water_detect_info *info = platform_get_drvdata(pdev);

	pr_info("%s\n", __func__);

	if (!IS_ERR_OR_NULL(info->wd_dev))
		tran_device_unregister(info->wd_dev);

	return 0;
}

static void water_detect_shutdown(struct platform_device *pdev)
{
	struct water_detect_info *info = platform_get_drvdata(pdev);

	info->is_shutdown_flag = true;
	pr_err("%s\n", __func__);

	return ;
}

static const struct of_device_id water_detect_of_match[] = {
	{.compatible = "transsion, water_detect",},
	{},
};
MODULE_DEVICE_TABLE(of, water_detect_of_match);

static struct platform_driver water_detect_platdrv = {
	.probe = water_detect_probe,
	.remove = water_detect_remove,
	.shutdown = water_detect_shutdown,
	.driver = {
		.name = "tran_water_detect",
		.owner = THIS_MODULE,
		.pm = &water_detect_pm_ops,
		.of_match_table = water_detect_of_match,
	},
};

static int __init water_detect_init(void)
{
	return platform_driver_register(&water_detect_platdrv);
}
late_initcall(water_detect_init);

static void __exit water_detect_exit(void)
{
	platform_driver_unregister(&water_detect_platdrv);
}
module_exit(water_detect_exit);

MODULE_DESCRIPTION("Transsion WATER DETECTION FUNCTION");
MODULE_AUTHOR("Schack");
MODULE_VERSION("1.0.0_G");
MODULE_LICENSE("GPL");

