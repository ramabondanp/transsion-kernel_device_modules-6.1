// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2020 MediaTek Inc.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/pinctrl/consumer.h>

//TODO
#define LDO0_I2C_ID 0
#define LDO1_I2C_ID 1
#define LDO3_I2C_ID 3
#define LDO4_I2C_ID 4
#define LDO6_I2C_ID 6
#define LDO7_I2C_ID 7
#define LDO8_I2C_ID 8
#define LDO9_I2C_ID 9
#define LDO10_I2C_ID 10
#define LDO11_I2C_ID 11
#define LDO13_I2C_ID 13
#define LDOMAX_I2C_ID (LDO13_I2C_ID)


enum dio8018_regulator_ids {
	DIO8018_LDO1,
	DIO8018_LDO2,
	DIO8018_LDO3,
	DIO8018_LDO4,
	DIO8018_LDO5,
	DIO8018_LDO6,
	DIO8018_LDO7,
};

enum dio8018_registers {
	DIO8018_PRODUCT_ID = 0x00,
	DIO8018_DEV_ID,
	DIO8018_ILIMIT,
	DIO8018_ENABLE,
	DIO8018_LDO1VOUT,
	DIO8018_LDO2VOUT,
	DIO8018_LDO3VOUT,
	DIO8018_LDO4VOUT,
	DIO8018_LDO5VOUT,
	DIO8018_LDO6VOUT,
	DIO8018_LDO7VOUT,
	DIO8018_SEQ1 = 0x0B,
	DIO8018_SEQ2,
	DIO8018_SEQ3,
	DIO8018_SEQ4,
	DIO8018_RDIS = 0x10,
	DIO8018_RESET,
	DIO8018_REG_MAX = DIO8018_RESET,
};

#define DIO8018_ID	0x04
static struct regmap *debug_regmap[LDOMAX_I2C_ID] = {};
//static int i2c_id = 0;

static int my_regulator_set_voltage_sel_regmap(struct regulator_dev *rdev, unsigned sel)
{
    int ret;
    pr_debug("wyj %s  %s sel=%d\n",__func__,  rdev->desc->name, sel);
    ret = regulator_set_voltage_sel_regmap(rdev, sel);
    return ret;
}
/*
static int my_regulator_get_voltage_sel_regmap(struct regulator_dev *rdev)
{
    int reg = rdev->desc->vsel_reg;
	int vol = 0;
    pr_info("sxw %s  %s  %d\n",__func__,  rdev->desc->name,rdev->desc->vsel_reg);
	switch(reg){
		case DIO8018_LDO1VOUT:
		case DIO8018_LDO2VOUT:
			vol = 1200000;
		break;
		case DIO8018_LDO3VOUT:
		case DIO8018_LDO4VOUT:
		case DIO8018_LDO5VOUT:
		case DIO8018_LDO6VOUT:
		case DIO8018_LDO7VOUT:
			vol = 2804000;
		break;
		default:
		break;
	}
	pr_info("sxw vol %d",vol);
    return vol;
}*/
/*
static int my_regulator_enable_regmap(struct regulator_dev *rdev)
{
    int ret;
    regmap_write(debug_regmap[i2c_id],DIO8018_RDIS,0x00);
    ret = regulator_enable_regmap(rdev);
    return ret;
}

static int my_regulator_disable_regmap(struct regulator_dev *rdev)
{
    int ret;
    regmap_write(debug_regmap[i2c_id],DIO8018_RDIS,0xff);
    ret = regulator_disable_regmap(rdev);
    return ret;
}*/

static const struct regulator_ops dio8018_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.set_voltage_sel = my_regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};


#define DIO8018_LDOD(_i2cid,_num, _supply)				\
	[DIO8018_LDO ## _num] = {					\
		.name =		   "DIO8018LDO"#_num#_i2cid,				\
		.of_match =	   of_match_ptr("DIO8018LDO"#_num#_i2cid),		\
		.regulators_node = of_match_ptr("regulators"),		\
		.type =		   REGULATOR_VOLTAGE,			\
		.owner =	   THIS_MODULE,				\
		.linear_ranges =   (struct linear_range[]) {		\
		      REGULATOR_LINEAR_RANGE(800000, 0x63, 0xff, 8000),	\
		},							\
		.n_linear_ranges = 1,					\
		.n_voltages =	   0xff,				\
		.vsel_reg =	   DIO8018_LDO ## _num ## VOUT,	\
		.vsel_mask =	   0xff,				\
		.enable_reg =	   DIO8018_ENABLE,			\
		.enable_mask =	   BIT(_num - 1),			\
		.enable_time =	   150,					\
		.supply_name =	   _supply,				\
		.ops =		   &dio8018_ops,			\
	}

#define DIO8018_LDOA(_i2cid, _num, _supply)				\
	[DIO8018_LDO ## _num] = {					\
		.name =		   "DIO8018LDO"#_num#_i2cid,				\
		.of_match =	   of_match_ptr("DIO8018LDO"#_num#_i2cid),		\
		.regulators_node = of_match_ptr("regulators"),		\
		.type =		   REGULATOR_VOLTAGE,			\
		.owner =	   THIS_MODULE,				\
		.linear_ranges =   (struct linear_range[]) {		\
		      REGULATOR_LINEAR_RANGE(1500000, 0x10, 0xff, 8000),	\
		},							\
		.n_linear_ranges = 1,					\
		.n_voltages =	   0xff,				\
		.vsel_reg =	   DIO8018_LDO ## _num ## VOUT,	\
		.vsel_mask =	   0xff,				\
		.enable_reg =	   DIO8018_ENABLE,			\
		.enable_mask =	   BIT(_num - 1),			\
		.enable_time =	   150,					\
		.supply_name =	   _supply,				\
		.ops =		   &dio8018_ops,			\
	}

static const struct regulator_desc dio8018_regulators0[] = {
	DIO8018_LDOD(0, 1, "VIND"),
	DIO8018_LDOD(0, 2, "VIND"),
	DIO8018_LDOA(0, 3, "VINA"),
	DIO8018_LDOA(0, 4, "VINA"),
	DIO8018_LDOA(0, 5, "VINA"),
	DIO8018_LDOA(0, 6, "VINA"),
	DIO8018_LDOA(0, 7, "VINA"),
};

static const struct regulator_desc dio8018_regulators1[] = {
	DIO8018_LDOD(1, 1, "VIND"),
	DIO8018_LDOD(1, 2, "VIND"),
	DIO8018_LDOA(1, 3, "VINA"),
	DIO8018_LDOA(1, 4, "VINA"),
	DIO8018_LDOA(1, 5, "VINA"),
	DIO8018_LDOA(1, 6, "VINA"),
	DIO8018_LDOA(1, 7, "VINA"),
};

static const struct regulator_desc dio8018_regulators3[] = {
	DIO8018_LDOD(3, 1, "VIND"),
	DIO8018_LDOD(3, 2, "VIND"),
	DIO8018_LDOA(3, 3, "VINA"),
	DIO8018_LDOA(3, 4, "VINA"),
	DIO8018_LDOA(3, 5, "VINA"),
	DIO8018_LDOA(3, 6, "VINA"),
	DIO8018_LDOA(3, 7, "VINA"),
};

static const struct regulator_desc dio8018_regulators4[] = {
	DIO8018_LDOD(4, 1, "VIND"),
	DIO8018_LDOD(4, 2, "VIND"),
	DIO8018_LDOA(4, 3, "VINA"),
	DIO8018_LDOA(4, 4, "VINA"),
	DIO8018_LDOA(4, 5, "VINA"),
	DIO8018_LDOA(4, 6, "VINA"),
	DIO8018_LDOA(4, 7, "VINA"),
};

static const struct regulator_desc dio8018_regulators6[] = {
	DIO8018_LDOD(6, 1, "VIND"),
	DIO8018_LDOD(6, 2, "VIND"),
	DIO8018_LDOA(6, 3, "VINA"),
	DIO8018_LDOA(6, 4, "VINA"),
	DIO8018_LDOA(6, 5, "VINA"),
	DIO8018_LDOA(6, 6, "VINA"),
	DIO8018_LDOA(6, 7, "VINA"),
};

static const struct regulator_desc dio8018_regulators7[] = {
	DIO8018_LDOD(7, 1, "VIND"),
	DIO8018_LDOD(7, 2, "VIND"),
	DIO8018_LDOA(7, 3, "VINA"),
	DIO8018_LDOA(7, 4, "VINA"),
	DIO8018_LDOA(7, 5, "VINA"),
	DIO8018_LDOA(7, 6, "VINA"),
	DIO8018_LDOA(7, 7, "VINA"),
};

static const struct regulator_desc dio8018_regulators8[] = {
	DIO8018_LDOD(8, 1, "VIND"),
	DIO8018_LDOD(8, 2, "VIND"),
	DIO8018_LDOA(8, 3, "VINA"),
	DIO8018_LDOA(8, 4, "VINA"),
	DIO8018_LDOA(8, 5, "VINA"),
	DIO8018_LDOA(8, 6, "VINA"),
	DIO8018_LDOA(8, 7, "VINA"),
};
static const struct regulator_desc dio8018_regulators9[] = {
	DIO8018_LDOD(9, 1, "VIND"),
	DIO8018_LDOD(9, 2, "VIND"),
	DIO8018_LDOA(9, 3, "VINA"),
	DIO8018_LDOA(9, 4, "VINA"),
	DIO8018_LDOA(9, 5, "VINA"),
	DIO8018_LDOA(9, 6, "VINA"),
	DIO8018_LDOA(9, 7, "VINA"),
};
static const struct regulator_desc dio8018_regulators10[] = {
	DIO8018_LDOD(10, 1, "VIND"),
	DIO8018_LDOD(10, 2, "VIND"),
	DIO8018_LDOA(10, 3, "VINA"),
	DIO8018_LDOA(10, 4, "VINA"),
	DIO8018_LDOA(10, 5, "VINA"),
	DIO8018_LDOA(10, 6, "VINA"),
	DIO8018_LDOA(10, 7, "VINA"),
};
static const struct regulator_desc dio8018_regulators11[] = {
	DIO8018_LDOD(11, 1, "VIND"),
	DIO8018_LDOD(11, 2, "VIND"),
	DIO8018_LDOA(11, 3, "VINA"),
	DIO8018_LDOA(11, 4, "VINA"),
	DIO8018_LDOA(11, 5, "VINA"),
	DIO8018_LDOA(11, 6, "VINA"),
	DIO8018_LDOA(11, 7, "VINA"),
};
static const struct regulator_desc dio8018_regulators13[] = {
	DIO8018_LDOD(13, 1, "VIND"),
	DIO8018_LDOD(13, 2, "VIND"),
	DIO8018_LDOA(13, 3, "VINA"),
	DIO8018_LDOA(13, 4, "VINA"),
	DIO8018_LDOA(13, 5, "VINA"),
	DIO8018_LDOA(13, 6, "VINA"),
	DIO8018_LDOA(13, 7, "VINA"),
};

static const struct regmap_config dio8018_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = DIO8018_REG_MAX,
};


static int dio8018_bus_num = -1;

static int shex_to_int(const char *hex_buf, int size)
{
    int i;
    int base = 1;
    int value = 0;
    char single;

    for (i = size - 1; i >= 0; i--) {
        single = hex_buf[i];

        if ((single >= '0') && (single <= '9')) {
            value += (single - '0') * base;
        } else if ((single >= 'a') && (single <= 'z')) {
            value += (single - 'a' + 10) * base;
        } else if ((single >= 'A') && (single <= 'Z')) {
            value += (single - 'A' + 10) * base;
        } else {
            return -EINVAL;
        }

        base *= 16;
    }

    return value;
}

static ssize_t dio8018_reg_show(struct device *dev,struct device_attribute *attr,char *buf)
{
	unsigned int val;
	int count = 0;

	if(dio8018_bus_num > LDOMAX_I2C_ID || dio8018_bus_num < 0 || debug_regmap[dio8018_bus_num] == NULL){
		return sprintf(buf,"NULL");
	}

	regmap_read(debug_regmap[dio8018_bus_num], 0x00, &val);
	count = sprintf(buf,"reg:0x00 val:0x%x\n", val);
	val = 0;

	regmap_read(debug_regmap[dio8018_bus_num], 0x01, &val);
	count += sprintf(buf+count,"reg:0x01 val:0x%x\n", val);
	val = 0;

	regmap_read(debug_regmap[dio8018_bus_num], 0x02, &val);
	count += sprintf(buf+count,"reg:0x02 val:0x%x\n", val);
	val = 0;

	regmap_read(debug_regmap[dio8018_bus_num], 0x03, &val);
	count += sprintf(buf+count,"reg:0x03 val:0x%x\n", val);
	val = 0;

	regmap_read(debug_regmap[dio8018_bus_num], 0x04, &val);
	count += sprintf(buf+count,"reg:0x04 val:0x%x\n", val);
	val = 0;

	regmap_read(debug_regmap[dio8018_bus_num], 0x05, &val);
	count += sprintf(buf+count,"reg:0x05 val:0x%x\n", val);
	val = 0;

	regmap_read(debug_regmap[dio8018_bus_num], 0x06, &val);
	count += sprintf(buf+count,"reg:0x06 val:0x%x\n", val);
	val = 0;

	regmap_read(debug_regmap[dio8018_bus_num], 0x0a, &val);
	count += sprintf(buf+count,"reg:0x0a val:0x%x\n", val);
	val = 0;

	regmap_read(debug_regmap[dio8018_bus_num], 0x0b, &val);
	count += sprintf(buf+count,"reg:0x0b val:0x%x\n", val);
	val = 0;

	regmap_read(debug_regmap[dio8018_bus_num], 0x0e, &val);
	count += sprintf(buf+count,"reg:0x0e val:0x%x\n", val);
	val = 0;

	regmap_read(debug_regmap[dio8018_bus_num], 0x0f, &val);
	count += sprintf(buf+count,"reg:0x0f val:0x%x\n", val);
	val = 0;

    return count;
}

/*
* write i2c1 dio8018 reg
* echo 01 0e 00 > dio8018_reg
*
* i2c1 dio8018 reg dump
* echo 01 > dio8018_reg
* cat dio8018_reg
*/
static ssize_t dio8018_reg_store(struct device *dev,struct device_attribute *attr,const char *buf,size_t count)
{
	int dio8018_nums = 0;
	unsigned int reg;
	unsigned int mask = 0xff;
	unsigned val;
	int ret = 0;
	int len = count-1;

	if(len == 2){
		dio8018_bus_num = shex_to_int(buf,2);
		return count;
	}

	if(len < 8){
		printk("[dio8018]: len %d < 8 error!", len);
		return count;
	}

	/* get bus id */
	dio8018_nums = shex_to_int(buf,2);
	if(dio8018_nums > LDOMAX_I2C_ID || debug_regmap[dio8018_nums] == NULL){
		printk("[dio8018]: bus %d not register",dio8018_nums);
		return count;
	}
	/* get reg */
	reg = shex_to_int(buf+3,2);
	/* get val */
	val	= shex_to_int(buf+6,2);
	printk("[dio8018]: bus=0x%x reg=0x%x val=0x%x",dio8018_nums, reg, val);

	if(reg > 0x0F){
		return count;
	}

	ret = regmap_update_bits(debug_regmap[dio8018_nums], reg, mask, val);
	if(ret){
		return count;
	}
    return count;
}
static DEVICE_ATTR(dio8018_reg, 0644, dio8018_reg_show, dio8018_reg_store);

static struct attribute *dio8018_info_attrs[] = {
    &dev_attr_dio8018_reg.attr,
    NULL
};

static const struct attribute_group dio8018_group = {
    .attrs = dio8018_info_attrs,
};

static int dio8018_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct regmap *regmap;
    const struct regulator_desc *preg_desc;
	int i, ret, i2cid;
	unsigned int data;

	regmap = devm_regmap_init_i2c(i2c, &dio8018_regmap);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&i2c->dev, "Failed to create regmap: %d\n", ret);
		return ret;
	}

	ret = regmap_read(regmap, DIO8018_PRODUCT_ID, &data);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read PRODUCT_ID: %d\n", ret);
		return ret;
	}
	if (data != DIO8018_ID) {
		dev_err(&i2c->dev, "Unsupported device id: 0x%x.\n", data);
		//return -ENODEV;
	}

	dev_info(&i2c->dev, "regulator ic device id: 0x%x.\n", data);

    i2cid = i2c_adapter_id(i2c->adapter);

	switch (i2cid) {
	case LDO0_I2C_ID:
		preg_desc = dio8018_regulators0;
		break;
	case LDO1_I2C_ID:
		preg_desc = dio8018_regulators1;
		break;
	case LDO3_I2C_ID:
		preg_desc = dio8018_regulators3;
		break;
	case LDO4_I2C_ID:
		preg_desc = dio8018_regulators4;
		break;
	case LDO6_I2C_ID:
		preg_desc = dio8018_regulators6;
		break;
	case LDO7_I2C_ID:
		preg_desc = dio8018_regulators7;
		break;
	case LDO8_I2C_ID:
		preg_desc = dio8018_regulators8;
		break;
	case LDO9_I2C_ID:
		preg_desc = dio8018_regulators9;
		break;
	case LDO10_I2C_ID:
		preg_desc = dio8018_regulators10;
		break;
	case LDO11_I2C_ID:
		preg_desc = dio8018_regulators11;
		break;
	case LDO13_I2C_ID:
		preg_desc = dio8018_regulators13;
		break;
	default:
		dev_err(&i2c->dev, "UnSupported I2C-[%d]\n",  i2cid);
		return -1;
		break;
	}

	config.dev = &i2c->dev;
	config.init_data = NULL;

	for (i = 0; i < ARRAY_SIZE(dio8018_regulators1); i++) {
		rdev = devm_regulator_register(&i2c->dev,
					       &preg_desc[i],
					       &config);
		if (IS_ERR(rdev)) {
			ret = PTR_ERR(rdev);
			dev_err(&i2c->dev, "Failed to register %s: %d\n",
				preg_desc[i].name, ret);
			return ret;
		}
	}
	
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id dio8018_dt_ids[] = {
	{ .compatible = "tran,dio8018", },
	{}
};
MODULE_DEVICE_TABLE(of, dio8018_dt_ids);
#endif

static const struct i2c_device_id dio8018_i2c_id[] = {
	{ "dio8018", },
	{}
};
MODULE_DEVICE_TABLE(i2c, dio8018_i2c_id);

static struct i2c_driver dio8018_regulator_driver = {
	.driver = {
		.name = "dio8018",
		.of_match_table = of_match_ptr(dio8018_dt_ids),
	},
	.probe = dio8018_i2c_probe,
	.id_table = dio8018_i2c_id,
};
module_i2c_driver(dio8018_regulator_driver);

MODULE_DESCRIPTION("DIO8018 PMIC voltage regulator driver");
MODULE_LICENSE("GPL");

