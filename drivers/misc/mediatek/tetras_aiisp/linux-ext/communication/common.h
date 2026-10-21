/*
 * (C) Copyright 2021-2022, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/**
 * @brief   header of common difine in communication
 * @date    2022-2-28
 */

#ifndef __COMM_COMMON_H__
#define __COMM_COMMON_H__

#define BYTEALIGN_WIDTH 4 /* spi2ahb read and write byte wide */
#define WRITE_RB_TIMES 3

#define COMM_OK 1
#define COMM_ERROR 0

#define COMM_CHANNEL_NUM  16

enum COMM_CHANNEL_ID {
	COMM_CHANNEL0 = 0,
	COMM_CHANNEL1,
	COMM_CHANNEL2,
	COMM_CHANNEL3,
};

enum COMM_CHANNEL_TYPE {
	IPC_TRANS_TYPE  = 0,
	UART_CHAN_TYPE,
};

enum COMM_PHY_CHANNEL_ID {
	COMM_IPC_CHAN = 0,
	SPI2AHB_CHAN,
	I2C2AHB_CHAN,
};

struct comm_chan_static_info {
	u16 used;
	u16 channel_no;
	u16 dst_chip_id;
	u16 dst_pno;
	u16 transfer_type;  /* such as ipc, */
	u16 phy_type; /* such as spi2ahb */
	char dst_name[NAME_LEN];
};

typedef void *send_dev_t;
typedef void *recv_dev_t;

#endif /* __COMM_COMMON_H__ */
