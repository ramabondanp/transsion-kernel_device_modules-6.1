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
#include "tc_hl7138.h"
#include "tc_charger_class.h"
#include "tc_algorithm_class.h"
#ifdef CONFIG_CHARGER_HL7138_REGMAP
#include <mt-plat/rt-regmap.h>
#endif /* CONFIG_CHARGER_HL7138_REGMAP */
#define BITS(_end, _start)          ((BIT(_end) - BIT(_start)) + BIT(_end))
/* Information */
#define HL7138_DRV_VERSION	"1.0.7_MTK"
#define HL7138_DEVID		0x03
#define HL7139_DEVID		0x0a
/* Def reg value */
#define HL7138_VBAT_OVP_DEF	0x22
#define HL7138_IBAT_OCP_DEF	0x3D
#define HL7138_WDT_TIMER_DEF	0x00

struct hl7138_chip;
static int hl7138_i2c_read8(struct hl7138_chip *chip, u8 reg, u8 *data);
enum hl7138_irqidx {
	HL7138_IRQIDX_STATE_CHG = 0,
	HL7138_IRQIDX_REG,
	HL7138_IRQIDX_TS_TEMP,
	HL7138_IRQIDX_V_NOT_OK,
	
	HL7138_IRQIDX_VIN_OVP,
	HL7138_IRQIDX_VIN_UVLO,
	HL7138_IRQIDX_TRACK_OV,
	HL7138_IRQIDX_TRACK_UV,

	HL7138_IRQIDX_VBAT_OVP,
	HL7138_IRQIDX_VOUT_OVP,
	HL7138_IRQIDX_PMID_QUAL,
	HL7138_IRQIDX_VBUS_UV,

	HL7138_IRQIDX_CUR,
	HL7138_IRQIDX_IIN_OCP,
	HL7138_IRQIDX_IBAT_OCP,
	HL7138_IRQIDX_IIN_UCP,

	HL7138_IRQIDX_SHORT,
	HL7138_IRQIDX_FET_SHORT,
	HL7138_IRQIDX_CFLY_SHORT,
	HL7138_IRQIDX_WDOG,

	HL7138_IRQIDX_DEV_MODE,
	HL7138_IRQIDX_THSD,
	HL7138_IRQIDX_MAX,
};

enum hl7138_notify {
	HL7138_NOTIFY_IBUSUCPF = 0,
	HL7138_NOTIFY_IBUSOCP,
	HL7138_NOTIFY_VBUSOVP,
	HL7138_NOTIFY_IBATOCP,
	HL7138_NOTIFY_VBATOVP,
	HL7138_NOTIFY_VOUTOVP,
	HL7138_NOTIFY_VDROVP,
	HL7138_NOTIFY_MAX,
};

enum hl7138_statflag_idx {
	HL7138_SF_INT = 0,
	HL7138_SF_INT_STS_A,
	HL7138_SF_INT_STS_B,
	HL7138_SF_STATUS_A,
	HL7138_SF_STATUS_B,
	HL7138_SF_MAX,
};

enum hl7138_type {
	HL7138_TYPE_STANDALONE = 0,
	HL7138_TYPE_STANDALONE_STATUS,
	HL7138_TYPE_SLAVE,
	HL7138_TYPE_MASTER,
	HL7138_TYPE_MAX,
};

static const char *hl7138_type_name[HL7138_TYPE_MAX] = {
	"standalone", "standalone", "slave", "master",
};

static const u32 hl7138_chgdev_notify_map[HL7138_NOTIFY_MAX] = {
	CHARGER_DEV_NOTIFY_IBUSUCP_FALL,
	CHARGER_DEV_NOTIFY_IBUSOCP,
	CHARGER_DEV_NOTIFY_VBUS_OVP,
	CHARGER_DEV_NOTIFY_IBATOCP,
	CHARGER_DEV_NOTIFY_BAT_OVP,
	CHARGER_DEV_NOTIFY_VOUTOVP,
	CHARGER_DEV_NOTIFY_VDROVP,
};

static const u8 hl7138_reg_sf[HL7138_SF_MAX] = {
	HL7138_REG_01,
	HL7138_REG_03,
	HL7138_REG_04,
	HL7138_REG_05,
	HL7138_REG_06,
};

struct hl7138_desc {
	const char *chg_name;
	const char *rm_name;
	u8 rm_slave_addr;
	u32 vbatovp;
	u32 ibatocp;
	u32 vbusovp;
	u32 ibusocp;
	u32 vacovp;
	u32 wdt;
	u32 ibat_rsense;
	u32 gpp_conf;
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

static const struct hl7138_desc hl7138_desc_defval = {
	.chg_name = "divider_charger",
	.rm_name = "hl7138",
	.rm_slave_addr = 0x66,
	.vbatovp = 4350000,
	.ibatocp = 8100000,
	.vbusovp = 12000000,
	.ibusocp = 4250000,
	.vacovp = 11000000,
	.wdt = 500000,
	.ibat_rsense = 1,	/* 5mohm */
	.gpp_conf = HL7138_GPP_SNSP,
// interrupt masked
    .state_chg_i_masked = 1,
	.reg_i_masked = 1,
	.ts_temp_i_masked = 1,
	.v_ok_i_masked = 1,
	.cur_i_masked = 1,
	.short_i_masked = 1,
	.wdog_i_masked = 1,
	.protocol_i_masked = 1,
	.charge_mode = HL7138_CHARGE_MODE_2_1,
	.vbat_reg_dis = HL7138_VBAT_REG_DISABLE,
	.ibat_reg_dis = HL7138_IBAT_REG_DISABLE,
	.iin_reg_dis = HL7138_IBUS_REG_DISABLE,
	.track_ov_dis = HL7138_PMID2OUT_OVP_DISABLE,
	.track_uv_dis = HL7138_PMID2OUT_UVP_DISABLE,
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

struct hl7138_chip {
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
	struct charger_properties chg_prop;
	struct hl7138_desc *desc;
	struct gpio_desc *irq_gpio;
	u32 ta_i2c_gpio;
	struct task_struct *notify_task;
	int irq;
	int notify;
#if IS_ENABLED(CONFIG_TRAN_USB_CONTROL)
	int rfc_control_type;
#endif
	u8 revision;
	u32 flag;
	u32 stat;
	u32 hm_cnt;
	enum hl7138_type type;
	bool wdt_en;
	bool force_adc_en;
	bool stop_thread;
	bool plug_in;
	bool notify_disable;
	bool leve_shift_disable;
	wait_queue_head_t wq;
	int id;
	atomic_t reset_flag;

#ifdef CONFIG_CHARGER_HL7138_REGMAP
	struct rt_regmap_device *rm_dev;
	struct rt_regmap_properties *rm_prop;
#endif /* CONFIG_CHARGER_HL7138_REGMAP */
};

enum hl7138_adc_channel {
	HL7138_ADC_VBUS = 0,
	HL7138_ADC_IBUS,
	HL7138_ADC_VBAT,
	HL7138_ADC_IBAT,
	HL7138_ADC_TSBUS,
	HL7138_ADC_VOUT,
	HL7138_ADC_TDIE,
	HL7138_ADC_MAX,
	HL7138_ADC_NOTSUPP = HL7138_ADC_MAX,
};

static const u8 hl7138_adc_reg[HL7138_ADC_MAX] = {
	HL7138_REG_42,
	HL7138_REG_44,
	HL7138_REG_46,
	HL7138_REG_48,
	HL7138_REG_4A,
	HL7138_REG_4C,
	HL7138_REG_4E,
};

static const char *hl7138_adc_name[HL7138_ADC_MAX] = {
	"Vbus", "Ibus", "Vbat", "Ibat", "VTS", "Vout", "TDie",
};

static const u32 hl7138_adc_accuracy_tbl[HL7138_ADC_MAX] = {
	35000,	/* VBUS */
	150000,	/* IBUS */
	20000,	/* VBAT */
	200000,	/* IBAT */
	35000,	/* VTS */
	20000,	/* VOUT */
	4,	/* TDIE */
};

static int hl7138_read_device(void *client, u32 addr, int len, void *dst)
{
	int ret;
	struct i2c_client *i2c = (struct i2c_client *)client;
	struct hl7138_chip *chip = i2c_get_clientdata(i2c);

	pm_stay_awake(chip->dev);
	mutex_lock(&chip->suspend_lock);
	ret = i2c_smbus_read_i2c_block_data(i2c, addr, len, dst);
	mutex_unlock(&chip->suspend_lock);
	pm_relax(chip->dev);
	return ret;
}

static int hl7138_write_device(void *client, u32 addr, int len, const void *src)
{
	int ret;
	struct i2c_client *i2c = (struct i2c_client *)client;
	struct hl7138_chip *chip = i2c_get_clientdata(i2c);

	pm_stay_awake(chip->dev);
	mutex_lock(&chip->suspend_lock);
	ret = i2c_smbus_write_i2c_block_data(i2c, addr, len, src);
	mutex_unlock(&chip->suspend_lock);
	pm_relax(chip->dev);
	return ret;
}

#ifdef CONFIG_CHARGER_HL7138_REGMAP
RT_REG_DECL(HL7138_REG_01, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_02, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_03, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_04, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_05, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_06, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_08, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_09, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_0A, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_0B, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_0C, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_0E, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_0F, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_10, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_11, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_12, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_13, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_14, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_15, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_16, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_17, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_18, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_19, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_1A, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_40, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_41, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_42, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_43, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_44, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_45, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_46, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_47, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_48, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_49, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_4A, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_4B, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_4C, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_4D, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_4E, 1, RT_VOLATILE, {});
RT_REG_DECL(HL7138_REG_4F, 1, RT_VOLATILE, {});

static const rt_register_map_t hl7138_regmap[] = {
	RT_REG(HL7138_REG_01),
	RT_REG(HL7138_REG_02),
	RT_REG(HL7138_REG_03),
	RT_REG(HL7138_REG_04),
	RT_REG(HL7138_REG_05),
	RT_REG(HL7138_REG_06),
	RT_REG(HL7138_REG_08),
	RT_REG(HL7138_REG_09),
	RT_REG(HL7138_REG_0A),
	RT_REG(HL7138_REG_0B),
	RT_REG(HL7138_REG_0C),
	RT_REG(HL7138_REG_0E),
	RT_REG(HL7138_REG_0F),
	RT_REG(HL7138_REG_10),
	RT_REG(HL7138_REG_11),
	RT_REG(HL7138_REG_12),
	RT_REG(HL7138_REG_13),
	RT_REG(HL7138_REG_14),
	RT_REG(HL7138_REG_15),
	RT_REG(HL7138_REG_16),
	RT_REG(HL7138_REG_17),
	RT_REG(HL7138_REG_18),
	RT_REG(HL7138_REG_19),
	RT_REG(HL7138_REG_1A),
	RT_REG(HL7138_REG_40),
	RT_REG(HL7138_REG_41),
	RT_REG(HL7138_REG_42),
	RT_REG(HL7138_REG_43),
	RT_REG(HL7138_REG_44),
	RT_REG(HL7138_REG_45),
	RT_REG(HL7138_REG_46),
	RT_REG(HL7138_REG_47),
	RT_REG(HL7138_REG_48),
	RT_REG(HL7138_REG_49),
	RT_REG(HL7138_REG_4A),
	RT_REG(HL7138_REG_4B),
	RT_REG(HL7138_REG_4C),
	RT_REG(HL7138_REG_4D),
	RT_REG(HL7138_REG_4E),
	RT_REG(HL7138_REG_4F),
};

static struct rt_regmap_fops hl7138_rm_fops = {
	.read_device = hl7138_read_device,
	.write_device = hl7138_write_device,
};

/*
static int hl7138_dump_regs(struct charger_device *chg_dev)
{
	struct hl7138_chip *chip = charger_get_data(chg_dev);
	const rt_register_map_t reg = NULL;
	int i = 0;
	u8 data[ARRAY_SIZE(hl7138_regmap)];
	for (i = 0; i < ARRAY_SIZE(hl7138_regmap); i++) {
		reg = hl7138_regmap[i];
		hl7138_i2c_read8(chip, reg->addr, &data[i]);
		dev_info(chip->dev, "%s 0x%x=0x%x\n", __func__, reg->addr, data[i]);
	}

	return 0;
}
*/
static int hl7138_register_regmap(struct hl7138_chip *chip)
{
	struct i2c_client *client = chip->client;
	struct rt_regmap_properties *prop = NULL;

	dev_info(chip->dev, "%s\n", __func__);

	prop = devm_kzalloc(&client->dev, sizeof(*prop), GFP_KERNEL);
	if (!prop)
		return -ENOMEM;

	prop->name = chip->desc->rm_name;
	prop->aliases = chip->desc->rm_name;
	prop->register_num = ARRAY_SIZE(hl7138_regmap);
	prop->rm = hl7138_regmap;
	prop->rt_regmap_mode = RT_SINGLE_BYTE | RT_CACHE_DISABLE |
			       RT_IO_PASS_THROUGH;
	prop->io_log_en = 0;

	chip->rm_prop = prop;
	chip->rm_dev = rt_regmap_device_register_ex(chip->rm_prop,
						    &hl7138_rm_fops, chip->dev,
						    client,
						    chip->desc->rm_slave_addr,
						    chip);
	if (!chip->rm_dev) {
		dev_err(chip->dev, "%s register regmap dev fail\n", __func__);
		return -EINVAL;
	}

	return 0;
}
#endif /* CONFIG_CHARGER_HL7138_REGMAP */

#define I2C_ACCESS_MAX_RETRY	3
static inline int __hl7138_i2c_write8(struct hl7138_chip *chip, u8 reg, u8 data)
{
	int ret, retry = 0;

	do {
#ifdef CONFIG_CHARGER_HL7138_REGMAP
		ret = rt_regmap_block_write(chip->rm_dev, reg, 1, &data);
#else
		ret = hl7138_write_device(chip->client, reg, 1, &data);
#endif /* CONFIG_CHARGER_HL7138_REGMAP */
		retry++;
		if (ret < 0) {
			if (reg == 0x19 && data == 0xFA)
			{
				mdelay(30);
			} else {
				usleep_range(10, 15);
			}
		}
	} while (ret < 0 && retry < I2C_ACCESS_MAX_RETRY);

	if (ret < 0) {
		dev_err(chip->dev, "%s I2CW[0x%02X] = 0x%02X fail\n", __func__,
			reg, data);
		return ret;
	}
	dev_dbg(chip->dev, "%s I2CW[0x%02X] = 0x%02X\n", __func__, reg, data);
	return 0;
}

static int __maybe_unused hl7138_i2c_write8(struct hl7138_chip *chip, u8 reg, u8 data)
{
	int ret;
	mutex_lock(&chip->io_lock);
	ret = __hl7138_i2c_write8(chip, reg, data);
	mutex_unlock(&chip->io_lock);
	return ret;
}

static inline int __hl7138_i2c_read8(struct hl7138_chip *chip, u8 reg, u8 *data)
{
	int ret, retry = 0;

	do {
#ifdef CONFIG_CHARGER_HL7138_REGMAP
		ret = rt_regmap_block_read(chip->rm_dev, reg, 1, data);
#else
		ret = hl7138_read_device(chip->client, reg, 1, data);
#endif /* CONFIG_CHARGER_HL7138_REGMAP */
		retry++;
		if (ret < 0)
			usleep_range(10, 15);
	} while (ret < 0 && retry < I2C_ACCESS_MAX_RETRY);

	if (ret < 0) {
		dev_err(chip->dev, "%s I2CR[0x%02X] fail\n", __func__, reg);
		return ret;
	}
	dev_dbg(chip->dev, "%s I2CR[0x%02X] = 0x%02X\n", __func__, reg, *data);
	return 0;
}

static int hl7138_i2c_read8(struct hl7138_chip *chip, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&chip->io_lock);
	ret = __hl7138_i2c_read8(chip, reg, data);
	mutex_unlock(&chip->io_lock);

	return ret;
}

static inline int __hl7138_i2c_write_block(struct hl7138_chip *chip, u8 reg,
					   u32 len, const u8 *data)
{
	int ret;

#ifdef CONFIG_CHARGER_HL7138_REGMAP
	ret = rt_regmap_block_write(chip->rm_dev, reg, len, data);
#else
	ret = hl7138_write_device(chip->client, reg, len, data);
#endif /* CONFIG_CHARGER_HL7138_REGMAP */

	return ret;
}

static int __maybe_unused hl7138_i2c_write_block(struct hl7138_chip *chip, u8 reg, u32 len,
				  const u8 *data)
{
	int ret;

	mutex_lock(&chip->io_lock);
	ret = __hl7138_i2c_write_block(chip, reg, len, data);
	mutex_unlock(&chip->io_lock);

	return ret;
}

static inline int __hl7138_i2c_read_block(struct hl7138_chip *chip, u8 reg,
					  u32 len, u8 *data)
{
	int ret;

#ifdef CONFIG_CHARGER_HL7138_REGMAP
	ret = rt_regmap_block_read(chip->rm_dev, reg, len, data);
#else
	ret = hl7138_read_device(chip->client, reg, len, data);
#endif /* CONFIG_CHARGER_HL7138_REGMAP */

	return ret;
}

static int hl7138_i2c_read_block(struct hl7138_chip *chip, u8 reg, u32 len,
				 u8 *data)
{
	int ret;

	mutex_lock(&chip->io_lock);
	ret = __hl7138_i2c_read_block(chip, reg, len, data);
	mutex_unlock(&chip->io_lock);

	return ret;
}

static int hl7138_i2c_test_bit(struct hl7138_chip *chip, u8 reg, u8 shft,
			       bool *one)
{
	int ret;
	u8 data;

	ret = hl7138_i2c_read8(chip, reg, &data);
	if (ret < 0) {
		*one = false;
		return ret;
	}

	*one = (data & (1 << shft)) ? true : false;
	return 0;
}

static int hl7138_i2c_update_bits(struct hl7138_chip *chip, u8 reg, u8 data,
				  u8 mask)
{
	int ret;
	u8 _data;

	mutex_lock(&chip->io_lock);
	ret = __hl7138_i2c_read8(chip, reg, &_data);
	if (ret < 0)
		goto out;
	_data &= ~mask;
	_data |= (data & mask);
	ret = __hl7138_i2c_write8(chip, reg, _data);
out:
	mutex_unlock(&chip->io_lock);
	return ret;
}

static inline int hl7138_set_bits(struct hl7138_chip *chip, u8 reg, u8 mask)
{
	return hl7138_i2c_update_bits(chip, reg, mask, mask);
}

static inline int hl7138_clr_bits(struct hl7138_chip *chip, u8 reg, u8 mask)
{
	return hl7138_i2c_update_bits(chip, reg, 0x00, mask);
}

static inline u8 hl7138_val_toreg(u32 min, u32 max, u32 step, u32 target,
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

static inline u8 hl7138_val_toreg_via_tbl(const u32 *tbl, int tbl_size,
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

static int hl7138_internal_voltage_protection(struct hl7138_chip *chip)
{
	int ret = 0;
	u8 HL7138_REG_A0 = 0xa0;
	u8 HL7138_REG_A7 = 0xa7;
	dev_info(chip->dev, "%s\n", __func__);
	ret = hl7138_i2c_write8(chip,HL7138_REG_A0,0xf9);
	if(ret < 0){
		return ret;
	}
	ret = hl7138_i2c_write8(chip,HL7138_REG_A0,0x9f);
	if(ret < 0){
		return ret;
	}

	ret = hl7138_i2c_write8(chip,HL7138_REG_A7,0x04);
	if(ret < 0){
		return ret;
	}
	ret = hl7138_i2c_write8(chip,HL7138_REG_A0,0x00);
	if(ret < 0){
		return ret;
	}
	return ret;
}

static u8 hl7138_vbatovp_toreg(u32 uV)
{
	return hl7138_val_toreg(4000000, 4600000, 10000, uV, false);
}

static u8 hl7138_ibatocp_toreg(u32 uA)
{
	return hl7138_val_toreg(2000000, 6600000, 100000, uA, true);
}

//static u8 hl7138_ibatucp_toreg(u32 uA)
//{
//	return hl7138_val_toreg(0, 6350000, 50000, uA, false);
//}

static u8 hl7138_vbusovp_toreg(u32 uV)
{
	return hl7138_val_toreg(4000000, 19000000, 1000000, uV, false);
}

static u8 hl7138_ibusocp_toreg(u32 uA)
{
	return hl7138_val_toreg(1000000, 3500000, 50000, uA, true);
}

static const u32 hl7138_wdt[] = {
	200000, 500000, 1000000, 2000000, 5000000, 10000000, 20000000, 40000000,
};

static u8 hl7138_wdt_toreg(u32 uS)
{
	return hl7138_val_toreg_via_tbl(hl7138_wdt, ARRAY_SIZE(hl7138_wdt), uS);
}

static u8 hl7138_vinovp_toreg(u32 uV)
{
	return hl7138_val_toreg(10200000, 11700000, 100000, uV, false);
}

static int __hl7138_update_status(struct hl7138_chip *chip);
static int __hl7138_init_chip(struct hl7138_chip *chip);

/* Must be called while holding a lock */
static int hl7138_enable_wdt(struct hl7138_chip *chip, bool en)
{
	int ret;

	if (chip->wdt_en == en)
		return 0;
	ret = (en ? hl7138_clr_bits : hl7138_set_bits)
		(chip, HL7138_REG_14, HL7138_WATCHDOG_DIS);
	if (ret < 0)
		return ret;
	chip->wdt_en = en;
	return 0;
}

static int __hl7138_get_adc(struct hl7138_chip *chip,
			    enum hl7138_adc_channel chan, int *val)
{
	int ret;
	u8 data[2];

	if (!chip->plug_in) {
		*val = 0;
		dev_err(chip->dev, "charger not online, forbidden adc");
	}

	/* Since the adc may be miswritten by the RFC TA,
	 * it needs to be written every time it comes in*/
	ret = hl7138_i2c_write8(chip, HL7138_REG_41, 0x00);
	if (ret < 0)
		goto out;

	ret = hl7138_set_bits(chip, HL7138_REG_40, HL7138_ADC_EN_MASK);
	if (ret < 0)
		goto out;
	usleep_range(40000, 42000);

	ret = hl7138_i2c_read_block(chip, hl7138_adc_reg[chan], 2, data);
	if (ret < 0)
		goto out;
	switch (chan) {
	case HL7138_ADC_IBUS:
		*val = ((data[0] << 4) + (data[1] & BITS(3,0))) * 1100;
		break;
	case HL7138_ADC_VBUS:
		*val = ((data[0] << 4) + (data[1] & BITS(3,0))) * 4000;
		break;
	case HL7138_ADC_VOUT:
	case HL7138_ADC_VBAT:
		*val = ((data[0] << 4) + (data[1] & BITS(3,0))) * 1250;
		break;
	case HL7138_ADC_IBAT:
		*val = ((data[0] << 4) + (data[1] & BITS(3,0))) * 2200;
		break;
	case HL7138_ADC_TDIE:
		*val = ((data[0] << 4) + (data[1] & BITS(3,0))) * 625 / 10000;
		break;
	case HL7138_ADC_TSBUS:
		*val = 25;
		break;
	default:
		ret = -ENOTSUPP;
		break;
	}
	if (ret < 0)
		dev_err(chip->dev, "%s %s fail(%d)\n", __func__,
			hl7138_adc_name[chan], ret);
	else
		dev_info(chip->dev, "%s %s %d\n", __func__,
			 hl7138_adc_name[chan], *val);
out:
	return ret;
}

#if IS_ENABLED(CONFIG_WIRELESS_TA)
int hl7138_get_charger_type(struct charger_device *chg_dev)
{
	union power_supply_propval prop, prop2, prop3;
	static struct power_supply *chg_psy;
	int ret;

	chg_psy = power_supply_get_by_name("charger");
	if (IS_ERR_OR_NULL(chg_psy)) {
		pr_info("%s Couldn't get chg_psy\n", __func__);
	} else {
		ret = power_supply_get_property(chg_psy,POWER_SUPPLY_PROP_ONLINE, &prop);
		ret = power_supply_get_property(chg_psy,POWER_SUPPLY_PROP_TYPE, &prop2);
		ret = power_supply_get_property(chg_psy,POWER_SUPPLY_PROP_USB_TYPE, &prop3);

		if (prop.intval == 0 ||(prop2.intval == POWER_SUPPLY_TYPE_USB &&
		    prop3.intval == POWER_SUPPLY_USB_TYPE_UNKNOWN))
			prop2.intval = POWER_SUPPLY_TYPE_UNKNOWN;
	}

	pr_info("%s online:%d type:%d usb_type:%d\n", __func__,prop.intval,prop2.intval,prop3.intval);

	return prop2.intval;
}
#endif

static int hl7138_enable_chg(struct charger_device *chg_dev, bool en)
{
	int ret;
	u8 val = 0;
	u8 HL7138_REG_A7 = 0xa7;
	struct hl7138_chip *chip = charger_get_data(chg_dev);
#if 1
	u32 err_check = BIT(HL7138_IRQIDX_VIN_OVP) |
			BIT(HL7138_IRQIDX_VBAT_OVP) |
			BIT(HL7138_IRQIDX_VOUT_OVP) |
			BIT(HL7138_IRQIDX_IIN_OCP)  |
			BIT(HL7138_IRQIDX_FET_SHORT) |
			BIT(HL7138_IRQIDX_CFLY_SHORT);
	u32 stat_check = 0;
#endif
#if IS_ENABLED(CONFIG_WIRELESS_TA)
	int chg_type=hl7138_get_charger_type(chg_dev);
#endif

	dev_info(chip->dev, "%s %d\n", __func__, en);
	mutex_lock(&chip->adc_lock);

	if(chip->id == HL7138_DEVID){
		ret = hl7138_i2c_read8(chip,HL7138_REG_A7,&val);
		dev_info(chip->dev, "%s reg[0xa7]:%x\n", __func__, val);
		if(val!=0x04){
			ret = hl7138_internal_voltage_protection(chip);
			if(ret < 0){
				dev_info(chip->dev, "%s protection voltage fail\n", __func__);
				goto out_unlock;
			}
		}
	}

	chip->force_adc_en = en;
	if (!en) {
#if IS_ENABLED(CONFIG_WIRELESS_TA)
		if(chg_type==POWER_SUPPLY_TYPE_WIRELESS){
			ret=hl7138_i2c_update_bits(chip, HL7138_REG_13, HL7138_IBUS_UCP_DEB_10MS<<HL7138_IBUS_UCP_DEB_SHIFT
				,HL7138_IBUS_UCP_DEB_MASK);
			if (ret < 0)
				goto out_unlock;
		}
#endif
		ret = hl7138_clr_bits(chip, HL7138_REG_12,
				      HL7138_CHG_EN_MASK);
		if (ret < 0)
			goto out_unlock;
		ret = hl7138_clr_bits(chip, HL7138_REG_40,
				      HL7138_ADC_EN_MASK);
		if (ret < 0)
			goto out_unlock;
		ret = hl7138_enable_wdt(chip, false);
		goto out_unlock;
	}
	/* Enable ADC to check status before enable charging */
	ret = hl7138_set_bits(chip, HL7138_REG_40, HL7138_ADC_EN_MASK);
	if (ret < 0)
		goto out_unlock;
	mutex_unlock(&chip->adc_lock);
	usleep_range(12000, 15000);

	mutex_lock(&chip->stat_lock);
	__hl7138_update_status(chip);
#if 1
	if ((chip->stat & err_check) ||
	    ((chip->stat & stat_check) != stat_check)) {
		dev_err(chip->dev, "%s error(0x%08X,0x%08X,0x%08X)\n", __func__,
			chip->stat, err_check, stat_check);
		ret = -EINVAL;
		mutex_unlock(&chip->stat_lock);
		goto out;
	}
#endif
	mutex_unlock(&chip->stat_lock);
	if (!chip->desc->wdt_dis) {
		ret = hl7138_enable_wdt(chip, true);
		if (ret < 0)
			goto out;
	}
#if IS_ENABLED(CONFIG_WIRELESS_TA)
	if(chg_type==POWER_SUPPLY_TYPE_WIRELESS){
		ret=hl7138_i2c_update_bits(chip, HL7138_REG_13, HL7138_IBUS_UCP_DEB_1000MS<<HL7138_IBUS_UCP_DEB_SHIFT
			,HL7138_IBUS_UCP_DEB_MASK);
		if (ret < 0)
			goto out;
	}
#endif
	ret = hl7138_set_bits(chip, HL7138_REG_12, HL7138_CHG_EN_MASK);
	if (ret < 0)
		goto out;

	goto out;
out_unlock:
	mutex_unlock(&chip->adc_lock);
out:
	return ret;
}

static int hl7138_reset_ucp(struct charger_device *chg_dev)
{
	struct hl7138_chip *chip = charger_get_data(chg_dev);
	int ret = 0;
	dev_info(chip->dev, "%s %d\n", __func__, __LINE__);
	ret=hl7138_i2c_update_bits(chip, HL7138_REG_13, HL7138_IBUS_UCP_DEB_10MS<<HL7138_IBUS_UCP_DEB_SHIFT
		,HL7138_IBUS_UCP_DEB_MASK);
	if (ret < 0)
		return -EINVAL;
	return ret;
}

static int hl7138_set_run_spec(struct charger_device *chg_dev, u32 run_spec)
{
	struct hl7138_chip *chip = charger_get_data(chg_dev);
	int ret = 0;

	dev_info(chip->dev, "%s run spec:%d\n", __func__, run_spec);

	switch (run_spec) {
	case SUPPORT_SPEC_2_1:
		ret = hl7138_i2c_update_bits(chip, HL7138_REG_15,
			HL7138_CHARGE_MODE_2_1 << HL7138_CHARGE_MODE_SHIFT, HL7138_CHARGE_MODE_MASK);
		break;
	case SUPPORT_SPEC_1_1:
		ret = hl7138_i2c_update_bits(chip, HL7138_REG_15,
			HL7138_CHARGE_MODE_1_1 << HL7138_CHARGE_MODE_SHIFT, HL7138_CHARGE_MODE_MASK);
		break;
	default:
		dev_info(chip->dev, "unknown run spec:%d\n", run_spec);
		break;
	}

	return 0;
}

static int hl7138_is_chg_enabled(struct charger_device *chg_dev, bool *en)
{
	int ret;
	u8 val = 0;
	u8 HL7138_REG_A7 = 0xa7;
	struct hl7138_chip *chip = charger_get_data(chg_dev);

	if(chip->id == HL7138_DEVID){
		ret = hl7138_i2c_read8(chip,HL7138_REG_A7,&val);
		dev_info(chip->dev, "%s reg[a7]:%x\n", __func__, val);
		if(val!=0x04){
			hl7138_internal_voltage_protection(chip);
		}
	}

	ret = hl7138_i2c_test_bit(chip, HL7138_REG_12, HL7138_CHG_EN_SHIFT,
				   en);
	dev_info(chip->dev, "%s %d, ret(%d)\n", __func__, *en, ret);
	return ret;
}

static inline enum hl7138_adc_channel to_hl7138_adc(enum adc_channel chan)
{
	switch (chan) {
	case ADC_CHANNEL_VBUS:
		return HL7138_ADC_VBUS;
	case ADC_CHANNEL_VBAT:
		return HL7138_ADC_VBAT;
	case ADC_CHANNEL_IBUS:
		return HL7138_ADC_IBUS;
	case ADC_CHANNEL_IBAT:
		return HL7138_ADC_IBAT;
	case ADC_CHANNEL_TEMP_JC:
		return HL7138_ADC_TDIE;
	case ADC_CHANNEL_VOUT:
		return HL7138_ADC_VOUT;
    case ADC_CHANNEL_TSBUS:
		return HL7138_ADC_TSBUS;
	default:
		break;
	}
	return HL7138_ADC_NOTSUPP;
}

static int hl7138_get_adc(struct charger_device *chg_dev, enum adc_channel chan,
			  int *min, int *max)
{
	int ret;
	struct hl7138_chip *chip = charger_get_data(chg_dev);
	enum hl7138_adc_channel _chan = to_hl7138_adc(chan);

	if (_chan == HL7138_ADC_NOTSUPP)
		return -EINVAL;
	mutex_lock(&chip->adc_lock);
	ret = __hl7138_get_adc(chip, _chan, max);
	if (ret < 0)
		goto out;
	if (min != max)
		*min = *max;
out:
	mutex_unlock(&chip->adc_lock);
	return ret;
}

static int hl7138_get_adc_accuracy(struct charger_device *chg_dev,
				   enum adc_channel chan, int *min, int *max)
{
	enum hl7138_adc_channel _chan = to_hl7138_adc(chan);

	if (_chan == HL7138_ADC_NOTSUPP)
		return -EINVAL;
	*min = *max = hl7138_adc_accuracy_tbl[_chan];
	return 0;
}

static int hl7138_set_vinovp(struct charger_device *chg_dev, u32 uV)
{
	struct hl7138_chip *chip = charger_get_data(chg_dev);
	u8 reg = hl7138_vinovp_toreg(uV);
	
	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, uV, reg);
	return hl7138_i2c_update_bits(chip, HL7138_REG_0C, reg,
				      HL7138_VIN_OVP_MASK);
}

static int hl7138_set_vbusovp_alarm(struct charger_device *chg_dev, u32 uV)
{
	return 0;
}

static int hl7138_set_ibusocp(struct charger_device *chg_dev, u32 uA)
{
	struct hl7138_chip *chip = charger_get_data(chg_dev);
	u8 reg = hl7138_ibusocp_toreg(uA);

	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, uA, reg);
	return hl7138_i2c_update_bits(chip, HL7138_REG_0E, reg,
				      HL7138_IBUS_OCP_MASK);
}

static int hl7138_set_vbatovp(struct charger_device *chg_dev, u32 uV)
{
	struct hl7138_chip *chip = charger_get_data(chg_dev);
	u8 reg = hl7138_vbatovp_toreg(uV);

	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, uV, reg);
	return hl7138_i2c_update_bits(chip, HL7138_REG_08, reg,
				      HL7138_BAT_OVP_MASK);
}

static int hl7138_set_vbatovp_alarm(struct charger_device *chg_dev, u32 uV)
{
	return 0;
}

#if 0
static int hl7138_is_vbushigerr(struct charger_device *chg_dev, bool *err)
{
	int ret;
	struct hl7138_chip *chip = charger_get_data(chg_dev);

		dev_info(chip->dev, "%s %d\n", __func__, __LINE__);
	mutex_lock(&chip->adc_lock);
	ret = hl7138_set_bits(chip, HL7138_REG_40, HL7138_ADC_EN_MASK);
	if (ret < 0)
		goto out;
	usleep_range(12000, 15000);
	ret = hl7138_i2c_test_bit(chip, HL7138_REG_05,
				  HL7138_TRACK_OV_STS_SHIFT, err);

	if (!chip->force_adc_en)
		hl7138_clr_bits(chip, HL7138_REG_40, HL7138_ADC_EN_MASK);
out:
	mutex_unlock(&chip->adc_lock);
	
		dev_info(chip->dev, "%s %d\n", __func__, __LINE__);
	return ret;
}
#endif

static int hl7138_is_vbuslowerr(struct charger_device *chg_dev, bool *err)
{
	int ret;
	struct hl7138_chip *chip = charger_get_data(chg_dev);

	mutex_lock(&chip->adc_lock);
	ret = hl7138_set_bits(chip, HL7138_REG_40, HL7138_ADC_EN_MASK);
	if (ret < 0)
		goto out;
	usleep_range(12000, 15000);
	ret = hl7138_i2c_test_bit(chip, HL7138_REG_05,
				  HL7138_TRACK_UV_STS_SHIFT, err);

	if (!chip->force_adc_en)
		hl7138_clr_bits(chip, HL7138_REG_40, HL7138_ADC_EN_MASK);
out:
	mutex_unlock(&chip->adc_lock);
	return ret;
}

static int hl7138_set_ibatocp(struct charger_device *chg_dev, u32 uA)
{
	struct hl7138_chip *chip = charger_get_data(chg_dev);
	u8 reg = hl7138_ibatocp_toreg(uA);

	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, uA, reg);
	return hl7138_i2c_update_bits(chip, HL7138_REG_0A, reg,
				      HL7138_BAT_OCP_MASK);
}

static inline void hl7138_set_notify(struct hl7138_chip *chip,
				     enum hl7138_notify notify)
{
	dev_info(chip->dev, "notify disable = %d\n", chip->notify_disable);
	if(chip->notify_disable)
		return;

	mutex_lock(&chip->notify_lock);
	chip->notify |= BIT(notify);
	mutex_unlock(&chip->notify_lock);
}

static int hl7138_state_chg_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7138_reg_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7138_ts_temp_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s %d\n", __func__,
		 !!(chip->stat & BIT(HL7138_IRQIDX_TS_TEMP)));
	return 0;
}

static int hl7138_v_not_ok_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7138_vin_ovp_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	hl7138_set_notify(chip, HL7138_NOTIFY_VBUSOVP);
	return 0;
}

static int hl7138_vin_uvlo_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7138_track_ov_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7138_track_uv_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s %d\n", __func__,
		 !!(chip->stat & BIT(HL7138_IRQIDX_TRACK_UV)));
	return 0;
}

static int hl7138_vbat_ovp_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s %d\n", __func__,
		 !!(chip->stat & BIT(HL7138_IRQIDX_VBAT_OVP)));
	hl7138_set_notify(chip, HL7138_NOTIFY_VBATOVP);
	return 0;
}

static int hl7138_vout_ovp_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	hl7138_set_notify(chip, HL7138_NOTIFY_VOUTOVP);
	return 0;
}

static int hl7138_pmic_qual_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7138_vubs_uv_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7138_cur_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s %d\n", __func__,
		 !!(chip->stat & BIT(HL7138_IRQIDX_CUR)));
	return 0;
}

static int hl7138_iin_ocp_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s %d\n", __func__,
		 !!(chip->stat & BIT(HL7138_IRQIDX_IIN_OCP)));
	hl7138_set_notify(chip, HL7138_NOTIFY_IBUSOCP);
	return 0;
}

static int hl7138_ibat_ocp_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	hl7138_set_notify(chip, HL7138_NOTIFY_IBATOCP);
	return 0;
}

static int hl7138_iin_ucp_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	hl7138_set_notify(chip, HL7138_NOTIFY_IBUSUCPF);
	return 0;
}

static int hl7138_short_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7138_fet_short_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7138_cfly_short_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7138_wdog_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7138_dev_mode_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int hl7138_thsd_sts_irq_handler(struct hl7138_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

struct irq_map_desc {
	const char *name;
	int (*hdlr)(struct hl7138_chip *chip);
	u8 flag_idx;
	u8 stat_idx;
	u8 flag_mask;
	u8 stat_mask;
	u32 irq_idx;
	bool stat_only;
};

#define HL7138_IRQ_DESC(_name, _flag_i, _stat_i, _flag_s, _stat_s, _irq_idx, \
			_stat_only) \
	{.name = #_name, .hdlr = hl7138_##_name##_irq_handler, \
	 .flag_idx = _flag_i, .stat_idx = _stat_i, \
	 .flag_mask = (1 << _flag_s), .stat_mask = (1 << _stat_s), \
	 .irq_idx = _irq_idx, .stat_only = _stat_only}

#define HL7138_IRQ_DESC_2(_name, _flag_i, _stat_i, _flag_s, _stat_s, _irq_idx, \
			_stat_only) \
	{.name = #_name, .hdlr = hl7138_##_name##_irq_handler, \
	 .flag_idx = _flag_i, .stat_idx = _stat_i, \
	 .flag_mask = _flag_s, .stat_mask = _stat_s, \
	 .irq_idx = _irq_idx, .stat_only = _stat_only}
/*
 * RSS: Reister index of flag, Shift of flag, Shift of state
 * RRS: Register index of flag, Register index of state, Shift of flag
 * RS: Register index of flag, Shift of flag
 * RSSO: Register index of state, Shift of state, State Only
 */
#define HL7138_IRQ_DESC_RSS(_name, _flag_i, _flag_s, _stat_s, _irq_idx) \
	HL7138_IRQ_DESC(_name, _flag_i, _flag_i, _flag_s, _stat_s, _irq_idx, \
			false)

#define HL7138_IRQ_DESC_RRS(_name, _flag_i, _stat_i, _flag_s, _irq_idx) \
	HL7138_IRQ_DESC(_name, _flag_i, _stat_i, _flag_s, _flag_s, _irq_idx, \
			false)

#define HL7138_IRQ_DESC_RS(_name, _flag_i, _flag_s, _irq_idx) \
	HL7138_IRQ_DESC(_name, _flag_i, _flag_i, _flag_s, _flag_s, _irq_idx, \
			false)

#define HL7138_IRQ_DESC_RSSO(_name, _flag_i, _flag_s, _irq_idx) \
	HL7138_IRQ_DESC(_name, _flag_i, _flag_i, _flag_s, _flag_s, _irq_idx, \
			true)

static const struct irq_map_desc hl7138_irq_map_tbl[HL7138_IRQIDX_MAX] = {
	HL7138_IRQ_DESC_2(state_chg_sts, HL7138_SF_INT, HL7138_SF_INT_STS_A, 1 << 7, 3 << 6,
			HL7138_IRQIDX_STATE_CHG, false),
	HL7138_IRQ_DESC_2(reg_sts, HL7138_SF_INT, HL7138_SF_INT_STS_A, 1 << 6, 15 << 2,
			HL7138_IRQIDX_REG, false),
	HL7138_IRQ_DESC(ts_temp_sts, HL7138_SF_INT, HL7138_SF_INT_STS_A, 5, 1,
			HL7138_IRQIDX_TS_TEMP, false),

	HL7138_IRQ_DESC(v_not_ok_sts, HL7138_SF_INT, HL7138_SF_INT_STS_B, 4, 4,
			HL7138_IRQIDX_V_NOT_OK, false),
	HL7138_IRQ_DESC(vin_ovp_sts, HL7138_SF_STATUS_A, HL7138_SF_STATUS_A, 7, 7,
			HL7138_IRQIDX_VIN_OVP, false),
	HL7138_IRQ_DESC(vin_uvlo_sts, HL7138_SF_STATUS_A, HL7138_SF_STATUS_A, 6, 6,
			HL7138_IRQIDX_VIN_UVLO, false),
	HL7138_IRQ_DESC(track_ov_sts, HL7138_SF_STATUS_A, HL7138_SF_STATUS_A, 5, 5,
			HL7138_IRQIDX_TRACK_OV, false),
	HL7138_IRQ_DESC(track_uv_sts, HL7138_SF_STATUS_A, HL7138_SF_STATUS_A, 4, 4,
			HL7138_IRQIDX_TRACK_UV, false),
	HL7138_IRQ_DESC(vbat_ovp_sts, HL7138_SF_STATUS_A, HL7138_SF_STATUS_A, 3, 3,
			HL7138_IRQIDX_VBAT_OVP, false),
	HL7138_IRQ_DESC(vout_ovp_sts, HL7138_SF_STATUS_A, HL7138_SF_STATUS_A, 2, 2,
			HL7138_IRQIDX_VOUT_OVP, false),
	HL7138_IRQ_DESC(pmic_qual_sts, HL7138_SF_STATUS_A, HL7138_SF_STATUS_A, 1, 1,
			HL7138_IRQIDX_PMID_QUAL, false),
	HL7138_IRQ_DESC(vubs_uv_sts, HL7138_SF_STATUS_A, HL7138_SF_STATUS_A, 0, 0,
			HL7138_IRQIDX_VBUS_UV, false),

	HL7138_IRQ_DESC(cur_sts, HL7138_SF_INT, HL7138_SF_INT_STS_B, 3, 3,
			HL7138_IRQIDX_CUR, false),
	HL7138_IRQ_DESC(iin_ocp_sts, HL7138_SF_STATUS_B, HL7138_SF_STATUS_B, 7, 7,
			HL7138_IRQIDX_IIN_OCP, false),
	HL7138_IRQ_DESC(ibat_ocp_sts, HL7138_SF_STATUS_B, HL7138_SF_STATUS_B, 6, 6,
			HL7138_IRQIDX_IBAT_OCP, false),
	HL7138_IRQ_DESC(iin_ucp_sts, HL7138_SF_STATUS_B, HL7138_SF_STATUS_B, 5, 5,
			HL7138_IRQIDX_IIN_UCP, false),

	HL7138_IRQ_DESC(short_sts, HL7138_SF_INT, HL7138_SF_INT_STS_B, 2, 2,
			HL7138_IRQIDX_SHORT, false),
	HL7138_IRQ_DESC(fet_short_sts, HL7138_SF_STATUS_B, HL7138_SF_STATUS_B, 4, 4,
			HL7138_IRQIDX_TRACK_UV, false),
	HL7138_IRQ_DESC(cfly_short_sts, HL7138_SF_STATUS_B, HL7138_SF_STATUS_B, 3, 3,
			HL7138_IRQIDX_CFLY_SHORT, false),

	HL7138_IRQ_DESC(wdog_sts, HL7138_SF_INT, HL7138_SF_INT_STS_B, 1, 1,
			HL7138_IRQIDX_WDOG, false),
	HL7138_IRQ_DESC_2(dev_mode_sts, HL7138_SF_STATUS_B, HL7138_SF_STATUS_B, 3 << 1, 3 << 1,
			HL7138_IRQIDX_DEV_MODE, false),
	HL7138_IRQ_DESC(thsd_sts, HL7138_SF_STATUS_B, HL7138_SF_STATUS_B, 0, 0,
			HL7138_IRQIDX_THSD, false),
};

static int __hl7138_update_status(struct hl7138_chip *chip)
{
	int i;
	u8 sf[HL7138_SF_MAX] = {0};
	const struct irq_map_desc *desc;

	for (i = 0; i < HL7138_SF_MAX; i++){
		hl7138_i2c_read8(chip, hl7138_reg_sf[i], &sf[i]);
		//dev_info(chip->dev, "%s sf[0x%x]=0x%x\n", __func__,hl7138_reg_sf[i],sf[i]);
	}

	for (i = 0; i < ARRAY_SIZE(hl7138_irq_map_tbl); i++) {
		desc = &hl7138_irq_map_tbl[i];
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
	return 0;
}

static int __maybe_unused hl7138_update_status(struct hl7138_chip *chip)
{
	int ret;

	mutex_lock(&chip->stat_lock);
	ret = __hl7138_update_status(chip);
	mutex_unlock(&chip->stat_lock);
	return ret;
}

static int hl7138_notify_task_threadfn(void *data)
{
	int i;
	struct hl7138_chip *chip = data;

	while (!kthread_should_stop()) {
		wait_event_interruptible(chip->wq, chip->notify != 0 ||
					 kthread_should_stop());
		if (kthread_should_stop())
			goto out;
		pm_stay_awake(chip->dev);
		mutex_lock(&chip->notify_lock);
		for (i = 0; i < HL7138_NOTIFY_MAX; i++) {
			if (chip->notify & BIT(i)) {
				chip->notify &= ~BIT(i);
				mutex_unlock(&chip->notify_lock);
				charger_dev_notify(chip->chg_dev,
						   hl7138_chgdev_notify_map[i]);
				mutex_lock(&chip->notify_lock);
			}
		}
		mutex_unlock(&chip->notify_lock);
		pm_relax(chip->dev);
	}
out:
	return 0;
}
static void hl7138_state_update_handler(struct work_struct *data)
{
	int i;
	const struct irq_map_desc *desc;
	struct hl7138_chip *chip = container_of(data,struct hl7138_chip,state_update_work);
#ifdef CONFIG_CHARGER_HL7138_REGMAP
	//hl7138_dump_regs(chip->chg_dev);
#endif
	pm_stay_awake(chip->dev);
	mutex_lock(&chip->stat_lock);
	__hl7138_update_status(chip);
	for (i = 0; i < ARRAY_SIZE(hl7138_irq_map_tbl); i++) {
		desc = &hl7138_irq_map_tbl[i];
		if ((chip->flag & (1 << desc->irq_idx)) && desc->hdlr)
			desc->hdlr(chip);
	}
	chip->flag = 0;
	wake_up_interruptible(&chip->wq);
	mutex_unlock(&chip->stat_lock);
	pm_relax(chip->dev);
}

static irqreturn_t hl7138_irq_handler(int irq, void *data)
{
	struct hl7138_chip *chip = data;

	schedule_work(&chip->state_update_work);

	return IRQ_HANDLED;
}
static int hl7138_en_rfc_detect(struct hl7138_chip *chip)
{
	int ret = 0;
	ret = hl7138_i2c_update_bits(chip, HL7138_REG_19, (HL7138_QC20_DET_DIS << HL7138_QC20_DET_EN_SHIFT),
				      HL7138_QC20_DET_EN_MASK);
#if IS_ENABLED(CONFIG_CHARGER_HL7138_WATCHDOG_CLOSE)
	ret = hl7138_i2c_update_bits(chip, HL7138_REG_1A, (3 << HL7138_LT_WD_TMR_SHIFT),
				      HL7138_LT_WD_TMR_MASK);
#else
	ret = hl7138_i2c_update_bits(chip, HL7138_REG_1A, (2 << HL7138_LT_WD_TMR_SHIFT),
				      HL7138_LT_WD_TMR_MASK);
#endif
	ret = hl7138_i2c_update_bits(chip, HL7138_REG_16, (HL7138_PMID2OUT_OVP_900MV << HL7138_PMID2OUT_OVP_SHIFT),
				      HL7138_PMID2OUT_OVP_MASK);
	ret = hl7138_i2c_update_bits(chip, HL7138_REG_16, (HL7138_PMID2OUT_UVP_400MV << HL7138_PMID2OUT_UVP_SHIFT),
				      HL7138_PMID2OUT_UVP_MASK);

	ret = hl7138_i2c_update_bits(chip, HL7138_REG_19, (1 << HL7138_TECN_EN_SHIFT),
				      HL7138_TECN_EN_MASK);
	ret = hl7138_i2c_update_bits(chip, HL7138_REG_1A, (1 << HL7138_TECN_DET_MAN_SHIFT),
				      HL7138_TECN_DET_MAN_MASK);
	return ret;
}
static int hl7138_rfc_reset(struct charger_device *chg_dev)
{
	struct hl7138_chip *chip = charger_get_data(chg_dev);
	int ret = 0;
	u8 val,dpdm_cfg;
	dev_info(chip->dev, "%s %d\n", __func__, __LINE__);
	if(atomic_read(&chip->reset_flag)){
		return 0;
	}
	atomic_set(&chip->reset_flag,1);
#if 0
	ret = hl7138_i2c_write8(chip, HL7138_REG_14, 0xC0);
	if (ret < 0)
		return ret;
#endif
	ret = hl7138_i2c_write8(chip, HL7138_REG_19, 0xFA);
	ret = hl7138_i2c_write8(chip, HL7138_REG_19, 0x00);
	ret = hl7138_i2c_write8(chip, HL7138_REG_1A, 0x00);
	ret = hl7138_i2c_write8(chip, HL7138_REG_40, 0x00);

	hl7138_i2c_read8(chip, HL7138_REG_15, &val);
	dpdm_cfg = (val >> 2) && 0x01;
	dev_info(chip->dev, "%s 0x%x = 0x%x\n", __func__,HL7138_REG_15, val);
	if(dpdm_cfg){
		dev_info(chip->dev, "%s DPDM_CFG 0 = %d\n", __func__, dpdm_cfg);
		hl7138_i2c_update_bits(chip, HL7138_REG_15, (0 << 2),0x04);

		hl7138_i2c_read8(chip, HL7138_REG_15, &val);
		dpdm_cfg = (val >> 2) && 0x01;
		dev_info(chip->dev, "%s DPDM_CFG 1 = %d\n", __func__, dpdm_cfg);
	}
	atomic_set(&chip->reset_flag,0);

	return ret;
}
static int __hl7138_set_dp_dm(struct hl7138_chip *chip, enum dpdm_ctrl_status dp_status, 
	enum dpdm_ctrl_status dm_status, bool en_rfc_detect)
{
	int ret = 0;

	if((dp_status == DPDM_CTRL_HZ) && (dm_status == DPDM_CTRL_HZ) && chip->chg_dev) {
		ret = hl7138_rfc_reset(chip->chg_dev);
		return ret;
	}

	if (en_rfc_detect) {
		hl7138_en_rfc_detect(chip);
	}

#if IS_ENABLED(CONFIG_TRAN_USB_CONTROL)
	if (chip->rfc_control_type == 3) {
		dev_info(chip->dev, "%s hl7138 write reg[0x1a] for 0x20\n", __func__);
		hl7138_i2c_write8(chip,HL7138_REG_1A, 0x20);
	}
#endif

	switch (dp_status) {
		case DPDM_CTRL_HZ:
			ret = hl7138_i2c_update_bits(chip, HL7138_REG_19, (HL7138_FORCE_DP_HIZ << HL7138_FORCE_DP_SHIFT),
				      HL7138_FORCE_DP_MASK);
			break;
		case DPDM_CTRL_0V:
			//ret = hl7138_i2c_update_bits(chip, HL7138_REG_19, (HL7138_COMP_TH_0_3V << HL7138_COMP_TH_SHIFT),
				      //HL7138_COMP_TH_MASK);
			ret = hl7138_i2c_update_bits(chip, HL7138_REG_19, (HL7138_FORCE_DP_0V << HL7138_FORCE_DP_SHIFT),
				      HL7138_FORCE_DP_MASK);
			break;
		case DPDM_CTRL_0_6V:
			ret = hl7138_i2c_update_bits(chip, HL7138_REG_19, (HL7138_FORCE_DP_0_6V << HL7138_FORCE_DP_SHIFT),
				      HL7138_FORCE_DP_MASK);
			break;
		case DPDM_CTRL_3_3V:
			ret = hl7138_i2c_update_bits(chip, HL7138_REG_19, (HL7138_COMP_TH_0_8V << HL7138_COMP_TH_SHIFT),
				      HL7138_COMP_TH_MASK);
			ret = hl7138_i2c_update_bits(chip, HL7138_REG_19, (HL7138_FORCE_DP_3_3V << HL7138_FORCE_DP_SHIFT),
				      HL7138_FORCE_DP_MASK);
			break;
		default:
			break;
	}

	switch (dm_status) {
		case DPDM_CTRL_HZ:
			ret = hl7138_i2c_update_bits(chip, HL7138_REG_19, (HL7138_FORCE_DM_HIZ << HL7138_FORCE_DM_SHIFT),
				      HL7138_FORCE_DM_MASK);
			break;
		case DPDM_CTRL_0V:
			ret = hl7138_i2c_update_bits(chip, HL7138_REG_19, (HL7138_FORCE_DM_0V << HL7138_FORCE_DM_SHIFT),
				      HL7138_FORCE_DM_MASK);
			break;
		case DPDM_CTRL_0_6V:
			ret = hl7138_i2c_update_bits(chip, HL7138_REG_19, (HL7138_FORCE_DM_0_6V << HL7138_FORCE_DM_SHIFT),
				      HL7138_FORCE_DM_MASK);
			break;
		case DPDM_CTRL_3_3V:
			ret = hl7138_i2c_update_bits(chip, HL7138_REG_19, (HL7138_FORCE_DM_3_3V << HL7138_FORCE_DM_SHIFT),
				      HL7138_FORCE_DM_MASK);
			break;
		default:
			break;
	}
	return ret;
}

static int hl7138_set_dp_dm(struct charger_device *chg_dev, enum dpdm_ctrl_status dp_status,
	enum dpdm_ctrl_status dm_status, bool en_rfc_detect)
{
	struct hl7138_chip *chip = charger_get_data(chg_dev);
	int ret = 0;
	__hl7138_set_dp_dm(chip, dp_status, dm_status, en_rfc_detect);

	dev_info(chip->dev, "%s %d/%d\n", __func__, dp_status, dm_status);
	return ret;
}

static int hl7138_get_dp_dm(struct charger_device *chg_dev, bool dp)
{
	struct hl7138_chip *chip = charger_get_data(chg_dev);
	int ret = 0;
	u8 data;
	ret = hl7138_i2c_read8(chip, HL7138_REG_1A, &data);
	if (ret < 0)
		return ret;

	if (dp) {
		ret = (0x1 << 1) & data;
	} else {
		ret = (0x1) & data;
	}
	dev_info(chip->dev, "%s %s:%d\n", __func__, dp ? "dp": "dm", ret);
	ret = !!ret;
	return ret;
}

static int hl7138_i2c_trans(struct charger_device *chg_dev, bool high)
{
	struct hl7138_chip *chip = charger_get_data(chg_dev);
	int ret = 0;

	dev_info(chip->dev, "%s %d\n", __func__, high);

	if(high) {
#if IS_ENABLED(CONFIG_TRAN_USB_CONTROL)
		if (chip->rfc_control_type == 3)
			ret = hl7138_i2c_write8(chip, HL7138_REG_19, 0xFA);
		else
#endif
			ret = hl7138_i2c_write8(chip, HL7138_REG_19, 0xFE);

		mutex_lock(&chip->io_lock);
	} else {
		mutex_unlock(&chip->io_lock);
		ret = hl7138_i2c_write8(chip, HL7138_REG_19, 0xFA);
	}
	udelay(100);
	return ret;
}

static int hl7138_adc_init(struct charger_device *chg_dev, bool en)
{
	int ret = 0;
#if 0
	struct hl7138_chip *chip = charger_get_data(chg_dev);
	dev_err(chip->dev, "%s en=%d\n", __func__,en);

	if (en) {
		ret = hl7138_i2c_write8(chip, HL7138_REG_40, 0x01);
		if (ret < 0)
			goto out;
		usleep_range(120000, 140000);
	} else {
		ret = hl7138_i2c_write8(chip, HL7138_REG_40, 0x0);
		if (ret < 0)
			goto out;
	}
out:
#endif
	return ret;
}

static int hl7138_init_chip(struct charger_device *chg_dev)
{
	struct hl7138_chip *chip = charger_get_data(chg_dev);

	dev_info(chip->dev, "%s\n", __func__);

	return __hl7138_init_chip(chip);
}

static void hl7138_reset_func(struct hl7138_chip *chip)
{
	u8 val_02 = 0;
	u8 val_40 = 0;
	int i = 0;
	dev_info(chip->dev, "%s\n", __func__);
	for(i = 0;i < 3;i++){
		hl7138_i2c_write8(chip,HL7138_REG_02,0x00);
		hl7138_i2c_write8(chip,HL7138_REG_40,0x05);
		hl7138_i2c_write8(chip,HL7138_REG_14,0xc0);
		mdelay(15);
		hl7138_i2c_read8(chip,HL7138_REG_02,&val_02);
		hl7138_i2c_read8(chip,HL7138_REG_40,&val_40);
		dev_info(chip->dev,"%s,reg[02]:%x,reg[40]:%x\n", __func__,val_02,val_40);
		if((val_02==0xff)&&(val_40==0x00)){
			dev_info(chip->dev,"%s successfully\n", __func__);
			return;
		}
	}
}

static int hl7138_plug_in(struct charger_device *chg_dev)
{
	int ret = 0;
	struct hl7138_chip *chip = charger_get_data(chg_dev);

	dev_info(chip->dev, "%s\n", __func__);

	/* If plug in, enable adc */
	mutex_lock(&chip->adc_lock);
	ret = hl7138_set_bits(chip, HL7138_REG_40, HL7138_ADC_EN_MASK);
	if (ret < 0) {
		dev_info(chip->dev, "enable adc failed, ret:%d", ret);
	}
	usleep_range(40000, 42000);

	chip->plug_in = true;
	mutex_unlock(&chip->adc_lock);
	return 0;
}

static int hl7138_plug_out(struct charger_device *chg_dev)
{
	int ret = 0;
	struct hl7138_chip *chip = charger_get_data(chg_dev);

	dev_info(chip->dev, "%s\n", __func__);

	if(chip->id == HL7138_DEVID){
		hl7138_reset_func(chip);
	}

	/* If plug out, disable adc */
	mutex_lock(&chip->adc_lock);
	chip->plug_in = false;
	ret = hl7138_clr_bits(chip, HL7138_REG_40,
			      HL7138_ADC_EN_MASK);
	if (ret < 0) {
		dev_info(chip->dev, "disable adc failed, ret:%d", ret);
	}
	mutex_unlock(&chip->adc_lock);
	return 0;
}

static int hl7138_set_ibusucp_en(struct charger_device *chg_dev, bool en)
{
	struct hl7138_chip *chip = charger_get_data(chg_dev);
	int ret = 0;

	dev_info(chip->dev, "%s: en:%d\n", __func__, en);

	return ret;
}

static int hl7138_set_dp_func(struct charger_device *chg_dev, enum dpdm_ctrl_status status)
{
	struct hl7138_chip *chip = charger_get_data(chg_dev);
	int ret = 0;

	switch (status) {
		case DPDM_CTRL_HZ:
			ret = hl7138_i2c_update_bits(chip, HL7138_REG_19,
				(HL7138_FORCE_DP_HIZ << HL7138_FORCE_DP_SHIFT), HL7138_FORCE_DP_MASK);
			break;
		case DPDM_CTRL_0V:
			ret = hl7138_i2c_update_bits(chip, HL7138_REG_19,
				(HL7138_FORCE_DP_0V << HL7138_FORCE_DP_SHIFT), HL7138_FORCE_DP_MASK);
			break;
		case DPDM_CTRL_0_6V:
			ret = hl7138_i2c_update_bits(chip, HL7138_REG_19,
				(HL7138_FORCE_DP_0_6V << HL7138_FORCE_DP_SHIFT), HL7138_FORCE_DP_MASK);
			break;
		case DPDM_CTRL_3_3V:
			ret = hl7138_i2c_update_bits(chip, HL7138_REG_19,
				(HL7138_FORCE_DP_3_3V << HL7138_FORCE_DP_SHIFT), HL7138_FORCE_DP_MASK);
			break;
		default:
			break;
	}

	dev_info(chip->dev, "%s: status:%d, ret:%d\n", __func__, status, ret);

	return ret;
}

static int hl7138_set_dm_func(struct charger_device *chg_dev, enum dpdm_ctrl_status status)
{
	struct hl7138_chip *chip = charger_get_data(chg_dev);
	int ret = 0;

	switch (status) {
		case DPDM_CTRL_HZ:
			ret = hl7138_i2c_update_bits(chip, HL7138_REG_19,
				(HL7138_FORCE_DM_HIZ << HL7138_FORCE_DM_SHIFT), HL7138_FORCE_DM_MASK);
			break;
		case DPDM_CTRL_0V:
			ret = hl7138_i2c_update_bits(chip, HL7138_REG_19,
				(HL7138_FORCE_DM_0V << HL7138_FORCE_DM_SHIFT), HL7138_FORCE_DM_MASK);
			break;
		case DPDM_CTRL_0_6V:
			ret = hl7138_i2c_update_bits(chip, HL7138_REG_19,
				(HL7138_FORCE_DM_0_6V << HL7138_FORCE_DM_SHIFT), HL7138_FORCE_DM_MASK);
			break;
		case DPDM_CTRL_3_3V:
			ret = hl7138_i2c_update_bits(chip, HL7138_REG_19,
				(HL7138_FORCE_DM_3_3V << HL7138_FORCE_DM_SHIFT), HL7138_FORCE_DM_MASK);
			break;
		default:
			break;
	}

	dev_info(chip->dev, "%s: status:%d, ret:%d\n", __func__, status, ret);

	return ret;
}

static int hl7138_en_dpdm_ctrl(struct charger_device *chg_dev, bool en)
{
	struct hl7138_chip *chip = charger_get_data(chg_dev);
	int ret = 0;

	if (en) {
		ret = hl7138_i2c_update_bits(chip, HL7138_REG_19, (1 << HL7138_TECN_EN_SHIFT),
					      HL7138_TECN_EN_MASK);
		ret = hl7138_i2c_update_bits(chip, HL7138_REG_1A, (1 << HL7138_TECN_DET_MAN_SHIFT),
					      HL7138_TECN_DET_MAN_MASK);
	} else {
		ret = hl7138_i2c_update_bits(chip, HL7138_REG_19, (0 << HL7138_TECN_EN_SHIFT),
					      HL7138_TECN_EN_MASK);
		ret = hl7138_i2c_update_bits(chip, HL7138_REG_1A, (0 << HL7138_TECN_DET_MAN_SHIFT),
					      HL7138_TECN_DET_MAN_MASK);
	}
	
	dev_info(chip->dev, "%s: en:%d, ret:%d\n", __func__, en, ret);

	return ret;
}

static const struct charger_ops hl7138_chg_ops = {
	.enable = hl7138_enable_chg,
	.is_enabled = hl7138_is_chg_enabled,
	.get_adc = hl7138_get_adc,
	.set_vbusovp = hl7138_set_vinovp,
	.set_ibusocp = hl7138_set_ibusocp,
	.set_vbatovp = hl7138_set_vbatovp,
	.set_ibatocp = hl7138_set_ibatocp,
	.set_ibusucp_enable = hl7138_set_ibusucp_en,

	.init_chip = hl7138_init_chip,
	.set_vbatovp_alarm = hl7138_set_vbatovp_alarm,
	.set_vbusovp_alarm = hl7138_set_vbusovp_alarm,

	//.is_vbushigerr = hl7138_is_vbushigerr,
	.is_vbuslowerr = hl7138_is_vbuslowerr,
	.get_adc_accuracy = hl7138_get_adc_accuracy,

	.set_dp_dm = hl7138_set_dp_dm,
	.get_dp_dm = hl7138_get_dp_dm,
	.i2c_trans = hl7138_i2c_trans,
	.plug_out = hl7138_plug_out,
	.plug_in = hl7138_plug_in,
	.init_adc = hl7138_adc_init,
	.soft_reset = hl7138_rfc_reset,
	.reset_ucp=hl7138_reset_ucp,
	.set_run_spec = hl7138_set_run_spec,
	.set_dp = hl7138_set_dp_func,
	.set_dm = hl7138_set_dm_func,
	.en_dpdm_ctrl = hl7138_en_dpdm_ctrl,
};

static int hl7138_register_chgdev(struct hl7138_chip *chip)
{
	chip->chg_prop.alias_name = chip->desc->chg_name;
	chip->chg_dev = charger_device_register(chip->desc->chg_name, chip->dev,
						chip, &hl7138_chg_ops,
						&chip->chg_prop);
	if (!chip->chg_dev)
		return -EINVAL;
	return 0;
}

static int hl7138_clearall_irq(struct hl7138_chip *chip)
{
	int i, ret;
	u8 data;

	for (i = 0; i < HL7138_SF_MAX; i++) {
		ret = hl7138_i2c_read8(chip, hl7138_reg_sf[i], &data);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int hl7138_init_irq(struct hl7138_chip *chip)
{
	int ret = 0, len = 0;
	char *name = NULL;

	dev_info(chip->dev, "%s\n", __func__);
	ret = hl7138_clearall_irq(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s clr all irq fail(%d)\n", __func__, ret);
		return ret;
	}
	if (chip->type == HL7138_TYPE_SLAVE)
		return 0;

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
		hl7138_irq_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT, name,
		chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s request thread irq fail(%d)\n", __func__,
			ret);
		return ret;
	}
	device_init_wakeup(chip->dev, true);
	return 0;
}

#define HL7138_DT_VALPROP(name, reg, shft, mask, func, base) \
	{#name, offsetof(struct hl7138_desc, name), reg, shft, mask, func, base}

struct hl7138_dtprop {
	const char *name;
	size_t offset;
	u8 reg;
	u8 shft;
	u8 mask;
	u8 (*toreg)(u32 val);
	u8 base;
};

static inline void hl7138_parse_dt_u32(struct device_node *np, void *desc,
				       const struct hl7138_dtprop *props,
				       int prop_cnt)
{
	int i;

	for (i = 0; i < prop_cnt; i++) {
		if (unlikely(!props[i].name))
			continue;
		of_property_read_u32(np, props[i].name, desc + props[i].offset);
	}
}

static inline void hl7138_parse_dt_bool(struct device_node *np, void *desc,
					const struct hl7138_dtprop *props,
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

static inline int hl7138_apply_dt(struct hl7138_chip *chip, void *desc,
				  const struct hl7138_dtprop *props,
				  int prop_cnt)
{
	int i, ret;
	u32 val;

	for (i = 0; i < prop_cnt; i++) {
		val = *(u32 *)(desc + props[i].offset);
		if (props[i].toreg)
			val = props[i].toreg(val);
		val += props[i].base;
		ret = hl7138_i2c_update_bits(chip, props[i].reg,
					     val << props[i].shft,
					     props[i].mask);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static const struct hl7138_dtprop hl7138_dtprops_u32[] = {
	HL7138_DT_VALPROP(vbatovp, HL7138_REG_08, 0, 0x3f,
			  hl7138_vbatovp_toreg, 0),
	HL7138_DT_VALPROP(ibatocp, HL7138_REG_0A, 0, 0x3f,
			  hl7138_ibatocp_toreg, 0),
	HL7138_DT_VALPROP(vbusovp, HL7138_REG_0B, 0, 0x0f,
			  hl7138_vbusovp_toreg, 0),
	HL7138_DT_VALPROP(ibusocp, HL7138_REG_0E, 0, 0x3f,
			  hl7138_ibusocp_toreg, 0),
	HL7138_DT_VALPROP(wdt, HL7138_REG_14, 0, 0x07,
			  hl7138_wdt_toreg, 0),
	//HL7138_DT_VALPROP(ibat_rsense, HL7138_REG_REGCTRL, 1, 0x02, NULL, 0),
	HL7138_DT_VALPROP(gpp_conf, HL7138_REG_15, 0, 0x03, NULL, 0),
};

static const struct hl7138_dtprop hl7138_dtprops_bool[] = {
	HL7138_DT_VALPROP(state_chg_i_masked, HL7138_REG_02, 7, 0x80, NULL, 0),
	HL7138_DT_VALPROP(reg_i_masked, HL7138_REG_02, 6, 0x40, NULL, 0),
	HL7138_DT_VALPROP(ts_temp_i_masked, HL7138_REG_02, 5, 0x20, NULL, 0),
	HL7138_DT_VALPROP(v_ok_i_masked, HL7138_REG_02, 4, 0x10, NULL, 0),
	HL7138_DT_VALPROP(cur_i_masked, HL7138_REG_02, 3, 0x08, NULL, 0),
	HL7138_DT_VALPROP(short_i_masked, HL7138_REG_02, 2, 0x04, NULL, 0),
	HL7138_DT_VALPROP(wdog_i_masked, HL7138_REG_02, 1, 0x02, NULL, 0),
	HL7138_DT_VALPROP(protocol_i_masked, HL7138_REG_02, 0, 0x01, NULL, 0),
	HL7138_DT_VALPROP(charge_mode, HL7138_REG_15, 7, 0x80, NULL, 0),

	HL7138_DT_VALPROP(vbat_reg_dis, HL7138_REG_11, 7, 0x80, NULL, 0),
	HL7138_DT_VALPROP(ibat_reg_dis, HL7138_REG_11, 6, 0x40, NULL, 0),
	HL7138_DT_VALPROP(iin_reg_dis, HL7138_REG_10, 6, 0x40, NULL, 0),
	HL7138_DT_VALPROP(vbatovp_dis, HL7138_REG_08, 7, 0x80, NULL, 0),
	HL7138_DT_VALPROP(ibatocp_dis, HL7138_REG_0A, 7, 0x80, NULL, 0),
	HL7138_DT_VALPROP(ibusocp_dis, HL7138_REG_0E, 7, 0x80, NULL, 0),
	HL7138_DT_VALPROP(track_ov_dis, HL7138_REG_16, 7, 0x80, NULL, 0),
	HL7138_DT_VALPROP(track_uv_dis, HL7138_REG_16, 6, 0x40, NULL, 0),
	HL7138_DT_VALPROP(wdt_dis, HL7138_REG_14, 3, 0x08, NULL, 0),
	//HL7138_DT_VALPROP(tdieotp_dis, HL7138_REG_CHGCTRL1, 0, 0x01, NULL, 0),
	HL7138_DT_VALPROP(voutovp_dis, HL7138_REG_13, 5, 0x20, NULL, 0),
	HL7138_DT_VALPROP(ibusadc_dis, HL7138_REG_41, 6, 0x40, NULL, 0),
	HL7138_DT_VALPROP(tdieadc_dis, HL7138_REG_41, 2, 0x04, NULL, 0),
	//HL7138_DT_VALPROP(tsbatadc_dis, HL7138_REG_ADCEN, 1, 0x02, NULL, 0),
	//HL7138_DT_VALPROP(tsbusadc_dis, HL7138_REG_ADCEN, 2, 0x04, NULL, 0),
	HL7138_DT_VALPROP(ibatadc_dis, HL7138_REG_41, 4, 0x10, NULL, 0),
	HL7138_DT_VALPROP(vbatadc_dis, HL7138_REG_41, 5, 0x20, NULL, 0),
	HL7138_DT_VALPROP(voutadc_dis, HL7138_REG_41, 1, 0x02, NULL, 0),
	//HL7138_DT_VALPROP(vacadc_dis, HL7138_REG_ADCEN, 6, 0x40, NULL, 0),
	HL7138_DT_VALPROP(vbusadc_dis, HL7138_REG_41, 7, 0x80, NULL, 0),
};

static int hl7138_parse_dt(struct hl7138_chip *chip)
{
	struct hl7138_desc *desc;
	struct device_node *np = chip->dev->of_node;
	struct device_node *child_np;
#if IS_ENABLED(CONFIG_TRAN_USB_CONTROL)
	struct device_node *usb_control_np = NULL;
#endif

	if (!np)
		return -ENODEV;

	if (chip->type == HL7138_TYPE_SLAVE)
		goto ignore_intr;

	chip->irq_gpio = devm_gpiod_get(chip->dev, "hl7138,intr", GPIOD_IN);
	if (IS_ERR(chip->irq_gpio))
		return PTR_ERR(chip->irq_gpio);

	chip->ta_i2c_gpio = of_get_named_gpio(np, "ta_i2c_gpio", 0);
	if (chip->ta_i2c_gpio < 0) {
		dev_err(chip->dev, "%s: ta_i2c_gpio get_named  fail\n", __func__);
		//return -EFAULT;
	}
ignore_intr:
	desc = devm_kzalloc(chip->dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	memcpy(desc, &hl7138_desc_defval, sizeof(*desc));
	if (of_property_read_string(np, "rm_name", &desc->rm_name) < 0)
		dev_info(chip->dev, "%s no rm name\n", __func__);
	if (of_property_read_u8(np, "rm_slave_addr", &desc->rm_slave_addr) < 0)
		dev_info(chip->dev, "%s no regmap slave addr\n", __func__);
	child_np = of_get_child_by_name(np, hl7138_type_name[chip->type]);
	if (!child_np) {
		dev_err(chip->dev, "%s no node(%s) found\n", __func__,
			hl7138_type_name[chip->type]);
		return -ENODEV;
	}
	if (of_property_read_string(child_np, "chg_name", &desc->chg_name) < 0)
		dev_info(chip->dev, "%s no chg name\n", __func__);
	chip->notify_disable = of_property_read_bool(child_np, "notify_disable");
	chip->leve_shift_disable = of_property_read_bool(child_np, "leve_shift_disable");
	
#if IS_ENABLED(CONFIG_TRAN_USB_CONTROL)
	usb_control_np = of_find_node_by_name(NULL, "usb_control");
	if (!usb_control_np) {
		dev_err(chip->dev, "[%s] of_find_node_by_name fail\n", __func__);
	} else {
		of_property_read_u32(usb_control_np, "rfc_control_type", &chip->rfc_control_type);
		dev_info(chip->dev, "[%s] rfc_control_type = %d\n", __func__, chip->rfc_control_type);
	}
#endif

	hl7138_parse_dt_u32(child_np, (void *)desc, hl7138_dtprops_u32,
			    ARRAY_SIZE(hl7138_dtprops_u32));
	hl7138_parse_dt_bool(child_np, (void *)desc, hl7138_dtprops_bool,
			     ARRAY_SIZE(hl7138_dtprops_bool));
	chip->desc = desc;
	return 0;
}

static int hl7138_clear_status(struct hl7138_chip *chip)
{
	u8 value = 0;
	int ret = 0;
	int i = 0;
	dev_info(chip->dev, "%s\n", __func__);
	for(i = 1;i < 8;i++){
		ret = hl7138_i2c_read8(chip,i,&value);
		if(ret < 0)
			return ret;
	}

	//IIN_UCP_TH to 150mA
	ret = hl7138_i2c_write8(chip,HL7138_REG_12,0x51);
	if(ret < 0)
			return ret;
	
	return 0;
}

static int __hl7138_init_chip(struct hl7138_chip *chip)
{
	int ret;

	dev_info(chip->dev, "%s\n", __func__);
	
	if(chip->id == HL7138_DEVID){
		ret = hl7138_clear_status(chip);
		if (ret < 0) {
			dev_err(chip->dev, "%s init chip fail(%d)\n", __func__, ret);
			return ret ;
		}
	}
	//if(chip->chg_dev)
		//hl7138_i2c_trans(chip->chg_dev, false);
	ret = hl7138_i2c_update_bits(chip, HL7138_REG_19, (HL7138_QC20_DET_DIS << HL7138_QC20_DET_EN_SHIFT),
				      HL7138_QC20_DET_EN_MASK);

#if IS_ENABLED(CONFIG_CHARGER_HL7138_WATCHDOG_CLOSE)
	ret = hl7138_i2c_update_bits(chip, HL7138_REG_1A, (3 << HL7138_LT_WD_TMR_SHIFT),
				      HL7138_LT_WD_TMR_MASK);
#else
	ret = hl7138_i2c_update_bits(chip, HL7138_REG_1A, (2 << HL7138_LT_WD_TMR_SHIFT),
				      HL7138_LT_WD_TMR_MASK);
#endif
	ret = hl7138_i2c_update_bits(chip, HL7138_REG_16, (HL7138_PMID2OUT_OVP_900MV << HL7138_PMID2OUT_OVP_SHIFT),
				      HL7138_PMID2OUT_OVP_MASK);
	ret = hl7138_i2c_update_bits(chip, HL7138_REG_16, (HL7138_PMID2OUT_UVP_400MV << HL7138_PMID2OUT_UVP_SHIFT),
				      HL7138_PMID2OUT_UVP_MASK);
	if(!chip->leve_shift_disable){
	ret = hl7138_i2c_update_bits(chip, HL7138_REG_19, (1 << HL7138_TECN_EN_SHIFT),
				      HL7138_TECN_EN_MASK);
	ret = hl7138_i2c_update_bits(chip, HL7138_REG_1A, (1 << HL7138_TECN_DET_MAN_SHIFT),
				      HL7138_TECN_DET_MAN_MASK);
	dev_err(chip->dev, "%s leve_shift enable ,chip->leve_shift_disable =%d\n", __func__, chip->leve_shift_disable);
	}
#ifdef TRAN_CL9
	ret = hl7138_i2c_update_bits(chip, HL7138_REG_12, (2 << HL7138_FSW_SET_SHIFT),
				      HL7138_FSW_SET_MASK);
#else
	ret = hl7138_i2c_update_bits(chip, HL7138_REG_12, (0 << HL7138_FSW_SET_SHIFT),
				      HL7138_FSW_SET_MASK);	
#endif
	ret = hl7138_i2c_update_bits(chip, HL7138_REG_12, (HL7138_IBUS_UCP_ENABLE << HL7138_IBUS_UCP_DIS_SHIFT),
				      HL7138_IBUS_UCP_DIS_MASK);
	ret = hl7138_apply_dt(chip, (void *)chip->desc, hl7138_dtprops_u32,
			      ARRAY_SIZE(hl7138_dtprops_u32));
	if (ret < 0)
		return ret;

	if(chip->id == HL7138_DEVID){
		hl7138_internal_voltage_protection(chip);
	}

	ret = hl7138_apply_dt(chip, (void *)chip->desc, hl7138_dtprops_bool,
			      ARRAY_SIZE(hl7138_dtprops_bool));
	if (ret < 0)
		return ret;
	chip->wdt_en = !chip->desc->wdt_dis;
	return chip->wdt_en ? hl7138_enable_wdt(chip, false) : 0;
}

static int hl7138_check_devinfo(struct i2c_client *client, u8 *chip_rev,
				enum hl7138_type *type)
{
	int ret;
	int chip_id = 0;

	ret = i2c_smbus_read_byte_data(client, HL7138_REG_DEVINFO);
	dev_info(&client->dev, "%s ret=0x%x\n", __func__, ret);
	if (ret < 0)
		return ret;
	if (((ret & 0x0f) != HL7138_DEVID) && ((ret & 0x0f) != HL7139_DEVID))
		return -ENODEV;
	*chip_rev = (ret & 0xf0) >> 4;
	chip_id = (ret & 0x0f);
	ret = i2c_smbus_read_byte_data(client, HL7138_REG_06);
	if (ret < 0)
		return ret;
	*type = (ret & 0x06) >> 1;
	dev_info(&client->dev, "%s rev(0x%02X), type(%s)\n", __func__,
		 *chip_rev, hl7138_type_name[*type]);
	return chip_id;
}
static ssize_t hl7138_store_reg(struct device *dev, struct device_attribute *attr,
                const char *buf, size_t count)
{
	struct hl7138_chip *chip = dev_get_drvdata(dev);
    unsigned int databuf[2] = {0, 0};

	if(2 == sscanf(buf, "%x %x", &databuf[0], &databuf[1])) {
		//check reg addr
		if((databuf[0] <= 0x1A && databuf[0] >= 0x0 )
		||(databuf[0] >= 0x40 && databuf[0] <= 0x4f))
		{
			hl7138_i2c_write8(chip, databuf[0], databuf[1]);
			dev_info(chip->dev, "%s 0x%x=0x%x\n", __func__, databuf[0], databuf[1]);
		}
	}
    return count;
}

static ssize_t hl7138_show_reg(struct device *dev,struct device_attribute *attr,char *buf)
{
	struct hl7138_chip *chip = dev_get_drvdata(dev);

	ssize_t len = 0;
	unsigned char i = 0;
	unsigned char reg_val = 0;

	for (i = 0; i <= 0x1A; i++) {
		hl7138_i2c_read8(chip, i, &reg_val);
		len += sprintf(buf + len, "reg:0x%02X=0x%02X\n", i, reg_val);
		dev_info(chip->dev, "%s 0x%x=0x%x\n", __func__, i, reg_val);
	}
	for (i = 0; i <= 0x0f; i++) {
		hl7138_i2c_read8(chip, 0x40 + i, &reg_val);
		len += sprintf(buf + len, "reg:0x%02X=0x%02X\n", 0x40 + i, reg_val);
		dev_info(chip->dev, "%s 0x%x=0x%x\n", __func__, 0x40 + i, reg_val);
	}

	return len;
}

static const DEVICE_ATTR(show_reg,0664,hl7138_show_reg,hl7138_store_reg);

static int hl7138_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	int ret;
	struct hl7138_chip *chip;
	u8 chip_rev;
	enum hl7138_type type;

	dev_info(&client->dev, "%s(%s)\n", __func__, HL7138_DRV_VERSION);

	ret = hl7138_check_devinfo(client, &chip_rev, &type);
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

	ret = hl7138_parse_dt(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s parse dt fail(%d)\n", __func__, ret);
		goto err;
	}
#ifdef CONFIG_CHARGER_HL7138_REGMAP
	ret = hl7138_register_regmap(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s reg regmap fail(%d)\n", __func__, ret);
		goto err;
	}
#endif /* CONFIG_CHARGER_HL7138_REGMAP */
	ret = __hl7138_init_chip(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s init chip fail(%d)\n", __func__, ret);
		goto err_initchip;
	}
	ret = hl7138_register_chgdev(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s reg chgdev fail(%d)\n", __func__, ret);
		goto err_initchip;
	}

	dev_set_drvdata(chip->dev,chip);
	ret = device_create_file(chip->dev, &dev_attr_show_reg);

	chip->notify_task = kthread_run(hl7138_notify_task_threadfn, chip,
					"notify_thread");
	if (IS_ERR(chip->notify_task)) {
		dev_err(chip->dev, "%s run notify thread fail(%d)\n", __func__,
			ret);
		ret = PTR_ERR(chip->notify_task);
		goto err_initirq;
	}
	INIT_WORK(&chip->state_update_work,hl7138_state_update_handler);
	ret = hl7138_init_irq(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s init irq fail(%d)\n", __func__, ret);
		goto err_initirq;
	}
	//hl7138_i2c_trans(chip->chg_dev, false);
	if (hl7138_check_devinfo(client, &chip_rev, &type) == HL7139_DEVID)
		hl7138_rfc_reset((chip->chg_dev));

	dev_info(chip->dev, "%s successfully\n", __func__);
	return 0;
err_initirq:
	charger_device_unregister(chip->chg_dev);
err_initchip:
#ifdef CONFIG_CHARGER_HL7138_REGMAP
	rt_regmap_device_unregister(chip->rm_dev);
#endif /* CONFIG_CHARGER_HL7138_REGMAP */
err:
	mutex_destroy(&chip->notify_lock);
	mutex_destroy(&chip->suspend_lock);
	mutex_destroy(&chip->hm_lock);
	mutex_destroy(&chip->stat_lock);
	mutex_destroy(&chip->adc_lock);
	mutex_destroy(&chip->io_lock);
	return ret;
}

static void hl7138_i2c_shutdown(struct i2c_client *client)
{
	struct hl7138_chip *chip = i2c_get_clientdata(client);
	if (!chip)
		return;
	dev_info(&client->dev, "%s\n", __func__);
	disable_irq(chip->irq);
	__hl7138_set_dp_dm(chip, DPDM_CTRL_HZ, DPDM_CTRL_HZ, true);
}

static int hl7138_i2c_remove(struct i2c_client *client)
{
	struct hl7138_chip *chip = i2c_get_clientdata(client);

	dev_info(&client->dev, "%s\n", __func__);
	if (!chip)
		return 0;
	if (chip->notify_task)
		kthread_stop(chip->notify_task);
	charger_device_unregister(chip->chg_dev);
#ifdef CONFIG_CHARGER_HL7138_REGMAP
	rt_regmap_device_unregister(chip->rm_dev);
#endif /* CONFIG_CHARGER_HL7138_REGMAP */
	mutex_destroy(&chip->notify_lock);
	mutex_destroy(&chip->suspend_lock);
	mutex_destroy(&chip->hm_lock);
	mutex_destroy(&chip->stat_lock);
	mutex_destroy(&chip->adc_lock);
	mutex_destroy(&chip->io_lock);
	return 0;
}

static int __maybe_unused hl7138_i2c_suspend(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct hl7138_chip *chip = i2c_get_clientdata(i2c);

	dev_info(dev, "%s\n", __func__);
	mutex_lock(&chip->suspend_lock);
	if (device_may_wakeup(dev))
		enable_irq_wake(chip->irq);
	return 0;
}

static int __maybe_unused hl7138_i2c_resume(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct hl7138_chip *chip = i2c_get_clientdata(i2c);

	dev_info(dev, "%s\n", __func__);
	mutex_unlock(&chip->suspend_lock);
	if (device_may_wakeup(dev))
		disable_irq_wake(chip->irq);
	return 0;
}

static SIMPLE_DEV_PM_OPS(hl7138_pm_ops, hl7138_i2c_suspend, hl7138_i2c_resume);

static const struct of_device_id hl7138_of_id[] = {
	{ .compatible = "tc,hl7138" },
	{ .compatible = "tc,hl7139" },
	{},
};
MODULE_DEVICE_TABLE(of, hl7138_of_id);

static const struct i2c_device_id hl7138_i2c_id[] = {
	{ "hl7138", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, hl7138_i2c_id);

static struct i2c_driver hl7138_i2c_driver = {
	.driver = {
		.name = "hl7138",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(hl7138_of_id),
		.pm = &hl7138_pm_ops,
	},
	.probe = hl7138_i2c_probe,
	.shutdown = hl7138_i2c_shutdown,
	.remove = hl7138_i2c_remove,
	.id_table = hl7138_i2c_id,
};
module_i2c_driver(hl7138_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HL7138 Charger Driver");
MODULE_AUTHOR("ShuFan Lee<shufan_lee@richtek.com>");
MODULE_VERSION(HL7138_DRV_VERSION);
/*
 *
 * 20210609
 * 	0x19 reset must 0xFE --> 0xFA --> 0x00
 */
