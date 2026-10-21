/*
 * (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author        Notes
 * 2021-12-23     Zhu Shiqiang  Initialize.
 */

/**
 * @brief   IPC Interface
 * @date    2021-12-23
 */

#ifndef __IPC_DEV_H__
#define __IPC_DEV_H__

#include <defs.h>
#include "ipc.h"
#include "ipc_config.h"

struct ipc_vdev {
	struct ipc_dev *dev; /* ipc device data struct */
	struct ipc_channel chan;
	wait_queue_head_t ipc_event;
	int ipc_event_flag;
};

struct ipc_dev {
	char *name;
	char *hclk_name;
	struct clk *ipc_hclk;
	uint32_t irq;
	uint32_t ack_mode;
	struct rt_event event;
	mutex_t send_lock;
	mutex_t recv_lock;
	int ack_flag;
	uint32_t irq_cnt;
	struct rt_device device;
	struct ipc_vdev *vdev[IPC_VDEV_MAX];
	struct chan_config dev_cfg[];
};

struct ipc_vdev *ipc_dev_open(struct ipc_dev *dev, struct user_config *config);
int ipc_dev_close(struct ipc_vdev *vdev);
int ipc_dev_send(struct ipc_vdev *vdev, struct ipc_send_msg *msg);
int ipc_dev_recv(struct ipc_vdev *vdev, struct ipc_recv_msg *msg);
#endif /* __IPC_DEV_H__*/
