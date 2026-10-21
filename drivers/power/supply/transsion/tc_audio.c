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
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/iio/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include "tc_common_class.h"
#include "tc_water_detect.h"

struct tc_audio {
	struct device *dev;
	struct platform_device *pdev;
	struct tran_device *audio_dev;
	struct tran_properties audio_props;
	struct power_supply *bat_psy;
	struct tran_device *usbc_analog_switch_dev;
	int wd_scheme;
	int gpio_sbu2_micgnd;
};
/*
static __maybe_unused int audio_check_psy_ptr(struct power_supply **psy, const char *name)
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
*/
/*ouyang fixed remove*/
#define SELECT_SBU1 0
#define SELECT_SBU2 1

static void set_sw_gpio_switch_sbu(int gpio,int val)
{
	if (gpio < 0 || gpio == U32_MAX)
		return;

	gpio_set_value(gpio, val);
	pr_err("%s:water sbu1/2 gpio en =%d\n",__func__,val);
	return;
}

static void audio_select_sbu_1_2(struct tc_audio *info, bool enable)
{
	if(enable)
		set_sw_gpio_switch_sbu(info->gpio_sbu2_micgnd, SELECT_SBU1);
	else
		set_sw_gpio_switch_sbu(info->gpio_sbu2_micgnd, SELECT_SBU2);

	return;
}

static int audio_get_property(struct tran_device *dev,
				enum tran_common_prop prop,
				union com_propval *val)
{
	struct tc_audio *info = tran_get_data(dev);
	union com_propval tran_val = {0, };
	int ret = 0;

	switch (prop) {
		case TRAN_PROP_WATER_DETECT:
			if(info->wd_scheme == SCHEME_USB_SWITCH){
				if (IS_ERR_OR_NULL(info->usbc_analog_switch_dev)){
					info->usbc_analog_switch_dev = tran_get_by_name("usbc_analog_switch");
					if (IS_ERR_OR_NULL(info->usbc_analog_switch_dev)) {
						pr_err("get usbc_analog_switch_dev fail\n");
						return -EINVAL;
					}
				}

				tran_dev_get_prop(info->usbc_analog_switch_dev,TRAN_PROP_WATER_DETECT, &tran_val);
				val->intval = tran_val.intval;
			}
			break;
		default:
			ret = -EINVAL;
	}

	return ret;
}

static int audio_set_property(struct tran_device *dev,
					   enum tran_common_prop prop,
					   const union com_propval *val)
{
	int ret = 0;
	struct tc_audio *info = tran_get_data(dev);

	switch (prop) {
	case TRAN_PROP_WD_SELECT_SBU_1_2:
		if(info->wd_scheme == SCHEME_USB_ID_AND_GPIO_AND_SBU_1_2)
			audio_select_sbu_1_2(info, !!val->intval);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static struct tran_ops tc_audio_ops = {
	.get_prop = audio_get_property,
	.set_prop = audio_set_property,
};

static int tc_audio_prop_init(struct tc_audio *info)
{
    info->audio_props.alias_name = "tc_audio";
	info->audio_dev = tran_device_register("tc_audio",
						info->dev, info,
						&tc_audio_ops,
						&info->audio_props);
	if (IS_ERR_OR_NULL(info->audio_dev))
		return -ENODEV;

	return 0;
}

static int tc_audio_parse_dt(struct tc_audio *info,
				struct device *dev)
{
	int ret = 0;
	struct device_node *wd_node;
	struct device_node *np = dev->of_node;

	//get water detect node scheme
	wd_node = of_parse_phandle(np, "water_detect", 0);
	if (!wd_node) {
		pr_err("get wd_node fail\n");
		return -1;
	}

	ret = of_property_read_u32(wd_node, "scheme", &info->wd_scheme);
	if (ret < 0) {
		pr_err("parse scheme failed,use default 0, ret:%d\n", ret);
		info->wd_scheme = SCHEME_SUBPMIC;
	}

	info->gpio_sbu2_micgnd = of_get_named_gpio(np, "gpio_sbu2_micgnd", 0);
	if (info->gpio_sbu2_micgnd < 0 || info->gpio_sbu2_micgnd == U32_MAX) {
		pr_err("parse gpio_sbu2_micgnd failed, gpio_sbu2_micgnd:%d\n", info->gpio_sbu2_micgnd);
	}

	pr_err("%s,wd_scheme = %d\n",__func__,info->wd_scheme);

	return ret;
}

static int tc_audio_probe(struct platform_device *pdev)
{
	struct tc_audio *info = NULL;

	pr_info("%s: starts\n", __func__);

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->pdev = pdev;
	info->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, info);
	platform_set_drvdata(pdev, info);

	tc_audio_parse_dt(info, info->dev);

	tc_audio_prop_init(info);
	pr_info("%s: done\n", __func__);

	return 0;
}

static const struct of_device_id tc_audio_of_match[] = {
	{.compatible = "tc_audio",},
	{},
};

static int tc_audio_remove(struct platform_device *pdev)
{
	struct tc_audio *info = platform_get_drvdata(pdev);

	pr_info("%s\n", __func__);

	if (!IS_ERR_OR_NULL(info->audio_dev))
		tran_device_unregister(info->audio_dev);

	return 0;
}

MODULE_DEVICE_TABLE(of, tc_audio_of_match);

static struct platform_driver tc_audio_driver = {
	.probe = tc_audio_probe,
	.remove = tc_audio_remove,
	.driver = {
		.name = "tc_audio",
		.of_match_table = tc_audio_of_match,
	},
};
module_platform_driver(tc_audio_driver);

MODULE_DESCRIPTION("TC Audio Hal Device Driver");
MODULE_LICENSE("GPL");

