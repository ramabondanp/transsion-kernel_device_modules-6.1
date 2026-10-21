/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * Change Logs:
 * Date           Author           Notes
 * 2022-02-24     Zhu Shiqiang     Initialize.
 */

/**
 * @brief   ipc dev header file
 * @date    2022-02-24
 */

#ifndef __IPC_H__
#define __IPC_H__

#include "ipc_config.h"
#include "ipc_ring_buffer.h"

/* ipc work mode parameters */
#define IPCDEV_FOREVER	-1
#define IPCDEV_ASYNC	0
#define IPC_VDEV_MAX	4

enum ipc_cmd {
	IPC_OPEN_CMD,
	IPC_CLOSE_CMD,
	IPC_SEND_CMD,
	IPC_RECV_CMD,
	IPC_CMD_MAX
};

/* struct for msg send */
struct ipc_send_msg {
	uint32_t *buf;
	uint32_t size;
	int timeout_ms;
};

/* struct for msg recv */
struct ipc_recv_msg {
	uint32_t *buf;
	uint32_t size;
	int timeout_ms;
};

#define IPC_CHANID_MAX 4

struct chan_config {
	const char *name;
	const uint32_t mboxid;
	const enum master_id srcid;
	const enum master_id dstid;
	uint32_t irq;
};

struct ipc_channel {
	enum master_id mst;
	struct chan_config *chan_cfg;
	ring_buffer_head_t rb_head;
};

/* config struct */
struct user_config {
	const char *name;
};

/* TODO: should use ipc_chan_xxx */
int ipc_send(struct ipc_channel *chan, void *buf, uint32_t size);
int ipc_recv(struct ipc_channel *chan, void *buf, uint32_t size);

/* TODO: not export to dev */
int ipc_irq_status(void);
int ipc_irq_clear(struct ipc_channel *chan, uint32_t ack_mode);
int ipc_release_and_irq_clear(struct ipc_channel *chan, uint32_t ack_mode);
int ipc_data_store(struct ipc_channel *chan);
void ipc_send_ack(struct ipc_channel *chan, uint32_t ack_mode);
void ipc_release(struct ipc_channel *chan);
void ipc_reset(void);
int ipc_chan_open(struct ipc_channel *chan);
int ipc_chan_close(struct ipc_channel *chan);
uint32_t ipc_find_chan_by_name(const struct chan_config *dev_chan_cfg, const char *name);

#endif /* __IPC_H__*/
