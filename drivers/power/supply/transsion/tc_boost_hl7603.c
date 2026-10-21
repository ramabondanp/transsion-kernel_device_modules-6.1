// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/phy/phy.h>
#include <linux/power_supply.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include "tc_boost_hl7603.h"
/**********************************************************
  *
  *   [I2C Function For Read/Write hl7603]
  *
  *********************************************************/
static inline u8 hl7603_val_toreg(u32 min, u32 max, u32 step, u32 target)
{
	if (target <= min)
		return 0;
	if (target >= max)
		return (max - min) / step;
	return (target - min) / step;
}
static u8 hl7603_vout_sel_toreg(u32 mV)
{
	return hl7603_val_toreg(2850, 5500, 50, mV);
}
unsigned int hl7603_write_byte(struct hl7603_device_info *info, uint8_t addr, unsigned char writeData)
{
	unsigned char xfers = 1;
	int ret, retries = 1;
	unsigned char buf[8];
	mutex_lock(&info->hl7603_i2c_access);
	buf[0] = addr;
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
			pr_info("skipping non-existent adapter %s\n",
				info->client->adapter->name);
			break;
		}
	} while (ret != xfers && --retries);
	mutex_unlock(&info->hl7603_i2c_access);
	return ret == xfers ? 1 : -1;
}
unsigned int hl7603_read_byte(struct hl7603_device_info *info, unsigned char addr, unsigned char *returnData)
{
	unsigned char xfers = 2;
	int ret, retries = 3;
	mutex_lock(&info->hl7603_i2c_access);
	do {
		struct i2c_msg msgs[2] = {
			{
				.addr = info->client->addr,
				.flags = 0,
				.len = 1,
				.buf = &addr,
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
			pr_info("skipping non-existent adapter %s\n",
				info->client->adapter->name);
			break;
		}
		if (ret != xfers)
			mdelay(10);
	} while (ret != xfers && --retries);
	mutex_unlock(&info->hl7603_i2c_access);
	return ret == xfers ? 0 : -1;
}
/**********************************************************
  *
  *   [Read / Write Function]
  *
  *********************************************************/
unsigned int hl7603_read_interface(struct hl7603_device_info *chip, uint8_t RegNum,
				uint8_t *val, uint8_t MASK, uint8_t SHIFT)
{
	uint8_t hl7603_reg = 0;
	unsigned int ret = 0;
	ret = hl7603_read_byte(chip, RegNum, &hl7603_reg);
	pr_info("[hl7603_read_interface] Reg[%x]=0x%x\n", RegNum, hl7603_reg); 
	hl7603_reg &= (MASK << SHIFT);
	*val = (hl7603_reg >> SHIFT);
	pr_info("[hl7603_read_interface] val=0x%x\n", *val);  
	return ret;
}
unsigned int hl7603_config_interface(struct hl7603_device_info *chip, uint8_t RegNum,
				uint8_t val, uint8_t MASK, uint8_t SHIFT)
{
	uint8_t hl7603_reg = 0;
	unsigned int ret = 0;
	ret = hl7603_read_byte(chip, RegNum, &hl7603_reg);
	
	hl7603_reg &= ~(MASK << SHIFT);
	hl7603_reg |= (val << SHIFT);
	ret = hl7603_write_byte(chip, RegNum, hl7603_reg);
	return ret;
}
unsigned int hl7603_get_reg_value(struct hl7603_device_info *chip, unsigned int reg)
{
	unsigned int ret = 0;
	uint8_t reg_val = 0;
	ret = hl7603_read_interface(chip, (uint8_t) reg, &reg_val, 0xFF, 0x0);
	if (ret == 0)
		pr_info("ret=%d\n", ret);
	return reg_val;
}
int hl7603_enable(struct hl7603_device_info *chip, unsigned char en)
{
	int ret = 0;
	ret = hl7603_config_interface(chip, HL7603_CONFIG1, en, HL7603_DEV_EN_MASK, HL7603_DEV_EN_SHIFT);
	pr_info("[%s] en =%d, ret =%d \n", __func__,  en, ret);
	return ret;
}
int hl7603_set_mode(struct hl7603_device_info *chip, uint8_t mode)
{
	int ret = 0;
	if (mode != 0 && mode != 2 ) {
		pr_info("[%s] error mode = %d only 0 or 3\n", __func__, mode);
		return -1;
	}
	ret = hl7603_config_interface(chip, HL7603_CONFIG1, (unsigned char)mode, HL7603_MODE_CFG_MASK,HL7603_MODE_CFG_SHIFT);
	return ret;
}
int hl7603_reg_init(struct hl7603_device_info *chip)
{
	int ret = 0;
	int vout_value = 0;
	
	vout_value = hl7603_vout_sel_toreg(chip->voltage_value);
	ret = hl7603_config_interface(chip, HL7603_VOUT_VSEL,(unsigned char)vout_value, HL7603_VOUT_REG_MASK, HL7603_VOUT_REG_SHIFT);
	//if (ret == 0)
	//	return -1;
	
	ret = hl7603_set_mode(chip, 2);
	//if (ret == 0)
	//	return -1;
	
	ret = hl7603_enable(chip, 1);
	//if (ret == 0)
	//	return -1;
	pr_info("%s_hw_init,chip_id=%d\n", chip->name,chip->chip_id);
	
	return 0;
}
static int hl7603_reg_reset(void *dev_data)
{
	int ret;
	u8 reg;
	struct hl7603_device_info *di = dev_data;
	hl7603_config_interface(di, HL7603_CONFIG1, HL7603_RESET_MASK,HL7603_RESET_SHIFT, 0x01);
	msleep(10);//10ms
	ret = hl7603_read_byte(di, HL7603_CONFIG1, &reg);
	if (ret < 0)
		return -EPERM;
	pr_info("reg_reset [%x]=0x%x\n", HL7603_CONFIG1, reg);
	return 0;
}
int hl7603_parse_dts(struct hl7603_device_info *chip)
{
	struct device_node *np = chip->dev->of_node;
	if (!np)
		return -ENODEV;
	if (of_property_read_u32(np, "halo,hl7603,hl7603_vout_voltage", &chip->voltage_value) < 0) {
		chip->voltage_value = 3450;
		dev_err(chip->dev, "failed to read bat-ovp-threshold\n");
	}
	return 0;
}
int hl7603a_parse_dts(struct hl7603_device_info *chip)
{
	struct device_node *np = chip->dev->of_node;
	
	if (!np)
		return -ENODEV;
	if (of_property_read_u32(np, "halo,hl7603a,hl7603a_vout_voltage", &chip->voltage_value) < 0) {
		chip->voltage_value = 3450;
		dev_err(chip->dev, "failed to read bat-ovp-threshold\n");
	
	}
	return 0;
}
static int hl7603_driver_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
    int ret;
	uint8_t hl7603x_chip_id_val = 0;
	struct hl7603_device_info *di = NULL;
	
	pr_info("%s start\n", __func__);

	if (!client || !client->dev.of_node || !id)
		return -ENODEV;
	di = devm_kzalloc(&client->dev, sizeof(struct hl7603_device_info), GFP_KERNEL);
	if (!di) {
		pr_info("%s alloc hl7603_chip failed!\n", __func__);
		return -ENOMEM;
	}

	di->dev = &client->dev;
	di->client = client;
	i2c_set_clientdata(client, di);

	ret = hl7603_read_byte(di, HL7603_CONFIG0, &hl7603x_chip_id_val);
	pr_info("%s ret=%d\n", __func__,ret);
	if (ret < 0)
		goto hl7603_fail_2;
	
	pr_info("%s hl7603x_chip_id_val=%d\n", __func__,hl7603x_chip_id_val);
	if(hl7603x_chip_id_val == HL7603_CHIP_ID){
		di->chip_id = HL7603_CHIP_ID;
		hl7603_parse_dts(di);
	}
	else if((hl7603x_chip_id_val == HL7603A_CHIP_ID) || (hl7603x_chip_id_val == HL7603A_BACK_CHIP_ID)){
		di->chip_id = HL7603A_CHIP_ID;
		hl7603a_parse_dts(di);
	}
	else {
		di->chip_id = HL7603_CHIP_ID;
		hl7603_parse_dts(di);
	}
		
	ret = hl7603_reg_reset(di);
	if (ret)
		goto hl7603_fail_2;
	
	ret = hl7603_reg_init(di);
	if (ret)
		goto hl7603_fail_2;
	i2c_set_clientdata(client, di);
	return 0;
hl7603_fail_2:
	devm_kfree(&client->dev, di);
	pr_info("%s probe failed \n",__func__);
	return ret;
}
static void hl7603_remove(struct i2c_client *client)
{
	struct hl7603_device_info *di = i2c_get_clientdata(client);
	if (!di)
		return;
	hl7603_reg_reset(di);
	return;
}
static void hl7603_shutdown(struct i2c_client *client)
{
	struct hl7603_device_info *di = i2c_get_clientdata(client);
	if (!di)
		return;
	hl7603_reg_reset(di);
}
static const struct of_device_id hl7603_of_match[] = {
	{
		.compatible = "mediatek,hl7603x",
		.data = NULL,
	},
	{},
};
static const struct i2c_device_id hl7603_i2c_id[] = {
	{ "hl7603x", 0 }, {}
};
static struct i2c_driver hl7603_driver = {
	.probe = hl7603_driver_probe,
	.remove = hl7603_remove,
	.shutdown = hl7603_shutdown,
	.id_table = hl7603_i2c_id,
	.driver = {
		.owner = THIS_MODULE,
		.name = "hl7603x",
		.of_match_table = of_match_ptr(hl7603_of_match),
	},
};
static int __init hl7603_init(void)
{
	return i2c_add_driver(&hl7603_driver);
}
static void __exit hl7603_exit(void)
{
	i2c_del_driver(&hl7603_driver);
}
module_init(hl7603_init);
module_exit(hl7603_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("hl7603x module driver");
MODULE_AUTHOR("Halo Technologies Co., Ltd.");
