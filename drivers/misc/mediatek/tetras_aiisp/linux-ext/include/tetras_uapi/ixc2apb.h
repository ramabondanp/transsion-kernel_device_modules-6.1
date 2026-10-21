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

#ifndef __UAPI_IxC2APB_H__
#define __UAPI_IxC2APB_H__

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/i2c.h>

/**
 * struct ixc2apb_msg	struct of kernel->userspace msg
 * @offset		hw CSR state value or apb address
 * @len			data len in Bytes when visit apb
 * @flag		adjust electrical timing
 * @usr			userspace buffer pointer
 */
struct ixc2apb_msg {
	__u32 offset;
	__u32 len;
	__u32 flag;
	const __u8 __user *usr;
};

struct ixc2apb_info {
	__u32	max_tx_len;
	__u32	max_rx_len;
};

struct i2c2apb_msg {
	__u32 num;
	struct i2c_msg *msg;
};


#define IxC2APB_MAGIC		0x54				/* "T" */
#define IxC2APB_SET_PWR		_IOW(IxC2APB_MAGIC,  0x01, struct ixc2apb_msg)
#define IxC2APB_SET_CMD		_IOW(IxC2APB_MAGIC,  0x02, struct ixc2apb_msg)
#define IxC2APB_GET_CMD		_IOWR(IxC2APB_MAGIC, 0x03, struct ixc2apb_msg)
#define IxC2APB_SET_DATA	_IOW(IxC2APB_MAGIC,  0x04, struct ixc2apb_msg)
#define IxC2APB_GET_DATA	_IOWR(IxC2APB_MAGIC, 0x05, struct ixc2apb_msg)
#define IxC2APB_SET_STAT	_IOW(IxC2APB_MAGIC,  0x06, struct ixc2apb_msg)
#define IxC2APB_GET_STAT	_IOWR(IxC2APB_MAGIC, 0x07, struct ixc2apb_msg)
#define IxC2APB_CLR_STAT	_IOW(IxC2APB_MAGIC,  0x08, struct ixc2apb_msg)
#define IxC2APB_RST_STAT	_IOW(IxC2APB_MAGIC,  0x09, struct ixc2apb_msg)
#define IxC2APB_I2C_MSG_RW	_IOW(IxC2APB_MAGIC,  0x0a, struct i2c2apb_msg)

#endif /* __UAPI_IxC2APB_H__ */
