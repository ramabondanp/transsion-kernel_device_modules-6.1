// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#define pr_fmt(fmt)     "[TA_CONTROL] %s: " fmt, __func__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/alarmtimer.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include "tc_charger_class.h"
#include "tc_adapter_class.h"
#include "tc_ta_class.h"
#include "tc_common_class.h"
#include "tc_misc_intf.h"
#include "tc_charger.h"

static bool g_fake_adapter_off;
module_param(g_fake_adapter_off, bool, 0644);

struct adapter_info {
	struct device *dev;
	struct tran_device *ac_ctl_dev;
	struct tran_properties adapter_control_props;
	struct tc_ta_classdev *rfc;
	struct tran_device *usb_control_dev;
	struct tran_device *tcc_dev;
	struct tran_device *tc_gauge;
	struct tran_device *tc_lcd;
	struct tran_device *tc_phy_det;
	struct notifier_block phy_det_notifier;
	struct notifier_block fb_notifier;
	struct wakeup_source *suspend_lock;

	struct mutex adapter_lock;
	int polling_interval;
	int judge_trigger_off_cnt;
	bool adapter_off;
	bool adapter_support_switch;
	bool adapter_switch_off;
	bool ac_ctl_kpoc;
	bool chg_full;;
	bool adapter_plug_in;
	bool lcd_on_state;
	bool phy_plug_in;
	bool phy_det_support;

	struct notifier_block nb;
	struct work_struct adapter_control_work;
	struct power_supply *chg_psy;
	struct power_supply *batt_psy;
	struct alarm adapter_control_timer;
	struct work_struct judge_trigger_off_work;
	int chg_status;
	int raw_soc;
};

static const char * const POWER_SUPPLY_STATUS_TEXT[] = {
	[POWER_SUPPLY_STATUS_UNKNOWN]		= "Unknown",
	[POWER_SUPPLY_STATUS_CHARGING]		= "Charging",
	[POWER_SUPPLY_STATUS_DISCHARGING]	= "Discharging",
	[POWER_SUPPLY_STATUS_NOT_CHARGING]	= "Not charging",
	[POWER_SUPPLY_STATUS_FULL]		= "Full",
};

static void adapter_control_start_alarmtimer(struct adapter_info *info);

static void adapter_control_pull_up_en(struct adapter_info *info, bool en)
{
	union com_propval tran_val = {0, };

	if (!info->usb_control_dev)
		info->usb_control_dev = tran_get_by_name("usb_control");

	pr_info("TRAN_PROP_USB_CTRL_TA_OFF:%d\n", en);
	tran_val.intval = en;
	tran_dev_set_prop(info->usb_control_dev, TRAN_PROP_USB_CTRL_TA_OFF, &tran_val);
}

static int adapter_control_ac_off(struct adapter_info *info, bool en)
{
	int ret = 0;
	union com_propval com_val = {0, };

	if (info->ac_ctl_kpoc) {
		pr_err("kpoc not support ac off\n");
		return ret;
	}

	if (!info->tcc_dev)
		info->tcc_dev = tran_get_by_name("tran_chg_control");

	if (!info->rfc)
		info->rfc = tc_ta_device_get_by_name("tfcp_ta");

	if (en) {
		com_val.intval = TRAN_ADAPTER_CONTROL;
		tran_dev_set_prop(info->tcc_dev, TRAN_PROP_SET_CHARGER_STATUS, &com_val);

		mdelay(500);

		ret = tc_ta_device_enable_wdt(info->rfc, false);
		if (ret < 0) {
			pr_err("disable wdt failed, ret = %d\n", ret);
			return ret;
		}

		ret = tc_ta_device_set_output_control(info->rfc, false);
		if (ret < 0) {
			pr_err("set output control failed, ret = %d\n", ret);
			return ret;
		}
		info->adapter_switch_off = true;
		adapter_control_pull_up_en(info, true);
	} else {
		info->adapter_switch_off = false;
		adapter_control_pull_up_en(info, false);
		/* wait TA reset */
		mdelay(100);
		com_val.intval = TRAN_ADAPTER_CONTROL;
		tran_dev_set_prop(info->tcc_dev, TRAN_PROP_RESET_CHARGER_STATUS, &com_val);
	}

	return ret;
}

static bool adapter_control_check_ta_switch_support(struct adapter_info *info)
{
	int ret;
	int power;
	bool support = false;
	struct tc_ta_classdev *tfcp_ta = NULL;

	tfcp_ta = tc_ta_device_get_by_name("tfcp_ta");

	/* check ta power */
	ret = tc_ta_device_get_power_limit(tfcp_ta, &power);
	if (ret < 0) {
		pr_err("get power limit failed\n");
		goto out;
	}

	pr_info("rfc ta support power:%dW\n", power);

	if (power > 60)
		goto out;

	/* check ta register */
	ret = tc_ta_device_get_output_control_support(tfcp_ta, &support);
	if (ret < 0) {
		pr_err("get output control failed:%d\n", ret);
		goto out;
	}

	if (support == true)
		goto out;

	return false;
out:
	return true;
}

static int adapter_control_get_property(struct tran_device *dev,
				enum tran_common_prop prop,
				union com_propval *val)
{
	struct adapter_info *info = tran_get_data(dev);
	int ret = 0;

	switch (prop) {
		case TRAN_PROP_ADAPTER_SWITCH_STATUS:
			val->intval = info->adapter_switch_off;
			break;
		default:
			ret = -EINVAL;
	}

	return ret;
}

static void adapter_control_reset(struct adapter_info *info)
{
	// reset status
	alarm_cancel(&info->adapter_control_timer);
	info->adapter_support_switch = false;
	info->chg_full = false;
	info->judge_trigger_off_cnt = 0;

	pr_err("info->adapter_switch_off:%d\n", info->adapter_switch_off);
	if (info->adapter_switch_off)
		adapter_control_ac_off(info, false);
}

static int adapter_control_set_property(struct tran_device *dev,
					   enum tran_common_prop prop,
					   const union com_propval *val)
{
	struct adapter_info *info = tran_get_data(dev);
	int ret = 0;

	pr_info("prop=%d, val=%d\n", prop, val->intval);

	switch (prop) {
		case TRAN_PROP_USB_PRE_PLUG_IN:
			mutex_lock(&info->adapter_lock);
			adapter_control_reset(info);
			info->adapter_plug_in = true;
			mutex_unlock(&info->adapter_lock);
			break;
		case TRAN_PROP_USB_PRE_PLUG_OUT:
			mutex_lock(&info->adapter_lock);
			info->adapter_plug_in = false;
			info->chg_full = false;
			info->judge_trigger_off_cnt = 0;
			alarm_cancel(&info->adapter_control_timer);
			mutex_unlock(&info->adapter_lock);
			break;
		case TRAN_PROP_IS_RFC_TA:
			/* Check whether the charger supports the switch off function */
			mutex_lock(&info->adapter_lock);
			info->adapter_support_switch = 
				 adapter_control_check_ta_switch_support(info);
			if (info->adapter_support_switch && info->chg_full) {
				pr_info("already chg full, trigger adapter colse check\n");
				schedule_work(&info->judge_trigger_off_work);
			}
			mutex_unlock(&info->adapter_lock);
			break;
		case TRAN_PROP_CHG_FULL_STATE:
			mutex_lock(&info->adapter_lock);
			info->chg_full = val->intval;
			if (val->intval)
				schedule_work(&info->judge_trigger_off_work);
			else {
				cancel_work_sync(&info->judge_trigger_off_work);
				info->judge_trigger_off_cnt = 0;
			}
			mutex_unlock(&info->adapter_lock);
			break;
		default:
			ret = -EINVAL;
	}

	return ret;
}

static struct tran_ops adapter_control_ops = {
	.get_prop = adapter_control_get_property,
	.set_prop = adapter_control_set_property,
};

static void adapter_control_judge_trigger_off_work(struct work_struct *work)
{
	struct adapter_info *info = container_of(work,
				struct adapter_info, judge_trigger_off_work);
	union power_supply_propval status = {0, };
	union com_propval tran_val = {0, };
	/* u32 curr = 0; */

	__pm_stay_awake(info->suspend_lock);

	mutex_lock(&info->adapter_lock);
	if (info->adapter_support_switch == false || info->adapter_switch_off == true) {
		pr_info("not support or already close, return, %d %d\n",
			info->adapter_support_switch, info->adapter_switch_off);
		goto out;
	}

	if (IS_ERR_OR_NULL(info->chg_psy)) {
		pr_err("retry to get chg_psy\n");
		info->chg_psy = power_supply_get_by_name("charger");
		if (IS_ERR_OR_NULL(info->chg_psy)) {
			pr_err("failed to get chg_psy\n");
			goto out;
		}
	}

	if (IS_ERR_OR_NULL(info->tc_gauge)) {
		pr_err("retry to get tc_gauge\n");
		info->tc_gauge = tran_get_by_name("tran_batt");
		if (IS_ERR_OR_NULL(info->tc_gauge)) {
			pr_err("failed to get tc_gauge\n");
			goto out;
		}
	}

	power_supply_get_property(info->chg_psy,
		POWER_SUPPLY_PROP_STATUS, &status);
	info->chg_status = status.intval;

	tran_dev_get_prop(info->tc_gauge,
		TRAN_PROP_ACCURACY_UISOC, &tran_val);
	info->raw_soc = tran_val.intval;

	pr_info("chg_status:%s, batt raw_soc:%d,lcd_on_state=%d, adapter_switch_off=%d\n",
		POWER_SUPPLY_STATUS_TEXT[info->chg_status],
		info->raw_soc, info->lcd_on_state, info->adapter_switch_off);

	if ((info->chg_status == POWER_SUPPLY_STATUS_FULL || info->chg_full) &&
		info->adapter_switch_off == false &&
		info->raw_soc == 10000 &&
		info->lcd_on_state == false) {
		info->judge_trigger_off_cnt++;
	} else {
		info->judge_trigger_off_cnt = 0;
	}

	if (info->judge_trigger_off_cnt >= 10) {
		pr_info("trigger control ac off\n");
		info->judge_trigger_off_cnt = 0;
		adapter_control_ac_off(info, true);
	} else {
		pr_info("Deletion of condition, continue to check\n");
		adapter_control_start_alarmtimer(info);
	}

out:
	mutex_unlock(&info->adapter_lock);
	__pm_relax(info->suspend_lock);
	/* tc_ta_device_get_output_current(info->rfc, &curr); */

	/* if (curr < 100) { */
	/*         pr_info("trigger control ac off\n"); */
	/*         adapter_control_ac_off(info, true); */
	/* } else { */
	/*         pr_info("syspower high, recheck\n"); */
	/*         adapter_control_start_alarmtimer(info); */
	/* } */
}

static enum alarmtimer_restart adapter_control_alarm_func(struct alarm *alarm, ktime_t now)
{
	struct adapter_info *info = container_of(alarm,
				struct adapter_info, adapter_control_timer);

	pr_err("adapter_control_alarm_func run succ\n");

	schedule_work(&info->judge_trigger_off_work);

	return ALARMTIMER_NORESTART;
}

static void adapter_control_start_alarmtimer(struct adapter_info *info)
{
	struct timespec64 time, time_now;
	ktime_t temp_time;
	ktime_t ktime;
	int ret;

	/* If the timer was already set, cancel it */
	ret = alarm_try_to_cancel(&info->adapter_control_timer);
	if (ret < 0) {
		pr_err("callback was running, skip timer\n");
		return;
	}

	temp_time = ktime_get_boottime();
	time_now = ktime_to_timespec64(temp_time);

	time.tv_sec = time_now.tv_sec + info->polling_interval; // 30seconds
	time.tv_nsec = 0;

	ktime = ktime_set(time.tv_sec, time.tv_nsec);

	alarm_start(&info->adapter_control_timer, ktime);

	pr_err("adapter control timer start:%lld %ld\n",
		(long long)time.tv_sec, time.tv_nsec);
}

static void adapter_control_init_alarmtimer(struct adapter_info *info)
{
	alarm_init(&info->adapter_control_timer, ALARM_BOOTTIME,
		adapter_control_alarm_func);

	INIT_WORK(&info->judge_trigger_off_work,
		adapter_control_judge_trigger_off_work);

	pr_info("adapter control alarm timer init\n");
}

static void adapter_control_work(struct work_struct *work)
{
	struct adapter_info *info = container_of(work,
				struct adapter_info, adapter_control_work);
	union power_supply_propval status = {0, };
	union com_propval tran_val = {0, };

	power_supply_get_property(info->chg_psy,
		POWER_SUPPLY_PROP_STATUS, &status);
	info->chg_status = status.intval;

	mutex_lock(&info->adapter_lock);
	tran_dev_get_prop(info->tc_gauge,
		TRAN_PROP_ACCURACY_UISOC, &tran_val);
	info->raw_soc = tran_val.intval;

	pr_info("chg_status:%s, batt raw_soc:%d\n",
		POWER_SUPPLY_STATUS_TEXT[info->chg_status],
		info->raw_soc);

	if (g_fake_adapter_off) {
		pr_err("debug adapter off\n");
		g_fake_adapter_off = false;
		adapter_control_ac_off(info, true);
		goto out;
	}

	if (info->adapter_support_switch == false)
		goto out;

	if (info->raw_soc < 10000 &&
		info->adapter_switch_off == true) {

		pr_info("battery level drops to warning level. Recharge\n");
		adapter_control_ac_off(info, false);
	}
out:		
	mutex_unlock(&info->adapter_lock);
}
static int tran_adaper_control_psy_notifier_cb(struct notifier_block *nb,
			unsigned long event, void *data)
{
	struct adapter_info *info = container_of(nb, struct adapter_info, nb);
	struct power_supply *psy = data;

	if (event != PSY_EVENT_PROP_CHANGED) {
		pr_err("no CHANGED\n");
		return NOTIFY_OK;
	}

	if (IS_ERR_OR_NULL(info->chg_psy)) {
		pr_err("retry to get chg_psy\n");
		info->chg_psy = power_supply_get_by_name("charger");
		if (IS_ERR_OR_NULL(info->chg_psy)) {
			pr_err("failed to get chg_psy\n");
			return NOTIFY_OK;
		}
	}

	if (IS_ERR_OR_NULL(info->batt_psy)) {
		pr_err("retry to get batt_psy\n");
		info->batt_psy = power_supply_get_by_name("battery");
		if (IS_ERR_OR_NULL(info->batt_psy)) {
			pr_err("failed to get batt_psy\n");
			return NOTIFY_OK;
		}
	}

	if (IS_ERR_OR_NULL(info->tc_gauge)) {
		pr_err("retry to get tc_gauge\n");
		info->tc_gauge = tran_get_by_name("tc_gauge");
		if (IS_ERR_OR_NULL(info->tc_gauge)) {
			pr_err("failed to get tc_gauge\n");
			return NOTIFY_OK;
		}
	}

	if (psy == info->chg_psy || psy == info->batt_psy) {
		pr_err("schedule adapter control work\n");
		schedule_work(&info->adapter_control_work);
	}

	return NOTIFY_OK;
}

static int adapter_init_chg(struct adapter_info *info)
{

        info->adapter_control_props.alias_name = "adapter_control";
	info->ac_ctl_dev = tran_device_register("adapter_control",
						info->dev, info,
						&adapter_control_ops,
						&info->adapter_control_props);
	if (IS_ERR_OR_NULL(info->ac_ctl_dev))
		return -EPROBE_DEFER;

	return 0;
}

static int adapter_control_parse_dt(struct adapter_info *info,
				struct device *dev)
{
	int ret = 0;
	struct device_node *np = dev->of_node;

	ret = of_property_read_u32(np, "polling_interval", &info->polling_interval);
	if (ret < 0) {
		pr_err("parse polling_interval failed ret = %d, use default 30min\n", ret);
		info->polling_interval = 30; // 30min
	}

	return 0;
}

static int fb_notifier_callback(struct notifier_block *nb,
	unsigned long event, void *v)
{
	struct adapter_info *info = container_of(nb,
		struct adapter_info, fb_notifier);

	pr_info("fb_notifier event:%lu\n", event);
	switch (event) {
	case TRAN_DEV_NOTIFY_SCREEN_ON:
		info->lcd_on_state = true;
		break;
	case TRAN_DEV_NOTIFY_SCREEN_OFF:
		info->lcd_on_state = false;
		break;
	default:
		pr_err("Unknown event:%lu\n", event);
		break;
	}

	return 0;
}

static int phy_det_notifier_callback(struct notifier_block *nb,
                    unsigned long event, void *data)
{
	struct adapter_info *info = container_of(nb,
			struct adapter_info, phy_det_notifier);
	
	if (!info->phy_det_support) {
		pr_info("not support phy_det\n");
		return 0;
	}

	switch (event) {
	case TRAN_DEV_NOTIFY_PHY_PLUG_IN:
		info->phy_plug_in = true;
		pr_info("adapter control phy det plug in\n");
		break;
	case TRAN_DEV_NOTIFY_PHY_PLUG_OUT:
		info->phy_plug_in = false;
		mutex_lock(&info->adapter_lock);
		adapter_control_reset(info);
		mutex_unlock(&info->adapter_lock);
		pr_info("adapter control phy det plug out\n");
		break;
	default:
		break;
	}

	return 0;
}

static int tc_phy_det_notifier_init(struct adapter_info *info)
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

static int adapter_control_probe(struct platform_device *pdev)
{
	int ret;
	struct adapter_info *info;
	int boot_mode = 11; // UNKNOWN_BOOT

	pr_info("enter\n");

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);

	mutex_init(&info->adapter_lock);

	info->suspend_lock =
		wakeup_source_register(NULL, "tran adapter control");

	info->rfc = tc_ta_device_get_by_name("tfcp_ta");
	if (!info->rfc) {
		dev_err(info->dev, "%s get rfc dev fail\n", __func__);
	}

	ret = adapter_control_parse_dt(info, info->dev);
	if(ret < 0) {
		pr_err("adapter control parse dts failed, ret = %d", ret);
		goto err_parse_dt;
	}

	/* 8 = KERNEL_POWER_OFF_CHARGING_BOOT */
	/* 9 = LOW_POWER_OFF_CHARGING_BOOT */
	boot_mode = tc_get_boot_mode();
	if (boot_mode == 8 || boot_mode == 9)
		info->ac_ctl_kpoc = true;

	adapter_control_init_alarmtimer(info);

	/* register power supply notify */
	info->nb.notifier_call = tran_adaper_control_psy_notifier_cb;
	power_supply_reg_notifier(&info->nb);
	INIT_WORK(&info->adapter_control_work, adapter_control_work);

	/* register fb notifier */
	info->tc_lcd = tran_get_by_name("tc_lcd");
	if (IS_ERR_OR_NULL(info->tc_lcd)) {
		pr_err("Failed to register fb notifier client\n");
		ret = -ENODEV;
		goto err_get_tc_lcd;
	}

	info->fb_notifier.notifier_call = fb_notifier_callback;
	ret = register_tran_device_notifier(info->tc_lcd, &info->fb_notifier);
	if (ret < 0) {
		pr_err("register lcd notify failed, ret = %d\n", ret);
		goto err_register_lcd_notify;
	}

	ret = adapter_init_chg(info);
	if (ret < 0) {
		ret = -ENODEV;
		pr_info("adapter control device failed\n");
		goto err_register_dev;
	}


	ret = tc_phy_det_notifier_init(info);
	if (ret != 0) {
		pr_err("%s register phy_det notify fail!\n", __func__);
	}

	pr_info("successfully\n");

	return 0;

err_register_dev:
	tran_device_unregister(info->ac_ctl_dev);
err_register_lcd_notify:
err_get_tc_lcd:
err_parse_dt:
	mutex_destroy(&info->adapter_lock);
	return ret;
}

static const struct of_device_id adapter_control_of_match[] = {
	{.compatible = "transsion, adapter_control",},
	{},
};
MODULE_DEVICE_TABLE(of, adapter_control_of_match);

static int adapter_control_remove(struct platform_device *pdev)
{
	return 0;
}

static void adapter_control_shutdown(struct platform_device *dev)
{
	struct adapter_info *info = platform_get_drvdata(dev);

	info->adapter_off = false;
}

static struct platform_driver adapter_control_platdrv = {
	.probe = adapter_control_probe,
	.remove = adapter_control_remove,
	.shutdown = adapter_control_shutdown,
	.driver = {
		.name = "tc_adapter_control",
		.owner = THIS_MODULE,
		.of_match_table = adapter_control_of_match,
	},
};

static int __init tc_detect_init(void)
{
	return platform_driver_register(&adapter_control_platdrv);
}
late_initcall(tc_detect_init);

static void __exit tc_detect_exit(void)
{
	platform_driver_unregister(&adapter_control_platdrv);
}
module_exit(tc_detect_exit);

MODULE_DESCRIPTION("Transsion TA Control For AI charger");
MODULE_AUTHOR("Transsion Inc.");
MODULE_VERSION("1.0.0_G");
MODULE_LICENSE("GPL");

