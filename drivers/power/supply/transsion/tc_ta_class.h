// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#ifndef __LINUX_TC_TA_CLASS_H__
#define __LINUX_TC_TA_CLASS_H__

struct tc_ta_classdev;

struct tc_ta_device_ops {
	int (*get_verinfo)(struct tc_ta_classdev *ttc,u8 *info, u32 length);
	int (*get_fwcode)(struct tc_ta_classdev *ttc,u8 *code, u32 length);
	int (*get_mfinfo)(struct tc_ta_classdev *ttc,u8 *code, u32 length);
	int (*get_datecode)(struct tc_ta_classdev *ttc,u8 *code, u32 length);
	int (*get_min_voltage)(struct tc_ta_classdev *ttc, u32 *volt);
	int (*get_max_voltage)(struct tc_ta_classdev *ttc, u32 *volt);
	int (*get_min_current)(struct tc_ta_classdev *ttc, u32 *curr);
	int (*get_max_current)(struct tc_ta_classdev *ttc, u32 *curr);
	int (*get_output_voltage)(struct tc_ta_classdev *ttc, u32 *volt);
	int (*get_output_current)(struct tc_ta_classdev *ttc, u32 *curr);
	int (*get_status)(struct tc_ta_classdev *ttc, u32 *status);
	int (*get_temp1)(struct tc_ta_classdev *ttc, u32 *degree);
	int (*get_temp2)(struct tc_ta_classdev *ttc, u32 *degree);
	int (*set_output_control)(struct tc_ta_classdev *ttc,u32 control);
	int (*set_mode)(struct tc_ta_classdev *ttc, u32 mode);
	int (*set_voltage)(struct tc_ta_classdev *ttc, u32 volt);
	int (*set_current)(struct tc_ta_classdev *ttc, u32 curr);
	int (*suspend)(struct tc_ta_classdev *ttc);
	int (*resume)(struct tc_ta_classdev *ttc);
	int (*set_voltage_current)(struct tc_ta_classdev *ttc, u32 volt,u32 curr);
	int (*get_output_voltage_current)(struct tc_ta_classdev *ttc,u32 *volt, u32 *curr);
	int (*authentication)(struct tc_ta_classdev *ttc);
	int (*enable_wdt)(struct tc_ta_classdev *ttc, bool en);
	int (*set_wdt)(struct tc_ta_classdev *ttc, u32 ms);
	int (*get_power_limit)(struct tc_ta_classdev *ttc, u32 *power);
	int (*get_max_power_duration)(struct tc_ta_classdev *ttc,u32 *time);
	int (*get_output_control_support)(struct tc_ta_classdev *ptc, bool *support);
};

struct tc_ta_classdev {
	const char *name;
	struct device *dev;
	const struct tc_ta_device_ops *ops;
	const struct attribute_group	**groups;
};

/* API List */
extern int tc_ta_device_get_verinfo(struct tc_ta_classdev *ttc,u8 *info, u32 length);
extern int tc_ta_device_get_fwcode(struct tc_ta_classdev *ttc,u8 *code, u32 length);
extern int tc_ta_device_get_mfinfo(struct tc_ta_classdev *ttc,u8 *code, u32 length);
extern int tc_ta_device_get_datecode(struct tc_ta_classdev *ttc,u8 *code, u32 length);
extern int tc_ta_device_get_min_voltage(struct tc_ta_classdev *ttc,u32 *volt);
extern int tc_ta_device_get_max_voltage(struct tc_ta_classdev *ttc,u32 *volt);
extern int tc_ta_device_get_min_current(struct tc_ta_classdev *ttc,u32 *curr);
extern int tc_ta_device_get_max_current(struct tc_ta_classdev *ttc,u32 *curr);
extern int tc_ta_device_get_output_voltage(struct tc_ta_classdev *ttc,u32 *volt);
extern int tc_ta_device_get_output_current(struct tc_ta_classdev *ttc,u32 *curr);
extern int tc_ta_device_get_status(struct tc_ta_classdev *ttc, u32 *status);
extern int tc_ta_device_get_temp1(struct tc_ta_classdev *ttc, u32 *degree);
extern int tc_ta_device_get_temp2(struct tc_ta_classdev *ttc, u32 *degree);
extern int tc_ta_device_set_output_control(struct tc_ta_classdev *ttc,u32 control);
extern int tc_ta_device_set_mode(struct tc_ta_classdev *ttc, u32 mode);
extern int tc_ta_device_set_voltage(struct tc_ta_classdev *ttc, u32 volt);
extern int tc_ta_device_set_current(struct tc_ta_classdev *ttc, u32 curr);
extern int tc_ta_device_set_voltage_current(struct tc_ta_classdev *ttc,u32 volt, u32 curr);
extern int tc_ta_device_get_output_voltage_current(struct tc_ta_classdev *ttc, u32 *volt, u32 *curr);
extern int tc_ta_device_authentication(struct tc_ta_classdev *ttc);
extern int tc_ta_device_enable_wdt(struct tc_ta_classdev *ttc, bool en);
extern int tc_ta_device_set_wdt(struct tc_ta_classdev *ttc, u32 ms);

extern int tc_ta_classdev_register(struct device *parent,struct tc_ta_classdev *ttc);
extern void tc_ta_classdev_unregister(struct tc_ta_classdev *ttc);
extern int devm_tc_ta_classdev_register(struct device *parent,struct tc_ta_classdev *ttc);
extern struct tc_ta_classdev *tc_ta_device_get_by_name(const char *name);
extern int tc_ta_device_get_power_limit(struct tc_ta_classdev *ttc, u32 *power);
extern int tc_ta_device_get_max_power_duration(struct tc_ta_classdev *ttc,u32 *time);
extern int tc_ta_device_get_output_control_support(struct tc_ta_classdev *ptc, bool *support);
#endif /* __LINUX_TC_TA_CLASS_H__ */
