// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#ifndef LINUX_TC_LED_CLASS_H
#define LINUX_TC_LED_CLASS_H

#include <linux/kernel.h>
#include <linux/device.h>

struct tc_led_properties {
	const char *alias_name;
};

struct tc_led_device {
	struct tc_led_properties props;
	const struct tc_led_ops *ops;
	struct platform_device *pdev;
	struct srcu_notifier_head evt_nh;
	struct device dev;
	struct led_classdev cdev;
	const struct attribute_group *dev_groups;
};

struct tc_led_ops {
	int (*show_tran_led_cmd)(struct tc_led_device *dev, char *buf);
	int (*store_tran_led_cmd)(struct tc_led_device *dev, const char *cmd);
	int (*show_tran_led_check)(struct tc_led_device *dev, char *buf);
	int (*store_tran_led_check)(struct tc_led_device *dev, const char *cmd);
};

struct tc_led_device *tc_led_device_register(const char *name,
		struct device *parent, void *devdata,
		const struct tc_led_ops *ops,
		const struct tc_led_properties *props);

extern int tc_led_dev_show_tran_led_cmd(struct tc_led_device *dev, char *buf);
extern int tc_led_dev_store_tran_led_cmd(struct tc_led_device *dev, const char *cmd);
extern int tc_led_dev_show_tran_led_check(struct tc_led_device *dev, char *buf);
extern int tc_led_dev_store_tran_led_check(struct tc_led_device *dev, const char *cmd);
extern struct tc_led_device *get_led_device(void);

#define to_led_device(obj) container_of(obj, struct tc_led_device, dev)

#endif