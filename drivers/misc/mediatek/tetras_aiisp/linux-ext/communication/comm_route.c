/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
/**
 * @brief   comm route
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
#include "comm_ret_code.h"
#include "common.h"
#include "comm_phy.h"
#include "comm_log.h"
#include "comm_route.h"

struct comm_chan_static_info channel_config_table[COMM_CHANNEL_NUM] = {
	{
		1,
		COMM_CHANNEL0,
		CHIP_RTOS,
		0xff,
		IPC_TRANS_TYPE,
		COMM_IPC_CHAN,
		"",
	},
	{
		0,
		COMM_CHANNEL1,
		CHIP_RTOS,
		0xff,
		IPC_TRANS_TYPE,
		I2C2AHB_CHAN,
		"",
	},
};

struct comm_chan_static_info *comm_route_table_init(u16 channel_no)
{
	struct comm_chan_static_info *chan_static_info = NULL;
	struct comm_chan_static_info *chan_config = NULL;

	if (channel_no > COMM_CHANNEL_NUM) {
		comm_err("channel is %d, bigger than %d!\n", channel_no,
			 COMM_CHANNEL_NUM);
		return NULL;
	}

	chan_config = &channel_config_table[channel_no];
	if (chan_config->used != 0) {
		chan_static_info = kzalloc(sizeof(struct comm_chan_static_info),
					   GFP_KERNEL);
		if (NULL == chan_static_info) {
			comm_err("alloc fail in init route table!\n");
			return NULL;
		}
		chan_static_info->used = chan_config->used;
		chan_static_info->channel_no = channel_no;
		chan_static_info->dst_chip_id = chan_config->dst_chip_id;
		chan_static_info->dst_pno = chan_config->dst_pno;
		chan_static_info->transfer_type = chan_config->transfer_type;
		chan_static_info->phy_type = chan_config->phy_type;
		strncpy(chan_static_info->dst_name, chan_config->dst_name,
			NAME_LEN - 1);
	}
	return chan_static_info;
}
