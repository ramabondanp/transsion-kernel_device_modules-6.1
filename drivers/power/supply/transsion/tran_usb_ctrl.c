// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#define pr_fmt(fmt)     "[USB_CONTROL] %s: " fmt, __func__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
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
#include "tc_charger_class.h"
#include "tc_common_class.h"
#include "tc_ta_class.h"
#include "tc_misc_intf.h"
#include "tc_voter.h"

struct usb_info {
	struct device *dev;
	struct tran_device *ac_ctl_dev;
	struct tran_properties usb_control_props;
	struct charger_device *chg1_dev;
	struct charger_device *dvchg1_dev;
	struct mutex usb_lock;
	struct mutex pinctrl_lock;
	struct mutex gpio_lock;
	struct tran_device *usb_control_dev;
	int tc30_control_type;
	int hvdcp20_control_type;
	int hvdcp30_control_type;
	int rfc_control_type;
	int ta_control_type;
	int effector;
	int type;
	int dp_gpio;
	int dm_gpio;
	bool usb_plug_in;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pin_default;
	struct pinctrl_state *hvdcp_handshake;
	struct pinctrl_state *hvdcp_boost_5v;
	struct pinctrl_state *hvdcp_boost_9v;
	struct pinctrl_state *hvdcp_reset;
	struct pinctrl_state *tc30_handshake;
	struct pinctrl_state *tc30_reset;
	struct pinctrl_state *rfc_i2c_mode;
	struct pinctrl_state *rfc_dual_high;
	struct pinctrl_state *rfc_high_low;
	struct pinctrl_state *rfc_reset;
	struct pinctrl_state *rfc_usb_switch_on;
	struct pinctrl_state *rfc_usb_switch_off;
	struct pinctrl_state *ta_off;
	struct pinctrl_state *ta_reset;

};

enum {
	USB_CTRL_NONE = 0,
	USB_CTRL_HVDCP20,
	USB_CTRL_HVDCP30,
	USB_CTRL_TC30,
	USB_CTRL_RFC,
	USB_CTRL_TA_OFF,
};

enum {
	TC30_CONTROL_NOT_SUPPORT = 0,
	TC30_CONTROL_BY_SWITCH_IC,
	TC30_CONTROL_BY_GPIO,
	TC30_CONTROL_BY_SWITCH_IC_AND_GPIO,
	TC30_CONTROL_BY_SWITCH_IC_ONESHOT,
	TC30_CONTROL_BY_GPIO_AND_WAS4783,

	HVDCP20_CONTROL_NOT_SUPPORT = 0,
	HVDCP20_CONTROL_BY_SWITCH_IC,
	HVDCP20_CONTROL_BY_GPIO,
	HVDCP20_CONTROL_BY_SWITCH_IC_AND_GPIO,
	HVDCP20_CONTROL_BY_SWITCH_IC_COUPLER,

	HVDCP30_CONTROL_NOT_SUPPORT = 0,
	HVDCP30_CONTROL_BY_SWITCH_IC,
	HVDCP30_CONTROL_BY_CP_IC,

	RFC_CONTROL_NOT_SUPPORT = 0,
	RFC_CONTROL_BY_CP_IC,
	RFC_CONTROL_BY_GPIO,
	RFC_CONTROL_BY_CP_IC_AND_GPIO,
	RFC_CONTROL_BY_WAS4783_AND_GPIO,

	TA_CONTROL_NOT_SUPPORT = 0,
	TA_CONTROL_BY_GPIO,
	TA_CONTROL_BY_WAS4783,
};

static const char * const usb_ctrl_name[] = {
	[USB_CTRL_NONE]      = "USB_CTRL_NONE",
	[USB_CTRL_HVDCP20]   = "USB_CTRL_HVDCP20",
	[USB_CTRL_HVDCP30]   = "USB_CTRL_HVDCP30",
	[USB_CTRL_TC30]	     = "USB_CTRL_TC30",
	[USB_CTRL_RFC]	     = "USB_CTRL_RFC",
	[USB_CTRL_TA_OFF]    = "USB_CTRL_TA_OFF",
};

static const char *tc30_state_to_str(int state)
{
	switch (state) {
	case TC30_NONE:
		return "TC30_NONE";
	case TC30_7V5:
		return "TC30_7V5";
	case TC30_SWITCH_5V:
		return "TC30_SWITCH_5V";
	case TC30_SWITCH_7V5:
		return "TC30_SWITCH_7V5";
	default:
		break;
	}
	pr_err("%s unknown state:%d\n", __func__, state);

	return "UNKNOWN";
}

static const char *hvdcp20_state_to_str(int state)
{
	switch (state) {
	case HVDCP20_NONE:
		return "HVDCP20_NONE";
	case HVDCP20_9V:
		return "HVDCP20_9V";
	case HVDCP20_SWITCH_5V:
		return "HVDCP20_SWITCH_5V";
	case HVDCP20_SWITCH_9V:
		return "HVDCP20_SWITCH_9V";
	default:
		break;
	}
	pr_err("%s unknown state:%d\n", __func__, state);

	return "UNKNOWN";
}

static const char *hvdcp30_state_to_str(int state)
{
	switch (state) {
	case HVDCP30_NONE:
		return "HVDCP30_NONE";
	case HVDCP30_HANDSHAKE:
		return "HVDCP30_HANDSHAKE";
	case HVDCP30_DP_PULSE:
		return "HVDCP30_DP_PULSE";
	case HVDCP30_DM_PULSE:
		return "HVDCP30_DM_PULSE";
	default:
		break;
	}
	pr_err("%s unknown state:%d\n", __func__, state);

	return "UNKNOWN";
}

static const char *rfc_state_to_str(int state)
{
	switch (state) {
	case RFC_NONE:
		return "RFC_NONE";
	case RFC_HANDSHAKE:
		return "RFC_HANDSHAKE";
	default:
		break;
	}
	pr_err("%s unknown state:%d\n", __func__, state);

	return "UNKNOWN";
}

static const char *rfc_ctrl_type_to_str(int state)
{
	switch (state) {
	case RFC_CONTROL_NOT_SUPPORT:
		return "RFC_CONTROL_NOT_SUPPORT";
	case RFC_CONTROL_BY_CP_IC:
		return "RFC_CONTROL_BY_CP_IC";
	case RFC_CONTROL_BY_GPIO:
		return "RFC_CONTROL_BY_GPIO";
	case RFC_CONTROL_BY_CP_IC_AND_GPIO:
		return "RFC_CONTROL_BY_CP_IC_AND_GPIO";
	case RFC_CONTROL_BY_WAS4783_AND_GPIO:
		return "RFC_CONTROL_BY_WAS4783_AND_GPIO";
	default:
		break;
	}
	pr_err("unknown state:%d\n", state);

	return "UNKNOWN";
}

static const char *tc30_ctrl_type_to_str(int state)
{
	switch (state) {
	case TC30_CONTROL_NOT_SUPPORT:
		return "TC30_CONTROL_NOT_SUPPORT";
	case TC30_CONTROL_BY_SWITCH_IC:
		return "TC30_CONTROL_BY_SWITCH_IC";
	case TC30_CONTROL_BY_GPIO:
		return "TC30_CONTROL_BY_GPIO";
	case TC30_CONTROL_BY_SWITCH_IC_AND_GPIO:
		return "TC30_CONTROL_BY_SWITCH_IC_AND_GPIO";
	case TC30_CONTROL_BY_SWITCH_IC_ONESHOT:
		return "TC30_CONTROL_BY_SWITCH_IC_ONESHOT";
	case TC30_CONTROL_BY_GPIO_AND_WAS4783:
		return "TC30_CONTROL_BY_GPIO_AND_WAS4783";
	default:
		break;
	}
	pr_err("unknown state:%d\n", state);

	return "UNKNOWN";
}

static const char *hvdcp20_ctrl_type_to_str(int state)
{
	switch (state) {
	case HVDCP20_CONTROL_NOT_SUPPORT:
		return "HVDCP20_CONTROL_NOT_SUPPORT";
	case HVDCP20_CONTROL_BY_SWITCH_IC:
		return "HVDCP20_CONTROL_BY_SWITCH_IC";
	case HVDCP20_CONTROL_BY_GPIO:
		return "HVDCP20_CONTROL_BY_GPIO";
	case HVDCP20_CONTROL_BY_SWITCH_IC_AND_GPIO:
		return "HVDCP20_CONTROL_BY_SWITCH_IC_AND_GPIO";
	case HVDCP20_CONTROL_BY_SWITCH_IC_COUPLER:
		return "HVDCP20_CONTROL_BY_SWITCH_IC_COUPLER";
	default:
		break;
	}
	pr_err("unknown state:%d\n", state);

	return "UNKNOWN";
}

static const char *hvdcp30_ctrl_type_to_str(int state)
{
	switch (state) {
	case HVDCP30_CONTROL_NOT_SUPPORT:
		return "HVDCP30_CONTROL_NOT_SUPPORT";
	case HVDCP30_CONTROL_BY_SWITCH_IC:
		return "HVDCP30_CONTROL_BY_SWITCH_IC";
	case HVDCP30_CONTROL_BY_CP_IC:
		return "HVDCP30_CONTROL_BY_CP_IC";
	default:
		break;
	}
	pr_err("unknown state:%d\n", state);

	return "UNKNOWN";
}

static const char *ta_ctrl_type_to_str(int state)
{
	switch (state) {
	case TA_CONTROL_NOT_SUPPORT:
		return "TA_CONTROL_NOT_SUPPORT";
	case TA_CONTROL_BY_GPIO:
		return "TA_CONTROL_BY_GPIO";
	case TA_CONTROL_BY_WAS4783:
		return "TA_CONTROL_BY_WAS4783";
	default:
		break;
	}
	pr_err("unknown state:%d\n", state);

	return "UNKNOWN";
}

struct tran_usb_ctrl_desc {
	const char *name;
	int (*hdlr)(struct usb_info *info, int val);
};

#define TRAN_NOTIFY_DESC(_name) \
	{.name = #_name, \
	 .hdlr = tran_##_name##_setting_handler}

static int pinctrl_select_state_lock(struct usb_info *info,
			struct pinctrl_state *state)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(state)) {
		pr_err("pinctrl error!\n");
		return -EINVAL;
	}

	mutex_lock(&info->pinctrl_lock);
	ret = pinctrl_select_state(info->pinctrl, state);
	mutex_unlock(&info->pinctrl_lock);
	
	return ret;
}

static ssize_t show_pinctrl_debug(struct device *dev,struct device_attribute *attr,char *buf)
{
	return sprintf(buf, "NOT SUPPORT\n");
}

static ssize_t store_pinctrl_debug(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = 0, index = 0;
	struct usb_info *info = dev_get_drvdata(dev);

	sscanf(buf, "%d", &index);

	pr_info("index = %d\n", index);
	switch (index) {
	case 0:
		ret = pinctrl_select_state_lock(info, info->pin_default);
		break;
	case 1:
		ret = pinctrl_select_state_lock(info, info->hvdcp_handshake);
		break;
	case 2:
		ret = pinctrl_select_state_lock(info, info->hvdcp_boost_5v);
		break;
	case 3:
		ret = pinctrl_select_state_lock(info, info->hvdcp_boost_9v);
		break;
	case 4:
		ret = pinctrl_select_state_lock(info, info->hvdcp_reset);
		break;
	case 5:
		ret = pinctrl_select_state_lock(info, info->tc30_handshake);
		break;
	case 6:
		ret = pinctrl_select_state_lock(info, info->tc30_reset);
		break;
	case 7:
		ret = pinctrl_select_state_lock(info, info->rfc_i2c_mode);
		break;
	case 8:
		ret = pinctrl_select_state_lock(info, info->rfc_dual_high);
		break;
	case 9:
		ret = pinctrl_select_state_lock(info, info->rfc_high_low);
		break;
	case 10:
		ret = pinctrl_select_state_lock(info, info->rfc_reset);
		break;
	case 11:
		ret = pinctrl_select_state_lock(info, info->rfc_usb_switch_on);
		break;
	case 12:
		ret = pinctrl_select_state_lock(info, info->rfc_usb_switch_off);
		break;
	}
	if (ret != 0) {
		pr_err("pinctrl_debug failed(%d)\n", ret);
	}

	return size;
}
static DEVICE_ATTR(pinctrl_debug, 0664, show_pinctrl_debug, store_pinctrl_debug);

static int hvdcp20_control_by_switch_ic_and_gpio_function(struct usb_info *info, int val)
{
	int i, ret = 0;

	pr_info("state: %s\n", hvdcp20_state_to_str(val));

	switch (val) {
	case HVDCP20_NONE:
		ret = pinctrl_select_state_lock(info, info->hvdcp_reset);
		if (ret != 0) {
			pr_err("hvdcp20 reset pinctrl by switch ic and gpio failed(%d)\n", ret);
			goto out;
		}
		charger_dev_set_dp(info->chg1_dev, DPDM_CTRL_HZ);
		charger_dev_set_dm(info->chg1_dev, DPDM_CTRL_HZ);
		msleep(100);
		break;
	case HVDCP20_9V:
		ret = charger_dev_set_dp(info->chg1_dev, DPDM_CTRL_0_6V);
		if (ret != 0) {
			pr_err("hvdcp20 handshake by switch ic and gpio failed(%d)\n", ret);
			goto out;
		}
		/* Need delay max 1.5s wait dm reset to 0mV */
		for (i = 0; i < 15; i++) {
			if (!info->usb_plug_in)
				goto out;
			msleep(100);
		}
		ret = pinctrl_select_state_lock(info, info->hvdcp_boost_9v);
		ret = charger_dev_set_dm(info->chg1_dev, DPDM_CTRL_0_6V);
		ret = charger_dev_set_dp(info->chg1_dev, DPDM_CTRL_HZ);
		if (ret != 0) {
			pr_err("hvdcp20 boost 9v by switch ic and gpio failed(%d)\n", ret);
			goto out;
		}
		msleep(100);
		break;
	case HVDCP20_SWITCH_5V:
		charger_dev_set_dm(info->chg1_dev, DPDM_CTRL_HZ);
		charger_dev_set_dp(info->chg1_dev, DPDM_CTRL_0_6V);
		ret = pinctrl_select_state_lock(info, info->hvdcp_boost_5v);
		if (ret != 0) {
			pr_err("hvdcp20 switch 5v by switch ic and gpio failed(%d)\n", ret);
			goto out;
		}
		msleep(100);
		break;
	case HVDCP20_SWITCH_9V:
		ret = pinctrl_select_state_lock(info, info->hvdcp_boost_9v);
		ret = charger_dev_set_dm(info->chg1_dev, DPDM_CTRL_0_6V);
		ret = charger_dev_set_dp(info->chg1_dev, DPDM_CTRL_HZ);
		if (ret != 0) {
			pr_err("hvdcp20 switch 9v by switch ic and gpio failed(%d)\n", ret);
			goto out;
		}
		msleep(100);
		break;
	default:
		break;
	}

out:
	return ret;
}

static int hvdcp20_control_by_switch_ic_coupler_function(struct usb_info *info, int val)
{
	int i, ret = 0;

	pr_info("state: %s\n", hvdcp20_state_to_str(val));

	switch (val) {
	case HVDCP20_NONE:
		ret = charger_dev_set_dp_dm_coupler(info->chg1_dev, DPDM_CTRL_DP_HZ_DM_HZ);
		if (ret != 0) {
			pr_err("hvdcp20 reset by switch ic failed(%d)\n", ret);
			goto out;
		}
		msleep(100);
		break;
	case HVDCP20_9V:
		ret = charger_dev_set_dp_dm_coupler(info->chg1_dev, DPDM_CTRL_DP_HZ_DM_HZ);
		if (ret != 0) {
			pr_err("hvdcp20 handshake set dpdm by switch ic failed(%d)\n", ret);
			goto out;
		}
		msleep(100);

		ret = charger_dev_set_dp_dm_coupler(info->chg1_dev, DPDM_CTRL_DP_0_6_DM_HZ);
		if (ret != 0) {
			pr_err("hvdcp20 handshake by switch ic failed(%d)\n", ret);
			goto out;
		}

		/* Need delay max 1.5s wait dm reset to 0mV */
		for (i = 0; i < 15; i++) {
			if (!info->usb_plug_in)
				goto out;
			msleep(100);
		}

		ret = charger_dev_set_dp_dm_coupler(info->chg1_dev, DPDM_CTRL_DP_3_3_DM_0_6);
		if (ret != 0) {
			pr_err("hvdcp20 boost 9v by switch ic failed(%d)\n", ret);
			goto out;
		}
		msleep(100);
		break;
	case HVDCP20_SWITCH_5V:
		ret = charger_dev_set_dp_dm_coupler(info->chg1_dev, DPDM_CTRL_DP_0_6_DM_HZ);
		if (ret != 0) {
			pr_err("hvdcp20 switch 5v by switch ic failed(%d)\n", ret);
			goto out;
		}
		msleep(100);
		break;
	case HVDCP20_SWITCH_9V:
		ret = charger_dev_set_dp_dm_coupler(info->chg1_dev, DPDM_CTRL_DP_3_3_DM_0_6);
		if (ret != 0) {
			pr_err("hvdcp20 switch 9v by switch ic failed(%d)\n", ret);
			goto out;
		}
		msleep(100);
		break;
	default:
		break;
	}

out:
	return ret;

}

static int hvdcp20_control_by_switch_ic_function(struct usb_info *info, int val)
{
	int i, ret = 0;

	pr_info("state: %s\n", hvdcp20_state_to_str(val));

	switch (val) {
	case HVDCP20_NONE:
		ret = charger_dev_set_dp(info->chg1_dev, DPDM_CTRL_HZ);
		if (ret != 0) {
			pr_err("hvdcp20 reset by switch ic failed(%d)\n", ret);
			goto out;
		}
		ret = charger_dev_set_dm(info->chg1_dev, DPDM_CTRL_HZ);
		if (ret != 0) {
			pr_err("hvdcp20 reset by switch ic failed(%d)\n", ret);
			goto out;
		}
		msleep(100);
		break;
	case HVDCP20_9V:
		if (!info->usb_plug_in)
			goto out;
		/* Resolve the issue of some third-party chargers not recognizing the QC protocol */
		ret = charger_dev_set_dp(info->chg1_dev, DPDM_CTRL_0_6V);
		if (ret != 0) {
			pr_err("set dp 0.6 failed (%d)\n", ret);
		}
		ret = charger_dev_set_dm(info->chg1_dev, DPDM_CTRL_0_6V);
		if (ret != 0) {
			pr_err("set dm 0.6 failed (%d)\n", ret);
		}
		msleep(100);

		ret = charger_dev_set_dp(info->chg1_dev, DPDM_CTRL_HZ);
		if (ret != 0) {
			pr_err("hvdcp20 handshake by switch ic set dp failed(%d)\n", ret);
			goto out;
		}
		ret = charger_dev_set_dm(info->chg1_dev, DPDM_CTRL_HZ);
		if (ret != 0) {
			pr_err("hvdcp20 handshake by switch ic set dm failed(%d)\n", ret);
			goto out;
		}

		msleep(100);

		ret = charger_dev_set_dp(info->chg1_dev, DPDM_CTRL_0_6V);
		if (ret != 0) {
			pr_err("hvdcp20 handshake by switch ic failed(%d)\n", ret);
			goto out;
		}
		/* Need delay max 1.5s wait dm reset to 0mV */
		for (i = 0; i < 15; i++) {
			if (!info->usb_plug_in)
				goto out;
			msleep(100);
		}
		ret = charger_dev_set_dp(info->chg1_dev, DPDM_CTRL_3_3V);
		if (ret != 0) {
			pr_err("hvdcp20 boost 9v by switch ic failed(%d)\n", ret);
			goto out;
		}
		ret = charger_dev_set_dm(info->chg1_dev, DPDM_CTRL_0_6V);
		if (ret != 0) {
			pr_err("hvdcp20 boost 9v by switch ic failed(%d)\n", ret);
			goto out;
		}
		msleep(200);
		break;
	case HVDCP20_SWITCH_5V:
		if (!info->usb_plug_in)
			goto out;
		ret = charger_dev_set_dm(info->chg1_dev, DPDM_CTRL_HZ);
		if (ret != 0) {
			pr_err("hvdcp20 switch 5v by switch ic failed(%d)\n", ret);
			goto out;
		}
		ret = charger_dev_set_dp(info->chg1_dev, DPDM_CTRL_0_6V);
		if (ret != 0) {
			pr_err("hvdcp20 switch 5v by switch ic failed(%d)\n", ret);
			goto out;
		}
		msleep(100);
		break;
	case HVDCP20_SWITCH_9V:
		if (!info->usb_plug_in)
			goto out;
		ret = charger_dev_set_dp(info->chg1_dev, DPDM_CTRL_3_3V);
		ret = charger_dev_set_dm(info->chg1_dev, DPDM_CTRL_0_6V);
		if (ret != 0) {
			pr_err("hvdcp20 switch 9v by switch ic failed(%d)\n", ret);
			goto out;
		}
		msleep(100);
		break;
	default:
		break;
	}

out:
	return ret;
}

static int hvdcp20_control_by_gpio_function(struct usb_info *info, int val)
{
	int i, ret = 0;

	pr_info("state: %s\n", hvdcp20_state_to_str(val));

	switch (val) {
	case HVDCP20_NONE:
		ret = pinctrl_select_state_lock(info, info->hvdcp_reset);
		if (ret != 0) {
			pr_err("hvdcp20 reset by gpio failed(%d)\n", ret);
			goto out;
		}
		msleep(100);
		break;
	case HVDCP20_9V:
		ret = pinctrl_select_state_lock(info, info->hvdcp_handshake);
		if (ret != 0) {
			pr_err("hvdcp20 handshake by gpio failed(%d)\n", ret);
			goto out;
		}
		/* Need delay max 1.5s wait dm reset to 0mV */
		for (i = 0; i < 15; i++) {
			if (!info->usb_plug_in)
				goto out;
			msleep(100);
		}
		ret = pinctrl_select_state_lock(info, info->hvdcp_boost_9v);
		if (ret != 0) {
			pr_err("hvdcp20 boost 9v by gpio failed(%d)\n", ret);
			goto out;
		}
		msleep(100);
		break;
	case HVDCP20_SWITCH_5V:
		ret = pinctrl_select_state_lock(info, info->hvdcp_boost_5v);
		if (ret != 0) {
			pr_err("hvdcp20 switch 5v by gpio failed(%d)\n", ret);
			goto out;
		}
		msleep(100);
		break;
	case HVDCP20_SWITCH_9V:
		ret = pinctrl_select_state_lock(info, info->hvdcp_boost_9v);
		if (ret != 0) {
			pr_err("hvdcp20 switch 9v by gpio failed(%d)\n", ret);
			goto out;
		}
		msleep(100);
		break;
	default:
		break;
	}
out:
	return ret;
}

static int hvdcp30_control_by_switch_ic_function(struct usb_info *info, int val)
{
	int ret = 0;
	int i;
	int pre_vbus = 0, post_vbus = 0;

	pr_info("state: %s\n", hvdcp30_state_to_str(val));

	switch (val) {
	case HVDCP30_NONE:
		ret = charger_dev_set_dp(info->chg1_dev, DPDM_CTRL_HZ);
		if (ret != 0) {
			pr_err("hvdcp30 reset by switch ic failed(%d)\n", ret);
			goto out;
		}
		ret = charger_dev_set_dm(info->chg1_dev, DPDM_CTRL_HZ);
		if (ret != 0) {
			pr_err("hvdcp30 reset by switch ic failed(%d)\n", ret);
			goto out;
		}
		msleep(100);
		break;
	case HVDCP30_HANDSHAKE:
		ret = charger_dev_set_dp(info->chg1_dev, DPDM_CTRL_HZ);
		if (ret != 0) {
			pr_err("hvdcp30 handshake by switch ic set dp failed(%d)\n", ret);
			goto out;
		}
		ret = charger_dev_set_dm(info->chg1_dev, DPDM_CTRL_HZ);
		if (ret != 0) {
			pr_err("hvdcp30 handshake by switch ic set dm failed(%d)\n", ret);
			goto out;
		}

		msleep(100);

		ret = charger_dev_set_dp(info->chg1_dev, DPDM_CTRL_0_6V);
		if (ret != 0) {
			pr_err("hvdcp30 handshake by switch ic failed(%d)\n", ret);
			goto out;
		}
		/* Need delay max 1.5s wait dm reset to 0mV */
		for (i = 0; i < 15; i++) {
			if (!info->usb_plug_in)
				goto out;
			msleep(100);
		}
		ret = charger_dev_set_dm(info->chg1_dev, DPDM_CTRL_3_3V);
		if (ret != 0) {
			pr_err("hvdcp30 into continues mode failed(%d)\n", ret);
			goto out;
		}
		msleep(100);

		// check CLASS_A(3.6 ~ 12) or CLASS_B(3.6 ~ 20)

		pre_vbus = tc_get_vbus();
		/* vbus up */
		for (i = 0; i < 10; i++) {

			if (!info->usb_plug_in)
				goto out;

			ret = charger_dev_set_dp(info->chg1_dev, DPDM_CTRL_3_3V);
			if (ret != 0) {
				pr_err("hvdcp30 handshake dp pulse step_1 failed(%d)\n", ret);
				goto out;
			}

			ret = charger_dev_set_dp(info->chg1_dev, DPDM_CTRL_0_6V);
			if (ret != 0) {
				pr_err("hvdcp30 handshake dp pulse step_2 failed(%d)\n", ret);
				goto out;
			}

			msleep(60);
		}

		post_vbus = tc_get_vbus();
		if ((post_vbus - pre_vbus) < 1000) {
			pr_info("is not hvdcp30, post_vbus:%dmV, pre_vbus:%dmV\n",
				post_vbus, pre_vbus);
			ret = NOT_HVDCP30;
			goto out;
		}

		ret = IS_HVDCP30_A;
		/* vbus down */
		for (i = 0; i < 10; i++) {

			if (!info->usb_plug_in)
				goto out;

			ret = charger_dev_set_dm(info->chg1_dev, DPDM_CTRL_0_6V);
			if (ret != 0) {
				pr_err("hvdcp30 handshake dm pulse step_1 failed(%d)\n", ret);
				goto out;
			}

			ret = charger_dev_set_dm(info->chg1_dev, DPDM_CTRL_3_3V);
			if (ret != 0) {
				pr_err("hvdcp30 handshake dm pulse step_2 failed(%d)\n", ret);
				goto out;
			}

			msleep(60);
		}

		break;
	case HVDCP30_DP_PULSE: /* vbus up */
		ret = charger_dev_set_dp(info->chg1_dev, DPDM_CTRL_3_3V);
		if (ret != 0) {
			pr_err("hvdcp30 dp pulse step_1 failed(%d)\n", ret);
			goto out;
		}

		ret = charger_dev_set_dp(info->chg1_dev, DPDM_CTRL_0_6V);
		if (ret != 0) {
			pr_err("hvdcp30 dp pulse step_2 failed(%d)\n", ret);
			goto out;
		}

		msleep(100);
		break;
	case HVDCP30_DM_PULSE: /* vbus down */
		ret = charger_dev_set_dm(info->chg1_dev, DPDM_CTRL_0_6V);
		if (ret != 0) {
			pr_err("hvdcp30 dm pulse step_1 failed(%d)\n", ret);
			goto out;
		}

		ret = charger_dev_set_dm(info->chg1_dev, DPDM_CTRL_3_3V);
		if (ret != 0) {
			pr_err("hvdcp30 dm pulse step_2 failed(%d)\n", ret);
			goto out;
		}

		msleep(100);
		break;
	default:
		break;
	}

	return ret;
out:
	ret = NOT_HVDCP30;
	return ret;
}

static int hvdcp30_control_by_cp_ic_function(struct usb_info *info, int val)
{
	int ret = 0;
	int i;
	int pre_vbus = 0, post_vbus = 0;
	struct votable *total_aicr_vote = NULL;

	pr_info("state: %s\n", hvdcp30_state_to_str(val));

	total_aicr_vote = find_votable("total_aicr");
	if (total_aicr_vote == NULL) {
		pr_info("can't find total_aicr_vote\n");
		goto out;
	}
			
	switch (val) {
	case HVDCP30_NONE:
		ret = charger_dev_set_dp(info->dvchg1_dev, DPDM_CTRL_HZ);
		if (ret != 0) {
			pr_err("hvdcp30 reset dp by switch ic failed(%d)\n", ret);
			goto out;
		}
		ret = charger_dev_set_dm(info->dvchg1_dev, DPDM_CTRL_HZ);
		if (ret != 0) {
			pr_err("hvdcp30 reset dm by switch ic failed(%d)\n", ret);
			goto out;
		}
		ret = charger_dev_en_dpdm_ctrl(info->dvchg1_dev, false);
		if (ret != 0) {
			pr_err("hvdcp30 close dpdm ctrl by switch ic failed(%d)\n", ret);
			goto out;
		}
		msleep(100);
		break;
	case HVDCP30_HANDSHAKE:

		if (!info->usb_plug_in)
			goto out;

		ret = charger_dev_en_dpdm_ctrl(info->dvchg1_dev, true );
		if (ret != 0) {
			pr_err("hvdcp30 en dpdm ctrl by switch ic failed(%d)\n", ret);
			goto out;
		}

		ret = charger_dev_set_dp(info->dvchg1_dev, DPDM_CTRL_HZ);
		if (ret != 0) {
			pr_err("hvdcp30 handshake by switch ic set dp failed(%d)\n", ret);
			goto out;
		}
		ret = charger_dev_set_dm(info->dvchg1_dev, DPDM_CTRL_HZ);
		if (ret != 0) {
			pr_err("hvdcp30 handshake by switch ic set dm failed(%d)\n", ret);
			goto out;
		}

		msleep(100);

		ret = charger_dev_set_dp(info->dvchg1_dev, DPDM_CTRL_0_6V);
		if (ret != 0) {
			pr_err("hvdcp30 handshake by switch ic failed(%d)\n", ret);
			goto out;
		}
		/* Need delay max 1.5s wait dm reset to 0mV */
		for (i = 0; i < 15; i++) {
			if (!info->usb_plug_in)
				goto out;
			msleep(100);
		}
		ret = charger_dev_set_dm(info->dvchg1_dev, DPDM_CTRL_3_3V);
		if (ret != 0) {
			pr_err("hvdcp30 into continues mode failed(%d)\n", ret);
			goto out;
		}
		msleep(100);

		vote(total_aicr_vote, HVDCP30_VOTER, true, 500000);
		// check CLASS_A(3.6 ~ 12) or CLASS_B(3.6 ~ 20)

		pre_vbus = tc_get_vbus();
		/* vbus up */
		for (i = 0; i < 10; i++) {

			if (!info->usb_plug_in)
				goto out;

			ret = charger_dev_set_dp(info->dvchg1_dev, DPDM_CTRL_3_3V);
			if (ret != 0) {
				pr_err("hvdcp30 handshake dp pulse step_1 failed(%d)\n", ret);
				goto out;
			}

			ret = charger_dev_set_dp(info->dvchg1_dev, DPDM_CTRL_0_6V);
			if (ret != 0) {
				pr_err("hvdcp30 handshake dp pulse step_2 failed(%d)\n", ret);
				goto out;
			}

			msleep(60);
		}

		post_vbus = tc_get_vbus();
		if ((post_vbus - pre_vbus) < 1000) {
			pr_info("is not hvdcp30, post_vbus:%dmV, pre_vbus:%dmV\n",
				post_vbus, pre_vbus);
			ret = NOT_HVDCP30;
			goto out;
		}

		/* vbus up */
		for (i = 0; i < 28; i++) {

			if (!info->usb_plug_in)
				goto out;

			ret = charger_dev_set_dp(info->dvchg1_dev, DPDM_CTRL_3_3V);
			if (ret != 0) {
				pr_err("hvdcp30 handshake dp pulse step_1 failed(%d)\n", ret);
				goto out;
			}

			ret = charger_dev_set_dp(info->dvchg1_dev, DPDM_CTRL_0_6V);
			if (ret != 0) {
				pr_err("hvdcp30 handshake dp pulse step_2 failed(%d)\n", ret);
				goto out;
			}

			msleep(60);
		}

		post_vbus = tc_get_vbus();
		pr_info("is hvdcp30, up v check class, post_vbus:%dmV\n", post_vbus);
		if (post_vbus > 12000)
			ret = IS_HVDCP30_B;
		else
			ret = IS_HVDCP30_A;

		/* vbus down */
		for (i = 0; i < 40; i++) {

			if (!info->usb_plug_in)
				goto out;

			ret = charger_dev_set_dm(info->dvchg1_dev, DPDM_CTRL_0_6V);
			if (ret != 0) {
				pr_err("hvdcp30 handshake dm pulse step_1 failed(%d)\n", ret);
				goto out;
			}

			ret = charger_dev_set_dm(info->dvchg1_dev, DPDM_CTRL_3_3V);
			if (ret != 0) {
				pr_err("hvdcp30 handshake dm pulse step_2 failed(%d)\n", ret);
				goto out;
			}

			msleep(60);

			post_vbus = tc_get_vbus();
			pr_info("is hvdcp30, reset vbus, post_vbus:%dmV\n", post_vbus);
			if (post_vbus <= 5300)
				break;
		}

		vote(total_aicr_vote, HVDCP30_VOTER, false, 0);

		break;
	case HVDCP30_DP_PULSE: /* vbus up */
		if (!info->usb_plug_in)
			goto out;

		ret = charger_dev_set_dp(info->dvchg1_dev, DPDM_CTRL_3_3V);
		if (ret != 0) {
			pr_err("hvdcp30 dp pulse step_1 failed(%d)\n", ret);
			goto out;
		}

		ret = charger_dev_set_dp(info->dvchg1_dev, DPDM_CTRL_0_6V);
		if (ret != 0) {
			pr_err("hvdcp30 dp pulse step_2 failed(%d)\n", ret);
			goto out;
		}

		msleep(100);
		break;
	case HVDCP30_DM_PULSE: /* vbus down */
		if (!info->usb_plug_in)
			goto out;

		ret = charger_dev_set_dm(info->dvchg1_dev, DPDM_CTRL_0_6V);
		if (ret != 0) {
			pr_err("hvdcp30 dm pulse step_1 failed(%d)\n", ret);
			goto out;
		}

		ret = charger_dev_set_dm(info->dvchg1_dev, DPDM_CTRL_3_3V);
		if (ret != 0) {
			pr_err("hvdcp30 dm pulse step_2 failed(%d)\n", ret);
			goto out;
		}

		msleep(100);
		break;
	default:
		break;
	}

	vote(total_aicr_vote, HVDCP30_VOTER, false, 0);
	return ret;
out:
	vote(total_aicr_vote, HVDCP30_VOTER, false, 0);
	ret = NOT_HVDCP30;
	return ret;

}

static int tc30_control_by_switch_ic_function(struct usb_info *info, int val)
{
	int ret = 0;

	pr_info("state: %s\n", tc30_state_to_str(val));

	switch (val) {
	case TC30_NONE:
	case TC30_SWITCH_5V:
		ret = charger_dev_set_dp(info->chg1_dev, DPDM_CTRL_HZ);
		ret = charger_dev_set_dm(info->chg1_dev, DPDM_CTRL_HZ);
		msleep(100);
		break;
	case TC30_7V5:
	case TC30_SWITCH_7V5:
		ret = charger_dev_set_dp(info->chg1_dev, DPDM_CTRL_3_3V);
		ret = charger_dev_set_dm(info->chg1_dev, DPDM_CTRL_0V);
		msleep(100);
		break;
	default:
		break;
	}

	if (ret != 0) {
		pr_err("tc30 control by switch ic failed(%d)\n", ret);
	}

	return ret;
}

static int tc30_control_by_switch_ic_oneshot_function(struct usb_info *info, int val)
{
	int ret = 0;

	pr_info("state: %s\n", tc30_state_to_str(val));

	switch (val) {
	case TC30_NONE:
	case TC30_SWITCH_5V:
		ret = charger_dev_set_tc30_oneshot(info->chg1_dev, TC30_SWITCH_5V);
		msleep(100);
		break;
	case TC30_7V5:
	case TC30_SWITCH_7V5:
		ret = charger_dev_set_tc30_oneshot(info->chg1_dev, TC30_SWITCH_7V5);
		msleep(100);
		break;
	default:
		break;
	}

	if (ret != 0) {
		pr_err("tc30 control by switch ic failed(%d)\n", ret);
	}

	return ret;
}

static int tc30_control_by_gpio_function(struct usb_info *info, int val)
{
	int ret = 0;

	pr_info("state: %s\n", tc30_state_to_str(val));

	switch (val) {
	case TC30_NONE:
	case TC30_SWITCH_5V:
		ret = pinctrl_select_state_lock(info, info->tc30_reset);
		msleep(100);
		break;
	case TC30_7V5:
	case TC30_SWITCH_7V5:
		ret = pinctrl_select_state_lock(info, info->tc30_handshake);
		msleep(100);
		break;
	default:
		break;
	}

	if (ret != 0) {
		pr_err("tc30 pinctrl control by gpio failed(%d)\n", ret);
	}

	return ret;
}

static int tc30_control_by_gpio_and_was4783_function(struct usb_info *info, int val)
{
	int ret = 0;
	union com_propval tran_val = {0, };

	pr_info("state: %s\n", tc30_state_to_str(val));

	if (!info->usb_control_dev)
		info->usb_control_dev = tran_get_by_name("usbc_analog_switch");

	switch (val) {
	case TC30_NONE:
	case TC30_SWITCH_5V:
		ret = pinctrl_select_state_lock(info, info->tc30_reset);
		tran_val.intval = false;
		tran_dev_set_prop(info->usb_control_dev, TRAN_PROP_USB_CTRL_TC30_TA, &tran_val);
		msleep(100);
		break;
	case TC30_7V5:
	case TC30_SWITCH_7V5:
		ret = pinctrl_select_state_lock(info, info->tc30_handshake);
		tran_val.intval = true;
		tran_dev_set_prop(info->usb_control_dev, TRAN_PROP_USB_CTRL_TC30_TA, &tran_val);
		msleep(100);
		break;
	default:
		break;
	}

	if (ret != 0) {
		pr_err("tc30 pinctrl control by gpio failed(%d)\n", ret);
	}

	return ret;
}

static int gpio_get_usb_status(struct usb_info *info, int gpio, int *state)
{
	int ret = 0;

	mutex_lock(&info->gpio_lock);
	ret = gpio_request(gpio, "default");
	if (ret < 0) {
		pr_err("gpio request failed\n");
		mutex_unlock(&info->gpio_lock);
		return -EINVAL;
	}

	*state = gpio_get_value(gpio);

	gpio_free(gpio);
	mutex_unlock(&info->gpio_lock);

	return ret;

}

static void rfc_usb_switch_en(struct usb_info *info, bool en)
{
	if (info->rfc_control_type != RFC_CONTROL_BY_CP_IC_AND_GPIO &&
		info->rfc_control_type != RFC_CONTROL_BY_GPIO) {
		return;
	}

	if (en) {
		pinctrl_select_state_lock(info, info->rfc_usb_switch_on);
	} else {
		pinctrl_select_state_lock(info, info->rfc_usb_switch_off);
	}
}

static int rfc_handshake_by_gpio(struct usb_info *info)
{
	int ret = 0;
	int dp_state = 0, dm_state = 0;
	u32 val;
	struct tc_ta_classdev *rfc_ta = tc_ta_device_get_by_name("tfcp_ta");

	/* step0  */
	rfc_usb_switch_en(info, true);
	pinctrl_select_state_lock(info, info->rfc_dual_high);
	mdelay(20);

	/* step1  */
	pinctrl_select_state_lock(info, info->rfc_high_low);
	mdelay(5);
	
	gpio_get_usb_status(info, info->dp_gpio, &dp_state);
	gpio_get_usb_status(info, info->dm_gpio, &dm_state);
	if (dp_state != 0 || dm_state != 1) {
		ret = -EINVAL;
		pr_err("rfc handshake by gpio step1 failed(%d, %d)\n", dp_state, dm_state);
		goto out;
	}

	/* step2  */
	pinctrl_select_state_lock(info, info->rfc_dual_high);
	mdelay(20);
	gpio_get_usb_status(info, info->dp_gpio, &dp_state);
	gpio_get_usb_status(info, info->dm_gpio, &dm_state);
	if (dp_state != 1 || dm_state != 1) {
		ret = -EINVAL;
		pr_err("rfc handshake by gpio step2 failed(%d, %d)\n", dp_state, dm_state);
		goto out;
	}

	/* The Indian specification TA needs a delay of 100ms to detect the grid */
	mdelay(100);

	/* step3  */
	ret = tc_ta_device_get_min_voltage(rfc_ta, &val);
	if (ret != 0) {
		ret = -EINVAL;
		pr_err("rfc handshake by gpio step3 failed\n");
		goto out;
	}

	pinctrl_select_state_lock(info, info->rfc_i2c_mode);

out:
	if (ret != 0)
		rfc_usb_switch_en(info, false);
	return ret;
}

static int rfc_control_by_gpio_function(struct usb_info *info, int val)
{
	int ret = 0;

	pr_info("state: %s\n", rfc_state_to_str(val));

	switch (val) {
	case RFC_NONE:
		rfc_usb_switch_en(info, false);
		ret = pinctrl_select_state_lock(info, info->rfc_reset);
		break;
	case RFC_HANDSHAKE:
		ret = rfc_handshake_by_gpio(info);
		break;
	default:
		break;
	}

	if (ret != 0) {
		pr_err("tc30 pinctrl control by gpio failed(%d)\n", ret);
	}

	return ret;
}

static int rfc_handshake_by_cp_ic(struct usb_info *info)
{
	int ret = 0;
	int dp_state = 0, dm_state = 0;
	u32 val;
	bool is_rfc_ta = false;
	struct tc_ta_classdev *rfc_ta = tc_ta_device_get_by_name("tfcp_ta");

	ret = charger_dev_cp_rfc_detect(info->dvchg1_dev,&is_rfc_ta);
	if (ret != -ENOTSUPP) {
		ret = !is_rfc_ta;
		goto step3;
	}

	/* step0  */
	charger_dev_set_dp_dm(info->dvchg1_dev,
		DPDM_CTRL_3_3V, DPDM_CTRL_3_3V, true);

	mdelay(20);

	/* step1  */
	charger_dev_set_dp_dm(info->dvchg1_dev,
				DPDM_CTRL_0V, DPDM_CTRL_3_3V, false);
	mdelay(5);
	dp_state = !!charger_dev_get_dp_dm(info->dvchg1_dev, true);
	dm_state = !!charger_dev_get_dp_dm(info->dvchg1_dev, false);
	if (dp_state != 0 || dm_state != 1) {
		ret = -EINVAL;
		pr_err("rfc handshake step1 failed(%d, %d)\n", dp_state, dm_state);
		goto out;
	}

	/* step2  */
	charger_dev_set_dp_dm(info->dvchg1_dev,
				DPDM_CTRL_3_3V, DPDM_CTRL_3_3V, false);
	mdelay(20);
	dp_state = !!charger_dev_get_dp_dm(info->dvchg1_dev, true);
	dm_state = !!charger_dev_get_dp_dm(info->dvchg1_dev, false);
	if (dp_state != 1 || dm_state != 1) {
		ret = -EINVAL;
		pr_err("rfc handshake step2 failed(%d, %d)\n", dp_state, dm_state);
		goto out;
	}
step3:
	/* step3  */
	rfc_usb_switch_en(info, true);
	ret = tc_ta_device_get_min_voltage(rfc_ta, &val);
	if (ret != 0) {
		ret = -EINVAL;
		pr_err("rfc handshake step3 failed\n");
		goto out;
	}

out:
	if (ret != 0)
		rfc_usb_switch_en(info, false);

	return ret;
}

static int rfc_control_by_cp_ic_function(struct usb_info *info, int val)
{
	int ret = 0;

	pr_info("state: %s\n", rfc_state_to_str(val));

	switch (val) {
	case RFC_NONE:
		ret = charger_dev_set_dp_dm(info->dvchg1_dev,
			DPDM_CTRL_HZ, DPDM_CTRL_HZ, true);
		break;
	case RFC_HANDSHAKE:
		ret = rfc_handshake_by_cp_ic(info);
		break;
	default:
		break;
	}

	if (ret != 0) {
		pr_err("rfc control by cp ic failed(%d)\n", ret);
	}

	return ret;
}

static int rfc_usb_switch_by_was4783_function(struct usb_info *info, int val)
{
	int ret = 0;
	union com_propval tran_val = {0, };

	pr_info("state: %s\n", !!val ? "OFF" : "ON");

	if (!info->usb_control_dev)
		info->usb_control_dev = tran_get_by_name("usbc_analog_switch");

	tran_val.intval = val;
	ret = tran_dev_set_prop(info->usb_control_dev,
			TRAN_PROP_USB_CTRL_RFC, &tran_val);
	if (ret < 0) {
		pr_err("RFC control by was4783 failed(%d)\n", ret);
	}
	pr_info("%s rfc was4783 ret = %d\n",__func__,ret);
	return ret;
}

static int rfc_handshake_by_was4783_and_gpio(struct usb_info *info)
{
	int ret = 0;
	int dp_state = 0, dm_state = 0;
	u32 val;
	struct tc_ta_classdev *rfc_ta = tc_ta_device_get_by_name("tfcp_ta");

	/* step0 switch dp dm */
	ret = rfc_usb_switch_by_was4783_function(info,true);
	if (ret != 0) {
		pr_err("rfc handshake was4783 and gpio failed(%d)\n", ret);
		goto out;
	}

	/* step1  dp dm high 1*/
	pinctrl_select_state_lock(info, info->rfc_dual_high);
	pr_info("rfc handshake was4783 and gpio dp dm high1\n");
	mdelay(20);

	/* step2  dp low dm high */
	ret = gpio_direction_input(info->dm_gpio);
	if (ret < 0) {
		pr_err("%s: gpio_direction_input fail\n", __func__);
		return ret;
	}
	gpio_direction_output(info->dp_gpio,0);
	gpio_set_value(info->dp_gpio, 0);
	mdelay(5);
	dm_state = gpio_get_value(info->dm_gpio);
	dp_state = gpio_get_value(info->dp_gpio);
	if (dp_state != 0 || dm_state != 1) {
		ret = -EINVAL;
		pr_err("rfc handshake was4783 and gpio low/high failed(%d, %d)\n", dp_state, dm_state);
		goto out;
	}
	pr_info("rfc handshake was4783 and gpio low/high (%d,%d)\n", dp_state, dm_state);

	/* step3 dp dm high 2 */
	gpio_set_value(info->dp_gpio, 1);
	mdelay(20);
	dm_state = gpio_get_value(info->dm_gpio);
	dp_state = gpio_get_value(info->dp_gpio);
	if (dp_state != 1 || dm_state != 1) {
		ret = -EINVAL;
		pr_err("rfc handshake was4783 and gpio high/high failed(%d, %d)\n", dp_state, dm_state);
		goto out;
	}
	pr_info("rfc handshake was4783 and gpio high/high2(%d,%d)\n", dp_state, dm_state);
	
	/* The Indian specification TA needs a delay of 100ms to detect the grid */
	mdelay(100);

	/* step4 dpdm switch  i2c */
	pinctrl_select_state_lock(info, info->rfc_i2c_mode);
	pr_info("rfc handshake was4783 and gpio i2c mode\n");
	ret = tc_ta_device_get_min_voltage(rfc_ta, &val);
	if (ret != 0) {
		ret = -EINVAL;
		pr_err("rfc handshake by gpio step3 failed\n");
		goto out;
	}
	pr_info("rfc handshake was4783 and gpio successfully\n");

out:
	if (ret != 0) {
		pinctrl_select_state_lock(info, info->rfc_i2c_mode);
		rfc_usb_switch_by_was4783_function(info,false);
	}
	return ret;
}

static int rfc_control_by_was4783_and_gpio_function(struct usb_info *info, int val)
{
	int ret = 0;

	pr_info("state: %s\n", rfc_state_to_str(val));

	switch (val) {
	case RFC_NONE:
		ret = rfc_usb_switch_by_was4783_function(info,false);
		ret = pinctrl_select_state_lock(info, info->rfc_reset);
		break;
	case RFC_HANDSHAKE:
		ret = rfc_handshake_by_was4783_and_gpio(info);
		break;
	default:
		break;
	}

	if (ret != 0) {
		pr_err("rfc control by was4783 and gpio failed(%d)\n", ret);
	}

	return ret;
}

static int rfc_control_by_cp_ic_and_gpio_function(struct usb_info *info, int val)
{
	int ret = 0;

	pr_info("state: %s\n", rfc_state_to_str(val));

	switch (val) {
	case RFC_NONE:
		rfc_usb_switch_en(info, false);
		ret = charger_dev_set_dp_dm(info->dvchg1_dev,
			DPDM_CTRL_HZ, DPDM_CTRL_HZ, true);
		break;
	case RFC_HANDSHAKE:
		ret = rfc_handshake_by_cp_ic(info);
		break;
	default:
		break;
	}

	if (ret != 0) {
		pr_err("rfc control by cp ic and gpio failed(%d)\n", ret);
	}

	return ret;
}

static int ta_control_by_gpio_function(struct usb_info *info, int val)
{
	int ret = 0;

	pr_info("state: %s\n", !!val ? "OFF" : "ON");

	ret = pinctrl_select_state_lock(info, !!val ? info->ta_off : info->ta_reset);
	if (ret != 0) {
		pr_err("ta pinctrl control by gpio failed(%d)\n", ret);
	}
	return ret;
}

static int ta_control_by_was4783_function(struct usb_info *info, int val)
{
	int ret = 0;
	union com_propval tran_val = {0, };

	pr_info("state: %s\n", !!val ? "OFF" : "ON");

	if (!info->usb_control_dev)
		info->usb_control_dev = tran_get_by_name("usbc_analog_switch");

	tran_val.intval = val;
	ret = tran_dev_set_prop(info->usb_control_dev,
			TRAN_PROP_USB_CTRL_TA_OFF, &tran_val);
	if (ret < 0) {
		pr_err("ta control by was4783 failed(%d)\n", ret);
	}

	return ret;
}

static int tran_usb_ctrl_none_setting_handler(struct usb_info *info, int val)
{
	pr_err("abnormal logic, need check code\n");

	return 0;
}

static int tran_usb_ctrl_hvdcp20_setting_handler(struct usb_info *info, int val)
{
	int ret = 0;

	pr_info("%s\n", hvdcp20_ctrl_type_to_str(info->hvdcp20_control_type));

	switch (info->hvdcp20_control_type) {
	case HVDCP20_CONTROL_BY_SWITCH_IC:
		ret = hvdcp20_control_by_switch_ic_function(info, val);
		if (ret < 0) {
			pr_err("hvdcp control failed(%d)!\n", ret);
		}
		break;
	case HVDCP20_CONTROL_BY_SWITCH_IC_AND_GPIO:
		ret = hvdcp20_control_by_switch_ic_and_gpio_function(info, val);
		if (ret < 0) {
			pr_err("hvdcp control failed(%d)!\n", ret);
		}
		break;
	case HVDCP20_CONTROL_BY_GPIO:
		ret = hvdcp20_control_by_gpio_function(info, val);
		if (ret < 0) {
			pr_err("hvdcp control failed(%d)!\n", ret);
		}
		break;
	case HVDCP20_CONTROL_BY_SWITCH_IC_COUPLER:
		ret = hvdcp20_control_by_switch_ic_coupler_function(info, val);
		if (ret < 0) {
			pr_err("hvdcp control failed(%d)!\n", ret);
		}
		break;
	case HVDCP20_CONTROL_NOT_SUPPORT:
		break;
	default:
		break;
	}

	return ret;
}

static int tran_usb_ctrl_hvdcp30_setting_handler(struct usb_info *info, int val)
{
	int ret = 0;

	pr_info("%s\n", hvdcp30_ctrl_type_to_str(info->hvdcp30_control_type));

	switch (info->hvdcp30_control_type) {
	case HVDCP30_CONTROL_BY_SWITCH_IC:
		ret = hvdcp30_control_by_switch_ic_function(info, val);
		if (ret < 0) {
			pr_err("hvdcp by switch ic control failed(%d)!\n", ret);
		}
		break;
	case HVDCP30_CONTROL_BY_CP_IC:
		ret = hvdcp30_control_by_cp_ic_function(info, val);
		if (ret < 0) {
			pr_err("hvdcp cp ic control failed(%d)!\n", ret);
		}
		break;
	case HVDCP30_CONTROL_NOT_SUPPORT:
		break;
	default:
		break;
	}

	return ret;
}

static int tran_usb_ctrl_tc30_setting_handler(struct usb_info *info, int val)
{
	int ret = 0;

	pr_info("%s\n", tc30_ctrl_type_to_str(info->tc30_control_type));
	switch (info->tc30_control_type) {
	case TC30_CONTROL_BY_SWITCH_IC:
		ret = tc30_control_by_switch_ic_function(info, val);
		if (ret < 0) {
			pr_err("tc30 control by switch ic failed(%d)!\n", ret);
		}
		break;
	case TC30_CONTROL_BY_SWITCH_IC_ONESHOT:
		ret = tc30_control_by_switch_ic_oneshot_function(info, val);
		if (ret < 0) {
			pr_err("tc30 control by switch ic oneshot failed(%d)!\n", ret);
		}
		break;
	case TC30_CONTROL_BY_SWITCH_IC_AND_GPIO:
	case TC30_CONTROL_BY_GPIO:
		ret = tc30_control_by_gpio_function(info, val);
		if (ret < 0) {
			pr_err("tc30 control by gpio failed(%d)!\n", ret);
		}
		break;
	case TC30_CONTROL_BY_GPIO_AND_WAS4783:
		ret = tc30_control_by_gpio_and_was4783_function(info, val);
		if (ret < 0) {
			pr_err("tc30 control by gpio failed(%d)!\n", ret);
		}
		break;
	case TC30_CONTROL_NOT_SUPPORT:
		break;
	default:
		break;
	}

	return ret;
}

static int tran_usb_ctrl_rfc_setting_handler(struct usb_info *info, int val)
{
	int ret = 0;

	pr_info("%s\n", rfc_ctrl_type_to_str(info->rfc_control_type));

	switch (info->rfc_control_type) {
	case RFC_CONTROL_BY_WAS4783_AND_GPIO:
		ret = rfc_control_by_was4783_and_gpio_function(info, val);
		if (ret < 0) {
			pr_err("rfc control by was4783 and gpio failed(%d)!\n", ret);
		}
		break;
	case RFC_CONTROL_BY_CP_IC_AND_GPIO:
		ret = rfc_control_by_cp_ic_and_gpio_function(info, val);
		if (ret != 0) {
			pr_err("rfc control by cp ic and gpio failed(%d)!\n", ret);
		}
		break;
	case RFC_CONTROL_BY_CP_IC:
		ret = rfc_control_by_cp_ic_function(info, val);
		if (ret != 0) {
			pr_err("rfc control by cp ic failed(%d)!\n", ret);
		}
		break;
	case RFC_CONTROL_BY_GPIO:
		ret = rfc_control_by_gpio_function(info, val);
		if (ret < 0) {
			pr_err("rfc control by gpio failed(%d)!\n", ret);
		}
		break;
	case RFC_CONTROL_NOT_SUPPORT:
	default:
		break;
	}

	return ret;
}

static int tran_usb_ctrl_ta_off_setting_handler(struct usb_info *info, int val)
{
	int ret = 0;

	pr_info("%s\n", ta_ctrl_type_to_str(info->ta_control_type));
	switch (info->ta_control_type) {
	case TA_CONTROL_BY_GPIO:
		ret = ta_control_by_gpio_function(info, val);
		if (ret < 0) {
			pr_err("ta control failed!\n");
		}
	    	break;
	case TA_CONTROL_BY_WAS4783:
		ret = ta_control_by_was4783_function(info, val);
		if (ret < 0) {
			pr_err("ta control failed!\n");
		}
		break;
	case TA_CONTROL_NOT_SUPPORT:
	default:
		pr_info("ta_control not support!\n");
	}

	return ret;
}

static const struct tran_usb_ctrl_desc tran_usb_ctrl_tbl[] = {

	TRAN_NOTIFY_DESC(usb_ctrl_none),
	TRAN_NOTIFY_DESC(usb_ctrl_hvdcp20),
	TRAN_NOTIFY_DESC(usb_ctrl_hvdcp30),
	TRAN_NOTIFY_DESC(usb_ctrl_tc30),
	TRAN_NOTIFY_DESC(usb_ctrl_rfc),
	TRAN_NOTIFY_DESC(usb_ctrl_ta_off),
};

static int usb_control_get_property(struct tran_device *dev,
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

static int tran_usb_ctrl(struct usb_info *info, int effector, int val)
{
	int ret;

	mutex_lock(&info->usb_lock);

	if (effector == USB_CTRL_TA_OFF &&
		info->effector != USB_CTRL_NONE &&
		info->effector != USB_CTRL_TA_OFF) {
		pr_info("trigger TA OFF, close other usb control[%s]\n",
			usb_ctrl_name[info->effector]);
		ret = tran_usb_ctrl_tbl[info->effector].hdlr(info, false);
		info->effector = USB_CTRL_NONE;
	}

	if (info->effector == USB_CTRL_NONE && val != 0) {
		pr_info("set usb ctrl[%s]:%d\n", usb_ctrl_name[effector], val);
		info->effector = effector;
		info->type = val;
	} else if (info->effector == effector && val == 0) {
		pr_info("reset usb ctrl[%s]:%d\n", usb_ctrl_name[effector], val);
		info->effector = USB_CTRL_NONE;
		info->type = val;
	} else if (info->effector == effector && info->type != val) {
		pr_info("update usb ctrl[%s]:%d\n", usb_ctrl_name[effector], val);
		info->type = val;
	} else if (info->effector == USB_CTRL_HVDCP30 && effector == USB_CTRL_HVDCP30 && (val == HVDCP30_DP_PULSE || val == HVDCP30_DM_PULSE)) {
		pr_info("loop trigger usb ctrl[%s]:%d\n", usb_ctrl_name[effector], val);
		info->type = val;
	} else {
		pr_info("ignore usb ctrl[%s]:%d\n", usb_ctrl_name[effector], val);
		goto out;
	}

	ret = tran_usb_ctrl_tbl[effector].hdlr(info, val);
out:
	mutex_unlock(&info->usb_lock);

	return ret;
}
static void charger_dev_check_status(struct usb_info *info)
{
	if (IS_ERR_OR_NULL(info->chg1_dev)) {
		info->chg1_dev = get_charger_by_name("primary_chg");
	}

	if (IS_ERR_OR_NULL(info->dvchg1_dev)) {
		info->dvchg1_dev = get_charger_by_name("primary_dvchg");
	}
}

static int usb_control_set_property(struct tran_device *dev,
					   enum tran_common_prop prop,
					   const union com_propval *val)
{
	struct usb_info *info = tran_get_data(dev);
	int ret = 0;

	pr_info("prop=%d, val=%d\n", prop, val->intval);

	charger_dev_check_status(info);

	switch (prop) {
		case TRAN_PROP_USB_CTRL_HVDCP20:
			ret = tran_usb_ctrl(info, USB_CTRL_HVDCP20, val->intval);
			break;
		case TRAN_PROP_USB_CTRL_HVDCP30:
			ret = tran_usb_ctrl(info, USB_CTRL_HVDCP30, val->intval);
			break;
		case TRAN_PROP_USB_CTRL_TC30:
			ret = tran_usb_ctrl(info, USB_CTRL_TC30, val->intval);
			break;
		case TRAN_PROP_USB_CTRL_RFC:
			ret = tran_usb_ctrl(info, USB_CTRL_RFC, val->intval);
			break;
		case TRAN_PROP_USB_CTRL_TA_OFF:
			ret = tran_usb_ctrl(info, USB_CTRL_TA_OFF, val->intval);
			break;
		case TRAN_PROP_USB_CTRL_RESET:
			/* tran_usb_ctrl_reset_setting_handler(info); */
			break;
		case TRAN_PROP_USB_PLUG_IN:
			info->usb_plug_in = true;
			break;
		case TRAN_PROP_USB_PLUG_OUT:
			info->usb_plug_in = false;
			/* tran_usb_ctrl_reset_setting_handler(info); */
			break;
		default:
			ret = -EINVAL;
	}

	return ret;
}

static struct tran_ops usb_control_ops = {
	.get_prop = usb_control_get_property,
	.set_prop = usb_control_set_property,
};

static int usb_init_chg(struct usb_info *info)
{

        info->usb_control_props.alias_name = "usb_control";
	info->ac_ctl_dev = tran_device_register("usb_control",
						info->dev, info,
						&usb_control_ops,
						&info->usb_control_props);
	if (IS_ERR_OR_NULL(info->ac_ctl_dev))
		return -EPROBE_DEFER;

	return 0;
}


static int usb_control_pinctrl_init(struct usb_info *info,
				struct device *dev)
{
	int  i, ret = 0;
	struct {
		const char *name;
		struct pinctrl_state **state;
	} props[] = {
		{"default", &info->pin_default},
		{"hvdcp_handshake", &info->hvdcp_handshake},
		{"hvdcp_boost_5v", &info->hvdcp_boost_5v},
		{"hvdcp_boost_9v", &info->hvdcp_boost_9v},
		{"hvdcp_reset", &info->hvdcp_reset},
		{"tc30_handshake", &info->tc30_handshake},
		{"tc30_reset", &info->tc30_reset},
		{"rfc_i2c_mode", &info->rfc_i2c_mode},
		{"rfc_dual_high", &info->rfc_dual_high},
		{"rfc_high_low", &info->rfc_high_low},
		{"rfc_reset", &info->rfc_reset},
		{"ta_off", &info->ta_off},
		{"ta_reset", &info->ta_reset},
		{"rfc_usb_switch_on", &info->rfc_usb_switch_on},
		{"rfc_usb_switch_off", &info->rfc_usb_switch_off},
	};


	if (IS_ERR_OR_NULL(dev)) {
		pr_err("Cannot find dev pinctrl!\n");
		return -ENODEV;
	}

	info->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR_OR_NULL(info->pinctrl)) {
		pr_err("Cannot find dev pinctrl!\n");
		return -ENODEV;
	}

	for (i = 0; i < ARRAY_SIZE(props); i++) {
		*(props[i].state) =
			pinctrl_lookup_state(info->pinctrl, props[i].name);
		if (IS_ERR_OR_NULL(*(props[i].state)))
			pr_err("Cannot find %s pinctrl\n", props[i].name);
	}

	return ret;
}

static int usb_control_parse_dt(struct usb_info *info,
				struct device *dev)
{
	int ret = 0;
	struct device_node *np = dev->of_node;

	info->dp_gpio = of_get_named_gpio(np, "dp_gpio", 0);
        if (info->dp_gpio < 0) {
		info->dp_gpio = U32_MAX;
		pr_err("dp_gpio = %d get fail\n", info->dp_gpio);
        }

	info->dm_gpio = of_get_named_gpio(np, "dm_gpio", 0);
        if (info->dm_gpio < 0) {
		info->dm_gpio = U32_MAX;
		pr_err("dm_gpio = %d get fail\n", info->dm_gpio);
        }

	ret = of_property_read_u32(np, "tc30_control_type", &info->tc30_control_type);
	if (ret < 0) {
		pr_err("get tc30_control_type fail(%d)\n", info->tc30_control_type);
		return -EINVAL;
        }

	ret = of_property_read_u32(np, "hvdcp20_control_type", &info->hvdcp20_control_type);
	if (ret < 0) {
		pr_err("get hvdcp20_control_type fail(%d)\n", info->hvdcp20_control_type);
		return -EINVAL;
        }
	
	ret = of_property_read_u32(np, "hvdcp30_control_type", &info->hvdcp30_control_type);
	if (ret < 0) {
		pr_err("get hvdcp30_control_type fail(%d)\n", info->hvdcp30_control_type);
        }

	ret = of_property_read_u32(np, "rfc_control_type", &info->rfc_control_type);
	if (ret < 0) {
		pr_err("get rfc_control_type fail(%d)\n", info->rfc_control_type);
		return -EINVAL;
        }

	ret = of_property_read_u32(np, "ta_control_type", &info->ta_control_type);
	if (ret < 0) {
		pr_err("get ta_control_type fail(%d)\n", info->ta_control_type);
		return -EINVAL;
        }

	return ret;
}

static int usb_control_probe(struct platform_device *pdev)
{
	int ret;
	struct usb_info *info;

	pr_info("enter\n");

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);

	mutex_init(&info->usb_lock);
	mutex_init(&info->pinctrl_lock);
	mutex_init(&info->gpio_lock);

	charger_dev_check_status(info);

	ret = usb_control_parse_dt(info, info->dev);
	if(ret < 0) {
		pr_err("usb control parse dts failed, ret = %d", ret);
		goto err_parse_dt;
	}

	ret = usb_control_pinctrl_init(info, info->dev);
	if(ret < 0) {
		pr_err("usb control parse pinctrl failed, ret = %d", ret);
		goto err_parse_pinctrl;
	}

	ret = usb_init_chg(info);
	if (ret < 0) {
		ret = -ENODEV;
		pr_info("usb control device failed\n");
		goto err_register_dev;
	}

	ret = device_create_file(&(pdev->dev), &dev_attr_pinctrl_debug);
	if (ret < 0) {
		pr_err( "create file fail(%d)\n", ret);
	}
	
	pr_info("successfully\n");

	return 0;

err_register_dev:
	tran_device_unregister(info->ac_ctl_dev);
/* err_get_dvchg: */
/* err_get_rfc_dev: */
err_parse_pinctrl:
err_parse_dt:
	mutex_destroy(&info->usb_lock);
	return ret;
}

static int usb_control_remove(struct platform_device *pdev)
{
	return 0;
}

static void usb_control_shutdown(struct platform_device *dev)
{
		
}

static const struct of_device_id usb_control_of_match[] = {
	{.compatible = "transsion, usb_control",},
	{},
};
MODULE_DEVICE_TABLE(of, usb_control_of_match);

static struct platform_driver usb_control_platdrv = {
	.probe = usb_control_probe,
	.remove = usb_control_remove,
	.shutdown = usb_control_shutdown,
	.driver = {
		.name = "usb_control",
		.owner = THIS_MODULE,
		.of_match_table = usb_control_of_match,
	},
};

static int __init usb_control_init(void)
{
	return platform_driver_register(&usb_control_platdrv);
}
device_initcall_sync(usb_control_init);

static void __exit usb_control_exit(void)
{
	platform_driver_unregister(&usb_control_platdrv);
}
module_exit(usb_control_exit);

MODULE_DESCRIPTION("Transsion USB signal Control For charger");
MODULE_AUTHOR("Schack");
MODULE_VERSION("1.0.0_G");
MODULE_LICENSE("GPL");


