// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Transsion Inc.
 */
/*
* Copyright (c) 2022 Southchip Semiconductor Technology(Shanghai) Co., Ltd.
*/

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
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/regmap.h>
#include "tc_charger_class.h"
#include "tc_common_class.h"
#include "tc_gauge.h"

#define SC760X_DRV_VERSION			  "1.0.0_G"

enum {
	SC760X_MASTER = 0,
	SC760X_SLAVE,
};

static const char* sc760x_irq_name[] = {
	[SC760X_MASTER] = "sc760x_master_irq",
	[SC760X_SLAVE] = "sc760x_slave_irq",
};

static int sc760x_role_data[] = {
	[SC760X_MASTER] = SC760X_MASTER,
	[SC760X_SLAVE] = SC760X_SLAVE,
};

#define IBAT_CHG_LIM_BASE			   50
#define IBAT_CHG_LIM_LSB				50

#define ITRICHG_BASE					12500//uA
#define ITRICHG_LSB					 12500//uA

#define IPRECHG_BASE					50
#define IPRECHG_LSB					 50

#define VFC_CHG_BASE					2800
#define VFC_CHG_LSB					 50

#define BAT_OVP_BASE					4000
#define BAT_OVP_LSB					 50

#define SC7603_DEVICE_ID				0x09


enum {
	ADC_IBAT,
	ADC_VBAT,
	ADC_VCHG,
	ADC_TBAT,
	ADC_TDIE,
	ADC_MAX_NUM,
}ADC_CH;

static const int sc760x_adc_m[] = 
	{3125, 125, 125, 3125, 5};

static const int sc760x_adc_l[] = 
	{1000, 100, 100, 10000, 10};


enum sc760x_fields {
	DEVICE_REV, DEVICE_ID,
	IBAT_CHG_LIM,
	POW_LIM_DIS, VBALANCE_DIS, POW_LIM,
	ILIM_DIS, IBAT_CHG_LIM_DIS, BAT_DET_DIS, VDIFF_CHECK_DIS, LS_OFF, SHIP_EN,
	REG_RST, EN_LOWPOWER, VDIFF_OPEN_TH, AUTO_BSM_DIS, AUTO_BSM_TH, SHIP_WT,
	ITRICHG, VPRE_CHG,
	IPRECHG, VFC_CHG,
	CHG_OVP_DIS, CHG_OVP,
	BAT_OVP_DIS, BAT_OVP,
	CHG_OCP_DIS, CHG_OCP,
	DSG_OCP_DIS, DSG_OCP,
	TDIE_FLT_DIS, TDIE_FLT,
	TDIE_ALRM_DIS, TDIE_ALRM,
	CHG_OVP_DEG, BAT_OVP_DEG, CHG_OCP_DEG, DSG_OCP_DEG,
	AUTO_BSM_DEG,
	WORK_MODE, BAT_ABSENT_STAT, VDUFF_STAT,
	ADC_EN, ADC_RATE, ADC_FREEZE,
	F_MAX_FIELDS,
};

struct sc760x_cfg_e {
	int bat_chg_lim_disable;
	int bat_chg_lim;
	int pow_lim_disable;
	int pow_lim;
	int ilim_disable;
	int load_switch_disable;
	int lp_mode_enable;
	int auto_bsm_th;
	int itrichg;
	int iprechg;
	int vfc_chg;
	int chg_ovp_disable;
	int chg_ovp;
	int bat_ovp_disable;
	int bat_ovp;
	int chg_ocp_disable;
	int chg_ocp;
	int dsg_ocp_disable;
	int dsg_ocp;
	int tdie_flt_disable;
	int tdie_alm_disable;
	int tdie_alm;
	const char *chg_name;
};

static struct sc760x_cfg_e default_cfg = {
	.bat_chg_lim_disable = 0,
	.bat_chg_lim = 39,
	.pow_lim_disable = 0,
	.pow_lim = 15,
	.ilim_disable = 0,
	.load_switch_disable = 0,
	.lp_mode_enable = 0,
	.auto_bsm_th = 1,
	.itrichg = 3,
	.iprechg = 2, 
	.vfc_chg = 2,
	.chg_ovp_disable = 0,
	.chg_ovp = 0,
	.bat_ovp_disable = 0,
	.bat_ovp = 10,
	.chg_ocp_disable = 0,
	.chg_ocp = 2,
	.dsg_ocp_disable = 0,
	.dsg_ocp = 2,
	.tdie_flt_disable = 0,
	.tdie_alm_disable = 0,
	.tdie_alm = 9,
};

struct sc760x_chip {
	struct device *dev;
	struct i2c_client *client;
	struct charger_device *chg_dev;
	struct charger_properties chg_prop;
	struct regmap *regmap;
	struct regmap_field *rmap_fields[F_MAX_FIELDS];
	struct tran_device *tc_gauge;

	struct sc760x_cfg_e *cfg;
	int enable_gpio;
	int irq_gpio;
	int irq;

	int role;
};

//REGISTER
static const struct reg_field sc760x_reg_fields[] = {
	/*reg00*/
	[DEVICE_REV] = REG_FIELD(0x00, 4, 7),
	[DEVICE_ID] = REG_FIELD(0x00, 0, 3),
	/*reg01*/
	[IBAT_CHG_LIM] = REG_FIELD(0x01, 0, 7),
	/*reg02*/
	[POW_LIM_DIS] = REG_FIELD(0x02, 7, 7),
	[VBALANCE_DIS] = REG_FIELD(0x02, 5, 5),
	[POW_LIM] = REG_FIELD(0x02, 0, 3),
	/*reg03*/
	[ILIM_DIS] = REG_FIELD(0x03, 7, 7),
	[IBAT_CHG_LIM_DIS] = REG_FIELD(0x03, 6, 6),
	[BAT_DET_DIS] = REG_FIELD(0x03, 5, 5),
	[VDIFF_CHECK_DIS] = REG_FIELD(0x03, 4, 4),
	[LS_OFF] = REG_FIELD(0x03, 3, 3),
	[SHIP_EN] = REG_FIELD(0x03, 0, 2),
	/*reg04*/
	[REG_RST] = REG_FIELD(0x04, 7, 7),
	[EN_LOWPOWER] = REG_FIELD(0x04, 6, 6),
	[VDIFF_OPEN_TH] = REG_FIELD(0x04, 4, 5),
	[AUTO_BSM_DIS] = REG_FIELD(0x04, 3, 3),
	[AUTO_BSM_TH] = REG_FIELD(0x04, 2, 2),
	[SHIP_WT] = REG_FIELD(0x04, 0, 0),
	/*reg05*/
	[ITRICHG] = REG_FIELD(0x05, 5, 7),
	[VPRE_CHG] = REG_FIELD(0x05, 0, 2),
	/*reg06*/
	[IPRECHG] = REG_FIELD(0x06, 4, 7),
	[VFC_CHG] = REG_FIELD(0x06, 0, 3),
	/*reg07*/
	[CHG_OVP_DIS] = REG_FIELD(0x07, 7, 7),
	[CHG_OVP] = REG_FIELD(0x07, 6, 6),
	/*reg08*/
	[BAT_OVP_DIS] = REG_FIELD(0x08, 7, 7),
	[BAT_OVP] = REG_FIELD(0x08, 2, 6),
	/*reg09*/
	[CHG_OCP_DIS] = REG_FIELD(0x09, 7, 7),
	[CHG_OCP] = REG_FIELD(0x09, 4, 6),
	/*reg0A*/
	[DSG_OCP_DIS] = REG_FIELD(0x0A, 7, 7),
	[DSG_OCP] = REG_FIELD(0x0A, 4, 6),
	/*reg0B*/
	[TDIE_FLT_DIS] = REG_FIELD(0x0B, 7, 7),
	[TDIE_FLT] = REG_FIELD(0x0B, 0, 3),
	/*reg0C*/
	[TDIE_ALRM_DIS] = REG_FIELD(0x0C, 7, 7),
	[TDIE_ALRM] = REG_FIELD(0x0C, 0, 3),
	/*reg0D*/
	[CHG_OVP_DEG] = REG_FIELD(0x0D, 6, 7),
	[BAT_OVP_DEG] = REG_FIELD(0x0D, 4, 5),
	[CHG_OCP_DEG] = REG_FIELD(0x0D, 2, 3),
	[DSG_OCP_DEG] = REG_FIELD(0x0D, 0, 1),
	/*reg0E*/
	[AUTO_BSM_DEG] = REG_FIELD(0x0E, 6, 7),
	/*reg0F*/
	[WORK_MODE] = REG_FIELD(0x0F, 5, 7),
	/*reg15*/
	[ADC_EN] = REG_FIELD(0x15, 7, 7),
	[ADC_RATE] = REG_FIELD(0x15, 6, 6),
	[ADC_FREEZE] = REG_FIELD(0x15, 5, 5),
};

static const struct regmap_config sc760x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

/*********************************************************/
static int sc760x_field_read(struct sc760x_chip *sc,
				enum sc760x_fields field_id, int *val)
{
	int ret;

	ret = regmap_field_read(sc->rmap_fields[field_id], val);
	if (ret < 0) {
		dev_err(sc->dev, "sc760x read field %d fail: %d\n", field_id, ret);
	}
	
	return ret;
}

static int sc760x_field_write(struct sc760x_chip *sc,
				enum sc760x_fields field_id, int val)
{
	int ret;
	
	ret = regmap_field_write(sc->rmap_fields[field_id], val);
	if (ret < 0) {
		dev_err(sc->dev, "sc760x read field %d fail: %d\n", field_id, ret);
	}
	
	return ret;
}

static int sc760x_read_block(struct sc760x_chip *sc,
				int reg, uint8_t *val, int len)
{
	int ret;

	ret = regmap_bulk_read(sc->regmap, reg, val, len);
	if (ret < 0) {
		dev_err(sc->dev, "sc760x read %02x block failed %d\n", reg, ret);
	}

	return ret;
}

/*******************************************************/
static int sc760x_check_tran_dev_ptr(struct tran_device **dev, const char *name)
{
	if (IS_ERR_OR_NULL(*dev)) {
		*dev = tran_get_by_name(name);
		if (IS_ERR_OR_NULL(*dev)) {
			pr_err("%s Couldn't get dev(%s)\n", __func__, name);
			return -EINVAL;
		}
	}

	return 0;
}

static bool sc760x_check_dual_batt_working(struct sc760x_chip *sc)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = sc760x_check_tran_dev_ptr(&sc->tc_gauge, "tc_gauge");
	if (ret < 0) {
		pr_info("%s Couldn't get tc_gauge\n", __func__);
		return false;
	}

	tran_dev_get_prop(sc->tc_gauge, TRAN_PROP_BATT_WORKING_STATUS, &prop);

	 if (prop.intval == GAUGE_DUAL_BATT_ONLINE)
		return true;

	dev_info(sc->dev, "not dual batt working!");

	return false;
}

__maybe_unused static int sc760x_reg_reset(struct sc760x_chip *sc)
{
	return sc760x_field_write(sc, REG_RST, 1);
}

__maybe_unused static int sc760x_set_ibat_limit(struct sc760x_chip *sc, int curr)
{
	dev_info(sc->dev, "%s : %dmA\n", __func__, curr);

	return sc760x_field_write(sc, IBAT_CHG_LIM,
			(curr - IBAT_CHG_LIM_BASE) / IBAT_CHG_LIM_LSB);
}

__maybe_unused static int sc760x_set_power_limit_dis(struct sc760x_chip *sc, bool en)
{
	return sc760x_field_write(sc, POW_LIM_DIS, !!en);
}

__maybe_unused static int sc760x_set_ilimit_dis(struct sc760x_chip *sc, bool en)
{
	return sc760x_field_write(sc, ILIM_DIS, !!en);
}

__maybe_unused static int sc760x_set_load_switch(struct sc760x_chip *sc, bool en)
{
	return sc760x_field_write(sc, LS_OFF, !!en);
}

__maybe_unused static int sc760x_set_lowpower_mode(struct sc760x_chip *sc, bool en)
{
	bool dual_batt_working = false;

	dual_batt_working = sc760x_check_dual_batt_working(sc);
	if (!dual_batt_working) {
		return 0;
	}

	return sc760x_field_write(sc, EN_LOWPOWER, !!en);
}

__maybe_unused static int sc760x_set_itrickle(struct sc760x_chip *sc, int curr)
{
	dev_info(sc->dev, "%s : %dmA\n", __func__, curr);

	return sc760x_field_write(sc, ITRICHG,
			(curr * 1000 - ITRICHG_BASE) / ITRICHG_LSB);
}

__maybe_unused static int sc760x_set_iprechg(struct sc760x_chip *sc, int curr)
{
	dev_info(sc->dev, "%s : %dmA\n", __func__, curr);

	return sc760x_field_write(sc, IPRECHG,
			(curr - IPRECHG_BASE) / IPRECHG_LSB);
}

__maybe_unused static int sc760x_set_vfcchg(struct sc760x_chip *sc, int volt)
{
	dev_info(sc->dev, "%s : %dmV\n", __func__, volt);

	return sc760x_field_write(sc, VFC_CHG,
			(volt - VFC_CHG_BASE) / VFC_CHG_LSB);
}

__maybe_unused static int sc760x_set_batovp(struct sc760x_chip *sc, int volt)
{
	dev_info(sc->dev, "%s : %dmV\n", __func__, volt);

	return sc760x_field_write(sc, BAT_OVP,
			(volt - BAT_OVP_BASE) / BAT_OVP_LSB);
}

__maybe_unused static int sc760x_get_work_mode(struct sc760x_chip *sc, int *mode)
{
	return sc760x_field_read(sc, WORK_MODE, mode);
}

__maybe_unused static int sc760x_get_device_id(struct sc760x_chip *sc, int *id)
{
	return sc760x_field_read(sc, DEVICE_ID, id);
}

__maybe_unused static int sc760x_set_adc_enable(struct sc760x_chip *sc, bool en)
{
	return sc760x_field_write(sc, ADC_EN, !!en);
}

__maybe_unused static int sc760x_get_adc(struct sc760x_chip *sc, 
			int channel, int *result)
{
	int reg = 0x17 + channel * 2;
	u8 val[2] = {0};
	int ret;

	ret = sc760x_read_block(sc, reg, val, 2);
	if (ret) {
		return ret;
	}

	*result = (val[1] | (val[0] << 8)) * 
				sc760x_adc_m[channel] / sc760x_adc_l[channel];

	return ret;
}

static int sc760x_dump_reg(struct sc760x_chip *sc)
{
	int ret;
	int val;
	int i;

	for (i = 0; i <= 0x20; i++) {
		ret = regmap_read(sc->regmap, i, &val);
		dev_err(sc->dev, "%s reg[0x%02x] = 0x%02x\n", 
				__func__, i, val);
	}

	return ret;
}

static irqreturn_t sc760x_irq_handler(int irq, void *data)
{
	struct sc760x_chip *sc = data;

	dev_info(sc->dev, "%s\n", __func__);
	sc760x_dump_reg(sc);

	return IRQ_HANDLED;
}

static ssize_t sc760x_show_registers(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct sc760x_chip *sc = dev_get_drvdata(dev);
	u8 addr;
	int val;
	u8 tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sc7603");
	for (addr = 0x0; addr <= 0x20; addr++) {
		ret = regmap_read(sc->regmap, addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx,
					"Reg[%.2X] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t sc760x_store_register(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct sc760x_chip *sc = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= 0x20)
		regmap_write(sc->regmap, (unsigned char)reg, (unsigned char)val);

	return count;
}

static DEVICE_ATTR(registers, 0660, sc760x_show_registers, sc760x_store_register);

static void sc760x_create_device_node(struct device *dev)
{
	device_create_file(dev, &dev_attr_registers);
}

static int sc760x_set_charging_current(struct charger_device *chg_dev, u32 uA)
{
	int ret = 0;
	int curr = 0;
	struct sc760x_chip *sc = dev_get_drvdata(&chg_dev->dev);

	dev_info(sc->dev, "%s\n", __func__);
	curr = uA / 1000;
	ret = sc760x_set_ibat_limit(sc, curr);
	if (ret < 0) {
		dev_err(sc->dev, "%s set charging_current fail(%d)\n", __func__, ret);
		return ret;
	}

	return ret;

}

static int sc760x_dump_registers(struct charger_device *chg_dev)
{
	int ret = 0;
	int ibat = 0, vchr = 0, vbat = 0, real_power = 0;
	int pwr_lmt, work_mode, stat2, flag1, flag2;
	struct sc760x_chip *sc = dev_get_drvdata(&chg_dev->dev);

//	sc760x_dump_reg(sc);

	ret = sc760x_get_adc(sc, ADC_IBAT, &ibat);
	if (ret != 0) {
		dev_err(sc->dev, "%s get ibat adc failed= %d\n", 
				__func__, ret);
	}

	ret = sc760x_get_adc(sc, ADC_VCHG, &vchr);
	if (ret != 0) {
		dev_err(sc->dev, "%s get vchr adc failed= %d\n", 
				__func__, ret);
	}

	ret = sc760x_get_adc(sc, ADC_VBAT, &vbat);
	if (ret != 0) {
		dev_err(sc->dev, "%s get vchr adc failed= %d\n", 
				__func__, ret);
	}

	real_power = (vchr - vbat) * ibat / 1000;

	ret = regmap_read(sc->regmap, 0x02, &pwr_lmt);
	if (ret < 0) {
		dev_err(sc->dev, "%s get pwr_lmt failed= %d\n", 
				__func__, ret);
	}
	
	ret = regmap_read(sc->regmap, 0x0F, &work_mode);
	if (ret < 0) {
		dev_err(sc->dev, "%s get work_mode failed= %d\n", 
				__func__, ret);
	}
	
	ret = regmap_read(sc->regmap, 0x10, &stat2);
	if (ret < 0) {
		dev_err(sc->dev, "%s get stat2 failed= %d\n", 
				__func__, ret);
	}
	
	ret = regmap_read(sc->regmap, 0x11, &flag1);
	if (ret < 0) {
		dev_err(sc->dev, "%s get flag1 failed= %d\n", 
				__func__, ret);
	}
	
	ret = regmap_read(sc->regmap, 0x12, &flag2);
	if (ret < 0) {
		dev_err(sc->dev, "%s get flag2 failed= %d\n", 
				__func__, ret);
	}
	
	dev_err(sc->dev, "%s ibat = %dmA, vchr = %dmV, vbat = %dmV, real_power = %dmW, " \
			"reg[0x02] = 0x%02X, reg[0x0F] = 0x%02X, reg[0x10] = 0x%02X, " \
			"reg[0x11] = 0x%02X, reg[0x12] = 0x%02X", 
			__func__, ibat, vchr, vbat, real_power, pwr_lmt, work_mode, stat2, 
			flag1, flag2);
	return ret;

}

static int sc760x_set_low_power_mode(struct charger_device *chg_dev, bool en)
{
	int ret = 0;
	struct sc760x_chip *sc = dev_get_drvdata(&chg_dev->dev);
	static bool low_power_mode_en = false;

	dev_info(sc->dev, "%s, old:%d, new:%d\n",
		__func__, low_power_mode_en, en);

	if (low_power_mode_en == en)
		return ret;

	low_power_mode_en = en;

	if (en) {
		ret = sc760x_set_lowpower_mode(sc, true);
		if (ret < 0) {
			dev_err(sc->dev, "%s set low power mode fail(%d)\n", __func__, ret);
			return ret;
		}
		sc760x_set_adc_enable(sc, false);
	} else {
		ret = sc760x_set_lowpower_mode(sc, false);
		if (ret < 0) {
			dev_err(sc->dev, "%s set low power mode fail(%d)\n", __func__, ret);
			return ret;
		}
		sc760x_set_adc_enable(sc, true);
	}

	return ret;
}

static const struct charger_ops sc760x_chg_ops = {

	.set_low_power_mode = sc760x_set_low_power_mode,
	.set_charging_current = sc760x_set_charging_current,
	.dump_registers = sc760x_dump_registers,
};

static int sc760x_register_chgdev(struct sc760x_chip *sc)
{
	sc->chg_prop.alias_name = sc->cfg->chg_name;
	sc->chg_dev = charger_device_register(sc->cfg->chg_name, sc->dev,
					      sc, &sc760x_chg_ops,
					      &sc->chg_prop);

	if (!sc->chg_dev)
		return -EINVAL;

	return 0;
}

static int sc760x_parse_dt(struct sc760x_chip *sc, struct device *dev)
{
	struct device_node *np = dev->of_node;
	int i;
	int ret;
	struct {
		char *name;
		int *conv_data;
	} props[] = {
		{"sc,sc760x,bat-chg-lim-disable", &(sc->cfg->bat_chg_lim_disable)},
		{"sc,sc760x,bat-chg-lim", &(sc->cfg->bat_chg_lim)},
		{"sc,sc760x,pow-lim-disable", &(sc->cfg->pow_lim_disable)},
		{"sc,sc760x,pow-lim", &(sc->cfg->pow_lim)},
		{"sc,sc760x,ilim-disable", &(sc->cfg->ilim_disable)},
		{"sc,sc760x,load-switch-disable", &(sc->cfg->load_switch_disable)},
		{"sc,sc760x,low-power-mode-enable", &(sc->cfg->lp_mode_enable)},
		{"sc,sc760x,auto_bsm_th", &(sc->cfg->auto_bsm_th)},
		{"sc,sc760x,itrichg", &(sc->cfg->itrichg)},
		{"sc,sc760x,iprechg", &(sc->cfg->iprechg)},
		{"sc,sc760x,vfc-chg", &(sc->cfg->vfc_chg)},
		{"sc,sc760x,chg-ovp-disable", &(sc->cfg->chg_ovp_disable)},
		{"sc,sc760x,chg-ovp", &(sc->cfg->chg_ovp)},
		{"sc,sc760x,bat-ovp-disable", &(sc->cfg->bat_ovp_disable)},
		{"sc,sc760x,bat-ovp", &(sc->cfg->bat_ovp)},
		{"sc,sc760x,chg-ocp-disable", &(sc->cfg->chg_ocp_disable)},
		{"sc,sc760x,chg-ocp", &(sc->cfg->chg_ocp)},
		{"sc,sc760x,dsg-ocp-disable", &(sc->cfg->dsg_ocp_disable)},
		{"sc,sc760x,dsg-ocp", &(sc->cfg->dsg_ocp)},
		{"sc,sc760x,tdie-flt-disable", &(sc->cfg->tdie_flt_disable)},
		{"sc,sc760x,tdie-alm-disable", &(sc->cfg->tdie_alm_disable)},
		{"sc,sc760x,tdie-alm", &(sc->cfg->tdie_alm)},
	};

	ret = of_get_named_gpio(np, "sc760x,intr_gpio", 0);
	if (ret < 0) {
		dev_err(sc->dev, "no intr_gpio info\n");
		return ret;
	}
	sc->irq_gpio = ret;

	ret = of_get_named_gpio(np, "sc760x,enable_gpio", 0);
	if (ret < 0) {
		dev_err(sc->dev, "no enable_gpio info\n");
		return ret;
	}
	sc->enable_gpio = ret;

	ret = of_property_read_string(np, "chg_name", &(sc->cfg->chg_name));
	if (ret) {
		dev_err(sc->dev, "failed to read sc760x chg_name\n");
		return ret;
	}

	/* initialize data for optional properties */
	for (i = 0; i < ARRAY_SIZE(props); i++) {
		ret = of_property_read_u32(np, props[i].name,
						props[i].conv_data);
		if (ret < 0) {
			dev_err(sc->dev, "can not read %s\n", props[i].name);
			continue;
		}
	}

	return ret;
}

static int sc760x_enable_chip(struct sc760x_chip *sc)
{
	int ret = 0;

	ret = gpio_direction_output(sc->enable_gpio, 0);
	if (ret < 0) {
		dev_err(sc->dev, "failed to set GPIO%d ; ret = %d", sc->enable_gpio, ret);
		return ret;
	}
	dev_err(sc->dev, "%s gpio get val1(%d)\n", __func__, gpio_get_value(sc->enable_gpio));
	
	return ret;
}

static int sc760x_check_dev_info(struct sc760x_chip *sc)
{
	int ret = 0;
	int device_id;
	
	ret = sc760x_get_device_id(sc, &device_id);
	if (ret < 0) {
		dev_err(sc->dev, "get device id failed\n");
		return ret;
	}

	if (device_id != SC7603_DEVICE_ID) {
		dev_err(sc->dev, "device id not match\n");
		return -EINVAL;
	}

	return 0;
}

static int sc760x_init_device(struct sc760x_chip *sc)
{
	int ret = 0;
	int i;
	struct {
		enum sc760x_fields field_id;
		int conv_data;
	} props[] = {
		{IBAT_CHG_LIM_DIS, sc->cfg->bat_chg_lim_disable},
		{IBAT_CHG_LIM, sc->cfg->bat_chg_lim},
		{POW_LIM_DIS, sc->cfg->pow_lim_disable},
		{POW_LIM, sc->cfg->pow_lim},
		{ILIM_DIS, sc->cfg->ilim_disable},
		{LS_OFF, sc->cfg->load_switch_disable},
		{EN_LOWPOWER, sc->cfg->lp_mode_enable},
		{AUTO_BSM_TH, sc->cfg->auto_bsm_th},
		{ITRICHG, sc->cfg->itrichg},
		{IPRECHG, sc->cfg->iprechg},
		{VFC_CHG, sc->cfg->vfc_chg},
		{CHG_OVP_DIS, sc->cfg->chg_ovp_disable},
		{CHG_OVP, sc->cfg->chg_ovp},
		{BAT_OVP_DIS, sc->cfg->bat_ovp_disable},
		{BAT_OVP, sc->cfg->bat_ovp},
		{CHG_OCP_DIS, sc->cfg->chg_ocp_disable},
		{CHG_OCP, sc->cfg->chg_ocp},
		{DSG_OCP_DIS, sc->cfg->dsg_ocp_disable},
		{DSG_OCP, sc->cfg->dsg_ocp},
		{TDIE_FLT_DIS, sc->cfg->tdie_flt_disable},
		{TDIE_ALRM_DIS, sc->cfg->tdie_alm_disable},
		{TDIE_ALRM, sc->cfg->tdie_alm},
	};

	dev_err(sc->dev, "%s gpio get val2(%d)\n", __func__, gpio_get_value(sc->enable_gpio));

	for (i = 0; i < ARRAY_SIZE(props); i++) {
		ret = sc760x_field_write(sc, props[i].field_id, props[i].conv_data);
	}

	ret = devm_gpio_request(sc->dev, sc->enable_gpio, "sc760x,enable_gpio");
	if (ret < 0) {
		dev_err(sc->dev, "failed to request GPIO%d ; ret = %d", sc->enable_gpio, ret);
		return ret;
	}

	return sc760x_dump_reg(sc);
}

static int sc760x_register_interrupt(struct sc760x_chip *sc)
{
	int ret = 0;

	ret = devm_gpio_request(sc->dev, sc->irq_gpio, "sc760x,intr_gpio");
	if (ret < 0) {
		dev_err(sc->dev, "failed to request GPIO%d ; ret = %d", sc->irq_gpio, ret);
		return ret;
	}

	ret = gpio_direction_input(sc->irq_gpio);
	if (ret < 0) {
		dev_err(sc->dev, "failed to set GPIO%d ; ret = %d", sc->irq_gpio, ret);
		return ret;
	}

	sc->irq = gpio_to_irq(sc->irq_gpio);
	if (ret < 0) {
		dev_err(sc->dev, "failed gpio to irq GPIO%d ; ret = %d", sc->irq_gpio, ret);
		return ret;
	}

	ret = devm_request_threaded_irq(sc->dev, sc->irq, NULL,
					sc760x_irq_handler,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					sc760x_irq_name[sc->role], sc);
	if (ret < 0) {
		dev_err(sc->dev, "request thread irq failed:%d\n", ret);
		return ret;
	}

	enable_irq_wake(sc->irq);

	return 0;
}

static struct of_device_id sc760x_charger_match_table[] = {
	{   .compatible = "southchip,sc7603_master", 
		.data = &sc760x_role_data[SC760X_MASTER], },
	{   .compatible = "southchip,sc7603_slave", 
		.data = &sc760x_role_data[SC760X_SLAVE], },
	{	/* sentinel */ },
};

static int sc760x_charger_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct sc760x_chip *sc;
	const struct of_device_id *match;
	struct device_node *node = client->dev.of_node;
	int ret = 0;
	int i;

	pr_err("%s (%s)\n", __func__, SC760X_DRV_VERSION);

	sc = devm_kzalloc(&client->dev, sizeof(struct sc760x_chip), GFP_KERNEL);
	if (!sc)
		return -ENOMEM;

	sc->dev = &client->dev;
	sc->client = client;

	sc->regmap = devm_regmap_init_i2c(client,
					&sc760x_regmap_config);
	if (IS_ERR(sc->regmap)) {
		dev_err(sc->dev, "Failed to initialize regmap\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(sc760x_reg_fields); i++) {
		const struct reg_field *reg_fields = sc760x_reg_fields;

		sc->rmap_fields[i] =
			devm_regmap_field_alloc(sc->dev,
						sc->regmap,
						reg_fields[i]);
		if (IS_ERR(sc->rmap_fields[i])) {
			dev_err(sc->dev, "cannot allocate regmap field\n");
			return PTR_ERR(sc->rmap_fields[i]);
		}
	}

	i2c_set_clientdata(client, sc);
	sc760x_create_device_node(&(client->dev));

	match = of_match_node(sc760x_charger_match_table, node);
	if (match == NULL) {
		dev_err(sc->dev, "device tree match not found!\n");
		goto err_get_match;
	}

	sc->role = *(int *)match->data;

	sc->cfg = &default_cfg;
	ret = sc760x_parse_dt(sc, &client->dev);
	if (ret < 0) {
		dev_err(sc->dev, "%s parse dt failed(%d)\n", __func__, ret);
		goto err_parse_dt;
	}

	ret = sc760x_enable_chip(sc);
	if (ret < 0) {
		dev_err(sc->dev, "%s enable chip failed(%d)\n", __func__, ret);
		goto err_enable_chip;
	}
	
	ret = sc760x_check_dev_info(sc);
	if (ret < 0) {
		dev_err(sc->dev, "%s check dev failed(%d)\n", __func__, ret);
		goto err_check_dev;
	}
	
	ret = sc760x_init_device(sc);
	if (ret < 0) {
		dev_err(sc->dev, "%s init device failed(%d)\n", __func__, ret);
		goto err_init_device;
	}

	ret = sc760x_register_interrupt(sc);
	if (ret < 0) {
		dev_err(sc->dev, "%s register irq fail(%d)\n",
					__func__, ret);
		goto err_register_irq;
	}

	ret = sc760x_register_chgdev(sc);
	if (ret < 0) {
		dev_err(sc->dev, "%s register charger device fail(%d)\n",
					__func__, ret);
		goto err_register_device;
	}

	dev_err(sc->dev, "sc760x[%s] probe successfully!\n", 
			sc->role == SC760X_MASTER ? "master" : "slave");
	return 0;

err_register_device:
err_register_irq:
err_init_device:
err_check_dev:
err_enable_chip:
err_parse_dt:
err_get_match:
	dev_err(sc->dev, "sc760x probe failed!\n");
	devm_kfree(sc->dev, sc);
	return ret;
}

static void sc760x_charger_shutdown(struct i2c_client *client)
{
	struct sc760x_chip *sc = dev_get_drvdata(&client->dev);
	int ret = 0;

	if(IS_ERR_OR_NULL(sc)) {
		pr_err("%s sc is NULL\n", __func__);
		return;
	}

	ret = sc760x_set_lowpower_mode(sc, true);
	if (ret < 0) {
		dev_err(sc->dev, "%s set low power mode fail(%d)\n", __func__, ret);
	}
	dev_info(sc->dev, "shutdown successfully!");
}

static int sc760x_charger_remove(struct i2c_client *client)
{
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sc760x_suspend(struct device *dev)
{
	struct sc760x_chip *sc = dev_get_drvdata(dev);

	dev_info(sc->dev, "Suspend successfully!");
	if (device_may_wakeup(dev))
		enable_irq_wake(sc->irq);
	disable_irq(sc->irq);

	return 0;
}
static int sc760x_resume(struct device *dev)
{
	struct sc760x_chip *sc = dev_get_drvdata(dev);

	dev_info(sc->dev, "Resume successfully!");
	if (device_may_wakeup(dev))
		disable_irq_wake(sc->irq);
	enable_irq(sc->irq);

	return 0;
}

static const struct dev_pm_ops sc760x_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(sc760x_suspend, sc760x_resume)
};
#endif

static struct i2c_driver sc760x_charger_driver = {
	.driver	 = {
		.name   = "sc760x",
		.owner  = THIS_MODULE,
		.of_match_table = sc760x_charger_match_table,
#ifdef CONFIG_PM_SLEEP
		.pm = &sc760x_pm,
#endif
	},
	.probe	  = sc760x_charger_probe,
	.shutdown = sc760x_charger_shutdown,
	.remove	 = sc760x_charger_remove,
};

module_i2c_driver(sc760x_charger_driver);

MODULE_DESCRIPTION("SC SC760X Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("South Chip <Aiden-yu@southchip.com>");

