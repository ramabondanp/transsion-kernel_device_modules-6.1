// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Transsion Inc.
 */
#define pr_fmt(fmt)  "[TC_CHG_TYPE_DET] %s:" fmt, __func__

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/suspend.h>
#include <linux/alarmtimer.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include "tc_common_class.h"
#include "tc_charger_class.h"
#include "tc_misc_intf.h"
#include "tc_charger.h"
#include "tc_tcpc.h"

struct tc_detect {
	struct device *dev;
	struct platform_device *pdev;
	/* device tree */
	u32 bc12_sel;
	/* tc detect */
	wait_queue_head_t attach_wq;
	atomic_t chrdet_start;
	struct task_struct *attach_task;
	struct mutex attach_lock;
	struct mutex bc12_lock;
	int typec_attach;
	bool tcpc_kpoc;
	/* tc_detect bc12 retry */
	struct work_struct bc12_retry_work;
	struct work_struct bc12_work;
	struct alarm bc12_retry_timer;
	int bc12_retry_interval;
	int bc12_retry_cnt;
	bool bc12_in_progress;
	int pd_type;
	/* charger dev */
	struct charger_device *chg1_dev;
	bool is_full;
	/* tc common dev */
	struct tran_device *usb_ctl_dev;
	struct tran_device *tc_charger_dev; 
	struct tran_device *ambient_dev;
	struct tran_device *pid_chg_dev;
	struct tran_device *adapter_ctrl_dev;
	struct tran_device *wd_dev;
	struct tran_device *charge_transfer_dev;
	struct tran_device *port_burn_dev;
	struct tran_device *temp_forecast_dev;
	struct notifier_block chg_nb;
	/* charger psy */
	struct power_supply_desc charger_psy_desc;
	struct power_supply_config charger_psy_cfg;
	struct power_supply *chg_psy;
	enum power_supply_usb_type charger_usb_types;
	int status_ctrl;
	/* wired psy */
	struct mutex wired_data_lock;
	struct power_supply_desc wired_psy_desc;
	struct power_supply_config wired_psy_cfg;
	struct power_supply *wired_psy;
	enum power_supply_usb_type wired_usb_type;
	bool wired_online;
	/* wireless psy */
	struct mutex wireless_data_lock;
	struct power_supply_desc wireless_psy_desc;
	struct power_supply_config wireless_psy_cfg;
	enum power_supply_usb_type wireless_usb_type;
	struct power_supply *wireless_psy;
	bool wireless_online;
	/* typec notify */
	struct notifier_block pd_nb;
	/* suspend notify */
	struct notifier_block pm_nb;
	struct tc_tcpc_vbus_state vbus_state;
	bool is_audio_plug_in;
	bool is_suspend;
	bool attach_alrdy;

	int pd_aicr_lmit;
	int usb_suspend_pp_flag;
};

#define PHY_MODE_BC11_SET 1
#define PHY_MODE_BC11_CLR 2

static void tc_detect_start_bc12_retry_timer(struct tc_detect *tcd);

enum {
	TC_CTD_BY_SWCHG = 0,
	TC_CTD_BY_PMIC,
};

static const char * const POWER_SUPPLY_TYPE_TEXT[] = {
	[POWER_SUPPLY_TYPE_UNKNOWN]		= "Unknown",
	[POWER_SUPPLY_TYPE_BATTERY]		= "Battery",
	[POWER_SUPPLY_TYPE_UPS]			= "UPS",
	[POWER_SUPPLY_TYPE_MAINS]		= "Mains",
	[POWER_SUPPLY_TYPE_USB]			= "USB",
	[POWER_SUPPLY_TYPE_USB_DCP]		= "USB_DCP",
	[POWER_SUPPLY_TYPE_USB_CDP]		= "USB_CDP",
	[POWER_SUPPLY_TYPE_USB_ACA]		= "USB_ACA",
	[POWER_SUPPLY_TYPE_USB_TYPE_C]		= "USB_C",
	[POWER_SUPPLY_TYPE_USB_PD]		= "USB_PD",
	[POWER_SUPPLY_TYPE_USB_PD_DRP]		= "USB_PD_DRP",
	[POWER_SUPPLY_TYPE_APPLE_BRICK_ID]	= "BrickID",
	[POWER_SUPPLY_TYPE_WIRELESS]		= "Wireless",
};

static const char * const POWER_SUPPLY_USB_TYPE_TEXT[] = {
	[POWER_SUPPLY_USB_TYPE_UNKNOWN]		= "Unknown",
	[POWER_SUPPLY_USB_TYPE_SDP]		= "SDP",
	[POWER_SUPPLY_USB_TYPE_DCP]		= "DCP",
	[POWER_SUPPLY_USB_TYPE_CDP]		= "CDP",
	[POWER_SUPPLY_USB_TYPE_ACA]		= "ACA",
	[POWER_SUPPLY_USB_TYPE_C]		= "C",
	[POWER_SUPPLY_USB_TYPE_PD]		= "PD",
	[POWER_SUPPLY_USB_TYPE_PD_DRP]		= "PD_DRP",
	[POWER_SUPPLY_USB_TYPE_PD_PPS]		= "PD_PPS",
	[POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID]	= "BrickID",
};

static enum power_supply_property tc_detect_charger_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
};

static enum power_supply_property tc_detect_wired_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_STATUS,
};

static enum power_supply_property tc_detect_wireless_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_TYPE,
	/* POWER_SUPPLY_PROP_USB_TYPE, */
	POWER_SUPPLY_PROP_STATUS,
};

static enum power_supply_usb_type tc_detect_charger_usb_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
};

static enum power_supply_usb_type tc_detect_wired_usb_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
};

static int tc_detect_get_charger_status(struct tc_detect *tcd)
{
	pr_info("status_ctrl:%d, online:%d,%d, is_full: %d",
		tcd->status_ctrl, tcd->wired_online,
		tcd->wireless_online, tcd->is_full);

	if (tcd->status_ctrl >= 0
		&& tcd->status_ctrl <= POWER_SUPPLY_STATUS_FULL)
		return tcd->status_ctrl;

	if (!tcd->wired_online && !tcd->wireless_online)
		return POWER_SUPPLY_STATUS_DISCHARGING;
	else if (tcd->is_full)
		return POWER_SUPPLY_STATUS_FULL;
	else
		return POWER_SUPPLY_STATUS_CHARGING;
	
}

static bool tc_detect_get_charger_online(struct tc_detect *tcd)
{
	bool wired_online = false;
	bool wireless_online = false;

	mutex_lock(&tcd->wired_data_lock);
	wired_online = tcd->wired_online;
	mutex_unlock(&tcd->wired_data_lock);

	mutex_lock(&tcd->wireless_data_lock);
	wireless_online = tcd->wireless_online;
	mutex_unlock(&tcd->wireless_data_lock);

	if (wired_online || wireless_online)
		return true;

	return false;
}

static enum power_supply_type tc_detect_get_psy_type(struct tc_detect *tcd)
{
	enum power_supply_type wired_psy_type = POWER_SUPPLY_TYPE_UNKNOWN;
	enum power_supply_type wireless_psy_type = POWER_SUPPLY_TYPE_UNKNOWN;

	mutex_lock(&tcd->wired_data_lock);
	wired_psy_type = tcd->wired_psy_desc.type;
	mutex_unlock(&tcd->wired_data_lock);

	mutex_lock(&tcd->wireless_data_lock);
	wireless_psy_type = tcd->wireless_psy_desc.type;
	mutex_unlock(&tcd->wireless_data_lock);

	if (wired_psy_type != POWER_SUPPLY_TYPE_UNKNOWN)
		return wired_psy_type;
	else if (wireless_psy_type != POWER_SUPPLY_TYPE_UNKNOWN)
		return wireless_psy_type;

	return POWER_SUPPLY_TYPE_USB;
}

static void tc_detect_update_charger_psy_type(struct tc_detect *tcd)
{
	enum power_supply_type wired_psy_type = POWER_SUPPLY_TYPE_UNKNOWN;
	enum power_supply_type wireless_psy_type = POWER_SUPPLY_TYPE_UNKNOWN;

	mutex_lock(&tcd->wired_data_lock);
	wired_psy_type = tcd->wired_psy_desc.type;
	mutex_unlock(&tcd->wired_data_lock);

	mutex_lock(&tcd->wireless_data_lock);
	wireless_psy_type = tcd->wireless_psy_desc.type;
	mutex_unlock(&tcd->wireless_data_lock);

	if (wired_psy_type != POWER_SUPPLY_TYPE_UNKNOWN)
		tcd->charger_psy_desc.type = wired_psy_type;
	else if (wireless_psy_type != POWER_SUPPLY_TYPE_UNKNOWN)
		tcd->charger_psy_desc.type = wireless_psy_type;
	else
		tcd->charger_psy_desc.type = POWER_SUPPLY_TYPE_USB;
}

static int tc_detect_charger_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	struct tc_detect *tcd = NULL;

	tcd = (struct tc_detect *)power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = tc_detect_get_charger_online(tcd);
		break;
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = tc_detect_get_psy_type(tcd);
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		mutex_lock(&tcd->wired_data_lock);
		if (tcd->wireless_online)
			val->intval = tcd->wireless_usb_type;
		else
			val->intval = tcd->wired_usb_type;
		mutex_unlock(&tcd->wired_data_lock);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = tc_detect_get_charger_status(tcd);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		charger_dev_get_constant_voltage(tcd->chg1_dev, &val->intval);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int tc_detect_charger_set_property(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val)
{
	struct tc_detect *tcd = NULL;
	union com_propval pp_val = {0};
	union com_propval aicr_val = {-1};

	tcd = (struct tc_detect *)power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		tcd->status_ctrl = val->intval;
		power_supply_changed(tcd->chg_psy);
		break;
		/* Add for PD test */
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		if (val->intval & USB_CURRENT_MASK) {
			if (val->intval & UNLIMIT_CURRENT_MASK)
				aicr_val.intval = -1;
			else
				aicr_val.intval = (val->intval & ~(USB_CURRENT_MASK)) * 1000;

			if (aicr_val.intval < 100000 && aicr_val.intval >= 0) {
				pp_val.intval = false;
				tcd->usb_suspend_pp_flag = false;
			} else if (aicr_val.intval >= 100000 || aicr_val.intval == -1) {
				pp_val.intval = tcd->pd_aicr_lmit >= 100000;
				tcd->usb_suspend_pp_flag = true;
			}
		} else {
			pp_val.intval = tcd->pd_aicr_lmit >= 100000;
		}
		pr_info("%s : aicr:%d  power_path:%d suspend_flag=%d\n", __func__, aicr_val.intval, pp_val.intval, tcd->usb_suspend_pp_flag);
		tran_dev_set_prop(tcd->tc_charger_dev, TRAN_PROP_USB_PD_VOTE_AICR, &aicr_val);
		tran_dev_set_prop(tcd->tc_charger_dev, TRAN_PROP_USB_PD_SET_POWER_PATH, &pp_val);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int tc_detect_wired_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	struct tc_detect *tcd = NULL;

	tcd = (struct tc_detect *)power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		mutex_lock(&tcd->wired_data_lock);
		val->intval = tcd->wired_online;
		mutex_unlock(&tcd->wired_data_lock);
		break;
	case POWER_SUPPLY_PROP_TYPE:
		mutex_lock(&tcd->wired_data_lock);
		val->intval = tcd->wired_psy_desc.type;
		mutex_unlock(&tcd->wired_data_lock);
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		mutex_lock(&tcd->wired_data_lock);
		val->intval = tcd->wired_usb_type;
		mutex_unlock(&tcd->wired_data_lock);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		/* val->intval = tc_detect_get_charger_status(tcd); */
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int tc_detect_wired_set_property(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val)
{
	struct tc_detect *tcd = NULL;

	tcd = (struct tc_detect *)power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		mutex_lock(&tcd->wired_data_lock);
		tcd->wired_online = val->intval;
		mutex_unlock(&tcd->wired_data_lock);
		break;
	case POWER_SUPPLY_PROP_TYPE:
		mutex_lock(&tcd->wired_data_lock);
		tcd->wired_psy_desc.type = val->intval;
		mutex_unlock(&tcd->wired_data_lock);
		tc_detect_update_charger_psy_type(tcd);
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		mutex_lock(&tcd->wired_data_lock);
		tcd->wired_usb_type = val->intval;
		mutex_unlock(&tcd->wired_data_lock);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int tc_detect_wireless_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	struct tc_detect *tcd = NULL;

	tcd = (struct tc_detect *)power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = tcd->wireless_online;
		/* val->intval = tc_detect_get_charger_online(tcd); */
		break;
	case POWER_SUPPLY_PROP_TYPE:
		/* val->intval = tc_detect_get_charger_type(tcd); */
		break;
	case POWER_SUPPLY_PROP_STATUS:
		/* val->intval = tc_detect_get_charger_status(tcd); */
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int tc_detect_wireless_set_property(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val)
{
	struct tc_detect *tcd = NULL;
	union com_propval com_val = {0, };

	tcd = (struct tc_detect *)power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		mutex_lock(&tcd->wireless_data_lock);
		tcd->wireless_online = val->intval;
		mutex_unlock(&tcd->wireless_data_lock);
		break;
	case POWER_SUPPLY_PROP_TYPE:
		mutex_lock(&tcd->wireless_data_lock);
		tcd->wireless_psy_desc.type = val->intval;
		mutex_unlock(&tcd->wireless_data_lock);
		tc_detect_update_charger_psy_type(tcd);
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		mutex_lock(&tcd->wireless_data_lock);
		tcd->wireless_usb_type = val->intval;
		mutex_unlock(&tcd->wireless_data_lock);

		/* wireless access, notify healthd */
		power_supply_changed(tcd->chg_psy);

		/* wireless access, trigger charger thread */
		com_val.intval = (tcd->wireless_usb_type == POWER_SUPPLY_USB_TYPE_DCP) ? true : false;
		tran_dev_set_prop(tcd->tc_charger_dev, TRAN_PROP_WAKE_UP_CHARGER, &com_val);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static char *tcd_psy_supplied_to[] = {
	"battery",
	"tc_gauge",
};

static int tc_detect_psy_register(struct tc_detect *tcd)
{
	struct platform_device *pdev = tcd->pdev;

	tcd->charger_psy_desc.name = "charger";
	tcd->charger_psy_desc.type = POWER_SUPPLY_TYPE_USB;
	tcd->charger_psy_desc.properties = tc_detect_charger_properties;
	tcd->charger_psy_desc.num_properties = ARRAY_SIZE(tc_detect_charger_properties);
	tcd->charger_psy_desc.get_property = tc_detect_charger_get_property;
	tcd->charger_psy_desc.set_property = tc_detect_charger_set_property;
	tcd->charger_psy_desc.usb_types = tc_detect_charger_usb_types;
	tcd->charger_psy_desc.num_usb_types = ARRAY_SIZE(tc_detect_charger_usb_types);
	tcd->charger_psy_cfg.drv_data = tcd;
	tcd->charger_psy_cfg.supplied_to = tcd_psy_supplied_to;
	tcd->charger_psy_cfg.num_supplicants = ARRAY_SIZE(tcd_psy_supplied_to);
	tcd->chg_psy = devm_power_supply_register(&pdev->dev, &tcd->charger_psy_desc,
			&tcd->charger_psy_cfg);

	if (IS_ERR_OR_NULL(tcd->chg_psy)) {
		pr_err("charger psy register fail\n");
		return -EINVAL;
	}

	tcd->wired_psy_desc.name = "wired";
	tcd->wired_psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
	tcd->wired_psy_desc.properties = tc_detect_wired_properties;
	tcd->wired_psy_desc.num_properties = ARRAY_SIZE(tc_detect_wired_properties);
	tcd->wired_psy_desc.get_property = tc_detect_wired_get_property;
	tcd->wired_psy_desc.set_property = tc_detect_wired_set_property;
	tcd->wired_psy_desc.usb_types = tc_detect_wired_usb_types;
	tcd->wired_psy_desc.num_usb_types = ARRAY_SIZE(tc_detect_wired_usb_types);
	tcd->wired_psy_cfg.drv_data = tcd;
	tcd->wired_psy = devm_power_supply_register(&pdev->dev, &tcd->wired_psy_desc,
			&tcd->wired_psy_cfg);

	if (IS_ERR_OR_NULL(tcd->wired_psy)) {
		pr_err("wired psy register fail\n");
		return -EINVAL;
	}

	tcd->wireless_psy_desc.name = "wireless";
	tcd->wireless_psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
	tcd->wireless_psy_desc.properties = tc_detect_wireless_properties;
	tcd->wireless_psy_desc.num_properties = ARRAY_SIZE(tc_detect_wireless_properties);
	tcd->wireless_psy_desc.get_property = tc_detect_wireless_get_property;
	tcd->wireless_psy_desc.set_property = tc_detect_wireless_set_property;
	tcd->wireless_psy_cfg.drv_data = tcd;
	tcd->wireless_psy = devm_power_supply_register(&pdev->dev, &tcd->wireless_psy_desc,
			&tcd->wireless_psy_cfg);

	if (IS_ERR_OR_NULL(tcd->wireless_psy)) {
		pr_err("wireless psy register fail\n");
		return -EINVAL;
	}

	return 0;
}

static int tcd_get_primary_module(struct tc_detect *tcd)
{
	/* primary module */
	tcd->chg1_dev = get_charger_by_name("primary_chg");
	if (IS_ERR_OR_NULL(tcd->chg1_dev)) {
		pr_err("can't find primary_chg\n");
		return -ENODEV;
	}

	tcd->tc_charger_dev = tran_get_by_name("tc_charger");
	if (IS_ERR_OR_NULL(tcd->tc_charger_dev)) {
		pr_err("can't find tc_charger common dev\n");
		return -ENODEV;
	}
	return 0;
}	

static void tcd_get_secondary_module(struct tc_detect *tcd)
{

	/* secondary module */
	if (IS_ERR_OR_NULL(tcd->adapter_ctrl_dev)) {
		tcd->adapter_ctrl_dev = tran_get_by_name("adapter_control");
		if(IS_ERR_OR_NULL(tcd->adapter_ctrl_dev))
			pr_err("can't find adapter control dev\n");
	}

	if (IS_ERR_OR_NULL(tcd->wd_dev)) {
		tcd->wd_dev = tran_get_by_name("water_detect");
		if (IS_ERR_OR_NULL(tcd->wd_dev))
			pr_err("can't find water_detect common dev\n");
	}

	if (IS_ERR_OR_NULL(tcd->usb_ctl_dev)) {
		tcd->usb_ctl_dev = tran_get_by_name("usb_control");
		if (IS_ERR_OR_NULL(tcd->usb_ctl_dev))
			pr_err("can't find usb_control common dev\n");
	}

	if (IS_ERR_OR_NULL(tcd->pid_chg_dev)) {
		tcd->pid_chg_dev = tran_get_by_name("pid_chg_algo");
		if (IS_ERR_OR_NULL(tcd->pid_chg_dev))
			pr_err("can't find pid_chg_dev common dev\n");
	}

	if (IS_ERR_OR_NULL(tcd->ambient_dev)) {
		tcd->ambient_dev = tran_get_by_name("ambient_detect");
		if (IS_ERR_OR_NULL(tcd->ambient_dev))
			pr_err("can't find ambient common dev\n");
	}

	if (IS_ERR_OR_NULL(tcd->charge_transfer_dev)) {
		tcd->charge_transfer_dev = tran_get_by_name("charge_transfer");
		if (IS_ERR_OR_NULL(tcd->charge_transfer_dev))
			pr_err("can't find charge transfer dev\n");
	}

	if (IS_ERR_OR_NULL(tcd->port_burn_dev)) {
		tcd->port_burn_dev = tran_get_by_name("port_burn");
		if (IS_ERR_OR_NULL(tcd->port_burn_dev))
			pr_err("can't find port burn dev\n");
	}

	if (IS_ERR_OR_NULL(tcd->temp_forecast_dev)) {
		tcd->temp_forecast_dev = tran_get_by_name("temp_forecast");
		if (IS_ERR_OR_NULL(tcd->temp_forecast_dev))
			pr_err("can't find temp_forecast dev\n");
	}
	return;
}

static int tcd_get_related_module(struct tc_detect *tcd)
{
	int ret = 0;

	/* primary module */
	ret = tcd_get_primary_module(tcd);
	/* secondary module */
	tcd_get_secondary_module(tcd);
	return ret;
}

static bool is_usb_rdy(struct tc_detect *tcd)
{
	bool ready = true;
	struct device_node *node = NULL;

	node = of_parse_phandle(tcd->dev->of_node, "usb", 0);
	if (node) {
		ready = !of_property_read_bool(node, "cdp-block");
		pr_info("usb ready = %d\n", ready);
	} else
		pr_err("usb node missing or invalid\n");

	return ready;
}

static int tcd_set_usbsw(struct tc_detect *tcd,
				enum chg_usbsw usbsw)
{
	struct phy *phy;
	int ret, mode = (usbsw == TC_USBSW_CHG) ? PHY_MODE_BC11_SET :
					       PHY_MODE_BC11_CLR;

	pr_info("usbsw = %d\n", usbsw);

	phy = phy_get(tcd->dev, "usb2-phy");
	if (IS_ERR_OR_NULL(phy)) {
		pr_err("failed to get usb2-phy\n");
		return -ENODEV;
	}

	ret = phy_set_mode_ext(phy, PHY_MODE_USB_DEVICE, mode);
	if (ret)
		pr_err("failed to set phy ext mode\n");

	phy_put(tcd->dev, phy);

	return ret;
}

static void do_bc12_pre_work(struct tc_detect *tcd, int attach)
{
	int i;
	static const int max_wait_cnt = 250;

	if (attach) {
		/* CDP port specific process */
		pr_info("check CDP block\n");
		for (i = 0; i < max_wait_cnt; i++) {
			if (is_usb_rdy(tcd))
				break;
			attach = tcd->typec_attach;
			if (attach != TC_ATTACH_TYPE_NONE) {
				msleep(100);
			} else {
				pr_info("change attach:%d, disable bc12\n", attach);
				break;
			}
		}
		if (i == max_wait_cnt)
			pr_info("CDP timeout\n");
		else
			pr_info("CDP free\n");
	}

	tcd_set_usbsw(tcd, attach ? TC_USBSW_CHG : TC_USBSW_USB);
}

static void do_bc12_post_work(struct tc_detect *tcd,
	enum power_supply_type psy_type,
	enum power_supply_usb_type usb_type)
{
	if ((psy_type == POWER_SUPPLY_TYPE_USB_DCP && usb_type == POWER_SUPPLY_USB_TYPE_DCP) ||
		(psy_type == POWER_SUPPLY_TYPE_USB && usb_type == POWER_SUPPLY_USB_TYPE_DCP))
		tcd_set_usbsw(tcd, TC_USBSW_CHG);
	else
		tcd_set_usbsw(tcd, TC_USBSW_USB);
}

static void tc_detect_bc12_retry_work(struct work_struct *data)
{
	struct tc_detect *tcd = (struct tc_detect *)container_of(data,
		struct tc_detect, bc12_retry_work);
	int i;
	int port_stat = TC_PORT_STAT_NOINFO;
	union com_propval com_val = {1, };
	union power_supply_propval val = {0, };
	enum power_supply_type psy_type = POWER_SUPPLY_TYPE_UNKNOWN;
	enum power_supply_usb_type usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;

	do_bc12_pre_work(tcd, TC_ATTACH_TYPE_TYPEC);

	charger_dev_enable_chg_type_det(tcd->chg1_dev, true);

	for (i = 0; i < 10; i++) {
		if (tcd->typec_attach == TC_ATTACH_TYPE_NONE) {
			pr_info("Adapter plug out, stop retry\n");
			goto stop_retry;
		}

		msleep(50);
		charger_dev_get_port_stat(tcd->chg1_dev, &port_stat);
		if (port_stat != TC_PORT_STAT_NOINFO)
			break;
	}

	switch (port_stat) {
	case TC_PORT_STAT_NOINFO:
	case TC_PORT_STAT_UNKNOWN_TA:
		pr_info("unknow adapter:%d, trigger retry check\n", port_stat);
		goto continue_to_try;
	case TC_PORT_STAT_DCP:
		psy_type = POWER_SUPPLY_TYPE_USB_DCP;
		usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		break;
	case TC_PORT_STAT_SDP:
		psy_type = POWER_SUPPLY_TYPE_USB;
		usb_type = POWER_SUPPLY_USB_TYPE_SDP;
		break;
	case TC_PORT_STAT_CDP:
		psy_type = POWER_SUPPLY_TYPE_USB_CDP;
		usb_type = POWER_SUPPLY_USB_TYPE_CDP;
		break;
	default:
		pr_err("unknow prot stat:%d, check switch charger driver\n", port_stat);
		return;
	}

	val.intval = psy_type;
	power_supply_set_property(tcd->wired_psy, POWER_SUPPLY_PROP_TYPE, &val);
	val.intval = usb_type;
	power_supply_set_property(tcd->wired_psy, POWER_SUPPLY_PROP_USB_TYPE, &val);

	do_bc12_post_work(tcd, psy_type, usb_type);

	power_supply_changed(tcd->chg_psy);

	/* trigger charger thread */
	tran_dev_set_prop(tcd->tc_charger_dev, TRAN_PROP_WAKE_UP_CHARGER, &com_val);

	pr_info("port_stat:%s\n", port_stat_name[port_stat]);

	return;

continue_to_try:
	if (tcd->bc12_retry_cnt++ < 30) {
		tc_detect_start_bc12_retry_timer(tcd);
		return;
	}
stop_retry:
	alarm_cancel(&tcd->bc12_retry_timer);
	tcd->bc12_retry_cnt = 0;
	pr_info("stop bc12 retry, reset param\n");
}

static void tc_detect_bc12_run(struct tc_detect *tcd,
				enum power_supply_type *psy_type,
				enum power_supply_usb_type *usb_type)
{
	int i;
	int ret = 0;
	bool vbus_gd = false;
	int port_stat = TC_PORT_STAT_NOINFO;
	
	/* step 1: check switch ic vbus good ready */
	for (i = 0; i < 15; i++) {
		if (tcd->typec_attach == TC_ATTACH_TYPE_NONE) {
			pr_info("Adapter plug out, stop bc12 running\n");
			goto stop_bc12;
		}

		msleep(20);
		ret = charger_dev_get_vbus_gd(tcd->chg1_dev, &vbus_gd);
		if (ret < 0) {
			pr_info("do without check vbus_gd or err:%d\n", ret);
			break;
		}

		if (vbus_gd) {
			pr_info("switch ic vbus_gd is ready\n");
			msleep(100);
			break;
		}
	}
	
	/* step 2: trigger bc12 flow */
	charger_dev_enable_chg_type_det(tcd->chg1_dev, true);

	/* step 3: get bc12 result */
	for (i = 0; i < 30; i++) {
		if (tcd->typec_attach == TC_ATTACH_TYPE_NONE) {
			pr_info("Adapter plug out, stop bc12 running\n");
			goto stop_bc12;
		}

		msleep(50);
		charger_dev_get_port_stat(tcd->chg1_dev, &port_stat);
		if (port_stat != TC_PORT_STAT_NOINFO)
			break;
	}

	if (tcd->wireless_online) {
		pr_info("wireless online, ignore BC1.2\n");
		return;
	}

	switch (port_stat) {
	case TC_PORT_STAT_DCP:
		*psy_type = POWER_SUPPLY_TYPE_USB_DCP;
		*usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		break;
	case TC_PORT_STAT_SDP:
		*psy_type = POWER_SUPPLY_TYPE_USB;
		*usb_type = POWER_SUPPLY_USB_TYPE_SDP;
		break;
	case TC_PORT_STAT_CDP:
		*psy_type = POWER_SUPPLY_TYPE_USB_CDP;
		*usb_type = POWER_SUPPLY_USB_TYPE_CDP;
		break;
	case TC_PORT_STAT_NOINFO:
	case TC_PORT_STAT_UNKNOWN_TA:
		*psy_type = POWER_SUPPLY_TYPE_USB;
		*usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		pr_info("unknow adapter, trigger retry check\n");
		tc_detect_start_bc12_retry_timer(tcd);
		break;
	default:
		pr_err("unknow prot stat:%d, check switch charger driver\n", port_stat);
		return;
	}

	pr_info("port_stat:%s\n", port_stat_name[port_stat]);

	return;

stop_bc12:
	*psy_type = POWER_SUPPLY_TYPE_UNKNOWN;
	*usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
	return;
}

static void tc_detect_do_bc12(struct tc_detect *tcd, int attach)
{
	union power_supply_propval val = {0, };
	enum power_supply_type psy_type = POWER_SUPPLY_TYPE_UNKNOWN;
	enum power_supply_usb_type usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;

	if (tcd->wireless_online) {
		pr_info("wireless online, ignore BC1.2\n");
		return;
	}

	switch (attach) {
	case TC_ATTACH_TYPE_NONE:
		psy_type = POWER_SUPPLY_TYPE_UNKNOWN;
		usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		break;
	case TC_ATTACH_TYPE_TYPEC:
		do_bc12_pre_work(tcd, attach);
		if (!tcd->wireless_online) 
			tc_detect_bc12_run(tcd, &psy_type, &usb_type);		
		do_bc12_post_work(tcd, psy_type, usb_type);
		break;
	case TC_ATTACH_TYPE_PD_SDP:
		psy_type = POWER_SUPPLY_TYPE_USB;
		usb_type = POWER_SUPPLY_USB_TYPE_SDP;
		break;
	case TC_ATTACH_TYPE_PD_DCP:
		psy_type = POWER_SUPPLY_TYPE_USB_DCP;
		usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		break;
	case TC_ATTACH_TYPE_PD_NONSTD:
		psy_type = POWER_SUPPLY_TYPE_USB;
		usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		break;
	default:
		pr_err("unknow attach:%d, check sw version\n", attach);
		return;
	}

	pr_info("attach:%s, psy_type:%s, usb_type:%s,wireless_online:%d\n",
		attach_type_name[attach],
		POWER_SUPPLY_TYPE_TEXT[psy_type],
		POWER_SUPPLY_USB_TYPE_TEXT[usb_type],tcd->wireless_online);

	if (tcd->wireless_online) {
		pr_info("wireless online, ignore BC1.2\n");
		return;
	}

	val.intval = attach;
	power_supply_set_property(tcd->wired_psy, POWER_SUPPLY_PROP_ONLINE, &val);
	val.intval = psy_type;
	power_supply_set_property(tcd->wired_psy, POWER_SUPPLY_PROP_TYPE, &val);
	val.intval = usb_type;
	power_supply_set_property(tcd->wired_psy, POWER_SUPPLY_PROP_USB_TYPE, &val);

	/* power_supply_changed(tcd->wired_psy); */
	power_supply_changed(tcd->chg_psy);
}

static void tc_detect_do_bc12_preprocess(struct tc_detect *tcd, int attach)
{
	union com_propval com_val = {0, };

	/* Redundant processing start */
	alarm_cancel(&tcd->bc12_retry_timer);
	tcd->bc12_retry_cnt = 0;
	tcd->bc12_in_progress = true;
	pr_info("attach changed, stop bc12 retry, reset param\n");
	/* Redundant processing end */

	if (IS_ERR_OR_NULL(tcd->adapter_ctrl_dev))
		tcd->adapter_ctrl_dev = tran_get_by_name("adapter_control");

	/* pre plug in/out, inform related module */

	if (attach == 0) {
		com_val.intval = !!attach;
		tran_dev_set_prop(tcd->adapter_ctrl_dev, TRAN_PROP_USB_PRE_PLUG_OUT, &com_val);
	} else {
		com_val.intval = !!attach;
		tran_dev_set_prop(tcd->adapter_ctrl_dev, TRAN_PROP_USB_PRE_PLUG_IN, &com_val);
	}
}

static void tc_detect_do_bc12_postprocess(struct tc_detect *tcd, int attach)
{
	union com_propval com_val = {0, };
	
	/*check mode*/
	tcd_get_secondary_module(tcd);

	/* post plug in/out, inform related module */
	if (attach == 0) {
		com_val.intval = !!attach;
		tran_dev_set_prop(tcd->wd_dev, TRAN_PROP_USB_PLUG_OUT, &com_val);
		tran_dev_set_prop(tcd->ambient_dev, TRAN_PROP_USB_PLUG_OUT, &com_val);
		tran_dev_set_prop(tcd->usb_ctl_dev, TRAN_PROP_USB_PLUG_OUT, &com_val);
		tran_dev_set_prop(tcd->pid_chg_dev, TRAN_PROP_USB_PLUG_OUT, &com_val);
		tran_dev_set_prop(tcd->charge_transfer_dev, TRAN_PROP_USB_PLUG_OUT, &com_val);
		tran_dev_set_prop(tcd->port_burn_dev, TRAN_PROP_USB_PLUG_OUT, &com_val);
		tran_dev_set_prop(tcd->temp_forecast_dev, TRAN_PROP_USB_PLUG_OUT, &com_val);
	} else {
		com_val.intval = !!attach;

		/*check water */
		if (!tcd->tcpc_kpoc && attach == TC_ATTACH_TYPE_TYPEC) 
			tran_dev_set_prop(tcd->wd_dev, TRAN_PROP_USB_PLUG_IN, &com_val);

		tran_dev_set_prop(tcd->ambient_dev, TRAN_PROP_USB_PLUG_IN, &com_val);
		tran_dev_set_prop(tcd->usb_ctl_dev, TRAN_PROP_USB_PLUG_IN, &com_val);
		tran_dev_set_prop(tcd->pid_chg_dev, TRAN_PROP_USB_PLUG_IN, &com_val);
		tran_dev_set_prop(tcd->charge_transfer_dev, TRAN_PROP_USB_PLUG_IN, &com_val);
		tran_dev_set_prop(tcd->port_burn_dev, TRAN_PROP_USB_PLUG_IN, &com_val);
		tran_dev_set_prop(tcd->temp_forecast_dev, TRAN_PROP_USB_PLUG_IN, &com_val);
	}

	/* trigger charger thread */
	tran_dev_set_prop(tcd->tc_charger_dev, TRAN_PROP_WAKE_UP_CHARGER, &com_val);
	tcd->bc12_in_progress = false;
}

static void tc_detect_bc12_work(struct work_struct *data)
{
	struct tc_detect *tcd = (struct tc_detect *)container_of(data,
		struct tc_detect, bc12_work);
	int attach = tcd->typec_attach;

	pr_info("attach:%s\n", attach_type_name[attach]);

	mutex_lock(&tcd->bc12_lock);
	tc_detect_do_bc12_preprocess(tcd, attach);
	tc_detect_do_bc12(tcd, attach);
	tc_detect_do_bc12_postprocess(tcd, attach);
	mutex_unlock(&tcd->bc12_lock);
}

static int typec_attach_thread(void *data)
{
	struct tc_detect *tcd = data;
	int ret = 0, attach;

	pr_info("++\n");

	while (!kthread_should_stop()) {
		if (tcd == NULL) {
			pr_info("tcd is null\n");
			return -ENODEV;
		}
		ret = wait_event_interruptible(tcd->attach_wq,
			   atomic_read(&tcd->chrdet_start) > 0 ||
							 kthread_should_stop());
		if (ret == -ERESTARTSYS) {
			pr_info("error when wait_event_interruptible\n");
			break;
		}
		if (ret < 0) {
			pr_info("wait event been interrupted(%d)\n", ret);
			continue;
		}
		if (kthread_should_stop())
			break;

		mutex_lock(&tcd->attach_lock);
		
		attach = tcd->typec_attach;

		pr_info("attach:%s\n", attach_type_name[attach]);
		schedule_work(&tcd->bc12_work);

		atomic_set(&tcd->chrdet_start, 0);
		mutex_unlock(&tcd->attach_lock);
	}

	return ret;
}

static void handle_typec_pd_attach(struct tc_detect *tcd, int attach)
{
	mutex_lock(&tcd->attach_lock);
	tcd->typec_attach = attach;
	tcd->attach_alrdy = attach ? true : false;
	atomic_inc(&tcd->chrdet_start);
	wake_up_interruptible(&tcd->attach_wq);
	mutex_unlock(&tcd->attach_lock);
}

static void handle_audio_attach(struct tc_detect *tcd, struct tc_tcpc_noti *noti)
{
	int attach;

	/* If state.mv > 0mV, maybe already connect charge */
	attach = !!noti->vbus_state.mv ? ATTACH_TYPE_PD_NONSTD :
			 ATTACH_TYPE_NONE;
	pr_notice("%s: audio_attach:%d\n", __func__, attach);
	handle_typec_pd_attach(tcd, attach);
}

static void handle_attach_rdy_attach(struct tc_detect *tcd, struct tc_tcpc_noti *noti)
{
	int attach_alrdy = 0;

	if (noti->pd_type == TC_PD_CONNECT_PE_READY_SNK ||
	    noti->pd_type == TC_PD_CONNECT_PE_READY_SNK_PD30 ||
	    noti->pd_type == TC_PD_CONNECT_PE_READY_SNK_APDO) {
		mutex_lock(&tcd->attach_lock);
		attach_alrdy = tcd->attach_alrdy;
		if (attach_alrdy) {
			pr_info("attach_alrdy is already done\n");
			mutex_unlock(&tcd->attach_lock);
			return;
		}
		tcd->attach_alrdy = true;
		mutex_unlock(&tcd->attach_lock);
			
		handle_typec_pd_attach(tcd, TC_ATTACH_TYPE_PD_SDP);
	}
}

static void kernel_power_off_status_check(struct tc_detect *tcd)
{
	int counter = 0;

	if (tcd->tcpc_kpoc) {
		pr_info("typec unattached, power off\n");
		while (1) {
			if (counter >= 7000) {
				if (pm_suspend_target_state != PM_SUSPEND_ON) {
					pr_info("skip shutdown during pm suspend state %d", pm_suspend_target_state);
				} else {
					ksys_sync_helper();
					kernel_power_off();
				}

				break;
			}
			if (tcd->is_suspend == false) {
				pr_info("not in suspend, shutdown\n");
				//kernel_power_off();
			} else {
				pr_info("suspend, cannot shutdown\n");
				//msleep(20);
			}
			counter++;
			msleep(50);
		}
	}
	
}

static int tc_pd_tcp_notifier_call(struct notifier_block *nb,
				unsigned long event, void *data)
{
	struct tc_tcpc_noti *noti = data;
	struct tc_detect *tcd = (struct tc_detect *)container_of(nb,
		struct tc_detect, pd_nb);
	union com_propval pp_val = {0};
	switch (event) {
	case TC_TYPEC_SNK_VBUS:
		memset(&tcd->vbus_state, 0, sizeof(struct tc_tcpc_vbus_state));
		memcpy(&tcd->vbus_state, &noti->vbus_state, sizeof(struct tc_tcpc_vbus_state));
		pr_info("%s: sink vbus %dmV %dmA type(0x%02x)\n", __func__,
			tcd->vbus_state.mv, tcd->vbus_state.ma, noti->vbus_state.type);
		tcd->pd_aicr_lmit = tcd->vbus_state.ma * 1000;

		if (tcd->vbus_state.ma < 100) {
			pp_val.intval = false;
		} else {
			pp_val.intval = tcd->usb_suspend_pp_flag;
		}
		/* set power_path */
		tran_dev_set_prop(tcd->tc_charger_dev, TRAN_PROP_USB_PD_SET_POWER_PATH, &pp_val);
		/* If connect Audio at first,and then plug in charge */

		if (tcd->is_audio_plug_in)
			handle_audio_attach(tcd, noti);
		break;
	case TC_TYPEC_AUDIO_PLUG_IN:
		tcd->is_audio_plug_in = true;
		handle_audio_attach(tcd, noti);
		break;
	case TC_TYPEC_AUDIO_PLUG_OUT:
		tcd->is_audio_plug_in = false;
		break;
	case TC_PD_TYPE:
		tcd->pd_type = noti->pd_type;
		handle_attach_rdy_attach(tcd, noti);
		break;
	case TC_TYPEC_USB_PLUG_IN:
		pr_info("USB Plug in\n");
		handle_typec_pd_attach(tcd, TC_ATTACH_TYPE_TYPEC);
		break;
	case TC_TYPEC_USB_PLUG_OUT:
		pr_info("USB Plug out\n");
		tcd->pd_aicr_lmit = -1;
		tcd->usb_suspend_pp_flag = true;
		tcd->is_audio_plug_in = false;
		handle_typec_pd_attach(tcd, TC_ATTACH_TYPE_NONE);
		kernel_power_off_status_check(tcd);
		break;
	case TC_PD_SNK_TO_SRC:
		pr_info("Sink_to_Source\n");
		handle_typec_pd_attach(tcd, TC_ATTACH_TYPE_NONE);
		kernel_power_off_status_check(tcd);
		break;
	default:
		break;
	};
	return NOTIFY_OK;
}

static int tc_detect_pm_event(struct notifier_block *notifier,
			unsigned long pm_event, void *unused)
{
	struct tc_detect *tcd;

	tcd = (struct tc_detect *)container_of(notifier,
		struct tc_detect, pm_nb);

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		tcd->is_suspend = true;
		pr_info("enter PM_SUSPEND_PREPARE\n");
		break;
	case PM_POST_SUSPEND:
		tcd->is_suspend = false;
		pr_info("enter PM_POST_SUSPEND\n");
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static int tc_chg_notifier_call(struct notifier_block *notifier,
			unsigned long event, void *data)
{
	struct tc_detect *tcd;

	tcd = (struct tc_detect *)container_of(notifier,
		struct tc_detect, chg_nb);

	pr_info("tc chg_type event: %lu\n", event);
	switch (event) {
	case EVENT_FULL:
		tcd->is_full = true;
		break;
	case EVENT_RECHARGE:
	case EVENT_PLUG_OUT:
		tcd->is_full = false;
		break;
	default:
		break;
	}
	power_supply_changed(tcd->chg_psy);

	return NOTIFY_DONE;
}

static void tc_parse_dt(struct tc_detect *tcd)
{
	struct device_node *np = tcd->dev->of_node;
	int ret = 0;

	ret = of_property_read_u32(np, "bc12_sel", &tcd->bc12_sel);
	if (ret < 0) {
		pr_err("bc12_sel dtsi not defined, set default bc12_sel\n");
		tcd->bc12_sel = TC_CTD_BY_SWCHG;
	}

	pr_info("dtsi defined bc12_sel:%d\n", tcd->bc12_sel);

	ret = of_property_read_u32(np, "bc12_retry_interval", &tcd->bc12_retry_interval);
	if (ret < 0) {
		pr_err("bc12_retry_interval dtsi not defined, set default bc12_retry_interval\n");
		tcd->bc12_retry_interval = 1;
	}

	pr_info("dtsi defined bc12_retry_interval:%d\n", tcd->bc12_retry_interval);
}

static enum alarmtimer_restart
	tc_detect_bc12_retry_timer_func(struct alarm *alarm, ktime_t now)
{
	struct tc_detect *tcd =
		container_of(alarm, struct tc_detect, bc12_retry_timer);

	schedule_work(&tcd->bc12_retry_work);

	pr_info("run bc12_retry_timer func\n");

	return ALARMTIMER_NORESTART;
}

static void tc_detect_start_bc12_retry_timer(struct tc_detect *tcd)
{
	struct timespec64 time, time_now;
	ktime_t temp_time;
	ktime_t ktime;
	int ret;

	/* If the timer was already set, cancel it */
	ret = alarm_try_to_cancel(&tcd->bc12_retry_timer);
	if (ret < 0) {
		pr_err("callback was running, skip timer\n");
		return;
	}

	temp_time = ktime_get_boottime();
	time_now = ktime_to_timespec64(temp_time);

	time.tv_sec = time_now.tv_sec + tcd->bc12_retry_interval;
	time.tv_nsec = 0;

	ktime = ktime_set(time.tv_sec, time.tv_nsec);

	alarm_start(&tcd->bc12_retry_timer, ktime);

	pr_info("alarm hweoc timer start:%d, %lld %ld\n",
		ret, time.tv_sec, time.tv_nsec);
}

static void tc_detect_init_bc12_retry_timer(struct tc_detect *tcd)
{
	alarm_init(&tcd->bc12_retry_timer, ALARM_BOOTTIME,
		tc_detect_bc12_retry_timer_func);

	INIT_WORK(&tcd->bc12_retry_work, tc_detect_bc12_retry_work);

	pr_info("bc12 retry timer init\n");
}

static void kernel_power_off_charging_check(struct tc_detect *tcd)
{
	int bootmode = 0;

        // KERNEL_POWER_OFF_CHARGING_BOOT = 8, LOW_POWER_OFF_CHARGING_BOOT = 9
	bootmode = tc_get_boot_mode(); 
	if (bootmode == 8 || bootmode == 9)
		tcd->tcpc_kpoc = true;
	else
		tcd->tcpc_kpoc = false;

	pr_info("bootmode = %d, %d", bootmode, tcd->tcpc_kpoc);
	
}

static int tc_detect_probe(struct platform_device *pdev)
{
	struct tc_detect *tcd = NULL;
	int ret = 0;

	pr_info("starts\n");

	tcd = devm_kzalloc(&pdev->dev, sizeof(*tcd), GFP_KERNEL);
	if (IS_ERR_OR_NULL(tcd)) {
		pr_err("tcd devm_kzalloc failed\n");
		return -ENOMEM;
	}

	dev_set_drvdata(&pdev->dev, tcd);
	tcd->pdev = pdev;
	tcd->dev = &pdev->dev;

	init_waitqueue_head(&tcd->attach_wq);
	atomic_set(&tcd->chrdet_start, 0);
	mutex_init(&tcd->attach_lock);
	mutex_init(&tcd->bc12_lock);
	mutex_init(&tcd->wired_data_lock);
	mutex_init(&tcd->wireless_data_lock);

	/* init param */
	tcd->status_ctrl = -1;
	tcd->pd_aicr_lmit = -1;
	tcd->usb_suspend_pp_flag = true;

	tc_parse_dt(tcd);

	ret = tc_detect_psy_register(tcd);
	if (ret != 0) {
		pr_err("tc detect psy register failed\n");
		return ret;
	}

	INIT_WORK(&tcd->bc12_work, tc_detect_bc12_work);
	tc_detect_init_bc12_retry_timer(tcd);

	tcd->pm_nb.notifier_call = tc_detect_pm_event;
	ret = register_pm_notifier(&tcd->pm_nb);
	if (ret < 0) {
		pr_err("register pm failed. ret:%d\n", ret);
		return ret;
	}

	tcd->pd_nb.notifier_call = tc_pd_tcp_notifier_call;
	ret = register_tc_tcpc_notifier(&tcd->pd_nb);
	if (ret < 0) {
		pr_err("register tcpc notifier failed, ret:%d", ret);
		return ret;
	}

	ret = tcd_get_related_module(tcd);
	if (ret < 0) {
		pr_err("tcd failed to get related module\n");
		return -ENODEV;
	}

	tcd->chg_nb.notifier_call = tc_chg_notifier_call;
	register_tran_device_notifier(tcd->tc_charger_dev, &tcd->chg_nb);

	kernel_power_off_charging_check(tcd);

	tcd->attach_task = kthread_run(typec_attach_thread, tcd,
				       "typec_attach_thread");
	if (IS_ERR_OR_NULL(tcd->attach_task)) {
		pr_err("run typec attach kthread fail\n");
		return -EINVAL;
	}

	pr_info("done\n");

	return 0;
}

static const struct of_device_id tc_detect_of_match[] = {
	{.compatible = "tc_chgtype",},
	{},
};

static int tc_detect_remove(struct platform_device *pdev)
{
	return 0;
}

MODULE_DEVICE_TABLE(of, tc_detect_of_match);

static struct platform_driver tc_detect_driver = {
	.probe = tc_detect_probe,
	.remove = tc_detect_remove,
	.driver = {
		.name = "Tc_chgtype",
		.of_match_table = tc_detect_of_match,
	},
};

static int __init tc_detect_init(void)
{
	return platform_driver_register(&tc_detect_driver);
}
late_initcall(tc_detect_init);

static void __exit tc_detect_exit(void)
{
	platform_driver_unregister(&tc_detect_driver);
}
module_exit(tc_detect_exit);

MODULE_DESCRIPTION("TC Charger type detect Hal Device Driver");
MODULE_LICENSE("GPL");
