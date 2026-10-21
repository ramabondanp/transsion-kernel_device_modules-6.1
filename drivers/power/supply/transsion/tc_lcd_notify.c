// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#define pr_fmt(fmt)     "[lcd_notify] %s: " fmt, __func__
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
#include <linux/fb.h>
#include "mtk_disp_notify.h"
#include "tc_charger.h"

#include "tc_common_class.h"
#include "tc_charger_class.h"

struct lcd_info {
	struct platform_device *pdev;
	struct device *dev;
	struct tran_device *lcd_dev;
	struct tran_properties lcd_props;
	struct notifier_block tc_screen_notifier;
	bool screen_on;
};

static int tc_screen_notifier_callback(struct notifier_block *nb,
                    unsigned long event,void *data)
{
	int blank = *(int *)data;
	struct lcd_info *info = container_of(nb,
			struct lcd_info, tc_screen_notifier);
	
#if IS_ENABLED(CONFIG_TRAN_FOLD_DISPLAY)
	if ((event == MTK_DISP_EARLY_EVENT_BLANK) && 
		(blank == MTK_DISP_BLANK_POWERDOWN || blank == TRAN_DISP_BLANK_POWERDOWN_DSI1)) {
#else
	if ((event == MTK_DISP_EARLY_EVENT_BLANK) && 
		(blank == MTK_DISP_BLANK_POWERDOWN)) {
#endif
		pr_info("%s: screen off\n", __func__);
		info->screen_on = false;
		tran_dev_notify(info->lcd_dev, TRAN_DEV_NOTIFY_SCREEN_OFF, info);

#if IS_ENABLED(CONFIG_TRAN_FOLD_DISPLAY)
	} else if ((event == MTK_DISP_EVENT_BLANK) &&
		(blank == MTK_DISP_BLANK_UNBLANK || blank == TRAN_DISP_BLANK_UNBLANK_DSI1)) {
#else
	} else if ((event == MTK_DISP_EVENT_BLANK) &&
		(blank == MTK_DISP_BLANK_UNBLANK)) {
#endif
		pr_info("%s: screen on\n", __func__);
		info->screen_on = true;
		tran_dev_notify(info->lcd_dev, TRAN_DEV_NOTIFY_SCREEN_ON, info);
	}

	return 0;
}

static int lcd_get_property(struct tran_device *dev,
				enum tran_common_prop prop,
				union com_propval *val)
{
	int ret = 0;
	//struct lcd_info *info = tran_get_data(dev);

	switch (prop) {
		default:
			ret = -EINVAL;
	}

	return ret;
}

static int lcd_set_property(struct tran_device *dev,
					   enum tran_common_prop prop,
					   const union com_propval *val)
{
	int ret = 0;
	//struct lcd_info *info = tran_get_data(dev);

	switch (prop) {
		default:
			ret = -EINVAL;
	}

	return ret;
}

static struct tran_ops lcd_detect_ops = {
	.get_prop = lcd_get_property,
	.set_prop = lcd_set_property,
};

static int tc_lcd_prop_init(struct lcd_info *info)
{

        info->lcd_props.alias_name = "tc_lcd";
	info->lcd_dev = tran_device_register("tc_lcd",
						info->dev, info,
						&lcd_detect_ops,
						&info->lcd_props);
	if (IS_ERR_OR_NULL(info->lcd_dev))
		return -ENODEV;

	return 0;
}

static int tc_lcd_probe(struct platform_device *pdev)
{
	int ret;
	struct lcd_info *info = NULL;

	pr_info("enter\n");
	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);
	info->pdev = pdev;

	ret = tc_lcd_prop_init(info);
	if (ret < 0) {
		pr_info("register tc_lcd device failed\n");
		goto out;
	}

	info->tc_screen_notifier.notifier_call = tc_screen_notifier_callback;
	ret = mtk_disp_notifier_register("pid", &info->tc_screen_notifier);
	if (ret !=0) {
		pr_err("%s register screen notify fail!\n", __func__);
		goto out;
	}

	pr_info("successfully\n");

	return 0;
out:
	return ret;
}

static int tc_lcd_remove(struct platform_device *dev)
{

	return 0;
}

static void tc_lcd_shutdown(struct platform_device *dev)
{
	return;
}

static const struct of_device_id tc_lcd_of_match[] = {
	{.compatible = "tc,lcd",},
	{},
};

MODULE_DEVICE_TABLE(of, tc_lcd_of_match);

static struct platform_driver tc_lcd_platdrv = {
	.probe = tc_lcd_probe,
	.remove = tc_lcd_remove,
	.shutdown = tc_lcd_shutdown,
	.driver = {
		.name = "tc_lcd",
		.owner = THIS_MODULE,
		.of_match_table = tc_lcd_of_match,
	},
};

static void __exit tran_class_exit(void)
{
	platform_driver_unregister(&tc_lcd_platdrv);
}

static int __init tran_class_init(void)
{
	return platform_driver_register(&tc_lcd_platdrv);
}

subsys_initcall(tran_class_init);
module_exit(tran_class_exit);

MODULE_DESCRIPTION("TC Driver");
MODULE_LICENSE("GPL");
