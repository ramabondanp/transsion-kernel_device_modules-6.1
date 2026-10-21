/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Change Logs:
 * Date           Author         Notes
 * 2022-01-20     yanghua     Initialize.
 */

/**
 * @brief   sdiobridge kernel internel header file
 * @date    2021-01-20
 */

#ifndef __SDIOBRIDGE_INTERNEL_H__
#define __SDIOBRIDGE_INTERNEL_H__

#include <linux/types.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mutex.h>
#include <linux/notifier.h>

#define KB					(1024)
#define MB					(KB * KB)
#define XFER_MAX_BUFFER_LEN			(2 * MB)
#define KERNEL_MEMCPY_MAX_SIZE			512

#define MAX_RECORD_NUM				8

struct xfer_record {
	u32 addr;
	u32 len;
	bool is_write;
	int ret;
};

struct sdiobridge_priv {
	struct mutex xfer_lock;
	struct mutex ioctl_lock;
	u8 *ioc_xfer_buf;
	u8 *kernel_xfer_buf;
	u32 cur_rec_id;
	struct notifier_block pm_nb;
	struct xfer_record records[MAX_RECORD_NUM];
	u32 version;
};

extern struct platform_driver mmc_pwrseq_sdio2axi_driver;

#endif /* __SDIOBRIDGE_INTERNEL_H__ */
