/*
 * (C) Copyright 2024, Imvision Co., Ltd
 * This file is classified as confidential level C4 within Imvision
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author         Notes
 * 2023-4-20     yanghua     Initialize.
 */

/**
 * @brief   sdiobridge driver
 * @date    2023-04-24
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/version.h>
#include "sdiobridge_internel.h"
#include "sdio2axi_sc.h"
#include "sdiobridge.h"
#include "ai_isp_pmctrl.h"

static struct sdio_func *sdio_func;

static int sdiobridge_reset(void);

static void sdio2axi_xfer_log_dump(struct sdiobridge_priv *priv)
{
	u32 cur_rec_id;
	u32 i;
	struct xfer_record *rec;
	char *op;

	for (i = 0; i < MAX_RECORD_NUM; i++) {
		cur_rec_id = (priv->cur_rec_id + i) % MAX_RECORD_NUM;
		rec = &priv->records[cur_rec_id];
		op = rec->is_write ? "write" : "read";
		dev_err(&sdio_func->dev,
			"SW rec %d : Addr 0x%x, Len 0x%x, %s, Ret %d\n",
			i, rec->addr, rec->len, op, rec->ret);
	}
}

static void sdio2axi_xfer_log_record(struct sdiobridge_priv *priv, u32 addr,
			    u32 len, bool is_write, int ret)
{
	u32 cur_rec_id;
	struct xfer_record *rec;

	cur_rec_id = priv->cur_rec_id;
	rec = &priv->records[cur_rec_id];
	rec->addr = addr;
	rec->len = len;
	rec->is_write = is_write;
	rec->ret = ret;

	cur_rec_id++;
	cur_rec_id = cur_rec_id % MAX_RECORD_NUM;
	priv->cur_rec_id = cur_rec_id;
}

/* This is xfered by cmd52 */
static int sdio2axi_csr_reg_op_by_bytes(u32 offset, u8 *val,
					u32 nbytes, bool is_write)
{
	int i;
	int ret = 0;

	for (i = 0; i < nbytes; i++) {
		if (!is_write)
			val[i] = sdio_readb(sdio_func, offset + i, &ret);
		else
			sdio_writeb(sdio_func, val[i], offset + i, &ret);
		if (ret) {
			dev_err(&sdio_func->dev, "%s reg offset 0x%x failed, ret=%d\n",
				is_write ? "write" : "read", offset + i, ret);
			return ret;
		}
	}

	return 0;
}

static u32 reg_need_read_by_cmd52_tab[] = {
	AXI_CSR_OFFSET,
	FIFO_STS0_OFFSET,
	FIFO_STS1_OFFSET,
};

static u32 reg_need_write_by_cmd52_tab[] = {
	SDIO_STS_OFFSET,
};

static bool reg_need_op_by_cmd52(u32 reg, bool is_write)
{
	int i, len;
	u32 *arr;

	if (is_write) {
		arr = reg_need_write_by_cmd52_tab;
		len = ARRAY_SIZE(reg_need_write_by_cmd52_tab);
	} else {
		arr = reg_need_read_by_cmd52_tab;
		len = ARRAY_SIZE(reg_need_read_by_cmd52_tab);
	}

	for (i = 0; i < len; i++) {
		if (reg == arr[i])
			return true;
	}

	return false;
}

static int sdio2axi_csr_reg_op(u32 offset, u32 *val, bool is_write)
{
	int ret;
	int retrys = 0;

_retry:
	ret = 0;
	if (reg_need_op_by_cmd52(offset, is_write)) {
		ret = sdio2axi_csr_reg_op_by_bytes(offset, (u8 *)val, 4, is_write);
	} else {
		if (!is_write)
			*val = sdio_readl(sdio_func, offset, &ret);
		else
			sdio_writel(sdio_func, *val, offset, &ret);
	}

	if (ret) {
		dev_err(&sdio_func->dev, "%s offset 0x%x failed, ret=%d\n",
			is_write ? "write" : "read", offset, ret);
		if ((ret == -ETIMEDOUT) && !retrys) {
			dev_warn(&sdio_func->dev, "Re-enum sdio2axi...\n");
			if (sdiobridge_reset()) {
				dev_err(&sdio_func->dev, "Re-enum sdio2axi failed\n");
			} else {
				retrys = 1;
				goto _retry;
			}
		}
	}

	return ret;
}

static int sdio2axi_csr_readl(u32 offset, u32 *val)
{
	return sdio2axi_csr_reg_op(offset, val, 0);
}

static int sdio2axi_csr_writel(u32 val, u32 offset)
{
	return sdio2axi_csr_reg_op(offset, &val, 1);
}

static int sdio2axi_csr_setbits32(u32 offset, u32 set)
{
	int ret;
	u32 reg;

	ret = sdio2axi_csr_readl(offset, &reg);
	if (ret)
		return ret;

	reg |= set;

	return sdio2axi_csr_writel(reg, offset);
}

static int sdio2axi_set_axi_addr(u32 axi_addr)
{
	int ret;

	ret = sdio2axi_csr_writel(axi_addr >> 12, AXI_CTRL0_OFFSET);
	if (ret)
		dev_err(&sdio_func->dev, "write addr failed, ret=%d\n", ret);

	return ret;
}

/* SDIO2AXI error dump functions */
static char *err_string[] = {
	[CMD_CRC_ERR_OFFSET] = "cmd crc error",
	[CMD_SEQ_ERR_OFFSET] = "cmd seq error",
	[DAT_SEQ_ERR_OFFSET] = "data seq error",
	[DAT0_CRC_ERR_OFFSET] = "data0 crc error",
	[DAT1_CRC_ERR_OFFSET] = "data1 crc error",
	[DAT2_CRC_ERR_OFFSET] = "data2 crc error",
	[DAT3_CRC_ERR_OFFSET] = "data3 crc error",
	[ILLEGAL_CMD_OFFSET] = "illegal cmd",
	[INVALID_FUNC_OFFSET] = "invalid function",
	[INACTIVE_OFFSET] = "inactive error",
	[OUT_OF_RANGE_OFFSET] = "out of range",
	[CMD53_QUEUE_WERR_OFFSET] = "cmd53 queue w-error",
	[CMD53_QUEUE_RERR_OFFSET] = "cmd53 queue r-error",
	[WFIFO_WERR_OFFSET] = "wfifo overflow",
	[WFIFO_RERR_OFFSET] = "wfifo underflow",
	[RFIFO_WERR_OFFSET] = "rfifo overflow",
	[RFIFO_RERR_OFFSET] = "rfifo underflow",
	[RESP_ERR_OFFSET] = "axi response error",
};

static void sdio2axi_sdio_err_dump(void)
{
	u32 val;

	if (!sdio2axi_csr_readl(SDIO_STS_OFFSET, &val)) {
		dev_err(&sdio_func->dev, "sdio_sts 0x%08x, vlt 1p8v %d, clk edge %d, "
			"resp flags 0x%x, rca 0x%x\n",
			val, get_masked_val(val, VOLTAGE_1P8V),
			get_masked_val(val, CLOCK_EDGE),
			get_masked_val(val, RESP_FLAGS),
			get_masked_val(val, RCA));
	}
}

static void sdio2axi_axi_err_dump(void)
{
	u32 val;

	if (!sdio2axi_csr_readl(AXI_CTRL1_OFFSET, &val)) {
		dev_err(&sdio_func->dev, "axi_ctrl_1 0x%08x, axi size %d, "
			"axi max burst len %d\n",
			val, get_masked_val(val, AXI_SIZE),
			get_masked_val(val, AXI_MAX_BURST_LEN));
	}
	if (!sdio2axi_csr_readl(AXI_CTRL2_OFFSET, &val)) {
		dev_err(&sdio_func->dev, "axi_ctrl_2 0x%08x, axi max outstanding %d\n",
			val, get_masked_val(val, AXI_MAX_OUTSTANDING));
	}
}

static void sdio2axi_fifo_err_dump(void)
{
	u32 val;

	if (!sdio2axi_csr_readl(FIFO_STS0_OFFSET, &val)) {
		dev_err(&sdio_func->dev, "fifo sts0 0x%08x, rfifo_wcnt %02d, "
			"wfifo_rcnt %02d, cmd53_queue_rcnt %02d",
			val, get_masked_val(val, RFIFO_WCNT),
			get_masked_val(val, WFIFO_RCNT),
			get_masked_val(val, CMD53_QUEUE_RCNT));
	}
	if (!sdio2axi_csr_readl(FIFO_STS1_OFFSET, &val)) {
		dev_err(&sdio_func->dev, "fifo sts1 0x%08x, rfifo_rcnt %02d, "
			"wfifo_wcnt %02d, cmd53_queue_wcnt %02d",
			val, get_masked_val(val, RFIFO_RCNT),
			get_masked_val(val, WFIFO_WCNT),
			get_masked_val(val, CMD53_QUEUE_WCNT));
	}
}

static void sdio2axi_glb_csr_dump(void)
{
	u32 val;

	if (!sdio2axi_csr_readl(AXI_CSR_OFFSET, &val)) {
		dev_err(&sdio_func->dev, "axi_csr 0x%08x, axi pending trans %d, "
			"remain xfer beat %d, axi xfer beats cnt %d\n",
			val, get_masked_val(val, AXI_PENDING_TRANS),
			get_masked_val(val, AXI_REMAIN_XFER_BEAT),
			get_masked_val(val, AXI_XFER_BEATS_CNT));
	}
}

static void sdio2axi_hw_axi_log_dump(void)
{
	int i;
	u32 val, addr, data;

	for (i = 0; i < 4; i++) {
		sdio2axi_csr_readl(CMD_LOG_OFFSET(i), &val);
		sdio2axi_csr_readl(DATA_LOG_OFFSET(i), &data);
		sdio2axi_csr_readl(ADDR_LOG_OFFSET(i), &addr);
		dev_err(&sdio_func->dev,
			"AXI log %d, op-type:%s, addr 0x%08x, data 0x%08x, "
			"resp 0x%02x, burst_len %2d\n", i,
			get_masked_val(val, CMD_LOG_READ) ? "read" : "write",
			addr, data, get_masked_val(val, CMD_LOG_RESP),
			get_masked_val(val, CMD_LOG_BURST_LEN));
	}
}

static void sdio2axi_err_irq_dump(void)
{
	int i;
	u32 val;

	if (!sdio2axi_csr_readl(SDIO_INT_RAW_OFFSET, &val)) {
		sdio2axi_csr_writel(val, SDIO_INT_CLR_OFFSET);
		for (i = 0; i < ARRAY_SIZE(err_string); i++) {
			if (val & 0x1)
				dev_err(&sdio_func->dev,  "INT_ERR: %s\n", err_string[i]);
			val = val >> 1;
		}
	}
}

static void sdio2axi_err_dump(u32 csr)
{
	if (csr & SDIO_ERR_MASK) {
		dev_err(&sdio_func->dev, "SDIO error occurs\n");
		sdio2axi_sdio_err_dump();
	}
	if (csr & AXI_ERR_MASK) {
		dev_err(&sdio_func->dev, "AXI error occurs\n");
		sdio2axi_axi_err_dump();
	}
	if (csr & FIFO_ERR_MASK) {
		dev_err(&sdio_func->dev, "FIFO error occurs\n");
		sdio2axi_fifo_err_dump();
	}
	sdio2axi_glb_csr_dump();
	sdio2axi_hw_axi_log_dump();
	sdio2axi_err_irq_dump();
}

static int sdio2axi_polling_idle(void)
{
	int ret = 0;
	u8 csr_lsb = 0x0;
	u32 timeout = 100;

	while (!(csr_lsb & SDIO_IDLE_MASK) && (--timeout)) {
		ret = sdio2axi_csr_reg_op_by_bytes(AXI_CSR_OFFSET, &csr_lsb, 1, 0);
		if (ret) {
			dev_err(&sdio_func->dev, "read csr err\n");
			return ret;
		}
	}

	if (!timeout || (csr_lsb & CSR_GLB_ERR_MASK)) {
		ret = -EIO;
		sdio2axi_err_dump(csr_lsb);
		if (!timeout) {
			dev_err(&sdio_func->dev, "wait idle timeout, csr 0x%x, "
				"sdio reset..\n", csr_lsb);
			if (sdiobridge_reset())
				dev_err(&sdio_func->dev, "sdio2axi reset failed\n");
			ret = -ETIMEDOUT;
		}
	}

	return ret;
}

static int __sdio2axi_data_xfer(u32 addr, u8 *data, u32 size, bool is_write)
{
	int ret, ret_poll;

	sdio_claim_host(sdio_func);

	ret = sdio2axi_set_axi_addr(addr);
	if (ret)
		goto out;

	if (is_write)
		ret = sdio_memcpy_toio(sdio_func, AXI_TRIGGER_ADDR(addr), data, size);
	else
		ret = sdio_memcpy_fromio(sdio_func, data, AXI_TRIGGER_ADDR(addr), size);

	if (ret)
		dev_err(&sdio_func->dev, "sdio memcpy faild\n");

	ret_poll = sdio2axi_polling_idle();
	if (ret_poll && !ret)
		ret = ret_poll;
out:
	sdio_release_host(sdio_func);

	return ret;
}

static int sdio2axi_axi_xfer(u32 addr, u8 *data, u32 size, bool is_write)
{
	int ret;
	u32 remainder = size;
	u32 max_blocks;
	/* Do the bulk of the transfer using block mode. */
	if (size > sdio_func->cur_blksize) {
		/**
		 * Blocks per command is limited by host count, host transfer
		 * size and the maximum for IO_RW_EXTENDED of 511 blocks.
		 */
		max_blocks = 511u;

		while (remainder >= sdio_func->cur_blksize) {
			u32 blocks;

			blocks = remainder / sdio_func->cur_blksize;
			if (blocks > max_blocks)
				blocks = max_blocks;
			size = blocks * sdio_func->cur_blksize;

			ret = __sdio2axi_data_xfer(addr, data, size, is_write);
			if (ret)
				return ret;

			remainder -= size;
			data += size;
			addr += size;
		}
	}

	/* Write the remainder using byte mode. */
	while (remainder > 0) {
		size = min(remainder, sdio_func->cur_blksize);

		ret = __sdio2axi_data_xfer(addr, data, size, is_write);
		if (ret)
			return ret;

		remainder -= size;
		data += size;
		addr += size;
	}

	return 0;
}


int __sdio_bridge_read(u32 addr, u8 *data, u32 nbytes, bool cpy_need)
{
	int ret;
	struct sdiobridge_priv *priv;

	if (sdio_func == NULL) {
		pr_err("sdiobridge slave driver not init!\n");
		return -ENODEV;
	}

	priv = sdio_get_drvdata(sdio_func);

	mutex_lock(&priv->xfer_lock);

	/* Do data copy in case of unaligned user buffer */
	if (cpy_need && (nbytes <= KERNEL_MEMCPY_MAX_SIZE)) {
		ret = sdio2axi_axi_xfer(addr, priv->kernel_xfer_buf, nbytes, 0);
		if (!ret)
			memcpy(data, priv->kernel_xfer_buf, nbytes);
	} else {
		ret = sdio2axi_axi_xfer(addr, data, nbytes, 0);
	}

	sdio2axi_xfer_log_record(priv, addr, nbytes, false, ret);
	if (ret) {
		dev_err(&sdio_func->dev,
			"failed read %d bytes from address 0x%x\n", nbytes, addr);
		sdio2axi_xfer_log_dump(priv);
	}

	mutex_unlock(&priv->xfer_lock);

	return ret;
}

/**
 * @brief read an amount of data via sdiobridge from isp soc.
 * @param addr		addr the ahb addr to read.
 * @param data		read buffer start address.
 * @nbytes		the number of bytes to read
 * @return		success ?
 * @ retval 0		ok
 * @ retval others	err
 */
int sdio_bridge_read(u32 addr, u8 *data, u32 nbytes)
{
	return __sdio_bridge_read(addr, data, nbytes, true);
}
EXPORT_SYMBOL(sdio_bridge_read);


int __sdio_bridge_write(u32 addr, const u8 *data, u32 nbytes, bool cpy_need)
{
	int ret;
	struct sdiobridge_priv *priv;

	if (sdio_func == NULL) {
		pr_err("sdiobridge slave driver not init!\n");
		return -ENODEV;
	}

	priv = sdio_get_drvdata(sdio_func);

	mutex_lock(&priv->xfer_lock);

	/* Do data copy in case of unaligned user buffer */
	if (cpy_need && (nbytes <= KERNEL_MEMCPY_MAX_SIZE)) {
		memcpy(priv->kernel_xfer_buf, data, nbytes);
		ret = sdio2axi_axi_xfer(addr, priv->kernel_xfer_buf, nbytes, 1);
	} else {
		ret = sdio2axi_axi_xfer(addr, (u8 *)data, nbytes, 1);
	}

	sdio2axi_xfer_log_record(priv, addr, nbytes, true, ret);
	if (ret) {
		dev_err(&sdio_func->dev,
			"failed write %d bytes to address 0x%x\n", nbytes, addr);
		sdio2axi_xfer_log_dump(priv);
	}

	mutex_unlock(&priv->xfer_lock);

	return ret;
}

/**
 * @brief write an amount of data via sdiobridge from isp soc.
 * @param addr		addr the axi addr to write.
 * @param data		read buffer start address.
 * @nbytes		the number of bytes to write
 * @return		success ?
 * @ retval 0		ok
 * @ retval others	err
 */
int sdio_bridge_write(u32 addr, const u8 *data, u32 nbytes)
{
	return __sdio_bridge_write(addr, data, nbytes, true);
}
EXPORT_SYMBOL(sdio_bridge_write);

static void sdiobridge_hw_init(struct sdio_func *func)
{
	if (sdio_enable_func(func))
		dev_err(&func->dev, "failed to enable function\n");

	if (sdio_set_block_size(func, 0))
		dev_err(&func->dev, "failed to set_default blk size\n");

	sdio2axi_csr_writel(0xffffffff, SDIO_INT_CLR_OFFSET);
	sdio2axi_csr_writel(0x0, SDIO_INT_MASK_OFFSET);
	sdio2axi_csr_writel(LOG_EN_MASK, AXI_CSR_OFFSET);
	sdio2axi_csr_setbits32(SDIO_STS_OFFSET, SDIO_STS_EN_MASK);
}

static int sdiobridge_reset(void)
{
	int ret = 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
	ret = mmc_hw_reset(sdio_func->card->host);
#else
	ret = mmc_sw_reset(sdio_func->card);
#endif
	if (ret) {
		dev_err(&sdio_func->dev, "sdio2axi reset failed\n");
		return ret;
	}

	sdiobridge_hw_init(sdio_func);

	return 0;
}

static int sdiobridge_reset_locked(struct sdiobridge_priv *sdiobridge)
{
	int ret = 0;
	unsigned long start, end;

	start = jiffies;
	mutex_lock(&sdiobridge->xfer_lock);
	sdio_claim_host(sdio_func);

	ret = sdiobridge_reset();

	sdio_release_host(sdio_func);
	mutex_unlock(&sdiobridge->xfer_lock);
	end = jiffies;
	dev_dbg(&sdio_func->dev, "re-enum time %d ms\n", jiffies_to_msecs(end - start));

	return ret;
}

static int sdio2axi_pm_event(struct notifier_block *nb,
			     unsigned long event, void *data)
{
	int ret = 0;
	struct sdiobridge_priv *sdiobridge = container_of(nb, struct sdiobridge_priv,
							  pm_nb);

	if (event & AIISP_PM_EVENT_RESET_DONE)
		ret = sdiobridge_reset_locked(sdiobridge);

	return ret;
}

#ifdef CONFIG_SDIO2AXI_CIS_DEBUG
static void sdio2axi_cis_tuples_parse(struct sdio_func *func, struct sdio_func_tuple *tuple)
{
	int i;

	while (tuple) {
		dev_info(&func->dev, "Tuple code 0x%x\n", tuple->code);
		for (i = 0; i < tuple->size; i++)
			dev_info(&func->dev, "Content %d 0x%x\n", i, tuple->data[i]);
		tuple = tuple->next;
	}
}

static void sdio2axi_cis_print(struct sdio_func *func)
{
	uint32_t vendor, device, blksize, max_dtr;

	vendor = func->card->cis.vendor;
	device = func->card->cis.device;
	blksize = func->card->cis.blksize;
	max_dtr = func->card->cis.max_dtr;
	dev_info(&func->dev, "Common cis: vendor 0x%x, device 0x%x\n", vendor, device);
	dev_info(&func->dev, "Common cis: blksize 0x%x, max_dtr %d\n", blksize, max_dtr);
	dev_info(&func->dev, "Common cis remaining\n");
	sdio2axi_cis_tuples_parse(func, func->card->tuples);
	dev_info(&func->dev, "Func cis: max_blksize 0x%x, enable_timeout 0x%x\n",
		 func->max_blksize, func->enable_timeout);
	dev_info(&func->dev, "Func cis remaining\n");
	sdio2axi_cis_tuples_parse(func, func->tuples);
}
#endif

static int sdiobridge_probe(struct sdio_func *func, const struct sdio_device_id *id)
{
	u32 version = 0;
	struct sdiobridge_priv *sdiobridge;

	dev_info(&func->dev, "tetras_sdio2axi: blk_size=0x%x\n", func->cur_blksize);
#ifdef CONFIG_SDIO2AXI_CIS_DEBUG
	sdio2axi_cis_print(func);
#endif

	sdiobridge = devm_kzalloc(&func->dev, sizeof(*sdiobridge), GFP_KERNEL);
	if (!sdiobridge)
		return -ENOMEM;

	sdiobridge->ioc_xfer_buf = devm_kzalloc(&func->dev, XFER_MAX_BUFFER_LEN, GFP_KERNEL);
	if (!sdiobridge->ioc_xfer_buf)
		return -ENOMEM;

	sdiobridge->kernel_xfer_buf = devm_kzalloc(&func->dev, KERNEL_MEMCPY_MAX_SIZE, GFP_KERNEL);
	if (!sdiobridge->kernel_xfer_buf)
		return -ENOMEM;

	sdio_set_drvdata(func, sdiobridge);
	mutex_init(&sdiobridge->xfer_lock);
	mutex_init(&sdiobridge->ioctl_lock);
	sdio_func = func;
	sdiobridge->pm_nb.notifier_call = sdio2axi_pm_event;
	aiisp_pm_register_notifier(&sdiobridge->pm_nb);

	sdio_claim_host(func);

	sdiobridge_hw_init(func);
	sdio2axi_csr_readl(SDIO2AXI_SC_TID_OFFSET, &version);
	dev_info(&func->dev, "sdio2axi version 0x%x\n", version);
	sdiobridge->version = version;

	sdio_release_host(func);

	return 0;
}

static void sdiobridge_remove(struct sdio_func *sdio)
{
	struct sdiobridge_priv *sdiobridge = sdio_get_drvdata(sdio);

	mutex_destroy(&sdiobridge->xfer_lock);
	mutex_destroy(&sdiobridge->ioctl_lock);
	aiisp_pm_unregister_notifier(&sdiobridge->pm_nb);
	devm_kfree(&sdio->dev, sdiobridge->ioc_xfer_buf);
	devm_kfree(&sdio->dev, sdiobridge);
}

/* Scatter functions */
static int check_scatter(u32 scatter_num, struct scatter_wr_unit *scatters, u32 *sz)
{
	int i;
	u32 size = 0;

	for (i = 0; i < scatter_num; i++) {
		if (scatters[i].data_len > MAX_SCATTER_LEN || !scatters[i].data_len) {
			dev_err(&sdio_func->dev, "%d scatter len %d invalid!\n",
				i, scatters[i].data_len);
			return -EINVAL;
		}
		if (scatters[i].addr % sizeof(u32)) {
			dev_err(&sdio_func->dev, "%d scatter addr 0x%x invalid!\n",
				i, scatters[i].addr);
			return -EINVAL;
		}
		size += scatters[i].data_len;
		/* Addr_len occupy 1 word */
		size++;
	}
	*sz = (size * sizeof(u32));

	return 0;
}

static void scatter_buf_fill(u8 *buf, u32 scatter_num,
			     struct scatter_wr_unit *scatters)
{
	int i;
	int buf_id = 0;
	u32 *scatter_data;

	scatter_data = (u32 *)&buf[0];
	for (i = 0; i < scatter_num; i++) {
		u32 addr_len;
		int len;
		struct scatter_wr_unit *scatter = &scatters[i];

		addr_len = scatter->addr;
		addr_len |= (scatter->data_len - 1) & GENMASK(1, 0);
		scatter_data[buf_id++] = addr_len;
		for (len = 0; len < scatter->data_len; len++)
			scatter_data[buf_id++] = scatter->data[len];
	}
}

static int sdio2axi_scatter_write(u32 scatter_num,
				  struct scatter_wr_unit *scatters)
{
	u8 *scatter_buf;
	u32 size = 0;
	int ret, ret_poll;
	u32 remainder, first_xfer_size;

	if (check_scatter(scatter_num, scatters, &size))
		return -EINVAL;

	scatter_buf = devm_kzalloc(&sdio_func->dev, size, GFP_KERNEL);
	if (!scatter_buf)
		return -ENOMEM;

	scatter_buf_fill(scatter_buf, scatter_num, scatters);

	remainder = size % (sdio_func->cur_blksize);
	first_xfer_size = size - remainder;
	sdio_claim_host(sdio_func);
	if (first_xfer_size) {
		ret = sdio_memcpy_toio(sdio_func, SCATTER_TRIGGER_ADDR,
				       scatter_buf, first_xfer_size);
		if (ret)
			dev_err(&sdio_func->dev, "scatter blk memcpy faild\n");
	}

	if (remainder) {
		ret = sdio_memcpy_toio(sdio_func, SCATTER_TRIGGER_ADDR,
				       scatter_buf + first_xfer_size, remainder);
		if (ret)
			dev_err(&sdio_func->dev, "scatter bytes memcpy faild\n");
	}

	ret_poll = sdio2axi_polling_idle();
	if (ret_poll && !ret)
		ret = ret_poll;

	sdio_release_host(sdio_func);

	devm_kfree(&sdio_func->dev, scatter_buf);

	return ret;
}

static int sdiobridge_ioc_scatter_wr(struct sdiobridge_priv *sdiobridge,
				    void __user *ubuf)
{
	int ret = 0;
	struct sdiobridge_scatter_msg msg;
	void __user *udata_buf;
	uint32_t size;
	struct scatter_wr_unit *scatters;

	mutex_lock(&sdiobridge->ioctl_lock);

	if (copy_from_user(&msg, ubuf, sizeof(msg))) {
		ret = -EINVAL;
		goto err_scatter_wr;
	}
	/* write scatter lists */
	udata_buf = (void __user *)msg.scatters;
	size = msg.scatter_num * sizeof(struct scatter_wr_unit);
	scatters = devm_kzalloc(&sdio_func->dev, size, GFP_KERNEL);
	if (!scatters) {
		ret = -ENOMEM;
		goto err_scatter_wr;
	}

	if (copy_from_user(scatters, udata_buf, size)) {
		ret = -EINVAL;
		goto err_scatter_mem;
	}

	ret = sdio2axi_scatter_write(msg.scatter_num, scatters);

err_scatter_mem:
	devm_kfree(&sdio_func->dev, scatters);

err_scatter_wr:
	mutex_unlock(&sdiobridge->ioctl_lock);

	return ret;
}

static int sdiobridge_ioc_xfer(struct sdiobridge_priv *sdiobridge,
			       void __user *ubuf, bool is_write)
{
	int ret = 0;
	struct sdiobridge_msg msg;
	void __user *udata_buf;

	mutex_lock(&sdiobridge->ioctl_lock);

	if (copy_from_user(&msg, ubuf, sizeof(msg))) {
		ret = -EINVAL;
		goto err_rw;
	}

	/* write data or read data buffer */
	udata_buf = (void __user *)msg.buffer;

	/* Make sure len(in word len) not exceed buffer len */
	if (msg.len > XFER_MAX_BUFFER_LEN) {
		ret = -EINVAL;
		goto err_rw;
	}

	if (!is_write) {
		ret = __sdio_bridge_read(msg.addr, sdiobridge->ioc_xfer_buf, msg.len, false);
		if (ret) {
			msg.error_code = (__u32)-ret;
			goto err_rw;
		}

		/* Write received data to userspcae */
		if (copy_to_user(udata_buf, sdiobridge->ioc_xfer_buf,
				msg.len)) {
			ret = -EINVAL;
			goto err_rw;
		}
	} else {
		/* Copy write data from userspcae */
		if (copy_from_user(sdiobridge->ioc_xfer_buf, udata_buf, msg.len)) {
			ret = -EINVAL;
			goto err_rw;
		}

		ret = __sdio_bridge_write(msg.addr, (const u8 *)sdiobridge->ioc_xfer_buf,
					  msg.len, false);
		if (ret) {
			msg.error_code = (__u32)-ret;
			goto err_rw;
		}
	}

err_rw:
	if (copy_to_user(ubuf, &msg, sizeof(msg)))
		ret = -EINVAL;

	mutex_unlock(&sdiobridge->ioctl_lock);

	return ret;
}

static int sdiobridge_ioc_reset(struct sdiobridge_priv *sdiobridge)
{
	return sdiobridge_reset_locked(sdiobridge);
}

static int sdiobridge_ioc_blk_size(struct sdiobridge_priv *sdiobridge,
				   void __user *ubuf)
{
	int ret = 0;
	u32 blk_size;

	mutex_lock(&sdiobridge->xfer_lock);
	sdio_claim_host(sdio_func);

	if (copy_from_user(&blk_size, ubuf, sizeof(u32))) {
		ret = -EINVAL;
		goto err_blk_size;
	}

	ret = sdio_set_block_size(sdio_func, blk_size);
	dev_info(&sdio_func->dev, "cur size %d bytes\n", sdio_func->cur_blksize);

err_blk_size:
	sdio_release_host(sdio_func);
	mutex_unlock(&sdiobridge->xfer_lock);

	return ret;
}

static int sdiobridge_ioc_reg_xfer(struct sdiobridge_priv *sdiobridge,
				   void __user *ubuf, bool is_write)
{
	int ret = 0;
	struct sdiobridge_reg_ctrl msg;
	void __user *udata_buf;
	u32 reg;

	mutex_lock(&sdiobridge->xfer_lock);
	sdio_claim_host(sdio_func);

	if (copy_from_user(&msg, ubuf, sizeof(msg))) {
		ret = -EINVAL;
		goto err_rw;
	}

	/* write data or read data buffer */
	udata_buf = (void __user *)msg.reg_val;

	if (!is_write) {
		ret = sdio2axi_csr_readl(msg.addr, &reg);
		if (ret) {
			msg.error_code = (__u32)-ret;
			goto err_rw;
		}

		/* Write received data to userspcae */
		if (copy_to_user(udata_buf, &reg, sizeof(u32))) {
			ret = -EINVAL;
			goto err_rw;
		}
	} else {
		/* Copy write data from userspcae */
		if (copy_from_user(&reg, udata_buf, sizeof(u32))) {
			ret = -EINVAL;
			goto err_rw;
		}

		ret = sdio2axi_csr_writel(reg, msg.addr);
		if (ret) {
			msg.error_code = (__u32)-ret;
			goto err_rw;
		}
	}

err_rw:
	if (copy_to_user(ubuf, &msg, sizeof(msg)))
		ret = -EINVAL;

	sdio_release_host(sdio_func);
	mutex_unlock(&sdiobridge->xfer_lock);

	return ret;
}

static long sdiobridge_ioctl(struct file *file,
			   unsigned int cmd, unsigned long arg)
{
	void __user *ubuf = (void __user *)arg;
	struct sdiobridge_info info;
	int ret = 0;
	struct sdio_func *sdio_func;
	struct sdiobridge_priv *sdiobridge;

	sdio_func = file->private_data;
	if (!sdio_func)
		return -EINVAL;

	sdiobridge = sdio_get_drvdata(sdio_func);
	if (!sdiobridge)
		return -EINVAL;

	switch (cmd) {
	case SDIOBRIDGE_CMD_READ:
		ret = sdiobridge_ioc_xfer(sdiobridge, ubuf, false);
		break;
	case SDIOBRIDGE_CMD_WRITE:
		ret = sdiobridge_ioc_xfer(sdiobridge, ubuf, true);
		break;
	case SDIOBRIDGE_CMD_RD_REG:
		ret = sdiobridge_ioc_reg_xfer(sdiobridge, ubuf, false);
		break;
	case SDIOBRIDGE_CMD_WR_REG:
		ret = sdiobridge_ioc_reg_xfer(sdiobridge, ubuf, true);
		break;
	case SDIOBRIDGE_CMD_RESET:
		ret = sdiobridge_ioc_reset(sdiobridge);
		break;
	case SDIOBRIDGE_CMD_BLK_SIZE:
		ret = sdiobridge_ioc_blk_size(sdiobridge, ubuf);
		break;
	case SDIOBRIDGE_CMD_SCAT_WR:
		ret = sdiobridge_ioc_scatter_wr(sdiobridge, ubuf);
		break;
	case SDIOBRIDGE_CMD_DEVINFO:
		info.max_xfer_len = XFER_MAX_BUFFER_LEN;
		info.version = sdiobridge->version;
		if (copy_to_user(ubuf, &info, sizeof(info)))
			ret = -EINVAL;
		break;
	default:
		return -ENOIOCTLCMD;
	}

	return ret;
}

static int sdiobridge_open(struct inode *inode, struct file *file)
{
	if (!sdio_func) {
		pr_err("sdio bridge: no device found!\n");
		return -EINVAL;
	}

	file->private_data = sdio_func;

	return 0;
}

static int sdiobridge_close(struct inode *inode, struct file *file)
{
	file->private_data = NULL;

	return 0;
}

static ssize_t sdiobridge_read(struct file *file, char __user *buf,
			       size_t len, loff_t *offp)
{
	return -EFAULT;
}

static const struct file_operations sdiobridge_misc_fops = {
	.owner		= THIS_MODULE,
	.read		= sdiobridge_read,
	.open		= sdiobridge_open,
	.release	= sdiobridge_close,
	.unlocked_ioctl	= sdiobridge_ioctl,
};

static struct miscdevice sdiobridge_misc = {
	MISC_DYNAMIC_MINOR,
	"tetras_sdiobridge",
	&sdiobridge_misc_fops,
};

static const struct sdio_device_id sdio2axi_ids[] = {
	{ SDIO_DEVICE(0x0, 0x0) },
	{ /* end: all zeroes */			},
};

static struct sdio_driver sdiobridge_driver = {
	.name		= "tetras_sdio2axi",
	.id_table	= sdio2axi_ids,
	.probe		= sdiobridge_probe,
	.remove		= sdiobridge_remove,
};

static int __init sdiobridge_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&mmc_pwrseq_sdio2axi_driver);
	if (ret) {
		pr_err("sdio2axi pwrseq driver register failed!\n");
		return ret;
	}

	ret = sdio_register_driver(&sdiobridge_driver);
	if (ret) {
		pr_err("sdiobridge driver register failed!\n");
		platform_driver_unregister(&mmc_pwrseq_sdio2axi_driver);
		return ret;
	}

	ret = misc_register(&sdiobridge_misc);
	if (ret) {
		pr_err("sdiobridge: can't misc_register\n");
		sdio_unregister_driver(&sdiobridge_driver);
		platform_driver_unregister(&mmc_pwrseq_sdio2axi_driver);
	}

	return ret;
}

static void __exit sdiobridge_exit(void)
{
	misc_deregister(&sdiobridge_misc);
	sdio_unregister_driver(&sdiobridge_driver);
	platform_driver_unregister(&mmc_pwrseq_sdio2axi_driver);
}

module_init(sdiobridge_init);
module_exit(sdiobridge_exit);

MODULE_AUTHOR("yanghua@tetras.ai");
MODULE_DESCRIPTION("sdiobridge Protocol Driver: v0.0");
MODULE_LICENSE("GPL v2");
