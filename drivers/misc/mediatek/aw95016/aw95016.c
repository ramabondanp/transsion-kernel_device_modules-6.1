// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * aw95016.c   aw95016 gpio and key
 *
 *  Author: awinic
 *
 * Copyright (c) 2021 Shanghai Awinic Technology Co., Ltd. All Rights Reserved
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/workqueue.h>
#include <linux/errno.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/leds.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/dma-mapping.h>
#include <linux/hrtimer.h>
#include <linux/leds.h>
#include <linux/fb.h>
//#include <stddef.h>
#include "aw95016.h"

#define HRTIMER_FRAME	20
#define AW95016_DRIVER_VERSION	"V0.3.0"
#define MAP(port) ((AW95016_PORT_GOURP(port)*8)+AW95016_PORT_SHIFT(port))
#define CHECK_IRQ(mask,shift) ((mask>>shift)&0x1)
#define ENUM_TO_STRING(enumVar) #enumVar

static struct aw95016_gpio_data *aw95016_gpio_map;
static struct aw95016 *aw95016_data;
static bool g_chip_init_complete = false;
static struct pinctrl *aw95016_pinctrl;
static struct pinctrl_state *aw95016_sda;
static struct pinctrl_state *aw95016_scl;
/*********************************************************
 *
 * awxxxx i2c write/read byte
 *
 ********************************************************/
static unsigned int
i2c_write_byte(struct i2c_client *client, unsigned char reg_addr, unsigned char reg_data)
{
	int ret = 0;
	unsigned char wdbuf[2] = {0};

	struct i2c_msg msgs[] = {
		{
			.addr	= client->addr,
			.flags	= 0,
			.len	= 2,
			.buf	= wdbuf,
		},
	};

	mutex_lock(&aw95016_data->bus_lock);
	wdbuf[0] = reg_addr;
	wdbuf[1] = reg_data;

	ret = i2c_transfer(client->adapter, msgs, 1);
	if (ret < 0)
		AW_ERROR("msg %s i2c write error: %d\n", __func__, ret);
	mutex_unlock(&aw95016_data->bus_lock);

	return ret;

}

static unsigned int
i2c_read_byte(struct i2c_client *client, unsigned char reg_addr, unsigned char *val)
{
	int ret = 0;
	unsigned char rdbuf[2] = {0};

	struct i2c_msg msgs[] = {
		{
			.addr	= client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= &reg_addr,
		},
		{
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= 1,
			.buf	= rdbuf,
		},
	};

	mutex_lock(&aw95016_data->bus_lock);
	ret = i2c_transfer(client->adapter, msgs, 2);
	if (ret < 0)
		AW_ERROR("msg %s i2c read error: %d\n", __func__, ret);
	*val = rdbuf[0];
	mutex_unlock(&aw95016_data->bus_lock);

	return ret;
}

/*********************************************************
 *
 * awxxxx i2c write bytes
 *
 ********************************************************/
static int __maybe_unused i2c_write_multi_byte(struct i2c_client *client,
				unsigned char reg_addr,
				unsigned char *buf, unsigned int len)
{
	int ret = 0;
	unsigned char *wdbuf;

	wdbuf = kmalloc(len + 1, GFP_KERNEL);
	if (wdbuf == NULL)
		return -ENOMEM;

	wdbuf[0] = reg_addr;
	memcpy(&wdbuf[1], buf, len);

	ret = i2c_master_send(client, wdbuf, len + 1);
	if (ret < 0)
		AW_ERROR("%s: i2c master send error\n", __func__);

	kfree(wdbuf);

	return ret;
}

static unsigned int __maybe_unused i2c_read_multi_byte(struct i2c_client *client,
					unsigned char reg_addr,
					unsigned char *buf,
					unsigned int len)
{
	int ret = 0;
	unsigned char *rdbuf = NULL;

	struct i2c_msg msgs[] = {
		{
			.addr	= client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= &reg_addr,
		},
		{
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= len,
		},
	};

	rdbuf = kmalloc(len, GFP_KERNEL);
	if (rdbuf == NULL)
		return  -ENOMEM;

	msgs[1].buf = rdbuf;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0)
		AW_ERROR("msg %s i2c read error: %d\n", __func__, ret);
	if (buf != NULL)
		memcpy(buf, rdbuf, len);
	kfree(rdbuf);
	return ret;
}

/* val 1 -> enable 0 - > disable */
static void __maybe_unused
aw95016_enbale_interrupt_by_mask(struct aw95016 *aw95016, unsigned int val)
{
    unsigned char reg_val[2] = {0};

    if (val) {
        reg_val[0] = aw95016_data->interrupt_mask&0xf;
        reg_val[1] = aw95016_data->interrupt_mask >> 8;
    } else {
        /* do nothing */
    }
   //i2c_write_multi_byte(aw95016_data->client, P0_MAK, reg_val, ARRAY_SIZE(reg_val));
   i2c_write_byte(aw95016_data->client, P0_MAK, reg_val[0]);
   i2c_write_byte(aw95016_data->client, P1_MAK, reg_val[1]);
}

/* Must be read single-byte */
static void aw95016_clear_interrupt(struct aw95016 *aw95016)
{
	unsigned char reg_val[2] = {0};

	i2c_read_byte(aw95016_data->client, P0_INPUT, &reg_val[0]);
	i2c_read_byte(aw95016_data->client, P1_INPUT, &reg_val[1]);
}

/* Don't reset in kernel init !*/
static int __maybe_unused aw95016_reset(struct aw95016 *aw95016)
{
	gpio_set_value(aw95016_data->rst_gpio, 1);
	usleep_range(1000, 2000);
	gpio_set_value(aw95016_data->rst_gpio, 0);
	usleep_range(1000, 2000);
	gpio_set_value(aw95016_data->rst_gpio, 1);
	usleep_range(6000, 6500);

    return 0;
}

static char* enum_to_str(u32 enumVar)
{
    switch(enumVar) {
		case P0_INPUT:
            return ENUM_TO_STRING(P0_INPUT);
       case P0_OUTPUT:
            return ENUM_TO_STRING(P0_OUTPUT);
       case P0_DIR:
            return ENUM_TO_STRING(P0_DIR);
       case P0_PEN:
            return ENUM_TO_STRING(P0_PEN);
       case P0_PMD:
            return ENUM_TO_STRING(P0_PMD);
       case P0_DOMD:
            return ENUM_TO_STRING(P0_DOMD);
       case P0_MAK:
            return ENUM_TO_STRING(P0_MAK);
       case P0_INTST:
            return ENUM_TO_STRING(P0_INTST);
        case P1_INPUT:
            return ENUM_TO_STRING(P1_INPUT);
       case P1_OUTPUT:
            return ENUM_TO_STRING(P1_OUTPUT);
       case P1_DIR:
            return ENUM_TO_STRING(P1_DIR);
       case P1_PEN:
            return ENUM_TO_STRING(P1_PEN);
       case P1_PMD:
            return ENUM_TO_STRING(P1_PMD);
       case P1_DOMD:
            return ENUM_TO_STRING(P1_DOMD);
       case P1_MAK:
            return ENUM_TO_STRING(P1_MAK);
       case P1_INTST:
            return ENUM_TO_STRING(P1_INTST);
       default:
            return "unknow";
    }
}

static int aw95016_port_set(unsigned int port, unsigned int cmd, unsigned int val)
{
    unsigned char reg_val = 0;
	unsigned char reg_old = 0;

	mutex_lock(&aw95016_data->port_lock);

	cmd += AW95016_PORT_GOURP(port);
	i2c_read_byte(aw95016_data->client, cmd, &reg_val);
	reg_old = reg_val;

    if(val)
        reg_val |= (0x1 << AW95016_PORT_SHIFT(port));
    else
        reg_val &= ~(0x1 << AW95016_PORT_SHIFT(port));

    AW_DEBUG("set P%d_%d(%s=%d) new:0x%x old:0x%x", AW95016_PORT_GOURP(port), AW95016_PORT_SHIFT(port),
             enum_to_str(cmd), val, reg_val, reg_old);
	i2c_write_byte(aw95016_data->client, cmd, reg_val);

	mutex_unlock(&aw95016_data->port_lock);
    return 0;

}

static int aw95016_port_get(unsigned int port, unsigned int cmd, unsigned int *val)
{
    unsigned char reg_val = 0;

	mutex_lock(&aw95016_data->port_lock);
	cmd += AW95016_PORT_GOURP(port);
	i2c_read_byte(aw95016_data->client, cmd, &reg_val);

    AW_DEBUG("get P%d_%d(%s): 0x%x", AW95016_PORT_GOURP(port), AW95016_PORT_SHIFT(port),
             enum_to_str(cmd), reg_val);
    *val = 0x1&(reg_val >> AW95016_PORT_SHIFT(port));

	mutex_unlock(&aw95016_data->port_lock);
    return 0;
}

static void aw95016_get_interrupt(unsigned int *mask)
{
    unsigned char reg_val[2] = {0};

    i2c_read_byte(aw95016_data->client, P0_INTST, &reg_val[0]);
    i2c_read_byte(aw95016_data->client, P1_INTST, &reg_val[1]);
    
    *mask = reg_val[0] | reg_val[1] << 8;
}

/* gpio irq en set */
int aw95016_irq_enable(unsigned int port, int enable)
{
	int ret = 0;
	struct aw95016_gpio_data *p_data = NULL;
	
	if(g_chip_init_complete && MAP(port) < AW95016_PORT_MAX) {
		p_data = &aw95016_gpio_map[MAP(port)];
	} else {
		AW_ERROR("chip complete false or value error!");
		return -EINVAL;
	}

	mutex_lock(&p_data->state_lock);
	/* value check */
	if(p_data->irq_enabled == enable) {
		AW_DEBUG("double enable ignor");
		ret = 0;
		goto irq_en_err;
	}

    if(p_data->irq_registered == false) {
        AW_DEBUG("irq not registered!");
        ret = -EINVAL;
		goto irq_en_err;
    }

	/* MSAk bit 0 means enable */
	ret = aw95016_port_set(port, P0_MAK, !enable);
	if(ret) {
		AW_ERROR("P%d_%d MAK set failed!", AW95016_PORT_GOURP(port), AW95016_PORT_SHIFT(port));
		goto irq_en_err;
	}

	/* record irq enable state */
	if(!enable) {
		/* irq disable */
		aw95016_data->interrupt_mask |= 0x1 << MAP(port);
		aw95016_data->irq_enabled_count++;
	} else {
		/* irq enable */
		aw95016_data->interrupt_mask &= ~(0x1 << MAP(port));
		aw95016_data->irq_enabled_count--;
	}
	p_data->irq_enabled = enable;
	p_data->mask_shift = MAP(port);

	AW_DEBUG("P%d_%d MAK set 0x%02x", AW95016_PORT_GOURP(port), AW95016_PORT_SHIFT(port), aw95016_data->interrupt_mask);

irq_en_err:
	mutex_unlock(&p_data->state_lock);

    return ret;
}
EXPORT_SYMBOL(aw95016_irq_enable);

/* irq register */
int aw95016_register_irq(unsigned int port, void* call_back) 
{
	int ret = 0;
	struct aw95016_gpio_data *p_data = NULL;

	if(g_chip_init_complete && MAP(port) < AW95016_PORT_MAX) {
		p_data = &aw95016_gpio_map[MAP(port)];
	} else {
		AW_ERROR("chip complete false or value error!");
		return -EINVAL;
	}

	mutex_lock(&p_data->state_lock);
    /* check port */
    if(port < P0_0 || port > P1_7 || call_back == NULL) {
        ret = -EINVAL;
		goto register_irq_err;
    }

    /* request work mem */
	if(p_data->irq_work != NULL || p_data->irq_registered == true) {
		AW_ERROR("irq is registered!");
		ret = -EBUSY;
		goto register_irq_err;
	}

	p_data->irq_work = kzalloc(sizeof(struct work_struct), GFP_KERNEL);
	if(!p_data->irq_work) {
		AW_ERROR("irq work mem request faild!");
		ret = -EINVAL;
		goto register_irq_err;
	}
	
	/* init work */
	INIT_WORK(p_data->irq_work, call_back);
    p_data->irq_registered = true;

register_irq_err:
	mutex_unlock(&p_data->state_lock);
	return ret;
}
EXPORT_SYMBOL(aw95016_register_irq);

int aw95016_unregister_irq(unsigned int port) 
{
	int ret = 0;
	struct aw95016_gpio_data *p_data = NULL;

	if(g_chip_init_complete && MAP(port) < AW95016_PORT_MAX) {
		p_data = &aw95016_gpio_map[MAP(port)];
	} else {
		AW_ERROR("chip complete false or value error!");
		return -EINVAL;
	}

	mutex_lock(&p_data->state_lock);
    /* check port */
    if(port < P0_0 || port > P1_7) {
        ret = -EINVAL;
		goto unregister_irq_err;
    }

    if(p_data->irq_registered == true) {
        ret = -EINVAL;
		goto unregister_irq_err;
    }

	/* disable irq */
	aw95016_irq_enable(port, 0);

    /*  cancel work */
	cancel_work_sync(p_data->irq_work);

	if(p_data->irq_work != NULL) {
		kfree(p_data->irq_work);
		p_data->irq_work = NULL;
	}

    p_data->irq_registered = false;

unregister_irq_err:
	mutex_unlock(&p_data->state_lock);
	return ret;
}
EXPORT_SYMBOL(aw95016_unregister_irq);

/* gpio pull up down set*/
int aw95016_set_pull_mode(unsigned int port, int mode) {
	int ret = 0;
	struct aw95016_gpio_data *p_data = NULL;

	if(g_chip_init_complete && MAP(port) < AW95016_PORT_MAX) {
		p_data = &aw95016_gpio_map[MAP(port)];
	} else {
		AW_ERROR("chip complete false or value error!");
		return -EINVAL;
	}

	mutex_lock(&p_data->state_lock);
	ret = aw95016_port_set(port, P0_PMD, mode);
	if(ret) {
		AW_ERROR("P%d_%d PMD set failed!", AW95016_PORT_GOURP(port), AW95016_PORT_SHIFT(port));
		goto pull_mode_err;
	}

	p_data->pull_state = mode;

pull_mode_err:
	mutex_unlock(&p_data->state_lock);
	return ret;
}
EXPORT_SYMBOL(aw95016_set_pull_mode);

/* gpio pull en set*/
int aw95016_pull_enable(unsigned int port, int enable) {
	int ret = 0;
	struct aw95016_gpio_data *p_data = NULL;

	if(g_chip_init_complete && MAP(port) < AW95016_PORT_MAX) {
		p_data = &aw95016_gpio_map[MAP(port)];
	} else {
		AW_ERROR("chip complete false or value error!");
		return -EINVAL;
	}

	mutex_lock(&p_data->state_lock);
	ret = aw95016_port_set(port, P0_PEN, enable);
	if(ret) {
		AW_ERROR("P%d_%d PEN set failed!", AW95016_PORT_GOURP(port), AW95016_PORT_SHIFT(port));
		goto pull_en_err;
	}

	p_data->pull_en = enable;

pull_en_err:
	mutex_unlock(&p_data->state_lock);
	return ret;
}
EXPORT_SYMBOL(aw95016_pull_enable);

/* gpio dir set */
int aw95016_set_dir(unsigned int port, int mode) 
{
	int ret = 0;
	struct aw95016_gpio_data *p_data = NULL;

	if(g_chip_init_complete && MAP(port) < AW95016_PORT_MAX) {
		p_data = &aw95016_gpio_map[MAP(port)];
	} else {
		AW_ERROR("chip complete false or value error!");
		return -EINVAL;
	}

	mutex_lock(&p_data->state_lock);
	ret = aw95016_port_set(port, P0_DIR, mode);
	if(ret) {
		AW_ERROR("P%d_%d DIR set failed!", AW95016_PORT_GOURP(port), AW95016_PORT_SHIFT(port));
		goto set_dir_err;
	}

	p_data->direction = mode;

set_dir_err:
	mutex_unlock(&p_data->state_lock);
	return ret;
}
EXPORT_SYMBOL(aw95016_set_dir);

/* gpio level set */
int aw95016_set_output(unsigned int port, int level) 
{
	int ret = 0;
	struct aw95016_gpio_data *p_data = NULL;

	if(g_chip_init_complete && MAP(port) < AW95016_PORT_MAX) {
		p_data = &aw95016_gpio_map[MAP(port)];
	} else {
		AW_ERROR("chip complete false or value error!");
		return -EINVAL;
	}

	mutex_lock(&p_data->state_lock);
	/* check mode */
	if(p_data->direction != AW95016_GPIO_OUTPUT) {
		AW_ERROR("set level faild, P%d_%d is not out mode", AW95016_PORT_GOURP(port), AW95016_PORT_SHIFT(port));
		ret = -EINVAL;
		goto output_en_err;
	}
	ret = aw95016_port_set(port, P0_OUTPUT, level);
	if(ret) {
		AW_ERROR("P%d_%d out level set failed!", AW95016_PORT_GOURP(port), AW95016_PORT_SHIFT(port));
		goto output_en_err;
	}

output_en_err:
	mutex_unlock(&p_data->state_lock);
	return ret;
}
EXPORT_SYMBOL(aw95016_set_output);

/* gpio level set */
int aw95016_get_level(unsigned int port, int *level) {
	int ret = 0;
	struct aw95016_gpio_data *p_data = NULL;

	if(g_chip_init_complete && MAP(port) < AW95016_PORT_MAX) {
		p_data = &aw95016_gpio_map[MAP(port)];
	} else {
		AW_ERROR("chip complete false or value error!");
		return -EINVAL;
	}

	mutex_lock(&p_data->state_lock);
	/* check mode */
	if(p_data->direction == AW95016_GPIO_OUTPUT) {
        ret = aw95016_port_get(port, P0_OUTPUT, level);
        if(ret) {
            AW_ERROR("P%d_%d out level set failed!", AW95016_PORT_GOURP(port), AW95016_PORT_SHIFT(port));
            goto get_lvl_err;
        }
    } else if(p_data->direction == AW95016_GPIO_INPUT) {
        ret = aw95016_port_get(port, P0_INPUT, level);
        if(ret) {
            AW_ERROR("P%d_%d out level set failed!", AW95016_PORT_GOURP(port), AW95016_PORT_SHIFT(port));
            goto get_lvl_err;
        }
    }

get_lvl_err:
	mutex_unlock(&p_data->state_lock);
	return ret;
}
EXPORT_SYMBOL(aw95016_get_level);

static int aw95016_parse_dts(struct aw95016 *aw95016)
{
	int ret = 0;
	struct device_node *np = aw95016_data->dev->of_node;

	aw95016_data->irq_gpio = of_get_named_gpio(np, "irq-gpio", 0);
	if (aw95016_data->irq_gpio < 0) {
		AW_ERROR(" %s: get irq gpio failed\r\n", __func__);
		return -EINVAL;
	}
	AW_DEBUG("irq_gpio = %d\r\n", aw95016_data->irq_gpio);

	ret = devm_gpio_request(aw95016_data->dev, aw95016_data->irq_gpio, "aw95016 irq gpio");
	if (ret) {
		AW_ERROR(" %s: devm_gpio_request irq gpio failed\r\n", __func__);
		return -EBUSY;
	}

	gpio_direction_input(aw95016_data->irq_gpio);
	aw95016_data->irq_num = gpio_to_irq(aw95016_data->irq_gpio);
	if (aw95016_data->irq_num < 0) {
		ret = aw95016_data->irq_num;
        AW_ERROR(" %s: gpio to irq failed\r\n", __func__);
		return ret;
	}
	AW_DEBUG("aw95016 irq num=%d\n", aw95016_data->irq_num);

	aw95016_data->rst_gpio = of_get_named_gpio(np, "reset-gpio", 0);

	if ((!gpio_is_valid(aw95016_data->rst_gpio))) {
		AW_ERROR(" dts don't provide reset-gpio\n");
		//return -EINVAL;
	}

	ret = gpio_request(aw95016_data->rst_gpio, "aw95016-reset");
	if (ret) {
		AW_ERROR(" unable to request gpio [%d]\n",
			aw95016_data->rst_gpio);
		//return ret;
	} else {
		ret = gpio_direction_output(aw95016_data->rst_gpio, 1);
		if (ret) {
			//gpio_free(aw95016_data->rst_gpio);
			AW_ERROR(" unable to set direction of gpio[%d]\n", aw95016_data->rst_gpio);
			//return ret;
		}
	}

	aw95016_data->sim_gpio = of_get_named_gpio(np, "sim-gpio", 0);
	if ((!gpio_is_valid(aw95016_data->sim_gpio))) {
		dev_err(aw95016_data->dev, "[aw95016] %s: dts don't provide sim-gpio\n", __func__);
		return -EINVAL;
	}

	ret = gpio_request(aw95016_data->sim_gpio, "aw95016-sim");
	if (ret) {
		dev_err(&aw95016_data->client->dev, "[aw95016] %s: unable to request gpio [%d]\n", __func__,
			aw95016_data->sim_gpio);
	} 

	return ret;
}

/*********************************************************
 *
 * aw95016 reg
 *
 ********************************************************/
static ssize_t aw_rw_reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned char reg_val = 0;
	unsigned char i = 0;
	ssize_t len = 0;
	struct i2c_client *client = aw95016_data->client;

    len += snprintf(buf + len, PAGE_SIZE-len, "******  aw95016 reg dump  ******\n");
	for (i = 0; i <= 0x17; i++) {
		i2c_read_byte(client, i, &reg_val);
		len += snprintf(buf + len, PAGE_SIZE-len, "[0x%02x]=0x%02x\n", i, reg_val);
	}
	i2c_read_byte(client, 0x1A, &reg_val);
	len += snprintf(buf+len, PAGE_SIZE-len, "[0x%02x]=0x%02x\n", 0x1A, reg_val);
	i2c_read_byte(client, 0x60, &reg_val);
	len += snprintf(buf+len, PAGE_SIZE-len, "[0x%02x]=0x%02x\n", 0x60, reg_val);
	i2c_read_byte(client, 0x61, &reg_val);
	len += snprintf(buf+len, PAGE_SIZE-len, "[0x%02x]=0x%02x\n", 0x61, reg_val);

    len += snprintf(buf + len, PAGE_SIZE-len, "******  aw95016 reg dump  ******\n");
	return len;
}

static ssize_t
aw_rw_reg_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	unsigned int databuf[2];
	struct i2c_client *client = aw95016_data->client;

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2)
		i2c_write_byte(client, databuf[0], databuf[1]);

	return len;
}

static ssize_t aw_fw_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    unsigned char val;
	unsigned char cnt;
	ssize_t len = 0;
	struct i2c_client *client = aw95016_data->client;

	for (cnt = 5; cnt > 0; cnt--) {
		i2c_read_byte(client, RESET, &val);
		AW_DEBUG("%s val=0x%x\n", __func__, val);
		if (val == AW95016_ID)
			break;
		mdelay(5);
	}

    len += snprintf(buf + len, PAGE_SIZE-len, "******  aw95016 chip id = 0x%x  ******\n", val);
	return len;
}

static ssize_t aw_fw_version_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	/* do noting */
	return len;
}

static ssize_t aw_port_ctr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	return len;
}

static ssize_t aw_port_ctr_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	/* do noting */
	return len;
}

static DEVICE_ATTR(aw_fw_version, S_IRUGO | S_IWUSR, aw_fw_version_show, aw_fw_version_store);
static DEVICE_ATTR(aw_rw_reg, S_IRUGO | S_IWUSR, aw_rw_reg_show, aw_rw_reg_store);
static DEVICE_ATTR(aw_port_ctr, S_IRUGO | S_IWUSR, aw_port_ctr_show, aw_port_ctr_store);

/* add your attr in here*/
static struct attribute *aw_attributes[] = {
    &dev_attr_aw_fw_version.attr,
    &dev_attr_aw_rw_reg.attr,
    &dev_attr_aw_port_ctr.attr,
    NULL
};

static struct attribute_group aw_attribute_group = {
    .attrs = aw_attributes
};

static int aw_create_sysfs(struct i2c_client *client)
{
	int ret = 0;
	struct device *dev = &(client->dev);

    ret = sysfs_create_group(&dev->kobj, &aw_attribute_group);
    if (ret) {
        AW_ERROR("sysfs_create_group() failed!!");
        sysfs_remove_group(&dev->kobj, &aw_attribute_group);
        return -ENOMEM;
    } else {
        AW_INFO("sysfs_create_group() succeeded!!");
    }

    return ret;
}

/*
int aw_remove_sysfs(struct i2c_client *client)
{
	struct device *dev = &(client->dev);

    sysfs_remove_group(&dev->kobj, &fts_attribute_group);
    return 0;
}
*/

/*********************************************************
 *
 * aw95016 check chipid
 *
 ********************************************************/
static int aw95016_read_chipid(struct i2c_client *client)
{
	unsigned char val;
	unsigned char cnt;

	for (cnt = 5; cnt > 0; cnt--) {
		i2c_read_byte(client, RESET, &val);

		AW_DEBUG("%s val=0x%x\n", __func__, val);
		if (val == AW95016_ID)
			return 0;
		mdelay(5);
	}
	return -EINVAL;
}

irqreturn_t aw95016_irq_func(int irq, void *data)
{
	unsigned int mask = 0;
	int i;
	// disable_irq_nosync(p_key_data->priv->irq_num); is necessary?
    /* check interrupt, deal irq work*/
	aw95016_get_interrupt(&mask);
	//AW_DEBUG("IRQ TRIGGER  mask 0x%x", mask);

	for(i=0; i<AW95016_PORT_MAX; i++) {
		if(CHECK_IRQ(mask,i)) {
			//AW_DEBUG("IRQ TRIGGER %d, mask 0x%x", i, mask);
			if (!work_pending(aw95016_gpio_map[i].irq_work)) {
				queue_work(aw95016_data->irq_wq, aw95016_gpio_map[i].irq_work);
			}
		}
	}

	/* clear aw95016 interrupt */
	aw95016_clear_interrupt(aw95016_data);

	return 0;
}

static void aw95016_gpio_free_all_resource(struct aw95016 *aw95016)
{

}

static int __maybe_unused hw_is_h833c(void)
{
        struct device_node *np;
        const char *cmd_line;
		char *boot_param = "ae10_hw_h833c";
        int ret = 0;

        np = of_find_node_by_path("/chosen");
        if (!np) {
                AW_INFO("Can't get the /chosen");
                return -EIO;
        }

        ret = of_property_read_string(np, "bootargs", &cmd_line);
        if (ret < 0) {
                AW_INFO("Can't get the bootargs");
                return ret;
        }

        if(strstr(cmd_line, boot_param)){
			AW_DEBUG("hw is h833c");
			return 1;
        } else {
			AW_DEBUG("hw is not h833c");
            return 0;
        }
}

static int __maybe_unused get_dtbo_idx(void)
{
        struct device_node *np;
        const char *cmd_line;
		char *boot_param = "androidboot.dtbo_idx=1";
        int ret = 0;

        np = of_find_node_by_path("/chosen");
        if (!np) {
                AW_INFO("Can't get the /chosen");
                return -EIO;
        }

        ret = of_property_read_string(np, "bootargs", &cmd_line);
        if (ret < 0) {
                AW_INFO("Can't get the bootargs");
                return ret;
        }

        if(strstr(cmd_line, boot_param)){
			AW_DEBUG("dtbo_idx = 1");
			return 1;
        } else {
			AW_DEBUG("dtbo_idx = 0");
            return 0;
        }
}

/* i2c10 Pinctrl configuration */
static int __maybe_unused aw95016_pinctrl_init(struct i2c_client *client)
{
	int ret = 0;
	AW_INFO("aw95016_pinctrl_init Start\n");
	aw95016_pinctrl = devm_pinctrl_get(&client->dev);
	if (IS_ERR(aw95016_pinctrl)) {
		AW_ERROR("Failed to get aw95016 pinctrl.\n");
		ret = PTR_ERR(aw95016_pinctrl);
	}

	/* hr03 HWEN pin initialization */
	aw95016_scl = pinctrl_lookup_state(aw95016_pinctrl, "aw95016_scl10");
	if (IS_ERR(aw95016_scl)) {
		AW_ERROR("Failed to init (%s)\n", "aw95016_scl10");
		ret = PTR_ERR(aw95016_scl);
	} else {
		pinctrl_select_state(aw95016_pinctrl, aw95016_scl);
	}

	aw95016_sda = pinctrl_lookup_state(aw95016_pinctrl, "aw95016_sda10");
	if (IS_ERR(aw95016_sda)) {
		AW_ERROR("Failed to init (%s)\n", "aw95016_scl10");
		ret = PTR_ERR(aw95016_sda);
	} else {
		pinctrl_select_state(aw95016_pinctrl, aw95016_sda);
	}

	AW_INFO("aw95016_pinctrl_init End ret=%d\n", ret);
	return ret;
}

/*********************************************************
 *
 * aw95016 driver
 *
 ********************************************************/
static int aw95016_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	int i = 0;

	AW_DEBUG("%s enter, %s\n", __func__, AW95016_DRIVER_VERSION);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "I2C transfer not Supported\n");
		return -EIO;
	}

	aw95016_data = devm_kzalloc(&client->dev, sizeof(struct aw95016), GFP_KERNEL);
	if (!aw95016_data) {
		AW_DEBUG("%s fail!\n", __func__);
		return -ENOMEM;
	}

    aw95016_gpio_map = devm_kzalloc(&client->dev, AW95016_PORT_MAX*sizeof(struct aw95016_gpio_data), GFP_KERNEL);
    if(!aw95016_gpio_map) {
        AW_ERROR("requeset mem aw95016_gpio_map error\n");
        //goto err_free_data;
    } else {
		for(i=0; i<AW95016_PORT_MAX; i++) {
			mutex_init(&aw95016_gpio_map[i].state_lock);
		}
	}

	aw95016_data->dev = &client->dev;
	aw95016_data->client = client;
	aw95016_data->interrupt_mask = 0xffff;
	mutex_init(&aw95016_data->bus_lock);
	mutex_init(&aw95016_data->port_lock);

    /* create multi-threading work queue */
    aw95016_data->irq_wq = create_workqueue("aw95016_wq");
	i2c_set_clientdata(client, aw95016_data);

	ret = aw95016_parse_dts(aw95016_data);
    if(ret != 0) {
        //goto err_free_map;
    }

    /* maybe don't reset ic in kernel init */
    ret = aw95016_reset(aw95016_data);
    if (ret) {
        //goto err_free_mem;
    }

	if (aw95016_read_chipid(client)) {
		AW_ERROR("read_chipid error\n");
		return -EIO;
	}

	ret = devm_request_threaded_irq(aw95016_data->dev, aw95016_data->irq_num, NULL,
					aw95016_irq_func,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"aw95016_irq", NULL);
	if (ret < 0) {
		dev_err(aw95016_data->dev, "[aw95016] %s register irq failed\r\n", __func__);
		//goto err_free_gpio;
	}

	device_init_wakeup(aw95016_data->dev, 1);
	enable_irq_wake(aw95016_data->irq_num);

	/* create debug dev node - reg */
	aw_create_sysfs(client);

	aw95016_data->pm_suspended = false;
	g_chip_init_complete = true;
	AW_INFO("%s success!\n", __func__);
	return 0;

//err_free_gpio:
	//gpio_free(aw95016_data->rst_gpio);
    //gpio_free(aw95016_data->irq_gpio);
//err_free_map:
    //devm_kfree(&client->dev, aw95016_gpio_map);
//err_free_data:
	//devm_kfree(&client->dev, aw95016_data);
	//g_chip_init_complete = false;
	//AW_INFO("%s fail!\n", __func__);
	//return ret;
}

static void aw95016_i2c_remove(struct i2c_client *client)
{
	aw95016_gpio_free_all_resource(aw95016_data);
	devm_kfree(aw95016_data->dev, aw95016_data);
}

static int aw95016_suspend(struct device *dev)
{
	aw95016_data->pm_suspended = true;

	return 0;
}

static int aw95016_resume(struct device *dev)
{
	aw95016_data->pm_suspended = false;

	return 0;
}

static const struct dev_pm_ops aw95016_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(aw95016_suspend, aw95016_resume)
};

static const struct of_device_id aw95016_of_match[] = {
	{ .compatible = "awinic,aw95016",},
	{},
};

static const struct i2c_device_id aw95016_i2c_id[] = {
	{"awinic,aw95016", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, aw95016_i2c_id);

static struct i2c_driver aw95016_i2c_driver = {
	.driver = {
		.name = "aw95016",
		.owner = THIS_MODULE,
		.of_match_table = aw95016_of_match,
		.pm = &aw95016_pm_ops,
	},
	.probe = aw95016_i2c_probe,
	.remove = aw95016_i2c_remove,
	.id_table = aw95016_i2c_id,
};

static int __init aw95016_i2c_init(void)
{
	int ret = 0;

	ret = i2c_add_driver(&aw95016_i2c_driver);
	if (ret) {
		AW_ERROR("fail to add aw95016 device into i2c\n");
		return ret;
	}
	AW_INFO("aw95016_i2c_init");

	return 0;
}
module_init(aw95016_i2c_init);

static void __exit aw95016_i2c_exit(void)
{
	i2c_del_driver(&aw95016_i2c_driver);
}
module_exit(aw95016_i2c_exit);

MODULE_DESCRIPTION("aw95016 driver");
MODULE_LICENSE("GPL");
