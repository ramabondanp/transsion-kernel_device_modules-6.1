/*
 * (C) Copyright 2021-2022, Shenzhen Tetras.AI Technology Co., Ltd
 *
 * Change Logs:
 * Date           Author         Notes
 * 2022-01-11     suntongce      Initialize.
 */

/**
 * @brief   Tetras I2C2APB driver
 * @date    2022-01-11
 */

#ifndef __TETRAS_I2C2APB_H__
#define __TETRAS_I2C2APB_H__

#include <linux/miscdevice.h>

#define I2C2APB_DEV_NAME		"tetras_i2c2apb"

#define I2C2APB_DRV_STR			"tetras,i2c2apb"
#define I2C2APB_MANU_ID			(0)
#define I2C2APB_PART_ID			(0)

#define I2C2APB_READ			1
#define I2C2APB_WRITE			0
#define RETRY_WAIT_TIME_USEC		(2000)

/* this is Qualcomm 8350 i2c' limit */
#define MAX_QCON_BUFFER_SIZE		(64 * 1024)
#define MAX_BUFFER_SIZE			(32 * 1024)
#define FIFO_DEPTH			(32)

/* hw related parameter*/
#define IxC2APB_OPS_UNIT		(4)
#define MAX_I2C_RW_DATA_LEN		(1024 * 4)
#define I2C2APB_INSTR_OFFSET		(5)

#define I2C_RDWR_IOCTL_MAX_MSGS		(10)

/* csr detail */
#define CSR_PAGE_OFFSET			(26)
#define CSR_PAGE_WIDTH			(6)
#define CSR_FIFO_LEVEL_OFFSET		(16)
#define CSR_FIFO_LEVEL_WIDTH		(6)
#define CSR_CONFLICT_OFFSET		(7)
#define CSR_CONFLICT_WIDTH		(1)
#define CSR_SLVERR_OFFSET		(6)
#define CSR_SLVERR_WIDTH		(1)
#define CSR_FIFO_UNDERRUN_OFFSET	(5)
#define CSR_FIFO_UNDERRUN_WIDTH		(1)
#define CSR_FIFO_OVERRUN_OFFSET		(4)
#define CSR_FIFO_OVERRUN_WIDTH		(1)

#define GET_MSK_FROM_WID(w)		((1 << w) - 1)

#define GET_CSR_PAGE(r)			(((r) >> CSR_PAGE_OFFSET) & \
					 ((1 << CSR_PAGE_WIDTH) - 1))
#define GET_CSR_FIFO_LEVEL(r)		(((r) >> CSR_FIFO_LEVEL_OFFSET) & \
					 ((1 << CSR_FIFO_LEVEL_WIDTH) - 1))
#define GET_CSR_FIFO_UNDERRUN(r)	(((r) >> CSR_FIFO_UNDERRUN_OFFSET) & \
					 ((1 << CSR_FIFO_UNDERRUN_WIDTH) - 1))
#define GET_CSR_CONFLICT(r)		(((r) >> CSR_CONFLICT_OFFSET) & \
					 ((1 << CSR_CONFLICT_WIDTH) - 1))
#define GET_CSR_SLVERR(r)		(((r) >> CSR_SLVERR_OFFSET) & \
					 ((1 << CSR_SLVERR_WIDTH) - 1))
#define GET_CSR_FIFO_OVERRUN(r)		(((r) >> CSR_FIFO_OVERRUN_OFFSET) & \
					 ((1 << CSR_FIFO_OVERRUN_WIDTH) - 1))

/**
 * enum I2C2APB_INSTR	enumeration value of instruction
 * @RdStat		Read CSR state instruction , 1 transfer
 * @WrStat		Write CSR state instruction, 2 transfer
 * @WrData		Write data to fifo instruction, 1 transfer
 * @RdData		Read data from fifo instruction, 2 transfer
 * @RdReq		Read data from apb port to fifo instruction, 1 transfer
 */
enum I2C2APB_INSTR {
	RdStat = 0,
	WrStat = 1,
	WrData = 2,
	RdData = 3,
	RdReq  = 4,
};

#define CSR_INIT_VAL			(0x3)

#define WS_INSTR			(((WrStat << I2C2APB_INSTR_OFFSET)))

#define RS_INSTR			(((RdStat << I2C2APB_INSTR_OFFSET)))

#define WD_INSTR(a)			((((((a) >> 2) & 0xFFFFFF) << 8) |\
					 (WrData << I2C2APB_INSTR_OFFSET)))

#define RD_INSTR			((RdData << I2C2APB_INSTR_OFFSET))

#define RR_INSTR(a, len)		((((((a) >> 2) & 0xFFFFFF) << 8) |\
					  (RdReq << I2C2APB_INSTR_OFFSET) |\
					  (((len - IxC2APB_OPS_UNIT) /\
					    IxC2APB_OPS_UNIT) & 0x1F)))

/**
 * struct i2c2apb_dev	Struct represeting i2c2apb device and driver data
 * @opencnt		device open count
 * @op_mutex		operation mutex
 * @i2c_client		i2c client
 * @miscdev		misc device for userspace
 * @csr			device cached csr value
 * @flag		debug control
 */
struct i2c2apb_dev {
	int opencnt;
	struct mutex op_mutex;

	struct i2c_client *client;
	struct miscdevice miscdev;
	unsigned int csr;
	unsigned int flag;
};

int i2c2apb_dev_probe(struct i2c_client *device);
int i2c2apb_dev_remove(struct i2c_client *device);
int i2c2apb_dev_suspend(struct device *device);
int i2c2apb_dev_resume(struct device *device);
#endif /* <__TETRAS_I2C2APB_H__ */

