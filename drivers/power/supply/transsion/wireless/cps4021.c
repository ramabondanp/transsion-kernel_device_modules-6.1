// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/unistd.h>
#include <linux/of_gpio.h>
#include "cps4021.h"
#include "wireless_class.h"
#include "tc_misc_intf.h"
#include "tc_pe5.h"

#define DEVICE_NAME "CPS4021"
#define DRIVER_FIRMWARE_VERSION "1.0.2"
#define BIN_FW_OFFSET 	0xc5

#define REG_NONE_ACCESS		0
#define REG_RD_ACCESS		BIT(0)
#define REG_WR_ACCESS		BIT(1)
#define REG_BIT_ACCESS		BIT(2)

struct reg_attr {
	const char *name;
	u16 addr;
	u8 flag;
};

enum REG_INDEX {
	CHIPID = 0,
	VOUT,
	INT_FLAG,
	INTCTLR,
	VOUTSET,
	CMD,
	INDEX_MAX,
};

static struct reg_attr reg_access[INDEX_MAX] = {
	[CHIPID]	= {"CHIPID", REG_CHIPID, REG_RD_ACCESS},
	[VOUT]		= {"VOUT", REG_RX_ADC_VOUT, REG_RD_ACCESS},
	[INT_FLAG]	= {"INT_FLAG", REG_INTFLAG, REG_RD_ACCESS},
	[INTCTLR]	= {"INTCLR", REG_INTCLR, REG_WR_ACCESS},
	[VOUTSET]	= {"VOUTSET", REG_RX_VOUT_SET, REG_RD_ACCESS | REG_WR_ACCESS},
	[CMD]		= {"CMD", REG_CMD, REG_RD_ACCESS | REG_WR_ACCESS | REG_BIT_ACCESS},
};

static u8 wbuf_high_addr_4000[] = {0xFF, 0x82, 0x00, 0x40};
static u8 wbuf_unmask_all[] = {0xE0, 0x08, 0xFF, 0xFF};
static u8 wbuf_anaglog_password[] = {0xE7, 0x5C, 0x50, 0x12};
static u8 wbuf_i2c_timeout[] = {0xE0, 0x04, 0x1D, 0x00};
static u8 wbuf_high_addr_2000[] = {0xFF, 0x82, 0x00, 0x20};
static u8 wbuf_high_addr_4004[] = {0xFF, 0x82, 0x04, 0x40};
static u8 wbuf_remap[] = {0x00, 0xA0, 0xFF, 0x00};
static u8 wbuf_disable_trim[] = {0x00, 0x11, 0x80, 0x00};
static u8 wbuf_restart[] = {0xFF, 0x80, 0x00, 0x00};

static int cps4021_read(struct cps4021_dev *chip, u32 reg, u8 *val)
{
	unsigned int temp;
	int rc = 0;

	if (!chip || !val)
		return -EINVAL;

	mutex_lock(&chip->i2c_lock);
	rc = regmap_read(chip->regmap, reg, &temp);
	if (rc >= 0)
		*val = (u8)temp;
	mutex_unlock(&chip->i2c_lock);

	return rc;
}

static int cps4021_write(struct cps4021_dev *chip, u32 reg, u8 val)
{
	int rc = 0;

	if (!chip)
		return -EINVAL;

	mutex_lock(&chip->i2c_lock);
	rc = regmap_write(chip->regmap, reg, val);
	if (rc < 0)
		dev_err(chip->dev, "CPS4021 write error: %d\n", rc);
	mutex_unlock(&chip->i2c_lock);

	return rc;
}

static int cps4021_write_nbyte(struct cps4021_dev *chip,
							int reg, int value, int data_len)
{
    int ret = 0;

    mutex_lock(&chip->i2c_lock);
    ret = regmap_raw_write(chip->regmap, reg, &value, data_len);
    mutex_unlock(&chip->i2c_lock);

	return ret;
}

static int cps4021_read_buffer(struct cps4021_dev *chip,
		u32 reg, u8 *buf, u32 size)
{
	int rc = 0;

	while (size--) {
		rc = chip->bus.read((void *)chip, reg++, buf++);
		if (rc < 0) {
			dev_err(chip->dev, "CPS4021 read error: %d\n", rc);
			goto out;
		}
	}
out:
	return rc;
}

/*
16 bit reg  read n bytes ,max is 4 bytes
*/
static int cps4021_read_nbyte(struct cps4021_dev *chip, int addr, int data_len)
{
    int ret;
    u8 read_date[4];
    int r_date = 0;

    mutex_lock(&chip->i2c_lock);
    ret = regmap_raw_read(chip->regmap, addr, read_date, data_len);
    mutex_unlock(&chip->i2c_lock);

    if (ret < 0)
        return ret;

    r_date = read_date[3];
    r_date = r_date << 8;
    r_date |= read_date[2];
    r_date = r_date << 8;
    r_date |= read_date[1];
    r_date = r_date << 8;
    r_date |= read_date[0];

    return r_date;
}

static int cps4021_write_buffer(struct cps4021_dev *chip,
		u32 reg, u8 *buf, u32 size)
{
	int rc = 0;

	while (size--) {
		rc = chip->bus.write((void *)chip, reg++, *buf++);
		if (rc < 0) {
			dev_err(chip->dev, "CPS4021 write error: %d\n", rc);
			goto out;
		}
	}
out:
	return rc;
}

static int cps4021_write_multi_data(struct cps4021_dev *chip,
		struct i2c_msg *regs, int num_regs)
{
    int ret;

    mutex_lock(&chip->i2c_lock);
    ret = i2c_transfer(chip->client->adapter, regs, num_regs);
    mutex_unlock(&chip->i2c_lock);

    if (ret < 0) {
        dev_err(chip->dev, "[%s] i2c transfer error:ret:%d!\n", __func__, ret);
        return CPS_WLS_FAIL;
    }

    return CPS_WLS_SUCCESS;
}

static int cps4021_update_bits(struct cps4021_dev *chip, u8 reg, u8 data,
				  u8 mask)
{
	int ret;
	u8 _data;

	ret = cps4021_read(chip, reg, &_data);
	if (ret < 0)
		goto out;

	_data &= ~mask;
	_data |= (data & mask);

	ret = cps4021_write(chip, reg, _data);
out:
	return ret;
}

static int cps4021_program_cmd_send(struct cps4021_dev *chip, int cmd)
{
    return cps4021_write_nbyte(chip, ADDR_CMD, cmd, sizeof(cmd));
}

static int cps4021_program_wait_cmd_done(struct cps4021_dev *dev)
{
    int res;
    int cnt = 100;

    do {
        cps4021_write_nbyte(dev, 0xFF82, 0x2000, 2);    
        res = cps4021_read_nbyte(dev, ADDR_FLAG, 4);
        if (res == CPS_WLS_FAIL)
            return CPS_WLS_FAIL;

        msleep(10);

        switch (res & 0xFF) {
        case RUNNING:
            break;
        case PASS:
			return true;
        case FAIL:
            pr_err("---> FAIL : %x\n", res);
            return false;
        case ILLEGAL:
            pr_err("---> ILLEGAL : %x\n", res);
            return false;
        default:
            pr_err("---> ERROR-CODE : %x\n", res);
            return false;
        }
    } while (cnt--);

    return false;
}

static ssize_t get_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	Alig16 val = {0, };
	ssize_t len = 0;
	int i = 0;
	struct cps4021_dev *chip = dev_get_drvdata(dev);

	for (i = 0; i < INDEX_MAX; i++) {
		if (reg_access[i].flag & REG_RD_ACCESS) {
			cps4021_read_buffer(chip, reg_access[i].addr, val.ptr, sizeof(val));
			len += snprintf(buf + len, PAGE_SIZE - len, "reg:%s 0x%04x=0x%04x,%d\n",
				reg_access[i].name, reg_access[i].addr, val.value, val.value);
		}
	}

	return 0;
}

static ssize_t set_reg(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t len)
{
	unsigned int databuf[2];
	Alig16 val = {0, };
	u8 tmp[2];
	u16 regdata;
	int i = 0;
	int ret = 0;
	struct cps4021_dev *chip = dev_get_drvdata(dev);

	ret = sscanf(buf, "%x %x", &databuf[0], &databuf[1]);

	if (2 == ret) {
		for (i = 0; i < INDEX_MAX; i++) {
			if (databuf[0] == reg_access[i].addr) {
				if (reg_access[i].flag & REG_WR_ACCESS) {
					val.value = databuf[1];
					if (reg_access[i].flag & REG_BIT_ACCESS) {
						cps4021_read_buffer(chip, databuf[0], tmp, 2);
						regdata = tmp[0] << 8 | tmp[1];
						val.value |= regdata;
						pr_info("get reg: 0x%04x set reg: 0x%04x \n", regdata, val.value);
						cps4021_write_buffer(chip, databuf[0], val.ptr, 2);
					} else {
						pr_info("Set reg : [0x%04x]  0x%x \n", databuf[0], val.value);
						cps4021_write_buffer(chip, databuf[0], val.ptr, 2);
					}
				}
				break;
			}
		}
	} else {
		pr_info("error\n");
	}
	return len;
}

static DEVICE_ATTR(reg, S_IRUGO | S_IWUSR, get_reg, set_reg);

static struct attribute *cps4021_sysfs_attrs[] = {
	&dev_attr_reg.attr,
	NULL,
};

static const struct attribute_group cps4021_sysfs_group = {
	.name  = "cps4021group",
	.attrs = cps4021_sysfs_attrs,
};

static const struct regmap_config cps4021_regmap_config = {
	.reg_bits	 = 16,
	.val_bits	 = 8,
};

static int cps4021_tx_pt_int_irq_handler(struct cps4021_dev *chip)
{
	pr_info("%s\n", __func__);
	wireless_ic_set_state(chip->wl_chg, WIRELESS_TX_DET_RX);
	return 0;
}

static int cps4021_tx_ept_irq_handler(struct cps4021_dev *chip)
{
	Alig32 val = {0};
	Alig16 q_val = {0};

	cps4021_read_buffer(chip, REG_TX_EPT_RST, val.ptr, sizeof(val));
	pr_info("ept val:%x\n", val.value);
	if (val.value & BIT(12)) {
		cps4021_read_buffer(chip, REG_TX_Q_FACTOR_VAL, q_val.ptr, sizeof(q_val));
		pr_info("q factor val:%x\n", q_val.value);
		wireless_ic_set_state(chip->wl_chg, WIRELESS_TX_MODE_CLOSE);
		return 0;
	}
	wireless_ic_set_state(chip->wl_chg, WIRELESS_TX_RMV_RX);

	return 0;
}

static int cps4021_tx_ac_det_irq_handler(struct cps4021_dev *chip)
{
	wireless_ic_set_state(chip->wl_chg, WIRELESS_TX_AC_VALID);
	return 0;
}

static int cps4021_tx_init_done_irq_handler(struct cps4021_dev *chip)
{
	wireless_ic_set_state(chip->wl_chg, WIRELESS_TX_INIT_DONE);
	return 0;
}

static int cps4021_tx_pvt_ask_irq_handler(struct cps4021_dev *chip)
{
	return 0;
}

#define CPS4021_TX_IRQ_DESC(_name, _stat_s) 	\
{												\
	.name = #_name,								\
	.stat_mask = _stat_s,						\
	.hdlr = cps4021_tx_##_name##_irq_handler	\
}

#define CPS4021_RX_IRQ_DESC(_name, _stat_s) 	\
	{.name = #_name, .stat_mask = _stat_s, 		\
	 .hdlr = cps4021_rx_##_name##_irq_handler 	\
}

static const struct irq_map_desc cps4021_tx_irq_map_tbl[] = {
	CPS4021_TX_IRQ_DESC(ept, INT_TX_EPT),
	CPS4021_TX_IRQ_DESC(pt_int, PT_INT),
	CPS4021_TX_IRQ_DESC(pvt_ask, INT_TX_PVT_ASK),
	CPS4021_TX_IRQ_DESC(ac_det, INT_TX_AC_DET),
};

static void cps4021_tx_interrupt_handler(struct cps4021_dev *chip, u32 tx_value)
{
	int i;
	const struct irq_map_desc *desc;

	for (i = 0; i < ARRAY_SIZE(cps4021_tx_irq_map_tbl); i++) {
		desc = &cps4021_tx_irq_map_tbl[i];
		if ((tx_value & desc->stat_mask) && desc->hdlr) {
			desc->hdlr(chip);
		}
	}
}

static int cps4021_fskrecv_fsk_det(struct cps4021_dev *chip, u16 value)
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

static int cps4021_rx_ldo_on_irq_handler(struct cps4021_dev *chip)
{
	u8 protocol_mode = 0;
	int ret = 0;
	Alig16 fw_ver = {0};

	chip->ldo_on = true;
	wireless_ic_set_state(chip->wl_chg, WIRELESS_LDO_ON);

	ret = cps4021_read_buffer(chip, REG_MINORVER, fw_ver.ptr, sizeof(fw_ver));
	if (ret < 0) {
		pr_err("read fw version fail\n");
		return ret;
	}

	ret = cps4021_read_buffer(chip, REG_RX_POWER_MODE,
		&protocol_mode, sizeof(protocol_mode));
	if (ret < 0) {
		pr_err("read protocol mode fail\n");
		return ret;
	}

	pr_info("wls protocol mode : %x, %s, fw_ver:0x%x", protocol_mode,
		wls_protocol_mode_name(protocol_mode), fw_ver.value);

	if (protocol_mode <= MPP_FORCE && protocol_mode >= MPP_RESTRICT) {
		wireless_ic_set_state(chip->wl_chg, WIRELESS_RX_MPP);
	}

	chip->protocol = protocol_mode;
	return ret;
}

static int cps4021_rx_fskrecv_irq_handler(struct cps4021_dev *chip)
{
	int ret = 0;
	Alig16 val = {0};

	cps4021_read_buffer(chip, REG_RX_BC, val.ptr, sizeof(val));

	ret = cps4021_fskrecv_fsk_det(chip, val.value);
	if (!ret)
		goto out;
out:
	return 0;
}

static int cps4021_rx_ldoopp_irq_handler(struct cps4021_dev *chip)
{
	//wireless_ic_set_state(chip->wl_chg, WIRELESS_RX_HW_ERR);
	return 0;
}

static int cps4021_rx_ldoocp_irq_handler(struct cps4021_dev *chip)
{
	wireless_ic_set_state(chip->wl_chg, WIRELESS_RX_HW_ERR);
	return 0;
}

static int cps4021_rx_ldoovp_irq_handler(struct cps4021_dev *chip)
{
	wireless_ic_set_state(chip->wl_chg, WIRELESS_RX_HW_ERR);
	return 0;
}

static int cps4021_rx_epp_ready_irq_handler(struct cps4021_dev *chip)
{
	wireless_ic_set_state(chip->wl_chg, WIRELESS_RX_EPP_READY);
	return 0;
}

static int cps4021_rx_sr_br_h2f_irq_handler(struct cps4021_dev *chip)
{
	wireless_ic_set_state(chip->wl_chg, WIRELESS_RX_SR_BR_H2F);
	return 0;
}

static const struct irq_map_desc cps4021_rx_irq_map_tbl[] = {
	CPS4021_RX_IRQ_DESC(ldo_on, INT_RX_LDO_ON),
	CPS4021_RX_IRQ_DESC(fskrecv, INT_RX_FSK_PKT),
	CPS4021_RX_IRQ_DESC(ldoopp, INT_RX_LDOOPP),
	CPS4021_RX_IRQ_DESC(ldoocp, INT_RX_LDOOCP),
	CPS4021_RX_IRQ_DESC(ldoovp, INT_RX_LDOOVP),
	CPS4021_RX_IRQ_DESC(epp_ready, INT_RX_EPP_CALI),
	CPS4021_RX_IRQ_DESC(sr_br_h2f, INT_RX_SR_BR_H2F),
};

static void cps4021_rx_interrupt_handler(struct cps4021_dev *chip, u32 rx_value)
{
	int i;
	const struct irq_map_desc *desc;

	for (i = 0; i < ARRAY_SIZE(cps4021_rx_irq_map_tbl); i++) {
		desc = &cps4021_rx_irq_map_tbl[i];
		if ((rx_value & desc->stat_mask) && desc->hdlr) {
			desc->hdlr(chip);
		}
	}
}

static int cps4021_get_vout(struct cps4021_dev *chip)
{
	Alig16 vout = {0};
	int ret = 0;

	ret = cps4021_read_buffer(chip, REG_RX_ADC_VOUT, vout.ptr, sizeof(vout));
	if (ret < 0) {
		pr_err("%s: chip may offline!\n", __func__);
		return 0;
	}
	return vout.value;
}

static int cps4021_get_iout(struct cps4021_dev *chip)
{
	Alig16 iout = {0};
	int ret = 0;

	ret = cps4021_read_buffer(chip, REG_RX_ADC_IRECT, iout.ptr, sizeof(iout));
	if (ret < 0) {
		pr_err("%s: chip may offline!\n", __func__);
		return 0;
	} 
	return iout.value;
}

static int cps4021_set_vout_val(struct cps4021_dev *chip, int volt, bool shutdown)
{
	int ret = 0, count = 30;
	Alig16 val = {0}, ce = {0};

	val.value = (u16)volt;
	pr_info("%s, volt:%d\n", __func__, volt);
	ret = cps4021_write_buffer(chip, REG_RX_VOUT_SET, val.ptr, sizeof(val));
	if (ret < 0) {
		pr_err("%s, write vout reg fail\n", __func__);
		return ret;
	}

	if (shutdown)
		return 0;

	do {
		msleep(140);
		ret = cps4021_read_buffer(chip, REG_RX_CE_VAL, ce.ptr, sizeof(ce));
		if (ret < 0) {
			return ret;
		}
		pr_info("--->ce value:%d\n", (short)(ce.value));

		if ((short)(ce.value) < 2) {
			return 0;
		}
	} while(count--);

	return 0;
}

static irqreturn_t cps4021_irq_handle(int irq, void  *data)
{
	Alig32 val = {0};
	int ret = 0;
	u8 mode = 0;
	struct cps4021_dev *chip = (struct cps4021_dev *)data;

	pm_stay_awake(chip->dev);

	ret = cps4021_read_buffer(chip, REG_INTFLAG, val.ptr, sizeof(val));
	if (ret < 0) {
		dev_info(chip->dev, "cps4021 read REG_INTFLAG failed, ret = %d\n", ret);
		goto out;
	}
	pr_info("%s: cps4021 irq status = 0x%x\n", __func__, val.value);
	if (!val.value) {
		pr_info("[%s] no irq here\n", __func__);
		goto out;
	}
	cps4021_write_buffer(chip, REG_INTCLR, val.ptr, sizeof(val));

	ret = cps4021_read_buffer(chip, REG_SYSMODE, &mode, sizeof(mode));
	if (ret < 0) {
		dev_info(chip->dev, "cps4021 read REG_SYSMODE failed, ret = %d\n", ret);
		goto out;
	}

	switch (mode) {
	case BCK_PWR_MODE:
		pr_info("cps4021 in BCK_PWR_MODE\n");
		if (chip->tx_mode_en) {
			pr_err("cps4021 mode abnormal error\n");
			chip->tx_mode_en = false;
			wireless_ic_set_state(chip->wl_chg, WIRELESS_TX_MODE_RESTART);
		}

		if (BIT(0) & val.value) {
			cps4021_tx_init_done_irq_handler(chip);
		}
		break;
	case RX_MODE:
		pr_info("cps4021 in Rx mode\n");
		cps4021_rx_interrupt_handler(chip, val.value);
		break;
	case TX_MODE:
		pr_info("cps4021 in Tx mode\n");
		cps4021_tx_interrupt_handler(chip, val.value);
		break;
	default:
		pr_err("cps4021 mode unknown\n");
		break;
	}
out:
	pm_relax(chip->dev);
	return IRQ_HANDLED;
}

static irqreturn_t cps4021_pg_irq_handle(int irq, void *data)
{
	struct cps4021_dev *chip = (struct cps4021_dev *)data;

	chip->pg = !gpiod_get_value(chip->pg_gpio);

	pr_info("%s:%s\n", __func__,chip->pg ? "pg on" : "pg off");
	wireless_ic_set_state(chip->wl_chg, WIRELESS_PG_CHANGE);

	return IRQ_HANDLED;
}

static irqreturn_t cps4021_wired_irq_handle(int irq, void *data)
{
	int ret = 0;
	struct cps4021_dev *chip = (struct cps4021_dev *)data;

	ret = !gpiod_get_value(chip->wired_gpio);
	pr_info("%s:%s\n", __func__, ret ? "wired plugin" : "wired pulgout");

	wireless_ic_set_state(chip->wl_chg, WIRELESS_WIRED_CHANGE);

	return IRQ_HANDLED;
}

static int cps4021_parse_dt(struct cps4021_dev *chip)
{
	int ret = 0;
	const struct device_node *np = chip->dev->of_node;

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

	chip->dv_vctrl_gpio = devm_gpiod_get(chip->dev, "dv_vctrl", GPIOD_OUT_LOW);
	if (IS_ERR(chip->dv_vctrl_gpio)){
		pr_err("%s get dv ctrl gpio failed\n", __func__);
		return PTR_ERR(chip->dv_vctrl_gpio);
	}

	chip->sleep_gpio = devm_gpiod_get(chip->dev, "sleep", GPIOD_OUT_LOW);
	if (IS_ERR(chip->sleep_gpio)){
		pr_err("%s get sleep gpio failed\n", __func__);
	}

	chip->protocol_gpio = devm_gpiod_get(chip->dev, "protocol", GPIOD_OUT_HIGH);
	if (IS_ERR(chip->protocol_gpio)){
		pr_err("get protocol gpio failed\n");
	}

	chip->ovp_ctrl_gpio = devm_gpiod_get(chip->dev, "ovp_ctrl", GPIOD_OUT_HIGH);
	if (IS_ERR(chip->ovp_ctrl_gpio)){
		pr_err("%s get tx mode gpio failed\n", __func__);
	}

	ret = of_property_read_u32(np, "drop_vol", &chip->par.drop_vol);
	if (ret) {
		chip->par.drop_vol = 1000;
	}

	ret = of_property_read_u16(np, "q_factor", &chip->par.q_factor);
	if (ret) {
		chip->par.q_factor = 0x19;
	}

	ret = of_property_read_u16(np, "tx_ping_ocp_th", &chip->par.tx_ping_ocp_th);
	if (ret) {
		chip->par.tx_ping_ocp_th = 0x7d0;
	}

	ret = of_property_read_u8(np, "tx_fop_min", &chip->par.tx_fop_min);
	if (ret) {
		chip->par.tx_fop_min = 0x70;
	}

	ret = of_property_read_u8(np, "tx_fop_max", &chip->par.tx_fop_max);
	if (ret) {
		chip->par.tx_fop_max = 0x94;
	}

	ret = of_property_read_u16(np, "tx_fod_th", &chip->par.tx_fod_th);
	if (ret) {
		chip->par.tx_fod_th = 0x7d0;
	}

	return 0;
}

static int cps4021_get_pg_status(struct wireless_charger *wc, bool *value)
{
	struct cps4021_dev *chip = (struct cps4021_dev *)wc->private_d;

	*value = !gpiod_get_value(chip->pg_gpio);

	return 0;
}

static int cps4021_get_wl_vbus(struct wireless_charger *wc, int *value)
{
	struct cps4021_dev *chip = (struct cps4021_dev *)wc->private_d;

	*value = cps4021_get_vout(chip);

	return 0;
}

static int cps4021_get_wl_ibus(struct wireless_charger *wc, int *value)
{
	struct cps4021_dev *chip = (struct cps4021_dev *)wc->private_d;

	*value = cps4021_get_iout(chip);

	return 0;
}

static int cps4021_plug_out(struct wireless_charger *wc)
{
	struct cps4021_dev *chip = wc->private_d;

	memset(chip->hw_info, 0, sizeof(*chip->hw_info));
	chip->site = false;
	chip->ldo_on = false;
	chip->protocol = 0;
	pr_info("%s\n", __func__);

	return 0;
}

static int cps4021_plug_in(struct wireless_charger *wc)
{
	struct cps4021_dev *chip = (struct cps4021_dev *)wc->private_d;

	memset(chip->hw_info, 0, sizeof(*chip->hw_info));

	return 0;
}

static int cps4021_get_wl_hw_info(struct wireless_charger *wc,
	struct wls_hw_info *hw_info)
{
	struct cps4021_dev *chip = (struct cps4021_dev *)wc->private_d;

	memcpy(hw_info, chip->hw_info, sizeof(*hw_info));
	return 0;
}

static int cps4021_set_wl_voltage(struct wireless_charger *wl_chg, int volt, bool shutdown)
{
	struct cps4021_dev *chip = (struct cps4021_dev *)wl_chg->private_d;

	if (!chip->ldo_on) {
		pr_err("wls ldo off, set wls voltage failed");
		return -EINVAL;
	}

	return cps4021_set_vout_val(chip, volt, shutdown);
}

static int cps4021_dump_register(struct wireless_charger *wl_chg)
{
	return 0;
}

static int cps4021_get_bridge_mode(struct wireless_charger *wl_chg)
{
	int ret = 0;
	Alig16 val = {0};
	struct cps4021_dev *chip = wl_chg->private_d;

	ret = cps4021_read_buffer(chip, REG_RX_STATUS, val.ptr, 1);
	if (ret) {
		pr_err("%s fail(%d)\n", __func__, ret);
		return ret;
	}

	pr_info("%s, reg[%x]:0x%x\n", __func__, REG_RX_STATUS, val.value);
	return val.value;
}

static int tx_mode_br_to_half(struct cps4021_dev *chip)
{
	return cps4021_update_bits(chip, REG_CMD, RXCMD_BOOSTER_EN, RXCMD_BOOSTER_EN);
}

static int cps4021_set_bridge_mode(struct wireless_charger *wl_chg)
{
	struct cps4021_dev *chip = wl_chg->private_d;

	tx_mode_br_to_half(chip);
  	
  	return 0;
}

static int cps4021_set_bridge_logic(struct wireless_charger *wl_chg)
{
	int ret = 0;
	Alig16 val = {0};
	struct cps4021_dev *chip = wl_chg->private_d;

	pr_info("%s\n", __func__);

	val.value = 9000;
	ret = cps4021_write_buffer(chip, REG_RX_VOUT_SET, val.ptr, sizeof(val));
	if (ret < 0) {
		pr_err("set volt fail\n");
		return ret;
	}

	return 0;
}

static int tx_mode_br_to_full(struct cps4021_dev *chip)
{
	return cps4021_update_bits(chip, REG_CMD, TXCMD_TO_FULL_BR, TXCMD_TO_FULL_BR);
}

static int tx_mode_enable(struct cps4021_dev *chip)
{
	return cps4021_update_bits(chip, REG_CMD, TXCMD_TO_TX_MODE, TXCMD_TO_TX_MODE);
}

static int tx_mode_pin_enable(struct cps4021_dev *chip)
{
	return cps4021_update_bits(chip, REG_FUNC_EN, TXFUNC_PING_EN, TXFUNC_PING_EN);
}

static int tx_mode_fod_enable(struct cps4021_dev *chip)
{
	return cps4021_update_bits(chip, REG_FUNC_EN, TXFUNC_FOD_EN, TXFUNC_FOD_EN);
}

#if 0
static int tx_mode_q_factor_enable(struct cps4021_dev *chip)
{
	return cps4021_update_bits(chip, REG_FUNC_EN, TXFUNC_Q_VALUE_EN, TXFUNC_Q_VALUE_EN);
}
#endif

static int tx_mode_br_cmd_enable(struct cps4021_dev *chip)
{
	return cps4021_update_bits(chip, REG_FUNC_EN, 0, TXFUNC_BR_CRTL_EN);
}

static int tx_mode_qi_pro_sel(struct cps4021_dev *chip, int pro)
{
	int ret = 0;

	if (pro) {
		ret = cps4021_update_bits(chip, REG_FUNC_EN, TXFUNC_QI_PRO_SEL, TXFUNC_QI_PRO_SEL);
	}

	return ret;
}

static int cps4021_set_tx_mode(struct wireless_charger *wl_chg, bool en, struct tx_config *txc)
{
	int ret = 0;
	Alig32 val = {0};
	struct cps4021_dev *chip = wl_chg->private_d;

	pr_info("%s en:%d, txp:%d, protocol:%d\n", __func__, en, txc->power, txc->protocol);

	if (en) {
		msleep(100);
		if (txc->protocol) {
			ret = cps4021_write_buffer(chip, REG_TX_MAX_POWER, &txc->power, 1);
			if (ret < 0) {
				pr_err("enable tx max power mode fail!\n");
				return ret;
			}
		}

		val.value = chip->par.q_factor;
		ret = cps4021_write_buffer(chip, REG_TX_REF_Q_FACTOR, val.ptr, 2);
		if (ret < 0) {
			pr_err("tx ref q factor fail!\n");
			return ret;
		}

		val.value = chip->par.tx_ping_ocp_th;
		ret = cps4021_write_buffer(chip, REG_TX_PING_OCP_TH, val.ptr, 2);
		if (ret < 0) {
			pr_err("tx ping ocp fail!\n");
			return ret;
		}

		val.value = chip->par.tx_fop_min;
		ret = cps4021_write_buffer(chip, REG_TX_FOP_MIN, val.ptr, 1);
		if (ret < 0) {
			pr_err("tx ping ocp fail!\n");
			return ret;
		}

		val.value = chip->par.tx_fop_max;
		ret = cps4021_write_buffer(chip, REG_TX_FOP_MAX, val.ptr, 1);
		if (ret < 0) {
			pr_err("tx ping ocp fail!\n");
			return ret;
		}

		val.value = chip->par.tx_fod_th;
		ret = cps4021_write_buffer(chip, REG_TX_FOD_TH, val.ptr, 2);
		if (ret < 0) {
			pr_err("tx fod th fail!\n");
			return ret;
		}

		tx_mode_enable(chip);
		//tx_mode_q_factor_enable(chip);
		tx_mode_qi_pro_sel(chip, txc->protocol);
		tx_mode_fod_enable(chip);
		tx_mode_pin_enable(chip);
		tx_mode_br_cmd_enable(chip);
		tx_mode_br_to_full(chip);

		chip->tx_mode_en = true;
	} else {
		chip->tx_mode_en = false;
		val.value = TXCMD_TO_BP_MODE;
		ret = cps4021_write_buffer(chip, REG_CMD, val.ptr, sizeof(val));
		if (ret < 0) {
			pr_err("disable tx mode fail!\n");
			return ret;
		}
	}

	return ret;
}

static int cps4021_get_power(struct wireless_charger *wl_chg, int *val)
{
	u8 power = 0;
	struct cps4021_dev *chip = wl_chg->private_d;

	if (!chip->ldo_on) {
		return -EINVAL;
	}

	cps4021_read(chip, REG_RX_NEGO_POWER, &power);
	*val = power / 2;
	pr_info("%s power:%d,%d\n", __func__, *val, power);

	return 0;
}

static int cps4021_write_unmask_all(struct cps4021_dev *chip)
{
    struct i2c_msg msg_unmask_all[] = {
        {
            .addr = chip->client->addr,
            .flags = chip->client->flags & I2C_M_TEN,
            .buf = wbuf_high_addr_4000,
            .len = ARRAY_SIZE(wbuf_high_addr_4000),
        },
        {
            .addr = chip->client->addr,
            .flags = chip->client->flags & I2C_M_TEN,
            .buf = wbuf_unmask_all,
            .len = ARRAY_SIZE(wbuf_unmask_all),
        },
        {
            .addr = chip->client->addr,
            .flags = chip->client->flags & I2C_M_TEN,
            .buf = wbuf_high_addr_2000,
            .len = ARRAY_SIZE(wbuf_high_addr_2000),
        },
    };

    return cps4021_write_multi_data(chip, msg_unmask_all, ARRAY_SIZE(msg_unmask_all));
}

static int cps4021_write_anaglog_password(struct cps4021_dev *chip)
{
    struct i2c_msg msg_anaglog_password[] = {
        {
            .addr = chip->client->addr,
            .flags = chip->client->flags & I2C_M_TEN,
            .buf = wbuf_high_addr_4000,
            .len = ARRAY_SIZE(wbuf_high_addr_4000),
        },
        {
            .addr = chip->client->addr,
            .flags = chip->client->flags & I2C_M_TEN,
            .buf = wbuf_anaglog_password,
            .len = ARRAY_SIZE(wbuf_anaglog_password),
        },
        {
            .addr = chip->client->addr,
            .flags = chip->client->flags & I2C_M_TEN,
            .buf = wbuf_high_addr_2000,
            .len = ARRAY_SIZE(wbuf_high_addr_2000),
        },
    };

    return cps4021_write_multi_data(chip, msg_anaglog_password, ARRAY_SIZE(msg_anaglog_password));
}

static int cps4021_write_i2c_timeout(struct cps4021_dev *chip)
{
    struct i2c_msg msg_i2c_timeout[] = {
        {
            .addr = chip->client->addr,
            .flags = chip->client->flags & I2C_M_TEN,
            .buf = wbuf_high_addr_4000,
            .len = ARRAY_SIZE(wbuf_high_addr_4000),
        },
        {
            .addr = chip->client->addr,
            .flags = chip->client->flags & I2C_M_TEN,
            .buf = wbuf_i2c_timeout,
            .len = ARRAY_SIZE(wbuf_i2c_timeout),
        },
        {
            .addr = chip->client->addr,
            .flags = chip->client->flags & I2C_M_TEN,
            .buf = wbuf_high_addr_2000,
            .len = ARRAY_SIZE(wbuf_high_addr_2000),
        },
    };

    return cps4021_write_multi_data(chip, msg_i2c_timeout, ARRAY_SIZE(msg_i2c_timeout));
}

static int cps4021_write_remap_restart(struct cps4021_dev *chip)
{
    struct i2c_msg msg_remap_restart[] = {
        {
            .addr = chip->client->addr,
            .flags = chip->client->flags & I2C_M_TEN,
            .buf = wbuf_high_addr_4004,
            .len = ARRAY_SIZE(wbuf_high_addr_4004),
        },
        {
            .addr = chip->client->addr,
            .flags = chip->client->flags & I2C_M_TEN,
            .buf = wbuf_remap,
            .len = ARRAY_SIZE(wbuf_remap),
        },
        {
            .addr = chip->client->addr,
            .flags = chip->client->flags & I2C_M_TEN,
            .buf = wbuf_disable_trim,
            .len = ARRAY_SIZE(wbuf_disable_trim),
        },
        {
            .addr = chip->client->addr,
            .flags = chip->client->flags & I2C_M_TEN,
            .buf = wbuf_restart,
            .len = ARRAY_SIZE(wbuf_restart),
        },
		{
            .addr = chip->client->addr,
            .flags = chip->client->flags & I2C_M_TEN,
            .buf = wbuf_high_addr_2000,
            .len = ARRAY_SIZE(wbuf_high_addr_2000),
        },
    };

    return cps4021_write_multi_data(chip, msg_remap_restart, ARRAY_SIZE(msg_remap_restart));
}

static void cps4021_prepare_for_upgrade(struct cps4021_dev *dev)
{
	/* write password */
	cps4021_write_nbyte(dev, 0xFF84, 0x7A8B, 2);

	/*MCU reset and unmask all address*/
    cps4021_write_nbyte(dev, 0xFF80, 0x08, 1);

	cps4021_write_unmask_all(dev);

    msleep(10);
    /*Write analog register password*/
	cps4021_write_anaglog_password(dev);

    /*Set the I2C timeout to 1s*/
	cps4021_write_i2c_timeout(dev);
}

static int cps4021_write_bootcode_to_sam(struct cps4021_dev *dev)
{
	int ret = 0;
	const struct firmware *bl_bin;
	const void *data = NULL;
	size_t fw_size = 0;

	ret = request_firmware(&bl_bin, "cps4021_bootloader.bin", &dev->client->dev);
	if(ret < 0) {
		pr_err("%s: failed to request bin bootloader (%d)\n", __func__, ret);
		return -EINVAL;
	}

	data = bl_bin->data;
	fw_size = bl_bin->size;

	cps4021_write_buffer(dev, 0x0000, (u8 *)data, fw_size);

	release_firmware(bl_bin);

	cps4021_write_remap_restart(dev);

	msleep(10);

	return 0;
}

static bool cps4021_bootloader_crc_check(struct cps4021_dev *dev)
{
	cps4021_write_nbyte(dev, 0xFF84,0x7A8B,2);  /*write password*/

    /*unmask all address*/
	cps4021_write_unmask_all(dev);
    /*Set the I2C timeout to 1s*/
	cps4021_write_i2c_timeout(dev);

	cps4021_program_cmd_send(dev, CACL_CRC_TEST);

	return cps4021_program_wait_cmd_done(dev);
}

static bool cps4021_load_fw(struct cps4021_dev *dev,
		const struct firmware *fw, bool force)
{
	int ret = 0, i = 0;
	int addr = 0;
	const void *data = fw->data;
	size_t fw_size = fw->size;
	const int cfg_buf_size = 256;
	int buf0_flag, buf1_flag;

	cps4021_write_nbyte(dev, 0xFF82, 0x2000, 2); 
	cps4021_write_nbyte(dev, ADDR_BUF_SIZE, cfg_buf_size, 4);

    /*ERASER MTP*/
	cps4021_program_cmd_send(dev, PGM_ERASER_0);
	ret = cps4021_program_wait_cmd_done(dev);
	if (!ret)
		return ret;

	for (i = 0; i < fw_size / 4 / cfg_buf_size; i++) {
		if (buf0_flag == 0) {
			cps4021_write_buffer(dev, ADDR_BUFFER0, (u8 *)data + addr, cfg_buf_size * 4);
			addr = addr + cfg_buf_size * 4;

			if (buf1_flag == 1) {
				ret = cps4021_program_wait_cmd_done(dev);
				if (!ret) {
					pr_err("%s: --->wirte buf0 data to mtp fail\n", __func__);
					return ret;
				}
				buf1_flag = 0;
			}
			cps4021_program_cmd_send(dev, PGM_BUFFER0);
			buf0_flag = 1;
			continue;
		}

		if (buf1_flag == 0) {
			cps4021_write_buffer(dev, ADDR_BUFFER1, (u8 *)data + addr, cfg_buf_size * 4);
			addr = addr + cfg_buf_size * 4;

			if (buf0_flag == 1) {
				ret = cps4021_program_wait_cmd_done(dev);
				if (!ret) {
					pr_err("%s: --->wirte buf1 data to mtp fail\n", __func__);
					return ret;
				}
				buf0_flag = 0;
			}
			cps4021_program_cmd_send(dev, PGM_BUFFER1);
			buf1_flag = 1;
			continue;
		}
	}

	ret = cps4021_program_wait_cmd_done(dev);
	if (!ret)
		return ret;

	return true;
}

static bool cps4021_app_crc_check(struct cps4021_dev *dev)
{
	cps4021_program_cmd_send(dev, CACL_CRC_APP);
    return cps4021_program_wait_cmd_done(dev);
}

static bool check_upgrade_conditions(const struct firmware *fw, struct cps4021_dev *dev)
{
	int ret = 0;
	int i = 0;
	u16 bin_fw = 0, ic_fw = 0;
	u8 l_byte, h_byte;
	const u8 *data = fw->data;
	size_t fw_size = fw->size;

	if (fw_size < BIN_FW_OFFSET) {
		pr_err("fw bin is not right\n");
		return false;
	}

	bin_fw = ((data[BIN_FW_OFFSET - 1] << 8) | data[BIN_FW_OFFSET]);

	for(i = 0; i < 3; i++) {
		ret = cps4021_read(dev, REG_MINORVER, &l_byte);
		if (ret) {
			pr_err("read reg[%d] fail, ret:%d\n", REG_MINORVER, ret);
			cps4021_prepare_for_upgrade(dev);
		} else {
			break;
		}
	}

	if (ret < 0)
		return false;

	ret = cps4021_read(dev, REG_MAJORVER, &h_byte);
	if (ret) {
		pr_err("read reg[%d] fail, ret:%d\n", REG_MAJORVER, ret);
		return false;
	}

	ic_fw = ((h_byte << 8) | l_byte);
	pr_info("%s, bin_fw:0x%x, in_fw:0x%x\n", __func__, bin_fw, ic_fw);

	if (bin_fw != ic_fw) {
		return true;
	}

	return false;
}

static int cps4021_update_fw(struct wireless_charger *wl_chg,
		const struct firmware *fw, bool force)
{
	int ret = 0;
	struct cps4021_dev *dev = wl_chg->private_d;

	if (!force && !check_upgrade_conditions(fw, dev)) {
		pr_info("fw version condition not satisfied, not need update\n");
		return 0;
	}

	/* step 1 */
	pr_info("update fw step 1\n");
	cps4021_prepare_for_upgrade(dev);

	/* step 2 wirte bootloader code to sam*/
	pr_info("update fw step 2\n");
	ret = cps4021_write_bootcode_to_sam(dev);
	if (ret)
		return ret;

	/* step 3 crc check*/
	pr_info("update fw step 3\n");
	ret = cps4021_bootloader_crc_check(dev);
	if (!ret)
		return ret;

	/* step 4 load firmware to mtp */
	pr_info("update fw step 4\n");
	ret = cps4021_load_fw(dev, fw, true);
	if (!ret)
		return ret;

	/* step 5 check app CRC */
	pr_info("update fw step 5\n");
	ret = cps4021_app_crc_check(dev);
	if (!ret)
		return !ret;

	cps4021_program_cmd_send(dev, SYS_RESET);
	msleep(100);

	pr_info("%s successfully!\n", __func__);
	return 0;
}

static int cps4021_get_fw_version(struct wireless_charger *wl_chg, u32 *ver)
{
	int ret = 0;
	u8 l_byte = 0;
	u8 h_byte = 0;
	struct cps4021_dev *chip = wl_chg->private_d;

	ret = cps4021_read(chip, REG_MINORVER, &l_byte);
	if (ret) {
		pr_err("read reg[%d] fail, ret:%d\n", REG_MINORVER, ret);
		return ret;
	}

	ret = cps4021_read(chip, REG_MAJORVER, &h_byte);
	if (ret) {
		pr_err("read reg[%d] fail, ret:%d\n", REG_MAJORVER, ret);
		return ret;
	}

	*ver = (h_byte << 8) | l_byte;
	return 0;
}

static int cps4021_set_ovp_ctrl(struct wireless_charger *wl_chg, bool en)
{
	struct cps4021_dev *chip = wl_chg->private_d;

	if (IS_ERR_OR_NULL(chip->ovp_ctrl_gpio))
		return -EIO;

	gpiod_set_value(chip->ovp_ctrl_gpio, en);
	return 0;
}

static int cps4021_wired_path_setup(struct wireless_charger *wl_chg, bool en)
{
	struct cps4021_dev *chip = wl_chg->private_d;

	if (IS_ERR_OR_NULL(chip->dv_vctrl_gpio))
		return -EIO;

	gpiod_set_value(chip->dv_vctrl_gpio, en);
	return 0;
}

static int cps4021_get_wired_state(struct wireless_charger *wl_chg, bool *status)
{
	struct cps4021_dev *chip = wl_chg->private_d;

	if (IS_ERR_OR_NULL(chip->wired_gpio))
		return -EIO;

	*status = !gpiod_get_value(chip->wired_gpio);
	return 0;
}

static int cps4021_set_cmd_init(struct cps4021_dev *chip)
{
	int ret = 0;
	Alig32 val = {0};

	val.value = RXCMD_LDOON_INIT | RXCMD_EPPCAL_INIT;
	ret = cps4021_write_buffer(chip, REG_CMD, val.ptr, sizeof(val));
	if (ret < 0) {
		pr_err("%s, write rx cmd reg fail : %d\n", __func__, ret);
	}

	return ret;
}

static int cps4021_set_power_on_state(struct cps4021_dev *chip)
{
	wireless_ic_set_state(chip->wl_chg, WIRELESS_PROBE_END);
	chip->pg = !gpiod_get_value(chip->pg_gpio);
	if (!chip->pg)
		return 0;

	return cps4021_set_cmd_init(chip);
}

static int cps4021_set_sleep_mode(struct wireless_charger *wl_chg, bool en)
{
	struct cps4021_dev *chip = wl_chg->private_d;

	if (IS_ERR_OR_NULL(chip->sleep_gpio)) {
		pr_err("sleep_gpio is null\n");
		return 0;
	}

	pr_info("%s: en = %d\n", __func__, en);
	gpiod_set_value(chip->sleep_gpio, en);

	return 0;
}

static int cps4021_set_drop_vol(struct wireless_charger *wl_chg)
{
	Alig32 val = {0};
	struct cps4021_dev *chip = wl_chg->private_d;

	pr_info("%s\n", __func__);
	val.value = chip->par.drop_vol;
	cps4021_set_vout_val(chip, 7500, 0);
	return cps4021_write_buffer(chip, REG_RX_DROP_VOL_MAX, val.ptr, sizeof(val));
}

static int cps4021_get_adc(struct wireless_charger *wl_chg, enum adc_channel chan)
{
	int i = 0, ret = 0;
	Alig16 val = {0};
	struct cps4021_dev *chip = wl_chg->private_d;

	switch (chan) {
	case PE50_ADCCHAN_VOUT:
		for (i = 0; i <6; i++) {
			ret = cps4021_read_buffer(chip, REG_RX_ADC_VRECT + i * 2, val.ptr, sizeof(val));
			if (ret) {
				return ret;
			}
			pr_info("--->reg[%x] = %d<---\n", REG_RX_ADC_VRECT + i * 2, val.value);
		}
		break;
	case PE50_ADCCHAN_TCHG:
		ret = cps4021_read_buffer(chip, REG_RX_ADC_TMP_DIE, val.ptr, sizeof(val));
		if (ret < 0) {
			pr_err("%s, get tmp fail\n", __func__);
			return ret;
		}
		pr_info("cps4210 tmp:%d\n", val.value);
		break;
	default:
		break;
	}

	return 0;
}

static short cps4021_get_tx_ce_value(struct wireless_charger *wl_chg)
{
	int ret = 0;
	Alig16 ce = {0};
	struct cps4021_dev *chip = wl_chg->private_d;

	ret = cps4021_read_buffer(chip, REG_RX_CE_VAL, ce.ptr, sizeof(ce));
	if (ret < 0) {
		return ret;
	}
	pr_info("--->ce:%d\n", (short)ce.value);
	return (short)ce.value;
}

static int cps4021_get_tx_bridge_voltage(struct wireless_charger *wl_chg)
{
	struct cps4021_dev *chip = wl_chg->private_d;

	return chip->hw_info->tx_bridge_vol_max;
}

static u8 cps4021_get_rx_protocol(struct wireless_charger *wl_chg)
{
	struct cps4021_dev *chip = wl_chg->private_d;

	return chip->protocol;
}

static int cps4021_wirte_reg_data(struct wireless_charger *wl_chg, int addr, u8 data)
{
	struct cps4021_dev *chip = wl_chg->private_d;

	return cps4021_write_buffer(chip, addr, &data, sizeof(data));
}

static int cps4021_read_reg_data(struct wireless_charger *wl_chg, int addr, u8 *data)
{
	u8 val = 0;
	struct cps4021_dev *chip = wl_chg->private_d;

	cps4021_read_buffer(chip, addr, &val, sizeof(val));
	pr_info("%s, %x, %x\n", __func__, addr, val);
	*data = val;

	return 0;
}

static struct wls_ops cps4021_chg_ops = {
	.get_wireless_pg                	= cps4021_get_pg_status,
	.get_wireless_vbus              	= cps4021_get_wl_vbus,
	.get_wireless_ibus              	= cps4021_get_wl_ibus,
	.set_wireless_plug_out          	= cps4021_plug_out,
	.set_wireless_plug_in           	= cps4021_plug_in,
	.set_wireless_sleep         		= cps4021_set_sleep_mode,
	.get_wireless_hw_info				= cps4021_get_wl_hw_info,
	.set_wireless_voltage           	= cps4021_set_wl_voltage,
	.dump_wireless_status           	= cps4021_dump_register,
	.set_wireless_tx_mode           	= cps4021_set_tx_mode,
	.get_wireless_power	        		= cps4021_get_power,
	.set_wireless_fw_update 			= cps4021_update_fw,
	.get_wireless_fw_version 			= cps4021_get_fw_version,
	.set_ovp_ctrl 				 		= cps4021_set_ovp_ctrl,
	.wired_path_setup 					= cps4021_wired_path_setup,
	.get_wired_state 					= cps4021_get_wired_state,
	.get_wireless_adc 					= cps4021_get_adc,
	.get_bridge_mode 					= cps4021_get_bridge_mode,
	.set_bridge_mode 					= cps4021_set_bridge_mode,
	.set_bridge_logic					= cps4021_set_bridge_logic,
	.get_tx_ce_value 					= cps4021_get_tx_ce_value,
	.tx_bridge_voltage 					= cps4021_get_tx_bridge_voltage,
	.set_drop_voltage					= cps4021_set_drop_vol,
	.get_wls_protocol 					= cps4021_get_rx_protocol,
	.wirte_reg_data 					= cps4021_wirte_reg_data,
	.read_reg_data 						= cps4021_read_reg_data,
};

static int cps4021_request_irqs(struct cps4021_dev *chip)
{
	int ret = 0;
	struct i2c_client *client = chip->client;

	chip->dev_irq = gpiod_to_irq(chip->irq_gpio);
	ret = devm_request_threaded_irq(&client->dev, chip->dev_irq,
			NULL, cps4021_irq_handle, IRQF_TRIGGER_FALLING
			| IRQF_ONESHOT, "cps4021_irq", chip);
	if (ret) {
		pr_err("%s:failed to request IRQ %d: %d\n",
				__func__, chip->dev_irq, ret);
		goto exit;
	}

	chip->pg_irq = gpiod_to_irq(chip->pg_gpio);
	ret = devm_request_threaded_irq(&client->dev, chip->pg_irq,
			NULL, cps4021_pg_irq_handle, IRQF_TRIGGER_RISING
			| IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "cps4021_pg", chip);
	if (ret) {
		pr_err("%s:failed to request pg IRQ %d: %d\n",
				__func__, chip->pg_irq, ret);
		goto exit;
	}

	chip->wired_irq = gpiod_to_irq(chip->wired_gpio);
	ret = devm_request_threaded_irq(&client->dev, chip->wired_irq,
			NULL, cps4021_wired_irq_handle, IRQF_TRIGGER_RISING
			| IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "cps4021_wired", chip);
	if (ret) {
		pr_err("%s:failed to request irq:%d, ret:%d\n",
			__func__, chip->wired_irq, ret);
		goto exit;
	}

exit:
	return ret;
}

static int cps4021_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int rc = 0;
	struct cps4021_dev *chip = NULL;
	struct wireless_charger *wl_chg = NULL;

	pr_info("CPS4021 wireless probe, version %s\n", DRIVER_FIRMWARE_VERSION);
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

	chip->regmap = devm_regmap_init_i2c(client, &cps4021_regmap_config);
	if (!chip->regmap) {
		pr_err("CPS4021 parent regmap is missing\n");
		return -EINVAL;
	}

	chip->client 		= client;
	chip->dev           = &client->dev;
	chip->wl_chg		= wl_chg;
	chip->bus.read	    = cps4021_read;
	chip->bus.write     = cps4021_write;
	chip->bus.read_buf  = cps4021_read_buffer;
	chip->bus.write_buf = cps4021_write_buffer;

	i2c_set_clientdata(client, chip);

	mutex_init(&chip->i2c_lock);

	wl_chg->ops = &cps4021_chg_ops;
	wl_chg->name = "cps4021";
	register_wireless_charger_device(wl_chg, (void *)chip);

	rc = cps4021_parse_dt(chip);
	if (rc) {
		pr_info("CPS4021_int parse dt failed\n");
		goto err_parse_dt;
	}

	rc = cps4021_request_irqs(chip);
	if (rc)
		goto err_irq;

	device_init_wakeup(chip->dev, true);

	rc = sysfs_create_group(&client->dev.kobj, &cps4021_sysfs_group);
	cps4021_set_power_on_state(chip);
	pr_info("CPS4021 probed successfully\n");
	return 0;

err_parse_dt:
	mutex_destroy(&chip->i2c_lock);
err_irq:
	unregister_wireless_charger_device(chip->wl_chg);
	return -EINVAL;
}

static void cps4021_remove(struct i2c_client *client)
{
	struct cps4021_dev *dev = i2c_get_clientdata(client);
	pr_info("%s!\n", __func__);
	sysfs_remove_group(&client->dev.kobj, &cps4021_sysfs_group);

	unregister_wireless_charger_device(dev->wl_chg);
	free_irq(dev->dev_irq, dev);
	free_irq(dev->pg_irq, dev);
	free_irq(dev->wired_irq, dev);

	gpiod_put(dev->pg_gpio);
	gpiod_put(dev->irq_gpio);
	gpiod_put(dev->dv_vctrl_gpio);
	gpiod_put(dev->sleep_gpio);

	mutex_destroy(&dev->i2c_lock);

	devm_kfree(&client->dev, dev->hw_info);
	devm_kfree(&client->dev, dev->wl_chg);
	devm_kfree(&client->dev, dev);
}

static int cps4021_i2c_suspend(struct device *dev)
{
	struct cps4021_dev *chip = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		enable_irq_wake(chip->dev_irq);
		enable_irq_wake(chip->pg_irq);
	}

	return 0;
}

static int cps4021_i2c_resume(struct device *dev)
{
	struct cps4021_dev *chip = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		disable_irq_wake(chip->dev_irq);
		disable_irq_wake(chip->pg_irq);
	}

	return 0;
}

static void cps4021_shutdown(struct i2c_client *client)
{
	struct cps4021_dev *chip = i2c_get_clientdata(client);

	if (chip) {
		disable_irq_nosync(chip->pg_irq);
		disable_irq_nosync(chip->dev_irq);
	}
}

static SIMPLE_DEV_PM_OPS(cps4021_pm_ops, cps4021_i2c_suspend, cps4021_i2c_resume);

static const struct i2c_device_id cps4021_dev_id[] = {
	{"cps4021", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, cps4021_dev_id);

static const struct of_device_id cps4021_of_match[] = {
	{.compatible = "wireless_charger,cps4021"},
	{},
};

static struct i2c_driver cps4021_i2c_driver = {
	.driver = {
		.name		= DEVICE_NAME,
		.owner		= THIS_MODULE,
		.of_match_table = cps4021_of_match,
		.pm		= &cps4021_pm_ops,
	},
	.probe	  = cps4021_probe,
	.remove   = cps4021_remove,
	.shutdown = cps4021_shutdown,
	.id_table = cps4021_dev_id,
};

module_i2c_driver(cps4021_i2c_driver);

MODULE_AUTHOR("Transsion Inc.");
MODULE_DESCRIPTION("CPS4021 Wireless Charger");
MODULE_LICENSE("GPL");
