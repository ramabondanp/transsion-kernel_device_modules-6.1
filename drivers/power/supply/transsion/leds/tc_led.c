// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Transsion Inc.
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
#include <linux/platform_device.h>
#include <linux/of.h>
#include "tc_led_class.h"

static ssize_t store_tran_led_cmd(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	if (!size)
		return 0;
    tc_led_dev_store_tran_led_cmd(get_led_device(), buf);
    return size;
}

static ssize_t show_tran_led_cmd(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	char pBuf;
	tc_led_dev_show_tran_led_cmd(get_led_device(),&pBuf);
	return sprintf(buf, "%c\n", pBuf);
}
static DEVICE_ATTR(tran_led_cmd, 0664, show_tran_led_cmd, store_tran_led_cmd);

static ssize_t store_tran_led_check(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	if (!size)
		return 0;
    tc_led_dev_store_tran_led_check(get_led_device(), buf);
    return size;
}

static ssize_t show_tran_led_check(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	char pBuf;
	tc_led_dev_show_tran_led_check(get_led_device(),&pBuf);
	return sprintf(buf, "%c\n", pBuf);
}
static DEVICE_ATTR(tran_led_check, 0664, show_tran_led_check, store_tran_led_check);

static struct attribute *led_class_attrs[] = {
	&dev_attr_tran_led_cmd.attr,
	&dev_attr_tran_led_check.attr,
	NULL,
};

static const struct attribute_group led_group = {
	.attrs = led_class_attrs,
};

static int tc_led_probe(struct platform_device *pdev)
{
	struct tc_led_device *led_dev;
	struct kobject *kobj;
	int rc;

	pr_info("%s: starts\n", __func__);

	led_dev = devm_kzalloc(&pdev->dev, sizeof(*led_dev), GFP_KERNEL);
	if (!led_dev)
		return -ENOMEM;
	led_dev->dev = pdev->dev;
	platform_set_drvdata(pdev, led_dev);
	led_dev->pdev = pdev;

	rc = sysfs_create_group(&pdev->dev.kobj, &led_group);
	if(rc < 0){
		pr_err("%s : sysfs_create_group failed rc=%d\n", __func__,rc);
		return 0;
	}

	kobj = kobject_create_and_add("led", NULL);
	if (!kobj) {
		pr_err("%s:sysfs_create_group fail",__func__);
		return 0;
	}

	rc = sysfs_create_link(kobj,&led_dev->dev.kobj,"led");
	if(rc < 0){
		pr_err("%s : sysfs_create_link failed rc=%d\n", __func__,rc);
		return 0;
	}

	pr_info("%s: done\n", __func__);
	return 0;
}

static int tc_led_remove(struct platform_device *dev)
{
	return 0;
}

static void tc_led_shutdown(struct platform_device *dev)
{
	return;
}

static const struct of_device_id tc_led_of_match[] = {
	{.compatible = "tc,led",},
	{},
};

MODULE_DEVICE_TABLE(of, tc_led_of_match);

struct platform_device tc_charger_device = {
	.name = "tc_led",
	.id = -1,
};

static struct platform_driver tc_led_driver = {
	.probe = tc_led_probe,
	.remove = tc_led_remove,
	.shutdown = tc_led_shutdown,
	.driver = {
		   .name = "tc_led",
		   .of_match_table = tc_led_of_match,
	},
};

static int __init tc_led_init(void)
{
	return platform_driver_register(&tc_led_driver);
}

late_initcall(tc_led_init);

static void __exit tc_led_exit(void)
{
	platform_driver_unregister(&tc_led_driver);
}
module_exit(tc_led_exit);

MODULE_DESCRIPTION("TC LED Driver");
MODULE_LICENSE("GPL");
