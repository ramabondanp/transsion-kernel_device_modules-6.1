// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include "tran_mt5728.h"
#include "wireless_class.h"
#include "tc_misc_intf.h"
#include "tc_pe5.h"

#define DEVICE_NAME "MT5728"
#define DRIVER_FIRMWARE_VERSION "1.0.1"

#define REG_NONE_ACCESS 0
#define REG_RD_ACCESS	BIT(0)
#define REG_WR_ACCESS 	BIT(1)
#define REG_BIT_ACCESS	BIT(2)
#define MT5728_FW_NAME 	"mt5728_wl_fw.bin"

#define MT5728_TX_MIN_FREQ 120
#define MT5728_TX_PING_FREQ 145

static int mt5728_update_fw(struct wireless_charger *wl_chg,
	const struct firmware *fw, bool force);
static int mt5728_sram_write(struct mt5728_dev *chip,u32 addr, u8 *data,u32 len);

struct irq_map_desc {
	const char *name;
	int (*hdlr)(struct mt5728_dev *chip);
	u32 stat_mask;
};

struct write_info {
	Alig32 val;
	u32 addr;
};

#define MT5728_TX_IRQ_DESC(_name, _stat_s) \
	{.name = #_name, .stat_mask = _stat_s, \
	 .hdlr = mt5728_tx_##_name##_irq_handler}
	
#define MT5728_RX_IRQ_DESC(_name, _stat_s) \
	{.name = #_name, .stat_mask = _stat_s, \
	 .hdlr = mt5728_rx_##_name##_irq_handler}

enum REG_INDEX {
	CHIPID = 0,
	VOUT,
	INT_FLAG,
	INTCTLR,
	VOUTSET,
	/* VFC, */
	CMD,
	INDEX_MAX,
};

static int mt5728_read(struct mt5728_dev *chip, u32 reg, u8 *val)
{
	unsigned int value;
	int ret;

	if (!chip || !chip->regmap || !val)
		return -EINVAL;

	mutex_lock(&chip->i2c_lock);
	ret = regmap_read(chip->regmap, reg, &value);
	if (ret >= 0)
		*val = (u8)value;
	mutex_unlock(&chip->i2c_lock);

	return ret;
}

static int mt5728_write(struct mt5728_dev *chip, u32 reg, u8 val)
{
	int ret = 0;

	if (!chip || !chip->regmap)
		return -EINVAL;

	mutex_lock(&chip->i2c_lock);
	ret = regmap_write(chip->regmap, reg, val);
	if (ret < 0)
		dev_err(chip->dev, "MT5728 write error: %d\n", ret);
	mutex_unlock(&chip->i2c_lock);

	return ret;
}

static int mt5728_read_buffer(struct mt5728_dev *chip, u32 reg, u8 *buf, u32 size)
{
	int ret = 0;

	while (size--) {
		ret = chip->bus.read(chip, reg++, buf++);
		if (ret < 0) {
			dev_err(chip->dev, "MT5728 read buf error: %d\n", ret);
			goto out;
		}
	}
out:
	return ret;
}

static int mt5728_write_buffer(struct mt5728_dev *chip, u32 reg, u8 *buf, u32 size)
{
	int ret = 0;

	while (size--) {
		ret = chip->bus.write(chip, reg++, *buf++);
		if (ret < 0) {
			dev_err(chip->dev, "MT5728 write buf error: %d\n", ret);
			goto out;
		}
	}
out:
	return ret;
}

static void mt5728_set_work_freq(struct mt5728_dev *chip, int freq)
{
	Alig16 val = {0};

	val.value = (80000 / freq) - 2;
	mt5728_write_buffer(chip, 0x0050, val.ptr, 2);

	return;
}

static int mt5728_enter_sleep_mode(struct mt5728_dev *chip, bool en)
{
	pr_info("mt5728 enter %s mode\n", en ? "sleep" : "normal");
	gpiod_set_value(chip->sleep_gpio, en);
	return 0;
}

static int mt5728_soft_reset(struct mt5728_dev *chip) 
{
	Alig16 val = {0};
	int ret = 0;

	pr_info("%s\n", __func__);
	val.value = MT5728_KEY;
	ret = mt5728_write_buffer(chip, MT5728_SYS_KEY_REG, val.ptr, 1);
	if (ret)
		return ret;

	val.ptr[0] = MT5728_M0_RESET;
	ret = mt5728_write_buffer(chip, MT5728_M0_CTRL_REG, val.ptr, 1);
	if (ret)
		return ret;
	msleep(20);

	return ret;
}

static int mt5728_get_tdie(struct mt5728_dev *chip) 
{
	Alig16 tdie = {0, };
	int ret = 0;

	ret = mt5728_read_buffer(chip, REG_RX_TEMP, tdie.ptr, 2);
	if (ret < 0) {
		pr_err("%s: chip may offline!", __func__);
		return 0;
	}
	pr_info("%s: tdie = %d\n", __func__, tdie.value);
	return tdie.value;
}

static const struct regmap_config mt5728_regmap_config = {
	.reg_bits	 = 16,
	.val_bits	 = 8,
};

static const struct regmap_config mt5728_regmap32_config = {
	.reg_bits	 = 32,
	.val_bits	 = 8,
};

static int __maybe_unused mt5728_get_tx_vout(struct mt5728_dev *chip)
{
	Alig16 vout = {0};
	int ret = 0;

	ret = mt5728_read_buffer(chip, REG_TX_VOUT, vout.ptr, 2);
	if (ret) {
		return ret;
	}

	return vout.value;
}

static int mt5728_get_vout(struct mt5728_dev *chip) 
{
	Alig16 vout = {0};
	int ret = 0;

	ret = mt5728_read_buffer(chip, REG_VOUT, vout.ptr, 2);
	if (ret < 0) {
		pr_err("%s: chip may offline!", __func__);
		return ret;
	}
	return vout.value;
}

static int mt5728_get_iout(struct mt5728_dev *chip) 
{
	Alig16 iout = {0};
	int ret = 0;

	ret = mt5728_read_buffer(chip, REG_IOUT, iout.ptr, 2);
	if (ret < 0) {
		pr_err("%s: chip may offline!", __func__);
		return ret;
	}
	return iout.value;
}

static int compare_cep_val_change(signed char *cep , int len)
{
	int i = 0, j = 0;

	for (i = 0; i < (len - 1); i++) {
		if (cep[i] <= cep[i + 1]) {
			j++;
		}
	}

	if (j == (len - 1)) {
		return -EINVAL;
	}

	return 0;
}

static void mt5728_check_cep_val(struct mt5728_dev *chip)
{
	int count = 30, rc = 0, i = 0;
	signed char val = 0;
	signed char cep_tmp[6] = {0};

	do {
		msleep(140);
		rc = mt5728_read_buffer(chip, REG_RX_CEP, &val, sizeof(val));
		if (rc < 0 || !chip->pg) {
			return;
		}

		cep_tmp[i++] = val;
		pr_info("--->ce value:%d\n", val);

		if (val < 2) {
			return;
		}

		if (i == ARRAY_SIZE(cep_tmp)) {
			i = 0;
			if (compare_cep_val_change(cep_tmp, ARRAY_SIZE(cep_tmp))) {
				pr_info("ce val is not change\n");
				return;
			}
		}

	} while(count--);
}

static int mt5728_set_vout_val(struct mt5728_dev *chip, int volt, bool shutdown)
{
	Alig16 val = {0};
	int ret = 0;

	pr_info("%s volt:%d\n", __func__, volt);
	val.value = (u16)volt;
	ret = mt5728_write_buffer(chip, REG_VOUTSET, val.ptr, 2);
	if (ret < 0) {
		pr_err("set volt fail\n");
		return ret;
	}

	val.value = VOUT_CHANGE;
	ret=mt5728_write_buffer(chip, REG_CMD, val.ptr, 2);
	if(ret<0){
		pr_err("%s cmd fail %d\n", __func__,ret);
		return ret;
	}

	if (!shutdown)
		mt5728_check_cep_val(chip);

	return 0;
}

static int mt5728_get_adc(struct wireless_charger *wl_chg, enum adc_channel chan)
{
	struct mt5728_dev *chip = wl_chg->private_d;

	switch (chan) {
	case PE50_ADCCHAN_TCHG:
		mt5728_get_tdie(chip);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static short mt5728_get_tx_ce_value(struct wireless_charger *wl_chg)
{
	signed char val = 0;
	int ret = 0;
	struct mt5728_dev *chip = (struct mt5728_dev *)wl_chg->private_d;

	ret = mt5728_read_buffer(chip, REG_RX_CEP, &val, sizeof(val));
	if (ret < 0) {
		pr_err("mt5708 read 0x%x failed, ret = %d\n", REG_RX_CEP, ret);
		return -EIO;
	}

	pr_info("--->cep:%d\n", val);
	return val;
}

static int mt5728_enter_lpm_mode(struct mt5728_dev *chip,bool en)
{
	Alig16 val;
	if(en){
		val.value = 0x5728;
		mt5728_write_buffer(chip, REG_KEY, val.ptr, 2);
		val.value = LOW_POWER;
		mt5728_write_buffer(chip, REG_CMD, val.ptr, 2);
	}

	pr_info("%s \n",__func__);
	return 0;
}

static irqreturn_t mt5728_pg_irq_handle(int irq, void *data)
{
	struct mt5728_dev *chip = (struct mt5728_dev *)data;

	chip->pg = !gpiod_get_value(chip->pg_gpio);
	pr_info("%s:%s\n", __func__, chip->pg ? "pg on" : "pg off");

	wireless_ic_set_state(chip->wl_chg, WIRELESS_PG_CHANGE);

	return IRQ_HANDLED;
}

static irqreturn_t mt5728_wired_irq_handle(int irq, void *data)
{
	struct mt5728_dev *chip = (struct mt5728_dev *)data;

	chip->wired_state = !gpiod_get_value(chip->wired_gpio);
	pr_info("%s:%s\n", __func__,chip->wired_state ? "wired plugin" : "wired pulgout");

	wireless_ic_set_state(chip->wl_chg, WIRELESS_WIRED_CHANGE);

	return IRQ_HANDLED;
}

static int mt5728_parse_dt(struct mt5728_dev *chip)
{
	struct device_node *np = chip->dev->of_node;
	int ret=0;

	chip->tx_support_epp = of_property_read_bool(np, "tx_mode_support_epp");
//	chip->tx_support_epp = true;

	ret = of_property_read_u32(np, "min_freq", &chip->min_freq);
	if (ret < 0){
		pr_err("%s no min_freq(%d)\n", __func__, ret);
		chip->min_freq = MT5728_TX_MIN_FREQ;
	}

	chip->pg_gpio = devm_gpiod_get(chip->dev, "powergood", GPIOD_IN);
	if (IS_ERR(chip->pg_gpio)) {
		pr_err("%s:gpio_is_valid error, pg_gpio\n",__func__);
		return PTR_ERR(chip->pg_gpio);
	}

	chip->irq_gpio = devm_gpiod_get(chip->dev, "eint_wpc", GPIOD_IN);
	if (IS_ERR(chip->irq_gpio)){
		pr_err("%s get irq_gpio failed\n", __func__);
		return PTR_ERR(chip->irq_gpio);
	}

	chip->wired_gpio = devm_gpiod_get(chip->dev, "wired", GPIOD_IN);
	if (IS_ERR(chip->wired_gpio)){
		pr_err("%s get wired gpio failed\n", __func__);
		return PTR_ERR(chip->wired_gpio);
	}

	/* chip->wired_path_gpio = devm_gpiod_get(chip->dev, "wired_path", GPIOD_OUT_LOW); */
	/* if (IS_ERR(chip->wired_path_gpio)){ */
	/*         pr_err("%s get dv ctrl gpio failed\n", __func__); */
	/*         return PTR_ERR(chip->wired_path_gpio); */
	/* } */
	chip->wired_path_gpio = devm_gpiod_get(chip->dev, "dv_vctrl", GPIOD_OUT_LOW);
	if (IS_ERR(chip->wired_path_gpio)){
		pr_err("%s get dv ctrl gpio failed\n", __func__);
		//return PTR_ERR(chip->wired_path_gpio);
	}

	chip->sleep_gpio = devm_gpiod_get(chip->dev, "sleep", GPIOD_OUT_LOW);
	if (IS_ERR(chip->sleep_gpio)){
		pr_err("%s get sleep gpio failed\n", __func__);
		return PTR_ERR(chip->sleep_gpio);
	}

	chip->tx_mode_gpio = devm_gpiod_get(chip->dev, "tx_mode", GPIOD_OUT_LOW);
	if (IS_ERR(chip->tx_mode_gpio)){
		pr_err("%s get tx mode gpio failed\n", __func__);
	}

	chip->ovp_ctrl_gpio = devm_gpiod_get(chip->dev, "ovp_ctrl", GPIOD_OUT_HIGH);
	if (IS_ERR(chip->ovp_ctrl_gpio)){
		pr_err("%s get ovp_ctrl gpio failed\n", __func__);
	}

	return 0;
}

static void mt5728_clear_irq(struct mt5728_dev *chip)
{
	Alig32 val = {0};
	int ret;

	ret = mt5728_read_buffer(chip, REG_INTFLAG, val.ptr, 4);
	if (ret < 0) {
		dev_info(chip->dev, "mt5728 read REG_INTFLAG failed, ret = %d\n", ret);
		return;
	}

	if (val.value == 0) {
		pr_info("[%s] No INT here\n", __func__);
		return;
	}

	mt5728_write_buffer(chip, REG_INTCLR, val.ptr, 4);
	pr_info("[%s] write REG_INTCLR : 0x%08x\n", __func__, val.value);
}

static int mt5728_set_path_setup(struct wireless_charger *wlc, bool en)
{
	struct mt5728_dev *chip = wlc->private_d;

	pr_info("%s, en:%d\n", __func__, en);
	if (IS_ERR(chip->wired_path_gpio)){
		pr_err("%s get dv ctrl gpio failed\n", __func__);
		return 0;
	}
	gpiod_set_value(chip->wired_path_gpio, en);
	msleep(20);
	return 0;
}

static int mt5728_get_wired_state(struct wireless_charger *wlc, bool *status)
{
	struct mt5728_dev *chip = wlc->private_d;

	if (IS_ERR_OR_NULL(chip->wired_gpio))
		return -EIO;

	*status = !gpiod_get_value(chip->wired_gpio);
	return 0;
}

static int mt5728_get_pg_status(struct wireless_charger *wlc, bool *value)
{
	struct mt5728_dev *chip = (struct mt5728_dev *)wlc->private_d;

	*value = !gpiod_get_value(chip->pg_gpio);

	return 0;
}

static int mt5728_get_wl_vbus(struct wireless_charger *wlc, int *value)
{	
	struct mt5728_dev *chip = (struct mt5728_dev *)wlc->private_d;

	*value = mt5728_get_vout(chip);
	return 0;
}

static int mt5728_get_wl_ibus(struct wireless_charger *wlc, int *value)
{
	struct mt5728_dev *chip = (struct mt5728_dev *)wlc->private_d;

	*value = mt5728_get_iout(chip);

	return 0;
}

static int mt5728_plug_out(struct wireless_charger *wlc)
{
	struct mt5728_dev *chip = (struct mt5728_dev *)wlc->private_d;

	memset(chip->hw_info, 0, sizeof(*chip->hw_info));
	chip->site = false;
	chip->ldo_on = false;
	chip->protocol = 0;
	pr_info("%s\n", __func__);

	return 0;
}

static int mt5728_plug_in(struct wireless_charger *wlc)
{	
	struct mt5728_dev *chip = (struct mt5728_dev *)wlc->private_d;

	memset(chip->hw_info, 0, sizeof(*chip->hw_info));

	return 0;
}

static int mt5728_set_sleep_mode(struct wireless_charger *wlc, bool en)
{
	struct mt5728_dev *chip = (struct mt5728_dev *)wlc->private_d;

	pr_info("%s: en = %d\n", __func__, en);
	gpiod_set_value(chip->sleep_gpio, en);
	return 0;
}

static int mt5728_set_lpm_mode(struct wireless_charger *wlc, bool en)
{
	struct mt5728_dev *chip = (struct mt5728_dev *)wlc->private_d;

	pr_info("%s : en=%d\n",__func__,en);
	if(en)
		mt5728_enter_lpm_mode(chip,true);
	else
		mt5728_enter_lpm_mode(chip,false);
	return 0;
}

static int mt5728_get_wl_hw_info(struct wireless_charger *wc,
	struct wls_hw_info *hw_info)
{
	struct mt5728_dev *chip = (struct mt5728_dev *)wc->private_d;

	memcpy(hw_info, chip->hw_info, sizeof(*hw_info));
	return 0;
}

static int mt5728_set_wl_voltage(struct wireless_charger *wl_chg, int volt, bool shutdown)
{	
	struct mt5728_dev *chip = (struct mt5728_dev *)wl_chg->private_d;

	if (!chip->ldo_on) {
		pr_err("wls ldo off, set wls voltage failed");
		return -EINVAL;
	}

	return mt5728_set_vout_val(chip, volt, shutdown);
}

static int mt5728_dump_register(struct wireless_charger *wl_chg)
{
	return 0;
}

static void mt5728_set_tx_min_freq(struct mt5728_dev *chip, int freq)
{
	Alig16 val = {0};

	val.value = (80000/freq) - 2;
	mt5728_write_buffer(chip, REG_TX_MIN_PERIOD, val.ptr, 2);

	return;
}

static int mt5728_set_tx_mode(struct wireless_charger *wl_chg, bool en, struct tx_config *txc)
{
	int ret = 0;
	struct mt5728_dev *chip = wl_chg->private_d;

	pr_info("%s en:%d, txp:%d, protocol:%d\n", __func__, en, txc->power, txc->protocol);

	mt5728_write(chip, REG_TX_MODE_PROTOCOL, !!txc->protocol);

	if (txc->protocol) {
		mt5728_write(chip, REG_TX_MODE_POWER, txc->power);
	}

	if (en) {
		if (!chip->tx_support_epp) {
		ret = mt5728_soft_reset(chip);
		if (ret) {
			pr_err("mt5728 soft rest fail\n");
			}
		}
		mt5728_set_tx_min_freq(chip, chip->min_freq);

		//ping 125k
		mt5728_set_work_freq(chip, MT5728_TX_PING_FREQ);
	}

	return 0;
}

static int mt5728_get_pot_power(struct wireless_charger *wl_chg, int *val)
{	
	struct mt5728_dev *chip = wl_chg->private_d;

	if (!chip->ldo_on) {
		return -EINVAL;
	}

	*val = chip->pot_power;
	return 0;
}

static int mt5728_tx_epp_fw_update(struct mt5728_dev *chip)
{
	int ret = 0;
	const struct firmware *fw = NULL;
	const u8 *data = NULL;
	size_t len = 0;

	pr_info("%s start\n", __func__);

	ret = request_firmware(&fw, "mt5728_tx_epp.bin", &chip->client->dev);
	if (ret) {
		pr_err("%s, request mt5728 tx epp.bin fail\n",__func__);
		return ret;
	}

	data = fw->data;
	len = fw->size;
	ret = mt5728_sram_write(chip, REG_TX_EPP_RAM, (u8 *)data, len);
	if (ret) {
		pr_err("%s, update fw to ram fail\n", __func__);
	}

	release_firmware(fw);

	return ret;
}
static int mt5728_set_ovp_ctrl(struct wireless_charger *wl_chg, bool en)
{
	struct mt5728_dev *chip = wl_chg->private_d;

	pr_info("%s, en:%d\n", __func__, en);

	if (IS_ERR_OR_NULL(chip->ovp_ctrl_gpio))
		return -EIO;

	gpiod_set_value(chip->ovp_ctrl_gpio, !en);

	return 0;
}


static int mt5728_get_fw_version(struct wireless_charger *wl_chg, u32 *ver)
{
	int ret = 0;
	Alig16 val = {0};
	struct mt5728_dev *chip = wl_chg->private_d;

	ret = mt5728_read_buffer(chip, REG_FW_VER, val.ptr, sizeof(val));
	if (ret) {
		pr_err("read reg[%d] fail, ret:%d\n", REG_FW_VER, ret);
		return ret;
	}

	*ver = be16_to_cpu(val.value);
	return 0;
}

static u8 mt5728_get_wls_protocol(struct wireless_charger *wl_chg)
{
	struct mt5728_dev *chip = wl_chg->private_d;

	return chip->protocol;
}

static int mt5728_get_bridge_mode(struct wireless_charger *wl_chg)
{
	return FULL_BRIDGE;
}

static int mt5728_tx_mode_prepare(struct wireless_charger *wl_chg, bool en)
{
	u8 tx_cmd = 0x1;
	struct mt5728_dev *chip = wl_chg->private_d;

	pr_info("%s en : %d\n", __func__, en);

	if (!IS_ERR_OR_NULL(chip->tx_mode_gpio)) {
		gpiod_set_value(chip->tx_mode_gpio, en);
	}

	if (en && chip->tx_support_epp) {
		msleep(300);
		mt5728_tx_epp_fw_update(chip);
		mt5728_write_buffer(chip, REG_ENTER_TX_MODE, &tx_cmd, sizeof(tx_cmd));
	}

	return 0;
}

static int mt5728_set_drop_vol(struct wireless_charger *wl_chg)
{
	Alig32 val = {0};
	struct mt5728_dev *chip = wl_chg->private_d;

	pr_info("%s\n", __func__);

	mt5728_set_vout_val(chip, 7500, false);

	val.value = 1000;;
	return mt5728_write_buffer(chip, REG_RX_DROP_VOL_MAX, val.ptr, sizeof(val));
}

static int mt5728_wirte_reg_data(struct wireless_charger *wl_chg, int addr, u8 data)
{
	struct mt5728_dev *chip = wl_chg->private_d;

	return mt5728_write_buffer(chip, addr, &data, sizeof(data));
}

static int mt5728_read_reg_data(struct wireless_charger *wl_chg, int addr, u8 *data)
{
	u8 val = 0;
	struct mt5728_dev *chip = wl_chg->private_d;

	mt5728_read_buffer(chip, addr, &val, sizeof(val));
	pr_info("%s, %x, %x\n", __func__, addr, val);
	*data = val;

	return 0;
}

static struct wls_ops mt5728_chg_ops = {
	.get_wireless_pg 					= mt5728_get_pg_status,
	.get_wireless_vbus 					= mt5728_get_wl_vbus,
	.get_wireless_ibus 					= mt5728_get_wl_ibus,
	.set_wireless_plug_out 				= mt5728_plug_out,
	.set_wireless_plug_in 				= mt5728_plug_in,
	.set_wireless_sleep 				= mt5728_set_sleep_mode,
	.set_wireless_lpm_mode				= mt5728_set_lpm_mode,
	.get_wireless_hw_info 				= mt5728_get_wl_hw_info,
	.set_wireless_voltage           	= mt5728_set_wl_voltage,
	.dump_wireless_status 				= mt5728_dump_register,
	.set_wireless_tx_mode 				= mt5728_set_tx_mode,
	.get_wireless_power 				= mt5728_get_pot_power,
	.set_wireless_fw_update 			= mt5728_update_fw,
	.set_ovp_ctrl 				 		= mt5728_set_ovp_ctrl,
	.get_wireless_fw_version 			= mt5728_get_fw_version,
	.wired_path_setup 					= mt5728_set_path_setup,
	.get_wired_state 					= mt5728_get_wired_state,
	.get_wireless_adc					= mt5728_get_adc,
	.get_tx_ce_value 					= mt5728_get_tx_ce_value,
	.get_wls_protocol 					= mt5728_get_wls_protocol,
	.get_bridge_mode					= mt5728_get_bridge_mode,
	.tx_mode_prepare 					= mt5728_tx_mode_prepare,
	.set_drop_voltage					= mt5728_set_drop_vol,
	.wirte_reg_data 					= mt5728_wirte_reg_data,
	.read_reg_data 						= mt5728_read_reg_data,
};

static int mt5728_tx_power_trans_irq_handler(struct mt5728_dev *chip)
{
	pr_info("%s\n", __func__);
	wireless_ic_set_state(chip->wl_chg, WIRELESS_TX_DET_RX);
	return 0;
}

static int mt5728_tx_ept_irq_handler(struct mt5728_dev *chip)
{
	pr_info("%s\n", __func__);
	wireless_ic_set_state(chip->wl_chg, WIRELESS_TX_RMV_RX);
	return 0;
}

static int mt5728_tx_ac_valid_irq_handler(struct mt5728_dev *chip)
{
	pr_info("%s\n", __func__);
	wireless_ic_set_state(chip->wl_chg, WIRELESS_TX_AC_VALID);
	return 0;
}

static int mt5728_tx_ocp_ping_irq_handler(struct mt5728_dev *chip)
{
	pr_info("%s\n", __func__);
	return 0;
}

static int mt5728_tx_init_mode_irq_handler(struct mt5728_dev *chip)
{
	pr_info("%s\n", __func__);
	wireless_ic_set_state(chip->wl_chg, WIRELESS_TX_INIT_DONE);
	return 0;
}

static int mt5728_tx_ppp_irq_handler(struct mt5728_dev *chip)
{
	return 0;
}

static int mt5728_tx_fod_irq_handler(struct mt5728_dev *chip)
{
	pr_info("%s\n", __func__);
	wireless_ic_set_state(chip->wl_chg, WIRELESS_TX_MODE_CLOSE);
	return 0;
}

static const struct irq_map_desc mt5728_tx_irq_map_tbl[] = {

	MT5728_TX_IRQ_DESC(power_trans, INT_POWER_TRANS),
	MT5728_TX_IRQ_DESC(ept, INT_TX_EPT),
	MT5728_TX_IRQ_DESC(ac_valid, INT_TX_AC_VALID),
	MT5728_TX_IRQ_DESC(ocp_ping, INT_OCPFRX),
	MT5728_TX_IRQ_DESC(init_mode, INT_TX_MODE),
	MT5728_TX_IRQ_DESC(ppp, INT_RECIVER_PPP),
	MT5728_TX_IRQ_DESC(fod, INT_FODDE),
};

static int mt5728_fskrecv_fsk_det(struct mt5728_dev *chip, u16 value)
{

	int ret = 0;
	switch (value) {
	case CMD_EPP_NEGO_END:
		break;
	case CMD_SITE_OCCUR:
		chip->site = true;
		break;
	case CMD_SITE_CANCEL:
		chip->site = false;
		break;
	case CMD_EPP_OVER_LOAD:
		break;
	default:
		ret = -EINVAL;	
	}

	return ret;
}

static int mt5728_rx_ldo_on_irq_handler(struct mt5728_dev *chip)
{
	Alig16 val = {0};
	u8 protocol_mode = 0;
	int ret = 0;

	chip->ldo_on = true;

	ret = mt5728_read_buffer(chip, REG_FW_VER, val.ptr, sizeof(val));
	if (ret) {
		pr_err("read reg[%d] fail, ret:%d\n", REG_FW_VER, ret);
	}

	ret = mt5728_read_buffer(chip, REG_PROTOCOL_TYPE,
		&protocol_mode, sizeof(protocol_mode));

	pr_info("wls protocol mode : %x, %s, fw_ver:0x%x", protocol_mode,
		wls_protocol_mode_name(protocol_mode), be16_to_cpu(val.value));

	if (protocol_mode <= MPP_FORCE && protocol_mode >= MPP_RESTRICT) {
		wireless_ic_set_state(chip->wl_chg, WIRELESS_RX_MPP);
	}

	chip->protocol = protocol_mode;

	wireless_ic_set_state(chip->wl_chg, WIRELESS_LDO_ON);
	return ret;
}

static int mt5728_rx_fsk_recv_irq_handler(struct mt5728_dev *chip)
{
	int ret = 0;
	Alig16 val = {0,};

	dev_info(chip->dev, "%s\n", __func__);
	mt5728_read_buffer(chip, REG_BC, val.ptr, 2);
	
	ret = mt5728_fskrecv_fsk_det(chip, val.value);
	if (!ret)
		goto out;
out:
	return 0;
}

static int mt5728_rx_ldoshort_irq_handler(struct mt5728_dev *chip)
{
	wireless_ic_set_state(chip->wl_chg, WIRELESS_RX_HW_ERR);
	return 0;
}

static int mt5728_rx_ldoocp_irq_handler(struct mt5728_dev *chip)
{
	wireless_ic_set_state(chip->wl_chg, WIRELESS_RX_HW_ERR);
	return 0;
}

static int mt5728_rx_ldoovp_irq_handler(struct mt5728_dev *chip)
{
	//wireless_ic_set_state(chip->wl_chg, WIRELESS_RX_HW_ERR);
	return 0;
}

static int mt5728_rx_epp_int_irq_handler(struct mt5728_dev *chip)
{
	Alig16 val = {0,};

	mt5728_read_buffer(chip, REG_POT_POWER, val.ptr, 1);
	chip->pot_power = val.value / 2;
	pr_info("%s, epp power:%d\n", __func__, chip->pot_power);
	wireless_ic_set_state(chip->wl_chg, WIRELESS_RX_EPP_READY);

	return 0;
}

static int mt5728_rx_ppp_timeout_irq_handler(struct mt5728_dev *chip)
{
	pr_info("%s\n", __func__);
	return 0;
}

static int mt5728_rx_ppp_success_irq_handler(struct mt5728_dev *chip)
{
	pr_info("%s\n", __func__);
	return 0;
}

static const struct irq_map_desc mt5728_rx_irq_map_tbl[] = {

	MT5728_RX_IRQ_DESC(ldoocp, INT_RX_OCP),
	MT5728_RX_IRQ_DESC(ldoovp, INT_RX_OVP),
	MT5728_RX_IRQ_DESC(ldo_on, INT_LDO_ON),
	MT5728_RX_IRQ_DESC(fsk_recv, INT_FSK_RECV),
	MT5728_RX_IRQ_DESC(epp_int, INT_EPP),
	MT5728_RX_IRQ_DESC(ppp_timeout, INT_FSK_TIMEOUT),
	MT5728_RX_IRQ_DESC(ppp_success, INT_FSK_SUCCESS),
	MT5728_RX_IRQ_DESC(ldoshort, INT_LDO_SHORT),
};

static void mt5728_rx_interrupt_handler(struct mt5728_dev *chip, u32 rx_value)
{
	int i;
	const struct irq_map_desc *desc;

	for (i = 0; i < ARRAY_SIZE(mt5728_rx_irq_map_tbl); i++) {
		desc = &mt5728_rx_irq_map_tbl[i];
		if ((rx_value & desc->stat_mask) && desc->hdlr) {
			desc->hdlr(chip);
		}
	}
}

static void mt5728_tx_interrupt_handler(struct mt5728_dev *chip, u32 tx_value)
{
	int i;
	const struct irq_map_desc *desc;

	for (i = 0; i < ARRAY_SIZE(mt5728_tx_irq_map_tbl); i++) {
		desc = &mt5728_tx_irq_map_tbl[i];
		if ((tx_value & desc->stat_mask) && desc->hdlr) {
			desc->hdlr(chip);
		}
	}
}

static irqreturn_t mt5728_irq_handle(int irq, void * data)
{
	Alig32 val = {0}, scmd = {0};
	int ret = 0;
	u8 mode;
	struct mt5728_dev *chip = (struct mt5728_dev *)data;

	pm_stay_awake(chip->dev);
	ret = mt5728_read_buffer(chip, REG_INTFLAG, val.ptr, 4);
	if (ret < 0) {
		dev_err(chip->dev, "mt5728 read REG_INTFLAG failed, ret = %d\n", ret);
		goto out;
	}

	pr_info("%s: mt5728 irq status = 0x%X\n", __func__, val.value);

	if (val.value == 0) {
		goto out;
	}

	/* clear irq reg data */
	scmd.value = CLEAR_INT;
	mt5728_write_buffer(chip, REG_INTCLR, val.ptr, 4);
	mt5728_write_buffer(chip, REG_CMD, scmd.ptr, 2);

	ret = mt5728_read_buffer(chip, REG_SYSMODE, &mode, 1);
	if (ret < 0) {
		dev_err(chip->dev, "mt5728 read REG_SYSMODE failed, ret = %d\n", ret);
		goto out;
	}

	pr_info("mt5728 mode val= 0x%x, rx_mode:%d, tx_mode:%d\n",
		mode, mode&RXMODE, !!(mode&TXMODE));
	mode = mode & (RXMODE | TXMODE);
	switch (mode) {
	case RXMODE:
		mt5728_rx_interrupt_handler(chip, val.value);
		break;
	case TXMODE:
		mt5728_tx_interrupt_handler(chip, val.value);
		break;
	default:
		pr_err("mt5728 mode unknown\n");
		break;
	}

out:
	pm_relax(chip->dev);
	return IRQ_HANDLED;
}

static int mt5728_sram_write(struct mt5728_dev *chip,u32 addr, u8 *data,u32 len)
{
	u32 offset,length,size;
	int ret=0;

	offset = 0;
	length = 0;
	size = len;
	pr_info("[%s] Length to write:%d\n",__func__, len);
	while(size > 0) {
		if(size > SRAM_PAGE_SIZE) {
			length = SRAM_PAGE_SIZE;
		} else {
			length = size;
		}
		pr_info("[%s] Length of this write :%d\n",__func__,length);
		ret = mt5728_write_buffer(chip, addr + offset ,data+offset, length);
		if (ret < 0) {
			pr_err("%s LINE:%d\n",__func__,__LINE__);
			goto out;
		}
		size -= length;
		offset += length;
		usleep_range(2000, 3000);
	}
out:
	pr_info("%s --\n",__func__);
	return ret;
}

static int mt5728_run_pgm_fw(struct mt5728_dev *chip)
{
	Alig32 val={.value=0};
	int ret=0;
	pr_info("%s  ++\n",__func__);
	val.value  = MT5728_WDG_DISABLE;

	ret = mt5728_write_buffer(chip, MT5728_PMU_WDGEN_REG, val.ptr, 2);
	if (ret < 0) {
		pr_err("%s LINE:%d\n",__func__,__LINE__);
		goto out;
	}

	val.value  = MT5728_WDT_INTFALG;
	ret = mt5728_write_buffer(chip, MT5728_PMU_FLAG_REG, val.ptr, 2);
	if (ret < 0) {
		pr_err("%s LINE:%d\n",__func__,__LINE__);
		goto out;
	}

	val.value = MT5728_KEY;
	ret = mt5728_write_buffer(chip, MT5728_SYS_KEY_REG, val.ptr, 2);
	if (ret < 0) {
		pr_err("%s LINE:%d\n",__func__,__LINE__);
		goto out;
	}

	val.value = 0x08;
	ret = mt5728_write_buffer(chip, MT5728_CODE_REMAP_REG, val.ptr, 2);
	if (ret < 0) {
		pr_err("%s LINE:%d\n",__func__,__LINE__);
		goto out;
	}

	val.value = 0x0FFF;
	ret = mt5728_write_buffer(chip, MT5728_SRAM_REMAP_REG, val.ptr, 2);
	if (ret < 0) {
		pr_err("%s LINE:%d\n",__func__,__LINE__);
		goto out;
	}
	msleep(50);
	ret = mt5728_sram_write(chip, 0x1800,(u8 *)mt5728_pgm_bin, sizeof(mt5728_pgm_bin));
	if (ret < 0) {
		pr_err("%s LINE:%d\n",__func__,__LINE__);
		goto out;
	}
	msleep(50);
	val.value = MT5728_KEY;
	ret = mt5728_write_buffer(chip, MT5728_SYS_KEY_REG, val.ptr, 2);
	if (ret < 0) {
		pr_err("%s LINE:%d\n",__func__,__LINE__);
		goto out;
	}
	val.value = MT5728_M0_RESET;
	ret = mt5728_write_buffer(chip, MT5728_M0_CTRL_REG, val.ptr, 2);
	if (ret < 0) {
		pr_err("%s LINE:%d\n",__func__,__LINE__);
		goto out;
	}
	msleep(50);
out:
	pr_info("%s succeed!\n",__func__);
	return ret;
}

static int __mt5728_mtp_crc_verify(struct mt5728_dev *chip)
{
	int ret = 0;
	Alig32 sysmodeExt = {.value = 0};

	ret = mt5728_read_buffer(chip, REG_SYSMODE_EXT, sysmodeExt.ptr, 1); 
	if (ret < 0) {
		pr_info("[%s]read SysModeExt fail",__func__);
		return -EINVAL;
	}
	pr_info("[%s] sysmode ext : %x\n", __func__, sysmodeExt.ptr[0]); 
	ret = sysmodeExt.ptr[0] & SYSMODEEXT_FWCRC_OK;
	if (ret) {
		pr_info("[%s] mt5728_mtp_verify success",__func__); 
		return ret;
	}

	return 0;
}

static int mt5728_mtp_crc_step(struct mt5728_dev *chip, uint32_t crc_len, uint16_t crc_value)
{
	int i = 0;
	int ret = 0;
	struct write_info crc[] = {
		{{.value = MT5728_KEY}, MT5728_SYS_KEY_REG},
		{{.value = MT5728_WDG_DISABLE}, MT5728_PMU_WDGEN_REG},
		{{.value = crc_value}, REG_FWCRCVAL},
		{{.value = crc_len}, REG_FWLENGTH},
		{{.value = RX_FWCRCCHECK}, REG_CMD}
	};

	for (i = 0; i < ARRAY_SIZE(crc); i++) {
		ret = mt5728_write_buffer(chip, crc[i].addr, crc[i].val.ptr, 2);
		if (ret < 0) {
			pr_err("[%s] write reg[%x] fail", __func__, crc[i].addr);
			return ret;
		}
	}

	return 0;
}

static bool mt5728_mtp_crc_verify(struct mt5728_dev *chip, uint32_t len, uint16_t crc)
{
	int ret = 0;
	int retry = 5;

	ret = mt5728_mtp_crc_step(chip, len, crc);
	if (ret) {
		return false;
	}

	do {
		ret = __mt5728_mtp_crc_verify(chip);
		if (ret == SYSMODEEXT_FWCRC_OK)
			return true;
		else if (ret == SYSMODEEXT_FWCRC_ERR)
			return false;

		msleep(20);
	} while (retry--);

	pr_info("[%s] verify fail", __func__);

	return false;
}

static int mt5728_mtp_wirte_step(struct mt5728_dev *chip, Pgm_Type_t pgm)
{
	int i = 0;
	int ret = 0;
	struct write_info mtp[] = {
		{{.value = pgm.status}, PGM_STATUS_ADDR},
		{{.value = pgm.addr}, PGM_ADDR_ADDR},
		{{.value = pgm.length}, PGM_LENGTH_ADDR},
		{{.value = pgm.cs}, PGM_CHECKSUM_ADDR},
	};

	for (i = 0; i < ARRAY_SIZE(mtp); i++) {
		ret = mt5728_write_buffer(chip, mtp[i].addr, mtp[i].val.ptr, 2);
		if (ret < 0) {
			pr_err("[%s] write reg[%x] fail", __func__, mtp[i].addr);
			return ret;
		}
	}

	return 0;
}

static bool mt5728_mtp_write(struct mt5728_dev *chip, u32 addr, const u8 *buf , u32 len)
{
	int ret = 0;
	u32 offset;
	u32 write_size, status, times;
	u32 write_retrycnt = 3;
	int i;
	Alig32 val = {0};
	Pgm_Type_t pgm;
	offset = 0;
	write_size = 0;
	pr_info("[%s] Size to write:%d %d\n", __func__, len, chip->client->addr);

	ret = mt5728_run_pgm_fw(chip);
	if (ret < 0 ) {
		pr_err("%s LINE:%d\n",__func__,__LINE__);
		goto out;
	}

	while (len > 0) {
		if (len > MTP_BLOCK_SIZE) {
		    pgm.length = MTP_BLOCK_SIZE;
		    write_size = MTP_BLOCK_SIZE;
		} else {
		    pgm.length = len;
		    write_size = len;
		}

		pgm.addr = addr + offset;
		pgm.cs = pgm.addr;
		pgm.status = PGM_STATUS_READY;

		for (i = 0; i < pgm.length; ++i) {
		    pgm.data[i] = buf[offset + i];
		    pgm.cs += pgm.data[i];
		}
		pgm.cs += pgm.length;
		val.value = pgm.status;

		ret = mt5728_mtp_wirte_step(chip, pgm);
		if (ret < 0) {
			pr_err("%s LINE:%d\n",__func__,__LINE__);
			goto out;
		}

		ret = mt5728_write_buffer(chip, PGM_DATA_ADDR, pgm.data, pgm.length);
		if (ret < 0) {
			pr_err("%s LINE:%d\n",__func__,__LINE__);
			goto out;
		}

		val.value = PGM_STATUS_WMTP;
		ret = mt5728_write_buffer(chip, PGM_STATUS_ADDR,val.ptr, 2);
		if (ret < 0) {
			pr_err("%s LINE:%d\n",__func__,__LINE__);
			goto out;
		}
		msleep(25);
		ret = mt5728_read_buffer(chip, PGM_STATUS_ADDR, val.ptr, 2);
		if (ret < 0) {
			pr_err("%s LINE:%d\n",__func__,__LINE__);
			goto out;
		}
		status = val.ptr[0];
		times = 0;
		while(status == PGM_STATUS_WMTP) {
			msleep(50);
			ret = mt5728_read_buffer(chip, PGM_STATUS_ADDR, val.ptr, 2);
			if (ret < 0) {
				pr_err("%s LINE:%d\n",__func__,__LINE__);
				goto out;
			}
			status = val.ptr[0];
			times += 1;
			if (times > 100) {
				pr_err("[%s] Program write timeout\n",__func__);
				return false;
			}
		}
		if (status == PGM_STATUS_PROGOK) {
			len -= write_size;
			offset += write_size;
			pr_info("%s-%d PGM_STATUS_PROGOK\n",__func__, __LINE__);
		} else if (write_retrycnt > 0){
			write_retrycnt--;
			pr_err("[%s] NUKNOWN write_retrycnt:%d\n",__func__,write_retrycnt);
			continue;
		} else {
			pr_err("[%s] PGM_STATUS_NUKNOWN \n",__func__);
			return false;
		}
  
	}
	pr_info("%s succeed!\n",__func__);
	return true;
out:
	pr_info("[%s] Error\n",__func__);
	return false;
}

static int mt5728_update_fw(struct wireless_charger *wl_chg,
	const struct firmware *fw, bool force)
{
	int ret = 0;
	struct mt5728_dev *chip = wl_chg->private_d;
	const u8 *data = fw->data;
	u32 bin_crc = 0;
	u32 bin_len = 0;

	bin_crc = *(u8 *)(data + MT5728_MTP_CRC_LOW);
	bin_crc = bin_crc | *(u8 *)(data + MT5728_MTP_CRC_HIGH) << 8;

	bin_len = *(u8 *)(data + MT5728_MTP_LEN_LOW);
	bin_len = bin_len | *(u8 *)(data + MT5728_MTP_LEN_HIGH) << 8;

	pr_info("%s, crc:%x, len:%x\n", __func__, bin_crc, bin_len);

	if((bin_len +32)!= fw->size){
		pr_err("%s: wrong fw data in system\n",__func__);
		return ret;
	}

	mt5728_enter_sleep_mode(chip, true);

	ret = mt5728_mtp_crc_verify(chip, bin_len, bin_crc);
	if (!ret | force) {
		ret = mt5728_mtp_write(chip, 0x0, data + 32, bin_len);
	}

	mt5728_enter_sleep_mode(chip, false);
	return !ret;
}

static int mt5728_request_irqs(struct mt5728_dev *chip)
{
	int ret = 0;
	struct i2c_client *client = chip->client;

	chip->dev_irq = gpiod_to_irq(chip->irq_gpio);
	ret = devm_request_threaded_irq(&client->dev, chip->dev_irq,
			NULL, mt5728_irq_handle, IRQF_TRIGGER_FALLING |
			IRQF_ONESHOT, "mt5728_irq", chip);
	if (ret) {
		pr_err("%s:failed to request dev IRQ %d: %d\n",
		__func__, chip->dev_irq, ret);
		goto exit;
	}

	chip->pg_irq = gpiod_to_irq(chip->pg_gpio);
	ret = devm_request_threaded_irq(&client->dev, chip->pg_irq,
			NULL, mt5728_pg_irq_handle, IRQF_TRIGGER_RISING
			| IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "mt5728_pg", chip);
	if (ret) {
		pr_err("%s:failed to request pg IRQ %d: %d\n",
		__func__, chip->pg_irq, ret);
		goto exit;
	}

	chip->wired_irq = gpiod_to_irq(chip->wired_gpio);
	ret = devm_request_threaded_irq(&client->dev, chip->wired_irq,
			NULL, mt5728_wired_irq_handle, IRQF_TRIGGER_RISING
			| IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "mt5728_wired", chip);
	if (ret) {
		pr_err("%s:failed to request irq:%d, ret:%d\n",
		__func__, chip->wired_irq, ret);
		goto exit;
	}

exit:
	return ret;
}

static int mt5728_set_power_on_state(struct mt5728_dev *chip)
{
	wireless_ic_set_state(chip->wl_chg, WIRELESS_PROBE_END);
	chip->pg = !gpiod_get_value(chip->pg_gpio);
	pr_info("pg:%d\n", chip->pg);
	if (!chip->pg)
		return 0;

	mt5728_clear_irq(chip);
	chip->ldo_on = true;
	wireless_ic_set_state(chip->wl_chg, WIRELESS_PG_CHANGE);
	wireless_ic_set_state(chip->wl_chg, WIRELESS_LDO_ON);

	return mt5728_rx_epp_int_irq_handler(chip);
}

static int mt5728_probe(struct i2c_client* client, const struct i2c_device_id* id)
{
	int ret = 0;
	struct mt5728_dev *chip = NULL;
	struct wireless_charger *wl_chg = NULL;

	pr_info("MT5728 wireless probe, version %s\n", DRIVER_FIRMWARE_VERSION);
	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->hw_info = devm_kzalloc(&client->dev, sizeof(*chip->hw_info), GFP_KERNEL);
	if (!chip->hw_info)
		return -ENOMEM;

	wl_chg = devm_kzalloc(&client->dev, sizeof(*wl_chg), GFP_KERNEL);
	if (!wl_chg)
		return -ENOMEM;

	wl_chg->wm_chg = get_charger_by_name("wireless_manager");
	if (!wl_chg->wm_chg) {
		pr_err("%s: get wireless manager failed\n", __func__);
		return -EPROBE_DEFER;
	}

	chip->regmap = devm_regmap_init_i2c(client, &mt5728_regmap_config);
	if (!chip->regmap) {
		pr_err("MT5728 parent regmap is missing\n");
		return -EINVAL;
	}

	chip->client        	= client;
	chip->dev           	= &client->dev;
	chip->wl_chg			= wl_chg;
	chip->bus.read	    	= mt5728_read;
	chip->bus.write     	= mt5728_write;

	i2c_set_clientdata(client, chip);

	mutex_init(&chip->i2c_lock);

	wl_chg->ops = &mt5728_chg_ops;
	wl_chg->name = "mt5728";
	register_wireless_charger_device(wl_chg, (void *)chip);

	ret = mt5728_parse_dt(chip);
	if (ret) {
		pr_info("MT5728_int parse dt failed\n");
		goto unregister;
	}

	ret = mt5728_request_irqs(chip);
	if (ret)
		goto unregister;

	device_init_wakeup(chip->dev, true);

	mt5728_enter_sleep_mode(chip, false);
	/* mt5728_clear_irq(chip); */

	mt5728_set_power_on_state(chip);
	pr_info("MT5728 probed successfully\n");

	return 0;
unregister:
	unregister_wireless_charger_device(wl_chg);
	return -EINVAL;	
}

static void mt5728_remove(struct i2c_client* client)
{
	pr_info("%s:MT5728 remove\n",__func__);
	return;
}

static int mt5728_i2c_suspend(struct device *dev)
{
	struct mt5728_dev *chip = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		pr_debug("%s\n", __func__);
		enable_irq_wake(chip->pg_irq);
		enable_irq_wake(chip->dev_irq);
	}

	return 0;
}

static int mt5728_i2c_resume(struct device *dev)
{
	struct mt5728_dev *chip = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		pr_debug("%s\n", __func__);
		disable_irq_wake(chip->pg_irq);
		disable_irq_wake(chip->dev_irq);
	}

	return 0;
}

static void mt5728_shutdown(struct i2c_client *client)
{
	struct mt5728_dev *chip = i2c_get_clientdata(client);

	if (chip) {
		disable_irq_nosync(chip->pg_irq);
		disable_irq_nosync(chip->dev_irq);
	}
}

static SIMPLE_DEV_PM_OPS(mt5728_pm_ops, mt5728_i2c_suspend, mt5728_i2c_resume);

static const struct i2c_device_id mt5728_dev_id[] = {
	{"wireless", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, mt5728_dev_id);

static const struct of_device_id mt5728_of_match[] = {
	{.compatible = "tran,mt5728"},
	{},
};

static struct i2c_driver mt5728_i2c_driver = {
	.driver = {
		.name		= DEVICE_NAME,
		.owner		= THIS_MODULE,
		.of_match_table = mt5728_of_match,
		.pm		= &mt5728_pm_ops,
	},
	.probe	  = mt5728_probe,
	.remove   = mt5728_remove,
	.shutdown = mt5728_shutdown,
	.id_table = mt5728_dev_id,
};

module_i2c_driver(mt5728_i2c_driver);

MODULE_AUTHOR("Transsion Inc.");
MODULE_DESCRIPTION("MT5728 Wireless Power");
MODULE_LICENSE("GPL");
