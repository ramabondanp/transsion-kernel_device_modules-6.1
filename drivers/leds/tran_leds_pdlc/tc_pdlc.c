// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Transsion Inc.
 #
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/iio/consumer.h>
#include <linux/iio/iio.h>
#include <linux/preempt.h>
#include <linux/notifier.h>
#include <linux/leds.h>
#include <uapi/linux/sched/types.h>
#include <linux/kthread.h>
#include "tc_pdlc.h"

void pdlc_light_control(int loop_count, int type, int brightness, int res) __weak;

static void tran_pdlc_light_control(int loop_count, int type, int brightness,
				    int res)
{
	if (pdlc_light_control)
		pdlc_light_control(loop_count, type, brightness, res);
}

/*******************************************************************************
 *
 * pdlc control interface
 *
 ******************************************************************************/
static void tran_pdlc_boost_ctl(struct pdlc_data *pdlc, bool enable)
{
	struct pinctrl *pinctrl = pdlc->pinctrl;
	
	dev_info(pdlc->pdlc_dev, "%s:enable:%d\n", __func__, enable);
	if (enable)
		pinctrl_select_state(pinctrl, pdlc->gpio_boost_on_state);
	else
		pinctrl_select_state(pinctrl, pdlc->gpio_boost_off_state);
}

static void tran_pdlc_enable_func(struct pdlc_data *pdlc)
{	
	dev_info(pdlc->pdlc_dev, "%s\n", __func__);
	tran_pdlc_boost_ctl(pdlc, true);
	tran_pdlc_light_control(0, true, 0, 0);
}

static void pdlc_boost_off_func(struct work_struct *work)
{
	struct pdlc_data *pdlc = container_of(to_delayed_work(work),
			struct pdlc_data, pdlc_boost_off_work);

	dev_info(pdlc->pdlc_dev, "%s:enter\n", __func__);
	pdlc->boost_off_onging = true;
	if(!atomic_read(&pdlc->pdlc_eanble))
		tran_pdlc_boost_ctl(pdlc, false);
	
	pdlc->boost_off_onging = false;
}

/*******************************************************************************
 *
 * sysfs attribute group: tran_pdlc_cmd store/show
 *
 ******************************************************************************/
static ssize_t store_tran_pdlc_cmd(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct pdlc_data *pdlc = dev->driver_data;
	int num[10];

	memset(num, 0, 10);
	if (sscanf(buf, "%x %x %x %x %x %x", &num[0], &num[1], &num[2], &num[3],&num[4],&num[5]) == 6) {
		dev_err(pdlc->pdlc_dev,"%s 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",__func__,num[0], num[1], num[2], num[3],num[4],num[5]);
		pdlc->latest_cmd = num[1];
		//tran_pdlc_boost_ctl(pdlc, true);
		switch(num[0]) {//type
			case 0x00:
				switch(num[1]) {//mode
					case 0x00://close
						atomic_set(&pdlc->pdlc_eanble, 0);
						if(!pdlc->boost_off_onging) {
							tran_pdlc_light_control(0, false, 0, 0);
							schedule_delayed_work(&pdlc->pdlc_boost_off_work, 4000);
						}
						break;
					case 0x01://open
						atomic_set(&pdlc->pdlc_eanble, 1);
						tran_pdlc_enable_func(pdlc);
						break;
					default:
						pdlc->latest_cmd = 0;
						dev_err(pdlc->pdlc_dev,"%s store(no para) %d\n", __func__,__LINE__);
						break;
				}
				break;
			default:
				dev_err(pdlc->pdlc_dev,"%s(no para) %d\n", __func__,__LINE__);
				break;
		}
	}

	return size;
}

static ssize_t show_tran_pdlc_cmd(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct pdlc_data *pdlc = dev->driver_data;

	return sprintf(buf, "%d\n",  pdlc->latest_cmd);
}

/*******************************************************************************
 *
 * pdlc parse func
 *
 ******************************************************************************/
static int tran_pdlc_parse_dts(struct pdlc_data *pdlc, struct device_node *np)
{
	int ret = 0;

	pdlc->support_pdlc_hw_detect = of_property_read_bool(np, "support_pdlc_hw_detect");
	dev_info(pdlc->pdlc_dev, "dts support_pdlc_hw_detect = %d\n", pdlc->support_pdlc_hw_detect);
	
	if(pdlc->support_pdlc_hw_detect) {
		pdlc->detect_gpio = of_get_named_gpio(np, "hw-detect-gpio", 0);
		if (gpio_is_valid(pdlc->detect_gpio)) {
			ret = devm_gpio_request_one(pdlc->pdlc_dev,
						pdlc->detect_gpio,
						GPIOF_IN,
						"pdlc_detect_gpio");
			if (ret) {
				dev_err(pdlc->pdlc_dev,
					"%s: gpio request failed\n", __func__);
				return ret;	
			}	
		}
	}

	return 0;
}

/*******************************************************************************
 *
 * pdlc gpio pinctl
 *
 ******************************************************************************/
static void tran_pdlc_pinctrl_init(struct pdlc_data *pdlc)
{
	struct pinctrl *pinctrl = pdlc->pinctrl;
	//struct tc_pdlc_pinctrl *pinctrl_data = &pdlc->pinctrl_data;

	pdlc->gpio_boost_on_state = pinctrl_lookup_state(pinctrl, "tran_pdlc_boost_on");
	if (IS_ERR(pdlc->gpio_boost_on_state))
		dev_err(pdlc->pdlc_dev, "Cannot find gpio_boost_on_state\n");

	pdlc->gpio_boost_off_state = pinctrl_lookup_state(pinctrl, "tran_pdlc_boost_off");
	if (IS_ERR(pdlc->gpio_boost_off_state))
		dev_err(pdlc->pdlc_dev, "Cannot find gpio_boost_off_state\n");
}

static const struct attribute_group *pdlc_group[] = {
	//&activate_group,
	//&breath_time_group,
	NULL
};

static const struct of_device_id tran_pdlc_of_ids[] = {
	{.compatible = "transsion,tran_pdlc",},
	{},
};
MODULE_DEVICE_TABLE(of, tran_pdlc_of_ids);

static DEVICE_ATTR(tran_pdlc_cmd, 0664,
	show_tran_pdlc_cmd, store_tran_pdlc_cmd);

static int tran_pdlc_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct pdlc_data *tran_pdlc = NULL;
	struct device_node *np = pdev->dev.of_node;
	struct kobject *kobj = NULL;

    TRAN_PDLC_ERR("enter");
	
    tran_pdlc = kzalloc(sizeof(*tran_pdlc), GFP_KERNEL);
    if (!tran_pdlc || !np) {
		TRAN_PDLC_ERR("no enough mem for tran_pdlc!");
		ret = -ENOMEM;
        goto out;
    }

	platform_set_drvdata(pdev, tran_pdlc);
	tran_pdlc->pdlc_dev = &pdev->dev;
	tran_pdlc->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR_OR_NULL(tran_pdlc->pinctrl)) {
		ret = PTR_ERR(tran_pdlc->pinctrl);
		TRAN_PDLC_ERR("Cannot find tran_pdlc->pinctrl!\n");
		goto out;
	}

	tran_pdlc_parse_dts(tran_pdlc, np);
	
	if(tran_pdlc->support_pdlc_hw_detect) {
		if(gpio_get_value(tran_pdlc->detect_gpio) != 0)
			goto out;
	}

	tran_pdlc_pinctrl_init(tran_pdlc);
	tran_pdlc->pdlc_led_dev.name = "tran_pdlc";
	tran_pdlc->pdlc_led_dev.groups = pdlc_group;
	tran_pdlc->pdlc_led_dev.brightness = 1;
	//tran_pdlc->pdlc_breath_time = 5000;
	ret = devm_led_classdev_register(&pdev->dev, &tran_pdlc->pdlc_led_dev);
	if (ret < 0) {
		TRAN_PDLC_ERR("led class register fail\n");
		goto out;
	}

	//tran_pdlc->led_dev = tc_led_device_register("tran_pdlc", tran_pdlc->pdlc_dev, tran_pdlc, &leds_pdlc_ops, NULL);
	ret = device_create_file(&pdev->dev, &dev_attr_tran_pdlc_cmd);
	if (ret < 0) {
		pr_err("error creating tran_pdlc_cmd\n");
		goto out;
	}

	kobj = kobject_create_and_add("pdlc", NULL);
	if (!kobj) {
		TRAN_PDLC_ERR("sysfs_create_group\n");
		goto out;
	}

	ret = sysfs_create_link(kobj,&pdev->dev.kobj,"pdlc");
	if(ret < 0){
		TRAN_PDLC_ERR("sysfs_create_link failed\n");
		goto out;
	}

	INIT_DELAYED_WORK(&tran_pdlc->pdlc_boost_off_work, pdlc_boost_off_func);

	TRAN_PDLC_ERR("probe completed successfully");
	return 0;

out:
	kfree(tran_pdlc);
	return ret;
}

static int tran_pdlc_remove(struct platform_device *pdev)
{
	return 0;
}

static void tran_pdlc_shutdown(struct platform_device *pdev)
{
	return;
}

static struct platform_driver tran_pdlc = {
	.driver = {
		.name = "pdlc",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(tran_pdlc_of_ids),
	},
	.probe = tran_pdlc_probe,
	.remove = tran_pdlc_remove,
	.shutdown = tran_pdlc_shutdown,
};
module_platform_driver(tran_pdlc);

MODULE_SOFTDEP("pre: aw86224_light");
MODULE_DESCRIPTION("Transsion PDLC Driver");
MODULE_AUTHOR("Transsion, Inc.");
MODULE_LICENSE("GPL");
