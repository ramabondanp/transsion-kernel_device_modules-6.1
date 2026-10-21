// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Transsion Inc.
 */

#define pr_fmt(fmt)     "[port_burn] %s: " fmt, __func__
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/delay.h>
#include "tc_misc_intf.h"
#include "tc_charger.h"
#include "tc_common_class.h"
#define PORT_VALID_VOL 2500
#define VBUS_VALID_VOL 6500

struct burn_info {
	struct platform_device *pdev;
	struct device *dev;
	struct tran_device *tc_chg;
	struct tran_device *burn_dev;
	struct tran_device *tc_phy_det;
	struct notifier_block phy_det_notifier;
	struct power_supply *chg_psy;
	struct tran_properties port_burn_props;
	struct wakeup_source *burn_wakelock;
	struct mutex burn_lock;
	struct mutex control_lock;
	struct alarm burn_alarm;
	struct timespec64 endtime;
	wait_queue_head_t wait_que;
	bool burn_thread_timeout;
	bool burning_flag;
	bool plug_in;
	bool is_tc30;
	bool is_rfc;
	bool adapter_cut_off;
	bool kpoc;
	bool phy_plug_in;
	bool phy_det_support;
	int control_gpio;
	/* int abnormal_resistance_temp; */
	int burn_max_temp;
	int burn_gap_temp;
	int burning_interval;
	int normal_interval;
	int pre_temp;
	int curr_temp;
};

enum debug_node {
	BURN_MAX_TEMP = 0,
	BURN_GAP_TEMP,
	/* BURN_RESISTANCE_TEMP, */
	BURNING_INTERVAL,
	NORMAL_INTERVAL,
	NOTIFY_ON,
	NOTIFY_OFF,
	TRANCARE,
};

enum port_state {
	PORT_NORMAL = 0,
	PORT_BURNING,
	PORT_MAX,
};

struct tag_bootmode {
	u32 size;
	u32 tag;
	u32 bootmode;
	u32 boottype;
};

static void port_burn_send_data(struct burn_info *info)
{
	(void)info;
	pr_info("port burn occurred\n");
}

static int port_burn_set_uevent_env(struct burn_info *info, int state)
{
	char name_buf[120] = {0};
	char state_buf[120] = {0};
	char *prop_buf = NULL;
	char *envp[3] = {NULL};
	int env_offset = 0;
	int length = 0;
	prop_buf = (char *)get_zeroed_page(GFP_ATOMIC);
	if (!prop_buf) {
		dev_err(&info->pdev->dev, "out of memory in port burn\n");
		return -ENOMEM;
	}
	length = sprintf(prop_buf, "%s\n", info->pdev->name);
	if (length > 0) {
		if (prop_buf[length - 1] == '\n')
			prop_buf[length - 1] = 0;
		snprintf(name_buf, sizeof(name_buf), "PORT_NAME=%s", prop_buf);
		envp[env_offset++] = name_buf;
	}
	length = sprintf(prop_buf, "%d\n",state);
	if (length > 0) {
		if (prop_buf[length - 1] == '\n')
			prop_buf[length - 1] = 0;
		snprintf(state_buf, sizeof(state_buf), "PORT_STATE=%s", prop_buf);
		envp[env_offset++] = state_buf;
	}
	envp[env_offset] = NULL;
	pr_err("name_buf:%s \n",name_buf);
	pr_err("state_buf:%s \n",state_buf);
	kobject_uevent_env(&info->pdev->dev.kobj, KOBJ_CHANGE, envp);
	free_page((unsigned long)prop_buf);
	return 0;
}

static bool is_marked_adapter(struct burn_info *info)
{
	return info->is_tc30 || info->is_rfc;
}

static int port_burn_get_vbus(struct burn_info *info)
{
	return tc_get_vbus();
}

static int port_burn_ctrl_charge(struct burn_info *info, bool state)
{
	int i, ret = 0;
	int vbus = 0;
	int vbus_cont = 40;
	union com_propval com_val = {0, };
	/* true = discharging */
	com_val.intval = state;
	ret = tran_dev_set_prop(info->tc_chg, TRAN_PROP_PORT_BURN_VOTE, &com_val);
	if (ret < 0) {
		pr_err("port burn set prop failed(%d)\n", ret);
		return ret;
	}

	if (state) {
		for (i = 0; i < vbus_cont; i++){
			vbus = port_burn_get_vbus(info);
			if (vbus <= VBUS_VALID_VOL) {
				pr_err("port burn vbus(%d)\n", vbus);
				break;
			} else {
				pr_err("wait vbus(%d) down,cnt:%d\n", vbus, i);
			}
			msleep(50);
		}
	}
	return ret;
}

static int port_burn_exit_protocol(struct burn_info *info)
{
	int ret = 0;
	struct tran_device *usb_control_dev = NULL;
	union com_propval tran_val = {0, };

	usb_control_dev = tran_get_by_name("usb_control");
	if (IS_ERR_OR_NULL(usb_control_dev)) {
		pr_err("%s get usb control device fail!\n", __func__);
		return -EINVAL;
	}

	tran_val.intval = RFC_NONE;
	ret = tran_dev_set_prop(usb_control_dev,
			TRAN_PROP_USB_CTRL_RFC, &tran_val);

	msleep(150);

	return ret;

}

static void port_burn_ctrl_adapter(struct burn_info *info, bool enable)
{
	if (info->control_gpio < 0)
		return;
	if (info->adapter_cut_off == enable)
		return;
	if (is_marked_adapter(info) || info->adapter_cut_off) {
		if (enable)
			port_burn_exit_protocol(info);
		gpio_set_value(info->control_gpio, enable);
		info->adapter_cut_off = enable;
		pr_info("%s vbus and gnd for port burning\n", enable ? "short" : "release");
	}
}

static void port_burning_protect(struct burn_info *info, bool enable)
{
        pr_info("port burning protect:%d\n", enable);
	mutex_lock(&info->control_lock);
	port_burn_ctrl_charge(info, enable);
	port_burn_ctrl_adapter(info, enable);
	mutex_unlock(&info->control_lock);
}

void wake_up_burn_thread(struct burn_info *info)
{
	if (IS_ERR_OR_NULL(info))
		return;
	if (!info->burn_wakelock->active)
		__pm_stay_awake(info->burn_wakelock);
	info->burn_thread_timeout = true;
	wake_up_interruptible(&info->wait_que);
}
static enum alarmtimer_restart
	burn_alarm_timer_func(struct alarm *alarm, ktime_t now)
{
	struct burn_info *info =
		container_of(alarm, struct burn_info, burn_alarm);
	wake_up_burn_thread(info);
	return ALARMTIMER_NORESTART;
}

static void port_burn_start_timer(struct burn_info *info)
{
	struct timespec64 end_time, time_now;
	ktime_t ktime, ktime_now;
	int ret = 0;
	if (info->kpoc) {
		pr_info("kpoc skip det!");
		return;
	}
	/* If the timer was already set, cancel it */
	ret = alarm_try_to_cancel(&info->burn_alarm);
	if (ret < 0) {
		pr_err("%s: callback was running\n", __func__);
	}
	ktime_now = ktime_get_boottime();
	time_now = ktime_to_timespec64(ktime_now);
	if (info->burning_flag) {
		end_time.tv_sec = time_now.tv_sec + info->burning_interval;
		end_time.tv_nsec = time_now.tv_nsec;
	} else {
		end_time.tv_sec = time_now.tv_sec + info->normal_interval;
		end_time.tv_nsec = time_now.tv_nsec;
	}
	info->endtime = end_time;
	ktime = ktime_set(info->endtime.tv_sec, info->endtime.tv_nsec);
	pr_err("%s: alarm timer start:%d, %lld %ld\n", __func__, ret,
		(long long)info->endtime.tv_sec, info->endtime.tv_nsec);
	alarm_start(&info->burn_alarm, ktime);
}

static bool port_burn_init(struct burn_info *info)
{
	static bool init_done = false;
	if (init_done)
		return true;
	if (!info->tc_chg) {
		info->tc_chg = tran_get_by_name("tc_charger");
		if (!info->tc_chg) {
			pr_err("get tc device fail\n");
			return false;
		}
	}
	if (!info->chg_psy) {
		info->chg_psy = power_supply_get_by_name("charger");
		if (!info->chg_psy) {
			pr_err("get charger psy fail\n");
			return false;
		}
	}
	init_done = true;
	return true;
}

static void port_burn_det(struct burn_info *info)
{
	int ret = 0;
	int vbus = 0;
	pr_err("%s : enter\n", __func__);
	if (!port_burn_init(info)) {
		pr_info("wait port_burn init done");
		return;
	}
	if (info->kpoc) {
		pr_info("kpoc skip det!");
		return;
	}
	if (!tc_get_boot_finish()) {
		pr_info("wait boot complete!");
		return;
	}
	info->pre_temp = info->curr_temp;
	ret = tc_get_port_temp(&info->curr_temp);
	if (ret < 0) {
		pr_info("get port_temp failed(%d)\n", ret);
	}

	if (!info->burning_flag) {
		if (!info->plug_in)
			return;
		if (info->curr_temp >= info->burn_max_temp &&
			info->curr_temp != info->pre_temp) {
			pr_err("PORT BURNINGN OCCRUED (%d, %d)\n",
				info->curr_temp, info->pre_temp);
			info->burning_flag = true;
			port_burn_set_uevent_env(info, PORT_BURNING);
			port_burning_protect(info, true);
			port_burn_send_data(info);
			msleep(500);
			vbus = port_burn_get_vbus(info);
			if (info->adapter_cut_off && vbus > PORT_VALID_VOL) {
				pr_err("PORT RESISTANCE AGING (%d, %d)\n",
					vbus, PORT_VALID_VOL);
				port_burn_ctrl_adapter(info, false);
			
			}
		}
	} else {
		if (info->adapter_cut_off && vbus > PORT_VALID_VOL) {
			pr_err("PORT RESISTANCE AGING (%d, %d)\n",
				vbus, PORT_VALID_VOL);
			port_burn_ctrl_adapter(info, false);
		
		}
		if ((info->curr_temp < info->burn_max_temp - info->burn_gap_temp)) {
			pr_err("PORT BURNINGN CANCEL %d(%d, %d)\n",
				info->phy_plug_in, info->curr_temp, info->pre_temp);
			info->burning_flag = false;
			port_burn_set_uevent_env(info, PORT_NORMAL);
			port_burning_protect(info, false);
		}
	}
}

static int port_burn_thread(void *arg)
{
	int ret;
	struct burn_info *info = arg;
	while (1) {
		ret = wait_event_interruptible(info->wait_que,
			(info->burn_thread_timeout == true));
		if (ret < 0) {
			pr_err("%s: wait event been interrupted(%d)\n", __func__, ret);
			continue;
		}
		info->burn_thread_timeout = false;
		mutex_lock(&info->burn_lock);
		if (!info->burn_wakelock->active)
			__pm_stay_awake(info->burn_wakelock);
		port_burn_det(info);
		if (info->plug_in || info->burning_flag) {
			port_burn_start_timer(info);
		}
		__pm_relax(info->burn_wakelock);
		mutex_unlock(&info->burn_lock);
	}
	return 0;
}

static int burn_get_property(struct tran_device *dev,
				enum tran_common_prop prop,
				union com_propval *val)
{
	int ret = 0;
	switch (prop) {
		default:
			ret = -EINVAL;
	}
	return ret;
}

static int burn_set_property(struct tran_device *dev,
					   enum tran_common_prop prop,
					   const union com_propval *val)
{
	int ret = 0;
	struct burn_info *info = tran_get_data(dev);
	switch (prop) {
		case TRAN_PROP_USB_PLUG_IN:
			info->plug_in = true;
			wake_up_burn_thread(info);
			pr_info("port plug in\n");
			break;
		case TRAN_PROP_USB_PLUG_OUT:
			info->plug_in = false;
			info->is_tc30 = false;
			info->is_rfc = false;
			break;
		case TRAN_PROP_IS_TC30_TA:
			info->is_tc30 = true;
			break;
		case TRAN_PROP_IS_RFC_TA:
			info->is_rfc = true;
			break;
		default:
			ret = -EINVAL;
	}
	return ret;
}

static struct tran_ops port_burn_ops = {
	.get_prop = burn_get_property,
	.set_prop = burn_set_property,
};

static int burn_prop_init(struct burn_info *info)
{
        info->port_burn_props.alias_name = "port_burn";
	info->burn_dev = tran_device_register("port_burn",
						info->dev, info,
						&port_burn_ops,
						&info->port_burn_props);
	if (IS_ERR_OR_NULL(info->burn_dev))
		return -ENODEV;
	return 0;
}

static ssize_t burn_debug_set_prop(struct device* dev,
	struct device_attribute *attr, const char* buf, size_t len)
{
	int databuf[6];
	struct burn_info *info = dev_get_drvdata(dev);
	if (buf == NULL || len == 0)
		return len;
    	sscanf(buf, "%d %d %d", &databuf[0], &databuf[1], &databuf[2]);
	switch (databuf[0]) {
		case BURN_MAX_TEMP:
		    info->burn_max_temp = databuf[1];
		    break;
		case BURN_GAP_TEMP:
		    info->burn_gap_temp  = databuf[1];
		    break;
		/* case BURN_RESISTANCE_TEMP: */
		/*     info->abnormal_resistance_temp = databuf[1]; */
		/*     break; */
		case BURNING_INTERVAL:
		    info->burning_interval = databuf[1];
		    break;
		case NORMAL_INTERVAL:
		    info->normal_interval = databuf[1];
		    break;
		case NOTIFY_ON:
		    port_burn_set_uevent_env(info, PORT_BURNING);
		    break;
		case NOTIFY_OFF:
		    port_burn_set_uevent_env(info, PORT_NORMAL);
		    break;
		case TRANCARE: 
		    port_burn_send_data(info);
		    break;
		default:
		    pr_err("error input\n");
	
	}
	return len;
}

static ssize_t burn_debug_get_prop(struct device* dev,
	struct device_attribute *attr, char* buf)
{
	return sprintf(buf, "not support\n");
}

static DEVICE_ATTR(burn_debug, 0660,
	burn_debug_get_prop, burn_debug_set_prop);
static struct attribute* burn_sysfs_attrs[] = {
	&dev_attr_burn_debug.attr,
	NULL,
};

static const struct attribute_group burn_sysfs_group = {
	.name  = "burn",
	.attrs = burn_sysfs_attrs,
};

static int port_burn_parse_dt(struct burn_info *info,
	struct device *dev)
{
	int ret = 0;
	struct device_node *boot_node = NULL;
	struct tag_bootmode *tag = NULL;
	int boot_mode = 11; // UNKNOWN_BOOT
	struct device_node *np = dev->of_node;
	boot_node = of_parse_phandle(info->dev->of_node, "bootmode", 0);
	if (!boot_node) {
		pr_err("failed to get boot mode phandle\n");
	} else {
		tag = (struct tag_bootmode *)of_get_property(boot_node, "atag,boot", NULL);
		if (!tag)
			pr_err("failed to get atag,boot\n");
		else
			boot_mode = tag->bootmode;
	}
	/* 8 = KERNEL_POWER_OFF_CHARGING_BOOT */
	/* 9 = LOW_POWER_OFF_CHARGING_BOOT */
	if (boot_mode == 8 || boot_mode == 9)
		info->kpoc = true;

	ret = of_property_read_u32(np, "burn_max_temp", &info->burn_max_temp);
	if (ret < 0) {
		pr_err("parse burn_max_temp failed ret = %d",ret);
		goto out;
	}
		pr_err("parse burn_max_temp  = %d", info->burn_max_temp);
	/* ret = of_property_read_u32(np, "abnormal_resistance_temp", */
	/*                 &info->abnormal_resistance_temp); */
	/* if (ret < 0) { */
	/*         pr_err("parse abnormal_resistance_temp failed ret = %d",ret); */
	/*         goto out; */
	/* } */
	ret = of_property_read_u32(np, "burn_gap_temp", &info->burn_gap_temp);
	if (ret < 0) {
		pr_err("parse burn_low_power_update_time failed ret = %d",ret);
		ret = 0;
		goto out;
	}
	ret = of_property_read_u32(np, "burning_interval", &info->burning_interval);
	if (ret < 0) {
		pr_err("parse burning_interval failed ret = %d",ret);
		goto out;
	}
	ret = of_property_read_u32(np, "normal_interval", &info->normal_interval);
	if (ret < 0) {
		pr_err("parse normal_interval failed ret = %d",ret);
		goto out;
	}
	info->control_gpio = of_get_named_gpio(np, "control_gpio", 0);
        if (info->control_gpio < 0) {
                pr_info("control_gpio = %d get fail\n", info->control_gpio);
		goto out;
        }
	ret = gpio_request(info->control_gpio, "burn_control_gpio");
	if (ret < 0){
		ret = 0;
	  	pr_err("control_gpio request failed!\n");
		goto out;
	}
	gpio_set_value(info->control_gpio, 0);
out:
	return ret;
}

static int phy_det_notifier_callback(struct notifier_block *nb,
                    unsigned long event, void *data)
{
	struct burn_info *info = container_of(nb,
			struct burn_info, phy_det_notifier);
	
	if (!info->phy_det_support) {
		pr_info("not support phy_det\n");
		return 0;
	}

	switch (event) {
	case TRAN_DEV_NOTIFY_PHY_PLUG_IN:
		info->phy_plug_in = true;
		pr_info("port burn phy det plug in\n");
		break;
	case TRAN_DEV_NOTIFY_PHY_PLUG_OUT:
		info->phy_plug_in = false;
		wake_up_burn_thread(info);
		pr_info("port burn phy det plug out\n");
		break;
	default:
		break;
	}

	return 0;
}

static int tc_phy_det_notifier_init(struct burn_info *info)
{
	int ret = 0;

	info->tc_phy_det = tran_get_by_name("phy_det");
	if (IS_ERR_OR_NULL(info->tc_phy_det)) {
		pr_err("%s get tc phy_det device fail!\n", __func__);
		return -ENODEV;
		goto out;
	}

	info->phy_det_notifier.notifier_call = phy_det_notifier_callback;
	ret = register_tran_device_notifier(info->tc_phy_det,
				&info->phy_det_notifier);
	if (ret != 0) {
		pr_err("register lcd notify failed, ret = %d\n", ret);
		goto out;
	}

	info->phy_det_support = true;
out:
	return ret;
}

static int port_burn_probe(struct platform_device *pdev)
{
	int ret;
	struct burn_info *info = NULL;
	pr_info("enter\n");
	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);
	info->pdev = pdev;
	mutex_init(&info->burn_lock);
	mutex_init(&info->control_lock);
	init_waitqueue_head(&info->wait_que);
	ret = port_burn_parse_dt(info, info->dev);
	if(ret < 0) {
		pr_err("port burn parse dts failed, ret = %d\n", ret);
		goto err_parse_dt;
	}
	ret = burn_prop_init(info);
	if (ret < 0) {
		ret = -ENODEV;
		pr_info("register burn device failed\n");
		goto err_register_dev;
	}
	info->burn_wakelock =
		wakeup_source_register(NULL, "burn detect");
	alarm_init(&info->burn_alarm, ALARM_BOOTTIME,
		burn_alarm_timer_func);
	kthread_run(port_burn_thread, info, "port_burn");
	ret = sysfs_create_group(&pdev->dev.kobj, &burn_sysfs_group);
	ret = tc_phy_det_notifier_init(info);
	if (ret != 0) {
		pr_err("%s register phy_det notify fail!\n", __func__);
	}
	pr_info("successfully\n");
	return 0;
err_register_dev:
	tran_device_unregister(info->burn_dev);
err_parse_dt:
	mutex_destroy(&info->burn_lock);
	mutex_destroy(&info->control_lock);
	return ret;
}

static int port_burn_prepare_suspend(struct device *dev)
{
//	struct burn_info *info = dev_get_drvdata(dev);
	return 0;
}

static void port_burn_complete_resume(struct device *dev)
{
	//struct burn_info *info = dev_get_drvdata(dev);
}

static const struct dev_pm_ops port_burn_pm_ops = {
	.prepare	= port_burn_prepare_suspend,
	.complete       = port_burn_complete_resume,
};

static int port_burn_remove(struct platform_device *pdev)
{
	return 0;
}

static void port_burn_shutdown(struct platform_device *dev)
{
	return;
}

static const struct of_device_id port_burn_of_match[] = {
	{.compatible = "transsion,port_burning",},
	{},
};

MODULE_DEVICE_TABLE(of, port_burn_of_match);
static struct platform_driver port_burn_platdrv = {
	.probe = port_burn_probe,
	.remove = port_burn_remove,
	.shutdown = port_burn_shutdown,
	.driver = {
		.name = "port_burn",
		.owner = THIS_MODULE,
		.pm = &port_burn_pm_ops,
		.of_match_table = port_burn_of_match,
	},
};

static int __init burn_det_init(void)
{
	return platform_driver_register(&port_burn_platdrv);
}
module_init(burn_det_init);

static void __exit burn_det_exit(void)
{
	platform_driver_unregister(&port_burn_platdrv);
}

module_exit(burn_det_exit);
MODULE_DESCRIPTION("Transsion BURN DETECT FUNCTION");
MODULE_AUTHOR("UNKNOWN");
MODULE_VERSION("1.0.0_G");
MODULE_LICENSE("GPL");
