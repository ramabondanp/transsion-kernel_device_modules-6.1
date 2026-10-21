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

#define DEBOUNCE_TIME    100
#define P1_4 0x14

#ifdef KEY_HOLSTER_HALLUP
#else
#define KEY_HOLSTER_HALLUP	252
#endif

#ifdef KEY_HOLSTER_HALLDOWN
#else
#define KEY_HOLSTER_HALLDOWN	253
#endif

extern int aw95016_irq_enable(unsigned int port, int enable);
extern int aw95016_register_irq(unsigned int port, void* call_back);
extern int aw95016_get_level(unsigned int port, int *level);
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
static struct hall_priv *g_hall_dev = NULL;

static int tran_tp_get_boot_mode(void)
{
    struct device_node *boot_node = NULL;
    struct tag_bootmode *tags = NULL;

    boot_node = of_find_node_by_path("/chosen");
    if(!boot_node) {
        pr_info(KERN_ERR "tran_get_boot_mode error");
        return -1;
    } else {
        tags = (struct tag_bootmode *)of_get_property(boot_node,"atag,boot", NULL);
        if(tags) {
            pr_info("bootmode is %x",tags->bootmode);
            return tags->bootmode;
        } else {
            pr_info(KERN_ERR "tran_get_boot_mode bootmode error");
            return -1;
        }
    }
}

static irqreturn_t hall_thread_factory_func(int irq_num, void *data)
{
    struct hall_priv *priv = data;
    static u8 hall_factory_status = 0;

    if (hall_state == HALL_CLOSE) {
        hall_factory_status++;
        hall_state = HALL_OPEN;
    } else {
        hall_factory_status++;
        hall_state = HALL_CLOSE;
    }
    if(2 == hall_factory_status) {
        input_report_key(priv->idev, KEY_HOLSTER_HALLDOWN, 1);
        input_sync(priv->idev);
        input_report_key(priv->idev, KEY_HOLSTER_HALLDOWN, 0);
        input_sync(priv->idev);
        hall_factory_status = 0;
    }
    pr_info("hall_thread_factory_func hall_factory_status = %d[%s]\n",
            hall_factory_status, hall_state?"HALL_OPEN":"HALL_CLOSE");

    return IRQ_HANDLED;
}

static irqreturn_t hall_thread_func(int irq_num, void *data)
{
	struct hall_priv *priv = data;
	int irq_pin_level = -1;

	/* check irq level , high close low open */
	aw95016_get_level(P1_4, &irq_pin_level);
	if(irq_pin_level == 0) {
		hall_state = HALL_CLOSE;
		switch_set_state(&priv->sdev, hall_state);
		input_report_key(priv->idev, KEY_HOLSTER_HALLDOWN, 1);
		input_sync(priv->idev);
		input_report_key(priv->idev, KEY_HOLSTER_HALLDOWN, 0);
		input_sync(priv->idev);
	} else if(irq_pin_level == 1) {
		hall_state = HALL_OPEN;
		switch_set_state(&priv->sdev, hall_state);
		input_report_key(priv->idev, KEY_HOLSTER_HALLUP, 1);
		input_sync(priv->idev);
		input_report_key(priv->idev, KEY_HOLSTER_HALLUP, 0);
		input_sync(priv->idev);
	}

	pm_wakeup_event(&priv->idev->dev, 3000);

	pr_info("[holster]: hall_thread_func hall_state = %d[%s]\n",
			hall_state, hall_state?"HALL_OPEN":"HALL_CLOSE");

	return IRQ_HANDLED;
}

static void holster_irq_handle(struct work_struct *work)
{

    if(g_hall_dev) {
        hall_thread_func(0, g_hall_dev);
    }
}

static void factory_holster_irq_handle(struct work_struct *work)
{

    if(g_hall_dev) {
        hall_thread_factory_func(0,g_hall_dev);
    }
}

int holster_hall_get_state(void)
{
    return hall_state;
}
EXPORT_SYMBOL_GPL(holster_hall_get_state);

static int hall_probe(struct platform_device *pdev)
{
    struct hall_priv *priv;
    struct device_node *node = pdev->dev.of_node;
    int error;
    int boot_mode;

    pr_info("[holster]: hall_probe enter\n");

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

    __set_bit(EV_KEY, priv->idev->evbit);
    input_set_capability(priv->idev, EV_KEY, KEY_HOLSTER_HALLUP);
    input_set_capability(priv->idev, EV_KEY, KEY_HOLSTER_HALLDOWN);

    error = input_register_device(priv->idev);
    if (error) {
        pr_err("Failed to register input device\n");
        return error;
    }
    priv->sdev.name = "holster_hall";
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

    /*
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
    }*/

    boot_mode = tran_tp_get_boot_mode();
    if (boot_mode == 4) {
        pr_info("factory boot mode don't set debounce.\n");
    } else {
        /*
            error = gpiod_set_debounce(priv->gpiod, DEBOUNCE_TIME * 1000);
            if (error < 0) {
                pr_err("set debounce failed\n");
            }*/
    }

    /*
    irq_num = gpiod_to_irq(priv->gpiod);
    if (irq_num < 0) {
        pr_err("failed get irq num %d\n", irq_num);
        return -EINVAL;
    }*/

    g_hall_dev = priv;
    if (boot_mode == 4) {
        aw95016_register_irq(P1_4, factory_holster_irq_handle);
        /*
            error = devm_request_threaded_irq(&pdev->dev, irq_num, NULL,
                    hall_thread_factory_func, IRQF_TRIGGER_LOW | IRQF_ONESHOT,
                    dev_name(&pdev->dev), priv);
            */
    } else {
        aw95016_register_irq(P1_4, holster_irq_handle);
        /*
            error = devm_request_threaded_irq(&pdev->dev, irq_num, NULL,
                    hall_thread_func, IRQF_TRIGGER_LOW | IRQF_ONESHOT,
                    dev_name(&pdev->dev), priv);
        */
    }
    aw95016_irq_enable(P1_4, 1);

    /*
    if (error) {
            pr_err("Failed to devm_request_threaded_irq\n");
            goto err_unregister_sdev;
    }
    enable_irq_wake(irq_num);
    */
    platform_set_drvdata(pdev, priv);

    device_init_wakeup(&priv->idev->dev, true);

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
    return 0;
}

static const struct of_device_id hall_of_match[] = {
    { .compatible = "mediatek,hall_holster"},
    { },
};

static struct platform_driver hall_driver = {
    .probe = hall_probe,
    .remove = hall_remove,
    .driver = {
        .name = "hall_holster",
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
