/*
 * (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * Change Logs:
 * Date           Author        Notes
 * 2022-01-21     Zhu Shiqiang  Initialize.
 */

/**
 * @brief   IPC Face Header
 * @date    2022-01-21
 */

#ifndef __IPC_DEV_H__
#define __IPC_DEV_H__

#include "ipc.h"

struct ipc_channel;
struct ipc_vdev {
	struct ipc_dev *dev; /* ipc device data struct */
	struct ipc_channel *chan;
	wait_queue_head_t read_queue;
	bool recv_flag;
};

struct ipc_dev {
	struct mutex send_lock;
	struct mutex recv_lock;
	bool ack_flag;
	uint32_t irq_cnt;
	uint32_t ack_mode;
	int gpio; /* ipc interrupt gpio */
	struct device *dev;
	struct ipc_vdev *vdev[IPC_VDEV_MAX];
	struct chan_config *dev_cfg;
};

struct ipc_vdev *ipc_dev_open(struct ipc_dev *dev, struct user_config *config);
int ipc_dev_close(struct ipc_vdev *vdev);
int ipc_dev_send(struct ipc_vdev *vdev, struct ipc_send_msg *msg);
int ipc_dev_recv(struct ipc_vdev *vdev, struct ipc_recv_msg *msg);
#endif /* __IPC_DEV_H__*/
