// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 Transsion Inc.
 */

#define pr_fmt(fmt)     "[phy_det] %s: " fmt, __func__
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/delay.h>
#include "tc_charger.h"
#include "tc_common_class.h"

struct phy_info {
	struct platform_device *pdev;
	struct device *dev;
	struct tran_device *phy_det_dev;
	struct tran_properties phy_det_props;
	struct gpio_desc *irq_gpio;
	struct delayed_work phy_det_boot;
	int irq;
	bool plug_in;
};

static int phy_det_get_property(struct tran_device *dev,
				enum tran_common_prop prop,
				union com_propval *val)
{
	int ret = 0;
	/* struct phy_info *info = tran_get_data(dev); */
	switch (prop) {
		default:
			ret = -EINVAL;
	}
	return ret;
}

static int phy_det_set_property(struct tran_device *dev,
					   enum tran_common_prop prop,
					   const union com_propval *val)
{
	int ret = 0;
	/* struct phy_info *info = tran_get_data(dev); */
	switch (prop) {
		default:
			ret = -EINVAL;
	}
	return ret;
}

static struct tran_ops phy_det_ops = {
	.get_prop = phy_det_get_property,
	.set_prop = phy_det_set_property,
};

static int phy_det_prop_init(struct phy_info *info)
{
        info->phy_det_props.alias_name = "phy_det";
	info->phy_det_dev = tran_device_register("phy_det",
						info->dev, info,
						&phy_det_ops,
						&info->phy_det_props);
	if (IS_ERR_OR_NULL(info->phy_det_dev))
		return -ENODEV;
	return 0;
}

static irqreturn_t phy_det_irq_handler(int irq, void *data)
{
	struct phy_info *info = data;
	int i, plug_in = 0;
	int pre_val = -1, cur_val = -1;
	int cnt = 0, cnt_max = 6;

	for (i = 0; i < 100; i++) {
		cur_val = gpiod_get_value_cansleep(info->irq_gpio);
		if (pre_val == cur_val) {
			if (++cnt >= cnt_max) {
				break;
			}
		} else {
			cnt = 1;
		}
		pre_val = cur_val;
		mdelay(5);
	}

	if (cnt < cnt_max) {
		pr_info("phy plug shake, return\n");
		return IRQ_HANDLED;
	}

	plug_in = !cur_val;

	if (info->plug_in == plug_in)
		return IRQ_HANDLED;

	info->plug_in = plug_in;
	pr_info("phy plug %s", info->plug_in ? "in" : "out");
	if (info->plug_in)
		tran_dev_notify(info->phy_det_dev, TRAN_DEV_NOTIFY_PHY_PLUG_IN, NULL);
	else
		tran_dev_notify(info->phy_det_dev, TRAN_DEV_NOTIFY_PHY_PLUG_OUT, NULL);
	return IRQ_HANDLED;
}

static int phy_det_init_irq(struct phy_info *info)
{
	int ret = 0;
	info->irq = gpiod_to_irq(info->irq_gpio);
	if (info->irq < 0) {
		pr_err("irq mapping fail(%d)\n", info->irq);
		return ret;
	}
	pr_info("irq = %d\n", info->irq);
	/* Request threaded IRQ */
	ret = devm_request_threaded_irq(info->dev, info->irq, NULL,
		phy_det_irq_handler, IRQF_TRIGGER_FALLING |
		IRQF_TRIGGER_RISING | IRQF_ONESHOT, "phy_det_irq",
		info);
	if (ret < 0) {
		pr_err("request thread irq fail(%d)\n", ret);
		return ret;
	}

	enable_irq_wake(info->irq);

	return 0;
}

static void phy_det_boot_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct phy_info *info = container_of(dwork,
				struct phy_info, phy_det_boot);

	if (!tc_get_boot_finish()) {
		pr_info("phy det wait boot complete!");
		schedule_delayed_work(&info->phy_det_boot, 5);
		return;
	}

	phy_det_irq_handler(info->irq, info);
}

static int phy_det_parse_dt(struct phy_info *info,
				struct device *dev)
{
	int ret = 0;
	/* struct device_node *np = dev->of_node; */
	info->irq_gpio = devm_gpiod_get(dev, "phy,intr", GPIOD_IN);
	if (IS_ERR(info->irq_gpio))
		return PTR_ERR(info->irq_gpio);
	return ret;
}

static int phy_det_probe(struct platform_device *pdev)
{
	int ret;
	struct phy_info *info = NULL;
	pr_info("enter\n");
	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);
	info->pdev = pdev;
	ret = phy_det_parse_dt(info, info->dev);
	if(ret < 0) {
		pr_err("phy_det parse dts failed, ret = %d\n", ret);
		goto err_parse_dt;
	}
	phy_det_init_irq(info);
	ret = phy_det_prop_init(info);
	if (ret < 0) {
		ret = -ENODEV;
		pr_info("register phy_det device failed\n");
		goto err_register_dev;
	}
	INIT_DELAYED_WORK(&info->phy_det_boot, phy_det_boot_work);
	pr_info("successfully\n");
	return 0;
err_register_dev:
	tran_device_unregister(info->phy_det_dev);
err_parse_dt:
	return ret;
}

static int phy_det_prepare_suspend(struct device *dev)
{
	//struct phy_info *info = dev_get_drvdata(dev);
	dev_info(dev, "%s\n", __func__);
	return 0;
}

static void phy_det_complete_resume(struct device *dev)
{
	//struct phy_info *info = dev_get_drvdata(dev);
	dev_info(dev, "%s\n", __func__);
}

static const struct dev_pm_ops phy_det_pm_ops = {
	.prepare	= phy_det_prepare_suspend,
	.complete       = phy_det_complete_resume,
};

static int phy_det_remove(struct platform_device *pdev)
{
	return 0;
}

static void phy_det_shutdown(struct platform_device *dev)
{
	return;
}

static const struct of_device_id phy_det_of_match[] = {
	{.compatible = "transsion,phy_det",},
	{},
};

MODULE_DEVICE_TABLE(of, phy_det_of_match);
static struct platform_driver phy_det_platdrv = {
	.probe = phy_det_probe,
	.remove = phy_det_remove,
	.shutdown = phy_det_shutdown,
	.driver = {
		.name = "phy_det",
		.owner = THIS_MODULE,
		.pm = &phy_det_pm_ops,
		.of_match_table = phy_det_of_match,
	},
};

static int __init phy_det_init(void)
{
	return platform_driver_register(&phy_det_platdrv);
}
module_init(phy_det_init);

static void __exit phy_det_exit(void)
{
	platform_driver_unregister(&phy_det_platdrv);
}
module_exit(phy_det_exit);

MODULE_DESCRIPTION("TRANSSION PHY DET FUNCTION");
MODULE_AUTHOR("UNKNOWN");
MODULE_VERSION("1.0.0_G");
MODULE_LICENSE("GPL");
