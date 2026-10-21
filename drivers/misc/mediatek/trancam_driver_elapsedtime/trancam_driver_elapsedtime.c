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

#include "trancam_driver_elapsedtime.h"

#define TRANCAM_DRV_ELAPSEDTIME_LOG 1

void (*perf_elapsedtime_init_fp)(struct timespec64 *ptv);
EXPORT_SYMBOL_GPL(perf_elapsedtime_init_fp);

void (*perf_elapsedtime_fp)(struct timespec64 *ptv, char *ta, int idx);
EXPORT_SYMBOL_GPL(perf_elapsedtime_fp);

#if TRANCAM_DRV_ELAPSEDTIME_LOG
void trancam_drv_elapsedtime_init(struct timespec64 *ptv)
{
	ktime_get_real_ts64(ptv);
}

void trancam_drv_elapsedtime(struct timespec64 *ptv, char *tag, int idx)
{
	struct timespec64 now, diff;

	ktime_get_real_ts64(&now);
	diff = timespec64_sub(now, *ptv);

	pr_info("[PerformanceFlow][%s] idx = %d, Profile = %llu us\n", tag, idx, timespec64_to_ns(&diff)/1000);
}

#else
void trancam_drv_elapsedtime_init(struct timespec64 *ptv) {}
void trancam_drv_elapsedtime(struct timespec64 *ptv, char *tag, int idx) {}
#endif

static int __init trancam_elapsedtime_utils_init(void)
{
	perf_elapsedtime_init_fp = trancam_drv_elapsedtime_init;
	perf_elapsedtime_fp =  trancam_drv_elapsedtime;
	pr_info("[PerformanceFlow]module_init\n");
	return 0;
}

static void __exit trancam_elapsedtime_utils_exit(void)
{
	perf_elapsedtime_init_fp = NULL;
	perf_elapsedtime_fp = NULL;
}

module_init(trancam_elapsedtime_utils_init);
module_exit(trancam_elapsedtime_utils_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TRANSSION CAM_DRV ELAPSEDTIME_UTILS");
MODULE_AUTHOR("TRANSSION Inc.");
