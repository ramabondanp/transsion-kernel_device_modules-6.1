// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2005 Transsion Inc.
 */

#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <uapi/linux/uleds.h>
#include <linux/of.h>
#include "tc_led_class.h"

static struct tc_led_device *pLed_dev;

int tc_led_dev_store_tran_led_cmd(struct tc_led_device *dev, const char *cmd)
{
	if (dev != NULL && dev->ops != NULL &&
	    dev->ops->store_tran_led_cmd){
		return dev->ops->store_tran_led_cmd(dev, cmd);
	}

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tc_led_dev_store_tran_led_cmd);

int tc_led_dev_show_tran_led_cmd(struct tc_led_device *dev, char *buf)
{
	if (dev != NULL && dev->ops != NULL &&
	    dev->ops->show_tran_led_cmd)
		return dev->ops->show_tran_led_cmd(dev, buf);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tc_led_dev_show_tran_led_cmd);

int tc_led_dev_store_tran_led_check(struct tc_led_device *dev, const char *cmd)
{
	if (dev != NULL && dev->ops != NULL &&
	    dev->ops->store_tran_led_check){
		return dev->ops->store_tran_led_check(dev, cmd);
	}

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tc_led_dev_store_tran_led_check);

int tc_led_dev_show_tran_led_check(struct tc_led_device *dev, char *buf)
{
	if (dev != NULL && dev->ops != NULL &&
	    dev->ops->show_tran_led_check)
		return dev->ops->show_tran_led_check(dev, buf);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tc_led_dev_show_tran_led_check);

struct tc_led_device *get_led_device(void)
{
	return pLed_dev;
}
EXPORT_SYMBOL(get_led_device);

struct tc_led_device *tc_led_device_register(const char *name,
		struct device *parent, void *devdata,
		const struct tc_led_ops *ops,
		const struct tc_led_properties *props)
{
	struct tc_led_device *led_dev;

	int rc;

	led_dev = kzalloc(sizeof(*led_dev), GFP_KERNEL);
	if (!led_dev)
		return ERR_PTR(-ENOMEM);

	led_dev->dev.parent = parent;
	dev_set_name(&led_dev->dev, name);
	dev_set_drvdata(&led_dev->dev, devdata);

	/* Copy properties */
	if (props) {
		memcpy(&led_dev->props, props,
		       sizeof(struct tc_led_properties));
	}
	rc = device_register(&led_dev->dev);
	if (rc) {
		kfree(led_dev);
		return ERR_PTR(rc);
	}
	led_dev->ops = ops;

	pLed_dev = led_dev;
	return led_dev;
}
EXPORT_SYMBOL(tc_led_device_register);

void tc_led_device_unregister(struct tc_led_device *led_dev)
{
	if (!led_dev)
		return;

	led_dev->ops = NULL;
	device_unregister(&led_dev->dev);
}
EXPORT_SYMBOL(tc_led_device_unregister);

MODULE_AUTHOR("Transsion Inc.");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Transsion LED Class Interface");
