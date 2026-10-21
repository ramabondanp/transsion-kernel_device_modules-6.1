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
 * @brief   spibridge uapi header file
 * @date    2021-01-20
 */

#ifndef _UAPISPIBRIDGE_H
#define _UAPISPIBRIDGE_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define AI_ISP_V1_HID 0x3ed6
#define AI_ISP_V2_HID 0x0706

enum read_flag {
	READ_WITH_8_WAIT_CYCLES = 0,
	READ_WITH_16_WAIT_CYCLES,
	READ_WITH_24_WAIT_CYCLES,
	READ_WITH_32_WAIT_CYCLES,
	READ_BY_POLLING,
};

enum ls_bridge_error_code {
	ERR_START = ERANGE,
	ERR_WR_XFER_DATA,
	ERR_WR_AHB_ERR,
	ERR_WR_TIMEOUT,
	ERR_RD_XFER_DATA,
	ERR_RD_AHB_ERR,
	ERR_RD_TIMEOUT,
};

struct ls_bridge_msg {
	__u32	addr;
	__u32	data_len;
	__u8    *buffer;
	__u32	flag;
	__u32	error_code;
	__u8	device_id;
};

enum ls_bridge_device_id {
	SPI_DEVICE = 0,
	I2C_DEVICE,
};

/**
 * struct bridge_csr_msg - csr read/write msg struct.
 * @reg_addr: register offset address in spi2axi module.
 * @reg_val: register value.
 * @device_id: SPI_DEVICE or I2C_DEVICE.
 */
struct ls_bridge_csr_msg {
	__u8 reg_addr;
	__u32 reg_val;
	__u8 device_id;
};

struct ls_bridge_test_msg {
	__u32 test_cmd;
	__u8 device_id;
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
 * struct ls_bridge_scatter_msg - low speed bridge2axi scatter message
 * @device_id: SPI_DEVICE or I2C_DEVICE
 * @scatter_num: word length of this sactter, should be less than MAX_SCATTER_LEN.
 * @scatters: scatter write uints
 */
struct ls_bridge_scatter_msg {
	__u8 device_id;
	__u32 scatter_num;
	struct scatter_wr_unit *scatters;
};

enum spibridge_type {
	SPI_BRIDGE_SPI2AXI = 0,
	SPI_BRIDGE_SPI2AHB,
	SPI_BRIDGE_NUM,
};

struct spibridge_info {
	__u32	max_xfer_len;
	__u32	cur_bridge_id;
	__u32	version_id;
};

enum busrt_len_op {
	LS_BRIDGE_BURST_LEN_GET,
	LS_BRIDGE_BURST_LEN_SET,
};

struct ls_bridge_busrt_op {
	__u32	burst_op;
	__u32	max_burst_len;
	__u32	cur_burst_len;
	__u32	burst_len_set;
	__u8 device_id;
};

struct ls_bridge_speed_msg {
	__u8	mode;
	__u32	set_speed;
	__u32	get_speed;
	__u32	min_speed;
	__u32	max_speed;
	__u32	best_speed;
};

enum speed_op {
	SPEED_SET = 0,
	SPEED_BEST_SET,
	SPEED_SINGLE_GET,
	SPEED_RANGE_GET,
};

/*
 * LS_BRIDGE_CMD_READ:
 * read 64, 32, 16, 8, 4, or 1 frame data from ai_isp
 */
#define LS_BRIDGE_CMD_READ	_IOWR('t', 1, struct ls_bridge_msg)

/*
 * LS_BRIDGE_CMD_WRITE:
 * write 64, 32, 16, 8, 4, or 1 frame data to ai_isp
 */
#define LS_BRIDGE_CMD_WRITE	_IOW('t', 2, struct ls_bridge_msg)

/*
 * SPIBRIDGE_CMD_DEVINFO
 * write data to ai_isp
 */
#define SPIBRIDGE_CMD_DEVINFO	_IOR('t', 3, struct spibridge_info)

/*
 * SPIBRIDGE_CMD_SWITCH
 * switch spi2ahb or spi2axi
 */
#define SPIBRIDGE_CMD_SWITCH	_IOW('t', 4, __u32)

/*
 * LS_BRIDGE_CMD_BURST_LEN
 * Set burst length of soc bridge
 */
#define LS_BRIDGE_CMD_BURST_LEN	_IOWR('t', 5, struct ls_bridge_busrt_op)

/*
 * SPIBRIDGE_CMD_SET_MODE
 * Set spi/i2c mode
 */
#define SPIBRIDGE_CMD_SET_MODE _IOW('t', 6, __u32)

/*
 * LS_BRIDGE_CMD_SCAT_WR
 * Scatter write function
 */
#define LS_BRIDGE_CMD_SCAT_WR	_IOW('t', 7, struct ls_bridge_scatter_msg)

/*
 * SPIBRIDGE_CMD_SPEED_OP
 * Set or get spi transfer speed
 */
#define SPIBRIDGE_CMD_SPEED_OP	_IOW('t', 8, struct ls_bridge_speed_msg)

/*
 * SPIBRIDGE_CMD_RESET
 * Reset spi/i2c
 */
#define SPIBRIDGE_CMD_RESET _IOW('t', 10, __u32)

/*
 * LS_BRIDGE_CMD_READ_CSR
 * using i2c2axi or spi2axi read register value in spi2axi module
 */
#define LS_BRIDGE_CMD_READ_CSR	_IOWR('t', 12, struct ls_bridge_csr_msg)

/*
 * LS_BRIDGE_CMD_WRITE_CSR
 * using i2c2axi or spi2axi write register value in spi2axi module
 */
#define LS_BRIDGE_CMD_WRITE_CSR	 _IOW('t', 13, struct ls_bridge_csr_msg)

#endif
