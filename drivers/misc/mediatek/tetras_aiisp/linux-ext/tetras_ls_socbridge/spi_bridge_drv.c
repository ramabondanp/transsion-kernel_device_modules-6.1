/*
 * (C) Copyright 2024, Imvision Co., Ltd
 * This file is classified as confidential level C4 within Imvision
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author         Notes
 * 2023-04-27     yangyuzun      Initialize.
 */

/**
 * @brief   spi2axi driver
 * @date    2023-04-27
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include "spibridge.h"
#include "spi_bridge_drv.h"
#include "ls_bridge2axi.h"
#include "ls_bridge_internel.h"

#define is_unaligned(a) ((a % 4) != 0)
#define MAX_REXER_TIMES		(6)

static int spi_check_data(struct device *dev, u32 data_len, u32 addr)
{

	if (bridge_dev->version_id == AI_ISP_V1_HID &&
	    (is_unaligned(data_len) || is_unaligned(addr))) {
		dev_err(dev, "data length and addr must be\
		a multiple of 4 bytes!\n");
		return -EINVAL;
	}

	return 0;
}

/**
 * @brief write an amount of data via spibridge to isp soc.
 * NOTE: v1 only support words level(32 bit) level transfer.
 * v2 support byte level transfer.
 * @param addr		addr the ahb/axi addr to write.
 * @param data		write buffer start address
 * @param data_len	the number of bytes to write
 * @return		success ?
 * @ retval 0		ok
 * @ retval others	err
 */
int spi_bridge_write(u32 addr, const u8 *data, u32 data_len)
{
	int ret, i;
	struct spibridge_priv *priv;
	struct spibridge *bridge;
	struct device *dev;

	priv = spi_get_drvdata(bridge_dev->spibridge_slave);
	dev = &bridge_dev->spibridge_slave->dev;

	if (spi_check_data(dev, data_len, addr))
		return -EINVAL;

	mutex_lock(&bridge_dev->xfer_lock);

	bridge = priv->cur_bridge;

	for (i = 0; i < MAX_REXER_TIMES; ++i) {
		ret = bridge->ops->write(dev, addr, data,
					 data_len, WRITE_BY_POLLING);
		if (!ret) {
			if (i) {
				pr_warn("Write op finished after %d retrys\n", i);
			}
			break;
		} else if (priv->xfer_timedout == -ETIMEDOUT) {
			if (!strcmp(bridge->name, "spi2axi")) {
				ls_bridge2axi_reset(dev);
			}
		}
	}

	priv->xfer_timedout = 0;
	xfer_log_record(addr, data_len, true, ret);
	if (ret) {
		if (!strcmp(bridge->name, "spi2axi"))
			ls_bridge2axi_reset(dev);
		xfer_log_dump(dev);
	}
	mutex_unlock(&bridge_dev->xfer_lock);

	return ret;
}
EXPORT_SYMBOL(spi_bridge_write);

/**
 * @brief read an amount of data via spibridge to isp soc.
 * NOTE: v1 only support words level(32 bit) level transfer.
 * v2 support byte level transfer.
 * @param addr		addr the ahb/axi addr to read.
 * @param data		read buffer start address.
 * @data_len		the number of bytes to read
 * @return		success ?
 * @ retval 0		ok
 * @ retval others	err
 */
int spi_bridge_read(u32 addr, u8 *data, u32 data_len)
{
	int ret, i;
	struct spibridge_priv *priv;
	struct spibridge *bridge;
	struct device *dev;

	priv = spi_get_drvdata(bridge_dev->spibridge_slave);
	dev = &bridge_dev->spibridge_slave->dev;

	if (spi_check_data(dev, data_len, addr))
		return -EINVAL;

	mutex_lock(&bridge_dev->xfer_lock);

	bridge = priv->cur_bridge;

	for (i = 0; i < MAX_REXER_TIMES; ++i) {
		ret = bridge->ops->read(dev, addr, data,
					data_len, READ_WITH_32_WAIT_CYCLES);
		if (!ret) {
			if (i) {
				pr_warn("Read op finished after %d retrys\n", i);
			}
			break;
		} else if (priv->xfer_timedout == -ETIMEDOUT) {
			if (!strcmp(bridge->name, "spi2axi")) {
				ls_bridge2axi_reset(dev);
			}
		}
	}

	priv->xfer_timedout = 0;
	xfer_log_record(addr, data_len, false, ret);
	if (ret) {
		if (!strcmp(bridge->name, "spi2axi"))
			ls_bridge2axi_reset(dev);
		xfer_log_dump(dev);
	}
	mutex_unlock(&bridge_dev->xfer_lock);

	return ret;
}
EXPORT_SYMBOL(spi_bridge_read);

static struct ls_bridge_ops *spi_bridge_ops_tab[SPI_BRIDGE_NUM] = {
	[SPI_BRIDGE_SPI2AXI] = &spi2axi,
	[SPI_BRIDGE_SPI2AHB] = &spi2ahb,
};

int switch_bridge(struct spi_device *spi, u32 bridge_id)
{
	int ret = 0;
	struct spibridge_priv *priv;
	struct spibridge *cur_bridge;
	struct spibridge *tar_bridge;
	struct device *dev;

	dev = &bridge_dev->spibridge_slave->dev;
	if (bridge_id >= SPI_BRIDGE_NUM) {
		dev_err(&spi->dev, "invalid bridge id 0x%x\n", bridge_id);
		return -ENODEV;
	}

	priv = spi_get_drvdata(bridge_dev->spibridge_slave);
	if (!priv->switch_enable) {
		dev_err(&spi->dev, "bridge switch disable\n");
		return -ENODEV;
	}

	tar_bridge = priv->bridges[bridge_id];
	if (!tar_bridge) {
		dev_err(&spi->dev, "0x%x bridge abnormal\n", bridge_id);
		return -ENODEV;
	}

	mutex_lock(&bridge_dev->xfer_lock);

	cur_bridge = priv->cur_bridge;
	if (cur_bridge == tar_bridge) {
		dev_err(&spi->dev, "already using %s\n", cur_bridge->name);
		goto switch_unlock;
	}

	/* Use current bridge to write switch register */
	ret = cur_bridge->ops->write(dev, priv->switch_reg_addr,
				     (u8 *)&tar_bridge->hw_sel, 4, WRITE_NO_CHECK);
	if (ret) {
		dev_err(&spi->dev, "switch to %s failed\n", tar_bridge->name);
		goto switch_unlock;
	}

	if (!strcmp(tar_bridge->name, "spi2axi"))
		ls_bridge2axi_reset(dev);

	priv->cur_bridge = tar_bridge;
	dev_info(&spi->dev, "switch to %s success\n", priv->cur_bridge->name);

switch_unlock:
	mutex_unlock(&bridge_dev->xfer_lock);

	return ret;
}

static bool is_bridge_implemented(const char *name, u32 *bridge_id)
{
	int i;
	struct ls_bridge_ops *ops;

	for (i = 0; i < SPI_BRIDGE_NUM; i++) {
		ops = spi_bridge_ops_tab[i];
		if (!strcmp(ops->name, name)) {
			*bridge_id = i;
			return true;
		}
	}

	return false;
}

static struct spibridge *create_child_bridge(struct device *dev,
					    struct device_node *child,
					    u32 *bridge_id)
{
	u32 hw_sel;
	u32 id;
	int ret;
	const char *bridge_name;
	struct spibridge *bridge;
	struct spi_device *spi = to_spi_device(dev);
	struct spibridge_priv *spibridge = spi_get_drvdata(spi);

	ret = of_property_read_string(child, "bridge_name", &bridge_name);
	if (ret) {
		dev_err(&spi->dev, "invalid node, no bridge_name\n");
		return NULL;
	}

	if (!is_bridge_implemented(bridge_name, &id)) {
		dev_err(&spi->dev, "invalid bridge_name %s\n", bridge_name);
		return NULL;
	}

	ret = of_property_read_u32(child, "hw_sel", &hw_sel);
	if (ret) {
		dev_err(&spi->dev, "invalid node, no hw_sel\n");
		return NULL;
	}

	bridge = devm_kzalloc(&spi->dev, sizeof(*bridge), GFP_KERNEL);
	if (!bridge)
		return NULL;
	bridge->hw_sel = hw_sel;
	bridge->name = bridge_name;
	bridge->ops = spi_bridge_ops_tab[id];

	spibridge->bridges[id] = bridge;

	ret = bridge->ops->dt_probe(dev, child, id);

	if (ret) {
		dev_err(&spi->dev, "%s dt probe failed\n", bridge->name);
		devm_kfree(&spi->dev, bridge);
		bridge = NULL;
		spibridge->bridges[id] = bridge;
	}

	*bridge_id = id;

	return bridge;
}

/* init spibridge and select 2axi or 2ahb */
int sel_cur_spibridge(struct spi_device *spi)
{
	int ret;
	u32 switch_reg_addr, version;
	struct spibridge_priv *spibridge;
	struct device_node *nc = spi->dev.of_node;
	struct device_node *child;

	spibridge = devm_kzalloc(&spi->dev, sizeof(*spibridge), GFP_KERNEL);
	if (!spibridge) {
		dev_err(&spi->dev, "malloc spibridge failed.\n");
		return -ENOMEM;
	}

	ret = of_property_read_u32(nc, "switch_reg_addr", &switch_reg_addr);
	if (ret) {
		spibridge->switch_enable = false;
	} else {
		spibridge->switch_enable = true;
		spibridge->switch_reg_addr = switch_reg_addr;
		dev_info(&spi->dev, "switch addr 0x%x\n", switch_reg_addr);
	}

	spi_set_drvdata(spi, spibridge);

	for_each_available_child_of_node(nc, child) {
		struct spibridge *bridge;
		u32 bridge_id;

		bridge = create_child_bridge(&spi->dev, child, &bridge_id);
		if (!bridge)
			continue;

		if (of_property_read_bool(child, "default_bridge")) {
			spibridge->cur_bridge = bridge;
			switch (bridge_id) {
			case SPI_BRIDGE_SPI2AXI:
				ret = ls_bridge2axi_get_version(&spi->dev, &version);
				bridge_dev->version_id = version >> 16;
				break;
			case SPI_BRIDGE_SPI2AHB:
				bridge_dev->version_id = AI_ISP_V1_HID;
				break;
			}
			dev_info(&spi->dev, "version id is 0x%x.\n",
				 bridge_dev->version_id);
		}

		dev_info(&spi->dev, "%s dt probe success\n", bridge->name);
	}

	if (!spibridge->cur_bridge) {
		dev_err(&spi->dev, "no default bridge found\n");
		return -ENODEV;
	}

	return 0;
}


u32 get_cur_bridge_id(struct spibridge_priv *priv)
{
	int i;
	struct spibridge *cur_bridge;

	cur_bridge = priv->cur_bridge;
	for (i = 0; i < SPI_BRIDGE_NUM; i++) {
		if (cur_bridge == priv->bridges[i])
			return i;
	}

	return 0;
}

int spibridge_speed_op(struct spi_device *spi, struct ls_bridge_speed_msg *msg)
{
	struct spibridge_priv *priv;
	struct spibridge *bridge;

	priv = spi_get_drvdata(bridge_dev->spibridge_slave);
	bridge = priv->cur_bridge;

	return spi2axi_speed_op(&spi->dev, msg);
}

int spibridge_set_mode(struct spi_device *spi, u32 mode)
{
	return spi2axi_set_mode(&spi->dev, mode);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
static int spi_bridge_remove(struct spi_device *spi)
{
	struct spibridge_priv *spibridge = spi_get_drvdata(spi);

	devm_kfree(&spi->dev, spibridge);

	return 0;
}
#else
static void spi_bridge_remove(struct spi_device *spi)
{
	struct spibridge_priv *spibridge = spi_get_drvdata(spi);

	devm_kfree(&spi->dev, spibridge);
}
#endif

static int spibridge_probe(struct spi_device *spi)
{
	int ret;

	ret = sel_cur_spibridge(spi);
	if (ret) {
		dev_err(&spi->dev, "sel current bridge failed.\n");
		return ret;
	}

	if (!bridge_dev) {
		dev_err(&spi->dev, "no bridge_dev\n");
		return -ENOMEM;
	} else {
		bridge_dev->spibridge_slave = spi;
	}

	return 0;
}

static const struct of_device_id spibridge_dt_ids[] = {
	{ .compatible = "tetras,spibridge", },
	{ }
};

struct spi_driver spibridge_driver = {
	.driver = {
		.name = "spibridge",
		.of_match_table = of_match_ptr(spibridge_dt_ids),
	},
	.probe  = spibridge_probe,
	.remove = spi_bridge_remove,
};
