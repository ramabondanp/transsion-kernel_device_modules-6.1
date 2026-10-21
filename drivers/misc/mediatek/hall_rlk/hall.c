/*
* HALL driver
*
* Copyright (C) 2017 TRANSSION HOLDINGS
*
* Author: achang.zhang@reallytek.com
*
* This program is free software; you can redistribute  it and/or modify it
* under  the terms of  the GNU General  Public License as published by the
* Free Software Foundation;  either version 2 of the  License, or (at your
* option) any later version.
*
*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include "switch_class.h"
#include <linux/miscdevice.h>

#define DEBOUNCE_TIME    100
#if  IS_ENABLED(CONFIG_HOLSTER_HALL)
#define KEY_HOLSTER_HALLUP	252
#define KEY_HOLSTER_HALLDOWN	253
#endif
struct platform_device *g_tran_pdev;
static wait_queue_head_t fold_state_queue;
static int fold_state_change;

enum {
    HALL_CLOSE,
    HALL_OPEN
};

struct hall_priv {
    struct switch_dev sdev;
    struct input_dev *idev;
    struct gpio_desc *gpiod;
};

static int hall_state = HALL_OPEN;

static int tran_tp_get_boot_mode(void)
{
    struct device_node *boot_node = NULL;
    struct tag_bootmode *tags = NULL;

    boot_node = of_find_node_by_path("/chosen");
    if(!boot_node){
        pr_info(KERN_ERR "tran_get_boot_mode error");
        return -1;
    }else{
        tags = (struct tag_bootmode *)of_get_property(boot_node,"atag,boot", NULL);
        if(tags){
            pr_info("bootmode is %x",tags->bootmode);
            return tags->bootmode;
        }else{
            pr_info(KERN_ERR "tran_get_boot_mode bootmode error");
            return -1;
        }
    }
}

static ssize_t show_ultra_hall_state(struct device *dev,struct device_attribute *attr, char *buf)
{
    int length = 0;
    ssize_t result;

    wait_event_interruptible(fold_state_queue, fold_state_change);
    fold_state_change = 0;
    length = snprintf(buf, PAGE_SIZE - 1, "%d\n",
                            hall_state);
    result = (ssize_t)length;
    return result;
}

DEVICE_ATTR(ultra_hall_state, 0444, show_ultra_hall_state, NULL);

static struct attribute *ultra_hall_attributes[] = {
    &dev_attr_ultra_hall_state.attr,
    NULL
};

static struct attribute_group ultra_hall_attribute_group = {
    .attrs = ultra_hall_attributes
};

static struct miscdevice ultra_hall_misc =
{
    .minor = MISC_DYNAMIC_MINOR,
    .name = "ultra_hall_state",
};

static irqreturn_t hall_thread_factory_func(int irq_num, void *data)
{
    struct hall_priv *priv = data;
    static u8 hall_factory_status = 0;

    if (hall_state == HALL_CLOSE) {
            hall_factory_status++;
            hall_state = HALL_OPEN;
            irq_set_irq_type(irq_num, IRQF_TRIGGER_HIGH);
    } else {
            hall_factory_status++;
            hall_state = HALL_CLOSE;
            irq_set_irq_type(irq_num, IRQF_TRIGGER_LOW);
    }
    if(2 == hall_factory_status) {
	        input_report_key(priv->idev, KEY_HALLDOWN, 1);
            input_sync(priv->idev);
            input_report_key(priv->idev, KEY_HALLDOWN, 0);
            input_sync(priv->idev);
            hall_factory_status = 0;
    }
    pr_info("hall_thread_factory_func hall_state = %d[%s]\n",
                    hall_state, hall_state?"HALL_OPEN":"HALL_CLOSE");

    return IRQ_HANDLED;
}

static irqreturn_t hall_thread_func(int irq_num, void *data)
{
    struct hall_priv *priv = data;

    if (hall_state == HALL_OPEN) {
            hall_state = HALL_CLOSE;
            irq_set_irq_type(irq_num, IRQF_TRIGGER_HIGH);
            switch_set_state(&priv->sdev, hall_state);
#if  IS_ENABLED(CONFIG_HOLSTER_HALL)
            input_report_key(priv->idev, KEY_HOLSTER_HALLDOWN, 1);
            input_sync(priv->idev);
            input_report_key(priv->idev, KEY_HOLSTER_HALLDOWN, 0);
#else
            input_report_key(priv->idev, KEY_HALLDOWN, 1);
            input_sync(priv->idev);
            input_report_key(priv->idev, KEY_HALLDOWN, 0);
#endif
            input_sync(priv->idev);
            fold_state_change = 1;
            wake_up_interruptible(&fold_state_queue);
    } else {
            hall_state = HALL_OPEN;
            irq_set_irq_type(irq_num, IRQF_TRIGGER_LOW);
            switch_set_state(&priv->sdev, hall_state);
#if  IS_ENABLED(CONFIG_HOLSTER_HALL)
            input_report_key(priv->idev, KEY_HOLSTER_HALLUP, 1);
            input_sync(priv->idev);
            input_report_key(priv->idev, KEY_HOLSTER_HALLUP, 0);
#else
            input_report_key(priv->idev, KEY_HALLUP, 1);
            input_sync(priv->idev);
            input_report_key(priv->idev, KEY_HALLUP, 0);
#endif
            input_sync(priv->idev);
            fold_state_change = 1;
            wake_up_interruptible(&fold_state_queue);
    }

    pr_info("hall_thread_func hall_state = %d[%s]\n",
                    hall_state, hall_state?"HALL_OPEN":"HALL_CLOSE");
    pm_wakeup_event(&g_tran_pdev->dev, 3000);
    return IRQ_HANDLED;
}

int hall_get_state(void)
{
    return hall_state;
}
EXPORT_SYMBOL_GPL(hall_get_state);

static int hall_probe(struct platform_device *pdev)
{
    struct hall_priv *priv;
    struct device_node *node = pdev->dev.of_node;
    int error;
    int irq_num;
    int boot_mode;

    pr_info("hall_probe enter\n");

    priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
    if (!priv) {
            pr_err("Failed to allocate memory\n");
            return -ENOMEM;
    }

    priv->idev = devm_input_allocate_device(&pdev->dev);
    if (!priv->idev) {
            pr_err("Failed to allocate input device\n");
            return -ENOMEM;
    }

    priv->idev->name = pdev->name;
    priv->idev->dev.parent = &pdev->dev;
    g_tran_pdev = pdev;
    __set_bit(EV_KEY, priv->idev->evbit);
	
    boot_mode = tran_tp_get_boot_mode();
	if (boot_mode == 4) {
    input_set_capability(priv->idev, EV_KEY, KEY_HALLUP);
    input_set_capability(priv->idev, EV_KEY, KEY_HALLDOWN);
    } else {
#if  IS_ENABLED(CONFIG_HOLSTER_HALL)
    input_set_capability(priv->idev, EV_KEY, KEY_HOLSTER_HALLUP);
    input_set_capability(priv->idev, EV_KEY, KEY_HOLSTER_HALLDOWN);
#else
    input_set_capability(priv->idev, EV_KEY, KEY_HALLUP);
    input_set_capability(priv->idev, EV_KEY, KEY_HALLDOWN);
#endif
    }

    error = input_register_device(priv->idev);
    if (error) {
            pr_err("Failed to register input device\n");
            return error;
    }

    misc_register(&ultra_hall_misc);
    error = sysfs_create_group(&ultra_hall_misc.this_device->kobj, &ultra_hall_attribute_group);
    if (error) {
        pr_err("Failed to creat sysfs node\n");
    }
    init_waitqueue_head(&fold_state_queue);
    priv->sdev.name = "hall";
    error = switch_dev_register(&priv->sdev);
    if (error) {
            pr_err("Failed to register switch device\n");
            return error;
    }
    switch_set_state(&priv->sdev, HALL_OPEN);

    if (!node) {
            pr_err("Device_node is null\n");
            error = -ENOENT;
            goto err_unregister_sdev;
    }

    error = of_get_named_gpio(node, "hall_rlk,irq-gpio", 0);
    if (error < 0) {
        pr_err("invalid irq-gpio in dt: %d\n", error);
        return -EINVAL;
        goto err_unregister_sdev;
    }
    pr_info("get irq-gpio[%d] from dt\n", error);

    priv->gpiod = gpio_to_desc(error);
    if (!priv->gpiod){
        return -EINVAL;
    }

    if (boot_mode == 4) {
        pr_info("factory boot mode don't set debounce.\n");
    } else {
        error = gpiod_set_debounce(priv->gpiod, DEBOUNCE_TIME * 1000);
        if (error < 0) {
            pr_err("set debounce failed\n");
        }
    }

    irq_num = gpiod_to_irq(priv->gpiod);
    if (irq_num < 0) {
        pr_err("failed get irq num %d\n", irq_num);
        return -EINVAL;
    }

    if (boot_mode == 4) {
            error = devm_request_threaded_irq(&pdev->dev, irq_num, NULL,
                    hall_thread_factory_func, IRQF_TRIGGER_LOW | IRQF_ONESHOT,
                    dev_name(&pdev->dev), priv);
    } else {
            error = devm_request_threaded_irq(&pdev->dev, irq_num, NULL,
                    hall_thread_func, IRQF_TRIGGER_LOW | IRQF_ONESHOT,
                    dev_name(&pdev->dev), priv);
    }

    if (error) {
            pr_err("Failed to devm_request_threaded_irq\n");
            goto err_unregister_sdev;
    }
    enable_irq_wake(irq_num);

    platform_set_drvdata(pdev, priv);

    device_init_wakeup(&pdev->dev, true);
    pr_info("hall_probe ok\n");

    return 0;

err_unregister_sdev:
    switch_dev_unregister(&priv->sdev);
    return error;
}

static int hall_remove(struct platform_device *pdev)
{
    struct hall_priv *priv = platform_get_drvdata(pdev);

    switch_dev_unregister(&priv->sdev);
    sysfs_remove_group(&ultra_hall_misc.this_device->kobj, &ultra_hall_attribute_group);

    return 0;
}

static const struct of_device_id hall_of_match[] = {
    { .compatible = "mediatek,hall"},
    { },
};

static struct platform_driver hall_driver = {
    .probe = hall_probe,
    .remove = hall_remove,
    .driver = {
            .name = "hall",
            .owner = THIS_MODULE,
            .of_match_table = hall_of_match,
    },
};

static int __init hall_init(void)
{
	int ret = 0;
	pr_info("hall_init in\n");
	ret = platform_driver_register(&hall_driver);
	if (ret)
	{
		printk("Failed to register platform driver\n");
		return ret;
	}
	pr_info("hall_init out\n");
	return ret;
}

static void  __exit hall_exit(void)
{
	pr_info("hall_exit in\n");
	platform_driver_unregister(&hall_driver);
	pr_info("hall_exit out\n");
}

module_init(hall_init);
module_exit(hall_exit);

/* Module information */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("<achang.zhang@reallytek.com>");
MODULE_DESCRIPTION("HALL driver");

