/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
/**
 * @brief   header of  communication ipc interface
 * @date    2022-2-28
 */

#ifndef __COMM_IPC_H__
#define __COMM_IPC_H__

struct comm_ipc_vdev_info {
	struct ipc_vdev *send_vdev;
	struct ipc_vdev *recv_vdev;
};

int comm_ipc_init(void **send_dev, void **recv_dev);
void comm_ipc_exit(void *send_dev, void *recv_dev);
int comm_ipc_send(void *send_dev, void *buffer, u32 buf_size);
int comm_ipc_recv(void *recv_dev, void *buffer, u32 buf_size);
int comm_ipc_readdata(u32 addr, char *buffer, u32 buf_size);
int comm_ipc_writedata(u32 addr, char *buffer, u32 buf_size);
#endif /* __COMM_IPC_H__ */
