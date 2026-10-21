/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note
 *
 * Change Logs:
 * Date           Author         Notes
 * 2022-01-20     yanghua     Initialize.
 */

/**
 * @brief   sdiobridge uapi header file
 * @date    2021-01-20
 */

#ifndef _UAPISDIOBRIDGE_H
#define _UAPISDIOBRIDGE_H

#include <linux/types.h>
#include <linux/ioctl.h>

struct sdiobridge_msg {
	__u32	addr;
	__u32	len;
	__u8	*buffer;
	__u32	error_code;
};

struct sdiobridge_reg_ctrl {
	__u32	addr;
	__u32	*reg_val;
	__u32	error_code;
};

#define MAX_SCATTER_LEN		4

/**
 * struct scatter_wr_unit - scatter write unit
 * @addr: start address of this scatter, should be 4bytes aligned.
 * @data_len: word length of this sactter, should be less than MAX_SCATTER_LEN.
 * @data: data of this scatter.
 */
struct scatter_wr_unit {
	__u32	addr;
	__u32	data_len;
	__u32	data[MAX_SCATTER_LEN];
};

/**
 * struct scatter_wr_unit - scatter write unit
 * @addr: start address of this scatter, should be 4bytes aligned.
 * @data_len: word length of this sactter, should be less than MAX_SCATTER_LEN.
 * @data: data of this scatter.
 */
struct sdiobridge_scatter_msg {
	__u32 scatter_num;
	struct scatter_wr_unit *scatters;
};

struct sdiobridge_info {
	__u32	max_xfer_len;
	__u32	version;
	__u32	reserved0;
	__u32	reserved1;
};

enum busrt_len_op {
	SDIOBRIDGE_BURST_LEN_GET,
	SDIOBRIDGE_BURST_LEN_SET,
};

struct sdiobridge_busrt_op {
	__u32	burst_op;
	__u32	max_burst_len;
	__u32	cur_burst_len;
	__u32	burst_len_set;
};

/*
 * SDIOBRIDGE_CMD_READ:
 * read 64, 32, 16, 8, 4, or 1 frame data from ai_isp
 */
#define SDIOBRIDGE_CMD_READ	_IOWR('t', 1, struct sdiobridge_msg)

/*
 * SDIOBRIDGE_CMD_WRITE:
 * write 64, 32, 16, 8, 4, or 1 frame data to ai_isp
 */
#define SDIOBRIDGE_CMD_WRITE	_IOW('t', 2, struct sdiobridge_msg)

/*
 * SDIOBRIDGE_CMD_DEVINFO
 * write data to ai_isp
 */
#define SDIOBRIDGE_CMD_DEVINFO	_IOR('t', 3, struct sdiobridge_info)

/*
 * SDIOBRIDGE_CMD_READ_REG:
 * read reg from sdio2axi
 */
#define SDIOBRIDGE_CMD_RD_REG	_IOWR('t', 4, struct sdiobridge_reg_ctrl)

/*
 * SDIOBRIDGE_CMD_WRITE_REG:
 * write reg to sdio2axi
 */
#define SDIOBRIDGE_CMD_WR_REG	_IOW('t', 5, struct sdiobridge_reg_ctrl)

/*
 * SDIOBRIDGE_CMD_RESET:
 * Reset sdio2axi
 */
#define SDIOBRIDGE_CMD_RESET	_IO('t', 6)

/*
 * SDIOBRIDGE_CMD_BLK_SIZE:
 * Set block size
 */
#define SDIOBRIDGE_CMD_BLK_SIZE	_IOW('t', 7, __u32)

/*
 * SDIOBRIDGE_CMD_SCAT_WR
 * Scatter write function
 */

#define SDIOBRIDGE_CMD_SCAT_WR	_IOW('t', 8, struct sdiobridge_scatter_msg)

#endif
