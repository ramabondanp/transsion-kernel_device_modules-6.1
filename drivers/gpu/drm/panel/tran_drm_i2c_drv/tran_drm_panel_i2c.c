// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Transsion Inc.
 */

#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
//#include "mtk_drm_panel_common.h"
#include "tran_drm_panel_i2c.h"

/* i2c control start */
#define TRAN_PANEL_I2C_ID_NAME "I2C_LCD_BIAS"
static struct i2c_client *tran_panel_i2c_client;
uint8_t g_shutdown_flag = 0;
EXPORT_SYMBOL(g_shutdown_flag);

struct mtk_panel_i2c_dev {
	struct i2c_client *client;
};

static ssize_t shutdown_flag_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	char buff[2]= {'0','\0'};

	if (count == 0 || count > 2) {
		pr_err("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}
	ret = copy_from_user(buff, buf, 1);
	if (ret) {
		pr_err("copy_from_user fail.\n");
		return -EPERM;
	}

	ret = sscanf(buff, "%d", &tmp);
	if (ret != 1) {
		pr_err("Invalid value! ret = %d", ret);
		ret = -EINVAL;
		goto out;
	}

	g_shutdown_flag = tmp;
	pr_info("g_shutdown_flag = %d\n", g_shutdown_flag);

	ret = count;

out:
	return ret;
}
static ssize_t shutdown_flag_read(struct file *file, char __user *buff, size_t count, loff_t *offp)
{
	static int finished = 0;
	int32_t len = 0;
	char *page = NULL;
	char *ptr = NULL;

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		pr_info("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	page = kmalloc(PAGE_SIZE,GFP_KERNEL);
	if (!page) {
		return -ENOMEM;
	}

	ptr = page;
	ptr += sprintf(page, "%d\n", g_shutdown_flag);
	len = ptr -page;
	if (*offp >= len) {
		kfree(page);
		return 0;
	}
	if (copy_to_user(buff,(char *)page,len))
	{
		kfree(page);
		return len;
	}
	*offp += len;
	kfree(page);
	pr_info("--\n");
	return len;
}

static const struct proc_ops shutdown_flag_fops = {
	.proc_read   = shutdown_flag_read,
	.proc_write  = shutdown_flag_write,
};

int tran_panel_i2c_write_bytes(unsigned char addr, unsigned char value)
{
	int ret = 0;
	struct i2c_client *client = tran_panel_i2c_client;
	char write_data[2] = { 0 };

	if (client == NULL) {
		pr_err("%s: ERROR!! client is null\n", __func__);
		return 0;
	}

	write_data[0] = addr;
	write_data[1] = value;
	ret = i2c_master_send(client, write_data, 2);
	if (ret < 0) {
		pr_err("%s: ERROR write 0x%x with 0x%x fail %d\n",
			__func__, addr, value, ret);
	}

	return ret;
}
EXPORT_SYMBOL(tran_panel_i2c_write_bytes);

int tran_panel_i2c_read_bytes(unsigned char addr, unsigned char *returnData)
{
	char cmd_buf[2] = { 0x00, 0x00 };
	int ret = 0;
	struct i2c_client *client = tran_panel_i2c_client;

	if (client == NULL) {
		pr_err("%s: ERROR!! tran_panel_i2c_client is null\n", __func__);
		return 0;
	}

	cmd_buf[0] = addr;
	ret = i2c_master_send(client, &cmd_buf[0], 1);
	ret = i2c_master_recv(client, &cmd_buf[1], 1);
	if (ret < 0) {
		pr_err("%s: ERROR read 0x%x fail %d\n",
			__func__, addr, ret);
	}
	*returnData = cmd_buf[1];

	return ret;
}
EXPORT_SYMBOL(tran_panel_i2c_read_bytes);

static int tran_panel_i2c_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct proc_dir_entry *proc_entry = NULL;

	pr_info("%s: name=%s addr=0x%x\n", __func__, client->name,
		 client->addr);

	proc_entry = proc_create("shutdown_flag", 0666, NULL, &shutdown_flag_fops);
	if (proc_entry == NULL) {
		pr_err("Create proc shutdown_flag node fail \n");
	} else {
		pr_info("Create proc shutdown_flag node success \n");
	}
	tran_panel_i2c_client = client;
	return 0;
}

static void tran_panel_i2c_remove(struct i2c_client *client)
{
	pr_info("%s: name=%s addr=0x%x\n", __func__, client->name,
		 client->addr);
	tran_panel_i2c_client = NULL;
	i2c_unregister_device(client);
	//return 0;
}

static const struct of_device_id tran_panel_i2c_of_match[] = {
	{.compatible = "mediatek,i2c_lcd_bias"},
	{},
};

static const struct i2c_device_id mtk_panel_i2c_id[] = {
	{TRAN_PANEL_I2C_ID_NAME, 0},
	{},
};

struct i2c_driver tran_drm_i2c_driver = {
	.id_table = mtk_panel_i2c_id,
	.probe = tran_panel_i2c_probe,
	.remove = tran_panel_i2c_remove,
	/* .detect         = mtk_panel_i2c_detect, */
	.driver = {
		.owner = THIS_MODULE,
		.name = TRAN_PANEL_I2C_ID_NAME,
		.of_match_table = tran_panel_i2c_of_match,
	},
};

static int __init tran_drm_i2c_init(void)
{
	int ret = 0;
	pr_info("%s, add i2c driver\n", __func__);

	ret = i2c_add_driver(&tran_drm_i2c_driver);
	if (ret < 0) {
		pr_err("%s, failed to register i2c driver: %d\n",
			  __func__, ret);
		return ret;
	}
	return 0;
}

static void __exit tran_drm_i2c_exit(void)
{
	i2c_del_driver(&tran_drm_i2c_driver);
}

module_init(tran_drm_i2c_init);
module_exit(tran_drm_i2c_exit);

MODULE_AUTHOR("Transsion Inc.");
MODULE_DESCRIPTION("mediatek, panel i2c driver");
MODULE_LICENSE("GPL v2");
