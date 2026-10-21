// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/i2c.h>
#include <linux/of_device.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/reboot.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/consumer.h>
#include <linux/phy/phy.h>

#include "tc_charger_class.h"
#include "tc_charger.h"
#include <linux/linear_range.h>
#include <linux/regmap.h>

#include "tc_sc8950.h"

#define GPIO_CHG_ENABLE     0
#define GPIO_CHG_DISABLE    1

#ifndef PAGE_SIZE
# define PAGE_SIZE 4096
#endif
#define SC8950_MANUFACTURER	"Southchip"

const unsigned int sc8950_otg_oc_threshold[] = {
	500000, 750000,1200000,1400000,1650000,1875000,2150000,2450000
};

static inline u8 sc8950_val_toreg(u32 min, u32 max, u32 step, u32 target,
				  bool ru)
{
	if (target <= min)
		return 0;

	if (target >= max)
		return (max - min) / step;

	if (ru)
		return (target - min + step) / step;
	return (target - min) / step;
}

static u8 sc8950_vboost_toreg(u32 uV)
{
	return sc8950_val_toreg(SC8950_VBOOST_MIN, SC8950_VBOOST_MAX, SC8950_VBOOST_STEP, uV, true);
}

/***************** I2C Operation *****************/
unsigned int __sc8950_read_byte(struct sc8950_info *info, unsigned char cmd, 
	unsigned char *returnData)
{
	unsigned char xfers = 2;
	int ret, retries = 3;
	unsigned char s_buf[1];

	s_buf[0] = cmd;
	
	do {
		struct i2c_msg msgs[2] = {
			{
				.addr = info->client->addr,
				.flags = 0,
				.len = 1,
				.buf = s_buf,
			},
			{

				.addr = info->client->addr,
				.flags = I2C_M_RD,
				.len = 1,
				.buf = returnData,
			}
		};

		/*
		 * Avoid sending the segment addr to not upset non-compliant
		 * DDC monitors.
		 */
		ret = i2c_transfer(info->client->adapter, msgs, xfers);

		if (ret == -ENXIO) {
			dev_err(info->dev, "skipping non-existent adapter %s\n",
				info->client->adapter->name);
			break;
		}

		if (ret != xfers)
			mdelay(10);
	} while (ret != xfers && --retries);

	return ret == xfers ? 0 : -1;
}

unsigned int sc8950_read_byte(struct sc8950_info *info, unsigned char cmd, 
	unsigned char *returnData)
{
	unsigned int ret = 0;
	
	mutex_lock(&info->sc8950_i2c_access);
	ret = __sc8950_read_byte(info, cmd, returnData);
	mutex_unlock(&info->sc8950_i2c_access);

	return ret;
}

unsigned int __sc8950_write_byte(struct sc8950_info *info, unsigned char cmd,
	unsigned char writeData)
{
	unsigned char xfers = 1;
	int ret, retries = 3;
	unsigned char buf[2];

	buf[0] = cmd;
	memcpy(&buf[1], &writeData, 1);

	do {
		struct i2c_msg msgs[1] = {
			{
				.addr = info->client->addr,
				.flags = 0,
				.len = 1 + 1,
				.buf = buf,
			},
		};
		/*
		 * Avoid sending the segment addr to not upset non-compliant
		 * DDC monitors.
		 */
		ret = i2c_transfer(info->client->adapter, msgs, xfers);

		if (ret == -ENXIO) {
			dev_err(info->dev, "skipping non-existent adapter %s\n",
				info->client->adapter->name);
			break;
		}

		if (ret != xfers)
			mdelay(10);
	} while (ret != xfers && --retries);

	return ret == xfers ? 1 : -1;
}

unsigned int sc8950_write_byte(struct sc8950_info *info, unsigned char cmd,
	unsigned char writeData)
{
	int ret = 0;

	mutex_lock(&info->sc8950_i2c_access);
	ret = __sc8950_write_byte(info, cmd, writeData);
	mutex_unlock(&info->sc8950_i2c_access);

	return ret;
}

unsigned int sc8950_update_bits(struct sc8950_info *info, unsigned char RegNum,
	unsigned char val, unsigned char mask,unsigned char shift)
{
	unsigned char sc8950_reg = 0, sc8950_reg_old = 0, sc8950_reg_new = 0;
	unsigned int ret = 0;
	
	mutex_lock(&info->sc8950_i2c_access);
	
	ret = __sc8950_read_byte(info, RegNum, &sc8950_reg);
	if (ret < 0) {
		dev_err(info->dev, "%s read Reg[0x%x] failed\n", __func__, RegNum);
		goto err;
	}

	sc8950_reg_old = sc8950_reg;
	
	sc8950_reg &= ~(mask << shift);
	sc8950_reg |= (val << shift);

	ret = __sc8950_write_byte(info, RegNum, sc8950_reg);
	if (ret < 0) {
		dev_err(info->dev, "%s write Reg[0x%x] failed\n", __func__, RegNum);
		goto err;
	}

	ret = __sc8950_read_byte(info, RegNum, &sc8950_reg_new);
	if (ret < 0) {
		dev_err(info->dev, "%s read Reg[0x%x] again failed\n", __func__, RegNum);
		goto err;
	}

err:
	mutex_unlock(&info->sc8950_i2c_access);
	dev_dbg(info->dev, "[%s] write Reg[%x]=0x%x from 0x%x new:0x%x\n", __func__,
		RegNum, sc8950_reg, sc8950_reg_old, sc8950_reg_new);

	return ret;
}

unsigned int sc8950_read_interface(struct sc8950_info *info, unsigned char RegNum,
				    unsigned char *val, unsigned char MASK,unsigned char SHIFT)
{
	unsigned char sc8950_reg = 0;
	unsigned int ret = 0;

	mutex_lock(&info->sc8950_i2c_access);

	ret = __sc8950_read_byte(info, RegNum, &sc8950_reg);
	if (ret < 0) {
		dev_err(info->dev, "[%s] read Reg[%x] failed ret = %d\n", __func__, RegNum, ret);
		goto err;
	}

	dev_info(info->dev, "[%s] Reg[%x]=0x%x ", __func__, RegNum, sc8950_reg);

	sc8950_reg &= (MASK << SHIFT);
	*val = (sc8950_reg >> SHIFT);

err:
	dev_dbg(info->dev, " val=0x%x\n", *val);
	mutex_unlock(&info->sc8950_i2c_access);

	return ret;
}
/***************** I2C Operation *****************/

/***************** SC8950 IC Reg Operation *****************/
static int sc8950_set_hiz(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s hiz_en:0x%x\n", __func__, value);
	if (info->boost_mode && value)
		return 0;
	return sc8950_update_bits(info, SC8950_REG_INPUT_CONTROL,
			value, SC8950_REG_INPUT_CONTROL_HIZ_MASK,
			SC8950_REG_INPUT_CONTROL_HIZ_SHIFT);
}

static int sc8950_get_hiz(struct sc8950_info *info, unsigned char *value)
{
	return sc8950_read_interface(info, SC8950_REG_INPUT_CONTROL,
			value, SC8950_REG_INPUT_CONTROL_HIZ_MASK,
			SC8950_REG_INPUT_CONTROL_HIZ_SHIFT);
}

static int sc8950_set_ibus(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s ibus:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_INPUT_CONTROL,
			value, SC8950_REG_INPUT_CONTROL_INDPM_MASK,
			SC8950_REG_INPUT_CONTROL_INDPM_SHIFT);
}

static int sc8950_get_ibus(struct sc8950_info *info, unsigned char *value)
{
	return sc8950_read_interface(info, SC8950_REG_INPUT_CONTROL,
			value, SC8950_REG_INPUT_CONTROL_INDPM_MASK,
			SC8950_REG_INPUT_CONTROL_INDPM_SHIFT);
}

static int __maybe_unused sc8950_set_dp(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s dp:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_DPDM_CONTROL1,
			value, SC8950_REG_DPDM_CONTROL1_DP_DAC_MASK,
			SC8950_REG_DPDM_CONTROL1_DP_DAC_SHIFT);
}

static int __maybe_unused sc8950_set_dm(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s dm:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_DPDM_CONTROL1,
			value, SC8950_REG_DPDM_CONTROL1_DM_DAC_MASK,
			SC8950_REG_DPDM_CONTROL1_DM_DAC_SHIFT);
}

static int sc8950_set_ico(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s ico_en:0x%x\n", __func__, value);
	
	return sc8950_update_bits(info, SC8950_REG_DPDM_CONTROL2,
			value, SC8950_REG_DPDM_CONTROL2_ICO_EN_MASK,
			SC8950_REG_DPDM_CONTROL2_ICO_EN_SHIFT);
}

static int sc8950_set_hvdcp(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s hvdcp_en:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_DPDM_CONTROL2,
			value, SC8950_REG_DPDM_CONTROL2_HVDCP_EN_MASK,
			SC8950_REG_DPDM_CONTROL2_HVDCP_EN_SHIFT);
}

static int sc8950_set_force_dpdm(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s force_dpdm_en:0x%x\n", __func__, value);
	
	return sc8950_update_bits(info, SC8950_REG_DPDM_CONTROL2,
			value, SC8950_REG_DPDM_CONTROL2_FORCE_DPDM_MASK,
			SC8950_REG_DPDM_CONTROL2_FORCE_DPDM_SHIFT);
}

static int sc8950_set_autodpdm(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s autodpdm_en:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_DPDM_CONTROL2,
			value, SC8950_REG_DPDM_CONTROL2_AUTO_DPDPM_EN_MASK,
			SC8950_REG_DPDM_CONTROL2_AUTO_DPDPM_EN_SHIFT);
}

static int sc8950_set_wdt_rst(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s wdt_rst:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_SYSTEM_CONTROL1,
			value, SC8950_REG_SYSTEM_CONTROL1_WD_RST_MASK,
			SC8950_REG_SYSTEM_CONTROL1_WD_RST_SHIFT);
}

static int sc8950_set_en_otg(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s otg en:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_SYSTEM_CONTROL1,
			value, SC8950_REG_SYSTEM_CONTROL1_OTG_CFG_MASK,
			SC8950_REG_SYSTEM_CONTROL1_OTG_CFG_SHIFT);
}

static int sc8950_get_en_otg(struct sc8950_info *info, unsigned char *value)
{
	return sc8950_read_interface(info, SC8950_REG_SYSTEM_CONTROL1,
			value, SC8950_REG_SYSTEM_CONTROL1_OTG_CFG_MASK,
			SC8950_REG_SYSTEM_CONTROL1_OTG_CFG_SHIFT);
}

static int sc8950_set_en_charge(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s charge en:0x%x,boost_mode:%d\n", __func__, value, info->boost_mode);
	/* in boost mode,if en charge will cut off reverse charge */
	if (info->boost_mode && value)
		return 0;

	return sc8950_update_bits(info, SC8950_REG_SYSTEM_CONTROL1,
				value, SC8950_REG_SYSTEM_CONTROL1_CHG_CFG_MASK,
				SC8950_REG_SYSTEM_CONTROL1_CHG_CFG_SHIFT);
}

static int sc8950_get_en_charge(struct sc8950_info *info, unsigned char *value)
{
	return sc8950_read_interface(info, SC8950_REG_SYSTEM_CONTROL1,
			value, SC8950_REG_SYSTEM_CONTROL1_CHG_CFG_MASK,
			SC8950_REG_SYSTEM_CONTROL1_CHG_CFG_SHIFT);
}

static int sc8950_set_vsys_min(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s vsys_min:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_SYSTEM_CONTROL1,
			value, SC8950_REG_SYSTEM_CONTROL1_VSYS_MIN_MASK,
			SC8950_REG_SYSTEM_CONTROL1_VSYS_MIN_SHIFT);
}

static int sc8950_set_vbatlow_otg(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s vbatlow_otg:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_SYSTEM_CONTROL1,
			value, SC8950_REG_SYSTEM_CONTROL1_VBATLOW_OTG_MASK,
			SC8950_REG_SYSTEM_CONTROL1_VBATLOW_OTG_SHIFT);
}

static int sc8950_set_icc(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s icc:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_ICC,
			value, SC8950_REG_ICC_ICC_MASK,
			SC8950_REG_ICC_ICC_SHIFT);
}

static int sc8950_get_icc(struct sc8950_info *info, unsigned char *value)
{
	return sc8950_read_interface(info, SC8950_REG_ICC,
			value, SC8950_REG_ICC_ICC_MASK,
			SC8950_REG_ICC_ICC_SHIFT);
}

static int sc8950_set_iprechg(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s iprechg:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_ITC_ITERM,
			value, SC8950_REG_ITC_ITERM_ITC_MASK,
			SC8950_REG_ITC_ITERM_ITC_SHIFT);
}

static int sc8950_set_iterm(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s iterm:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_ITC_ITERM,
			value, SC8950_REG_ITC_ITERM_ITERM_MASK,
			SC8950_REG_ITC_ITERM_ITERM_SHIFT);
}

static int sc8950_get_iterm(struct sc8950_info *info, unsigned char *value)
{
	return sc8950_read_interface(info, SC8950_REG_ITC_ITERM,
			value, SC8950_REG_ITC_ITERM_ITERM_MASK,
			SC8950_REG_ITC_ITERM_ITERM_SHIFT);
}

static int sc8950_set_vreg(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s vreg:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_VBAT,
			value, SC8950_REG_VBAT_VBAT_MASK,
			SC8950_REG_VBAT_VBAT_SHIFT);
}

static int sc8950_get_vreg(struct sc8950_info *info, unsigned char *value)
{
	return sc8950_read_interface(info, SC8950_REG_VBAT,
			value, SC8950_REG_VBAT_VBAT_MASK,
			SC8950_REG_VBAT_VBAT_SHIFT);
}

static int sc8950_set_vrechg(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s vrechg:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_VBAT,
			value, SC8950_REG_VBAT_VRECHG_MASK,
			SC8950_REG_VBAT_VRECHG_SHIFT);
}

static int sc8950_set_en_term(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s term en:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_SYSTEM_CONTROL2,
			value, SC8950_REG_SYSTEM_CONTROL2_EN_TERM_MASK,
			SC8950_REG_SYSTEM_CONTROL2_EN_TERM_SHIFT);
}

static int sc8950_set_statdis(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s STAT pin function en:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_SYSTEM_CONTROL2,
			value, SC8950_REG_SYSTEM_CONTROL2_STAT_DIS_MASK,
			SC8950_REG_SYSTEM_CONTROL2_STAT_DIS_SHIFT);
}

static int sc8950_set_watchdog_cfg(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s wtd_cfg:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_SYSTEM_CONTROL2,
			value, SC8950_REG_SYSTEM_CONTROL2_TWD_MASK,
			SC8950_REG_SYSTEM_CONTROL2_TWD_SHIFT);
}

static int sc8950_set_en_timer(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s safety timer en:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_SYSTEM_CONTROL2,
			value, SC8950_REG_SYSTEM_CONTROL2_EN_TIMER_MASK,
			SC8950_REG_SYSTEM_CONTROL2_EN_TIMER_SHIFT);
}

static int sc8950_get_en_timer(struct sc8950_info *info, unsigned char *value)
{
	return sc8950_read_interface(info, SC8950_REG_SYSTEM_CONTROL2,
			value, SC8950_REG_SYSTEM_CONTROL2_EN_TIMER_MASK,
			SC8950_REG_SYSTEM_CONTROL2_EN_TIMER_SHIFT);
}

static int sc8950_set_tchg(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s charge time:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_SYSTEM_CONTROL2,
			value, SC8950_REG_SYSTEM_CONTROL2_TCHG_MASK,
			SC8950_REG_SYSTEM_CONTROL2_TCHG_SHIFT);
}

static int sc8950_set_chg_thermal(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s chg_thermal:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_TREG,
			value, SC8950_REG_TREG_TREG_MASK,
			SC8950_REG_TREG_TREG_SHIFT);
}

static int sc8950_set_batfet_reset_en(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s batfet reset en:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_SYSTEM_CONTROL3,
			value, SC8950_REG_SYSTEM_CONTROL3_BATFET_RST_EN_MASK,
			SC8950_REG_SYSTEM_CONTROL3_BATFET_RST_EN_SHIFT);
}

static int sc8950_get_boostv(struct sc8950_info *info, unsigned char *value)
{
	return sc8950_read_interface(info, SC8950_REG_BOOST_CONTROL,
			value, SC8950_REG_BOOST_CONTROL_VBOOST_MASK,
			SC8950_REG_BOOST_CONTROL_VBOOST_SHIFT);
}

static int sc8950_set_boostv(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s boostv:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_BOOST_CONTROL,
			value, SC8950_REG_BOOST_CONTROL_VBOOST_MASK,
			SC8950_REG_BOOST_CONTROL_VBOOST_SHIFT);
}

static int sc8950_get_iboost(struct sc8950_info *info, unsigned char *value)
{
	return sc8950_read_interface(info, SC8950_REG_BOOST_CONTROL,
			value, SC8950_REG_BOOST_CONTROL_IBOOST_MASK,
			SC8950_REG_BOOST_CONTROL_IBOOST_SHIFT);
}

static int sc8950_set_iboost(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s iboost:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_BOOST_CONTROL,
			value, SC8950_REG_BOOST_CONTROL_IBOOST_MASK,
			SC8950_REG_BOOST_CONTROL_IBOOST_SHIFT);
}

static int sc8950_get_vbus_stat(struct sc8950_info *info, unsigned char *value)
{
	return sc8950_read_interface(info, SC8950_REG_STAT1,
			value, SC8950_REG_STAT1_VBUS_STAT_MASK,
			SC8950_REG_STAT1_VBUS_STAT_SHIFT);
}

static int sc8950_get_chg_stat(struct sc8950_info *info, unsigned char *value)
{
	return sc8950_read_interface(info, SC8950_REG_STAT1,
			value, SC8950_REG_STAT1_CHRG_STAT_MASK,
			SC8950_REG_STAT1_CHRG_STAT_SHIFT);
}

static int sc8950_get_vg_stat(struct sc8950_info *info, unsigned char *value)
{
	return sc8950_read_interface(info, SC8950_REG_STAT6,
			value, SC8950_REG_STAT6_VBUS_GD_MASK,
			SC8950_REG_STAT6_VBUS_GD_SHIFT);
}

static int sc8950_get_shipmode_stat(struct sc8950_info *info, unsigned char *value)
{
	return sc8950_read_interface(info, SC8950_REG_SYSTEM_CONTROL3,
			value, SC8950_REG_SYSTEM_CONTROL3_BATFET_DIS_MASK,
			SC8950_REG_SYSTEM_CONTROL3_BATFET_DIS_SHIFT);
}


static int sc8950_set_force_vindpm(struct sc8950_info *info, unsigned char value)
{
	unsigned int ret = 0, cyc_cnt = 3;
	unsigned char retval;

	dev_info(info->dev, "%s force vindpm:0x%x\n", __func__, value);

	sc8950_read_interface(info, SC8950_REG_VINDPM,
			&retval, SC8950_REG_VINDPM_FORCE_MASK,
			SC8950_REG_VINDPM_FORCE_SHIFT);
	
	while(retval != 0x1 && cyc_cnt > 0){
		/* 0x1: Run Absolute VINDPM Threshold */
		sc8950_update_bits(info, SC8950_REG_VINDPM,
				0x1, SC8950_REG_VINDPM_FORCE_MASK,
				SC8950_REG_VINDPM_FORCE_SHIFT);

		msleep(5);
		cyc_cnt--;
		sc8950_read_interface(info, SC8950_REG_VINDPM,
				&retval, SC8950_REG_VINDPM_FORCE_MASK,
				SC8950_REG_VINDPM_FORCE_SHIFT);
	}
	
	ret = sc8950_update_bits(info, SC8950_REG_VINDPM,
			value, SC8950_REG_VINDPM_VALUE_MASK,
			SC8950_REG_VINDPM_VALUE_SHIFT);

	return ret;
}

static int sc8950_get_force_vindpm(struct sc8950_info *info, unsigned char *value)
{
	unsigned int ret = 0;
	unsigned char retval;
	
	sc8950_read_interface(info, SC8950_REG_VINDPM,
			&retval, SC8950_REG_VINDPM_FORCE_MASK,
			SC8950_REG_VINDPM_FORCE_SHIFT);

	if(retval != 0x1) {
		dev_info(info->dev, "%s its not force vindpm,return!\n", __func__);
	} else {
		dev_info(info->dev, "%s its force vindpm!\n", __func__);
		ret = sc8950_read_interface(info, SC8950_REG_VINDPM,
				value, SC8950_REG_VINDPM_VALUE_MASK,
				SC8950_REG_VINDPM_VALUE_SHIFT);
	}
	return ret;
}

static int sc8950_get_vindpm_stat(struct sc8950_info *info, unsigned char *value)
{
	return sc8950_read_interface(info, SC8950_REG_STAT8,
			value, SC8950_REG_STAT8_VINDPM_STAT_MASK,
			SC8950_REG_STAT8_VINDPM_STAT_SHIFT);
}

static int sc8950_get_iindpm_stat(struct sc8950_info *info, unsigned char *value)
{
	return sc8950_read_interface(info, SC8950_REG_STAT8,
			value, SC8950_REG_STAT8_IINDPM_STAT_MASK,
			SC8950_REG_STAT8_IINDPM_STAT_SHIFT);
}

static int sc8950_get_pd(struct sc8950_info *info, unsigned char *value)
{
	return sc8950_read_interface(info, SC8950_REG_PN,
			value, SC8950_REG_PN_PN_MASK,
			SC8950_REG_PN_PN_SHIFT);
}

static int sc8950_set_rst(struct sc8950_info *info, unsigned char value)
{
	dev_info(info->dev, "%s rst:0x%x\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_PN,
			value, SC8950_REG_PN_REG_RST_MASK,
			SC8950_REG_PN_REG_RST_SHIFT);
}

static int __sc8950_set_key(struct sc8950_info *info)
{
	sc8950_write_byte(info, SC8950_REG_7D, SC8950_KEY1);
	sc8950_write_byte(info, SC8950_REG_7D, SC8950_KEY2);
	sc8950_write_byte(info, SC8950_REG_7D, SC8950_KEY3);
	return sc8950_write_byte(info, SC8950_REG_7D, SC8950_KEY4);
}

static int __maybe_unused sc8950_set_private(struct sc8950_info *info)
{
	int ret;
	unsigned char val;

	ret = sc8950_read_byte(info, SC8950_REG_DPDM3, &val);
	if (ret < 0) {
		__sc8950_set_key(info);
	}

	sc8950_write_byte(info, SC8950_REG_DPDM3, SC8950_PRIVATE);

	return __sc8950_set_key(info);
}

static int __maybe_unused sc8950_set_transsin_protocol(struct sc8950_info *info, bool en)
{
	int ret;
	unsigned char sc8950_reg = 0;

	ret = sc8950_read_byte(info, SC8950_REG_83, &sc8950_reg);
	if (ret < 0) {
		__sc8950_set_key(info);
	}

	sc8950_update_bits(info, SC8950_REG_83, (!!en), 
			SC8950_REG_83_TC30_DPDM_SET_MASK,
			SC8950_REG_83_TC30_DPDM_SET_SHIFT);

	return __sc8950_set_key(info);
}

static int sc8950_get_reg83(struct sc8950_info *info, unsigned char *val)
{
	int ret;

	ret = sc8950_read_byte(info, SC8950_REG_83, val);
	if (ret < 0) {
		__sc8950_set_key(info);
	}

	sc8950_read_byte(info, SC8950_REG_83, val);

	dev_dbg(info->dev, "[%s] Reg[%x]=0x%x\n", __func__, SC8950_REG_83, *val);

	return __sc8950_set_key(info);
}

static int sc8950_set_ovp(struct sc8950_info *info, unsigned char val)
{
	int ret;
	unsigned char sc8950_reg = 0;

	ret = sc8950_read_byte(info, SC8950_REG_83, &sc8950_reg);
	if (ret < 0) {
		__sc8950_set_key(info);
	}

	sc8950_update_bits(info, SC8950_REG_83, val, 
			SC8950_REG_83_VBUS_OVP_MASK,
			SC8950_REG_83_VBUS_OVP_SHIFT);

	return __sc8950_set_key(info);
}
/***************** SC8950 IC Reg Operation *****************/

/************** SC8950 Charger Ops Operation ***************/
static int sc8950_set_ichg(struct charger_device *chg_dev, u32 uA)
{
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	if(uA >= SC8950_ICC_MAX) {
		uA = SC8950_ICC_MAX;
	}
	dev_info(info->dev, "%s set %d uA\n", __func__, uA);

	return sc8950_set_icc(info, uA / SC8950_ICC_LSB);
}

static int sc8950_set_aicr(struct charger_device *chg_dev, u32 uA)
{
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);
	u32 tmp = uA;
	unsigned char reg_val = 0;

	if(uA >= SC8950_AICR_MAX) {
		tmp = SC8950_AICR_MAX;
	}else if (uA <= SC8950_AICR_OFFSET) {
		tmp = SC8950_AICR_OFFSET;
	}
	reg_val = (tmp - SC8950_AICR_OFFSET) / SC8950_AICR_LSB;
	
	dev_info(info->dev, "%s uA:%d reg_val:%d\n", __func__, uA, reg_val);
	
	return sc8950_set_ibus(info, reg_val);
}

static int __maybe_unused sc8950_check_bc12_done(struct sc8950_info *info, unsigned char *state)
{
	int ret = 0;
	unsigned char val = 0;

	ret = sc8950_read_byte(info, SC8950_REG_STAT1, &val);
	if (ret < 0) {
		dev_err(info->dev, "%s failed!\n", __func__);
		return ret;
	}

	*state = ((val & 0x04) >> 2);

	return 0;
}

static int sc8950_plug_in(struct charger_device *chg_dev)
{
	int ret = 0;
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);
	dev_info(info->dev, "%s\n", __func__);

	ret = sc8950_set_en_charge(info, 0x1);
	if (ret < 0) {
		dev_err(info->dev, "%s dis charge fail(%d)\n", __func__, ret);
		return ret;
	}

	/* enable wdt */
	ret = sc8950_set_watchdog_cfg(info, SC8950_WDT_TIME_DIS);
	if(ret < 0) {
		dev_err(info->dev, "%s set wdt failed ret:%d\n", __func__, ret);
		return ret;
	}

	return ret;
}

static int sc8950_plug_out(struct charger_device *chg_dev)
{
	int ret = 0;
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	dev_info(info->dev, "%s\n", __func__);

	/* Disable charging */
	ret = sc8950_set_en_charge(info, 0x0);
	if (ret < 0) {
		dev_err(info->dev, "%s dis charge fail(%d)\n", __func__, ret);
		return ret;
	}

	/* Disable WDT */
	ret = sc8950_set_watchdog_cfg(info, SC8950_WDT_TIME_DIS);
	if (ret < 0) {
		dev_err(info->dev, "%s set wdt fail(%d)\n", __func__, ret);
	}

	info->vbus_stat_pre = false;

	return ret;
}

static int sc8950_enable_charging(struct charger_device *chg_dev, bool en)
{
	int ret = 0;
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);
	int val = en ? GPIO_CHG_ENABLE : GPIO_CHG_DISABLE;

	ret = sc8950_set_en_charge(info, en);
	if (ret < 0) {
		dev_err(info->dev, "%s enable charge fail(%d)\n", __func__, ret);
		return ret;
	}

	if (gpio_is_valid(info->en_gpio)) {
		gpio_direction_output(info->en_gpio, val);
	}

	dev_info(info ->dev, "%s en:%d\n", __func__, en);
	return ret;
}

static int sc8950_is_charging_enabled(struct charger_device *chg_dev, bool *en)
{
	int ret;
	unsigned char val = 0;
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	ret = sc8950_get_en_charge(info, &val);
	if (ret < 0) {
		dev_err(info->dev, "%s get charge stat fail(%d)\n", __func__, ret);
		return ret;
	}

	if (gpio_is_valid(info->en_gpio)) {
		val = gpio_get_value(info->en_gpio);
		if (val == GPIO_CHG_ENABLE)
			*en = true;
		else
			*en = false;
		pr_debug("%s: sc8950 gpio is %s\n",
			__func__, *en ? "enabled" : "disabled");
		return 0;
	}

	*en = val;

	dev_info(info ->dev, "%s en:%d\n", __func__, *en);

	return ret;
}

static int sc8950_get_ichg(struct charger_device *chg_dev, u32 *uA)
{
	int ret = 0;
	unsigned char val;
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	ret = sc8950_get_icc(info, &val);
	if (ret < 0) {
		dev_err(info->dev, "%s get icc fail(%d)\n", __func__, ret);
		return ret;
	}

	*uA = val * SC8950_ICC_LSB;

	dev_info(info ->dev, "%s uA:%d\n", __func__, *uA);

	return ret;
}

static int sc8950_get_min_ichg(struct charger_device *chg_dev, u32 *uA)
{
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);
	
	*uA = SC8950_ICC_MIN;
	
	dev_info(info->dev, "%s %d uA\n", __func__, *uA);
	
	return 0;
}

static int sc8950_get_cv(struct charger_device *chg_dev, u32 *uV)
{
	int ret = 0;
	unsigned char val;
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	ret = sc8950_get_vreg(info, &val);
	if (ret < 0) {
		dev_err(info->dev, "%s get cv fail(%d)\n", __func__, ret);
		return ret;
	}

	*uV = SC8950_CV_BASE + val * SC8950_CV_LSB;
	dev_info(info ->dev, "%s uV:%d\n", __func__, *uV);

	return ret;
}

static int sc8950_set_cv(struct charger_device *chg_dev, u32 uV)
{
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	if(uV >= SC8950_CV_MAX) {
		uV = SC8950_CV_MAX;
	}
	dev_info(info->dev, "%s set %d uV\n", __func__, uV);

	return sc8950_set_vreg(info, (uV - SC8950_CV_BASE) / SC8950_CV_LSB);
}

static int sc8950_get_aicr(struct charger_device *chg_dev, u32 *uA)
{
	int ret = 0;
	unsigned char val;
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	ret = sc8950_get_ibus(info, &val);
	if (ret < 0) {
		dev_err(info->dev, "%s get ibus fail(%d)\n", __func__, ret);
		return ret;
	}

	*uA = val * SC8950_AICR_LSB + SC8950_AICR_OFFSET;
	dev_info(info ->dev, "%s uA:%d\n", __func__, *uA);

	return ret;
}

static int sc8950_get_min_aicr(struct charger_device *chg_dev, u32 *uA)
{
	*uA = SC8950_AICR_MIN;

	return 0;
}

static int sc8950_get_ieoc(struct charger_device *chg_dev, u32 *uA)
{
	int ret = 0;
	unsigned char val;
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	ret = sc8950_get_iterm(info, &val);
	if (ret < 0) {
		dev_err(info->dev, "%s get ieoc fail(%d)\n", __func__, ret);
		return ret;
	}

	*uA = SC8950_ITERM_BASE + val * SC8950_ITERM_LSB;
	dev_info(info ->dev, "%s uA:%d\n", __func__, *uA);

	return ret;
}

static int sc8950_set_ieoc(struct charger_device *chg_dev, u32 uA)
{
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);
	unsigned char ieoc = 0;

	if(uA >= SC8950_ITERM_MAX) {
		uA = SC8950_ITERM_MAX;
	}

	ieoc = (uA - SC8950_ITERM_BASE) / SC8950_ITERM_LSB;

	dev_info(info->dev, "%s set %d uA reg:0x%x\n", __func__, uA, ieoc);

	return sc8950_set_iterm(info, ieoc);
}

static int sc8950_kick_wdt(struct charger_device *chg_dev)
{
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	return sc8950_set_wdt_rst(info, 1);
}

static int sc8950_set_mivr(struct charger_device *chg_dev, u32 uV)
{
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	if(uV >= SC8950_VINDPM_MAX) {
		uV = SC8950_VINDPM_MAX;
	}
	dev_info(info->dev, "%s set %d uV\n", __func__, uV);

	return sc8950_set_force_vindpm(info, (uV - SC8950_VINDPM_BASE) / SC8950_VINDPM_LSB);
}

int sc8950_get_mivr(struct charger_device *chg_dev, u32 *uV)
{
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);
	unsigned char val = 0;
	int ret = 0;

	ret = sc8950_get_force_vindpm(info, &val);

	*uV = val * SC8950_VINDPM_LSB + SC8950_VINDPM_BASE;
	if(*uV <= SC8950_VINDPM_MIN)
		*uV = SC8950_VINDPM_MIN;

	dev_info(info->dev, "%s get mivr: %d uV\n", __func__, *uV);

	return ret;
}

static int sc8950_get_mivr_state(struct charger_device *chg_dev, bool *in_loop)
{
	int ret;
	unsigned char val;
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	ret = sc8950_get_vindpm_stat(info, &val);
	if (ret < 0) {
		dev_err(info->dev, "%s get mivr_state failed(%d)\n", __func__, ret);
		return ret;
	}

	*in_loop = val;
	dev_info(info ->dev, "%s mivr_state:0x%x\n", __func__, *in_loop);

	return ret;
}

static int sc8950_get_aicr_state(struct charger_device *chg_dev, bool *in_loop)
{
	int ret;
	unsigned char val;
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	ret = sc8950_get_iindpm_stat(info, &val);
	if (ret < 0) {
		dev_err(info->dev, "%s get aicr_state failed(%d)\n", __func__, ret);
		return ret;
	}

	*in_loop = val;
	dev_info(info ->dev, "%s aicr_state:0x%x\n", __func__, *in_loop);

	return ret;
}


static int sc8950_en_hz(struct charger_device *chg_dev, bool en)
{
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	dev_info(info ->dev, "%s powerpath en:0x%x\n", __func__, en);

	return sc8950_set_hiz(info, en);
}

static int sc8950_enable_powerpath(struct charger_device *chg_dev, bool en)
{
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	dev_info(info ->dev, "%s powerpath en:0x%x\n", __func__, en);

	return sc8950_set_hiz(info, !en);
}

static int sc8950_is_powerpath_enabled(struct charger_device *chg_dev, bool *en)
{
	int ret;
	unsigned char val;
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	ret = sc8950_get_hiz(info, &val);
	if (ret < 0) {
		dev_err(info->dev, "%s get powerpath_enabled state failed(%d)\n", __func__, ret);
		return ret;
	}

	*en = !val;
	dev_info(info ->dev, "%s powerpath_enabled:0x%x\n", __func__, *en);

	return 0;
}

static int sc8950_enable_safety_timer(struct charger_device *chg_dev, bool en)
{
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	return sc8950_set_en_timer(info, en);
}

static int sc8950_is_safety_timer_enabled(struct charger_device *chg_dev,
                    bool *en)
{
	int ret;
	unsigned char val;
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	ret = sc8950_get_en_timer(info, &val);
	if (ret < 0) {
		dev_err(info->dev, "%s get safety_timer_enabled state failed(%d)\n", __func__, ret);
		return ret;
	}

	*en = val;
	dev_info(info ->dev, "%s safety_timer_enabled:0x%x\n", __func__, *en);

	return ret;
}

static int sc8950_enable_te(struct charger_device *chg_dev, bool en)
{
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	return sc8950_set_en_term(info, en);
}

static int sc8950_enable_otg(struct charger_device *chg_dev, bool en)
{
	int ret;
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	info->boost_mode = en;

	if (en) {
		ret = sc8950_set_watchdog_cfg(info, SC8950_WDT_TIME_DIS);
		if (ret < 0) {
			dev_err(info->dev, "%s set wdt fail(%d)\n", __func__, ret);
			return ret;
		}

		ret=sc8950_set_hiz(info,!en);
		if (ret < 0) {
			dev_err(info->dev, "%s set wdt fail(%d)\n", __func__, ret);
			return ret;
		}
	}

	ret = sc8950_set_en_charge(info, !en);
	if (ret < 0) {
		dev_err(info->dev, "%s set charge fail(%d)\n", __func__, ret);
		return ret;
	}
	ret = sc8950_set_en_otg(info, en);
	if (ret < 0) {
		dev_err(info->dev, "%s set otg fail(%d)\n", __func__, ret);
		return ret;
	}

	if (!en) {
		ret = sc8950_set_watchdog_cfg(info, SC8950_WDT_TIME_DIS);
		if (ret < 0) {
			dev_err(info->dev, "%s set wdt fail(%d)\n", __func__, ret);
		}
	}

	return ret;
}

static int sc8950_set_boost_current_limit(struct charger_device *chg_dev,
                    u32 uA)
{
	unsigned char val;
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	if (uA <= 500000) {
		val = 0x0;
	} else if (uA <= 750000) {
		val = 0x1;
	} else if (uA <= 1200000) {
		val = 0x2;
	} else if (uA <= 1400000) {
		val = 0x3;
	} else if (uA <= 1650000) {
		val = 0x4;
	} else if (uA <= 1875000) {
		val = 0x5;
	} else if (uA <= 2150000) {
		val = 0x6;
	} else {
		val = 0x7;
	}

	dev_info(info->dev, "%s uA:%d reg_val:0x%x\n", __func__, uA, val);

	return sc8950_set_iboost(info, val);
}
static int sc8950_set_boost_voltage_limit(struct charger_device *chg_dev,
					u32 uV)
{
	unsigned char regval=0;
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	regval = sc8950_vboost_toreg(uV);
	dev_info(info->dev, "%s: uV=%d regval=%d\n",__func__,uV,regval);
	return sc8950_set_boostv(info,regval);
}
static int __sc8950_dump_registers(struct sc8950_info *info)
{
	int ret;
	int i;
	unsigned char val;
	unsigned char reg83_val = 0;

	for (i = 0; i < SC8950_REG_MAX; i++) {
		ret = sc8950_read_byte(info, i, &val);
		if (ret < 0) {
			return ret;
		}
		dev_info(info->dev, "%s reg[0x%02x] = 0x%02x\n", __func__, i, val);
	}

	sc8950_get_reg83(info, &reg83_val);
	dev_info(info->dev, "%s reg[0x%02x] = 0x%02x\n", __func__, SC8950_REG_83, reg83_val);

	return 0;
}

static int sc8950_dump_registers(struct charger_device *chg_dev)
{
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	return __sc8950_dump_registers(info);
}

static int sc8950_set_dp_function(struct charger_device *chg_dev, enum dpdm_ctrl_status status)
{
	int ret = 0;
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	dev_info(info->dev, "sc8950 set dp %s\n", dpdm_ctrl_name[status]);
	switch (status) {
		case DPDM_CTRL_HZ:
			ret = sc8950_set_dp(info, SC8950_USB_HIZ);
			break;
		case DPDM_CTRL_0V:
			ret = sc8950_set_dp(info, SC8950_USB_0V);
			break;
		case DPDM_CTRL_0_6V:
			ret = sc8950_set_dp(info, SC8950_USB_0_6V);
			break;
		case DPDM_CTRL_3_3V:
			ret = sc8950_set_dp(info, SC8950_USB_3_3V);
			break;
		default:
			break;
	}

	return ret;
}

static int sc8950_set_dm_function(struct charger_device *chg_dev, enum dpdm_ctrl_status status)
{
	int ret = 0;
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	dev_info(info->dev, "sc8950 set dm %s\n", dpdm_ctrl_name[status]);
	switch (status) {
		case DPDM_CTRL_HZ:
			ret = sc8950_set_dm(info, SC8950_USB_HIZ);
			break;
		case DPDM_CTRL_0V:
			ret = sc8950_set_dm(info, SC8950_USB_0V);
			break;
		case DPDM_CTRL_0_6V:
			ret = sc8950_set_dm(info, SC8950_USB_0_6V);
			break;
		case DPDM_CTRL_3_3V:
			ret = sc8950_set_dm(info, SC8950_USB_3_3V);
			break;
		default:
			break;
	}

	return ret;
}

static int sc8950_check_fault_state(struct sc8950_info *info, unsigned char *state)
{
	int ret = 0;

	ret = sc8950_read_byte(info, SC8950_REG_STAT2, state);
	if (ret < 0) {
		dev_err(info->dev, "%s read reg[0x%x] failed!\n", __func__, SC8950_REG_STAT2);
		return ret;
	}

	return 0;
}

static int sc8950_get_port_stat(struct charger_device *chg_dev, int *port_stat)
{
	int ret = 0;
	unsigned char val = 0;
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);
	/* static int i = 0; */

	*port_stat = TC_PORT_STAT_NOINFO; 

	ret = sc8950_get_vbus_stat(info,&val);
	if (ret < 0) {
		dev_err(info->dev, "fail to get port stat");
		goto out;
	}

	switch (val) {
		case SC8950_CHG_TYPE_SDP:
			*port_stat = TC_PORT_STAT_SDP;
			break;
		case SC8950_CHG_TYPE_CDP:
			*port_stat = TC_PORT_STAT_CDP;
			break;
		case SC8950_CHG_TYPE_DCP:
			*port_stat = TC_PORT_STAT_DCP;
			break;
		case SC8950_CHG_TYPE_NOVBUS:
			*port_stat = TC_PORT_STAT_NOINFO; 
			break;
		case SC8950_CHG_TYPE_UNKNOW:
		case SC8950_CHG_TYPE_SDPNSTD:
		default:
			*port_stat = TC_PORT_STAT_UNKNOWN_TA; 
			break;
	}


out:
	dev_info(info->dev, "sc8950 port stat:%d\n", val);
	return ret;

}

static int sc8950_get_vbus_gd_stat(struct charger_device *chg_dev, bool *vbus_gd)
{
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);
	unsigned char stat = 0;

	sc8950_get_vg_stat(info, &stat);

	pr_info("%s: vbus_gd_stat:%d\n", __func__, stat);

	*vbus_gd = stat;

	return 0;
}

static int sc8950_enable_chg_type_det(struct charger_device *chg_dev, bool en)
{
	int ret = 0;
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	if (info->boost_mode && en) {
		dev_notice(info->dev, "in boost mode!\n");
		goto out;
	}

	sc8950_set_force_dpdm(info, 1);
out:
	return ret;
}

static int sc8950_set_ircmp(struct charger_device *chg_dev, u32 value)
{
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	if(value >= SC8950_BAT_COMP_MAX) {
		value = SC8950_BAT_COMP_MAX;
	}

	dev_err(info->dev, "%s set ircmp:%d\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_TREG,
			value / SC8950_BAT_COMP_LSB, SC8950_REG_TREG_BAT_COMP_MASK,
			SC8950_REG_TREG_BAT_COMP_SHIFT);
}

static int sc8950_set_ivcmp(struct charger_device *chg_dev, u32 value)
{
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	if(value >= SC8950_VCLAMP_MAX) {
		value = SC8950_VCLAMP_MAX;
	}

	dev_err(info->dev, "%s set ivcmp:%d\n", __func__, value);
	return sc8950_update_bits(info, SC8950_REG_TREG,
			value / SC8950_VCLAMP_LSB, SC8950_REG_TREG_VCLAMP_MASK,
			SC8950_REG_TREG_VCLAMP_SHIFT);
}

static int sc8950_is_charging_done(struct charger_device *chg_dev, bool *done)
{
	int ret;
	unsigned char val;
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);

	ret = sc8950_get_chg_stat(info, &val);
	if (ret < 0) {
		dev_err(info->dev, "%s get chg stat fail(%d)\n", __func__, ret);
		return ret;
	}

	*done = (val == 3 ? true : false);
	dev_info(info->dev, "%s done:%d\n", __func__, *done);

	return ret;
}

static int sc8950_get_shipmode_status(struct charger_device *chg_dev)
{
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);
	int ret = 0;
	unsigned char val = 0;
	ret = sc8950_get_shipmode_stat(info, &val);
	if (ret < 0)
		pr_err("%s get failed!\n", __func__);

	pr_info("%s : %d\n", __func__, val);
	return ret < 0 ? 0 : val;
}

static int sc8950_set_shipmode(struct charger_device *chg_dev, bool on)
{
	struct sc8950_info *info = dev_get_drvdata(&chg_dev->dev);
	dev_info(info->dev, "%s on:%d\n", __func__, on);
	sc8950_update_bits(info, SC8950_REG_SYSTEM_CONTROL3, on, SC8950_REG_SYSTEM_CONTROL3_BATFET_DLY_MASK, SC8950_REG_SYSTEM_CONTROL3_BATFET_DLY_SHIFT);
	mdelay(10);
	sc8950_update_bits(info, SC8950_REG_SYSTEM_CONTROL3, on, SC8950_REG_SYSTEM_CONTROL3_BATFET_DIS_MASK, SC8950_REG_SYSTEM_CONTROL3_BATFET_DIS_SHIFT);
	return 0;
}

static bool sc8950_check_devinfo(struct sc8950_info *info)
{
	unsigned char devinfo = 0;
	int ret = 0;

	ret = sc8950_get_pd(info, &devinfo);
	if (ret < 0) {
		dev_notice(info->dev, "%s get_pd fail(%d)\n", __func__, ret);
		return false;
	}

	switch(devinfo) {
		case SC8950_VENDOR_ID:
			info->vendor_id = SC8950_VENDOR_ID;
			break;
		default:
			info->vendor_id = SC8950_VENDOR_ID_UNKNOW;
			return false;
	};

	dev_info(info->dev, "%s chip id is 0x%x\n", __func__, devinfo);

	return true;
}
/************** SC8950 Charger Ops Operation ***************/

static int sc8950_parse_dt(struct sc8950_info *info,  struct device *dev)
{
	struct device_node *np = dev->of_node;
	int ret = 0;

	dev_info(info->dev, "%s\n", __func__);
	if (!np) {
		dev_info(info->dev, "%s: no of node\n", __func__);
		return -ENODEV;
	}

	ret = of_property_read_string(np, "chg_name", &info->chg_name);
	if (ret < 0) {
		info->chg_name="primary_chg";
		dev_err(info->dev, "%s no chg_name(%d)\n", __func__, ret);
	}

	ret = of_property_read_string(np, "chg_alias_name", &(info->chg_props.alias_name));
	if (ret < 0) {
               info->chg_props.alias_name = "sc8950";
               pr_info("%s: no alias name\n", __func__);
       }

	info->intr_gpio = of_get_named_gpio(np, "sc8950,intr_gpio", 0);
	if(info->intr_gpio < 0){
		info->intr_gpio = U32_MAX;
		pr_info("%s irq_gpio=%d get fail\n", __func__, info->intr_gpio);
	}

	info->en_gpio = of_get_named_gpio(np, "sc8950,en_gpio", 0);
	if (info->en_gpio < 0){
		info->en_gpio = U32_MAX;
		pr_info("%s en_gpio = %d get fail\n", __func__, info->en_gpio);
	} else {
		ret = gpio_request(info->en_gpio, "sc8950 chg en pin");
		if (ret) {
			pr_err("%s: %d en_gpio request failed\n", __func__, info->en_gpio);
		} else {
			gpio_direction_output(info->en_gpio, GPIO_CHG_DISABLE);
		}
	}
	return 0;
}

static int sc8950_hw_init(struct sc8950_info *info)
{
	dev_info(info->dev, "%s", __func__);

	sc8950_set_rst(info, 0x1);				/* reset chip register */
	sc8950_set_ico(info, 0x0);				/* disable ico */
	sc8950_set_hvdcp(info, 0x0);				/* disable hvdcp handshake */
	sc8950_set_autodpdm(info, 0x0);			/* disable auto dp/dm detection */
	sc8950_set_hiz(info, 0x0);				/* disable hiz */
	sc8950_set_force_vindpm(info, 0x14);		/* VIN DPM check 4.5V */
	sc8950_set_wdt_rst(info, 0x1);			/* Kick watchdog */
	sc8950_set_vsys_min(info, 0x5);			/* Minimum system voltage 3.5V */
	sc8950_set_iprechg(info, 0x7);			/* Precharge current 480mA */
	sc8950_set_iterm(info, 0x3);				/* Termination current 180mA */
	sc8950_set_vreg(info, 0x2B);				/* VREG 4.528V */
	sc8950_set_statdis(info, 0x0);				/* enable statdis */
	sc8950_set_vbatlow_otg(info, 0x0);		/* minimum otg vbat 2.8V */
	sc8950_set_vrechg(info, 0x0);				/* VRECHG 0.1V */
	sc8950_set_en_term(info, 0x1);			/* Enable termination */
	sc8950_set_watchdog_cfg(info, 0x0);		/* disable WDT */
	sc8950_set_en_timer(info, 0x1);			/* Enable charge timer */
	sc8950_set_tchg(info, 0x3);				/* charge time 20 hours */
	sc8950_set_batfet_reset_en(info, 0x0);	/* disable batfet rst */
	sc8950_set_ovp(info, 0x3);				/* set ovp to 14.5v */
	sc8950_set_chg_thermal(info, 0x3);		/* set thermal to 120 */
	sc8950_set_boostv(info,0xe);				/* set vboost 5.3v*/
	sc8950_set_en_charge(info, 0x0);

	//sc8950_set_private(info);				/* Loop optimization */
	
	info->boost_mode = false;
	return 0;
}

static struct charger_ops sc8950_chg_ops = {
	/* cable plug in/out */
	.plug_in = sc8950_plug_in,
	.plug_out = sc8950_plug_out,

	/* enable/disable charger */
	.enable = sc8950_enable_charging,
	.is_enabled = sc8950_is_charging_enabled,

	/* get/set charging current*/
	.get_charging_current = sc8950_get_ichg,
	.set_charging_current = sc8950_set_ichg,
	.get_min_charging_current = sc8950_get_min_ichg,

	/* set cv */
	.get_constant_voltage = sc8950_get_cv,
	.set_constant_voltage = sc8950_set_cv,

	/* set input_current */
	.get_input_current = sc8950_get_aicr,
	.set_input_current = sc8950_set_aicr,
	.get_aicr_state = sc8950_get_aicr_state,
	.get_min_input_current = sc8950_get_min_aicr,

	/* set termination current */
	.get_eoc_current = sc8950_get_ieoc,
	.set_eoc_current = sc8950_set_ieoc,

	/* kick wdt */
	.kick_wdt = sc8950_kick_wdt,

	.set_mivr = sc8950_set_mivr,
	.get_mivr = sc8950_get_mivr,
	.get_mivr_state = sc8950_get_mivr_state,

	/* enable/disable powerpath */
	.enable_powerpath = sc8950_enable_powerpath,
	.enable_hz = sc8950_en_hz,
	.is_powerpath_enabled = sc8950_is_powerpath_enabled,

	/* enable/disable charging safety timer */
	.enable_safety_timer = sc8950_enable_safety_timer,
	.is_safety_timer_enabled = sc8950_is_safety_timer_enabled,

	/* enable term */
	.enable_termination = sc8950_enable_te,

	/* OTG */
	.enable_otg = sc8950_enable_otg,
	.set_boost_current_limit = sc8950_set_boost_current_limit,
	.set_boost_voltage = sc8950_set_boost_voltage_limit,

	/* charger type detection */
	.enable_chg_type_det = sc8950_enable_chg_type_det,
	.get_port_stat = sc8950_get_port_stat,
	.get_vbus_gd = sc8950_get_vbus_gd_stat,

	.is_charging_done = sc8950_is_charging_done,

	.dump_registers = sc8950_dump_registers,

	.set_dp = sc8950_set_dp_function,
	.set_dm = sc8950_set_dm_function,

	.set_ircmp = sc8950_set_ircmp,
	.set_ivcmp = sc8950_set_ivcmp,

	.get_shipmode_status = sc8950_get_shipmode_status,
	.enable_shipmode = sc8950_set_shipmode,
};

static int sc8950_init_chg(struct sc8950_info *info)
{
	info->chg_dev = charger_device_register(info->chg_name,
			info->dev, info, &sc8950_chg_ops, &info->chg_props);
	if (!info->chg_dev->dev.class || IS_ERR_OR_NULL(info->chg_dev))
		return -EPROBE_DEFER;

	return 0;
}

static int sc8950_eoc_irq_handler(struct sc8950_info *info)
{
	bool is_done = false;

	sc8950_is_charging_done(info->chg_dev, &is_done);
	if(is_done) {
		dev_err(info->dev, "%s happen!\n", __func__);
		charger_dev_notify(info->chg_dev, CHARGER_DEV_NOTIFY_EOC);
	}

	return 0;
}

static int sc8950_vg_irq_handler(struct sc8950_info *info)
{
	unsigned char stat = 0;

	sc8950_get_vg_stat(info, &stat);
	pr_info("%s vg_stat:%d\n", __func__, stat);

	return 0;
}

static int sc8950_fault_irq_handler(struct sc8950_info *info)
{
	unsigned char charg_fault = 0, tmp = 0;
	struct chgdev_notify *noti = &(info->chg_dev->noti);
	unsigned char is_otg_en = 0;

	sc8950_check_fault_state(info, &charg_fault);

	if (charg_fault & SC8950_WATCHDOG_FAULT_MASK) {
		dev_err(info->dev, "%s Watchdog timer expiration!reg[c]=0x%x\n", __func__, charg_fault);
	}

	if (charg_fault & SC8950_BOOST_FAULT_MASK) {
		sc8950_get_en_otg(info, &is_otg_en);
		if (is_otg_en == 0) {
			dev_err(info->dev, "%s vbus ovp!reg[c]=0x%x\n", __func__, charg_fault);
			info->vbus_stat_pre = true;
			noti->vbusov_stat = SC8950_BOOST_FAULT_VBUS_OVP;
			charger_dev_notify(info->chg_dev, CHARGER_DEV_NOTIFY_VBUS_OVP);
			charger_dev_notify(info->chg_dev, CHARGER_DEV_NOTIFY_OTG_FAULT);
		} else {
			dev_err(info->dev, "%s vbus ocp!reg[c]=0x%x\n", __func__, charg_fault);
		}
	} else if (info->vbus_stat_pre == true) {
		info->vbus_stat_pre = false;
		noti->vbusov_stat = SC8950_BOOST_FAULT_NORMAL;
		charger_dev_notify(info->chg_dev, CHARGER_DEV_NOTIFY_VBUS_OVP);
	}

	if (charg_fault & SC8950_CHRG_FAULT_MASK) {
		tmp = ((charg_fault & SC8950_CHRG_FAULT_MASK) >> SC8950_CHRG_FAULT_SHIFT);
		if (tmp == SC8950_CHRG_FAULT_INPUT_FAULT) {
			dev_err(info->dev, "%s vac ovp!reg[c]=0x%x\n", __func__, charg_fault);
			info->vbus_stat_pre = true;
			noti->vbusov_stat = SC8950_BOOST_FAULT_VBUS_OVP;
			charger_dev_notify(info->chg_dev, CHARGER_DEV_NOTIFY_VBUS_OVP);
		} else if (tmp == SC8950_CHRG_FAULT_THERMAL_SHUTDOWN) {
			dev_err(info->dev, "%s thermal shutdown!reg[c]=0x%x\n", __func__, charg_fault);
		} else if (tmp == SC8950_CHRG_FAULT_SAFETY_TIMER_EXPIRATION) {
			dev_err(info->dev, "%s safety timer expiration!reg[c]=0x%x\n", __func__, charg_fault);
		}
	} else {
		tmp = ((charg_fault & SC8950_CHRG_FAULT_MASK) >> SC8950_CHRG_FAULT_SHIFT);
		if (tmp == SC8950_CHRG_FAULT_NORMAL && info->vbus_stat_pre) {
			info->vbus_stat_pre = false;
			noti->vbusov_stat = SC8950_BOOST_FAULT_NORMAL;
			charger_dev_notify(info->chg_dev, CHARGER_DEV_NOTIFY_VBUS_OVP);
		}
	}

	if (charg_fault & SC8950_BAT_FAULT_MASK) {
		dev_err(info->dev, "%s bat ovp!reg[c]=0x%x\n", __func__, charg_fault);
	}

	if (charg_fault & SC8950_NTC_FAULT_MASK) {
		tmp = ((charg_fault & SC8950_NTC_FAULT_MASK) >> SC8950_NTC_FAULT_SHIFT);
		if (tmp == SC8950_NTC_FAULT_WARM) {
			dev_err(info->dev, "%s ntc warm!reg[c]=0x%x\n", __func__, charg_fault);
		} else if (tmp == SC8950_NTC_FAULT_COOL) {
			dev_err(info->dev, "%s ntc cool !reg[c]=0x%x\n", __func__, charg_fault);
		} else if (tmp == SC8950_NTC_FAULT_COLD) {
			dev_err(info->dev, "%s ntc cold !reg[c]=0x%x\n", __func__, charg_fault);
		} else if (tmp == SC8950_NTC_FAULT_HOT) {
			dev_err(info->dev, "%s ntc hot !reg[c]=0x%x\n", __func__, charg_fault);
		}
	}

	return 0;
}

static const struct irq_mapping_tbl sc8950_irq_mapping_tbl[] = {
	SC8950_IRQ_MAPPING(vg),
	SC8950_IRQ_MAPPING(eoc),
	SC8950_IRQ_MAPPING(fault),
};

void sc8950_work_handler(struct work_struct *work)
{
	int i = 0;
	struct sc8950_info *info = container_of(work, struct sc8950_info, sc8950_work);

	disable_irq(info->irq);

	dev_info(info->dev, "%s\n", __func__);
	
	for (i = 0; i < ARRAY_SIZE(sc8950_irq_mapping_tbl); i++){
		sc8950_irq_mapping_tbl[i].hdlr(info);
	}

	enable_irq(info->irq);
}

static irqreturn_t sc8950_irq_handler(int irq, void *data)
{
        struct sc8950_info *info = (struct sc8950_info *)data;

        dev_info(info->dev, "%s\n", __func__);
        queue_work(info->sc8950_wq, &info->sc8950_work);

        return 0;
}

static int sc8950_register_interrupt(struct sc8950_info *info)
{
	int ret = 0;

        if(info->intr_gpio != U32_MAX){
		dev_info(info->dev, "%s\n", __func__);

                ret = devm_gpio_request_one(info->dev, info->intr_gpio, GPIOF_DIR_IN,
                                devm_kasprintf(info->dev, GFP_KERNEL,
                                "sc8950_intr_gpio.%s", dev_name(info->dev)));
                if (ret < 0) {
                        dev_notice(info->dev, "%s gpio request fail(%d)\n",
                                              __func__, ret);
                        return ret;
                }
                info->irq = gpio_to_irq(info->intr_gpio);
                if (info->irq < 0) {
                        dev_notice(info->dev, "%s gpio2irq fail(%d)\n",
                                              __func__, info->irq);
                        return info->irq;
                }
                dev_info(info->dev, "%s irq = %d\n", __func__, info->irq);

                // Request threaded IRQ
                ret = devm_request_threaded_irq(info->dev, info->irq, NULL,
                                                sc8950_irq_handler,
                                                IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                                                devm_kasprintf(info->dev, GFP_KERNEL,
                                                "sc8950_irq.%s", dev_name(info->dev)),
                                                info);
                if (ret < 0) {
			dev_notice(info->dev, "%s request threaded irq fail(%d)\n",
                                              __func__, ret);
                        return ret;
                }
        }else{
                pr_err("%s failed!no intr_gpio!\n", __func__);
        }

        return ret;
}
/*regulator otg ops*/
int __maybe_unused sc8950_enable_regulator_otg(struct regulator_dev *rdev)
{
	struct sc8950_info *info = rdev_get_drvdata(rdev);

	dev_info(info->dev, "%s\n", __func__);
	/* otg current to 1.2A */
	return sc8950_enable_otg(info->chg_dev, true);
}

int  __maybe_unused sc8950_disable_regulator_otg(struct regulator_dev *rdev)
{
	struct sc8950_info *info = rdev_get_drvdata(rdev);

	dev_info(info->dev, "%s\n", __func__);

	return sc8950_enable_otg(info->chg_dev, false);
}

static int sc8950_boost_set_voltage_sel(struct regulator_dev *rdev,
					unsigned int sel)
{
	unsigned char regval=0;
	struct sc8950_info *info = rdev_get_drvdata(rdev);

	regval=sc8950_vboost_toreg(sel);
	dev_info(info->dev, "%s: sel=%d regval=%d\n",__func__,sel,regval);
	return sc8950_set_boostv(info,regval);
}

static int sc8950_boost_get_voltage_sel(struct regulator_dev *rdev)
{
	struct sc8950_info *info = rdev_get_drvdata(rdev);
	unsigned char val=0;

	sc8950_get_boostv(info,&val);
	dev_info(info->dev, "%s: val=%d\n",__func__,val);
	return val;
}

static int sc8950_boost_set_current_limit(struct regulator_dev *rdev,
						int min_uA, int max_uA)
{
	struct sc8950_info *info = rdev_get_drvdata(rdev);
	int i;

	for (i = 0; i < ARRAY_SIZE(sc8950_otg_oc_threshold); i++) {
		if (min_uA <= sc8950_otg_oc_threshold[i])
			break;
	}
	if (i == ARRAY_SIZE(sc8950_otg_oc_threshold) ||
		sc8950_otg_oc_threshold[i] > max_uA) {
		dev_notice(info->dev,"%s: out of current range\n", __func__);
		return -EINVAL;
	}
	dev_info(info->dev, "%s: select otg_oc = %d\n",__func__, sc8950_otg_oc_threshold[i]);
	return sc8950_set_iboost(info,i);
}

static int sc8950_boost_get_current_limit(struct regulator_dev *rdev)
{
	int ret = 0;
	struct sc8950_info *info = rdev_get_drvdata(rdev);
	unsigned char regval = 0;

	ret = sc8950_get_iboost(info,&regval);
	if (ret < 0)
		return ret;

	if (regval >= ARRAY_SIZE(sc8950_otg_oc_threshold))
		return -EINVAL;

	dev_info(info->dev, "%s: regval=%d get otg_oc = %d\n",__func__,regval,sc8950_otg_oc_threshold[regval]);
	return sc8950_otg_oc_threshold[regval];
}

static ssize_t sc8950_show_registers(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct sc8950_info *info = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sc8950");
	for (addr = 0x0; addr <= 0x14; addr++) {
		ret = sc8950_read_byte(info, addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx,
					"Reg[%.2X] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t sc8950_store_register(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct sc8950_info *info = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2) {
		sc8950_write_byte(info, (unsigned char)reg, (unsigned char)val);
	}

	return count;
}

static DEVICE_ATTR(sc8950_registers_debug, 0664, sc8950_show_registers, sc8950_store_register);

static int __maybe_unused sc8950_otg_is_enabled(struct regulator_dev *rdev)
{
	struct sc8950_info *info = rdev_get_drvdata(rdev);
	dev_info(info->dev, "%s  is_en:%d\n", __func__, info->boost_mode);
	return info->boost_mode;
}

static const struct regulator_ops sc8950_chg_otg_ops = {
	.list_voltage = regulator_list_voltage_linear,
/*
	.enable = sc8950_enable_regulator_otg,
	.disable = sc8950_disable_regulator_otg,
	.is_enabled = sc8950_otg_is_enabled,
*/
	.set_voltage_sel = sc8950_boost_set_voltage_sel,
	.get_voltage_sel = sc8950_boost_get_voltage_sel,
	.set_current_limit = sc8950_boost_set_current_limit,
	.get_current_limit = sc8950_boost_get_current_limit,
};

static const struct regulator_desc sc8950_otg_rdesc = {
	.of_match = "usb-otg-vbus",
	.name = "usb-otg-vbus",
	.ops = &sc8950_chg_otg_ops,
	.owner = THIS_MODULE,
	.type = REGULATOR_VOLTAGE,
	.min_uV = 3900000,
	.uV_step = 100000, /* step  100mV */
	.n_voltages = 15, /* 3900mV to 5400mV */
	.linear_min_sel = 0,
};

static const struct regulator_init_data sc8950_vbus_init_data = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS|REGULATOR_CHANGE_CURRENT,
		.min_uA = 500000,
		.max_uA = 2450000,
	},
};

static int sc8950_init_regulator(struct sc8950_info *info)
{
	struct regulator_config config = { };
	int ret=0;
	dev_info(info->dev, "%s\n", __func__);

	config.dev = info->dev;
	config.driver_data = info;
	config.init_data = &sc8950_vbus_init_data;
	info->otg_rdev = devm_regulator_register(info->dev, &sc8950_otg_rdesc,&config);

	if (IS_ERR(info->otg_rdev)) {
		ret = PTR_ERR(info->otg_rdev);
		dev_info(info->dev, "%s fail (%d)!\n", __func__,ret);
		return ret;
	}
	dev_info(info->dev, "%s succesfully!\n", __func__);
	return 0;
}

static int sc8950_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct sc8950_info *info = NULL;
	int ret = 0;
	
	dev_info(&client->dev, "%s\n", __func__);

	info = devm_kzalloc(&client->dev, sizeof(struct sc8950_info), GFP_KERNEL);
	if (!info) {
		dev_info(&client->dev, "%s alloc sc8950_chip failed!\n", __func__);
		return -ENOMEM;
	}

	info->client = client;
	info->dev = &client->dev;
	i2c_set_clientdata(client, info);

	mutex_init(&info->sc8950_i2c_access);

	if(sc8950_parse_dt(info, &client->dev) < 0){
		dev_err(&client->dev, "%s parse dt failed!\n", __func__);
		goto err_parse_dt;
	}

	if(!sc8950_check_devinfo(info)) {
		dev_err(&client->dev, "%s sc8950 ic is not exist!\n", __func__);
		ret = -ENODEV;
		goto err_parse_dt;
	}

	sc8950_hw_init(info);

	/* Register charger device */
	ret = sc8950_init_chg(info);
	if (ret) {
		ret = -EPROBE_DEFER;
		dev_err(&client->dev, "%s: register device failed\n", __func__);
		goto err_register_chg_dev;
	}

	info->sc8950_wq = create_singlethread_workqueue(info->chg_name);
        if (!info->sc8950_wq) {
                dev_info(info->dev, "%s create_singlethread_workqueue failed!\n", __func__);
        }else{
                INIT_WORK(&info->sc8950_work, sc8950_work_handler);
        }

	ret = sc8950_register_interrupt(info);
	if (ret < 0) {
		dev_err(info->dev, "%s register irq fail(%d)\n", __func__, ret);
		goto err_register_irq;
	}

	ret = sc8950_init_regulator(info);
	if (ret < 0) {
		ret = PTR_ERR(info->otg_rdev);
		dev_info(info->dev, "%s regulator register fail\n", __func__);
		goto err_register_irq;
	}

	device_create_file(info->dev, &dev_attr_sc8950_registers_debug);

	__sc8950_dump_registers(info);

	dev_err(info->dev, "sc8950 probe successfully!\n");

	return 0;

err_register_irq:
	charger_device_unregister(info->chg_dev);
err_register_chg_dev:
err_parse_dt:
	mutex_destroy(&info->sc8950_i2c_access);
	devm_kfree(info->dev, info);
	return ret;
}

static void sc8950_remove(struct i2c_client *client)
{
	struct sc8950_info *info = i2c_get_clientdata(client);

	if(!info)
		return;

	charger_device_unregister(info->chg_dev);
	mutex_destroy(&info->sc8950_i2c_access);
	dev_info(info->dev, "%s end\n", __func__);
	return;
}

static void sc8950_shutdown(struct i2c_client *client)
{
	struct sc8950_info *info = i2c_get_clientdata(client);

	if(!info)
		return;

	sc8950_set_rst(info, 0x1);
	i2c_set_clientdata(client, NULL);
	dev_info(info->dev, "%s end\n", __func__);
	return;
}

static int sc8950_suspend(struct device *dev)
{
	return 0;
}

static int sc8950_resume(struct device *dev)
{
	return 0;
}
static SIMPLE_DEV_PM_OPS(sc8950_pm_ops, sc8950_suspend, sc8950_resume);

static const struct of_device_id sc8950_of_device_id[] = {
	{ .compatible = "southchip,sc8950", },
	{ },
};
MODULE_DEVICE_TABLE(of, sc8950_of_device_id);

static struct i2c_driver sc8950_i2c_driver = {
	.driver = {
		.name = "sc8950",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(sc8950_of_device_id),
		.pm = &sc8950_pm_ops,
	},
	.probe = sc8950_probe,
	.remove = sc8950_remove,
	.shutdown = sc8950_shutdown,
};
module_i2c_driver(sc8950_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SC8950 Charger Driver");

