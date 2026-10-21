/*
 * (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author         Notes
 * 2023-04-27     yangyuzun      Initialize.
 */

/**
 * @brief   mult bridge(spi,i2c) to axi common driver
 * @date    2023-04-27
 */
#include <linux/of.h>
#include <linux/uaccess.h>
#include "spi2axi_csr.h"
#include <linux/device.h>
#include "spibridge.h"
#include "ls_bridge_internel.h"
#include "i2c_bridge_drv.h"
#include "spi_bridge_drv.h"

static struct ls_bridge_ops *sel_ops(struct device *dev)
{
	struct device_node *nc = dev->of_node;
	const char *compatible;
	int ret;

	ret = of_property_read_string(nc, "compatible", &compatible);
	if (ret) {
		dev_err(dev, "invalid node, no compatible\n");
		return NULL;
	}

	if (!strcmp(compatible, "tetras,spibridge"))
		return &spi2axi;
	else if (!strcmp(compatible, "tetras,i2c2axi"))
		return &i2c2axi;

	return NULL;
}

static bool is_fifo_full(u32 fifo_status, u32 len)
{
	bool full = 0;
	u32 fifo_depth, fifo_cnt;
	u32 w_len;

	/*
	 * NOTE: fifo takes 4 bytes as a unit, the part of
	 * data length greater than zero and less than four
	 * bytes will automatically add one fifo count when
	 * ai-isp-v2 transfering unaligned data.
	 */
	w_len = DIV_ROUND_UP(len, 4);
	fifo_depth = fifo_status & FIFO_DEPTH_MASK;
	fifo_cnt = (fifo_status & FIFO_CNT_MASK) >> FIFO_CNT_OFFSET;
	if (w_len > fifo_depth) {
		if (fifo_cnt >= (fifo_depth - MAX_BURST_LEN))
			full = 1;
	} else {
		if (fifo_cnt == w_len)
			full = 1;
	}

	return full;
}

static void fifo_status_dump(struct device *dev, u32 fifo_status)
{
	u32 fifo_depth;
	u32 fifo_cnt;
	u32 max_fifo_cnt_reached;
	u32 fifo_empty;
	u32 fifo_full;

	if (fifo_status & OVERFLOW_ERR_MASK)
		dev_err(dev, "ls_bridge2axi fifo overflow\n");

	if (fifo_status & UNDERFLOW_ERR_MASK)
		dev_err(dev, "ls_bridge2axi fifo underflow\n");

	fifo_depth = fifo_status & FIFO_DEPTH_MASK;
	fifo_cnt = (fifo_status & FIFO_CNT_MASK) >> FIFO_CNT_OFFSET;
	max_fifo_cnt_reached = (fifo_status & MAX_FIFO_CNT_REACHED_MASK)
				>> MAX_FIFO_CNT_REACHED_OFFSET;
	fifo_empty = (fifo_status & FIFO_EMPTY_MASK) >> FIFO_EMPTY_OFFSET;
	fifo_full = (fifo_status & FIFO_FULL_MASK) >> FIFO_FULL_OFFSET;

	dev_err(dev, "fifo depth %d, fifo cnt %d, max fifo reached %d,"
		"fifo is empty %d, fifo is full %d\n", fifo_depth, fifo_cnt,
		max_fifo_cnt_reached, fifo_empty, fifo_full);
}

static void biu_status0_dump(struct device *dev, u32 biu_status0)
{
	u32 sub_status;

	sub_status = (biu_status0 & MAX_OTS_REACHED_MASK)
		     >> MAX_OTS_REACHED_OFFSET;
	dev_err(dev, "biu max outstanding reached %d\n", sub_status);

	sub_status = (biu_status0 & BIU_SM_STATE_MASK)
		     >> BIU_SM_STATE_OFFSET;
	dev_err(dev, "biu state machine status %d\n", sub_status);

	sub_status = (biu_status0 & BIU_PENDING_TRANS_MASK)
		     >> BIU_PENDING_TRANS_OFFSET;
	dev_err(dev, "biu pending trans %d\n", sub_status);
}

static void addr_err_dump(struct device *dev, u32 err_resp_status)
{
	u32 sub_status;

	if (err_resp_status & ERR_RESP_MASK) {
		sub_status = (err_resp_status & ADDR_MASK);
		dev_err(dev, "ls_bridge2axi error response occurs!\n");
		dev_err(dev, "ls_bridge2axi error addr 0x%x!\n", sub_status);
	}
}

static void instr_error_dump(struct device *dev, u32 instr_status)
{
	u32 err_instr;

	if (instr_status & ERR_INSTR_VLD_MASK) {
		err_instr = (instr_status & ERR_INSTR_MASK) >> ERR_INSTR_OFFSET;
		dev_err(dev, "ls_bridge2axi invalid instruction %x\n", err_instr);
	}
}

static void log_dump(struct device *dev, u32 cmd, int i)
{
	char *op;
	u32 qos, burst_len, resp, write, read;

	qos = (cmd & CMD_LOG_QOS_MASK) >> CMD_LOG_QOS_OFFSET;
	burst_len = (cmd & CMD_LOG_BURST_LEN_MASK) >> CMD_LOG_BURST_LEN_OFFSET;
	resp = (cmd & CMD_LOG_RESP_MASK) >> CMD_LOG_RESP_OFFSET;
	write = (cmd & CMD_LOG_WRITE_MASK) >> CMD_LOG_WRITE_OFFSET;
	read = (cmd & CMD_LOG_READ_MASK) >> CMD_LOG_READ_OFFSET;
	op = write ? "write" : "read";

	dev_err(dev, "HW log %d: qos %d, burst_len %d, resp %d, op %s\n",
		i, qos, burst_len, resp, op);
}

int ls_bridge2axi_glb_csr_setbits(struct device *dev, u32 bits_mask)
{
	u32 reg_glb_csr;
	struct ls_bridge_ops *ops;

	ops = sel_ops(dev);
	if (!ops) {
		dev_err(dev, "sel ops failed!\n");
		return -ENOMEM;
	}

	if (ops->read_csr(dev, &reg_glb_csr, GLB_CSR_OFFSET)) {
		dev_err(dev, "read glb csr failed!\n");
		return -ERR_RD_XFER_DATA;
	}

	reg_glb_csr = bits_mask << BIT_MASK_OFFSET;
	reg_glb_csr |= bits_mask;

	return ops->write_csr(dev, reg_glb_csr, GLB_CSR_OFFSET);
}

static int ls_bridge2axi_force_pending_clear(struct device *dev)
{
	return ls_bridge2axi_glb_csr_setbits(dev, FORCE_PEND_CLR_MASK);
}

static void fifo_error_detect(struct device *dev)
{
	int ret;
	u32 fifo_status;
	struct ls_bridge_ops *ops;

	ops = sel_ops(dev);
	if (!ops) {
		dev_err(dev, "sel ops failed!\n");
		return;
	}

	ret = ops->read_csr(dev, &fifo_status, FIFO_STS_OFFSET);
	if (ret) {
		dev_err(dev, "spi read fifo status error\n");
		return;
	}

	fifo_status_dump(dev, fifo_status);
}

static void biu_error_detect(struct device *dev)
{
	int ret;
	u32 biu_status0;
	u32 biu_status1;
	u32 err_resp_status;
	struct ls_bridge_ops *ops;

	ops = sel_ops(dev);
	if (!ops) {
		dev_err(dev, "sel ops failed!\n");
		return;
	}

	ret = ops->read_csr(dev, &biu_status0, BIU_STS0_OFFSET);
	if (ret)
		dev_err(dev, "read biu0 status error\n");
	else
		biu_status0_dump(dev, biu_status0);

	ret = ops->read_csr(dev, &biu_status1, BIU_STS1_OFFSET);
	if (ret)
		dev_err(dev, "read biu1 status error\n");
	else
		dev_err(dev, "biu running byte count %d\n", biu_status1);

	ret = ops->read_csr(dev, &err_resp_status, ERR_ADDR_STS_OFFSET);
	if (ret) {
		dev_err(dev, "read err_resp_status error\n");
	} else {
		addr_err_dump(dev, err_resp_status);
	}
}

static void instr_error_detect(struct device *dev)
{
	int ret;
	u32 instr_status;
	struct ls_bridge_ops *ops;

	ops = sel_ops(dev);
	if (!ops) {
		dev_err(dev, "sel ops failed!\n");
		return;
	}

	ret = ops->read_csr(dev, &instr_status, ERR_INSTR_STS_OFFSET);
	if (ret) {
		dev_err(dev, "ls_bridge2axi read instr_status error\n");
		return;
	} else {
		instr_error_dump(dev, instr_status);
	}
}

static int ls_bridge2axi_log_dump(struct device *dev)
{
	int i, ret;
	struct ls_bridge_ops *ops;

	ops = sel_ops(dev);
	if (!ops) {
		dev_err(dev, "sel ops failed!\n");
		return -ENOMEM;
	}

	for (i = 0; i < LOGGER_GRP_NUM; i++) {
		u32 cmd, addr, data;
		ret = ops->read_csr(dev, &cmd, LOGGER_CMD_LOG_GROUP(i));
		if (ret) {
			dev_err(dev, "read LOGGER_CMD_LOG_GROUP%i failed\n", i);
			return ret;
		} else {
			log_dump(dev, cmd, i);
		}

		ret = ops->read_csr(dev, &addr, LOGGER_ADDR_LOG_GROUP(i));
		if (ret) {
			dev_err(dev, "read LOGGER_ADDR_LOG_GROUP%i failed\n", i);
			return ret;
		} else {
			dev_err(dev, "addr 0x%x\n", addr);
		}

		ret = ops->read_csr(dev, &data, LOGGER_DATA_LOG_GROUP(i));
		if (ret) {
			dev_err(dev, "read LOGGER_DATA_LOG_GROUP%i failed\n", i);
			return ret;
		} else {
			dev_err(dev, "data 0x%x\n", data);
		}
	}

	return 0;
}

static void error_dump(struct device *dev)
{
	fifo_error_detect(dev);
	biu_error_detect(dev);
	instr_error_detect(dev);
	ls_bridge2axi_log_dump(dev);
}

uint32_t error_detect(struct device *dev, u32 reg_glb_csr)
{
	int ret;
	u32 glb_err_mask;

	glb_err_mask = FIFO_ERR_MASK | BIU_ERR_MASK | INSTR_ERR_MASK;

	if (reg_glb_csr & glb_err_mask) {
		dev_err(dev, "ls_bridge2axi glb csr 0x%x\n", reg_glb_csr);
		if (reg_glb_csr & FIFO_ERR_MASK) {
			dev_err(dev, "ls_bridge2axi fifo error\n");
			fifo_error_detect(dev);
		}
		if (reg_glb_csr & BIU_ERR_MASK) {
			dev_err(dev, "ls_bridge2axi BIU error\n");
			biu_error_detect(dev);
		}
		if (reg_glb_csr & INSTR_ERR_MASK) {
			dev_err(dev, "ls_bridge2axi invalid instruction\n");
			instr_error_detect(dev);
		}

		ret = ls_bridge2axi_glb_csr_setbits(dev, ERR_CLR_MASK);
		if (ret)
			dev_err(dev, "spi write glb csr error\n");

		ls_bridge2axi_log_dump(dev);

		return -1;
	}

	return 0;
}

/**
 * @brief polling to wait fifo full
 * @param dev	spi or i2c device struct.
 * @param len	data bytes len.
 */
int polling_fifo_full(struct device *dev, u32 len)
{
	int ret;
	u32 fifo_status;
	unsigned long timeout;
	bool full = 0;
	struct ls_bridge_ops *ops;

	ops = sel_ops(dev);
	if (!ops) {
		dev_err(dev, "sel ops failed!\n");
		return -ENOMEM;
	}

	timeout = jiffies + msecs_to_jiffies(1000);
	while (!full) {
		ret = ops->read_csr(dev, &fifo_status, FIFO_STS_OFFSET);
		if (ret) {
			dev_err(dev, "ls_bridge2axi read fifo status error\n");
			return ret;
		}

		full = is_fifo_full(fifo_status, len);

		if (time_after(jiffies, timeout)) {
			dev_err(dev, "ls_bridge2axi fifo full timeout!\n");
			error_dump(dev);
			return -1;
		}
	}

	return 0;
}

int polling_xfer_finish(struct device *dev, u32 *glb_csr)
{
	int ret = 0;
	u32 reg_glb_csr = 0;
	unsigned long timeout;
	struct ls_bridge_ops *ops;

	ops = sel_ops(dev);
	if (!ops) {
		dev_err(dev, "sel ops failed!\n");
		return -ENOMEM;
	}

	timeout = jiffies + msecs_to_jiffies(1000);
	while (!(reg_glb_csr & XFER_FINISH_MASK)) {
		ret = ops->read_csr(dev, &reg_glb_csr, GLB_CSR_OFFSET);
		if (ret) {
			dev_err(dev, "ls_bridge2axi read glb csr error\n");
			goto polling_out;
		}
		if (time_after(jiffies, timeout)) {
			dev_err(dev, "ls_bridge2axi finish timeout!\n");
			ret = -1;
			error_dump(dev);
			goto polling_out;
		}
	}

polling_out:
	*glb_csr = reg_glb_csr;
	return ret;
}

/**
 * check_scatter: check if each scatter is valid
 * @spi: spi device handler
 * @scatter_num: scatter_number of scatters
 * @scatters: scatters array
 * @sz: total buffer size needed by these sactters
 * @return: 0 on success, negtive on failed.
 */
int check_scatter(struct device *dev, u32 scatter_num,
		  struct scatter_wr_unit *scatters, u32 *sz)
{
	int i;
	u32 size = 0;

	for (i = 0; i < scatter_num; i++) {
		if (scatters[i].data_len > MAX_SCATTER_LEN || !scatters[i].data_len) {
			dev_err(dev, "%d scatter len %d invalid!\n",
				i, scatters[i].data_len);
			return -EINVAL;
		}
		if (scatters[i].addr % sizeof(u32)) {
			dev_err(dev, "%d scatter addr 0x%x invalid!\n",
				i, scatters[i].addr);
			return -EINVAL;
		}
		size += scatters[i].data_len;
		/* Addr_len occupy 1 word */
		size++;
	}

	/* CMD occupy 1 byte */
	*sz = (size * sizeof(u32)) + 1;

	return 0;
}

/**
 * scatter_buf_fill: fill spi xfer buffer with scatter info
 * Format is : [cmd] + [addr_len data0 .. dataN] + ...[addr_len data0 .. dataN]
 * @buf: buffer to be filled
 * @scatter_num: scatter_number of scatters
 * @scatters: scatters array
 */
void scatter_buf_fill(u8 *buf, u32 scatter_num,
		      struct scatter_wr_unit *scatters)
{
	int i;
	int buf_id = 0;
	u32 *scatter_data;

	buf[0] = CMD_AXI_SWR;
	scatter_data = (u32 *)&buf[1];
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

void ls_bridge2axi_reset(struct device *dev)
{
	int ret;
	u32 reg_glb_csr = BIU_CLEARING_MASK;
	unsigned long timeout;
	struct ls_bridge_ops *ops;

	ops = sel_ops(dev);
	if (!ops) {
		dev_err(dev, "sel ops failed!\n");
		return;
	}

	ops->send_single_cmd(dev, CMD_DEV_RST);
	timeout = jiffies + msecs_to_jiffies(1000);
	while (reg_glb_csr & BIU_CLEARING_MASK) {
		ret = ops->read_csr(dev, &reg_glb_csr, GLB_CSR_OFFSET);
		if (ret) {
			dev_err(dev, "ls_bridge2axi read glb csr error\n");
			return;
		}
		if (time_after(jiffies, timeout)) {
			dev_err(dev, "BIU clear timeout! ls_bridge2axi reset failed\n");
			ls_bridge2axi_force_pending_clear(dev);
			return;
		}
	}
	dev_warn(dev, "ls_bridge2axi reset success.\n");

	/* Restore device contex */
	if (ops->reinit)
		ops->reinit(dev);
}

int ls_bridge2axi_set_trans_mode(struct device *dev, u8 mode)
{
	struct ls_bridge_ops *ops;
	int ret;
	ops = sel_ops(dev);
	if (!ops) {
		dev_err(dev, "sel ops failed!\n");
		return -EINVAL;
	}

	ret = ops->send_single_cmd(dev, mode);
	if (ret)
		dev_err(dev, "ls_bridge2axi_set_trans_mode error!\n");

	return ret;
}

int ls_bridge2axi_get_version(struct device *dev, u32 *version)
{
	struct ls_bridge_ops *ops;

	ops = sel_ops(dev);
	if (!ops) {
		dev_err(dev, "sel ops failed!\n");
		return -ENOMEM;
	}

	return ops->read_csr(dev, version, CSR_HID_OFFSET);
}

int ls_bridge2axi_scatter_wr(struct device *dev, struct ls_bridge_scatter_msg *msg)
{
	int ret = 0;
	void __user *udata_buf;
	uint32_t size;
	struct scatter_wr_unit *scatters;
	struct ls_bridge_ops *ops;

	ops = sel_ops(dev);
	if (!ops) {
		dev_err(dev, "sel ops failed!\n");
		return -ENOMEM;
	}

	/* write scatter lists */
	udata_buf = (void __user *)msg->scatters;
	size = msg->scatter_num * sizeof(struct scatter_wr_unit);
	scatters = devm_kzalloc(dev, size, GFP_KERNEL);
	if (!scatters) {
		ret = -ENOMEM;
		goto err_spi_scatter_wr;
	}

	if (copy_from_user(scatters, udata_buf, size)) {
		ret = -EINVAL;
		goto err_spi_scatter_mem;
	}

	mutex_lock(&bridge_dev->xfer_lock);
	ret = ops->scatter_write(dev, msg->scatter_num, scatters);
	mutex_unlock(&bridge_dev->xfer_lock);

err_spi_scatter_mem:
	devm_kfree(dev, scatters);

err_spi_scatter_wr:

	return ret;
}

int ls_bridge2axi_ioc_csr(struct device *dev, u8 csr_addr, u32 *val, bool is_write)
{
	int ret;
	struct ls_bridge_ops *ops;

	ops = sel_ops(dev);
	if (!ops) {
		dev_err(dev, "sel ops failed!\n");
		return -ENOMEM;
	}

	mutex_lock(&bridge_dev->xfer_lock);
	if (is_write)
		ret = ops->write_csr(dev, *val, csr_addr);
	else
		ret = ops->read_csr(dev, val, csr_addr);
	mutex_unlock(&bridge_dev->xfer_lock);

	return ret;
}

static int ls_bridge2axi_burst_len_get(struct device *dev, u32 *burst_len)
{
	int ret;
	u32 reg;
	struct ls_bridge_ops *ops;

	ops = sel_ops(dev);
	if (!ops) {
		dev_err(dev, "sel ops failed!\n");
		return -ENOMEM;
	}

	ret = ops->read_csr(dev, &reg, BIU_CTL_OFFSET);
	if (ret) {
		dev_err(dev, "ls_bridge2axi read csr error\n");
		return ret;
	}
	reg = (reg & MAX_BURST_LEN_MASK) >> MAX_BURST_LEN_OFFSET;
	*burst_len = reg + 1;

	return 0;
}

static int ls_bridge2axi_burst_len_set(struct device *dev, u32 burst_len)
{
	int ret;
	u32 reg, cur_burst;
	u32 timeout = 0;
	struct ls_bridge_ops *ops;

	ops = sel_ops(dev);
	if (!ops) {
		dev_err(dev, "sel ops failed!\n");
		return -ENOMEM;
	}

	ret = ops->read_csr(dev, &reg, BIU_CTL_OFFSET);
	if (ret) {
		dev_err(dev, "ls_bridge2axi read csr error\n");
		return ret;
	}
	reg = reg & (~MAX_BURST_LEN_MASK);
	reg |= burst_len - 1;
	ret = ops->write_csr(dev, reg, BIU_CTL_OFFSET);
	if (ret) {
		dev_err(dev, "ls_bridge2axi read csr error\n");
		return ret;
	}

	do {
		ret = ls_bridge2axi_burst_len_get(dev, &cur_burst);
		if (ret)
			return ret;
		if (timeout++ >= 1000) {
			dev_err(dev, "ls_bridge2axi set burst timeout\n");
			return -EBUSY;
		}
	} while (cur_burst != burst_len);

	return 0;
}

int ls_bridge2axi_burst_len_op(struct device *dev, struct ls_bridge_busrt_op *op)
{
	int ret;

	if (op->burst_op == LS_BRIDGE_BURST_LEN_GET) {
		op->max_burst_len = MAX_BURST_LEN;
		ret = ls_bridge2axi_burst_len_get(dev, &op->cur_burst_len);
		if (ret)
			return ret;
	} else {
		u32 burst_len = op->burst_len_set;

		if (burst_len > MAX_BURST_LEN)
			burst_len = MAX_BURST_LEN;

		if (burst_len <= 0)
			burst_len = 1;

		ret = ls_bridge2axi_burst_len_set(dev, burst_len);
		if (ret)
			return ret;
	}

	return 0;
}
