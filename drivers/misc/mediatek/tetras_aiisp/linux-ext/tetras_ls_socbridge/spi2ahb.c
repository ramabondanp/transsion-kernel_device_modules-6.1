/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Change Logs:
 * Date           Author         Notes
 * 2022-1-20     yanghua     Initialize.
 */

/**
 * @brief   Spi2ahb protocol driver
 * @date    2021-12-24
 */

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include "spibridge.h"
#include "ls_bridge_internel.h"
#include "spi_bridge_drv.h"

#define CMD_LEN_1				0
#define CMD_LEN_4				1
#define CMD_LEN_8				2
#define CMD_LEN_16				3
#define CMD_LEN_32				4
#define CMD_LEN_64				5

#define INVALID_DATA_LEN_CODE			0

#define WRITEx(len_code)			(0x80 | len_code)
#define DIRECTREAD(len_code, wait_code)		\
		   ((0x60 | len_code) | (wait_code << 3))
#define WRITESTATUS				(0xC0)
#define WRITESTATUS1_WAIT			(0xC8)
#define WRITESTATUS2_WAIT			(0xD0)
#define READSTATUS				(0x40)
#define READSTATUS1_WAIT			(0x48)
#define READSTATUS2_WAIT			(0x50)
#define READREQ(len_code)			(0x20 | len_code)
#define READx(len_code)				(0x00 | len_code)
#define READx1_WAIT(len_code)			(0x08 | len_code)
#define READx2_WAIT(len_code)			(0x10 | len_code)

#define WRITE_STATUS_ERR_MASK			BIT(6)
#define WRITE_STATUS_READY			BIT(7)
#define READ_STATUS_ERR_MASK			BIT(6)
#define READ_STATUS_READY			BIT(7)

#define SPI_ADDR_MASK				GENMASK(23, 0)
#define HADDR_MASK				GENMASK(31, 25)
#define CONFIG_SPACE_BIT			BIT(23)
#define AHB_2_SPI_CONFIG_ADDR(AHB_ADDR)		\
			      (((AHB_ADDR >> 2) & SPI_ADDR_MASK) | CONFIG_SPACE_BIT)
#define AHB_2_SPI_COMMON_ADDR(AHB_ADDR)		\
			      (((AHB_ADDR >> 2) & SPI_ADDR_MASK) & (~CONFIG_SPACE_BIT))

#define PROTOCOL_MAX_WORD_NUM			64

#define READFLAG_2_WAITCYCLE(read_flag)		(read_flag)

static u8 len_code[65] = {
	[1] = CMD_LEN_1 + 1,
	[4] = CMD_LEN_4 + 1,
	[8] = CMD_LEN_8 + 1,
	[16] = CMD_LEN_16 + 1,
	[32] = CMD_LEN_32 + 1,
	[64] = CMD_LEN_64 + 1,
};

struct spi2ahb_cfg_data {
	u32 tx_fifo_len;
	u32 rx_fifo_len;
	u32 last_haddr;
	u32 conf_addr_start;
	u32 conf_addr_end;
	u32 haddr_conf_addr;
};

static inline u8 len_to_code(u32 data_len)
{
	return len_code[data_len] - 1;
}

static void *spi2ahb_get_cfg_data(struct spi_device *spi)
{
	struct spibridge_priv *priv;
	struct spibridge *spi2ahb;

	priv = spi_get_drvdata(spi);
	spi2ahb = priv->bridges[SPI_BRIDGE_SPI2AHB];

	return spi2ahb->cfg_data;
}

static u32 get_len_per_xfer(struct spi_device *spi, u32 data_len, bool is_write)
{
	u32 rx_fifo_len;
	u32 max_len = PROTOCOL_MAX_WORD_NUM;
	struct spi2ahb_cfg_data *cfg;

	cfg = spi2ahb_get_cfg_data(spi);
	rx_fifo_len = cfg->rx_fifo_len;

	/* Read data len cannot exceed rx fifo depth */
	if (is_write == false)
		max_len = MIN(rx_fifo_len, max_len);

	while (max_len >= 4) {
		if (data_len >= max_len)
			return max_len;
		else
			max_len = max_len / 2;
	}

	return 1;
}

static int spi2ahb_writex(struct spi_device *spi, u32 spi_addr,
                          const u32 *data, u32 data_len)
{
	u8 cmd;
	struct spi_transfer xfer[2];
	u8 len_code;

	memset(xfer, 0, sizeof(xfer));

	len_code = len_to_code(data_len);
	if (len_code == 0xff) {
		dev_err(&spi->dev, "invalid len_code!\n");
		return -1;
	}

	cmd = WRITEx(len_code);
	spi_addr |= (uint32_t)cmd << 24;

	/* 1 byte instruction + 3 bytes addr */
	xfer[0].len = 4;
	xfer[0].tx_buf = &spi_addr;
	xfer[0].bits_per_word = 32;

	xfer[1].len = (data_len * 4);
	xfer[1].tx_buf = data;
	xfer[1].bits_per_word = 32;

	return spi_sync_transfer(spi, xfer, ARRAY_SIZE(xfer));
}

static int spi2ahb_get_status(struct spi_device *spi, bool is_write, u8 *status)
{
	/* Read with 1 wait cycle */
	u32 cmd;
	struct spi_transfer xfer;
	u32 in_buf;
	int ret;

	if (is_write)
		cmd = WRITESTATUS1_WAIT;
	else
		cmd = READSTATUS1_WAIT;
	cmd = cmd << 9;

	memset(&xfer, 0, sizeof(xfer));

	xfer.len = 4;
	xfer.tx_buf = &cmd;
	xfer.rx_buf = &in_buf;
	xfer.bits_per_word = 17;

	ret = spi_sync_transfer(spi, &xfer, 1);
	if (ret)
		return ret;

	*status = (u8)(in_buf);

	return ret;
}


static int __spi2ahb_write_one(struct spi_device *spi, u32 spi_addr,
                               const u32 *data, u32 data_len, u32 flag)
{
	int ret = 0;
	unsigned long timeout;
	u8 status = 0;

	ret = spi2ahb_writex(spi, spi_addr, data, data_len);
	if (ret) {
		dev_err(&spi->dev, "spi2ahb writex cmd failed!\n");
		return -ERR_WR_XFER_DATA;
	}

	if (flag == WRITE_NO_CHECK)
		return 0;

	timeout = jiffies + msecs_to_jiffies(1000);
	while (status != WRITE_STATUS_READY) {
		ret = spi2ahb_get_status(spi, true, &status);
		if (ret < 0)
			return ret;
		/* AHB error response */
		if (status & WRITE_STATUS_ERR_MASK) {
			dev_err(&spi->dev, "spi2ahb write ahb error response!\n");
			return -ERR_WR_AHB_ERR;
		}
		if (time_after(jiffies, timeout)) {
			dev_err(&spi->dev, "spi2ahb write timeout!\n");
			return -ERR_WR_TIMEOUT;
		}
	}

	return 0;
}

#define SET_ADDR_EACH_TIME  1
static int ahb_to_spi_addr(struct spi_device *spi, u32 ahb_addr, u32 *spi_addr)
{
	int ret;
	u32 haddr;
	u32 conf_addr_start;
	u32 conf_addr_end;
	u32 haddr_conf_addr;
	struct spi2ahb_cfg_data *cfg;

	cfg = spi2ahb_get_cfg_data(spi);
	conf_addr_start = cfg->conf_addr_start;
	conf_addr_end = cfg->conf_addr_end;
	haddr_conf_addr = cfg->haddr_conf_addr;

	if ((ahb_addr < conf_addr_end) && (ahb_addr >= conf_addr_start)) {
		*spi_addr = AHB_2_SPI_CONFIG_ADDR(ahb_addr);
		return 0;
	}

	haddr = ahb_addr & HADDR_MASK;
#if SET_ADDR_EACH_TIME
	ret = __spi2ahb_write_one(spi, AHB_2_SPI_CONFIG_ADDR(haddr_conf_addr),
				 &haddr, 1, WRITE_BY_POLLING);
	if (ret) {
		dev_err(&spi->dev, "set haddr failed!\n");
		return ret;
	}
#else
	if (haddr != cfg->last_haddr) {
		ret = __spi2ahb_write_one(spi,
					 AHB_2_SPI_CONFIG_ADDR(haddr_conf_addr),
					 &haddr, 1, WRITE_BY_POLLING);
		if (ret) {
			dev_err(&spi->dev, "set haddr failed!\n");
			return ret;
		}
		cfg->last_haddr = haddr;
	}
#endif

	*spi_addr = AHB_2_SPI_COMMON_ADDR(ahb_addr);

	return 0;
}

static int spi2ahb_write_one(struct spi_device *spi, u32 ahb_addr,
                             const u32 *data, u32 data_len, u32 flag)
{
	int ret;
	u32 spi_addr;

	ret = ahb_to_spi_addr(spi, ahb_addr, &spi_addr);
	if (ret < 0) {
		dev_err(&spi->dev, "write addr transform failed!\n");
		return ret;
	}

	return __spi2ahb_write_one(spi, spi_addr, data, data_len, flag);
}

static int spi2ahb_write(struct device *dev, u32 ahb_addr,
			 const u8 *data, u32 data_len, u32 flag)
{
	int ret = 0;
	u32 len;
	u32 w_data_len = data_len * 4;
	struct spi_device *spi;

	spi = to_spi_device(dev);
	while (w_data_len) {
		len = get_len_per_xfer(spi, w_data_len, true);
		ret = spi2ahb_write_one(spi, ahb_addr, (u32 *)data, len, flag);
		if (ret) {
			dev_err(&spi->dev,
			        "write addr 0x%x len %d failed!\n", ahb_addr, len);
			return ret;
		}
		ahb_addr += len * 4;
		data += len * 4;
		w_data_len -= len;
	}

	return 0;
}

static int spi2ahb_direct_readx(struct spi_device *spi, u32 spi_addr,
				u32 *data, u32 data_len, u8 wait_cycles_code)
{
	int len_code;
	u8 cmd;
	struct spi_transfer xfer[2];
	u8 op_buf[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	u32 dummy_nbytes;

	memset(xfer, 0, sizeof(xfer));

	len_code = len_to_code(data_len);
	if (len_code == 0xff) {
		dev_err(&spi->dev, "invalid len_code!\n");
		return -1;
	}

	cmd = DIRECTREAD(len_code, wait_cycles_code);

	op_buf[0] = cmd;
	op_buf[1] = (u8)(spi_addr >> 16);
	op_buf[2] = (u8)(spi_addr >> 8);
	op_buf[3] = (u8)spi_addr;

	/*
	 * dummy bytes, TODO: add support for quad spi .
	 * 00 - 8 wait cycles - 1 byte for standard spi;
	 * 01 - 16 wait cycles - 2 bytes for standard spi;
	 * 10 - 24 wait cycles - 3 bytes for standard spi;
	 * 11 - 32 wait cycles - 4 bytes for standard spi;
	 */
	dummy_nbytes = wait_cycles_code + 1;
	xfer[0].len = 4 + dummy_nbytes;
	xfer[0].tx_buf = op_buf;
	xfer[0].bits_per_word = 8;

	/* data, data_len *4 bytes */
	xfer[1].len = data_len * 4;
	xfer[1].rx_buf = data;
	xfer[1].bits_per_word = 32;

	return spi_sync_transfer(spi, xfer, ARRAY_SIZE(xfer));
}

static int spi2ahb_readreq(struct spi_device *spi, u32 spi_addr, u32 data_len)
{
	u8 cmd;
	int len_code;
	struct spi_transfer xfer;

	memset(&xfer, 0, sizeof(xfer));
	len_code = len_to_code(data_len);
	if (len_code == 0xff) {
		dev_err(&spi->dev, "invalid len_code!\n");
		return -1;
	}
	cmd = READREQ(len_code);

	spi_addr |= (uint32_t)cmd << 24;
	/* instruction + addr = 4 bytes */
	xfer.len = 4;
	xfer.tx_buf = &spi_addr;
	xfer.bits_per_word = 32;

	return spi_sync_transfer(spi, &xfer, 1);
}

static int spi2ahb_readx(struct spi_device *spi, u32 *data, u32 data_len)
{
	int len_code;
	u16 cmd;
	struct spi_transfer xfer[2];
	int ret;
	struct spi_message msg;

	memset(xfer, 0, sizeof(xfer));

	spi_message_init(&msg);

	len_code = len_to_code(data_len);
	if (len_code == 0xff) {
		dev_err(&spi->dev, "invalid len_code!\n");
		return -1;
	}

	cmd = READx1_WAIT(len_code) << 1;

	/* instruction, 1 byte + 1 cycle dummy clock */
	xfer[0].len = 2;
	xfer[0].tx_buf = &cmd;
	xfer[0].bits_per_word = 9;
	spi_message_add_tail(&xfer[0],  &msg);

	/* 1 wait cycle and 1 byte of data[0] */
	xfer[1].len = data_len * 4;
	xfer[1].rx_buf = data;
	xfer[1].bits_per_word = 32;
	spi_message_add_tail(&xfer[1],  &msg);

	ret = spi_sync(spi, &msg);

	return ret;
}

static int spi2ahb_read_by_polling(struct spi_device *spi, u32 spi_addr,
                                   u32 *data, u32 data_len)
{
	int ret;
	unsigned long timeout;
	u8 status = 0;

	ret = spi2ahb_readreq(spi, spi_addr, data_len);
	if (ret) {
		dev_err(&spi->dev, "read request failed!\n");
		return -ERR_RD_XFER_DATA;
	}

	timeout = jiffies + msecs_to_jiffies(1000);
	while (status != READ_STATUS_READY) {
		ret = spi2ahb_get_status(spi, false, &status);
		if (ret < 0)
			return ret;
		/* AHB error response */
		if (status & READ_STATUS_ERR_MASK) {
			dev_err(&spi->dev, "spi2ahb read ahb error response!\n");
			return -ERR_RD_AHB_ERR;
		}
		if (time_after(jiffies, timeout)) {
			dev_err(&spi->dev, "spi2ahb read timeout! status 0x%x\n", status);
			return -ERR_RD_TIMEOUT;
		}
	}

	ret = spi2ahb_readx(spi, data, data_len);
	if (ret) {
		dev_err(&spi->dev, "spi2ahb readx error!\n");
		return -ERR_RD_XFER_DATA;
	}

	return 0;
}


static int spi2ahb_read_one(struct spi_device *spi, u32 ahb_addr,
			    u32 *data, u32 data_len, u32 read_flag)
{
	int ret;
	u32 spi_addr;
	u8 wait_cycles_code;

	ret = ahb_to_spi_addr(spi, ahb_addr, &spi_addr);
	if (ret < 0) {
		dev_err(&spi->dev, "read addr transform failed!\n");
		return ret;
	}

	wait_cycles_code = READFLAG_2_WAITCYCLE(read_flag);

	if (read_flag <= READ_WITH_32_WAIT_CYCLES) {
		ret = spi2ahb_direct_readx(spi, spi_addr, data, data_len, wait_cycles_code);
		if (ret) {
			dev_err(&spi->dev, "direct read failed!\n");
			return -ERR_RD_XFER_DATA;
		}
	} else {
		ret = spi2ahb_read_by_polling(spi, spi_addr, data, data_len);
		if (ret) {
			dev_err(&spi->dev, "polling read failed!\n");
			return ret;
		}
	}

	return ret;
}

static int spi2ahb_read(struct device *dev, u32 ahb_addr, u8 *data,
			u32 data_byte_len, u32 read_flag)
{
	int ret = 0;
	u32 data_w_len = data_byte_len * sizeof(u32);
	u32 len;
	struct spi_device *spi;
	u32 *u32_buf;

	u32_buf = (u32 *)data;
	spi = to_spi_device(dev);
	while (data_w_len) {
		len = get_len_per_xfer(spi, data_w_len, false);
		ret = spi2ahb_read_one(spi, ahb_addr,
				       u32_buf, len, read_flag);
		if (ret) {
			dev_err(&spi->dev,
			        "read addr 0x%x len %d failed!\n", ahb_addr, len);
			return ret;
		}
		ahb_addr += len * 4;
		u32_buf += len;
		data_w_len -= len;
	}

	return 0;
}

static int spi2ahb_dt_probe(struct device *dev, struct device_node *nc, u32 bridge_id)
{
	u32 tx_fifo_len;
	u32 rx_fifo_len;
	u32 conf_addr_start;
	u32 haddr_conf_addr;
	struct spi2ahb_cfg_data *data;
	struct spi_device *parent;
	struct spibridge_priv *spibridge = dev_get_drvdata(dev);

	parent = to_spi_device(dev);
	if (of_property_read_u32(nc, "tx-fifo-len", &tx_fifo_len)) {
		dev_err(&parent->dev, "spi2ahb: not found tx-fifo-len in dt!\n");
		return -EINVAL;
	}

	if (of_property_read_u32(nc, "rx-fifo-len", &rx_fifo_len)) {
		dev_err(&parent->dev, "spi2ahb: not found rx-fifo-len in dt!\n");
		return -EINVAL;
	}

	if (of_property_read_u32(nc, "conf_addr_start", &conf_addr_start)) {
		dev_err(&parent->dev, "spi2ahb: not found conf_addr_start in dt!\n");
		return -EINVAL;
	}

	if (of_property_read_u32(nc, "haddr_conf_addr", &haddr_conf_addr)) {
		dev_err(&parent->dev, "spi2ahb: not found haddr_conf_addr in dt!\n");
		return -EINVAL;
	}

	data = devm_kzalloc(&parent->dev,
			   sizeof(struct spi2ahb_cfg_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->tx_fifo_len = tx_fifo_len;
	data->rx_fifo_len = rx_fifo_len;
	data->conf_addr_start = conf_addr_start;
	data->conf_addr_end = conf_addr_start + 32 * MB;
	data->last_haddr = 0;
	data->haddr_conf_addr = haddr_conf_addr;

	dev_info(&parent->dev, "spi2ahb TX fifo depth %d, RX fifo depth %d\n",
	         data->tx_fifo_len, data->rx_fifo_len);

	dev_info(&parent->dev,
		"spi2ahb cfg space start 0x%x, end 0x%x, cfg_reg 0x%x\n",
	        data->conf_addr_start, data->conf_addr_end, data->haddr_conf_addr);

	spibridge->bridges[bridge_id]->cfg_data = data;

	return 0;
}

struct ls_bridge_ops spi2ahb = {
	.name = "spi2ahb",
	.read = spi2ahb_read,
	.write = spi2ahb_write,
	.dt_probe = spi2ahb_dt_probe,
};
