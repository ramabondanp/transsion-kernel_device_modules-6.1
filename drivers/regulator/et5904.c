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

enum et5904_regulator_ids {
	ET5904_LDO1,
	ET5904_LDO2,
	ET5904_LDO3,
	ET5904_LDO4,
};

enum et5904_registers {
	ET5904_PRODUCT_ID = 0x00,
	ET5904_ILIMIT,
	ET5904_RDIS,
	ET5904_LDO1VOUT,
	ET5904_LDO2VOUT,
	ET5904_LDO3VOUT,
	ET5904_LDO4VOUT,
	ET5904_SEQ1 = 0x0A,
	ET5904_SEQ2,
	ET5904_ENABLE = 0x0E,
};

#define ET5904_ID	0x00

static struct regmap *debug_regmap[LDOMAX_I2C_ID] = {};
static int i2c_id = 0;

static int my_regulator_set_voltage_sel_regmap(struct regulator_dev *rdev, unsigned sel)
{
    int ret;
    pr_debug("wyj %s  %s sel=%d\n",__func__,  rdev->desc->name, sel);
    ret = regulator_set_voltage_sel_regmap(rdev, sel);
    return ret;
}

static int my_regulator_enable_regmap(struct regulator_dev *rdev)
{
    int ret;

    regmap_write(rdev->regmap, ET5904_RDIS, 0x0);
    ret = regulator_enable_regmap(rdev);
    return ret;
}

static int my_regulator_disable_regmap(struct regulator_dev *rdev)
{
    int ret;

    regmap_write(rdev->regmap, ET5904_RDIS, 0xf);
    ret = regulator_disable_regmap(rdev);
    return ret;
}

static const struct regulator_ops et5904_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.set_voltage_sel = my_regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = my_regulator_enable_regmap,
	.disable = my_regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};


#define ET5904_LDOD(_i2cid,_num, _supply)				\
	[ET5904_LDO ## _num] = {					\
		.name =		   "ET5904LDO"#_num#_i2cid,				\
		.of_match =	   of_match_ptr("ET5904LDO"#_num#_i2cid),		\
		.regulators_node = of_match_ptr("regulators"),		\
		.type =		   REGULATOR_VOLTAGE,			\
		.owner =	   THIS_MODULE,				\
		.linear_ranges =   (struct linear_range[]) {		\
		      REGULATOR_LINEAR_RANGE(600000, 0x0, 0xff, 6000),	\
		},							\
		.n_linear_ranges = 1,					\
		.n_voltages =	   0xff,				\
		.vsel_reg =	   ET5904_LDO ## _num ## VOUT,	\
		.vsel_mask =	   0xff,				\
		.enable_reg =	   ET5904_ENABLE,			\
		.enable_mask =	   BIT(_num - 1),			\
		.enable_time =	   150,					\
		.supply_name =	   _supply,				\
		.ops =		   &et5904_ops,			\
	}

#define ET5904_LDOA(_i2cid, _num, _supply)				\
	[ET5904_LDO ## _num] = {					\
		.name =		   "ET5904LDO"#_num#_i2cid,				\
		.of_match =	   of_match_ptr("ET5904LDO"#_num#_i2cid),		\
		.regulators_node = of_match_ptr("regulators"),		\
		.type =		   REGULATOR_VOLTAGE,			\
		.owner =	   THIS_MODULE,				\
		.linear_ranges =   (struct linear_range[]) {		\
		      REGULATOR_LINEAR_RANGE(1200000, 0x0, 0xff, 12500),	\
		},							\
		.n_linear_ranges = 1,					\
		.n_voltages =	   0xff,				\
		.vsel_reg =	   ET5904_LDO ## _num ## VOUT,	\
		.vsel_mask =	   0xff,				\
		.enable_reg =	   ET5904_ENABLE,			\
		.enable_mask =	   BIT(_num - 1),			\
		.enable_time =	   150,					\
		.supply_name =	   _supply,				\
		.ops =		   &et5904_ops,			\
	}

static const struct regulator_desc et5904_regulators0[] = {
	ET5904_LDOD(0, 1, "VIND"),
	ET5904_LDOD(0, 2, "VIND"),
	ET5904_LDOA(0, 3, "VINA"),
	ET5904_LDOA(0, 4, "VINA"),
};

static const struct regulator_desc et5904_regulators1[] = {
	ET5904_LDOD(1, 1, "VIND"),
	ET5904_LDOD(1, 2, "VIND"),
	ET5904_LDOA(1, 3, "VINA"),
	ET5904_LDOA(1, 4, "VINA"),
};

static const struct regulator_desc et5904_regulators3[] = {
	ET5904_LDOD(3, 1, "VIND"),
	ET5904_LDOD(3, 2, "VIND"),
	ET5904_LDOA(3, 3, "VINA"),
	ET5904_LDOA(3, 4, "VINA"),
};

static const struct regulator_desc et5904_regulators4[] = {
	ET5904_LDOD(4, 1, "VIND"),
	ET5904_LDOD(4, 2, "VIND"),
	ET5904_LDOA(4, 3, "VINA"),
	ET5904_LDOA(4, 4, "VINA"),
};

static const struct regulator_desc et5904_regulators6[] = {
	ET5904_LDOD(6, 1, "VIND"),
	ET5904_LDOD(6, 2, "VIND"),
	ET5904_LDOA(6, 3, "VINA"),
	ET5904_LDOA(6, 4, "VINA"),
};

static const struct regulator_desc et5904_regulators7[] = {
	ET5904_LDOD(7, 1, "VIND"),
	ET5904_LDOD(7, 2, "VIND"),
	ET5904_LDOA(7, 3, "VINA"),
	ET5904_LDOA(7, 4, "VINA"),
};

static const struct regulator_desc et5904_regulators8[] = {
	ET5904_LDOD(8, 1, "VIND"),
	ET5904_LDOD(8, 2, "VIND"),
	ET5904_LDOA(8, 3, "VINA"),
	ET5904_LDOA(8, 4, "VINA"),
};
static const struct regulator_desc et5904_regulators9[] = {
	ET5904_LDOD(9, 1, "VIND"),
	ET5904_LDOD(9, 2, "VIND"),
	ET5904_LDOA(9, 3, "VINA"),
	ET5904_LDOA(9, 4, "VINA"),
};
static const struct regulator_desc et5904_regulators10[] = {
	ET5904_LDOD(10, 1, "VIND"),
	ET5904_LDOD(10, 2, "VIND"),
	ET5904_LDOA(10, 3, "VINA"),
	ET5904_LDOA(10, 4, "VINA"),
};
static const struct regulator_desc et5904_regulators11[] = {
	ET5904_LDOD(11, 1, "VIND"),
	ET5904_LDOD(11, 2, "VIND"),
	ET5904_LDOA(11, 3, "VINA"),
	ET5904_LDOA(11, 4, "VINA"),
};
static const struct regulator_desc et5904_regulators13[] = {
	ET5904_LDOD(13, 1, "VIND"),
	ET5904_LDOD(13, 2, "VIND"),
	ET5904_LDOA(13, 3, "VINA"),
	ET5904_LDOA(13, 4, "VINA"),
};

static const struct regmap_config et5904_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = ET5904_ENABLE,
};

static struct pinctrl *pinctrl;
struct pinctrl_state *pinctrl_i2c_data_gpio;
struct pinctrl_state *pinctrl_i2c_clk_gpio;
static void tran_i2c_driver_ability_init(struct device *dev)
{
	int ret = 0;
	pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(pinctrl)) {
		pr_err("Cannot find ftm pinctrl!\n");
		return;
	}
	pinctrl_i2c_clk_gpio = pinctrl_lookup_state(pinctrl, "i2c_ldo_sclk_mode");
	if (IS_ERR(pinctrl_i2c_clk_gpio)) {
		ret = PTR_ERR(pinctrl_i2c_clk_gpio);
		pr_err("Cannot find pinctrl pinctrl_i2c_clk_gpio\n");
		//return;
	} else {
		pinctrl_select_state(pinctrl, pinctrl_i2c_clk_gpio);
	}
	pinctrl_i2c_data_gpio = pinctrl_lookup_state(pinctrl, "i2c_ldo_sda_mode");
	if (IS_ERR(pinctrl_i2c_data_gpio)) {
		ret = PTR_ERR(pinctrl_i2c_data_gpio);
		pr_err("Cannot find pinctrl pinctrl_i2c_data_gpio\n");
		//return;
	} else {
		pinctrl_select_state(pinctrl, pinctrl_i2c_data_gpio);
	}
}

#if IS_ENABLED(CONFIG_TRAN_DUAL_INTERNAL_TEMP_PATCH)
#define MAINLCM_LDO_ID	LDO6_I2C_ID

extern void regulator_init_complete_mainlcm(void);
extern void regulator_init_complete_sublcm(void);
#if IS_ENABLED(TRAN_AE10)
#define LCM_SUB_LDO_ID	LDO1_I2C_ID
extern void regulator1_init_complete_mainlcm(void);
extern void regulator1_init_complete_sublcm(void);
#endif
#endif


static int et5904_bus_num = -1;

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

static ssize_t et5904_reg_show(struct device *dev,struct device_attribute *attr,char *buf)
{
	unsigned int val;
	int count = 0;

	if(et5904_bus_num > LDOMAX_I2C_ID || et5904_bus_num < 0 || debug_regmap[et5904_bus_num] == NULL){
		return sprintf(buf,"NULL");
	}

	regmap_read(debug_regmap[et5904_bus_num], 0x00, &val);
	count = sprintf(buf,"reg:0x00 val:0x%x\n", val);
	val = 0;

	regmap_read(debug_regmap[et5904_bus_num], 0x01, &val);
	count += sprintf(buf+count,"reg:0x01 val:0x%x\n", val);
	val = 0;

	regmap_read(debug_regmap[et5904_bus_num], 0x02, &val);
	count += sprintf(buf+count,"reg:0x02 val:0x%x\n", val);
	val = 0;

	regmap_read(debug_regmap[et5904_bus_num], 0x03, &val);
	count += sprintf(buf+count,"reg:0x03 val:0x%x\n", val);
	val = 0;

	regmap_read(debug_regmap[et5904_bus_num], 0x04, &val);
	count += sprintf(buf+count,"reg:0x04 val:0x%x\n", val);
	val = 0;

	regmap_read(debug_regmap[et5904_bus_num], 0x05, &val);
	count += sprintf(buf+count,"reg:0x05 val:0x%x\n", val);
	val = 0;

	regmap_read(debug_regmap[et5904_bus_num], 0x06, &val);
	count += sprintf(buf+count,"reg:0x06 val:0x%x\n", val);
	val = 0;

	regmap_read(debug_regmap[et5904_bus_num], 0x0a, &val);
	count += sprintf(buf+count,"reg:0x0a val:0x%x\n", val);
	val = 0;

	regmap_read(debug_regmap[et5904_bus_num], 0x0b, &val);
	count += sprintf(buf+count,"reg:0x0b val:0x%x\n", val);
	val = 0;

	regmap_read(debug_regmap[et5904_bus_num], 0x0e, &val);
	count += sprintf(buf+count,"reg:0x0e val:0x%x\n", val);
	val = 0;

	regmap_read(debug_regmap[et5904_bus_num], 0x0f, &val);
	count += sprintf(buf+count,"reg:0x0f val:0x%x\n", val);
	val = 0;

    return count;
}

/*
* write i2c1 et5904 reg
* echo 01 0e 00 > et5904_reg
*
* i2c1 et5904 reg dump
* echo 01 > et5904_reg
* cat et5904_reg
*/
static ssize_t et5904_reg_store(struct device *dev,struct device_attribute *attr,const char *buf,size_t count)
{
	int et5904_nums = 0;
	unsigned int reg;
	unsigned int mask = 0xff;
	unsigned val;
	int ret = 0;
	int len = count-1;

	if(len == 2){
		et5904_bus_num = shex_to_int(buf,2);
		return count;
	}

	if(len < 8){
		printk("[et5904]: len %d < 8 error!", len);
		return count;
	}

	/* get bus id */
	et5904_nums = shex_to_int(buf,2);
	if(et5904_nums > LDOMAX_I2C_ID || debug_regmap[et5904_nums] == NULL){
		printk("[et5904]: bus %d not register",et5904_nums);
		return count;
	}
	/* get reg */
	reg = shex_to_int(buf+3,2);
	/* get val */
	val	= shex_to_int(buf+6,2);
	printk("[et5904]: bus=0x%x reg=0x%x val=0x%x",et5904_nums, reg, val);

	if(reg > 0x0F){
		return count;
	}

	ret = regmap_update_bits(debug_regmap[et5904_nums], reg, mask, val);
	if(ret){
		return count;
	}
    return count;
}
static DEVICE_ATTR(et5904_reg, 0644, et5904_reg_show, et5904_reg_store);

static struct attribute *et5904_info_attrs[] = {
    &dev_attr_et5904_reg.attr,
    NULL
};

static const struct attribute_group et5904_group = {
    .attrs = et5904_info_attrs,
};

static int et5904_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct regmap *regmap;
    const struct regulator_desc *preg_desc;
	int i, ret, i2cid;
	unsigned int data;
	int status;

	struct regulator *regulator_en;

	tran_i2c_driver_ability_init(&i2c->dev);

	regulator_en = regulator_get(&i2c->dev, "i2c");
	if (IS_ERR(regulator_en)) {
		regulator_en = NULL;
		pr_err("regulator_en get fail!");
	}


	regmap = devm_regmap_init_i2c(i2c, &et5904_regmap);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&i2c->dev, "Failed to create regmap: %d\n", ret);
		return ret;
	}

	ret = regmap_read(regmap, ET5904_PRODUCT_ID, &data);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read PRODUCT_ID: %d\n", ret);
		return ret;
	}
	if (data != ET5904_ID) {
		dev_err(&i2c->dev, "Unsupported device id: 0x%x.\n", data);
		//return -ENODEV;
	}

	dev_info(&i2c->dev, "regulator ic device id: 0x%x.\n", data);
	if(data == 0x04)
	{
		printk("enable gpio\n");
		if(regulator_en)
		{
			status = regulator_enable(regulator_en);
			mdelay(10);
		}
	}
	else
	{
		printk("disable gpio\n");
		if(regulator_en)
		{
			status = regulator_enable(regulator_en);
			mdelay(10);
			regulator_disable(regulator_en);
		}
	}

    i2cid = i2c_adapter_id(i2c->adapter);

	switch (i2cid) {
	case LDO0_I2C_ID:
		preg_desc = et5904_regulators0;
		break;
	case LDO1_I2C_ID:
		preg_desc = et5904_regulators1;
		break;
	case LDO3_I2C_ID:
		preg_desc = et5904_regulators3;
		break;
	case LDO4_I2C_ID:
		preg_desc = et5904_regulators4;
		break;
	case LDO6_I2C_ID:
		preg_desc = et5904_regulators6;
		break;
	case LDO7_I2C_ID:
		preg_desc = et5904_regulators7;
		break;
	case LDO8_I2C_ID:
		preg_desc = et5904_regulators8;
		break;
	case LDO9_I2C_ID:
		preg_desc = et5904_regulators9;
		break;
	case LDO10_I2C_ID:
		preg_desc = et5904_regulators10;
		break;
	case LDO11_I2C_ID:
		preg_desc = et5904_regulators11;
		break;
	case LDO13_I2C_ID:
		preg_desc = et5904_regulators13;
		break;
	default:
		dev_err(&i2c->dev, "UnSupported I2C-[%d]\n",  i2cid);
		return -1;
		break;
	}

	config.dev = &i2c->dev;
	config.init_data = NULL;

	for (i = 0; i < ARRAY_SIZE(et5904_regulators1); i++) {
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

    #if IS_ENABLED(TRAN_AD10)
	if (i2cid == MAINLCM_LDO_ID)
	{
		regulator_init_complete_mainlcm();
		regulator_init_complete_sublcm();
	}
	#endif

	if(i2cid < LDOMAX_I2C_ID){
		debug_regmap[i2cid] = regmap;

		i2c_id = i2cid;

	}

    ret = sysfs_create_group(&i2c->dev.kobj, &et5904_group);
    if (ret) {
        sysfs_remove_group(&i2c->dev.kobj, &et5904_group);
        return ret;
    }
	return 0;
}

static void et5904_reg_shutdown(struct i2c_client *i2c)
{
#if IS_ENABLED(CONFIG_TRAN_DUAL_INTERNAL_TEMP_PATCH)
#if IS_ENABLED(TRAN_AE10)
	int i2cid;
	unsigned int val;
	i2cid = i2c_adapter_id(i2c->adapter);
	if(i2cid == MAINLCM_LDO_ID || i2cid == LCM_SUB_LDO_ID){
		if(debug_regmap[i2cid] != NULL){
			printk("[et5904][%d]: shutdown entry", i2cid);
			regmap_update_bits(debug_regmap[i2cid], ET5904_ENABLE, 0xff, 0x00);
			regmap_read(debug_regmap[i2cid], ET5904_ENABLE, &val);
			printk("[et5904][%d]:dump ET5904_ENABLE = 0x%x", i2cid, val);
		}
	}
#endif
#endif
}

#ifdef CONFIG_OF
static const struct of_device_id et5904_dt_ids[] = {
	{ .compatible = "tran,et5904", },
	{}
};
MODULE_DEVICE_TABLE(of, et5904_dt_ids);
#endif

static const struct i2c_device_id et5904_i2c_id[] = {
	{ "et5904", },
	{}
};
MODULE_DEVICE_TABLE(i2c, et5904_i2c_id);

static struct i2c_driver et5904_regulator_driver = {
	.driver = {
		.name = "et5904",
		.of_match_table = of_match_ptr(et5904_dt_ids),
	},
	.probe = et5904_i2c_probe,
	.id_table = et5904_i2c_id,
	.shutdown = et5904_reg_shutdown,
};
module_i2c_driver(et5904_regulator_driver);

MODULE_DESCRIPTION("ET5904 PMIC voltage regulator driver");
MODULE_LICENSE("GPL");

