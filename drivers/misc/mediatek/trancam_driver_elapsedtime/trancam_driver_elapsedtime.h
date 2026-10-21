/**
 * Transsion Top Secret
 * Copyright (C) 2022 Transsion Inc.
 *
 * @Description : interface of statistical time interval for camera driver
 *
 * @Author      : wei.miao@transsion.com
 * @Version     : 2022-08-16
 *
*/
#ifndef __TRANCAM_DRV_ELAPSEDTIME_UTILS_H
#define __TRANCAM_DRV_ELAPSEDTIME_UTILS_H

#include <linux/module.h>

extern void (*perf_elapsedtime_init_fp)(struct timespec64 *ptv);
extern void (*perf_elapsedtime_fp)(struct timespec64 *ptv, char *ta, int idx);
#endif
