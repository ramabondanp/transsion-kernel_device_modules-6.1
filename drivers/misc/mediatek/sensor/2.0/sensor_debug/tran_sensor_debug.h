// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Transsion Inc.
 */
#ifndef __TRAN_SENSOR_DEBUG__
#define __TRAN_SENSOR_DEBUG__

#include "../core/hf_sensor_type.h"

struct transsion_sensor_debug_info {
	uint32_t sensor_type;
	uint32_t debug_enable;
};

struct tran_sensor_debug_device {
	int (*tran_sensor_debug)(struct tran_sensor_debug_device *trdev,
				uint32_t sensor_type, uint32_t debug_enable);
};

int tran_sensor_debug_register(struct tran_sensor_debug_device *device);
void tran_sensor_debug_unregister(struct tran_sensor_debug_device *device);
#endif /* __TRAN_SENSOR_DEBUG__ */
