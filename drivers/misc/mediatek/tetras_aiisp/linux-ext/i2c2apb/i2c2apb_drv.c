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

#include <linux/types.h>
#include <linux/version.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/printk.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include "ixc2apb.h"
#include "i2c2apb_drv.h"

/**
 * i2c2apb_i2c_wrrd - read data from a register of the i2c slave device.
 *
 * @client: i2c device.
 * @inst: instruction
 * @buf: raw write data buffer.
 * @len: length of the buffer to write
 * Returns negative errno, 0 for success
 */
static int i2c2apb_i2c_wrrd(struct i2c_client *client, unsigned int inst,
			    char *buf, unsigned int len)
{
	struct i2c_msg msgs[2];
	int ret;
	int i = 0;

	pr_debug("head 0x%x len %d", inst, len);
	if ((len > MAX_I2C_RW_DATA_LEN) || (len % IxC2APB_OPS_UNIT)) {
		pr_err("read len %d err, max %d B, must be %d B align\n",
		       len, MAX_I2C_RW_DATA_LEN, IxC2APB_OPS_UNIT);
		return -EINVAL;
	}

	msgs[i].flags = 0;
	msgs[i].addr  = client->addr;
	msgs[i].len   = IxC2APB_OPS_UNIT;
	msgs[i].buf   = (u8 *)&inst;

	i++;
	msgs[i].flags = I2C_M_RD;
	msgs[i].addr  = client->addr;
	msgs[i].len   = len;
	msgs[i].buf   = buf;

	ret = i2c_transfer(client->adapter, &msgs[0], 2);

	for (i = 0; i < msgs[0].len; i++) {
		pr_debug("msg0[%d] = 0x%x\n", i, msgs[0].buf[i]);
	}
	for (i = 0; i < msgs[1].len; i++) {
		pr_debug("msg1[%d] = 0x%x\n", i, msgs[1].buf[i]);
	}

	return ret < 0 ? ret : (ret != ARRAY_SIZE(msgs) ? -EIO : 0);
}

/**
 * i2c2apb_i2c_write - write data to a register of the i2c slave device.
 *
 * @client: i2c device.
 * @inst: instruction
 * @buf: raw data buffer to write.
 * @len: length of the buffer to write
 * Returns negative errno, 0 for success
 */
static int i2c2apb_i2c_write(struct i2c_client *client, unsigned int inst,
			     const u8 *buf, unsigned len)
{
	u8 *addr_buf;
	struct i2c_msg msg;
	int ret;

	pr_debug("apbaddr 0x%x len %d", inst, len);
	if (len % IxC2APB_OPS_UNIT) {
		pr_err("write len %d err, must be %d B align\n",
		       len, IxC2APB_OPS_UNIT);
		return -EINVAL;
	}

	if (len + IxC2APB_OPS_UNIT > MAX_QCON_BUFFER_SIZE) {
		pr_err("write len exceed max buffer size\n");
		return -EINVAL;
	}

	addr_buf = kmalloc(len + IxC2APB_OPS_UNIT, GFP_KERNEL);
	if (!addr_buf)
		return -ENOMEM;

	memcpy(&addr_buf[0], (char *)&inst, sizeof(int));
	if (len) {
		memcpy(&addr_buf[IxC2APB_OPS_UNIT], buf, len);
	}

	msg.flags = 0;
	msg.addr = client->addr;
	msg.buf = addr_buf;
	msg.len = (uint16_t)len + IxC2APB_OPS_UNIT;
	ret = i2c_transfer(client->adapter, &msg, 1);

	kfree(addr_buf);

	return ret < 0 ? ret : (ret != 1 ? -EIO : 0);
}

static inline int i2c2apb_i2c_read_csr(struct i2c_client *client, int *csr)
{
	int ret = -EIO;
	struct i2c2apb_dev *i2c2apb = i2c_get_clientdata(client);

	ret = i2c2apb_i2c_wrrd(client, RS_INSTR, (u8 *)csr, sizeof(int));

	pr_debug(" RS_INSTR = 0x%x\n", RS_INSTR);
	pr_debug("PAGE 0x%x:\n", GET_CSR_PAGE(*csr));
	pr_debug("FIFO_LEVEL 0x%x:\n", GET_CSR_FIFO_LEVEL(*csr));
	pr_debug("CONFLICT 0x%x:\n", GET_CSR_CONFLICT(*csr));
	pr_debug("SLVERR 0x%x:\n", GET_CSR_SLVERR(*csr));
	pr_debug("FIFO_UNDERRUN 0x%x:\n", GET_CSR_FIFO_UNDERRUN(*csr));
	pr_debug("FIFO_OVERRUN 0x%x:\n", GET_CSR_FIFO_OVERRUN(*csr));
	pr_debug("csr context: 0x%x\n", *csr);

	i2c2apb->csr = *csr;

	return ret;
}

static inline int i2c2apb_i2c_write_csr(struct i2c_client *client, int *csr)
{
	pr_debug("write csr 0x%x\n", *csr);
	pr_debug(" WS_INSTR = 0x%x\n", WS_INSTR);

	return i2c2apb_i2c_write(client, WS_INSTR, (const u8 *)csr, sizeof(int));
}

/**
 * i2c2apb_sync_page_addr - sync device's page address
 *
 * @client: i2c device.
 * @addr: address to visit
 * Returns negative errno, 0 for success
 */
static int i2c2apb_sync_page_addr(struct i2c_client *client, int addr)
{
	int ret = 0;
	int csr;
	ret = i2c2apb_i2c_read_csr(client, &csr);
	if (ret) {
		pr_err(" failed ret: 0x%x\n", ret);
		return ret;
	}

	csr &= ~(GET_MSK_FROM_WID(CSR_PAGE_WIDTH) << CSR_PAGE_OFFSET);
	csr |= addr & (GET_MSK_FROM_WID(CSR_PAGE_WIDTH) << CSR_PAGE_OFFSET);
	ret = i2c2apb_i2c_write_csr(client, &csr);
	if (ret) {
		pr_err(" failed ret: 0x%x\n", ret);
		return ret;
	}
	return ret;
}

static inline int csr_status_check(struct i2c_client *client, int *csr)
{
	int ret = 0;

	if (GET_CSR_CONFLICT(*csr)) {
		pr_err("i3c2apb occur conflict error\n");
		pr_err("CONFLICT 0x%x:\n", GET_CSR_CONFLICT(*csr));
		ret = -EINVAL;
	}

	if (GET_CSR_SLVERR(*csr)) {
		pr_err("i3c2apb occur slave error\n");
		pr_err("SLVERR 0x%x:\n", GET_CSR_SLVERR(*csr));
		ret = -EINVAL;
	}

	if (GET_CSR_FIFO_UNDERRUN(*csr)) {
		pr_err("i3c2apb occur underflow error\n");
		pr_err("FIFO_UNDERRUN 0x%x:\n", GET_CSR_FIFO_UNDERRUN(*csr));
		ret = -EINVAL;
	}

	if (GET_CSR_FIFO_OVERRUN(*csr)) {
		pr_err("i3c2apb occur overflow error\n");
		pr_err("FIFO_OVERRUN 0x%x:\n", GET_CSR_FIFO_OVERRUN(*csr));
		ret = -EINVAL;
	}

	if (ret != 0) {
		*csr = CSR_INIT_VAL;
		ret = i2c2apb_i2c_write_csr(client, csr);
		if (ret) {
			pr_err("clr_stat err %x\n", ret);
			ret = -EAGAIN;
		}
	}

	return ret;
}

static inline int i2c2apb_i2c_read_data(struct i2c_client *client,
					struct ixc2apb_msg *msg, u8 *buf)
{
	int ret = 0;
	int csr = 0;

	pr_debug(" reg: 0x%x, aligned 0x%x, l 0x%x, m 0x%x, h 0x%x, page 0x%x\n",
		 msg->offset, msg->offset >> 2, (msg->offset >> 2) & 0xFF,
		 (msg->offset >> 10) & 0xFF, (msg->offset >> 18) & 0xFF,
		 (msg->offset >> 26) & 0xFF);
	pr_debug(" RR_INSTR = 0x%x\n", RR_INSTR(msg->offset, msg->len));
	pr_debug(" RD_INSTR = 0x%x\n", RD_INSTR);

	ret = i2c2apb_sync_page_addr(client, msg->offset);
	if (ret) {
		pr_err(" failed set sync_page_addr. ret: 0x%x\n", ret);
		return ret;
	}

	ret = i2c2apb_i2c_write(client, RR_INSTR(msg->offset, msg->len), 0, 0);
	if (ret) {
		pr_err(" failed send RR. ret: 0x%x\n", ret);
		return ret;
	}

	ret = i2c2apb_i2c_wrrd(client, RD_INSTR, buf, msg->len);
	if (ret) {
		pr_err(" failed send RD. ret: 0x%x\n", ret);
		return ret;
	}

	ret = i2c2apb_i2c_read_csr(client, &csr);
	if (ret) {
		pr_err(" failed read csr. ret: 0x%x\n", ret);
		return ret;
	}

	ret = csr_status_check(client, &csr);
	if (ret) {
		pr_err("csr register occur exception\n");
		return ret;
	}
	return ret;
}

static inline int i2c2apb_i2c_write_data(struct i2c_client *client,
					 struct ixc2apb_msg *msg, u8 *buf)
{
	int ret = 0;
	int csr;

	ret = i2c2apb_sync_page_addr(client, msg->offset);
	if (ret) {
		pr_err(" failed ret: 0x%x\n", ret);
		return ret;
	}

	csr = WD_INSTR(msg->offset);
	pr_debug(" WD_INSTR = 0x%x\n", csr);
	ret = i2c2apb_i2c_write(client, csr, buf, msg->len);
	if (ret) {
		pr_err(" failed ret: 0x%x\n", ret);
		return ret;
	}

	ret = i2c2apb_i2c_read_csr(client, &csr);
	if (ret) {
		pr_err(" failed read csr. ret: 0x%x\n", ret);
		return ret;
	}

	ret = csr_status_check(client, &csr);
	if (ret) {
		pr_err("csr register occur exception\n");
		return ret;
	}

	return ret;
}

int i2c2apb_open(struct inode *inode, struct file *filp)
{
	struct i2c2apb_dev *i2c2apb = container_of(filp->private_data,
						   struct i2c2apb_dev, miscdev);
	int ret = 0;
	if (!i2c2apb)
		return -ENODEV;

	filp->private_data = i2c2apb;
	mutex_lock(&i2c2apb->op_mutex);

	i2c2apb->opencnt++;

	mutex_unlock(&i2c2apb->op_mutex);
	return ret;
}

int i2c2apb_close(struct inode *inode, struct file *filp)
{
	struct i2c2apb_dev *i2c2apb = container_of(filp->private_data,
						   struct i2c2apb_dev, miscdev);

	if (!i2c2apb)
		return -ENODEV;

	mutex_lock(&i2c2apb->op_mutex);

	if (i2c2apb->opencnt > 0)
		i2c2apb->opencnt--;

	mutex_unlock(&i2c2apb->op_mutex);

	filp->private_data = NULL;

	return 0;
}

/* borrow from i2c-dev.c */
static noinline int i2cdev_ioctl_rdwr(struct i2c_client *client,
				      unsigned nmsgs, struct i2c_msg *msgs)
{
	u8 __user **data_ptrs;
	int i, res;

	data_ptrs = kmalloc_array(nmsgs, sizeof(u8 __user *), GFP_KERNEL);
	if (data_ptrs == NULL) {
		kfree(msgs);
		return -ENOMEM;
	}

	res = 0;
	for (i = 0; i < nmsgs; i++) {
		/* Limit the size of the message to a sane amount */
		if (msgs[i].len > 8192) {
			res = -EINVAL;
			break;
		}

		data_ptrs[i] = (u8 __user *)msgs[i].buf;
		msgs[i].buf = memdup_user(data_ptrs[i], msgs[i].len);
		if (IS_ERR(msgs[i].buf)) {
			res = PTR_ERR(msgs[i].buf);
			break;
		}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 86))
		/* memdup_user allocates with GFP_KERNEL, so DMA is ok */
		msgs[i].flags |= I2C_M_DMA_SAFE;
#endif

		/*
		 * If the message length is received from the slave (similar
		 * to SMBus block read), we must ensure that the buffer will
		 * be large enough to cope with a message length of
		 * I2C_SMBUS_BLOCK_MAX as this is the maximum underlying bus
		 * drivers allow. The first byte in the buffer must be
		 * pre-filled with the number of extra bytes, which must be
		 * at least one to hold the message length, but can be
		 * greater (for example to account for a checksum byte at
		 * the end of the message.)
		 */
		if (msgs[i].flags & I2C_M_RECV_LEN) {
			if (!(msgs[i].flags & I2C_M_RD) ||
			    msgs[i].len < 1 || msgs[i].buf[0] < 1 ||
			    msgs[i].len < msgs[i].buf[0] +
			    I2C_SMBUS_BLOCK_MAX) {
				i++;
				res = -EINVAL;
				break;
			}

			msgs[i].len = msgs[i].buf[0];
		}
	}
	if (res < 0) {
		int j;
		for (j = 0; j < i; ++j)
			kfree(msgs[j].buf);
		kfree(data_ptrs);
		kfree(msgs);
		return res;
	}

	res = i2c_transfer(client->adapter, msgs, nmsgs);
	while (i-- > 0) {
		if (res >= 0 && (msgs[i].flags & I2C_M_RD)) {
			if (copy_to_user(data_ptrs[i], msgs[i].buf,
					 msgs[i].len))
				res = -EFAULT;
		}
		kfree(msgs[i].buf);
	}
	kfree(data_ptrs);
	kfree(msgs);
	return res;
}

/**
 *  @brief   IOCTL function  to be used to set or get data from upper layer.
 *
 *  @param   pfile  fil node for opened device.
 *  @cmd     IOCTL type from upper layer.
 *  @arg     IOCTL arg from upper layer.
 *
 *  @return 0 on success, error code for failures.
 */
long i2c2apb_ioctl(struct file *pfile, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	void __user *argp = (void __user *)arg;
	struct i2c2apb_dev *i2c2apb = pfile->private_data;
	struct ixc2apb_msg msg;
	u8 *iobuf = NULL;

	if (!i2c2apb)
		return -ENODEV;

	if (copy_from_user(&msg, argp, sizeof(msg))) {
		return -EFAULT;
	}

	pr_debug("\nmsg : \n");
	pr_debug("offset : 0x%x\n", msg.offset);
	pr_debug("len : %d bytes\n", msg.len);
	pr_debug("usr : 0x%x\n", msg.usr);
	pr_debug("flag : 0x%x\n", msg.flag);

	switch (cmd) {
	case IxC2APB_SET_PWR:
		pr_debug("not support yet\n");
		break;

	case IxC2APB_GET_CMD:
		pr_debug("\n");

		msg.offset = MAX_BUFFER_SIZE / IxC2APB_OPS_UNIT;
		msg.len = FIFO_DEPTH;

		if (copy_to_user((void __user *)argp, &msg,
				 sizeof(struct ixc2apb_msg))) {
			pr_warn(" to userspace err\n");
			ret = -EFAULT;
		}

		break;

	case IxC2APB_SET_DATA:
		pr_debug("set data : addr %x \n", msg.offset);

		if (msg.len > MAX_BUFFER_SIZE || msg.len <= 0) {
			ret = -EINVAL;
			break;
		}

		iobuf = memdup_user(msg.usr, msg.len);
		if (IS_ERR(iobuf)) {
			ret = PTR_ERR(iobuf);
			break;
		}

		ret = i2c2apb_i2c_write_data(i2c2apb->client, &msg, iobuf);
		if (ret) {
			pr_err("set_data err %x\n", ret);
			ret = -EAGAIN;
		}

		kfree(iobuf);
		iobuf = NULL;

		break;

	case IxC2APB_GET_DATA:
		pr_debug("get data : addr %x, \n", msg.offset);

		if (msg.len > FIFO_DEPTH * IxC2APB_OPS_UNIT || msg.len <= 0) {
			ret = -EINVAL;
			break;
		}
		iobuf = kzalloc(msg.len, GFP_KERNEL);
		if (!iobuf) {
			pr_err(" malloc iobuf failed\n");
			ret = -ENOMEM;
			break;
		}

		ret = i2c2apb_i2c_read_data(i2c2apb->client, &msg, iobuf);
		if (ret) {
			pr_err(" get_data err %x\n", ret);
			ret = -EAGAIN;
		}

		if (copy_to_user((void __user *)msg.usr, iobuf, msg.len)) {
			pr_warn(" from userspace err\n");
			ret = -EFAULT;
		}
		kfree(iobuf);
		iobuf = NULL;

		break;

	case IxC2APB_SET_STAT:
		pr_debug("set stat 0x%x\n", msg.offset);

		ret = i2c2apb_i2c_write_csr(i2c2apb->client,
					    &msg.offset);
		if (ret) {
			pr_err(" set_stat err %x\n", ret);
			ret = -EAGAIN;
		}

		break;

	case IxC2APB_GET_STAT:
		pr_debug("get stat\n");

		ret = i2c2apb_i2c_read_csr(i2c2apb->client, &msg.offset);
		if (ret) {
			pr_err(" get_stat err %x\n", ret);
			ret = -EAGAIN;
			break;
		}

		if (copy_to_user((void __user *)argp, &msg,
				 sizeof(struct ixc2apb_msg))) {
			pr_warn(" to userspace err\n");
			ret = -EFAULT;
		}

		break;

	case IxC2APB_CLR_STAT:
		pr_debug("get stat\n");

		msg.offset = CSR_INIT_VAL;
		ret = i2c2apb_i2c_write_csr(i2c2apb->client, &msg.offset);
		if (ret) {
			pr_err(" clr_stat err %x\n", ret);
			ret = -EAGAIN;
		}

		break;

	/* borrow from i2c-dev.c */
	case IxC2APB_I2C_MSG_RW: {
		struct i2c2apb_msg msgs;
		struct i2c_msg *i2cmsg = NULL;

		if (copy_from_user(&msgs, argp, sizeof(msgs))) {
			ret = -EFAULT;
			break;
		}

		if (msgs.num > I2C_RDWR_IOCTL_MAX_MSGS)
			return -EINVAL;

		i2cmsg = memdup_user(msgs.msg, msgs.num * sizeof(i2cmsg));
		if (IS_ERR(i2cmsg)) {
			pr_err(" malloc i2cmsg failed\n");
			ret = PTR_ERR(i2cmsg);
			break;
		};

		ret = i2cdev_ioctl_rdwr(i2c2apb->client, msgs.num, i2cmsg);

		break;

	}
	default:
		pr_err(" bad cmd %lu\n", arg);
		ret = -ENOIOCTLCMD;
		break;
	}
	return ret;
}

static const struct file_operations i2c2apb_fops = {
	.owner = THIS_MODULE,
	.open = i2c2apb_open,
	.release = i2c2apb_close,
	.unlocked_ioctl = i2c2apb_ioctl,
};

static int i2c2apb_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int ret = 0;
	struct i2c2apb_dev *data = NULL;
	dev_dbg(&client->dev, "I2C Address: 0x%02x\n", client->addr);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "I2C check functionality failed.\n");
		ret = -ENXIO;
		goto err;
	}

	data = (struct i2c2apb_dev *)devm_kzalloc(&client->dev,
						  sizeof(struct i2c2apb_dev),
						  GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto err;
	}

	mutex_init(&data->op_mutex);

	data->miscdev.minor = MISC_DYNAMIC_MINOR;
	data->miscdev.name = I2C2APB_DEV_NAME;
	data->miscdev.fops = &i2c2apb_fops;
	ret = misc_register(&data->miscdev);
	if (ret) {
		dev_err(&client->dev, "Unable to register device\n");
		goto destroy;
	}

	data->opencnt = 0;
	data->client = client;
	i2c_set_clientdata(client, data);
	data->csr = 0;

	pr_info(" success\n");

	return 0;
destroy:
	mutex_destroy(&data->op_mutex);
	devm_kfree(&client->dev, data);
err:
	pr_err(" failed\n");
	return ret;
}

static int i2c2apb_remove(struct i2c_client *client)
{
	struct i2c2apb_dev *i2c2apb = i2c_get_clientdata(client);

	dev_err(&client->dev, "i2c dev 0x%02x removed\n", client->addr);

	if (!i2c2apb) {
		pr_err(" device doesn't exist anymore\n");
		return -ENODEV;
	}

	misc_deregister(&i2c2apb->miscdev);

	mutex_destroy(&i2c2apb->op_mutex);

	devm_kfree(&client->dev, i2c2apb);

	return 0;
}

static int i2c2apb_suspend(struct device *dev)
{
	return 0;
}

static int i2c2apb_resume(struct device *dev)
{
	return 0;
}

static SIMPLE_DEV_PM_OPS(i2c2apb_pm_ops, i2c2apb_suspend, i2c2apb_resume);

static const struct i2c_device_id i2c2apb_id[] = {
	{ "tetras,i2c2apb", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, i2c2apb_id);

#ifdef CONFIG_OF
static const struct of_device_id i2c2apb_of_match[] = {
	{ .compatible = "tetras,i2c2apb" },
	{ }
};
MODULE_DEVICE_TABLE(of, i2c2apb_of_match);
#endif

static struct i2c_driver i2c2apb_driver = {
	.probe = i2c2apb_probe,
	.remove = i2c2apb_remove,
	.id_table = i2c2apb_id,
	.driver = {
		.name = "i2c2apb",
		.of_match_table = of_match_ptr(i2c2apb_of_match),
		.pm = &i2c2apb_pm_ops,
	},
};
module_i2c_driver(i2c2apb_driver);

MODULE_AUTHOR("");
MODULE_DESCRIPTION("TETRAS I2C2APB driver");
MODULE_LICENSE("GPL v2");
