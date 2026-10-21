/*
 * (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Change Logs:
 * Date           Author         Notes
 * 2022-1-20     yanghua     Initialize.
 */

/**
 * @brief   Spi2axi protocol driver header
 * @date    2021-12-24
 */

#ifndef __SPI2AXI_CSR_H__
#define __SPI2AXI_CSR_H__

#define CMD_DEV_RST			0xFF
#define CMD_USE_WORD			0xF1
#define CMD_USE_BYTE			0xF0
#define CMD_CSR_WR			0x15
#define CMD_CSR_RD			0x0A
#define CMD_AXI_WR			0x13
#define CMD_AXI_QWR			0x33
#define CMD_AXI_SWR			0x43
#define CMD_AXI_RR			0x06
#define CMD_AXI_RD			0x0C
#define CMD_AXI_QRD			0x2C

/* 8-bit cmd + 8-bit addr + 32-bit val */
#define CSR_BUF_LEN			6
#define RR_WR_BUF_LEN			5
#define CSR_RD_BUF_LEN			4

#define CSR_HID_OFFSET			0x0

#define CSR_HID_VER_OFFSET		0
#define CSR_HID_VER_MASK		(0xffff << 0)
#define CSR_HID_ID_OFFSET		16
#define CSR_HID_ID_MASK			(0xffff << 16)

#define BYTE_COUNT_OFFSET		0x10

#define GLB_CSR_OFFSET			0x14

#define XFER_FINISH_OFFSET		0
#define XFER_FINISH_MASK		(0x1 << 0)
#define BIU_CLEARING_OFFSET		1
#define BIU_CLEARING_MASK		(0x1 << 1)
#define FIFO_ERR_OFFSET			2
#define FIFO_ERR_MASK			(0x1 << 2)
#define BIU_ERR_OFFSET			3
#define BIU_ERR_MASK			(0x1 << 3)
#define INSTR_ERR_OFFSET		4
#define INSTR_ERR_MASK			(0x1 << 4)
#define DATA_WORD_OFFSET		5
#define DATA_WORD_MASK			(0x1 << 5)
#define LOG_EN_OFFSET			6
#define LOG_EN_MASK			(0x1 << 6)
#define CLR_OFFSET			7
#define CLR_MASK			(0x1 << 7)
#define ERR_CLR_OFFSET			8
#define ERR_CLR_MASK			(0x1 << 8)
#define STAT_CLR_OFFSET			9
#define STAT_CLR_MASK			(0x1 << 9)
#define LOG_CLR_OFFSET			10
#define LOG_CLR_MASK			(0x1 << 10)
#define FORCE_PEND_CLR_OFFSET		11
#define FORCE_PEND_CLR_MASK		(0x1 << 11)
#define BIT_MASK_OFFSET			16


#define BIU_CTL_OFFSET			0x18

#define MAX_BURST_LEN_OFFSET		0
#define MAX_BURST_LEN_MASK		(0xf << 0)
#define BUS_ADDR_35_32_OFFSET		4
#define BUS_ADDR_35_32_MASK		(0xf << 4)


#define QOS_CTL_OFFSET			0x1c

#define DYNAMIC_QOS_EN_OFFSET		0
#define DYNAMIC_QOS_EN_MASK		(0x1 << 0)
#define QOS_LOW_OFFSET			4
#define QOS_LOW_MASK			(0xf << 4)
#define QOS_HIGH_OFFSET			8
#define QOS_HIGH_MASK			(0xf << 8)
#define QOS_THRE_OFFSET			16
#define QOS_THRE_MASK			(0xff << 16)


#define PRESSURE_CTL_OFFSET		0x20

#define PRES_CTL_EN_OFFSET		0
#define PRES_CTL_EN_MASK		(0x1 << 0)
#define PRES_VAL_OFFSET			4
#define PRES_VAL_MASK			(0xf << 4)
#define PRES_SET_THRE_OFFSET		16
#define PRES_SET_THRE_MASK		(0xff << 16)
#define PRES_CLR_THRE_OFFSET		24
#define PRES_CLR_THRE_MASK		(0xff << 24)


#define TIMING_CTL_OFFSET		0x24

#define READ_XFER_ADV_EN_OFFSET		0
#define READ_XFER_ADV_EN_MASK		(0x1 << 0)
#define READ_ADV_SCLK_OFFSET		1
#define READ_ADV_SCLK_MASK		(0x1 < 1)
#define READ_ADV_1SCLK			(0x0)
#define READ_ADV_2SCLK			(0x1)

#define READ_ADV_MODE_OFFSET		2
#define READ_ADV_MODE_MASK		(0x1 < 2)
#define READ_ADV_MODE0			(0x0)
#define READ_ADV_MODE3			(0x1)

#define READ_ADV_CYCLE_OFFSET		4
#define READ_ADV_CYCLE_MASK		(0xf << 4)
#define READ_ADV_WR_KEY_OFSSET		16
#define READ_ADV_WR_KEY_MASK		(0xffff << 16)
#define READ_ADV_WR_KEY_VAL		(0xace5)


#define FIFO_STS_OFFSET			0x28

#define FIFO_DEPTH_OFFSET		0
#define FIFO_DEPTH_MASK			(0xff << 0)
#define FIFO_CNT_OFFSET			8
#define FIFO_CNT_MASK			(0xff << 8)
#define MAX_FIFO_CNT_REACHED_OFFSET	16
#define MAX_FIFO_CNT_REACHED_MASK	(0xff << 16)
#define FIFO_EMPTY_OFFSET		24
#define FIFO_EMPTY_MASK			(0x1 << 24)
#define FIFO_FULL_OFFSET		25
#define FIFO_FULL_MASK			(0x1 << 25)
#define OVERFLOW_ERR_OFFSET		26
#define OVERFLOW_ERR_MASK		(0x1 << 26)
#define UNDERFLOW_ERR_OFFSET		27
#define UNDERFLOW_ERR_MASK		(0x1 << 27)


#define BIU_STS0_OFFSET			0x2c

#define BIU_PENDING_TRANS_OFFSET	0
#define BIU_PENDING_TRANS_MASK		(0x3f << 0)
#define BIU_SM_STATE_OFFSET		8
#define BIU_SM_STATE_MASK		(0xf << 8)
#define MAX_OTS_REACHED_OFFSET		12
#define MAX_OTS_REACHED_MASK		(0x3f << 12)


#define BIU_STS1_OFFSET			0x30

#define ERR_ADDR_STS_OFFSET		0x34

#define ERR_RESP_OFFSET			0
#define ERR_RESP_MASK			(0x1 << 0)
#define ADDR_OFFSET			2
#define ADDR_MASK			(0x3fffffff << 2)


#define ERR_INSTR_STS_OFFSET		0x3c

#define ERR_INSTR_VLD_OFFSET		0
#define ERR_INSTR_VLD_MASK		(0x1 << 0)
#define ERR_INSTR_OFFSET		8
#define ERR_INSTR_MASK			(0xff << 8)


#define LOGGER_GRP_NUM			4
#define LOGGER_CMD_LOG_GROUP(n)		(0x40 + (n * 0x10))
#define CMD_LOG_READ_OFFSET		0
#define CMD_LOG_READ_MASK		(0x1 << 0)
#define CMD_LOG_WRITE_OFFSET		1
#define CMD_LOG_WRITE_MASK		(0x1 << 1)
#define CMD_LOG_RESP_OFFSET		2
#define CMD_LOG_RESP_MASK		(0x3 << 2)
#define CMD_LOG_BURST_LEN_OFFSET	4
#define CMD_LOG_BURST_LEN_MASK		(0xf << 4)
#define CMD_LOG_QOS_OFFSET		8
#define CMD_LOG_QOS_MASK		(0xf << 8)
#define CMD_LOG_ADDR_35_32_OFFSET	16
#define CMD_LOG_ADDR_35_32_MASK		(0xf << 16)

#define LOGGER_ADDR_LOG_GROUP(n)	(0x44 + (n * 0x10))
#define LOGGER_DATA_LOG_GROUP(n)	(0x48 + (n * 0x10))

/* v2 only */
#define LOCK				0x80
#define REL_LOCK			0x84
#define I2C_STATE			0x88
#define TRANS_MODE_EN_MASK		(0x1 << 16)
#define CLK_STRE_EN_MASK		(0x2 << 16)
/* end v2 only */

#define SCRATCH_GRP_NUM			4
#define SCRATCH_GROUP(n, v2)		((v2 ? 0x90 : 0x80) + (n * 0x4))

#define CALI_PAT_A_OFFSET		0xa0
#define CALI_PAT_A_VAL			0xa5a5a5a5
#define CALI_PAT_B_OFFSET		0xa4
#define CALI_PAT_B_VAL			0xc3c3c3c3
#define CALI_PAT_C_OFFSET		0xa8
#define CALI_PAT_C_VAL			0x76543210
#define CALI_PAT_D_OFFSET		0xac
#define CALI_PAT_D_VAL			0xaabbccdd

#define MAX_BURST_LEN			16

#define TRAIN_CTL_OFFSET		0xf8
#define TRAIN_CTL_KEY			0xACE5
#define TRAIN_CTL_KEY_MASK		GENMASK(31, 16)
#define TRAIN_CTL_KEY_SHIFT		16
#define TRAIN_CTL_EN_BIT		BIT(0)

#define SPI_MODE_MASK			(0x3)

enum ip_test_id {
	TEST_LSB_MSB_RD_WR,
	TEST_ERR_INSTRUCTION,
	TEST_HW_LOG_DUMP,
	TEST_FIFO_UNDERFLOW,
	TEST_RST,
	MAX_TEST_ID,
};

#endif
