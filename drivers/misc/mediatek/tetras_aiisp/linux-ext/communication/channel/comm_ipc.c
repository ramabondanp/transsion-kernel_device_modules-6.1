/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * Change Logs:
 * Date           Author        Notes
 */
/**
 * @brief   comm io
 * @date    2022-2-28
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/printk.h>
#include <linux/mutex.h>
#include "comm_drv_io.h"
#include "ipc_dev.h"
#include "ipc_io.h"

#include "../common.h"
#include "../comm_ret_code.h"
#include "../comm_log.h"
#include "comm_ipc.h"

#define IPC_RECV_TIMEOUT 5000

int comm_ipc_init(send_dev_t *send_dev, recv_dev_t *recv_dev)
{
	struct user_config recv_config;
	struct user_config send_config;

	recv_config.name = "m7_2_qcom";
	send_config.name = "qcom_2_m7";

	if (NULL == send_dev || NULL == recv_dev) {
		comm_err("input null\n");
		return RET_ERROR;
	}

	/* recv configuration */
	*recv_dev = (recv_dev_t)ipc_dev_open(NULL, &recv_config);
	if (recv_dev == NULL) {
		comm_err("ipc dev open recv failed\n");
		return RET_ERROR;
	}

	/* send configuration */
	*send_dev = (send_dev_t)ipc_dev_open(NULL, &send_config);
	if (*send_dev == NULL) {
		comm_err("ipc dev open send failed\n");
		ipc_dev_close((struct ipc_vdev *)*recv_dev);
		return RET_ERROR;
	}
	return RET_OK;
}

void comm_ipc_exit(send_dev_t send_dev, recv_dev_t recv_dev)
{
	if (NULL != send_dev)
		ipc_dev_close((struct ipc_vdev *)send_dev);
	if (NULL != recv_dev)
		ipc_dev_close((struct ipc_vdev *)recv_dev);
}

int comm_ipc_send(send_dev_t p_send_dev, void *buffer, u32 buf_size)
{
	int ret = RET_OK;
	struct ipc_send_msg send_msg;

	send_msg.buf = (u32 *)buffer;
	send_msg.size = buf_size;
	send_msg.timeout_ms = 0;

	ret = ipc_dev_send((struct ipc_vdev *)p_send_dev, &send_msg);

	return ret;
}

int comm_ipc_recv(recv_dev_t p_recv_dev, void *buffer, u32 buf_size)
{
	int ret = RET_OK;
	struct ipc_recv_msg recv_msg;

	recv_msg.buf = (u32 *)buffer;
	recv_msg.size = buf_size;
	recv_msg.timeout_ms = IPC_RECV_TIMEOUT;

	ret = ipc_dev_recv((struct ipc_vdev *)p_recv_dev, &recv_msg);

	return ret;
}

#define SZ_ALIGN(sz, align) ((sz) % (align) ?  (sz) / (align) + 1 : (sz) / (align))
int comm_ipc_readdata(u32 addr, char *buffer, u32 buf_size)
{
	int align_len = 0;
	int read_len;

	align_len = SZ_ALIGN(buf_size, BYTEALIGN_WIDTH) * 4;

	read_len = ipc_read_nbytes(addr, align_len, buffer);
	return (read_len == align_len) ? 0 : -1;
}

int comm_ipc_writedata(u32 addr, char *buffer, u32 buf_size)
{
	int align_len = 0;
	int write_len;

	align_len = SZ_ALIGN(buf_size, BYTEALIGN_WIDTH) * 4;

	write_len = ipc_write_nbytes(addr, align_len, buffer);
	return (write_len == align_len) ? 0 : -1;
}
