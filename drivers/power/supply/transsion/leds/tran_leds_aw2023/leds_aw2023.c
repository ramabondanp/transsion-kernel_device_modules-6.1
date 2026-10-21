// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 Transsion Inc.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/errno.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/leds.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/dma-mapping.h>
#include <linux/hrtimer.h>
#include <linux/fb.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/kthread.h>	/* For Kthread_run */
#include <linux/alarmtimer.h>
#include <linux/mutex.h>
#include "leds_aw2023.h"
#include "tc_led_class.h"

#define AW2023_DRIVER_VERSION "V1.0.2"

/* register address */
#define AW2023_REG_RESET					0x00
#define AW2023_REG_GCR						0x01
#define AW2023_REG_STATUS					0x02
#define AW2023_REG_PATST					0x03
#define AW2023_REG_CUR						0x04
#define AW2023_REG_LCTR					0x30
#define AW2023_REG_LCFG0					0x31
#define AW2023_REG_LCFG1					0x32
#define AW2023_REG_LCFG2					0x33
#define AW2023_REG_PWM0						0x34
#define AW2023_REG_PWM1						0x35
#define AW2023_REG_PWM2						0x36
#define AW2023_REG_LED0T0					0x37
#define AW2023_REG_LED0T1					0x38
#define AW2023_REG_LED0T2					0x39
#define AW2023_REG_LED1T0					0x3A
#define AW2023_REG_LED1T1					0x3B
#define AW2023_REG_LED1T2					0x3C
#define AW2023_REG_LED2T0					0x3D
#define AW2023_REG_LED2T1					0x3E
#define AW2023_REG_LED2T2					0x3F

/* register bits */
#define AW2023_CHIPID						0x09
#define AW2023_RESET_MASK					0x55
#define AW2023_CHIP_DISABLE_MASK			0x00
#define AW2023_CHIP_ENABLE_MASK				0x01
#define AW2023_LED_BREATH_MODE_MASK			0x10
#define AW2023_LED_MANUAL_MODE_MASK			0x00
#define AW2023_LED_BREATHE_PWM_MASK			0xFF
#define AW2023_LED_MANUAL_PWM_MASK			0xFF
#define AW2023_LED_FADEIN_MODE_MASK			0x20
#define AW2023_LED_FADEOUT_MODE_MASK		0x40
#define Delay_time	0x00


#define MAX_RISE_TIME_MS					15
#define MAX_HOLD_TIME_MS					15
#define MAX_FALL_TIME_MS					15
#define MAX_OFF_TIME_MS						15



/* aw2023 register read/write access*/
#define REG_NONE_ACCESS						0
#define REG_RD_ACCESS						1 << 0
#define REG_WR_ACCESS						1 << 1
#define AW2023_REG_MAX						0x7F



struct aw2023_led {
	struct i2c_client *client;
	struct led_classdev cdev;
	struct device *dev;
	int imax;
	//int led_current;
	int rise_time_ms;
	int hold_time_ms;
	int fall_time_ms;
	int off_time_ms;
	int led_mode;
	int led_color;
	int brightness_value;
	int brightness_count;
	struct work_struct brightness_work;
	struct mutex lock;
	int id;
	struct tc_led_device *led_dev;
};

struct aw2023_led *led_array;

#define GETARRAYNUM(array) (ARRAY_SIZE(array))

const  unsigned int TIME_OF_PATTERN[] = {
	0, 130, 260, 380, 510, 770, 1040, 1600, 2100, 2600, 3100, 4200, 5200, 6200, 7300, 8300
};

static int findClosest(const unsigned int *pList, unsigned int number, unsigned int level)
{
    int closest = 8300;
    int diff = 8300;
	int currentDiff = 0;
	int i = 0;

	for (i = 0; i < number; i++) {
		currentDiff = abs(pList[i] - level);
		if (currentDiff < diff) {
			diff = currentDiff;
			closest = pList[i];
		}
	}

	for (i = 0; i < number; i++) {
		if (closest == pList[i])
			return i;
	}
	return 0;
}
static int aw2023_write(struct aw2023_led *led, u8 reg, u8 val)
{
	int ret = -EINVAL, retry_times = 0;

	do {
		ret = i2c_smbus_write_byte_data(led->client, reg, val);
		retry_times ++;
		if(retry_times == 5)
			break;
	}while (ret < 0);
	mdelay(1);

	return ret;    
}

static int aw2023_read(struct aw2023_led *led, u8 reg, u8 *val)
{
	int ret = -EINVAL, retry_times = 0;

	do{
		ret = i2c_smbus_read_byte_data(led->client, reg);
		retry_times ++;
		if(retry_times == 5)
			break;
	}while (ret < 0);
	if (ret < 0)
		return ret;

	*val = ret;
	return 0;
}

static ssize_t aw2023_show_registers(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
    struct aw2023_led *led = dev_get_drvdata(dev);
    u8 addr;
    u8 val;
    int len;
    int idx = 0;
    int ret;

    idx = snprintf(buf, PAGE_SIZE, "%s:\n", "aw2023");
    for (addr = 0x0; addr <= 0x3E; addr++) {
        ret = aw2023_read(led, addr, &val);
        if (ret == 0) {
            len = snprintf(buf + idx, PAGE_SIZE - idx,
                           "Reg[%.2X] = 0x%.2x\n", addr, val);
            idx += len;
        } else {
            dev_err(dev, "Failed to read register 0x%.2X\n", addr);
        }
    }

    return idx;
}

static ssize_t aw2023_store_register(struct device *dev,
                                     struct device_attribute *attr, const char *buf, size_t count)
{
    struct aw2023_led *led = dev_get_drvdata(dev);
    int ret;
    unsigned int reg;
    unsigned int val;

    ret = sscanf(buf, "%x %x", &reg, &val);
    if (ret == 2) {
        ret = aw2023_write(led, reg, val);
        if (ret) {
            dev_err(dev, "Failed to write register 0x%.2X\n", reg);
            return ret;
        }
    } else {
        dev_err(dev, "Invalid input format\n");
        return -EINVAL;
    }

    return count;
}
static DEVICE_ATTR(aw2023_registers_debug, 0664, aw2023_show_registers, aw2023_store_register);

static void aw2023_soft_reset(struct aw2023_led *led)
{
	aw2023_write(led, AW2023_REG_RESET, AW2023_RESET_MASK);
	msleep(5);
}

static void aw2023_brightness_work(struct work_struct *work)
{
	struct aw2023_led *led = container_of(work, struct aw2023_led,
					brightness_work);
	u8 val;
	int i = 0;

	pr_info("func=%s,brightness=%d\n",__func__,led->cdev.brightness);
	mutex_lock(&led->lock);

	/* enable aw2023 if disabled */
	aw2023_read(led, AW2023_REG_GCR, &val);
	if (!(val&AW2023_CHIP_ENABLE_MASK)) {
		aw2023_write(led, AW2023_REG_GCR, AW2023_CHIP_ENABLE_MASK);
		msleep(2);
	}

	if (led->cdev.brightness > 0) {
		if (led->cdev.brightness > led->cdev.max_brightness)
			led->cdev.brightness = led->cdev.max_brightness;
		switch (led->led_color) {
			case COLOR_RED:
			case COLOR_GREEN:
			case COLOR_BLUE:
				led->id = led->led_color;
				aw2023_write(led, AW2023_REG_PWM0 + led->id, led->cdev.brightness);
				aw2023_write(led, AW2023_REG_LCFG0 + led->id, led->imax);
				aw2023_write(led, AW2023_REG_CUR + led->id, led->imax);
				aw2023_write(led, AW2023_REG_GCR, 0x1);
				aw2023_write(led, AW2023_REG_LCTR, (1 << led->id)); //0x30 BIT2:LED2 BIT1:LED1 BIT0:LED0
				break;
			case COLOR_YELLOW: //R+G
				for (i=0;i<2;i++) {
					led->id = i;
					aw2023_write(led, AW2023_REG_PWM0 + led->id, led->cdev.brightness);
					aw2023_write(led, AW2023_REG_CUR + led->id, led->imax);
					aw2023_write(led, AW2023_REG_LCFG0 + led->id, led->imax);
				}
				aw2023_write(led, AW2023_REG_GCR, 0x1);
				aw2023_write(led, AW2023_REG_LCTR, 0x03); //0x30 BIT2:LED2 BIT1:LED1 BIT0:LED0
				break;
			case COLOR_PURPLE: //zi r+b
				for (i=0;i<2;i++) {
					led->id = 2*i;
					aw2023_write(led, AW2023_REG_PWM0 + led->id, led->cdev.brightness);
					aw2023_write(led, AW2023_REG_CUR + led->id, led->imax);
					aw2023_write(led, AW2023_REG_LCFG0 + led->id, led->imax);
				}
				aw2023_write(led, AW2023_REG_GCR, 0x1);
				aw2023_write(led, AW2023_REG_LCTR, 0X05); //0x30 BIT2:LED2(G) BIT1:LED1(G) BIT0:LED0(R)
				break;
			case COLOR_CYAN: //qing g+b
				for (i=1;i<3;i++) {
					led->id = i;
					aw2023_write(led, AW2023_REG_PWM0 + led->id, led->cdev.brightness);
					aw2023_write(led, AW2023_REG_CUR + led->id, led->imax);
					aw2023_write(led, AW2023_REG_LCFG0 + led->id, led->imax);
				}
				aw2023_write(led, AW2023_REG_GCR, 0x1);
				aw2023_write(led, AW2023_REG_LCTR, 0X06); //0x30 BIT2:LED2(G) BIT1:LED1(G) BIT0:LED0(R)	
				break;
			case COLOR_WHITE: //bau r+g+b
				for (i=0;i<3;i++) {
					led->id = i;
					aw2023_write(led, AW2023_REG_PWM0 + led->id, led->cdev.brightness);
					aw2023_write(led, AW2023_REG_CUR + led->id, led->imax);
					aw2023_write(led, AW2023_REG_LCFG0 + led->id, led->imax);
				}
				aw2023_write(led, AW2023_REG_GCR, 0x1);
				aw2023_write(led, AW2023_REG_LCTR, 0X07); //0x30 BIT2:LED2(G) BIT1:LED1(G) BIT0:LED0(R)	
				break;
			default:
				aw2023_write(led, AW2023_REG_GCR, 0x00);
				aw2023_write(led, AW2023_REG_LCTR, 0x0); //0x30 bit2:LED3	bit1:LED2 bit0:LED1	
				break;
		}			
	} else {
		aw2023_write(led, AW2023_REG_GCR, 0x0);
		aw2023_write(led, AW2023_REG_LCTR, 0x0);
	}

	/*
	* If value in AW_REG_LED_ENABLE is 0, it means the RGB leds are
	* all off. So we need to power it off.
	*/
	aw2023_read(led, AW2023_REG_LCTR, &val);
	if (val == 0) {
		aw2023_write(led, AW2023_REG_GCR, AW2023_CHIP_DISABLE_MASK);
	
			mutex_unlock(&led->lock);
			return;
	}

	mutex_unlock(&led->lock);
}

static void aw2023_led_time_regester(struct aw2023_led *led)
{
	int size = GETARRAYNUM(TIME_OF_PATTERN);
	led->rise_time_ms = findClosest(TIME_OF_PATTERN, size, led->rise_time_ms);
	led->fall_time_ms = findClosest(TIME_OF_PATTERN, size, led->fall_time_ms);
	led->hold_time_ms = findClosest(TIME_OF_PATTERN, size, led->hold_time_ms);
	led->off_time_ms = findClosest(TIME_OF_PATTERN, size, led->off_time_ms);
}

static void aw2023_led_breathe_set(struct aw2023_led *led)
{
	u8 val;
	int i=0;

	pr_info("func=%s,led->id=%d,imax=%d,brightness_value=%d,rise_time_ms=%d,fall_time_ms=%d,brightness_count=%d\n",
		__func__,led->id,led->imax,led->brightness_value,led->rise_time_ms,led->fall_time_ms,led->brightness_count);
	/* enable regulators if they are disabled */
	/* enable aw2023 if disabled */
	aw2023_read(led, AW2023_REG_GCR, &val);
	if (!(val&0x01)) {
		aw2023_write(led, AW2023_REG_GCR, AW2023_CHIP_ENABLE_MASK);
	}

	led->cdev.brightness = led->brightness_value ? led->cdev.max_brightness : 0;
	aw2023_led_time_regester(led);
	if (led->brightness_value > 0) {
		switch (led->led_color) {
			case COLOR_RED:
			case COLOR_GREEN:
			case COLOR_BLUE:
				led->id = led->led_color;
				aw2023_write(led, AW2023_REG_PWM0 + led->id, led->brightness_value);	
				aw2023_write(led, AW2023_REG_LCFG0 + led->id , (led->imax | 1 <<4));
				aw2023_write(led, AW2023_REG_CUR + led->id, led->imax);
				aw2023_write(led, AW2023_REG_LED0T0 + led->id*3,(led->rise_time_ms << 4 | led->hold_time_ms));
				aw2023_write(led, AW2023_REG_LED0T1 + led->id*3, (led->fall_time_ms << 4 | led->off_time_ms));
				aw2023_write(led, AW2023_REG_LED0T2 + led->id*3, Delay_time<<4 | led->brightness_count);

				aw2023_write(led, AW2023_REG_GCR, 0x1);
				aw2023_write(led, AW2023_REG_LCTR, (1 << led->id)); //0x30 BIT2:led2  BIT1:LED1  BIT0:LED0
				break;
			case COLOR_YELLOW: //huang r+g
				for (i=0; i<2;i++) {
					led->id = i;
					aw2023_write(led, AW2023_REG_PWM0 + led->id, led->brightness_value);	
					aw2023_write(led, AW2023_REG_LCFG0 + led->id , (led->imax | 1 <<4));
					aw2023_write(led, AW2023_REG_CUR + led->id, led->imax);
					aw2023_write(led, AW2023_REG_LED0T0 + led->id*3,(led->rise_time_ms << 4 | led->hold_time_ms));
					aw2023_write(led, AW2023_REG_LED0T1 + led->id*3, (led->fall_time_ms << 4 | led->off_time_ms));
					aw2023_write(led, AW2023_REG_LED0T2 + led->id*3, Delay_time<<4 | led->brightness_count);			
				}
				aw2023_write(led, AW2023_REG_GCR, 0x1);
				aw2023_write(led, AW2023_REG_LCTR, 0x03); //0x30 BIT2:led2  BIT1:LED1  BIT0:LED0
				break;
			case COLOR_PURPLE: //zi r+b
				for (i=0; i<2;i++) {
					led->id = 2*i;
					aw2023_write(led, AW2023_REG_PWM0 + led->id, led->brightness_value);	
					aw2023_write(led, AW2023_REG_LCFG0 + led->id , (led->imax | 1 <<4));
					aw2023_write(led, AW2023_REG_CUR + led->id, led->imax);
					aw2023_write(led, AW2023_REG_LED0T0 + led->id*3,(led->rise_time_ms << 4 | led->hold_time_ms));
					aw2023_write(led, AW2023_REG_LED0T1 + led->id*3, (led->fall_time_ms << 4 | led->off_time_ms));
					aw2023_write(led, AW2023_REG_LED0T2 + led->id*3, Delay_time<<4 | led->brightness_count);			
				}
				aw2023_write(led, AW2023_REG_GCR, 0x1);
				aw2023_write(led, AW2023_REG_LCTR, 0x05); //0x30 BIT2:led2  BIT1:LED1  BIT0:LED0
				break;
			case COLOR_CYAN:	//qing g+b
				for (i=1; i<3;i++) {
					led->id = i;
					aw2023_write(led, AW2023_REG_PWM0 + led->id, led->brightness_value);	
					aw2023_write(led, AW2023_REG_LCFG0 + led->id , (led->imax | 1 <<4));
					aw2023_write(led, AW2023_REG_CUR + led->id, led->imax);
					aw2023_write(led, AW2023_REG_LED0T0 + led->id*3,(led->rise_time_ms << 4 | led->hold_time_ms));
					aw2023_write(led, AW2023_REG_LED0T1 + led->id*3, (led->fall_time_ms << 4 | led->off_time_ms));
					aw2023_write(led, AW2023_REG_LED0T2 + led->id*3, Delay_time<<4 | led->brightness_count);			
				}
				aw2023_write(led, AW2023_REG_GCR, 0x1);
				aw2023_write(led, AW2023_REG_LCTR, 0x06); //0x30 BIT2:led2  BIT1:LED1  BIT0:LED0
				break;
			case COLOR_WHITE:	//bai r+g+b
				for (i=0; i<3;i++) {
					led->id = i;
					aw2023_write(led, AW2023_REG_PWM0 + led->id, led->brightness_value);	
					aw2023_write(led, AW2023_REG_LCFG0 + led->id , (led->imax | 1 <<4));
					aw2023_write(led, AW2023_REG_CUR + led->id, led->imax);
					aw2023_write(led, AW2023_REG_LED0T0 + led->id*3,(led->rise_time_ms << 4 | led->hold_time_ms));
					aw2023_write(led, AW2023_REG_LED0T1 + led->id*3, (led->fall_time_ms << 4 | led->off_time_ms));
					aw2023_write(led, AW2023_REG_LED0T2 + led->id*3, Delay_time<<4 | led->brightness_count);			
				}
				aw2023_write(led, AW2023_REG_GCR, 0x1);
				aw2023_write(led, AW2023_REG_LCTR, 0x07); //0x30 BIT2:led2  BIT1:LED1  BIT0:LED0
				break;
			default:
				aw2023_write(led, AW2023_REG_GCR, 0x0);
				aw2023_write(led, AW2023_REG_LCTR, 0x0);
				break;
		}
	} else {
		aw2023_write(led, AW2023_REG_LCTR, 0x0);
		aw2023_write(led, AW2023_REG_LCTR, 0x0);
	}

	/*
	* If value in AW_REG_LED_ENABLE is 0, it means the RGB leds are
	* all off. So we need to power it off.
	*/
	aw2023_read(led, AW2023_REG_LCTR, &val);
	if (val == 0) {
		aw2023_write(led, AW2023_REG_GCR, AW2023_CHIP_DISABLE_MASK);
			return;
	}
}

static void aw2023_set_brightness(struct led_classdev *cdev,
				enum led_brightness brightness)
{
	struct aw2023_led *led = container_of(cdev, struct aw2023_led, cdev);

	led->cdev.brightness = brightness;

	schedule_work(&led->brightness_work);
}

static int aw2023_led_work(struct aw2023_led *led)
{
	led->id = led->led_color;
	led->brightness_count = min(led->brightness_count, AW2023_REPEAT_MAX_TIMES);
	pr_info("aw2023 led_mode=%d,led_color=%d,brightness_value=%d,rise_time_ms=%d,hold_time_ms=%d,fall_time_ms=%d,off_time_ms=%d\n",
			led->led_mode, led->led_color,led->brightness_value,
			led->rise_time_ms, led->hold_time_ms, led->fall_time_ms,led->off_time_ms);

	switch (led->led_mode) {
		case AW2023_LED_OFF:
			led->led_mode = 0;
			led->brightness_value = 0;
			aw2023_soft_reset(led);
			break;
		case AW2023_LED_NORMAL:
			aw2023_set_brightness(&led->cdev,led->brightness_value);
			break;
		case AW2023_LED_BLINK:
		case AW2023_LED_BREATHE:
			aw2023_led_breathe_set(led);
			break;
		default:
		
			break;
	}

	return 0;
}

static int aw2023_show_tran_led_cmd(struct tc_led_device *tc_led_dev, char *buf)
{
	struct aw2023_led *led = dev_get_drvdata(&tc_led_dev->dev);

	return sprintf(buf, "%d\n", led->led_mode);
}

static int aw2023_store_tran_led_cmd(struct tc_led_device *tc_led_dev, const char *cmd)
{
	struct aw2023_led *led = dev_get_drvdata(&tc_led_dev->dev);
	int rc;
	int temp[2];

	rc = sscanf(cmd, "%d %d %d %d %d %d\n",
			&led->led_mode,&led->led_color,&led->brightness_value,
			&temp[0], &temp[1], &led->brightness_count);
	if(led->led_mode == AW2023_LED_BLINK){
		led->hold_time_ms = temp[0];
		led->off_time_ms = temp[1];
	}else if(led->led_mode == AW2023_LED_BREATHE){
		led->rise_time_ms = temp[0];
		led->fall_time_ms = temp[1];
	}
	aw2023_led_work(led);
	return rc;
}

static const struct tc_led_ops leds_aw2023_ops = {
	.store_tran_led_cmd = aw2023_store_tran_led_cmd,
	.show_tran_led_cmd = aw2023_show_tran_led_cmd,
};

static int aw2023_check_chipid(struct aw2023_led *led)
{
	u8 val;
	u8 cnt;

	for(cnt = 5; cnt > 0; cnt --)
	{
		aw2023_read(led, AW2023_REG_RESET, &val);
		dev_notice(&led->client->dev,"aw2023 chip id %0x",val);
		if (val == AW2023_CHIPID)
			return 0;
	}
		return -EINVAL;
}

static struct attribute *aw2023_led_attributes[] = {
	NULL,
};

static struct attribute_group aw2023_led_attr_group = {
	.attrs = aw2023_led_attributes
};
static int aw2023_led_err_handle(struct aw2023_led *led_array)
{

	sysfs_remove_group(&led_array->cdev.dev->kobj,&aw2023_led_attr_group);
	led_classdev_unregister(&led_array->cdev);
	cancel_work_sync(&led_array->brightness_work);
	return 0;
}

static int aw2023_parse_led_cdev(struct aw2023_led *led_array,
        struct device_node *np)
{
    struct device_node *temp = NULL;
    int ret = -1;

    pr_info("%s: start\n", __func__);

	if (led_array == NULL)
		return -EINVAL;

    for_each_child_of_node(np, temp) {
        ret = of_property_read_string(temp, "aw2023,name",
            &led_array->cdev.name);
        if (ret < 0) {
            pr_info("Failure reading led name, ret = %d\n", ret);
            goto free_err;
        }
        ret = of_property_read_u32(temp, "aw2023,imax",
            &led_array->imax);
        if (ret < 0) {
            pr_info("Failure reading imax, ret = %d\n", ret);
            led_array->imax = AW2023_IMAX;
        }
        ret = of_property_read_u32(temp, "aw2023,brightness",
            &led_array->cdev.brightness);
        if (ret < 0) {
           pr_info("Failure reading brightness, ret = %d\n", ret);
           led_array->cdev.brightness = AW2023_MAX_BRIGHTNESS;
        }
        ret = of_property_read_u32(temp, "aw2023,max_brightness",
            &led_array->cdev.max_brightness);
        if (ret < 0) {
           pr_info("Failure reading max brightness, ret = %d\n", ret);
            led_array->cdev.brightness = AW2023_MAX_BRIGHTNESS;
        }
    }
	INIT_WORK(&led_array->brightness_work, aw2023_brightness_work);
    led_array->cdev.brightness_set = aw2023_set_brightness;
    ret = led_classdev_register(&led_array->client->dev, &led_array->cdev);
    if (ret) {
        pr_info("unable to register led ret=%d\n", ret);
        goto free_err;
    }

    ret = sysfs_create_group(&led_array->cdev.dev->kobj,
            &aw2023_led_attr_group);
    if (ret) {
        pr_info("led sysfs ret: %d\n", ret);
        goto free_class;
    }
 pr_info("%s: imax=%d,brightness=%d,max_value=%d end\n",
		__func__,led_array->imax,led_array->cdev.brightness,led_array->cdev.max_brightness);
    return 0;

free_class:
 	aw2023_led_err_handle(led_array);
	led_classdev_unregister(&led_array->cdev);
	cancel_work_sync(&led_array->brightness_work);
	return ret;
free_err:
	aw2023_led_err_handle(led_array);
    return ret;
}

static int aw2023_led_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct device_node *node;
	int ret = -EINVAL;
	//struct kobject *kobj;

	node = client->dev.of_node;
	if (node == NULL)
		return -EINVAL;

	led_array = devm_kzalloc(&client->dev,sizeof(struct aw2023_led), GFP_KERNEL);
	if (!led_array)
		return -ENOMEM;

	led_array->client = client;
	led_array->client->dev = client->dev;
	led_array->led_mode = 0;
	led_array->dev = &client->dev;
	led_array->brightness_value = 0;
	led_array->led_color = 0;

	mutex_init(&led_array->lock);

	i2c_set_clientdata(client, led_array);

	ret = aw2023_check_chipid(led_array);
	if (ret) {
		pr_info("Check chip id error\n");
		goto fail_parsed_node;
	}

	/* soft rst */
	aw2023_soft_reset(led_array);
	
	dev_set_drvdata(&client->dev, led_array);

    ret = aw2023_parse_led_cdev(led_array, node);
    if (ret < 0) {
        pr_info("%s error creating led class dev\n", __func__);
        goto fail_parsed_node;
    }

	ret = device_create_file(&client->dev, &dev_attr_aw2023_registers_debug);
#if 0
	kobj = kobject_create_and_add("led", NULL);
	if (!kobj) {
		pr_info("%s:sysfs_create_group fail",__func__);
		goto fail_parsed_node;
	}

	ret = sysfs_create_link(kobj,&client->dev.kobj,"led");
	if(ret<0){
		pr_info("%s : sysfs_create_link failed\n", __func__);
		goto fail_parsed_node;
	}
#endif
	led_array->led_dev = tc_led_device_register("aw2023", led_array->dev, led_array, &leds_aw2023_ops, NULL);
	if (IS_ERR_OR_NULL(led_array->led_dev)) {
		pr_err("%s : register tc led failed!\n", __func__);
		goto fail_parsed_node;
	}

	pr_info("%s: end\n", __func__);
	return 0;

fail_parsed_node:
	mutex_destroy(&led_array->lock);
	devm_kfree(&client->dev, led_array);
	led_array = NULL;
	pr_info("%s: fail\n", __func__);
	return ret;
}

static void aw2023_led_remove(struct i2c_client *client)
{
	struct aw2023_led *led_array = i2c_get_clientdata(client);
	if (IS_ERR_OR_NULL(led_array))
		return;

	mutex_destroy(&led_array->lock);
	devm_kfree(&client->dev, led_array);
	led_array = NULL;
	return;
}

static void aw2023_led_shutdown(struct i2c_client *client)
{
	struct aw2023_led *led_array = i2c_get_clientdata(client);
	
	aw2023_write(led_array, AW2023_REG_GCR, AW2023_CHIP_DISABLE_MASK);
}

static const struct i2c_device_id aw2023_led_id[] = {
	{"aw2023_led", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, aw2023_led_id);

static struct of_device_id aw2023_match_table[] = {
	{ .compatible = "awinic,aw2023_led",},
	{ },
};

static struct i2c_driver aw2023_led_driver = {
	.probe = aw2023_led_probe,
	.remove = aw2023_led_remove,
	.shutdown = aw2023_led_shutdown,
	.driver = {
		.name = "aw2023_led",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(aw2023_match_table),
	},
	.id_table = aw2023_led_id,
};

static int __init aw2023_led_init(void)
{
	pr_info("%s: driver version: %s\n", __func__, AW2023_DRIVER_VERSION);
	return i2c_add_driver(&aw2023_led_driver);
}
module_init(aw2023_led_init);

static void __exit aw2023_led_exit(void)
{
	i2c_del_driver(&aw2023_led_driver);
}
module_exit(aw2023_led_exit);

MODULE_AUTHOR("<liweilei@awinic.com.cn>");
MODULE_DESCRIPTION("AWINIC AW2023 LED driver");
MODULE_LICENSE("GPL v2");
