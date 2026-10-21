// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include "tc_ta_class.h"

static struct class *tc_ta_class;

static inline bool tc_ta_ops_ok(struct tc_ta_classdev *ptc)
{
	return ptc && ptc->ops;
}

int tc_ta_device_get_power_limit(struct tc_ta_classdev *ptc, u32 *power)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->get_power_limit)
		? ptc->ops->get_power_limit(ptc, power) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_get_power_limit);

int tc_ta_device_get_verinfo(struct tc_ta_classdev *ptc, u8 *info,
			       u32 length)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->get_verinfo)
		? ptc->ops->get_verinfo(ptc, info, length) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_get_verinfo);

int tc_ta_device_get_fwcode(struct tc_ta_classdev *ptc, u8 *code,
			      u32 length)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->get_fwcode)
		? ptc->ops->get_fwcode(ptc, code, length) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_get_fwcode);

int tc_ta_device_get_mfinfo(struct tc_ta_classdev *ptc, u8 *code,
			      u32 length)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->get_mfinfo)
		? ptc->ops->get_mfinfo(ptc, code, length) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_get_mfinfo);

int tc_ta_device_get_datecode(struct tc_ta_classdev *ptc, u8 *code,
				u32 length)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->get_datecode)
		? ptc->ops->get_datecode(ptc, code, length) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_get_datecode);

int tc_ta_device_get_min_voltage(struct tc_ta_classdev *ptc, u32 *volt)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->get_min_voltage)
		? ptc->ops->get_min_voltage(ptc, volt) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_get_min_voltage);

int tc_ta_device_get_max_voltage(struct tc_ta_classdev *ptc, u32 *volt)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->get_max_voltage)
		? ptc->ops->get_max_voltage(ptc, volt) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_get_max_voltage);

int tc_ta_device_get_min_current(struct tc_ta_classdev *ptc, u32 *curr)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->get_min_current)
		? ptc->ops->get_min_current(ptc, curr) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_get_min_current);

int tc_ta_device_get_max_current(struct tc_ta_classdev *ptc, u32 *curr)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->get_max_current)
		? ptc->ops->get_max_current(ptc, curr) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_get_max_current);

int tc_ta_device_get_output_voltage(struct tc_ta_classdev *ptc, u32 *volt)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->get_output_voltage)
		? ptc->ops->get_output_voltage(ptc, volt) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_get_output_voltage);

int tc_ta_device_get_output_current(struct tc_ta_classdev *ptc, u32 *curr)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->get_output_current)
		? ptc->ops->get_output_current(ptc, curr) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_get_output_current);

int tc_ta_device_get_status(struct tc_ta_classdev *ptc, u32 *status)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->get_status)
		? ptc->ops->get_status(ptc, status) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_get_status);

int tc_ta_device_get_temp1(struct tc_ta_classdev *ptc, u32 *degree)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->get_temp1)
		? ptc->ops->get_temp1(ptc, degree) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_get_temp1);

int tc_ta_device_get_temp2(struct tc_ta_classdev *ptc, u32 *degree)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->get_temp2)
		? ptc->ops->get_temp2(ptc, degree) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_get_temp2);

int tc_ta_device_set_output_control(struct tc_ta_classdev *ptc, u32 control)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->set_output_control)
		? ptc->ops->set_output_control(ptc, control) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_set_output_control);

int tc_ta_device_set_mode(struct tc_ta_classdev *ptc, u32 mode)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->set_mode)
		? ptc->ops->set_mode(ptc, mode) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_set_mode);

int tc_ta_device_set_voltage(struct tc_ta_classdev *ptc, u32 volt)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->set_voltage)
		? ptc->ops->set_voltage(ptc, volt) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_set_voltage);

int tc_ta_device_set_current(struct tc_ta_classdev *ptc, u32 curr)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->set_current)
		? ptc->ops->set_current(ptc, curr) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_set_current);

int tc_ta_device_set_voltage_current(struct tc_ta_classdev *ptc, u32 volt,
				       u32 curr)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->set_voltage_current)
		? ptc->ops->set_voltage_current(ptc, volt, curr) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_set_voltage_current);

int tc_ta_device_get_output_voltage_current(struct tc_ta_classdev *ptc,
					      u32 *volt, u32 *curr)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->get_output_voltage_current)
		? ptc->ops->get_output_voltage_current(ptc, volt, curr)
		: -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_get_output_voltage_current);

int tc_ta_device_authentication(struct tc_ta_classdev *ptc)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->authentication)
		? ptc->ops->authentication(ptc) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_authentication);

int tc_ta_device_enable_wdt(struct tc_ta_classdev *ptc, bool en)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->enable_wdt)
		? ptc->ops->enable_wdt(ptc, en) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_enable_wdt);

int tc_ta_device_set_wdt(struct tc_ta_classdev *ptc, u32 ms)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->set_wdt)
		? ptc->ops->set_wdt(ptc, ms) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_set_wdt);

int tc_ta_device_get_max_power_duration(struct tc_ta_classdev *ptc,
					      u32 *time)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->get_max_power_duration)
		? ptc->ops->get_max_power_duration(ptc, time)
		: -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_get_max_power_duration);

int tc_ta_device_get_output_control_support(struct tc_ta_classdev *ptc, bool *support)
{
	return (tc_ta_ops_ok(ptc) && ptc->ops->get_output_control_support)
		? ptc->ops->get_output_control_support(ptc, support) : -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tc_ta_device_get_output_control_support);

#ifdef CONFIG_PM_SLEEP
static int tc_ta_classdev_suspend(struct device *dev)
{
	struct tc_ta_classdev *ptc = dev_get_drvdata(dev);

	return (tc_ta_ops_ok(ptc) && ptc->ops->suspend) ? ptc->ops->suspend(ptc) : 0;
}

static int tc_ta_classdev_resume(struct device *dev)
{
	struct tc_ta_classdev *ptc = dev_get_drvdata(dev);

	return (tc_ta_ops_ok(ptc) && ptc->ops->resume) ? ptc->ops->resume(ptc) : 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(tc_ta_class_pm_ops,
			 tc_ta_classdev_suspend, tc_ta_classdev_resume);

int tc_ta_classdev_register(struct device *parent,
			      struct tc_ta_classdev *ptc)
{
	ptc->dev = device_create_with_groups(tc_ta_class, parent, 0,
					      ptc, ptc->groups, "%s",
					      ptc->name);
	if (IS_ERR(ptc->dev))
		return PTR_ERR(ptc->dev);
	return 0;
}
EXPORT_SYMBOL_GPL(tc_ta_classdev_register);

void tc_ta_classdev_unregister(struct tc_ta_classdev *ptc)
{
	device_unregister(ptc->dev);
}
EXPORT_SYMBOL_GPL(tc_ta_classdev_unregister);

static void devm_tc_ta_classdev_release(struct device *dev, void *res)
{
	tc_ta_classdev_unregister(*(struct tc_ta_classdev **)res);
}

int devm_tc_ta_classdev_register(struct device *parent,
				   struct tc_ta_classdev *ptc)
{
	struct tc_ta_classdev **pptc;
	int rc;

	pptc = devres_alloc(devm_tc_ta_classdev_release,
			    sizeof(*pptc), GFP_KERNEL);
	if (!pptc)
		return -ENOMEM;
	rc = tc_ta_classdev_register(parent, ptc);
	if (rc < 0) {
		devres_free(pptc);
		return rc;
	}
	*pptc = ptc;
	devres_add(parent, pptc);
	return 0;
}
EXPORT_SYMBOL_GPL(devm_tc_ta_classdev_register);

static int tc_ta_match_device_by_name(struct device *dev, const void *data)
{
	const char *name = data;
	struct tc_ta_classdev *ptc = dev_get_drvdata(dev);

	return strcmp(ptc->name, name) == 0;
}

struct tc_ta_classdev *tc_ta_device_get_by_name(const char *name)
{
	struct tc_ta_classdev *ptc = NULL;
	struct device *dev = class_find_device(tc_ta_class, NULL, name,
						  tc_ta_match_device_by_name);

	if (dev)
		ptc = dev_get_drvdata(dev);
	return ptc;
}
EXPORT_SYMBOL_GPL(tc_ta_device_get_by_name);

static int __init tc_ta_init(void)
{
	tc_ta_class = class_create(THIS_MODULE, "tc_ta");
	if (IS_ERR(tc_ta_class))
		return PTR_ERR(tc_ta_class);
	tc_ta_class->pm = &tc_ta_class_pm_ops;
	return 0;
}
module_init(tc_ta_init);

static void __exit tc_ta_exit(void)
{
	class_destroy(tc_ta_class);
}
module_exit(tc_ta_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Proprietary TA Class driver");
MODULE_VERSION("1.0.0");
