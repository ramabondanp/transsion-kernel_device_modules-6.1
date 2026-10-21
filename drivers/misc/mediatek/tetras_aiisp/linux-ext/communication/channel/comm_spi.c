/*
 * (C) Copyright 2021-2022, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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
#include "spi2ahb_io.h"

#include "../common.h"
#include "../comm_ret_code.h"
#include "../comm_phy.h"
#include "comm_spi.h"
int comm_spi_read(u32 read_addr, char *buffer, u32 buf_size)
{
	int ret = RET_OK;
	int read_len = 0;

	read_len = buf_size % BYTEALIGN_WIDTH ?
		   buf_size / BYTEALIGN_WIDTH + 1 :
		   buf_size / BYTEALIGN_WIDTH;
	ret = spi_bridge_read(read_addr, buffer, read_len * 4);

	return ret;
}

int comm_spi_write(u32 write_addr, char *buffer, u32 buf_size)
{
	int ret = RET_OK;
	int write_len = 0;

	write_len = buf_size % BYTEALIGN_WIDTH ?
		    buf_size / BYTEALIGN_WIDTH + 1 :
		    buf_size / BYTEALIGN_WIDTH;
	ret = spi_bridge_write(write_addr, buffer, write_len * 4);

	return ret;
}
