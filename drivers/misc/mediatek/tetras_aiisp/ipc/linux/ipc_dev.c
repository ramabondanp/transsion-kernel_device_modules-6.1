/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Change Logs:
 * Date           Author           Notes
 * 2022-2-24      Zhu Shiqiang     Initialize.
 */

/**
 * @brief   ipc driver
 * @date    2021-12-24
 */

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include "ipc_printk.h"
#include "ipc_config.h"
#include "ipcm.h"
#include "ipc_dev.h"
#include "ipc_io.h"
#include "ai_isp_pmctrl.h"

static struct miscdevice ipc_miscdev;
uint32_t bridge_mode = IPC_BRIDGE_SPI;

#define IPC_RECV_TIMEOUT 5000
static struct ipc_vdev *recv_vdev;
static struct ipc_vdev *send_vdev;

static const char bridge_name[IPC_BRIDGE_MAX][8] = {
	"spi",
	"sdio",
};

static int ipc_dev_wait_ack(struct ipc_vdev *vdev, int timeout)
{
	int ret;
	struct ipc_dev *ipcdev = vdev->dev;

	if (timeout == IPCDEV_ASYNC)
		return 0;

	ret = wait_event_interruptible_timeout(vdev->read_queue,
					       ipcdev->ack_flag,
					       msecs_to_jiffies(timeout)
					      );
	if (ret == 0) {
		ipc_err("wait ack timeout: %d ms!!\n", timeout);
		ret = -ETIME;
		goto out;
	} else if (ret == -ERESTARTSYS) {
		ipc_err("wait ack interrupted by a signal!\n");
		goto out;
	}

	ipcdev->ack_flag = false;
	ipc_release(vdev->chan);

	return 0;
out:
	ipcdev->ack_flag = false;
	return ret;
}

struct ipc_vdev *ipc_dev_open(struct ipc_dev *dev, struct user_config *config)
{
	int ret;
	struct ipc_vdev *vdev = NULL;
	struct ipc_channel *chan = NULL;
	struct chan_config *vdev_chan_cfg = NULL;
	struct ipc_dev *ipcdev = dev;
	uint32_t chan_id = 0;

	if (!ipcdev && ipc_miscdev.this_device) {
		ipcdev = dev_get_drvdata(ipc_miscdev.this_device);
	} else {
		ipc_err("parameters error!\n");
		return NULL;
	}

	if (!config) {
		ipc_err("config val is null\n");
		return NULL;
	}

	vdev = kzalloc(sizeof(struct ipc_vdev), GFP_KERNEL);
	if (!vdev) {
		ipc_err("failed to alloc memory\n");
		return NULL;
	}

	/* TODO: */
	vdev->chan = kzalloc(sizeof(struct ipc_channel), GFP_KERNEL);
	if (!vdev->chan) {
		ipc_err("failed to alloc memory\n");
		goto alloc_chan_err;
	}

	chan_id = ipc_find_chan_by_name(ipcdev->dev_cfg, config->name);
	if (chan_id >= IPC_CHANID_MAX) {
		ipc_err("not support chan id:%s\n", config->name);
		goto chan_id_err;
	}

	chan = vdev->chan;
	vdev_chan_cfg = &ipcdev->dev_cfg[chan_id];

	/* bind vdev and mboxid */
	ipcdev->vdev[vdev_chan_cfg->mboxid] = vdev;

	init_waitqueue_head(&vdev->read_queue);
	vdev->recv_flag = false;

	chan->chan_cfg = vdev_chan_cfg;
	chan->mst = IPC_MASTER_AP;
	ret = ipc_chan_open(chan);
	if (ret) {
		ipc_err("ipc chan open err: %d\n", ret);
		goto chan_id_err;
	}

	vdev->dev = ipcdev;
	ipc_debug("vdev mboxd: %d chan adr: %p, chan name: %s\n",
		  vdev_chan_cfg->mboxid, vdev->chan, config->name);

	return vdev;

chan_id_err:
	kfree(vdev->chan);
alloc_chan_err:
	kfree(vdev);
	return NULL;
}
EXPORT_SYMBOL(ipc_dev_open);

int ipc_dev_close(struct ipc_vdev *vdev)
{
	if (!vdev || !vdev->chan) {
		ipc_err("vdev or ipc chan is null\n");
		return -EINVAL;
	}
	ipc_chan_close(vdev->chan);

	kfree(vdev->chan);
	kfree(vdev);

	return 0;
}
EXPORT_SYMBOL(ipc_dev_close);

int ipc_dev_send(struct ipc_vdev *vdev, struct ipc_send_msg *msg)
{
	int ret = 0;
	struct ipc_dev *ipcdev = NULL;
	struct ipc_channel *chan = NULL;

	if (!vdev || !msg) {
		ipc_err("vdev or msg is null\n");
		return -EINVAL;
	}
	ipcdev = vdev->dev;
	chan = vdev->chan;

	if (!(msg->buf)) {
		ipc_err("msg buf is null\n");
		return -EINVAL;
	}

	mutex_lock(&ipcdev->send_lock);

	ret = ipc_send(chan, msg->buf, msg->size);
	if (ret) {
		ipc_err("ipc send error\n");
		goto send_err;
	}

	ret = ipc_dev_wait_ack(vdev, msg->timeout_ms);
	if (ret) {
		ipc_err("ipc wait ack error\n");
		goto wait_err;
	}
	mutex_unlock(&ipcdev->send_lock);

	return ret;

wait_err:
send_err:
	mutex_unlock(&ipcdev->send_lock);

	return ret;
}
EXPORT_SYMBOL(ipc_dev_send);

int ipc_dev_recv(struct ipc_vdev *vdev, struct ipc_recv_msg *msg)
{
	int ret = 0;
	struct ipc_dev *ipcdev = NULL;
	struct ipc_channel *chan = NULL;

	if (!vdev || !msg) {
		ipc_err("vdev or msg is null\n");
		return -EINVAL;
	}
	chan = vdev->chan;
	ipcdev = vdev->dev;

	if (!(msg->buf)) {
		ipc_err("msg buf is null\n");
		return -EINVAL;
	}

	mutex_lock(&ipcdev->recv_lock);
	ret = ipc_recv(chan, msg->buf, msg->size);
	if (!ret) {
		mutex_unlock(&ipcdev->recv_lock);
		vdev->recv_flag = false;
		return ret;
	}
	mutex_unlock(&ipcdev->recv_lock);

	ret = wait_event_interruptible_timeout(vdev->read_queue, vdev->recv_flag,
					       msecs_to_jiffies(msg->timeout_ms)
					      );
	if (ret == 0) {
		ipc_err("wait wakeup event timeout!!\n");
		ret = -ETIME;
		goto wait_timeout;
	} else if (ret == -ERESTARTSYS) {
		ipc_err("wait event interrupted by a signal!\n");
		goto wait_timeout;
	}

	mutex_lock(&ipcdev->recv_lock);
	ret = ipc_recv(chan, msg->buf, msg->size);
	if (ret) {
		ipc_err("recv data failed\n");
		goto recv_err;
	}
	vdev->recv_flag = false;
	mutex_unlock(&ipcdev->recv_lock);

	return ret;

recv_err:
	mutex_unlock(&ipcdev->recv_lock);

wait_timeout:
	vdev->recv_flag = false;
	return ret;
}
EXPORT_SYMBOL(ipc_dev_recv);

static int ipcdev_open(struct inode *inode, struct file *file)
{
	struct user_config recv_config;
	struct user_config send_config;

	recv_config.name = "m7_2_qcom";
	send_config.name = "qcom_2_m7";

	if (NULL == send_vdev || NULL == recv_vdev) {
		ipc_err("input null\n");
		return -EINVAL;
	}

	/* recv configuration */
	recv_vdev = ipc_dev_open(NULL, &recv_config);
	if (recv_vdev == NULL) {
		ipc_err("ipc dev open recv failed\n");
		return -EINVAL;
	}

	/* send configuration */
	send_vdev = ipc_dev_open(NULL, &send_config);
	if (send_vdev == NULL) {
		ipc_err("ipc dev open send failed\n");
		ipc_dev_close(recv_vdev);
		return -EINVAL;
	}
	ipc_info("ipc open command triggered\n");

	return 0;
}

static int ipcdev_close(struct inode *inode, struct file *file)
{
	if (NULL != send_vdev)
		ipc_dev_close(send_vdev);
	if (NULL != recv_vdev)
		ipc_dev_close(recv_vdev);
	ipc_info("ipc close command triggered\n");

	return 0;
}

static ssize_t ipcdev_read(struct file *filp, char __user *buf,
			   size_t count, loff_t *unused)
{
	int ret = 0;
	struct ipc_recv_msg recv_msg;

	if (count > MBOX_DR_SIZE) {
		ipc_err("read out of range of mailbox!\n");
		return -EINVAL;
	}

	recv_msg.buf = kzalloc(count, GFP_KERNEL);
	if (NULL == recv_msg.buf) {
		ipc_err("alloc is NULL!\n");
		return -ENOMEM;
	}

	recv_msg.timeout_ms = IPC_RECV_TIMEOUT;

	ret = ipc_dev_recv(recv_vdev, &recv_msg);
	if (ret) {
		ipc_err("ipc_dev_recv fail: %d\n", ret);
		goto out;
	}
	if (copy_to_user(buf, recv_msg.buf, recv_msg.size)) {
		ipc_err("copy to user failed!\n");
		ret = -EFAULT;
	}

out:
	kfree(recv_msg.buf);
	return ret;
}

static ssize_t ipcdev_write(struct file *filp, const char __user *buf,
			    size_t count, loff_t *unused)
{
	int ret = 0;
	struct ipc_send_msg send_msg;

	send_msg.buf = NULL;
	send_msg.buf = kzalloc(count, GFP_KERNEL);
	if (NULL == send_msg.buf) {
		ipc_err("comm write malloc fail!\n");
		return -ENOMEM;
	}
	if (copy_from_user((char *)send_msg.buf, (char *)buf, count)) {
		ipc_err("copy from user failed!\n");
		ret = -EINVAL;
		goto out;
	}
	send_msg.size = count;
	send_msg.timeout_ms = IPC_RECV_TIMEOUT;

	ret = ipc_dev_send(send_vdev, &send_msg);
	ipc_debug("ipc write command triggered %d\n", ret);
out:
	kfree(send_msg.buf);
	return ret;
}

/* TODO: */
static long ipcdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	ipc_info("ipc read command triggered\n");

	return 0;
}

static const struct file_operations ipcdev_fops = {
	.owner = THIS_MODULE,
	.open = ipcdev_open,
	.release = ipcdev_close,
	.read = ipcdev_read,
	.write = ipcdev_write,
	.unlocked_ioctl	= ipcdev_ioctl,
};

static struct miscdevice ipc_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ipc_driver",
	.fops = &ipcdev_fops,
};

static int ipc_vdev_data_recv(struct ipc_vdev *vdev)
{
	struct ipc_dev *ipcdev = vdev->dev;
	int ret = 0;

	ipc_data_store(vdev->chan);

	vdev->recv_flag = true;

	ret = ipc_release_and_irq_clear(vdev->chan, ipcdev->ack_mode);
	if (ret)
		return IRQ_HANDLED;

	wake_up_interruptible(&vdev->read_queue);

	ipc_send_ack(vdev->chan, ipcdev->ack_mode);

	return 0;
}

static irqreturn_t ipc_dev_irq_thread(int irq, void *dev)
{
	int ret, val, index;
	struct ipc_vdev *vdev = NULL;
	struct ipc_dev *ipcdev = (struct ipc_dev *)dev;

	if (!is_chip_power_on())
		return IRQ_HANDLED;

	val = ipc_irq_status();
	ipc_debug("irq status 0x%x irq_cnt:%d\n", val, ipcdev->irq_cnt);
	while (val) {
		index = ffs(val) - 0x1;
		if (index >= IPC_VDEV_MAX) {
			ipc_warn("index(%d) is larger then max(%d)", index, IPC_VDEV_MAX);
			val &= (~(0x1 << index));
			continue;
		}

		vdev = ipcdev->vdev[index];
		if (!vdev) {
			pr_err("[error]: vedv is null, irq status: 0x%x", val);
			return IRQ_NONE;
		}

		ret = ipc_vdev_data_recv(vdev);
		if (ret)
			return IRQ_HANDLED;

		val &= (~(0x1 << index));
	}

	return IRQ_HANDLED;
}

static irqreturn_t ipc_dev_irq_handler(int irq, void *dev_id)
{
	struct ipc_dev *ipcdev = (struct ipc_dev *)dev_id;

	ipcdev->irq_cnt++;

	return IRQ_WAKE_THREAD;
}

static ssize_t ipc_bridge_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;

	len = snprintf(buf, 64, "ipc using %sbridge\n", bridge_name[bridge_mode]);

	return len;
}

static ssize_t ipc_bridge_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t n)
{
	int rc = 0;
	uint32_t val = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc) {
		pr_err("input str invalid %s\n", buf);
		return n;
	}

	if (val >= IPC_BRIDGE_MAX) {
		pr_err("valid input is from %d - %d, you input %d\n",
		       IPC_BRIDGE_INVAL + 1, IPC_BRIDGE_MAX - 1, val);
		return n;
	}

	bridge_mode = val;
	return n;
}

static DEVICE_ATTR_RW(ipc_bridge);

static int ipcdev_probe(struct platform_device *pdev)
{
	int ret = 0;
	uint32_t irq_num = 0;
	struct ipc_dev *ipcdev = NULL;
	struct device *dev = &pdev->dev;
	const struct chan_config init_cfg[] = IPC_CHAN_CONFIGS;

	ipcdev = devm_kzalloc(dev, sizeof(struct ipc_dev), GFP_KERNEL);
	if (!ipcdev) {
		dev_err(dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	ipcdev->dev_cfg = devm_kzalloc(dev, sizeof(init_cfg), GFP_KERNEL);
	if (!ipcdev->dev_cfg) {
		dev_err(dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	memcpy(ipcdev->dev_cfg, init_cfg, sizeof(init_cfg));

	ipcdev->gpio = of_get_named_gpio_flags(dev->of_node, "gpio-ipc", 0, NULL);
	if (ipcdev->gpio < 0) {
		dev_err(dev, "fail to get ipc gpio from dts!\n");
		return -EINVAL;
	}

	ret = gpio_request(ipcdev->gpio, "gpio-ipc");
	if (ret) {
		dev_err(dev, "ipc unable to request gpio [%d]\n", ipcdev->gpio);
		return ret;
	}

	ret = gpio_direction_input(ipcdev->gpio);
	if (ret) {
		dev_err(dev, "ipc set direction for gpio [%d] failed\n", ipcdev->gpio);
		goto gpio_err;
	}

	irq_num = gpio_to_irq(ipcdev->gpio);
	ret = devm_request_threaded_irq(dev, irq_num, ipc_dev_irq_handler,
					ipc_dev_irq_thread,
					IRQF_TRIGGER_FALLING |
					IRQF_ONESHOT,
					"ipc_irq", ipcdev);
	if (ret) {
		dev_err(dev, "ipc failed irq=%d request ret = %d\n", irq_num, ret);
		goto irq_err;
	}

	/* TODO: ack mode should get from dts */
	ipcdev->ack_mode = 0;

	ret = misc_register(&ipc_miscdev);
	if (ret != 0) {
		dev_err(dev, "ipc misc_register failed\n");
		goto misc_err;
	}

	dev_set_drvdata(ipc_miscdev.this_device, ipcdev);

	mutex_init(&ipcdev->send_lock);
	mutex_init(&ipcdev->recv_lock);

	ipcdev->dev = dev;

	platform_set_drvdata(pdev, ipcdev);
	ret = device_create_file(ipc_miscdev.this_device, &dev_attr_ipc_bridge);
	if (ret) {
		dev_err(ipc_miscdev.this_device, "create attr file failed\n");
		goto ipc_err;
	}

	return ret;

ipc_err:
	misc_deregister(&ipc_miscdev);

misc_err:
irq_err:
gpio_err:
	gpio_free(ipcdev->gpio);
	dev_err(dev, "ipc driver probe error %d\n", ret);
	return ret;
}

static int ipcdev_remove(struct platform_device *pdev)
{
	struct ipc_dev *ipcdev = NULL;

	ipcdev = platform_get_drvdata(pdev);

	gpio_free(ipcdev->gpio);

	misc_deregister(&ipc_miscdev);

	/* note: need to release ipc resource */
	dev_info(&pdev->dev, "tetras ipcdev driver remove completed\n");

	return 0;
}

static const struct of_device_id ipc_of_match[] = {
	{.compatible = "tetras,ipcdev", },
	{},
};
MODULE_DEVICE_TABLE(of, ipc_of_match);

static struct platform_driver ipc_driver = {
	.probe     = ipcdev_probe,
	.remove    = ipcdev_remove,
	.driver    = {
		.name = "tetras-ipcdev",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ipc_of_match),
	},
};

static int __init ipcdev_init(void)
{
	int ret;

	ret = platform_driver_register(&ipc_driver);
	if (ret)
		pr_warn("ipc driver not registered\n");

	return ret;
}

static void __exit ipcdev_exit(void)
{
	platform_driver_unregister(&ipc_driver);
}

module_init(ipcdev_init);
module_exit(ipcdev_exit);

MODULE_AUTHOR("zhushiqiang@tetras.ai");
MODULE_DESCRIPTION("IPC DEV Driver");
MODULE_LICENSE("GPL v2");
