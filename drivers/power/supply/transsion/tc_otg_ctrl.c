// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/extcon.h>
#include <linux/alarmtimer.h>
#include <linux/miscdevice.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/power_supply.h>
#include <linux/iio/consumer.h>
#include <linux/version.h>
#include <tc_adapter_class.h>
#include <tc_misc_intf.h>
#include <tc_charger.h>
#include <tc_tcpc.h>
#include "tc_charger_class.h"

/* NORMAL_BOOT: cover mtk_boot_common.h */
#define NORMAL_BOOT 0
#define KERNEL_POWER_OFF_CHARGING_BOOT 8
#define LOW_POWER_OFF_CHARGING_BOOT 9
#define TRAN_OTG_DCP_CTRL_BY_EXTERNAL_DEVICE    1
#define TRAN_OTG_DCP_CTRL_BY_IC 2
#define TRAN_OTG_DCP_CTRL_DEFAULT_TYPE  TRAN_OTG_DCP_CTRL_BY_EXTERNAL_DEVICE

enum {
	TC_OTG_DISABLED = 0,
	TC_OTG_DEFAULT,
	TC_OTG_FILE_TRANS = TC_OTG_DEFAULT,
	TC_OTG_REVERSE_CHG,
	TC_OTG_ABNORMAL,
};

struct tag_bootmode {
	u32 size;
	u32 tag;
	u32 bootmode;
	u32 boottype;
};

struct tc_otg_pinctrl {
	struct pinctrl_state *meta_gpio_low;
	struct pinctrl_state *meta_gpio_high;
	struct pinctrl_state *otg_gpio_low;
	struct pinctrl_state *otg_gpio_high;
	struct pinctrl_state *otg_dcp_low;
	struct pinctrl_state *otg_dcp_high;
	struct pinctrl_state *otg_vchg_en_low;
	struct pinctrl_state *otg_vchg_en_high;
	struct pinctrl_state *otg_ovp1_en_low;
	struct pinctrl_state *otg_ovp1_en_high;
};

struct tc_otg_info {
	struct device *dev;
	struct charger_device *chg_dev;
	struct charger_device *dvchg1_dev;
	struct charger_device *wls_dev;
	struct power_supply *wls_psy;
	struct delayed_work init_dwork;
	struct delayed_work pd_dfp_oc_recovery_work;
	struct work_struct otg_work;
	struct hrtimer otg_timer;
	struct notifier_block pd_nb;
	struct tran_device *tc_charger_dev;
	struct pinctrl *pinctrl;
	struct tc_otg_pinctrl pinctrl_data;
	struct tadapter_device *pd_adapter;
	struct mutex tc_otg_ctl_lock;
	struct tran_device *tc_otg_dev;
	struct tran_properties tc_otg_props;
	struct wakeup_source *tc_otg_ws;
	int tc_otg_enable;
	bool tc_otg_plugin;
	int pd_hard_reset_val;
	bool pd_hard_reset;
	bool pd_reset;
	bool pd_reverse_chg_support;
	bool otg_avoid_hl7139a_step;
	bool support_otg_dcp;
	bool otg_dcp_by_pmic;
	bool support_ext_otg;
	bool inited;
	bool pd_connect;
	atomic_t is_shutdown;
	unsigned int tc_otg_boost_curr_max;
	unsigned int tc_otg_boost_curr_min;
	signed int otg_revercharge_limit_temp;
	unsigned int otg_reverse_chg_curr_lmt;
	signed int otg_reverse_chg_limit_temp_step1;
	signed int otg_reverse_chg_recovery_area;
	unsigned int wdt_polling_interval;
	unsigned int boost_cur; 
	int otg_cur_ctl_val;
	int otg_dcp_ctrl_type;
	bool otg_hight_temp_flag;
	int bootmode;
	int lmt_boost_cur;
	int otg_reverse_chg_curr_lmt_dbg;
};

#define OTG_BOOST_CURRENT_MAX 2000000
#define OTG_BOOST_CURRENT_MIN 500000    /* USB,hight tmep or battery lower than 30% */

#define FTM_OTG_TEST_GPIO_OTG_CTL _IOW('k', 15, int)
#define FTM_OTG_TEST_GPIO_META_CTL _IOW('k', 16, int)

static struct tc_otg_info *g_tc_otg_info = NULL;

void tc_otg_wait_pd_connected(struct tc_otg_info *info)
{
	int wait_cnt_max = 15; /*1.5s max*/
	while (!info->pd_connect && wait_cnt_max--
		&& info->tc_otg_plugin) {
		msleep(100);
	}
	dev_info(info->dev, "%s: wait done,cnt:%d,connect:%d\n", __func__, wait_cnt_max, info->pd_connect);
}

int tc_pd_send_hardreset(struct tc_otg_info *info)
{
	int ret = -1;

	info->pd_adapter = get_tadapter_by_name("pd_adapter");
	if (IS_ERR_OR_NULL(info->pd_adapter))
		return -ENODEV;

	tc_otg_wait_pd_connected(info);
	info->pd_hard_reset = true;
	ret = tadapter_dev_send_hardreset(info->pd_adapter);

	dev_info(info->dev, "%s: tran pd hardrset ret=%d\n",__func__,ret);

	return ret;
}

int tc_pd_vbus_short_change(struct tc_otg_info *info, bool en)
{
	int ret = 0;
	union com_propval set_val = {0};
	char *env[2] = {"VBUSSHORT=1", NULL};
	char *env1[2] = {"VBUSSHORT=0", NULL};

	dev_info(info->dev, "%s\n", __func__);

	if (en) {
		set_val.intval = ARRAY_SIZE(env);
		set_val.ptr = &env[0];
		ret = tran_dev_set_prop(info->tc_charger_dev, TRAN_PROP_SEND_UEVENT, &set_val);
	} else {
		set_val.intval = ARRAY_SIZE(env1);
		set_val.ptr = &env1[0];
		ret =tran_dev_set_prop(info->tc_charger_dev, TRAN_PROP_SEND_UEVENT, &set_val);
	}
	if (ret)
		dev_info(info->dev, "%s: kobject_uevent_fail, ret=%d\n", __func__, ret);

	return ret;
}

static void pd_dfp_oc_recovery_func(struct work_struct *work) 
{
	struct tc_otg_info *info = container_of(work, struct tc_otg_info,
						pd_dfp_oc_recovery_work.work);
	ktime_t ktime = ktime_set(0, 2000 * 1000 * 1000);
	info->tc_otg_enable = TC_OTG_FILE_TRANS;
	tc_pd_vbus_short_change(info, false);
	hrtimer_start(&info->otg_timer, ktime, HRTIMER_MODE_REL);
	pr_info("%s:pd oc recovery, over 5 minute\n",__func__);
	return;
}

static void usb20_extern_otg_ctl(bool enable)
{
	struct tc_otg_info *info = g_tc_otg_info;
	struct pinctrl *pinctrl = info->pinctrl;
	struct tc_otg_pinctrl *pinctrl_data = &g_tc_otg_info->pinctrl_data;

	if (!info->support_ext_otg)
		return;
	if (IS_ERR_OR_NULL(pinctrl_data->otg_vchg_en_high) || IS_ERR_OR_NULL(pinctrl_data->otg_vchg_en_low))
		return;
	if (enable) {
		pinctrl_select_state(pinctrl, pinctrl_data->otg_vchg_en_high);
	} else {
		pinctrl_select_state(pinctrl, pinctrl_data->otg_vchg_en_low);
	}
}

//extern int charger_dev_enable_dcp(struct charger_device *chg_dev, bool en);
static void usb20_otg_dcp(bool en)
{
	struct tc_otg_info *info = g_tc_otg_info;
	struct pinctrl *pinctrl = info->pinctrl;
	struct tc_otg_pinctrl *pinctrl_data = &g_tc_otg_info->pinctrl_data;

	if (!info->support_otg_dcp || info->pd_reverse_chg_support)
		return;

	if (info->otg_dcp_by_pmic) {
        	dev_info(info->dev, "en by pmic %d\n", en);
		//charger_dev_enable_dcp(info->chg_dev, en);
		return;
	}

	if (IS_ERR_OR_NULL(pinctrl_data->otg_dcp_high) ||
			IS_ERR_OR_NULL(pinctrl_data->otg_dcp_low))
		return;

	pinctrl_select_state(pinctrl, en ? pinctrl_data->otg_dcp_high : pinctrl_data->otg_dcp_low);
        dev_info(info->dev, "en by dcp ic %d\n", en);
}

static void usb20_otg_ovp(bool en)
{
	struct tc_otg_info *info = g_tc_otg_info;
	struct pinctrl *pinctrl = info->pinctrl;
	struct tc_otg_pinctrl *pinctrl_data = &g_tc_otg_info->pinctrl_data;

	if (!info->support_otg_dcp)
		return;

	if (IS_ERR_OR_NULL(pinctrl_data->otg_ovp1_en_low) || IS_ERR_OR_NULL(pinctrl_data->otg_ovp1_en_high) ||
		IS_ERR_OR_NULL(pinctrl_data->otg_vchg_en_high) || IS_ERR_OR_NULL(pinctrl_data->otg_vchg_en_low))
		return;

    if (en) {
		pinctrl_select_state(pinctrl, pinctrl_data->otg_vchg_en_high);
		msleep(100);
		pinctrl_select_state(pinctrl, pinctrl_data->otg_ovp1_en_low);
    } else {
		pinctrl_select_state(pinctrl, pinctrl_data->otg_ovp1_en_high);
		pinctrl_select_state(pinctrl, pinctrl_data->otg_vchg_en_low);
    }
    dev_info(info->dev, "usb20_otg_ovp is %d\n", en);
}

static void tc_ftm_otg_ctl(int en)
{
	struct pinctrl *pinctrl = g_tc_otg_info->pinctrl;
	struct tc_otg_pinctrl *pinctrl_data = &g_tc_otg_info->pinctrl_data;

	if ((IS_ERR_OR_NULL(pinctrl_data->otg_gpio_low)) || (IS_ERR_OR_NULL(pinctrl_data->otg_gpio_high)))
		return;

	pinctrl_select_state(pinctrl, en ? pinctrl_data->otg_gpio_high: pinctrl_data->otg_gpio_low);
}

static void tc_ftm_meta_ctl(int en)
{
	struct pinctrl *pinctrl = g_tc_otg_info->pinctrl;
	struct tc_otg_pinctrl *pinctrl_data = &g_tc_otg_info->pinctrl_data;
	if ((IS_ERR_OR_NULL(pinctrl_data->meta_gpio_low)) || (IS_ERR_OR_NULL(pinctrl_data->meta_gpio_high)))
		return;

	pinctrl_select_state(pinctrl, en ? pinctrl_data->meta_gpio_high : pinctrl_data->meta_gpio_low);
}

static void tc_otg_init_work(struct work_struct *work)
{
	struct tc_otg_info *info = container_of(work, struct tc_otg_info,
						init_dwork.work);
	struct device *dev = info->dev;
	static int retry_cnt = 0;

	dev_info(dev, "%s\n", __func__);
	info->inited = false;

	/* get charger ic dev */
	info->chg_dev = get_charger_by_name("primary_chg");
	if (IS_ERR_OR_NULL(info->chg_dev)) {
		dev_err(dev, "get primary_chg dev failed\n");
		goto retry;
	}

	info->dvchg1_dev = get_charger_by_name("primary_dvchg");
	if (IS_ERR_OR_NULL(info->dvchg1_dev)) {
		dev_err(dev, "get primary_dvchg dev failed\n");
	}

	info->tc_charger_dev = tran_get_by_name("tc_charger");
	if (IS_ERR_OR_NULL(info->tc_charger_dev)) {
		dev_err(info->dev, "can't find tc_charger common dev\n");
		goto retry;
	}

	/* get wireless psy */
	info->wls_psy = power_supply_get_by_name("wireless");
	if (IS_ERR_OR_NULL(info->wls_psy)) {
		dev_err(dev, "get wls psy failed\n");
	}

	info->wls_dev = get_charger_by_name("wireless_manager");
	if (IS_ERR_OR_NULL(info->wls_dev)) {
		dev_err(dev, "can't find wireless charger device ***\n");
	}

	info->inited = true;

	dev_info(dev, "get charger psy suc\n");
	return;

retry:
	info->inited = false;
	if (retry_cnt ++ < 10)
		schedule_delayed_work(&info->init_dwork, msecs_to_jiffies(500));
	else
		info->inited = true;
}

static bool tc_check_vbus_short(struct tc_otg_info *info)
{
	int vchg = tc_get_vbus();

	if (!info->tc_otg_enable || !info->tc_otg_plugin
		|| info->pd_hard_reset || !info->otg_cur_ctl_val) {
		return false;
	} else if (vchg < 2500) {
		dev_info(info->dev, "ERROR:otg mode vbus short = %d\n", vchg);
		return true;
	}
	return false;
}

int tc_set_otg_plug_in(bool plug_in)
{
	struct tc_otg_info *info = g_tc_otg_info;
	if (IS_ERR_OR_NULL(info) || IS_ERR_OR_NULL(info->chg_dev)) {
		pr_err("%s:get info or chg_dev fail\n", __func__);
		return -EINVAL;
	}

	if (!plug_in) {
		usb20_otg_dcp(plug_in);
		usb20_otg_ovp(plug_in);
		if (info->pd_reverse_chg_support) {
			cancel_delayed_work(&info->pd_dfp_oc_recovery_work);
			if (info->tc_otg_enable == TC_OTG_ABNORMAL) {
				info->tc_otg_enable = TC_OTG_FILE_TRANS;
				tc_pd_vbus_short_change(info, false);
			}
		}
		if(info->tc_otg_plugin) {
			hrtimer_cancel(&info->otg_timer);
			charger_dev_enable_otg(info->chg_dev, false);
		}
	} else {
		ktime_t ktime = ktime_set(0, 2000 * 1000 * 1000);
		hrtimer_start(&info->otg_timer, ktime, HRTIMER_MODE_REL);
	}
	info->tc_otg_plugin = plug_in;
	return 0;
}
EXPORT_SYMBOL_GPL(tc_set_otg_plug_in);

static void tc_otg_pinctrl_init(struct tc_otg_info *info)
{
	struct device *dev = info->dev;
	struct pinctrl *pinctrl = info->pinctrl;
	struct tc_otg_pinctrl *pinctrl_data = &info->pinctrl_data;

	/* init meta gpio */
	pinctrl_data->meta_gpio_low = pinctrl_lookup_state(pinctrl, "meta_gpio_low");
	if (IS_ERR(pinctrl_data->meta_gpio_low))
		dev_err(dev, "Cannot find meta_gpio_low\n");

	pinctrl_data->meta_gpio_high = pinctrl_lookup_state(pinctrl, "meta_gpio_high");
	if (IS_ERR(pinctrl_data->meta_gpio_high))
		dev_err(dev, "Cannot find meta_gpio_high\n");

	/* init otg ata gpio */
	pinctrl_data->otg_gpio_low = pinctrl_lookup_state(pinctrl, "otg_gpio_low");
	if (IS_ERR(pinctrl_data->otg_gpio_low))
		dev_err(dev, "Cannot find otg_gpio_low\n");

	pinctrl_data->otg_gpio_high = pinctrl_lookup_state(pinctrl, "otg_gpio_high");
	if (IS_ERR(pinctrl_data->otg_gpio_high))
		dev_err(dev, "Cannot find otg_gpio_high\n");

	/* init otg dcdc gpio */
	pinctrl_data->otg_vchg_en_low = pinctrl_lookup_state(pinctrl, "drvvbus_low");
	if (IS_ERR(pinctrl_data->otg_vchg_en_low))
		dev_err(dev, "Cannot find pinctrl drvvbus_low\n");

	pinctrl_data->otg_vchg_en_high = pinctrl_lookup_state(pinctrl, "drvvbus_high");
	if (IS_ERR(pinctrl_data->otg_vchg_en_high))
		dev_err(dev, "Cannot find pinctrl drvvbus_high\n");

	/* init otg dcp gpio */
	pinctrl_data->otg_dcp_low = pinctrl_lookup_state(pinctrl, "otg_dcp_low");
    if (IS_ERR(pinctrl_data->otg_dcp_low))
		dev_err(dev, "Cannot find pinctrl otg_dcp_low\n");

    pinctrl_data->otg_dcp_high = pinctrl_lookup_state(pinctrl, "otg_dcp_high");
    if (IS_ERR(pinctrl_data->otg_dcp_high))
		dev_err(dev, "Cannot find pinctrl otg_dcp_hige\n");

	/* init otg ovp gpio */
  	pinctrl_data->otg_ovp1_en_low = pinctrl_lookup_state(pinctrl, "otg_ovp_low");
  	if (IS_ERR(pinctrl_data->otg_ovp1_en_low))
        dev_err(dev, "Cannot find pinctrl otg_ovp_low\n");

  	pinctrl_data->otg_ovp1_en_high = pinctrl_lookup_state(pinctrl, "otg_ovp_high");
  	if (IS_ERR(pinctrl_data->otg_ovp1_en_high))
                dev_err(dev, "Cannot find pinctrl otg_ovp_high\n");
}

static int tc_otg_dts_parse(struct tc_otg_info *info)
{
	struct device_node *np = info->dev->of_node;
	struct device *dev = info->dev;

	info->support_otg_dcp = of_property_read_bool(np, "support_otg_dcp");
	dev_info(dev, "dts parse support_otg_dcp = %d\n", info->support_otg_dcp);

	info->otg_dcp_by_pmic = of_property_read_bool(np, "otg_dcp_by_pmic");
	dev_info(dev, "dts parse otg_dcp_by_pmic = %d\n", info->otg_dcp_by_pmic);
	if (of_property_read_u32(np, "otg_dcp_ctrl_type", &info->otg_dcp_ctrl_type) < 0)
		info->otg_dcp_ctrl_type = TRAN_OTG_DCP_CTRL_DEFAULT_TYPE;
	if (TRAN_OTG_DCP_CTRL_BY_IC == info->otg_dcp_ctrl_type)
		info->otg_dcp_by_pmic = 1;

	info->pd_reverse_chg_support = of_property_read_bool(np, "pd_reverse_chg_support");
	info->otg_avoid_hl7139a_step = of_property_read_bool(np, "otg_avoid_hl7139a_step");

	info->support_ext_otg = of_property_read_bool(np, "support_ext_otg");
	dev_info(dev, "dts parse support_ext_otg = %d\n", info->support_ext_otg);

	if (of_property_read_s32(np, "otg_revercharge_limit_temp",
			&info->otg_revercharge_limit_temp) < 0)
		info->otg_revercharge_limit_temp = 45;
	dev_info(dev, "dts parse otg_revercharge_limit_temp = %d\n", info->otg_revercharge_limit_temp);
	if (of_property_read_u32(np, "tc_otg_boost_curr_max", &info->tc_otg_boost_curr_max) < 0)
		info->tc_otg_boost_curr_max = OTG_BOOST_CURRENT_MAX;
	if (of_property_read_u32(np, "tc_otg_boost_curr_min", &info->tc_otg_boost_curr_min) < 0)
		info->tc_otg_boost_curr_min = OTG_BOOST_CURRENT_MIN;
	if (of_property_read_u32(np, "otg_reverse_chg_curr_lmt", &info->otg_reverse_chg_curr_lmt) < 0)
		info->otg_reverse_chg_curr_lmt = 0;
	if (of_property_read_s32(np, "otg_reverse_chg_limit_temp_step1", &info->otg_reverse_chg_limit_temp_step1) < 0)
		info->otg_reverse_chg_limit_temp_step1 = 42;
	if (of_property_read_s32(np, "otg_reverse_chg_recovery_area", &info->otg_reverse_chg_recovery_area) < 0)
		info->otg_reverse_chg_recovery_area = 3;

	if (of_property_read_u32(dev->of_node, "boost_period",
			&info->wdt_polling_interval))
		info->wdt_polling_interval = 20;

	dev_info(dev, "dts parse boost_period = %d %d\n", info->wdt_polling_interval,info->otg_avoid_hl7139a_step);

	return 0;
}

static int dvchg_enable_otg(struct tc_otg_info *info, int en)
{
	if(IS_ERR_OR_NULL(info->dvchg1_dev)){
		info->dvchg1_dev = get_charger_by_name("primary_dvchg");
		if (IS_ERR_OR_NULL(info->dvchg1_dev)){
			pr_err("%s:primary_dvchg  enable otg failed\n",__func__);
			return -EINVAL;
		}
	}

	return charger_dev_enable_otg(info->dvchg1_dev, en);
}

#if IS_ENABLED(CONFIG_TC_WIRELESS_CHARGER)
static bool tc_is_wireless_working(struct tc_otg_info *info)
{
	union power_supply_propval online_val = {.intval = 0};
	bool boost_state = false;
	int ret = 0;

	if (IS_ERR_OR_NULL(info->wls_psy)) {
		info->wls_psy = power_supply_get_by_name("wireless");
		if (IS_ERR_OR_NULL(info->wls_psy)) {
			dev_err(info->dev, "get wls psy failed\n");
			return false;
		}
	}

	ret = power_supply_get_property(info->wls_psy, POWER_SUPPLY_PROP_ONLINE, &online_val);
	if (ret) {
		dev_err(info->dev, "get wireless online status fail\n");
		return false;
	}
	if (online_val.intval) {
		dev_info(info->dev, "wireless charging,ignore otg vbus operation.\n");
		return true;
	}

	if (IS_ERR_OR_NULL(info->wls_dev)) {
		info->wls_dev = get_charger_by_name("wireless_manager");
		if (IS_ERR_OR_NULL(info->wls_dev)) {
			dev_err(info->dev, "can't find wireless charger device ***\n");
			return false;
		}
	}

	/*Get wirless reverse charge status*/
	ret = wireless_manager_reverse_state(info->wls_dev, &boost_state);
	if (ret) {
		dev_err(info->dev, "get wireless boost status fail\n");
		return false;
	}
	ret = boost_state;
	return ret;
}
#endif

bool tc_otg_wait_init_done(struct tc_otg_info *info)
{
	int max_wait_cnt = 15;
	while(!info->inited && max_wait_cnt--
		&& !atomic_read(&info->is_shutdown)) {
		msleep(200);
	}
	return info->inited;
}

static int tc_otg_set_vbus(int en)
{
	struct tc_otg_info *info = g_tc_otg_info;
	struct charger_device *chg_dev; 
	int bat_tmp = 0;

	if (IS_ERR_OR_NULL(info))
		return -EINVAL;

#if IS_ENABLED(CONFIG_TC_WIRELESS_CHARGER)
	if (tc_is_wireless_working(info))
		return 0;
#endif

	if(info->bootmode == KERNEL_POWER_OFF_CHARGING_BOOT || info->bootmode == LOW_POWER_OFF_CHARGING_BOOT)
		return 0;

	if (info->bootmode != NORMAL_BOOT && info->tc_otg_plugin && !en)
		return 0;

	if (!tc_otg_wait_init_done(info)) {
		dev_err(info->dev, "%s:init not finished!\n", __func__);
		return -EINVAL;
	}

	dev_info(info->dev, "%s:tc_otg_enable %d, en = %d, plug = %d, pd_hard_reset_val = %d\n",
			__func__, info->tc_otg_enable, en, info->tc_otg_plugin, info->pd_hard_reset_val);

	if (en > 0 && info->pd_hard_reset_val > 0)
		en = info->pd_hard_reset_val;

	chg_dev = info->chg_dev;

	mutex_lock(&info->tc_otg_ctl_lock);
	if (en == TC_OTG_DISABLED) {
		if (info->pd_hard_reset) {
			dev_info(info->dev, "%s pd_hard_reset close vbus\n", __func__);
		}
		//c2c occurred hardreset
		if (info->tc_otg_enable != TC_OTG_REVERSE_CHG || !info->tc_otg_plugin) {
			usb20_otg_dcp(false);
			usb20_otg_ovp(false);
		}
		usb20_extern_otg_ctl(false);
		dvchg_enable_otg(info, false);
		charger_dev_set_pfm_mode(chg_dev, false);
		charger_dev_enable_otg(chg_dev ,false);
		goto out;
	}

	info->boost_cur = (info->lmt_boost_cur == 0) ? info->tc_otg_boost_curr_max : info->lmt_boost_cur;

	bat_tmp = tc_get_battery_temperature();
	if (bat_tmp >= info->otg_revercharge_limit_temp) {
		if (info->tc_otg_enable == TC_OTG_REVERSE_CHG)
			en = TC_OTG_FILE_TRANS;
		dev_info(info->dev, "temp over 45, limit 500mA\n");
		info->boost_cur = info->tc_otg_boost_curr_min;
	}

	charger_dev_set_boost_current_limit(chg_dev, info->boost_cur);
	charger_dev_set_boost_voltage(chg_dev, 5300000);

	if (info->pd_reverse_chg_support) {
		if(en == TC_OTG_REVERSE_CHG)
			charger_dev_set_pfm_mode(chg_dev ,true);

		/*Avoid HL7139a+wireless mode vbus step when plug in Sandisk disk can not work*/
		dev_info(info->dev, "%s: %d %d\n",__func__,info->otg_avoid_hl7139a_step,info->pd_reset);
		if (info->otg_avoid_hl7139a_step){
			if(en != info->otg_cur_ctl_val) {
				if (!info->pd_reset)
					charger_dev_disable_otg_step(info->dvchg1_dev, true);

				dvchg_enable_otg(info, true);
				charger_dev_enable_otg(chg_dev, true);

				if (!info->pd_reset)
					charger_dev_disable_otg_step(info->dvchg1_dev, false);

			}
		}else{
			dvchg_enable_otg(info, true);
			charger_dev_enable_otg(chg_dev, true);
		}

		if (info->pd_hard_reset) {
			msleep(100);
			info->pd_hard_reset = false;
		}
	} else {
		if (en == TC_OTG_FILE_TRANS) {
			//c2c occurred hardreset,skip dcp&ovp operation in reverse chg mode
			if (info->tc_otg_enable != TC_OTG_REVERSE_CHG) {
				usb20_otg_dcp(false);
				usb20_otg_ovp(false);
			} else if(bat_tmp < info->otg_revercharge_limit_temp) {
				//retest reverse chg in midtest
				usb20_otg_dcp(true);
				usb20_otg_ovp(true);
			}
			usb20_extern_otg_ctl(true);
			dvchg_enable_otg(info, true);
			charger_dev_enable_otg(chg_dev, true);
		} else if (en == TC_OTG_REVERSE_CHG && info->tc_otg_plugin) {
			info->lmt_boost_cur = 0;
			//add otg close action,make slave device detect bc12
			charger_dev_enable_otg(chg_dev ,false);
			usb20_otg_dcp(true);
			charger_dev_set_pfm_mode(chg_dev ,true);
			dvchg_enable_otg(info, false);
			msleep(200);
			charger_dev_enable_otg(chg_dev ,true);
			usb20_otg_ovp(true);
		}
	}
out:
	info->otg_cur_ctl_val = en;
	mutex_unlock(&info->tc_otg_ctl_lock);
	return 0;
}

static int tc_otg_pd_tcp_notifier_call(struct notifier_block *nb,
				unsigned long event, void *data)
{
	struct tc_tcpc_noti *noti = data;
	struct tc_otg_info *info = (struct tc_otg_info *)container_of(nb,
		struct tc_otg_info, pd_nb);
	int vbus_on = TC_OTG_DISABLED;

	dev_err(info->dev, "%s event:%lu\n", __func__, event);
	switch (event) {
	case TC_TYPEC_SRC_VBUS:
		vbus_on = (noti->vbus_state.mv) ? TC_OTG_DEFAULT : TC_OTG_DISABLED;
		tc_otg_set_vbus(vbus_on);
		break;
	case TC_TYPEC_OTG_PLUG_IN:
		tc_set_otg_plug_in(true);
		if (info->tc_otg_enable == TC_OTG_DISABLED)
			tc_otg_set_vbus(TC_OTG_DISABLED);
		__pm_stay_awake(info->tc_otg_ws);
		break;
	case TC_PD_SRC_TO_SNK:
		tc_set_otg_plug_in(false);
		__pm_relax(info->tc_otg_ws);
		break;
	case TC_PD_SNK_TO_SRC:
		tc_set_otg_plug_in(true);
		__pm_stay_awake(info->tc_otg_ws);
		break;
	case TC_TYPEC_OTG_PLUG_OUT:
		tc_set_otg_plug_in(false);
		__pm_relax(info->tc_otg_ws);
		break;
	case TC_PD_TYPE:
		if (noti->pd_type == TC_PD_CONNECT_NONE) {
			info->pd_connect = false;
		} else if (noti->pd_type == TC_PD_CONNECT_PE_READY_SNK_PD30 ||
			noti->pd_type == TC_PD_CONNECT_PE_READY_SRC_PD30) {
			info->pd_connect = true;
		}
		info->pd_reset = false;
		break;
	case TC_TYPEC_USB_PLUG_OUT:
		tc_set_otg_plug_in(false);
		__pm_relax(info->tc_otg_ws);
		break;
	case TC_PD_CONNECT_HARD_RESET:
		info->pd_reset = true;
		break;
	default:
		break;
	};
	return NOTIFY_OK;
}

static void tc_otg_work_func(struct work_struct *work)
{
	struct tc_otg_info *info = container_of(work, struct tc_otg_info, otg_work);
	ktime_t ktime = ktime_set(0, 2000 * 1000 * 1000);
	int bat_temp = 25;
	static int error_cnt = 0;
	bool is_vbus_short = false;
	__maybe_unused int error_cnt_max = 2;

	/* feed wdt */
	charger_dev_kick_wdt(info->chg_dev);

	/* vbus short check */
	mutex_lock(&info->tc_otg_ctl_lock);
	is_vbus_short = tc_check_vbus_short(info);
	mutex_unlock(&info->tc_otg_ctl_lock);
	if (is_vbus_short) {
		if (info->pd_reverse_chg_support) {
			if (info->tc_otg_plugin) {
				info->tc_otg_enable = TC_OTG_ABNORMAL;
				tc_pd_vbus_short_change(info, true);
					cancel_delayed_work(&info->pd_dfp_oc_recovery_work);
					INIT_DELAYED_WORK(&info->pd_dfp_oc_recovery_work,pd_dfp_oc_recovery_func);
					queue_delayed_work(system_unbound_wq,&info->pd_dfp_oc_recovery_work,HZ*60*5);
			}
		} else {
			error_cnt++;
			if (error_cnt >= error_cnt_max) {
				tc_otg_set_vbus(TC_OTG_DISABLED);
			} else {
				hrtimer_start(&info->otg_timer, ktime, HRTIMER_MODE_REL);
			}
		}
		return;
	} else {
		error_cnt = 0;
	}
	/* temp check
	 * temp over 45 switch to file transfer
	*/
	if (info->tc_otg_enable == TC_OTG_REVERSE_CHG) {
		bat_temp = tc_get_battery_temperature();
		if (bat_temp >= info->otg_revercharge_limit_temp && !info->otg_hight_temp_flag) {
			dev_info(info->dev, "temp over 45, close 10w/6w boost charge\n");
			info->otg_hight_temp_flag = true;
			if (info->pd_reverse_chg_support) {
				tc_pd_send_hardreset(info);
			} else {
				tc_otg_set_vbus(TC_OTG_DISABLED);
				msleep(200);
				tc_otg_set_vbus(TC_OTG_FILE_TRANS);
			}
		} /* temp over 42, otg boost power charge limit ctl */
		else if (bat_temp >= info->otg_reverse_chg_limit_temp_step1 && (info->lmt_boost_cur == 0)) {
			if (info->otg_reverse_chg_curr_lmt_dbg > 0) {
				info->lmt_boost_cur = info->tc_otg_boost_curr_max - info->otg_reverse_chg_curr_lmt_dbg;
			} else {
				info->lmt_boost_cur = info->tc_otg_boost_curr_max - info->otg_reverse_chg_curr_lmt; //decrease boost current when tbat over 42
			}
			if (info->lmt_boost_cur < info->tc_otg_boost_curr_min) {
				info->lmt_boost_cur = info->tc_otg_boost_curr_min;
			}
			charger_dev_set_boost_current_limit(info->chg_dev, info->lmt_boost_cur);
		/* temp -- */
		} else if ((bat_temp <= (info->otg_reverse_chg_limit_temp_step1 - info->otg_reverse_chg_recovery_area))
				&& info->lmt_boost_cur > 0) {
			info->lmt_boost_cur = 0;
			charger_dev_set_boost_current_limit(info->chg_dev, info->tc_otg_boost_curr_max);
		} else if (info->otg_hight_temp_flag && (bat_temp < info->otg_revercharge_limit_temp - info->otg_reverse_chg_recovery_area)) {
			dev_info(info->dev, "temp recovery to nomarl\n");
			info->otg_hight_temp_flag = false;
 			if (info->pd_reverse_chg_support) {
				tc_pd_send_hardreset(info);
 			}else {
				tc_otg_set_vbus(TC_OTG_DISABLED);
				msleep(100);
				tc_otg_set_vbus(TC_OTG_REVERSE_CHG);
			}
		}
	}
	hrtimer_start(&info->otg_timer, ktime, HRTIMER_MODE_REL);
}

static enum hrtimer_restart tc_otg_timer_callback(struct hrtimer *timer)
{
	struct tc_otg_info *info = container_of(timer, struct tc_otg_info, otg_timer);
	schedule_work(&info->otg_work);
	return HRTIMER_NORESTART;
}

static long tc_otg_ftm_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	int en[2];
	void __user *user_data = (void __user *)arg;
	int ret = -1;

	switch (cmd) {
	case FTM_OTG_TEST_GPIO_OTG_CTL:
		ret = copy_from_user(en, user_data, 4);
		printk("[%s] FTM_OTG_TEST_GPIO_OTG_CTL: %d,ret =%d\n", __func__, en[0], ret);
		tc_ftm_otg_ctl(en[0]);
		break;
	case FTM_OTG_TEST_GPIO_META_CTL:
		ret = copy_from_user(en, user_data, 4);
		printk("[%s] FTM_OTG_TEST_GPIO_META_CTL: %d,ret =%d\n", __func__, en[0], ret);
		tc_ftm_meta_ctl(en[1]);
		break;
	default:
		printk("[%s] Error ID\n", __func__);
		break;
	}

	return ret;
}

static int tc_otg_ftm_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int tc_otg_ftm_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations tc_otg_ftm_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = tc_otg_ftm_ioctl,
	.open = tc_otg_ftm_open,
	.release = tc_otg_ftm_release,
};

static struct miscdevice tc_otg_miscdev = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "otg_ftm",
	.fops		= &tc_otg_ftm_fops
};

void tc_otg_ftm_init(struct tc_otg_info *info)
{
	int ret = 0;
	ret = misc_register(&tc_otg_miscdev);
}

static int tc_update_otg_val(struct tc_otg_info *info, int val)
{
	int ret = 0;
	struct device *dev = NULL;

	if (IS_ERR_OR_NULL(info)) {
		pr_err("otg info is NULL");
		return -ENODEV;
	}
	dev = info->dev;

	if (val > TC_OTG_REVERSE_CHG) {
		dev_err(dev, "%s:invalid otg ctrl val = %d\n", __func__, val);
		return ret;
	}

	/* write the same val */
	if (info->tc_otg_enable == val && info->tc_otg_plugin)
		return ret;

	/* if write val > 0 but otg not plug in when sys boost,only update val and return */
	if (val > 0 && !info->tc_otg_plugin) {
		info->tc_otg_enable = val;
		return ret;
	}

	if (info->pd_reverse_chg_support) {
		if (val == 0) {
			dev_info(dev, "[%s]10W pd case,<%d>skip\n",__func__, val);
			return ret;
		} else if (info->tc_otg_enable != val && info->tc_otg_plugin) {
			dev_info(dev, "%s:hardreset,switch to %s mode!\n", __func__, val == TC_OTG_REVERSE_CHG ? "c2c chg" : "file trans");
			info->pd_hard_reset_val = val;
			info->tc_otg_enable = val;
			tc_pd_send_hardreset(info);
			return ret;
		}
	} else {
		if (info->tc_otg_enable == TC_OTG_REVERSE_CHG && val == TC_OTG_FILE_TRANS) {
			dev_info(dev, "%s:otg switch to file trans mode!\n",__func__);
			tc_otg_set_vbus(TC_OTG_DISABLED);
			msleep(100);
		}
		info->tc_otg_enable = val;
		dev_info(dev, "%s val = %d, plug=%d\n", __func__, val, info->tc_otg_plugin);
		tc_otg_set_vbus(val);
	}

	return ret;
}

static int tc_otg_get_property(struct tran_device *dev,
				enum tran_common_prop prop,
				union com_propval *val)
{
	int ret = 0;
	struct tc_otg_info *info = tran_get_data(dev);
	switch (prop) {
		case TRAN_PROP_OTG_CTL_TYPE:
			val->intval = info->tc_otg_enable;
			break;
		case TRAN_PROP_OTG_PROTECT_FLAG:
			val->intval = info->otg_hight_temp_flag;
			break;
		default:
			ret = -EINVAL;
	}

	return ret;
}

static int tc_otg_set_property(struct tran_device *dev,
					   enum tran_common_prop prop,
					   const union com_propval *val)
{
	int ret = 0;
	struct tc_otg_info *info = tran_get_data(dev);
	switch (prop) {
		case TRAN_PROP_OTG_CTL_TYPE:
			ret = tc_update_otg_val(info, val->intval);
			break;
		default:
			ret = -EINVAL;
	}

	return ret;
}

static struct tran_ops tc_otg_ops = {
	.get_prop = tc_otg_get_property,
	.set_prop = tc_otg_set_property,
};

static int tc_otg_prop_init(struct tc_otg_info *info)
{
	info->tc_otg_props.alias_name = "tc_otg";
	info->tc_otg_dev = tran_device_register("tc_otg",
						info->dev, info,
						&tc_otg_ops,
						&info->tc_otg_props);
	if (IS_ERR_OR_NULL(info->tc_otg_dev))
		return -ENODEV;

	return 0;
}

static int tc_otg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tc_otg_info *info = NULL;
	int ret;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = dev;
	platform_set_drvdata(pdev, info);
	dev_set_drvdata(dev, info);

	g_tc_otg_info = info;

	info->bootmode = tc_get_boot_mode();
	tc_otg_dts_parse(info);

	info->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR_OR_NULL(info->pinctrl)) {
		dev_err(dev, "no pinctrl dev find,maybe no need!\n");
	} else {
		tc_otg_pinctrl_init(info);
	}

	if (info->bootmode != NORMAL_BOOT) {
		dev_err(info->dev, "otg ftm init\n");
		info->tc_otg_enable = TC_OTG_FILE_TRANS;
		tc_otg_ftm_init(info);
	}

	info->tc_otg_ws = wakeup_source_register(info->dev,"tc_otg ws");
	if (IS_ERR_OR_NULL(info->tc_otg_ws)){
		dev_err(dev, "failed to register tc_otg wakeup source!\n");
		return -EINVAL;
	}

	info->pd_nb.notifier_call = tc_otg_pd_tcp_notifier_call;
	ret = register_tc_tcpc_notifier(&info->pd_nb);

	mutex_init(&info->tc_otg_ctl_lock);
	INIT_DELAYED_WORK(&info->init_dwork, tc_otg_init_work);
	schedule_delayed_work(&info->init_dwork, 0);

	hrtimer_init(&info->otg_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	info->otg_timer.function = tc_otg_timer_callback;
	INIT_WORK(&info->otg_work, tc_otg_work_func);
	if (info->pd_reverse_chg_support) {
		info->tc_otg_enable = TC_OTG_FILE_TRANS;
		INIT_DELAYED_WORK(&info->pd_dfp_oc_recovery_work, pd_dfp_oc_recovery_func);
	}
	ret = tc_otg_prop_init(info);
	if (ret < 0) {
		dev_err(info->dev, "tc otg prop init failed!\n");
		goto err_of_prop_init;
	}
	dev_info(dev, "%s done(%d)\n", __func__, ret);

	return ret;

err_of_prop_init:
	g_tc_otg_info = NULL;
	dev_info(info->dev, "%s faile!\n", __func__);
	devm_kfree(&pdev->dev, info);
	return -ENODEV;
}

static const struct of_device_id tc_otg_of_match[] = {
	{ .compatible = "tran,otg_fun", },
	{},
};
MODULE_DEVICE_TABLE(of, tc_otg_of_match);

static void tc_otg_shutdown(struct platform_device *pdev)
{
	struct tc_otg_info *info = platform_get_drvdata(pdev);

	cancel_delayed_work(&info->init_dwork);

	atomic_set(&info->is_shutdown, true);

}

static int tc_otg_remove(struct platform_device *pdev)
{
	struct tc_otg_info *info = platform_get_drvdata(pdev);

	if(info->tc_otg_ws)
		wakeup_source_unregister(info->tc_otg_ws);

	return 0;
}



static struct platform_driver tc_otg_driver = {
	.probe = tc_otg_probe,
	.driver = {
		.name = "tc_otg",
		.of_match_table = tc_otg_of_match,
	},
	.shutdown = tc_otg_shutdown,
	.remove = tc_otg_remove,
};

struct platform_device tc_otg_device = {
	.name = "tc_otg",
	.id = -1,
};

static int __init tc_otg_init(void)
{
	return platform_driver_register(&tc_otg_driver);
}

static void __exit tc_otg_exit(void)
{
	platform_driver_unregister(&tc_otg_driver);
}

late_initcall(tc_otg_init);
module_exit(tc_otg_exit);

MODULE_AUTHOR("Mike Lockwood <lockwood@android.com>");
MODULE_DESCRIPTION("tran otg driver");
MODULE_LICENSE("GPL v2");
