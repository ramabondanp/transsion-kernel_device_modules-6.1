/*
 * (C) Copyright 2021-2022, Shenzhen Tetras.AI Technology Co., Ltd
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

#include "common.h"
#include "comm_ret_code.h"
#include "comm_log.h"
#include "channel/comm_ipc.h"
#include "comm_transfer.h"

#define TRANSFER_TYPE_NUM 8

typedef int (*comm_transfer_init)(send_dev_t *send_dev, recv_dev_t *recv_dev);
typedef void (*comm_transfer_exit)(send_dev_t send_dev, recv_dev_t recv_dev);
typedef int (*comm_transfer_send)(send_dev_t send_dev, void *buffer, u32 buf_size);
typedef int (*comm_transfer_recv)(recv_dev_t recv_dev, void *buffer, u32 buf_size);

struct comm_transfer_info {
	u32 transfer_type;
	comm_transfer_init trans_init_func;
	comm_transfer_exit trans_exit_func;
	comm_transfer_send  trans_send_func;
	comm_transfer_recv trans_recv_func;
};

struct comm_transfer_info transfer_config_table[TRANSFER_TYPE_NUM] = {
	{
		IPC_TRANS_TYPE,
		comm_ipc_init,
		comm_ipc_exit,
		comm_ipc_send,
		comm_ipc_recv,
	},
};

comm_transfer_init get_trans_init_by_type(u32 v_trans_type)
{
	int i = 0;

	for (i = 0; i < TRANSFER_TYPE_NUM; i++) {
		if (v_trans_type == transfer_config_table[i].transfer_type)
			break;
	}
	if (i == TRANSFER_TYPE_NUM) {
		comm_err("can't find transfer %d init func!\n",
			 v_trans_type);
		return NULL;
	}
	return transfer_config_table[i].trans_init_func;
}

comm_transfer_exit get_trans_exit_by_type(u32 v_trans_type)
{
	int i = 0;

	for (i = 0; i < TRANSFER_TYPE_NUM; i++) {
		if (v_trans_type == transfer_config_table[i].transfer_type)
			break;
	}
	if (i == TRANSFER_TYPE_NUM) {
		comm_err("can't find transfer %d init func!\n",
			 v_trans_type);
		return NULL;
	}
	return transfer_config_table[i].trans_exit_func;
}

comm_transfer_send get_trans_send_by_dev(struct comm_trans_dev_info *p_dev_info)
{
	int i = 0;

	for (i = 0; i < TRANSFER_TYPE_NUM; i++) {
		if (p_dev_info->trans_type == transfer_config_table[i].transfer_type)
			break;
	}
	if (i == TRANSFER_TYPE_NUM) {
		comm_err("can't find transfer %d init func!\n",
			 p_dev_info->trans_type);
		return NULL;
	}
	return transfer_config_table[i].trans_send_func;
}

comm_transfer_recv get_trans_recv_by_dev(struct comm_trans_dev_info *p_dev_info)
{
	int i = 0;

	for (i = 0; i < TRANSFER_TYPE_NUM; i++) {
		if (p_dev_info->trans_type == transfer_config_table[i].transfer_type)
			break;
	}
	if (i == TRANSFER_TYPE_NUM) {
		comm_err("can't find transfer %d init func!\n",
			 p_dev_info->trans_type);
		return NULL;
	}
	return transfer_config_table[i].trans_recv_func;
}

int comm_transfer_dev_init(struct comm_trans_dev_info *p_dev_info)
{
	int ret = RET_ERROR;
	comm_transfer_init trans_init = NULL;

	if (NULL == p_dev_info) {
		comm_err("input null\n");
		return RET_ERROR;
	}
	comm_info("p_dev_info->trans_type is %d, p_dev_info is %p\n",
		  p_dev_info->trans_type, p_dev_info);
	trans_init = get_trans_init_by_type(p_dev_info->trans_type);
	if (NULL == trans_init) {
		comm_err("get %d trans init fail!\n", p_dev_info->trans_type);
		return RET_ERROR;
	}
	ret = trans_init(&p_dev_info->trans_send_dev, &p_dev_info->trans_recv_dev);
	return ret;
}

void comm_transfer_dev_exit(struct comm_trans_dev_info *p_dev_info)
{
	comm_transfer_exit trans_exit = NULL;

	if (NULL == p_dev_info) {
		comm_err("input null\n");
		return;
	}

	trans_exit = get_trans_exit_by_type(p_dev_info->trans_type);
	if (NULL == trans_exit) {
		comm_err("get %d trans exit fail!\n", p_dev_info->trans_type);
		return;
	}
	trans_exit(p_dev_info->trans_send_dev, p_dev_info->trans_recv_dev);
	return;
}


int comm_transfer_dev_send(struct comm_trans_dev_info *p_dev_info, void *buffer,
			   u32 buf_size)
{
	int ret = RET_ERROR;
	comm_transfer_send trans_send = NULL;

	if (NULL == p_dev_info) {
		comm_err("input null\n");
		return RET_ERROR;
	}
	trans_send = get_trans_send_by_dev(p_dev_info);
	if (NULL == trans_send) {
		comm_err("get %d trans send fail!\n", p_dev_info->trans_type);
		return RET_ERROR;
	}
	ret = trans_send(p_dev_info->trans_send_dev, buffer, buf_size);

	return ret;
}

int comm_transfer_dev_recv(struct comm_trans_dev_info *p_dev_info, void *buffer,
			   u32 buf_size)
{
	int ret = RET_ERROR;
	comm_transfer_recv trans_recv = NULL;

	if (NULL == p_dev_info) {
		comm_err("input null\n");
		return RET_ERROR;
	}
	trans_recv = get_trans_recv_by_dev(p_dev_info);
	if (NULL == trans_recv) {
		comm_err("get %d trans recv fail!\n", p_dev_info->trans_type);
		return RET_ERROR;
	}
	ret = trans_recv(p_dev_info->trans_recv_dev, buffer, buf_size);

	return ret;
}
