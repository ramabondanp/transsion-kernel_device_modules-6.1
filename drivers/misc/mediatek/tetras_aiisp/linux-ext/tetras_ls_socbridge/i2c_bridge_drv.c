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
 * @brief   i2c2axi driver
 * @date    2023-04-27
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include "spibridge.h"
#include "ls_bridge2axi.h"
#include "ls_bridge_internel.h"
#include "i2c_bridge_drv.h"

static int get_i2c_trans_len(struct device *dev, u32 data_len)
{
	struct i2cbridge_priv *priv;

	priv = dev_get_drvdata(dev);
	if (priv->max_buf_len)
		return MIN(data_len, priv->max_buf_len);

	return data_len;
}

int i2c_bridge_read(u32 addr, u8 *data, u32 data_len)
{
	int ret;
	u32 len;
	struct i2cbridge_priv *priv;
	struct device *dev;

	dev = sel_device(I2C_DEVICE, true);
	if (!dev)
		return -ENOMEM;
	priv = dev_get_drvdata(dev);

	mutex_lock(&bridge_dev->xfer_lock);
	while (data_len) {
		len = get_i2c_trans_len(dev, data_len);
		ret = priv->ops->read(dev, addr,
				      data, len, 0);
		xfer_log_record(addr, len, false, ret);
		if (ret) {
			xfer_log_dump(dev);
			break;
		}
		data_len -= len;
		data += len;
		addr += len;
	}
	mutex_unlock(&bridge_dev->xfer_lock);

	return ret;
}
EXPORT_SYMBOL(i2c_bridge_read);

int i2c_bridge_write(u32 addr, u8 *data, u32 data_len)
{
	int ret;
	u32 len;
	struct i2cbridge_priv *priv;
	struct device *dev;

	dev = sel_device(I2C_DEVICE, true);
	if (!dev)
		return -ENOMEM;
	priv = dev_get_drvdata(dev);

	mutex_lock(&bridge_dev->xfer_lock);
	while (data_len) {
		len = get_i2c_trans_len(dev, data_len);
		ret = priv->ops->write(dev, addr,
				       data, len, 0);
		xfer_log_record(addr, data_len, true, ret);
		if (ret) {
			xfer_log_dump(dev);
			break;
		}
		data_len -= len;
		data += len;
		addr += len;
	}
	mutex_unlock(&bridge_dev->xfer_lock);

	return ret;
}
EXPORT_SYMBOL(i2c_bridge_write);

static const struct i2c_device_id i2c2axi_id[] = {
	{ "tetras,i2c2axi", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, i2c2axi_id);

static const struct of_device_id i2c2axi_dt_ids[] = {
	{ .compatible = "tetras,i2c2axi", },
	{ }
};

int i2c2axi_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct i2cbridge_priv *i2cbridge;
	struct device_node *nc = client->dev.of_node;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "I2C check functionality failed.\n");
		return -ENXIO;
	}

	i2cbridge = devm_kzalloc(&client->dev, sizeof(struct i2cbridge_priv), GFP_KERNEL);
	if (!i2cbridge) {
		dev_err(&client->dev, "malloc i2cbridge failed.\n");
		return -ENOMEM;
	}
	i2c_set_clientdata(client, i2cbridge);

	bridge_dev->i2cbridge_slave = client;

	i2cbridge->ops = &i2c2axi;

	if (of_property_read_u32(nc, "max_buf_len", &i2cbridge->max_buf_len)) {
		dev_warn(&client->dev, "max_buf_len not set, please ensure\
			 i2c transfer buf has not limit!\n");
	}
	dev_info(&client->dev, "i2c trasfer max buf len is %d.\n", i2cbridge->max_buf_len);
	dev_info(&client->dev, "i2c2axi dt probe success\n");

	return 0;
}
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
int i2c2axi_remove(struct i2c_client *client)
{
	struct i2cbridge_priv *i2cbridge;

	i2cbridge = i2c_get_clientdata(client);
	devm_kfree(&client->dev, i2cbridge);

	return 0;
}
#else
void i2c2axi_remove(struct i2c_client *client)
{
	struct i2cbridge_priv *i2cbridge;

	i2cbridge = i2c_get_clientdata(client);
	devm_kfree(&client->dev, i2cbridge);
}
#endif
struct i2c_driver i2cbridge_driver = {
	.probe = i2c2axi_probe,
	.remove = i2c2axi_remove,
	.id_table = i2c2axi_id,
	.driver = {
		.name = "i2c2axi",
		.of_match_table = of_match_ptr(i2c2axi_dt_ids),
	},
};
