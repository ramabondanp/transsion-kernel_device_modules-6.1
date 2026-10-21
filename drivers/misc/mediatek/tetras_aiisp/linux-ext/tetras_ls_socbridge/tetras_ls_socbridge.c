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
 * @brief   low speed soc_bridge driver
 * @date    2021-12-24
 */

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include "spibridge.h"
#include "ls_bridge_internel.h"
#include "ls_bridge2axi.h"
#include "spi_bridge_drv.h"
#include "i2c_bridge_drv.h"

struct bridge_priv *bridge_dev;

/**
 * @brief select device by device_id.
 * @param dev	SPI_DEVICE or I2C_DEVICE specified by user.
 * @param bridge_2axi	if is true, only return spi2axi.
 */
struct device *sel_device(u8 device_id, bool bridge2axi)
{
	struct device *spi_dev;

	switch (device_id) {
	case SPI_DEVICE:
		spi_dev = &bridge_dev->spibridge_slave->dev;
		if (bridge2axi) {
			struct spibridge *bridge;
			struct spibridge_priv *priv;
			priv = dev_get_drvdata(spi_dev);
			bridge = priv->cur_bridge;
			if (!strcmp(bridge->name, "spi2ahb")) {
				pr_err("spi2ahb don't support this ops!\n");
				spi_dev = NULL;
			}
		}
		return spi_dev;
	case I2C_DEVICE:
		if (!bridge_dev->i2cbridge_slave) {
			pr_err("i2c_slave is null\n");
			return NULL;
		}
		return &bridge_dev->i2cbridge_slave->dev;
	default:
		pr_err("invalid device id.\n");
		return NULL;
	}
}

/**
 * @brief: ls_bridge 2axi/2ahb read/write ai-isp memery interface.
 * @param ubuf		must be struct ls_bridge_msg.
 * @param is_write	write flag,true is write,false is read.
 * @return		success ?
 * @ retval 0		ok
 * @ retval others	err
 */
static int bridge_ioc_xfer(void __user *ubuf, bool is_write)
{
	int ret = 0;
	u8 *data;
	struct ls_bridge_msg msg;
	struct device *dev;
	void __user *udata_buf;

	data = bridge_dev->ioc_xfer_buf;

	mutex_lock(&bridge_dev->ioctl_lock);

	if (copy_from_user(&msg, ubuf, sizeof(struct ls_bridge_msg))) {
		pr_err("ls_bridge ioc xfer copy data from user failed!\n");
		ret = -EINVAL;
		goto err_rw;
	}

	dev = sel_device(msg.device_id, false);
	if (!dev) {
		ret = -ENOMEM;
		goto err_rw;
	}

	/* Make sure data_len(in byte len) not exceed buffer len */
	if (msg.data_len > (XFER_MAX_BUFFER_LEN)) {
		dev_err(dev, "data_len exceed buffer len!\n");
		ret = -EINVAL;
		goto err_rw;
	}

	/* write data or read data buffer */
	udata_buf = (void __user *)msg.buffer;

	if (!is_write) {
		switch (msg.device_id) {
		case SPI_DEVICE:
			ret = spi_bridge_read(msg.addr, data, msg.data_len);
			break;
		case I2C_DEVICE:
			ret = i2c_bridge_read(msg.addr, data, msg.data_len);
			break;
		default:
			ret = -EINVAL;
			goto err_rw;
		}

		if (ret) {
			msg.error_code = (__u32)-ret;
			goto err_rw;
		}
		/* Write received data to userspcae */
		if (copy_to_user(udata_buf, bridge_dev->ioc_xfer_buf,
				 msg.data_len)) {
			ret = -EINVAL;
			goto err_rw;
		}
	} else {
		/* Copy write data from userspcae */
		if (copy_from_user(bridge_dev->ioc_xfer_buf,
				   udata_buf, msg.data_len)) {
			ret = -EINVAL;
			goto err_rw;
		}
		switch (msg.device_id) {
		case SPI_DEVICE:
			ret = spi_bridge_write(msg.addr,
					       data, msg.data_len);
			break;
		case I2C_DEVICE:
			ret = i2c_bridge_write(msg.addr,
					       data, msg.data_len);
			break;
		default:
			ret = -EINVAL;
			goto err_rw;
		}

		if (ret) {
			msg.error_code = (__u32)-ret;
			goto err_rw;
		}
	}

err_rw:
	if (copy_to_user(ubuf, &msg, sizeof(msg)))
		ret = -EINVAL;
	mutex_unlock(&bridge_dev->ioctl_lock);

	return ret;
}

static int bridge_ioc_set_mode(void __user *ubuf)
{
	int ret = 0;
	struct spi_device *spi_slave;
	struct i2c_client *i2c_slave;
	struct ls_bridge_msg msg;
	struct device *dev;

	mutex_lock(&bridge_dev->ioctl_lock);

	if (copy_from_user(&msg, ubuf, sizeof(struct ls_bridge_msg))) {
		pr_err("bridge_ioc_set_mode copy data from user failed!\n");
		ret = -EINVAL;
		goto err_mode;
	}

	spi_slave = bridge_dev->spibridge_slave;
	i2c_slave = bridge_dev->i2cbridge_slave;

	if (msg.device_id == SPI_DEVICE) {
		if (spi_slave == NULL) {
			pr_err("invalid spi_slave!\n");
			ret = -EINVAL;
			goto err_mode;
		}
		dev = &spi_slave->dev;
	} else if (msg.device_id == I2C_DEVICE) {
		if (i2c_slave == NULL) {
			pr_err("invalid i2c_slave!\n");
			ret = -EINVAL;
			goto err_mode;
		}
		dev = &i2c_slave->dev;
	} else {
		pr_err("invalid device_id!\n");
		ret = -EINVAL;
		goto err_mode;
	}

	switch (msg.flag) {
	case SPI_MODE_0:
	case SPI_MODE_3:
		ret = spibridge_set_mode(spi_slave, msg.flag);
		break;
	case CMD_USE_BYTE:
	case CMD_USE_WORD:
		ret = ls_bridge2axi_set_trans_mode(dev, msg.flag);
		break;
	default:
		pr_err("invalid mode!\n");
		ret = -EINVAL;
		break;
	}

err_mode:
	mutex_unlock(&bridge_dev->ioctl_lock);

	return ret;
}

static int bridge_ioc_reset(void __user *ubuf)
{
	int ret = 0;
	struct spi_device *spi_slave;
	struct i2c_client *i2c_slave;
	struct ls_bridge_msg msg;
	struct device *dev;

	mutex_lock(&bridge_dev->ioctl_lock);

	if (copy_from_user(&msg, ubuf, sizeof(struct ls_bridge_msg))) {
		pr_err("bridge_ioc_reset copy data from user failed!\n");
		ret = -EINVAL;
		goto err_reset;
	}

	spi_slave = bridge_dev->spibridge_slave;
	i2c_slave = bridge_dev->i2cbridge_slave;

	if (msg.device_id == SPI_DEVICE) {
		if (spi_slave == NULL) {
			pr_err("invalid spi_slave!\n");
			ret = -EINVAL;
			goto err_reset;
		}
		dev = &spi_slave->dev;
		ls_bridge2axi_reset(dev);
		ret = spi2axi_tunning(dev);
	} else if (msg.device_id == I2C_DEVICE) {
		if (i2c_slave == NULL) {
			pr_err("invalid i2c_slave!\n");
			ret = -EINVAL;
			goto err_reset;
		}
		dev = &i2c_slave->dev;
		ls_bridge2axi_reset(dev);
	} else {
		pr_err("invalid device_id!\n");
		ret = -EINVAL;
	}

err_reset:
	mutex_unlock(&bridge_dev->ioctl_lock);

	return ret;
}

static int bridge_ioc_csr(void __user *ubuf, bool is_write)
{
	int ret = 0;
	struct ls_bridge_csr_msg msg;
	struct device *dev = NULL;

	mutex_lock(&bridge_dev->ioctl_lock);
	if (copy_from_user(&msg, ubuf, sizeof(struct ls_bridge_csr_msg))) {
		ret = -EINVAL;
		pr_err("ls_bridge ioc csr copy data from user failed!\n");
		goto err_ioc_csr;
	}

	dev = sel_device(msg.device_id, true);
	if (!dev) {
		ret = -ENOMEM;
		goto err_ioc_csr;
	}

	ret = ls_bridge2axi_ioc_csr(dev, msg.reg_addr,
				    &msg.reg_val, is_write);

err_ioc_csr:
	ret = copy_to_user(ubuf, &msg, sizeof(struct ls_bridge_csr_msg));
	mutex_unlock(&bridge_dev->ioctl_lock);

	return ret;
}

static int bridge_ioc_scatter_wr(void __user *ubuf)
{
	int ret = 0;
	struct ls_bridge_scatter_msg msg;
	struct device *dev;

	mutex_lock(&bridge_dev->ioctl_lock);

	if (copy_from_user(&msg, ubuf, sizeof(msg))) {
		pr_err("ls_bridge ioc scatter_wr copy \
			data from user failed!\n");
		ret = -EINVAL;
		goto err_bridge_scatter;
	}

	dev = sel_device(msg.device_id, true);
	if (!dev) {
		ret = -ENOMEM;
		goto err_bridge_scatter;
	}

	ret = ls_bridge2axi_scatter_wr(dev, &msg);

err_bridge_scatter:
	mutex_unlock(&bridge_dev->ioctl_lock);

	return ret;
}

static int bridge_burst_op(void __user *ubuf)
{
	struct device *dev;
	int ret;
	struct ls_bridge_busrt_op burst_msg;

	mutex_lock(&bridge_dev->ioctl_lock);

	if (copy_from_user(&burst_msg, ubuf, sizeof(burst_msg))) {
		pr_err("bridge_burst_op copy data from user failed!\n");
		ret = -EINVAL;
		goto err_burst;
	}

	dev = sel_device(burst_msg.device_id, true);
	if (!dev) {
		ret = -ENOMEM;
		goto err_burst;
	}

	ret = ls_bridge2axi_burst_len_op(dev, &burst_msg);

	if (copy_to_user(ubuf, &burst_msg, sizeof(burst_msg))) {
		pr_err("bridge_burst_op copy data to user failed!\n");
		ret = -EINVAL;
		goto err_burst;
	}

err_burst:
	mutex_unlock(&bridge_dev->ioctl_lock);

	return ret;
}

static int bridge_ioc_speed_op(void __user *ubuf)
{
	int ret = 0;
	struct spi_device *spi_slave;
	struct ls_bridge_speed_msg speed_msg;

	mutex_lock(&bridge_dev->ioctl_lock);

	spi_slave = bridge_dev->spibridge_slave;

	if (copy_from_user(&speed_msg, ubuf, sizeof(struct ls_bridge_speed_msg))) {
		pr_err("bridge_ioc_speed_op copy data from user failed!\n");
		ret = -EINVAL;
		goto err_speed;
	}

	mutex_lock(&bridge_dev->xfer_lock);
	ret = spibridge_speed_op(spi_slave, &speed_msg);
	mutex_unlock(&bridge_dev->xfer_lock);

	if (copy_to_user(ubuf, &speed_msg, sizeof(struct ls_bridge_speed_msg))) {
		pr_err("bridge_ioc_speed_op copy data to user failed!\n");
		ret = -EINVAL;
		goto err_speed;
	}

err_speed:
	mutex_unlock(&bridge_dev->ioctl_lock);

	return ret;
}

static int bridge_ioc_devinfo(void __user *ubuf)
{
	int ret = 0;
	struct spi_device *spi_slave;
	struct spibridge_info info;
	struct spibridge_priv *spibridge;

	mutex_lock(&bridge_dev->ioctl_lock);

	spi_slave = bridge_dev->spibridge_slave;

	spibridge = spi_get_drvdata(spi_slave);
	if (!spibridge) {
		pr_err("spibridge error!\n");
		ret = -EINVAL;
		goto err_devinfo;
	}

	info.max_xfer_len = XFER_MAX_BUFFER_LEN;
	info.cur_bridge_id = get_cur_bridge_id(spibridge);
	mutex_lock(&bridge_dev->xfer_lock);
	ret = ls_bridge2axi_get_version(&spi_slave->dev, &info.version_id);
	mutex_unlock(&bridge_dev->xfer_lock);

	if (copy_to_user(ubuf, &info, sizeof(info))) {
		pr_err("bridge_ioc_devinfo copy data to user failed!\n");
		ret = -EINVAL;
		goto err_devinfo;
	}

err_devinfo:
	mutex_unlock(&bridge_dev->ioctl_lock);

	return ret;
}

static int bridge_ioc_switch_bridge(void __user *ubuf)
{
	int ret = 0;
	u32 bridge_id;
	struct spi_device *spi_slave;

	mutex_lock(&bridge_dev->ioctl_lock);

	spi_slave = bridge_dev->spibridge_slave;

	if (copy_from_user(&bridge_id, ubuf, sizeof(u32))) {
		pr_err("bridge_ioc_switch_bridge copy data from user failed!\n");
		ret = -EINVAL;
		goto err_switch;
	}

	ret = switch_bridge(spi_slave, bridge_id);

err_switch:
	mutex_unlock(&bridge_dev->ioctl_lock);

	return ret;
}

static long bridge_ioctl(struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	void __user *ubuf = (void __user *)arg;
	struct spi_device *spi_slave;

	spi_slave = bridge_dev->spibridge_slave;
	if (!spi_slave) {
		pr_err("invalid spi_slave!\n");
		return -EINVAL;
	}

	switch (cmd) {
	case LS_BRIDGE_CMD_READ:
		ret = bridge_ioc_xfer(ubuf, false);
		break;
	case LS_BRIDGE_CMD_WRITE:
		ret = bridge_ioc_xfer(ubuf, true);
		break;
	case LS_BRIDGE_CMD_SCAT_WR:
		ret = bridge_ioc_scatter_wr(ubuf);
		break;
	case LS_BRIDGE_CMD_READ_CSR:
		ret = bridge_ioc_csr(ubuf, false);
		break;
	case LS_BRIDGE_CMD_WRITE_CSR:
		ret = bridge_ioc_csr(ubuf, true);
		break;
	case LS_BRIDGE_CMD_BURST_LEN:
		ret = bridge_burst_op(ubuf);
		break;
	/* spi unique options */
	case SPIBRIDGE_CMD_SPEED_OP:
		ret = bridge_ioc_speed_op(ubuf);
		break;
	case SPIBRIDGE_CMD_SET_MODE:
		ret = bridge_ioc_set_mode(ubuf);
		break;
	case SPIBRIDGE_CMD_RESET:
		ret = bridge_ioc_reset(ubuf);
		break;
	case SPIBRIDGE_CMD_DEVINFO:
		ret = bridge_ioc_devinfo(ubuf);
		break;
	case SPIBRIDGE_CMD_SWITCH:
		ret = bridge_ioc_switch_bridge(ubuf);
		break;
	default:
		pr_err("invalid cmd!\n");
		return -ENOIOCTLCMD;
	}

	return ret;
}

void xfer_log_record(u32 addr, u32 len, bool is_write, int ret)
{
	u32 cur_rec_id;
	struct xfer_record *rec;

	cur_rec_id = bridge_dev->cur_rec_id;
	rec = &bridge_dev->records[cur_rec_id];
	rec->addr = addr;
	rec->len = len;
	rec->is_write = is_write;
	rec->ret = ret;

	cur_rec_id++;
	cur_rec_id = cur_rec_id % MAX_RECORD_NUM;
	bridge_dev->cur_rec_id = cur_rec_id;
}

void xfer_log_dump(struct device *dev)
{
	u32 cur_rec_id;
	u32 i;
	struct xfer_record *rec;
	char *op;

	for (i = 0; i < MAX_RECORD_NUM; i++) {
		cur_rec_id = (bridge_dev->cur_rec_id + i) % MAX_RECORD_NUM;
		rec = &bridge_dev->records[cur_rec_id];
		op = rec->is_write ? "write" : "read";
		dev_err(dev, "SW rec %d : Addr 0x%x, Len 0x%x, %s, Ret %d\n",
			i, rec->addr, rec->len, op, rec->ret);
	}
}

static int bridge_open(struct inode *inode, struct file *file)
{
	if (!bridge_dev || !bridge_dev->spibridge_slave) {
		pr_err("socbridge: no device found!\n");
		return -EINVAL;
	}

	file->private_data = bridge_dev;

	return 0;
}

static int bridge_close(struct inode *inode, struct file *file)
{
	file->private_data = NULL;

	return 0;
}

static ssize_t bridge_read(struct file *file, char __user *buf,
			   size_t len, loff_t *offp)
{
	return -EFAULT;
}

static const struct file_operations spibridge_misc_fops = {
	.owner		= THIS_MODULE,
	.read		= bridge_read,
	.unlocked_ioctl	= bridge_ioctl,
	.open		= bridge_open,
	.release	= bridge_close,
};

static struct miscdevice spibridge_misc = {
	MISC_DYNAMIC_MINOR,
	"tetras_spibridge",
	&spibridge_misc_fops,
};

static int __init bridge_init(void)
{
	int ret;

	bridge_dev = kzalloc(sizeof(struct bridge_priv), GFP_KERNEL);

	if (!bridge_dev) {
		pr_err("tetras_ls_socbridge bridge_dev is null!\n");
		return -ENOMEM;
	}

	bridge_dev->ioc_xfer_buf = kzalloc(XFER_MAX_BUFFER_LEN, GFP_KERNEL);
	if (!bridge_dev->ioc_xfer_buf) {
		pr_err("tetras_ls_socbridge ioxfer buff is null!\n");
		return -ENOMEM;
	}

	ret = spi_register_driver(&spibridge_driver);
	if (ret) {
		pr_err("spibridge driver register failed!\n");
		return ret;
	}

	ret = misc_register(&spibridge_misc);
	if (ret) {
		pr_err("spibridge: can't misc_register\n");
		spi_unregister_driver(&spibridge_driver);
		return ret;
	}

	/* v1 may have not i2c2axi */
	if (i2c_add_driver(&i2cbridge_driver))
		pr_err("i2cbridge driver register failed!\n");
	mutex_init(&bridge_dev->xfer_lock);
	mutex_init(&bridge_dev->ioctl_lock);

	return 0;
}

static void __exit bridge_exit(void)
{
	misc_deregister(&spibridge_misc);
	spi_unregister_driver(&spibridge_driver);
	i2c_del_driver(&i2cbridge_driver);

	mutex_destroy(&bridge_dev->xfer_lock);
	mutex_destroy(&bridge_dev->ioctl_lock);

	kfree(bridge_dev);
}

module_init(bridge_init);
module_exit(bridge_exit);

MODULE_AUTHOR("yanghua@tetras.ai");
MODULE_DESCRIPTION("Spibridge Protocol Driver: v2.0");
MODULE_LICENSE("GPL v2");
