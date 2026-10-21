// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Transsion Inc.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/leds.h>
#include <linux/firmware.h>
#include <linux/i2c-dev.h>
#include "leds-aw20144.h"
#include "leds-aw20144_effect.h"
#include "tc_led_class.h"

#define AW20144_I2C_NAME		"aw20144_led"
#define AW20144_DRIVER_VERSION		"v0.3.0"
#define AW20144_READ_CHIPID_RETRIES	5
#define AW_I2C_READ_RETRIES		5
#define AW_I2C_WRITE_RETRIES		5
#define AW20144_EFFECT_CNT		5

AWCFGDATA aw20144_cfg_array[] = {
	{aw20144_all_rgb_off, sizeof(aw20144_all_rgb_off)},
	{aw20144_all_rgb_on, sizeof(aw20144_all_rgb_on)},
	{aw20144_red_ok_on, sizeof(aw20144_red_ok_on)},
	{aw20144_red_ok_blink, sizeof(aw20144_red_ok_blink)},
	{aw20144_red_blink_off, sizeof(aw20144_red_blink_off)},
};

static char aw20144_cfg_bin[][32] = {
	"aw20144_all_rgb_off.bin",
	"aw20144_all_rgb_on.bin",
	"aw20144_red_ok_on.bin",
	"aw20144_red_ok_blink.bin",
	"aw20144_red_blink_off.bin",
};

static struct aw20144 *paw20144;
atomic_t aw_process_flag = ATOMIC_INIT(0);
/*******************************************************************************
 *
 * aw20144 i2c read/write
 *
 ******************************************************************************/

static int aw20144_i2c_read(struct aw20144 *aw20144,
			unsigned char reg_addr, unsigned char *reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW_I2C_READ_RETRIES) {
		ret = i2c_smbus_read_byte_data(aw20144->client, reg_addr);
		if (ret < 0) {
			pr_err("%s: i2c read cnt=%d, error=%d\n",
					__func__, cnt, ret);
		} else {
			*reg_data = ret;
			break;
		}
		cnt++;
		usleep_range(2000, 2500);
	}

	return ret;
}

static int aw20144_i2c_write(struct aw20144 *aw20144,
			unsigned char reg_addr, unsigned char reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW_I2C_WRITE_RETRIES) {
		ret = i2c_smbus_write_byte_data(aw20144->client,
						reg_addr, reg_data);
		if (ret < 0) {
			pr_err("%s: i2c write cnt=%d, error=%d\n",
						__func__, cnt, ret);
		} else {
			break;
		}

		cnt++;
		usleep_range(2000, 2500);
	}

	return ret;
}

static int aw20144_i2c_write_bit(struct aw20144 *aw20144,
				unsigned char reg_addr, unsigned int mask,
				unsigned char reg_data)
{
	unsigned char reg_val;

	aw20144_i2c_read(aw20144, reg_addr, &reg_val);
	reg_val &= mask;
	reg_val |= (reg_data & (~mask));
	aw20144_i2c_write(aw20144, reg_addr, reg_val);

	return 0;
}

static int aw20144_set_page(struct aw20144 *aw20144, unsigned char reg_data)
{
	return aw20144_i2c_write(aw20144, AWPAGEADDR, reg_data);
}

static int aw20144_block_write(struct aw20144 *aw20144,
					const char *buf, int count)
{
	return i2c_master_send(aw20144->client, buf, count);
}

/*******************************************************************************
 *
 * aw20144 led init
 *
 ******************************************************************************/

static int aw20144_led_init(struct aw20144 *aw20144)
{
	int i = 0;

	pr_info("enter %s\n", __func__);
	/* enter page0 */
	aw20144_set_page(aw20144, AWPAGE0);
	/* set SW active number */
	aw20144_i2c_write_bit(aw20144, REG_GCR, GCR_SWSEL_MSK, GCR_SWSEL_VAL << GCR_SWSEL_POS);
	/* set global current */
	aw20144_i2c_write(aw20144, REG_GCCR, aw20144->imax);
	/* set constant current */
	aw20144_set_page(aw20144, AWPAGE2);
	for (i = 0; i <= AW20144_CFG_CNT_PAGE2 - 2; i++)
		aw20144_i2c_write(aw20144, REG_SL0 + i, aw20144->sl_current);

	return 0;
}

/*******************************************************************************
 *
 * aw20144 brightness work
 *
 ******************************************************************************/

static void aw20144_brightness_work(struct work_struct *work)
{
	struct aw20144 *aw20144 = container_of(work, struct aw20144,
							brightness_work);
	unsigned char reg_page1_pwm[AW20144_CFG_CNT_PAGE1];

	pr_info("enter %s\n", __func__);

	aw20144_set_page(aw20144, AWPAGE1);

	if (aw20144->cdev.brightness > aw20144->cdev.max_brightness)
		aw20144->cdev.brightness = aw20144->cdev.max_brightness;

	if (aw20144->cdev.brightness > 0) {
		/* set all led brightness */
		memset(reg_page1_pwm, aw20144->cdev.brightness,
						sizeof(reg_page1_pwm));
		/* base address */
		reg_page1_pwm[0] = 0x00;
		aw20144_block_write(aw20144, reg_page1_pwm,
						AW20144_CFG_CNT_PAGE1);
		/* set chip enable */
		aw20144_set_page(aw20144, AWPAGE0);
		aw20144_i2c_write_bit(aw20144, REG_GCR, BIT_CHIPEN_DISABLE,
							BIT_CHIPEN_ENABLE);
	} else {
		/* clear all led brightness */
		memset(reg_page1_pwm, 0, sizeof(reg_page1_pwm));
		aw20144_block_write(aw20144, reg_page1_pwm,
						AW20144_CFG_CNT_PAGE1);
	}
}

static void aw20144_set_brightness(struct led_classdev *cdev,
					enum led_brightness brightness)
{
	struct aw20144 *aw20144 = container_of(cdev, struct aw20144, cdev);

	pr_info("enter %s\n", __func__);

	aw20144->cdev.brightness = brightness;
	schedule_work(&aw20144->brightness_work);
}

static void aw20144_rgbblink_cfg(struct aw20144 *aw20144, unsigned int *databuf)
{
	pr_info("enter %s\n", __func__);

	/* enter page0 */
	aw20144_set_page(aw20144, AWPAGE0);
	/* set PWMH0/PWML0 */
	aw20144->max_rgbblink = (databuf[1] & 0x0000ff00) >> 8;
	aw20144_i2c_write(aw20144, REG_PWMH0, aw20144->max_rgbblink);
	aw20144->min_rgbblink = (databuf[1] & 0x000000ff);
	aw20144_i2c_write(aw20144, REG_PWML0, aw20144->min_rgbblink);
	/* set rise/hold/fall/off time */
	aw20144->time_rise_hold = (databuf[2] & 0x0000ff00) >> 8;
	aw20144_i2c_write(aw20144, REG_PAT0T0,
					aw20144->time_rise_hold);
	aw20144->time_fall_off = (databuf[2] & 0x000000ff);
	aw20144_i2c_write(aw20144, REG_PAT0T1,
					aw20144->time_fall_off);
	/* set chip enable */
	aw20144_i2c_write_bit(aw20144, REG_GCR,
			BIT_CHIPEN_DISABLE, BIT_CHIPEN_ENABLE);
	/* set auto breath mode */
	aw20144_i2c_write_bit(aw20144, REG_PAT0CFG,
				BIT_PATMD_MANUAL, BIT_PATMD_AUTO);
	/* enable auto breath */
	aw20144_i2c_write_bit(aw20144, REG_PAT0CFG,
				BIT_PATEN_DISABLE, BIT_PATEN_ENABLE);
	/* run clear */
	aw20144_i2c_write_bit(aw20144, REG_PATGO,
				BIT_RUN0_DISABLE, BIT_RUN0_DISABLE);
	/* run auto breath */
	aw20144_i2c_write_bit(aw20144, REG_PATGO,
				BIT_RUN0_DISABLE, BIT_RUN0_ENABLE);
}

static void aw20144_cfg_bin_loaded(const struct firmware *cont,
						void *context)
{
	struct aw20144 *aw20144 = context;

	int i = 0;
	unsigned char page = 0;
	unsigned char reg_addr = 0;
	unsigned char reg_val = 0;

	pr_info("enter %s\n", __func__);

	if (!cont) {
		dev_err(aw20144->dev,
				"%s: no bin file found\n", __func__);
		release_firmware(cont);
		return;
	}

	for (i = 0; i < cont->size; i += 2) {
		if (*(cont->data + i) == AWPAGEADDR) {
			page = *(cont->data + i + 1);
			pr_info("%s: enter page %x\n", __func__, page);
		}
		aw20144_i2c_write(aw20144, *(cont->data + i),
					*(cont->data + i + 1));

		reg_addr = *(cont->data + i);
		reg_val = *(cont->data + i + 1);
		if ((page == AWPAGE0) && (reg_addr == REG_RSTN)
				&& (reg_val == AWREG_SWRST)) {
			usleep_range(5000, 5500);
			pr_info("%s: software reset complete\n", __func__);
			aw20144_led_init(aw20144);
			pr_info("%s: led init complete\n", __func__);
		}
	}

	release_firmware(cont);
	pr_info("%s: config bin load complete\n", __func__);
}

static int aw20144_cfg_update_bin(struct aw20144 *aw20144)
{
	pr_info("enter %s\n", __func__);

	return request_firmware_nowait(THIS_MODULE, FW_ACTION_UEVENT,
				aw20144_cfg_bin[aw20144->designeffect],
				aw20144->dev, GFP_KERNEL, aw20144,
				aw20144_cfg_bin_loaded);
}

static int aw20144_cfg_update_array(struct aw20144 *aw20144,
					unsigned char *cfg_data,
					unsigned int cfg_size)
{
	unsigned int i = 0;
	unsigned char page = 0;
	unsigned char reg_addr = 0;
	unsigned char reg_val = 0;

	pr_info("enter %s\n", __func__);

	for (i = 0; i < cfg_size; i += 2) {
		if (cfg_data[i] == AWPAGEADDR) {
			page = cfg_data[i + 1];
			pr_info("%s: enter page %x\n", __func__, page);
		}

		aw20144_i2c_write(aw20144, cfg_data[i], cfg_data[i + 1]);

		reg_addr = cfg_data[i];
		reg_val = cfg_data[i + 1];
		if ((page == AWPAGE0) && (reg_addr == REG_RSTN)
				&& (reg_val == AWREG_SWRST)) {
			usleep_range(5000, 5500);
			pr_info("%s: software reset complete\n", __func__);
			aw20144_led_init(aw20144);
			pr_info("%s: led init complete\n", __func__);
		}
	}

	pr_info("%s: config array load complete\n", __func__);

	return 0;
}

/*******************************************************************************
 *
 * sysfs attribute group: design effect store
 *
 ******************************************************************************/

static ssize_t aw20144_designeffect_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw20144 *aw20144 = container_of(led_cdev, struct aw20144, cdev);

	unsigned int databuf[1];
	int ret = -1;

	pr_info("enter %s\n", __func__);

	ret = kstrtou32(buf, 0, &databuf[0]);
	if (ret < 0) {
		dev_err(aw20144->dev, "%s: input data invalid!", __func__);
		return ret;
	}

	aw20144->designeffect = databuf[0];
	if (aw20144->effect_bin) {
		if (aw20144->designeffect < AW20144_EFFECT_CNT) {
			aw20144_cfg_update_bin(aw20144);
		} else {
			dev_err(aw20144->dev,
				"%s: input data out of range!\n", __func__);
			return -EAGAIN;
		}
	} else {
		if (aw20144->designeffect < AW20144_EFFECT_CNT) {
			aw20144_cfg_update_array(aw20144,
			aw20144_cfg_array[aw20144->designeffect].cfg_data,
			aw20144_cfg_array[aw20144->designeffect].cfg_size);
		} else {
			dev_err(aw20144->dev,
				"%s: input data out of range!\n", __func__);
			return -EAGAIN;
		}
	}

	return len;
}

/*******************************************************************************
 *
 * sysfs attribute group: allrgbblink store
 *
 ******************************************************************************/

static ssize_t aw20144_allrgbblink_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw20144 *aw20144 = container_of(led_cdev,
						struct aw20144, cdev);
	unsigned char reg_page3_pwm[AW20144_CFG_CNT_PAGE3];
	unsigned int databuf[3] = { 0, 0, 0 };

	pr_info("enter %s\n", __func__);
	/* enter page 3 */
	aw20144_set_page(aw20144, AWPAGE3);

	if (sscanf(buf, "%x %x %x",
			&databuf[0], &databuf[1], &databuf[2]) == 3) {
		/* set rgb blink value */
		aw20144->rgb_color = (databuf[0] & 0x000000ff);
		memset(reg_page3_pwm, aw20144->rgb_color,
						sizeof(reg_page3_pwm));
		/* base address */
		reg_page3_pwm[0] = 0x00;
		aw20144_block_write(aw20144, reg_page3_pwm,
						AW20144_CFG_CNT_PAGE3);
		/* blink parameter configuration */
		aw20144_rgbblink_cfg(aw20144, databuf);
	} else {
		dev_err(aw20144->dev,
			"%s: input data invalid!", __func__);
		return -EAGAIN;
	}

	return len;
}

/*******************************************************************************
 *
 * sysfs attribute group: onergbblink store
 *
 ******************************************************************************/

static ssize_t aw20144_onergbblink_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw20144 *aw20144 = container_of(led_cdev, struct aw20144, cdev);
	unsigned int databuf[3] = { 0, 0, 0 };

	pr_info("enter %s\n", __func__);

	if (sscanf(buf, "%x %x %x", &databuf[0], &databuf[1],
						&databuf[2]) == 3) {
		/* enter page 3 */
		aw20144_set_page(aw20144, AWPAGE3);
		/* select rgb and color */
		aw20144->rgb_num = (databuf[0] & 0x0000ff00) >> 8;
		aw20144->rgb_color = (databuf[0] & 0x000000ff);
		if (aw20144->rgb_num <= AW20144_RGB_NUM) {
			aw20144_i2c_write(aw20144, aw20144->rgb_num,
						aw20144->rgb_color);
			/* blink parameter configuration */
			aw20144_rgbblink_cfg(aw20144, databuf);
		} else {
			dev_err(aw20144->dev,
				"%s: rgb number invalid!", __func__);
			return -EAGAIN;
		}
	} else {
		dev_err(aw20144->dev, "%s: input data invalid!", __func__);
		return -EAGAIN;
	}

	return len;
}

/*******************************************************************************
 *
 * sysfs attribute group: allrgbbrightness store
 *
 ******************************************************************************/

static ssize_t aw20144_allrgbbrightness_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw20144 *aw20144 = container_of(led_cdev, struct aw20144, cdev);

	int led_num = 0;
	unsigned char reg_page1_pwm[AW20144_CFG_CNT_PAGE1];
	unsigned int databuf[1] = { 0 };
	int ret = -1;

	pr_info("enter %s\n", __func__);

	ret = kstrtou32(buf, 0, &databuf[0]);
	if (ret < 0) {
		dev_err(aw20144->dev, "%s: input data invalid!", __func__);
		return ret;
	}

	/* enter page 1 */
	aw20144_set_page(aw20144, AWPAGE1);
	/* base address */
	reg_page1_pwm[0] = 0x00;

	for (led_num = 1; led_num < AW20144_CFG_CNT_PAGE1; led_num += 3) {
		reg_page1_pwm[led_num] = (databuf[0] & 0x00ff0000) >> 16;
		reg_page1_pwm[led_num + 1] = (databuf[0] & 0x0000ff00) >> 8;
		reg_page1_pwm[led_num + 2] = (databuf[0] & 0x000000ff);
	}
	/* set all pwm value */
	aw20144_block_write(aw20144, reg_page1_pwm, AW20144_CFG_CNT_PAGE1);
	/* set chip enable */
	aw20144_set_page(aw20144, AWPAGE0);
	aw20144_i2c_write_bit(aw20144, REG_GCR,
				BIT_CHIPEN_DISABLE, BIT_CHIPEN_ENABLE);

	return len;
}

/*******************************************************************************
 *
 * sysfs attribute group: onerrgbbrightness store
 *
 ******************************************************************************/

static ssize_t aw20144_onergbbrightness_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw20144 *aw20144 = container_of(led_cdev, struct aw20144, cdev);

	unsigned int databuf[2] = { 0, 0 };

	pr_info("enter %s\n", __func__);

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		/* enter page 1 */
		aw20144_set_page(aw20144, AWPAGE1);
		if (databuf[0] <= AW20144_RGB_NUM) {
			aw20144->rgbbrightness =
					(databuf[1] & 0x00ff0000) >> 16;
			aw20144_i2c_write(aw20144, databuf[0] * 3,
						aw20144->rgbbrightness);
			aw20144->rgbbrightness =
					(databuf[1] & 0x0000ff00) >> 8;
			aw20144_i2c_write(aw20144, (databuf[0] * 3 + 1),
						aw20144->rgbbrightness);
			aw20144->rgbbrightness = (databuf[1] & 0x000000ff);
			aw20144_i2c_write(aw20144, (databuf[0] * 3 + 2),
						aw20144->rgbbrightness);
			/* enter page 0 */
			aw20144_set_page(aw20144, AWPAGE0);
			/* set chip enable */
			aw20144_i2c_write_bit(aw20144, REG_GCR,
				BIT_CHIPEN_DISABLE, BIT_CHIPEN_ENABLE);
		} else {
			dev_err(aw20144->dev,
					"%s: rgb number invalid!", __func__);
			return -EAGAIN;
		}
	} else {
		dev_err(aw20144->dev, "%s: input data invalid!", __func__);
		return -EAGAIN;
	}

	return len;
}

/*******************************************************************************
 *
 * sysfs attribute group: reg store/show
 *
 ******************************************************************************/

static ssize_t aw20144_reg_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw20144 *aw20144 = container_of(led_cdev, struct aw20144, cdev);

	ssize_t len = 0;
	unsigned char i = 0;
	unsigned char reg_val = 0;

	pr_info("enter %s\n", __func__);
	/* enter page 0 */
	aw20144_set_page(aw20144, AWPAGE0);
	for (i = 0; i < AW20144_REG_PAGE0_MAX; i++) {
		if (!(aw20144_reg_page0_access[i] & REG_RD_ACCESS))
			continue;
		aw20144_i2c_read(aw20144, i, &reg_val);
		len += snprintf(buf + len, PAGE_SIZE - len,
				"PAGE0 reg: 0x%02x = 0x%02x\n", i, reg_val);
	}

	return len;
}

static ssize_t aw20144_reg_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw20144 *aw20144 = container_of(led_cdev, struct aw20144, cdev);

	unsigned int databuf[3] = { 0, 0, 0 };

	pr_info("enter %s\n", __func__);

	if (sscanf(buf, "%x %x %x",
			&databuf[0], &databuf[1], &databuf[2]) == 3) {
		if (databuf[0] == AWPAGE0) {
			/* select page */
			aw20144_set_page(aw20144, databuf[0]);
			/* write value in address */
			aw20144_i2c_write(aw20144, databuf[1], databuf[2]);
		} else {
			dev_err(aw20144->dev,
				"%s: input reg page invalid!\n", __func__);
		}
	} else {
		dev_err(aw20144->dev,
				"%s: input reg data format err\n", __func__);
	}

	return len;
}

/*******************************************************************************
 *
 * hardware enable/off
 *
 ******************************************************************************/

static int aw20144_hw_enable(struct aw20144 *aw20144)
{
	pr_info("enter %s\n", __func__);

	if (aw20144 && gpio_is_valid(aw20144->enable_gpio)) {
		gpio_set_value_cansleep(aw20144->enable_gpio, 0);
		usleep_range(2000, 2500);

		gpio_set_value_cansleep(aw20144->enable_gpio, 1);
		usleep_range(3000, 3500);
		pr_info("%s: set gpio hight\n", __func__);
	} else {
		dev_err(aw20144->dev,
			"%s: aw20144 or gpio unavailable", __func__);
		return -EIO;
	}

	return 0;
}

static int aw20144_hw_off(struct aw20144 *aw20144)
{
	pr_info("enter %s\n", __func__);

	if (aw20144 && gpio_is_valid(aw20144->enable_gpio)) {
		gpio_set_value_cansleep(aw20144->enable_gpio, 0);
		usleep_range(2000, 2500);
		pr_info("%s: set gpio low\n", __func__);
	} else {
		dev_err(aw20144->dev,
			"%s: aw20144 or gpio unavailable\n", __func__);
		return -EIO;
	}

	return 0;
}

/*******************************************************************************
 *
 * sysfs attribute group: soft reset store/show
 *
 ******************************************************************************/

static ssize_t aw20144_swrst_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw20144 *aw20144 = container_of(led_cdev, struct aw20144, cdev);
	unsigned int databuf[3] = { 0, 0, 0 };

	pr_info("enter %s\n", __func__);

	if (sscanf(buf, "%x %x %x", &databuf[0], &databuf[1],
						&databuf[2]) == 3) {
		/* select page */
		aw20144_set_page(aw20144, databuf[0]);
		/* software reset  */
		aw20144_i2c_write(aw20144, databuf[1], databuf[2]);
		usleep_range(8000, 85000);
		pr_info("%s: software reset complete\n", __func__);
		aw20144_led_init(aw20144);
		pr_info("%s: led init complete\n", __func__);
	} else {
		dev_err(aw20144->dev,
			"%s: input reg data format err\n", __func__);
	}

	return len;
}

/*******************************************************************************
 *
 * sysfs attribute group: hwen store/show
 *
 ******************************************************************************/

static ssize_t aw20144_hwen_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw20144 *aw20144 = container_of(led_cdev, struct aw20144, cdev);
	unsigned int databuf[1] = { 0 };
	int ret = -1;

	pr_info("enter %s\n", __func__);

	ret = kstrtou32(buf, 0, &databuf[0]);
	if (ret < 0) {
		dev_err(aw20144->dev, "%s: input data invalid!", __func__);
		return ret;
	}

	if (databuf[0] == 1) {
		aw20144_hw_enable(aw20144);
		pr_info("%s: hw enable complete\n", __func__);
	} else {
		aw20144_hw_off(aw20144);
		pr_info("%s: hw off complete\n", __func__);
	}

	return len;
}

static ssize_t aw20144_hwen_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw20144 *aw20144 = container_of(led_cdev, struct aw20144, cdev);
	ssize_t len = 0;

	pr_info("enter %s\n", __func__);

	len += snprintf(buf + len, PAGE_SIZE - len, "hwen = %d\n",
				gpio_get_value(aw20144->enable_gpio));

	return len;
}

/*******************************************************************************
 *
 * sysfs attribute group: tran_led_cmd store/show
 *
 ******************************************************************************/
#if 1
static int aw20144_store_tran_led_cmd(struct tc_led_device *dev, const char *cmd)
{
	struct aw20144 *aw20144 = paw20144;
	int num[10];

	memset(num, 0, 10);

	if (sscanf(cmd, "%x %x %x %x %x %x", &num[0], &num[1], &num[2], &num[3],&num[4],&num[5]) == 6) {
		dev_err(aw20144->dev,"%s 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",__func__,num[0], num[1], num[2], num[3],num[4],num[5]);
		aw20144->latest_cmd = num[0];
		switch(num[0]){//type
			case 0x00:
				switch(num[1]){//mode
					case 0x00://close
						paw20144->designeffect = 0;
						if(atomic_read(&aw_process_flag) != 0) {
							atomic_set(&aw_process_flag, 0);
						}
						aw20144_cfg_update_bin(paw20144);
						//aw20144_hw_off(paw20144);
						dev_err(aw20144->dev,"%s: soft_show_en = 1\n", __func__);
						break;
					case 0x01://open
						aw20144_hw_enable(paw20144);
						paw20144->designeffect = 1;
						if(atomic_read(&aw_process_flag) != 0) {
							atomic_set(&aw_process_flag, 0);
						}
						aw20144_cfg_update_bin(paw20144);
						dev_err(aw20144->dev,"%s: soft_show_en = 1\n", __func__);
						break;
					case 0x02://charging
						break;
					case 0x03://poweron
						break;
					case 0x04://incoming
						break;
					case 0x05://notify
						break;
					case 0x06://game
						break;
					case 0x07://poweron
						break;
					case 0x08://notify
						break;
					case 0x09://midtest
						aw20144_hw_enable(paw20144);
						paw20144->designeffect = 1;
						if(atomic_read(&aw_process_flag) != 0) {
							atomic_set(&aw_process_flag, 0);
						}
						aw20144_cfg_update_bin(paw20144);
						aw20144_i2c_write(paw20144, REG_GCCR, paw20144->imax);
						dev_err(aw20144->dev,"%s: soft_show_en = 1\n", __func__);
						break;
					case 0x0A://audio
						break;
					case 0x0B://fully charged
						break;
					case 0x0C://audio gain
						break;
					default:
						aw20144->latest_cmd = 0;
						dev_err(aw20144->dev,"%s(no para) %d\n", __func__,__LINE__);
						break;
				}
				break;
			default:
				dev_err(aw20144->dev,"%s(no para) %d\n", __func__,__LINE__);
				break;
		}
	}

	return 0;
}

static int aw20144_show_tran_led_cmd(struct tc_led_device *dev, char *buf)
{
	return sprintf(buf, "%d\n",  paw20144->latest_cmd);
}
#else
static ssize_t store_tran_led_cmd(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw20144 *aw20144 = container_of(led_cdev, struct aw20144, cdev);
	int num[10];
	if (!size)
		return 0;

	memset(num, 0, 10);

	if (size > (sizeof(num) - 1))
		return -EINVAL;

	if (sscanf(buf, "%x %x %x %x %x %x", &num[0], &num[1], &num[2], &num[3],&num[4],&num[5]) == 6) {
		dev_err(aw20144->dev,"%s 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",__func__,num[0], num[1], num[2], num[3],num[4],num[5]);
		aw20144->latest_cmd = num[0];
		switch(num[0]){//type
			case 0x00:
				switch(num[1]){//mode
					case 0x00://open
						aw20144_hw_enable(paw20144);
						paw20144->designeffect = 1;
						if(atomic_read(&aw_process_flag) != 0) {
							atomic_set(&aw_process_flag, 0);
						}
						aw20144_cfg_update_bin(paw20144);
						dev_err(aw20144->dev,"%s: soft_show_en = 1\n", __func__);
						break;
					case 0x01://close
						paw20144->designeffect = 0;
						if(atomic_read(&aw_process_flag) != 0) {
							atomic_set(&aw_process_flag, 0);
						}
						aw20144_cfg_update_bin(paw20144);
						//aw20144_hw_off(paw20144);
						dev_err(aw20144->dev,"%s: soft_show_en = 1\n", __func__);
						break;
					case 0x02://charging
						break;
					case 0x03://poweron
						break;
					case 0x04://incoming
						break;
					case 0x05://notify
						break;
					case 0x06://game
						break;
					case 0x07://poweron
						break;
					case 0x08://notify
						break;
					case 0x09://midtest
						aw20144_hw_enable(paw20144);
						paw20144->designeffect = 1;
						if(atomic_read(&aw_process_flag) != 0) {
							atomic_set(&aw_process_flag, 0);
						}
						aw20144_cfg_update_bin(paw20144);
						aw20144_i2c_write(paw20144, REG_GCCR, paw20144->imax);
						dev_err(aw20144->dev,"%s: soft_show_en = 1\n", __func__);
						break;
					case 0x0A://audio
						break;
					case 0x0B://fully charged
						break;
					case 0x0C://audio gain
						break;
					default:
						aw20144->latest_cmd = 0;
						dev_err(aw20144->dev,"%s(no para) %d\n", __func__,__LINE__);
						break;
				}
				break;
			default:
				dev_err(aw20144->dev,"%s(no para) %d\n", __func__,__LINE__);
				break;
		}
	}

	return size;
}

static ssize_t show_tran_led_cmd(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw20144 *aw20144 = container_of(led_cdev, struct aw20144, cdev);
	return sprintf(buf, "%d\n",  aw20144->latest_cmd);
}
#endif

/*******************************************************************************
 *
 * sysfs attribute group: tran_led_check store/show
 *
 ******************************************************************************/
#define REG_GCR_SHORT_CFG 0xb5
#define REG_GCR_OPEN_CFG 0xb7
#define REG_GCR_CLOSE_CFG 0xb0

static int aw20144_factory_is_except(struct aw20144 *aw20144, int reg, int val)
{
	int i;
	for(i = 0;i < aw20144->ignore_open_circuit_test_cfg_size; i++)
	{
		if(reg == aw20144->ignore_open_circuit_test_cfg_reg[i]
			&& val == aw20144->ignore_open_circuit_test_cfg_val[i])
			return 1;
	}
	return 0;
}

static void aw20144_reg_dump(struct aw20144 *aw20144)
{
	unsigned char reg_val = 0;
	int i = 0;

	aw20144_set_page(aw20144, AWPAGE0);
	for (i = 0; i < AW20144_REG_PAGE0_MAX; i++) {
		if (!(aw20144_reg_page0_access[i] & REG_RD_ACCESS))
			continue;
		aw20144_i2c_read(aw20144, i, &reg_val);
		dev_err(aw20144->dev,"%s i=%x reg_val=%x\n", __func__,i, reg_val);
	}
}

static int aw20144_factory_test(struct aw20144 *aw20144, enum factory_mode mode)
{
	int i,ret;
	unsigned char reg_val = 0;

	switch(mode){
		case AW20144_FACTORY_SHORT:
			paw20144->designeffect = 1;
			if(atomic_read(&aw_process_flag) != 0) {
				atomic_set(&aw_process_flag, 0);
			}
			aw20144_cfg_update_bin(paw20144);
			usleep_range(200000, 250000);
			aw20144_i2c_write(paw20144, REG_GCCR, paw20144->imax);
			aw20144_i2c_write(paw20144, REG_GCR, REG_GCR_SHORT_CFG);
			usleep_range(200000, 250000);
			ret = aw20144_i2c_read(paw20144, REG_GCR, &reg_val);
			dev_err(aw20144->dev,"%s read val mode=%d cfg val=%x ret=%d %d\n", __func__,mode,reg_val,ret,__LINE__);
			break;
		case AW20144_FACTORY_OPEN:
			paw20144->designeffect = 1;
			if(atomic_read(&aw_process_flag) != 0) {
				atomic_set(&aw_process_flag, 0);
			}
			aw20144_cfg_update_bin(paw20144);
			usleep_range(200000, 250000);
			aw20144_i2c_write(paw20144, REG_GCCR, paw20144->imax);
			aw20144_i2c_write(paw20144, REG_GCR, REG_GCR_OPEN_CFG);
			usleep_range(200000, 250000);
			ret = aw20144_i2c_read(paw20144, REG_GCR, &reg_val);
			dev_err(aw20144->dev,"%s read val mode=%d cfg val=%x ret=%d %d\n", __func__,mode,reg_val,ret,__LINE__);
			break;
		default:
			return -1;
	};
	if(ret < 0){
		dev_err(aw20144->dev,"%s write factory mode error %d\n", __func__,__LINE__);
		return -1;
	}
	for(i = REG_OSR0; i <= REG_OSR23 ;i++)
	{
		ret = aw20144_i2c_read(paw20144, i, &reg_val);
		if(ret < 0){
			dev_err(aw20144->dev,"%s read val mode=%d error reg=%x val=%x %d\n", __func__,mode, i, reg_val,__LINE__);
			return -1;
		}
		if(reg_val != 0)
		{
			if(mode == AW20144_FACTORY_SHORT || (mode == AW20144_FACTORY_OPEN && !aw20144_factory_is_except(aw20144, i, reg_val)))
			{
				dev_err(aw20144->dev,"%s factory mode=%d error reg=%x val=%x  %d\n", __func__,mode, i, reg_val,__LINE__);
				paw20144->test_result = 1;
				aw20144_reg_dump(paw20144);
				goto aw20144_factory_test_exit;
			}
		}
		dev_err(aw20144->dev,"%s factory mode=%d reg=%x val=%x  %d\n", __func__,mode, i, reg_val,__LINE__);
	}
	paw20144->test_result = 0;
aw20144_factory_test_exit:
	//ret = aw20144_i2c_write_bit(paw20144, REG_GCR, BIT_FACTORY_TEST,BIT_STOP_TEST);
	usleep_range(200000, 250000);
	aw20144_i2c_write(paw20144, REG_GCR, REG_GCR_CLOSE_CFG);
	paw20144->designeffect = 0;
	if(atomic_read(&aw_process_flag) != 0) {
		atomic_set(&aw_process_flag, 0);
	}
	aw20144_cfg_update_bin(paw20144);
	usleep_range(200000, 250000);
	//aw20144_hw_off(paw20144);
	dev_err(aw20144->dev,"%s mode=%d result=%d %d\n", __func__,mode,paw20144->test_result,__LINE__);
	return ret;
}

#if 0
static ssize_t store_tran_led_check(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw20144 *aw20144 = container_of(led_cdev, struct aw20144, cdev);
	int num[10];
	if (!size)
		return 0;

	memset(num, 0, 10);

	if (size > (sizeof(num) - 1))
		return -EINVAL;

	if (sscanf(buf, "%d", &num[0]) == 1) {
		dev_err(aw20144->dev,"%s %d\n",__func__,num[0]);
		aw20144->latest_cmd = num[0];
		switch(num[0]){//type
			case 1:
				aw20144_factory_test(paw20144, AW20144_FACTORY_SHORT);
				break;
			case 2:
				aw20144_factory_test(paw20144, AW20144_FACTORY_OPEN);
				break;
			default:
				dev_err(aw20144->dev,"%s(no para) %d\n", __func__,__LINE__);
				break;
		}
	}
	return size;
}

static ssize_t show_tran_led_check(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n",  paw20144->test_result);
}
#else
static int aw20144_store_tran_led_check(struct tc_led_device *dev, const char *cmd)
{
	int num[10];
	memset(num, 0, 10);

	if (sscanf(cmd, "%d", &num[0]) == 1) {
		dev_err(paw20144->dev,"%s %d\n",__func__,num[0]);
		paw20144->latest_cmd = num[0];
		switch(num[0]){//type
			case 1:
				aw20144_factory_test(paw20144, AW20144_FACTORY_SHORT);
				break;
			case 2:
				aw20144_factory_test(paw20144, AW20144_FACTORY_OPEN);
				break;
			default:
				dev_err(paw20144->dev,"%s(no para) %d\n", __func__,__LINE__);
				break;
		}
	}
	return 0;
}

static int aw20144_show_tran_led_check(struct tc_led_device *dev, char *buf)
{
	return sprintf(buf, "%d\n",  paw20144->test_result);
}
#endif

static DEVICE_ATTR(designeffect, S_IWUSR | S_IRUGO,
			NULL, aw20144_designeffect_store);
static DEVICE_ATTR(allrgbblink, S_IWUSR | S_IRUGO,
			NULL, aw20144_allrgbblink_store);
static DEVICE_ATTR(onergbblink, S_IWUSR | S_IRUGO,
			NULL, aw20144_onergbblink_store);
static DEVICE_ATTR(allrgbbrightness, S_IWUSR | S_IRUGO,
			NULL, aw20144_allrgbbrightness_store);
static DEVICE_ATTR(onergbbrightness, S_IWUSR | S_IRUGO,
			NULL, aw20144_onergbbrightness_store);
static DEVICE_ATTR(reg, S_IWUSR | S_IRUGO,
			aw20144_reg_show, aw20144_reg_store);
static DEVICE_ATTR(swrst, S_IWUSR | S_IRUGO,
			NULL, aw20144_swrst_store);
static DEVICE_ATTR(hwen, S_IWUSR | S_IRUGO,
			aw20144_hwen_show, aw20144_hwen_store);
#if 0
static DEVICE_ATTR(tran_led_cmd, 0664,
			show_tran_led_cmd, store_tran_led_cmd);
static DEVICE_ATTR(tran_led_check, 0664,
			show_tran_led_check, store_tran_led_check);
#endif

static struct attribute *aw20144_led_attributes[] = {
	&dev_attr_reg.attr,
	&dev_attr_hwen.attr,
	&dev_attr_swrst.attr,
	&dev_attr_onergbbrightness.attr,
	&dev_attr_allrgbbrightness.attr,
	&dev_attr_onergbblink.attr,
	&dev_attr_allrgbblink.attr,
	&dev_attr_designeffect.attr,
	NULL
};

static struct attribute_group aw20144_attribute_group = {
	.attrs = aw20144_led_attributes
};

/*******************************************************************************
 *
 * read chip id
 *
 ******************************************************************************/

static int aw20144_read_chipid(struct aw20144 *aw20144)
{
	int ret = -1;
	unsigned char cnt = 0;
	unsigned char reg_val = 0;

	pr_info("enter %s\n", __func__);

	/* enter page0 */
	aw20144_set_page(aw20144, AWPAGE0);
	aw20144_i2c_write(aw20144, REG_RSTN, AWREG_SWRST);
	usleep_range(2000, 2500);

	/* hardware enable */
	ret = aw20144_hw_enable(aw20144);
	if (ret)
		dev_err(aw20144->dev, "%s: hardware enable failed", __func__);

	while (cnt < AW20144_READ_CHIPID_RETRIES) {
		ret = aw20144_i2c_read(aw20144, REG_RSTN, &reg_val);
		pr_info("AW20144 chip id is %0x\n", reg_val);
		if ((reg_val == AW20144_CHIPID) || (reg_val == AW20144_CHIPID_A2)) {
			pr_info("read aw20144 chipid successful\n");
			return 0;
		}

		dev_err(aw20144->dev, "read aw20144 id failed, err=%d\n", ret);
		cnt++;
		usleep_range(1000, 1500);
	}

	return ret;
}

/*******************************************************************************
 *
 * parse device tree
 *
 ******************************************************************************/

static int aw20144_parse_dts(struct aw20144 *aw20144,
						struct device_node *np)
{
	int ret = -1;

	pr_info("enter %s\n", __func__);

	aw20144->enable_gpio = of_get_named_gpio(np, "enable-gpio", 0);
	if (gpio_is_valid(aw20144->enable_gpio)) {
		dev_info(aw20144->dev, "%s: enable gpio available\n", __func__);
	} else {
		dev_err(aw20144->dev,
			"%s: enable gpio unavailable\n", __func__);
		return -EIO;
	}

	ret = of_property_read_u32(np, "aw20144,imax", &aw20144->imax);
	if (ret < 0) {
		dev_err(aw20144->dev,
			"%s: parse imax err, ret = %d\n", __func__, ret);
		return ret;
	}
	pr_info("%s: led imax = 0x%x\n", __func__, aw20144->imax);

	ret = of_property_read_u32(np, "aw20144,sl_current",
						&aw20144->sl_current);
	if (ret < 0) {
		dev_err(aw20144->dev,
			"%s: parse sl_current err, ret = %d\n", __func__, ret);
		return ret;
	}
	pr_info("%s: led sl_current = 0x%x\n", __func__, aw20144->sl_current);

	ret = of_property_read_u32(np, "aw20144,max_brightness",
					&aw20144->cdev.max_brightness);
	if (ret < 0) {
		dev_err(aw20144->dev,
		"%s: parse max-brightness err, ret = %d\n", __func__, ret);
		return ret;
	}
	pr_info("%s: led max brightness = 0x%x\n", __func__,
					aw20144->cdev.max_brightness);


	ret = of_property_read_u32(np, "ignore_open_circuit_test_cfg_size", &aw20144->ignore_open_circuit_test_cfg_size);
	if (ret < 0) {
		dev_err(aw20144->dev,
			"%s: parse ignore_open_circuit_test_cfg_size err, ret = %d\n", __func__, ret);
	}

	ret = of_property_read_u32_array(np, "ignore_open_circuit_test_cfg_reg",
		(u32 *)aw20144->ignore_open_circuit_test_cfg_reg,
		aw20144->ignore_open_circuit_test_cfg_size);
	if (ret < 0) {
		dev_err(aw20144->dev,
			"%s: parse ignore_open_circuit_test_cfg_reg err, ret = %d\n", __func__, ret);
	}

	ret = of_property_read_u32_array(np, "ignore_open_circuit_test_cfg_val",
		(u32 *)aw20144->ignore_open_circuit_test_cfg_val,
		aw20144->ignore_open_circuit_test_cfg_size);
	if (ret < 0) {
		dev_err(aw20144->dev,
			"%s: parse ignore_open_circuit_test_cfg_val err, ret = %d\n", __func__, ret);
	}

	aw20144->effect_bin = of_property_read_bool(np, "aw20144,effect-bin");
	if (aw20144->effect_bin)
		pr_info("%s: led effect use bin\n", __func__);

	return 0;
}

static int aw20144_i2c_dev = 0;
static struct class *aw20144_i2c_class;

static int i2cdev_open(struct inode *inode, struct file *file)
{
	file->private_data = paw20144->client;
	return 0;
}

static int i2cdev_release(struct inode *inode, struct file *file)
{
	return 0;
}

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
		/* memdup_user allocates with GFP_KERNEL, so DMA is ok */
		msgs[i].flags |= I2C_M_DMA_SAFE;

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

static long aw20144_i2cdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct i2c_client *client = file->private_data;

	dev_dbg(&client->adapter->dev, "ioctl, cmd=0x%02x, arg=0x%02lx\n",
		cmd, arg);

	switch (cmd) {
	case I2C_RDWR: {
		struct i2c_rdwr_ioctl_data rdwr_arg;
		struct i2c_msg *rdwr_pa;

		if (copy_from_user(&rdwr_arg,
				   (struct i2c_rdwr_ioctl_data __user *)arg,
				   sizeof(rdwr_arg)))
			return -EFAULT;

		if (!rdwr_arg.msgs || rdwr_arg.nmsgs == 0)
			return -EINVAL;

		/*
		 * Put an arbitrary limit on the number of messages that can
		 * be sent at once
		 */
		if (rdwr_arg.nmsgs > I2C_RDWR_IOCTL_MAX_MSGS)
			return -EINVAL;

		rdwr_pa = memdup_user(rdwr_arg.msgs,
				      rdwr_arg.nmsgs * sizeof(struct i2c_msg));
		if (IS_ERR(rdwr_pa))
			return PTR_ERR(rdwr_pa);

		return i2cdev_ioctl_rdwr(client, rdwr_arg.nmsgs, rdwr_pa);
	}

	case I2C_RETRIES:
		if (arg > INT_MAX)
			return -EINVAL;

		client->adapter->retries = arg;
		break;
	case I2C_TIMEOUT:
		if (arg > INT_MAX)
			return -EINVAL;

		/* For historical reasons, user-space sets the timeout
		 * value in units of 10 ms.
		 */
		client->adapter->timeout = msecs_to_jiffies(arg * 10);
		break;
	default:
		/* NOTE:  returning a fault code here could cause trouble
		 * in buggy userspace code.  Some old kernel bugs returned
		 * zero in this case, and userspace code might accidentally
		 * have depended on that bug.
		 */
		return -ENOTTY;
	}
	return 0;
}

static struct file_operations aw20144_i2c_ops = {
    .owner	 = THIS_MODULE,
    .read    = NULL,
    .write   = NULL,
    .open    = i2cdev_open,
    .release = i2cdev_release,
    .unlocked_ioctl = aw20144_i2cdev_ioctl,
};

static int aw20144_i2c_dev_init(void)
{
    aw20144_i2c_dev = register_chrdev(0,"aw20144_i2c",&aw20144_i2c_ops);
    if(aw20144_i2c_dev < 0) {
        return -1;
    }

    aw20144_i2c_class = class_create(THIS_MODULE, "aw20144_i2c");
    if(IS_ERR(aw20144_i2c_class)) {
        unregister_chrdev(aw20144_i2c_dev,"aw20144_i2c");
        return -1;
    }
    device_create(aw20144_i2c_class, NULL, MKDEV(aw20144_i2c_dev, 0),NULL, "aw20144_i2c");

    return 0;
}

#if 1
static const struct tc_led_ops leds_aw20144_ops = {
	.store_tran_led_cmd = aw20144_store_tran_led_cmd,
	.show_tran_led_cmd = aw20144_show_tran_led_cmd,
	.store_tran_led_check = aw20144_store_tran_led_check,
	.show_tran_led_check = aw20144_show_tran_led_check,
};
#endif

/*******************************************************************************
 *
 * i2c driver probe
 *
 ******************************************************************************/

static int aw20144_i2c_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct aw20144 *aw20144;
	struct device_node *np = client->dev.of_node;
	int ret = -1;

	pr_info("enter %s\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "%s: check i2c error\n", __func__);
		return -ENODEV;
	}

	aw20144 = devm_kzalloc(&client->dev,
				sizeof(struct aw20144), GFP_KERNEL);
	if (aw20144 == NULL) {
		ret = -ENOMEM;
		goto err_devm_kzalloc;
	}

	aw20144->cdev.name = AW20144_I2C_NAME;
	aw20144->client = client;
	aw20144->dev = &client->dev;
	paw20144 = aw20144;
	aw20144->test_result = 0;

	/* be used in aw20144_i2c_remove */
	i2c_set_clientdata(client, aw20144);

	/* parse device tree */
	if (np) {
		ret = aw20144_parse_dts(aw20144, np);
		if (ret) {
			dev_err(&client->dev,
				"%s: parse dts failed\n", __func__);
			goto err_parse_dts;
		} else {
			pr_info("%s: parse dts successful\n", __func__);
		}
	} else {
		dev_err(&client->dev, "%s: np is NULL\n", __func__);
		goto err_np_null;
	}

	/* init enable gpio */
	if (gpio_is_valid(aw20144->enable_gpio)) {
		ret = devm_gpio_request_one(&client->dev,
						aw20144->enable_gpio,
						GPIOF_OUT_INIT_LOW,
						"aw20144_enable_gpio");
		if (ret) {
			dev_err(&client->dev,
				"%s: gpio request failed\n", __func__);
			goto err_gpio_request;
		}
	}

	/* read chip id */
	ret = aw20144_read_chipid(aw20144);
	if (ret < 0) {
		dev_err(&client->dev, "%s: read chipid error\n", __func__);
		goto err_read_chipid;
	}

	aw20144_led_init(aw20144);

	INIT_WORK(&aw20144->brightness_work, aw20144_brightness_work);
	aw20144->cdev.brightness_set = aw20144_set_brightness;

	ret = led_classdev_register(aw20144->dev, &aw20144->cdev);
	if (ret) {
		dev_err(aw20144->dev,
		"%s: classdev register failed, ret = %d\n", __func__, ret);
		goto err_register_class;
	} else {
		pr_info("%s: classdev register successful\n", __func__);
	}

	ret = sysfs_create_group(&aw20144->cdev.dev->kobj,
						&aw20144_attribute_group);
	if (ret) {
		dev_err(aw20144->dev,
		"%s: sysfs creat group failed, ret = %d\n", __func__, ret);
		goto error_create_group;
	} else {
		pr_info("%s: sysfs creat group successful\n", __func__);
	}
#if 1
	aw20144->led_dev = tc_led_device_register("aw20144", aw20144->dev, aw20144, &leds_aw20144_ops, NULL);
#else
	ret = device_create_file(aw20144->dev, &dev_attr_tran_led_cmd);
	ret = device_create_file(aw20144->dev, &dev_attr_tran_led_check);
#endif
	aw20144_i2c_dev_init();
	return 0;

error_create_group:
	led_classdev_unregister(&aw20144->cdev);
err_register_class:
err_read_chipid:
err_gpio_request:
err_np_null:
err_parse_dts:
	devm_kfree(&client->dev, aw20144);
err_devm_kzalloc:
	return ret;
}

/*******************************************************************************
 *
 * i2c driver remove
 *
 ******************************************************************************/

static void aw20144_i2c_remove(struct i2c_client *client)
{
	struct aw20144 *aw20144 = i2c_get_clientdata(client);

	pr_info("enter %s\n", __func__);

	devm_kfree(&client->dev, aw20144);
}

static const struct i2c_device_id aw20144_i2c_id[] = {
	{AW20144_I2C_NAME, 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, aw20144_i2c_id);

static const struct of_device_id aw20144_dt_match[] = {
	{.compatible = "awinic,aw20144_led"},
	{},
};

static struct i2c_driver aw20144_i2c_driver = {
	.driver = {
		.name = AW20144_I2C_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(aw20144_dt_match),
	},
	.probe = aw20144_i2c_probe,
	.remove = aw20144_i2c_remove,
	.id_table = aw20144_i2c_id,
};

static int __init aw20144_i2c_init(void)
{
	int ret = -1;

	pr_info("driver version %s\n", AW20144_DRIVER_VERSION);

	ret = i2c_add_driver(&aw20144_i2c_driver);
	if (ret) {
		pr_err("add aw20144 driver failed\n");
		return ret;
	}

	return 0;
}

module_init(aw20144_i2c_init);

static void __exit aw20144_i2c_exit(void)
{
	i2c_del_driver(&aw20144_i2c_driver);
}

module_exit(aw20144_i2c_exit);

MODULE_DESCRIPTION("AW20144 LED Driver");
MODULE_LICENSE("GPL v2");

