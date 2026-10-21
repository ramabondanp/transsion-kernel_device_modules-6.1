// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Transsion Inc.
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/version.h>
#include "tc_common_class.h"
#include "tc_charger_class.h"
#include "tc_misc_intf.h"
#include "tc_gauge.h"
#include "tc_charger.h"
#include "tc_auto_test.h"

struct tc_gauge {
	struct device *dev;
	struct platform_device *pdev;
	struct tran_device *gauge_dev;
	struct tran_device *ambient_dev;
	struct tran_properties gauge_props;
	struct tran_device *bat_dev;
	struct power_supply_desc gauge_psy_desc;
	struct power_supply_config gauge_psy_cfg;
	struct power_supply *gauge_psy;
	struct power_supply *batt_psy;
	struct power_supply *chg_psy;
	struct charger_device *chg1_dev;
	struct tran_device *charge_transfer_dev;
	struct mutex tc_gauge_mutex;
	struct class *bat_cali_class;
	struct cdev *bat_cali_cdev;
	struct notifier_block gauge_notifier;
	int batt_status;
	int batt_cycle;
	int bat_cali_major;
	dev_t bat_cali_devno;
	int fake_batt_cycle;
	int batt_cycle_ratio;
	int fixed_bat_tmp;
};

int gauge_get_uisoc(struct tc_gauge *info);

//For Midtest apk
#define BAT_CALI_DEVNAME "MT_pmic_adc_cali"
#define TEST_ADC_CALI_PRINT _IO('k', 0)
#define SET_ADC_CALI_Slop _IOW('k', 1, int)
#define SET_ADC_CALI_Offset _IOW('k', 2, int)
#define SET_ADC_CALI_Cal _IOW('k', 3, int)
#define ADC_CHANNEL_READ _IOW('k', 4, int)
#define BAT_STATUS_READ _IOW('k', 5, int)
#define Set_Charger_Current _IOW('k', 6, int)
/* add for meta tool----------------------------------------- */
#define Get_META_BAT_VOL _IOW('k', 10, int)
#define Get_META_BAT_SOC _IOW('k', 11, int)
#define Get_META_BAT_CAR_TUNE_VALUE _IOW('k', 12, int)
#define Set_META_BAT_CAR_TUNE_VALUE _IOW('k', 13, int)
#define Set_BAT_DISABLE_NAFG _IOW('k', 14, int)
#define Set_CARTUNE_TO_KERNEL _IOW('k', 15, int)

static long adc_cali_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int *user_data_addr;
	int ret = 0;
	int adc_in_data[2] = { 1, 1 };
	int adc_out_data[2] = { 1, 1 };
	struct tc_gauge *info;
	struct power_supply *psy;

	pr_err("%s enter\n", __func__);

	psy = power_supply_get_by_name("battery");
	if (psy == NULL)
		return -ENODEV;

	info = (struct tc_gauge *)power_supply_get_drvdata(psy);

	mutex_lock(&info->tc_gauge_mutex);
	user_data_addr = (int *)arg;
	ret = copy_from_user(adc_in_data, user_data_addr, sizeof(adc_in_data));
	if (adc_in_data[1] < 0) {
		pr_err("%s unknown data: %d\n", __func__, adc_in_data[1]);
		mutex_unlock(&info->tc_gauge_mutex);
		return -EFAULT;
	}

	switch (cmd) {
	case Get_META_BAT_VOL:
		adc_out_data[0] =
			tc_get_battery_voltage();
		if (copy_to_user(user_data_addr, adc_out_data,
			sizeof(adc_out_data))) {
			mutex_unlock(&info->tc_gauge_mutex);
			return -EFAULT;
		}

		pr_err("**** unlocked_ioctl :Get_META_BAT_VOL Done!\n");
		break;
	case Get_META_BAT_SOC:
		adc_out_data[0] = gauge_get_uisoc(info);

		if (copy_to_user(user_data_addr, adc_out_data,
			sizeof(adc_out_data))) {
			mutex_unlock(&info->tc_gauge_mutex);
			return -EFAULT;
		}

		pr_err("**** unlocked_ioctl :Get_META_BAT_SOC Done!\n");
		break;
	case ADC_CHANNEL_READ:
		/* g_ADC_Cali = KAL_FALSE; *//* 20100508 Infinity */
		if (adc_in_data[0] == 0) {
			/* I_SENSE */
			adc_out_data[0] =
				tc_get_battery_voltage() * adc_in_data[1];
		} else if (adc_in_data[0] == 1) {
			/* BAT_SENSE */
			adc_out_data[0] =
				tc_get_battery_voltage() * adc_in_data[1];
		} else if (adc_in_data[0] == 3) {
			/* V_Charger */
			adc_out_data[0] = tc_get_vbus() * adc_in_data[1];
			/* adc_out_data[0] = adc_out_data[0] / 100; */
		} else if (adc_in_data[0] == 30) {
			/* V_Bat_temp magic number */
			adc_out_data[0] =
				tc_get_battery_temperature() * adc_in_data[1];
		} else if (adc_in_data[0] == 66)
			adc_out_data[0] = tc_get_battery_current();
		else {
			pr_err("unknown channel(%d,%d)\n",
				adc_in_data[0], adc_in_data[1]);
		}

		if (adc_out_data[0] < 0)
			adc_out_data[1] = 1;	/* failed */
		else
			adc_out_data[1] = 0;	/* success */

		if (adc_in_data[0] == 30)
			adc_out_data[1] = 0;	/* success */

		if (adc_in_data[0] == 66)
			adc_out_data[1] = 0;	/* success */

		ret = copy_to_user(user_data_addr, adc_out_data, 8);
		pr_err(
				"**** unlocked_ioctl : Channel %d * %d times = %d\n",
				adc_in_data[0], adc_in_data[1], adc_out_data[0]);
		break;
	default:
		mutex_unlock(&info->tc_gauge_mutex);
		pr_err("**** unlocked_ioctl unknown IOCTL: 0x%08x\n", cmd);
		return -EINVAL;
	}

	mutex_unlock(&info->tc_gauge_mutex);

	return 0;
}

static int adc_cali_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int adc_cali_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations adc_cali_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = adc_cali_ioctl,
	.open = adc_cali_open,
	.release = adc_cali_release,
};

static int adc_cali_cdev_init(struct platform_device *pdev)
{
	int ret = 0;
	struct class_device *class_dev = NULL;
	struct tc_gauge *info;

	info = platform_get_drvdata(pdev);

	mutex_init(&info->tc_gauge_mutex);

	ret = alloc_chrdev_region(&info->bat_cali_devno, 0, 1, BAT_CALI_DEVNAME);
	if (ret)
		pr_err("Error: Can't Get Major number for adc_cali\n");

	info->bat_cali_cdev = cdev_alloc();
	info->bat_cali_cdev->owner = THIS_MODULE;
	info->bat_cali_cdev->ops = &adc_cali_fops;
	ret = cdev_add(info->bat_cali_cdev, info->bat_cali_devno, 1);
	if (ret)
		pr_err("adc_cali Error: cdev_add\n");

	info->bat_cali_major = MAJOR(info->bat_cali_devno);
	info->bat_cali_class = class_create(THIS_MODULE, BAT_CALI_DEVNAME);
	class_dev = (struct class_device *)device_create(info->bat_cali_class,
		NULL,
		info->bat_cali_devno,
		NULL, BAT_CALI_DEVNAME);

	return 0;
}

static int gauge_check_psy_ptr(struct power_supply **psy, const char *name)
{
	if (IS_ERR_OR_NULL(*psy)) {
		*psy = power_supply_get_by_name(name);
		if (IS_ERR_OR_NULL(*psy)) {
			pr_err("%s Couldn't get psy(%s)\n", __func__, name);
			return -EINVAL;
		}
	}

	return 0;
}

static int gauge_check_tran_dev_ptr(struct tran_device **dev, const char *name)
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

static int gauge_check_device_init(struct tc_gauge *info)
{
	int ret = 0;
	static bool init_done = false;

	if (init_done)
		return ret;

	if (IS_ERR_OR_NULL(info->chg1_dev)) {
		info->chg1_dev = get_charger_by_name("primary_chg");
		if (IS_ERR_OR_NULL(info->chg1_dev)) {
			pr_err("find primary_chg fail\n");
			return -ENODEV;
		}
	}

	if (IS_ERR_OR_NULL(info->batt_psy)) {
		info->batt_psy = power_supply_get_by_name("battery");
		if (IS_ERR_OR_NULL(info->batt_psy)) {
			pr_err("find batter psy fail\n");
			return -ENODEV;
		}
	}
	init_done = true;

	return ret;
}

bool gauge_is_battery_exist(struct tc_gauge *info)
{
	int ret = 0;
	union power_supply_propval prop = {.intval = 0};

	ret = power_supply_get_property(info->batt_psy,
			POWER_SUPPLY_PROP_PRESENT, &prop);
	if (ret < 0) {
		pr_err("%s Couldn't check batt present!\n", __func__);
		return true;
	}

	pr_debug("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_uisoc(struct tc_gauge *info)
{
	int ret = 0;
	union power_supply_propval prop = {.intval = 0};

	ret = power_supply_get_property(info->batt_psy,
			POWER_SUPPLY_PROP_CAPACITY, &prop);
	if (ret < 0 || (prop.intval < 0 && prop.intval != -1)) {
		pr_err("%s Couldn't get capacity\n", __func__);
		return DEFAULT_UISOC;
	}

	pr_debug("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_qmax(struct tc_gauge *info)
{
	int ret = 0;
	union power_supply_propval prop = {.intval = 0};

	ret = power_supply_get_property(info->batt_psy,
			POWER_SUPPLY_PROP_CHARGE_FULL, &prop);
	if (ret < 0 || prop.intval < 0) {
		pr_err("%s Couldn't get capacity\n", __func__);
		return DEFAULT_Q_MAX;
	}

	pr_info("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_real_soc(struct tc_gauge *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return 0;
	}
	tran_dev_get_prop(info->bat_dev, TRAN_PROP_FG_REAL_SOC, &prop);

	pr_info("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_vsoc(struct tc_gauge *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return 0;
	}
	tran_dev_get_prop(info->bat_dev, TRAN_PROP_FG_VSOC, &prop);
	pr_info("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_csoc(struct tc_gauge *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return 0;
	}
	tran_dev_get_prop(info->bat_dev, TRAN_PROP_FG_CSOC, &prop);
	pr_info("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_d0_c(struct tc_gauge *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return 0;
	}
	tran_dev_get_prop(info->bat_dev, TRAN_PROP_FG_DO_C, &prop);
	pr_info("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_d0_v(struct tc_gauge *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return 0;
	}
	tran_dev_get_prop(info->bat_dev, TRAN_PROP_FG_DO_V, &prop);
	pr_info("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_aging(struct tc_gauge *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return 0;
	}
	tran_dev_get_prop(info->bat_dev, TRAN_PROP_FG_AGING, &prop);
	pr_info("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_accuracy_uisoc(struct tc_gauge *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return 0;
	}
	tran_dev_get_prop(info->bat_dev, TRAN_PROP_ACCURACY_UISOC, &prop);
	pr_info("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_fg_raw_cycle_count(struct tc_gauge *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return 0;
	}

	tran_dev_get_prop(info->bat_dev, TRAN_PROP_BATTERY_RAW_CYCLE, &prop);
	pr_info("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_fg_cycle_count(struct tc_gauge *info)
{
	if (info->fake_batt_cycle != -1)
		return info->fake_batt_cycle;

	return info->batt_cycle;
}

int gauge_get_battery_voltage(struct tc_gauge *info)
{
	int ret = 0;
	union power_supply_propval prop = {.intval = 0};

	ret = power_supply_get_property(info->batt_psy,
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &prop);
	if (ret < 0) {
		pr_err("%s Couldn't get batt voltage now\n", __func__);
		return DEFAULT_BATT_VOLTAGE;
	}

	pr_debug("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_battery_temperature(struct tc_gauge *info)
{
	int ret = 0;
	union power_supply_propval prop = {.intval = 0};

	ret = power_supply_get_property(info->batt_psy,
			POWER_SUPPLY_PROP_TEMP, &prop);
	if (ret < 0) {
		pr_err("%s Couldn't get battery temp\n", __func__);
		return DEFAULT_BATT_TEMP;
	}

	pr_debug("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_battery_current(struct tc_gauge *info)
{
	int ret = 0;
	union power_supply_propval prop = {.intval = 0};

	ret = power_supply_get_property(info->batt_psy,
			POWER_SUPPLY_PROP_CURRENT_NOW, &prop);
	if (ret < 0) {
		pr_err("%s Couldn't get current now\n", __func__);
		return 0;
	}

	pr_debug("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_charger_full_design(struct tc_gauge *info)
{
	int ret = 0;
	union power_supply_propval prop = {.intval = 0};

	ret = power_supply_get_property(info->batt_psy,
			POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, &prop);
	if (ret < 0) {
		pr_err("%s Couldn't get design full now\n", __func__);
		return 0;
	}

	pr_debug("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_fg_hw_car(struct tc_gauge *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return 0;
	}
	
	tran_dev_get_prop(info->bat_dev, TRAN_PROP_FG_HW_CAR, &prop);
	
	pr_debug("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_fg_rc(struct tc_gauge *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return 0;
	}
	
	tran_dev_get_prop(info->bat_dev, TRAN_PROP_BATT_RM, &prop);
	
	pr_debug("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_master_batt_working(struct tc_gauge *info, int *status)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return 0;
	}
	
	ret = tran_dev_get_prop(info->bat_dev, TRAN_PROP_BATT_A_WORKING, &prop);
	if (ret < 0) {
		*status = 1;
		goto out;
	}

	*status = prop.intval;
out:	
	pr_debug("%s: %d\n", __func__, *status);

	return ret;
}

int gauge_get_slave_batt_working(struct tc_gauge *info, int *status)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return 0;
	}
	
	ret = tran_dev_get_prop(info->bat_dev, TRAN_PROP_BATT_B_WORKING, &prop);
	if (ret < 0) {
		*status = 0;
		goto out;
	}

	*status = prop.intval;
out:	
	pr_info("%s: %d\n", __func__, *status);

	return ret;
}

int gauge_get_master_batt_voltage(struct tc_gauge *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return 0;
	}

	ret = tran_dev_get_prop(info->bat_dev, TRAN_PROP_MASTER_BATT_VOLT, &prop);
	if (ret < 0) {
		pr_err("%s: failed get master batt volt, ret:%d\n", __func__, ret);
		return 0;
	}

	pr_debug("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_slave_batt_voltage(struct tc_gauge *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return 0;
	}

	ret = tran_dev_get_prop(info->bat_dev, TRAN_PROP_SLAVE_BATT_VOLT, &prop);
	if (ret < 0) {
		pr_err("%s: failed get slave batt volt, ret:%d\n", __func__, ret);
		return 0;
	}

	pr_debug("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_master_batt_current(struct tc_gauge *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return 0;
	}

	ret = tran_dev_get_prop(info->bat_dev, TRAN_PROP_MASTER_BATT_NOW_CURR, &prop);
	if (ret < 0) {
		pr_err("%s: failed get master batt curr, ret:%d\n", __func__, ret);
		return 0;
	}

	pr_debug("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_slave_batt_current(struct tc_gauge *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return 0;
	}

	ret = tran_dev_get_prop(info->bat_dev, TRAN_PROP_SLAVE_BATT_NOW_CURR, &prop);
	if (ret < 0) {
		pr_err("%s: failed get slave batt curr, ret:%d\n", __func__, ret);
		return 0;
	}

	pr_debug("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_master_batt_temp(struct tc_gauge *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return 0;
	}

	ret = tran_dev_get_prop(info->bat_dev, TRAN_PROP_MASTER_BATT_TEMP, &prop);
	if (ret < 0) {
		pr_err("%s: failed get master batt temp, ret:%d\n", __func__, ret);
		return 0;
	}

	pr_debug("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_slave_batt_temp(struct tc_gauge *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return 0;
	}

	ret = tran_dev_get_prop(info->bat_dev, TRAN_PROP_SLAVE_BATT_TEMP, &prop);
	if (ret < 0) {
		pr_err("%s: failed get slave batt temp, ret:%d\n", __func__, ret);
		return 0;
	}

	pr_info("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_master_batt_en(struct tc_gauge *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return 0;
	}

	ret = tran_dev_get_prop(info->bat_dev, TRAN_PROP_MASTER_BATT_EN, &prop);
	if (ret < 0) {
		pr_err("%s: failed get master batt en, ret:%d\n", __func__, ret);
		return 0;
	}

	pr_info("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_slave_batt_en(struct tc_gauge *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return 0;
	}

	ret = tran_dev_get_prop(info->bat_dev, TRAN_PROP_SLAVE_BATT_EN, &prop);
	if (ret < 0) {
		pr_err("%s: failed get slave batt en, ret:%d\n", __func__, ret);
		return 0;
	}

	pr_info("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_get_batt_run_mode(struct tc_gauge *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return 0;
	}

	tran_dev_get_prop(info->bat_dev, TRAN_PROP_BATT_RUN_MODE, &prop);
	
	pr_info("%s: %d\n", __func__, prop.intval);

	return prop.intval;
}

int gauge_notify_car_reset(struct tc_gauge *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = gauge_check_tran_dev_ptr(&info->ambient_dev, "ambient_detect");
	if (ret < 0) {
		pr_info("%s Couldn't get ambient_dev\n", __func__);
		return 0;
	}
	
	tran_dev_set_prop(info->ambient_dev, TRAN_PROP_MTK_GAUGE_CAR_RESET, &prop);
	
	tran_auto_test_set_property(TRAN_TEST_PROP_FG_OLD_CAR, -1);

	pr_info("%s\n", __func__);

	return 0;
}

static void gauge_update_battery_cycle(struct tc_gauge *info)
{
	int raw_cycle = 0;
	char buf[64] = {0};
	char *env[2] = {NULL, NULL};

	raw_cycle = gauge_get_fg_raw_cycle_count(info);
	pr_info("%s: get raw_cycle:%d\n", __func__, raw_cycle);

	snprintf(buf, sizeof(buf), "BAT_CYCLE:%d", raw_cycle);
	env[0] = buf;

	kobject_uevent_env(&info->pdev->dev.kobj, KOBJ_CHANGE, env);
}

static int gauge_check_batt_working_status(struct tc_gauge *info)
{
	int status, master_working, slave_working;

	gauge_get_master_batt_working(info, &master_working);
	gauge_get_slave_batt_working(info, &slave_working);

	if (master_working && slave_working)
		status = GAUGE_DUAL_BATT_ONLINE;
	else if (master_working)
		status = GAUGE_SINGLE_MASTER_BATT_ONLINE;
	else if (slave_working)
		status = GAUGE_SINGLE_SLAVE_BATT_ONLINE;
	else
		status = GAUGE_NONE_BATT_ONLINE;

	pr_info("%s: batt working status = %d\n",__func__,status);

	return status;
}

static int gauge_check_cap_level(struct tc_gauge *info)
{
	int uisoc = 0;

	uisoc = gauge_get_uisoc(info);

	if (uisoc >= 100)
		return POWER_SUPPLY_CAPACITY_LEVEL_FULL;
	else if (uisoc >= 80 && uisoc < 100)
		return POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
	else if (uisoc >= 20 && uisoc < 80)
		return POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	else if (uisoc > 0 && uisoc < 20)
		return POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	else if (uisoc == 0) {
#if IS_ENABLED(CONFIG_TRAN_LOW_BATTERY_AGING)
		return POWER_SUPPLY_CAPACITY_LEVEL_LOW;
#else
		if (info->batt_status == POWER_SUPPLY_STATUS_CHARGING)
			return POWER_SUPPLY_CAPACITY_LEVEL_LOW;
		else
			return POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
#endif
	} else
		return POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;
}

static int tc_gauge_get_property(struct tran_device *dev,
				enum tran_common_prop prop,
				union com_propval *val)
{
	int ret = 0;
	int value = 0;
	int bootmode;
	union com_propval tran_val = {.intval = 0};
	struct tc_gauge *info = tran_get_data(dev);

	ret = gauge_check_device_init(info);
	if (ret < 0) {
		pr_err("gauge check device fail\n");
		return -EINVAL;
	}

	//Only for battery start
	switch (prop) {
	case TRAN_PROP_PSY_STATUS:
		val->intval = info->batt_status;
		break;
	case TRAN_PROP_PSY_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case TRAN_PROP_PSY_PRESENT:
		break;
	case TRAN_PROP_PSY_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case TRAN_PROP_PSY_CYCLE_COUNT:
		break;
	case TRAN_PROP_PSY_CAPACITY:
		if (IS_ERR_OR_NULL(info->charge_transfer_dev))
			info->charge_transfer_dev = tran_get_by_name("charge_transfer");

		bootmode = tc_get_boot_mode();

		/* 0 = NORMAL_BOOT */
		/* 8 = KERNEL_POWER_OFF_CHARGING_BOOT */
		/* 9 = LOW_POWER_OFF_CHARGING_BOOT */
		if ((bootmode == 0 || bootmode == 8 || bootmode == 9)
			&& !IS_ERR_OR_NULL(info->charge_transfer_dev)) {
			ret = tran_dev_get_prop(info->charge_transfer_dev,
					TRAN_PROP_GET_BATT_UI_SOC, &tran_val);
			if (!ret && tran_val.intval >= 0)
				val->intval = tran_val.intval;
		}
		break;
	case TRAN_PROP_PSY_CURRENT_NOW:
		break;
	case TRAN_PROP_PSY_CURRENT_AVG:
		break;
	case TRAN_PROP_PSY_CHARGE_FULL:
		break;
	case TRAN_PROP_PSY_CHARGE_COUNTER:
		break;
	case TRAN_PROP_PSY_VOLTAGE_NOW:
		/* 1 = META_BOOT, 4 = FACTORY_BOOT 5=ADVMETA_BOOT */
		/* 6= ATE_factory_boot */
		bootmode = tc_get_boot_mode();
		if (bootmode == 1 || bootmode == 4
			|| bootmode == 5 || bootmode == 6) {
			val->intval = 4000000;
			break;
		}
		break;
	case TRAN_PROP_PSY_TEMP:
		if (info->fixed_bat_tmp != 0xffff) {
			val->intval = info->fixed_bat_tmp;
			pr_err("liml TRAN_PROP_PSY_TEMP\n");
		}
		break;
	case TRAN_PROP_PSY_CAPACITY_LEVEL:
		val->intval = gauge_check_cap_level(info);
		break;
	case TRAN_PROP_PSY_TIME_TO_FULL_NOW:
		break;
	case TRAN_PROP_PSY_CHARGE_FULL_DESIGN:
		break;
	case TRAN_PROP_PSY_CONSTANT_CHARGE_VOLTAGE:
		break;
	//Only for battery end

	//old_start
	case TRAN_PROP_PRESENT:
		value = gauge_is_battery_exist(info);
		val->intval = value;
		break;
	case TRAN_PROP_CAPACITY:
		value = gauge_get_uisoc(info);
		val->intval = value;
		break;
	case TRAN_PROP_VOLTAGE_NOW:
		value = gauge_get_battery_voltage(info);
		val->intval = value;
		break;
	case TRAN_PROP_TEMP:
		value = gauge_get_battery_temperature(info);
		val->intval = value;
		break;
	case TRAN_PROP_CURRENT_NOW:
		value = gauge_get_battery_current(info);
		val->intval = value;
		break;
	case TRAN_PROP_CHARGE_FULL_DESIGN:
		value = gauge_get_charger_full_design(info);
		val->intval = value;
		break;
	case TRAN_PROP_FG_HW_CAR:
		value = gauge_get_fg_hw_car(info);
		val->intval = value;
		break;
	case TRAN_PROP_BATT_RM:
		value = gauge_get_fg_rc(info);
		val->intval = value;
		break;
	case TRAN_PROP_GET_Q_MAX:
		value = gauge_get_qmax(info);
		val->intval = value / 1000;
		break;
	case TRAN_PROP_ACCURACY_UISOC:
		value = gauge_get_accuracy_uisoc(info);
		val->intval = value;
		break;
	case TRAN_PROP_BATTERY_RAW_CYCLE:
		value = gauge_get_fg_raw_cycle_count(info);
		val->intval = value;
		break;
	case TRAN_PROP_BATTERY_CYCLE:
		value = gauge_get_fg_cycle_count(info);
		val->intval = value;
		break;
	case TRAN_PROP_BATT_WORKING_STATUS:
		value = gauge_check_batt_working_status(info);
		val->intval = value;
		break;
	case TRAN_PROP_BATT_RUN_MODE:
		value = gauge_get_batt_run_mode(info);
		val->intval = value;
		break;
	case TRAN_PROP_BATT_STATUS:
		value = info->batt_status;
		val->intval = value;
		break;
	case TRAN_PROP_BATT_FW_STATUS:
		break;
	case TRAN_PROP_MASTER_BATT_VOLT:
		value = gauge_get_master_batt_voltage(info);
		val->intval = value;
		break;
	case TRAN_PROP_MASTER_BATT_NOW_CURR:
		value = gauge_get_master_batt_current(info);
		val->intval = value;
		break;
	case TRAN_PROP_MASTER_BATT_TEMP:
		value = gauge_get_master_batt_temp(info);
		val->intval = value;
		break;
	case TRAN_PROP_MASTER_BATT_EN:
		value = gauge_get_master_batt_en(info);
		val->intval = value;
		break;
	case TRAN_PROP_SLAVE_BATT_VOLT:
		value  = gauge_get_slave_batt_voltage(info);
		val->intval = value;
		break;
	case TRAN_PROP_SLAVE_BATT_NOW_CURR:
		value = gauge_get_slave_batt_current(info);
		val->intval = value;
		break;
	case TRAN_PROP_SLAVE_BATT_TEMP:
		value = gauge_get_slave_batt_temp(info);
		val->intval = value;
		break;
	case TRAN_PROP_SLAVE_BATT_EN:
		value = gauge_get_slave_batt_en(info);
		val->intval = value;
		break;
	case TRAN_PROP_FG_REAL_SOC:
		value = gauge_get_real_soc(info);
		val->intval = value;
		break;
	case TRAN_PROP_FG_VSOC:
		value = gauge_get_vsoc(info);
		val->intval = value;
		break;
	case TRAN_PROP_FG_CSOC:
		value = gauge_get_csoc(info);
		val->intval = value;
		break;
	case TRAN_PROP_FG_UI_SOC:
		value = gauge_get_uisoc(info);
		val->intval = value;
		break;
	case TRAN_PROP_FG_DO_C:
		value = gauge_get_d0_c(info);
		val->intval = value;
		break;
	case TRAN_PROP_FG_DO_V:
		value = gauge_get_d0_v(info);
		val->intval = value;
		break;
	case TRAN_PROP_FG_AGING:
		value = gauge_get_aging(info);
		val->intval = value;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int tc_gauge_set_property(struct tran_device *dev,
					   enum tran_common_prop prop,
					   const union com_propval *val)
{
	int ret = 0;
	struct tc_gauge *info = tran_get_data(dev);

	ret = gauge_check_device_init(info);
	if (ret < 0) {
		pr_err("gauge check device fail\n");
		return -EINVAL;
	}

	switch (prop) {
		case TRAN_PROP_MTK_GAUGE_CAR_RESET:
			gauge_notify_car_reset(info);
			break;
		case TRAN_PROP_BATTERY_CYCLE:
			info->batt_cycle = val->intval;
			break;
		default:
			ret = -EINVAL;
	}

	return ret;
}

static struct tran_ops tc_gauge_ops = {
	.get_prop = tc_gauge_get_property,
	.set_prop = tc_gauge_set_property,
};

static int tc_gauge_prop_init(struct tc_gauge *info)
{

        info->gauge_props.alias_name = "tc_gauge";
	info->gauge_dev = tran_device_register("tc_gauge",
						info->dev, info,
						&tc_gauge_ops,
						&info->gauge_props);
	if (IS_ERR_OR_NULL(info->gauge_dev))
		return -ENODEV;

	return 0;
}

static enum power_supply_property gauge_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
};

static int gauge_psy_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	int ret = 0;
	struct tc_gauge *info = (struct tc_gauge *)power_supply_get_drvdata(psy);

	/* ret = gauge_check_device_init(info); */
	/* if (ret < 0) { */
	/*         pr_err("gauge check device fail\n"); */
	/*         return -EINVAL; */
	/* } */

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = info->batt_status;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int gauge_psy_set_property(struct power_supply *psy,
	enum power_supply_property psp,
	const union power_supply_propval *val)
{
	int ret = 0;
	/* struct tc_gauge *info = (struct tc_gauge *)power_supply_get_drvdata(psy); */

	/* ret = gauge_check_device_init(info); */
	/* if (ret < 0) { */
	/*         pr_err("gauge check device fail\n"); */
	/*         return -ENODEV; */
	/* } */


	return ret;
}

static void tc_gauge_external_power_changed(struct power_supply *psy)
{
	int ret = 0;
	union power_supply_propval online, status;
	struct tc_gauge *info = (struct tc_gauge *)power_supply_get_drvdata(psy);

	ret = gauge_check_psy_ptr(&info->chg_psy, "charger");
	if (ret < 0) {
		pr_err("get chg_psy failed, check!\n");
		goto out;
	}

	ret = power_supply_get_property(info->chg_psy,
		POWER_SUPPLY_PROP_ONLINE, &online);

	ret = power_supply_get_property(info->chg_psy,
		POWER_SUPPLY_PROP_STATUS, &status);

	if (!online.intval) {
		info->batt_status = POWER_SUPPLY_STATUS_DISCHARGING;
	} else {
		if (status.intval == POWER_SUPPLY_STATUS_NOT_CHARGING ||
			status.intval == POWER_SUPPLY_STATUS_DISCHARGING) {

			info->batt_status = status.intval;

		} else {
			info->batt_status = POWER_SUPPLY_STATUS_CHARGING;
		}
	}

	gauge_update_battery_cycle(info);

out:
	power_supply_changed(info->gauge_psy);
}

static int tc_gauge_psy_init(struct tc_gauge *info)
{
	struct platform_device *pdev = info->pdev;

	info->gauge_psy_desc.name = "tc_gauge";
	info->gauge_psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->gauge_psy_desc.properties = gauge_properties;
	info->gauge_psy_desc.num_properties = ARRAY_SIZE(gauge_properties);
	info->gauge_psy_desc.get_property = gauge_psy_get_property;
	info->gauge_psy_desc.set_property = gauge_psy_set_property;
	info->gauge_psy_desc.external_power_changed =
				tc_gauge_external_power_changed;
	info->gauge_psy_cfg.drv_data = info;
	info->gauge_psy = power_supply_register(&pdev->dev, &info->gauge_psy_desc,
			&info->gauge_psy_cfg);

	if (IS_ERR_OR_NULL(info->gauge_psy)) {
		pr_err("gauge psy register fail\n");
		return -EINVAL;
	}

	return 0;
}

static ssize_t tran_fake_battery_cycle_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct tc_gauge *data = dev->driver_data;

	return sprintf(buf, "echo -1 cancel fake_batt_cycle, now:%d\n", data->fake_batt_cycle);
}

static ssize_t tran_fake_battery_cycle_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	struct tc_gauge *data = dev->driver_data;
	int val = -1;

	if (IS_ERR_OR_NULL(buf)) {
		pr_info("%s: error buffer\n", __func__);
		return size;
	}

	if (kstrtoint(buf, 10, &val) != 0) {
		pr_info("%s: parse buf failed\n", __func__);
		return size;
	}

	pr_info("%s: pre fake_batt_cycle = %d\n", __func__, data->fake_batt_cycle);

	data->fake_batt_cycle = val;

	pr_info("%s: post fake_batt_cycle = %d\n", __func__, data->fake_batt_cycle);

	return size;
}
static DEVICE_ATTR_RW(tran_fake_battery_cycle);

static ssize_t tran_battery_cycle_ratio_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct tc_gauge *data = dev->driver_data;

	return sprintf(buf, "reboot device reset battery cycle ratio, now:%d\n", data->batt_cycle_ratio);
}

static ssize_t tran_battery_cycle_ratio_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	struct tc_gauge *data = dev->driver_data;
	int val = -1;

	if (IS_ERR_OR_NULL(buf)) {
		pr_info("%s: error buffer\n", __func__);
		return size;
	}

	if (kstrtoint(buf, 10, &val) != 0) {
		pr_info("%s: parse buf failed\n", __func__);
		return size;
	}

	pr_info("%s: pre batt_cycle_ratio = %d\n", __func__, data->batt_cycle_ratio);

	data->batt_cycle_ratio = val;
	tc_set_alg_prop(alg_name_array[PE5_ID], ALG_SET_CYCLE_RATIO, data->batt_cycle_ratio);

	pr_info("%s: post batt_cycle_ratio = %d\n", __func__, data->batt_cycle_ratio);

	return size;
}
static DEVICE_ATTR_RW(tran_battery_cycle_ratio);

static ssize_t tran_fixed_bat_tmp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct tc_gauge *info = dev->driver_data;

	return sprintf(buf, "info->fixed_bat_tmp%d\n", info->fixed_bat_tmp);
}

static ssize_t tran_fixed_bat_tmp_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	int val = 0xffff;
	struct tc_gauge *info = dev->driver_data;

	if (IS_ERR_OR_NULL(buf)) {
		pr_info("%s: error buffer\n", __func__);
		return size;
	}

	if (kstrtoint(buf, 10, &val) != 0) {
		pr_info("%s: parse buf failed\n", __func__);
		return size;
	}

	pr_info("%s: pre bat fixed temp = %d\n", __func__, val);

	if (val != 0xffff && val>= -400 && val <= 650) {
		info->fixed_bat_tmp = val;
	}

	return size;
}
static DEVICE_ATTR_RW(tran_fixed_bat_tmp);

static struct attribute *gauge_sysfs_attrs[] = {
	&dev_attr_tran_fake_battery_cycle.attr,
	&dev_attr_tran_battery_cycle_ratio.attr,
	&dev_attr_tran_fixed_bat_tmp.attr,
	NULL,
};

static const struct attribute_group gauge_sysfs_group = {
	.attrs = gauge_sysfs_attrs,
};

static int tc_gauge_notifier_callback(struct notifier_block *nb,
                    unsigned long event, void *data)
{
	struct tc_gauge *info = container_of(nb,
			struct tc_gauge, gauge_notifier);
	
	switch (event) {
	case TRAN_DEV_NOTIFY_BATT_ONLINE_CHANGE:
	case TRAN_DEV_NOTIFY_BATT_MASTER_CHG_FULL:
	case TRAN_DEV_NOTIFY_BATT_SLAVE_CHG_FULL:
		tran_dev_notify(info->gauge_dev, event, info);
		break;
	default:
		break;
	}

	return 0;
}

static int gauge_init_notifier(struct tc_gauge *info)
{
	int ret = 0;

	ret = gauge_check_tran_dev_ptr(&info->bat_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get bat_dev\n", __func__);
		return -ENODEV;
	}
	

	info->gauge_notifier.notifier_call = tc_gauge_notifier_callback;
	ret = register_tran_device_notifier(info->bat_dev,
				&info->gauge_notifier);
	if (ret != 0) {
		pr_err("register gauge notify failed, ret = %d\n", ret);
		return ret;
	}

	return ret;
}

static int tc_gauge_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct tc_gauge *info = NULL;

	pr_info("%s: starts\n", __func__);

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, info);
	info->pdev = pdev;
	info->dev = &pdev->dev;
	info->fixed_bat_tmp = 0xffff;
	info->fake_batt_cycle = -1;
	platform_set_drvdata(pdev, info);

	ret = gauge_check_device_init(info);
	if (ret < 0) {
		pr_err("wait related module load, defer\n");
		ret = -EPROBE_DEFER;
		goto out;
	}
	
	ret = sysfs_create_group(&pdev->dev.kobj, &gauge_sysfs_group);
	if (ret < 0) {
		pr_err("register gauge sysfs group failed\n\n");
		goto out;
	}

	ret = tc_gauge_prop_init(info);
	if (ret < 0) {
		pr_err("register battery dev failed,ret = %d\n", ret);
		goto out;
	}

	ret = tc_gauge_psy_init(info);
	if (ret < 0) {
		pr_err("register battery psy failed,ret = %d\n", ret);
		goto out;
	}

	adc_cali_cdev_init(pdev);

	gauge_init_notifier(info);

	pr_info("%s: done\n", __func__);
	return 0;
out:
	return ret;
}

static const struct of_device_id tc_gauge_of_match[] = {
	{.compatible = "tc_gauge",},
	{},
};

static int tc_gauge_remove(struct platform_device *pdev)
{
	return 0;
}

MODULE_DEVICE_TABLE(of, tc_gauge_of_match);

static struct platform_driver tc_gauge_driver = {
	.probe = tc_gauge_probe,
	.remove = tc_gauge_remove,
	.driver = {
		.name = "Tc_gauge",
		.of_match_table = tc_gauge_of_match,
	},
};
module_platform_driver(tc_gauge_driver);

MODULE_DESCRIPTION("TC Gauge Hal Device Driver");
MODULE_LICENSE("GPL");
