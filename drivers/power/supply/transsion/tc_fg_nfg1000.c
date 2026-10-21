// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 Transsion Inc.
 */

#define pr_fmt(fmt)	"[nfg1000] %s: " fmt, __func__
#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/alarmtimer.h>
#include <linux/kthread.h>
#include "tc_common_class.h"
#include "mtk_battery.h"
#include "tc_common_class.h"

#if IS_ENABLED(CONFIG_TC_FUEL_GAUGE_NFG1000_OTA)
#include "tc_fg_nfg1000.h"
stnfg1000OtaInfo gnfg1000OtaInfo;
static u8 *nfg1000_ad_data_m = NULL;
static u8 *nfg1000_ad_data_s = NULL;
static int nfg1000_force_update_app(struct nfg_fg_chip *bq);
#define I2C_MAX_BUFFER_SIZE 260
#else
#define I2C_MAX_BUFFER_SIZE 32
#endif
static DEFINE_MUTEX(ota_lock);
#define I2C_NO_REG_DATA 0XFF    //no reg op, only send data

#define DF_VER_BIT_START 7
#define DF_NAME_LEN 10

#define nfg_info	pr_info
#define nfg_dbg	pr_debug
#define nfg_err	pr_err
#define nfg_log	pr_err

#define	INVALID_REG_ADDR	0xFF

#define FG_FLAGS_FD				BIT(4)
#define	FG_FLAGS_FC				BIT(5)
#define	FG_FLAGS_DSG				BIT(6)
#define FG_FLAGS_RCA				BIT(9)

enum nfg_fg_reg_idx {
	NFG_FG_REG_CTRL = 0,
	NFG_FG_REG_TEMP,		/* Battery Temperature */
	NFG_FG_REG_VOLT,		/* Battery Voltage */
	NFG_FG_REG_CURR,		/* Battery Current */
	NFG_FG_REG_AI,		/* Average Current */
	NFG_FG_REG_BATT_STATUS,	/* BatteryStatus */
	NFG_FG_REG_TTE,		/* Time to Empty */
	NFG_FG_REG_TTF,		/* Time to Full */
	NFG_FG_REG_FCC,		/* Full Charge Capacity */
	NFG_FG_REG_RM,		/* Remaining Capacity */
	NFG_FG_REG_CC,		/* Cycle Count */
	NFG_FG_REG_SOC,		/* Relative State of Charge */
	NFG_FG_REG_SOH,		/* State of Health */
	NFG_FG_REG_DC,		/* Design Capacity */
	NFG_FG_REG_ALT_MAC,	/* AltManufactureAccess*/
	NFG_FG_REG_MAC_CHKSUM,	/* MACChecksum */
	NUM_REGS,
};

enum nfg_fg_device {
	NFG1000,
	NFG1000_MASTER,
	NFG1000_SLAVE,
};

static const unsigned char *device2str[] = {
	"tran_fg",
	"tran_fg_master",
	"tran_fg_slave",
};

static u8 nfg1000_regs[NUM_REGS] = {
	0x00,	/* CONTROL */
	0x06,	/* TEMP */
	0x08,	/* VOLT */
	0x0C,   /* Battery CURRENT */
	0x14,	/* AVG CURRENT */
	0x0A,	/* BatteryStatus FLAGS */
	0x16,	/* Time to empty */
	0x18,	/* Time to full */
	0x12,	/* Full charge capacity */
	0x10,	/* Remaining Capacity */
	0x2A,	/* CycleCount */
	0x2C,	/*  Relative State of Charge RSOC*/
	0x2E,	/* State of Health */
	0x3C,	/* Design Capacity */
	0x3E,	/* AltManufacturerAccess*/
	0x60,	/* MACChecksum */
};

#define NFG1000_BLOCK_NUM 4
#define NFG1000_DM_SZ 32

struct nfg_fg_chip {
	struct device *dev;
	struct i2c_client *client;

	struct mutex i2c_rw_lock;
	struct mutex data_lock;

	u8 chip;
	u8 regs[NUM_REGS];

	/* status tracking */
	bool batt_fc;
	bool batt_fd;	/* full depleted */
	bool batt_dsg;
	bool batt_rca;	/* remaining capacity alarm */

	int seal_state; /* 0 - Full Access, 1 - Unsealed, 2 - Sealed */
	int batt_tte;
	int batt_soc;
	int batt_fcc;	/* Full charge capacity */
	int batt_rm;	/* Remaining capacity */
	int batt_dc;	/* Design Capacity */
	int batt_volt;
	int batt_temp;
	int temp_flag;
	int batt_curr;
	int batt_flags;
	int now_curr;
	int batt_cyclecnt;	/* cycle count */
	int ttf;

	/* debug */
	int skip_reads;
	int skip_writes;

	int fake_soc;
	int fake_temp;

	/* ctrl gpio */
	int nfg1000_master_gpio;
	int nfg1000_slave_gpio;
	int nfg1000_master_qmax;
	int nfg1000_slave_qmax;
	int chg_en_gpio;
	int stage_diff_gpio;
	int chip_en;
	int retry_times;
	int err_times;

	int BAT_STATUS;

	struct power_supply *fg_psy;
	struct power_supply_desc fg_psy_d;
	struct tran_device *fg_dev;
	struct tran_properties batt_prop;

	bool update_fw;
	bool app_update_suc;
	bool df_update_suc;
	bool i2c_support_dma;
	bool chemic_id_err_recovery_support;
	const char *fg_chemic_id;
	
	bool need_update_df;
	struct delayed_work ota_work;
	struct pinctrl *pinctrl;
	struct pinctrl_state *i2c_scl_gpio_init;
	struct pinctrl_state *i2c_sda_gpio_init;
};

static ssize_t store_bat_en(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct nfg_fg_chip *bq = dev->driver_data;
	int ret = 0;
	unsigned int val = 0;

	if (buf != NULL && size != 0) {
		ret = kstrtouint(buf, 10, &val);
		pr_err("%s: val = %d\n", __func__, val);

		if(val != 0 && val != 1)
			return size;

		switch(bq->chip)
		{
			case NFG1000_MASTER:
				if(bq->nfg1000_master_gpio != U32_MAX)
					gpio_set_value(bq->nfg1000_master_gpio, val);
				break;
			case NFG1000_SLAVE:
				if(bq->nfg1000_slave_gpio != U32_MAX)
					gpio_set_value(bq->nfg1000_slave_gpio, val);
				break;
			default:
				break;
		}
	}

	return size;
}

static ssize_t show_bat_en(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nfg_fg_chip *bq = dev->driver_data;
	int gpio = 0;

	switch(bq->chip)
	{
		case NFG1000_MASTER:
			if(bq->nfg1000_master_gpio != U32_MAX)
				gpio = gpio_get_value(bq->nfg1000_master_gpio);
			break;
		case NFG1000_SLAVE:
			if(bq->nfg1000_slave_gpio != U32_MAX)
				gpio = gpio_get_value(bq->nfg1000_slave_gpio);
			break;
		default:
			break;
	}

	return sprintf(buf, "gpio = %d\n",  gpio);
}

static DEVICE_ATTR(bat_en, 0664, show_bat_en, store_bat_en);

#define I2C_ACCESS_MAX_RETRY	10
static int __fg_read_word(struct i2c_client *client, u8 reg, u16 *val)
{
	s32 ret;
	int retry = 0;
	do {
		ret = i2c_smbus_read_word_data(client, reg);
		retry++;
		if (ret < 0)
			mdelay(10);
	} while (ret < 0 && retry < I2C_ACCESS_MAX_RETRY);

	if (ret < 0) {
		nfg_err("i2c read word fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}

	*val = (u16)ret;
	//pr_info("liml %s,reg=0x%X,*val=%d\n", __func__,reg,*val);
	return 0;
}

static int __fg_write_word(struct i2c_client *client, u8 reg, u16 val)
{
	s32 ret;
	int retry = 0;
	do {
		ret = i2c_smbus_write_word_data(client, reg, val);
		retry++;
		if (ret < 0)
			mdelay(10);
	} while (ret < 0 && retry < I2C_ACCESS_MAX_RETRY);

	if (ret < 0) {
		nfg_err("i2c write word fail:can't write 0x%02X to reg 0x%02X\n", val, reg);
		return ret;
	}

	return 0;
}

static int fg_read_word(struct nfg_fg_chip *bq, u8 reg, u16 *val)
{
	int ret;

	if (bq->skip_reads) {
		*val = 0;
		return 0;
	}

	mutex_lock(&bq->i2c_rw_lock);
	ret = __fg_read_word(bq->client, reg, val);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;
}

__maybe_unused static int fg_write_word(struct nfg_fg_chip *bq, u8 reg, u16 val)
{
	int ret;

	if (bq->skip_writes)
		return 0;

	mutex_lock(&bq->i2c_rw_lock);
	ret = __fg_write_word(bq->client, reg, val);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;
}

static u8 CalcChecksum(u8 *inbuf,int len)
{
	int i;
	u8 xorsum = 0;
	for(i=0;i<len;i++) {
		xorsum +=inbuf[i];
	}
	xorsum = 0xFF - xorsum;
	return xorsum;
}

static int nfg1000_i2c_read(struct nfg_fg_chip *bq,u16 I2caddr,u8 reg,u8 *poutbuf,u32 len)
{
	struct i2c_msg msg[2];
	struct i2c_client *client = bq->client;
	int ret = 0;
	int retry = 0;
	u8 msg_len;

	if(!poutbuf)
	{
		dev_err(bq->dev, "%s:condition is error!!\n", __func__);
		return -EINVAL;
	}

	if(reg == I2C_NO_REG_DATA)
	{
		msg[0].addr = client->addr;
		msg[0].flags = I2C_M_RD;
		msg[0].buf = poutbuf;
		msg[0].len = len;
		msg_len = 1;
	}
	else
	{
		msg[0].addr = client->addr;
		msg[0].flags = 0;
		msg[0].buf = &reg;
		msg[0].len = sizeof(reg);

		msg[1].addr = client->addr;
		msg[1].flags = I2C_M_RD;
		msg[1].buf = poutbuf;
		msg[1].len = len;

		msg_len = 2;
	}

	do {
		ret = i2c_transfer(client->adapter, msg, msg_len);
		if (ret == msg_len)
			break;
		mdelay(10);
	} while (retry++ < I2C_ACCESS_MAX_RETRY);
	if(ret < 0) {
		dev_err(bq->dev, "%s:write reg:0x%x failed!!\n",__func__, reg);
	}

	return ret == msg_len ? 0 : -1;
}

static int nfg1000_i2c_write(struct nfg_fg_chip *bq,u16 I2caddr,u8 reg,u8 *pinbuf,u32 len)
{
	struct i2c_msg msg;
	struct i2c_client *client = bq->client;
	int ret = 0;
	int retry = 0;
	u8 databuf[I2C_MAX_BUFFER_SIZE] = {0};
	u8 msg_len;

	if(!pinbuf || (len>=I2C_MAX_BUFFER_SIZE))
	{
		dev_err(bq->dev, "%s:condition is error!!\n", __func__);
		return -EINVAL;
	}

	if(reg == I2C_NO_REG_DATA)
	{
		if(len)
			memcpy(&databuf[0],pinbuf,len);
		msg.len = len;
	}
	else
	{
		databuf[0] = reg;
		if(len)
			memcpy(&databuf[1],pinbuf,len);
		msg.len = len+1;
	}

	msg.addr = client->addr,
	msg.flags = 0;
	msg.buf = databuf;
	msg_len = 1;

	do {
		ret = i2c_transfer(client->adapter, &msg, msg_len);
		if (ret == msg_len)
			break;
		mdelay(10);
	} while (retry++ < I2C_ACCESS_MAX_RETRY);
	if(ret < 0) {
		dev_err(bq->dev, "%s:reg:0x%x failed=%d\n",__func__, reg, ret);
	}

	return ret == msg_len ? 0 : -1;
}
/*
static int fg_mac_read_block8(struct nfg_fg_chip *dev, u16 reg, u8 *poutbuf, u32 len)
{
	u8 reg_data[2];
	u8 block_data[36];
	int ret;

	if(!poutbuf)
	{
		dev_err(dev->dev, "%s:poutbuf is NULL!!\n", __func__);
		return -1;
	}

	reg_data[0] = (reg >> 0) & 0xFF;
	reg_data[1] = (reg >> 8) & 0xFF;
	ret = nfg1000_i2c_write(dev,0x55,0x3E,reg_data,2);
	if(ret < 0)
	{
		dev_err(dev->dev, "%s:write reg:%d failed!!=%d\n",__func__, reg, ret);
		return -1;
	}
	msleep(10);

	ret = nfg1000_i2c_read(dev,0x55,0x3E,block_data,8);
	if(ret < 0) {
		dev_err(dev->dev, "%s:read reg1:%d failed!!=%d\n",__func__, reg, ret);
		return -1;
	}
	ret = nfg1000_i2c_read(dev,0x55,0x46,&block_data[8],8);
	if(ret < 0) {
		dev_err(dev->dev, "%s:read reg2:%d failed!!=%d\n",__func__, reg, ret);
		return -1;
	}
	ret = nfg1000_i2c_read(dev,0x55,0x4E,&block_data[16],8);
	if(ret < 0) {
		dev_err(dev->dev, "%s:read reg3:%d failed!!=%d\n",__func__, reg, ret);
		return -1;
	}
	ret = nfg1000_i2c_read(dev,0x55,0x56,&block_data[24],8);
	if(ret < 0) {
		dev_err(dev->dev, "%s:read reg4:%d failed!!=%d\n",__func__, reg, ret);
		return -1;
	}
	ret = nfg1000_i2c_read(dev,0x55,0x5E,&block_data[32],4);
	if(ret < 0) {
		dev_err(dev->dev, "%s:read reg5:%d failed!!=%d\n",__func__, reg, ret);
		return -1;
	}

	if((reg_data[0] != block_data[0]) || (reg_data[1] != block_data[1]) ||
		(block_data[34] != CalcChecksum(block_data,34) || (block_data[35]!=len+4)))
	{
		dev_err(dev->dev, "%s:command:(%d,%d)(%d,%d)(%d,%d)(%d,%d) failed!!\n", __func__,
			reg_data[0],block_data[0],reg_data[1],block_data[1],block_data[35],len+4,
			CalcChecksum(block_data,34), block_data[34]);
		return -2;
	}
	memcpy(poutbuf,&block_data[2],len);
	mdelay(5);

	return 0;
}
*/

static int fg_mac_read_block(struct nfg_fg_chip *dev, u16 reg, u8 *poutbuf, u32 len)
{
	u8 reg_data[2];
	u8 block_data[36];
	int ret;

	if(!poutbuf)
	{
		dev_err(dev->dev, "%s:poutbuf is NULL!!\n", __func__);
		return -1;
	}

	reg_data[0] = (reg >> 0) & 0xFF;
	reg_data[1] = (reg >> 8) & 0xFF;
	ret = nfg1000_i2c_write(dev,0x55,0x3E,reg_data,2);
	if(ret < 0)
	{
		dev_err(dev->dev, "%s:write reg:%d failed!!=%d\n",__func__, reg, ret);
		return -1;
	}
	msleep(10);

	ret = nfg1000_i2c_read(dev,0x55,0x3E,block_data,36);
	if(ret < 0)
	{
		dev_err(dev->dev, "%s:read reg:%d failed!!=%d\n",__func__, reg, ret);
		return -1;
	}

	if((reg_data[0] != block_data[0]) || (reg_data[1] != block_data[1]) ||
		(block_data[34] != CalcChecksum(block_data,34) || (block_data[35]!=len+4)))
	{
		dev_err(dev->dev, "%s:command:(%d,%d)(%d,%d)(%d,%d)(%d,%d) failed!!\n", __func__,
			reg_data[0],block_data[0],reg_data[1],block_data[1],block_data[35],len+4,
			CalcChecksum(block_data,34), block_data[34]);
		return -2;
	}
	memcpy(poutbuf,&block_data[2],len);
	mdelay(5);

	return 0;
}

static int fg_read_status(struct nfg_fg_chip *bq)
{
	int ret;
	u16 flags;

	ret = fg_read_word(bq, bq->regs[NFG_FG_REG_BATT_STATUS], &flags);
	if (ret < 0) {
		pr_err("%s: failed to read batt status, flags:%d, ret:%d\n", __func__, flags, ret);
		bq->batt_flags = ret;
		return ret;
	}

	mutex_lock(&bq->data_lock);
	bq->batt_fc		= !!(flags & FG_FLAGS_FC);
	bq->batt_fd		= !!(flags & FG_FLAGS_FD);
	bq->batt_rca		= !!(flags & FG_FLAGS_RCA);
	bq->batt_dsg		= !!(flags & FG_FLAGS_DSG);
	bq->batt_flags          = flags;
	mutex_unlock(&bq->data_lock);

	if (bq->chip == NFG1000_MASTER) {
		pr_info("master_dump:flags:0x%X, batt_fc:%d, batt_fd:%d, batt_rca:%d, batt_dsg:%d\n",
			flags, bq->batt_fc, bq->batt_fd, bq->batt_rca, bq->batt_dsg);
	} else {
		pr_info("slave_dump:flags:0x%X, batt_fc:%d, batt_fd:%d, batt_rca:%d, batt_dsg:%d\n",
			flags, bq->batt_fc, bq->batt_fd, bq->batt_rca, bq->batt_dsg);
	}

	//fg_debug_dump_regs(bq);

	return 0;
}

static int fg_read_rsoc(struct nfg_fg_chip *bq)
{
	int ret;
	u16 soc = 0;

	ret = fg_read_word(bq, bq->regs[NFG_FG_REG_SOC], &soc);
	if (ret < 0) {
		nfg_err("could not read RSOC, ret = %d\n", ret);
		return ret;
	}

	return soc;

}

static int fg_read_temperature(struct nfg_fg_chip *bq)
{
	int ret;
	u16 temp = 0;
	int temp_val;

	ret = fg_read_word(bq, bq->regs[NFG_FG_REG_TEMP], &temp);
	if (ret < 0) {
		nfg_err("could not read temperature, ret = %d\n", ret);
		return 280;
	}

	temp_val = temp - 2730;

	if (!bq->temp_flag && temp_val < 700 && temp_val > -300) {
		pr_info("bq->temp_flag set true, temp_val:%d\n", temp_val);
		bq->temp_flag = true;
		return temp_val;
	}

	if (!bq->temp_flag) {
		pr_info("bq->temp_flag not true, return 280\n");
		return 280;
	}

	temp_val = abs(temp_val - bq->batt_temp);
	if (temp_val > 300) {
		nfg_err("ignore this temp, raw:%d, temp:%d\n", temp, temp - 2730);
		return bq->batt_temp;
	}

	/* K -> C */
	return temp - 2730;
}

static int fg_read_volt(struct nfg_fg_chip *bq)
{
	int ret;
	u16 volt = 0;

	ret = fg_read_word(bq, bq->regs[NFG_FG_REG_VOLT], &volt);
	if (ret < 0) {
		return ret;
	}

	return volt;

}

static int fg_read_time_to_full(struct nfg_fg_chip *bq, int *time)
{
	int ret;
	u16 value = 0;

	ret = fg_read_word(bq, bq->regs[NFG_FG_REG_TTF], &value);
	if (ret < 0) {
		nfg_err("could not read TTF, ret = %d\n", ret);
		return ret;
	}

	*time = (int)value;

	pr_info("read time to full TTF: %d\n", *time);

	return ret;
}

static int fg_read_now_current(struct nfg_fg_chip *bq, int *curr)
{
	int ret;
	u16 now_curr = 0;

	ret = fg_read_word(bq, bq->regs[NFG_FG_REG_CURR], &now_curr);
	if (ret < 0) {
		nfg_err("could not read now current, ret = %d\n", ret);
		return ret;
	}
	*curr = (int)((s16)now_curr);

	return ret;
}

static int fg_read_current(struct nfg_fg_chip *bq, int *curr)
{
	int ret;
	u16 avg_curr = 0;

	ret = fg_read_word(bq, bq->regs[NFG_FG_REG_AI], &avg_curr);
	if (ret < 0) {
		nfg_err("could not read current, ret = %d\n", ret);
		return ret;
	}
	*curr = (int)((s16)avg_curr);

	return ret;
}

static int fg_read_fcc(struct nfg_fg_chip *bq)
{
	int ret;
	u16 fcc;

	if (bq->regs[NFG_FG_REG_FCC] == INVALID_REG_ADDR) {
		nfg_err("FCC command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[NFG_FG_REG_FCC], &fcc);

	if (ret < 0)
		nfg_err("could not read FCC, ret=%d\n", ret);

	return fcc;
}

static int fg_read_dc(struct nfg_fg_chip *bq)
{
	int ret;
	u16 dc;

	if (bq->regs[NFG_FG_REG_DC] == INVALID_REG_ADDR) {
		nfg_err("DesignCapacity command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[NFG_FG_REG_DC], &dc);
	if (ret < 0) {
		nfg_err("could not read DC, ret=%d\n", ret);
		return ret;
	}

	return dc;
}

static int fg_read_rm(struct nfg_fg_chip *bq)
{
	int ret;
	u16 rm;

	if (bq->regs[NFG_FG_REG_RM] == INVALID_REG_ADDR) {
		nfg_err("RemainingCapacity command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[NFG_FG_REG_RM], &rm);

	if (ret < 0) {
		nfg_err("could not read DC, ret=%d\n", ret);
		return ret;
	}

	return rm;

}

static int fg_read_cyclecount(struct nfg_fg_chip *bq)
{
	int ret;
	u16 cc;

	if (bq->regs[NFG_FG_REG_CC] == INVALID_REG_ADDR) {
		nfg_err("Cycle Count not supported!\n");
		return -1;
	}

	ret = fg_read_word(bq, bq->regs[NFG_FG_REG_CC], &cc);

	if (ret < 0) {
		nfg_err("could not read Cycle Count, ret=%d\n", ret);
		return ret;
	}

	return cc;
}

static int fg_read_tte(struct nfg_fg_chip *bq)
{
	int ret;
	u16 tte;

	if (bq->regs[NFG_FG_REG_TTE] == INVALID_REG_ADDR) {
		nfg_err("Time To Empty not supported!\n");
		return -1;
	}

	ret = fg_read_word(bq, bq->regs[NFG_FG_REG_TTE], &tte);

	if (ret < 0) {
		nfg_err("could not read Time To Empty, ret=%d\n", ret);
		return ret;
	}

	if (ret == 0xFFFF)
		return -ENODATA;

	return tte;
}

static int fg_get_batt_status(struct nfg_fg_chip *bq)
{
	fg_read_status(bq);

	if (bq->batt_fc)
		return POWER_SUPPLY_STATUS_FULL;
	else if (bq->batt_dsg)
		return POWER_SUPPLY_STATUS_DISCHARGING;
	else if (bq->batt_curr > 0)
		return POWER_SUPPLY_STATUS_CHARGING;
	else
		return POWER_SUPPLY_STATUS_NOT_CHARGING;
}

enum {
	FULL_UNSEAL,
	UNSEAL,
	SEAL = 3,
};

static int fg_device_name_check(struct nfg_fg_chip *dev)
{
	int ret;
	u16 device_name_cmd = 0x004a;
	u8 read_out[32];
	u8 p[3] = {0x54, 0x53, 0x4e};
	u8 *p1 = "NFG1000S01";
	int i;

#if IS_ENABLED(CONFIG_TC_FUEL_GAUGE_NFG1000_OTA)
	ret = fg_mac_read_block(dev, device_name_cmd, read_out, 16);
	if(ret) {
		dev_info(dev->dev, "fg_ota:force updata ota\n");
		mutex_lock(&ota_lock);
		if (nfg1000_force_update_app(dev)) {
			mutex_unlock(&ota_lock);
			dev_err(dev->dev, "%s, force update failed = %d\n", __func__, ret);
			return ret;
		}
		mutex_unlock(&ota_lock);
	}
#endif
	ret = fg_mac_read_block(dev, device_name_cmd, read_out, 16);
	if(ret) {
		dev_err(dev->dev, "%s, failed = %d\n", __func__, ret);
		return ret;
	}
	dev_info(dev->dev, "%s, device name: %s\n", __func__, read_out);
	for(i = 0; i < 3; i++) {
		if (read_out[i] != p[i]) {
			break;
		}
	}
	if (i >= 3)
		return 0;
	else
		dev_err(dev->dev, "device name not match\n");
	for(i = 0; i < DF_VER_BIT_START; i++) {
		if (read_out[i] != p1[i]) {
			dev_err(dev->dev, "device name not match too\n");
			return -1;
		}
	}
	return 0;
}

static int fg_mfn_check(struct nfg_fg_chip *dev)
{
	int ret;
	u8 read_out[32];
	u16 device_mfn_cmd = 0x004c;
	u8 mfn_info[3] = {0x4d, 0x50, 0x43};
	int i;

	ret = fg_mac_read_block(dev, device_mfn_cmd, read_out, 15);
	if(ret) {
		dev_err(dev->dev, "%s, failed = %d\n", __func__, ret);
		return ret;
	}
	dev_info(dev->dev, "%s, mfn name: %s\n", __func__, read_out);
	for(i = 0; i < 3; i++) {
		if (read_out[i] != mfn_info[i]) {
			dev_err(dev->dev, "mfn not match\n");
			return -1;
		}
	}
	return 0;
}

static bool nfg_get_fast_charge_status(struct nfg_fg_chip *bq)
{
	u8 read_out[4];
	u16 fast_chg_st_cmd = 0x0055;
	int ret;

	ret = fg_mac_read_block(bq, fast_chg_st_cmd, read_out, sizeof(read_out));
	if(ret) {
		dev_err(bq->dev, "%s, failed = %d\n", __func__, ret);
		return false;
	}
	if (read_out[2] & 0x20)
		return true;

	return false;
}

static int nfg_en_fast_charge(struct nfg_fg_chip *bq, bool en)
{
	int ret;
	int cmd;
	u8 reg_data[2];

	if (nfg_get_fast_charge_status(bq) == en)
		return 0;

	if (en)
		cmd = 0x003E; //set
	else
		cmd = 0x003F; //clear

	reg_data[0] = (cmd >> 0) & 0xFF;
	reg_data[1] = (cmd >> 8) & 0xFF;
	ret = nfg1000_i2c_write(bq, bq->client->addr, 0x3E, reg_data, 2);
	if(ret < 0)
	{
		dev_err(bq->dev, "%s:set fast charge failed!!=%d\n",__func__, ret);
		return -1;
	}
	dev_info(bq->dev, "%s: fast charge en = %d\n",__func__, en);

	return ret;
}

//ota func start
#if IS_ENABLED(CONFIG_TC_FUEL_GAUGE_NFG1000_OTA)
static u32 crc_table[256];
static void init_crc_table(void)
{
	u32 c;
	u32 i, j;

	for (i = 0; i < 256; i++) {
		c = i;
		for (j = 0; j < 8; j++) {
			if (c & 1)
				c = 0xedb88320L ^ (c >> 1);
			else
				c = c >> 1;
		}
		crc_table[i] = c;
	}
}

static u32 crc32(u32 crc, const char *buffer, u32 size)
{
	u32 i;

	for (i = 0; i < size; i++)
	{
		crc = crc_table[(crc ^ buffer[i]) & 0xff] ^ (crc >> 8);
	}
	crc = crc ^ 0xFFFFFFFF;
	return crc ;
}
/*
static int fg_get_seal_st(struct nfg_fg_chip *dev)
{
	int ret;
	struct i2c_client *client = dev->client;
	u8 reg_data[16];

	ret = nfg1000_i2c_read(dev, client->addr, 0x0, reg_data, 15);
	dev_info(dev->dev, "fg_ota:get seal st = 0x%x\n", reg_data[15]);
	ret = reg_data[15] & 0x3;
	return ret;
}
*/
static int of_get_fw(struct nfg_fg_chip *bq)
{
	struct device_node *node = bq->dev->of_node;
	int ret;
	int nfg1000_ad_data_len_m = 0;
	int  nfg1000_ad_data_len_s = 0;

	if (bq->chip == NFG1000_MASTER) {
		ret = of_property_read_u32(node, "nfg1000_ad_data_len_m", &nfg1000_ad_data_len_m);
		if (ret)
			return -EINVAL;
		if (nfg1000_ad_data_len_m > CODE_SIZE)
			return -EINVAL;
		dev_info(bq->dev, "fg_ota: nfg1000_ad_data_len_m = %d\n",nfg1000_ad_data_len_m);

		nfg1000_ad_data_m = kzalloc(nfg1000_ad_data_len_m, GFP_KERNEL);
		if (IS_ERR_OR_NULL(nfg1000_ad_data_m))
			return -EINVAL;
		//ret = of_property_read_u8_array(node, "nfg1000_ad_data_m", nfg1000_ad_data_m, nfg1000_ad_data_len_m);
		ret = of_property_read_variable_u8_array(node, "nfg1000_ad_data_m", nfg1000_ad_data_m,
				nfg1000_ad_data_len_m, nfg1000_ad_data_len_m);
		if (ret < 0) {
			return -EINVAL;
		} else if (ret != nfg1000_ad_data_len_m) {
			dev_info(bq->dev, "fg_ota: data len no equal ret:%d\n", ret);
			return -EINVAL;
		}
		return nfg1000_ad_data_len_m;
	}

	//slave
	if (bq->chip == NFG1000_SLAVE) {
		ret = of_property_read_u32(node, "nfg1000_ad_data_len_s", &nfg1000_ad_data_len_s);
		if (ret)
			return -EINVAL;
		if (nfg1000_ad_data_len_s > CODE_SIZE)
			return -EINVAL;
		dev_info(bq->dev, "fg_ota: nfg1000_ad_data_len_s = %d\n",nfg1000_ad_data_len_s);

		nfg1000_ad_data_s = kzalloc(nfg1000_ad_data_len_s, GFP_KERNEL);
		if (IS_ERR_OR_NULL(nfg1000_ad_data_s))
			return -EINVAL;
		ret = of_property_read_variable_u8_array(node, "nfg1000_ad_data_s", nfg1000_ad_data_s,
				nfg1000_ad_data_len_s, nfg1000_ad_data_len_s);
		if (ret < 0) {
			return -EINVAL;
		} else if (ret != nfg1000_ad_data_len_s) {
			dev_info(bq->dev, "fg_ota: data len no equal ret:%d\n", ret);
			return -EINVAL;
		}
		return nfg1000_ad_data_len_s;
	}
	return 0;
}

static int nfg1000_ota_init(struct nfg_fg_chip *bq,const char *input_data,int data_length)
{
	memset(&gnfg1000OtaInfo,0,sizeof(stnfg1000OtaInfo));

	if(data_length == CODE_SIZE) //53248
	{
		gnfg1000OtaInfo.dataflashstartnumber = APP_SIZE;
		gnfg1000OtaInfo.code_update_type = CODE_UPDATE_ALL;
		gnfg1000OtaInfo.chemIDstartnumber = CHEMID_START_ALL;
		gnfg1000OtaInfo.chemIDstopnumber = CHEMID_STOP_ALL;
	}
	else if(data_length == APP_SIZE) //49152
	{
		gnfg1000OtaInfo.code_update_type = CODE_UPDATE_APP;
	}
	else if(data_length == DATAFLASH_SIZE) //4096
	{
		gnfg1000OtaInfo.dataflashstartnumber = 0;
		gnfg1000OtaInfo.code_update_type = CODE_UPDATE_DATAFLASH;
		gnfg1000OtaInfo.chemIDstartnumber = CHEMID_START_DATAFLASH;
		gnfg1000OtaInfo.chemIDstopnumber = CHEMID_STOP_DATAFLASH;
	}
	else
	{
		printk("nfg1000_ota_init:input data length error!%d\n",data_length);
		return -1;
	}

	gnfg1000OtaInfo.unseal_key = NFG1000_UNSEAL_KEY;
	gnfg1000OtaInfo.mcu_auth_code = NFG1000_CHIP_NAME;
	gnfg1000OtaInfo.regs = nfg1000_regs;
	gnfg1000OtaInfo.dev = bq;
	gnfg1000OtaInfo.I2caddr = I2C_DEVICE_NFG1000_SLAVEADDR;
	gnfg1000OtaInfo.fileBuf = input_data;
	gnfg1000OtaInfo.UpdatefileLen = APP_SIZE;
	gnfg1000OtaInfo.appstartaddr = 0;
	gnfg1000OtaInfo.dataflashstartaddr = 0x30000;

	return 0;
}

static int nfg1000_i2c_BLOCK_command_read_with_CHECKSUM(struct nfg_fg_chip *dev,u16 reg,u8 *poutbuf,u32 len)
{
	u8 reg_data[2];
	u8 block_data[36];
	int ret;

	reg_data[0] = (reg >> 0) & 0xFF;
	reg_data[1] = (reg >> 8) & 0xFF;

	if(!poutbuf)
	{
		dev_err(dev->dev, "%s:poutbuf is NULL!!\n", __func__);
		return -1;
	}

	ret = nfg1000_i2c_write(dev,0x55,0x3E,reg_data,2);
	if(ret < 0)
	{
		dev_err(dev->dev, "%s:read reg:%d failed!!\n",__func__, reg);
		return -1;
	}
	ret = nfg1000_i2c_read(dev,0x55,0x3E,block_data,36);
	if(ret < 0)
	{
		dev_err(dev->dev, "%s:read reg:%d failed!!\n",__func__, reg);
		return -1;
	}

	if((reg_data[0] != block_data[0]) || (reg_data[1] != block_data[1]) ||
		(block_data[34] != CalcChecksum(block_data,34)) || (block_data[35] != len+4))
	{
		dev_err(dev->dev, "%s:command:(%d,%d)(%d,%d)(%d,%d)(%d,%d) failed!!\n", __func__,
			reg_data[0],block_data[0],reg_data[1],block_data[1],block_data[35],len+4,
			CalcChecksum(block_data,34), block_data[34]);
		return -2;
	}

	memcpy(poutbuf,&block_data[2],len);

	mdelay(NFG1000_com_WAIT_TIME);

	return 0;
}

static int nfg1000_i2c_BLOCK_command_write_with_CHECKSUM(struct nfg_fg_chip *dev,u16 reg,const char *pinbuf,u32 len)
{
	u8 databuf[36] = {0};
	int ret;

	databuf[0] = (reg >> 0) & 0xFF;
	databuf[1] = (reg >> 8) & 0xFF;

	if(!pinbuf || (len>=I2C_BLOCK_MAX_BUFFER_SIZE))
	{
		printk("nfg1000_i2c_BLOCK_command_write_with_CHECKSUM:condition is error!!\n");
		return -1;
	}

	if(len)
		memcpy(&databuf[2],pinbuf,len);
	databuf[34] = CalcChecksum(&databuf[0],34);
	databuf[35] = len+4;

	ret = nfg1000_i2c_write(dev,0x55,0x3E,databuf,36);
	if(ret < 0)
	{
		printk("nfg1000_i2c_BLOCK_command_write_with_CHECKSUM:write reg:%d failed!!\n",reg);
		return -1;
	}

	mdelay(NFG1000_com_WAIT_TIME);

	return 0;
}

static int nfg1000_ota_unseal(stnfg1000OtaInfo *di)
{
	int ret;int i=0;
	u8 u8pwd[2] = {0};
	u8 seal_state_read[32] = {0};
	u16 seal_state_cmd = 0x0054;

	if (di->unseal_key == 0)
	{
		printk("nfg1000_battery_unseal:unseal failed due to missing key\n");
		return -ERROR_CODE_CHECKSUM;
	}

	for(i = 0;i < 3;i++)
	{
		//unseal_key:0x01020304 Big-endian mode
		u8pwd[0] = ((NFG1000_UNSEAL_KEY>>24))&0xff;
		u8pwd[1] = (NFG1000_UNSEAL_KEY>>16)&0xff;
		ret = nfg1000_i2c_write(di->dev,di->I2caddr,di->regs[NFG_FG_REG_ALT_MAC],u8pwd,2);
		if(ret)
		{
			printk("nfg1000_ota_program_unseal_state_check:unseal write error!%x\n", ret);
			return -ERROR_CODE_I2C_WRITE;
		}
		mdelay(NFG1000_write_WAIT_TIME);

		u8pwd[0] = ((NFG1000_UNSEAL_KEY>>8))&0xff;
		u8pwd[1] = ((NFG1000_UNSEAL_KEY>>0))&0xff;
		ret = nfg1000_i2c_write(di->dev,di->I2caddr,di->regs[NFG_FG_REG_ALT_MAC],u8pwd,2);
		if(ret)
		{
			printk("nfg1000_ota_program_unseal_state_check:unseal write error!%x\n", ret);
			return -ERROR_CODE_I2C_WRITE;
		}
		mdelay(NFG1000_write_WAIT_TIME);

		u8pwd[0] = ((NFG1000_FULL_KEY>>24))&0xff;
		u8pwd[1] = ((NFG1000_FULL_KEY>>16))&0xff;
		ret = nfg1000_i2c_write(di->dev,di->I2caddr,di->regs[NFG_FG_REG_ALT_MAC],u8pwd,2);
		mdelay(NFG1000_write_WAIT_TIME);

		u8pwd[0] = ((NFG1000_FULL_KEY>>8))&0xff;
		u8pwd[1] = ((NFG1000_FULL_KEY>>0))&0xff;
		ret = nfg1000_i2c_write(di->dev,di->I2caddr,di->regs[NFG_FG_REG_ALT_MAC],u8pwd,2);
		mdelay(NFG1000_write_WAIT_TIME);

		ret = nfg1000_i2c_BLOCK_command_read_with_CHECKSUM(di->dev,seal_state_cmd ,seal_state_read,4);
		if(ret)
		{
			printk("nfg1000_ota_program_unseal_state_check:unseal state read error!%x\n", ret);
			mdelay(NFG1000_boot_WAIT_TIME);
			return -ERROR_CODE_I2C_WRITE;
		}

		if((seal_state_read[1] & 0x03) == 0x01)
		{
			break;
		}
	}

	if(i == 3)
	{
		printk("nfg1000_ota_program_unseal_state_check:unseal opeartion error!%x\n", ret);
		return -ERROR_CODE_CHECKSUM;
	}

	return 0;
}

static int nfg1000_GetReturnCode(stnfg1000OtaInfo *di)
{
	int ret = 0;
	u8 uReCode[2] = {0};

	ret = nfg1000_i2c_read(di->dev,di->I2caddr,I2C_NO_REG_DATA,uReCode,1);
	if(ret)
	{
		printk("nfg1000_GetReturnCode:read reg: %x error!!\n", I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_READ;
	}

	if(uReCode[0] != NFG1000_SUCESS_CODE)
	{
		printk("nfg1000_GetReturnCode:return code error:%x!!\n",uReCode[0]);
		return -ERROR_CODE_RERUNCODE;
	}

	return 0;
}

static int nfg1000_ota_program_step0_firmware_version_check(stnfg1000OtaInfo *di)
{
	int ret = 0;
	u8 i = 0;
	u8 read_out[16] = {0};
	u16 fw_ver_cmd = 0x0002;

	ret = nfg1000_i2c_BLOCK_command_read_with_CHECKSUM(di->dev, fw_ver_cmd, read_out, 12);
	if(ret)
	{
		mdelay(NFG1000_boot_WAIT_TIME);
		return -ERROR_CODE_I2C_WRITE;
	}
	for(i = 0;i < 12;i++)
	{
		if(read_out[i] != di->fileBuf[FIRMWARE_VERSION + i]) {
			printk("fg_ota:fw ver not match%d = 0x%x,%x\n",
				i,read_out[i],di->fileBuf[FIRMWARE_VERSION + i]);
			break;
		}
	}
	if (i > 8) {
		printk("%s: fg_ota:fw is same, don't update fw\n", __func__);
		return -1;
	}

	return 0;
}

static int nfg1000_ota_program_boot_check(stnfg1000OtaInfo *di)
{

	int ret = 0;
	u8 u8Data[2] = {0};
	u8 uReCode[2] = {0};
	u8 retry_cnt = 0;

	printk("fg_ota: %s enter %d\n", __func__, __LINE__);
	u8Data[0] = 0x3f;
	for(retry_cnt = 0;retry_cnt < 3;retry_cnt++)
	{
		ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,1);
		ret = nfg1000_i2c_read(di->dev,di->I2caddr,I2C_NO_REG_DATA,uReCode,1);
		if(uReCode[0] == NFG1000_SUCESS_CODE)
		{
			break;
		}
	}
	if(uReCode[0] != NFG1000_SUCESS_CODE)
	{
		mdelay(NFG1000_boot_WAIT_TIME);
		for(retry_cnt = 0;retry_cnt < 3;retry_cnt++)
   		{
			ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,1);
			ret = nfg1000_i2c_read(di->dev,di->I2caddr,I2C_NO_REG_DATA,uReCode,1);
			if(uReCode[0] == NFG1000_SUCESS_CODE)
			{
			    break;
			}
		}
	}

	if(uReCode[0] != NFG1000_SUCESS_CODE)
	{
		printk("fg_ota:%s,return code error:%x!!\n",__func__, uReCode[0]);
		return -ERROR_CODE_RERUNCODE;
	}
	
	return 0;
}

static int nfg1000_ota_program_step1_EnterBootLoad(stnfg1000OtaInfo *di)
{

	int ret = 0;
	u8 u8Data[2] = {0};
	u8 uReCode[2] = {0};
	u8 retry_cnt = 0;

#define NFG1000_RESET_CMD	0x8a

	printk("fg_ota: %s enter %d\n", __func__, __LINE__);
	u8Data[1] = 0;
	u8Data[0] =NFG1000_RESET_CMD;
	ret = nfg1000_i2c_write(di->dev, di->I2caddr,di->regs[NFG_FG_REG_ALT_MAC],u8Data,2);
	if(ret)
	{
		printk("fg_ota:%s write reg: %x error!!\n",__func__, di->regs[NFG_FG_REG_ALT_MAC]);
		//return -ERROR_CODE_I2C_WRITE;
	}

	// wait 100ms
	mdelay(NFG1000_RESET_WAIT_TIME);

	u8Data[0] = 0x3f;
	for(retry_cnt = 0;retry_cnt < 3;retry_cnt++)
	{
		ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,1);
		ret = nfg1000_i2c_read(di->dev,di->I2caddr,I2C_NO_REG_DATA,uReCode,1);
		if(uReCode[0] == NFG1000_SUCESS_CODE)
		{
			break;
		}
	}
	if(uReCode[0] != NFG1000_SUCESS_CODE)
	{
		mdelay(NFG1000_boot_WAIT_TIME);
		for(retry_cnt = 0;retry_cnt < 3;retry_cnt++)
   		{
			ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,1);
			ret = nfg1000_i2c_read(di->dev,di->I2caddr,I2C_NO_REG_DATA,uReCode,1);
			if(uReCode[0] == NFG1000_SUCESS_CODE)
			{
			    break;
			}
		}
	}

	if(uReCode[0] != NFG1000_SUCESS_CODE)
	{
		printk("fg_ota:%s,return code error:%x!!\n",__func__, uReCode[0]);
		return -ERROR_CODE_RERUNCODE;
	}
	
	return 0;
}

static u8 CalcXorsum(u8 *inbuf,int len)
{
	int i;
	u8 xorsum = 0;
	for(i=0;i<len;i++)
	{
		xorsum ^=inbuf[i];
	}
	return xorsum;
}

static int nfg1000_ota_program_step2_ShaAuth(stnfg1000OtaInfo *di)
{
	int ret = 0;
	u8 u8Data[64] = {0};

	#define SHA_DATA_SIZE	32

	printk("fg_ota: %s enter %d\n", __func__, __LINE__);
	u8Data[0] = 0XE6;u8Data[1] = 0X19;
	ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,2);
	if(ret)
	{
		printk("nfg1000_ota_program_step2_ShaAuth:write reg: %x error!!\n",I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_WRITE;
	}
	ret = nfg1000_GetReturnCode(di);
	if(ret)
	{
		return ret;
	}

	memcpy(&u8Data[0],sha_256_ramdom_data,SHA_DATA_SIZE);u8Data[SHA_DATA_SIZE] = CalcXorsum(sha_256_ramdom_data,SHA_DATA_SIZE);
	ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,SHA_DATA_SIZE+1);
	if(ret)
	{
		printk("nfg1000_ota_program_step2_ShaAuth:write reg: %x error!!\n",I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_WRITE;
	}
	ret = nfg1000_GetReturnCode(di);
	if(ret)
	{
		return ret;
	}

	memcpy(&u8Data[0],sha_256_pass_word,SHA_DATA_SIZE);u8Data[SHA_DATA_SIZE] = CalcXorsum(sha_256_pass_word,SHA_DATA_SIZE);
	ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,SHA_DATA_SIZE+1);
	if(ret)
	{
		printk("nfg1000_ota_program_step2_ShaAuth:write reg: %x error!!\n",I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_WRITE;
	}

	ret = nfg1000_GetReturnCode(di);
	if(ret)
	{
		return ret;
	}

	return 0;
}

static int nfg1000_ota_program_step3_mcuAuth(stnfg1000OtaInfo *di)
{
	int ret = 0;
	u32 tmpMcuCode =0;
	u8 u8Data[8] = {0};
	u8 uReCode[8] = {0};
#define MCU_AUTH_CODE_SIZE		4

	printk("fg_ota: %s %d\n", __func__, __LINE__);
	u8Data[0] = 0x91;
	u8Data[1] = 0x6e;
	ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,2);
	if(ret)
	{
		printk("nfg1000_ota_program_step3_mcuAuth:write reg: %x error!!\n",I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_WRITE;
	}
	ret = nfg1000_GetReturnCode(di);
	if(ret)
	{
		return ret;
	}

	u8Data[0] = 0;
	u8Data[1] = 2;
	u8Data[2] = 0xb;
	u8Data[3] = 0xf8;
	//u8Data[4] = u8Data[0]^ u8Data[1]^u8Data[2]^ u8Data[3];
	u8Data[4] = CalcXorsum(u8Data,4);
	ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,5);
	if(ret)
	{
		printk("nfg1000_ota_program_step3_mcuAuth:write reg: %x error!!\n",I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_WRITE;
	}
	ret = nfg1000_GetReturnCode(di);
	if(ret)
	{
		return ret;
	}

	memset(u8Data,0,sizeof(u8Data));
	u8Data[0] = 0x3;
	u8Data[1] = (u8)0xff-((u8)MCU_AUTH_CODE_SIZE-1);//03 fc
	ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,2);
	if(ret)
	{
		printk("nfg1000_ota_program_step3_mcuAuth:write reg: %x error!!\n",I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_WRITE;
	}
	ret = nfg1000_GetReturnCode(di);
	if(ret)
	{
		return ret;
	}

	ret = nfg1000_i2c_read(di->dev,di->I2caddr,I2C_NO_REG_DATA,uReCode,4);
	if(ret)
	{
		printk("nfg1000_ota_program_step3_mcuAuth:read reg: %x error!!\n", I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_READ;
	}

	tmpMcuCode = NAKE_DWORD_8BITS(uReCode[3],uReCode[2],uReCode[1],uReCode[0]);
	if(di->mcu_auth_code != tmpMcuCode)
	{
		printk("nfg1000_ota_program_step3_mcuAuth:mcu code check error: file_code:%x  tmpMcuCode:%x !\n",di->mcu_auth_code,tmpMcuCode);
		return -ERROR_CODE_MCUCODE;
	}
	
	return 0;
}

static int nfg1000_ota_program_step4_EraseFlash(stnfg1000OtaInfo *di)
{
	int ret = 0;
	int datalen = 0;
	u8 u8Data[200] = {0};

	printk("fg_ota: %s %d\n", __func__, __LINE__);
	u8Data[0] = 0xc4;
	u8Data[1] = 0x3b;
	ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,2);
	if(ret)
	{
		printk("nfg1000_ota_program_step4_EraseFlash:write reg: %x error!!\n",I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_WRITE;
	}
	ret = nfg1000_GetReturnCode(di);
	if(ret)
	{
		return ret;
	}

	memcpy(u8Data,app_flash_erase_order,sizeof(app_flash_erase_order));
	datalen = sizeof(app_flash_erase_order);
	if(datalen<=0)
	{
		return datalen;
	}

	ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,datalen);
	if(ret)
	{
		printk("nfg1000_ota_program_step4_EraseFlash:write reg: %x error!!\n",I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_WRITE;
	}
	mdelay(NFG1000_erase_WAIT_TIME);
	ret = nfg1000_GetReturnCode(di);
	if(ret)
	{
		return ret;
	}
	
	return 0;
}

static int nfg1000_ota_program_step5_WriteUpdatefile(stnfg1000OtaInfo *di)
{
	int ret = 0;
	u8 u8Data[260] = {0}; 
	u8 u8checksum = 0;
	int i=0;
	int j=0;
	u32 tmpaddr = 0;

	printk("fg_ota: %s %d\n", __func__, __LINE__);
	//Start writing from position 256 with 00000100,
	//write the data at the back first, then write the data at the front 256.
	for(i=256;i<di->UpdatefileLen;i +=256)
	{
		u8Data[0] = 0xb1;
		u8Data[1] = 0x4e;
		ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,2);
		if(ret)
		{
			printk("nfg1000_ota_program_step5_WriteUpdatefile:write reg: %x error!!\n",I2C_NO_REG_DATA);
			return -ERROR_CODE_I2C_WRITE;
		}
		ret = nfg1000_GetReturnCode(di);
		if(ret)
		{
			return ret;
		}

		tmpaddr = di->appstartaddr+i;
		u8Data[0] = (tmpaddr>>24)&0xff;u8Data[1] = (tmpaddr>>16)&0xff;u8Data[2] = (tmpaddr>>8)&0xff;u8Data[3] = (tmpaddr)&0xff;
		u8Data[4] = u8Data[0]^u8Data[1]^u8Data[2]^u8Data[3];
		ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,5);
		if(ret)
		{
			printk("nfg1000_ota_program_step5_WriteUpdatefile:write reg: %x error!!\n",I2C_NO_REG_DATA);
			return -ERROR_CODE_I2C_WRITE;
		}
		ret = nfg1000_GetReturnCode(di);
		if(ret)
		{
			return ret;
		}

		u8checksum = 0;
		u8Data[0] = 0xff;
		u8checksum ^=u8Data[0];
		for(j=0;j<256;j++)
		{
			u8checksum ^=di->fileBuf[j+i];
			u8Data[1+j] = di->fileBuf[j+i];
		}
		u8Data[1+j] = u8checksum;

		ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,258);
		if(ret)
		{
			printk("nfg1000_ota_program_step5_WriteUpdatefile:write reg: %x error!!\n",I2C_NO_REG_DATA);
			return -ERROR_CODE_I2C_WRITE;
		}
		mdelay(NFG1000_write_WAIT_TIME);
		ret = nfg1000_GetReturnCode(di);
		if(ret)
		{
			return ret;
		}
	}

	for(i=252;i>=0;i=i-4)
	{
		u8Data[0] = 0xb1;u8Data[1] = 0x4e;
		ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,2);
		if(ret)
		{
			printk("nfg1000_ota_program_step5_WriteUpdatefile:write reg: %x error!!\n",I2C_NO_REG_DATA);
			return -ERROR_CODE_I2C_WRITE;
		}
		ret = nfg1000_GetReturnCode(di);
		if(ret)
		{
			return ret;
		}

		tmpaddr = di->appstartaddr+i;
		u8Data[0] = (tmpaddr>>24)&0xff;u8Data[1] = (tmpaddr>>16)&0xff;u8Data[2] = (tmpaddr>>8)&0xff;u8Data[3] = (tmpaddr)&0xff;
		u8Data[4] = u8Data[0]^u8Data[1]^u8Data[2]^u8Data[3];
		ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,5);
		if(ret)
		{
			printk("nfg1000_ota_program_step5_WriteUpdatefile:write reg: %x error!!\n",I2C_NO_REG_DATA);
			return -ERROR_CODE_I2C_WRITE;
		}
		ret = nfg1000_GetReturnCode(di);
		if(ret)
		{
			return ret;
		}

		u8checksum = 0;
		u8Data[0] = 0x3;
		u8checksum ^=u8Data[0];
		for(j=0;j<4;j++)
		{
			u8checksum ^=di->fileBuf[j+i];
			u8Data[1+j] = di->fileBuf[j+i];
		}
		u8Data[1+j] = u8checksum;

		ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,6);
		if(ret)
		{
			printk("nfg1000_ota_program_step5_WriteUpdatefile:write reg: %x error!!\n",I2C_NO_REG_DATA);
			return -ERROR_CODE_I2C_WRITE;
		}
		ret = nfg1000_GetReturnCode(di);
		if(ret)
		{
			return ret;
		}
		
	}
	return 0;
}

static int nfg1000_ota_program_step6_CheckCrc(stnfg1000OtaInfo *di)
{
	int ret = 0;
	u8 u8Data[8] = {0};
	u8 uReCode[8] = {0};
	u32 tmpaddr = 0;
	u32 Crc32tmp = 0;
	u32 Crc32_code = 0;

	printk("fg_ota: %s %d\n", __func__, __LINE__);
	u8Data[0] = 0xd0;
	u8Data[1] = 0x2f;
	ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,2);
	if(ret)
	{
		printk("nfg1000_ota_program_step6_CheckCrc:write reg: %x error!!\n",I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_WRITE;
	}
	ret = nfg1000_GetReturnCode(di);
	if(ret)
	{
		return ret;
	}

	tmpaddr = di->appstartaddr;
	u8Data[0] = (tmpaddr>>24)&0xff;u8Data[1] = (tmpaddr>>16)&0xff;u8Data[2] = (tmpaddr>>8)&0xff;u8Data[3] = (tmpaddr)&0xff;
	u8Data[4] = u8Data[0]^u8Data[1]^u8Data[2]^u8Data[3];
	ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,5);
	if(ret)
	{
		printk("nfg1000_ota_program_step6_CheckCrc:write reg: %x error!!\n",I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_WRITE;
	}
	ret = nfg1000_GetReturnCode(di);
	if(ret)
	{
		return ret;
	}

	tmpaddr = di->appstartaddr+di->UpdatefileLen - 1;
	u8Data[0] = (tmpaddr>>24)&0xff;u8Data[1] = (tmpaddr>>16)&0xff;u8Data[2] = (tmpaddr>>8)&0xff;u8Data[3] = (tmpaddr)&0xff;
	u8Data[4] = u8Data[0]^u8Data[1]^u8Data[2]^u8Data[3];
	ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,5);
	if(ret)
	{
		printk("nfg1000_ota_program_step6_CheckCrc:write reg: %x error!!\n",I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_WRITE;
	}
	ret = nfg1000_GetReturnCode(di);
	if(ret)
	{
		return ret;
	}

	mdelay(NFG1000_crc32_WAIT_TIME);
	ret = nfg1000_i2c_read(di->dev,di->I2caddr,I2C_NO_REG_DATA,uReCode,4);
	if(ret)
	{
		printk("nfg1000_ota_program_step6_CheckCrc:read reg: %x error!!\n", I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_READ;
	}

	Crc32tmp = NAKE_DWORD_8BITS(uReCode[3],uReCode[2],uReCode[1],uReCode[0]);

	init_crc_table();
	Crc32_code  = crc32(0xFFFFFFFF, di->fileBuf, di->UpdatefileLen);
	if(Crc32_code != Crc32tmp)
	{
		printk("nfg1000_ota_program_step6_CheckCrc:read reg: %x error!!\n",  Crc32_code);
		return -ERROR_CODE_I2C_READ;
	}

	return 0;
}

static int nfg1000_ota_program_step7_ExitBoot(stnfg1000OtaInfo *di)
{
	int ret = 0;
	u8 u8Data[8] = {0};
	
	printk("fg_ota: %s %d\n", __func__, __LINE__);
	u8Data[0] = 0xA1;u8Data[1] = 0x5E;
	ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,2);
	if(ret)
	{
		printk("nfg1000_ota_program_step7_ExitBoot:write reg: %x error!!\n",I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_WRITE;
	}
	ret = nfg1000_GetReturnCode(di);
	if(ret)
	{
		return ret;
	}

	u8Data[0] = 0;u8Data[1] = 0;u8Data[2] = 0;u8Data[3] = 0;
	u8Data[4] = u8Data[0]^u8Data[1]^u8Data[2]^u8Data[3];
	ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,5);
	if(ret)
	{
		printk("nfg1000_ota_program_step7_ExitBoot:write reg: %x error!!\n",I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_WRITE;
	}
	ret = nfg1000_GetReturnCode(di);
	if(ret)
	{
		return ret;
	}
	ret = nfg1000_GetReturnCode(di);
	if(ret)
	{
		return ret;
	}
	mdelay(NFG1000_seal_WAIT_TIME);

	return 0;
}

static int nfg1000_ota_program_step8_EraseDATA(stnfg1000OtaInfo *di)
{
	int ret = 0;int datalen = 0;
	u8 u8Data[20] = {0}; 

	printk("fg_ota: %s %d\n", __func__, __LINE__);
	u8Data[0] = 0xc4;
	u8Data[1] = 0x3b;
	ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,2);
	if(ret)
	{
		printk("nfg1000_ota_program_step8_EraseDATA:write reg: %x error!!\n",I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_WRITE;
	}
	ret = nfg1000_GetReturnCode(di);
	if(ret)
	{
		return ret;
	}

	memcpy(u8Data,data_flash_erase_order,sizeof(data_flash_erase_order));
	datalen = sizeof(data_flash_erase_order);
	if(datalen<=0)
	{
		return datalen;
	}

	ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,datalen);
	if(ret)
	{
		printk("nfg1000_ota_program_step8_EraseDATA:write reg: %x error!!\n",I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_WRITE;
	}
	mdelay(NFG1000_erase_DF_TIME);
	ret = nfg1000_GetReturnCode(di);
	if(ret)
	{
		return ret;
	}
	
	return 0;
}

static int nfg1000_ota_program_step9_WriteDatafile(stnfg1000OtaInfo *di)
{
	int ret = 0;
	u8 u8Data[260] = {0};
	u8 u8checksum = 0;
	u32 i=0;
	u32 j=0;
	u32 tmpaddr = 0;
	//only write 1k
	printk("fg_ota: %s %d\n", __func__, __LINE__);
	for(i = di->chemIDstartnumber;i < di->chemIDstopnumber;i += 256)
	{
		u8Data[0] = 0xb1;
		u8Data[1] = 0x4e;
		ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,2);
		if(ret)
		{
			printk("nfg1000_ota_program_step9_WriteDatafile:write reg: %x error!!\n",I2C_NO_REG_DATA);
			return -ERROR_CODE_I2C_WRITE;
		}
		ret = nfg1000_GetReturnCode(di);
		if(ret)
		{
			return ret;
		}

		//send add, high addr is head: dataflashstartaddr = 0x30000; dataflashstartnumber =0
		tmpaddr = di->dataflashstartaddr + i - di->dataflashstartnumber;
		u8Data[0] = (tmpaddr>>24)&0xff;u8Data[1] = (tmpaddr>>16)&0xff;u8Data[2] = (tmpaddr>>8)&0xff;u8Data[3] = (tmpaddr)&0xff;
		u8Data[4] = u8Data[0]^u8Data[1]^u8Data[2]^u8Data[3];
		ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,5);
		if(ret)
		{
			printk("nfg1000_ota_program_step9_WriteDatafile:write reg: %x error!!\n",I2C_NO_REG_DATA);
			return -ERROR_CODE_I2C_WRITE;
		}
		ret = nfg1000_GetReturnCode(di);
		if(ret)
		{
			return ret;
		}

		//send data length��256
		u8checksum = 0;
		u8Data[0] = 0xff;
		u8checksum ^=u8Data[0];
		for(j=0;j<256;j++)
		{
			u8checksum ^=di->fileBuf[j+i];
			u8Data[1+j] = di->fileBuf[j+i];
		}
		u8Data[1+j] = u8checksum;

		ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,258);
		if(ret)
		{
			printk("nfg1000_ota_program_step9_WriteDatafile:write reg: %x error!!\n",I2C_NO_REG_DATA);
			return -ERROR_CODE_I2C_WRITE;
		}
		mdelay(NFG1000_write_WAIT_TIME);
		ret = nfg1000_GetReturnCode(di);
		if(ret)
		{
			return ret;
		}
		
	}
	return 0;
}

static int nfg1000_ota_program_step10_CheckDataCrc(stnfg1000OtaInfo *di)
{
	int ret = 0;
	u8 u8Data[8] = {0};
	u8 uReCode[8] = {0};
	u32 tmpaddr = 0;
	u32 Crc32tmp = 0;
	u32 Crc32_code = 0;

	printk("fg_ota: %s %d\n", __func__, __LINE__);
	u8Data[0] = 0xd0;
	u8Data[1] = 0x2f;
	ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,2);
	if(ret)
	{
		printk("nfg1000_ota_program_step10_CheckDataCrc:write reg: %x error!!\n",I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_WRITE;
	}
	ret = nfg1000_GetReturnCode(di);
	if(ret)
	{
		return ret;
	}

	tmpaddr = 0x30C00;
	u8Data[0] = (tmpaddr>>24)&0xff;u8Data[1] = (tmpaddr>>16)&0xff;u8Data[2] = (tmpaddr>>8)&0xff;u8Data[3] = (tmpaddr)&0xff;
	u8Data[4] = u8Data[0]^u8Data[1]^u8Data[2]^u8Data[3];
	ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,5);
	if(ret)
	{
		printk("nfg1000_ota_program_step10_CheckDataCrc:write reg: %x error!!\n",I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_WRITE;
	}
	ret = nfg1000_GetReturnCode(di);
	if(ret)
	{
		return ret;
	}

	tmpaddr = 0x03FF;
	u8Data[0] = (tmpaddr>>24)&0xff;u8Data[1] = (tmpaddr>>16)&0xff;u8Data[2] = (tmpaddr>>8)&0xff;u8Data[3] = (tmpaddr)&0xff;
	u8Data[4] = u8Data[0]^u8Data[1]^u8Data[2]^u8Data[3];
	ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,5);
	if(ret)
	{
		printk("nfg1000_ota_program_step10_CheckDataCrc:write reg: %x error!!\n",I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_WRITE;
	}
	ret = nfg1000_GetReturnCode(di);
	if(ret)
	{
		return ret;
	}

	ret = nfg1000_i2c_read(di->dev,di->I2caddr,I2C_NO_REG_DATA,uReCode,4);
	if(ret)
	{
		printk("nfg1000_ota_program_step10_CheckDataCrc:read output crc: %x error!!\n", I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_READ;
	}

	Crc32tmp = NAKE_DWORD_8BITS(uReCode[3],uReCode[2],uReCode[1],uReCode[0]);

	init_crc_table();
	Crc32_code  = crc32(0xFFFFFFFF, &di->fileBuf[di->chemIDstartnumber], 1024);
	if(Crc32_code != Crc32tmp)
	{
		printk("nfg1000_ota_program_step10_CheckDataCrc:calculate crc: %x error!!\n",  Crc32_code);
		return -ERROR_CODE_I2C_READ;
	}

	return 0;
}

static int nfg1000_ota_program_step12_CheckDfSigCrc(stnfg1000OtaInfo *di)
{
	int ret = 0;
	u8 i = 0;
	u8 read_out[8] = {0};
	u16 df_sig = 0x0005;

	ret = nfg1000_i2c_BLOCK_command_read_with_CHECKSUM(di->dev, df_sig, read_out, 2);
	if(ret)
	{
		mdelay(NFG1000_boot_WAIT_TIME);
		return -ERROR_CODE_I2C_WRITE;
	}
	printk("fg_ota:df sig = 0x%x,%x\n", read_out[0], read_out[1]);
	for(i = 0;i < 2;i++)
	{
		if(read_out[i] != di->fileBuf[CONFIG_STATIC_SIG + di->dataflashstartnumber + i]) {
			printk("fg_ota:df sig not match%d = 0x%x,%x\n",
				i,read_out[i],di->fileBuf[CONFIG_STATIC_SIG + di->dataflashstartnumber + i]);
			return -1;
		}
	}
	return 0;
}

static int nfg1000_ota_program_step11_CheckCHemDfSigCrc(stnfg1000OtaInfo *di)
{
	int ret = 0;
	u8 i = 0;
	u8 read_out[8] = {0};
	u16 chem_df_sig = 0x0008;
	const char *p;

	ret = nfg1000_i2c_BLOCK_command_read_with_CHECKSUM(di->dev, chem_df_sig, read_out, 2);
	if(ret)
	{
		mdelay(NFG1000_boot_WAIT_TIME);
		return -ERROR_CODE_I2C_WRITE;
	}
	printk("fg_ota:chem sig = 0x%x,%x\n", read_out[0], read_out[1]);
	p = &di->fileBuf[CONFIG_DF_SIG + di->dataflashstartnumber];
	for(i = 0;i < 2;i++)
	{
		if(read_out[i] != p[i]) {
			printk("fg_ota:static sig not match%d = 0x%x,%x\n",
				i,read_out[i],p[i]);
			return -1;
		}
	}
	return 0;
}

static int nfg1000_ota_seal(stnfg1000OtaInfo *di)
{
	int ret;
	int i=0;
	u8 u8Data[8] = {0};
	u8 seal_state_read[32] = {0};
	u16 seal_state_cmd = 0x0054;
	
	printk("fg_ota: %s %d\n", __func__, __LINE__);
	for(i = 0;i < 3;i++)
	{
		u8Data[0] = 0x3E;u8Data[1] = 0x30;u8Data[2] = 0;
		ret = nfg1000_i2c_write(di->dev, di->I2caddr,I2C_NO_REG_DATA,u8Data,3);
		if(ret)
		{
			printk("nfg1000_ota_seal:write reg: %x error!!\n",I2C_NO_REG_DATA);
			return -ERROR_CODE_I2C_WRITE;
		}
	
		ret = nfg1000_i2c_BLOCK_command_read_with_CHECKSUM(di->dev,seal_state_cmd ,seal_state_read,4);

		if(ret)
		{
			printk("nfg1000_ota_program_seal_state_check:seal state read error!%x\n", ret);
			return -ERROR_CODE_I2C_WRITE;
		}
	
		if((seal_state_read[1] & 0x03) == 0x03)
		{
			break;
		}
	}

	if(i == 3)
	{
		printk("nfg1000_ota_program_seal_state_check:seal opeartion error!%x\n", ret);
		return -ERROR_CODE_I2C_WRITE;
	}
	
	return 0;
}

// updata config information
static int nfg1000_ota_updata_config(stnfg1000OtaInfo *di)
{
	u8 ret;
	u8 i = 0;
	u8 j = 0;
	u8 addr_cmd = 0;
	u8 retry_cnt = 0;
	u8 retry_flag = 0;
	u16 dataflash_base_addr = 0x4400;
	u8 config_data_read[32] = {0};
	u8 config_data_read_comp[32] = {0};

	printk("fg_ota: %s %d\n", __func__, __LINE__);
	for(i = 0; i < sizeof(nfg1000_Dataflash_updata_CMD);)
	{
		addr_cmd = nfg1000_Dataflash_updata_CMD[i];
		ret = nfg1000_i2c_BLOCK_command_write_with_CHECKSUM(di->dev, dataflash_base_addr + addr_cmd,&di->fileBuf[di->dataflashstartnumber + addr_cmd*32],32);
		mdelay(NFG1000_RESET_WAIT_TIME);
		if(ret)
		{
			printk("nfg1000_ota_updata_config:write reg: %x error!!\n",ret);
			break;
		}

		ret = nfg1000_i2c_BLOCK_command_read_with_CHECKSUM(di->dev,dataflash_base_addr + addr_cmd,config_data_read,32);
		if(ret)
		{
			printk("nfg1000_ota_updata_config:read reg: %x error!!\n",ret);
			return -ERROR_CODE_I2C_WRITE;
		}

		for(j = 0;j < 32;j++)
		{
			if(config_data_read[j] != di->fileBuf[addr_cmd*32 + di->dataflashstartnumber + j])
			{
				retry_flag = 1;
				retry_cnt++;
				break;
			}
		}
		if(retry_flag == 0)
		{
			retry_cnt = 0;
			i++;
		}
		else
		{
			if(retry_cnt > 2)
			{
				printk("nfg1000_ota_updata_config:config updata reg: %x error!!\n",addr_cmd);
				return -ERROR_CODE_I2C_WRITE;
			}
			
			retry_flag = 0;
		}
	}
	if(i != sizeof(nfg1000_Dataflash_updata_CMD))
	{
		printk("nfg1000_ota_updata config: %x error!!\n",I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_WRITE;
	}
	for(i = 0; i < 1;) {
		addr_cmd = 0x0c;
		ret = nfg1000_i2c_BLOCK_command_read_with_CHECKSUM(di->dev,dataflash_base_addr + addr_cmd,config_data_read,32);
		config_data_read[0] = di->fileBuf[di->dataflashstartnumber + addr_cmd*32+0];
		config_data_read[1] = di->fileBuf[di->dataflashstartnumber + addr_cmd*32+1];
		config_data_read[2] = di->fileBuf[di->dataflashstartnumber + addr_cmd*32+2];
		config_data_read[3] = di->fileBuf[di->dataflashstartnumber + addr_cmd*32+3];
		config_data_read[4] = di->fileBuf[di->dataflashstartnumber + addr_cmd*32+4];
		config_data_read[5] = di->fileBuf[di->dataflashstartnumber + addr_cmd*32+5];
		config_data_read[20] = di->fileBuf[di->dataflashstartnumber + addr_cmd*32+20];
		config_data_read[21] = di->fileBuf[di->dataflashstartnumber + addr_cmd*32+21];
		config_data_read[22] = di->fileBuf[di->dataflashstartnumber + addr_cmd*32+22];
		config_data_read[23] = di->fileBuf[di->dataflashstartnumber + addr_cmd*32+23];
		config_data_read[24] = di->fileBuf[di->dataflashstartnumber + addr_cmd*32+24];
		config_data_read[25] = di->fileBuf[di->dataflashstartnumber + addr_cmd*32+25];

		ret = nfg1000_i2c_BLOCK_command_write_with_CHECKSUM(di->dev, dataflash_base_addr + addr_cmd,config_data_read,32);
		mdelay(NFG1000_RESET_WAIT_TIME);
		if(ret)
		{
			printk("nfg1000_ota_updata_config:write config_data_read reg: %x error!!\n",ret);
			break;
		}

		ret = nfg1000_i2c_BLOCK_command_read_with_CHECKSUM(di->dev,dataflash_base_addr + addr_cmd,config_data_read_comp,32);
		if(ret)
		{
			printk("nfg1000_ota_updata_config_comp:read reg: %x error!!\n",ret);
			return -ERROR_CODE_I2C_WRITE;
		}

		for(j = 0;j < 32;j++)
		{
			if(config_data_read[j] != config_data_read_comp[j])
			{
				retry_flag = 1;
				retry_cnt++;
				break;
			}
		}
		if(retry_flag == 0)
		{
			retry_cnt = 0;
			i++;
		}
		else
		{
			if(retry_cnt > 2)
			{
				printk("nfg1000_ota_updata_config:config updata reg: %x error!!\n",addr_cmd);
				return -ERROR_CODE_I2C_WRITE;
			}
			retry_flag = 0;
		}
	}
	if(i != 1)
	{
		printk("nfg1000_ota_updata config: %x error!!\n",I2C_NO_REG_DATA);
		return -ERROR_CODE_I2C_WRITE;
	}

	addr_cmd = 0x57;
	ret = nfg1000_i2c_BLOCK_command_read_with_CHECKSUM(di->dev, addr_cmd,config_data_read,2);
	if(ret)
	{
		printk("nfg1000_ota_updata_config:write config_data_read reg: %x error!!\n",ret);
		return -ERROR_CODE_I2C_WRITE;
	}
	if((config_data_read[0] & 0x28) != 0x28)
	{
		printk("fg_ota:must config need update!!! = %x\n", config_data_read[0]);
		return -1;
	}
	return 0;
}

static ssize_t nfg1000_update_Data(void)
{
	if(nfg1000_ota_program_step1_EnterBootLoad(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_ENTER_BOOT;
	}
	if(nfg1000_ota_program_step2_ShaAuth(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_SHA256;
	}
	if(nfg1000_ota_program_step3_mcuAuth(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_CHIP_NAME;
	}
	if(nfg1000_ota_program_step8_EraseDATA(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_DATA_ERASE;
	}
	if(nfg1000_ota_program_step9_WriteDatafile(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_DATA_WRITE;
	}
	if(nfg1000_ota_program_step10_CheckDataCrc(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_APP_CHECKCRC;
	}
	if(nfg1000_ota_program_step7_ExitBoot(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_EXIT_BOOT;
	}
	if(nfg1000_ota_unseal(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_UNSEAL;
	}
	if(nfg1000_ota_updata_config(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_UNSEAL;
	}
	if (nfg1000_ota_program_step11_CheckCHemDfSigCrc(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_CHEM_SIG_CHECKCRC;
	}
	if(nfg1000_ota_program_step12_CheckDfSigCrc(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_DF_SIG_CHECKCRC;
	}
	if(nfg1000_ota_seal(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_SEAL;
	}

	return 0;
}

static int nfg1000_force_update_app(struct nfg_fg_chip *bq)
{
	const char *file_p = NULL;
	int file_sz = 0;
	int ret;

	dev_info(bq->dev, "fg_ota: %s enter %d\n", __func__, __LINE__);

	file_sz = of_get_fw(bq);
	if (bq->chip == NFG1000_MASTER) {
		file_p = nfg1000_ad_data_m;
	} else if (bq->chip == NFG1000_SLAVE) {
		file_p = nfg1000_ad_data_s;
	} else
		return -1;
	if (IS_ERR_OR_NULL(file_p)) {
		dev_err(bq->dev, "fg_ota: force update no date to update\n");
		return -1;
	}
	if (file_sz != CODE_SIZE && file_sz != APP_SIZE &&
			file_sz != DATAFLASH_SIZE) {
		dev_err(bq->dev, "fg_ota:file length err=%d\n", file_sz);
		return -1;
	}

	ret = nfg1000_ota_init(bq, file_p, file_sz);
	if (ret)
		return ret;

	if(nfg1000_ota_program_boot_check(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_ENTER_BOOT;
	}
	if(nfg1000_ota_program_step2_ShaAuth(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_SHA256;
	}
	if(nfg1000_ota_program_step3_mcuAuth(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_CHIP_NAME;
	}
	if(nfg1000_ota_program_step4_EraseFlash(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_APP_ERASE;
	}
	if(nfg1000_ota_program_step5_WriteUpdatefile(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_APP_WRITE;
	}
	if(nfg1000_ota_program_step6_CheckCrc(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_APP_CHECKCRC;
	}
	if(nfg1000_ota_program_step7_ExitBoot(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_EXIT_BOOT;
	}
	if(nfg1000_ota_seal(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_SEAL;
	}

	return 0;
}

static ssize_t nfg1000_update_APP(void)
{
	if(nfg1000_ota_program_step1_EnterBootLoad(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_ENTER_BOOT;
	}
	if(nfg1000_ota_program_step2_ShaAuth(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_SHA256;
	}
	if(nfg1000_ota_program_step3_mcuAuth(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_CHIP_NAME;
	}
	if(nfg1000_ota_program_step4_EraseFlash(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_APP_ERASE;
	}
	if(nfg1000_ota_program_step5_WriteUpdatefile(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_APP_WRITE;
	}
	if(nfg1000_ota_program_step6_CheckCrc(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_APP_CHECKCRC;
	}
	if(nfg1000_ota_program_step7_ExitBoot(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_EXIT_BOOT;
	}
	if(nfg1000_ota_seal(&gnfg1000OtaInfo))
	{
		return PROGRAM_ERROR_SEAL;
	}

	return 0;
}

struct tag_bootmode {
	u32 size;
	u32 tag;
	u32 bootmode;
	u32 boottype;
};

static unsigned int fg_get_bootmode(struct device *dev)
{
	struct device_node *boot_node = NULL;
	struct tag_bootmode *tag = NULL;
	unsigned int mode = 11;//Unkown

	boot_node = of_parse_phandle(dev->of_node, "bootmode", 0);
	if (!boot_node) {
		nfg_err("%s: failed to get boot mode phandle\n", __func__);
		return mode;
	}

	tag = (struct tag_bootmode *)of_get_property(boot_node,
						"atag,boot", NULL);
	if (!tag)
		nfg_err("%s: failed to get atag,boot\n", __func__);
	else
	{
		nfg_info("%s: size:0x%x tag:0x%x bootmode:0x%x boottype:0x%x\n",
			__func__, tag->size, tag->tag,
			tag->bootmode, tag->boottype);
		mode = tag->bootmode;
	}
	return mode;
}

static int nfg1000_ota_CheckCHemDfSigCrc(struct nfg_fg_chip *dev)
{
	stnfg1000OtaInfo *di = &gnfg1000OtaInfo;
	int ret = 0;
	u8 i = 0;
	u8 chemdfsig_out[8] = {0};
	u16 chem_df_sig = 0x0008;
	const char *p;

	ret = nfg1000_i2c_BLOCK_command_read_with_CHECKSUM(dev, chem_df_sig, chemdfsig_out, 2);
	if(ret)
	{
		mdelay(NFG1000_boot_WAIT_TIME);
		return -ERROR_CODE_I2C_WRITE;
	}
	printk("fg_ota:chem sig = 0x%x,%x\n", chemdfsig_out[0], chemdfsig_out[1]);
	p = &di->fileBuf[CONFIG_DF_SIG + di->dataflashstartnumber];
	for(i = 0;i < 2;i++)
	{
		if(chemdfsig_out[i] != p[i]) {
			printk("fg_ota:static sig not match%d = 0x%x,%x\n",
				i,chemdfsig_out[i],p[i]);
			return -1;
		}
	}
	return 0;
}

static int nfg1000_ota_CheckDfSigCrc(struct nfg_fg_chip *dev)
{
	stnfg1000OtaInfo *di = &gnfg1000OtaInfo;
	int ret = 0;
	u8 i = 0;
	u8 dfsig_out[8] = {0};
	u16 df_sig = 0x0005;

	ret = nfg1000_i2c_BLOCK_command_read_with_CHECKSUM(dev, df_sig, dfsig_out, 2);
	if(ret)
	{
		mdelay(NFG1000_boot_WAIT_TIME);
		return -ERROR_CODE_I2C_WRITE;
	}
	printk("fg_ota:df sig = 0x%x,%x\n", dfsig_out[0], dfsig_out[1]);
	for(i = 0;i < 2;i++)
	{
		if(dfsig_out[i] != di->fileBuf[CONFIG_STATIC_SIG + di->dataflashstartnumber + i]) {
			printk("fg_ota:df sig not match%d = 0x%x,%x\n",
				i,dfsig_out[i],di->fileBuf[CONFIG_STATIC_SIG + di->dataflashstartnumber + i]);
			return -1;
		}
	}
	return 0;
}

static int fg_ota_batt_check(struct nfg_fg_chip *dev)
{
	stnfg1000OtaInfo *di = &gnfg1000OtaInfo;
	u16 device_name_cmd = 0x004a;
	u16 device_mfn_cmd = 0x004c;
	u16 device_chem_cmd = 0x0006;
	u16 i = 0, retry_cnt = 3;
	u8 read_out[32] = {0};
	const char *p;
	int ret = 0;

	/* check file mfn, if not match don't do update */
	while (retry_cnt-- > 0) {
	dev_info(dev->dev, "fg_ota:%s times = %d\n", __func__, retry_cnt);
	memset(read_out, 0, sizeof(read_out));
	mdelay(10);
	ret = nfg1000_i2c_BLOCK_command_read_with_CHECKSUM(dev, device_mfn_cmd, read_out, 15);
	if(ret) {
		//return -ERROR_CODE_I2C_WRITE;
		ret = -ERROR_CODE_I2C_WRITE;
		continue;
	}

	p = &di->fileBuf[DATAFLASH_MFNNANE + di->dataflashstartnumber];
	dev_info(dev->dev, "fg_ota:mfn=%s\n", read_out);
	for(i = 0; i < 15; i++) {
		if (read_out[i] != p[i]) {
			dev_err(dev->dev, "fg_ota:mfn not match%d\n",i);
			return -1;
		}
	}

	/* check file device name, if match don't do update */
	memset(read_out, 0, sizeof(read_out));
	ret = nfg1000_i2c_BLOCK_command_read_with_CHECKSUM(dev, device_name_cmd, read_out, 16);
	if(ret) {
		//return -ERROR_CODE_I2C_WRITE;
		ret = -ERROR_CODE_I2C_WRITE;
		continue;
	}

	dev_info(dev->dev, "fg_ota:device_name=%s\n", read_out);
	p = &di->fileBuf[DATAFLASH_DEVICENANE + di->dataflashstartnumber];
	for(i = 0; i < DF_VER_BIT_START; i++) {
			if (read_out[i] != p[i]) {
			dev_err(dev->dev, "fg_ota:device name not match%d\n", i);
			dev->need_update_df = true;
			break;
			}
		}

	if (i >= DF_VER_BIT_START) {
		dev_err(dev->dev, "fg_ota:df old versin:%x %x,new version:%x %x\n",
			read_out[DF_NAME_LEN-1],read_out[DF_NAME_LEN],p[DF_NAME_LEN-1],p[DF_NAME_LEN]);
		if (p[DF_NAME_LEN-1] > read_out[DF_NAME_LEN-1] || p[DF_NAME_LEN] > read_out[DF_NAME_LEN]) {
			dev->need_update_df = true;
		}else if((p[DF_NAME_LEN-1] == read_out[DF_NAME_LEN-1] && p[DF_NAME_LEN] == read_out[DF_NAME_LEN])){
			if(dev->chemic_id_err_recovery_support  && (!dev->need_update_df)){
				ret = nfg1000_ota_CheckCHemDfSigCrc(dev);
				if(ret <0){
					dev->need_update_df = true;
					dev_info(dev->dev, "fg_ota:new check chemic crc  need update\n");	
					return 0;
				}
				ret = nfg1000_ota_CheckDfSigCrc(dev);
				if(ret <0){
					dev->need_update_df = true;
					dev_info(dev->dev, "fg_ota:new check df crc  need update\n");	
					return 0;
				}
			}
			dev_info(dev->dev, "fg_ota:crc check  same no need update\n");
			return -1;
		} else {
			dev_info(dev->dev, "fg_ota:device_name match no need update\n");
			return -1;
		}
	}

	/* match device chem, if not match don't do update */
	/* one battery one battery chem */
	memset(read_out, 0, sizeof(read_out));
	ret = nfg1000_i2c_BLOCK_command_read_with_CHECKSUM(dev, device_chem_cmd, read_out, 8);
	if(ret) {
		//return -ERROR_CODE_I2C_WRITE;
		ret = -ERROR_CODE_I2C_WRITE;
		continue;
	}
	dev_info(dev->dev, "fg_ota:read_out chem_name=%s\n", read_out);
	p = &di->fileBuf[CONFIG_CHEM_VERSION + di->dataflashstartnumber];
	
	if(dev->chemic_id_err_recovery_support){
		if (strcmp(p,dev->fg_chemic_id)!= 0){
			dev_info(dev->dev, "fg_ota:bin chem_name=%s\n",p);
			return -1;
		}
		if (strcmp(read_out,dev->fg_chemic_id)== 0){
			ret = nfg1000_ota_CheckCHemDfSigCrc(dev);
			if(ret <0){
				dev_info(dev->dev, "fg_ota:old check crc need update\n");	
				return 0;
			}
			dev_info(dev->dev, "fg_ota:old is check ok no update\n");
			return -1;
		}
			dev_info(dev->dev, "fg_ota:bat err need update\n");
	}else{
		for(i = 0;i < 8;i++) {
			if (read_out[i] != p[i]) {
				dev_err(dev->dev, "fg_ota:device_chem not match%d\n", i);
				return -1;
			}
		}
	}
	if (ret == 0)
		break;
	}

	dev_info(dev->dev, "fg_ota:%s end\n", __func__);
	return 0;
}

static int do_fg_update_work(struct nfg_fg_chip *bq)
{
	int ret = 0;

	if(!bq->app_update_suc &&
			((gnfg1000OtaInfo.code_update_type == CODE_UPDATE_ALL) ||
			(gnfg1000OtaInfo.code_update_type == CODE_UPDATE_APP))) {
		ret = nfg1000_ota_program_step0_firmware_version_check(&gnfg1000OtaInfo);
		/* last ota maybe failed, need force ota */
		if(ret == -ERROR_CODE_I2C_WRITE) {
			dev_info(bq->dev, "fg_ota:force update App------ %d\n", bq->chip);
			ret = nfg1000_update_APP();
			dev_info(bq->dev, "fg_ota:update APP ret = %d\n", ret);
			if (ret)
				goto ota_unlock;
		}
		else if (ret == 0) {
			dev_info(bq->dev, "fg_ota:update App ----- %d\n", bq->chip);
			if(nfg1000_ota_unseal(&gnfg1000OtaInfo)) {
				ret = PROGRAM_ERROR_UNSEAL;
				goto ota_unlock;
			}
			ret = nfg1000_update_APP();
			if (ret)
				goto ota_unlock;
		}
		bq->app_update_suc = true;
	}

	/* update dataflash */
	if (!bq->need_update_df) {
		dev_info(bq->dev, "fg_ota:no need update df\n");
		goto ota_unlock;
	}
	if(!bq->df_update_suc &&
			((gnfg1000OtaInfo.code_update_type == CODE_UPDATE_ALL) ||
			(gnfg1000OtaInfo.code_update_type == CODE_UPDATE_DATAFLASH))) {
		dev_info(bq->dev, "fg_ota:update Date------ %d\n", bq->chip);
		if(nfg1000_ota_unseal(&gnfg1000OtaInfo)) {
			ret = PROGRAM_ERROR_UNSEAL;
			goto ota_unlock;
		}

		ret = nfg1000_update_Data();
		dev_err(bq->dev, "fg_ota:update Data ret = %d\n", ret);
		if (ret == 0)
			bq->df_update_suc = true;
	}

ota_unlock:
	return ret;
}

static void fg_update_work(struct nfg_fg_chip *bq)
{
	const char *file_p = NULL;
	int file_sz = 0;
	int ret;

	dev_info(bq->dev, "fg_ota:start update, volt=%d\n", bq->batt_volt);
	if (bq->batt_volt < 3700)
		return;
	if (fg_get_bootmode(bq->dev) != 0)
		return;

	file_sz = of_get_fw(bq);
	if (bq->chip == NFG1000_MASTER) {
		file_p = nfg1000_ad_data_m;
	} else if (bq->chip == NFG1000_SLAVE) {
		file_p = nfg1000_ad_data_s;
	} else
		return;
	if (IS_ERR_OR_NULL(file_p)) {
		dev_err(bq->dev, "fg_ota: no date to update\n");
		return;
	}
	if (file_sz != CODE_SIZE && file_sz != APP_SIZE &&
			file_sz != DATAFLASH_SIZE) {
		dev_err(bq->dev, "fg_ota:file length err=%d\n", file_sz);
		return;
	}

	ret = nfg1000_ota_init(bq, file_p, file_sz);
	if (ret)
		return;

	/* check mfn, device name, chemic id */
	mutex_lock(&bq->i2c_rw_lock);
	//fg_get_seal_st(bq);
	ret = fg_ota_batt_check(bq);
	dev_info(bq->dev, "fg_ota:batt check err = %d\n", ret);
	if (ret && -ERROR_CODE_I2C_WRITE != ret) {
		mutex_unlock(&bq->i2c_rw_lock);
		return;
	}

	//do update
	bq->update_fw = true;
	while (bq->retry_times-- > 0) {
		dev_info(bq->dev, "fg_ota:%d do update work times=%d,%d,%d\n",
			bq->chip,bq->retry_times,bq->app_update_suc,bq->df_update_suc);
		ret = do_fg_update_work(bq);
		if (!ret)
			break;
		bq->err_times ++;
	}
	if (bq->chip == NFG1000_MASTER) {
		kfree(nfg1000_ad_data_m);
		nfg1000_ad_data_m = NULL;
	} else if (bq->chip == NFG1000_SLAVE) {
		kfree(nfg1000_ad_data_s);
		nfg1000_ad_data_s = NULL;
	}
	mutex_unlock(&bq->i2c_rw_lock);

	bq->update_fw = false;
}

static void fg_ota_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct nfg_fg_chip *bq = container_of(dwork,
				struct nfg_fg_chip, ota_work);

	mutex_lock(&ota_lock);
	bq->retry_times = 5;
	bq->err_times = 0;
	fg_update_work(bq);
	mutex_unlock(&ota_lock);
}
#endif
// ota add end

static ssize_t show_bat_info(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct power_supply *psy = dev->driver_data;
	struct nfg_fg_chip *bq = psy->drv_data;
	int ret = 0, len = 0;
	int i = 0, j=0;
	u16 device_name_cmd = 0x004a;
	u16 device_mfn_cmd = 0x004c;
	u16 device_chem_cmd = 0x0006;
	u16 chem_df_sig = 0x0008;
	u16 df_sig = 0x0005;
	u16 fw_ver_cmd = 0x0002;
	u8 read_out[32];
	u8 t[32];

	dev_err(dev, "%s\n", __func__);
	mutex_lock(&bq->i2c_rw_lock);
	ret = fg_mac_read_block(bq, device_mfn_cmd, read_out, 15);
	if(ret) {
		dev_err(dev, "%s read mfn failed\n", __func__);
		goto out;
	}
	t[0] = read_out[0];
	t[1] = read_out[1];
	t[2] = read_out[2];
	t[3] = '-';
	j = 3;

	ret = fg_mac_read_block(bq, device_name_cmd, read_out, 16);
	if(ret) {
		dev_err(dev, "%s read dev name failed\n",__func__);
		goto out;
	}
	for (i = 0; i<16; i++) {
		if (read_out[i] && read_out[i]!='\0') {
			j++;
			t[j] = read_out[i];
		}
	}
	j++;
	t[j] = '-';

	ret = fg_mac_read_block(bq, device_chem_cmd, read_out, 8);
	if(ret) {
		dev_err(dev, "%s read chem name failed\n", __func__);
		goto out;
	}
	for (i = 0; i<8; i++) {
		if (read_out[i] && read_out[i]!='\0') {
			j++;
			t[j] = read_out[i];
		}
	}

	ret = fg_mac_read_block(bq, chem_df_sig, read_out, 2);
	if(ret) {
		dev_err(dev, "%s read chem df sig name failed\n", __func__);
		goto out;
	}
	ret = fg_mac_read_block(bq, df_sig, &read_out[4], 2);
	if(ret) {
		dev_err(dev, "%s read df sig name failed\n", __func__);
		goto out;
	}

	ret = fg_mac_read_block(bq, fw_ver_cmd, &read_out[8], 12);
	if(ret) {
		dev_err(dev, "%s read chem df sig name failed\n", __func__);
		goto out;
	}

out:
	mutex_unlock(&bq->i2c_rw_lock);
	if (ret)
		len = sprintf(buf, "Batt%d:ERR\n", bq->chip);
	else
		len = sprintf(buf, "Bat%d:%s-chem(%x %x)df(%x %x)(%x%x%x%x%x%x%x%x)\n",
			bq->chip, t,read_out[0],read_out[1],read_out[4],read_out[5],
			read_out[8],read_out[9],read_out[10],read_out[11],read_out[12],read_out[13],read_out[14],read_out[15]);
	return len;
}
static DEVICE_ATTR(bat_info, S_IRUGO, show_bat_info, NULL);

static ssize_t fg_attr_show_Ra_table(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct nfg_fg_chip *bq = i2c_get_clientdata(client);
	u8 t_buf[40];
	u8 temp_buf[40];
	int ret;
	int i, idx, len;

	ret = fg_mac_read_block(bq, 0x40C0, t_buf, 32);
	if (ret < 0)
		return 0;

	idx = 0;
	len = sprintf(temp_buf, "Ra Flag:0x%02X\n", t_buf[0] << 8 | t_buf[1]);
	memcpy(&buf[idx], temp_buf, len);
	idx += len;
	len = sprintf(temp_buf, "RaTable:\n");
	memcpy(&buf[idx], temp_buf, len);
	idx += len;
	for (i = 1; i < 16; i++) {
		len = sprintf(temp_buf, "%d ", t_buf[i*2] << 8 | t_buf[i*2 + 1]);
		memcpy(&buf[idx], temp_buf, len);
		idx += len;
	}

	return idx;
}

static ssize_t fg_attr_show_Qmax(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct nfg_fg_chip *bq = i2c_get_clientdata(client);
	int ret;
	u8 t_buf[64];
	int len;

	memset(t_buf, 0, 64);

	ret = fg_mac_read_block(bq, 0x4146, t_buf, 2);
	if (ret < 0)
		return 0;

	len = sprintf(buf, "Qmax Cell 0 = %d\n", (t_buf[0] << 8) | t_buf[1]);

	return len;
}

static DEVICE_ATTR(RaTable, S_IRUGO, fg_attr_show_Ra_table, NULL);
static DEVICE_ATTR(Qmax, S_IRUGO, fg_attr_show_Qmax, NULL);

static struct attribute *fg_attributes[] = {
	&dev_attr_RaTable.attr,
	&dev_attr_Qmax.attr,
	NULL,
};

static const struct attribute_group fg_attr_group = {
	.attrs = fg_attributes,
};

static void nfg1000_chip_init(struct nfg_fg_chip *bq)
{
	int ret;

	if(bq->chip == NFG1000_MASTER && bq->nfg1000_master_gpio == U32_MAX)
		return;

	if(bq->chip == NFG1000_SLAVE && bq->nfg1000_slave_gpio == U32_MAX)
	{
		return;
	}

	if (bq->chip == NFG1000_MASTER) { // bat_a
		ret = gpio_direction_output(bq->nfg1000_master_gpio, 0);
		if (ret < 0) {
			pr_err("%s: fail to set nfg1000_master_gpio dir\n", __func__);
			return;
		}

		gpio_set_value(bq->nfg1000_master_gpio, 0);

		ret = gpio_get_value(bq->nfg1000_master_gpio);

		pr_err("%s: nfg1000_master_gpio state:%d\n", __func__, ret);
	} else if (bq->chip == NFG1000_SLAVE) {
			ret = gpio_direction_output(bq->nfg1000_slave_gpio, 0);
			if (ret < 0) {
				pr_err("%s: fail to set nfg1000_slave_gpio dir\n", __func__);
				return;
			}

			gpio_set_value(bq->nfg1000_slave_gpio, 0);

			ret = gpio_get_value(bq->nfg1000_slave_gpio);

			pr_err("%s: nfg1000_slave_gpio state:%d\n", __func__, ret);
	}
}

static int nfg1000_parse_dt(struct nfg_fg_chip *bq)
{
	struct device_node *node = bq->dev->of_node;
	int ret;

	bq->pinctrl = devm_pinctrl_get(bq->dev);
	if (IS_ERR_OR_NULL(bq->pinctrl)) {
		pr_err("%s:Cannot find i2c pinctrl!\n",__func__);
	}else{
		bq->i2c_scl_gpio_init = pinctrl_lookup_state(bq->pinctrl, "i2c_scl_gpio_init");
		if (IS_ERR_OR_NULL(bq->i2c_scl_gpio_init)) {
			ret = PTR_ERR(bq->i2c_scl_gpio_init);
			pr_err("%s:Cannot find pinctrl i2c_scl_gpio_init\n",__func__);
		}else{
			//set state
			ret = pinctrl_select_state(bq->pinctrl, bq->i2c_scl_gpio_init);
			if (ret < 0) {
				pr_err("%s:i2c_scl_gpio_init  fail\n",__func__);
			}
		}

		bq->i2c_sda_gpio_init = pinctrl_lookup_state(bq->pinctrl, "i2c_sda_gpio_init");
		if (IS_ERR_OR_NULL(bq->i2c_sda_gpio_init)) {
			ret = PTR_ERR(bq->i2c_sda_gpio_init);
			pr_err("%s:Cannot find pinctrl i2c_sda_gpio_init\n",__func__);
		}else{
			ret = pinctrl_select_state(bq->pinctrl, bq->i2c_sda_gpio_init);
			if (ret < 0) {
				pr_err("%s:i2c_sda_gpio_init fail\n",__func__);
			}
		}
	}

	if (bq->chip == NFG1000_MASTER) { // bat_a
		bq->nfg1000_master_gpio = of_get_named_gpio(node, "nfg1000_master_gpio", 0);
		if (bq->nfg1000_master_gpio < 0) {
			bq->nfg1000_master_gpio = U32_MAX;
			pr_err("%s: get master fg ctrl gpio fail\n", __func__);
			//return -1;
		}
		if(bq->nfg1000_master_gpio != U32_MAX){
			ret = gpio_request(bq->nfg1000_master_gpio, "nfg1000_master_gpio");
			if (ret < 0){
				pr_err ("nfg1000_master_gpio request failed!\n");
			}
		}
		pr_info("%s: nfg1000_master_gpio:%d",
			__func__, bq->nfg1000_master_gpio);

		/* chg en gpio */
		bq->chg_en_gpio = of_get_named_gpio(node, "chg_en_gpio", 0);
		if (bq->chg_en_gpio < 0) {
			bq->chg_en_gpio = U32_MAX;
			pr_err("%s: get chg_en_gpio fail\n", __func__);
			//return -1;
		}
		if(bq->chg_en_gpio != U32_MAX){
			ret = gpio_request(bq->chg_en_gpio, "chg_en_gpio");
			if (ret < 0){
				pr_err ("chg_en_gpio request failed!\n");
			}
        }

		/* stage_diff_gpio */
		bq->stage_diff_gpio = of_get_named_gpio(node, "stage_diff_gpio", 0);
		if (bq->stage_diff_gpio < 0) {
			bq->stage_diff_gpio = U32_MAX;
			pr_err("%s: get stage_diff_gpio fail\n", __func__);
			//return -1;
		}
		if(bq->stage_diff_gpio != U32_MAX){
			ret = gpio_request(bq->stage_diff_gpio, "stage_diff_gpio");
			if (ret < 0){
				pr_err ("stage_diff_gpio request failed!\n");
			}
        }

		if (of_property_read_s32(node, "tran_Q_max", &bq->nfg1000_master_qmax) < 0)
			bq->nfg1000_master_qmax = 2500;

		bq->chemic_id_err_recovery_support = of_property_read_bool(node, "chemic_id_err_recovery");
		pr_info("bq->chemic_id_err_recovery = %d\n", bq->chemic_id_err_recovery_support);
		
		ret = of_property_read_string(node, "fg_chemic_id", &bq->fg_chemic_id);
		if (ret < 0)
			pr_err("read fg_chemic_id failed\n");
		pr_info("chemic_id = %s\n", bq->fg_chemic_id);

	} else if (bq->chip == NFG1000_SLAVE) { // bat_b
		bq->nfg1000_slave_gpio = of_get_named_gpio(node, "nfg1000_slave_gpio", 0);
		if (bq->nfg1000_slave_gpio < 0) {
			bq->nfg1000_slave_gpio = U32_MAX;
			pr_err("%s: get master fg ctrl gpio fail\n", __func__);
			//return -1;
		}
		if(bq->nfg1000_slave_gpio != U32_MAX){
			ret = gpio_request(bq->nfg1000_slave_gpio, "nfg1000_slave_gpio");
			if (ret < 0){
				pr_err ("nfg1000_slave_gpio request failed!\n");
			}
		}

		pr_info("%s: nfg1000_slave_gpio:%d",
			__func__, bq->nfg1000_slave_gpio);
		if (of_property_read_s32(node, "tran_Q_max", &bq->nfg1000_slave_qmax) < 0)
			bq->nfg1000_slave_qmax = 2500;
	}

	return 0;
}

static int nfg_set_batt_en(struct nfg_fg_chip *bq, int chip_en)
{
	if(bq->chip == NFG1000_MASTER && bq->nfg1000_master_gpio == U32_MAX)
		return -1;

	if(bq->chip == NFG1000_SLAVE && bq->nfg1000_slave_gpio == U32_MAX)
	{
		return -1;
	}

	if (bq->chip == NFG1000_MASTER) { // bat_a
		if (chip_en == 0)
			gpio_set_value(bq->nfg1000_master_gpio, 1);
		else
			gpio_set_value(bq->nfg1000_master_gpio, 0);
	} else if (bq->chip == NFG1000_SLAVE) { // bat_b
		if (chip_en == 0){
			gpio_set_value(bq->nfg1000_slave_gpio, 1);
		}
		else{
			gpio_set_value(bq->nfg1000_slave_gpio, 0);
		}
	}
	return 0;
}

static int nfg_get_batt_en(struct nfg_fg_chip *bq)
{
	int gpio_value;

	if(bq->chip == NFG1000_MASTER && bq->nfg1000_master_gpio == U32_MAX)
		return -1;

	if(bq->chip == NFG1000_SLAVE && bq->nfg1000_slave_gpio == U32_MAX)
	{
		return -1;
	}

	if (bq->chip == NFG1000_MASTER) { // bat_a
		gpio_value = gpio_get_value(bq->nfg1000_master_gpio);
		if (gpio_value == 0)
			return 1;
		else
			return 0;
	} else if (bq->chip == NFG1000_SLAVE) { // bat_b
		gpio_value = gpio_get_value(bq->nfg1000_slave_gpio);
		if (gpio_value == 0)
			return 1;
		else
			return 0;
	}

	return 0;
}

static int nfg_battery_get_property(struct tran_device *fg_dev,
					enum tran_common_prop prop,
					union com_propval *val)
{
	struct nfg_fg_chip *bq = tran_get_data(fg_dev);
	int ret;

	
	switch (prop) {
	case TRAN_PROP_PRESENT:
		ret = fg_read_volt(bq);
		if (ret < 0) {
			val->intval = 0;
		} else {
			val->intval = 1;
		}
		break;
	case TRAN_PROP_BATT_TEMP:
		if (bq->fake_temp != -EINVAL) {
			val->intval = bq->fake_temp;
			break;
		}
		ret = fg_read_temperature(bq);
		mutex_lock(&bq->data_lock);
		bq->batt_temp = ret;
		val->intval = bq->batt_temp;
		mutex_unlock(&bq->data_lock);
		break;
	case TRAN_PROP_BATT_VOLT:
		ret = fg_read_volt(bq);
		mutex_lock(&bq->data_lock);
		if (ret >= 0)
			bq->batt_volt = ret;
		val->intval = bq->batt_volt * 1000;
		mutex_unlock(&bq->data_lock);
		break;
	case TRAN_PROP_BATT_AI: // Average Current 
		mutex_lock(&bq->data_lock);
		fg_read_current(bq, &bq->batt_curr);
		val->intval = bq->batt_curr * 1000;
		mutex_unlock(&bq->data_lock);
		break;
		/* fg_read_current(bq, &result); */
		/* val->intval = result; */
		/* break; */
	case TRAN_PROP_BATT_STATUS:
		ret = fg_get_batt_status(bq);
		mutex_lock(&bq->data_lock);
		val->intval = bq->batt_flags;
		mutex_unlock(&bq->data_lock);
		break;
	case TRAN_PROP_BATT_TTE: // time to Empty
		ret = fg_read_tte(bq);
		mutex_lock(&bq->data_lock);
		if (ret >= 0)
			bq->batt_tte = ret;

		val->intval = bq->batt_tte;
		mutex_unlock(&bq->data_lock);
		break;
	case TRAN_PROP_BATT_TTF: // time to Full
		mutex_lock(&bq->data_lock);
		fg_read_time_to_full(bq, &bq->ttf);
		val->intval = bq->ttf;
		mutex_unlock(&bq->data_lock);
		break;
	case TRAN_PROP_BATT_FCC:
		ret = fg_read_fcc(bq);
		mutex_lock(&bq->data_lock);
		if (ret > 0)
			bq->batt_fcc = ret;
		val->intval = bq->batt_fcc * 1000;
		mutex_unlock(&bq->data_lock);
		break;
	case TRAN_PROP_BATT_RM:
		ret = fg_read_rm(bq);
		mutex_lock(&bq->data_lock);
		if (ret >= 0)
			bq->batt_rm = ret;
		val->intval = bq->batt_rm;
		mutex_unlock(&bq->data_lock);
		break;
	case TRAN_PROP_BATT_CC: // Cycle Count
		ret = fg_read_cyclecount(bq);
		mutex_lock(&bq->data_lock);
		if (ret >= 0)
			bq->batt_cyclecnt = ret;
		val->intval = bq->batt_cyclecnt;
		mutex_unlock(&bq->data_lock);
		break;
	case TRAN_PROP_BATT_SOC: // Relative State of Charge
		if (bq->fake_soc >= 0) {
			val->intval = bq->fake_soc;
			break;
		}
		ret = fg_read_rsoc(bq);
		mutex_lock(&bq->data_lock);
		if (ret >= 0)
			bq->batt_soc = ret;
		val->intval = bq->batt_soc;
		mutex_unlock(&bq->data_lock);
		break;
	case TRAN_PROP_BATT_SOH:
		val->intval = 0;
		break;
	case TRAN_PROP_BATT_DC: // Full Design
		ret = fg_read_dc(bq);
		mutex_lock(&bq->data_lock);
		if (ret > 0)
			bq->batt_dc = ret;
		val->intval = bq->batt_dc * 1000;
		mutex_unlock(&bq->data_lock);
		break;
	case TRAN_PROP_CHARGE_FULL_DESIGN: 
		mutex_lock(&bq->data_lock);
		if (bq->chip == NFG1000_MASTER)
			val->intval = bq->nfg1000_master_qmax;
		else
			val->intval = bq->nfg1000_slave_qmax;
		mutex_unlock(&bq->data_lock);
		break;
	case TRAN_PROP_BATT_ALT_MAC:
		val->intval = 0;
		break;
	case TRAN_PROP_BATT_MAC_CHKSUM:
		val->intval = 0;
		break;
	case TRAN_PROP_BATT_EN:
		val->intval = nfg_get_batt_en(bq);
		break;
	case TRAN_PROP_BATT_NOW_CURR:
		mutex_lock(&bq->data_lock);
		fg_read_now_current(bq, &bq->now_curr);
		val->intval = bq->now_curr * 1000;
		mutex_unlock(&bq->data_lock);
		break;
	case TRAN_PROP_BATT_FW_STATUS:
		val->intval = bq->update_fw;
		break;
	case TRAN_PROP_BATTERY_RAW_CYCLE:
		ret = fg_read_cyclecount(bq);
		mutex_lock(&bq->data_lock);
		if (ret >= 0)
			bq->batt_cyclecnt = ret;
		val->intval = bq->batt_cyclecnt;
		mutex_unlock(&bq->data_lock);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

__maybe_unused static int nfg_set_fg_en(struct nfg_fg_chip *bq, int chip_en)
{
	if(bq->nfg1000_master_gpio == U32_MAX)
		return -1;

	if (chip_en == 0) 
		gpio_set_value(bq->nfg1000_master_gpio, 1);
	else
		gpio_set_value(bq->nfg1000_master_gpio, 0);

	return 0;
}

static int nfg_battery_set_property(struct tran_device *fg_dev,
					enum tran_common_prop prop,
					const union com_propval *val)
{
	struct nfg_fg_chip *bq = tran_get_data(fg_dev);

	switch (prop) {
	case TRAN_PROP_BATT_TEMP:
		bq->fake_temp = val->intval;
		break;
	case TRAN_PROP_BATT_SOC:
		bq->fake_soc = val->intval;
		/* power_supply_changed(bq->fg_psy); */
		break;
	case TRAN_PROP_BATT_EN:
		bq->chip_en = val->intval;
		nfg_set_batt_en(bq, bq->chip_en);
		pr_err("%s: chip_en:%d\n", __func__, bq->chip_en);
		break;
	case TRAN_PROP_BATT_FAST_CHG:
		mutex_lock(&bq->data_lock);
		nfg_en_fast_charge(bq, val->intval == 1 ? true : false);
		mutex_unlock(&bq->data_lock);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct tran_ops nfg1000_batt_ops = {
	.set_prop = nfg_battery_set_property,
	.get_prop = nfg_battery_get_property,
};

static int nfg_register_battdev(struct nfg_fg_chip *bq)
{
	bq->batt_prop.alias_name = device2str[bq->chip];
	bq->fg_dev = tran_device_register(device2str[bq->chip], bq->dev,
						bq, &nfg1000_batt_ops,
						&bq->batt_prop);

	if (!bq->fg_dev)
		return -EINVAL;
	return 0;
}

static int nfg_chg_status_init(struct nfg_fg_chip *bq)
{
	int ret;
	int gpio_status = 0;

	if(bq->chg_en_gpio == U32_MAX)
		return -1;

	ret = gpio_direction_output(bq->chg_en_gpio, 0);
	if (ret < 0) {
		pr_err("%s: fail to set chg_en_gpio dir\n", __func__);
		return -EINVAL;
	}

	if(bq->stage_diff_gpio != U32_MAX)
		gpio_status = gpio_get_value(bq->stage_diff_gpio);

	if (gpio_status == 1) {
		gpio_set_value(bq->chg_en_gpio, 1);
	} else {
		gpio_set_value(bq->chg_en_gpio, 0);
	}

	return 0;
}

static enum power_supply_property master_fg_props[] = {
	POWER_SUPPLY_PROP_STATUS,
};

static int master_fg_get_property(struct power_supply *psy, enum power_supply_property psp,
					union power_supply_propval *val)
{
	return 0;
}

static int master_fg_set_property(struct power_supply *psy,
			       enum power_supply_property prop,
			       const union power_supply_propval *val)
{
	return 0;
}

static int master_fg_prop_is_writeable(struct power_supply *psy,
				       enum power_supply_property prop)
{
	return 0;
}

static int master_fg_psy_register(struct nfg_fg_chip *bq)
{
	struct power_supply_config fg_psy_cfg = {};

	bq->fg_psy_d.name = "batt_fg_a";
	bq->fg_psy_d.type = POWER_SUPPLY_TYPE_UNKNOWN;
	bq->fg_psy_d.properties = master_fg_props;
	bq->fg_psy_d.num_properties = ARRAY_SIZE(master_fg_props);
	bq->fg_psy_d.get_property = master_fg_get_property;
	bq->fg_psy_d.set_property = master_fg_set_property;
	bq->fg_psy_d.property_is_writeable = master_fg_prop_is_writeable;

	fg_psy_cfg.drv_data = bq;
	fg_psy_cfg.num_supplicants = 0;
	bq->fg_psy = devm_power_supply_register(bq->dev,
						&bq->fg_psy_d,
						&fg_psy_cfg);
	if (IS_ERR(bq->fg_psy)) {
		nfg_err("Failed to register fg_psy");
		return PTR_ERR(bq->fg_psy);
	}
	device_create_file(&bq->fg_psy->dev, &dev_attr_bat_info);
	return 0;
}

static enum power_supply_property slave_fg_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
};

static int slave_fg_get_property(struct power_supply *psy, enum power_supply_property psp,
					union power_supply_propval *val)
{
	return 0;
}

static int slave_fg_set_property(struct power_supply *psy,
			       enum power_supply_property prop,
			       const union power_supply_propval *val)
{
	return 0;
}

static int slave_fg_prop_is_writeable(struct power_supply *psy,
				       enum power_supply_property prop)
{
	return 0;
}

static int slave_fg_psy_register(struct nfg_fg_chip *bq)
{
	struct power_supply_config fg_psy_cfg = {};

	bq->fg_psy_d.name = "batt_fg_b";
	bq->fg_psy_d.type = POWER_SUPPLY_TYPE_UNKNOWN;
	bq->fg_psy_d.properties = slave_fg_props;
	bq->fg_psy_d.num_properties = ARRAY_SIZE(slave_fg_props);
	bq->fg_psy_d.get_property = slave_fg_get_property;
	bq->fg_psy_d.set_property = slave_fg_set_property;
	bq->fg_psy_d.property_is_writeable = slave_fg_prop_is_writeable;

	fg_psy_cfg.drv_data = bq;
	fg_psy_cfg.num_supplicants = 0;
	bq->fg_psy = devm_power_supply_register(bq->dev,
						&bq->fg_psy_d,
						&fg_psy_cfg);
	if (IS_ERR(bq->fg_psy)) {
		nfg_err("Failed to register fg_psy");
		return PTR_ERR(bq->fg_psy);
	}
	device_create_file(&bq->fg_psy->dev, &dev_attr_bat_info);
	return 0;
}

static void fg_psy_unregister(struct nfg_fg_chip *bq)
{
	power_supply_unregister(bq->fg_psy);
	power_supply_unregister(bq->fg_psy);
}

static int nfg_fg_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	int ret;
	struct nfg_fg_chip *bq;
	u8 *regs;

	pr_err("%s: enter\n", __func__);

	bq = devm_kzalloc(&client->dev, sizeof(*bq), GFP_KERNEL);
	if (!bq)
		return -ENOMEM;

	client->addr = 0x55;
	bq->dev = &client->dev;
	bq->client = client;
	bq->chip = id->driver_data;

	bq->batt_soc	= 50;
	bq->batt_fcc	= -ENODATA;
	bq->batt_rm	= -ENODATA;
	bq->batt_dc	= -ENODATA;
	bq->batt_volt	= -ENODATA;
	bq->batt_temp	= 280;
	bq->temp_flag   = false;
	bq->batt_curr	= -ENODATA;
	bq->batt_cyclecnt = -ENODATA;

	bq->fake_soc	= -EINVAL;
	bq->fake_temp	= -EINVAL;
	bq->chip_en     = 1;
	bq->update_fw   = false;
	bq->dev->driver_data = bq;

	if (bq->chip == NFG1000 ||
		bq->chip == NFG1000_MASTER ||
		bq->chip == NFG1000_SLAVE) {
		regs = nfg1000_regs;
	} else {
		nfg_err("unexpected fuel gauge: %d\n", bq->chip);
		regs = nfg1000_regs;
	}
	memcpy(bq->regs, regs, NUM_REGS);

	i2c_set_clientdata(client, bq);

	mutex_init(&bq->i2c_rw_lock);
	mutex_init(&bq->data_lock);

	device_init_wakeup(bq->dev, 1);

	bq->i2c_support_dma = of_property_read_bool(bq->dev->of_node, "i2c_support_dma");
	pr_info("i2c support dma  = %d\n", bq->i2c_support_dma);
	if (bq->i2c_support_dma) {
		if (fg_device_name_check(bq) || fg_mfn_check(bq)) {
			return -ENODEV;
		}
	}
	dev_info(bq->dev, "batt%d dc = %d\n", bq->chip, fg_read_dc(bq));

	nfg1000_parse_dt(bq);

	ret = sysfs_create_group(&bq->dev->kobj, &fg_attr_group);
	if (ret)
		nfg_err("Failed to register sysfs, err:%d\n", ret);

	ret = nfg_register_battdev(bq);
	if (ret < 0) {
		pr_err("%s: register battery device fail\n", __func__);
		return ret;
	}

	nfg1000_chip_init(bq);

	ret = device_create_file(bq->dev, &dev_attr_bat_en);
	if (ret < 0)
		pr_err("%s: dev_attr_bat_en error\n", __func__);

	if (bq->chip == NFG1000_MASTER) { // bat_a
		nfg_chg_status_init(bq);
		master_fg_psy_register(bq);
	} else if (bq->chip == NFG1000_SLAVE) {
		slave_fg_psy_register(bq);
	}

#if IS_ENABLED(CONFIG_TC_FUEL_GAUGE_NFG1000_OTA)
	INIT_DELAYED_WORK(&bq->ota_work, fg_ota_work);
	schedule_delayed_work(&bq->ota_work, msecs_to_jiffies(25000));
#endif

	nfg_log("nfg fuel gauge probe successfully, %s\n", device2str[bq->chip]);

	return 0;
}

static inline bool is_device_suspended(struct nfg_fg_chip *bq)
{
	return 0;	
}

static int nfg_fg_suspend(struct device *dev)
{
	return 0;
}

static int nfg_fg_suspend_noirq(struct device *dev)
{
	return 0;
}

static int nfg_fg_resume(struct device *dev)
{
	return 0;
}

static void nfg_fg_remove(struct i2c_client *client)
{
	struct nfg_fg_chip *bq = i2c_get_clientdata(client);

	fg_psy_unregister(bq);
	mutex_destroy(&bq->data_lock);
	mutex_destroy(&bq->i2c_rw_lock);

	sysfs_remove_group(&bq->dev->kobj, &fg_attr_group);

}

static void nfg_fg_shutdown(struct i2c_client *client)
{
	pr_err("bq fuel gauge driver shutdown!\n");
}

static struct of_device_id nfg_fg_match_table[] = {
	{.compatible = "nfg,nfg1000",},
	{.compatible = "nfg,nfg1000_master",},
	{.compatible = "nfg,nfg1000_slave",},
	{},
};
MODULE_DEVICE_TABLE(of, nfg_fg_match_table);

static const struct i2c_device_id nfg_fg_id[] = {
	{ "nfg1000", NFG1000},
	{ "nfg1000_master", NFG1000_MASTER },
	{ "nfg1000_slave", NFG1000_SLAVE },
	{},
};
MODULE_DEVICE_TABLE(i2c, nfg_fg_id);

static const struct dev_pm_ops nfg_fg_pm_ops = {
	.resume		= nfg_fg_resume,
	.suspend_noirq = nfg_fg_suspend_noirq,
	.suspend	= nfg_fg_suspend,
};

static struct i2c_driver nfg_fg_driver = {
	.driver	= {
		.name   = "nfg_fg",
		.owner  = THIS_MODULE,
		.of_match_table = nfg_fg_match_table,
		.pm     = &nfg_fg_pm_ops,
	},
	.id_table       = nfg_fg_id,

	.probe          = nfg_fg_probe,
	.remove		= nfg_fg_remove,
	.shutdown	= nfg_fg_shutdown,

};

module_i2c_driver(nfg_fg_driver);

MODULE_DESCRIPTION("NFG1000 Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Texas Instruments");
