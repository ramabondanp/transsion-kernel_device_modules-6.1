// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#define pr_fmt(fmt) "[sc8548] %s: " fmt, __func__

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/delay.h>
#include <tc_charger_class.h>
#include <tc_algorithm_class.h>
#include "tc_sc8548_reg.h"

#if IS_ENABLED(CONFIG_TRAN_AW95016)
#include "../../../misc/mediatek/aw95016/aw95016.h"
extern int aw95016_set_output(unsigned int port, int level);
extern int aw95016_set_dir(unsigned int port, int mode);
extern int aw95016_register_irq(unsigned int port, void* call_back);
extern int aw95016_unregister_irq(unsigned int port);
extern int aw95016_irq_enable(unsigned int port, int enable);
extern int aw95016_set_pull_mode(unsigned int port, int mode);
extern int aw95016_pull_enable(unsigned int port, int enable);
extern int aw95016_get_level(unsigned int port, int *level);

#define PULL_DOWN 0
#define PULL_UP 1

#define DISABLE 0
#define ENBALE 1

static struct sc8548 *ex_sc = NULL;
#endif

typedef enum {
	ADC_IBUS,
	ADC_VBUS,
	ADC_VAC,
	ADC_VOUT,
	ADC_VBAT,
	ADC_IBAT,
	ADC_TDIE,
	ADC_TBUS,
	ADC_MAX_NUM,
}ADC_CH;

enum sc8548_notify {
	SC8548_NOTIFY_IBUSUCPF = 0,
	SC8548_NOTIFY_IBUSOCP,
	SC8548_NOTIFY_VBUSOVP,
	SC8548_NOTIFY_IBATOCP,
	SC8548_NOTIFY_VBATOVP,
	SC8548_NOTIFY_VOUTOVP,
	SC8548_NOTIFY_MAX,
};

static const u32 sc8548_chgdev_notify_map[SC8548_NOTIFY_MAX] = {
	CHARGER_DEV_NOTIFY_IBUSUCP_FALL,
	CHARGER_DEV_NOTIFY_IBUSOCP,
	CHARGER_DEV_NOTIFY_VBUS_OVP,
	CHARGER_DEV_NOTIFY_IBATOCP,
	CHARGER_DEV_NOTIFY_BAT_OVP,
	CHARGER_DEV_NOTIFY_VOUTOVP,
};

#define sc_err(fmt, ...)	\
do {						\
	printk(KERN_ERR "[sc8548-STANDALONE]:%s:" fmt, __func__, ##__VA_ARGS__);\
} while(0);

#define sc_info(fmt, ...)   \
do {						\
	printk(KERN_INFO "[sc8548-STANDALONE]:%s:" fmt, __func__, ##__VA_ARGS__);\
} while(0);

#define sc_dbg(fmt, ...)	\
do {						\
	printk(KERN_DEBUG "[sc8548-STANDALONE]:%s:" fmt, __func__, ##__VA_ARGS__);\
} while(0);

struct sc8548_cfg {
	bool bat_ovp_disable;
	bool bus_ocp_disable;
	bool bus_ovp_disable;

	int bat_ovp_th;
	int bat_ocp_th;
	int bus_ovp_th;
	int bus_ocp_th;
	int ac_ovp_th;
	int sense_r_mohm;
	const char *chg_name;
	struct gpio_desc *irq_gpio;
};

struct sc8548 {
	struct device *dev;
	struct i2c_client *client;
	int dev_id;
	bool sc8548e;
	int irq_gpio;
	int irq;

	struct mutex data_lock;
	struct mutex i2c_rw_lock;
	struct mutex charging_disable_lock;
	struct mutex irq_complete;

	bool irq_waiting;
	bool irq_disabled;
	bool resume_completed;

	bool batt_present;
	bool vbus_present;

	bool usb_present;
	bool charge_enabled; /* Register bit status */

	int  vbus_error;
	int charger_mode;
	int direct_charge;

	/* ADC reading */
	int vbat_volt;
	int vbus_volt;
	int vout_volt;
	int vac_volt;

	int ibat_curr;
	int ibus_curr;

	int die_temp;

	/* alarm/fault status */
	bool out_ovp_fault;
	bool bat_ovp_fault;
	bool bat_ocp_fault;
	bool bus_ovp_fault;
	bool bus_ocp_fault;
	bool bus_ucp_fault;
	bool adp_insert_fault;
	bool vbat_insert_fault;

	struct task_struct *notify_task;
	struct mutex notify_lock;
	int notify;
	wait_queue_head_t wq;
	bool notify_disable;
	bool is_rfc_ta;
	bool is_master;
	bool set_vil;
	struct delayed_work dpdm_check_work;

	bool therm_shutdown_flag;
	bool therm_shutdown_stat;

	struct sc8548_cfg *cfg;

	int skip_writes;
	int skip_reads;

	struct sc8548_platform_data *platform_data;

	struct power_supply_desc psy_desc;
	struct power_supply_config psy_cfg;
	struct power_supply *fc2_psy;

	struct charger_properties chg_prop;
	struct charger_device *chg_dev;

	bool exio_irq;
};

static const u32 sc8548_adc_accuracy_tbl[ADC_MAX_NUM] = {
	35000,	/* VBUS */
	150000,	/* IBUS */
	20000,	/* VBAT */
	200000,	/* IBAT */
	35000,	/* VTS */
	20000,	/* VOUT */
	4,	/* TDIE */
};

/************************************************************************/
static int __sc8548_read_word(struct sc8548 *sc, u8 reg, u16 *data)
{
	s32 ret;
	s32 retry_cnt = 3;
	while (retry_cnt--) {
		ret = i2c_smbus_read_word_data(sc->client, reg);
		if (ret < 0) {
			sc_err("i2c read fail: can't read from reg 0x%02X\n", reg);
			mdelay(10);
		} else {
			*data = (u16) ret;
			break;
		}
	}

	return 0;
}

static int __sc8548_read_byte(struct sc8548 *sc, u8 reg, u8 *data)
{
	s32 ret;
	s32 retry_cnt = 3;
	while(retry_cnt--) {
		ret = i2c_smbus_read_byte_data(sc->client, reg);
		if (ret < 0) {
			sc_err("i2c read fail: can't read from reg 0x%02X\n", reg);
			mdelay(10);
		} else {
			*data = (u8) ret;
			break;
		}
	}

	return 0;
}

static int __sc8548_write_byte(struct sc8548 *sc, int reg, u8 val)
{
	s32 ret;
	s32 retry_cnt = 3;
	while(retry_cnt--) {
		ret = i2c_smbus_write_byte_data(sc->client, reg, val);
		if (ret < 0) {
			sc_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
					val, reg, ret);
			mdelay(10);
		} else {
			break;
		}
	}
	return 0;
}

static int sc8548_read_byte(struct sc8548 *sc, u8 reg, u8 *data)
{
	int ret;

	if (sc->skip_reads) {
		*data = 0;
		return 0;
	}

	mutex_lock(&sc->i2c_rw_lock);
	ret = __sc8548_read_byte(sc, reg, data);
	mutex_unlock(&sc->i2c_rw_lock);

	return ret;
}

static int sc8548_write_byte(struct sc8548 *sc, u8 reg, u8 data)
{
	int ret;

	if (sc->skip_writes)
		return 0;

	mutex_lock(&sc->i2c_rw_lock);
	ret = __sc8548_write_byte(sc, reg, data);
	mutex_unlock(&sc->i2c_rw_lock);

	return ret;
}

static int sc8548_read_word(struct sc8548 *sc, u8 reg, u16 *data)
{
	int ret;

	if (sc->skip_reads) {
		*data = 0;
		return 0;
	}

	mutex_lock(&sc->i2c_rw_lock);
	ret = __sc8548_read_word(sc, reg, data);
	mutex_unlock(&sc->i2c_rw_lock);

	return ret;
}

static int sc8548_update_bits(struct sc8548*sc, u8 reg,
					u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	if (sc->skip_reads || sc->skip_writes)
		return 0;

	mutex_lock(&sc->i2c_rw_lock);
	ret = __sc8548_read_byte(sc, reg, &tmp);
	if (ret) {
		sc_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __sc8548_write_byte(sc, reg, tmp);
	if (ret)
		sc_err("Failed: reg=%02X, ret=%d\n", reg, ret);

out:
	mutex_unlock(&sc->i2c_rw_lock);
	return ret;
}

/*********************************************************************/

static int sc8548_enable_batovp(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8548_BAT_OVP_ENABLE;
	else
		val = SC8548_BAT_OVP_DISABLE;

	val <<= SC8548_BAT_OVP_DIS_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_00,
				SC8548_BAT_OVP_DIS_MASK, val);
	return ret;
}

static int sc8548_set_batovp_th(struct sc8548 *sc, int threshold)
{
	int ret;
	u8 val;

	if (threshold < SC8548_BAT_OVP_BASE)
		threshold = SC8548_BAT_OVP_BASE;

	val = (threshold - SC8548_BAT_OVP_BASE) / SC8548_BAT_OVP_LSB;

	val <<= SC8548_BAT_OVP_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_00,
				SC8548_BAT_OVP_MASK, val);
	return ret;
}

static int sc8548_enable_batocp(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8548_BAT_OCP_ENABLE;
	else
		val = SC8548_BAT_OCP_DISABLE;

	val <<= SC8548_BAT_OCP_DIS_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_01,
				SC8548_BAT_OCP_DIS_MASK, val);
	return ret;
}

static int sc8548_set_batocp_th(struct sc8548 *sc, int threshold)
{
	int ret;
	u8 val;

	if (threshold < SC8548_BAT_OCP_BASE)
		threshold = SC8548_BAT_OCP_BASE;

	val = (threshold - SC8548_BAT_OCP_BASE) / SC8548_BAT_OCP_LSB;

	val <<= SC8548_BAT_OCP_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_01,
				SC8548_BAT_OCP_MASK, val);
	return ret;
}

static int sc8548_set_acovp_th(struct sc8548 *sc, int threshold)
{
	int ret;
	u8 val;

	if (threshold < SC8548_AC_OVP_BASE)
		threshold = SC8548_AC_OVP_BASE;

	if (threshold == SC8548_AC_OVP_6P5V)
		val = 0x07;
	else
		val = (threshold - SC8548_AC_OVP_BASE) /  SC8548_AC_OVP_LSB;

	val <<= SC8548_AC_OVP_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_02,
				SC8548_AC_OVP_MASK, val);

	return ret;

}

static int sc8548_set_vdrop_ovp_th(struct sc8548 *sc, int threshold)
{
	int ret;
	u8 val;

	if (threshold == 400)
		val = SC8548_VDROP_OVP_THRESHOLD_400MV;
	else
		val = SC8548_VDROP_OVP_THRESHOLD_300MV;

	val <<= SC8548_VDROP_OVP_THRESHOLD_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_03,
				SC8548_VDROP_OVP_THRESHOLD_MASK, val);
	return ret;
}

static int sc8548_set_vdrop_deglitch(struct sc8548 *sc, int deglitch)
{
	int ret;
	u8 val;

	if (deglitch == 5000)
		val = SC8548_VDROP_DEGLITCH_SET_5MS;
	else
		val = SC8548_VDROP_DEGLITCH_SET_8US;

	val <<= SC8548_VDROP_DEGLITCH_SET_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_03,
				SC8548_VDROP_DEGLITCH_SET_MASK, val);
	return ret;
}

static int sc8548_enable_busovp(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8548_VBUS_OVP_ENABLE;
	else
		val = SC8548_VBUS_OVP_DISABLE;

	val <<= SC8548_VBUS_OVP_DIS_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_04,
				SC8548_VBUS_OVP_DIS_MASK, val);
	return ret;
}

static int sc8548_set_busovp_th(struct sc8548 *sc, int threshold)
{
	int ret;
	u8 val;

	if (threshold < SC8548_VBUS_OVP_BASE)
		threshold = SC8548_VBUS_OVP_BASE;

	val = (threshold - SC8548_VBUS_OVP_BASE) / SC8548_VBUS_OVP_LSB;

	val <<= SC8548_VBUS_OVP_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_04,
				SC8548_VBUS_OVP_MASK, val);
	return ret;
}

static int sc8548_enable_busocp(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8548_IBUS_OCP_ENABLE;
	else
		val = SC8548_IBUS_OCP_DISABLE;

	val <<= SC8548_IBUS_OCP_DIS_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_05,
				SC8548_IBUS_OCP_DIS_MASK, val);
	return ret;
}

static int sc8548_set_busocp_th(struct sc8548 *sc, int threshold)
{
	int ret;
	u8 val;

	if (threshold < SC8548_IBUS_OCP_BASE)
		threshold = SC8548_IBUS_OCP_BASE;

	val = (threshold - SC8548_IBUS_OCP_BASE) / SC8548_IBUS_OCP_LSB;
	
	if ((threshold - SC8548_IBUS_OCP_BASE) % SC8548_IBUS_OCP_LSB)
		val++;

	val <<= SC8548_IBUS_OCP_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_05,
				SC8548_IBUS_OCP_MASK, val);
	return ret;
}

static int sc8548_set_freq_shift(struct sc8548 *sc, u8 val)
{
	int ret = 0;

	if (sc->dev_id == SC8548D_DEVICE_ID) {
		/* set fsw 500kHz */
		ret = sc8548_update_bits(sc, SC8548_REG_07,
				SC8548D_CHG_FSW_MASK, 0x0);
	}
	return ret;
}

static int sc8548_set_i2c_vil(struct sc8548 *sc, u8 val)
{
	int ret = 0;

	if (!sc) {
		sc_err("%d %s: sc is NULL\n", __LINE__, __func__);
		return -EINVAL;
	}

	if (sc->dev_id == SC8548D_DEVICE_ID && sc->set_vil) {
	/* SC8548D/E 0:I2C_VIL=0.4V 1: I2C_VIL=0.65V*/
		val <<= SC8548_I2C_VIL_SHIFT;
		ret = sc8548_update_bits(sc, SC8548_REG_3A,
			SC8548_I2C_VIL_MASK, val);
		if (ret < 0) {
			sc_err("%d %s fail\n",__LINE__, __func__);
		}
	}

	//sc_err("%d %s done\n",__LINE__, __func__);
	return ret;
}

static int sc8548_enable_charge(struct sc8548 *sc, bool enable)
{
	int ret;	
	u8 val;

	if (enable)
		val = SC8548_CHG_ENABLE;
	else
		val = SC8548_CHG_DISABLE;

	val <<= SC8548_CHG_EN_SHIFT;

	sc_err("sc8548 charger %s\n", enable == false ? "disable" : "enable");
	ret = sc8548_update_bits(sc, SC8548_REG_07,
				SC8548_CHG_EN_MASK, val);

	return ret;
}

static int sc8548_check_charge_enabled(struct sc8548 *sc, bool *enabled)
{
	int ret;
	u8 val;

	ret = sc8548_read_byte(sc, SC8548_REG_07, &val);
	if (!ret)
		*enabled = !!(val & SC8548_CHG_EN_MASK);
	return ret;
}

static int sc8548_reg_reset(struct sc8548 *sc, bool enable)
{
	int ret;	
	u8 val;

	if (enable)
		val = SC8548_RESET_REG;
	else
		val = SC8548_NO_REG_RESET;

	val <<= SC8548_REG_RESET_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_07,
				SC8548_REG_RESET_MASK, val);

	return ret;
}

static int sc8548_set_ss_timeout(struct sc8548 *sc, int timeout)
{
	int ret;
	u8 val;

	switch (timeout) {
	case 0:
		val = SC8548_SS_TIMEOUT_DISABLE;
		break;
	case 40:
		val = SC8548_SS_TIMEOUT_40MS;
		break;
	case 80:
		val = SC8548_SS_TIMEOUT_80MS;
		break;
	case 320:
		val = SC8548_SS_TIMEOUT_320MS;
		break;
	case 1280:
		val = SC8548_SS_TIMEOUT_1280MS;
		break;
	case 5120:
		val = SC8548_SS_TIMEOUT_5120MS;
		break;
	case 20480:
		val = SC8548_SS_TIMEOUT_20480MS;
		break;
	case 81920:
		val = SC8548_SS_TIMEOUT_81920MS;
		break;
	default:
		val = SC8548_SS_TIMEOUT_DISABLE;
		break;
	}

	val <<= SC8548_SS_TIMEOUT_SET_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_08,
				SC8548_SS_TIMEOUT_SET_MASK,
				val);

	return ret;
}

static int sc8548_reg_timeout_enabled(struct sc8548 *sc, bool enable)
{
	int ret;	
	u8 val;

	if (enable)
		val = SC8548_650MS_REG_TIMEOUT_ENABLE;
	else
		val = SC8548_650MS_REG_TIMEOUT_DISABLE;

	val <<= SC8548_REG_TIMEOUT_DIS_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_08,
				SC8548_REG_TIMEOUT_DIS_MASK, val);

	return ret;
}

static int sc8548_set_sense_resistor(struct sc8548 *sc, int r_mohm)
{
	int ret;
	u8 val;

	if (r_mohm == 2)
		val = SC8548_SET_IBAT_SNS_RES_2MHM;
	else if (r_mohm == 5)
		val = SC8548_SET_IBAT_SNS_RES_5MHM;
	else
		return -EINVAL;

	val <<= SC8548_SET_IBAT_SNS_RES_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_08,
				SC8548_SET_IBAT_SNS_RES_MASK,
				val);
	return ret;
}

int sc8548_set_charge_mode(struct sc8548 *sc, u8 charge_mode)
{
	int ret;
	u8 val;
	if(charge_mode)
		val = SC8548_CHARGE_MODE_1_1;
	else
		val = SC8548_CHARGE_MODE_2_1;

	val <<= SC8548_CHARGE_MODE_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_09,
				SC8548_CHARGE_MODE_MASK,
				val);
	return ret;
}

int sc8548_get_charge_mode(struct sc8548 *sc)
{
	int ret;
	u8 val;

	ret = sc8548_read_byte(sc, SC8548_REG_09, &val);
	if(ret < 0) {
		return ret;
	}

	sc->charger_mode = (val >> SC8548_CHARGE_MODE_SHIFT) &
							SC8548_CHARGE_MODE_MASK;

	return ret;
}

static int sc8548_set_wdt(struct sc8548 *sc, int ms)
{
	int ret;
	u8 val;

	pr_info("sc8548_set_wdt :%d ms\n",ms);
	if (ms == 0)
		val = SC8548_WATCHDOG_DIS;
	else if (ms == 200)
		val = SC8548_WATCHDOG_200MS;
	else if (ms == 500)
		val = SC8548_WATCHDOG_500MS;
	else if (ms == 1000)
		val = SC8548_WATCHDOG_1S;
	else if (ms == 5000)
		val = SC8548_WATCHDOG_5S;
	else if (ms == 30000)
		val = SC8548_WATCHDOG_30S;
	else
		val = SC8548_WATCHDOG_DIS;

	val <<= SC8548_WATCHDOG_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_09,
				SC8548_WATCHDOG_MASK, val);
	return ret;
}

static int sc8548_vbat_regulation_enabled(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8548_VBAT_REG_ENABLE;
	else
		val = SC8548_VBAT_REG_DISABLE;

	val <<= SC8548_VBAT_REG_EN_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_0A,
				SC8548_VBAT_REG_EN_MASK, val);

	return ret;
}

static int sc8548_set_vbat_regulation(struct sc8548 *sc, int mv)
{
	int ret;
	u8 val;

	if (mv == 50)
		val = SC8548_SET_VBATREG_50MV;
	else if (mv == 100)
		val = SC8548_SET_VBATREG_100MV;
	else if (mv == 150)
		val = SC8548_SET_VBATREG_150MV;
	else if (mv == 200)
		val = SC8548_SET_VBATREG_200MV;
	else
		val = SC8548_SET_VBATREG_50MV;

	val <<= SC8548_SET_VBATREG_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_0A,
				SC8548_SET_VBATREG_MASK, val);
	return ret;
}

static int sc8548_ibat_regulation_enabled(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8548_IBAT_REG_ENABLE;
	else
		val = SC8548_IBAT_REG_DISABLE;

	val <<= SC8548_IBAT_REG_EN_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_0B,
				SC8548_IBAT_REG_EN_MASK, val);

	return ret;
}

static int sc8548_set_ibat_regulation(struct sc8548 *sc, int ma)
{
	int ret;
	u8 val;

	if (ma == 200)
		val = SC8548_SET_IBATREG_200MA;
	else if (ma == 300)
		val = SC8548_SET_IBATREG_300MA;
	else if (ma == 450)
		val = SC8548_SET_IBATREG_400MA;
	else if (ma == 500)
		val = SC8548_SET_IBATREG_500MA;
	else
		val = SC8548_SET_IBATREG_200MA;

	val <<= SC8548_SET_VBATREG_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_0B,
				SC8548_SET_VBATREG_MASK, val);
	return ret;
}

static int sc8548_ibus_regulation_enabled(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8548_IBUS_REG_ENABLE;
	else
		val = SC8548_IBUS_REG_DISABLE;

	val <<= SC8548_IBUS_REG_EN_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_0C,
				SC8548_IBUS_REG_EN_MASK, val);

	return ret;
}

static int sc8548_set_ibus_regulation(struct sc8548 *sc, int threshold)
{
	int ret;
	u8 val;

	if (threshold < SC8548_SET_IBUSREG_BASE)
		threshold = SC8548_SET_IBUSREG_BASE;

	val = (threshold - SC8548_SET_IBUSREG_BASE) / SC8548_SET_IBUSREG_LSB;

	val <<= SC8548_SET_IBUSREG_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_0C,
				SC8548_SET_IBUSREG_MASK, val);
	return ret;
}

static int sc8548_enable_adc(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8548_ADC_ENABLE;
	else
		val = SC8548_ADC_DISABLE;

	val <<= SC8548_ADC_EN_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_11,
				SC8548_ADC_EN_MASK, val);
	return ret;
}

static int sc8548_set_adc_scanrate(struct sc8548 *sc, bool oneshot)
{
	int ret;
	u8 val;

	if (oneshot)
		val = SC8548_ADC_RATE_ONESHOT;
	else
		val = SC8548_ADC_RATE_CONTINOUS;

	val <<= SC8548_ADC_RATE_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_11,
				SC8548_ADC_EN_MASK, val);
	return ret;
}

static int sc8548_set_adc_scan(struct sc8548 *sc, int channel, bool enable)
{
	int ret;
	u8 mask;
	u8 shift;
	u8 val;

	if (channel > ADC_MAX_NUM)
		return -EINVAL;

	switch (channel) {
	case ADC_IBUS:
		shift = SC8548_IBUS_ADC_DIS_SHIFT;
		mask = SC8548_IBUS_ADC_DIS_MASK;
		break;
	case ADC_VBUS:
		shift = SC8548_VBUS_ADC_DIS_SHIFT;
		mask = SC8548_VBUS_ADC_DIS_MASK;
		break;
	case ADC_VAC:
		shift = SC8548_VAC_ADC_DIS_SHIFT;
		mask = SC8548_VAC_ADC_DIS_MASK;
		break;
	case ADC_VOUT:
		shift = SC8548_VOUT_ADC_DIS_SHIFT;
		mask = SC8548_VOUT_ADC_DIS_MASK;
		break;
	case ADC_VBAT:
		shift = SC8548_VBAT_ADC_DIS_SHIFT;
		mask = SC8548_VBAT_ADC_DIS_MASK;
		break;
	case ADC_IBAT:
		shift = SC8548_IBAT_ADC_DIS_SHIFT;
		mask = SC8548_IBAT_ADC_DIS_MASK;
		break;
	case ADC_TDIE:
		shift = SC8548_TDIE_ADC_DIS_SHIFT;
		mask = SC8548_TDIE_ADC_DIS_MASK;
		break;
	}

	if (enable)
		val = 0 << shift;
	else
		val = 1 << shift;

	ret = sc8548_update_bits(sc, SC8548_REG_12,
			mask, val);

	return ret;
}

static int __maybe_unused sc8548_set_adc_freeze(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (sc->dev_id == SC8548_DEVICE_ID)
		return -1;

	val = enable;

	val <<= SC8548_ADC_FREEZE_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_11,
				SC8548_ADC_FREEZE_MASK, val);
	return ret;
}

#define ADC_REG_BASE SC8548_REG_13
static int sc8548_get_adc_data(struct sc8548 *sc, int channel,  int *result)
{
	int ret;
	u16 val;
	u16 t;

	if(channel >= ADC_MAX_NUM) 
		return 0;

	if (channel == ADC_TBUS) {
		*result = 25;
		return 0;
	}

	sc8548_set_adc_freeze(sc,true);
	ret = sc8548_read_word(sc, ADC_REG_BASE + (channel << 1), &val);
	sc8548_set_adc_freeze(sc,false);
	if (ret < 0)
		return ret;

	sc_info("adc result,channel(%d) val:%d\n",channel,val);

	t = val & 0xFF;
	t <<= 8;
	t |= (val >> 8) & 0xFF;
	val = t;

	//sc_err("adc debug: read word: %d, val: %d\n", ADC_REG_BASE + (channel << 1), val);
	if(channel == ADC_IBUS)
		val = val * SC8548_IBUS_ADC_LSB;
	else if(channel == ADC_VBUS)
		val = val * SC8548_VBUS_ADC_LSB;
	else if(channel == ADC_VAC)
		val = val * SC8548_VAC_ADC_LSB;
	else if(channel == ADC_VOUT)
		val = val * SC8548_VOUT_ADC_LSB;
	else if(channel == ADC_VBAT)
		val = val * SC8548_VBAT_ADC_LSB;
	else if(channel == ADC_IBAT)
		val = 2;
		/* val = val * SC8548_IBAT_ADC_LSB; */
	else if(channel == ADC_TDIE)
		val = val * SC8548_TDIE_ADC_LSB;

	//sc_err("adc debug: adc val: %d\n", val);
	
	if (channel != ADC_TDIE)
		*result = val * 1000;
	else
		*result = val;

	return 0;
}

//*******************************DPDM*************************************

static int sc8548_dm_500k_pd_enabled(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8548_DM_500K_PD_ENABLE;
	else
		val = SC8548_DM_500K_PD_DISABLE;

	val <<= SC8548_DM_500K_PD_EN_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_21,
				SC8548_DM_500K_PD_EN_MASK, val);
	return ret;
}

static int sc8548_dp_500k_pd_enabled(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8548_DP_500K_PD_ENABLE;
	else
		val = SC8548_DP_500K_PD_DISABLE;

	val <<= SC8548_DP_500K_PD_EN_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_21,
				SC8548_DP_500K_PD_EN_MASK, val);
	return ret;
}

static int sc8548_dm_20k_pd_enabled(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8548_DM_20K_PD_ENABLE;
	else
		val = SC8548_DM_20K_PD_DISABLE;

	val <<= SC8548_DM_20K_PD_EN_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_21,
				SC8548_DM_20K_PD_EN_MASK, val);
	return ret;
}

static int sc8548_dp_20k_pd_enabled(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8548_DP_20K_PD_ENABLE;
	else
		val = SC8548_DP_20K_PD_DISABLE;

	val <<= SC8548_DP_20K_PD_EN_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_21,
				SC8548_DP_20K_PD_EN_MASK, val);
	return ret;
}

static int sc8548_dm_sink_enabled(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8548_DM_SINK_ENABLE;
	else
		val = SC8548_DM_SINK_DISABLE;

	val <<= SC8548_DM_SINK_EN_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_21,
				SC8548_DM_SINK_EN_MASK, val);
	return ret;
}

static int sc8548_dp_sink_enabled(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8548_DP_SINK_ENABLE;
	else
		val = SC8548_DP_SINK_DISABLE;

	val <<= SC8548_DP_SINK_EN_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_21,
				SC8548_DP_SINK_EN_MASK, val);
	return ret;
}

static int sc8548_dp_source_capability(struct sc8548 *sc, int ua)
{
	int ret;
	u8 val;

	if (ua == 250)
		val = SC8548_DP_SRC_250UA;
	else
		val = SC8548_DP_SRC_10UA;

	val <<= SC8548_DP_SRC_10UA_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_21,
				SC8548_DP_SRC_10UA_MASK, val);
	return ret;
}

static int sc8548_dpdm_enabled(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8548_DPDM_ENABLE;
	else
		val = SC8548_DPDM_DISABLE;

	val <<= SC8548_DPDM_EN_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_21,
				SC8548_DPDM_EN_MASK, val);
	return ret;
}

static int sc8548_dpdmovp_enabled(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8548_DPDM_OVP_ENABLE;
	else
		val = SC8548_DPDM_OVP_DISABLE;

	val <<= SC8548_DPDM_OVP_DIS_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_22,
				SC8548_DPDM_OVP_DIS_MASK, val);
	return ret;
}

static int sc8548_set_dm_buf(struct sc8548 *sc, int mv)
{
	int ret;
	u8 val = 0;

	if (mv == 600)
		val = SC8548_DM_BUF_600MV;
	else if (mv == 1800)
		val = SC8548_DM_BUF_2000MV;
	else if (mv == 2700)
		val = SC8548_DM_BUF_2700MV;
	else if (mv == 3300)
		val = SC8548_DM_BUF_3300MV;

	val <<= SC8548_DM_BUF_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_22,
				SC8548_DM_BUF_MASK, val);
	return ret;
}

static int sc8548_set_dp_buf(struct sc8548 *sc, int mv)
{
	int ret;
	u8 val = 0;

	if (mv == 600)
		val = SC8548_DP_BUF_600MV;
	else if (mv == 1800)
		val = SC8548_DP_BUF_2000MV;
	else if (mv == 2700)
		val = SC8548_DP_BUF_2700MV;
	else if (mv == 3300)
		val = SC8548_DP_BUF_3300MV;

	val <<= SC8548_DP_BUF_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_22,
				SC8548_DP_BUF_MASK, val);
	return ret;
}

static int sc8548_dm_buf_enabled(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8548_DM_BUF_ENABLE;
	else
		val = SC8548_DM_BUF_DISABLE;

	val <<= SC8548_DM_BUF_EN_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_22,
				SC8548_DM_BUF_EN_MASK, val);
	return ret;
}

static int sc8548_dp_buf_enabled(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8548_DP_BUF_ENABLE;
	else
		val = SC8548_DP_BUF_DISABLE;

	val <<= SC8548_DP_BUF_EN_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_22,
				SC8548_DP_BUF_EN_MASK, val);
	return ret;
}

static int sc8548_dpdm_pull_up_enabled(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8548_DPDM_PULL_UP_ENABLE;
	else
		val = SC8548_DPDM_PULL_UP_DISABLE;

	switch (sc->dev_id) {
		case SC8548_DEVICE_ID:
			val <<= SC8548_DPDM_PULL_UP_EN_SHIFT;
			break;
		case SC8548D_DEVICE_ID:
			val <<= SC8548D_DPDM_PULL_UP_EN_SHIFT;
			break;
		default:
			break;
	}

	ret = sc8548_update_bits(sc, SC8548_REG_3D,
				SC8548_DPDM_PULL_UP_EN_MASK, val);
	return ret;
}

#if 0
static int sc8548_i2c_dpdm_bypass_enabled(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8548_I2C_DPDM_BYPASS_ENABLE;
	else
		val = SC8548_I2C_DPDM_BYPASS_DISABLE;

	val <<= SC8548_I2C_DPDM_BYPASS_EN_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_3D,
				SC8548_I2C_DPDM_BYPASS_EN_MASK, val);
	return ret;
}

static int sc8548_set_wdt2(struct sc8548 *sc, int ms)
{
	int ret;
	u8 val;

	if (ms == 16)
		val = SC8548_WD2_CFG_16MS;
	else if (ms == 32)
		val = SC8548_WD2_CFG_32MS;
	else
		val = SC8548_WD2_CFG_16MS;

	val <<= SC8548_WD2_CFG_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_3D,
				SC8548_WD2_CFG_MASK, val);
	return ret;
}

static int sc8548_wdt2_enabled(struct sc8548 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8548_WD2_ENABLE;
	else
		val = SC8548_WD2_DISABLE;

	val <<= SC8548_WD2_EN_SHIFT;

	ret = sc8548_update_bits(sc, SC8548_REG_3D,
				SC8548_WD2_EN_MASK, val);
	return ret;
}
#endif
//*********************************************************************

/*
 * interrupt does nothing, just info event chagne, other module could get info
 * through power supply interface
 */
static void sc8548_check_fault_status(struct sc8548 *sc);
static irqreturn_t sc8548_charger_interrupt(int irq, void *dev_id)
{
	struct sc8548 *sc = dev_id;

	sc_dbg("INT OCCURED\n");
#if 1
	mutex_lock(&sc->irq_complete);
	sc->irq_waiting = true;
	if (!sc->resume_completed) {
		dev_dbg(sc->dev, "IRQ triggered before device-resume\n");
		if (!sc->irq_disabled) {
			disable_irq_nosync(irq);
			sc->irq_disabled = true;
		}
		mutex_unlock(&sc->irq_complete);
		return IRQ_HANDLED;
	}
	sc->irq_waiting = false;
#if 1
	/* TODO */
	sc8548_check_fault_status(sc);
	wake_up_interruptible(&sc->wq);
#endif

#if 0
	sc8548_dump_reg(sc);
#endif
	mutex_unlock(&sc->irq_complete);
#endif
	//power_supply_changed(sc->fc2_psy);

	return IRQ_HANDLED;
}

#if IS_ENABLED(CONFIG_TRAN_AW95016)
static void sc8548_irq_handler_work(struct work_struct *work)
{
	struct sc8548 *sc = NULL;

	pr_info("%s\n", __func__);

	if(IS_ERR_OR_NULL(ex_sc)){
		pr_err("%s g_chip is NULL\n", __func__);
		return;
	}

	sc = ex_sc;
	sc8548_check_fault_status(sc);
	wake_up_interruptible(&sc->wq);
}
#endif

static inline void sc8548_set_notify(struct sc8548 *sc,bool flag,
				     enum sc8548_notify notify)
{
	if(sc->notify_disable || !flag)
		return;

	mutex_lock(&sc->notify_lock);
	sc->notify |= BIT(notify);
	mutex_unlock(&sc->notify_lock);
}

static int sc8548_notify_task_threadfn(void *data)
{
	int i;
	struct sc8548 *sc = data;

	while (!kthread_should_stop()) {
		wait_event_interruptible(sc->wq, sc->notify != 0 ||
					 kthread_should_stop());
		if (kthread_should_stop())
			goto out;
		pm_stay_awake(sc->dev);
		mutex_lock(&sc->notify_lock);
		for (i = 0; i < SC8548_NOTIFY_MAX; i++) {
			if (sc->notify & BIT(i)) {
				sc->notify &= ~BIT(i);
				mutex_unlock(&sc->notify_lock);
				charger_dev_notify(sc->chg_dev,
						   sc8548_chgdev_notify_map[i]);
				mutex_lock(&sc->notify_lock);
			}
		}
		mutex_unlock(&sc->notify_lock);
		pm_relax(sc->dev);
	}
out:
	return 0;
}

static void sc8548_dpdm_check_work_func(struct work_struct* work) 
{
	struct sc8548 *sc = container_of(work, struct sc8548,
						dpdm_check_work.work);
	u8 val = 0;
	if (sc->sc8548e) {
		return;
	}
	if (sc->is_rfc_ta) {
		pm_stay_awake(sc->dev);
		sc8548_read_byte(sc, SC8548_REG_21, &val);
		if (!(val & (1 << SC8548_DPDM_EN_SHIFT))) {
			sc_err("%s : dpdm reset triggerd!\n", __func__);
			sc8548_dpdm_enabled(sc,true);
			sc8548_dpdm_pull_up_enabled(sc,true);
		}

		schedule_delayed_work(&sc->dpdm_check_work, msecs_to_jiffies(40));
		pm_relax(sc->dev);
	}
}

static int sc8548_detect_device(struct sc8548 *sc)
{
	int ret;
	u8 data;

	ret = sc8548_read_byte(sc, SC8548_REG_36, &data);
	if (ret == 0) {
		if(data != SC8548_DEVICE_ID && data != SC8548D_DEVICE_ID) {
			sc_err("%s : device id %02x fail\n", __func__, data);
			return -ENOMEM;
		 }
	}
	if ((data == SC8548D_DEVICE_ID) && (sc->client->addr == SC8548E_DEVICE_ID)) {
		sc->sc8548e = true;
	} else {
		sc->sc8548e = false;
	}
	sc->dev_id = data;
	return ret;
}

static int sc8548_parse_dt(struct sc8548 *sc, struct device *dev)
{
	int ret;
	struct device_node *np = dev->of_node;

	sc->cfg = devm_kzalloc(dev, sizeof(struct sc8548_cfg),
					GFP_KERNEL);

	if (!sc->cfg)
		return -ENOMEM;

	sc->cfg->bat_ovp_disable = of_property_read_bool(np,
			"sc,sc8548,bat-ovp-disable");
	sc->cfg->bus_ovp_disable = of_property_read_bool(np,
			"sc,sc8548,bus-ovp-disable");
	sc->cfg->bus_ocp_disable = of_property_read_bool(np,
			"sc,sc8548,bus-ocp-disable");

	ret = of_property_read_u32(np, "sc,sc8548,bat-ovp-threshold",
			&sc->cfg->bat_ovp_th);
	if (ret) {
		sc_err("failed to read bat-ovp-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8548,bat-ocp-threshold",
			&sc->cfg->bat_ocp_th);
	if (ret) {
		sc_err("failed to read bat-ocp-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8548,ac-ovp-threshold",
			&sc->cfg->ac_ovp_th);
	if (ret) {
		sc_err("failed to read ac-ovp-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8548,bus-ovp-threshold",
			&sc->cfg->bus_ovp_th);
	if (ret) {
		sc_err("failed to read bus-ovp-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8548,bus-ocp-threshold",
			&sc->cfg->bus_ocp_th);
	if (ret) {
		sc_err("failed to read bus-ocp-threshold\n");
		return ret;
	}

	ret = of_property_read_u32(np, "sc,sc8548,sense-resistor-mohm",
			&sc->cfg->sense_r_mohm);
	if (ret) {
		sc_err("failed to read sense-resistor-mohm\n");
		return ret;
	}

	ret = of_property_read_string(np, "chg_name", &sc->cfg->chg_name);
	if (ret) {
		sc_err("failed to read sc8548 chg_name\n");
		return ret;
	}

	sc->is_master = of_property_read_bool(np, "is_master");
	sc->set_vil = of_property_read_bool(np, "set_vil");
	/*if (!sc->is_master && !strcmp(sc->cfg->chg_name,"primary_dvchg"))
		sc->is_master = true;*/
	sc->exio_irq = of_property_read_bool(np, "sc,exio_irq");
	sc_info("%s: chg_name:%s, is_master:%d, is_exio_irq=%d, vil=%d\n",__func__, sc->cfg->chg_name, sc->is_master, sc->exio_irq, sc->set_vil);
	if(!sc->exio_irq){
	sc->cfg->irq_gpio = devm_gpiod_get(sc->dev, "sc8548,intr", GPIOD_IN);
	if (IS_ERR(sc->cfg->irq_gpio))
		return PTR_ERR(sc->cfg->irq_gpio);
	}
	return 0;
}

static int sc8548_init_protection(struct sc8548 *sc)
{
	int ret;

	ret = sc8548_enable_batovp(sc, !sc->cfg->bat_ovp_disable);
	sc_info("%s bat ovp %s\n",
		sc->cfg->bat_ovp_disable ? "disable" : "enable",
		!ret ? "successfullly" : "failed");

	ret = sc8548_enable_busovp(sc, !sc->cfg->bus_ovp_disable);
	sc_info("%s bus ovp %s\n",
		sc->cfg->bus_ovp_disable ? "disable" : "enable",
		!ret ? "successfullly" : "failed");

	ret = sc8548_enable_busocp(sc, !sc->cfg->bus_ocp_disable);
	sc_info("%s bus ocp %s\n",
		sc->cfg->bus_ocp_disable ? "disable" : "enable",
		!ret ? "successfullly" : "failed");

	ret = sc8548_set_batovp_th(sc, sc->cfg->bat_ovp_th);
	sc_info("set bat ovp th %d %s\n", sc->cfg->bat_ovp_th,
		!ret ? "successfully" : "failed");

	ret = sc8548_set_batocp_th(sc, sc->cfg->bat_ocp_th);
	sc_info("set bat ocp threshold %d %s\n", sc->cfg->bat_ocp_th,
		!ret ? "successfully" : "failed");

	ret = sc8548_set_acovp_th(sc, sc->cfg->ac_ovp_th);
	sc_info("set ac ovp threshold %d %s\n", sc->cfg->ac_ovp_th,
		!ret ? "successfully" : "failed");

	ret = sc8548_set_busovp_th(sc, sc->cfg->bus_ovp_th);
	sc_info("set bus ovp threshold %d %s\n", sc->cfg->bus_ovp_th,
		!ret ? "successfully" : "failed");

	ret = sc8548_set_busocp_th(sc, sc->cfg->bus_ocp_th);
	sc_info("set bus ocp threshold %d %s\n", sc->cfg->bus_ocp_th,
		!ret ? "successfully" : "failed");

	ret = sc8548_set_sense_resistor(sc, sc->cfg->sense_r_mohm);
	sc_info("set sense r mohm %d %s\n", sc->cfg->sense_r_mohm,
		!ret ? "successfully" : "failed");

	/* add for zhushaoan */
	sc8548_enable_batocp(sc, false);

	return 0;
}

static int sc8548_init_adc(struct sc8548 *sc)
{
	sc8548_set_adc_scanrate(sc, false);
	sc8548_set_adc_scan(sc, ADC_IBUS, true);
	sc8548_set_adc_scan(sc, ADC_VBUS, true);
	sc8548_set_adc_scan(sc, ADC_VOUT, true);
	sc8548_set_adc_scan(sc, ADC_VBAT, true);
	/* zhushaoan changed */
	sc8548_set_adc_scan(sc, ADC_IBAT, false);
	/* sc8548_set_adc_scan(sc, ADC_IBAT, true); */
	sc8548_set_adc_scan(sc, ADC_TDIE, true);
	sc8548_set_adc_scan(sc, ADC_VAC, true);

	sc8548_enable_adc(sc, false);

	return 0;
}

static int sc8548_init_dpdm(struct sc8548 *sc)
{
	if (sc->is_master) {
		sc8548_dm_500k_pd_enabled(sc, true);
		sc8548_dp_500k_pd_enabled(sc, true);

		if (sc->dev_id == SC8548_DEVICE_ID) {
			sc8548_dpdm_enabled(sc, true);
		}
	}
	/* zhushaoan changed */
	sc8548_dm_20k_pd_enabled(sc, false);
	/* sc8548_dm_20k_pd_enabled(sc, true); */
	sc8548_dp_20k_pd_enabled(sc, false);

	sc8548_dm_sink_enabled(sc, false);
	sc8548_dp_sink_enabled(sc, false);

	sc8548_dp_source_capability(sc, 250);

	sc8548_dpdmovp_enabled(sc, true);
	sc8548_set_dm_buf(sc, 600);
	sc8548_set_dp_buf(sc, 600);
	sc8548_dm_buf_enabled(sc, false);
	sc8548_dp_buf_enabled(sc, false);

	return 0;
}

static int sc8548_init_regulation(struct sc8548 *sc)
{
	if (sc->dev_id == SC8548_DEVICE_ID) {
		sc8548_set_ibat_regulation(sc, 300);
		sc8548_reg_timeout_enabled(sc, true);
		sc8548_set_ibus_regulation(sc, 2400);
		sc8548_set_vbat_regulation(sc, 50);
		sc8548_set_vdrop_deglitch(sc, 5000);
		sc8548_set_vdrop_ovp_th(sc, 400);
		sc8548_vbat_regulation_enabled(sc, false);
		sc8548_ibat_regulation_enabled(sc, false);
		sc8548_ibus_regulation_enabled(sc, false);
	}

	return 0;
}

static int sc8548_init_device(struct sc8548 *sc)
{
	sc8548_reg_reset(sc, true);
	sc8548_set_wdt(sc, 0);
	sc8548_set_i2c_vil(sc,1);
	sc8548_set_ss_timeout(sc, 1280);

	sc8548_init_protection(sc);
	sc8548_init_adc(sc);

	sc8548_init_regulation(sc);

	sc8548_init_dpdm(sc);

	sc8548_set_freq_shift(sc,SC8548_FREQ_SHIFT_NORMINAL);

	return 0;
}

static int sc8548_set_present(struct sc8548 *sc, bool present)
{
	sc->usb_present = present;

	if (present)
		sc8548_init_device(sc);
	return 0;
}

static ssize_t sc8548_show_registers(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct sc8548 *sc = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sc8548");
	for (addr = 0x0; addr <= 0x3D; addr++) {
		if((addr <= 0x24) || (addr >= 0x3C && addr <= 0x3D)
			|| addr == 0x36) {
			ret = sc8548_read_byte(sc, addr, &val);
			if (ret == 0) {
				len = snprintf(tmpbuf, PAGE_SIZE - idx,
						"Reg[%.2X] = 0x%.2x\n", addr, val);
				memcpy(&buf[idx], tmpbuf, len);
				idx += len;
			}
		}
	}

	return idx;
}

static ssize_t sc8548_store_register(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct sc8548 *sc = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;
	ret = sscanf(buf, "%x %x", &reg, &val);
	pr_info("%s: reg = 0x%x val = %d\n",__func__,reg,val);
	if (ret == 2 && reg <= 0x3D)
		sc8548_write_byte(sc, (unsigned char)reg, (unsigned char)val);

	return count;
}

static DEVICE_ATTR(registers, 0660, sc8548_show_registers, sc8548_store_register);

static void sc8548_create_device_node(struct device *dev)
{
	device_create_file(dev, &dev_attr_registers);
}

static enum power_supply_property sc8548_charger_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	//POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	//POWER_SUPPLY_PROP_CHARGE_MODE,
};

static int sc8548_charger_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct sc8548 *sc = power_supply_get_drvdata(psy);

	/* int result; */
	int ret = 0;

	switch (psp) {
	/*case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		sc8548_check_charge_enabled(sc, &sc->charge_enabled);
		val->intval = sc->charge_enabled;
		break;*/
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = sc->usb_present;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		/* ret = sc8548_get_adc_data(sc, ADC_VBAT, &result); */
		/* if (!ret) */
		/*         sc->vbat_volt = result; */

		/* val->intval = sc->vbat_volt; */
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		/* ret = sc8548_get_adc_data(sc, ADC_IBUS, &result); */
		/* if (!ret) */
		/*         sc->ibus_curr = result; */

		/* val->intval = sc->ibus_curr; */
		val->intval = 0;
		break;
		/*
	case POWER_SUPPLY_PROP_CHARGE_MODE:
		ret = sc8548_get_charge_mode(sc);
		
		val->intval = sc->charger_mode;
		break;*/
	default:
		return -EINVAL;

	}

	return ret;
}

static int sc8548_charger_set_property(struct power_supply *psy,
					   enum power_supply_property prop,
					   const union power_supply_propval *val)
{
	struct sc8548 *sc = power_supply_get_drvdata(psy);

	sc_err("prop = %d\n",  prop);
	switch (prop) {
	/*case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		sc8548_enable_charge(sc, val->intval);
		sc8548_check_charge_enabled(sc, &sc->charge_enabled);
		sc_info("POWER_SUPPLY_PROP_CHARGING_ENABLED: %s\n",
				val->intval ? "enable" : "disable");
		break;*/
	case POWER_SUPPLY_PROP_PRESENT:
		sc8548_set_present(sc, val->intval);
		break;
	/*case POWER_SUPPLY_PROP_CHARGE_MODE:
		sc8548_set_charge_mode(sc, val->intval);
		break;*/
	default:
		return -EINVAL;
	}

	return 0;
}

static int sc8548_charger_is_writeable(struct power_supply *psy,
					   enum power_supply_property prop)
{
	int ret;

	switch (prop) {
	/*case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		ret = 1;
		break;*/
	default:
		ret = 0;
		break;
	}
	return ret;
}

static int sc8548_psy_register(struct sc8548 *sc)
{
	sc->psy_cfg.drv_data = sc;
	sc->psy_cfg.of_node = sc->dev->of_node;

	sc->psy_desc.name = sc->cfg->chg_name;
	sc->psy_desc.type = POWER_SUPPLY_TYPE_MAINS;
	sc->psy_desc.properties = sc8548_charger_props;
	sc->psy_desc.num_properties = ARRAY_SIZE(sc8548_charger_props);
	sc->psy_desc.get_property = sc8548_charger_get_property;
	sc->psy_desc.set_property = sc8548_charger_set_property;
	sc->psy_desc.property_is_writeable = sc8548_charger_is_writeable;

	sc->fc2_psy = devm_power_supply_register(sc->dev, 
			&sc->psy_desc, &sc->psy_cfg);
	if (IS_ERR(sc->fc2_psy)) {
		sc_err("failed to register fc2_psy\n");
		return PTR_ERR(sc->fc2_psy);
	}

	sc_info("%s power supply register successfully\n", sc->psy_desc.name);

	return 0;
}

static ADC_CH plat_channel_to_sc_channel(enum adc_channel plat_chan)
{
	ADC_CH sc_channel;

	switch(plat_chan) {
	case ADC_CHANNEL_VBUS:
		sc_channel = ADC_VBUS;
		break;
	case ADC_CHANNEL_VBAT:
		sc_channel = ADC_VBAT;
		break;
	case ADC_CHANNEL_IBUS:
		sc_channel = ADC_IBUS;
		break;
	case ADC_CHANNEL_IBAT:
		sc_channel = ADC_IBAT;
		break;
	case ADC_CHANNEL_TEMP_JC:
		sc_channel = ADC_TDIE;
		break;
	case ADC_CHANNEL_VOUT:
		sc_channel = ADC_VOUT;
		break;
	case ADC_CHANNEL_TSBUS:
		sc_channel = ADC_TBUS;
		break;
	default: 
		sc_channel = ADC_MAX_NUM;
		break;
	}

//sc_info("%s : adc channel:%d\n", __func__, sc_channel);
	return sc_channel;	
}

static int sc8548_enable_ovpgate(struct sc8548 *sc, bool en)
{
	int ret = 0;
	u8 val;

	if (sc->dev_id == SC8548_DEVICE_ID)
		return ret;

	if (en)
		ret = sc8548_update_bits(sc, SC8548_REG_0C,
				SC8548_OVPGATE_EN_MASK, SC8548_OVPGATE_EN_SHIFT);
	else
		ret = sc8548_update_bits(sc, SC8548_REG_0C,
				SC8548_OVPGATE_EN_MASK, SC8548_OVPGATE_DISEN_SHIFT);

	ret = sc8548_read_byte(sc, SC8548_REG_0C, &val);
	sc_info("%s, en : %d, val= %x\n", __func__, en, val);
	
	return ret;
}

static int sc8548_enable_otg(struct charger_device *chg_dev, bool en)
{
	int ret = 0;
	struct sc8548 *sc = charger_get_data(chg_dev);
	
	pr_info("%s, en : %d\n", __func__, en);

	ret = sc8548_enable_ovpgate(sc,en);

	//ret = sc8548_write_byte(sc, 0x3b, en << 2);
	if(ret <0)
		pr_info("%s, en otg fial", __func__);	

	return ret;
}

static int sc8548_enable_chg(struct charger_device *chg_dev, bool en)
{
	int ret = 0;
	struct sc8548 *sc = charger_get_data(chg_dev);

	if (en)
		sc8548_set_wdt(sc, 30000);
	else 
		sc8548_set_wdt(sc, 0);

	ret = sc8548_enable_charge(sc, en);

	return ret;
}

static int sc8548_is_chg_enabled(struct charger_device *chg_dev, bool *en)
{
	int ret;
	struct sc8548 *sc = charger_get_data(chg_dev);

	ret = sc8548_check_charge_enabled(sc, en);

	sc_info("%s : check cp enabled:%d, ret:%d\n", __func__, *en, ret);

	return ret;
}

static int sc8548_get_adc(struct charger_device *chg_dev,
	enum adc_channel chan, int *min, int *max)
{
	int ret;
	ADC_CH sc_channel;
	struct sc8548 *sc = charger_get_data(chg_dev);

	sc_channel = plat_channel_to_sc_channel(chan);
	if (sc_channel == ADC_MAX_NUM)
		return -EINVAL;

	ret = sc8548_get_adc_data(sc, sc_channel, min);
	if (ret < 0) {
		sc_err("%s : get adc fail:%d\n", __func__, sc_channel);
		return ret;
	}

	*max = *min;

	return ret;
}

static int sc8548_get_adc_accuracy(struct charger_device *chg_dev,
				   enum adc_channel chan, int *min, int *max)
{
	ADC_CH sc_channel;

	sc_channel = plat_channel_to_sc_channel(chan);

	if (sc_channel == ADC_MAX_NUM)
		return -EINVAL;
	*min = *max = sc8548_adc_accuracy_tbl[sc_channel];
	return 0;
}

static int sc8548_set_vbusovp(struct charger_device *chg_dev, u32 uV)
{
	int ret;
	int vbus_th_mv;
	struct sc8548 *sc = charger_get_data(chg_dev);

	vbus_th_mv = uV / 1000;
	if(vbus_th_mv >= SC8548_VBUS_OVP_MAX_VALUE_MV)
		vbus_th_mv = SC8548_VBUS_OVP_MAX_VALUE_MV;
	ret = sc8548_set_busovp_th(sc, vbus_th_mv);

	sc_info("%s : set vbus ovp uV:%d, vbus_th_mv:%d, ret:%d\n",
		__func__, uV, vbus_th_mv, ret);

	return ret;
}

static int sc8548_set_ibusocp(struct charger_device *chg_dev, u32 uA)
{
	int ret;
	int ibus_th_ma;
	struct sc8548 *sc = charger_get_data(chg_dev);

	ibus_th_ma = uA / 1000;
	if (ibus_th_ma >= SC8548_IBUS_OVP_MAX_VALUE_MA)
		ibus_th_ma = SC8548_IBUS_OVP_MAX_VALUE_MA;
	ret = sc8548_set_busocp_th(sc, ibus_th_ma);

	sc_info("%s : set ibus ocp uA:%d, ibus_th_ma:%d, ret:%d\n",
		__func__, uA, ibus_th_ma, ret);

	return ret;
}

static int sc8548_set_ibusucp(struct charger_device *chg_dev, bool en)
{
	u8 stat;
	struct sc8548 *sc = charger_get_data(chg_dev);
	static bool set_status = true;

	if (en == set_status)
		return 0;

	set_status = en;
	sc8548_update_bits(sc, SC8548_REG_05, SC8548_IBUS_UCP_DIS_MASK, !en << SC8548_IBUS_UCP_DIS_SHIFT);
	switch(sc->dev_id) {
	case SC8548D_DEVICE_ID:
		if (!en) {
			sc8548_update_bits(sc, SC8548_REG_05, 0x30, 0x3 << 4);
		}else {
			sc8548_update_bits(sc, SC8548_REG_05, 0x30, 0x0 << 4);
		}
		break;
	case SC8548_DEVICE_ID:
		if (!en) {
			sc8548_update_bits(sc, SC8548_REG_05, 0x20, 0x1 << 5);
		} else {
			sc8548_update_bits(sc, SC8548_REG_05, 0x20, 0x0 << 5);
		}
		break;
	default:
		break;
	}

	sc8548_read_byte(sc, SC8548_REG_05, &stat);
	/* sc_info("%s, en : %d, stat %x\n", __func__, en, stat); */
	return 0;
}

static int sc8548_set_vbatovp(struct charger_device *chg_dev, u32 uV)
{
	int ret;
	int vbat_th_mv;
	struct sc8548 *sc = charger_get_data(chg_dev);

	vbat_th_mv = uV / 1000;
	if (vbat_th_mv >= SC8548_VBAT_OVP_MAX_VALUE_MV)
		vbat_th_mv = SC8548_VBAT_OVP_MAX_VALUE_MV;

	ret = sc8548_set_batovp_th(sc, vbat_th_mv);

	sc_info("%s : set vbat ovp uV:%d, vbat_th_mv:%d, ret:%d\n",
		__func__, uV, vbat_th_mv, ret);

	return ret;
}

static int sc8548_set_ibatocp(struct charger_device *chg_dev, u32 uA)
{
	int ret;
	int ibat_th_ma;
	struct sc8548 *sc = charger_get_data(chg_dev);

	ibat_th_ma = uA / 1000;
	if (ibat_th_ma >= SC8548_IBAT_OVP_MAX_VALUE_MA)
		ibat_th_ma = SC8548_IBAT_OVP_MAX_VALUE_MA;
	ret = sc8548_set_batocp_th(sc, ibat_th_ma);

	sc_info("%s : set ibat ocp uA:%d, ibat_th_ma:%d, ret:%d\n",
		__func__, uA, ibat_th_ma, ret);

	return ret;
}

static int sc8548_get_reg04(struct sc8548 *sc)
{
	int ret;
        u8 val;

	ret = sc8548_read_byte(sc, SC8548_REG_04, &val);
	if (ret < 0) {
		sc_err("%s :read reg 04 fail\n", __func__);
	}else{
		if (val == SC8548_REG04_RESET_VAL) {
			sc_err("%s : need to set device val=%d\n", __func__, val);
			return 1;
		}
	}

	return 0;
}

static int sc8548_init_chip(struct charger_device *chg_dev)
{
	struct sc8548 *sc = charger_get_data(chg_dev);
	sc8548_enable_adc(sc, true);

	if (sc8548_get_reg04(sc))
		sc8548_init_device(sc);

	return 0;
}

static int sc8548_set_vbatovp_alarm(struct charger_device *chg_dev, u32 uV)
{
	return 0;
}

static int sc8548_set_vbusovp_alarm(struct charger_device *chg_dev, u32 uV)
{
	return 0;
}
#if 0
static int sc8548_is_vbushigerr(struct charger_device *chg_dev, bool *err)
{
	int ret;
	u8 stat = 0;
	struct sc8548 *sc = charger_get_data(chg_dev);
	
	ret = sc8548_read_byte(sc, SC8548_REG_06, &stat);
	if (ret < 0) {
		sc_err("%s : get vbus error stat SC8548_REG_06 fail\n", __func__);
		return ret;
	}
	
	sc_info("%s : get vbus error stat succ, stat:%d\n", __func__, stat);
		
	*err = (stat & SC8548_VBUS_ERRORHI_STAT_MASK) >> SC8548_VBUS_ERRORHI_STAT_SHIFT;

	return ret;
}
#endif
static int sc8548_is_vbuslowerr(struct charger_device *chg_dev, bool *err)
{
	int ret;
	u8 stat = 0;
	struct sc8548 *sc = charger_get_data(chg_dev);
	
	ret = sc8548_read_byte(sc, SC8548_REG_06, &stat);
	if (ret < 0) {
		sc_err("%s : get vbus error stat SC8548_REG_06 fail\n", __func__);
		return ret;
	}

	sc_info("%s : get vbus error stat succ, stat:%d\n", __func__, stat);
		
	*err = (stat & SC8548_VBUS_ERRORLO_STAT_MASK) >> SC8548_VBUS_ERRORLO_STAT_SHIFT;

	return ret;
}

static int sc8548_adc_init(struct charger_device *chg_dev, bool en)
{
	int ret;
	struct sc8548 *sc = charger_get_data(chg_dev);

	ret = sc8548_enable_adc(sc, en);
	msleep(20);

	return ret;
}

static int sc8548_chg_plug_in(struct charger_device *chg_dev)
{
	return 0;
}

static int sc8548_plug_out(struct charger_device *chg_dev)
{
	int ret;
	struct sc8548 *sc = charger_get_data(chg_dev);

	sc_err("%s: sc8548_plug_out\n", __func__);
	ret = sc8548_write_byte(sc, 0x21, 0x00);
	ret = sc8548_write_byte(sc, 0x22, 0x00);	
	ret = sc8548_write_byte(sc, 0x3D, 0x01);
	ret = sc8548_enable_adc(sc, false);	

	return ret;
}

static int sc8548_rfc_reset(struct charger_device *chg_dev)
{
	int ret;
	struct sc8548 *sc = charger_get_data(chg_dev);
	sc->is_rfc_ta = false;

	sc_info("%s: do rfc reset start!\n", __func__);
	if ((sc->dev_id == SC8548D_DEVICE_ID) && (!sc->sc8548e))
		cancel_delayed_work_sync(&sc->dpdm_check_work);

	ret = sc8548_write_byte(sc, 0x21, 0x00);
	/* ret = sc8548_dm_500k_pd_enabled(sc, false);
	 * ret = sc8548_dp_500k_pd_enabled(sc, false);
	 * ret = sc8548_dm_20k_pd_enabled(sc, false);
	 * ret = sc8548_dp_20k_pd_enabled(sc, false);
	 * ret = sc8548_dm_sink_enabled(sc, false);
	 * ret = sc8548_dp_sink_enabled(sc, false);
	 * ret = sc8548_dp_source_capability(sc, 250);
	 * ret = sc8548_dpdm_enabled(sc, false);  */

	ret = sc8548_write_byte(sc, 0x22, 0x00);
	/* ret = sc8548_dpdmovp_enabled(sc, false); */
	/* ret = sc8548_set_dm_buf(sc, 600); */
	/* ret = sc8548_set_dp_buf(sc, 600); */
	/* ret = sc8548_dm_buf_enabled(sc, false); */
	/* ret = sc8548_dp_buf_enabled(sc, false);  */

	ret = sc8548_write_byte(sc, 0x3D, 0x01);
	/* ret = sc8548_i2c_dpdm_bypass_enabled(sc, false); */
	/* ret = sc8548_dpdm_pull_up_enabled(sc, false); */
	/* ret = sc8548_set_wdt2(sc, 16); */
	/* ret = sc8548_wdt2_enabled(sc, false);  */

	if (sc->is_master) {
		sc8548_dm_500k_pd_enabled(sc, true);
		sc8548_dp_500k_pd_enabled(sc, true);

		if (sc->dev_id == SC8548_DEVICE_ID) {
			sc8548_dpdm_enabled(sc, true);
		}
	}

	sc_info("%s: do rfc reset end!\n", __func__);

	return ret;
}

static int sc8548_set_dp_dm(struct charger_device *chg_dev,
	enum dpdm_ctrl_status dp_status, enum dpdm_ctrl_status dm_status, bool en_rfc_detect)
{
	struct sc8548 *sc = charger_get_data(chg_dev);
	int ret = 0;
	if(en_rfc_detect){
		if ((dp_status == DPDM_CTRL_HZ && dm_status == DPDM_CTRL_HZ) && 
			(sc->dev_id == SC8548D_DEVICE_ID)){		
			ret = sc8548_rfc_reset(chg_dev);
			sc_info("%s: do rfc none \n", __func__);
		}
	}
	return ret;
}

static int sc8548_get_dp_dm(struct charger_device *chg_dev, bool dp)
{
	return 0;
}

static int sc8548_i2c_trans(struct charger_device *chg_dev, bool high)
{
	int ret = 0;
	struct sc8548 *sc = charger_get_data(chg_dev);

	if(!sc->is_rfc_ta) {
		sc_info("%s:high:%d TA plug out or reset!\n", __func__, high);
		if(!high)
			mutex_unlock(&sc->i2c_rw_lock);
		return -EOPNOTSUPP;
	}

	sc_info("%s: i2c trans, high: %d\n", __func__, high);
	if (high) {
			ret = sc8548_write_byte(sc, 0x21, 0x01);
			ret = sc8548_write_byte(sc, 0x22, 0x17);
			switch (sc->dev_id) {
			case SC8548_DEVICE_ID:
				ret = sc8548_write_byte(sc, 0x3D, 0x30);
				break;
			case SC8548D_DEVICE_ID:
				if (!sc->sc8548e) {
					mdelay(25);
				}
				ret = sc8548_write_byte(sc, 0x3D, 0xC1);
				break;
			default:
				break;
			}

		mutex_lock(&sc->i2c_rw_lock);
	} else {
		mutex_unlock(&sc->i2c_rw_lock);
			switch (sc->dev_id) {
			case SC8548_DEVICE_ID:
				ret = sc8548_write_byte(sc, 0x3D, 0x10);
				break;
			case SC8548D_DEVICE_ID:
				ret = sc8548_write_byte(sc, 0x3D, 0x40);
				break;
			default:
				break;
			}
			ret = sc8548_write_byte(sc, 0x22, 0x3f);
	}
	udelay(100);
	sc_err("%s:i2c trans sus ret =%d,high=%d\n",__func__,ret,high);
	return ret;
}

static bool sc8548_dp_dm_33v(struct sc8548 *sc)
{
	u8 stat = 0;
	u8 dp_stat = 0;
	u8 dm_stat = 0;

	sc8548_write_byte(sc, 0x22, 0x3f);
	mdelay(20);

	sc8548_read_byte(sc, SC8548_REG_23, &stat);
	dp_stat = (stat & SC8548_VDP_RD_MASK) >> SC8548_VDP_RD_SHIFT;
	dm_stat = (stat & SC8548_VDM_RD_MASK) >> SC8548_VDM_RD_SHIFT;

	switch (sc->dev_id) {
	case SC8548_DEVICE_ID:
		if (dp_stat != SC8548_VDP_RD_3000MV_3300MV ||
	        dm_stat != SC8548_VDM_RD_3000MV_3300MV) { 
			sc_err("all 3.3 err stat:%d, dp_stat:%d, dm_stat:%d",
				stat, dp_stat, dm_stat);
			return false;
		}
		break;
	case SC8548D_DEVICE_ID:
		if (dp_stat != SC8548D_VDP_RD_3300MV ||
	        dm_stat != SC8548D_VDM_RD_3300MV) { 
			sc_err("all 3.3 err stat:%d, dp_stat:%d, dm_stat:%d",
				stat, dp_stat, dm_stat);
			return false;
		}
		break;
	default:
		break;
	}

	sc_err("all 3.3 stat:%d, dp_stat:%d, dm_stat:%d",
		stat, dp_stat, dm_stat);
	return true;
}

static bool sc8548_dp_00v_dm_33v(struct sc8548 *sc)
{
	u8 stat = 0;
	u8 dp_stat = 0;
	u8 dm_stat = 0;
	// DP = 0.6V  DM = 3.3V
	switch (sc->dev_id) {
	case SC8548_DEVICE_ID:
		sc8548_write_byte(sc, 0x22, 0x33);
		break;
	case SC8548D_DEVICE_ID:
		sc8548_write_byte(sc, 0x21, 0x11);
		sc8548_write_byte(sc, 0x22, 0x32);
		break;
	default:
		break;
	}

	mdelay(10);

	sc8548_read_byte(sc, SC8548_REG_23, &stat);
	dp_stat = (stat & SC8548_VDP_RD_MASK) >> SC8548_VDP_RD_SHIFT;
	dm_stat = (stat & SC8548_VDM_RD_MASK) >> SC8548_VDM_RD_SHIFT;

	switch (sc->dev_id) {
	case SC8548_DEVICE_ID:
		if (dp_stat != SC8548_VDP_RD_325MV_1000MV ||
				dm_stat != SC8548_VDM_RD_3000MV_3300MV) {  
			sc_err("0.6 3.3 err stat:%d, dp_stat:%d, dm_stat:%d",
				stat, dp_stat, dm_stat);
			return false;
		}
		break;
	case SC8548D_DEVICE_ID:
		if (dp_stat != SC8548D_VDP_RD_0MV_325MV ||
				dm_stat != SC8548D_VDM_RD_3300MV) {  
			sc_err("0.6 3.3 err stat:%d, dp_stat:%d, dm_stat:%d\n",
				stat, dp_stat, dm_stat);
			return false;
		}
		break;
	default:
		break;
	}

	sc_err("0.6 3.3 stat:%d, dp_stat:%d, dm_stat:%d\n",
		stat, dp_stat, dm_stat);
	return true;
}

static int sc8548_rfc_detect(struct charger_device *chg_dev, bool *is_rfc_ta)
{
	int ret;
	struct sc8548 *sc = charger_get_data(chg_dev);

	sc_err("%s:enable rfc start\n",__func__);
	ret = sc8548_write_byte(sc, 0x21, 0x01);
	if (ret) {
		sc_err("%s wirte byte fail\n", __func__);
		return ret;
	}

	if (!sc8548_dp_dm_33v(sc)) {
		*is_rfc_ta = false;
		sc8548_rfc_reset(chg_dev);
		return -EINVAL;
	}

	if (!sc8548_dp_00v_dm_33v(sc)) {
		*is_rfc_ta = false;
		sc8548_rfc_reset(chg_dev);
		return -EINVAL;
	}

	sc8548_write_byte(sc, 0x21, 0x01);
	sc8548_write_byte(sc, 0x22, 0x3f);
	sc8548_write_byte(sc, 0x3D, 0x11);

	*is_rfc_ta = true;
	sc->is_rfc_ta = true;
	if ((sc->dev_id == SC8548D_DEVICE_ID) && (!sc->sc8548e))
		schedule_delayed_work(&sc->dpdm_check_work, msecs_to_jiffies(40));

	sc_err("%s:enable rfc sus\n",__func__);
	return 0;
}

static int sc8548_dump_regs(struct charger_device *chg_dev)
{
	int ret;
	u8 addr;
	u8 val;
	struct sc8548 *sc = charger_get_data(chg_dev);	

	sc_err("%s: ++++++++++++++++++start++++++++++++++++\n", __func__);
	for (addr = 0x0; addr <= 0x3D; addr++) {
		if((addr <= 0x24) || (addr >= 0x3C && addr <= 0x3D)
			|| addr == 0x36) {
			ret = sc8548_read_byte(sc, addr, &val);
			if (ret == 0) {
				sc_err("Reg[%.2X] = 0x%.2x\n", addr, val);
			}
		}
	}
	sc_err("%s: ++++++++++++++++++end++++++++++++++++\n", __func__);

	return 0;
}

static int sc8548_set_run_spec(struct charger_device *chg_dev, u32 run_spec)
{
	/* struct hl7138_chip *chip = charger_get_data(chg_dev); */
	return 0;
}

static const struct charger_ops sc8548_chg_ops = {
	.enable = sc8548_enable_chg,
	.is_enabled = sc8548_is_chg_enabled,
	.get_adc = sc8548_get_adc,
	.set_vbusovp = sc8548_set_vbusovp,
	.set_ibusocp = sc8548_set_ibusocp,
	.set_vbatovp = sc8548_set_vbatovp,
	.set_ibatocp = sc8548_set_ibatocp,
	.set_ibusucp_enable = sc8548_set_ibusucp,

	.init_chip = sc8548_init_chip,
	.set_vbatovp_alarm = sc8548_set_vbatovp_alarm,
	.set_vbusovp_alarm = sc8548_set_vbusovp_alarm,

	//.is_vbushigerr = sc8548_is_vbushigerr,
	.is_vbuslowerr = sc8548_is_vbuslowerr,
	//.is_direct_charging_vbushigerr = sc8548_is_vbushigerr,
	.get_adc_accuracy = sc8548_get_adc_accuracy,
	.set_dp_dm = sc8548_set_dp_dm,
	.get_dp_dm = sc8548_get_dp_dm,
	.i2c_trans = sc8548_i2c_trans,
	.plug_out = sc8548_plug_out,
	.plug_in = sc8548_chg_plug_in,
	.cp_rfc_detect = sc8548_rfc_detect,
	.enable_otg = sc8548_enable_otg,
	//.close_wireless_enable_otg = close_wireless_sc8548_enable_otg,
	.dump_registers = sc8548_dump_regs,

	.init_adc = sc8548_adc_init,
	.soft_reset = sc8548_rfc_reset,
	.set_run_spec = sc8548_set_run_spec,
};

static int sc8548_register_chgdev(struct sc8548 *sc)
{
	sc->chg_prop.alias_name = sc->cfg->chg_name;
	sc->chg_dev = charger_device_register(sc->cfg->chg_name, sc->dev,
					      sc, &sc8548_chg_ops,
					      &sc->chg_prop);

	if (!sc->chg_dev)
		return -EINVAL;

	return 0;
}

static void sc8548_check_fault_status(struct sc8548 *sc) {
	int ret;
	u8 flag = 0;
	u8 stat = 0;

	mutex_lock(&sc->data_lock);

	ret = sc8548_read_byte(sc, SC8548_REG_02, &stat);
	if (!ret && stat & SC8548_AC_OVP_FLAG_MASK) {
		sc_err("SC8548 VAC OVP\n");
	}

	if (sc->dev_id == SC8548_DEVICE_ID) {
		ret = sc8548_read_byte(sc, SC8548_REG_03, &stat);
		if (!ret && stat & SC8548_VDROP_OVP_FLAG_MASK) {
			sc_err("SC8548 VDROP OVP\n");
		}
	}

	ret = sc8548_read_byte(sc, SC8548_REG_06, &stat);
	if (!ret) {
		if (stat & SC8548_TSHUT_FLAG_MASK) {
			sc_err("SC8548 TSHUT\n");
		}
		if (stat & SC8548_SS_TIMEOUT_FLAG_MASK) {
			sc_err("SC8548 SS TIMEOUT\n");
		}
		if (sc->dev_id == SC8548_DEVICE_ID && (stat & SC8548_REG_TIMEOUT_FLAG_MASK)) {
			sc_err("SC8548 REG TIMEOUT\n");
		}
		if (sc->dev_id == SC8548D_DEVICE_ID && (stat & SC8548D_VBUS_TH_CHG_EN_MASK)) {
			sc_err("SC8548D VBUS_TH_CHG_EN\n");
		}
		if (stat & SC8548_PIN_DIAG_FALL_FLAG_MASK) {
			sc_err("SC8548 PIN DIAG FAIL\n");
		}
	}

	ret = sc8548_read_byte(sc, SC8548_REG_09, &stat);
	if (!ret && stat & SC8548_WD_TIMEOUT_FLAG_MASK) {
		sc_err("SC8548 WDT TIMEOUT\n");
		sc8548_dump_regs(sc->chg_dev);
	}

	ret = sc8548_read_byte(sc, SC8548_REG_0D, &stat);
	if (!ret) {
		if (stat & SC8548_PMID2OUT_UVP_FLAG_MASK) {
			sc_err("SC8548 PMID2OUT UVP\n");
		}
		if (stat & SC8548_PMID2OUT_OVP_FLAG_MASK) {
			sc_err("SC8548 PMID2OUT OVP\n");
		}
	}
	sc_err("PMID2OUT_OVP_UVP:%x\n", stat);

	sc8548_read_byte(sc, SC8548_REG_05, &stat);
	sc_err("IBUS_OCP_REG:%x\n", stat);
	ret = sc8548_read_byte(sc, SC8548_REG_0E, &stat);
	if (!ret && stat)
		sc_err("FAULT_STAT = 0x%02X\n", stat);

	ret = sc8548_read_byte(sc, SC8548_REG_0F, &flag);
	if (!ret && flag)
		sc_err("FAULT_FLAG = 0x%02X\n", flag);

	if (!ret && flag != 0) {
		sc->out_ovp_fault = !!(flag & SC8548_VOUT_OVP_FLAG_MASK);
		sc8548_set_notify(sc,sc->out_ovp_fault,SC8548_NOTIFY_VOUTOVP);

		sc->bat_ovp_fault = !!(flag & SC8548_VBAT_OVP_FLAG_MASK);
		sc8548_set_notify(sc,sc->bat_ovp_fault,SC8548_NOTIFY_VBATOVP);

		sc->bat_ocp_fault = !!(flag & SC8548_IBAT_OCP_FLAG_MASK);
		sc8548_set_notify(sc,sc->bat_ocp_fault,SC8548_NOTIFY_IBATOCP);

		sc->bus_ovp_fault = !!(flag & SC8548_VBUS_OVP_FLAG_MASK);
		sc8548_set_notify(sc,sc->bus_ovp_fault,SC8548_NOTIFY_VBUSOVP);

		sc->bus_ocp_fault = !!(flag & SC8548_IBUS_OCP_FLAG_MASK);
		sc8548_set_notify(sc,sc->bus_ocp_fault,SC8548_NOTIFY_IBUSOCP);

		sc->bus_ucp_fault = !!(flag & SC8548_IBUS_UCP_FALL_FLAG_MASK);
		sc8548_set_notify(sc,sc->bus_ucp_fault,SC8548_NOTIFY_IBUSUCPF);
		sc->adp_insert_fault = !!(flag & SC8548_ADAPTER_INSERT_FLAG_MASK);
		sc->vbat_insert_fault = !!(flag & SC8548_VBAT_INSERT_FLAG_MASK);
	}

	mutex_unlock(&sc->data_lock);
}

static void determine_initial_status(struct sc8548 *sc)
{
	if (sc->client->irq)
		sc8548_charger_interrupt(sc->client->irq, sc);
}

static struct of_device_id sc8548_charger_match_table[] = {
	{.compatible = "sc,sc8548-standalone"},
	{.compatible = "sc,sc8548-secondary"},
	{ },
};

static int sc8548_charger_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct sc8548 *sc;
	const struct of_device_id *match;
	struct device_node *node = client->dev.of_node;
	int ret;

	sc = devm_kzalloc(&client->dev, sizeof(struct sc8548), GFP_KERNEL);
	if (!sc)
		return -ENOMEM;

	sc->dev = &client->dev;

	sc->client = client;

	mutex_init(&sc->i2c_rw_lock);
	mutex_init(&sc->data_lock);
	mutex_init(&sc->charging_disable_lock);
	mutex_init(&sc->irq_complete);
	mutex_init(&sc->notify_lock);
	INIT_DELAYED_WORK(&sc->dpdm_check_work, sc8548_dpdm_check_work_func);
	init_waitqueue_head(&sc->wq);

	sc->resume_completed = true;
	sc->irq_waiting = false;

	ret = sc8548_detect_device(sc);
	if (ret) {
		sc_err("No sc8548 device found!\n");
		return -ENODEV;
	}

	i2c_set_clientdata(client, sc);
	sc8548_create_device_node(&(client->dev));

	match = of_match_node(sc8548_charger_match_table, node);
	if (match == NULL) {
		sc_err("device tree match not found!\n");
		return -ENODEV;
	}

	ret = sc8548_parse_dt(sc, &client->dev);
	if (ret)
		return -EIO;

	ret = sc8548_init_device(sc);
	if (ret) {
		sc_err("Failed to init device\n");
		return ret;
	}

	ret = sc8548_psy_register(sc);
	if (ret)
		return ret;

	ret = sc8548_register_chgdev(sc);
	if (ret)
		return ret;

	sc->notify_task = kthread_run(sc8548_notify_task_threadfn, sc,
					"notify_thread");
	if (IS_ERR(sc->notify_task)) {
		dev_err(sc->dev, "%s run notify thread fail(%d)\n", __func__,
			ret);
		ret = PTR_ERR(sc->notify_task);
		goto err_1;
	}
#if IS_ENABLED(CONFIG_TRAN_AW95016)
	if(sc->exio_irq){
		ex_sc = sc;
		aw95016_set_dir(P1_1, AW95016_GPIO_INPUT);
		aw95016_pull_enable(P1_1, ENBALE);
		aw95016_set_pull_mode(P1_1,PULL_UP);

		aw95016_register_irq(P1_1, sc8548_irq_handler_work);
	 	/* enable irq */
	 	aw95016_irq_enable(P1_1, 1);
		goto ignore_intr;
	}
#endif
	client->irq = gpiod_to_irq(sc->cfg->irq_gpio);
	if (client->irq < 0) {
		sc_err("Failed to gpiod to irq\n");
		goto ignore_intr;	
	}

	if (client->irq) {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
				NULL, sc8548_charger_interrupt,
				IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				"sc8548 charger irq", sc);
		if (ret < 0) {
			sc_err("request irq for irq=%d failed, ret =%d\n",
							client->irq, ret);
			goto ignore_intr;
		}
		enable_irq_wake(client->irq);
	}
ignore_intr:
	device_init_wakeup(sc->dev, 1);

	determine_initial_status(sc);

	sc8548_update_bits(sc, SC8548_REG_0D,SC8548_PMID2OUT_UVP_MASK, SC8548_PMID2OUT_UVP_U_200MV << SC8548_PMID2OUT_UVP_SHIFT);
	switch (sc->dev_id) {
	case SC8548_DEVICE_ID:
		sc8548_update_bits(sc, SC8548_REG_0D,SC8548_PMID2OUT_OVP_MASK, SC8548_PMID2OUT_OVP_500MV << SC8548_PMID2OUT_OVP_SHIFT);
		sc8548_update_bits(sc, SC8548_REG_05, SC8548_IBUS_UCP_FALL_DEGLITCH_SET_MASK,
							SC8548_IBUS_UCP_FALL_DEGLITCH_SET_5MS << SC8548_IBUS_UCP_FALL_DEGLITCH_SET_SHIFT);
		break;
	case SC8548D_DEVICE_ID:
		sc8548_update_bits(sc, SC8548_REG_0D,SC8548_PMID2OUT_OVP_MASK, SC8548D_PMID2OUT_OVP_600MV << SC8548_PMID2OUT_OVP_SHIFT);
		break;
	default:
		break;
	}

	sc_info("sc8548 probe successfully!\n");

	return 0;

err_1:
	power_supply_unregister(sc->fc2_psy);
	return ret;
}

static inline bool is_device_suspended(struct sc8548 *sc)
{
	return !sc->resume_completed;
}

static int sc8548_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sc8548 *sc = i2c_get_clientdata(client);

	mutex_lock(&sc->irq_complete);
	sc->resume_completed = false;
	mutex_unlock(&sc->irq_complete);
	sc_err("Suspend successfully!");

	return 0;
}

static int sc8548_suspend_noirq(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sc8548 *sc = i2c_get_clientdata(client);

	if (sc->irq_waiting) {
		pr_err_ratelimited("Aborting suspend, an interrupt was detected while suspending\n");
		return -EBUSY;
	}
	return 0;
}

static int sc8548_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sc8548 *sc = i2c_get_clientdata(client);

	mutex_lock(&sc->irq_complete);
	sc->resume_completed = true;
	if (sc->irq_waiting) {
		sc->irq_disabled = false;
		enable_irq(client->irq);
		mutex_unlock(&sc->irq_complete);
		sc8548_charger_interrupt(client->irq, sc);
	} else {
		mutex_unlock(&sc->irq_complete);
	}

	power_supply_changed(sc->fc2_psy);
	sc_err("Resume successfully!");

	return 0;
}
static void sc8548_charger_remove(struct i2c_client *client)
{
	struct sc8548 *sc = i2c_get_clientdata(client);
	if (sc->notify_task)
		kthread_stop(sc->notify_task);
#if IS_ENABLED(CONFIG_TRAN_AW95016)
	if(sc->exio_irq)
		aw95016_unregister_irq(P1_1);
#endif
	power_supply_unregister(sc->fc2_psy);
	mutex_destroy(&sc->charging_disable_lock);
	mutex_destroy(&sc->data_lock);
	mutex_destroy(&sc->i2c_rw_lock);
	mutex_destroy(&sc->irq_complete);
	mutex_destroy(&sc->notify_lock);
	devm_kfree(&client->dev, sc);
}

static void sc8548_charger_shutdown(struct i2c_client *client)
{
	int ret = 0;
	struct sc8548 *sc = i2c_get_clientdata(client);

	if (!sc)
		return;

	dev_info(&client->dev, "%s start!\n", __func__);
#if IS_ENABLED(CONFIG_TRAN_AW95016)
	if(sc->exio_irq)
		aw95016_unregister_irq(P1_1);
#endif
	disable_irq_nosync(sc->irq);
	sc->is_rfc_ta = false;
	if ((sc->dev_id == SC8548D_DEVICE_ID) && (!sc->sc8548e))
		cancel_delayed_work(&sc->dpdm_check_work);
	ret |= sc8548_write_byte(sc, 0x21, 0x00);
	ret |= sc8548_write_byte(sc, 0x22, 0x00);	
	ret |= sc8548_write_byte(sc, 0x3D, 0x00);
	sc8548_enable_adc(sc, false);

	dev_info(&client->dev, "%s done(%d)\n", __func__,ret);
}

static const struct dev_pm_ops sc8548_pm_ops = {
	.resume	 = sc8548_resume,
	.suspend_noirq = sc8548_suspend_noirq,
	.suspend	= sc8548_suspend,
};

static const struct i2c_device_id sc8548_charger_id[] = {
	{"sc8548-standalone", 0},
	{},
};
	
static struct i2c_driver sc8548_charger_driver = {
	.driver	 = {
		.name   = "sc8548-charger",
		.owner  = THIS_MODULE,
		.of_match_table = sc8548_charger_match_table,
		.pm = &sc8548_pm_ops,
	},
	.id_table   = sc8548_charger_id,

	.probe	  = sc8548_charger_probe,
	.remove	 = sc8548_charger_remove,
	.shutdown   = sc8548_charger_shutdown,
};

module_i2c_driver(sc8548_charger_driver);

MODULE_DESCRIPTION("SC SC8548 Charge Pump Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Aiden-yu@southchip.com");
