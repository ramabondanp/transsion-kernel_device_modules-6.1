
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
#include "tc_common_class.h"
#include <linux/mfd/mt6397/core.h>/* PMIC MFD core header */

struct tc_pmic {
	struct device *dev;
	struct platform_device *pdev;
	struct tran_device *pmic_dev;
	struct tran_properties pmic_props;
	struct power_supply *bat_psy;
	struct iio_channel *chan_vbus;
};

static __maybe_unused int pmic_check_psy_ptr(struct power_supply **psy, const char *name)
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

#define PMIC_RG_VCDT_HV_EN_ADDR		0xb88
#define PMIC_RG_VCDT_HV_EN_MASK		0x1
#define PMIC_RG_VCDT_HV_EN_SHIFT	11

static void pmic_set_register_value(struct regmap *map,
	unsigned int addr,
	unsigned int mask,
	unsigned int shift,
	unsigned int val)
{
	regmap_update_bits(map,
		addr,
		mask << shift,
		val << shift);
}

unsigned int pmic_get_register_value(struct regmap *map,
	unsigned int addr,
	unsigned int mask,
	unsigned int shift)
{
	unsigned int value = 0;

	regmap_read(map, addr, &value);
	value = (value & (mask << shift)) >> shift;
	return value;
}

int pmic_disable_hw_ovp(struct tc_pmic *info, int en)
{
	struct device_node *pmic_node;
	struct platform_device *pmic_pdev;
	struct mt6397_chip *chip;
	struct regmap *regmap;

	pmic_node = of_parse_phandle(info->pdev->dev.of_node, "pmic", 0);
	if (!pmic_node) {
		pr_err("get pmic_node fail\n");
		return -1;
	}

	pmic_pdev = of_find_device_by_node(pmic_node);
	if (!pmic_pdev) {
		pr_err("get pmic_pdev fail\n");
		return -1;
	}
	chip = dev_get_drvdata(&(pmic_pdev->dev));

	if (!chip) {
		pr_err("get chip fail\n");
		return -1;
	}

	regmap = chip->regmap;

	pmic_set_register_value(regmap,
		PMIC_RG_VCDT_HV_EN_ADDR,
		PMIC_RG_VCDT_HV_EN_SHIFT,
		PMIC_RG_VCDT_HV_EN_MASK,
		en);

	return 0;
}

#define R_CHARGER_1                             330
#define R_CHARGER_2                             39
int pmic_get_vbus(struct tc_pmic *info)
{
	int ret;
	int val;

	if (IS_ERR_OR_NULL(info->chan_vbus))
		return -ENOTSUPP;

	ret = iio_read_channel_processed(info->chan_vbus, &val);
	if (ret < 0) {
		pr_err("[%s]read fail,ret=%d\n", __func__, ret);
		return -EINVAL;
	}

	val = (((R_CHARGER_1 + R_CHARGER_2)
		* 100 * val) / R_CHARGER_2) / 100;

	return val;
}

static int pmic_get_property(struct tran_device *dev,
				enum tran_common_prop prop,
				union com_propval *val)
{
	int ret = 0;
	int value = 0;
	struct tc_pmic *info = tran_get_data(dev);

	switch (prop) {
	case TRAN_PROP_VBUS:
		value = pmic_get_vbus(info);
		val->intval = value;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int pmic_set_property(struct tran_device *dev,
					   enum tran_common_prop prop,
					   const union com_propval *val)
{
	int ret = 0;
	struct tc_pmic *info = tran_get_data(dev);

	switch (prop) {
	case TRAN_PROP_HW_OVP:
		pmic_disable_hw_ovp(info, !!val->intval);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static struct tran_ops tc_pmic_ops = {
	.get_prop = pmic_get_property,
	.set_prop = pmic_set_property,
};

static int tc_pmic_prop_init(struct tc_pmic *info)
{

        info->pmic_props.alias_name = "tc_pmic";
	info->pmic_dev = tran_device_register("tc_pmic",
						info->dev, info,
						&tc_pmic_ops,
						&info->pmic_props);
	if (IS_ERR_OR_NULL(info->pmic_dev))
		return -ENODEV;

	return 0;
}

static int tc_pmic_probe(struct platform_device *pdev)
{
	struct tc_pmic *info = NULL;

	pr_info("%s: starts\n", __func__);

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, info);
	info->pdev = pdev;
	info->dev = &pdev->dev;

	info->chan_vbus = devm_iio_channel_get(&pdev->dev, "pmic_vbus");
	if (IS_ERR(info->chan_vbus)) {
		dev_info(&pdev->dev, "get chan_vbus failed\n");
	}

	tc_pmic_prop_init(info);
	pr_info("%s: done\n", __func__);

	return 0;
}

static const struct of_device_id tc_pmic_of_match[] = {
	{.compatible = "tc_pmic",},
	{},
};

static int tc_pmic_remove(struct platform_device *pdev)
{
	return 0;
}

MODULE_DEVICE_TABLE(of, tc_pmic_of_match);

static struct platform_driver tc_pmic_driver = {
	.probe = tc_pmic_probe,
	.remove = tc_pmic_remove,
	.driver = {
		.name = "tc_pmic",
		.of_match_table = tc_pmic_of_match,
	},
};
module_platform_driver(tc_pmic_driver);

MODULE_DESCRIPTION("TC Pmic Hal Device Driver");
MODULE_LICENSE("GPL");
