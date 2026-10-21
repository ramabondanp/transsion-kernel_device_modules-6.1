/*
 * (C) Copyright 2025, Imvision Co., Ltd
 * This file is classified as confidential level C4 within Imvision
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Change Logs:
 * Date           Author         Notes
 * 2022-1-20     yanghua     Initialize.
 */

/**
 * @brief   Spi2axi protocol driver
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
#ifdef CONFIG_USING_QSPI
#include <linux/spi/spi-mem.h>
#endif
#include "ls_bridge_internel.h"
#include "spi_bridge_drv.h"
#include "ls_bridge2axi.h"

#define CONFIG_TUNING_ENABLE

#define SPI2AXI_MIN_SPEED_DEF			(100000)
#define SPI2AXI_MAX_SPEED_DEF			(60000000)
#define SPI2AXI_TUNE_NUM_AVAIL_WATER_LEVEL	(2)
#define SPI2AXI_TUNE_NUM_BEST_WATER_LEVEL	(4)

struct spi2axi_ctx {
#ifdef CONFIG_USING_QSPI
	struct spi_mem mem;
#endif
	uint32_t tune_val;
	uint32_t tune_num;
};

/* spi2axi typical speed value */
static u32 spi2axi_typical_speed[] = {700000, 1100000, 3800000, 10000000, 20000000, 40000000};

static void *spi2axi_get_ctx(struct spi_device *spi)
{
	struct spibridge_priv *priv;
	struct spibridge *spi2axi;

	priv = spi_get_drvdata(spi);
	spi2axi = priv->bridges[SPI_BRIDGE_SPI2AXI];

	return spi2axi->cfg_data;
}

static int spi2axi_single_cmd_send(struct device *dev, u8 cmd)
{
	struct spi_device *spi = to_spi_device(dev);

#ifndef CONFIG_USING_QSPI
	struct spi_transfer xfer;
	struct spibridge_priv *priv;

	priv = spi_get_drvdata(spi);
	memset(&xfer, 0x0, sizeof(xfer));

	xfer.len = 1;
	xfer.tx_buf = &cmd;
	xfer.bits_per_word = 8;

	return (priv->xfer_timedout = spi_sync_transfer(spi, &xfer, 1));
#else
	struct spi2axi_ctx *ctx;
	struct spi_mem_op op =
		SPI_MEM_OP(SPI_MEM_OP_CMD(cmd, 1),
				SPI_MEM_OP_NO_ADDR,
				SPI_MEM_OP_NO_DUMMY,
				SPI_MEM_OP_NO_DATA);

	ctx = spi2axi_get_ctx(spi);

	return spi_mem_exec_op(&ctx->mem, &op);
#endif
}

static int spi2axi_write_csr(struct device *dev, u32 val, u8 csr)
{
	struct spi_device *spi = to_spi_device(dev);

#ifndef CONFIG_USING_QSPI
	int i = 0;
	u8 buf[CSR_BUF_LEN] = {0};
	struct spi_transfer xfer;
	struct spibridge_priv *priv;

	priv = spi_get_drvdata(spi);

	memset(&xfer, 0x0, sizeof(xfer));

	buf[i++] = CMD_CSR_WR;
	buf[i++] = csr;
	buf[i++] = (u8)(val & 0xff);
	buf[i++] = (u8)((val >> 8) & 0xff);
	buf[i++] = (u8)((val >> 16) & 0xff);
	buf[i++] = (u8)((val >> 24) & 0xff);

	xfer.len = CSR_BUF_LEN;
	xfer.tx_buf = buf;
	xfer.bits_per_word = 8;

	return (priv->xfer_timedout = spi_sync_transfer(spi, &xfer, 1));
#else
	struct spi2axi_ctx *ctx;
	struct spi_mem_op op =
		SPI_MEM_OP(SPI_MEM_OP_CMD(CMD_CSR_WR, 1),
				SPI_MEM_OP_ADDR(1, csr, 1),
				SPI_MEM_OP_NO_DUMMY,
				SPI_MEM_OP_DATA_OUT(4, &val, 1));

	ctx = spi2axi_get_ctx(spi);

	return spi_mem_exec_op(&ctx->mem, &op);
#endif
}

static int spi2axi_read_csr(struct device *dev, u32 *val, u8 csr)
{
	struct spi_device *spi = to_spi_device(dev);

#ifndef CONFIG_USING_QSPI
	int ret;
	u8 txbuf[CSR_BUF_LEN] = {0};
	u8 rxbuf[CSR_BUF_LEN] = {0};
	struct spi_transfer xfer;
	struct spibridge_priv *priv;

	priv = spi_get_drvdata(spi);

	memset(&xfer, 0x0, sizeof(xfer));

	txbuf[0] = CMD_CSR_RD;
	txbuf[1] = csr;

	xfer.len = CSR_BUF_LEN;
	xfer.tx_buf = txbuf;
	xfer.rx_buf = rxbuf;
	xfer.bits_per_word = 8;

	ret = priv->xfer_timedout = spi_sync_transfer(spi, &xfer, 1);
	if (ret) {
		return ret;
	}

	*val = rxbuf[2] | (rxbuf[3] << 8) | (rxbuf[4] << 16) | (rxbuf[5] << 24);
	return ret;
#else
	struct spi2axi_ctx *ctx;

	struct spi_mem_op op =
		SPI_MEM_OP(SPI_MEM_OP_CMD(CMD_CSR_RD, 1),
				SPI_MEM_OP_ADDR(1, csr, 1),
				SPI_MEM_OP_NO_DUMMY,
				SPI_MEM_OP_DATA_IN(4, val, 1));

	ctx = spi2axi_get_ctx(spi);

	return spi_mem_exec_op(&ctx->mem, &op);
#endif
}

static int spi2axi_log_en(struct spi_device *spi)
{
	return ls_bridge2axi_glb_csr_setbits(&spi->dev, LOG_EN_MASK);
}

static int spi2axi_write_data(struct spi_device *spi, u32 axi_addr,
			      const u8 *data, u32 w_len)
{
#ifndef CONFIG_USING_QSPI
	int i = 0;
	u8 buf[RR_WR_BUF_LEN] = {0};
	struct spi_transfer xfers[2];
	struct spibridge_priv *priv;

	priv = spi_get_drvdata(spi);

	memset(xfers, 0x0, sizeof(xfers));

	buf[i++] = CMD_AXI_WR;
	buf[i++] = (u8)((axi_addr >> 24) & 0xff);
	buf[i++] = (u8)((axi_addr >> 16) & 0xff);
	buf[i++] = (u8)((axi_addr >> 8) & 0xff);
	buf[i++] = (u8)(axi_addr & 0xff);

	xfers[0].len = sizeof(buf);
	xfers[0].tx_buf = buf;
	xfers[0].bits_per_word = 8;

	xfers[1].len = w_len;
	xfers[1].tx_buf = data;
	xfers[1].bits_per_word = 8;

	return (priv->xfer_timedout = spi_sync_transfer(spi, xfers, ARRAY_SIZE(xfers)));
#else
	struct spi2axi_ctx *ctx;

	struct spi_mem_op op = SPI_MEM_OP(SPI_MEM_OP_CMD(CMD_AXI_WR, 1),
				SPI_MEM_OP_ADDR(4, axi_addr, 1),
				SPI_MEM_OP_NO_DUMMY,
				SPI_MEM_OP_DATA_OUT(w_len, data, 1));

	ctx = spi2axi_get_ctx(spi);

	return spi_mem_exec_op(&ctx->mem, &op);
#endif
}

static int spi2axi_read_request(struct spi_device *spi, u32 axi_addr)
{
#ifndef CONFIG_USING_QSPI
	int i = 0;
	u8 buf[RR_WR_BUF_LEN] = {0};
	struct spi_transfer xfer;
	struct spibridge_priv *priv;

	priv = spi_get_drvdata(spi);

	memset(&xfer, 0x0, sizeof(xfer));

	buf[i++] = CMD_AXI_RR;
	buf[i++] = (u8)((axi_addr >> 24) & 0xff);
	buf[i++] = (u8)((axi_addr >> 16) & 0xff);
	buf[i++] = (u8)((axi_addr >> 8) & 0xff);
	buf[i++] = (u8)(axi_addr & 0xff);

	xfer.len = sizeof(buf);
	xfer.tx_buf = buf;
	xfer.bits_per_word = 8;

	return (priv->xfer_timedout = spi_sync_transfer(spi, &xfer, 1));
#else
	struct spi2axi_ctx *ctx;
	struct spi_mem_op op =
		SPI_MEM_OP(SPI_MEM_OP_CMD(CMD_AXI_RR, 1),
				SPI_MEM_OP_ADDR(4, axi_addr, 1),
				SPI_MEM_OP_NO_DUMMY,
				SPI_MEM_OP_NO_DATA);

	ctx = spi2axi_get_ctx(spi);

	return spi_mem_exec_op(&ctx->mem, &op);
#endif
}

#define MAX_BURST_LEN	16

static int spi2axi_read_data(struct spi_device *spi, u8 *data, u32 len)
{
#ifndef CONFIG_USING_QSPI
	u8 buf[2] = {0};
	struct spi_transfer xfers[2];
	struct spibridge_priv *priv;

	priv = spi_get_drvdata(spi);

	memset(xfers, 0x0, sizeof(xfers));

	/* cmd and dummy */
	buf[0] = CMD_AXI_RD;
	buf[1] = 0xff;

	xfers[0].len = sizeof(buf);
	xfers[0].tx_buf = buf;
	xfers[0].bits_per_word = 8;

	xfers[1].len = len;
	xfers[1].rx_buf = data;
	xfers[1].bits_per_word = 8;

	return (priv->xfer_timedout = spi_sync_transfer(spi, xfers, ARRAY_SIZE(xfers)));
#else
	struct spi2axi_ctx *ctx;

	struct spi_mem_op op =
		SPI_MEM_OP(SPI_MEM_OP_CMD(CMD_AXI_RD, 1),
				SPI_MEM_OP_NO_ADDR,
				SPI_MEM_OP_DUMMY(1, 1),
				SPI_MEM_OP_DATA_IN(len, data, 1));

	ctx = spi2axi_get_ctx(spi);

	return spi_mem_exec_op(&ctx->mem, &op);
#endif
}

static int set_xfer_bytes(struct device *dev, u32 bytes)
{
	int ret;

	ret = spi2axi_write_csr(dev, bytes, BYTE_COUNT_OFFSET);
	if (ret)
		dev_err(dev, "spi write byte count error\n");

	return ret;
}

struct tuning_reg {
	uint8_t offset;
	uint32_t val;
};

static struct tuning_reg tuning_arr[] = {
	{CALI_PAT_A_OFFSET, CALI_PAT_A_VAL},
	{CALI_PAT_B_OFFSET, CALI_PAT_B_VAL},
	{CALI_PAT_C_OFFSET, CALI_PAT_C_VAL},
	{CALI_PAT_D_OFFSET, CALI_PAT_D_VAL},
};

static int check_pattern(struct spi_device *spi)
{
	int i;
	uint32_t pattern;
	struct tuning_reg *reg;

	for (i = 0; i < ARRAY_SIZE(tuning_arr); i++) {
		reg = &tuning_arr[i];
		if (spi2axi_read_csr(&spi->dev, &pattern, reg->offset)) {
			dev_err(&spi->dev, "spi read fifo status error\n");
			return -1;
		}

		if (pattern != reg->val)
			return -1;
	}

	return 0;
}

#ifdef CONFIG_TUNING_ENABLE
static int set_timing_ctl_val(struct spi_device *spi, uint32_t read_adv_cycles)
{
	uint32_t timing_ctl_val;

	timing_ctl_val = READ_XFER_ADV_EN_MASK;
	timing_ctl_val |= READ_ADV_1SCLK << READ_ADV_SCLK_OFFSET;
	if ((spi->mode & (SPI_CPHA | SPI_CPOL)) == SPI_MODE_0)
		timing_ctl_val |= READ_ADV_MODE0 << READ_ADV_MODE_OFFSET;
	else
		timing_ctl_val |= READ_ADV_MODE3 << READ_ADV_MODE_OFFSET;

	timing_ctl_val |= read_adv_cycles << READ_ADV_CYCLE_OFFSET;
	timing_ctl_val |= READ_ADV_WR_KEY_VAL << READ_ADV_WR_KEY_OFSSET;

	return spi2axi_write_csr(&spi->dev, timing_ctl_val, TIMING_CTL_OFFSET);
}

static int set_tuned_val(struct spi_device *spi)
{
	struct spi2axi_ctx *ctx;

	ctx = spi2axi_get_ctx(spi);

	return set_timing_ctl_val(spi, ctx->tune_val);
}

static int tuning_mode_enable_ctl(struct spi_device *spi, bool enable)
{
	uint32_t val;

	val = TRAIN_CTL_KEY << TRAIN_CTL_KEY_SHIFT;
	if (enable)
		val |= TRAIN_CTL_EN_BIT;

	return spi2axi_write_csr(&spi->dev, val, TRAIN_CTL_OFFSET);
}

int spi2axi_bus_tuning(struct spi_device *spi, struct spi2axi_ctx *data)
{
	int start = -1;
	int end = -1;
	uint32_t tuning_val;
	uint32_t read_adv_cycles;
	uint32_t tuning_num = 0;

	data->tune_num = 0;

	dev_err(&spi->dev, "spi2axi tunning start\n");
	tuning_mode_enable_ctl(spi, true);
	for (read_adv_cycles = 0; read_adv_cycles < 16; read_adv_cycles++) {
		if (set_timing_ctl_val(spi, read_adv_cycles)) {
			dev_err(&spi->dev, "write time ctl failed\n");
			return -1;
		}
		if (check_pattern(spi)) {
			dev_err(&spi->dev, "%d cycles skip\n", read_adv_cycles);
			continue;
		} else {
			dev_err(&spi->dev, "%d cycles ok\n", read_adv_cycles);
			if (start == -1)
				start = read_adv_cycles;
			end = read_adv_cycles;
			tuning_num++;
		}
	}
	tuning_mode_enable_ctl(spi, false);

	if (start == -1) {
		dev_err(&spi->dev, "spi tunning failed\n");
		return -1;
	}
	tuning_val = (start + end) / 2;
	data->tune_val = tuning_val;
	dev_err(&spi->dev, "tranning end, range from %d to %d\n", start, end);
	if (set_timing_ctl_val(spi, tuning_val)) {
		dev_err(&spi->dev, "write time ctl failed\n");
		return -1;
	}

	data->tune_num = tuning_num;
	return 0;
}
#else
static int spi2axi_bus_tuning(struct spi_device *spi, struct spi2axi_ctx *data)
{
	if (check_pattern(spi)) {
		dev_err(&spi->dev, "spi2axi pattern check failed\n");
		return -1;
	}

	return 0;
}
#endif

int spi2axi_write(struct device *dev, u32 axi_addr,
		 const u8 *data, u32 data_len, u32 flag)
{
	int ret;
	u32 reg_glb_csr;
	struct spi_device *spi;

	spi = to_spi_device(dev);
#ifdef CONFIG_TUNING_ENABLE
	set_tuned_val(spi);
#endif
	ret = set_xfer_bytes(dev, data_len);
	if (ret)
		return -ERR_WR_XFER_DATA;

	ret = spi2axi_write_data(spi, axi_addr, data, data_len);
	if (ret) {
		dev_err(&spi->dev, "spi write data error\n");
		return -ERR_WR_XFER_DATA;
	}

	if (flag == WRITE_NO_CHECK)
		return 0;

	ret = polling_xfer_finish(dev, &reg_glb_csr);
	if (ret)
		return -ERR_WR_TIMEOUT;

	ret = error_detect(&spi->dev, reg_glb_csr);
	if (ret)
		return -ERR_WR_XFER_DATA;

	return 0;
}

int spi2axi_read(struct device *dev, u32 axi_addr, u8 *data,
		 u32 data_len, u32 read_flag)
{
	int ret;
	u32 reg_glb_csr;
	struct spi_device *spi;

	spi = to_spi_device(dev);
#ifdef CONFIG_TUNING_ENABLE
	set_tuned_val(spi);
#endif
	ret = set_xfer_bytes(dev, data_len);
	if (ret)
		return -ERR_RD_XFER_DATA;

	ret = spi2axi_read_request(spi, axi_addr);
	if (ret) {
		dev_err(&spi->dev, "spi read request error\n");
		return -ERR_RD_XFER_DATA;
	}

	ret = polling_fifo_full(dev, data_len);
	if (ret)
		return -ERR_RD_TIMEOUT;

	ret = spi2axi_read_data(spi, data, data_len);
	if (ret) {
		dev_err(dev, "spi read data error\n");
		return -ERR_RD_XFER_DATA;
	}

	ret = polling_xfer_finish(dev, &reg_glb_csr);
	if (ret)
		return -ERR_RD_TIMEOUT;

	ret = error_detect(dev, reg_glb_csr);
	if (ret) {
		dev_err(dev, "Error data 0x%x\n", *data);
		return -ERR_RD_XFER_DATA;
	}

	return 0;
}

static int spi2axi_init(struct spi_device *spi, struct spi2axi_ctx *data)
{
	int ret;
	u32 glb_csr;

	ls_bridge2axi_reset(&spi->dev);

#ifdef CONFIG_USING_QSPI
	dev_info(&spi->dev, "using QSPI\n");
#else
	dev_info(&spi->dev, "using SPI\n");
#endif

	ret = spi2axi_bus_tuning(spi, data);
	if (ret)
		return ret;

	spi2axi_log_en(spi);
	spi2axi_read_csr(&spi->dev, &glb_csr, GLB_CSR_OFFSET);
	dev_info(&spi->dev, "Spi2axi glb csr 0x%x\n", glb_csr);

	return 0;
}

static int spi2axi_reinit(struct device *dev)
{
	struct spi_device *spi;

	spi = to_spi_device(dev);
	spi2axi_log_en(spi);
#ifdef CONFIG_TUNING_ENABLE
	set_tuned_val(spi);
#endif

	return 0;
}

static int spi2axi_set_speed(struct device *dev, u32 speed_hz)
{
	int ret;
	struct spi2axi_ctx *ctx;
	struct spi_device *spi;

	spi = to_spi_device(dev);
	ctx = spi2axi_get_ctx(spi);

	spi->max_speed_hz = speed_hz;

	ret = spi_setup(spi);
	if (ret) {
		dev_err(&spi->dev, "setup failed\n");
		return -1;
	}

	ret = spi2axi_bus_tuning(spi, ctx);
	if (ret) {
		dev_err(&spi->dev, "set speed %d hz failed\n", speed_hz);
	}

	return ret;
}

static u32 spi2axi_get_typical_speed(struct device *dev, u32 tune_num)
{
	int ret;
	int speed = -1;
	int i;
	struct spi2axi_ctx *ctx;
	struct spi_device *spi;

	spi = to_spi_device(dev);
	ctx = spi2axi_get_ctx(spi);

	for (i = sizeof(spi2axi_typical_speed) / sizeof(u32) - 1; i >= 0; i--) {
		spi->max_speed_hz = spi2axi_typical_speed[i];
		ret = spi_setup(spi);
		if (ret) {
			dev_err(&spi->dev, "setup failed\n");
			continue;
		}

		ret = spi2axi_bus_tuning(spi, ctx);
		if (ret) {
			dev_err(&spi->dev, "set speed %d hz failed\n", spi2axi_typical_speed[i]);
			continue;
		}

		if (ctx->tune_num < tune_num)
			continue;

		speed = spi2axi_typical_speed[i];
		break;
	}

	return speed;
}

static void spi2axi_get_speed_range(struct device *dev, u32 typical_speed,
				    struct ls_bridge_speed_msg *msg)
{
	u32 tmp_speed, left_speed, right_speed, multiples;
	struct spi2axi_ctx *ctx;
	struct spi_device *spi;

	spi = to_spi_device(dev);
	ctx = spi2axi_get_ctx(spi);

	/* 100K */
	multiples = 100000;
	left_speed = SPI2AXI_MIN_SPEED_DEF / multiples;
	right_speed = typical_speed / multiples;

	while (left_speed < right_speed) {
		tmp_speed = (left_speed + right_speed) / 2;
		spi->max_speed_hz = tmp_speed * multiples;
		/* tune_num >= SPI2AXI_TUNE_NUM_AVAIL_WATER_LEVEL */
		if (spi_setup(spi) || spi2axi_bus_tuning(spi, ctx)
		    || ctx->tune_num < SPI2AXI_TUNE_NUM_AVAIL_WATER_LEVEL) {
			left_speed = tmp_speed + 1;
		} else {
			right_speed = tmp_speed;
		}
	}

	msg->min_speed = right_speed * multiples;

	left_speed = typical_speed / multiples;
	right_speed = SPI2AXI_MAX_SPEED_DEF / multiples;

	while (left_speed < right_speed) {
		tmp_speed = (left_speed + right_speed + 1) / 2;
		spi->max_speed_hz = tmp_speed * multiples;
		/* tune_num >= SPI2AXI_TUNE_NUM_AVAIL_WATER_LEVEL */
		if (spi_setup(spi) || spi2axi_bus_tuning(spi, ctx)
		    || ctx->tune_num < SPI2AXI_TUNE_NUM_AVAIL_WATER_LEVEL) {
			right_speed = tmp_speed - 1;
		} else {
			left_speed = tmp_speed;
		}
	}

	msg->max_speed = left_speed * multiples;
}

static void spi2axi_get_best_speed(struct device *dev, u32 typical_speed,
				   struct ls_bridge_speed_msg *msg)
{
	u32 tmp_speed, left_speed, right_speed, multiples;
	struct spi2axi_ctx *ctx;
	struct spi_device *spi;

	spi = to_spi_device(dev);
	ctx = spi2axi_get_ctx(spi);

	/* 100K */
	multiples = 100000;
	left_speed = typical_speed / multiples;
	right_speed = SPI2AXI_MAX_SPEED_DEF / multiples;

	while (left_speed < right_speed) {
		tmp_speed = (left_speed + right_speed + 1) / 2;
		spi->max_speed_hz = tmp_speed * multiples;
		/* tune_num >= SPI2AXI_TUNE_NUM_BEST_WATER_LEVEL */
		if (spi_setup(spi) || spi2axi_bus_tuning(spi, ctx)
		    || ctx->tune_num < SPI2AXI_TUNE_NUM_BEST_WATER_LEVEL) {
			right_speed = tmp_speed - 1;
		} else {
			left_speed = tmp_speed;
		}
	}

	msg->best_speed = left_speed * multiples;
}

int spi2axi_speed_op(struct device *dev, struct ls_bridge_speed_msg *msg)
{
	int ret = 0;
	int typical_speed, old_speed;
	u32 tune_num;
	struct spi_device *spi;

	spi = to_spi_device(dev);
	old_speed = spi->max_speed_hz;

	switch (msg->mode) {
	case SPEED_SET:
		ret = spi2axi_set_speed(dev, msg->set_speed);
		break;
	case SPEED_SINGLE_GET:
		msg->get_speed = old_speed;
		dev_info(&spi->dev, "get current speed[%d]hz\n", msg->get_speed);
		break;
	case SPEED_RANGE_GET:
		/* tune_num >= SPI2AXI_TUNE_NUM_AVAIL_WATER_LEVEL */
		tune_num = SPI2AXI_TUNE_NUM_AVAIL_WATER_LEVEL;
		typical_speed = spi2axi_get_typical_speed(dev, tune_num);
		if (typical_speed <= 0) {
			dev_err(&spi->dev,
				"typical speeds with tune_num[%d]don't match this platform!\n",
				tune_num);
			ret = -1;
			break;
		}
		dev_info(&spi->dev, "get typical_speed[%d]hz\n", typical_speed);
		spi2axi_get_speed_range(dev, (u32)typical_speed, msg);
		dev_info(&spi->dev, "speed range[%d ~ %d]hz\n", msg->min_speed, msg->max_speed);
		break;
	case SPEED_BEST_SET:
		/* tune_num >= SPI2AXI_TUNE_NUM_BEST_WATER_LEVEL */
		tune_num = SPI2AXI_TUNE_NUM_BEST_WATER_LEVEL;
		typical_speed = spi2axi_get_typical_speed(dev, tune_num);
		if (typical_speed <= 0) {
			dev_err(&spi->dev,
				"typical speeds with tune_num[%d]don't match this platform!\n",
				tune_num);
			ret = -1;
			break;
		}
		dev_info(&spi->dev, "get typical_speed[%d]hz\n", typical_speed);
		spi2axi_get_best_speed(dev, (u32)typical_speed, msg);
		dev_info(&spi->dev, "get best speed[%d]hz\n", msg->best_speed);
		ret = spi2axi_set_speed(dev, msg->best_speed);
		break;
	default:
		dev_err(&spi->dev, "invalid speed_mode[%d]!\n", msg->mode);
		break;
	}

	if (ret < 0) {
		spi->max_speed_hz = old_speed;
	}

	/* reset spibridge to avoid impact of setting wrong speed during tuning */
	ls_bridge2axi_reset(dev);

	return ret;
}

int spi2axi_set_mode(struct device *dev, u32 mode)
{
	int ret;
	u32 old_mode;
	u32 cut_mode;
	struct spi2axi_ctx *ctx;
	struct spi_device *spi;

	spi = to_spi_device(dev);
	ctx = spi2axi_get_ctx(spi);

	cut_mode = spi->mode & (~SPI_MODE_MASK);
	old_mode = spi->mode & SPI_MODE_MASK;
	mode = mode & SPI_MODE_MASK;

	dev_info(&spi->dev, "spi2axi_set_mode old_mode[%d]\n", old_mode);

	if (mode == SPI_MODE_0 || mode == SPI_MODE_3) {
		spi->mode = mode | cut_mode;
	} else {
		dev_err(&spi->dev, "unsupport mode->[%d]\n", mode);
		return -1;
	}

	ret = spi_setup(spi);
	if (ret) {
		dev_err(&spi->dev, "setup failed\n");
		return -1;
	}

	ret = spi2axi_bus_tuning(spi, ctx);
	if (ret) {
		dev_err(&spi->dev, "set mode->[%d] failed\n", mode);
		spi->mode = old_mode | cut_mode;
	}

	return ret;
}

int spi2axi_tunning(struct device *dev)
{
	int ret;
	struct spi2axi_ctx *ctx;
	struct spi_device *spi;

	spi = to_spi_device(dev);
	ctx = spi2axi_get_ctx(spi);

	ret = spi2axi_bus_tuning(spi, ctx);
	if (ret) {
		dev_err(&spi->dev, "spi2axi_bus_tuning failed\n");
	}

	return ret;
}

static int spi2axi_scatter_write(struct device *dev, u32 scatter_num,
				 struct scatter_wr_unit *scatters)
{
	u8 *scatter_buf;
	u32 size = 0;
	int ret;
	u32 reg_glb_csr;
	struct spi_transfer xfer;
	struct spi_device *spi;
	struct spibridge_priv *priv;

	spi = to_spi_device(dev);
	priv = spi_get_drvdata(spi);

	if (check_scatter(dev, scatter_num, scatters, &size))
		return -EINVAL;

	scatter_buf = devm_kzalloc(dev, size, GFP_KERNEL);
	if (!scatter_buf)
		return -ENOMEM;

	scatter_buf_fill(scatter_buf, scatter_num, scatters);

	memset(&xfer, 0x0, sizeof(struct spi_transfer));
	xfer.len = size;
	xfer.tx_buf = scatter_buf;
	xfer.bits_per_word = 8;

	ret = priv->xfer_timedout = spi_sync_transfer(spi, &xfer, 1);
	if (ret) {
		dev_err(dev, "spi2axi sync xfer failed!\n");
		goto scatter_out_mem;
	}

	ret = polling_xfer_finish(dev, &reg_glb_csr);
	if (ret) {
		ret = -ERR_WR_TIMEOUT;
		goto scatter_out_mem;
	}

	ret = error_detect(dev, reg_glb_csr);
	if (ret) {
		ret = -ERR_WR_XFER_DATA;
		goto scatter_out_mem;
	}

scatter_out_mem:
	devm_kfree(dev, scatter_buf);

	return ret;
}

static int spi2axi_dt_probe(struct device *dev,
			    struct device_node *nc, u32 bridge_id)
{
	struct spi2axi_ctx *data;
	struct spi_device *parent;
	struct spibridge_priv *spibridge = dev_get_drvdata(dev);

	parent = to_spi_device(dev);

	data = devm_kzalloc(&parent->dev,
			    sizeof(struct spi2axi_ctx), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

#ifdef CONFIG_USING_QSPI
	data->mem.spi = parent;
	data->mem.name = "spi2axi";
#endif
	spibridge->bridges[bridge_id]->cfg_data = data;
	spi2axi_init(parent, data);

	return 0;
}

struct ls_bridge_ops spi2axi = {
	.name = "spi2axi",
	.read = spi2axi_read,
	.write = spi2axi_write,
	.dt_probe = spi2axi_dt_probe,
	.read_csr = spi2axi_read_csr,
	.write_csr = spi2axi_write_csr,
	.send_single_cmd = spi2axi_single_cmd_send,
	.scatter_write = spi2axi_scatter_write,
	.reinit = spi2axi_reinit,
};
