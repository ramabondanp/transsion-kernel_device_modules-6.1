// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Transsion Inc.
 */

#include <linux/proc_fs.h>
#include <linux/types.h>
#include <linux/printk.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include "tran_sensor_debug.h"

static struct transsion_sensor_debug_info sns_dbg_info = {0};
static struct tran_sensor_debug_device *tran_sensor_debug_dev;

int tran_sensor_debug_register(struct tran_sensor_debug_device *device)
{
	pr_info("[TRAN_SENSOR_DEBUG]register! ");
	struct tran_sensor_debug_device *dev = device;

	tran_sensor_debug_dev = dev;
	return 0;
}
EXPORT_SYMBOL_GPL(tran_sensor_debug_register);

void tran_sensor_debug_unregister(struct tran_sensor_debug_device *device)
{
	pr_info("[TRAN_SENSOR_DEBUG]unregister!");
}
EXPORT_SYMBOL_GPL(tran_sensor_debug_unregister);

static ssize_t sensor_debug_read(struct file *filp,
		char __user *user_buf, size_t count, loff_t * ppos)
{
	int len, ret = -1;
	char *page = NULL;
	char *ptr = NULL;

	pr_info("[TRAN_SENSOR_DEBUG]read: sensor_type: %d, debug_enable: %d.",
					sns_dbg_info.sensor_type, sns_dbg_info.debug_enable);
	page = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!page) {
		pr_info("[TRAN_SENSOR_DEBUG]page alloc fail!");
		kfree(page);
		return -ENOMEM;
	}
	ptr = page;
	ptr += sprintf(ptr, "sensor_type %d en %d\n", sns_dbg_info.sensor_type, sns_dbg_info.debug_enable);
	len = ptr - page;
	if(*ppos >= len)
	{
		kfree(page);
		return 0;
	}
	ret = copy_to_user(user_buf,(char *)page,len);
	*ppos += len;
	if(ret)
	{
		kfree(page);
		return ret;
	}
	kfree(page);
	return len;
}

static ssize_t sensor_debug_write(struct file *file,
		const char __user *user_buf, size_t count, loff_t *ppos)
{
	int32_t ret;
	uint32_t sensor_type = 0;
	uint32_t debug_enable = 0;
	char kbuf[16] = {0};
	struct tran_sensor_debug_device *dev = tran_sensor_debug_dev;
	size_t buf_size = min(count, sizeof(kbuf));

	if (unlikely(sensor_type >= SENSOR_TYPE_SENSOR_MAX)) {
		pr_err("[TRAN_SENSOR_DEBUG]Invalid value! sensor_type = %d", sensor_type);
		ret = -EINVAL;
		goto out;
	}
	ret = copy_from_user(kbuf, user_buf, buf_size);
	if (ret) {
		pr_err("[TRAN_SENSOR_DEBUG]copy_from_user fail.");
		return -EPERM;
	}
	ret = sscanf(kbuf, "%d %d", &sensor_type, &debug_enable);
	if (ret != 2) {
		pr_err("[TRAN_SENSOR_DEBUG]Invalid value! ret = %d.", ret);
		ret = -EINVAL;
		goto out;
	}
	sns_dbg_info.sensor_type = sensor_type;
	sns_dbg_info.debug_enable = debug_enable;
	pr_err("[TRAN_SENSOR_DEBUG]write: sns_dbg_info data: %d %d.", sns_dbg_info.sensor_type, sns_dbg_info.debug_enable);
	dev->tran_sensor_debug(dev, sensor_type, debug_enable);
	ret = count;
out:
	return ret;
}

static const struct proc_ops sns_debug_fops = {
	.proc_read           = sensor_debug_read,
	.proc_write          = sensor_debug_write,
};

static int __init tran_sensor_debug_init(void)
{
	pr_err("TRAN_SENSOR_DEBUG init start!\n");
	struct proc_dir_entry *sensor_debug = NULL;
	sensor_debug = proc_create("sensor_debug", 0666, NULL, &sns_debug_fops);
	if (sensor_debug == NULL) {
		pr_err("[TRAN_SENSOR_DEBUG]Couldn't create proc entry[sensor_debug]!");
	} else {
		pr_info("[TRAN_SENSOR_DEBUG]Create proc entry[sensor_debug] success!");
	}
	return 0;
}

static void __exit tran_sensor_debug_exit(void)
{
	pr_err("TRAN_SENSOR_DEBUG exit!\n");
}

module_init(tran_sensor_debug_init);
module_exit(tran_sensor_debug_exit);

MODULE_DESCRIPTION("transsion sensor debug");
MODULE_AUTHOR("Transsion Inc.");
MODULE_LICENSE("GPL v2");
