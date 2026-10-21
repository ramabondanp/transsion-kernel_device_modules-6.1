/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-only
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
#include "comm_phy.h"
#include "channel/comm_ipc.h"

#define PHY_TYPE_NUM 8

typedef int (*comm_phy_read_func)(u32 read_addr, char *buffer, u32 buf_size);
typedef int (*comm_phy_write_func)(u32 read_addr, char *buffer, u32 buf_size);

struct comm_phy_info {
	u32 phy_type;
	comm_phy_read_func  phy_read_func;
	comm_phy_write_func phy_write_func;
};

struct comm_phy_info phy_config_table[PHY_TYPE_NUM] = {
	{
		COMM_IPC_CHAN,
		comm_ipc_readdata,
		comm_ipc_writedata,
	},
};

comm_phy_read_func get_phy_read_func_by_type(u32 chan_phy_type)
{
	int i = 0;

	for (i = 0; i < PHY_TYPE_NUM; i++) {
		if (chan_phy_type == phy_config_table[i].phy_type)
			break;
	}
	if (i == PHY_TYPE_NUM) {
		comm_err("can't find phy %d read func!\n", chan_phy_type);
		return NULL;
	}
	return phy_config_table[i].phy_read_func;
}

comm_phy_write_func get_phy_write_func_by_type(u32 chan_phy_type)
{
	int i = 0;

	for (i = 0; i < PHY_TYPE_NUM; i++) {
		if (chan_phy_type == phy_config_table[i].phy_type)
			break;
	}
	if (i == PHY_TYPE_NUM) {
		comm_err("can't find phy %d write func!\n", chan_phy_type);
		return NULL;
	}
	return phy_config_table[i].phy_write_func;
}

int comm_phy_read(u32 read_addr, char *buffer, u32 buf_size, u32 chan_phy_type)
{
	int ret = RET_OK;
	comm_phy_read_func phy_read_func = NULL;

	phy_read_func = get_phy_read_func_by_type(chan_phy_type);
	if (NULL == phy_read_func) {
		comm_err("can't find phy type %d read func!\n", chan_phy_type);
		return RET_ERROR;
	}
	ret = phy_read_func(read_addr, buffer, buf_size);

	return ret;
}

int comm_phy_write(u32 write_addr, char *buffer, u32 buf_size, u32 chan_phy_type)
{
	int ret = RET_OK;
	comm_phy_write_func phy_write_func = NULL;

	phy_write_func = get_phy_write_func_by_type(chan_phy_type);
	if (NULL == phy_write_func) {
		comm_err("can't find phy type %d write func!\n", chan_phy_type);
		return RET_ERROR;
	}
	ret = phy_write_func(write_addr, buffer, buf_size);
	return ret;
}
