/*
 * switch_class.h
 *
 * Copyright (C) 2008 Google, Inc.
 * Author: Mike Lockwood <lockwood@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
*/

/***
    Mobile phone startup mode list
    NORMAL_BOOT = 0,
    META_BOOT = 1,
    RECOVERY_BOOT = 2,
    SW_REBOOT = 3,
    FACTORY_BOOT = 4,
    ADVMETA_BOOT = 5,
    ATE_FACTORY_BOOT = 6,
    ALARM_BOOT = 7,
    KERNEL_POWER_OFF_CHARGING_BOOT = 8,
    LOW_POWER_OFF_CHARGING_BOOT = 9,
    FASTBOOT = 99,
    DOWNLOAD_BOOT = 100,
    UNKNOWN_BOOT
***/
struct tag_bootmode {
	u32 size;
	u32 tag;
	u32 bootmode;
	u32 boottype;
};

struct switch_dev {
	const char	*name;
	struct device	*dev;
	int		index;
	int		state;

	ssize_t	(*print_name)(struct switch_dev *sdev, char *buf);
	ssize_t	(*print_state)(struct switch_dev *sdev, char *buf);
	int (*set_state)(struct switch_dev *sdev, int state);
};

extern void switch_set_state(struct switch_dev *sdev, int state);
extern int switch_dev_register(struct switch_dev *sdev);
extern void switch_dev_unregister(struct switch_dev *sdev);

