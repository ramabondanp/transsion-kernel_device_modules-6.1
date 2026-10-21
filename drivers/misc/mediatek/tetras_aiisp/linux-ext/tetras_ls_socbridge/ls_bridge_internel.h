/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Change Logs:
 * Date           Author         Notes
 * 2022-01-20     yanghua     Initialize.
 */

/**
 * @brief   Spibridge kernel internel header file
 * @date    2021-01-20
 */

#ifndef __LS_BRIDGE_INTERNEL_H__
#define __LS_BRIDGE_INTERNEL_H__

#include <linux/types.h>
#include <linux/spi/spi.h>
#include <linux/mutex.h>
#include "spi2axi_csr.h"

#define KB					(1024)
#define MB					(KB * KB)
#define XFER_MAX_BUFFER_LEN			(2 * MB)
#define MAX_RECORD_NUM				(16)

//#define MIN(a, b) ((a) < (b) ? (a) : (b))

extern struct i2c_driver i2cbridge_driver;
extern struct spi_driver spibridge_driver;

enum write_flag {
	WRITE_NO_CHECK = 0,
	WRITE_BY_POLLING,
};

struct xfer_record {
	u32 addr;
	u32 len;
	bool is_write;
	int ret;
};

struct bridge_priv {
	struct spi_device *spibridge_slave;
	struct i2c_client *i2cbridge_slave;
	struct mutex xfer_lock;
	struct mutex ioctl_lock;
	struct xfer_record records[MAX_RECORD_NUM];
	u32 cur_rec_id;
	u8 *ioc_xfer_buf;
	u16 version_id;
};

extern struct bridge_priv *bridge_dev;

/* ls_bridge common ops */
struct ls_bridge_ops {
	const char *name;
	int (*read)(struct device *dev, u32 axi_addr,
		    u8 *data, u32 data_len, u32 flag);
	int (*write)(struct device *dev, u32 axi_addr,
		     const u8 *data, u32 data_len, u32 flag);
	int (*read_csr)(struct device *dev, u32 *val, u8 csr);
	int (*write_csr)(struct device *dev, u32 val, u8 csr);
	int (*send_single_cmd)(struct device *dev, u8 cmd);
	int (*dt_probe)(struct device *dev,
			struct device_node *nc, u32 bridge_id);
	int (*scatter_write)(struct device *dev, u32 scatter_num,
			     struct scatter_wr_unit *scatters);
	int (*reinit)(struct device *dev);
};

extern struct ls_bridge_ops spi2ahb;
extern struct ls_bridge_ops spi2axi;
extern struct ls_bridge_ops i2c2axi;

/* tetras_ls_socbridge provider */
int cpy_buf_to_msg(void __user *ubuf, struct ls_bridge_msg *msg, struct device *dev);
void xfer_log_record(u32 addr, u32 len, bool is_write, int ret);
void xfer_log_dump(struct device *dev);
struct device *sel_device(u8 device_flag, bool bridge2axi);

#endif /* __SPIBRIDGE_INTERNEL_H__ */
