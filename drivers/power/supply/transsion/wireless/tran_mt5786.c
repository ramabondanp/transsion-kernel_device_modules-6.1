// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#define pr_fmt(fmt)    "[MT5786]: %s: " fmt, __func__

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/unistd.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include "tran_mt5786.h"

#define DEVICE_NAME "mt5786"

#define MT5786_TX_MIN_FREQ 120
#define MT5786_TX_MAX_FREQ 148

static int mt5786_rx_epp_ready_irq_handler(struct mt5786_dev *chip);
static int mt5786_rx_ldo_on_irq_handler(struct mt5786_dev *chip);

static const struct regmap_config mt5786_regmap_config = {
	.reg_bits     = 16,
	.val_bits     = 8,
	.max_register = 0xFFFF,
};

struct irq_map_desc {
	const char *name;
	int (*hdlr)(struct mt5786_dev *chip);
	u32 stat_mask;
};

static int mt5786_read(struct mt5786_dev *chip, u16 reg, u8* val)
{
	unsigned int temp;
	int rc;

	if (IS_ERR_OR_NULL(chip)) {
		pr_err("chip is nullter\n");
		return -ENODEV;
	}

	mutex_lock(&chip->i2c_lock);

	rc = regmap_read(chip->regmap, reg, &temp);
	if (rc >= 0) {
		*val = (u8)temp;
	} else {
		pr_err("read failed!rc = %d\n", rc);
	}

	mutex_unlock(&chip->i2c_lock);

	return rc;
}

static int mt5786_write(struct mt5786_dev *chip, u16 reg, u8 val)
{
	int rc = 0;

	if (IS_ERR_OR_NULL(chip)) {
		pr_err("chip is nullter\n");
		return -ENODEV;
	}

	mutex_lock(&chip->i2c_lock);

	rc = regmap_write(chip->regmap, reg, val);
	if (rc < 0) {
		pr_err("write failed!rc = %d\n", rc);
	}

	mutex_unlock(&chip->i2c_lock);

	return rc;
}

static int mt5786_read_buffer(struct mt5786_dev *chip, u16 reg, u8* buf, u32 size)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(chip)) {
		pr_err("chip is nullter\n");
		return -ENODEV;
	}

	mutex_lock(&chip->i2c_lock);
	ret = regmap_bulk_read(chip->regmap, reg, buf, size);
	if (ret) {
		pr_err("read 0x%x failed!\n", reg);
	}
	mutex_unlock(&chip->i2c_lock);

	return ret;
}

static int mt5786_write_buffer(struct mt5786_dev *chip, u16 reg, u8* buf, u32 size)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(chip)) {
		pr_err("chip is nullter\n");
		return -ENODEV;
	}

	mutex_lock(&chip->i2c_lock);
	ret = regmap_bulk_write(chip->regmap, reg, buf, size);
	if (ret) {
		pr_err("write 0x%x failed!\n", reg);
	}
	mutex_unlock(&chip->i2c_lock);

	return ret;
}

static int mt5786_update_bits(struct mt5786_dev *chip, u8 reg, u8 data,
				  u8 mask)
{
	int ret;
	u8 _data;

	ret = mt5786_read(chip, reg, &_data);
	if (ret < 0)
		goto out;

	_data &= ~mask;
	_data |= (data & mask);

	ret = mt5786_write(chip, reg, _data);
out:
	return ret;
}

static int mt5786_get_vout(struct mt5786_dev *chip) 
{
	Alig16 vout = {0};
	int ret = 0;

	ret = mt5786_read_buffer(chip, MT5786_RX_GET_VOUT, vout.ptr, sizeof(vout));
	if (ret < 0) {
		pr_err("read 0x%x failed!ret = %d \n", MT5786_RX_GET_VOUT, ret);
		return 0;
	}

	return vout.value;
}

static int mt5786_get_iout(struct mt5786_dev *chip) 
{
	Alig16 iout = {0};
	int ret = 0;

	ret = mt5786_read_buffer(chip, MT5786_RX_GET_IOUT, iout.ptr, sizeof(iout));
	if (ret < 0) {
		pr_err("read 0x%x failed!ret = %d \n", MT5786_RX_GET_IOUT, ret);
		return 0;
	}

	return iout.value;
}

static int mt5786_get_pg_status(struct wireless_charger *wc, bool *value)
{	
	struct mt5786_dev *chip = (struct mt5786_dev *)wc->private_d;

	*value = !gpiod_get_value(chip->pg_gpio) ? true : false;
	pr_info("pg:%d\n", *value);

	return 0;
}

static int mt5786_get_wl_vbus(struct wireless_charger *wc, int *value)
{	
	struct mt5786_dev *chip = (struct mt5786_dev *)wc->private_d;

	*value = mt5786_get_vout(chip);

	return 0;
}

static int mt5786_get_wl_ibus(struct wireless_charger *wc, int *value)
{
	struct mt5786_dev *chip = (struct mt5786_dev *)wc->private_d;

	*value = mt5786_get_iout(chip);

	return 0;
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

static int mt5786_set_vout_val(struct mt5786_dev *chip, int vol)
{
	Alig32 val_32;
	Alig16 val;
	int rc = 0, count = 20, i = 0;
	signed char val_cep = 0;
	signed char cep_tmp[6] = {0};

	pr_info("%s, vol:%d", __func__, vol);

	val.value = (u16)vol;
	rc = mt5786_write_buffer(chip, MT5786_RX_SET_VOUT, val.ptr, 2);
	if (rc < 0) {
		pr_err("write 0x%x failed!\n", MT5786_RX_SET_VOUT);
		return rc;
	}

	val_32.value = MT5786_VOUT_CHANGE;

	rc = mt5786_write_buffer(chip, MT5786_RX_CMD, val_32.ptr, sizeof(val_32));
	if (rc < 0) {
		pr_err("write 0x%x failed!\n", MT5786_RX_CMD);
		return rc;
	}

	//if (shutdown)
	//	return 0;

	do {
		msleep(200);
		rc = mt5786_read_buffer(chip, MT5786_RX_GET_CEP, &val_cep, sizeof(val_cep));
		if (rc < 0 || !chip->pg) {
			return -EIO;
		}

		cep_tmp[i++] = val_cep;
		pr_info("--->ce value:%d\n", val_cep);

		if (i == ARRAY_SIZE(cep_tmp)) {
			i = 0;
			if (compare_cep_val_change(cep_tmp, ARRAY_SIZE(cep_tmp))) {
				pr_info("ce val is not change\n");
				return 0;
			}
		}

		if (val_cep < 2) {
			return 0;
		}
	} while(count--);

	return 0;
}

static int mt5786_plug_out(struct wireless_charger *wc)
{
	struct mt5786_dev *chip = wc->private_d;

	pr_info("%s\n", __func__);
	memset(chip->hw_info, 0, sizeof(*chip->hw_info));
	chip->site = false;
	chip->overload = false;
	chip->ldo_on = false;
	chip->bpp_rdy = false;
	chip->protocol = 0;
	return 0;
}

static int mt5786_plug_in(struct wireless_charger *wc)
{	
	struct mt5786_dev *chip = wc->private_d;

	memset(chip->hw_info, 0, sizeof(*chip->hw_info));
	return 0;
}

static int mt5786_set_sleep_mode(struct wireless_charger *wl_chg, bool en)
{
	struct mt5786_dev *chip = wl_chg->private_d;

	if (IS_ERR_OR_NULL(chip->sleep_gpio)) {
		pr_err("sleep_gpio is null\n");
		return -EINVAL;
	}

	pr_info("%s, en = %d\n", __func__, en);
	gpiod_set_value(chip->sleep_gpio, en);

	return 0;
}

static int mt5786_get_wl_hw_info(struct wireless_charger *wc,
	struct wls_hw_info *hw_info)
{
	struct mt5786_dev *chip = (struct mt5786_dev *)wc->private_d;

	if (IS_ERR_OR_NULL(chip))
		return -EINVAL;

	memcpy(hw_info, chip->hw_info, sizeof(*hw_info));
	return 0;
}

static int mt5786_set_wl_voltage(struct wireless_charger *wl_chg, int volt, bool shutdown)
{	
	struct mt5786_dev *chip = (struct mt5786_dev *)wl_chg->private_d;

	if (!chip->pg) {
		pr_err("wls pg off, set wls voltage failed\n");
		return -EINVAL;
	}

	return mt5786_set_vout_val(chip, volt);
}

static void mt5786_set_tx_mode_freq(struct mt5786_dev *chip, u32 reg, int freq)
{
	Alig16 val = {0};

	val.value = freq * 10;

	mt5786_write_buffer(chip, reg, val.ptr, 2);
}

static int mt5786_tx_mode_enter(struct mt5786_dev *chip)
{
	int ret = 0;
	Alig32 val = {0};

	val.value = MT5786_ENTER_TX;

	ret = mt5786_write_buffer(chip, MT5786_TX_CMD, val.ptr, sizeof(val));
	if (ret < 0) {
		pr_err("%s, wirte tx cmd fail:%d\n", __func__, ret);
	}

	return ret;
}

static int mt5786_tx_mode_start(struct mt5786_dev *chip, bool en)
{
	int ret = 0;
	Alig32 val = {0};

	val.value = en ? MT5786_START_TX | MT5786_HALF_BR : MT5786_STOP_TX;

	ret = mt5786_write_buffer(chip, MT5786_TX_CMD, val.ptr, sizeof(val));
	if (ret < 0) {
		pr_err("%s, wirte tx cmd fail:%d\n", __func__, ret);
	}

	return ret;
}

static void mt5786_tx_mode_config(struct mt5786_dev *chip, struct tx_config *txc)
{
	pr_info("%s, protocol:%d, power:%d\n", __func__, txc->protocol, txc->power);

	mt5786_set_tx_mode_freq(chip, MT5786_MAX_PERIOD, chip->min_freq);
	mt5786_set_tx_mode_freq(chip, MT5786_MIN_PERIOD, chip->max_freq);

	mt5786_write(chip, MT5786_PROTOCOL_MODE, txc->protocol);
	if (txc->protocol) {
		mt5786_write(chip, MT5786_EPP_POWER, txc->power);
	}
}

static void mt5786_tx_set_bridge_mode(struct mt5786_dev *chip, enum setup_mode mode)
{
	pr_err("%s setup_mode:%d\n", __func__, mode);

	switch (mode) {
	case FULL_BRIDGE:
		mt5786_update_bits(chip, MT5786_TX_CMD + 2, BIT(3), BIT(3));
		break;
	case HALF_BRIDGE:
		mt5786_update_bits(chip, MT5786_TX_CMD + 2, BIT(2), BIT(2));
		break;
	default:
		break;
	}
}

static int mt5786_set_tx_mode(struct wireless_charger *wl_chg, bool en, struct tx_config *txc)
{	
	int ret = 0;
	struct mt5786_dev *chip = (struct mt5786_dev *)wl_chg->private_d;

	pr_info("%s en:%d\n", __func__, en);

	chip->tx_mode = en;
	if (en) {
		ret = mt5786_tx_mode_enter(chip);
		if (ret < 0) {
			return ret;
		}

		mt5786_tx_set_bridge_mode(chip, HALF_BRIDGE);
		msleep(20);

		ret = mt5786_tx_mode_start(chip, true);
		if (ret < 0) {
			return ret;
		}

		mt5786_tx_mode_config(chip, txc);
	} else {
		ret = mt5786_tx_mode_start(chip, false);
	}

	return ret;
}

static int mt5786_get_tx_power(struct wireless_charger *wl_chg, int *power)
{	
	Alig16 val = {0,};
	int ret;
	struct mt5786_dev *chip = (struct mt5786_dev *)wl_chg->private_d;
	
	ret = mt5786_read_buffer(chip, MT5786_RX_MAX_POWER, val.ptr, sizeof(val));
	if (ret < 0) {
		pr_err("mt5786 read 0x%x failed, ret = %d\n", MT5786_RX_MAX_POWER, ret);
		return -EIO;
	}

	*power = val.value / 2;
	pr_info("%s, power : %d\n", __func__, *power);
	return 0;
}

static int mt5786_work_sys_mode(struct mt5786_dev *chip)
{
	Alig32 mode = {0};
	int ret = 0;

	ret = mt5786_read_buffer(chip, MT5786_RX_SYS_MODE, mode.ptr, sizeof(mode));
	if (ret < 0) {
		pr_err("mt5786 read 0x%x failed, ret = %d\n", MT5786_RX_SYS_MODE, ret);
		return 0;
	}

	return mode.value & MT5786_SYSMODE_MASK;
}

static ssize_t mt5786_mtp_crc_slef_check_success(struct mt5786_dev *chip)
{
	int i;
	int ret;
	Alig16 val;

	for (i = 0; i < 5; i++) {
		msleep(10);
		ret = mt5786_read_buffer(chip, MT5786_RX_CRC_RESULT, val.ptr, 1);
		pr_info("wait mtp_crc_slef_check:%x,%x\n",val.ptr[0],val.ptr[1]);
		if (ret) {
			pr_err("mtp_crc_slef_check: read failed\n");
			return ret;
		}

		if (val.ptr[0] == 0x02) {
			return 0;
		}
	}

	return -1;
}

static bool mt5786_mtp_crc_fw_check_itself(struct mt5786_dev *chip, 
	u16 len, u16 crc)
{
	int ret;
	Alig16 val;

	val.value = crc;
	ret = mt5786_write_buffer(chip, MT5786_RX_CRC_VAL, val.ptr, 2);

	val.value = len;
	ret += mt5786_write_buffer(chip, MT5786_RX_CRC_LEN, val.ptr, 2);

	val.value = 0xAA;
	ret += mt5786_write_buffer(chip, MT5786_RX_CRC_CMD, val.ptr, 1);

	msleep(100);

	ret += mt5786_mtp_crc_slef_check_success(chip);

	if (ret) {
		pr_err("crc_check: failed\n");
		return false;
	}

	pr_info("[crc_check] succ\n");

	return true;
}

static void mt5786_sram_write(struct mt5786_dev *chip, 
	u32 addr, u8 *data, u32 len)
{
	u32 offset, length, size;

	offset = 0;
	length = 0;
	size = len;

	pr_info("Length to write:%d\n", len);

	while (size > 0) {
		if (size > SRAM_PAGE_SIZE) {
			length = SRAM_PAGE_SIZE;
		} else {
			length = size;
		}

		pr_info("Length of this write :%d\n", length);
		mt5786_write_buffer(chip, addr + offset, data + offset, length);
		size -= length;
		offset += length;
		usleep_range(2000, 3000);
	}

	pr_info("Write completion\n");
}

static int mt5786_stop_watchdog_try24times(struct mt5786_dev *chip)
{
	int i;
	Alig16 write, read;

	write.value  = MT5786_WDG_DISABLE;
	read.value = 1;

	for (i = 0; i < 24; i++) {
		mt5786_write_buffer(chip, MT5786_RX_PMU_WDGEN_REG, write.ptr, 1);
		udelay(10);
		mt5786_read_buffer(chip, MT5786_RX_PMU_WDGEN_REG, read.ptr, 1);
		pr_info("read 0x%x=%d\n", MT5786_RX_PMU_WDGEN_REG, read.ptr[0]);
		if (read.ptr[0] == 0) {
			return 0;
		}

		usleep_range(1000, 2000);
	}

	return -1;
}

static bool mt5786_run_bootloader(struct mt5786_dev *chip)
{
	Alig16 val;

	if(mt5786_stop_watchdog_try24times(chip) != 0) {
		pr_err("WatchDog Stop Fail");
		return false;
	} else {
		pr_err("WatchDog Stop OK");
	}

	val.value  = 0x07;
	mt5786_write_buffer(chip, MT5786_RX_PMU_FLAG_REG, val.ptr, 1);

	val.value = MT5786_KEY;
	mt5786_write_buffer(chip, MT5786_RX_SYS_KEY_REG, val.ptr, 1);

	val.value = 0x20;
	mt5786_write_buffer(chip, MT5786_RX_M0_CTRL_REG, val.ptr, 1);

	val.value = 0x01;
	mt5786_write_buffer(chip, 0x5209, val.ptr, 2);
	msleep(50);

	mt5786_sram_write(chip, 
		0x0000, (u8 *)mt5786_bootloader_bin, sizeof(mt5786_bootloader_bin));
	msleep(50);

	val.value = 0x90;
	mt5786_write_buffer(chip, MT5786_RX_M0_CTRL_REG, val.ptr, 1);
	msleep(100);

	val.value = 0;
	mt5786_read_buffer(chip, 0x1008, val.ptr, 2);
	pr_info("read0x%x=0x%x\n", 0x1008, val.value);
	if (val.value != MT5786_HWCHIP_ID) {
		pr_err("BootLoader Start Fail\n");
		return false;
	} else {
		pr_info("[%s] BootLoader Start Ok\n", __func__);
	}

	pr_info("finish \n");
	return true;
}

static bool mt5786_main_mtp_eraser(struct mt5786_dev *chip)
{
	Alig16 val;
	int i;

	val.value  = 0x8000;
	mt5786_write_buffer(chip, 0x1000, val.ptr, 2);

	val.value = 0;
	for (i = 0; i < 50; i++) {
		mt5786_read_buffer(chip, 0x1000, val.ptr, 2);
		if (val.value == 0x02) {
			pr_info("main mtp eraser Ok\n");
			return true;
		}
		msleep(10);
	}

	pr_info("main mtp eraser Fail, val:0x%x\n", val.value);

	return false;
}

static bool mt5786_mtp_write(struct mt5786_dev *chip, 
	u32 addr, u8 * buf , u32 len)
{
	u32 offset;
	u32 write_size ,status,times;
	u32 write_retrycnt;
	u32 size;
	int i;
	Alig16 val;
	Pgm_Type_t pgm;

	size = len;
	offset = 0;
	write_size = 0;

	pr_info("Size to write:%d\n", size);

	write_retrycnt = 8;

	while (size > 0) {
		if (size > MTP_BLOCK_SIZE) {
			pgm.length = MTP_BLOCK_SIZE;
			write_size = MTP_BLOCK_SIZE;
		} else {
			pgm.length = size;
			write_size = size;
		}

		pgm.addr = addr + offset;
		pgm.cs = pgm.addr;
		pgm.status = PGM_STATUS_READY;
		for (i = 0; i < pgm.length; ++i) {
			pgm.data[i] = buf[offset + i];
			pgm.cs += pgm.data[i];
		}

		pgm.cs += pgm.length;

		val.value = pgm.addr;
		mt5786_write_buffer(chip, MT5786_RX_PGM_ADDR_ADDR, val.ptr, 2);

		val.value = pgm.length;
		mt5786_write_buffer(chip, MT5786_RX_PGM_LENGTH_ADDR, val.ptr, 2);

		val.value = pgm.cs;
		mt5786_write_buffer(chip, MT5786_RX_PGM_CHECKSUM_ADDR, val.ptr, 2);
		mt5786_write_buffer(chip, MT5786_RX_PGM_DATA_ADDR, pgm.data, pgm.length);

		val.value = PGM_STATUS_WRITE;
		mt5786_write_buffer(chip, MT5786_RX_PGM_STATUS_ADDR, val.ptr, 2);

		msleep(25);
		mt5786_read_buffer(chip, MT5786_RX_PGM_STATUS_ADDR, val.ptr, 2);

		status = val.ptr[0];
		times = 0;

		while(status == PGM_STATUS_WRITE) {
			msleep(50);
			mt5786_read_buffer(chip, MT5786_RX_PGM_STATUS_ADDR, val.ptr, 2);
			status = val.ptr[0];
			pr_info("Program writing\n");
			times += 1;
			if (times > 100) {
				pr_info("Program write timeout\n");
				return false;
			}
		}

		if (status == PGM_STATUS_PROGOK) {
			size -= write_size;
			offset += write_size;
		} else if (status == PGM_STATUS_ERRCS) {
			if (write_retrycnt > 0) {
				write_retrycnt--;
				pr_info("ERRCS write_retrycnt:%d\n", write_retrycnt);
				continue;
			} else {
				pr_err("PGM_STATUS_ERRCS\n");
				return false;
			}
		} else if (status == PGM_STATUS_ERRPGM) {
			if (write_retrycnt > 0) {
				write_retrycnt--;
				pr_info("ERRPGM write_retrycnt:%d\n", write_retrycnt);
				continue;
			} else {
				pr_err("PGM_STATUS_ERRPGM\n");
				return false;
			}
		} else {
			if (write_retrycnt > 0) {
				write_retrycnt--;
				pr_err("NUKNOWN write_retrycnt:%d\n", write_retrycnt);
				continue;
			} else {
				pr_err("PGM_STATUS_NUKNOWN \n");
				return false;
			}
		}
	}

	return true;
}

static int mt5786_mtp_write_m0_core_reset(struct mt5786_dev *chip)
{
	Alig16 val;
	int ret = 0;

	val.value  = 0x57;
	ret = mt5786_write_buffer(chip, 0x5244, val.ptr, 1);

	val.value  = 0;
	ret = mt5786_write_buffer(chip, 0x5209, val.ptr, 1);

	val.value  = 0x80;
	ret = mt5786_write_buffer(chip, 0x5200, val.ptr, 1);

	return ret;
}

static int mt5786_brun_fw_task(struct mt5786_dev *chip, 
	u8 * buf , u32 len, u32 crc_val)
{
	bool ret = false;

	if (mt5786_run_bootloader(chip)) {
		pr_info("i2c bootloader OK\n");

		ret = mt5786_main_mtp_eraser(chip);
		if (!ret) {
			pr_err("mtp eraser failed!\n");
			return -EINVAL;
		}

		if (mt5786_mtp_write(chip, 0x0000, buf, len)) {
			pr_err("i2c write Fw OK\n");
			mt5786_mtp_write_m0_core_reset(chip);
			/*
			After the burning is completed, the verification is increased, 
			the power is cut off
			*/
			msleep(200);

			if (mt5786_mtp_crc_fw_check_itself(chip, len, crc_val)) {
				pr_err("Verify OK\n");
			} else {
				pr_err("Verify failed\n");
				return -EINVAL;
			}
		} else {
			pr_err("write Fw failed\n");
			return -EINVAL;
		}
	}

	return 0;
}

static __maybe_unused u8 mt5786_mtp_verify_with_boot_run(struct mt5786_dev *chip, 
	u32 addr, u32 len, u16 crcvalue)
{
	Alig16 val = {0};
	int waitTimeOutCnt = 0;
	int status = 0;
	u16 crcvlaue_chip = 0; 

	mt5786_write_buffer(chip, MT5786_RX_PGM_ADDR_ADDR, val.ptr, 2);

	val.value = len;
	mt5786_write_buffer(chip, MT5786_RX_PGM_LENGTH_ADDR, val.ptr, 2);

	val.value = crcvalue;
	mt5786_write_buffer(chip, MT5786_RX_PGM_CHECKSUM_ADDR, val.ptr, 2);

	val.value = (1 << 6);
	mt5786_write_buffer(chip, MT5786_RX_PGM_STATUS_ADDR, val.ptr, 2);

	waitTimeOutCnt = 100;
	while(waitTimeOutCnt--) {
		mt5786_read_buffer(chip, MT5786_RX_PGM_STATUS_ADDR, val.ptr, 2);
		pr_info("read 0x%x=%d %d\n", MT5786_RX_PGM_STATUS_ADDR, val.ptr[0], val.ptr[1]);
		status = val.ptr[0] | (val.ptr[1] << 8);
		if (status & (1 << 7)) {
			mt5786_read_buffer(chip, MT5786_RX_PGM_DATA_ADDR, val.ptr, 2);
			crcvlaue_chip = val.value;
			pr_err("mt5786_mtp_verify error,crcvlaue_chip:%x,crcvalue:%x", crcvlaue_chip, crcvalue);
			return false;
		}

		//VERIFYOK
		if (status & (1 << 8)) {
			mt5786_read_buffer(chip, MT5786_RX_PGM_DATA_ADDR, val.ptr, 2);
			crcvlaue_chip = val.value;
			pr_info("mt5786_mtp_verify success,crcvlaue_chip:%x,crcvalue:%x", crcvlaue_chip, crcvalue);
			return true;
		}
		msleep(60);
	}

	pr_info("TimeOut cal_crc :0x%04x\n", crcvalue);

	return false;
}

static int mt5786_read_chipid(struct mt5786_dev *chip)
{
	int ret = 0;
	Alig16 val = {0};

	mt5786_stop_watchdog_try24times(chip);

	ret = mt5786_read_buffer(chip, 0x5a50, val.ptr, sizeof(val));
	if (ret < 0)
		return ret;

	pr_info("chip id:%x\n", val.value);
	return val.value;
}

static int mt5786_wireless_update_fw(struct wireless_charger *wl_chg,
	const struct firmware *fw, bool force)
{
	int ret = 0;
	bool crc_check = 0;
	const void *data = fw->data;
	struct mt5786_dev *chip = wl_chg->private_d;
	u32 bin_crc = 0;
	u32 bin_len = 0;
	short chipid = 0;

	pr_info("%s, brush MTP program start!\n", __func__);

	chipid = mt5786_read_chipid(chip);
	if (chipid != MT5786_HWCHIP_ID) {
		pr_err("%s, read chip id fail(%x)\n", __func__, chipid);
		return -EINVAL;
	}

	bin_crc = *(u8 *)(data + MT5786_MTP_CRC_LOW);
	bin_crc = bin_crc | *(u8 *)(data + MT5786_MTP_CRC_HIGH) << 8;

	bin_len = *(u8 *)(data + MT5786_MTP_LEN_LOW);
	bin_len = bin_len | *(u8 *)(data + MT5786_MTP_LEN_HIGH) << 8;

	pr_info("%s, crc:%x, len:%x\n", __func__, bin_crc, bin_len);

	if ((bin_len + 32) != fw->size) {
		pr_err("%s, wrong fw data in system\n", __func__);
		return !ret;
	}

	crc_check = mt5786_mtp_crc_fw_check_itself(chip, bin_len, bin_crc);
	if (crc_check && !force) {
		pr_err("don't need update fw bin\n");
		return 0;
	}

	ret = mt5786_brun_fw_task(chip, (u8 *)(data + 32), bin_len, bin_crc);

	pr_info("%s %s!\n", __func__, !ret ? "successfully" : "fail");

	return ret;
}

static int mt5786_get_fw_version(struct wireless_charger *wl_chg, u32 *ver)
{
	int ret = 0;
	Alig32 val = {0};
	struct mt5786_dev *chip = wl_chg->private_d;

	ret = mt5786_read_buffer(chip, MT5786_MINOR_VER, val.ptr, sizeof(val));
	if (ret) {
		pr_err("read reg[%d] fail, ret:%d\n", MT5786_MINOR_VER, ret);
		return ret;
	}

	*ver = val.value;
	return 0;
}

static int mt5786_set_ovp_ctrl(struct wireless_charger *wl_chg, bool en)
{
	return 0;
}

static int mt5786_wired_path_setup(struct wireless_charger *wl_chg, bool en)
{
	struct mt5786_dev *chip = wl_chg->private_d;

	if (IS_ERR_OR_NULL(chip->wired_path_gpio)) {
		pr_debug("wired_path_gpio is nullter\n");
		return 0;
	}

	pr_info("wired_path_gpio ctrl:%d\n", en);
	gpiod_set_value(chip->wired_path_gpio, en);
	msleep(20);

	return 0;
}

static int mt5786_get_wired_state(struct wireless_charger *wl_chg, bool *status)
{
	struct mt5786_dev *chip = wl_chg->private_d;

	if (IS_ERR_OR_NULL(chip->wired_gpio)) {
		pr_debug("wired_gpio is nullter\n");
		return -EIO;
	}

	*status = !gpiod_get_value(chip->wired_gpio);
	pr_info("wired gpio status:%d\n", *status);

	return 0;
}

static int mt5786_set_power_on_state(struct mt5786_dev *chip)
{
	wireless_ic_set_state(chip->wl_chg, WIRELESS_PROBE_END);
	chip->pg = !gpiod_get_value(chip->pg_gpio);
	pr_info("%s pg:%d\n", __func__, chip->pg);
	if (!chip->pg)
		return 0;

	if (mt5786_work_sys_mode(chip) == MT5786_SYSMODE_RX) {
		mt5786_rx_ldo_on_irq_handler(chip);
	
		if (chip->protocol == EPP)
			mt5786_rx_epp_ready_irq_handler(chip);
	}

	return 0;
}

static int mt5786_get_adc(struct wireless_charger *wl_chg, enum adc_channel chan)
{
	return 0;
}

static int mt5786_get_vrect(struct wireless_charger *wl_chg, int *mv)
{
	Alig16 val = {0,};
	int ret;
	struct mt5786_dev *chip = (struct mt5786_dev *)wl_chg->private_d;
	
	ret = mt5786_read_buffer(chip, MT5786_RX_GET_VRECT, val.ptr, sizeof(val));
	if (ret < 0) {
		pr_err("mt5786 read 0x%x failed, ret = %d\n", MT5786_RX_GET_VRECT, ret);
		return -EIO;
	}
	*mv = val.value;
	
	return 0;
}

static short mt5786_get_cep(struct wireless_charger *wl_chg)
{
	signed char val = 0;
	int ret = 0;
	struct mt5786_dev *chip = (struct mt5786_dev *)wl_chg->private_d;

	ret = mt5786_read_buffer(chip, MT5786_RX_GET_CEP, &val, sizeof(val));
	if (ret < 0) {
		pr_err("mt5786 read 0x%x failed, ret = %d\n", MT5786_RX_GET_CEP, ret);
		return -EIO;
	}

	pr_info("%s--->cep:%d\n", __func__, val);
	return val;
}

static int mt5786_get_chip_temp(struct wireless_charger *wl_chg, int *temp)
{
	u8 val = 0;
	int ret;
	struct mt5786_dev *chip = (struct mt5786_dev *)wl_chg->private_d;

	ret = mt5786_read_buffer(chip, MT5786_RX_GET_TEMP, &val, sizeof(val));
	if (ret < 0) {
		pr_err("mt5786 read 0x%x failed, ret = %d\n", MT5786_RX_GET_TEMP, ret);
		return -EIO;
	}
	*temp = val;
	
	return 0;
}

static int mt5786_dump_reg(struct wireless_charger *wl_chg)
{
	int ret = 0;
	int cep = 0, vrect = 0, vout = 0, iout = 0, temp = 0;
	struct mt5786_dev *chip = (struct mt5786_dev *)wl_chg->private_d;

	if (!chip->pg) {
		pr_err("wireless offline\n");
		return 0;
	}

	cep = mt5786_get_cep(wl_chg);
	ret = mt5786_get_vrect(wl_chg, &vrect);
	ret = mt5786_get_chip_temp(wl_chg, &temp);
	vout = mt5786_get_vout(chip);
	iout = mt5786_get_iout(chip);
	pr_err("vrect = %d, vout = %d, iout = %d, cep = %d temp = %d\n", 
		vrect, vout, iout, cep, temp);

	return 0;
}

static int mt5786_set_bridge_mode(struct wireless_charger *wl_chg)
{
	struct mt5786_dev *chip = (struct mt5786_dev *)wl_chg->private_d;

	pr_info("%s\n", __func__);

	if (!chip->pg && !chip->tx_mode) {
		pr_err("wireless offline\n");
		return -ENODEV;
	}

	return mt5786_update_bits(chip, MT5786_RX_CMD + 2, BIT(1), BIT(1));
}

static int mt5786_get_bridge_mode(struct wireless_charger *wl_chg)
{
	int ret = 0;
	int bridge_mode = -1;
	Alig32 mode = {0};
	struct mt5786_dev *chip = (struct mt5786_dev *)wl_chg->private_d;

	if (!chip->pg) {
		pr_err("wireless offline\n");
		return -ENODEV;
	}

	ret = mt5786_read_buffer(chip, MT5786_RX_SYS_MODE, mode.ptr, sizeof(mode));
	if (ret < 0) {
		pr_err("mt5786 read 0x%x failed, ret = %d\n", MT5786_RX_SYS_MODE, ret);
		return ret;
	}

	if (mode.value & MT5786_SYSMODE_BRIDGE_MODE_MASK) {
		bridge_mode = FULL_BRIDGE;
	} else {
		bridge_mode = HALF_BRIDGE;
	}

	pr_info("bridge_mode:%d\n", bridge_mode);

	return bridge_mode;
}

static u8 mt5786_get_wls_protocol(struct wireless_charger *wl_chg)
{
	struct mt5786_dev *chip = wl_chg->private_d;

	return chip->protocol;
}

static int mt5786_wirte_reg_data(struct wireless_charger *wl_chg, int addr, u8 data)
{
	struct mt5786_dev *chip = wl_chg->private_d;

	return mt5786_write_buffer(chip, addr, &data, sizeof(data));
}

static int mt5786_read_reg_data(struct wireless_charger *wl_chg, int addr, u8 *data)
{
	u8 val = 0;
	struct mt5786_dev *chip = wl_chg->private_d;

	mt5786_read_buffer(chip, addr, &val, sizeof(val));
	pr_info("%s, %x, %x\n", __func__, addr, val);
	*data = val;

	return 0;
}

static int mt5786_get_tx_bridge_voltage(struct wireless_charger *wl_chg)
{
	struct mt5786_dev *chip = wl_chg->private_d;

	return chip->hw_info->tx_bridge_vol_max;
}

static int mt5786_set_lpm_mode(struct wireless_charger *wl_chg, bool en)
{
	struct mt5786_dev *chip = wl_chg->private_d;
	pr_info("%s. en:%d\n", __func__, en);

	return mt5786_update_bits(chip, MT5786_RX_CMD, en << 3, BIT(3));
}

static int mt5786_set_drop_vol(struct wireless_charger *wl_chg)
{
	struct mt5786_dev *chip = wl_chg->private_d;
	Alig16 vrect = {.value = 1000};

	pr_info("%s\n", __func__);

	mt5786_write_buffer(chip, MT5786_RX_VRECT_CURVE_SET, vrect.ptr, sizeof(vrect));
	mt5786_update_bits(chip, MT5786_RX_CMD + 2, BIT(3), BIT(3));
	mt5786_set_vout_val(chip, 7500);

	return 0;
}

static struct wls_ops mt5786_chg_ops = {
	.get_wireless_pg			= mt5786_get_pg_status,
	.get_wireless_vbus			= mt5786_get_wl_vbus,
	.get_wireless_ibus			= mt5786_get_wl_ibus,
	.set_wireless_plug_out			= mt5786_plug_out,
	.set_wireless_plug_in			= mt5786_plug_in,
	.set_wireless_sleep			= mt5786_set_sleep_mode,
	.get_wireless_hw_info 			= mt5786_get_wl_hw_info,
	.set_wireless_voltage			= mt5786_set_wl_voltage,
	.dump_wireless_status			= mt5786_dump_reg,
	.set_wireless_tx_mode			= mt5786_set_tx_mode,
	.get_wireless_power 			= mt5786_get_tx_power,
	.set_wireless_fw_update 		= mt5786_wireless_update_fw,
	.get_wireless_fw_version		= mt5786_get_fw_version,
	.set_ovp_ctrl				= mt5786_set_ovp_ctrl,
	.wired_path_setup			= mt5786_wired_path_setup,
	.get_wired_state			= mt5786_get_wired_state,
	.get_wireless_adc			= mt5786_get_adc,
	.get_tx_ce_value			= mt5786_get_cep,
	.get_bridge_mode			= mt5786_get_bridge_mode,
	.set_bridge_mode			= mt5786_set_bridge_mode,
	.get_wls_protocol 			= mt5786_get_wls_protocol,
	.wirte_reg_data 			= mt5786_wirte_reg_data,
	.read_reg_data 				= mt5786_read_reg_data,
	.tx_bridge_voltage 			= mt5786_get_tx_bridge_voltage,
	.set_wireless_lpm_mode 			= mt5786_set_lpm_mode,
	.set_drop_voltage			= mt5786_set_drop_vol,
};

static int mt5786_parse_dt(struct mt5786_dev *chip)
{
	int ret = 0;
	const struct device_node *np = chip->dev->of_node;

	chip->pg_gpio = devm_gpiod_get(chip->dev, "powergood", GPIOD_IN);
	if (IS_ERR(chip->pg_gpio)) {
		pr_err("gpio_is_valid error, pg_gpio\n");
		return PTR_ERR(chip->pg_gpio);
	}

	chip->irq_gpio = devm_gpiod_get(chip->dev, "eint_wpc", GPIOD_IN);
	if (IS_ERR(chip->irq_gpio)){
		pr_err("get irq_gpio failed\n");
		return PTR_ERR(chip->irq_gpio);
	}

	chip->wired_gpio = devm_gpiod_get(chip->dev, "wired", GPIOD_IN);
	if (IS_ERR(chip->wired_gpio)){
		pr_err("get wired gpio failed\n");
	}

	chip->wired_path_gpio = devm_gpiod_get(chip->dev, "wired_path", GPIOD_OUT_LOW);
	if (IS_ERR(chip->wired_path_gpio)){
		pr_err("get wired path gpio failed\n");
	}

	chip->protocol_gpio = devm_gpiod_get(chip->dev, "protocol", GPIOD_OUT_HIGH);
	if (IS_ERR(chip->protocol_gpio)){
		pr_err("get protocol gpio failed\n");
	}

	chip->sleep_gpio = devm_gpiod_get(chip->dev, "sleep", GPIOD_OUT_HIGH);
	if (IS_ERR(chip->sleep_gpio)){
		pr_err("get sleep gpiog failed\n");
	}

	chip->avdd_en = devm_gpiod_get(chip->dev, "enavdd", GPIOD_OUT_LOW);
	if (IS_ERR(chip->avdd_en)){
		pr_err("get avdd en gpio failed\n");
	}

	ret = of_property_read_u32(np, "min_freq", &chip->min_freq);
	if (ret < 0){
		pr_err("%s no min_freq(%d)\n", __func__, ret);
		chip->min_freq = MT5786_TX_MIN_FREQ;
	}

	ret = of_property_read_u32(np, "max_freq", &chip->max_freq);
	if (ret < 0){
		pr_err("%s no max_freq(%d)\n", __func__, ret);
		chip->max_freq = MT5786_TX_MAX_FREQ;
	}

	ret = of_property_read_u32(np, "project_power", &chip->project_power);
	if (ret < 0){
		pr_err("%s no project_power(%d)\n", __func__, ret);
		chip->project_power = 0;
	}

	ret = of_property_read_u8_array(np, "private_tx_fod_20v",
		chip->pt_fod_20v,
		sizeof(chip->pt_fod_20v));
	if (ret < 0){
		pr_err("%s private_tx_fod_20v fail(%d)\n", __func__, ret);
	}

	ret = of_property_read_u8_array(np, "private_tx_fod_10v",
		chip->pt_fod_10v,
		sizeof(chip->pt_fod_10v));
	if (ret < 0){
		pr_err("%s private_tx_fod_10v fail(%d)\n", __func__, ret);
	}

	return 0;
}

/* tx interrupt start */
static int mt5786_tx_power_trans_irq_handler(struct mt5786_dev *chip)
{
	wireless_ic_set_state(chip->wl_chg, WIRELESS_TX_DET_RX);
	return 0;
}

static int mt5786_tx_ept_irq_handler(struct mt5786_dev *chip)
{
	Alig32 val = {0};

	chip->wpc_mode = 0;
	mt5786_read_buffer(chip, MT5786_EPT_TYPE, val.ptr, sizeof(val));
	pr_info("%s ept val:%x\n", __func__, val.value);
	wireless_ic_set_state(chip->wl_chg, WIRELESS_TX_RMV_RX);

	return 0;
}

//When mt5786 is in tx mode, the user places the phone on the wireless dock(tx)
static int mt5786_tx_ac_valid_irq_handler(struct mt5786_dev *chip)
{
	wireless_ic_set_state(chip->wl_chg, WIRELESS_TX_AC_VALID);
	return 0;
}

static int mt5786_tx_wpc_mode_irq_handler(struct mt5786_dev *chip)
{
	u8 wpc_mode = 0;

	mt5786_read(chip, MT5786_WPC_STAT, &wpc_mode);
	chip->wpc_mode = wpc_mode;

	wireless_ic_set_state(chip->wl_chg, WIRELESS_TX_WPC_MODE);
	pr_info("%s, wpc mode:%d\n", __func__, wpc_mode);
	return 0;
}

static int mt5786_tx_init_done_irq_handler(struct mt5786_dev *chip)
{
	u8 value = 0x1;

	pr_info("%s\n", __func__);
	mt5786_write_buffer(chip, 0x81, &value, sizeof(value));

	return 0;
}

#define MT5786_TX_IRQ_DESC(_name, _stat_s) 	\
{												\
	.name = #_name,								\
	.stat_mask = _stat_s,						\
	.hdlr = mt5786_tx_##_name##_irq_handler	\
}

static const struct irq_map_desc mt5786_tx_irq_map_tbl[] = {
	MT5786_TX_IRQ_DESC(power_trans, MT5786_INT_TX_POWER_TRANSFER),
	//MT5786_TX_IRQ_DESC(remove_power, MT5786_INT_TX_REMOVE_POWER),
	MT5786_TX_IRQ_DESC(ept, MT5786_INT_TX_EPT),
	MT5786_TX_IRQ_DESC(ac_valid, MT5786_INT_TX_PING_CLASH),
	MT5786_TX_IRQ_DESC(wpc_mode, MT5786_INT_TX_WPC_MODE),
	MT5786_TX_IRQ_DESC(init_done, MT5786_INT_TX_DONE),
};

static void mt5786_tx_interrupt_handler(struct mt5786_dev *chip, u32 tx_value)
{
	int i;
	const struct irq_map_desc *desc;

	for (i = 0; i < ARRAY_SIZE(mt5786_tx_irq_map_tbl); i++) {
		desc = &mt5786_tx_irq_map_tbl[i];
		if ((tx_value & desc->stat_mask) && desc->hdlr) {
			desc->hdlr(chip);
		}
	}
}
/* tx interrupt end */

/* rx interrupt start */
static int mt5786_fskrecv_fsk_det(struct mt5786_dev *chip, u16 value)
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
		chip->overload = true;
		break;
	case CMD_OVER_LOAD_CANCEL:
		chip->overload = false;
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int mt5786_rx_ldo_on_irq_handler(struct mt5786_dev *chip)
{
	Alig16 val = {0};
	u8 protocol_mode = 0;
	int ret = 0;
	u8 ask_cap = 0x01;

	chip->ldo_on = true;

	ret = mt5786_read_buffer(chip, MT5786_MINOR_VER, val.ptr, sizeof(val));
	if (ret) {
		pr_err("read reg[%d] fail, ret:%d\n", MT5786_MINOR_VER, ret);
	}

	ret = mt5786_read_buffer(chip, MT5786_PROTOCOL_TYPE,
		&protocol_mode, sizeof(protocol_mode));

	pr_info("wls protocol mode : %x, %s, fw_ver:0x%x", protocol_mode,
		wls_protocol_mode_name(protocol_mode), val.value);

	if (protocol_mode <= MPP_FORCE && protocol_mode >= MPP_RESTRICT) {
		mt5786_write_buffer(chip, MT5786_RX_ASK_CAP, &ask_cap, sizeof(ask_cap));
		wireless_ic_set_state(chip->wl_chg, WIRELESS_RX_MPP);
	}

	chip->protocol = protocol_mode;

	wireless_ic_set_state(chip->wl_chg, WIRELESS_LDO_ON);
	return ret;
}

static int mt5786_rx_ldo_off_irq_handler(struct mt5786_dev *chip)
{
	Alig32 val = {0};

	chip->ldo_on = false;
	mt5786_read_buffer(chip, MT5786_RX_LDO_OFF_CAUSE, val.ptr, sizeof(val));
	pr_err("%s ldo_off cause(0xa8):0x%x", __func__, val.value);

	return 0;
}

static int mt5786_rx_fskrecv_irq_handler(struct mt5786_dev *chip)
{
	Alig16 val = {0};

	mt5786_read_buffer(chip, MT5786_RX_FSK_BUFF, val.ptr, sizeof(val));
	mt5786_fskrecv_fsk_det(chip, val.value);
	return 0;
}

static int mt5786_rx_ldoocp_irq_handler(struct mt5786_dev *chip)
{
	wireless_ic_set_state(chip->wl_chg, WIRELESS_RX_HW_ERR);
	return 0;
}

static int mt5786_rx_ldoovp_irq_handler(struct mt5786_dev *chip)
{
	wireless_ic_set_state(chip->wl_chg, WIRELESS_RX_HW_ERR);
	return 0;
}

static int mt5786_rx_bpp_ready_irq_handler(struct mt5786_dev *chip)
{
	chip->bpp_rdy = true;
	return 0;
}

static int mt5786_rx_epp_ready_irq_handler(struct mt5786_dev *chip)
{
	Alig16 val = {0};

	mt5786_read_buffer(chip, MT5786_RX_NEG_POWER, val.ptr, 2);
	pr_info("epp_power:%d\n", val.value / 2);

	wireless_ic_set_state(chip->wl_chg, WIRELESS_RX_EPP_READY);
	return 0;
}

static int mt5786_rx_power_on_irq_handler(struct mt5786_dev *chip)
{
	return 0;
}

static int mt5786_rx_ldo_otp_irq_handler(struct mt5786_dev *chip)
{
	Alig16 val = {0};
	int ret, vout = 0;
	
	ret = mt5786_read_buffer(chip, MT5786_RX_GET_VRECT, val.ptr, sizeof(val));
	if (ret < 0) {
		pr_err("mt5786 read 0x%x failed, ret = %d\n", MT5786_RX_GET_VRECT, ret);
		return -EIO;
	}

	vout = mt5786_get_vout(chip);
	
	pr_err("mt5786 vrect = %d vout = %d\n", val.value, vout);
	
	return ret;
}

static int mt5786_rx_ldo_otp1_irq_handler(struct mt5786_dev *chip)
{
	Alig16 val = {0};
	int ret, vout = 0;
	
	ret = mt5786_read_buffer(chip, MT5786_RX_GET_VRECT, val.ptr, sizeof(val));
	if (ret < 0) {
		pr_err("mt5786 read 0x%x failed, ret = %d\n", MT5786_RX_GET_VRECT, ret);
		return -EIO;
	}

	vout = mt5786_get_vout(chip);
	
	pr_err("mt5786 vrect = %d vout = %d\n", val.value, vout);
	
	return ret;
}

#define MT5786_RX_IRQ_DESC(_name, _stat_s) \
	{.name = #_name, .stat_mask = _stat_s, \
	 .hdlr = mt5786_rx_##_name##_irq_handler}

static const struct irq_map_desc mt5786_rx_irq_map_tbl[] = {
	MT5786_RX_IRQ_DESC(ldo_on, MT5786_INT_RX_MLDO_ON),
	MT5786_RX_IRQ_DESC(ldo_off, MT5786_INT_RX_MLDO_OFF),
	MT5786_RX_IRQ_DESC(fskrecv, MT5786_INT_RX_PPP_FSK_RCV),
	MT5786_RX_IRQ_DESC(ldoocp, MT5786_INT_RX_IOUT_OCP),
	MT5786_RX_IRQ_DESC(ldoovp, MT5786_INT_RX_MLDO_VOUT_OVP),
	MT5786_RX_IRQ_DESC(power_on, MT5786_INT_RX_SS_READY),
	MT5786_RX_IRQ_DESC(epp_ready, MT5786_INT_RX_WPC_NEG_EPP),
	MT5786_RX_IRQ_DESC(bpp_ready, MT5786_INT_RX_WPC_NEG_OK),
	MT5786_RX_IRQ_DESC(ldo_otp, MT5786_INT_RX_MLDO_OTP0),
	MT5786_RX_IRQ_DESC(ldo_otp1, MT5786_INT_RX_MLDO_OTP1),
};

static void mt5786_rx_interrupt_handler(struct mt5786_dev *chip, 
	u32 rx_value)
{
	int i;
	const struct irq_map_desc *desc;

	for (i = 0; i < ARRAY_SIZE(mt5786_rx_irq_map_tbl); i++) {
		desc = &mt5786_rx_irq_map_tbl[i];
		if ((rx_value & desc->stat_mask) && desc->hdlr) {
			desc->hdlr(chip);
		}
	}
}
/* rx interrupt end */

static int __mt5786_irq_handle(struct mt5786_dev *chip)
{
	Alig32 val = {0}, fclr = {0}, cmd = {0};
	int ret, sysmode = 0;

	pm_stay_awake(chip->dev);

	ret = mt5786_read_buffer(chip, MT5786_RX_INTFLAG, val.ptr, sizeof(val));
	if (ret < 0) {
		pr_err("mt5786 read 0x%x failed, ret = %d\n", MT5786_RX_INTFLAG, ret);
		goto out;
	}
	pr_err("mt5786 irq status = 0x%x\n", val.value);

	if (val.value == 0) {
		pr_info("No INT here\n");
		goto out;
	}

	//clear int status
	fclr.value = val.value;
	mt5786_write_buffer(chip, MT5786_RX_INTCLR, fclr.ptr, sizeof(fclr));

	mt5786_read_buffer(chip, MT5786_RX_CMD, cmd.ptr, sizeof(cmd));
	cmd.value |= MT5786_INT_CLEAR;
	mt5786_write_buffer(chip, MT5786_RX_CMD, cmd.ptr, sizeof(cmd));

	sysmode = mt5786_work_sys_mode(chip);
	switch (sysmode) {
	case MT5786_SYSMODE_SBY:
		wireless_ic_set_state(chip->wl_chg, WIRELESS_TX_INIT_DONE);
		pr_info("mt5786 in SBY MODE\n");
		break;
	case MT5786_SYSMODE_RX:
		pr_info("mt5786 in Rx mode\n");
		mt5786_rx_interrupt_handler(chip, val.value);
		break;
	case MT5786_SYSMODE_TX:
		pr_info("mt5786 in Tx mode\n");
		mt5786_tx_interrupt_handler(chip, val.value);
		break;
	case MT5786_SYSMODE_UNKNOWN:
	default:
		pr_err("mt5786 mode unknown\n");
		break;
	}
out:
	pm_relax(chip->dev);

	return IRQ_HANDLED;
}

static irqreturn_t mt5786_irq_handle(int irq, void  *data)
{
	struct mt5786_dev *chip = (struct mt5786_dev *)data;

	if (IS_ERR_OR_NULL(chip)) {
		pr_err("mt5786_dev is nullter!\n");
		goto out;
	}

	__mt5786_irq_handle(chip);
out:
	return IRQ_HANDLED;
}

static irqreturn_t mt5786_pg_irq_handle(int irq, void *data)
{
	struct mt5786_dev *chip = (struct mt5786_dev *)data;

	chip->pg = !gpiod_get_value(chip->pg_gpio);

	pr_info("%s\n", chip->pg ? "pg on" : "pg off");
	wireless_ic_set_state(chip->wl_chg, WIRELESS_PG_CHANGE);

	return IRQ_HANDLED;
}

static irqreturn_t __maybe_unused mt5786_wired_irq_handle(int irq, void *data)
{

	struct mt5786_dev *chip = (struct mt5786_dev *)data;

	chip->wired_state = !gpiod_get_value(chip->wired_gpio);
	pr_info("%s\n", chip->wired_state ? "wired plugin" : "wired pulgout");

	wireless_ic_set_state(chip->wl_chg, WIRELESS_WIRED_CHANGE);

	return IRQ_HANDLED;
}

static int mt5786_irq_register(struct mt5786_dev *chip)
{
	int rc = 0;

	if (IS_ERR_OR_NULL(chip->irq_gpio) || IS_ERR_OR_NULL(chip->pg_gpio)) {
		pr_err("irq_gpio or pg_gpio is null!\n");
		return -ENODEV;
	}

	chip->dev_irq = gpiod_to_irq(chip->irq_gpio);
	rc = devm_request_threaded_irq(chip->dev, chip->dev_irq, NULL,
		mt5786_irq_handle, IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "mt5786_irq", chip);
	if (rc) {
		pr_err("failed to request IRQ %d: %d\n", chip->dev_irq, rc);
		goto out;
	}

	chip->pg_irq = gpiod_to_irq(chip->pg_gpio);
	rc = devm_request_threaded_irq(chip->dev, chip->pg_irq, NULL, mt5786_pg_irq_handle,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "mt5786_pg", chip);
	if (rc) {
		pr_err("failed to request pg IRQ %d: %d\n", chip->pg_irq, rc);
		goto out;
	}

	if (!IS_ERR_OR_NULL(chip->wired_gpio)) {
		chip->wired_irq = gpiod_to_irq(chip->wired_gpio);
		rc = devm_request_threaded_irq(chip->dev, chip->wired_irq, NULL, 
			mt5786_wired_irq_handle, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | 
				IRQF_ONESHOT, "mt5786_wired", chip);
		if (rc) {
			pr_err("failed to request irq:%d, ret:%d\n", chip->wired_irq, rc);
			goto out;
		}
	}

out:
	return rc;
}

static int mt5786_probe(struct i2c_client* client, const struct i2c_device_id* id)
{
	struct mt5786_dev *chip = NULL;
	struct wireless_charger *wl_chg = NULL;
	int rc = 0;

	pr_info("%s start!\n", __func__);
	if (get_hw_wireless_ic()[0] || get_hw_wireless_ic()[1])
		return -EINVAL;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (IS_ERR_OR_NULL(chip)) {
		pr_err("kzalloc for mt5786_dev failed!\n");
		return -ENOMEM;
	}

	chip->hw_info = devm_kzalloc(&client->dev, sizeof(*chip->hw_info), GFP_KERNEL);
	if (!chip->hw_info)
		return -ENOMEM;

	wl_chg = devm_kzalloc(&client->dev, sizeof(*wl_chg), GFP_KERNEL);
	if (IS_ERR_OR_NULL(wl_chg)) {
		pr_err("kzalloc for wireless_charger failed!\n");
		return -ENOMEM;
	}

	wl_chg->wm_chg = get_charger_by_name("wireless_manager");
	if (IS_ERR_OR_NULL(wl_chg->wm_chg)) {
		pr_err("get wireless manager failed\n");
		return -EPROBE_DEFER;
	}

	chip->regmap = devm_regmap_init_i2c(client, &mt5786_regmap_config);
	if (IS_ERR_OR_NULL(chip->regmap)) {
		pr_err("mt5786 parent regmap is missing\n");
		return -EINVAL;
	}

	chip->client = client;
	chip->dev = &client->dev;
	chip->wl_chg = wl_chg;
	chip->bus.read = mt5786_read;
	chip->bus.write = mt5786_write;
	chip->bus.read_buf = mt5786_read_buffer;
	chip->bus.write_buf = mt5786_write_buffer;

	i2c_set_clientdata(client, chip);

	mutex_init(&chip->notify_lock);
	mutex_init(&chip->i2c_lock);

	wl_chg->ops = &mt5786_chg_ops;
	wl_chg->name = DEVICE_NAME;
	register_wireless_charger_device(wl_chg, (void *)chip);

	rc = mt5786_parse_dt(chip);
	if (rc < 0) {
		pr_err("parse dt failed!\n");
		goto err_parse_dt;
	}

	rc = mt5786_irq_register(chip);
	if (rc < 0) {
		pr_err("irq init failed!\n");
		goto err_irq_init;
	}

	device_init_wakeup(chip->dev, true);

	mt5786_set_power_on_state(chip);

	pr_info("success!\n");

	return 0;

err_irq_init:
err_parse_dt:
	unregister_wireless_charger_device(wl_chg);
	mutex_destroy(&chip->notify_lock);
	mutex_destroy(&chip->i2c_lock);

	return rc;
}

static int mt5786_remove(struct i2c_client *client)
{
	struct mt5786_dev *chip = i2c_get_clientdata(client);

	if (IS_ERR_OR_NULL(chip)) {
		pr_err("get mt5786_dev failed!\n");
		return 0;
	}

	mutex_destroy(&chip->notify_lock);
	mutex_destroy(&chip->i2c_lock);

	return 0;
}

static void mt5786_shutdown(struct i2c_client* client)
{
	struct mt5786_dev *chip = i2c_get_clientdata(client);

	if (chip) {
		disable_irq_nosync(chip->pg_irq);
		disable_irq_nosync(chip->dev_irq);
	}
}

static int mt5786_i2c_suspend(struct device *dev)
{
	struct mt5786_dev *chip = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		enable_irq_wake(chip->dev_irq);
		enable_irq_wake(chip->pg_irq);
	}

	return 0;
}

static int mt5786_i2c_resume(struct device *dev)
{
	struct mt5786_dev *chip = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		disable_irq_wake(chip->dev_irq);
		disable_irq_wake(chip->pg_irq);
	}

	return 0;
}

static const struct i2c_device_id mt5786_dev_id[] = {
	{"mt5786", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, mt5786_dev_id);
static SIMPLE_DEV_PM_OPS(mt5786_pm_ops, mt5786_i2c_suspend, mt5786_i2c_resume);

#ifdef CONFIG_OF
static const struct of_device_id mt5786_of_match[] = {
	{.compatible = "wireless_charger,mt5786"},
	{},
};
MODULE_DEVICE_TABLE(of, mt5786_of_match);
#endif

static struct i2c_driver mt5786_driver = {
	.driver = {
		.name           = "mt5786",
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr(mt5786_of_match),
		.pm		= &mt5786_pm_ops,
	},
	.probe 		= mt5786_probe,
	.remove 	= mt5786_remove,
	.shutdown 	= mt5786_shutdown,
	.id_table 	= mt5786_dev_id,
};

static int __init mt5786_driver_init(void) {
#ifdef CONFIG_OF
	pr_info("mt5786_driver_init start!\n ");
#endif
	return i2c_add_driver(&mt5786_driver);
}

module_init(mt5786_driver_init);

static void __exit mt5786_driver_exit(void) {
	pr_info("mt5786_driver_exit\n");
	return i2c_del_driver(&mt5786_driver);
}

module_exit(mt5786_driver_exit);

MODULE_AUTHOR("Transsion Inc.");
MODULE_DESCRIPTION("MT5786 Wireless Power Receiver");
MODULE_LICENSE("GPL");
