// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Transsion Inc.
 */
#define pr_fmt(fmt)	"[sc8565] %s: " fmt, __func__
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
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/pinctrl/consumer.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <tc_charger_class.h>
#include <tc_algorithm_class.h>
#include "tc_sc8565.h"
typedef enum {
	ADC_IBUS,
	ADC_VBUS,
	ADC_VUSB,
	ADC_VWPC,
	ADC_VOUT,
	ADC_VBAT,
	ADC_IBAT,
	ADC_RSV,
	ADC_TDIE,
	ADC_TBUS,
	ADC_MAX_NUM,
}ADC_CH;
#define SC8565_ROLE_STANDALONE	  0
#define SC8565_ROLE_SLAVE		   1
#define SC8565_ROLE_MASTER		  2
static int sc8565_mode_data[] = {
	[SC8565_ROLE_STANDALONE] = SC8565_ROLE_STANDALONE,
	[SC8565_ROLE_SLAVE] = SC8565_ROLE_SLAVE,
	[SC8565_ROLE_MASTER] = SC8565_ROLE_MASTER,
};
#define	VOUT_INSERT		 BIT(3)
#define	VBUS_INSERT		 BIT(2)
#define VWPC_INSERT		 BIT(1)
#define VUSB_INSERT		 BIT(0)
#define	BAT_OVP_FAULT_SHIFT		 0
#define	BAT_OCP_FAULT_SHIFT		 1
#define	USB_OVP_FAULT_SHIFT		 2
#define	WPC_OVP_FAULT_SHIFT		 3
#define	BUS_OCP_FAULT_SHIFT		 4
#define	BUS_UCP_FAULT_SHIFT		 5
#define	PMID2OUT_OVP_FAULT_SHIFT	6
#define	PMID2OUT_UVP_FAULT_SHIFT	7
enum sc8565_notify {
	SC8565_NOTIFY_IBUSUCP = 0,
	SC8565_NOTIFY_IBUSOCP,
	SC8565_NOTIFY_VBUSOVP,
	SC8565_NOTIFY_IBATOCP,
	SC8565_NOTIFY_VBATOVP,
	SC8565_NOTIFY_VOUTOVP,
	SC8565_NOTIFY_VDROVP,
	SC8565_NOTIFY_MAX,
};
static const u32 sc8565_chgdev_notify_map[SC8565_NOTIFY_MAX] = {
	CHARGER_DEV_NOTIFY_IBUSUCP_FALL,
	CHARGER_DEV_NOTIFY_IBUSOCP,
	CHARGER_DEV_NOTIFY_VBUS_OVP,
	CHARGER_DEV_NOTIFY_IBATOCP,
	CHARGER_DEV_NOTIFY_BAT_OVP,
	CHARGER_DEV_NOTIFY_VOUTOVP,
	CHARGER_DEV_NOTIFY_VDROVP,
};
#define sc_err(fmt, ...)								\
do {											\
	if (sc->mode == SC8565_ROLE_MASTER)						\
		dev_err(sc->dev, "[sc8565-MASTER]:%s:" fmt, __func__, ##__VA_ARGS__);	\
	else if (sc->mode == SC8565_ROLE_SLAVE)					\
		dev_err(sc->dev, "[sc8565-SLAVE]:%s:" fmt, __func__, ##__VA_ARGS__);	\
	else										\
		dev_err(sc->dev, "[sc8565-STANDALONE]:%s:" fmt, __func__, ##__VA_ARGS__);\
} while(0);
#define sc_info(fmt, ...)								\
do {											\
	if (sc->mode == SC8565_ROLE_MASTER)						\
		dev_info(sc->dev, "[sc8565-MASTER]:%s:" fmt, __func__, ##__VA_ARGS__);	\
	else if (sc->mode == SC8565_ROLE_SLAVE)					\
		dev_info(sc->dev, "[sc8565-SLAVE]:%s:" fmt, __func__, ##__VA_ARGS__);	\
	else										\
		dev_info(sc->dev, "[sc8565-STANDALONE]:%s:" fmt, __func__, ##__VA_ARGS__);\
} while(0);
#define sc_dbg(fmt, ...)								\
do {											\
	if (sc->mode == SC8565_ROLE_MASTER)						\
		dev_dbg(sc->dev, "[sc8565-MASTER]:%s:" fmt, __func__, ##__VA_ARGS__);	\
	else if (sc->mode == SC8565_ROLE_SLAVE)					\
		dev_dbg(sc->dev, "[sc8565-SLAVE]:%s:" fmt, __func__, ##__VA_ARGS__);	\
	else										\
		dev_dbg(sc->dev, "[sc8565-STANDALONE]:%s:" fmt, __func__, ##__VA_ARGS__);\
} while(0);
struct sc8565_cfg {
	bool bat_ovp_disable;
	bool bat_ocp_disable;
	bool bus_ocp_disable;
	bool bus_ucp_disable;
	bool pmid2out_ovp_disable;
	bool pmid2out_uvp_disable;
	int bat_ovp_th;
	int bat_ocp_th;
	int bus_ovp_th;
	int bus_ocp_th;
	int usb_ovp_th;
	int wpc_ovp_th;
	int out_ovp_th;
	int pmid2out_ovp_th;
	int pmid2out_uvp_th;
	int sense_r_mohm;
	const char *chg_name;
};
struct sc8565 {
	struct device *dev;
	struct i2c_client *client;
	int revision;
	int mode;
	int irq_gpio;
	int irq;
	struct mutex data_lock;
	struct mutex i2c_rw_lock;
	struct mutex irq_complete;
	struct mutex notify_lock;
	struct mutex suspend_lock;
	bool irq_waiting;
	bool irq_disabled;
	bool resume_completed;
	bool batt_present;
	bool vbus_present;
	bool vusb_present;
	bool vwpc_present;
	bool usb_present;
	bool qb_enabled;
	bool charge_enabled;	/* Register bit status */
	bool ovpgate_state;
	bool wpcgate_state;
	int work_mode;
	int vbus_error;
	int sc_lpm_gpio;
	/* ADC reading */
	int vbat_volt;
	int vbus_volt;
	int vout_volt;
	int vusb_volt;
	int vwpc_volt;
	int ibat_curr;
	int ibus_curr;
	int die_temp;
	/* alarm/fault status */
	bool bat_ovp_fault;
	bool bat_ocp_fault;
	bool usb_ovp_fault;
	bool wpc_ovp_fault;
	bool bus_ocp_fault;
	bool bus_ucp_fault;
	bool bus_ovp_fault;
	bool vout_ovp_fault;
	bool pmid2out_ovp_fault;
	bool pmid2out_uvp_fault;
	struct sc8565_cfg *cfg;
	int skip_writes;
	int skip_reads;
	struct charger_properties chg_prop;
	struct charger_device *chg_dev;
	struct work_struct state_update_work;
	int notify;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pinctrl_i2c_clk_gpio;
	struct pinctrl_state *pinctrl_i2c_sda_gpio;
};
	
static const u32 sc8565_adc_accuracy_tbl[ADC_MAX_NUM] = {
	35000,	/* VBUS */
	150000,	/* IBUS */
	20000,	/* VBAT */
	200000,	/* IBAT */
	35000,	/* VTS */
	20000,	/* VOUT */
	4,	/* TDIE */
};
/************************************************************************/
static int __sc8565_read_byte(struct sc8565 *sc, u8 reg, u8 *data)
{
	s32 ret;
	mutex_lock(&sc->suspend_lock);
	ret = i2c_smbus_read_byte_data(sc->client, reg);
	mutex_unlock(&sc->suspend_lock);
	if (ret < 0) {
		sc_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}
	*data = (u8)ret;
	return 0;
}
static int __sc8565_write_byte(struct sc8565 *sc, int reg, u8 val)
{
	s32 ret;
	mutex_lock(&sc->suspend_lock);
	ret = i2c_smbus_write_byte_data(sc->client, reg, val);
	mutex_unlock(&sc->suspend_lock);
	if (ret < 0) {
		sc_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
			val, reg, ret);
		return ret;
	}
	return 0;
}
static int sc8565_read_byte(struct sc8565 *sc, u8 reg, u8 *data)
{
	int ret;
	if (sc->skip_reads) {
		*data = 0;
		return 0;
	}
	mutex_lock(&sc->i2c_rw_lock);
	ret = __sc8565_read_byte(sc, reg, data);
	mutex_unlock(&sc->i2c_rw_lock);
	return ret;
}
static int sc8565_write_byte(struct sc8565 *sc, u8 reg, u8 data)
{
	int ret;
	if (sc->skip_writes)
		return 0;
	mutex_lock(&sc->i2c_rw_lock);
	ret = __sc8565_write_byte(sc, reg, data);
	mutex_unlock(&sc->i2c_rw_lock);
	return ret;
}
static int sc8565_update_bits(struct sc8565*sc, u8 reg,
					u8 mask, u8 data)
{
	int ret;
	u8 tmp;
	if (sc->skip_reads || sc->skip_writes)
		return 0;
	mutex_lock(&sc->i2c_rw_lock);
	ret = __sc8565_read_byte(sc, reg, &tmp);
	if (ret) {
		sc_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}
	tmp &= ~mask;
	tmp |= data & mask;
	ret = __sc8565_write_byte(sc, reg, tmp);
	if (ret)
		sc_err("Failed: reg=%02X, ret=%d\n", reg, ret);
out:
	mutex_unlock(&sc->i2c_rw_lock);
	return ret;
}
/*********************************************************************/
static int sc8565_enable_batovp(struct sc8565 *sc, bool enable)
{
	u8 val;
	if (enable)
		val = REG01_VBAT_OVP_ENABLE;
	else
		val = REG01_VBAT_OVP_DISABLE;
	val <<= REG01_VBAT_OVP_DIS_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_01,
				REG01_VBAT_OVP_DIS_MASK, val);
}
static int sc8565_set_batovp_th(struct sc8565 *sc, int threshold)
{
	u8 val;
	if (threshold < REG01_VBAT_OVP_BASE)
		threshold = REG01_VBAT_OVP_BASE;
	else if (threshold > REG01_VBAT_OVP_MAX)
		threshold = REG01_VBAT_OVP_MAX;
	val = (threshold - REG01_VBAT_OVP_BASE) / REG01_VBAT_OVP_LSB;
	val <<= REG01_VBAT_OVP_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_01,
				REG01_VBAT_OVP_MASK, val);
}
static int sc8565_enable_batocp(struct sc8565 *sc, bool enable)
{
	u8 val;
	if (enable)
		val = REG02_IBAT_OCP_ENABLE;
	else
		val = REG02_IBAT_OCP_DISABLE;
	val <<= REG02_IBAT_OCP_DIS_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_02,
				REG02_IBAT_OCP_DIS_MASK, val);
}
static int sc8565_set_batocp_th(struct sc8565 *sc, int threshold)
{
	u8 val;
	if (threshold < REG02_IBAT_OCP_BASE)
		threshold = REG02_IBAT_OCP_BASE;
	else if (threshold > REG02_IBAT_OCP_MAX)
		threshold = REG02_IBAT_OCP_MAX;
	val = (threshold - REG02_IBAT_OCP_BASE) / REG02_IBAT_OCP_LSB;
	val <<= REG02_IBAT_OCP_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_02,
				REG02_IBAT_OCP_MASK, val);
}
static int sc8565_set_usbovp_th(struct sc8565 *sc, int threshold)
{
	u8 val;
	if (threshold == 6500)
		val = REG02_VUSB_OVP_6PV5;
	else
		val = (threshold - REG02_VUSB_OVP_BASE) / REG02_VUSB_OVP_LSB;
	val <<= REG03_VUSB_OVP_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_03,
				REG03_VUSB_OVP_MASK, val);
}
static int sc8565_set_wpcovp_th(struct sc8565 *sc, int threshold)
{
	u8 val;
	if (threshold == 6500)
		val = REG02_VWPC_OVP_6PV5;
	else
		val = (threshold - REG02_VWPC_OVP_BASE) / REG02_VWPC_OVP_LSB;
	val <<= REG04_VWPC_OVP_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_04,
				REG04_VWPC_OVP_MASK, val);
}
static int sc8565_set_busovp_th(struct sc8565 *sc, int threshold)
{
	u8 val;
	if (sc->work_mode == REG0E_FORWARD_4_1_CHARGER_MODE
		|| sc->work_mode == REG0E_REVERSE_1_4_CONVERTER_MODE) {
		if (threshold < REG05_VBUS_OVP_41MODE_BASE)
			threshold = REG05_VBUS_OVP_41MODE_BASE;
		else if (threshold > REG05_VBUS_OVP_41MODE_MAX)
			threshold = REG05_VBUS_OVP_41MODE_MAX;
		
		val = (threshold - REG05_VBUS_OVP_41MODE_BASE) / REG05_VBUS_OVP_41MODE_LSB;
	}
	else if (sc->work_mode == REG0E_FORWARD_2_1_CHARGER_MODE
		|| sc->work_mode == REG0E_REVERSE_1_2_CONVERTER_MODE) {
		if (threshold < REG05_VBUS_OVP_21MODE_BASE)
			threshold = REG05_VBUS_OVP_21MODE_BASE;
		else if (threshold > REG05_VBUS_OVP_21MODE_MAX)
			threshold = REG05_VBUS_OVP_21MODE_MAX;
		
		val = (threshold - REG05_VBUS_OVP_21MODE_BASE) / REG05_VBUS_OVP_21MODE_LSB;
	}
	else {
		if (threshold < REG05_VBUS_OVP_11MODE_BASE)
			threshold = REG05_VBUS_OVP_11MODE_BASE;
		else if (threshold > REG05_VBUS_OVP_11MODE_MAX)
			threshold = REG05_VBUS_OVP_11MODE_MAX;
		
		val = (threshold - REG05_VBUS_OVP_11MODE_BASE) / REG05_VBUS_OVP_11MODE_LSB;
	}
	val <<= REG05_VBUS_OVP_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_05,
				REG05_VBUS_OVP_MASK, val);
}
static int sc8565_set_outovp_th(struct sc8565 *sc, int threshold)
{
	u8 val;
	if (threshold < REG05_VOUT_OVP_BASE)
		threshold = REG05_VOUT_OVP_BASE;
	
	val = (threshold - REG05_VOUT_OVP_BASE) / REG05_VOUT_OVP_LSB;
	val <<= REG05_VOUT_OVP_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_05,
				REG05_VOUT_OVP_MASK, val);
}
static int sc8565_enable_busocp(struct sc8565 *sc, bool enable)
{
	u8 val;
	if (enable)
		val = REG05_IBUS_OCP_ENABLE;
	else
		val = REG05_IBUS_OCP_DISABLE;
	val <<= REG06_IBUS_OCP_DIS_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_06,
				REG06_IBUS_OCP_DIS_MASK, val);
}
static int sc8565_set_busocp_th(struct sc8565 *sc, int threshold)
{
	u8 val;
	if (threshold < REG06_IBUS_OCP_BASE)
		threshold = REG06_IBUS_OCP_BASE;
	else if (threshold > REG06_IBUS_OCP_MAX)
		threshold = REG06_IBUS_OCP_MAX;
	val = (threshold - REG06_IBUS_OCP_BASE) / REG06_IBUS_OCP_LSB;
	val <<= REG06_IBUS_OCP_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_06,
				REG06_IBUS_OCP_MASK, val);
}
static int sc8565_enable_ibus_ucp(struct sc8565 *sc, bool enable)
{
	u8 val;
	if (enable)
		val = REG07_IBUS_UCP_ENABLE;
	else
		val = REG07_IBUS_UCP_DISABLE;
	val <<= REG07_IBUS_UCP_DIS_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_07,
				REG07_IBUS_UCP_DIS_MASK, val);
}
static int sc8565_set_ibus_ucp_fall_dg(struct sc8565 *sc, u8 time)
{
	u8 val = time;
	val <<= REG07_IBUS_UCP_FALL_DG_SET_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_07,
				REG07_IBUS_UCP_FALL_DG_SET_MASK, val);
}
static int sc8565_enable_pmid2outovp(struct sc8565 *sc, bool enable)
{
	u8 val;
	if (enable)
		val = REG08_PMID2OUT_OVP_ENABLE;
	else
		val = REG08_PMID2OUT_OVP_DISABLE;
	val <<= REG08_PMID2OUT_OVP_DIS_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_08,
				REG08_PMID2OUT_OVP_DIS_MASK, val);
}
static int sc8565_set_pmid2outovp_th(struct sc8565 *sc, int threshold)
{
	u8 val;
	if (threshold < REG08_PMID2OUT_OVP_BASE)
		threshold = REG08_PMID2OUT_OVP_BASE;
	val = (threshold - REG08_PMID2OUT_OVP_BASE) / REG08_PMID2OUT_OVP_LSB;
	val <<= REG08_PMID2OUT_OVP_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_08,
				REG08_PMID2OUT_OVP_MASK, val);
}
static int sc8565_enable_pmid2outuvp(struct sc8565 *sc, bool enable)
{
	u8 val;
	if (enable)
		val = REG09_PMID2OUT_UVP_ENABLE;
	else
		val = REG09_PMID2OUT_UVP_DISABLE;
	val <<= REG09_PMID2OUT_UVP_DIS_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_09,
				REG09_PMID2OUT_UVP_DIS_MASK, val);
}
static int sc8565_set_pmid2outuvp_th(struct sc8565 *sc, int threshold)
{
	u8 val;
	if (threshold < REG09_PMID2OUT_UVP_BASE)
		threshold = REG09_PMID2OUT_UVP_BASE;
	val = (threshold - REG09_PMID2OUT_UVP_BASE) / REG09_PMID2OUT_UVP_LSB;
	val <<= REG09_PMID2OUT_UVP_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_09,
				REG09_PMID2OUT_UVP_MASK, val);
}
static int sc8565_enable_charge(struct sc8565 *sc, bool enable)
{
	u8 val;
	/* SC8565_REG_7C 0x01: IC workround CFIY error */
	sc8565_write_byte(sc, SC8565_REG_7C, 0x01);
	if (enable)
		val = REG0B_CP_ENABLE;
	else
		val = REG0B_CP_DISABLE;
	val <<= REG0B_CP_EN_SHIFT;
	sc_err("sc8565 charger %s\n", enable == false ? "disable" : "enable");
	return sc8565_update_bits(sc, SC8565_REG_0B,
				REG0B_CP_EN_MASK, val);
}
static int sc8565_check_charge_enabled(struct sc8565 *sc, bool *enabled)
{
	int ret;
	u8 val;
	ret = sc8565_read_byte(sc, SC8565_REG_0B, &val);
	if (!ret && (val & REG0B_CP_EN_MASK)) {
		ret = sc8565_read_byte(sc, SC8565_REG_0A, &val);	
		if (!ret && (val & REG0A_CP_SWITCHING_STAT_MASK)) {
			*enabled = true;
			return ret;
		}
	}
	
	*enabled = false;
	return ret;
}
__maybe_unused static int sc8565_enable_qb(struct sc8565 *sc, bool enable)
{
	u8 val;
	if (enable)
		val = REG0B_QB_ENABLE;
	else
		val = REG0B_QB_DISABLE;
	val <<= REG0B_QB_EN_SHIFT;
	sc_err("sc8565 qb %s\n", enable == false ? "disable" : "enable");
	return sc8565_update_bits(sc, SC8565_REG_0B,
				REG0B_QB_EN_MASK, val);
}
__maybe_unused static int sc8565_check_qb_enabled(struct sc8565 *sc, bool *enabled)
{
	int ret;
	u8 val;
	ret = sc8565_read_byte(sc, SC8565_REG_0B, &val);
	if (!ret)
		*enabled = !!(val & REG0B_QB_EN_MASK);
	return ret;
}
static int sc8565_enable_acdrv_manual(struct sc8565 *sc, bool enable)
{	
	u8 val;
	if (enable)
		val = REG0B_ACDRV_MANUAL_MODE;
	else
		val = REG0B_ACDRV_AUTO_MODE;
	val <<= REG0B_ACDRV_MANUAL_EN_SHIFT;
	sc_err("sc8565 acdrv_manual %s\n", enable == false ? "Auto mode" : "Manual mode");
	return sc8565_update_bits(sc, SC8565_REG_0B,
				REG0B_ACDRV_MANUAL_EN_MASK, val);
}
static int sc8565_enable_wpcgate(struct sc8565 *sc, bool enable)
{
	u8 val;
	if (enable)
		val = REG0B_WPCGATE_ENABLE;
	else
		val = REG0B_WPCGATE_DISABLE;
	val <<= REG0B_WPCGATE_EN_SHIFT;
	sc_err("sc8565 wpc gate %s\n", enable == false ? "disable" : "enable");
	return sc8565_update_bits(sc, SC8565_REG_0B,
				REG0B_WPCGATE_EN_MASK, val);
}
static int sc8565_enable_ovpgate(struct sc8565 *sc, bool enable)
{
	u8 val;
	if (enable)
		val = REG0B_OVPGATE_ENABLE;
	else
		val = REG0B_OVPGATE_DISABLE;
	val <<= REG0B_OVPGATE_EN_SHIFT;
	sc_err("sc8565 ovp gate %s\n", enable == false ? "disable" : "enable");
	return sc8565_update_bits(sc, SC8565_REG_0B,
				REG0B_OVPGATE_EN_MASK, val);
}
__maybe_unused static int sc8565_enable_vbuspd(struct sc8565 *sc, bool enable)
{
	u8 val;
	if (enable)
		val = REG0B_VBUS_PD_ENABLE;
	else
		val = REG0B_VBUS_PD_DISABLE;
	val <<= REG0B_VBUS_PD_EN_SHIFT;
	sc_err("sc8565 vbus pull down %s\n", enable == false ? 
		"disable" : "enable");
	return sc8565_update_bits(sc, SC8565_REG_0B,
				REG0B_VBUS_PD_EN_MASK, val);
}
__maybe_unused static int sc8565_enable_vwpcpd(struct sc8565 *sc, bool enable)
{
	u8 val;
	if (enable)
		val = REG0B_VWPC_PD_ENABLE;
	else
		val = REG0B_VWPC_PD_DISABLE;
	val <<= REG0B_VWPC_PD_EN_SHIFT;
	sc_err("sc8565 vwpc pull down %s\n", enable == false ? 
		"disable" : "enable");
	return sc8565_update_bits(sc, SC8565_REG_0B,
				REG0B_VWPC_PD_EN_MASK, val);
}
__maybe_unused static int sc8565_enable_vusbpd(struct sc8565 *sc, bool enable)
{
	u8 val;
	if (enable)
		val = REG0B_VUSB_PD_ENABLE;
	else
		val = REG0B_VUSB_PD_DISABLE;
	val <<= REG0B_VUSB_PD_EN_SHIFT;
	sc_err("sc8565 vusb pull down %s\n", enable == false ? 
		"disable" : "enable");
	return sc8565_update_bits(sc, SC8565_REG_0B,
				REG0B_VUSB_PD_EN_MASK, val);
}
static int sc8565_set_ss_timeout(struct sc8565 *sc, int timeout)
{
	u8 val;
	switch (timeout) {
	case 320:
		val = REG0D_SS_TIMEOUT_320MS;
		break;
	case 1280:
		val = REG0D_SS_TIMEOUT_1280MS;
		break;
	case 5120:
		val = REG0D_SS_TIMEOUT_5120MS;
		break;
	case 20480:
		val = REG0D_SS_TIMEOUT_20480MS;
		break;
	case 81920:
		val = REG0D_SS_TIMEOUT_81920MS;
		break;
	default:
		val = REG0D_SS_TIMEOUT_DISABLE;
		break;
	}
	val <<= REG0D_SS_TIMEOUT_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_0D,
				REG0D_SS_TIMEOUT_MASK,
				val);
}
static int sc8565_set_wdt(struct sc8565 *sc, int ms)
{
	u8 val;
	switch (ms) {
	case 200:
		val = REG0D_WD_TIMEOUT_0P2S;
		break;
	case 500:
		val = REG0D_WD_TIMEOUT_0P5S;
		break;
	case 1000:
		val = REG0D_WD_TIMEOUT_1S;
		break;
	case 5000:
		val = REG0D_WD_TIMEOUT_5S;
		break;
	case 30000:
		val = REG0D_WD_TIMEOUT_30S;
		break;
	default:
		val = REG0D_WD_TIMEOUT_DISABLE;
		break;
	}
	val <<= REG0D_WD_TIMEOUT_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_0D,
				REG0D_WD_TIMEOUT_MASK, val);
}
static int sc8565_set_freq(struct sc8565 *sc, int hz)
{
	u8 val = 0;
	if (hz < 300)
		hz = 300;
	else if (hz > 1075)
		hz = 1075;
	val = (hz - REG0C_FSW_SET_BASE) / REG0C_FSW_SET_LSB;
	val <<= REG0C_FSW_SET_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_0C,
				REG0C_FSW_SET_MASK, val);
}
static int sc8565_freq_dither_en(struct sc8565 *sc, bool en)
{
	u8 val = 0;
	if (en)
		val = REG0C_FREQ_DITHER_ENABLE;
	else
		val = REG0C_FREQ_DITHER_DISABLE;
	val <<= REG0C_FREQ_DITHER_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_0C,
				REG0C_FREQ_DITHER_MASK, val);
}
static int sc8565_set_sense_resistor(struct sc8565 *sc, int r_mohm)
{
	u8 val;
	if (r_mohm == 1)
		val = REG0E_SET_IBAT_SNS_1MHM;
	else
		val = REG0E_SET_IBAT_SNS_2MHM;
	val <<= REG0E_SET_IBAT_SNS_RES_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_0E,
				REG0E_SET_IBAT_SNS_RES_MASK,
				val);
}
static int sc8565_set_reg_reset(struct sc8565 *sc)
{
	u8 val = REG0E_REG_RESET;
	val <<= REG0E_REG_RST_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_0E,
				REG0E_REG_RST_MASK, val);
}
static int sc8565_set_operation_mode(struct sc8565 *sc, int operation_mode)
{
	u8 val;
	sc_info("sc8565 operation mode is : %d\n", operation_mode);
	switch (operation_mode) {
		case REG0E_FORWARD_4_1_CHARGER_MODE:
			val = REG0E_FORWARD_4_1_CHARGER_MODE;
			break;
		case REG0E_FORWARD_2_1_CHARGER_MODE:
			val = REG0E_FORWARD_2_1_CHARGER_MODE;
			break;
		case REG0E_FORWARD_1_1_CHARGER_MODE:
		case REG0E_FORWARD_1_1_CHARGER_MODE1:
			val = REG0E_FORWARD_1_1_CHARGER_MODE;
			break;
		case REG0E_REVERSE_1_4_CONVERTER_MODE:
			val = REG0E_REVERSE_1_4_CONVERTER_MODE;
			break;
		case REG0E_REVERSE_1_2_CONVERTER_MODE:
			val = REG0E_REVERSE_1_2_CONVERTER_MODE;
			break;
		case REG0E_REVERSE_1_1_CONVERTER_MODE:
		case REG0E_REVERSE_1_1_CONVERTER_MODE1:
			val = REG0E_REVERSE_1_1_CONVERTER_MODE;
			break;
		default:
			sc_err("sc8565 set operation mode fail : not have this mode!\n");
			return -1;
			break;
	}
	sc->work_mode = val;
	val <<= REG0E_MODE_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_0E,
				REG0E_MODE_MASK, val);
}
__maybe_unused static int sc8565_get_operation_mode(struct sc8565 *sc, int *operation_mode)
{
	int ret;
	u8 value;
	ret = sc8565_read_byte(sc, SC8565_REG_0E, &value);
	if (ret) {
		sc_err("sc8565 get operation mode fail\n");
		return -1;
	}
	*operation_mode = ((value >> REG0E_MODE_SHIFT) & REG0E_MODE_MASK);
	sc_info("sc8565 operation mode is : %d\n", *operation_mode);
	return ret;
}
__maybe_unused static int sc8565_get_ovpgate_state(struct sc8565 *sc, bool *enable)
{
	int ret;
	u8 val;
	ret = sc8565_read_byte(sc, SC8565_REG_0F, &val);
	if (ret) {
		sc_err("sc8565 get ovpgate state fail\n");
		return -1;
	}
	*enable = !!(val & REG0F_OVPGATE_STAT_MASK);
	return ret;
}
__maybe_unused static int sc8565_get_wpcgate_state(struct sc8565 *sc, bool *enable)
{
	int ret;
	u8 val;
	ret = sc8565_read_byte(sc, SC8565_REG_0F, &val);
	if (ret) {
		sc_err("sc8565 get ovpgate state fail\n");
		return -1;
	}
	*enable = !!(val & REG0F_WPCGATE_STAT_MASK);
	return ret;
}
static int sc8565_enable_adc(struct sc8565 *sc, bool enable)
{
	u8 val;

	sc_err("sc8565_enable_adc %d\n", enable);
	if (enable)
		val = REG15_ADC_ENABLE;
	else
		val = REG15_ADC_DISABLE;
	val <<= REG15_ADC_EN_SHIFT;
	return sc8565_update_bits(sc, SC8565_REG_15,
				REG15_ADC_EN_MASK, val);
}
static int sc8565_get_adc_data(struct sc8565 *sc, int channel,  int *result)
{
	int ret;
	u8 val_l, val_h;
	u16 val;
	if(channel >= ADC_MAX_NUM) return 0;
	
	if (channel == ADC_TBUS) {
		*result = 25;
		return 0;
	}
	ret = sc8565_read_byte(sc, SC8565_REG_17 + (channel << 1), &val_h);
	ret = sc8565_read_byte(sc, SC8565_REG_17 + (channel << 1) + 1, &val_l);
	
	if (ret < 0)
		return ret;
	val = (val_h << 8) | val_l;
	if (channel == ADC_IBUS)			 val = val * REG18_IBUS_ADC_LSB;
	else if (channel == ADC_VBUS)		val = val * REG1A_VBUS_ADC_LSB;
	else if (channel == ADC_VUSB)		val = val * REG1C_VUSB_ADC_LSB;
	else if (channel == ADC_VWPC)		val = val * REG1E_VWPC_ADC_LSB;
	else if (channel == ADC_VOUT)		val = val * REG20_VOUT_ADC_LSB;
	else if (channel == ADC_VBAT)		val = val * REG22_VBAT_ADC_LSB;
	else if (channel == ADC_IBAT)		val = val * REG24_IBAT_ADC_LSB;
	else if (channel == ADC_TDIE)		val = val * REG27_TDIE_ADC_LSB;
	if (channel != ADC_TDIE)
		*result = val * 1000;
	else
		*result = val;
	return ret;
}
static int sc8565_set_adc_scan(struct sc8565 *sc, int channel, bool enable)
{
	u8 reg;
	u8 mask;
	u8 shift;
	u8 val;
	if (channel > ADC_MAX_NUM)
		return -EINVAL;
	if (channel == ADC_IBUS) {
		reg = SC8565_REG_15;
		shift = REG15_IBUS_ADC_DIS_SHIFT;
		mask = REG15_IBUS_ADC_DIS_MASK;
	} else {
		reg = SC8565_REG_16;
		shift = 8 - channel;
		mask = 1 << shift;
	}
	if (enable)
		val = 0 << shift;
	else
		val = 1 << shift;
	return sc8565_update_bits(sc, reg, mask, val);
}
__maybe_unused static int sc8565_check_vbus_error_status(struct sc8565 *sc)
{
	int ret;
	u8 data;
	
	ret = sc8565_read_byte(sc, SC8565_REG_0A, &data);
	if(ret == 0){
		sc_err("vbus error >>>>%02x\n", data);
		sc->vbus_error = data;
	}
	return ret;
}
static int sc8565_detect_device(struct sc8565 *sc)
{
	int ret;
	u8 data;
	ret = sc8565_read_byte(sc, SC8565_REG_6E, &data);
	if (ret == 0) {
		if (data != REG6E_DEVICE_ID) {
			return -ENAVAIL;
		}
		ret = sc8565_read_byte(sc, SC8565_REG_00, &data);
		if (ret == 0) {
			sc->revision = data;
		}
	}
	return ret;
}
static int sc8565_parse_dt(struct sc8565 *sc, struct device *dev)
{
	int ret;
	struct device_node *np = dev->of_node;
	sc->cfg = devm_kzalloc(dev, sizeof(struct sc8565_cfg),
					GFP_KERNEL);
	if (!sc->cfg)
		return -ENOMEM;
	sc->cfg->bat_ovp_disable = of_property_read_bool(np,
			"sc,sc8565,bat-ovp-disable");
	sc->cfg->bat_ocp_disable = of_property_read_bool(np,
			"sc,sc8565,bat-ocp-disable");
	sc->cfg->bus_ocp_disable = of_property_read_bool(np,
			"sc,sc8565,bus-ocp-disable");
	sc->cfg->bus_ucp_disable = of_property_read_bool(np,
			"sc,sc8565,bus-ucp-disable");
	sc->cfg->pmid2out_ovp_disable = of_property_read_bool(np,
			"sc,sc8565,pmid2out-ovp-disable");
	sc->cfg->pmid2out_uvp_disable = of_property_read_bool(np,
			"sc,sc8565,pmid2out-uvp-disable");
	sc->irq_gpio = of_get_named_gpio(np, "sc,sc8565,irq-gpio", 0);
	if (!gpio_is_valid(sc->irq_gpio)) {
		sc_err("fail to valid gpio : %d\n", sc->irq_gpio);
		return -EINVAL;
	}
	sc->sc_lpm_gpio = of_get_named_gpio(np, "sc,sc8565,lpm-gpio", 0);
	if (!gpio_is_valid(sc->sc_lpm_gpio)) {
		sc_err("fail to valid gpio : %d\n", sc->sc_lpm_gpio);
		return -EINVAL;
	}
	/* gpio_set_value(sc->sc_lpm_gpio, 0); */
	ret = of_property_read_u32(np, "sc,sc8565,bat-ovp-threshold",
			&sc->cfg->bat_ovp_th);
	if (ret) {
		sc_err("failed to read bat-ovp-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8565,bat-ocp-threshold",
			&sc->cfg->bat_ocp_th);
	if (ret) {
		sc_err("failed to read bat-ocp-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8565,bus-ovp-threshold",
			&sc->cfg->bus_ovp_th);
	if (ret) {
		sc_err("failed to read bus-ovp-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8565,bus-ocp-threshold",
			&sc->cfg->bus_ocp_th);
	if (ret) {
		sc_err("failed to read bus-ocp-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8565,usb-ovp-threshold",
			&sc->cfg->usb_ovp_th);
	if (ret) {
		sc_err("failed to read usb-ovp-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8565,wpc-ovp-threshold",
			&sc->cfg->wpc_ovp_th);
	if (ret) {
		sc_err("failed to read wpc-ovp-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8565,out-ovp-threshold",
			&sc->cfg->out_ovp_th);
	if (ret) {
		sc_err("failed to read out-ovp-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8565,pmid2out-ovp-threshold",
			&sc->cfg->pmid2out_ovp_th);
	if (ret) {
		sc_err("failed to read sc8565,pmid2out-ovp-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8565,pmid2out-uvp-threshold",
			&sc->cfg->pmid2out_uvp_th);
	if (ret) {
		sc_err("failed to read sc8565,pmid2out-uvp-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8565,sense-r-mohm",
			&sc->cfg->sense_r_mohm);
	if (ret) {
		sc_err("failed to read sc8565,sense-r-mohm\n");
		return ret;
	}
	ret = of_property_read_string(np, "chg_name", &sc->cfg->chg_name);
	if (ret) {
		sc_err("failed to read sc8565 chg_name\n");
		return ret;
	}
	return 0;
}
static int sc8565_init_protection(struct sc8565 *sc)
{
	int ret;
	ret = sc8565_enable_batovp(sc, !sc->cfg->bat_ovp_disable);
	sc_info("%s bat ovp %s\n",
		sc->cfg->bat_ovp_disable ? "disable" : "enable",
		!ret ? "successfullly" : "failed");
	ret = sc8565_enable_batocp(sc, !sc->cfg->bat_ocp_disable);
	sc_info("%s bat ocp %s\n",
		sc->cfg->bat_ocp_disable ? "disable" : "enable",
		!ret ? "successfullly" : "failed");
	ret = sc8565_enable_busocp(sc, !sc->cfg->bus_ocp_disable);
	sc_info("%s bus ocp %s\n",
		sc->cfg->bus_ocp_disable ? "disable" : "enable",
		!ret ? "successfullly" : "failed");
	ret = sc8565_enable_ibus_ucp(sc, !sc->cfg->bus_ucp_disable);
	sc_info("%s bus ucp %s\n",
		sc->cfg->bus_ucp_disable ? "disable" : "enable",
		!ret ? "successfullly" : "failed");
	ret = sc8565_enable_pmid2outovp(sc, !sc->cfg->pmid2out_ovp_disable);
	sc_info("%s pmid2out ovp %s\n",
		sc->cfg->pmid2out_ovp_disable ? "disable" : "enable",
		!ret ? "successfullly" : "failed");
	ret = sc8565_enable_pmid2outuvp(sc, !sc->cfg->pmid2out_uvp_disable);
	sc_info("%s pmid2out uvp %s\n",
		sc->cfg->pmid2out_uvp_disable ? "disable" : "enable",
		!ret ? "successfullly" : "failed");
	ret = sc8565_set_batovp_th(sc, sc->cfg->bat_ovp_th);
	sc_info("set bat ovp th %d %s\n", sc->cfg->bat_ovp_th,
		!ret ? "successfully" : "failed");
	ret = sc8565_set_batocp_th(sc, sc->cfg->bat_ocp_th);
	sc_info("set bat ocp threshold %d %s\n", sc->cfg->bat_ocp_th,
		!ret ? "successfully" : "failed");
	ret = sc8565_set_busovp_th(sc, sc->cfg->bus_ovp_th);
	sc_info("set bus ovp threshold %d %s\n", sc->cfg->bus_ovp_th,
		!ret ? "successfully" : "failed");
	ret = sc8565_set_busocp_th(sc, sc->cfg->bus_ocp_th);
	sc_info("set bus ocp threshold %d %s\n", sc->cfg->bus_ocp_th,
		!ret ? "successfully" : "failed");
	ret = sc8565_set_usbovp_th(sc, sc->cfg->usb_ovp_th);
	sc_info("set usb ovp threshold %d %s\n", sc->cfg->usb_ovp_th,
		!ret ? "successfully" : "failed");
	ret = sc8565_set_wpcovp_th(sc, sc->cfg->wpc_ovp_th);
	sc_info("set wpc ovp threshold %d %s\n", sc->cfg->wpc_ovp_th,
		!ret ? "successfully" : "failed");
	ret = sc8565_set_outovp_th(sc, sc->cfg->out_ovp_th);
	sc_info("set out ovp threshold %d %s\n", sc->cfg->out_ovp_th,
		!ret ? "successfully" : "failed");
	ret = sc8565_set_pmid2outuvp_th(sc, sc->cfg->pmid2out_uvp_th);
	sc_info("set pmid2out uvp threshold %d %s\n", sc->cfg->pmid2out_uvp_th,
		!ret ? "successfully" : "failed");
	ret = sc8565_set_pmid2outovp_th(sc, sc->cfg->pmid2out_ovp_th);
	sc_info("set pmid2out ovp threshold %d %s\n", sc->cfg->pmid2out_ovp_th,
		!ret ? "successfully" : "failed");
	return ret;
}
static int sc8565_init_adc(struct sc8565 *sc)
{
	sc8565_set_adc_scan(sc, ADC_IBUS, true);
	sc8565_set_adc_scan(sc, ADC_VBUS, true);
	sc8565_set_adc_scan(sc, ADC_VUSB, true);
	sc8565_set_adc_scan(sc, ADC_VWPC, true);
	sc8565_set_adc_scan(sc, ADC_VOUT, true);
	sc8565_set_adc_scan(sc, ADC_VBAT, true);
	sc8565_set_adc_scan(sc, ADC_IBAT, true);
	sc8565_set_adc_scan(sc, ADC_TDIE, true);
	//sc8565_enable_adc(sc, true);
	return 0;
}
static int sc8565_freq_config(struct sc8565 *sc)
{
	sc8565_set_freq(sc, 800);
	sc8565_freq_dither_en(sc, true);
	return 0;
}
static int sc8565_init_device(struct sc8565 *sc)
{
	sc8565_set_reg_reset(sc);
	sc8565_set_wdt(sc, 0);// disable watch dog
	sc8565_set_operation_mode(sc, REG0E_FORWARD_4_1_CHARGER_MODE); // default mode
	sc8565_set_ibus_ucp_fall_dg(sc, REG07_IBUS_UCP_FALL_DG_SET_5MS);
	sc8565_enable_acdrv_manual(sc, false);//ac drive auto mode
	sc8565_enable_wpcgate(sc, true);
	sc8565_enable_ovpgate(sc, true);
	sc8565_set_ss_timeout(sc, 5120);
	sc8565_set_sense_resistor(sc, sc->cfg->sense_r_mohm);
	sc8565_freq_config(sc);
	sc8565_init_protection(sc);
	sc8565_init_adc(sc);
	return 0;
}
__maybe_unused static int sc8565_set_present(struct sc8565 *sc, bool present)
{
	sc->usb_present = present;
	if (present)
		sc8565_init_device(sc);
	return 0;
}
static ssize_t sc8565_show_registers(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct sc8565 *sc = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[400];
	int len;
	int idx = 0;
	int ret;
	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sc8565");
	for (addr = 0x0; addr <= 0x7F; addr++) {
		if (addr <= 0x40 || addr == 0x6E || addr == 0x7F) {
			ret = sc8565_read_byte(sc, addr, &val);
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
static ssize_t sc8565_store_register(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct sc8565 *sc = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;
	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && (reg <= 0x40 || reg == 0x7F))
		sc8565_write_byte(sc, (unsigned char)reg, (unsigned char)val);
	return count;
}
static DEVICE_ATTR(registers, 0660, sc8565_show_registers, sc8565_store_register);
static void sc8565_create_device_node(struct device *dev)
{
	device_create_file(dev, &dev_attr_registers);
}
static void sc8565_set_notify(struct sc8565 *sc, enum sc8565_notify notify)
{
	mutex_lock(&sc->notify_lock);
	sc->notify |= BIT(notify);
	mutex_unlock(&sc->notify_lock);
}
static void sc8565_check_fault_status(struct sc8565 *sc)
{
	int ret;
	u8 flag = 0;
	u8 stat = 0;
	mutex_lock(&sc->data_lock);
	ret = sc8565_read_byte(sc, SC8565_REG_01, &flag);
	if (!ret) {
		sc_dbg("FAULT_STAT REG01 = 0x%02X\n", flag);
		sc->bat_ovp_fault = !!(flag & REG01_VBAT_OVP_FLAG_MASK);
		if (sc->bat_ovp_fault) {
			sc8565_set_notify(sc, SC8565_NOTIFY_VBATOVP);
			sc_info("BAT OVP\n");
		}
	}
	ret = sc8565_read_byte(sc, SC8565_REG_02, &flag);
	if (!ret) {
		sc_dbg("FAULT_STAT REG02 = 0x%02X\n", flag);
		sc->bat_ocp_fault = !!(flag & REG02_IBAT_OCP_FLAG_MASK);
		if (sc->bat_ocp_fault)
			sc_info("BAT OCP\n");
	}
	ret = sc8565_read_byte(sc, SC8565_REG_03, &flag);
	if (!ret) {
		sc_dbg("FAULT_STAT REG03 = 0x%02X\n", flag);
		sc->usb_ovp_fault = !!(flag & REG03_VUSB_OVP_FLAG_MASK);
		if (sc->usb_ovp_fault)
			sc_info("USB OVP\n");
	}
	ret = sc8565_read_byte(sc, SC8565_REG_04, &flag);
	if (!ret) {
		sc_dbg("FAULT_STAT REG04 = 0x%02X\n", flag);
		sc->wpc_ovp_fault = !!(flag & REG04_VWPC_OVP_FLAG_MASK);
		if (sc->wpc_ovp_fault)
			sc_info("WPC OVP\n");
	}
	ret = sc8565_read_byte(sc, SC8565_REG_06, &flag);
	if (!ret) {
		sc_dbg("FAULT_STAT REG06 = 0x%02X\n", flag);
		sc->bus_ocp_fault = !!(flag & REG06_IBUS_OCP_FLAG_MASK);
		if (sc->bus_ocp_fault) {
			sc8565_set_notify(sc, SC8565_NOTIFY_IBUSOCP);
			sc_info("IBUS OCP\n");
		}
	}
	ret = sc8565_read_byte(sc, SC8565_REG_07, &flag);
	if (!ret) {
		sc_dbg("FAULT_STAT REG07 = 0x%02X\n", flag);
		sc->bus_ucp_fault = !!(flag & REG07_IBUS_UCP_FALL_FLAG_MASK);
		if (sc->bus_ucp_fault) {
			sc8565_set_notify(sc, SC8565_NOTIFY_IBUSUCP);
			sc_info("IBUS UCP\n");
		}
	}
	ret = sc8565_read_byte(sc, SC8565_REG_08, &flag);
	if (!ret) {
		sc_dbg("FAULT_STAT REG08 = 0x%02X\n", flag);
		sc->pmid2out_ovp_fault = !!(flag & REG08_PMID2OUT_OVP_FLAG_MASK);
		if (sc->pmid2out_ovp_fault)
			sc_info("PMID2OUT OVP\n");
	}
	ret = sc8565_read_byte(sc, SC8565_REG_09, &flag);
	if (!ret) {
		sc_dbg("FAULT_STAT REG09 = 0x%02X\n", flag);
		sc->pmid2out_uvp_fault = !!(flag & REG09_PMID2OUT_UVP_FLAG_MASK);
		if (sc->pmid2out_uvp_fault)
			sc_info("PMID2OUT UVP\n");
	}
	ret = sc8565_read_byte(sc, SC8565_REG_13, &flag);
	if (!ret) {
		sc_dbg("FAULT_FLAG REG13 = 0x%02X\n", flag);
		sc->bus_ovp_fault = !!(flag & REG13_VBUS_OVP_FLAG_MASK);
		if (sc->bus_ovp_fault) {
			sc8565_set_notify(sc, SC8565_NOTIFY_VBUSOVP);
			sc_info("VBUS OVP\n");
		}
		sc->vout_ovp_fault = !!(flag & REG13_VOUT_OVP_FLAG_MASK);
		if (sc->vout_ovp_fault) {
			sc8565_set_notify(sc, SC8565_NOTIFY_VBUSOVP);
			sc_info("VOUT OVP\n");
		}
	}
	ret = sc8565_read_byte(sc, SC8565_REG_0A, &stat);
	if (!ret)
		sc_dbg("FAULT_STAT REG0A = 0x%02X\n", stat);
	
	ret = sc8565_read_byte(sc, SC8565_REG_11, &flag);
	if (!ret)
		sc_dbg("FAULT_FLAG REG11 = 0x%02X\n", flag);
#if 0
	ret = sc8565_read_byte(sc, SC8565_REG_0B, &stat);
	if (!ret)
		sc_dbg("FAULT_STAT REG0B = 0x%02X\n", stat);
	ret = sc8565_read_byte(sc, SC8565_REG_0F, &stat);
	if (!ret)
		sc_dbg("FAULT_STAT REG0F = 0x%02X\n", stat);
	ret = sc8565_read_byte(sc, SC8565_REG_10, &flag);
	if (!ret)
		sc_dbg("FAULT_FLAG REG10 = 0x%02X\n", flag);
	ret = sc8565_read_byte(sc, SC8565_REG_12, &flag);
	if (!ret)
		sc_dbg("FAULT_FLAG REG12 = 0x%02X\n", flag);
	
	ret = sc8565_read_byte(sc, SC8565_REG_14, &flag);
	if (!ret)
		sc_dbg("FAULT_FLAG REG14 = 0x%02X\n", flag);
#endif
	mutex_unlock(&sc->data_lock);
}
static void sc8565_notify(struct sc8565*sc)
{
	int i;
	mutex_lock(&sc->notify_lock);
	for (i = 0; i < SC8565_NOTIFY_MAX; i++) {
		if (sc->notify & BIT(i)) {
			sc->notify &= ~BIT(i);
			mutex_unlock(&sc->notify_lock);
			charger_dev_notify(sc->chg_dev, sc8565_chgdev_notify_map[i]);
			mutex_lock(&sc->notify_lock);
		}
	}
	mutex_unlock(&sc->notify_lock);
}
static void sc8565_state_update_work(struct work_struct *data)
{
	struct sc8565 *sc = container_of(data,
		struct sc8565, state_update_work);
	sc_info("INT OCCURED\n");
	pm_stay_awake(sc->dev);
	mutex_lock(&sc->irq_complete);
	sc8565_check_fault_status(sc);
	sc8565_notify(sc);
	mutex_unlock(&sc->irq_complete);
	pm_relax(sc->dev);
}
/*
* interrupt does nothing, just info event chagne, other module could get info
* through power supply interface
*/
static irqreturn_t sc8565_charger_interrupt(int irq, void *dev_id)
{
	struct sc8565 *sc = dev_id;
	schedule_work(&sc->state_update_work);
	return IRQ_HANDLED;
}
static int sc8565_irq_register(struct sc8565 *sc)
{
	int ret;
	struct device_node *node = sc->dev->of_node;
	if (!node) {
		sc_err("device tree node missing\n");
		return -EINVAL;
	}
	if (gpio_is_valid(sc->irq_gpio)) {
		ret = gpio_request_one(sc->irq_gpio, GPIOF_DIR_IN,"sc8565_irq");
		if (ret) {
			sc_err("failed to request sc8565_irq\n");
			return -EINVAL;
		}
		sc->irq = gpio_to_irq(sc->irq_gpio);
		if (sc->irq < 0) {
			sc_err("failed to gpio_to_irq\n");
			return -EINVAL;
		}
	} else {
		sc_err("irq gpio not provided\n");
		return -EINVAL;
	}
	if (sc->irq) {
		if (sc->mode == SC8565_ROLE_STANDALONE) {
			ret = devm_request_threaded_irq(&sc->client->dev, sc->irq,
					NULL, sc8565_charger_interrupt,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"sc8565 standalone irq", sc);
		} else if (sc->mode == SC8565_ROLE_MASTER) {
			ret = devm_request_threaded_irq(&sc->client->dev, sc->irq,
					NULL, sc8565_charger_interrupt,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"sc8565 master irq", sc);
		} else {
			ret = devm_request_threaded_irq(&sc->client->dev, sc->irq,
					NULL, sc8565_charger_interrupt,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"sc8565 slave irq", sc);
		}
		if (ret < 0) {
			sc_err("request irq for irq=%d failed, ret =%d\n",
							sc->irq, ret);
			return ret;
		}
		//enable_irq(sc->irq);
	}
	return ret;
}
static void determine_initial_status(struct sc8565 *sc)
{
	if (sc->client->irq)
		sc8565_charger_interrupt(sc->client->irq, sc);
}
static struct of_device_id sc8565_charger_match_table[] = {
	{
		.compatible = "sc,sc8565-standalone",
		.data = &sc8565_mode_data[SC8565_ROLE_STANDALONE],
	},
	{
		.compatible = "sc,sc8565-master",
		.data = &sc8565_mode_data[SC8565_ROLE_MASTER],
	},
	{
		.compatible = "sc,sc8565-slave",
		.data = &sc8565_mode_data[SC8565_ROLE_SLAVE],
	},
	{},
};

static int sc8565_enable_chg(struct charger_device *chg_dev, bool en)
{
	int ret = 0;
	struct sc8565 *sc = charger_get_data(chg_dev);
	if (en)
		sc8565_set_wdt(sc, 30000);
	else
		sc8565_set_wdt(sc, 0);
	sc8565_enable_charge(sc, en);
	return ret;
}

static int sc8565_is_chg_enabled(struct charger_device *chg_dev, bool *en)
{
	int ret = 0;
	struct sc8565 *sc = charger_get_data(chg_dev);
	ret = sc8565_check_charge_enabled(sc, en);
	sc_info("%s : check cp enabled:%d, ret:%d\n", __func__, *en, ret);
	return ret;
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
static int sc8565_get_adc(struct charger_device *chg_dev,
	enum adc_channel chan, int *min, int *max)
{
	int ret = 0;
	ADC_CH sc_channel;
	struct sc8565 *sc = charger_get_data(chg_dev);
	sc_channel = plat_channel_to_sc_channel(chan);
	if (sc_channel == ADC_MAX_NUM)
		return -EINVAL;
	ret = sc8565_get_adc_data(sc, sc_channel, min);
	if (ret < 0) {
		sc_err("%s : get adc fail:%d\n", __func__, sc_channel);
		return ret;
	}
	sc_info("adc chan:%d, data:%d\n", chan, *min);
	*max = *min;
	return ret;
}
static int sc8565_get_adc_accuracy(struct charger_device *chg_dev,
	enum adc_channel chan, int *min, int *max)
{
	ADC_CH sc_channel;
	sc_channel = plat_channel_to_sc_channel(chan);
	if (sc_channel == ADC_MAX_NUM)
		return -EINVAL;
	*min = *max = sc8565_adc_accuracy_tbl[sc_channel];
	return 0;
}
static int sc8565_set_vbusovp(struct charger_device *chg_dev, u32 uV)
{
	int ret = 0;
	int vbus_th_mv;
	struct sc8565 *sc = charger_get_data(chg_dev);
	vbus_th_mv = uV / 1000;
	ret = sc8565_set_busovp_th(sc, vbus_th_mv);
	sc_info("%s : set vbus ovp uV:%d, vbus_th_mv:%d, ret:%d\n",
		__func__, uV, vbus_th_mv, ret);
	return ret;
}
static int sc8565_set_ibusocp(struct charger_device *chg_dev, u32 uA)
{
	int ret = 0;
	int ibus_th_ma;
	struct sc8565 *sc = charger_get_data(chg_dev);
	ibus_th_ma = uA / 1000;
	ret = sc8565_set_busocp_th(sc, ibus_th_ma);
	sc_info("%s : set ibus ocp uA:%d, ibus_th_ma:%d, ret:%d\n",
		__func__, uA, ibus_th_ma, ret);
	return ret;
}
static int sc8565_set_vbatovp(struct charger_device *chg_dev, u32 uV)
{
	int ret = 0;
	int vbat_th_mv;
	struct sc8565 *sc = charger_get_data(chg_dev);
	vbat_th_mv = uV / 1000;
	ret = sc8565_set_batovp_th(sc, vbat_th_mv);
	sc_info("%s : set vbat ovp uV:%d, vbat_th_mv:%d, ret:%d\n",
		__func__, uV, vbat_th_mv, ret);
	return ret;
}
static int sc8565_set_ibatocp(struct charger_device *chg_dev, u32 uA)
{
	int ret = 0;
	int ibat_th_ma;
	struct sc8565 *sc = charger_get_data(chg_dev);
	ibat_th_ma = uA / 1000;
	ret = sc8565_set_batocp_th(sc, ibat_th_ma);
	sc_info("%s : set ibat ocp uA:%d, ibat_th_ma:%d, ret:%d\n",
		__func__, uA, ibat_th_ma, ret);
	return ret;
}

static int sc8565_set_ibusucp_en(struct charger_device *chg_dev,bool en)
{
	struct sc8565 *sc = charger_get_data(chg_dev);
	int ret = 0;

	ret = sc8565_enable_ibus_ucp(sc,en);
	sc_info("set ibus ucp ret:%d en:%d\n",ret,en);
	return ret;
}

static int sc8565_init_chip(struct charger_device *chg_dev)
{
	struct sc8565 *sc = charger_get_data(chg_dev);
	sc8565_enable_adc(sc, true);
	return 0;
}
static int sc8565_set_vbatovp_alarm(struct charger_device *chg_dev, u32 uV)
{
	return 0;
}
static int sc8565_set_vbusovp_alarm(struct charger_device *chg_dev, u32 uV)
{
	return 0;
}

/*
static int sc8565_is_vbushigerr(struct charger_device *chg_dev, bool *err)
{
	int ret = 0;
	u8 stat = 0;
	struct sc8565 *sc = charger_get_data(chg_dev);
	ret = sc8565_read_byte(sc, SC8565_REG_0A, &stat);
	if (ret < 0) {
		sc_err("%s : get vbus error stat SC8548_REG_06 fail\n", __func__);
		return ret;
	}
	sc_info("%s : get vbus error stat succ, stat:%d\n", __func__, stat);
	*err = (stat & REG0A_VBUS_ERRORHI_STAT_MASK) >> REG0A_VBUS_ERRORHI_STAT_SHIFT;
	return ret;
}
*/

static int sc8565_is_vbuslowerr(struct charger_device *chg_dev, bool *err)
{
	int ret = 0;
	u8 stat = 0;
	struct sc8565 *sc = charger_get_data(chg_dev);
	ret = sc8565_read_byte(sc, SC8565_REG_0A, &stat);
	if (ret < 0) {
		sc_err("%s : get vbus error stat SC8548_REG_06 fail\n", __func__);
		return ret;
	}
	sc_info("%s : get vbus error stat succ, stat:%d\n", __func__, stat);
	*err = (stat & REG0A_VBUS_ERRORLO_STAT_MASK) >> REG0A_VBUS_ERRORLO_STAT_SHIFT;
	return ret;
}
static int sc8565_set_dp_dm(struct charger_device *chg_dev,
	enum dpdm_ctrl_status dp_status, enum dpdm_ctrl_status dm_status, bool en_rfc_detect)
{
	return 0;
}
static int sc8565_get_dp_dm(struct charger_device *chg_dev, bool dp)
{
	return 0;
}

static int sc8565_i2c_trans(struct charger_device *chg_dev, bool high)
{
	return 0;
}

static int sc8565_rfc_detect(struct charger_device *chg_dev, bool *is_rfc_ta)
{
	return 0;
}
static int sc8565_adc_init(struct charger_device *chg_dev, bool en)
{
	int ret = 0;
	struct sc8565 *sc = charger_get_data(chg_dev);
	ret = sc8565_enable_adc(sc, en);
	msleep(20);

	return ret;
}
static int sc8565_rfc_reset(struct charger_device *chg_dev)
{
	return 0;
}
static int sc8565_set_run_spec(struct charger_device *chg_dev,
	u32 run_sepc)
{
	struct sc8565 *sc = charger_get_data(chg_dev);
	sc_info("ctrl charger pump run spec:%d\n", run_sepc);
	switch (run_sepc) {
	case SUPPORT_SPEC_4_1:
		sc8565_set_operation_mode(sc, REG0E_FORWARD_4_1_CHARGER_MODE);
		break;
	case SUPPORT_SPEC_2_1:
		sc8565_set_operation_mode(sc, REG0E_FORWARD_2_1_CHARGER_MODE);
		break;
	default:
		sc_err("unknown run spec:%d\n", run_sepc);
		break;
	}
	return 0;	
}

static int sc8565_get_run_spec(struct charger_device *chg_dev,
	u32 run_sepc)
{
	int chg_mode = 0;
	struct sc8565 *sc = charger_get_data(chg_dev);
	sc_info("ctrl charger pump get run spec:%d\n", run_sepc);
	sc8565_get_operation_mode(sc,&chg_mode);
	switch (chg_mode) {
	case REG0E_FORWARD_4_1_CHARGER_MODE:
		run_sepc = SUPPORT_SPEC_4_1;
		break;
	case REG0E_FORWARD_2_1_CHARGER_MODE:
		run_sepc = SUPPORT_SPEC_2_1;
		break;
	default:
		sc_err("unknown run spec:%d\n", chg_mode);
		break;
	}
	return 0;	
}

static int sc8565_enable_otg(struct charger_device *chg_dev, bool en)
{
	int ret = 0;
	struct sc8565 *sc = charger_get_data(chg_dev);
	sc_info("sc8565 %s otg mode\n", en ? "enable" : "disable");
	if (en) {
		sc8565_set_operation_mode(sc, REG0E_REVERSE_1_1_CONVERTER_MODE);
		ret = sc8565_enable_acdrv_manual(sc, true);
		sc8565_enable_ovpgate(sc, true);
	} else {
		sc8565_enable_ovpgate(sc, false);
		ret = sc8565_enable_acdrv_manual(sc, false);
		sc8565_set_operation_mode(sc, REG0E_FORWARD_4_1_CHARGER_MODE);
	}
	return ret;
}

static int sc8565_chg_plug_in(struct charger_device *chg_dev)
{
	return 0;
}

static int sc8565_chg_plug_out(struct charger_device *chg_dev)
{
	int ret = 0;
	struct sc8565 *sc = charger_get_data(chg_dev);
	ret = sc8565_enable_adc(sc, false);
	return ret;
}

static int sc8565_enable_special_function(struct charger_device *chg_dev,int mode)
{	
	int ret = 0;
	int chg_mode = 0;
	struct sc8565 *sc = charger_get_data(chg_dev);
	sc_info("sc8565 run mode = %d\n",mode);
	switch(mode){
		case HVCHG_BOOST_MODE:
			__sc8565_write_byte(sc, SC8565_REG_7C, 0x01);
			sc8565_set_operation_mode(sc, REG0E_REVERSE_1_2_CONVERTER_MODE);
			break;
		case HVCHG_BOOST_1_4_MODE:
			__sc8565_write_byte(sc, SC8565_REG_7C, 0x01);
			sc8565_set_operation_mode(sc, REG0E_REVERSE_1_4_CONVERTER_MODE);
			break;
		case HVCHG_EN_REV_MODE:
			__sc8565_write_byte(sc, SC8565_REG_04, 0x0e); 
			sc8565_enable_ibus_ucp(sc,true);
			sc8565_set_ss_timeout(sc, 0);
			sc8565_enable_acdrv_manual(sc, true);
			sc8565_enable_wpcgate(sc, true);
			sc8565_enable_qb(sc, true);
			sc8565_enable_pmid2outuvp(sc,false);
			mdelay(1);
			sc8565_enable_charge(sc, true);
			break;
		case HVCHG_NORMAL_MODE:
			sc8565_enable_charge(sc, false);
			sc8565_enable_qb(sc, false);
			sc8565_enable_acdrv_manual(sc, false);
			sc8565_enable_pmid2outuvp(sc,true);
			//__sc8565_write_byte(sc, SC8565_REG_04, 0x0f); //6.5v
			//sc8565_enable_wpcgate(sc, false);
			//sc8565_enable_ibus_ucp(sc,false);
			sc8565_get_operation_mode(sc,&chg_mode);
			if(chg_mode != REG0E_FORWARD_4_1_CHARGER_MODE)
				sc8565_set_operation_mode(sc, REG0E_FORWARD_4_1_CHARGER_MODE);
			sc8565_set_ss_timeout(sc, 5120);
			break;
		case HVCHG_CP_WIRELESS_MODE_INIT:
			sc8565_get_operation_mode(sc,&chg_mode);
			if(chg_mode == REG0E_FORWARD_4_1_CHARGER_MODE){
				sc8565_set_ibus_ucp_fall_dg(sc, REG07_IBUS_UCP_FALL_DG_SET_50MS);
				sc8565_set_pmid2outuvp_th(sc,450);
			}
			break;
		case HVCHG_SOFT_RESET:
			sc8565_set_ibus_ucp_fall_dg(sc, REG07_IBUS_UCP_FALL_DG_SET_5MS);
			sc8565_set_pmid2outuvp_th(sc,200);
			break;
		case HVCHG_WIRELESS_LPM_MODE:
			sc8565_enable_ovpgate(sc, false);
			sc8565_enable_acdrv_manual(sc,true);
			mdelay(20);
			sc8565_enable_acdrv_manual(sc,false);
			break;
		default:
			break;
	}
	return ret;
}

static const struct charger_ops sc8565_chg_ops = {
	.enable = sc8565_enable_chg,
	.is_enabled = sc8565_is_chg_enabled,
	.get_adc = sc8565_get_adc,
	.set_vbusovp = sc8565_set_vbusovp,
	.set_ibusocp = sc8565_set_ibusocp,
	.set_vbatovp = sc8565_set_vbatovp,
	.set_ibatocp = sc8565_set_ibatocp,
	.set_ibusucp_enable = sc8565_set_ibusucp_en,
	.init_chip = sc8565_init_chip,
	.set_vbatovp_alarm = sc8565_set_vbatovp_alarm,
	.set_vbusovp_alarm = sc8565_set_vbusovp_alarm,
	//.is_vbushigerr = sc8565_is_vbushigerr,
	.is_vbuslowerr = sc8565_is_vbuslowerr,
	//.is_direct_charging_vbushigerr = sc8565_is_vbushigerr,
	.get_adc_accuracy = sc8565_get_adc_accuracy,
	.set_dp_dm = sc8565_set_dp_dm,
	.get_dp_dm = sc8565_get_dp_dm,
	.i2c_trans = sc8565_i2c_trans,
	.cp_rfc_detect = sc8565_rfc_detect,
	/* .dump_registers = sc8565_dump_regs, */
	.init_adc = sc8565_adc_init,
	.soft_reset = sc8565_rfc_reset,
	.set_run_spec = sc8565_set_run_spec,
	.get_run_spec = sc8565_get_run_spec,
	.enable_otg = sc8565_enable_otg,
	.plug_in = sc8565_chg_plug_in,
	.plug_out = sc8565_chg_plug_out,
	.enable_special_function = sc8565_enable_special_function,
};
static int sc8565_register_chgdev(struct sc8565 *sc)
{
	sc->chg_prop.alias_name = sc->cfg->chg_name;
	sc->chg_dev = charger_device_register(sc->cfg->chg_name, sc->dev,
					      sc, &sc8565_chg_ops,
					      &sc->chg_prop);
	if (!sc->chg_dev)
		return -EINVAL;
	return 0;
}
static void sc8565_i2c_driver_ability_init(struct sc8565 *sc)
{
	sc->pinctrl = devm_pinctrl_get(sc->dev);
	if (IS_ERR(sc->pinctrl)) {
		sc_err("Cannot find pinctrl\n");
		return;
	}
	sc->pinctrl_i2c_clk_gpio = pinctrl_lookup_state(sc->pinctrl, "cp_i2c_clk_mode");
	if (IS_ERR(sc->pinctrl_i2c_clk_gpio)) {
		sc_err("Cannot find i2c_clk_mode\n");
		return;
	}
	sc->pinctrl_i2c_sda_gpio = pinctrl_lookup_state(sc->pinctrl, "cp_i2c_sda_mode");
	if (IS_ERR(sc->pinctrl_i2c_sda_gpio)) {
		sc_err("Cannot find i2c_sda_mode\n");
		return;
	}
}
static int sc8565_charger_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct sc8565 *sc;
	const struct of_device_id *match;
	struct device_node *node = client->dev.of_node;
	int ret;
	sc = devm_kzalloc(&client->dev, sizeof(struct sc8565), GFP_KERNEL);
	if (!sc)
		return -ENOMEM;
	sc->dev = &client->dev;
	sc->client = client;
	mutex_init(&sc->i2c_rw_lock);
	mutex_init(&sc->data_lock);
	mutex_init(&sc->irq_complete);
	mutex_init(&sc->notify_lock);
	mutex_init(&sc->suspend_lock);
	sc->resume_completed = true;
	sc->irq_waiting = false;
	sc8565_i2c_driver_ability_init(sc);
	ret = sc8565_detect_device(sc);
	if (ret) {
		sc_err("No sc8565 device found!\n");
		return -ENODEV;
	}
	
	i2c_set_clientdata(client, sc);
	sc8565_create_device_node(&(client->dev));
	match = of_match_node(sc8565_charger_match_table, node);
	if (match == NULL) {
		sc_err("device tree match not found!\n");
		return -ENODEV;
	}
	sc->mode =  *(int *)match->data;
	ret = sc8565_parse_dt(sc, &client->dev);
	if (ret)
		return -EIO;
	ret = sc8565_init_device(sc);
	if (ret) {
		sc_err("Failed to init device\n");
		return ret;
	}
	
	ret = sc8565_register_chgdev(sc);
	if (ret) {
		sc_err("Failed to register chgdev\n");
		return ret;
	}
	INIT_WORK(&sc->state_update_work, sc8565_state_update_work);
	ret = sc8565_irq_register(sc);
	if (ret)
		goto err_1;
	device_init_wakeup(sc->dev, 1);
	determine_initial_status(sc);
	sc_info("sc8565 probe successfully\n!");
	return 0;
err_1:
	return ret;
}
/* static inline bool is_device_suspended(struct sc8565 *sc) */
/* { */
/*         return !sc->resume_completed; */
/* } */
static int sc8565_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sc8565 *sc = i2c_get_clientdata(client);
	mutex_lock(&sc->suspend_lock);
	if (device_may_wakeup(dev))
		enable_irq_wake(sc->irq);
	sc_err("Suspend successfully!\n");
	return 0;
}
static int sc8565_suspend_noirq(struct device *dev)
{
	/* struct i2c_client *client = to_i2c_client(dev); */
	/* struct sc8565 *sc = i2c_get_clientdata(client); */
	/* if (sc->irq_waiting) { */
	/*         pr_err_ratelimited("Aborting suspend, an interrupt was detected while suspending\n"); */
	/*         return -EBUSY; */
	/* } */
	return 0;
}
static int sc8565_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sc8565 *sc = i2c_get_clientdata(client);
	mutex_unlock(&sc->suspend_lock);
	if (device_may_wakeup(dev))
		disable_irq_wake(sc->irq);
	sc_err("Resume successfully!\n");
	return 0;
}
static void sc8565_charger_remove(struct i2c_client *client)
{
	struct sc8565 *sc = i2c_get_clientdata(client);
	sc8565_enable_adc(sc, false);
	mutex_destroy(&sc->data_lock);
	mutex_destroy(&sc->i2c_rw_lock);
	mutex_destroy(&sc->irq_complete);
	mutex_destroy(&sc->notify_lock);
	mutex_destroy(&sc->suspend_lock);
}
static void sc8565_charger_shutdown(struct i2c_client *client)
{
	struct sc8565 *sc = i2c_get_clientdata(client);
	int ret;
	ret = i2c_smbus_write_byte_data(client, SC8565_REG_15, 0x00); //default adc reg value
	if (ret < 0)
		sc_err("write adc false fail\n");
	disable_irq(sc->irq);
	sc_err("shutdown successfully!\n");
}
static const struct dev_pm_ops sc8565_pm_ops = {
	.resume	 = sc8565_resume,
	.suspend_noirq = sc8565_suspend_noirq,
	.suspend	= sc8565_suspend,
};
static const struct i2c_device_id sc8565_charger_id[] = {
	{"sc8565-standalone", SC8565_ROLE_STANDALONE},
	{},
};
	
static struct i2c_driver sc8565_charger_driver = {
	.driver	 = {
		.name   = "sc8565-charger",
		.owner  = THIS_MODULE,
		.of_match_table = sc8565_charger_match_table,
		.pm = &sc8565_pm_ops,
	},
	.id_table   = sc8565_charger_id,
	.probe	  = sc8565_charger_probe,
	.remove	 = sc8565_charger_remove,
	.shutdown   = sc8565_charger_shutdown,
};
module_i2c_driver(sc8565_charger_driver);
MODULE_DESCRIPTION("SC SC8565 Charge Pump Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Aiden-yu@southchip.com");
