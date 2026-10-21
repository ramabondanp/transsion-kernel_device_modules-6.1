/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * aw95016.h   aw95016 martix key
 *
 * Copyright (c) 2021 Shanghai Awinic Technology Co., Ltd. All Rights Reserved
 *
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _AW95016_H_
#define _AW95016_H_

#define AW95016_ID 0x80
#define AW95016_PORT_MAX (0x10) /* 16 */
#define AW95016_INT_MASK (0xFFFF)

#define AWINIC_DEBUG		1

#ifdef AWINIC_DEBUG
#define AW_INFO(fmt, args...)	printk("[aw95016][info] %s:"fmt"\n", __func__, ##args)
#define AW_DEBUG(fmt, args...)	printk("[aw95016][debug] %s:"fmt"\n", __func__, ##args)
#define AW_ERROR(fmt, args...)	printk("[aw95016][error] %s:"fmt"\n", __func__, ##args)
#else
#define AW_INFO(fmt, args...)	printk("[aw95016][info] %s:"fmt"\n", __func__, ##args)
#define AW_DEBUG(fmt, args...)
#define AW_ERROR(fmt, args...)	printk("[aw95016][error] %s:"fmt"\n", __func__, ##args)
#endif

#define AW95016_PORT_MASK (0xF0)
#define AW95016_PORT_SHIFT_MASK (0x0F)

#define AW95016_PORT_GOURP(port) ((port&AW95016_PORT_MASK)>>4)
#define AW95016_PORT_SHIFT(port) (port&AW95016_PORT_SHIFT_MASK)

/* input state*/
#define P0_INPUT		0x00
#define P1_INPUT		0x01
/* output state */
#define P0_OUTPUT		0x02
#define P1_OUTPUT		0x03
/* port direction */
#define P0_DIR			0x06
#define P1_DIR			0x07
/* pull enable */
#define P0_PEN          0x0E
#define P1_PEN          0x0F
/* pull mode */
#define P0_PMD          0x10
#define P1_PMD          0x11
/* output mode */
#define P0_DOMD			0x16
#define P1_DOMD			0x17
/* interrup mask */
#define P0_MAK          0x12
#define P1_MAK          0x13
/* interrup state */
#define P0_INTST        0x14
#define P1_INTST        0x15

#define RESET			0x70

enum aw95016_port {
    P0_0 = 0x00,
    P0_1,
    P0_2,
    P0_3,
    P0_4,
    P0_5,
    P0_6,
    P0_7 = 0x07,
    P1_0 = 0x10,
    P1_1,
    P1_2,
    P1_3,
    P1_4,
    P1_5,
    P1_6,
    P1_7 = 0x17,
};

struct aw95016_gpio_data {
    bool pull_en;
    bool pull_state;
    bool direction;
	bool irq_enabled;
    bool irq_registered;
	unsigned int mask_shift;
    char pin_name[8];
	struct work_struct *irq_work;
    struct mutex state_lock;
};

struct aw95016 {
	int irq_gpio;
	int irq_num;
	int irq_enabled_count;
	int rst_gpio;
    int sim_gpio;
	bool pm_suspended;
	unsigned char chipid;
	/* enabled irq record */
	unsigned int interrupt_mask;

	struct i2c_client *client;
	struct device *dev;
    /* i2c bus lock */
	struct mutex bus_lock;
    struct mutex port_lock;
    struct workqueue_struct *irq_wq;
	struct regulator *power_supply;
};

enum aw95016_gpio_dir {
	AW95016_GPIO_INPUT = 0,
	AW95016_GPIO_OUTPUT = 1,
};

#endif
