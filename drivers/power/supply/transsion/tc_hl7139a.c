// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 Transsion Inc.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/gpio/consumer.h>
#include <linux/of_gpio.h>
#include "tc_hl7139a.h"
//#include "usb_switch.h"
#include "tc_charger_class.h"
#if IS_ENABLED(CONFIG_TC_CHARGER)
#include "tc_charger.h"
#include "tc_common_class.h"
#include "tc_misc_intf.h"
#endif

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

enum hl7139a_board_id {
	HL7139A_BOARD_H833 = 0,
	HL7139A_BOARD_H833C,
	HL7139A_BOARD_MAX,
};
static struct hl7139a_chip *g_chip = NULL;
#endif

#define BITS(_end, _start)          ((BIT(_end) - BIT(_start)) + BIT(_end))

/* Information */
#define HL7139A_DRV_VERSION	"1.0.0_TRAN"
#define HL7139A_DEVID		0x0A

/* Def reg value */
#define	HL7139A_VBAT_OVP_DEF		0x23
#define	HL7139A_IBAT_OCP_DEF		0x2e
#define	HL7139A_WDT_TIMER_DEF	0x00

struct hl7139a_chip;
static inline int __hl7139a_i2c_read8(struct hl7139a_chip *chip, u8 reg, u8 *data);

enum hl7139a_irqidx {
	HL7139A_IRQIDX_STATE_CHG = 0,
	HL7139A_IRQIDX_REG,
	HL7139A_IRQIDX_TS_TEMP,
	HL7139A_IRQIDX_V_NOT_OK,
	
	HL7139A_IRQIDX_VIN_OVP,
	HL7139A_IRQIDX_VIN_UVLO,
	HL7139A_IRQIDX_TRACK_OV,
	HL7139A_IRQIDX_TRACK_UV,

	HL7139A_IRQIDX_VBAT_OVP,
	HL7139A_IRQIDX_VOUT_OVP,
	HL7139A_IRQIDX_PMID_QUAL,
	HL7139A_IRQIDX_VBUS_UV,

	HL7139A_IRQIDX_CUR,
	HL7139A_IRQIDX_IIN_OCP,
	HL7139A_IRQIDX_IBAT_OCP,
	HL7139A_IRQIDX_IIN_UCP,

	HL7139A_IRQIDX_SHORT,
	HL7139A_IRQIDX_FET_SHORT,
	HL7139A_IRQIDX_CFLY_SHORT,
	HL7139A_IRQIDX_WDOG,

	HL7139A_IRQIDX_DEV_MODE,
	HL7139A_IRQIDX_THSD,
	HL7139A_IRQIDX_MAX,
};

enum hl7139a_notify {
	HL7139A_NOTIFY_IBUSUCPF = 0,
	HL7139A_NOTIFY_IBUSOCP,
	HL7139A_NOTIFY_VBUSOVP,
	HL7139A_NOTIFY_IBATOCP,
	HL7139A_NOTIFY_VBATOVP,
	HL7139A_NOTIFY_VOUTOVP,
	HL7139A_NOTIFY_VDROVP,
	HL7139A_NOTIFY_MAX,
};

enum hl7139a_statflag_idx {
	HL7139A_SF_INT = 0,
	HL7139A_SF_INT_STS_A,
	HL7139A_SF_INT_STS_B,
	HL7139A_SF_STATUS_A,
	HL7139A_SF_STATUS_B,
	HL7139A_SF_MAX,
};

enum hl7139a_type {
	HL7139A_TYPE_STANDALONE = 0,
	HL7139A_TYPE_STANDALONE_STATUS,
	HL7139A_TYPE_SLAVE,
	HL7139A_TYPE_MASTER,
	HL7139A_TYPE_MAX,
};

static const char *hl7139a_type_name[HL7139A_TYPE_MAX] = {
	"standalone", "standalone", "slave", "master",
};

static const u32 hl7139a_chgdev_notify_map[HL7139A_NOTIFY_MAX] = {
	CHARGER_DEV_NOTIFY_IBUSUCP_FALL,
	CHARGER_DEV_NOTIFY_IBUSOCP,
	CHARGER_DEV_NOTIFY_VBUS_OVP,
	CHARGER_DEV_NOTIFY_IBATOCP,
	CHARGER_DEV_NOTIFY_BAT_OVP,
	CHARGER_DEV_NOTIFY_VOUTOVP,
	CHARGER_DEV_NOTIFY_VDROVP,
};

static const u8 hl7139a_reg_sf[HL7139A_SF_MAX] = {
	HL7139A_REG_01,
	HL7139A_REG_03,
	HL7139A_REG_04,
	HL7139A_REG_05,
	HL7139A_REG_06,
};

struct hl7139a_desc {
	const char *chg_name;
	const char *rm_name;
	u8 rm_slave_addr;
	u32 vbatovp;
	u32 ibatocp;
	u32 vbusovp;
	u32 ibusocp;
	u32 vinovp;
	u32 wdt;
	u32 ibat_rsense;
	u32 i2c_vil_up;
	u32 lt_wd_tmr;
	u32 gpp_conf;
	u32 fsw_val;
	// interrupt masked
    	bool state_chg_i_masked;
	bool reg_i_masked;
	bool ts_temp_i_masked;
	bool v_ok_i_masked;
	bool cur_i_masked;
	bool short_i_masked;
	bool wdog_i_masked;
	bool protocol_i_masked;
	bool charge_mode;
	bool vbat_reg_dis;
	bool ibat_reg_dis;
	bool iin_reg_dis;
	bool tdie_reg_dis;
	bool track_ov_dis;
	bool track_uv_dis;
	bool vbatovp_dis;
	bool ibatocp_dis;
	bool ibusocp_dis;
	bool wdt_dis;
	bool tsbusotp_dis;
	bool tsbatotp_dis;
	bool tdieotp_dis;
	bool reg_en;
	bool voutovp_dis;
	bool ibusadc_dis;
	bool vbusadc_dis;
	bool vacadc_dis;
	bool voutadc_dis;
	bool vbatadc_dis;
	bool ibatadc_dis;
	bool tsbusadc_dis;
	bool tsbatadc_dis;
	bool tdieadc_dis;
};

static const struct hl7139a_desc hl7139a_desc_defval = {
	.chg_name = "divider_charger",
	.rm_name = "hl7139a",
	.rm_slave_addr = 0x5f,
	.vbatovp = 4500000,
	.ibatocp = 8600000,
	.vbusovp = 12000000,
	.ibusocp = 4250000,
	.vinovp = 11000000,
	.wdt = 300000,
	.ibat_rsense = 0,	/* 5mohm */
	.i2c_vil_up =0,
	.lt_wd_tmr =LT_WD_100MS,
	.gpp_conf = HL7139A_GPP_SNSP,
	.fsw_val = 0,
	// interrupt masked
    	.state_chg_i_masked = 1,
	.reg_i_masked = 1,
	.ts_temp_i_masked = 1,
	.v_ok_i_masked = 1,
	.cur_i_masked = 1,
	.short_i_masked = 1,
	.wdog_i_masked = 1,
	.protocol_i_masked = 1,
	.charge_mode = HL7139A_CHARGE_MODE_2_1,
	.vbat_reg_dis = HL7139A_VBAT_REG_DISABLE,
	.ibat_reg_dis = HL7139A_IBAT_REG_DISABLE,
	.iin_reg_dis = HL7139A_IBUS_REG_DISABLE,
	.tdie_reg_dis =HL7139A_TDIE_REG_DISABLE,
	.track_ov_dis = HL7139A_PMID2OUT_OVP_DISABLE,
	.track_uv_dis = HL7139A_PMID2OUT_UVP_DISABLE,
	.vbatovp_dis = false,
	.ibatocp_dis = false,
	.ibusocp_dis = false,
	.wdt_dis = false,
	.tsbusotp_dis = false,
	.tsbatotp_dis = false,
	.tdieotp_dis = false,
	.reg_en = false,
	.voutovp_dis = false,
};

struct hl7139a_chip {
	struct device *dev;
	struct i2c_client *client;
	struct mutex io_lock;
	struct mutex adc_lock;
	struct mutex stat_lock;
	struct work_struct state_update_work;
	struct mutex hm_lock;
	struct mutex suspend_lock;
	struct mutex notify_lock;
	struct charger_device *chg_dev;
	struct charger_device *wls_dev;
	struct charger_properties chg_prop;
	struct hl7139a_desc *desc;
	struct gpio_desc *irq_gpio;
	struct task_struct *notify_task;
	int irq;
	int notify;
	u8 revision;
	u32 flag;
	u32 stat;
	u32 hm_cnt;
	enum hl7139a_type type;
	bool wdt_en;
	bool force_adc_en;
	bool stop_thread;
	bool notify_disable;
	wait_queue_head_t wq;
	int id;
	atomic_t reset_flag;
#if IS_ENABLED(CONFIG_TRAN_AW95016)
	enum hl7139a_board_id board_id;
#endif
};

enum hl7139a_adc_channel {
	HL7139A_ADC_VBUS = 0,
	HL7139A_ADC_IBUS,
	HL7139A_ADC_VBAT,
	HL7139A_ADC_IBAT,
	HL7139A_ADC_TSBUS,
	HL7139A_ADC_VOUT,
	HL7139A_ADC_TDIE,
	HL7139A_ADC_MAX,
	HL7139A_ADC_NOTSUPP = HL7139A_ADC_MAX,
};

static const u8 hl7139a_adc_reg[HL7139A_ADC_MAX] = {
	HL7139A_REG_42,
	HL7139A_REG_44,
	HL7139A_REG_46,
	HL7139A_REG_48,
	HL7139A_REG_4A,
	HL7139A_REG_4C,
	HL7139A_REG_4E,
};

static const u8 hl7139a_reg_addr[] = {
	HL7139A_REG_DEVINFO,
	HL7139A_REG_03,
	HL7139A_REG_08,
	HL7139A_REG_09,
	HL7139A_REG_0A,
	HL7139A_REG_0B,
	HL7139A_REG_0C,
	HL7139A_REG_0E,
	HL7139A_REG_0F,
	HL7139A_REG_10,
	HL7139A_REG_11,
	HL7139A_REG_12,
	HL7139A_REG_13,
	HL7139A_REG_14,
	HL7139A_REG_15,
	HL7139A_REG_16,
	HL7139A_REG_19,
	HL7139A_REG_1A,
	HL7139A_REG_40,
	HL7139A_REG_41,
};

static const char *hl7139a_adc_name[HL7139A_ADC_MAX] = {
	"Vbus", "Ibus", "Vbat", "Ibat", "VTS", "Vout", "TDie",
};

static const u32 hl7139a_adc_accuracy_tbl[HL7139A_ADC_MAX] = {
	35000,	/* VBUS */
	150000,	/* IBUS */
	20000,	/* VBAT */
	200000,	/* IBAT */
	35000,	/* VTS */
	20000,	/* VOUT */
	4,	/* TDIE */
};

static int hl7139a_read_device(void *client, u32 addr, int len, void *dst)
{
	int ret;
	struct i2c_client *i2c = (struct i2c_client *)client;
	struct hl7139a_chip *chip = i2c_get_clientdata(i2c);

	pm_stay_awake(chip->dev);
	mutex_lock(&chip->suspend_lock);
	ret = i2c_smbus_read_i2c_block_data(i2c, addr, len, dst);
	mutex_unlock(&chip->suspend_lock);
	pm_relax(chip->dev);
	return ret;
}

static int hl7139a_write_device(void *client, u32 addr, int len, const void *src)
{
	int ret;
	struct i2c_client *i2c = (struct i2c_client *)client;
	struct hl7139a_chip *chip = i2c_get_clientdata(i2c);

	pm_stay_awake(chip->dev);
	mutex_lock(&chip->suspend_lock);
	ret = i2c_smbus_write_i2c_block_data(i2c, addr, len, src);
	mutex_unlock(&chip->suspend_lock);
	pm_relax(chip->dev);
	return ret;
}

#define I2C_ACCESS_MAX_RETRY	3
static inline int __hl7139a_i2c_write8(struct hl7139a_chip *chip, u8 reg, u8 data)
{
	int ret, retry = 0;

	do {
		ret = hl7139a_write_device(chip->client, reg, 1, &data);
		retry++;
		if (ret < 0)
			usleep_range(10, 15);
	} while (ret < 0 && retry < I2C_ACCESS_MAX_RETRY);

	if (ret < 0) {
		dev_err(chip->dev, "%s I2CW[0x%02X] = 0x%02X fail ret:%d\n", __func__,
			reg, data, ret);
		return ret;
	}

	return 0;
}

static int __maybe_unused hl7139a_i2c_write8(struct hl7139a_chip *chip, u8 reg, u8 data)
{
	int ret;
	//u8 _data_new = 0;_data_old = 0;
	mutex_lock(&chip->io_lock);
	//ret = __hl7139a_i2c_read8(chip, reg, &_data_old);
	ret = __hl7139a_i2c_write8(chip, reg, data);
	//ret = __hl7139a_i2c_read8(chip, reg, &_data_new);
	//dev_err(chip->dev, "%s I2CW[0x%02X] = 0x%02X, old=0x%x, _data_new=0x%x\n", __func__, reg, data, _data_old, _data_new);
	mutex_unlock(&chip->io_lock);
	return ret;
}
static inline int __hl7139a_i2c_read8(struct hl7139a_chip *chip, u8 reg, u8 *data)
{
	int ret, retry = 0;

	do {
		ret = hl7139a_read_device(chip->client, reg, 1, data);
		retry++;
		if (ret < 0)
			usleep_range(10, 15);
	} while (ret < 0 && retry < I2C_ACCESS_MAX_RETRY);

	if (ret < 0) {
		dev_err(chip->dev, "%s I2CR[0x%02X] fail\n", __func__, reg);
		return ret;
	}

	return 0;
}

static int hl7139a_i2c_read8(struct hl7139a_chip *chip, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&chip->io_lock);
	ret = __hl7139a_i2c_read8(chip, reg, data);
	mutex_unlock(&chip->io_lock);

	return ret;
}

static inline int __hl7139a_i2c_write_block(struct hl7139a_chip *chip, u8 reg,
					   u32 len, const u8 *data)
{
	int ret;

	ret = hl7139a_write_device(chip->client, reg, len, data);
	return ret;
}

static int __maybe_unused hl7139a_i2c_write_block(struct hl7139a_chip *chip, u8 reg, u32 len,
				  const u8 *data)
{
	int ret;

	mutex_lock(&chip->io_lock);
	ret = __hl7139a_i2c_write_block(chip, reg, len, data);
	mutex_unlock(&chip->io_lock);

	return ret;
}

static inline int __hl7139a_i2c_read_block(struct hl7139a_chip *chip, u8 reg,
					  u32 len, u8 *data)
{
	int ret;

	ret = hl7139a_read_device(chip->client, reg, len, data);
	return ret;
}

static int hl7139a_i2c_read_block(struct hl7139a_chip *chip, u8 reg, u32 len,
				 u8 *data)
{
	int ret;

	mutex_lock(&chip->io_lock);
	ret = __hl7139a_i2c_read_block(chip, reg, len, data);
	mutex_unlock(&chip->io_lock);

	return ret;
}

static int hl7139a_i2c_test_bit(struct hl7139a_chip *chip, u8 reg, u8 shft,
			       bool *one)
{
	int ret;
	u8 data;

	ret = hl7139a_i2c_read8(chip, reg, &data);
	if (ret < 0) {
		*one = false;
		return ret;
	}

	*one = (data & (1 << shft)) ? true : false;
	return 0;
}

static int hl7139a_i2c_update_bits(struct hl7139a_chip *chip, u8 reg, u8 data,
				  u8 mask)
{
	int ret;
	u8 _data, _data_old = 0;
	//u8 _data_new = 0;

	mutex_lock(&chip->io_lock);
	ret = __hl7139a_i2c_read8(chip, reg, &_data);
	if (ret < 0)
		goto out;
	_data_old = _data;
	_data &= ~mask;
	_data |= (data & mask);
	ret = __hl7139a_i2c_write8(chip, reg, _data);
	//ret = __hl7139a_i2c_read8(chip, reg, &_data_new);
	//dev_err(chip->dev, "%s I2CW[0x%02X] = 0x%02X, old=0x%x, _data_new=0x%x\n", __func__, reg, _data, _data_old, _data_new);
out:
	mutex_unlock(&chip->io_lock);
	return ret;
}

static inline int hl7139a_set_bits(struct hl7139a_chip *chip, u8 reg, u8 mask)
{
	return hl7139a_i2c_update_bits(chip, reg, mask, mask);
}

static inline int hl7139a_clr_bits(struct hl7139a_chip *chip, u8 reg, u8 mask)
{
	return hl7139a_i2c_update_bits(chip, reg, 0x00, mask);
}

static inline u8 hl7139a_val_toreg(u32 min, u32 max, u32 step, u32 target,
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

static inline u8 hl7139a_val_toreg_via_tbl(const u32 *tbl, int tbl_size,
					  u32 target)
{
	int i;

	if (target < tbl[0])
		return 0;

	for (i = 0; i < tbl_size - 1; i++) {
		if (target >= tbl[i] && target < tbl[i + 1])
			return i;
	}

	return tbl_size - 1;
}

static u8 hl7139a_vbatovp_toreg(u32 uV)
{
	return hl7139a_val_toreg(4000000, 4600000, 10000, uV, false);
}

static u8 hl7139a_ibatocp_toreg(u32 uA)
{
	return hl7139a_val_toreg(4000000, 8600000, 100000, uA, true);
}

static u8 hl7139a_vbusovp_toreg(u32 uV)
{
	return hl7139a_val_toreg(4000000, 19000000, 1000000, uV, true);
}

static u8 hl7139a_vinovp_toreg(u32 uV)
{
	return hl7139a_val_toreg(10200000, 11700000, 100000, uV, false);
}

static u8 hl7139a_ibusocp_toreg(u32 uA)
{
	return hl7139a_val_toreg(1800000, 4300000, 50000, uA, true);
}

static const u32 hl7139a_wdt[] = {
	200000, 500000, 1000000, 2000000, 5000000, 10000000, 20000000, 40000000,
};

static u8 hl7139a_wdt_toreg(u32 uS)
{
	return hl7139a_val_toreg_via_tbl(hl7139a_wdt, ARRAY_SIZE(hl7139a_wdt), uS);
}

static int __hl7139a_update_status(struct hl7139a_chip *chip);
static int __hl7139a_init_chip(struct hl7139a_chip *chip);

/* Must be called while holding a lock */
static int hl7139a_enable_wdt(struct hl7139a_chip *chip, bool en)
{
	int ret;

	pr_err("%s en:%d\n", __func__, en);

	if (chip->wdt_en == en)
		return 0;
	ret = (en ? hl7139a_clr_bits : hl7139a_set_bits)
		(chip, HL7139A_REG_14, HL7139A_WATCHDOG_DIS);
	if (ret < 0)
		return ret;
	chip->wdt_en = en;
	return 0;
}

static int __hl7139a_get_adc(struct hl7139a_chip *chip,
			    enum hl7139a_adc_channel chan, int *val)
{
	int ret;
	u8 data[2];
	struct hl7139a_desc *desc=chip->desc;

	ret = hl7139a_set_bits(chip, HL7139A_REG_41, 0x00);
	if (ret < 0)
		goto out;
	ret = hl7139a_set_bits(chip, HL7139A_REG_40, HL7139A_ADC_EN_MASK);
	if (ret < 0)
		goto out;
	usleep_range(24000, 30000);
	ret = hl7139a_i2c_read_block(chip, hl7139a_adc_reg[chan], 2, data);
	if (ret < 0)
		goto out_dis;
	switch (chan) {
	case HL7139A_ADC_IBUS:
		*val = ((data[0] << 4) + (data[1] & BITS(3,0))) * 1100;
		break;
	case HL7139A_ADC_VBUS:
		*val = ((data[0] << 4) + (data[1] & BITS(3,0))) * 4000;
		break;
	case HL7139A_ADC_VOUT:
	case HL7139A_ADC_VBAT:
		*val = ((data[0] << 4) + (data[1] & BITS(3,0))) * 1250;
		break;
	case HL7139A_ADC_IBAT:
		if(!desc->ibat_rsense)
			*val = ((data[0] << 4) + (data[1] & BITS(3,0))) * 2200;
		else
			*val = ((data[0] << 4) + (data[1] & BITS(3,0))) * 5500;
		break;
	case HL7139A_ADC_TDIE:
		*val = ((data[0] << 4) + (data[1] & BITS(3,0))) * 625 / 10000;
		break;
	case HL7139A_ADC_TSBUS:
		*val = 25;
		break;
	default:
		ret = -ENOTSUPP;
		break;
	}
	if (ret < 0)
		dev_err(chip->dev, "%s %s fail(%d)\n", __func__,
			hl7139a_adc_name[chan], ret);
	else
		dev_info(chip->dev, "%s %s %d rsense:%d\n", __func__,
			 hl7139a_adc_name[chan], *val,desc->ibat_rsense);
out_dis:
	if (!chip->force_adc_en)
		ret = hl7139a_clr_bits(chip, HL7139A_REG_40,
				      HL7139A_ADC_EN_MASK);
out:
	return ret;
}

static int hl7139a_enable_chg(struct charger_device *chg_dev, bool en)
{
	int ret;
	struct hl7139a_chip *chip = charger_get_data(chg_dev);
	u32 err_check = BIT(HL7139A_IRQIDX_VIN_OVP) |
			BIT(HL7139A_IRQIDX_VBAT_OVP) |
			BIT(HL7139A_IRQIDX_VOUT_OVP) |
			BIT(HL7139A_IRQIDX_IIN_OCP)  |
			BIT(HL7139A_IRQIDX_FET_SHORT) |
			BIT(HL7139A_IRQIDX_CFLY_SHORT);
	u32 stat_check = 0;

	dev_info(chip->dev, "%s %d\n", __func__, en);
	mutex_lock(&chip->adc_lock);

	chip->force_adc_en = en;
	if (!en) {
		ret = hl7139a_clr_bits(chip, HL7139A_REG_12,
				      HL7139A_CHG_EN_MASK);
		if (ret < 0)
			goto out_unlock;
		ret = hl7139a_clr_bits(chip, HL7139A_REG_40,
				      HL7139A_ADC_EN_MASK);
		if (ret < 0)
			goto out_unlock;
		ret = hl7139a_enable_wdt(chip, false);
		goto out_unlock;
	}
	/* Enable ADC to check status before enable charging */
	ret = hl7139a_set_bits(chip, HL7139A_REG_40, HL7139A_ADC_EN_MASK);
	if (ret < 0)
		goto out_unlock;
	mutex_unlock(&chip->adc_lock);
	usleep_range(12000, 15000);

	mutex_lock(&chip->stat_lock);
	__hl7139a_update_status(chip);
	if ((chip->stat & err_check) ||
	    ((chip->stat & stat_check) != stat_check)) {
		dev_err(chip->dev, "%s error(0x%08X,0x%08X,0x%08X)\n", __func__,
			chip->stat, err_check, stat_check);
		ret = -EINVAL;
		mutex_unlock(&chip->stat_lock);
		goto out;
	}

	mutex_unlock(&chip->stat_lock);
	if (!chip->desc->wdt_dis) {
		ret = hl7139a_enable_wdt(chip, true);
		if (ret < 0)
			goto out;
	}
	ret = hl7139a_set_bits(chip, HL7139A_REG_12, HL7139A_CHG_EN_MASK);
	if (ret < 0)
		goto out;

	goto out;
out_unlock:
	mutex_unlock(&chip->adc_lock);
out:
	return ret;
}

static int hl7139a_reset_ucp(struct charger_device *chg_dev)
{
	struct hl7139a_chip *chip = charger_get_data(chg_dev);
	int ret = 0;
	dev_info(chip->dev, "%s %d\n", __func__, __LINE__);
	ret=hl7139a_i2c_update_bits(chip, HL7139A_REG_13, HL7139A_IBUS_UCP_DEB_10MS<<HL7139A_IBUS_UCP_DEB_SHIFT
		,HL7139A_IBUS_UCP_DEB_MASK);
	if (ret < 0)
		return -EINVAL;
	return ret;
}

static int hl7139a_is_chg_enabled(struct charger_device *chg_dev, bool *en)
{
	int ret;
	struct hl7139a_chip *chip = charger_get_data(chg_dev);

	ret = hl7139a_i2c_test_bit(chip, HL7139A_REG_12, HL7139A_CHG_EN_SHIFT,en);
	dev_info(chip->dev, "%s %d, ret(%d)\n", __func__, *en, ret);
	return ret;
}

static inline enum hl7139a_adc_channel to_hl7139a_adc(enum adc_channel chan)
{
	switch (chan) {
	case ADC_CHANNEL_VBUS:
		return HL7139A_ADC_VBUS;
	case ADC_CHANNEL_VBAT:
		return HL7139A_ADC_VBAT;
	case ADC_CHANNEL_IBUS:
		return HL7139A_ADC_IBUS;
	case ADC_CHANNEL_IBAT:
		return HL7139A_ADC_IBAT;
	case ADC_CHANNEL_TEMP_JC:
		return HL7139A_ADC_TDIE;
	case ADC_CHANNEL_VOUT:
		return HL7139A_ADC_VOUT;
    case ADC_CHANNEL_TSBUS:
		return HL7139A_ADC_TSBUS;
	default:
		break;
	}
	return HL7139A_ADC_NOTSUPP;
}

static int hl7139a_get_adc(struct charger_device *chg_dev, enum adc_channel chan,
			  int *min, int *max)
{
	int ret;
	struct hl7139a_chip *chip = charger_get_data(chg_dev);
	enum hl7139a_adc_channel _chan = to_hl7139a_adc(chan);

	if (_chan == HL7139A_ADC_NOTSUPP)
		return -EINVAL;
	mutex_lock(&chip->adc_lock);
	ret = __hl7139a_get_adc(chip, _chan, max);
	if (ret < 0)
		goto out;
	if (min != max)
		*min = *max;
out:
	mutex_unlock(&chip->adc_lock);
	return ret;
}

static int hl7139a_get_adc_accuracy(struct charger_device *chg_dev,
				   enum adc_channel chan, int *min, int *max)
{
	enum hl7139a_adc_channel _chan = to_hl7139a_adc(chan);

	if (_chan == HL7139A_ADC_NOTSUPP)
		return -EINVAL;
	*min = *max = hl7139a_adc_accuracy_tbl[_chan];
	return 0;
}

static int hl7139a_set_vbusovp(struct charger_device *chg_dev, u32 uV)
{
	struct hl7139a_chip *chip = charger_get_data(chg_dev);
	u8 reg = hl7139a_vbusovp_toreg(uV);

	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, uV, reg);
	return hl7139a_i2c_update_bits(chip, HL7139A_REG_0B, reg,
				      HL7139A_AC_OVP_MASK);
}

static int hl7139a_set_vbusovp_alarm(struct charger_device *chg_dev, u32 uV)
{
	return 0;
}

static int hl7139a_set_ibusocp(struct charger_device *chg_dev, u32 uA)
{
	struct hl7139a_chip *chip = charger_get_data(chg_dev);
	u8 reg = hl7139a_ibusocp_toreg(uA);

	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, uA, reg);
	return hl7139a_i2c_update_bits(chip, HL7139A_REG_0E, reg,
				      HL7139A_IBUS_OCP_MASK);
}

static int hl7139a_set_vbatovp(struct charger_device *chg_dev, u32 uV)
{
	struct hl7139a_chip *chip = charger_get_data(chg_dev);
	u8 reg = hl7139a_vbatovp_toreg(uV);

	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, uV, reg);
	return hl7139a_i2c_update_bits(chip, HL7139A_REG_08, reg,
				      HL7139A_VBAT_OVP_MASK);
}

static int hl7139a_set_vbatovp_alarm(struct charger_device *chg_dev, u32 uV)
{
	return 0;
}

static int hl7139a_is_vbuslowerr(struct charger_device *chg_dev, bool *err)
{
	int ret;
	struct hl7139a_chip *chip = charger_get_data(chg_dev);

	mutex_lock(&chip->adc_lock);
	ret = hl7139a_set_bits(chip, HL7139A_REG_40, HL7139A_ADC_EN_MASK);
	if (ret < 0)
		goto out;
	usleep_range(12000, 15000);
	ret = hl7139a_i2c_test_bit(chip, HL7139A_REG_05,
				  HL7139A_TRACK_UV_STS_SHIFT, err);

	if (!chip->force_adc_en)
		hl7139a_clr_bits(chip, HL7139A_REG_40, HL7139A_ADC_EN_MASK);
out:
	mutex_unlock(&chip->adc_lock);
	return ret;
}

static int hl7139a_set_ibatocp(struct charger_device *chg_dev, u32 uA)
{
	struct hl7139a_chip *chip = charger_get_data(chg_dev);
	u8 reg = hl7139a_ibatocp_toreg(uA);

	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, uA, reg);
	return hl7139a_i2c_update_bits(chip, HL7139A_REG_0A, reg,
				      HL7139A_IBAT_OCP_MASK);
}

static ssize_t hl7139a_show_registers(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl7139a_chip *chip = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "hl7139a");
	for (addr = 0x0; addr <= 0x1A; addr++) {
		ret = hl7139a_i2c_read8(chip, addr, &val); 
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

static ssize_t hl7139a_store_register(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hl7139a_chip *chip = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2) {
		ret = hl7139a_i2c_write8(chip, reg, val);
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

static DEVICE_ATTR(hl7139a_registers_debug, 0664, hl7139a_show_registers, hl7139a_store_register);

static int hl7139a_set_ibusucp_en(struct charger_device *chg_dev, bool en)
{
	struct hl7139a_chip *chip = charger_get_data(chg_dev);
	int ret = 0;
	int alias_type = TC_UNKNOWN;

	alias_type = tc_get_alias_type();
	if (alias_type == TC_WIRELESS) {
		ret=hl7139a_i2c_update_bits(chip, HL7139A_REG_13, HL7139A_IBUS_UCP_DEB_500MS<<HL7139A_IBUS_UCP_DEB_SHIFT,HL7139A_IBUS_UCP_DEB_MASK);
	} else {
		ret=hl7139a_i2c_update_bits(chip, HL7139A_REG_13, HL7139A_IBUS_UCP_DEB_10MS<<HL7139A_IBUS_UCP_DEB_SHIFT,HL7139A_IBUS_UCP_DEB_MASK);
	}
	dev_err(chip->dev, "%s: en:%d\n", __func__, en);
	ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_12, (en << HL7139A_IBUS_UCP_DIS_SHIFT),
				    HL7139A_IBUS_UCP_DIS_MASK);

	return ret;
}

static int hl7139a_dump_regs(struct charger_device *chg_dev)
{
	int i = 0,ret=0;
	u8 regval;
	struct hl7139a_chip *chip = charger_get_data(chg_dev);

	for (i = 0; i < ARRAY_SIZE(hl7139a_reg_addr); i++) {
		ret = hl7139a_i2c_read8(chip, hl7139a_reg_addr[i],&regval);
		if (ret < 0) {
			dev_err(chip->dev, "%s read err!ret:%d\n", __func__, ret);
			continue;
		}
		dev_info(chip->dev,"%s reg0x%02X = 0x%02X\n",__func__, hl7139a_reg_addr[i],regval);
	}

	return 0;
}

__maybe_unused static int hl7139a_level_shift_wdt_set(struct charger_device *chg_dev, u8 val)
{
	struct hl7139a_chip *chip = charger_get_data(chg_dev);
	u8 wdt_level = 0;

	switch(val) {
		case LT_WD_5MS:
			wdt_level = LT_WD_5MS;
			break;
		case LT_WD_20MS:
			wdt_level = LT_WD_20MS;
			break;
		case LT_DISABLED:
			wdt_level = LT_DISABLED;
			break;
		default:
			wdt_level = LT_WD_100MS;
			break;
	}

	pr_info("%s wdt_level:%d\n", __func__, wdt_level);

	return hl7139a_i2c_update_bits(chip, HL7139A_REG_1A, (wdt_level << HL7139A_LT_WD_TMR_SHIFT),
				      HL7139A_LT_WD_TMR_MASK);
}

static inline void hl7139a_set_notify(struct hl7139a_chip *chip,
				     enum hl7139a_notify notify)
{
	dev_info(chip->dev, "notify disable = %d\n", chip->notify_disable);
	if(chip->notify_disable)
		return;

	mutex_lock(&chip->notify_lock);
	chip->notify |= BIT(notify);
	mutex_unlock(&chip->notify_lock);
}

static int hl7139a_state_chg_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7139a_reg_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7139a_ts_temp_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s %d\n", __func__,
		 !!(chip->stat & BIT(HL7139A_IRQIDX_TS_TEMP)));
	return 0;
}

static int hl7139a_v_not_ok_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7139a_vin_ovp_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	hl7139a_set_notify(chip, HL7139A_NOTIFY_VBUSOVP);
	return 0;
}

static int hl7139a_vin_uvlo_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7139a_track_ov_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7139a_track_uv_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s %d\n", __func__,
		 !!(chip->stat & BIT(HL7139A_IRQIDX_TRACK_UV)));
	return 0;
}

static int hl7139a_vbat_ovp_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s %d\n", __func__,
		 !!(chip->stat & BIT(HL7139A_IRQIDX_VBAT_OVP)));
	hl7139a_set_notify(chip, HL7139A_NOTIFY_VBATOVP);
	return 0;
}

static int hl7139a_vout_ovp_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	hl7139a_set_notify(chip, HL7139A_NOTIFY_VOUTOVP);
	return 0;
}

static int hl7139a_pmic_qual_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7139a_vubs_uv_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7139a_cur_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s %d\n", __func__,
		 !!(chip->stat & BIT(HL7139A_IRQIDX_CUR)));
	return 0;
}

static int hl7139a_iin_ocp_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s %d\n", __func__,
		 !!(chip->stat & BIT(HL7139A_IRQIDX_IIN_OCP)));
	hl7139a_set_notify(chip, HL7139A_NOTIFY_IBUSOCP);
	return 0;
}

static int hl7139a_ibat_ocp_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	hl7139a_set_notify(chip, HL7139A_NOTIFY_IBATOCP);
	return 0;
}

static int hl7139a_iin_ucp_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	hl7139a_set_notify(chip, HL7139A_NOTIFY_IBUSUCPF);
	return 0;
}

static int hl7139a_short_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7139a_fet_short_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7139a_cfly_short_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7139a_wdog_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7139a_dev_mode_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7139a_thsd_sts_irq_handler(struct hl7139a_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

struct irq_map_desc {
	const char *name;
	int (*hdlr)(struct hl7139a_chip *chip);
	u8 flag_idx;
	u8 stat_idx;
	u8 flag_mask;
	u8 stat_mask;
	u32 irq_idx;
	bool stat_only;
};

#define HL7139A_IRQ_DESC(_name, _flag_i, _stat_i, _flag_s, _stat_s, _irq_idx, \
			_stat_only) \
	{.name = #_name, .hdlr = hl7139a_##_name##_irq_handler, \
	 .flag_idx = _flag_i, .stat_idx = _stat_i, \
	 .flag_mask = (1 << _flag_s), .stat_mask = (1 << _stat_s), \
	 .irq_idx = _irq_idx, .stat_only = _stat_only}

#define HL7139A_IRQ_DESC_2(_name, _flag_i, _stat_i, _flag_s, _stat_s, _irq_idx, \
			_stat_only) \
	{.name = #_name, .hdlr = hl7139a_##_name##_irq_handler, \
	 .flag_idx = _flag_i, .stat_idx = _stat_i, \
	 .flag_mask = _flag_s, .stat_mask = _stat_s, \
	 .irq_idx = _irq_idx, .stat_only = _stat_only}
/*
 * RSS: Reister index of flag, Shift of flag, Shift of state
 * RRS: Register index of flag, Register index of state, Shift of flag
 * RS: Register index of flag, Shift of flag
 * RSSO: Register index of state, Shift of state, State Only
 */
#define HL7139A_IRQ_DESC_RSS(_name, _flag_i, _flag_s, _stat_s, _irq_idx) \
	HL7139A_IRQ_DESC(_name, _flag_i, _flag_i, _flag_s, _stat_s, _irq_idx, \
			false)

#define HL7139A_IRQ_DESC_RRS(_name, _flag_i, _stat_i, _flag_s, _irq_idx) \
	HL7139A_IRQ_DESC(_name, _flag_i, _stat_i, _flag_s, _flag_s, _irq_idx, \
			false)

#define HL7139A_IRQ_DESC_RS(_name, _flag_i, _flag_s, _irq_idx) \
	HL7139A_IRQ_DESC(_name, _flag_i, _flag_i, _flag_s, _flag_s, _irq_idx, \
			false)

#define HL7139A_IRQ_DESC_RSSO(_name, _flag_i, _flag_s, _irq_idx) \
	HL7139A_IRQ_DESC(_name, _flag_i, _flag_i, _flag_s, _flag_s, _irq_idx, \
			true)

static const struct irq_map_desc hl7139a_irq_map_tbl[HL7139A_IRQIDX_MAX] = {
	HL7139A_IRQ_DESC_2(state_chg_sts, HL7139A_SF_INT, HL7139A_SF_INT_STS_A, 1 << 7, 3 << 6,
			HL7139A_IRQIDX_STATE_CHG, false),
	HL7139A_IRQ_DESC_2(reg_sts, HL7139A_SF_INT, HL7139A_SF_INT_STS_A, 1 << 6, 15 << 2,
			HL7139A_IRQIDX_REG, false),
	HL7139A_IRQ_DESC(ts_temp_sts, HL7139A_SF_INT, HL7139A_SF_INT_STS_A, 5, 1,
			HL7139A_IRQIDX_TS_TEMP, false),

	HL7139A_IRQ_DESC(v_not_ok_sts, HL7139A_SF_INT, HL7139A_SF_INT_STS_B, 4, 4,
			HL7139A_IRQIDX_V_NOT_OK, false),
	HL7139A_IRQ_DESC(vin_ovp_sts, HL7139A_SF_STATUS_A, HL7139A_SF_STATUS_A, 7, 7,
			HL7139A_IRQIDX_VIN_OVP, false),
	HL7139A_IRQ_DESC(vin_uvlo_sts, HL7139A_SF_STATUS_A, HL7139A_SF_STATUS_A, 6, 6,
			HL7139A_IRQIDX_VIN_UVLO, false),
	HL7139A_IRQ_DESC(track_ov_sts, HL7139A_SF_STATUS_A, HL7139A_SF_STATUS_A, 5, 5,
			HL7139A_IRQIDX_TRACK_OV, false),
	HL7139A_IRQ_DESC(track_uv_sts, HL7139A_SF_STATUS_A, HL7139A_SF_STATUS_A, 4, 4,
			HL7139A_IRQIDX_TRACK_UV, false),
	HL7139A_IRQ_DESC(vbat_ovp_sts, HL7139A_SF_STATUS_A, HL7139A_SF_STATUS_A, 3, 3,
			HL7139A_IRQIDX_VBAT_OVP, false),
	HL7139A_IRQ_DESC(vout_ovp_sts, HL7139A_SF_STATUS_A, HL7139A_SF_STATUS_A, 2, 2,
			HL7139A_IRQIDX_VOUT_OVP, false),
	HL7139A_IRQ_DESC(pmic_qual_sts, HL7139A_SF_STATUS_A, HL7139A_SF_STATUS_A, 1, 1,
			HL7139A_IRQIDX_PMID_QUAL, false),
	HL7139A_IRQ_DESC(vubs_uv_sts, HL7139A_SF_STATUS_A, HL7139A_SF_STATUS_A, 0, 0,
			HL7139A_IRQIDX_VBUS_UV, false),

	HL7139A_IRQ_DESC(cur_sts, HL7139A_SF_INT, HL7139A_SF_INT_STS_B, 3, 3,
			HL7139A_IRQIDX_CUR, false),
	HL7139A_IRQ_DESC(iin_ocp_sts, HL7139A_SF_STATUS_B, HL7139A_SF_STATUS_B, 7, 7,
			HL7139A_IRQIDX_IIN_OCP, false),
	HL7139A_IRQ_DESC(ibat_ocp_sts, HL7139A_SF_STATUS_B, HL7139A_SF_STATUS_B, 6, 6,
			HL7139A_IRQIDX_IBAT_OCP, false),
	HL7139A_IRQ_DESC(iin_ucp_sts, HL7139A_SF_STATUS_B, HL7139A_SF_STATUS_B, 5, 5,
			HL7139A_IRQIDX_IIN_UCP, false),

	HL7139A_IRQ_DESC(short_sts, HL7139A_SF_INT, HL7139A_SF_INT_STS_B, 2, 2,
			HL7139A_IRQIDX_SHORT, false),
	HL7139A_IRQ_DESC(fet_short_sts, HL7139A_SF_STATUS_B, HL7139A_SF_STATUS_B, 4, 4,
			HL7139A_IRQIDX_TRACK_UV, false),
	HL7139A_IRQ_DESC(cfly_short_sts, HL7139A_SF_STATUS_B, HL7139A_SF_STATUS_B, 3, 3,
			HL7139A_IRQIDX_CFLY_SHORT, false),

	HL7139A_IRQ_DESC(wdog_sts, HL7139A_SF_INT, HL7139A_SF_INT_STS_B, 1, 1,
			HL7139A_IRQIDX_WDOG, false),
	HL7139A_IRQ_DESC_2(dev_mode_sts, HL7139A_SF_STATUS_B, HL7139A_SF_STATUS_B, 3 << 1, 3 << 1,
			HL7139A_IRQIDX_DEV_MODE, false),
	HL7139A_IRQ_DESC(thsd_sts, HL7139A_SF_STATUS_B, HL7139A_SF_STATUS_B, 0, 0,
			HL7139A_IRQIDX_THSD, false),
};

static int __hl7139a_update_status(struct hl7139a_chip *chip)
{
	int i;
	u8 sf[HL7139A_SF_MAX] = {0};
	const struct irq_map_desc *desc;

	for (i = 0; i < HL7139A_SF_MAX; i++){
		hl7139a_i2c_read8(chip, hl7139a_reg_sf[i], &sf[i]);
		dev_info(chip->dev, "%s sf[0x%x]=0x%x\n", __func__,hl7139a_reg_sf[i],sf[i]);
	}

	for (i = 0; i < ARRAY_SIZE(hl7139a_irq_map_tbl); i++) {
		desc = &hl7139a_irq_map_tbl[i];
		if (sf[desc->flag_idx] & desc->flag_mask) {
			if (!desc->stat_only)
				chip->flag |= BIT(desc->irq_idx);
		}
		if (sf[desc->stat_idx] & desc->stat_mask) {
			if (desc->stat_only &&
			    !(chip->stat & BIT(desc->irq_idx)))
				chip->flag |= BIT(desc->irq_idx);
			chip->stat |= BIT(desc->irq_idx);
		} else {
			if (desc->stat_only &&
			    (chip->stat & BIT(desc->irq_idx)))
				chip->flag |= BIT(desc->irq_idx);
			chip->stat &= ~BIT(desc->irq_idx);
		}
	}

	//hl7139a_dump_regs(chip);
	return 0;
}

static int __maybe_unused hl7139a_update_status(struct hl7139a_chip *chip)
{
	int ret;

	mutex_lock(&chip->stat_lock);
	ret = __hl7139a_update_status(chip);
	mutex_unlock(&chip->stat_lock);
	return ret;
}

static int hl7139a_notify_task_threadfn(void *data)
{
	int i;
	struct hl7139a_chip *chip = data;

	while (!kthread_should_stop()) {
		wait_event_interruptible(chip->wq, chip->notify != 0 ||
					 kthread_should_stop());
		if (kthread_should_stop())
			goto out;
		pm_stay_awake(chip->dev);
		mutex_lock(&chip->notify_lock);
		for (i = 0; i < HL7139A_NOTIFY_MAX; i++) {
			if (chip->notify & BIT(i)) {
				chip->notify &= ~BIT(i);
				mutex_unlock(&chip->notify_lock);
				charger_dev_notify(chip->chg_dev,
						   hl7139a_chgdev_notify_map[i]);
				mutex_lock(&chip->notify_lock);
			}
		}
		mutex_unlock(&chip->notify_lock);
		pm_relax(chip->dev);
	}
out:
	return 0;
}

static void hl7139a_state_update_handler(struct work_struct *data)
{
	int i;
	const struct irq_map_desc *desc;
	struct hl7139a_chip *chip = container_of(data,struct hl7139a_chip,state_update_work);
	pm_stay_awake(chip->dev);
	mutex_lock(&chip->stat_lock);
	__hl7139a_update_status(chip);
	for (i = 0; i < ARRAY_SIZE(hl7139a_irq_map_tbl); i++) {
		desc = &hl7139a_irq_map_tbl[i];
		if ((chip->flag & (1 << desc->irq_idx)) && desc->hdlr)
			desc->hdlr(chip);
	}
	chip->flag = 0;
	wake_up_interruptible(&chip->wq);
	mutex_unlock(&chip->stat_lock);
	pm_relax(chip->dev);
}

static irqreturn_t hl7139a_irq_handler(int irq, void *data)
{
	struct hl7139a_chip *chip = data;

	schedule_work(&chip->state_update_work);

	return IRQ_HANDLED;
}

static int hl7139a_en_rfc_detect(struct hl7139a_chip *chip)
{
	int ret = 0;
	struct hl7139a_desc *desc = chip->desc;
	ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_19, (HL7139A_QC20_DET_DIS << HL7139A_QC20_DET_EN_SHIFT),
				      HL7139A_QC20_DET_EN_MASK);
	ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_1A, (desc->lt_wd_tmr << HL7139A_LT_WD_TMR_SHIFT), HL7139A_LT_WD_TMR_MASK);
	ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_1A, (1 << HL7139A_I2C_VIL_UP_SHIFT), HL7139A_I2C_VIL_UP_MASK);
	ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_16, (HL7139A_PMID2OUT_OVP_900MV << HL7139A_PMID2OUT_OVP_SHIFT),
				      HL7139A_PMID2OUT_OVP_MASK);
	ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_16, (HL7139A_PMID2OUT_UVP_400MV << HL7139A_PMID2OUT_UVP_SHIFT),
				      HL7139A_PMID2OUT_UVP_MASK);
	ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_19, (1 << HL7139A_TECN_EN_SHIFT),
				      HL7139A_TECN_EN_MASK);
	ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_1A, (1 << HL7139A_TECN_DET_MAN_SHIFT),
				      HL7139A_TECN_DET_MAN_MASK);
	return ret;
}

static int hl7139a_rfc_reset(struct charger_device *chg_dev)
{
	struct hl7139a_chip *chip = charger_get_data(chg_dev);
	int ret = 0;
	dev_info(chip->dev, "%s %d\n", __func__, __LINE__);
	if(atomic_read(&chip->reset_flag)){
		return 0;
	}
	atomic_set(&chip->reset_flag,1);
	ret = hl7139a_i2c_write8(chip, HL7139A_REG_19, 0xFA);
	ret = hl7139a_i2c_write8(chip, HL7139A_REG_19, 0x02);
	ret = hl7139a_i2c_write8(chip, HL7139A_REG_1A, 0x10);
	ret = hl7139a_i2c_write8(chip, HL7139A_REG_40, 0x00);
	atomic_set(&chip->reset_flag,0);
	return ret;
}

static int __hl7139a_set_dp_dm(struct hl7139a_chip *chip, enum dpdm_ctrl_status dp_status, 
	enum dpdm_ctrl_status dm_status, bool en_rfc_detect)
{
	int ret = 0;

	if((dp_status == DPDM_CTRL_HZ) && (dm_status == DPDM_CTRL_HZ) && chip->chg_dev) {
		ret = hl7139a_rfc_reset(chip->chg_dev);
		return ret;
	}

	if (en_rfc_detect) {
		hl7139a_en_rfc_detect(chip);
	}

	switch (dp_status) {
		case DPDM_CTRL_HZ:
			ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_19, (HL7139A_FORCE_DP_HIZ << HL7139A_FORCE_DP_SHIFT),
				      HL7139A_FORCE_DP_MASK);
			break;
		case DPDM_CTRL_0V:
			ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_19, (HL7139A_FORCE_DP_0V << HL7139A_FORCE_DP_SHIFT),
				      HL7139A_FORCE_DP_MASK);
			break;
		case DPDM_CTRL_0_6V:
			ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_19, (HL7139A_FORCE_DP_0_6V << HL7139A_FORCE_DP_SHIFT),
				      HL7139A_FORCE_DP_MASK);
			break;
		case DPDM_CTRL_3_3V:
			ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_19, (HL7139A_COMP_TH_0_8V << HL7139A_COMP_TH_SHIFT),
				      HL7139A_COMP_TH_MASK);
			ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_19, (HL7139A_FORCE_DP_3_3V << HL7139A_FORCE_DP_SHIFT),
				      HL7139A_FORCE_DP_MASK);
			break;
		default:
			break;
	}

	switch (dm_status) {
		case DPDM_CTRL_HZ:
			ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_19, (HL7139A_FORCE_DM_HIZ << HL7139A_FORCE_DM_SHIFT),
				      HL7139A_FORCE_DM_MASK);
			break;
		case DPDM_CTRL_0V:
			ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_19, (HL7139A_FORCE_DM_0V << HL7139A_FORCE_DM_SHIFT),
				      HL7139A_FORCE_DM_MASK);
			break;
		case DPDM_CTRL_0_6V:
			ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_19, (HL7139A_FORCE_DM_0_6V << HL7139A_FORCE_DM_SHIFT),
				      HL7139A_FORCE_DM_MASK);
			break;
		case DPDM_CTRL_3_3V:
			ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_19, (HL7139A_FORCE_DM_3_3V << HL7139A_FORCE_DM_SHIFT),
				      HL7139A_FORCE_DM_MASK);
			break;
		default:
			break;
	}
	return ret;
}

static int hl7139a_set_dp_dm(struct charger_device *chg_dev, enum dpdm_ctrl_status dp_status,
	enum dpdm_ctrl_status dm_status, bool en_rfc_detect)
{
	struct hl7139a_chip *chip = charger_get_data(chg_dev);
	int ret = 0;
	dev_info(chip->dev, "%s\n", __func__);
	__hl7139a_set_dp_dm(chip, dp_status, dm_status, en_rfc_detect);

	dev_info(chip->dev, "%s %d/%d\n", __func__, dp_status, dm_status);
	return ret;
}

static int hl7139a_get_dp_dm(struct charger_device *chg_dev, bool dp)
{
	struct hl7139a_chip *chip = charger_get_data(chg_dev);
	int ret = 0;
	u8 data, data1;
	ret = hl7139a_i2c_read8(chip, HL7139A_REG_1A, &data);
	if (ret < 0)
		return ret;

	if (dp) {
		ret = ((0x02 & data) >> 1);
	} else {
		ret = (0x1) & data;
	}

	hl7139a_i2c_read8(chip, HL7139A_REG_19, &data1);
	
	dev_info(chip->dev, "%s %s:%d reg[19]:0x%x reg[1A]:0x%x\n", __func__, dp ? "dp": "dm", ret, data1, data);
	return ret;
}

static int hl7139a_i2c_trans(struct charger_device *chg_dev, bool high)
{
	struct hl7139a_chip *chip = charger_get_data(chg_dev);
	int ret = 0;

	dev_info(chip->dev, "%s high:%d\n", __func__, high);

	if(high) {
#if 0
		if (chip->rfc_control_type == 3)
			ret = __hl7139a_i2c_write8(chip, HL7139A_REG_19, 0xFA);
		else
#endif
		ret = __hl7139a_i2c_write8(chip, HL7139A_REG_19, 0xFE);
		mutex_lock(&chip->io_lock);
	} else {
		ret = __hl7139a_i2c_write8(chip, HL7139A_REG_19, 0xFA);
		mutex_unlock(&chip->io_lock);
	}
	udelay(100);
	return ret;
}

static int hl7139a_adc_init(struct charger_device *chg_dev, bool en)
{
	int ret = 0;
#if 0
	struct hl7139a_chip *chip = charger_get_data(chg_dev);
	dev_err(chip->dev, "%s en=%d\n", __func__,en);

	if (en) {
		ret = hl7139a_i2c_write8(chip, HL7139A_REG_40, 0x01);
		if (ret < 0)
			goto out;
		usleep_range(120000, 140000);
	} else {
		ret = hl7139a_i2c_write8(chip, HL7139A_REG_40, 0x0);
		if (ret < 0)
			goto out;
	}
out:
#endif
	return ret;
}

static int hl7139a_init_chip(struct charger_device *chg_dev)
{
	struct hl7139a_chip *chip = charger_get_data(chg_dev);

	__hl7139a_init_chip(chip);

	return 0;
}

static int hl7139a_set_run_spec(struct charger_device *chg_dev, u32 run_spec)
{
	//struct hl7139a_chip *chip = charger_get_data(chg_dev);
	return 0;
}

static int hl7139a_disable_otg_step(struct charger_device *chg_dev,  bool en)
{
 #if IS_ENABLED(CONFIG_TC_WIRELESS_CHARGER)
	struct hl7139a_chip *chip = charger_get_data(chg_dev);
	pr_info("%s en:%d\n", __func__, en);

	if (IS_ERR_OR_NULL(chip ->wls_dev)) {
		chip ->wls_dev = get_charger_by_name("wireless_manager");
		if (IS_ERR_OR_NULL(chip ->wls_dev)) {
			dev_err(chip ->dev, "can't find wireless charger device ***\n");
			return false;
		}
	}

	if (chip->wls_dev->ops->otg_mosfet_ctl) {
		if(en==false)
			msleep(80);
		chip->wls_dev->ops->otg_mosfet_ctl(chip->wls_dev, en);
	}
#endif

	return 0;
}

static int hl7139a_vctr_open_delay(struct charger_device *chg_dev, bool fast)
{
	struct hl7139a_chip *chip = charger_get_data(chg_dev);
	int ret=0;

	pr_info("%s fast:%d\n", __func__, fast);
	if (fast) {
		ret=hl7139a_i2c_write8(chip, 0xa0, 0xf9);
		if (ret < 0) {
			dev_err(chip->dev, "%s Failed to write register\n",__func__);
			return ret;
		}
		ret=hl7139a_i2c_write8(chip, 0xa0, 0x9f);
		if (ret < 0) {
			dev_err(chip->dev, "%s Failed to write register\n",__func__);
			return ret;
		}
		ret=hl7139a_i2c_write8(chip, 0xaa, 0x80);
		if (ret < 0) {
			dev_err(chip->dev, "%s Failed to write register\n",__func__);
			return ret;
		}
		ret=hl7139a_i2c_write8(chip, 0xa0, 0x00);
		if (ret < 0) {
			dev_err(chip->dev, "%s Failed to write register\n",__func__);
			return ret;
		}
	} else {
		ret=hl7139a_i2c_write8(chip, 0xa0, 0xf9);
		if (ret < 0) {
			dev_err(chip->dev, "%s Failed to write register\n",__func__);
			return ret;
		}
		ret=hl7139a_i2c_write8(chip, 0xa0, 0x9f);
		if (ret < 0) {
			dev_err(chip->dev, "%s Failed to write register\n",__func__);
			return ret;
		}
		ret=hl7139a_i2c_write8(chip, 0xaa, 0x00);
		if (ret < 0) {
			dev_err(chip->dev, "%s Failed to write register\n",__func__);
			return ret;
		}
		ret=hl7139a_i2c_write8(chip, 0xa0, 0x00);
		if (ret < 0) {
			dev_err(chip->dev, "%s Failed to write register\n",__func__);
			return ret;
		}
	}

	return 0;

}

static const struct charger_ops hl7139a_chg_ops = {
	.enable = hl7139a_enable_chg,
	.is_enabled = hl7139a_is_chg_enabled,
	.get_adc = hl7139a_get_adc,
	.set_vbusovp = hl7139a_set_vbusovp,
	.set_ibusocp = hl7139a_set_ibusocp,
	.set_vbatovp = hl7139a_set_vbatovp,
	.set_ibatocp = hl7139a_set_ibatocp,
	.set_ibusucp_enable = hl7139a_set_ibusucp_en,

	.init_chip = hl7139a_init_chip,
	.set_vbatovp_alarm = hl7139a_set_vbatovp_alarm,
	.set_vbusovp_alarm = hl7139a_set_vbusovp_alarm,

	.is_vbuslowerr = hl7139a_is_vbuslowerr,
	.get_adc_accuracy = hl7139a_get_adc_accuracy,

	.set_dp_dm = hl7139a_set_dp_dm,
	.get_dp_dm = hl7139a_get_dp_dm,
	.i2c_trans = hl7139a_i2c_trans,
	.init_adc = hl7139a_adc_init,
	.soft_reset = hl7139a_rfc_reset,
	.reset_ucp=hl7139a_reset_ucp,
	.dump_registers = hl7139a_dump_regs,
	.set_run_spec = hl7139a_set_run_spec,
	.enable_otg = hl7139a_vctr_open_delay,
	.disable_otg_step =hl7139a_disable_otg_step,
	/* .level_shift_wdt_set = hl7139a_level_shift_wdt_set, */
};

static int hl7139a_register_chgdev(struct hl7139a_chip *chip)
{
	chip->chg_prop.alias_name = chip->desc->chg_name;
	chip->chg_dev = charger_device_register(chip->desc->chg_name, chip->dev,
						chip, &hl7139a_chg_ops,
						&chip->chg_prop);
	if (!chip->chg_dev)
		return -EINVAL;
	return 0;
}

static int hl7139a_clearall_irq(struct hl7139a_chip *chip)
{
	int i, ret;
	u8 data;

	for (i = 0; i < HL7139A_SF_MAX; i++) {
		ret = hl7139a_i2c_read8(chip, hl7139a_reg_sf[i], &data);
		if (ret < 0)
			return ret;
	}
	return 0;
}

#if IS_ENABLED(CONFIG_TRAN_AW95016)
static void hl7139a_irq_handler_work(struct work_struct *work)
{
	struct hl7139a_chip *chip = NULL;

	pr_info("%s\n", __func__);

	if(IS_ERR_OR_NULL(g_chip)){
		pr_err("%s g_chip is NULL\n", __func__);
		return;
	}

	chip = g_chip;
	schedule_work(&chip->state_update_work);
}
#endif

static int hl7139a_init_irq(struct hl7139a_chip *chip)
{
	int ret = 0, len = 0;
	char *name = NULL;

	dev_info(chip->dev, "%s\n", __func__);
	ret = hl7139a_clearall_irq(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s clr all irq fail(%d)\n", __func__, ret);
		return ret;
	}
	if (chip->type == HL7139A_TYPE_SLAVE)
		return 0;

#if IS_ENABLED(CONFIG_TRAN_AW95016)
	if(!(chip->board_id == HL7139A_BOARD_H833C && !strcmp(chip->desc->chg_name,"secondary_dvchg")))
#endif
	{
		len = strlen(chip->desc->chg_name);
		chip->irq = gpiod_to_irq(chip->irq_gpio);
		if (chip->irq < 0) {
			dev_err(chip->dev, "%s irq mapping fail(%d)\n", __func__,
				chip->irq);
			return ret;
		}
		dev_info(chip->dev, "%s irq = %d\n", __func__, chip->irq);

		/* Request threaded IRQ */
		name = devm_kzalloc(chip->dev, len + 5, GFP_KERNEL);
		snprintf(name, len + 5, "%s_irq", chip->desc->chg_name);
		ret = devm_request_threaded_irq(chip->dev, chip->irq, NULL,
			hl7139a_irq_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT, name,
			chip);
		if (ret < 0) {
			dev_err(chip->dev, "%s request thread irq fail(%d)\n", __func__,
				ret);
			return ret;
		}
	}

#if IS_ENABLED(CONFIG_TRAN_AW95016)
	if(chip->board_id == HL7139A_BOARD_H833C && !strcmp(chip->desc->chg_name,"secondary_dvchg"))
	{
		aw95016_set_dir(P1_1, AW95016_GPIO_INPUT);
		aw95016_pull_enable(P1_1, ENBALE);
		aw95016_set_pull_mode(P1_1,PULL_UP);

		aw95016_register_irq(P1_1, hl7139a_irq_handler_work);
	 	/* enable irq */
	 	aw95016_irq_enable(P1_1, 1);
		dev_info(chip->dev, "%s H833C init successfully\n", __func__);
	}
#endif

	device_init_wakeup(chip->dev, true);
	return 0;
}

#define HL7139A_DT_VALPROP(name, reg, shft, mask, func, base) \
	{#name, offsetof(struct hl7139a_desc, name), reg, shft, mask, func, base}

struct hl7139a_dtprop {
	const char *name;
	size_t offset;
	u8 reg;
	u8 shft;
	u8 mask;
	u8 (*toreg)(u32 val);
	u8 base;
};

static inline void hl7139a_parse_dt_u32(struct device_node *np, void *desc,
				       const struct hl7139a_dtprop *props,
				       int prop_cnt)
{
	int i;

	for (i = 0; i < prop_cnt; i++) {
		if (unlikely(!props[i].name))
			continue;
		of_property_read_u32(np, props[i].name, desc + props[i].offset);
		pr_info("%s:%s=%d\n", __func__,props[i].name,*(u32 *)(desc + props[i].offset));
	}
}

static inline void hl7139a_parse_dt_bool(struct device_node *np, void *desc,
					const struct hl7139a_dtprop *props,
					int prop_cnt)
{
	int i;

	for (i = 0; i < prop_cnt; i++) {
		if (unlikely(!props[i].name))
			continue;
		*((bool *)(desc + props[i].offset)) =
			of_property_read_bool(np, props[i].name);
	}
}

static inline int hl7139a_apply_dt(struct hl7139a_chip *chip, void *desc,
				  const struct hl7139a_dtprop *props,
				  int prop_cnt)
{
	int i, ret;
	u32 val;

	for (i = 0; i < prop_cnt; i++) {
		val = *(u32 *)(desc + props[i].offset);
		if (props[i].toreg)
			val = props[i].toreg(val);
		val += props[i].base;

		pr_info("%s:%s 0x%x=%d shft=%d mask=0x%x\n", __func__,props[i].name,props[i].reg,val,props[i].shft,props[i].mask);
		ret = hl7139a_i2c_update_bits(chip, props[i].reg,
					     val << props[i].shft,
					     props[i].mask);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static const struct hl7139a_dtprop hl7139a_dtprops_u32[] = {
	HL7139A_DT_VALPROP(vbatovp, HL7139A_REG_08, 0, 0x3f,hl7139a_vbatovp_toreg, 0),
	HL7139A_DT_VALPROP(ibatocp, HL7139A_REG_0A, 0, 0x3f,hl7139a_ibatocp_toreg, 0),
	HL7139A_DT_VALPROP(vbusovp, HL7139A_REG_0B, 0, 0x0f,hl7139a_vbusovp_toreg, 0),
	HL7139A_DT_VALPROP(vinovp, HL7139A_REG_0C, 0, 0x0f,hl7139a_vinovp_toreg, 0),
	HL7139A_DT_VALPROP(ibusocp, HL7139A_REG_0E, 0, 0x3f,hl7139a_ibusocp_toreg, 0),
	HL7139A_DT_VALPROP(wdt, HL7139A_REG_14, 0, 0x07,hl7139a_wdt_toreg, 0),
	HL7139A_DT_VALPROP(ibat_rsense, HL7139A_REG_15, 6, 0x40, NULL, 0),
	//HL7139A_DT_VALPROP(i2c_vil_up, HL7139A_REG_1A, 4, 0x10, NULL, 0),
	HL7139A_DT_VALPROP(lt_wd_tmr, HL7139A_REG_1A, 6, 0xC0, NULL, 0),
	HL7139A_DT_VALPROP(gpp_conf, HL7139A_REG_15, 0, 0x03, NULL, 0),
	HL7139A_DT_VALPROP(fsw_val, HL7139A_REG_12, 3, 0x78, NULL, 0),
};

static const struct hl7139a_dtprop hl7139a_dtprops_bool[] = {
	HL7139A_DT_VALPROP(state_chg_i_masked, HL7139A_REG_02, 7, 0x80, NULL, 0),
	HL7139A_DT_VALPROP(reg_i_masked, HL7139A_REG_02, 6, 0x40, NULL, 0),
	HL7139A_DT_VALPROP(ts_temp_i_masked, HL7139A_REG_02, 5, 0x20, NULL, 0),
	HL7139A_DT_VALPROP(v_ok_i_masked, HL7139A_REG_02, 4, 0x10, NULL, 0),
	HL7139A_DT_VALPROP(cur_i_masked, HL7139A_REG_02, 3, 0x08, NULL, 0),
	HL7139A_DT_VALPROP(short_i_masked, HL7139A_REG_02, 2, 0x04, NULL, 0),
	HL7139A_DT_VALPROP(wdog_i_masked, HL7139A_REG_02, 1, 0x02, NULL, 0),
	HL7139A_DT_VALPROP(protocol_i_masked, HL7139A_REG_02, 0, 0x01, NULL, 0),
	HL7139A_DT_VALPROP(charge_mode, HL7139A_REG_15, 7, 0x80, NULL, 0),
	HL7139A_DT_VALPROP(vbat_reg_dis, HL7139A_REG_11, 7, 0x80, NULL, 0),
	HL7139A_DT_VALPROP(ibat_reg_dis, HL7139A_REG_11, 6, 0x40, NULL, 0),
	HL7139A_DT_VALPROP(iin_reg_dis, HL7139A_REG_10, 6, 0x40, NULL, 0),
	HL7139A_DT_VALPROP(tdie_reg_dis, HL7139A_REG_10, 7, 0x80, NULL, 0),
	HL7139A_DT_VALPROP(vbatovp_dis, HL7139A_REG_08, 7, 0x80, NULL, 0),
	HL7139A_DT_VALPROP(ibatocp_dis, HL7139A_REG_0A, 7, 0x80, NULL, 0),
	HL7139A_DT_VALPROP(ibusocp_dis, HL7139A_REG_0E, 7, 0x80, NULL, 0),
	HL7139A_DT_VALPROP(track_ov_dis, HL7139A_REG_16, 7, 0x80, NULL, 0),
	HL7139A_DT_VALPROP(track_uv_dis, HL7139A_REG_16, 6, 0x40, NULL, 0),
	HL7139A_DT_VALPROP(wdt_dis, HL7139A_REG_14, 3, 0x08, NULL, 0),
	//HL7139A_DT_VALPROP(tdieotp_dis, HL7139A_REG_CHGCTRL1, 0, 0x01, NULL, 0),
	HL7139A_DT_VALPROP(voutovp_dis, HL7139A_REG_13, 5, 0x20, NULL, 0),
	HL7139A_DT_VALPROP(ibusadc_dis, HL7139A_REG_41, 6, 0x40, NULL, 0),
	HL7139A_DT_VALPROP(tdieadc_dis, HL7139A_REG_41, 2, 0x04, NULL, 0),
	//HL7139A_DT_VALPROP(tsbatadc_dis, HL7139A_REG_ADCEN, 1, 0x02, NULL, 0),
	//HL7139A_DT_VALPROP(tsbusadc_dis, HL7139A_REG_ADCEN, 2, 0x04, NULL, 0),
	HL7139A_DT_VALPROP(ibatadc_dis, HL7139A_REG_41, 4, 0x10, NULL, 0),
	HL7139A_DT_VALPROP(vbatadc_dis, HL7139A_REG_41, 5, 0x20, NULL, 0),
	HL7139A_DT_VALPROP(voutadc_dis, HL7139A_REG_41, 1, 0x02, NULL, 0),
	//HL7139A_DT_VALPROP(vacadc_dis, HL7139A_REG_ADCEN, 6, 0x40, NULL, 0),
	HL7139A_DT_VALPROP(vbusadc_dis, HL7139A_REG_41, 7, 0x80, NULL, 0),
};

#if IS_ENABLED(CONFIG_TRAN_AW95016)
static int hw_is_h833c(void)
{
        struct device_node *np;
        const char *cmd_line;
		char *boot_param = "ae10_hw_h833c";
        int ret = 0;

        np = of_find_node_by_path("/chosen");
        if (!np) {
                pr_err("Can't get the /chosen\n");
                return -EIO;
        }

        ret = of_property_read_string(np, "bootargs", &cmd_line);
        if (ret < 0) {
                pr_err("Can't get the bootargs\n");
                return ret;
        }

        if(strstr(cmd_line, boot_param)){
			pr_info("hw is h833c\n");
			return 1;
        } else {
			pr_info("hw is not h833c\n");
            return 0;
        }
}
#endif

static int hl7139a_parse_dt(struct hl7139a_chip *chip)
{
	struct hl7139a_desc *desc;
	struct device_node *np = chip->dev->of_node;
	struct device_node *child_np;

	if (!np)
		return -ENODEV;
#if IS_ENABLED(CONFIG_TRAN_AW95016)
	if(hw_is_h833c())
		chip->board_id = HL7139A_BOARD_H833C;
	else
		chip->board_id = HL7139A_BOARD_H833;

	dev_info(chip->dev, "%s board_id(%d)\n", __func__,chip->board_id);
#endif

	child_np = of_get_child_by_name(np, hl7139a_type_name[chip->type]);
	if (!child_np) {
		dev_err(chip->dev, "%s no node(%s) found\n", __func__,
			hl7139a_type_name[chip->type]);
		return -ENODEV;
	}

	desc = devm_kzalloc(chip->dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	memcpy(desc, &hl7139a_desc_defval, sizeof(*desc));

	if (of_property_read_string(child_np, "chg_name", &desc->chg_name) < 0)
		dev_info(chip->dev, "%s no chg name\n", __func__);

	if (chip->type == HL7139A_TYPE_SLAVE)
		goto ignore_intr;

#if IS_ENABLED(CONFIG_TRAN_AW95016)
	if(!(chip->board_id == HL7139A_BOARD_H833C && !strcmp(desc->chg_name,"secondary_dvchg")))
#endif
	{
		chip->irq_gpio = devm_gpiod_get(chip->dev, "hl7139a,intr", GPIOD_IN);
		if (IS_ERR(chip->irq_gpio))
			return PTR_ERR(chip->irq_gpio);
	}

ignore_intr:
	if (of_property_read_string(np, "rm_name", &desc->rm_name) < 0)
		dev_info(chip->dev, "%s no rm name\n", __func__);
	if (of_property_read_u8(np, "rm_slave_addr", &desc->rm_slave_addr) < 0)
		dev_info(chip->dev, "%s no regmap slave addr\n", __func__);

	chip->notify_disable = of_property_read_bool(child_np, "notify_disable");

	hl7139a_parse_dt_u32(child_np, (void *)desc, hl7139a_dtprops_u32,
			    ARRAY_SIZE(hl7139a_dtprops_u32));
	hl7139a_parse_dt_bool(child_np, (void *)desc, hl7139a_dtprops_bool,
			     ARRAY_SIZE(hl7139a_dtprops_bool));
	chip->desc = desc;
	dev_info(chip->dev, "%s chg_name:%s\n", __func__, desc->chg_name);
	return 0;
}

static int __hl7139a_init_chip(struct hl7139a_chip *chip)
{
	int ret;

	dev_info(chip->dev, "%s\n", __func__);
	ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_19, (HL7139A_QC20_DET_DIS << HL7139A_QC20_DET_EN_SHIFT),
				      HL7139A_QC20_DET_EN_MASK);
#if 0
	ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_1A, (LT_WD_100MS << HL7139A_LT_WD_TMR_SHIFT),
				      HL7139A_LT_WD_TMR_MASK);
#endif
	ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_16, (HL7139A_PMID2OUT_OVP_900MV << HL7139A_PMID2OUT_OVP_SHIFT),
				      HL7139A_PMID2OUT_OVP_MASK);
	ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_16, (HL7139A_PMID2OUT_UVP_400MV << HL7139A_PMID2OUT_UVP_SHIFT),
				      HL7139A_PMID2OUT_UVP_MASK);

	ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_19, (1 << HL7139A_TECN_EN_SHIFT),
				      HL7139A_TECN_EN_MASK);
	ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_1A, (1 << HL7139A_TECN_DET_MAN_SHIFT),
				      HL7139A_TECN_DET_MAN_MASK);
	ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_1A, (1 << HL7139A_I2C_VIL_UP_SHIFT),
				      HL7139A_I2C_VIL_UP_MASK);

	ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_12, (0 << HL7139A_FSW_SET_SHIFT),
				      HL7139A_FSW_SET_MASK);
	ret = hl7139a_i2c_update_bits(chip, HL7139A_REG_12, (HL7139A_IBUS_UCP_ENABLE << HL7139A_IBUS_UCP_DIS_SHIFT),
				      HL7139A_IBUS_UCP_DIS_MASK);
	ret = hl7139a_apply_dt(chip, (void *)chip->desc, hl7139a_dtprops_u32,
			      ARRAY_SIZE(hl7139a_dtprops_u32));
	if (ret < 0)
		return ret;

	ret = hl7139a_apply_dt(chip, (void *)chip->desc, hl7139a_dtprops_bool,
			      ARRAY_SIZE(hl7139a_dtprops_bool));
	if (ret < 0)
		return ret;
	chip->wdt_en = !chip->desc->wdt_dis;
	return chip->wdt_en ? hl7139a_enable_wdt(chip, false) : 0;
}

static int hl7139a_check_devinfo(struct i2c_client *client, u8 *chip_rev,
				enum hl7139a_type *type)
{
	int ret;
	int chip_id = 0;

	ret = i2c_smbus_read_byte_data(client, HL7139A_REG_DEVINFO);
	dev_info(&client->dev, "%s ret=0x%x\n", __func__, ret);
	if (ret < 0)
		return ret;

	if ((ret & 0x0f) != HL7139A_DEVID)
		return -ENODEV;

	*chip_rev = (ret & 0xf0) >> 4;
	chip_id = (ret & 0x0f);
	ret = i2c_smbus_read_byte_data(client, HL7139A_REG_06);
	if (ret < 0)
		return ret;

	*type = (ret & 0x06) >> 1;
	dev_info(&client->dev, "%s rev(0x%02X), type(%s)\n", __func__,
		 *chip_rev, hl7139a_type_name[*type]);

	return chip_id;
}

static int hl7139a_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	int ret;
	struct hl7139a_chip *chip;
	u8 chip_rev;
	enum hl7139a_type type;

	dev_info(&client->dev, "%s(%s)\n", __func__, HL7139A_DRV_VERSION);

	ret = hl7139a_check_devinfo(client, &chip_rev, &type);
	if (ret < 0)
		return ret;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	chip->id = ret;
	chip->dev = &client->dev;
	chip->client = client;
	chip->revision = chip_rev;
	chip->type = type;
	mutex_init(&chip->io_lock);
	mutex_init(&chip->adc_lock);
	mutex_init(&chip->stat_lock);
	mutex_init(&chip->hm_lock);
	mutex_init(&chip->suspend_lock);
	mutex_init(&chip->notify_lock);
	atomic_set(&chip->reset_flag,0);
	init_waitqueue_head(&chip->wq);
	i2c_set_clientdata(client, chip);

	ret = hl7139a_parse_dt(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s parse dt fail(%d)\n", __func__, ret);
		goto err;
	}

	ret = __hl7139a_init_chip(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s init chip fail(%d)\n", __func__, ret);
		goto err_initchip;
	}
	ret = hl7139a_register_chgdev(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s reg chgdev fail(%d)\n", __func__, ret);
		goto err_initchip;
	}

	hl7139a_level_shift_wdt_set(chip->chg_dev, LT_WD_100MS);
	ret = device_create_file(chip->dev, &dev_attr_hl7139a_registers_debug);

	chip->notify_task = kthread_run(hl7139a_notify_task_threadfn, chip,
					"notify_thread");
	if (IS_ERR(chip->notify_task)) {
		dev_err(chip->dev, "%s run notify thread fail(%d)\n", __func__,
			ret);
		ret = PTR_ERR(chip->notify_task);
		goto err_initirq;
	}

	INIT_WORK(&chip->state_update_work,hl7139a_state_update_handler);
#if IS_ENABLED(CONFIG_TRAN_AW95016)
	if(!strcmp(chip->desc->chg_name,"secondary_dvchg"))
		g_chip = chip;
#endif

	ret = hl7139a_init_irq(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s init irq fail(%d)\n", __func__, ret);
		goto err_initirq;
	}

	hl7139a_rfc_reset(chip->chg_dev);

	dev_info(chip->dev, "%s successfully\n", __func__);
	return 0;
err_initirq:
	charger_device_unregister(chip->chg_dev);
err_initchip:
err:
	mutex_destroy(&chip->notify_lock);
	mutex_destroy(&chip->suspend_lock);
	mutex_destroy(&chip->hm_lock);
	mutex_destroy(&chip->stat_lock);
	mutex_destroy(&chip->adc_lock);
	mutex_destroy(&chip->io_lock);
	return ret;
}

static void hl7139a_i2c_shutdown(struct i2c_client *client)
{
	struct hl7139a_chip *chip = i2c_get_clientdata(client);
	if (!chip)
		return;
	dev_info(&client->dev, "%s\n", __func__);

#if IS_ENABLED(CONFIG_TRAN_AW95016)
	if(chip->board_id == HL7139A_BOARD_H833C && !strcmp(chip->desc->chg_name,"secondary_dvchg"))
	{
		aw95016_unregister_irq(P1_1);
	}else
#endif
	disable_irq(chip->irq);

	__hl7139a_i2c_write8(chip, HL7139A_REG_14, 0xC8);
	__hl7139a_set_dp_dm(chip, DPDM_CTRL_HZ, DPDM_CTRL_HZ, true);
}

static void hl7139a_i2c_remove(struct i2c_client *client)
{
	struct hl7139a_chip *chip = i2c_get_clientdata(client);

	dev_info(&client->dev, "%s\n", __func__);
	if (!chip)
		return;
	if (chip->notify_task)
		kthread_stop(chip->notify_task);

#if IS_ENABLED(CONFIG_TRAN_AW95016)
	if(chip->board_id == HL7139A_BOARD_H833C && !strcmp(chip->desc->chg_name,"secondary_dvchg"))
	{
		aw95016_unregister_irq(P1_1);
	}
#endif

	charger_device_unregister(chip->chg_dev);
	mutex_destroy(&chip->notify_lock);
	mutex_destroy(&chip->suspend_lock);
	mutex_destroy(&chip->hm_lock);
	mutex_destroy(&chip->stat_lock);
	mutex_destroy(&chip->adc_lock);
	mutex_destroy(&chip->io_lock);
	return;
}

static int __maybe_unused hl7139a_i2c_suspend(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct hl7139a_chip *chip = i2c_get_clientdata(i2c);

	dev_info(dev, "%s\n", __func__);
	if (device_may_wakeup(dev)){
#if IS_ENABLED(CONFIG_TRAN_AW95016)
		if(chip->board_id == HL7139A_BOARD_H833C && !strcmp(chip->desc->chg_name,"secondary_dvchg"))
		{
			//Do nothing
		}else
#endif
		enable_irq_wake(chip->irq);
	}
	return 0;
}

static int __maybe_unused hl7139a_i2c_resume(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct hl7139a_chip *chip = i2c_get_clientdata(i2c);

	dev_info(dev, "%s\n", __func__);
	if (device_may_wakeup(dev)){
#if IS_ENABLED(CONFIG_TRAN_AW95016)
		if(chip->board_id == HL7139A_BOARD_H833C && !strcmp(chip->desc->chg_name,"secondary_dvchg"))
		{
			//Do nothing
		}else
#endif
		disable_irq_wake(chip->irq);
	}
	return 0;
}

static SIMPLE_DEV_PM_OPS(hl7139a_pm_ops, hl7139a_i2c_suspend, hl7139a_i2c_resume);

static const struct of_device_id hl7139a_of_id[] = {
	{ .compatible = "richtek,hl7139a" },
	{},
};
MODULE_DEVICE_TABLE(of, hl7139a_of_id);

static const struct i2c_device_id hl7139a_i2c_id[] = {
	{ "hl7139a", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, hl7139a_i2c_id);

static struct i2c_driver hl7139a_i2c_driver = {
	.driver = {
		.name = "hl7139a",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(hl7139a_of_id),
		.pm = &hl7139a_pm_ops,
	},
	.probe = hl7139a_i2c_probe,
	.shutdown = hl7139a_i2c_shutdown,
	.remove = hl7139a_i2c_remove,
	.id_table = hl7139a_i2c_id,
};
module_i2c_driver(hl7139a_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HL7139A Charger Driver");
MODULE_AUTHOR("Transsion Inc.");
MODULE_VERSION(HL7139A_DRV_VERSION);

