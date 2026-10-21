// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 Transsion Inc.
 */

#define pr_fmt(fmt)	"[bq27z561] %s: " fmt, __func__
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
#include "tc_bq27z561.h"
#include "tc_common_class.h"

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
#endif

#define bq_info	pr_info
#define bq_dbg	pr_debug
#define bq_err	pr_err
#define bq_log	pr_err

#define	INVALID_REG_ADDR	0xFF

#define FG_FLAGS_FD				BIT(4)
#define	FG_FLAGS_FC				BIT(5)
#define	FG_FLAGS_DSG				BIT(6)
#define FG_FLAGS_RCA				BIT(9)

enum bq_fg_reg_idx {
	BQ_FG_REG_CTRL = 0,
	BQ_FG_REG_TEMP,		/* Battery Temperature */
	BQ_FG_REG_VOLT,		/* Battery Voltage */
	BQ_FG_REG_CURR,		/* Battery Current */
	BQ_FG_REG_AI,		/* Average Current */
	BQ_FG_REG_BATT_STATUS,	/* BatteryStatus */
	BQ_FG_REG_TTE,		/* Time to Empty */
	BQ_FG_REG_TTF,		/* Time to Full */
	BQ_FG_REG_FCC,		/* Full Charge Capacity */
	BQ_FG_REG_RM,		/* Remaining Capacity */
	BQ_FG_REG_CC,		/* Cycle Count */
	BQ_FG_REG_SOC,		/* Relative State of Charge */
	BQ_FG_REG_SOH,		/* State of Health */
	BQ_FG_REG_DC,		/* Design Capacity */
	BQ_FG_REG_ALT_MAC,	/* AltManufactureAccess*/
	BQ_FG_REG_MAC_CHKSUM,	/* MACChecksum */
	NUM_REGS,
};

enum bq_fg_sub_cmd {
	FG_SUB_CMD_CTRL_STATUS	= 0x0000,
	FG_SUB_CMD_CHECKSUM	= 0x0004,
	/* FG_SUB_CMD_CHECKSUM	= 0x0009, */
	FG_SUB_CMD_SEAL		= 0x0020,
	FG_SUB_CMD_CALI_MODE    = 0x0040,
	FG_SUB_CMD_DEV_RESET	= 0x0041,
	FG_SUB_CMD_QUICK_START  = 0x0042,
	FG_SUB_CMD_FW_VER	= 0x0022,
	FG_SUB_CMD_UPD_EN       = 0x001F,
};

enum bq_fg_mac_cmd {
	FG_MAC_CMD_CTRL_STATUS	= 0x0000,
	FG_MAC_CMD_DEV_TYPE	= 0x0001,
	FG_MAC_CMD_FW_VER	= 0x0002,
	FG_MAC_CMD_HW_VER	= 0x0003,
	FG_MAC_CMD_IF_SIG	= 0x0004,
	FG_MAC_CMD_CHEM_ID	= 0x0006,
	FG_MAC_CMD_GAUGING	= 0x0021,
	FG_MAC_CMD_SEAL		= 0x0030,
	FG_MAC_CMD_DEV_RESET	= 0x0041,
};

enum {
	SEAL_STATE_RSVED,
	SEAL_STATE_UNSEALED,
	SEAL_STATE_SEALED,
	SEAL_STATE_FA,
	SEAL_STATE_ALL_UNSEALED,
};

enum bq_fg_device {
	BQ27Z561_MASTER,
	BQ27Z561_SLAVE,
};

static const unsigned char *device2str[] = {
	"tran_fg_master",
	"tran_fg_slave",
};

static u8 bq27z561_regs[NUM_REGS] = {
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

#define BQ27Z561_BLOCK_NUM 4
#define BQ27Z561_DM_SZ 32

struct bq_gi_data {
	int class_id;
	int block_id[BQ27Z561_BLOCK_NUM];
	u8 block_data[BQ27Z561_DM_SZ * BQ27Z561_BLOCK_NUM];
	int checksum;
};

struct bq_fg_chip {
	struct device *dev;
	struct i2c_client *client;

	struct mutex i2c_rw_lock;
	struct mutex data_lock;

	bool irq_waiting;
	bool irq_disabled;
	bool resume_completed;

	int fw_ver;
	int df_ver;

	u8 chip;
	u8 regs[NUM_REGS];

	/* status tracking */

	bool batt_fc;
	bool batt_fd;	/* full depleted */

	bool batt_dsg;
	bool batt_rca;	/* remaining capacity alarm */
	bool batt_exio_en;
	bool i2c_support_dma;

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

	/* debug */
	int skip_reads;
	int skip_writes;

	int fake_soc;
	int fake_temp;

	/* ctrl gpio */
	int bq27z561_master_gpio;
	int bq27z561_slave_gpio;
	int chg_en_gpio;
	int stage_diff_gpio;
	int chip_en;

	int BAT_STATUS;
	wait_queue_head_t  wait_que;
	int monitor_flag;
	struct hrtimer bq27z561_hrtimer;
	struct task_struct *battery_monitor;

	struct external_gauge_sysfs_field_info *attr;

	struct tran_device *fg_dev;
	struct tran_properties batt_prop;

	int class_num;
	const char *class_str;
	struct bq_gi_data *gi_data;
	bool update_fw;
	bool fw_v2001;
	struct delayed_work read_fw_work;
	struct pinctrl *pinctrl;
	struct pinctrl_state *i2c_scl_gpio_init;
	struct pinctrl_state *i2c_sda_gpio_init;

};

static ssize_t store_bat_en(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct bq_fg_chip *bq = dev->driver_data;
	int ret = 0;
	unsigned int val = 0;

	if (buf != NULL && size != 0) {
		ret = kstrtouint(buf, 10, &val);
		pr_err("%s: val = %d\n", __func__, val);

		if(val != 0 && val != 1)
			return size;

		switch(bq->chip)
		{
			case BQ27Z561_MASTER:
				if(bq->bq27z561_master_gpio != U32_MAX)
					gpio_set_value(bq->bq27z561_master_gpio, val);
				break;
			case BQ27Z561_SLAVE:
#if IS_ENABLED(CONFIG_TRAN_AW95016)
				if(bq->batt_exio_en){
					if(aw95016_set_output(P0_7,val) < 0)
						pr_info("%s,set aw ic gpio %d fail\n", __func__,val);
				}else
#endif
					if(bq->bq27z561_slave_gpio != U32_MAX)
						gpio_set_value(bq->bq27z561_slave_gpio, val);
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
	struct bq_fg_chip *bq = dev->driver_data;
	int gpio = 0;

	switch(bq->chip)
	{
		case BQ27Z561_MASTER:
			if(bq->bq27z561_master_gpio != U32_MAX)
				gpio = gpio_get_value(bq->bq27z561_master_gpio);
			break;
		case BQ27Z561_SLAVE:
#if IS_ENABLED(CONFIG_TRAN_AW95016)
			if(bq->batt_exio_en){
				if(aw95016_get_level(P0_7,&gpio) < 0)
					pr_info("%s,get aw ic gpio fail\n", __func__);
			}else
#endif
				if(bq->bq27z561_slave_gpio != U32_MAX)
					gpio = gpio_get_value(bq->bq27z561_slave_gpio);
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
		bq_err("i2c read word fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}

	*val = (u16)ret;

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
		bq_err("i2c write word fail:can't write 0x%02X to reg 0x%02X\n", val, reg);
		return ret;
	}

	return 0;
}

static int __fg_read_block(struct i2c_client *client, u8 reg, u8 *buf, u8 len)
{

	int ret;

	ret = i2c_smbus_read_i2c_block_data(client, reg, len, buf);

	return ret;
}

static int __fg_write_block(struct i2c_client *client, u8 reg, u8 *buf, u8 len)
{
	int ret;

	ret = i2c_smbus_write_i2c_block_data(client, reg, len, buf);

	return ret;
}

static int fg_read_word(struct bq_fg_chip *bq, u8 reg, u16 *val)
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

__maybe_unused static int fg_write_word(struct bq_fg_chip *bq, u8 reg, u16 val)
{
	int ret;

	if (bq->skip_writes)
		return 0;

	mutex_lock(&bq->i2c_rw_lock);
	ret = __fg_write_word(bq->client, reg, val);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;
}

__maybe_unused static int fg_read_block(struct bq_fg_chip *bq, u8 reg, u8 *buf, u8 len)
{
	int ret;

	if (bq->skip_reads)
		return 0;
	mutex_lock(&bq->i2c_rw_lock);
	ret = __fg_read_block(bq->client, reg, buf, len);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;

}

__maybe_unused static int fg_write_block(struct bq_fg_chip *bq, u8 reg, u8 *data, u8 len)
{
	int ret;

	if (bq->skip_writes)
		return 0;

	mutex_lock(&bq->i2c_rw_lock);
	ret = __fg_write_block(bq->client, reg, data, len);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;
}

static u8 checksum(u8 *data, u8 len)
{
	u8 i;
	u16 sum = 0;

	pr_err("%s: len:%d\n", __func__, len);

	if (len <= 0) {
		return 1;
	}

	for (i = 0; i < len; i++) {
		sum += data[i];
	}

	sum &= 0xFF;

	return 0xFF - sum;
	/* return sum; */
}

#define I2C_NO_REG_DATA			0XFF	//no reg op, only send data
#define I2C_MAX_BUFFER_SIZE		4
static int fg_i2c_read(struct bq_fg_chip *bq, u8 reg, u8 *poutbuf, u32 len)
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

static int fg_i2c_write(struct bq_fg_chip *bq, u8 reg, u8 *pinbuf, u32 len)
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
		dev_err(bq->dev, "%s:write reg:0x%x failed!!\n",__func__, reg);
	}

	return ret == msg_len ? 0 : -1;
}

static int fg_mac_read_block(struct bq_fg_chip *dev, u16 reg, u8 *poutbuf, u32 len)
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
	ret = fg_i2c_write(dev,0x3E,reg_data,2);
	if(ret < 0)
	{
		dev_err(dev->dev, "%s:read reg:%d failed!!\n",__func__, reg);
		return -1;
	}
	msleep(10);

	ret = fg_i2c_read(dev,0x3E,block_data,8);
	if(ret < 0) {
		dev_err(dev->dev, "%s:read reg1:%d failed!!=%d\n",__func__, reg, ret);
		return -1;
	}
	ret = fg_i2c_read(dev,0x46,&block_data[8],8);
	if(ret < 0) {
		dev_err(dev->dev, "%s:read reg2:%d failed!!=%d\n",__func__, reg, ret);
		return -1;
	}
	ret = fg_i2c_read(dev,0x4E,&block_data[16],8);
	if(ret < 0) {
		dev_err(dev->dev, "%s:read reg3:%d failed!!=%d\n",__func__, reg, ret);
		return -1;
	}
	ret = fg_i2c_read(dev,0x56,&block_data[24],8);
	if(ret < 0) {
		dev_err(dev->dev, "%s:read reg4:%d failed!!=%d\n",__func__, reg, ret);
		return -1;
	}
	ret = fg_i2c_read(dev,0x5E,&block_data[32],4);
	if(ret < 0) {
		dev_err(dev->dev, "%s:read reg5:%d failed!!=%d\n",__func__, reg, ret);
		return -1;
	}
	if(ret < 0)
	{
		dev_err(dev->dev, "%s:read reg:%d failed!!\n",__func__, reg);
		return -1;
	}

	//cksum_calc = checksum(t_buf, t_len - 2);
	if((reg_data[0] != block_data[0]) || (reg_data[1] != block_data[1]) ||
		(block_data[34] != checksum(block_data,34)))
	{
		dev_err(dev->dev, "%s:command:(%d,%d)(%d,%d)(%d,%d) failed!!\n", __func__,
			reg_data[0],block_data[0],reg_data[1],block_data[1],
			checksum(block_data,34), block_data[34]);
		return -2;
	}
	dev_err(dev->dev, "%s: read len = %d,%d\n",__func__, block_data[35],len);
	memcpy(poutbuf,&block_data[2],len);
	mdelay(5);

	return 0;
}

static int fg_read_status(struct bq_fg_chip *bq)
{
	int ret;
	u16 flags;

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_BATT_STATUS], &flags);
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

	if (bq->chip == BQ27Z561_MASTER) {
		pr_info("master_dump:flags:0x%X, batt_fc:%d, batt_fd:%d, batt_rca:%d, batt_dsg:%d\n",
			flags, bq->batt_fc, bq->batt_fd, bq->batt_rca, bq->batt_dsg);
	} else {
		pr_info("slave_dump:flags:0x%X, batt_fc:%d, batt_fd:%d, batt_rca:%d, batt_dsg:%d\n",
			flags, bq->batt_fc, bq->batt_fd, bq->batt_rca, bq->batt_dsg);
	}

	//fg_debug_dump_regs(bq);

	return 0;
}

static int fg_read_rsoc(struct bq_fg_chip *bq)
{
	int ret;
	u16 soc = 0;

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_SOC], &soc);
	if (ret < 0) {
		bq_err("could not read RSOC, ret = %d\n", ret);
		return ret;
	}

	return soc;

}

static int fg_read_temperature(struct bq_fg_chip *bq)
{
	int ret;
	u16 temp = 0;
	int temp_val;

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_TEMP], &temp);
	if (ret < 0) {
		bq_err("could not read temperature, ret = %d\n", ret);
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
		bq_err("ignore this temp, raw:%d, temp:%d\n", temp, temp - 2730);
		return bq->batt_temp;
	}

	/* K -> C */
	return temp - 2730;
}

static int fg_read_volt(struct bq_fg_chip *bq)
{
	int ret;
	u16 volt = 0;

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_VOLT], &volt);
	if (ret < 0) {
		bq_err("could not read voltage, ret = %d\n", ret);
		return ret;
	}

	return volt;

}

static int fg_read_now_current(struct bq_fg_chip *bq, int *curr)
{
	int ret;
	u16 now_curr = 0;

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_CURR], &now_curr);
	if (ret < 0) {
		bq_err("could not read now current, ret = %d\n", ret);
		return ret;
	}
	*curr = (int)((s16)now_curr);

	return ret;
}

static int fg_read_current(struct bq_fg_chip *bq, int *curr)
{
	int ret;
	u16 avg_curr = 0;

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_AI], &avg_curr);
	if (ret < 0) {
		bq_err("could not read current, ret = %d\n", ret);
		return ret;
	}
	*curr = (int)((s16)avg_curr);
	
	return ret;
}

static int fg_read_fcc(struct bq_fg_chip *bq)
{
	int ret;
	u16 fcc;

	if (bq->regs[BQ_FG_REG_FCC] == INVALID_REG_ADDR) {
		bq_err("FCC command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_FCC], &fcc);

	if (ret < 0)
		bq_err("could not read FCC, ret=%d\n", ret);

	return fcc;
}

static int fg_read_dc(struct bq_fg_chip *bq)
{

	int ret;
	u16 dc;

	if (bq->regs[BQ_FG_REG_DC] == INVALID_REG_ADDR) {
		bq_err("DesignCapacity command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_DC], &dc);

	if (ret < 0) {
		bq_err("could not read DC, ret=%d\n", ret);
		return ret;
	}

	return dc;
}

static int fg_read_rm(struct bq_fg_chip *bq)
{
	int ret;
	u16 rm;

	if (bq->regs[BQ_FG_REG_RM] == INVALID_REG_ADDR) {
		bq_err("RemainingCapacity command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_RM], &rm);

	if (ret < 0) {
		bq_err("could not read DC, ret=%d\n", ret);
		return ret;
	}

	return rm;

}

static int fg_read_cyclecount(struct bq_fg_chip *bq)
{
	int ret;
	u16 cc;

	if (bq->regs[BQ_FG_REG_CC] == INVALID_REG_ADDR) {
		bq_err("Cycle Count not supported!\n");
		return -1;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_CC], &cc);

	if (ret < 0) {
		bq_err("could not read Cycle Count, ret=%d\n", ret);
		return ret;
	}

	return cc;
}

static int fg_read_tte(struct bq_fg_chip *bq)
{
	int ret;
	u16 tte;

	if (bq->regs[BQ_FG_REG_TTE] == INVALID_REG_ADDR) {
		bq_err("Time To Empty not supported!\n");
		return -1;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_TTE], &tte);

	if (ret < 0) {
		bq_err("could not read Time To Empty, ret=%d\n", ret);
		return ret;
	}

	if (ret == 0xFFFF)
		return -ENODATA;

	return tte;
}

static int fg_get_batt_status(struct bq_fg_chip *bq)
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

static const u8 fg_dump_regs[] = {
	0x00, 0x02, 0x04, 0x06,
	0x08, 0x0A, 0x0C, 0x0E,
	0x10, 0x16, 0x18, 0x1A,
	0x1C, 0x1E, 0x20, 0x28,
	0x2A, 0x2C, 0x2E, 0x30,
	0x66, 0x68, 0x6C, 0x6E,
};

static ssize_t fg_attr_show_Ra_table(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bq_fg_chip *bq = i2c_get_clientdata(client);
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
	struct bq_fg_chip *bq = i2c_get_clientdata(client);
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

static void bq27z561_chip_init(struct bq_fg_chip *bq)
{
	int ret;

	if(bq->chip == BQ27Z561_MASTER && bq->bq27z561_master_gpio == U32_MAX)
		return;

	if(bq->chip == BQ27Z561_SLAVE && bq->bq27z561_slave_gpio == U32_MAX)
	{
		return;
	}

	if (bq->chip == BQ27Z561_MASTER) { // bat_a
		ret = gpio_direction_output(bq->bq27z561_master_gpio, 0);
		if (ret < 0) {
			pr_err("%s: fail to set bq27z561_master_gpio dir\n", __func__);
			return;
		}

		gpio_set_value(bq->bq27z561_master_gpio, 0);

		ret = gpio_get_value(bq->bq27z561_master_gpio);

		pr_err("%s: bq27z561_master_gpio state:%d\n", __func__, ret);
	} else if (bq->chip == BQ27Z561_SLAVE) {
#if IS_ENABLED(CONFIG_TRAN_AW95016)
		if(bq->batt_exio_en)
		{
			aw95016_set_dir(P0_7,AW95016_GPIO_OUTPUT);
			if(aw95016_set_output(P0_7,0) < 0)
				pr_info("%s,set aw ic gpio 0 fail\n", __func__);
			goto out; 
		}
#endif
				ret = gpio_direction_output(bq->bq27z561_slave_gpio, 0);
				if (ret < 0) {
					pr_err("%s: fail to set bq27z561_slave_gpio dir\n", __func__);
					return;
				}

				gpio_set_value(bq->bq27z561_slave_gpio, 0);

				ret = gpio_get_value(bq->bq27z561_slave_gpio);

				pr_err("%s: bq27z561_slave_gpio state:%d\n", __func__, ret);
#if IS_ENABLED(CONFIG_TRAN_AW95016)
out:
		return;
#endif
 	}

}

static int fg_device_name_check(struct bq_fg_chip *bq)
{
	int ret = 0;
	u16 device_name_cmd = 0x004a;
	u8 read_out[32];
	u8 p[2][3] = {
		{'T', 'S', 'N'},
		{'N', 'F', 'G'}
	};
	int i, j;
	int match = 0;
	int rows = sizeof(p) / sizeof(p[0]);
	int cols = sizeof(p[0]) / sizeof(p[0][0]);

	ret = fg_mac_read_block(bq, device_name_cmd, read_out, 15);
	if(ret) {
		dev_err(bq->dev, "%s, failed = %d\n", __func__, ret);
		return ret;
	}
	dev_info(bq->dev, "%s, device name: %s\n", __func__, read_out);
	for (i = 0; i < rows; i++) {
		for (j = 0; j < cols; j++) {
			if (p[i][j] != read_out[j]) {
				break;
			}
		}
		if (j >= cols) {
			match = 1;
			dev_err(bq->dev, "%s, device name match\n", __func__);
			break;
		}
	}
	return match ? 0 : -1;
}
/*
static int fg_device_name_check_nfg(struct bq_fg_chip *bq)
{
	int ret = 0;
	u16 device_name_cmd = 0x004a;
	u8 read_out[32];
	u8 p[3] = {'N', 'F', 'G'};
	int i;

	ret = fg_mac_read_block(bq, device_name_cmd, read_out, 15);
	if(ret) {
		dev_err(bq->dev, "%s, failed = %d\n", __func__, ret);
		return ret;
	}
	for (i = 0; i < sizeof(p); i++) {
		if (read_out[i] != p[i]) {
			dev_info(bq->dev, "%s, device name not match\n", __func__);
			return -1;
		}
	}
	return 0;
}
*/
static int fg_mfn_check(struct bq_fg_chip *bq)
{
	int ret;
	u8 read_out[32];
	u16 device_mfn_cmd = 0x004c;
	u8 mfn_info[2][3] = {
		{0x4e, 0x56, 0x54},//NVT
		{0x4d, 0x50, 0x43} //MPC
		};
	int i, j;
	int match = 0;
	int rows = sizeof(mfn_info) / sizeof(mfn_info[0]);
	int cols = sizeof(mfn_info[0]) / sizeof(mfn_info[0][0]);

	ret = fg_mac_read_block(bq, device_mfn_cmd, read_out, 11);
	if(ret) {
		dev_err(bq->dev, "%s, failed = %d\n", __func__, ret);
		return ret;
	}
	dev_info(bq->dev, "%s, mfn name: %s\n", __func__, read_out);
	for (i = 0; i < rows; i++) {
		for (j = 0; j < cols; j++) {
			if (mfn_info[i][j] != read_out[j]) {
				break;
			}
		}
		if (j >= cols) {
			match = 1;
			dev_err(bq->dev, "%s, device name match\n", __func__);
			break;
		}
	}
	return match ? 0 : -1;
}

static int bq27z561_parse_dt(struct bq_fg_chip *bq)
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

	if (bq->chip == BQ27Z561_MASTER) { // bat_a

		bq->bq27z561_master_gpio = of_get_named_gpio(node, "bq27z561_master_gpio", 0);
		if (bq->bq27z561_master_gpio < 0) {
			bq->bq27z561_master_gpio = U32_MAX;
			pr_err("%s: get master fg ctrl gpio fail\n", __func__);
			//return -1;
		}
		if(bq->bq27z561_master_gpio != U32_MAX){
			ret = gpio_request(bq->bq27z561_master_gpio, "bq27z561_master_gpio");
			if (ret < 0){
				pr_err ("bq27z561_master_gpio request failed!\n");
			}
		}
		pr_info("%s: bq27z561_master_gpio:%d",
			__func__, bq->bq27z561_master_gpio);

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

		bq->batt_exio_en = of_property_read_bool(node, "bq,batt_exio_en");
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

	} else if (bq->chip == BQ27Z561_SLAVE) { // bat_b

#if IS_ENABLED(CONFIG_TRAN_AW95016)
		bq->bq27z561_slave_gpio = U32_MAX;
		if(!bq->batt_exio_en)
#endif
		{
				bq->bq27z561_slave_gpio = of_get_named_gpio(node, "bq27z561_slave_gpio", 0);
				if (bq->bq27z561_slave_gpio < 0) {
					bq->bq27z561_slave_gpio = U32_MAX;
					pr_err("%s: get master fg ctrl gpio fail\n", __func__);
					//return -1;
				}
				if(bq->bq27z561_slave_gpio != U32_MAX){
					ret = gpio_request(bq->bq27z561_slave_gpio, "bq27z561_slave_gpio");
					if (ret < 0){
						pr_err ("bq27z561_slave_gpio request failed!\n");
					}
				}

				pr_info("%s: bq27z561_slave_gpio:%d",
					__func__, bq->bq27z561_slave_gpio);
		}
	}

	return 0;
}

static int bq_set_batt_en(struct bq_fg_chip *bq, int chip_en)
{
	if (bq->chip == BQ27Z561_MASTER &&
		bq->bq27z561_master_gpio == U32_MAX)
		return -1;

	if (bq->chip == BQ27Z561_SLAVE &&
		bq->bq27z561_slave_gpio == U32_MAX)
		return -1;

	if (bq->chip == BQ27Z561_MASTER) { // bat_a
		if (chip_en == 0)
			gpio_set_value(bq->bq27z561_master_gpio, 1);
		else
			gpio_set_value(bq->bq27z561_master_gpio, 0);
	} else if (bq->chip == BQ27Z561_SLAVE) { // bat_b
		if (chip_en == 0)
			gpio_set_value(bq->bq27z561_slave_gpio, 1);
		else
			gpio_set_value(bq->bq27z561_slave_gpio, 0);
	}
	
	return 0;
}

static int bq_get_batt_en(struct bq_fg_chip *bq)
{
	int gpio_value;

	if(bq->chip == BQ27Z561_MASTER &&
		bq->bq27z561_master_gpio == U32_MAX)
		return 1;

	if(bq->chip == BQ27Z561_SLAVE &&
		bq->bq27z561_slave_gpio == U32_MAX)
		return -1;

	if (bq->chip == BQ27Z561_MASTER) { // bat_a
		gpio_value = gpio_get_value(bq->bq27z561_master_gpio);
		if (gpio_value == 0)
			return 1;
		else
			return 0;
	} else if (bq->chip == BQ27Z561_SLAVE) { // bat_b
		gpio_value = gpio_get_value(bq->bq27z561_slave_gpio);
		if (gpio_value == 0)
			return 1;
		else
			return 0;
	}

	return 0;
}

static int bq_battery_get_property(struct tran_device *fg_dev,
					enum tran_common_prop prop,
					union com_propval *val)
{
	struct bq_fg_chip *bq = tran_get_data(fg_dev);
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
		pr_info("batt_volt = %d\n", ret);
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
		val->intval = 0;
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
	case TRAN_PROP_BATT_ALT_MAC:
		val->intval = 0;
		break;
	case TRAN_PROP_BATT_MAC_CHKSUM:
		val->intval = 0;
		break;
	case TRAN_PROP_BATT_EN:
		val->intval = bq_get_batt_en(bq);
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

static int bq_battery_set_property(struct tran_device *fg_dev,
					enum tran_common_prop prop,
					const union com_propval *val)
{
	struct bq_fg_chip *bq = tran_get_data(fg_dev);

	switch (prop) {
	case TRAN_PROP_BATT_TEMP:
		bq->fake_temp = val->intval;
		break;
	case TRAN_PROP_BATT_SOC:
		bq->fake_soc = val->intval;
		break;
	case TRAN_PROP_BATT_EN:
		bq->chip_en = val->intval;
		bq_set_batt_en(bq, bq->chip_en);
		pr_err("%s: chip_en:%d\n", __func__, bq->chip_en);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct tran_ops bq27z561_batt_ops = {
	.set_prop = bq_battery_set_property,
	.get_prop = bq_battery_get_property,
};

static int bq_register_battdev(struct bq_fg_chip *bq)
{
	bq->batt_prop.alias_name = device2str[bq->chip];
	bq->fg_dev = tran_device_register(device2str[bq->chip], bq->dev,
						bq, &bq27z561_batt_ops,
						&bq->batt_prop);

	if (!bq->fg_dev)
		return -EINVAL;
	return 0;
}

static int bq_chg_status_init(struct bq_fg_chip *bq)
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

static void fg_read_fw_version(struct bq_fg_chip *bq)
{

	int ret;
	u8 buf[36];

	ret = fg_write_word(bq, bq->regs[BQ_FG_REG_ALT_MAC], FG_MAC_CMD_FW_VER);

	if (ret < 0) {
		bq_err("Failed to send firmware version subcommand:%d\n", ret);
		return;
	}

	mdelay(2);

	ret = fg_mac_read_block(bq, bq->regs[BQ_FG_REG_ALT_MAC], buf, 11);
	if (ret < 0) {
		bq_err("Failed to read firmware version:%d\n", ret);
		return;
	}

	pr_err("FW Ver:%04X, Build:%04X\n",
		buf[2] << 8 | buf[3], buf[4] << 8 | buf[5]);
	pr_err("Ztrack Ver:%04X\n", buf[7] << 8 | buf[8]);
}

static void bq_read_fw_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct bq_fg_chip *bq = container_of(dwork,
				struct bq_fg_chip, read_fw_work);

	fg_read_fw_version(bq);
}

static int bq_fg_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	int ret;
	struct bq_fg_chip *bq;
	u8 *regs;

	pr_err("%s: enter\n", __func__);

	bq = devm_kzalloc(&client->dev, sizeof(*bq), GFP_KERNEL);
	if (!bq)
		return -ENOMEM;

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
	bq->fw_v2001    = false;
	/* bq->attr = third_fg_sysfs_field_tbl;  */
	bq->dev->driver_data = bq;

	if (bq->chip == BQ27Z561_MASTER ||
		bq->chip == BQ27Z561_SLAVE) {
		regs = bq27z561_regs;
	} else {
		bq_err("unexpected fuel gauge: %d\n", bq->chip);
		regs = bq27z561_regs;
	}

	memcpy(bq->regs, regs, NUM_REGS);

	i2c_set_clientdata(client, bq);

	mutex_init(&bq->i2c_rw_lock);
	mutex_init(&bq->data_lock);

	device_init_wakeup(bq->dev, 1);

	bq->i2c_support_dma = of_property_read_bool(bq->dev->of_node, "i2c_support_dma");
	pr_info("i2c support dma  = %d\n", bq->i2c_support_dma);
	if (bq->i2c_support_dma) {
		if (fg_device_name_check(bq) || fg_mfn_check(bq))
			return -ENODEV;
	}
	dev_info(bq->dev, "batt%d dc = %d\n", bq->chip, fg_read_dc(bq));
	
	bq27z561_parse_dt(bq);
	INIT_DELAYED_WORK(&bq->read_fw_work, bq_read_fw_work);
	ret = sysfs_create_group(&bq->dev->kobj, &fg_attr_group);
	if (ret)
		bq_err("Failed to register sysfs, err:%d\n", ret);

	ret = bq_register_battdev(bq);
	if (ret < 0) {
		pr_err("%s: register battery device fail\n", __func__);
		return ret;
	}	

	bq27z561_chip_init(bq);

	ret = device_create_file(bq->dev, &dev_attr_bat_en);
	if (ret < 0)
		pr_err("%s: dev_attr_bat_en error\n", __func__);

	if (bq->chip == BQ27Z561_MASTER) { // bat_a
		bq_chg_status_init(bq);

	}

	bq_log("bq fuel gauge probe successfully, %s\n", device2str[bq->chip]);

	return 0;
}

static inline bool is_device_suspended(struct bq_fg_chip *bq)
{
	return 0;	
}

static int bq_fg_suspend(struct device *dev)
{

	return 0;
}

static int bq_fg_suspend_noirq(struct device *dev)
{
	return 0;

}

static int bq_fg_resume(struct device *dev)
{
	return 0;
}

static void bq_fg_remove(struct i2c_client *client)
{
	struct bq_fg_chip *bq = i2c_get_clientdata(client);

	mutex_destroy(&bq->data_lock);
	mutex_destroy(&bq->i2c_rw_lock);

	sysfs_remove_group(&bq->dev->kobj, &fg_attr_group);

}

static void bq_fg_shutdown(struct i2c_client *client)
{
	pr_err("bq fuel gauge driver shutdown!\n");
}

static struct of_device_id bq_fg_match_table[] = {
	{.compatible = "ti,bq27z561_master",},
	{.compatible = "ti,bq27z561_slave",},
	{},
};
MODULE_DEVICE_TABLE(of, bq_fg_match_table);

static const struct i2c_device_id bq_fg_id[] = {
	{ "bq27z561_master", BQ27Z561_MASTER },
	{ "bq27z561_slave", BQ27Z561_SLAVE },
	{},
};
MODULE_DEVICE_TABLE(i2c, bq_fg_id);

static const struct dev_pm_ops bq_fg_pm_ops = {
	.resume		= bq_fg_resume,
	.suspend_noirq = bq_fg_suspend_noirq,
	.suspend	= bq_fg_suspend,
};

static struct i2c_driver bq_fg_driver = {
	.driver	= {
		.name   = "bq_fg",
		.owner  = THIS_MODULE,
		.of_match_table = bq_fg_match_table,
		.pm     = &bq_fg_pm_ops,
	},
	.id_table       = bq_fg_id,

	.probe          = bq_fg_probe,
	.remove		= bq_fg_remove,
	.shutdown	= bq_fg_shutdown,

};

module_i2c_driver(bq_fg_driver);

MODULE_DESCRIPTION("TI BQ27Z561 Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Texas Instruments");
