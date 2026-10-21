// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#define pr_fmt(fmt)     "[misc_intf] %s: " fmt, __func__
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
#include <linux/thermal.h>
#include <linux/iio/consumer.h>
#include "tc_charger.h"
#include "tc_misc_intf.h"

#include "tc_common_class.h"
#include "tc_charger_class.h"
#include "tc_adapter_class.h"

struct misc_info {
	struct platform_device *pdev;
	struct device *dev;
	struct tran_device *misc_dev;
	struct tran_device *gauge_dev;
	struct tran_device *pmic_dev;
	struct tran_device *tc_chg_dev;
	struct tran_properties misc_props;
	struct power_supply *bat_psy;
	struct power_supply *pmic_psy;
	struct charger_device *chg1_dev;
	struct charger_device *dvchg1_dev;
	struct iio_channel *tusb_channel;
	struct iio_channel *port_channel;
	int bootmode;
	int boottype;
	int tusb_div_res;
	int tusb_div_vol;
	int tbat_dbg;
	int tpcb_dbg;
	int tpa_4g_dbg;
	int tpa_5g_dbg;
	int uisoc_dbg;
	int min_tusb_temp;
	int max_tusb_temp;
	bool det_vbus_by_dvchg;
	bool det_ibus_by_swchg;
	bool det_tusb_by_swchg;
};

struct tag_bootmode {
	u32 size;
	u32 tag;
	u32 bootmode;
	u32 boottype;
};

enum tc_zone {
	ZONE_PCB = 0,
	ZONE_PA_4G,
	ZONE_PA_5G,
};

static const char * const tc_zone_name[] = {
	[ZONE_PA_4G]             = "ltepa_ntc",
	[ZONE_PA_5G]             = "nrpa_ntc",
	[ZONE_PCB]               = "ap_ntc",
};

enum {
	MISC_TBAT = 0,
	MISC_TPCB,
	MISC_TPA_4G,
	MISC_TPA_5G,
	MISC_UISOC,
	MISC_MAX,
};

static ssize_t charger_debug_set_prop(struct device* dev,
	struct device_attribute *attr, const char* buf, size_t len)
{
	int databuf[3];
	struct misc_info *info = dev_get_drvdata(dev);

	if (buf == NULL || len == 0)
		return len;

    	sscanf(buf, "%d %d %d", &databuf[0], &databuf[1], &databuf[2]);
	switch (databuf[0]) {
	case MISC_TBAT:
		info->tbat_dbg = databuf[1];
		break;
	case MISC_TPCB:
		info->tpcb_dbg = databuf[1];
		break;
	case MISC_TPA_4G:
		info->tpa_4g_dbg = databuf[1];
		break;
	case MISC_TPA_5G:
		info->tpa_5g_dbg = databuf[1];
		break;
	case MISC_UISOC:
		info->uisoc_dbg = databuf[1];
		break;
	default:
		pr_err("error input\n");
	
	}

	return len;
}

static ssize_t charger_debug_get_prop(struct device* dev,
	struct device_attribute *attr, char* buf)
{
	return sprintf(buf, "not support\n");
}

static DEVICE_ATTR(charger_debug, 0660,
	charger_debug_get_prop, charger_debug_set_prop);

static struct attribute* misc_sysfs_attrs[] = {
	&dev_attr_charger_debug.attr,
	NULL,
};

static const struct attribute_group misc_sysfs_group = {
	.attrs = misc_sysfs_attrs,
};
static struct misc_info *tc_get_misc_info(void)
{
	struct tran_device *dev = NULL;
	struct misc_info *info = NULL;

	dev = tran_get_by_name("tc_misc");
	if (IS_ERR_OR_NULL(dev)) {
		pr_err("get tc_misc dev fail\n");
		return NULL;
	}

	info = tran_get_data(dev);
	if (IS_ERR_OR_NULL(info)) {
		pr_err("get tc_misc info fail\n");
		return NULL;
	}

	return info;
}

int tc_thermal_zone_get_temp(enum tc_zone zone)
{
        struct thermal_zone_device *zone_dev;
        int ret, temp = 0;

        zone_dev = thermal_zone_get_zone_by_name(tc_zone_name[zone]);
        if (IS_ERR_OR_NULL(zone_dev)) {
	        pr_err("[%s] %s zone_dev get fail\n",
	                        __func__, tc_zone_name[zone]);
                return ERROR_NTC_TEMP;
	}
        ret = thermal_zone_get_temp(zone_dev, &temp);
        if (ret != 0) {
	        pr_err("[%s] %s NTC temp get temp fail(%d)\n",
	                        __func__, tc_zone_name[zone], ret);
                return ERROR_NTC_TEMP;
	}
	pr_err("[%s] %s NTC temp =%d\n",
	                __func__, tc_zone_name[zone], temp);
        return temp;
}

#ifdef MODULE
#define COMMAND_LINE_SIZE	2048
static char __chg_cmdline[COMMAND_LINE_SIZE];
static char *chg_cmdline = __chg_cmdline;

const char *chg_get_cmd(void)
{
	struct device_node * of_chosen = NULL;
	char *bootargs = NULL;

	if (__chg_cmdline[0] != 0)
		return chg_cmdline;

	of_chosen = of_find_node_by_path("/chosen");
	if (of_chosen) {
		bootargs = (char *)of_get_property(
					of_chosen, "bootargs", NULL);
		if (!bootargs)
			pr_err("%s: failed to get bootargs\n", __func__);
		else {
			strncpy(__chg_cmdline, bootargs, 100);
			pr_err("%s: bootargs: %s\n", __func__, bootargs);
		}
	} else
		pr_err("%s: failed to get /chosen \n", __func__);

	return chg_cmdline;
}
#else
const char *chg_get_cmd(void)
{
	return saved_command_line;
}
#endif

extern bool mt_boot_finish(void);
bool tc_get_boot_finish(void)
{
	return mt_boot_finish();
}
EXPORT_SYMBOL(tc_get_boot_finish);

bool tc_get_atm_mode(void)
{
	char atm_str[64] = {0};
	char *ptr = NULL, *ptr_e = NULL;
	char keyword[] = "androidboot.atm=";
	int size = 0;
	bool atm_enabled = false;

	ptr = strstr(chg_get_cmd(), keyword);
	if (ptr != 0) {
		ptr_e = strstr(ptr, " ");
		if (ptr_e == 0)
			goto end;

		size = ptr_e - (ptr + strlen(keyword));
		if (size <= 0)
			goto end;
		strncpy(atm_str, ptr + strlen(keyword), size);
		atm_str[size] = '\0';
		pr_err("atm_str: %s\n", atm_str);

		if (!strncmp(atm_str, "enable", strlen("enable")))
			atm_enabled = true;
	}
end:
	pr_err("atm_enabled = %d\n", atm_enabled);
	return atm_enabled;
}
EXPORT_SYMBOL(tc_get_atm_mode);

int tc_get_boot_mode(void)
{
	struct misc_info *info = tc_get_misc_info();

	if (IS_ERR_OR_NULL(info)) {
		pr_err("get tc_misc info fail\n");
		return -EINVAL;
	}

	return info->bootmode;
}
EXPORT_SYMBOL(tc_get_boot_mode);

int tc_get_boot_type(void)
{
	struct misc_info *info = tc_get_misc_info();

	if (IS_ERR_OR_NULL(info)) {
		pr_err("get tc_misc info fail\n");
		return -EINVAL;
	}

	return info->boottype;
}
EXPORT_SYMBOL(tc_get_boot_type);

static int misc_check_tran_dev_ptr(struct tran_device **dev, const char *name)
{
	if (IS_ERR_OR_NULL(*dev)) {
		*dev = tran_get_by_name(name);
		if (IS_ERR_OR_NULL(*dev)) {
			pr_err("%s Couldn't get dev(%s)\n", __func__, name);
			return -EINVAL;
		}
	}

	return 0;
}

static int misc_check_chg_dev_ptr(struct charger_device **dev, const char *name)
{
	if (IS_ERR_OR_NULL(*dev)) {
		*dev = get_charger_by_name(name);
		if (IS_ERR_OR_NULL(*dev)) {
			pr_err("%s Couldn't get dev(%s)\n", __func__, name);
			return -EINVAL;
		}
	}

	return 0;
}

int tc_get_monkey_flag(void)
{
	int ret = 0;
	int monkey_flag = TRAN_NORMAL_FLAG;
	union com_propval prop = {.intval = 0};
	struct misc_info *info = tc_get_misc_info();

	ret = misc_check_tran_dev_ptr(&info->tc_chg_dev, "tc_charger");
	if (ret < 0) {
		pr_info("%s Couldn't get tc_chg\n", __func__);
		return 0;
	}

	tran_dev_get_prop(info->tc_chg_dev, TRAN_PROP_MONKEY_FLAG, &prop);

	monkey_flag = prop.intval;
	
	return monkey_flag;
}
EXPORT_SYMBOL(tc_get_monkey_flag);

int tc_get_uisoc(void)
{
	int ret = 0;
	int uisoc = 50;
	union com_propval prop = {.intval = 0};
	struct misc_info *info = tc_get_misc_info();
	
	if (info->uisoc_dbg != S32_MAX) {
		uisoc = info->uisoc_dbg;
		goto out;
	}


	ret = misc_check_tran_dev_ptr(&info->gauge_dev, "tc_gauge");
	if (ret < 0) {
		pr_info("%s Couldn't get tc_gauge\n", __func__);
		return 50;
	}

	tran_dev_get_prop(info->gauge_dev, TRAN_PROP_CAPACITY, &prop);

	uisoc = prop.intval;

out:
	return uisoc;
}
EXPORT_SYMBOL(tc_get_uisoc);

int tc_get_qmax(void)
{
	int ret = 0;
	int qmax = 4000;
	union com_propval prop = {.intval = 0};
	struct misc_info *info = tc_get_misc_info();

	ret = misc_check_tran_dev_ptr(&info->gauge_dev, "tc_gauge");
	if (ret < 0) {
		pr_info("%s Couldn't get tc_gauge\n", __func__);
		return 4000;
	}

	tran_dev_get_prop(info->gauge_dev, TRAN_PROP_GET_Q_MAX, &prop);

	qmax = prop.intval;

	return qmax;
}
EXPORT_SYMBOL(tc_get_qmax);

int tc_get_alg_prop(char *alg_name, enum tchg_alg_props s, int *value)
{
	struct tchg_alg_device *alg_dev = NULL;

	if (alg_name == NULL)
		return -EINVAL;

	alg_dev = get_tchg_alg_by_name(alg_name);
	if (IS_ERR_OR_NULL(alg_dev))
		return -ENODEV;

	return tchg_alg_get_prop(alg_dev, s, value);
}
EXPORT_SYMBOL(tc_get_alg_prop);

int tc_set_alg_prop(char *alg_name, enum tchg_alg_props s, int value)
{
	struct tchg_alg_device *alg_dev = NULL;

	if (alg_name == NULL)
		return -EINVAL;

	alg_dev = get_tchg_alg_by_name(alg_name);
	if (IS_ERR_OR_NULL(alg_dev))
		return -ENODEV;

	return tchg_alg_set_prop(alg_dev, s, value);
}
EXPORT_SYMBOL(tc_set_alg_prop);

int tc_get_accurate_battery_temperature(void)
{
	int ret = 0;
	int bat_temp = 25;
	union com_propval prop = {.intval = 0};
	struct misc_info *info = tc_get_misc_info();

	if (info->tbat_dbg != S32_MAX) {
		bat_temp = info->tbat_dbg;
		goto out;
	}

	ret = misc_check_tran_dev_ptr(&info->gauge_dev, "tc_gauge");
	if (ret < 0) {
		pr_info("%s Couldn't get tc_gauge\n", __func__);
		return 0;
	}

	tran_dev_get_prop(info->gauge_dev, TRAN_PROP_TEMP, &prop);

	bat_temp = prop.intval;

out:
	return bat_temp;
}
EXPORT_SYMBOL(tc_get_accurate_battery_temperature);

int tc_get_battery_temperature(void)
{
	return tc_get_accurate_battery_temperature() / 10;

}
EXPORT_SYMBOL(tc_get_battery_temperature);


int tc_get_battery_current(void)
{
	int ret = 0;
	int bat_curr = 0;
	union com_propval prop = {.intval = 0};
	struct misc_info *info = tc_get_misc_info();

	ret = misc_check_tran_dev_ptr(&info->gauge_dev, "tc_gauge");
	if (ret < 0) {
		pr_info("%s Couldn't get tc_gauge\n", __func__);
		return 0;
	}

	tran_dev_get_prop(info->gauge_dev, TRAN_PROP_CURRENT_NOW, &prop);

	bat_curr = prop.intval / 1000;
	/* bat_curr = prop.intval / 100; */
	
	return bat_curr;
}
EXPORT_SYMBOL(tc_get_battery_current);

int tc_get_battery_voltage(void)
{
	int ret = 0;
	int vbat = 0;
	union com_propval prop = {.intval = 0};
	struct misc_info *info = tc_get_misc_info();

	ret = misc_check_tran_dev_ptr(&info->gauge_dev, "tc_gauge");
	if (ret < 0) {
		pr_info("%s Couldn't get tc_gauge\n", __func__);
		return 0;
	}
	
	tran_dev_get_prop(info->gauge_dev, TRAN_PROP_VOLTAGE_NOW, &prop);

	vbat = prop.intval / 1000;

	return vbat;
}
EXPORT_SYMBOL(tc_get_battery_voltage);

int tc_is_battery_exist(void)
{
	int ret = 0;
	bool bat_exit = true;
	union com_propval prop = {.intval = 0};
	struct misc_info *info = tc_get_misc_info();

	ret = misc_check_tran_dev_ptr(&info->gauge_dev, "tc_gauge");
	if (ret < 0) {
		pr_info("%s Couldn't get tc_gauge\n", __func__);
		return 0;
	}

	tran_dev_get_prop(info->gauge_dev, TRAN_PROP_PRESENT, &prop);

	bat_exit = !!prop.intval;
	
	return bat_exit;
}
EXPORT_SYMBOL(tc_is_battery_exist);

int tc_get_dvchg_vbus(void)
{
	int ret = 0;
	int vchr = 0;
	struct misc_info *info = tc_get_misc_info();

	ret = misc_check_chg_dev_ptr(&info->dvchg1_dev, "primary_dvchg");
	if (ret < 0) {
		pr_info("%s Couldn't get dvchg1_dev\n", __func__);
		return vchr;
	}

	ret = charger_dev_get_adc(info->dvchg1_dev, ADC_CHANNEL_VBUS, &vchr, &vchr);
	if (ret < 0) {
		pr_info("%s get dvchg vbus adc failed\n", __func__);
		return vchr;
	}

	vchr = vchr / 1000;
	return vchr;
}

int tc_get_pmic_chg_dev_vbus(void)
{
	int ret = 0;
	int vchr = 0;
	union com_propval prop = {.intval = 0};
	struct misc_info *info = tc_get_misc_info();

	ret = misc_check_chg_dev_ptr(&info->chg1_dev, "primary_chg");
	if (ret < 0) {
		pr_info("%s Couldn't get chg1_dev\n", __func__);
		return vchr;
	}	

	ret = charger_dev_get_vbus(info->chg1_dev, &vchr);
	if (ret >= 0) {
		vchr = vchr / 1000;
		return vchr;
	}

	ret = misc_check_tran_dev_ptr(&info->pmic_dev, "tc_pmic");
	if (ret < 0) {
		pr_info("%s Couldn't get tc_pmic\n", __func__);
		return vchr;
	}
	
	tran_dev_get_prop(info->pmic_dev, TRAN_PROP_VBUS, &prop);
	vchr = prop.intval;

	return vchr;			
}

int tc_get_vbus(void)
{
	int ret = 0;
	int vchr = 0;
	int method = MEASURE_BY_PMIC;
	union com_propval prop = {.intval = 0};
	struct misc_info *info = tc_get_misc_info();

	ret = misc_check_tran_dev_ptr(&info->tc_chg_dev, "tc_charger");
	if (ret < 0) {
		pr_info("%s Couldn't get tc_chg\n", __func__);
		return 0;
	}

	tran_dev_get_prop(info->tc_chg_dev, TRAN_PROP_GET_VBUS_MEASURE_METHOD, &prop);
	method = prop.intval;

	switch (method) {
		case MEASURE_BY_PMIC:
		case MEASURE_BY_SWITCH:
			vchr = tc_get_pmic_chg_dev_vbus();
			break;
		case MEASURE_BY_CP:
			vchr = tc_get_dvchg_vbus();
			break;
		case MEASURE_BY_OTHER:
			/*Add other get vbus way here*/
			break;
		default:
			vchr = tc_get_pmic_chg_dev_vbus();
        	break;
	}
	return vchr;
}
EXPORT_SYMBOL(tc_get_vbus);

int tc_get_ibus(void)
{
	int ret = 0;
	int ibus = 0;
	struct misc_info *info = tc_get_misc_info();

	if (info->det_ibus_by_swchg) {
		ret = misc_check_chg_dev_ptr(&info->chg1_dev, "primary_chg");
		if (ret < 0) {
			pr_info("%s Couldn't get chg1_dev\n", __func__);
			return 0;
		}
		ret = charger_dev_get_ibus(info->chg1_dev, &ibus);
		ibus = ibus / 1000;
	}

	return ibus;
}
EXPORT_SYMBOL(tc_get_ibus);

int tc_disable_hw_ovp(bool enable)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};
	struct misc_info *info = tc_get_misc_info();

	ret = misc_check_tran_dev_ptr(&info->pmic_dev, "tc_pmic");
	if (ret < 0) {
		pr_info("%s Couldn't get tc_pmic\n", __func__);
		return 0;
	}

	prop.intval = enable;
	tran_dev_set_prop(info->pmic_dev, TRAN_PROP_HW_OVP, &prop);
	
	return 0;
}
EXPORT_SYMBOL(tc_disable_hw_ovp);

int tc_get_battery_cycle(void)
{
	int ret = 0;
	int battery_cycle = 0;
	union com_propval prop = {.intval = 0};
	struct misc_info *info = tc_get_misc_info();

	ret = misc_check_tran_dev_ptr(&info->gauge_dev, "tc_gauge");
	if (ret < 0) {
		pr_info("%s Couldn't get tc_gauge\n", __func__);
		return 0;
	}

	tran_dev_get_prop(info->gauge_dev, TRAN_PROP_BATTERY_CYCLE, &prop);

	battery_cycle = prop.intval;

	return battery_cycle;
}
EXPORT_SYMBOL(tc_get_battery_cycle);

int tc_get_battery_raw_cycle(void)
{
	int ret = 0;
	int battery_cycle = 0;
	union com_propval prop = {.intval = 0};
	struct misc_info *info = tc_get_misc_info();

	ret = misc_check_tran_dev_ptr(&info->gauge_dev, "tc_gauge");
	if (ret < 0) {
		pr_info("%s Couldn't get tc_gauge\n", __func__);
		return 0;
	}

	tran_dev_get_prop(info->gauge_dev, TRAN_PROP_BATTERY_RAW_CYCLE, &prop);

	battery_cycle = prop.intval;

	return battery_cycle;
}
EXPORT_SYMBOL(tc_get_battery_raw_cycle);


int tc_set_battery_cycle(int battery_cycle)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};
	struct misc_info *info = tc_get_misc_info();

	ret = misc_check_tran_dev_ptr(&info->gauge_dev, "tc_gauge");
	if (ret < 0) {
		pr_info("%s Couldn't get tc_gauge\n", __func__);
		return 0;
	}

	prop.intval = battery_cycle;
	ret = tran_dev_set_prop(info->gauge_dev, TRAN_PROP_BATTERY_CYCLE, &prop);

	return ret;
}
EXPORT_SYMBOL(tc_set_battery_cycle);


int tc_get_charger_type(void)
{
	union power_supply_propval prop = {0};
	union power_supply_propval prop2 = {0};
	union power_supply_propval prop3 = {0};
	static struct power_supply *chg_psy = NULL;
	int ret;

	if (IS_ERR_OR_NULL(chg_psy)) {
		chg_psy = power_supply_get_by_name("charger");
		if (IS_ERR_OR_NULL(chg_psy)) {
			pr_err("%s Couldn't get charger\n", __func__);
			goto out;
		}
	}

	ret = power_supply_get_property(chg_psy,
		POWER_SUPPLY_PROP_ONLINE, &prop);

	ret = power_supply_get_property(chg_psy,
		POWER_SUPPLY_PROP_TYPE, &prop2);

	ret = power_supply_get_property(chg_psy,
		POWER_SUPPLY_PROP_USB_TYPE, &prop3);

	if (prop.intval == 0 ||
		(prop2.intval == POWER_SUPPLY_TYPE_USB &&
		prop3.intval == POWER_SUPPLY_USB_TYPE_UNKNOWN))
		prop2.intval = POWER_SUPPLY_TYPE_UNKNOWN;

	pr_debug("%s online:%d type:%d usb_type:%d\n", __func__,
		prop.intval,
		prop2.intval,
		prop3.intval);

out:
	return prop2.intval;
}
EXPORT_SYMBOL(tc_get_charger_type);

int tc_get_usb_type(void)
{
	union power_supply_propval prop = {0};
	union power_supply_propval prop2 = {0};
	static struct power_supply *chg_psy = NULL;
	int ret;

	if (IS_ERR_OR_NULL(chg_psy)) {
		chg_psy = power_supply_get_by_name("charger");
		if (IS_ERR_OR_NULL(chg_psy)) {
			pr_err("%s Couldn't get charger\n", __func__);
			goto out;
		}
	}

	ret = power_supply_get_property(chg_psy,
		POWER_SUPPLY_PROP_ONLINE, &prop);
	ret = power_supply_get_property(chg_psy,
		POWER_SUPPLY_PROP_USB_TYPE, &prop2);
	
	pr_debug("%s online:%d usb_type:%d\n", __func__,
		prop.intval,
		prop2.intval);

out:
	return prop2.intval;
}
EXPORT_SYMBOL(tc_get_usb_type);

int tc_get_alias_type(void)
{
	int chg_type = tc_get_charger_type();
	int usb_type = tc_get_usb_type();
	int alias_type = TC_UNKNOWN;

	switch (chg_type) {
	case POWER_SUPPLY_TYPE_UNKNOWN:
		alias_type = TC_UNKNOWN;
		break;
	case POWER_SUPPLY_TYPE_USB:
		if (usb_type == POWER_SUPPLY_USB_TYPE_SDP)
			alias_type = TC_SDP;
		else
			alias_type = TC_NON_STD;
		break;
	case POWER_SUPPLY_TYPE_USB_CDP:
		alias_type = TC_CDP;
		break;
	case POWER_SUPPLY_TYPE_USB_DCP:
		alias_type = TC_DCP;
		break;
	case POWER_SUPPLY_TYPE_WIRELESS:
		alias_type = TC_WIRELESS;
		break;
	default:
		alias_type = TC_UNKNOWN;
	}

	return alias_type;
}
EXPORT_SYMBOL(tc_get_alias_type);

int tc_get_pd_type(void)
{
	struct tadapter_device *pd_adapter = NULL;
	int pd_type = TC_PD_CONNECT_NONE;

	pd_adapter = get_tadapter_by_name("pd_adapter");
	if (IS_ERR_OR_NULL(pd_adapter)) {
		pr_err("failed get pd_adapter\n");
		return -ENODEV;
	}

	pd_type = tadapter_dev_get_property(pd_adapter, PD_TYPE);

	return pd_type;
}
EXPORT_SYMBOL(tc_get_pd_type);

int tc_get_batt_id(void)
{
	return 0;
}
EXPORT_SYMBOL(tc_get_batt_id);

int tc_get_accurate_tpcb_temp(void)
{
	int tpcb = 25000;
	struct misc_info *info = tc_get_misc_info();
	
	if (info->tpcb_dbg != S32_MAX) {
		tpcb = info->tpcb_dbg;
		goto out;
	}

	tpcb = tc_thermal_zone_get_temp(ZONE_PCB);

out:
	return tpcb;
}
EXPORT_SYMBOL(tc_get_accurate_tpcb_temp);


int tc_get_tpcb_temp(void)
{
	return tc_get_accurate_tpcb_temp() / 1000;
}
EXPORT_SYMBOL(tc_get_tpcb_temp);

int tc_get_accurate_tpa_temp_4g(void)
{
	int tpa_4g = 25000;
	struct misc_info *info = tc_get_misc_info();
	
	if (info->tpa_4g_dbg != S32_MAX) {
		tpa_4g = info->tpa_4g_dbg;
		goto out;
	}

	tpa_4g = tc_thermal_zone_get_temp(ZONE_PA_4G);

out:
	return tpa_4g;
}
EXPORT_SYMBOL(tc_get_accurate_tpa_temp_4g);

int tc_get_tpa_temp_4g(void)
{
	return tc_get_accurate_tpa_temp_4g() / 1000;
}
EXPORT_SYMBOL(tc_get_tpa_temp_4g);

int tc_get_accurate_tpa_temp_5g(void)
{
	int tpa_5g = 25000;
	struct misc_info *info = tc_get_misc_info();
	
	if (info->tpa_5g_dbg != S32_MAX) {
		tpa_5g = info->tpa_5g_dbg;
		goto out;
	}

	tpa_5g = tc_thermal_zone_get_temp(ZONE_PA_5G);

out:
	return tpa_5g;
}
EXPORT_SYMBOL(tc_get_accurate_tpa_temp_5g);

int tc_get_tpa_temp_5g(void)
{
	return tc_get_accurate_tpa_temp_5g() / 1000;
}
EXPORT_SYMBOL(tc_get_tpa_temp_5g);

int tc_get_accurate_tpa_temp_max(void)
{
	int tpa;
	int tpa_4g = ERROR_NTC_TEMP;
	int tpa_5g = ERROR_NTC_TEMP;

	tpa_4g = tc_get_accurate_tpa_temp_4g();

	tpa_5g = tc_get_accurate_tpa_temp_5g();

	tpa = max(tpa_4g, tpa_5g);
	if (tpa == ERROR_NTC_TEMP) {
		pr_err("get tpa temp failed");
	}

	return tpa;
}
EXPORT_SYMBOL(tc_get_accurate_tpa_temp_max);

int tc_get_tpa_temp_max(void)
{
	return tc_get_accurate_tpa_temp_max() / 1000;
}
EXPORT_SYMBOL(tc_get_tpa_temp_max);

struct TRAN_BTSCHARGER_TEMPERATURE {
        __s32 BTSCHARGER_Temp;
        __s32 TemperatureR;
};

/* NTCG104EF104F(100K) */
static struct TRAN_BTSCHARGER_TEMPERATURE TRAN_BTSCHARGER_Temperature_Table[] = {
        {-30, 2197860},
        {-25, 1583660},
        {-20, 1153370},
        {-15, 848600},
        {-10, 630470},
        {-5, 472780},
        {0,  357700},
        {5,  272930},
        {10, 209950},
        {15, 162770},
        {20, 127130},
        {25, 100000},           /* 100K */
        {30, 79200},
        {35, 63140},
        {40, 50650},
        {45, 40880},
        {50, 33190},
        {55, 27090},
        {60, 22230},
        {65, 18340},
        {70, 15210},
        {75, 12670},
        {80, 10600},
        {85, 8910},
        {90, 7520},
        {95, 6370},
        {100, 5420},
        {105, 4630},
        {110, 3970},
        {115, 3420},
        {120, 2951},
        {125, 2560}
};

/* convert register to temperature  */
static __s16 __maybe_unused tran_btscharger_thermistor_conver_temp(__s32 Res)
{
        int i = 0;
        int asize = 0;
        __s32 RES1 = 0, RES2 = 0;
        __s32 TAP_Value = -200, TMP1 = 0, TMP2 = 0;

        asize = (sizeof(TRAN_BTSCHARGER_Temperature_Table) / sizeof(struct TRAN_BTSCHARGER_TEMPERATURE));

        if (Res >= TRAN_BTSCHARGER_Temperature_Table[0].TemperatureR) {
                TAP_Value = -300;        /* min */
        } else if (Res <=
                TRAN_BTSCHARGER_Temperature_Table[asize - 1].TemperatureR) {
                TAP_Value = 1250;        /* max */
        } else {
                RES1 = TRAN_BTSCHARGER_Temperature_Table[0].TemperatureR;
                TMP1 = TRAN_BTSCHARGER_Temperature_Table[0].BTSCHARGER_Temp;

                for (i = 0; i < asize; i++) {
                        if (Res >=
                                TRAN_BTSCHARGER_Temperature_Table[i].TemperatureR) {
                                RES2 =
                                TRAN_BTSCHARGER_Temperature_Table[i].TemperatureR;
                                TMP2 =
                                TRAN_BTSCHARGER_Temperature_Table[i].BTSCHARGER_Temp;
                                break;
                        }
                        RES1 = TRAN_BTSCHARGER_Temperature_Table[i].TemperatureR;
                        TMP1 = TRAN_BTSCHARGER_Temperature_Table[i].BTSCHARGER_Temp;
                }

                TAP_Value = (((Res - RES2) * TMP1) + ((RES1 - Res) * TMP2)) * 10
                                                                / (RES1 - RES2);
        }

        return TAP_Value;
}
 
static int __maybe_unused adc_to_tmp(int val) 
{ 
	struct misc_info *info = tc_get_misc_info();
	long int ntc = 0; 
	long int vol_adc;
	__s32 TSBUS_TMP = -100;

	vol_adc = min(info->tusb_div_vol - 1, max(0,val));
	ntc = (info->tusb_div_res * vol_adc) / (info->tusb_div_vol - vol_adc); 
	TSBUS_TMP = tran_btscharger_thermistor_conver_temp(ntc); 
	pr_err("%s val = %d, ntc = %ld TSBUS_TMP = %d\n", __func__, val, ntc, TSBUS_TMP); 
	return TSBUS_TMP; 
}

int tc_get_tusb_temp(int *tusb)
{
	int val;
	int ret = 0;
	struct misc_info *info = tc_get_misc_info();

	if (info->det_tusb_by_swchg) {
		ret = misc_check_chg_dev_ptr(&info->chg1_dev, "primary_chg");
		if (ret < 0) {
			pr_info("%s Couldn't get chg1_dev\n", __func__);
			return 0;
		}
		ret = charger_dev_get_adc(info->chg1_dev,
				ADC_CHANNEL_TS, &val, &val);
	} else {
		if (IS_ERR_OR_NULL(info->tusb_channel)) {
			info->tusb_channel = devm_iio_channel_get(&info->pdev->dev, "tusb");
			if (IS_ERR_OR_NULL(info->tusb_channel)) {
				pr_err("tusb channel err!\n");
				goto out;
			}
		}
	
		ret = iio_read_channel_processed(info->tusb_channel, &val);
		if (ret < 0) {
		        pr_err("%s: fail(%d)\n", __func__, ret);
			goto out;
		}
	        *tusb = adc_to_tmp(val);
	}

	if (*tusb <= info->min_tusb_temp || *tusb >= info->max_tusb_temp) {
		pr_err("tusb not in range %d, use default\n", *tusb);
		goto out;
	}

	pr_err("obtain tusb: %d\n", *tusb);

	return 0;

out:
	*tusb = 250;
	return -EINVAL;
}
EXPORT_SYMBOL(tc_get_tusb_temp);

int tc_get_port_temp(int *port_temp)
{
	int ret = 0;
	int val;
	struct misc_info *info = tc_get_misc_info();

	if (IS_ERR_OR_NULL(info)) {
		pr_err("obtain tc_misc info failed\n");
		goto out;
	}

	if (IS_ERR_OR_NULL(info->port_channel)) {
		pr_err("port channel err!\n");
		goto out;
	}

	ret = iio_read_channel_processed(info->port_channel, &val);
	if (ret < 0) {
	        pr_err("%s: fail(%d)\n", __func__, ret);
		goto out;
	}
        *port_temp = adc_to_tmp(val);

	pr_err("obtain port_temp: %d\n", *port_temp);

	return 0;

out:
	*port_temp = 250;
	return -EINVAL;
}
EXPORT_SYMBOL(tc_get_port_temp);

int tc_get_bypass_energy(void)
{
	union com_propval val = {0};
	struct tran_device *tc_charger = tran_get_by_name("tc_charger");
	if (IS_ERR_OR_NULL(tc_charger)) {
		pr_err("%s: get tc_charger failed\n",__func__);
		return -ENODEV;
	}

	tran_dev_get_prop(tc_charger, TRAN_PROP_GET_BYPASS_ENERGY, &val);

	pr_info("%s: %d\n", __func__, val.intval);

	return val.intval;
}
EXPORT_SYMBOL(tc_get_bypass_energy);

bool tc_get_pe50_det_done(void)
{
	return true;
}
EXPORT_SYMBOL(tc_get_pe50_det_done);


static int tc_misc_parse_dt(struct misc_info *info,
				struct device *dev)
{
	int ret = 0;
	int val = 0;
	struct device_node *np = dev->of_node;
	struct device_node *boot_node = NULL;
	struct tag_bootmode *tag = NULL;

	boot_node = of_parse_phandle(dev->of_node, "bootmode", 0);
	if (!boot_node) {
		pr_err("failed to get boot mode phandle\n");
	} else {
		tag = (struct tag_bootmode *)of_get_property(boot_node,"atag,boot", NULL);
		if (!tag) {
			pr_err("failed to get atag,boot\n");
		} else {
			pr_err("size:0x%x tag:0x%x bootmode:0x%x boottype:0x%x\n",
				tag->size, tag->tag,tag->bootmode, tag->boottype);
			info->bootmode = tag->bootmode;
			info->boottype = tag->boottype;
		}
	}

	info->det_vbus_by_dvchg = of_property_read_bool(np, "det_vbus_by_dvchg");
	info->det_ibus_by_swchg = of_property_read_bool(np, "det_ibus_by_swchg");
	info->det_tusb_by_swchg = of_property_read_bool(np, "det_tusb_by_swchg");

	info->port_channel = devm_iio_channel_get(dev, "tport");
	if (IS_ERR(info->port_channel)) {
		pr_err("parse port channel failed");
	}

	info->tusb_channel = devm_iio_channel_get(dev, "tusb");
	if (IS_ERR(info->tusb_channel)) {
		pr_err("parse tusb channel failed");
	}

	if (of_property_read_u32(np, "min_tusb_temp", &val) >= 0)
		info->min_tusb_temp = val;
	else {
		pr_err("use default min_tusb_temp(-200)\n");
		info->min_tusb_temp = -200;
	}

	if (of_property_read_u32(np, "max_tusb_temp", &val) >= 0)
		info->max_tusb_temp = val;
	else {
		pr_err("use default max_tusb_temp(600)\n");
		info->max_tusb_temp = 600;
	}

	if (of_property_read_u32(np, "tusb_div_res", &val) >= 0)
		info->tusb_div_res = val;
	else {
		pr_err("use default tubs div_res:%d\n", TUSB_DIV_RES);
		info->tusb_div_res = TUSB_DIV_RES;
	}

	if (of_property_read_u32(np, "tusb_div_vol", &val) >= 0)
		info->tusb_div_vol = val;
	else {
		pr_err("use default tubs div_vol:%d\n", TUSB_DIV_VOL);
		info->tusb_div_vol = TUSB_DIV_VOL;
	}
	return ret;
}

static int misc_get_property(struct tran_device *dev,
				enum tran_common_prop prop,
				union com_propval *val)
{
	int ret = 0;
	//struct misc_info *info = tran_get_data(dev);

	switch (prop) {
		default:
			ret = -EINVAL;
	}

	return ret;
}

static int misc_set_property(struct tran_device *dev,
					   enum tran_common_prop prop,
					   const union com_propval *val)
{
	int ret = 0;
	//struct misc_info *info = tran_get_data(dev);

	switch (prop) {
		default:
			ret = -EINVAL;
	}

	return ret;
}

static struct tran_ops misc_detect_ops = {
	.get_prop = misc_get_property,
	.set_prop = misc_set_property,
};

static int tc_misc_prop_init(struct misc_info *info)
{

        info->misc_props.alias_name = "tc_misc";
	info->misc_dev = tran_device_register("tc_misc",
						info->dev, info,
						&misc_detect_ops,
						&info->misc_props);
	if (IS_ERR_OR_NULL(info->misc_dev))
		return -ENODEV;

	return 0;
}

static void tc_misc_param_init(struct misc_info *info)
{
	info->bootmode = -1;
	info->boottype = -1;
}

static void tc_misc_sysfs_init(struct misc_info *info)
{
	int ret = 0;

	info->tbat_dbg = S32_MAX;
	info->tpcb_dbg = S32_MAX;
	info->tpa_4g_dbg = S32_MAX;
	info->tpa_5g_dbg = S32_MAX;
	info->uisoc_dbg = S32_MAX;
	
	ret = sysfs_create_group(&(info->pdev->dev.kobj), &misc_sysfs_group);
	if(ret < 0){
		pr_err("%s : sysfs_create_group failed\n", __func__);
		return;
	}

	ret = sysfs_create_link(chg_kobj,
			&info->pdev->dev.kobj,"charger_dbg");
	if (ret < 0) {
		pr_err("%s : sysfs_create_link failed\n", __func__);
	}
}

static int tc_misc_probe(struct platform_device *pdev)
{
	int ret;
	struct misc_info *info = NULL;

	pr_info("enter\n");
	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);
	info->pdev = pdev;

	tc_misc_param_init(info);

	ret = tc_misc_parse_dt(info, info->dev);
	if(ret < 0) {
		pr_err("tc_misc parse dts failed, ret = %d", ret);
		goto out;
	}

	ret = tc_misc_prop_init(info);
	if (ret < 0) {
		pr_info("register tc_misc device failed\n");
		goto out;
	}

	tc_misc_sysfs_init(info);

	pr_info("successfully\n");

	return 0;
out:
	return ret;
}

static int tc_misc_remove(struct platform_device *dev)
{

	return 0;
}

static void tc_misc_shutdown(struct platform_device *dev)
{
	return;
}

static const struct of_device_id tc_misc_of_match[] = {
	{.compatible = "tc,misc_intf",},
	{},
};

MODULE_DEVICE_TABLE(of, tc_misc_of_match);

static struct platform_driver tc_misc_platdrv = {
	.probe = tc_misc_probe,
	.remove = tc_misc_remove,
	.shutdown = tc_misc_shutdown,
	.driver = {
		.name = "misc_intf",
		.owner = THIS_MODULE,
		.of_match_table = tc_misc_of_match,
	},
};

static void __exit tran_class_exit(void)
{
	platform_driver_unregister(&tc_misc_platdrv);
}

static int __init tran_class_init(void)
{
	return platform_driver_register(&tc_misc_platdrv);
}

subsys_initcall(tran_class_init);
module_exit(tran_class_exit);

MODULE_DESCRIPTION("TC Driver");
MODULE_LICENSE("GPL");
