/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Change Logs:
 * Date           Author         Notes
 * 2023-4-14     yangyuzun     Initialize.
 */

/**
 * @brief   i2c2axi protocol driver
 * @date    2023-4-14
 */

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/i2c.h>
#include "spibridge.h"
#include "ls_bridge_internel.h"
#include "ls_bridge2axi.h"
#include "i2c_bridge_drv.h"

static int i2c2axi_send_single_cmd(struct device *dev, u8 inst)
{
	int ret = 0;
	struct i2c_msg msg;
	struct i2c_client *client;

	client = to_i2c_client(dev);
	msg.flags = 0;
	msg.addr = client->addr;
	msg.buf = &inst;
	msg.len = 1;

	ret = i2c_transfer(client->adapter, &msg, 1);

	return ret < 0 ? ret : (ret != 1 ? -EIO : 0);
}

static int i2c2axi_write_csr(struct device *dev, u32 val, u8 csr)
{
	int i = 0, ret;
	struct i2c_msg msg;
	struct i2c_client *client;
	u8 buf[CSR_BUF_LEN] = {0};

	client = to_i2c_client(dev);
	buf[i++] = CMD_CSR_WR;
	buf[i++] = csr;
	memcpy(&buf[i++], &val, sizeof(val));

	msg.flags = 0;
	msg.addr = client->addr;
	msg.buf = buf;
	msg.len = CSR_BUF_LEN;

	ret = i2c_transfer(client->adapter, &msg, 1);

	return ret < 0 ? ret : (ret != 1 ? -EIO : 0);
}

static int i2c2axi_read_csr(struct device *dev, u32 *val, u8 csr)
{
	int ret;
	u8 tx_buf[2] = {0};
	struct i2c_msg msgs[2];
	struct i2c_client *client;

	client = to_i2c_client(dev);
	tx_buf[0] = CMD_CSR_RD;
	tx_buf[1] = csr;

	msgs[0].flags = 0;
	msgs[0].addr  = client->addr;
	msgs[0].buf   = tx_buf;;
	msgs[0].len   = 2;

	msgs[1].flags = I2C_M_RD;
	msgs[1].addr  = client->addr;
	msgs[1].buf   = (u8 *)val;
	msgs[1].len   = CSR_RD_BUF_LEN;

	ret = i2c_transfer(client->adapter, &msgs[0], 2);

	return ret < 0 ? ret : (ret != ARRAY_SIZE(msgs) ? -EIO : 0);
}

static int i2c2axi_read_request(struct i2c_client *client, u32 axi_addr)
{
	int i = 0, ret;
	struct i2c_msg msgs;
	u8 tx_buf[RR_WR_BUF_LEN] = {0};

	tx_buf[i++] = CMD_AXI_RR;
	tx_buf[i++] = (u8)((axi_addr >> 24) & 0xff);
	tx_buf[i++] = (u8)((axi_addr >> 16) & 0xff);
	tx_buf[i++] = (u8)((axi_addr >> 8) & 0xff);
	tx_buf[i++] = (u8)(axi_addr & 0xff);

	msgs.flags = 0;
	msgs.addr = client->addr;
	msgs.buf = tx_buf;
	msgs.len = RR_WR_BUF_LEN;

	ret = i2c_transfer(client->adapter, &msgs, 1);

	return ret < 0 ? ret : (ret != 1 ? -EIO : 0);
}

static int i2c2axi_read_data(struct i2c_client *client, u8 *data, u32 byte_len)
{
	int ret;
	struct i2c_msg msgs[2];
	u8 rd_inst_buf[2];

	rd_inst_buf[0] = CMD_AXI_RD;
	rd_inst_buf[1] = 0xff;

	msgs[0].flags = 0;
	msgs[0].addr = client->addr;
	msgs[0].buf = rd_inst_buf;
	msgs[0].len = 2;

	msgs[1].flags = I2C_M_RD;
	msgs[1].addr  = client->addr;
	msgs[1].buf   = data;
	msgs[1].len   = byte_len;

	ret = i2c_transfer(client->adapter, msgs, 2);

	return ret < 0 ? ret : (ret != 2 ? -EIO : 0);
}

static int i2c2axi_write_data(struct i2c_client *client, u32 axi_addr,
			      const u8 *data, u32 byte_len)
{
	int ret, i = 0;
	u8 *tx_buf;
	u32 buf_len = byte_len + RR_WR_BUF_LEN;
	struct i2c_msg msg;

	tx_buf = kmalloc(buf_len, GFP_KERNEL);
	if (!tx_buf)
		return -ENOMEM;

	tx_buf[i++] = CMD_AXI_WR;
	tx_buf[i++] = (u8)((axi_addr >> 24) & 0xff);
	tx_buf[i++] = (u8)((axi_addr >> 16) & 0xff);
	tx_buf[i++] = (u8)((axi_addr >> 8) & 0xff);
	tx_buf[i++] = (u8)(axi_addr & 0xff);

	/**
	 * LIMIT: some i2c adapter don't support
	 * flag I2C_M_NOSTART, so that memory must
	 * be allocated repeatedly.
	 */
	memcpy(&tx_buf[i++], data, byte_len);

	msg.flags = 0;
	msg.addr = client->addr;
	msg.buf = tx_buf;
	msg.len = buf_len;
	ret = i2c_transfer(client->adapter, &msg, 1);
	kfree(tx_buf);

	return ret < 0 ? ret : (ret != 1 ? -EIO : 0);
}

static int i2c2axi_write(struct device *dev, u32 axi_addr,
			 const u8 *data, u32 data_len, u32 flag)
{
	u32 reg_glb_csr = 0;
	struct i2c_client *client;

	client = to_i2c_client(dev);

	if (i2c2axi_write_csr(dev, data_len, BYTE_COUNT_OFFSET)) {
		dev_err(dev, "i2c2axi write byte count error\n");
		return -ERR_WR_XFER_DATA;
	}

	if (i2c2axi_write_data(client, axi_addr, data, data_len)) {
		dev_err(dev, "i2c write data error\n");
		return -ERR_WR_XFER_DATA;
	}

	if (polling_xfer_finish(dev, &reg_glb_csr)) {
		dev_err(dev, "i2c polling xfer finish failed.\n");
		return -ERR_WR_TIMEOUT;
	}

	if (error_detect(dev, reg_glb_csr)) {
		dev_err(dev, "i2c2axi error detect failed.\n");
		return -ERR_RD_XFER_DATA;
	}

	return 0;
}

int i2c2axi_read(struct device *dev, u32 axi_addr,
		 u8 *data, u32 data_len, u32 flag)
{
	u32 reg_glb_csr = 0;
	struct i2c_client *client;

	client = to_i2c_client(dev);
	if (i2c2axi_write_csr(dev, data_len, BYTE_COUNT_OFFSET)) {
		dev_err(dev, "i2c2axi write byte count error\n");
		return -ERR_WR_XFER_DATA;
	}

	if (i2c2axi_read_request(client, axi_addr)) {
		dev_err(dev, "i2c read request error\n");
		return -ERR_RD_XFER_DATA;
	}

	if (polling_fifo_full(dev, data_len))
		return -ERR_RD_TIMEOUT;

	if (i2c2axi_read_data(client, data, data_len)) {
		dev_err(dev, "i2c read error\n");
		return -ERR_RD_XFER_DATA;
	}

	if (polling_xfer_finish(dev, &reg_glb_csr)) {
		dev_err(dev, "i2c polling xfer finish failed.\n");
		return -ERR_RD_TIMEOUT;
	}

	if (error_detect(dev, reg_glb_csr)) {
		dev_err(dev, "i2c2axi error detect failed.\n");
		return -ERR_RD_XFER_DATA;
	}

	return 0;
}

static int i2c2axi_scatter_write(struct device *dev, u32 scatter_num,
				 struct scatter_wr_unit *scatters)
{
	u8 *scatter_buf;
	u32 size = 0;
	int ret;
	u32 reg_glb_csr;
	struct i2c_client *client;
	struct i2c_msg msg;

	client = to_i2c_client(dev);

	if (check_scatter(dev, scatter_num, scatters, &size))
		return -EINVAL;

	scatter_buf = devm_kzalloc(dev, size, GFP_KERNEL);
	if (!scatter_buf)
		return -ENOMEM;

	scatter_buf_fill(scatter_buf, scatter_num, scatters);

	msg.flags = 0;
	msg.addr = client->addr;
	msg.buf = scatter_buf;
	msg.len = size;

	ret = i2c_transfer(client->adapter, &msg, 1);

	if (ret != 1) {
		dev_err(dev, "i2c2axi scatter write transfer failed!\n");
		goto scatter_out;
	}

	ret = polling_xfer_finish(dev, &reg_glb_csr);
	if (ret) {
		ret = -ERR_WR_TIMEOUT;
		goto scatter_out;
	}

	ret = error_detect(dev, reg_glb_csr);
	if (ret) {
		ret = -ERR_WR_XFER_DATA;
		goto scatter_out;
	}

scatter_out:
	devm_kfree(dev, scatter_buf);

	return ret;
}

struct ls_bridge_ops i2c2axi = {
	.name = "i2c2axi",
	.write = i2c2axi_write,
	.read = i2c2axi_read,
	.read_csr = i2c2axi_read_csr,
	.write_csr = i2c2axi_write_csr,
	.send_single_cmd = i2c2axi_send_single_cmd,
	.scatter_write = i2c2axi_scatter_write,
};
