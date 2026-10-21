
/**
 * Copyright (C) 2015 Transsion Inc
 * This file is used to implement the extended functions(such as power enabling/disabling, spi clk enabling/disabling, etc.) for fingerprint device driver
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/compat.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <linux/cdev.h>
#include <linux/jiffies.h>
#include <linux/types.h>
#include <linux/spi/spi.h>
#ifdef CONFIG_PM_SLEEP
#include <linux/pm_wakeup.h>
#else
#include <linux/wakelock.h>
#endif

#include <linux/proc_fs.h>
#include <linux/of_irq.h>
#include <linux/sched.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/cdev.h>
#include <linux/clk.h>
#include <asm/uaccess.h>
#include <linux/kthread.h>
#include <linux/of_platform.h>

#define CONFIG_SPI_MT65XX_KERNEL510 y

#if !defined(CONFIG_SPI_MT65XX_KERNEL510)
#include <mt_spi.h>
#include <mt_spi_hal.h>
#else
#include "tran_spi.h"
#endif

#ifdef CONFIG_TRAN_FP_DRV_MODE_SPI
#ifdef CONFIG_SPI_MT65XX_KERNEL510
#include <linux/platform_data/spi-mt65xx.h>
#endif
#endif

#include <internal.h>
#include "tran_fp_ext.h"
#include <linux/of.h>
#include "mtk_disp_notify.h"
#include "transsion_fp_cust.h"


struct attribute_group tran_fp_attribute_group = {NULL};
extern struct tran_fp_data	* g_tran_fp_datap;
extern void mt_spi_enable_master_clk(struct spi_device *spidev);
extern void mt_spi_disable_master_clk(struct spi_device *spidev);

static int open_clk_count = 0, close_clk_count = 1;

static int pid;
static int tran_fp_reset_sensor(struct tran_fp_data *tran_fp_dev, u32 inter_delay, u32 post_delay);
static int tran_fp_open_clock(struct tran_fp_data *tran_fp_datap);
static int tran_fp_close_clock(struct tran_fp_data *tran_fp_datap);
static long tran_fp_pinctrl_init(struct tran_fp_data * tran_fp_dev);

struct mt_chip_conf tran_chip_config = {
	.setuptime =10,//6, //10,//15,//10 ,//3,
	.holdtime =10, //6,//10,//15,//10,//3,
	.high_time = 30,//4,//6,//8,//12, //25,//8,      //10--6m   15--4m   20--3m  30--2m  [ 60--1m 120--0.5m  300--0.2m]
	.low_time = 30,//4,//6,//8,//12,//25,//8,
	.cs_idletime = 2,//30,// 60,//100,//12,
	.ulthgh_thrsh = 0,

	.rx_mlsb = SPI_MSB,
	.tx_mlsb = SPI_MSB,
	.tx_endian = 0,
	.rx_endian = 0,

	.cpol = SPI_CPOL_0,
	.cpha = SPI_CPHA_0,

	.com_mod = DMA_TRANSFER,
	.pause = 0,
	.finish_intr = 1,
	.deassert = 0,
	.ulthigh = 0,
	.tckdly = 0,
};

/**
 * fingerprint pwoer supply have three ways now.
 *
 *only regulator power supply ,tran_fp_dev->fp_regulator_support = 1，Need to set voltage.
 *
 *only GPIO power supply，tran_fp_dev->fp_regulator_support = 2，only set gpio 0 or 1.
 *
 *GPIO & regulator power supply,tran_fp_dev->fp_regulator_support = 3，only set gpio 0 or 1.
 */


static int tran_fp_power_init(struct tran_fp_data *tran_fp_dev, bool on)
{
	int rc = 0;
	if(tran_fp_dev->fp_regulator_support == POWER_SUPORT_REGULATOR) {
		const char buf[FP_NAME_SIZE];
		tran_fp_dev->always_on = buf;
		if (!on) {
			if (tran_fp_dev->power_is_init == 1) {
				if (regulator_count_voltages(tran_fp_dev->vdd) > 0)
					regulator_set_voltage(tran_fp_dev->vdd, tran_fp_dev->min_uV, tran_fp_dev->max_uV);
				regulator_put(tran_fp_dev->vdd);
				tran_fp_dev->vdd = NULL;
				tran_fp_dev->power_is_init = 0;
			}
		} else {
			if (tran_fp_dev->power_is_init == 0) {
				tran_fp_dev->vdd = regulator_get(&tran_fp_dev->tran_fp_dev->dev, "vmch");
				if (IS_ERR(tran_fp_dev->vdd)) {
					rc = PTR_ERR(tran_fp_dev->vdd);
					TRAN_FP_ERROR("Regulator get failed vdd rc=%d\n", rc);
					return rc;
				} else {
					if ((rc = of_property_read_u32_array(tran_fp_dev->vdd->rdev->dev.of_node, "regulator-min-microvolt", &(tran_fp_dev->min_uV),1))) {
						TRAN_FP_ERROR("get regulator-min-microvolt failed, rc=%d\n", rc);
						return rc;
					}
					if ((rc = of_property_read_u32_array(tran_fp_dev->vdd->rdev->dev.of_node, "regulator-max-microvolt", &(tran_fp_dev->max_uV),1))) {
						TRAN_FP_ERROR("get regulator-max-microvolt failed, rc=%d\n", rc);
						return rc;
					}
					TRAN_FP_INFO("regulator-min-microvolt = %d uv, regulator-max-microvolt = %d uv", tran_fp_dev->min_uV, tran_fp_dev->max_uV);
					if (of_property_read_string(tran_fp_dev->vdd->rdev->dev.of_node, "regulator-always-on", &(tran_fp_dev->always_on)) == 0)
						TRAN_FP_INFO("regulator-always-on = %s", tran_fp_dev->always_on);
					else
						TRAN_FP_ERROR("regulator-always-on is not set or its property length is too long");
					tran_fp_dev->always_on = NULL;
					if (regulator_count_voltages(tran_fp_dev->vdd) > 0) {
						rc = regulator_set_voltage(tran_fp_dev->vdd, tran_fp_dev->max_uV, tran_fp_dev->max_uV);
						if (rc) {
							TRAN_FP_ERROR("Regulator set failed vdd rc=%d\n",rc);
							goto reg_vdd_put;
						}
					}
					tran_fp_dev->power_is_init = 1;
				}
			}
		}
		return rc;

reg_vdd_put:
		regulator_put(tran_fp_dev->vdd);
	}
	else {
		if (on) {
			if (tran_fp_dev->power_is_init == 0) {
#if (!defined(CONFIG_MTK_GPIO) || defined(CONFIG_MTK_GPIOLIB_STAND))
				TRAN_FP_INFO("prase tran,gpio_pwr-std \n");
				tran_fp_dev->hw_pwr_gpio = of_get_named_gpio(tran_fp_dev->tran_fp_dev->dev.of_node,"tran,gpio_pwr-std",0);

#else
				TRAN_FP_INFO("prase tran,gpio_pwr ");
				if ((rc = of_property_read_u32_array(tran_fp_dev->tran_fp_dev->dev.of_node, "tran,gpio_pwr", &(tran_fp_dev->hw_pwr_gpio),1))) {
					TRAN_FP_ERROR("get tran,gpio_pwr failed, rc=%d\n", rc);
					return rc;
				}
#endif
				if(tran_fp_dev->fp_regulator_support == POWER_SUPORT_REGULATOR_GPIO)//when fingerprint vdd is GPIO and used vmc power,fp_regulator_support == 3
				{
					if (tran_fp_dev->power_is_init == 0){
						tran_fp_dev->vdd = regulator_get(&tran_fp_dev->tran_fp_dev->dev, "vqmmc");
						if (IS_ERR(tran_fp_dev->vdd)) {
							rc = PTR_ERR(tran_fp_dev->vdd);
							TRAN_FP_ERROR("vqmcc get failed vdd rc=%d\n", rc);
							return rc;
						}
						if ((rc = of_property_read_u32_array(tran_fp_dev->vdd->rdev->dev.of_node, "regulator-min-microvolt", &(tran_fp_dev->min_uV),1))) {
							TRAN_FP_ERROR("get regulator-min-microvolt failed, rc=%d\n", rc);
							return rc;
						}
						if ((rc = of_property_read_u32_array(tran_fp_dev->vdd->rdev->dev.of_node, "regulator-max-microvolt", &(tran_fp_dev->max_uV),1))) {
							TRAN_FP_ERROR("get regulator-max-microvolt failed, rc=%d\n", rc);
							return rc;
						}
						TRAN_FP_INFO("vqmmc-regulator-min-microvolt = %d uv, vqmmc-regulator-max-microvolt = %d uv", tran_fp_dev->min_uV, tran_fp_dev->max_uV);
						if (regulator_count_voltages(tran_fp_dev->vdd) > 0) {
							rc = regulator_set_voltage(tran_fp_dev->vdd, tran_fp_dev->min_uV, tran_fp_dev->max_uV);//Not setting a specific voltage, only setting a range, because VMC power supply may be used for other modules
							if (rc) {
								TRAN_FP_ERROR("Regulator set failed vdd rc=%d\n",rc);
								goto reg_vdd_put;
							}
						}
					}
				}
				TRAN_FP_INFO("tran_fp_dev->hw_pwr_gpio= %d",tran_fp_dev->hw_pwr_gpio);
				if (gpio_is_valid(tran_fp_dev->hw_pwr_gpio)) {
					rc = devm_gpio_request(&tran_fp_dev->tran_fp_dev->dev,tran_fp_dev->hw_pwr_gpio,"tran_pwr_gpio");
					if (rc) {
						TRAN_FP_ERROR("devm_gpio_request pwr_gpio failed! rc = %d",rc);
						return -EFAULT;
					} else {
						tran_fp_dev->power_is_init = 1;
						TRAN_FP_INFO("devm_gpio_request pwr_gpio success!");
					}
				} else {
					TRAN_FP_ERROR("the state of pwr_gpio is invalid");
					return -EINVAL;
				}
			}
		} else {
			if (tran_fp_dev->power_is_init == 1) {
				if (gpio_is_valid(tran_fp_dev->hw_pwr_gpio)) {
					//devm_gpio_free(&tran_fp_dev->tran_fp_dev->dev,tran_fp_dev->hw_pwr_gpio);
					tran_fp_dev->power_is_init = 0;
					TRAN_FP_INFO("devm_gpio_free pwr_gpio success!");
				} else {
					TRAN_FP_ERROR("the state of pwr_gpio is invalid!");
					return -EINVAL;
				}
			}
		}
	}
	return rc;
}

static int tran_fp_power_onoff(struct tran_fp_data *tran_fp_datap,bool onoff)
{
	int rc = 0;
	if (tran_fp_datap->power_is_init == 1) {
		if(tran_fp_datap->fp_regulator_support == POWER_SUPORT_REGULATOR) {
			if (onoff) {
				if (tran_fp_datap->power_is_on == 0) {
					rc = regulator_enable(tran_fp_datap->vdd);
					mdelay(10);
					tran_fp_reset_gpio_high(tran_fp_datap);
					if (rc) {
						TRAN_FP_ERROR("Regulator vdd enable failed rc=%d\n", rc);
						goto fp_power_done;
					}
					tran_fp_datap->power_is_on = 1;
				}
			} else {
				if (tran_fp_datap->power_is_on == 1) {
					tran_fp_reset_gpio_low(tran_fp_datap);
					mdelay(10);
					rc = regulator_disable(tran_fp_datap->vdd);
					if (rc) {
						TRAN_FP_ERROR("Regulator vdd disable failed rc=%d\n", rc);
						goto fp_power_done;
					}
					tran_fp_datap->power_is_on = 0;
				}
			}

		} else {
			if(tran_fp_datap->fp_regulator_support == POWER_SUPORT_REGULATOR_GPIO){
				if (onoff) {
					if (tran_fp_datap->power_is_on == 0) {
						rc = regulator_enable(tran_fp_datap->vdd);
						if (rc) {
							TRAN_FP_ERROR("Regulator vqmmc vdd enable failed rc=%d\n", rc);
							goto fp_power_done;
						}
						//tran_fp_datap->power_is_on = 1;//power_is_on in here don't modify,will be  GPIO modify  later.
					}
				} else {
					if (tran_fp_datap->power_is_on == 1) {
						rc = regulator_disable(tran_fp_datap->vdd);
						if (rc) {
							TRAN_FP_ERROR("Regulator vqmmc vdd disable failed rc=%d\n", rc);
							goto fp_power_done;
						}
						//tran_fp_datap->power_is_on = 0;//power_is_on in here don't modify,will be  GPIO modify  later.
					}
				}
			}
			if (onoff) {
				if (tran_fp_datap->power_is_on == 0) {
					gpio_direction_output(tran_fp_datap->hw_pwr_gpio,1);
					mdelay(10);
					tran_fp_reset_gpio_high(tran_fp_datap);
					mdelay(1);
					tran_fp_datap->power_is_on = 1;
				}
			} else {
				if (tran_fp_datap->power_is_on == 1) {
					tran_fp_reset_gpio_low(tran_fp_datap);
					mdelay(10);
					gpio_direction_output(tran_fp_datap->hw_pwr_gpio,0);
					mdelay(1);
					tran_fp_datap->power_is_on = 0;
				}
			}
		}
	}
fp_power_done:
	return rc;
}

int tran_fp_get_power_val(struct tran_fp_data *tran_fp_dev,PWR_VAL_U * cur_val)
{
	int rc = 0;
	if(tran_fp_dev->fp_regulator_support == POWER_SUPORT_REGULATOR) {
		if (IS_ERR_OR_NULL(tran_fp_dev->vdd)) {
			rc = PTR_ERR(tran_fp_dev->vdd);
			TRAN_FP_ERROR("Regulator vdd is invalid,rc=%d\n", rc);
			return rc;
		} else {
			(*cur_val).rgltor_val.vdd_val = regulator_get_voltage(tran_fp_dev->vdd);
			//	(*cur_val).rgltor_val.vio_val = regulator_get_voltage(tran_fp_dev->vio);
			TRAN_FP_INFO("cur vdd_val = %d  uv ",(*cur_val).rgltor_val.vdd_val);
		}
	}
	else {
		if (gpio_is_valid(tran_fp_dev->hw_pwr_gpio)) {
			(*cur_val).gpio_val = gpio_get_value_cansleep(tran_fp_dev->hw_pwr_gpio);
			TRAN_FP_INFO("cur pwr_gpio_val = %d ", (*cur_val).gpio_val);
		} else {
			TRAN_FP_ERROR("the state of pwr_gpio is invalid!");
			return -EINVAL;
		}
	}
	return rc;
}

int tran_fp_parse_dts_file(struct tran_fp_data * tran_fp_dev)
{
#ifdef CONFIG_OF
	int value, ret;
	unsigned char tran_tee_afinity_tmp = 0 , i = 0;
	struct device_node *node = NULL, *np = NULL, *child = NULL;
	const char *str = NULL;

	TRAN_FP_INFO(" from dts pinctrl\n");

	node = of_find_compatible_node(NULL, NULL, "tran_fp");
	//tran_fp_dev->tran_fp_dev.dev->tran_fp_dev = node;
	if (node) {
		of_property_read_u32(node, "netlink-event", &value);
		TRAN_FP_INFO(" get netlink event[%d] from dts\n", value);
		ret = tran_fp_pinctrl_init(tran_fp_dev);
		if (ret) {
			TRAN_FP_ERROR(" tran_fp_pinctrl_init failed");
			return -EFAULT;
		} else
			TRAN_FP_INFO(" tran_fp_pinctrl_init success!\n");
	} else {
		TRAN_FP_ERROR(" device node is null\n");
		return -EINVAL;
	}
    //prase dts for optical fingerprint calibrate spot coordinates
	if(of_property_read_string(node,"fod_location_xy",&str)) {
		strcpy(tran_fp_dev->fod_location_xy,"null");
		TRAN_FP_ERROR("fod_location_xy is null\n");
	} else {
		strcpy(tran_fp_dev->fod_location_xy,str);
		TRAN_FP_INFO("fod_location_xy = %s\n",tran_fp_dev->fod_location_xy);
	}
    if(of_property_read_bool(node, "history-support")) {
        tran_fp_dev->history_support = true;
        TRAN_FP_INFO("set history-support\n");
    }
    else {
        tran_fp_dev->history_support = false;
        TRAN_FP_ERROR("not set history-support\n");
    }
	if(of_property_read_bool(node,"vmch-supply"))
		tran_fp_dev->fp_regulator_support = POWER_SUPORT_REGULATOR;
	else if(of_property_read_bool(node,"tran,gpio_pwr") || of_property_read_bool(node,"tran,gpio_pwr-std"))
		tran_fp_dev->fp_regulator_support = POWER_SUPORT_GPIO;
	else if(of_property_read_bool(node,"vqmmc-supply"))
		tran_fp_dev->fp_regulator_support = POWER_SUPORT_REGULATOR_GPIO;
	else tran_fp_dev->fp_regulator_support = POWER_SUPORT_NONE;
#if (!defined(CONFIG_MTK_GPIO) || defined(CONFIG_MTK_GPIOLIB_STAND))
	tran_fp_dev->reset_gpio = of_get_named_gpio(tran_fp_dev->tran_fp_dev->dev.of_node,"reset-gpio-std",0);
	if (tran_fp_dev->reset_gpio < 0) {
		TRAN_FP_ERROR("%s: get fp RST GPIO failed (%d)", __func__, tran_fp_dev->reset_gpio);
		return -EINVAL;
	}

	tran_fp_dev->irq_gpio = of_get_named_gpio(tran_fp_dev->tran_fp_dev->dev.of_node,"irq-gpio-std",0);
	if (tran_fp_dev->irq_gpio < 0) {
		TRAN_FP_ERROR("%s: get fp IRQ GPIO failed (%d)", __func__, tran_fp_dev->irq_gpio);
		return -EINVAL;
	}
#else
	if ((ret = of_property_read_u32_array(tran_fp_dev->tran_fp_dev->dev.of_node, "reset-gpio", &(tran_fp_dev->reset_gpio),1))) {
		TRAN_FP_ERROR("get reset-gpio failed, ret=%d\n", ret);
		return ret;
	}
	if ((ret = of_property_read_u32_array(tran_fp_dev->tran_fp_dev->dev.of_node, "irq-gpio", &(tran_fp_dev->irq_gpio),1))) {
		TRAN_FP_ERROR("get irq-gpio failed, ret=%d\n", ret);
		return ret;
	}
#endif
	np = of_find_node_by_path("/cpus/cpu-map/cluster0");
	if(np) {
		for_each_child_of_node(np,child) {
			if (of_property_read_bool(child,"cpu"))
				tran_tee_afinity_tmp = ( 1 << i++ );
			tran_fp_dev->tran_tee_afinity |= tran_tee_afinity_tmp;
		}
		tran_fp_dev->tran_tee_afinity = ~tran_fp_dev->tran_tee_afinity;
		TRAN_FP_INFO(" tran_fp_dev->tran_tee_afinity = %x\n",tran_fp_dev->tran_tee_afinity);
	} else TRAN_FP_ERROR(" /cpus/cpu-map/cluster0 node is null\n");
	of_node_put(np);

	TRAN_FP_INFO(" parse dts success!\n");
#endif
	return 0;

}

int tran_init_eint(struct tran_fp_data*tran_fp_dev)
{
	int ret = -EFAULT, irq = 0, debounce = 0;
	unsigned gpio_pin;
	int ints[2];
	struct device_node * dn= of_find_compatible_node(NULL,NULL,"tran_fp");
	if(IS_ERR(dn)){
		ret=PTR_ERR(dn);
		return ret;
	}
	tran_fp_dev->irq_node = dn;
	if(dn){
		irq = irq_of_parse_and_map(dn,0);
		TRAN_FP_INFO("parsed irq = %d\n",irq);
		if (irq < 0) {
			TRAN_FP_ERROR("irq number get error!\n");
			return -EINVAL;
		}
		if ((ret = of_property_read_u32_array(dn,"debounce",ints,ARRAY_SIZE(ints)))) {
			TRAN_FP_ERROR("get debounce failed, ret=%d\n", ret);
			return ret;
		}
		gpio_pin=ints[0];
		debounce=ints[1];
		TRAN_FP_INFO("int gpio = %d,debounce=%d\n",gpio_pin,debounce);
		gpio_set_debounce(ints[0],ints[1]);
	}else{
		TRAN_FP_ERROR("irq node not found !\n");
		return -EINVAL;
	}
	tran_fp_dev->irq = irq;
	return 0;
}

static int tran_fp_reset_sensor(struct tran_fp_data *tran_fp_dev, u32 inter_delay, u32 post_delay)
{
	int rc = 0;
	int value = 2;
	if(tran_fp_dev->tran_pinctrl)
	{
		rc = pinctrl_select_state(tran_fp_dev->tran_pinctrl, tran_fp_dev->pins_reset_low);
		if(rc) {
			TRAN_FP_ERROR("[tran]cannot set suspend pinctrl state\n");
			goto reset_stat_select_fail;
		}
	}
	udelay(inter_delay);
	value = gpio_get_value_cansleep(tran_fp_dev->reset_gpio);
	TRAN_FP_INFO("GPIO %d state is %d\n",tran_fp_dev->reset_gpio,value);
	if(tran_fp_dev->tran_pinctrl)
	{
		rc = pinctrl_select_state(tran_fp_dev->tran_pinctrl, tran_fp_dev->pins_reset_high);
		if(rc) {
			TRAN_FP_ERROR("[tran]cannot set active pinctrl state\n");
			goto reset_stat_select_fail;
		}
	}
	if (post_delay) {
		udelay(post_delay);
	}
	value = gpio_get_value_cansleep(tran_fp_dev->reset_gpio);
	TRAN_FP_INFO("GPIO %d state is %d\n",tran_fp_dev->reset_gpio,value);
reset_stat_select_fail:
	return rc;
}

static int tran_fp_shutdown_sensor(struct tran_fp_data *tran_fp_dev)
{
	int rc = 0;
	int value = 2;
	if(tran_fp_dev->tran_pinctrl)
	{
		rc = pinctrl_select_state(tran_fp_dev->tran_pinctrl, tran_fp_dev->pins_reset_low);
		if(rc)
			TRAN_FP_ERROR("[tran]cannot set suspend pinctrl state\n");
	}
	udelay(2000);
	value = gpio_get_value_cansleep(tran_fp_dev->reset_gpio);
	TRAN_FP_INFO("GPIO %d state is %d\n",tran_fp_dev->reset_gpio,value);
	return rc;
}

void tran_fp_reset_gpio_high(struct tran_fp_data *tran_fp_dev)
{
	int rc = 0;
	int value = 2;
#ifdef CONFIG_OF
	if(tran_fp_dev->tran_pinctrl)
	{
		rc = pinctrl_select_state(tran_fp_dev->tran_pinctrl, tran_fp_dev->pins_reset_high);
		if(rc) {
			TRAN_FP_ERROR("[tran]cannot set active pinctrl state\n");
		}
	}
#endif
	udelay(2000);
	value = gpio_get_value_cansleep(tran_fp_dev->reset_gpio);
	TRAN_FP_INFO("GPIO %d state is %d\n",tran_fp_dev->reset_gpio,value);
}

void tran_fp_reset_gpio_low(struct tran_fp_data *tran_fp_dev)
{
	int rc = 0;
	int value = 2;
#ifdef CONFIG_OF
	if(tran_fp_dev->tran_pinctrl)
	{
		rc = pinctrl_select_state(tran_fp_dev->tran_pinctrl, tran_fp_dev->pins_reset_low);
		if(rc) {
			TRAN_FP_ERROR("[tran]cannot set active pinctrl state\n");
		}
	}
#endif
	udelay(2000);
	value = gpio_get_value_cansleep(tran_fp_dev->reset_gpio);
	TRAN_FP_INFO("GPIO %d state is %d\n",tran_fp_dev->reset_gpio,value);
}

static void tran_fp_enable_irq(struct tran_fp_data *tran_fp_dev)
{
	tran_fp_dev->RcvIRQ = 0;
	if (1 == tran_fp_dev->irq_count) {
		TRAN_FP_INFO(" irq already enabled\n");
	} else {
		enable_irq(tran_fp_dev->irq);
		tran_fp_dev->irq_count = 1;
		TRAN_FP_DEBUG(" enable interrupt!\n");
	}
}

static void tran_fp_disable_irq(struct tran_fp_data *tran_fp_dev)
{
	if (0 == tran_fp_dev->irq_count) {
		TRAN_FP_INFO(" irq already disabled\n");
	} else {
		disable_irq_nosync(tran_fp_dev->irq);
		tran_fp_dev->irq_count = 0;
		TRAN_FP_DEBUG(" disable interrupt!\n");
	}
	tran_fp_dev->RcvIRQ = 0;
}

static long tran_fp_pinctrl_init(struct tran_fp_data * tran_fp_dev)
{
	long ret = 0;
#ifndef CONFIG_TRAN_FP_DRV_MODE_SPI
	tran_fp_dev->clk = devm_clk_get(&tran_fp_dev->tran_fp_dev->dev, "main");
	if (IS_ERR(tran_fp_dev->clk)) {
		TRAN_FP_ERROR("cannot get spi main clock or dma clock. main clk err : %ld .\n",PTR_ERR(tran_fp_dev->clk));
		return PTR_ERR(tran_fp_dev->clk);
	}
#endif
	tran_fp_dev->tran_pinctrl = devm_pinctrl_get(&tran_fp_dev->tran_fp_dev->dev);
	if(IS_ERR(tran_fp_dev->tran_pinctrl)){
		TRAN_FP_ERROR("devm_pinctrl_get failed!");
		ret=PTR_ERR(tran_fp_dev->tran_pinctrl);
		goto pinctrl_get_fail;
	}
	tran_fp_dev->pins_reset_high = pinctrl_lookup_state(tran_fp_dev->tran_pinctrl,TRAN_FP_RESET_HIGH);
	if(IS_ERR(tran_fp_dev->pins_reset_high)){
		TRAN_FP_ERROR("pinctrl_lookup_state reset_active failed!");
		ret=PTR_ERR(tran_fp_dev->pins_reset_high);
		goto lookup_state_fail;
	}
	tran_fp_dev->pins_reset_low = pinctrl_lookup_state(tran_fp_dev->tran_pinctrl,TRAN_FP_RESET_LOW);
	if(IS_ERR(tran_fp_dev->pins_reset_low)){
		TRAN_FP_ERROR("pinctrl_lookup_state reset_deactive failed!");
		ret=PTR_ERR(tran_fp_dev->pins_reset_low);
		goto lookup_state_fail;
	}
	tran_fp_dev->pins_irq = pinctrl_lookup_state(tran_fp_dev->tran_pinctrl,TRAN_FP_IRQ);
	if(IS_ERR(tran_fp_dev->pins_irq)){
		TRAN_FP_ERROR("pinctrl_lookup_state irq_active failed!");
		ret=PTR_ERR(tran_fp_dev->pins_irq);
		goto lookup_state_fail;
	}
	tran_fp_dev->pins_spi_default = pinctrl_lookup_state(tran_fp_dev->tran_pinctrl,TRAN_FP_SPI_DEFAULT);
	if(IS_ERR(tran_fp_dev->pins_spi_default)){
		TRAN_FP_ERROR("pinctrl_lookup_state spi_default failed!");
		ret=PTR_ERR(tran_fp_dev->pins_spi_default);
		goto no_spi_dts_default_config_no_influence;
	}
no_spi_dts_default_config_no_influence:
	tran_fp_dev->pins_spi_gpio = pinctrl_lookup_state(tran_fp_dev->tran_pinctrl,TRAN_FP_SPI_GPIO);
	if(IS_ERR(tran_fp_dev->pins_spi_gpio)){
		ret = PTR_ERR(tran_fp_dev->pins_spi_gpio);
		TRAN_FP_ERROR("pinctrl_lookup_state spi_gpio failed! ret = %ld", ret);
	}
	return 0;
lookup_state_fail:
pinctrl_get_fail:
	tran_fp_dev->tran_pinctrl=NULL;
	return ret;
}

static int tran_fp_resource_init(struct tran_fp_data *tran_fp_dev)
{
	int ret = 0;
	ret = tran_init_eint(tran_fp_dev);
	if (ret < 0) {
		TRAN_FP_ERROR("tran_init_eint error!\n");
		return -EINVAL;
	}
	spin_lock_init(&tran_fp_dev->spi_lock);
	mutex_init(&tran_fp_dev->buf_lock);
	mutex_init(&tran_fp_dev->release_lock);
	INIT_LIST_HEAD(&tran_fp_dev->device_entry);
#ifdef CONFIG_PM_SLEEP
	tran_fp_dev->tran_wakelock = wakeup_source_register(NULL, "tran_fp_wakelock");
#else
	wake_lock_init(&tran_fp_dev->tran_wakelock, WAKE_LOCK_SUSPEND, "tran_fp_wakelock");
#endif
	init_waitqueue_head(&tran_fp_dev->wq_irq_return);
	tran_fp_dev->wqueue = create_singlethread_workqueue("tran_fp_wq");
	return ret;
}

static int tran_fp_open_clock(struct tran_fp_data *tran_fp_datap)
{
	if (open_clk_count == 0)
	{
#ifdef CONFIG_TRAN_FP_DRV_MODE_SPI
		mt_spi_enable_master_clk(tran_fp_datap->tran_fp_dev);
#else
		clk_enable(tran_fp_datap->clk);
#endif
		TRAN_FP_DEBUG("entered \n");
		open_clk_count = 1;
		close_clk_count = 0;
	}
	return 0;
}

static int tran_fp_close_clock(struct tran_fp_data *tran_fp_datap)
{
	if (close_clk_count == 0) {
#ifdef CONFIG_TRAN_FP_DRV_MODE_SPI
		mt_spi_disable_master_clk(tran_fp_datap->tran_fp_dev);
#else
		clk_disable(tran_fp_datap->clk);
#endif
		TRAN_FP_DEBUG("entered \n");
		close_clk_count = 1;
		open_clk_count = 0;
	}
	return 0;
}

int tran_fp_input_init(void)
{
	int err = 0;
	TRAN_FP_INFO(" enter.\n");
	g_tran_fp_datap->input = input_allocate_device();

	if (!g_tran_fp_datap->input) {
		TRAN_FP_ERROR("input_allocate_device(..) failed.\n");
		return (-ENOMEM);
	}

	g_tran_fp_datap->input->name = "tran-keys";
	__set_bit(EV_KEY,   g_tran_fp_datap->input->evbit);
	__set_bit(KEY_HOME,   g_tran_fp_datap->input->keybit);
	//__set_bit(KEY_HOMEPAGE,   g_tran_fp_datap->input->keybit);   //maybe has some conflict with some game app
	__set_bit(KEY_MENU,   g_tran_fp_datap->input->keybit);
	__set_bit(KEY_BACK,   g_tran_fp_datap->input->keybit);
	__set_bit(KEY_POWER,  g_tran_fp_datap->input->keybit);
	__set_bit(KEY_F28,    g_tran_fp_datap->input->keybit);
	__set_bit(KEY_ENTER,  g_tran_fp_datap->input->keybit);
	__set_bit(KEY_UP,     g_tran_fp_datap->input->keybit);
	__set_bit(KEY_LEFT,   g_tran_fp_datap->input->keybit);
	__set_bit(KEY_RIGHT,  g_tran_fp_datap->input->keybit);
	__set_bit(KEY_DOWN,   g_tran_fp_datap->input->keybit);
	__set_bit(KEY_WAKEUP, g_tran_fp_datap->input->keybit);
	__set_bit(KEY_CAMERA, g_tran_fp_datap->input->keybit);

	if (g_tran_fp_datap->input_init)
		g_tran_fp_datap->input_init(g_tran_fp_datap);
	//__set_bit(KEY_VOLUMEDOWN, g_tran_fp_datap->input->keybit);
	//__set_bit(KEY_VOLUMEUP, g_tran_fp_datap->input->keybit);
	//__set_bit(KEY_SEARCH, g_tran_fp_datap->input->keybit);
	//__set_bit(KEY_CHAT, g_tran_fp_datap->input->keybit);

	err = input_register_device(g_tran_fp_datap->input);

	if (err) {
		TRAN_FP_ERROR("input_register_device(..) = %d.\n", err);
		input_free_device(g_tran_fp_datap->input);
		g_tran_fp_datap->input = NULL;
		return (-ENODEV);
	}

	TRAN_FP_INFO(" leave.\n");
	return err;
}

int tran_fp_irq_init(void)
{
	int rc = 0;
	if(g_tran_fp_datap->tran_pinctrl)
	{
		rc = pinctrl_select_state(g_tran_fp_datap->tran_pinctrl, g_tran_fp_datap->pins_irq);
		if(rc)
			TRAN_FP_ERROR("[tran]cannot set irq pinctrl state, rc = %d \n", rc);
	}
	return g_tran_fp_datap->irq_init();
}

int tran_netlink_init(struct tran_fp_data *tran_fp_datap)
{
	struct netlink_kernel_cfg cfg;
	memset(&cfg, 0, sizeof(struct netlink_kernel_cfg));
	cfg.input = tran_netlink_recv;

	tran_fp_datap->nl_sk = netlink_kernel_create(&init_net, TRAN_NETLINK_ROUTE, &cfg);
	if (tran_fp_datap->nl_sk == NULL) {
		TRAN_FP_ERROR(" netlink create failed\n");
		return -1;
	}

	TRAN_FP_INFO(" netlink create success\n");
	return 0;
}
int tran_netlink_bio_init(struct tran_fp_data *tran_fp_datap)
{
	struct netlink_kernel_cfg cfg_bio;
	memset(&cfg_bio, 0, sizeof(struct netlink_kernel_cfg));
	cfg_bio.input = tran_netlink_recv;

	tran_fp_datap->nl_sk_bio = netlink_kernel_create(&init_net, TRAN_BIO_NETLINK_ROUTE, &cfg_bio);
	if (tran_fp_datap->nl_sk_bio == NULL) {
		TRAN_FP_ERROR(" netlink_bio create failed\n");
		return -1;
	}

	TRAN_FP_INFO(" netlink_bio create success\n");
	return 0;
}
int tran_netlink_destroy(struct tran_fp_data *tran_fp_datap)
{
	if (tran_fp_datap->nl_sk != NULL) {
		netlink_kernel_release(tran_fp_datap->nl_sk);
		tran_fp_datap->nl_sk = NULL;
		return 0;
	}
	TRAN_FP_ERROR(" no netlink socket yet\n");
	return -1;
}
int tran_netlink_bio_destroy(struct tran_fp_data *tran_fp_datap)
{
	if (tran_fp_datap->nl_sk_bio != NULL) {
		netlink_kernel_release(tran_fp_datap->nl_sk_bio);
		tran_fp_datap->nl_sk_bio = NULL;
		return 0;
	}
	TRAN_FP_ERROR(" no netlink_bio socket yet\n");
	return -1;
}
void tran_netlink_send(struct tran_fp_data *tran_fp_datap, const int cmd)
{
	struct nlmsghdr *nlh = NULL;
	struct sk_buff *skb = NULL;
	int ret;
#ifdef FINGERPRINT_INTERRUPT_LOG
	TRAN_FP_INFO("[%s] send cmd %d\n", __func__, cmd);
#endif
	if (!tran_fp_datap || !tran_fp_datap->nl_sk) {
		TRAN_FP_ERROR("[%s] invalid socket\n", __func__);
		return;
	}

	if (! pid) {
		TRAN_FP_ERROR("[%s] invalid PID\n", __func__);
		return;
	}

	/*alloc data buffer for sending to native*/
	/*malloc data space at least 1500 bytes, which is ethernet data length*/
	skb = alloc_skb(TRAN_NL_MSG_LEN, GFP_ATOMIC);
	if (skb == NULL) {
		return;
	}

	nlh = nlmsg_put(skb, 0, 0, 0, TRAN_NL_MSG_LEN, 0);
	if (!nlh) {
		TRAN_FP_ERROR("[%s] nlmsg_put() failed\n", __func__);
		kfree_skb(skb);
		return;
	}

	NETLINK_CB(skb).portid = 0;
	NETLINK_CB(skb).dst_group = 0;

	*(char *)NLMSG_DATA(nlh) = cmd;
	ret = netlink_unicast(tran_fp_datap->nl_sk, skb, pid, MSG_DONTWAIT);
	if (ret == 0) {
		TRAN_FP_ERROR("[%s] send failed\n", __func__);
		kfree_skb(skb);
		return;
	}
#ifdef FINGERPRINT_INTERRUPT_LOG
	TRAN_FP_DEBUG("[%s] sent, len=%d\n", __func__, ret);
#endif
}
void tran_netlink_bio_send(struct tran_fp_data *tran_fp_datap, const int cmd)
{
	struct nlmsghdr *nlh = NULL;
	struct sk_buff *skb = NULL;
	int ret;

#ifdef FINGERPRINT_INTERRUPT_LOG
	TRAN_FP_INFO("[%s] send cmd %d\n", __func__, cmd);
#endif
	if (!tran_fp_datap || !tran_fp_datap->nl_sk_bio) {
		TRAN_FP_ERROR("[%s] invalid socket\n", __func__);
		return;
	}

	if (! pid) {
		TRAN_FP_ERROR("[%s] invalid PID\n", __func__);
		return;
	}

	/*alloc data buffer for sending to native*/
	/*malloc data space at least 1500 bytes, which is ethernet data length*/
	skb = alloc_skb(TRAN_NL_MSG_LEN, GFP_ATOMIC);
	if (skb == NULL) {
		return;
	}

	nlh = nlmsg_put(skb, 0, 0, 0, TRAN_NL_MSG_LEN, 0);
	if (!nlh) {
		TRAN_FP_ERROR("[%s] nlmsg_put() failed\n", __func__);
		kfree_skb(skb);
		return;
	}

	NETLINK_CB(skb).portid = 0;
	NETLINK_CB(skb).dst_group = 0;

	*(char *)NLMSG_DATA(nlh) = cmd;
	ret = netlink_unicast(tran_fp_datap->nl_sk_bio, skb, pid, MSG_DONTWAIT);
	if (ret == 0) {
		TRAN_FP_ERROR("[%s] send failed\n", __func__);
		kfree_skb(skb);
		return;
	}
#ifdef FINGERPRINT_INTERRUPT_LOG
	TRAN_FP_DEBUG("[%s] sent, len=%d\n", __func__, ret);
#endif
}
void tran_netlink_recv(struct sk_buff *__skb)
{
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh = NULL;
	char str[128];

	skb = skb_get(__skb);
	if (!skb ) {
		TRAN_FP_ERROR("[%s] skb NULL\n", __func__);
		return;
	}

	/* presume there is 5byte payload at leaset */
	if (skb->len >= NLMSG_SPACE(0)) {
		nlh = nlmsg_hdr(skb);
		memcpy(str, NLMSG_DATA(nlh), sizeof(str));
		pid = nlh->nlmsg_pid;
		TRAN_FP_DEBUG("[%s]nlh->nlmsg_pid = %d,  pid = %d, msg = %s\n", __func__, nlh->nlmsg_pid, pid, str);
	}
	kfree_skb(skb);
}

int tran_fp_report_key_nav_event(struct input_dev *input, tran_key_nav_t *kevent)
{
	int err = 0;
	unsigned int key_code = KEY_UNKNOWN;
	TRAN_FP_DEBUG("(..) enter.\n");

	switch (kevent->key) {
		case TRAN_KEY_NAV_NONE:
			key_code = KEY_UNKNOWN;
			TRAN_FP_INFO("key none");
			break;

		case TRAN_KEY_NAV_HOME:
			key_code = KEY_HOME;
			TRAN_FP_INFO("key home");
			break;

		case TRAN_KEY_NAV_POWER:
			key_code = KEY_POWER;
			TRAN_FP_INFO("key power");
			break;

		case TRAN_KEY_NAV_MENU:
			key_code = KEY_MENU;
			TRAN_FP_INFO("key menu");
			break;

		case TRAN_KEY_NAV_BACK:
			key_code = KEY_BACK;
			TRAN_FP_INFO("key back");
			break;

		case TRAN_KEY_NAV_CAMERA:
			key_code = KEY_CAMERA;
			TRAN_FP_INFO("key camera");
			break;

		case TRAN_KEY_NAV_FINGER_UP:
			TRAN_FP_INFO("nav finger up");
			break;

		case TRAN_KEY_NAV_FINGER_DOWN:
			TRAN_FP_INFO("nav finger down");
			break;

		case TRAN_KEY_NAV_UP:
			key_code = KEY_UP;
			TRAN_FP_INFO("nav up");
			break;

		case TRAN_KEY_NAV_DOWN:
			key_code = KEY_DOWN;
			TRAN_FP_INFO("nav down");
			break;

		case TRAN_KEY_NAV_LEFT:
			key_code = KEY_LEFT;
			TRAN_FP_INFO("nav left");
			break;

		case TRAN_KEY_NAV_RIGHT:
			key_code = KEY_RIGHT;
			TRAN_FP_INFO("nav right");
			break;

		case TRAN_KEY_NAV_CLICK:
			key_code = KEY_VOLUMEDOWN;
			TRAN_FP_INFO("nav volume down");
			break;

		case TRAN_KEY_NAV_HEAVY:
			key_code = KEY_CHAT;
			TRAN_FP_INFO("nav chat");
			break;

		case TRAN_KEY_NAV_LONG_PRESS:
			key_code = KEY_SEARCH;
			TRAN_FP_INFO("nav search");
			break;

		case TRAN_KEY_NAV_DOUBLE_CLICK:
			key_code = KEY_VOLUMEUP;
			TRAN_FP_INFO("nav volume up");
			break;

		case TRAN_KEY_NAV_F28:
			key_code = KEY_F28;
			TRAN_FP_INFO("nav F28");
			break;

		case TRAN_KEY_NAV_ENTER:
			key_code = KEY_ENTER;
			TRAN_FP_INFO("nav enter");
			break;

		case TRAN_KEY_NAV_WAKEUP:
			key_code = KEY_WAKEUP;
			TRAN_FP_INFO("nav wakeup");
			break;

		case TRAN_KEY_NAV_PAGEUP:
			key_code = KEY_PAGEUP;
			TRAN_FP_INFO("nav pageup");
			break;

		case TRAN_KEY_NAV_PAGEDOWN:
			key_code = KEY_PAGEDOWN;
			TRAN_FP_INFO("nav pagedown");
			break;
		default:
			TRAN_FP_INFO("key_nav none or bad parameter, kevent->key = %d ", kevent->key);
			break;
	}

	TRAN_FP_INFO(" key_code[%d], key=%d, value=%d\n", key_code, kevent->key, kevent->value);
	if ((kevent->key != TRAN_KEY_NAV_FINGER_DOWN) && (kevent->key != TRAN_KEY_NAV_FINGER_UP)) {
		input_report_key(input, key_code, kevent->value);
		input_sync(input);
	}
	TRAN_FP_DEBUG("(..) leave.\n");
	return err;
}

int tran_proc_init(struct tran_fp_data *tran_fp_dev)
{
	int ret = 0;
#ifdef CONFIG_TRAN_PROC_FS
	ret = tran_fp_dev->proc_init(tran_fp_dev);
#endif
	return ret;
}

int tran_proc_deinit(struct tran_fp_data *tran_fp_dev)
{
	int ret = 0;
#ifdef CONFIG_TRAN_PROC_FS
	ret = tran_fp_dev->proc_deinit(tran_fp_dev);
#endif
	return ret;
}

//manlin.zhang start.
void setVendorName(void) {
    if (strcmp(g_tran_fp_datap->ic_name, "GW9578") == 0 ||
        strcmp(g_tran_fp_datap->ic_name, "GW9598") == 0 ||
        strcmp(g_tran_fp_datap->ic_name, "GF3976") == 0 ||
        strcmp(g_tran_fp_datap->ic_name, "GF3656") == 0 ) {
        strcpy(g_tran_fp_datap->vendor_name, "common_goodix");
    } else if (strcmp(g_tran_fp_datap->ic_name, "GSL7002") == 0) {
        strcpy(g_tran_fp_datap->vendor_name, "common_silead");
    } else if (strcmp(g_tran_fp_datap->ic_name, "JV0307") == 0) {
        strcpy(g_tran_fp_datap->vendor_name, "common_JOOIV");
    } else if (strcmp(g_tran_fp_datap->ic_name, "FT9399") == 0 ||
               strcmp(g_tran_fp_datap->ic_name, "FT9391") == 0 ) {
        strcpy(g_tran_fp_datap->vendor_name, "common_FocalTech");
    } else {
        strcpy(g_tran_fp_datap->vendor_name, "null");
    }

    TRAN_FP_INFO("This IC vendor_name=%s ",g_tran_fp_datap->vendor_name);
}
//manlin.zhang end.
int tran_register_base_ops(struct tran_fp_data *tran_fp_datap)
{
	tran_fp_datap->resource_init  = tran_fp_resource_init;
	tran_fp_datap->power_init = tran_fp_power_init;
	tran_fp_datap->power_onoff = tran_fp_power_onoff;
	tran_fp_datap->spi_clk_on = tran_fp_open_clock;
	tran_fp_datap->spi_clk_off = tran_fp_close_clock;
	tran_fp_datap->reset      = tran_fp_reset_sensor;
	tran_fp_datap->shutdown      = tran_fp_shutdown_sensor;
	tran_fp_datap->enable_irq = tran_fp_enable_irq;
	tran_fp_datap->disable_irq = tran_fp_disable_irq;
	tran_fp_datap->notifier.notifier_call = tran_fb_notifier_callback;
	return 0;
}
