// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/time.h>
#if IS_ENABLED(CONFIG_TC_CHARGER)
#include "tc_common_class.h"
#include "tc_misc_intf.h"
#endif
/*
 * use tcpc dev to detect audio plug in
 */

#if IS_ENABLED(CONFIG_TCPC_CLASS)
//#include "../../../drivers/misc/mediatek/typec/tcpc/inc/tcpm.h"
//#include "../../../drivers/misc/mediatek/typec/tcpc/inc/tcpci_core.h"
#include "tcpm.h"
#include "tcpci_core.h"
#define USE_DUMP_REGISTER 1

enum SWITCH_STATUS {
	SWITCH_STATUS_INVALID = 0,
	SWITCH_STATUS_NOT_CONNECTED,
	SWITCH_STATUS_USB_MODE,
	SWITCH_STATUS_HEADSET_MODE,
	SWITCH_STATUS_UART_MODE,
	SWITCH_STATUS_MAX
};

#define WAS4783C_I2C_NAME	"was4783-driver"

#define WAS4783C_ID              0x00
#define WAS4783C_SWITCH_SETTINGS 0x04
#define WAS4783C_SWITCH_CONTROL  0x05
#define WAS4783C_SWITCH_STATUS0  0x06
#define WAS4783C_SWITCH_STATUS1  0x07
#define WAS4783C_SLOW_L          0x08
#define WAS4783C_SLOW_R          0x09
#define WAS4783C_SLOW_MIC        0x0A
#define WAS4783C_SLOW_SENSE      0x0B
#define WAS4783C_SLOW_GND        0x0C
#define WAS4783C_DELAY_L_R       0x0D
#define WAS4783C_DELAY_L_MIC     0x0E
#define WAS4783C_DELAY_L_SENSE   0x0F
#define WAS4783C_DELAY_L_AGND    0x10
#define WAS4783C_FUNCTION_ENABLE 0x12
#define WAS4783C_RES_PIN_SEL     0x13
#define WAS4783C_RES_DATA        0x14
#define WAS4783C_JACK_STATUS     0x17
#define WAS4783C_DETECTION_INT   0x18
#define WAS4783C_RESET           0x1E
#define WAS4783C_CURRENT_SOURCE  0x1F

#define WAS4783_CHIP_ID			0x31
#define DIO4485_CHIP_ID			0xF6

static int comp_ohm = -1;
module_param(comp_ohm, int, 0644);

static struct timer_list was4783_enable_timer;
static struct work_struct was4783_enable_switch_work;
static struct workqueue_struct *was4783_enable_switch_workqueue;

static struct timer_list was4783_delay_init_timer;
static struct work_struct was4783_delay_init_work;
static struct workqueue_struct *was4783_delay_init_workqueue;

struct was4783_priv {
	struct regmap *regmap;
	struct device *dev;
	struct power_supply *usb_psy;
	struct tcpc_device *tcpc_dev;
	struct notifier_block psy_nb;
	atomic_t usbc_mode;
	struct work_struct usbc_analog_work;
	struct blocking_notifier_head was4783_notifier;
	struct mutex notification_lock;
	struct tran_device *usbc_analog_switch_dev;
	struct tran_properties usbc_analog_switch_props;
	unsigned int chip_id;

};

static struct was4783_priv *global_was4783_data;

struct was4783_reg_val {
	u16 reg;
	u8 val;
};

static const struct regmap_config was4783_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = WAS4783C_CURRENT_SOURCE,
};

static const struct was4783_reg_val was_reg_i2c_defaults[] = {
	{WAS4783C_SWITCH_SETTINGS, 0x98},
	{WAS4783C_SWITCH_CONTROL, 0x18},
	{WAS4783C_SLOW_L, 0x00},
	{WAS4783C_SLOW_R, 0x00},
	{WAS4783C_SLOW_MIC, 0x00},
	{WAS4783C_SLOW_SENSE, 0x00},
	{WAS4783C_SLOW_GND, 0x00},
	{WAS4783C_DELAY_L_R, 0x00},
	{WAS4783C_DELAY_L_MIC, 0x00},
	{WAS4783C_DELAY_L_SENSE, 0x00},
	{WAS4783C_DELAY_L_AGND, 0x00},
	{WAS4783C_FUNCTION_ENABLE, 0x48},
	{WAS4783C_CURRENT_SOURCE, 0x07},
};

enum SWITCH_STATUS was4783_get_switch_mode(void);

extern void accdet_eint_callback_wrapper(unsigned int plug_status);

#if USE_DUMP_REGISTER
static void dump_register(void);
#endif

static void was4783_set_headset_pulg_in(unsigned int plug_status)
{
	accdet_eint_callback_wrapper(plug_status);
}

static void was4783_usbc_update_settings(struct was4783_priv *was_priv,
		u32 switch_control, u32 switch_enable)
{
	if (!was_priv) {
		pr_err("%s: invalid was_priv %p\n", __func__, was_priv);
		return;
	}
	if (!was_priv->regmap) {
		dev_err(was_priv->dev, "%s: regmap invalid\n", __func__);
		return;
	}

	regmap_write(was_priv->regmap, WAS4783C_SWITCH_SETTINGS, 0x80);
	regmap_write(was_priv->regmap, WAS4783C_SWITCH_CONTROL, switch_control);
	/* WAS4783C chip hardware requirement */
	usleep_range(50, 55);
	regmap_write(was_priv->regmap, WAS4783C_SWITCH_SETTINGS, switch_enable);
}

static int was4783_tcpc_event_changed(struct notifier_block *nb,
				      unsigned long evt, void *ptr)
{
	struct tcp_notify *noti = ptr;
	struct was4783_priv *was_priv = container_of(nb, struct was4783_priv, psy_nb);

	if (NULL == noti) {
		pr_err("%s: data is NULL. \n", __func__);
		return NOTIFY_DONE;
	}

	switch (evt) {
	case TCP_NOTIFY_TYPEC_STATE:
		if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
			noti->typec_state.new_state == TYPEC_ATTACHED_AUDIO) {
			/* Audio Plug in */
			pr_info("%s: Audio Plug In \n", __func__);

			regmap_write(was_priv->regmap, WAS4783C_SWITCH_SETTINGS, 0x9F);
			regmap_write(was_priv->regmap, WAS4783C_SWITCH_CONTROL, 0x00);
			regmap_write(was_priv->regmap, WAS4783C_FUNCTION_ENABLE, 0x49);

			mod_timer(&was4783_enable_timer, jiffies + (int)(0.2 * HZ));
			usleep_range(500, 550);
			pr_info("%s: was4783 delay 500us \n", __func__);
			was4783_set_headset_pulg_in(1);
		} else if (noti->typec_state.old_state == TYPEC_ATTACHED_AUDIO &&
			noti->typec_state.new_state == TYPEC_UNATTACHED) {
			/* Audio Plug out */
			pr_info("%s: Audio Plug Out \n", __func__);

			regmap_write(was_priv->regmap, WAS4783C_FUNCTION_ENABLE, 0x48);
			was4783_usbc_update_settings(was_priv, 0x18, 0x98);  // switch to usb

			was4783_set_headset_pulg_in(0);
		} else if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
			noti->typec_state.new_state != TYPEC_ATTACHED_AUDIO) {
			if (was4783_get_switch_mode() != SWITCH_STATUS_USB_MODE) {
				was4783_usbc_update_settings(was_priv, 0x18, 0x98);
			}
		}
		break;
	}

	return NOTIFY_OK;
}

static void was4783_update_reg_defaults(struct regmap *regmap)
{
	u8 i;

	for (i = 0; i < ARRAY_SIZE(was_reg_i2c_defaults); i++)
		regmap_write(regmap, was_reg_i2c_defaults[i].reg,
					was_reg_i2c_defaults[i].val);
}

enum SWITCH_STATUS was4783_get_switch_mode(void)
{
	uint val = 0;
	enum SWITCH_STATUS state = SWITCH_STATUS_INVALID;
	regmap_read(global_was4783_data->regmap, WAS4783C_SWITCH_STATUS0, &val);

	switch (val & 0xf) {
	case 0x0:
		state = SWITCH_STATUS_NOT_CONNECTED;
		break;
	case 0x5:
		state = SWITCH_STATUS_USB_MODE;
		break;
	case 0xA:
		state = SWITCH_STATUS_HEADSET_MODE;
		break;
	case 0xF:
		state = SWITCH_STATUS_UART_MODE;
		break;
	default:
		state = SWITCH_STATUS_INVALID;
		break;
	}

	return state;
}


static void was4783_enable_switch_handler(struct timer_list *t)
{
	int ret = 0;

	ret = queue_work(was4783_enable_switch_workqueue, &was4783_enable_switch_work);
	if (!ret)
		pr_info("%s, queue work return: %d!\n", __func__, ret);
}

static void was4783_enable_switch_work_callback(struct work_struct *work)
{
	u32 int_status = 0;
	u32 jack_status = 0;
	u32 switch_control = 0;
	u32 switch_setting = 0;

	pr_info("%s()\n", __func__);
	regmap_read(global_was4783_data->regmap, WAS4783C_DETECTION_INT, &int_status);
	regmap_read(global_was4783_data->regmap, WAS4783C_JACK_STATUS, &jack_status);

	if ((int_status & (1 << 2)) && (jack_status & (1 << 1))) {
		pr_info("%s: 3-pole detect\n", __func__);
		regmap_read(global_was4783_data->regmap, WAS4783C_SWITCH_SETTINGS, &switch_setting);
		regmap_read(global_was4783_data->regmap, WAS4783C_SWITCH_CONTROL, &switch_control);

		/* Set Bit 1 to 0, Enable Mic <---> SBU2 */
		regmap_write(global_was4783_data->regmap, WAS4783C_SWITCH_CONTROL, switch_control & (~(1 << 1)));
		/* WAS4780 chip hardware requirement */
		usleep_range(50, 55);
		/* Enable Bit 1, Enable Mic <---> SBU2 Switch */
		regmap_write(global_was4783_data->regmap, WAS4783C_SWITCH_SETTINGS, switch_setting | (1 << 1));
	}

	/* call accdet after set reg */
	mdelay(5);
	was4783_set_headset_pulg_in(1);

#if USE_DUMP_REGISTER
	dump_register();
#endif
}

#if USE_DUMP_REGISTER
static void dump_register(void)
{
	int adr = 0, value = 0;
	pr_info("%s:dump was4783c chip register\n", __func__);
	for (adr = 0; adr <= WAS4783C_CURRENT_SOURCE; adr++) {
		regmap_read(global_was4783_data->regmap, adr, &value);
		pr_info("register (0x%x) = 0x%x", adr, value);
	}
}
#endif

static void delay_init_timer_callback(struct timer_list *t)
{
	int ret = 0;

	ret = queue_work(was4783_delay_init_workqueue, &was4783_delay_init_work);
	pr_info("%s \n", __func__);

	if (!ret)
		pr_info("%s, queue work return: %d!\n", __func__, ret);
}

static void was4783_delay_init_work_callback(struct work_struct *work)
{
	pr_info("%s() \n", __func__);

	if (global_was4783_data && global_was4783_data->tcpc_dev) {
		/* check tcpc status at startup */
		if (TYPEC_ATTACHED_AUDIO == tcpm_inquire_typec_attach_state(global_was4783_data->tcpc_dev)) {
			/* Audio Plug in */
			pr_info("%s: Audio is Plug In status at startup\n", __func__);

			regmap_write(global_was4783_data->regmap, WAS4783C_SWITCH_SETTINGS, 0x9F);
			regmap_write(global_was4783_data->regmap, WAS4783C_SWITCH_CONTROL, 0x00);
			regmap_write(global_was4783_data->regmap, WAS4783C_FUNCTION_ENABLE, 0x49);
			mod_timer(&was4783_enable_timer, jiffies + (int)(0.2 * HZ));
			was4783_set_headset_pulg_in(1);
		}

	}
}

#if IS_ENABLED(CONFIG_TC_CHARGER)
#define SPECIAL_CABLE_L 25
#define SPECIAL_CABLE_H 55
static bool was4783_water_detect(struct was4783_priv *was_priv)
{
	int i = 0;
    int ret = 0;
	int value = 0;
	int ibus = 0;
	int comp_resistance;
	short special_cable_sbu1 = 0;
	short special_cable_sbu2 = 0;

	if (IS_ERR_OR_NULL(was_priv)) {
		pr_err("%s: invalid was_priv %p\n", __func__, was_priv);
		return false;
	}

	if (IS_ERR_OR_NULL(was_priv->regmap)) {
		dev_err(was_priv->dev, "%s: regmap invalid\n", __func__);
		return false;
	}

	/* calculate comp resistance */
	ibus = tc_get_ibus();
	if (comp_ohm != -1)
		comp_resistance = ibus * comp_ohm * 1000 / 9375 / 100; // 4:0.004ohm; 9375:LSB9.375
	else
		comp_resistance = ibus * 4 * 1000 / 9375 / 100; // 4:0.004ohm; 9375:LSB9.375
	pr_info("%s: raw comp_resistance:%d\n", __func__, comp_resistance);

	if (comp_resistance < 0)
		comp_resistance = 0;
	else if (comp_resistance > 30)
		comp_resistance = 30;

	if (comp_resistance != 0)
		comp_resistance = comp_resistance / 10 * 10 + 10;

	pr_info("%s: real comp_resistance:%d\n", __func__, comp_resistance);

	/* detect SBU1 */
	regmap_update_bits(was_priv->regmap, WAS4783C_RES_PIN_SEL, 0x07, 0x03);
	regmap_update_bits(was_priv->regmap, WAS4783C_FUNCTION_ENABLE, 0x22, 0x22);

	for (i = 0; i < 10; i++) {
		ret = regmap_read(was_priv->regmap, WAS4783C_DETECTION_INT, &value);
		if (value & 0x01) {
			pr_info("%s: SBU1 res detect done:0x%x\n", __func__, value);
			break;
		}
		msleep(5);
	}

	if ((ret < 0) && (value == 0x0)) {
		pr_err("%s:ic read data error\n", __func__);
		return false;
	}

	regmap_read(was_priv->regmap, WAS4783C_RES_DATA, &value);
	pr_info("%s: SBU1 res data:%d\n", __func__, value * 10);
	value  = value * 10 + comp_resistance;
	/* maybe some other manufacture cable, so exclude the range */
	if (value > SPECIAL_CABLE_L && value < SPECIAL_CABLE_H) {
		special_cable_sbu1 = 1;
		pr_info("%s: special cable SBU1 check value %d\n", __func__, value);
	} else if (value < 300) {
		msleep(100);
		regmap_read(was_priv->regmap, WAS4783C_RES_DATA, &value);
		value  = value * 10 + comp_resistance;
		if (value < 300) {
			pr_info("%s: SBU1 check have water:%d\n", __func__, value);
			return true;
		}
	}

	/* detect SBU2 */
	regmap_update_bits(was_priv->regmap, WAS4783C_RES_PIN_SEL, 0x07, 0x04);
	regmap_update_bits(was_priv->regmap, WAS4783C_FUNCTION_ENABLE, 0x22, 0x22);

	for (i = 0; i < 10; i++) {
		regmap_read(was_priv->regmap, WAS4783C_DETECTION_INT, &value);
		if (value & 0x01) {
			pr_info("%s: SBU2 res detect done:0x%x\n", __func__, value);
			break;
		}
		msleep(5);
	}

	regmap_read(was_priv->regmap, WAS4783C_RES_DATA, &value);
	pr_info("%s: SBU2 res data:%d\n", __func__, value * 10);
	value  = value * 10 + comp_resistance;
	/* maybe some other manufacture cable, so exclude the range */
	if (value > SPECIAL_CABLE_L && value < SPECIAL_CABLE_H) {
		special_cable_sbu2 = 1;
		pr_info("%s: special cable SBU2 check value %d\n", __func__, value);
	} else if (value < 300) {
		msleep(100);
		regmap_read(was_priv->regmap, WAS4783C_RES_DATA, &value);
		value  = value * 10 + comp_resistance;
		if (value < 300) {
			pr_info("%s: SBU2 check have water:%d\n", __func__, value);
			return true;
		}
	}

	/* only two sbu pin detected that been defined special_cable */
	if (special_cable_sbu1 != special_cable_sbu2)
		return true;

	return false;
}
#endif

static int usbc_analog_switch_get_property(struct tran_device *dev,
				enum tran_common_prop prop,
				union com_propval *val)
{
	struct was4783_priv *was_priv = tran_get_data(dev);
	int ret = 0;

	switch (prop) {
	case TRAN_PROP_WATER_DETECT:
		val->intval = was4783_water_detect(was_priv);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int usbc_analog_switch_set_property(struct tran_device *dev,
					   enum tran_common_prop prop,
					   const union com_propval *val)
{
	struct was4783_priv *was_priv = tran_get_data(dev);
	int ret = 0;

	switch (prop) {
	case TRAN_PROP_USB_PLUG_IN:
		break;
	case TRAN_PROP_USB_PLUG_OUT:
		if (was_priv->chip_id == DIO4485_CHIP_ID)
			regmap_write(was_priv->regmap, 0x1E, 0x01);
		break;
	case TRAN_PROP_USB_CTRL_TA_OFF:
	case TRAN_PROP_USB_CTRL_TC30_TA:
	case TRAN_PROP_USB_CTRL_RFC:
		if (val->intval) {
			was4783_usbc_update_settings(was_priv, 0x98, 0x98);
		} else {
			was4783_usbc_update_settings(was_priv, 0x18, 0x98);
		}
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static struct tran_ops usbc_analog_switch_ops = {
	.get_prop = usbc_analog_switch_get_property,
	.set_prop = usbc_analog_switch_set_property,
};

static int was4783_probe(struct i2c_client *i2c,
			 const struct i2c_device_id *id)
{
	struct was4783_priv *was_priv;
	int rc = 0;
	int reg_val = 0;

	was_priv = devm_kzalloc(&i2c->dev, sizeof(*was_priv),
				GFP_KERNEL);
	if (!was_priv)
		return -ENOMEM;

	global_was4783_data = was_priv;
	was_priv->dev = &i2c->dev;

	was_priv->tcpc_dev = tcpc_dev_get_by_name("type_c_port0");
	if (!was_priv->tcpc_dev) {
		rc = -EPROBE_DEFER;
		pr_err("%s get tcpc device type_c_port0 fail \n", __func__);
		goto err_data;
	}

	was_priv->regmap = devm_regmap_init_i2c(i2c, &was4783_regmap_config);
	if (IS_ERR_OR_NULL(was_priv->regmap)) {
		dev_err(was_priv->dev, "%s: Failed to initialize regmap: %d\n",
			__func__, rc);
		if (!was_priv->regmap) {
			rc = -EINVAL;
			goto err_supply;
		}
		rc = PTR_ERR(was_priv->regmap);
		goto err_supply;
	}

	regmap_read(was_priv->regmap, WAS4783C_ID, &reg_val);
	was_priv->chip_id = reg_val;
	if (was_priv->chip_id != WAS4783_CHIP_ID && was_priv->chip_id != DIO4485_CHIP_ID) {
		pr_err("%s: device id error!\n", __func__);
		rc = -ENODEV;
		goto err_supply;
	}

	was4783_update_reg_defaults(was_priv->regmap);

	global_was4783_data->psy_nb.notifier_call = was4783_tcpc_event_changed;
	global_was4783_data->psy_nb.priority = 0;
	rc = register_tcp_dev_notifier(global_was4783_data->tcpc_dev, &global_was4783_data->psy_nb, TCP_NOTIFY_TYPE_USB);
	if (rc) {
		pr_err("%s: register_tcp_dev_notifier failed\n", __func__);
		goto err_supply;
	}
	
	mutex_init(&was_priv->notification_lock);
	i2c_set_clientdata(i2c, was_priv);

	was_priv->usbc_analog_switch_props.alias_name = "usbc_analog_switch";
	was_priv->usbc_analog_switch_dev = tran_device_register("usbc_analog_switch",
								was_priv->dev, was_priv,
								&usbc_analog_switch_ops,
								&was_priv->usbc_analog_switch_props);
	if (IS_ERR_OR_NULL(was_priv->usbc_analog_switch_dev)) {
		pr_err("%s: failed to register tran common class\n", __func__);
		rc = -ENODEV;
		goto err_supply;
	}

	was_priv->was4783_notifier.rwsem =
		(struct rw_semaphore)__RWSEM_INITIALIZER
		((was_priv->was4783_notifier).rwsem);
	was_priv->was4783_notifier.head = NULL;


	was4783_enable_switch_workqueue = create_singlethread_workqueue("enableSwitchQueue");
	INIT_WORK(&was4783_enable_switch_work, was4783_enable_switch_work_callback);
	if (!was4783_enable_switch_workqueue) {
		rc = -1;
		pr_notice("%s create was4783_enable_switch workqueue fail.\n", __func__);
		goto err_common_dev;
	}

	pr_info("%s(), setup enable timer", __func__);
	timer_setup(&was4783_enable_timer, was4783_enable_switch_handler, 0);

	was4783_delay_init_workqueue = create_singlethread_workqueue("delayInitQueue");
	INIT_WORK(&was4783_delay_init_work, was4783_delay_init_work_callback);

	/* delay 2s to register tcpc event change, after accdet init done */
	timer_setup(&was4783_delay_init_timer, delay_init_timer_callback, 0);
	mod_timer(&was4783_delay_init_timer, jiffies + (int)(2 * HZ));

	pr_info("%s audio switch use was4783 reg_val is %x", __func__, reg_val);

	return 0;

err_common_dev:
	tran_device_unregister(was_priv->usbc_analog_switch_dev);
err_supply:
	unregister_tcp_dev_notifier(global_was4783_data->tcpc_dev, &global_was4783_data->psy_nb, TCP_NOTIFY_TYPE_USB);
err_data:
	devm_kfree(&i2c->dev, was_priv);
	return rc;
}

static void was4783_remove(struct i2c_client *i2c)
{
	struct was4783_priv *was_priv =
			(struct was4783_priv *)i2c_get_clientdata(i2c);

	if (!was_priv)
		return; //-EINVAL;

	was4783_usbc_update_settings(was_priv, 0x18, 0x98);

	mutex_destroy(&was_priv->notification_lock);
	dev_set_drvdata(&i2c->dev, NULL);

	return;
}

static const struct of_device_id was4783_i2c_dt_match[] = {
	{
		.compatible = "mediatek,was4783-audioswitch",
	},
	{}
};

static struct i2c_driver was4783_i2c_driver = {
	.driver = {
		.name = WAS4783C_I2C_NAME,
		.of_match_table = was4783_i2c_dt_match,
	},
	.probe = was4783_probe,
	.remove = was4783_remove,
};

static int __init was4783_init(void)
{
	int rc;

	rc = i2c_add_driver(&was4783_i2c_driver);
	if (rc)
		pr_err("was4783: Failed to register I2C driver: %d\n", rc);

	return rc;
}

static void __exit was4783_exit(void)
{
	i2c_del_driver(&was4783_i2c_driver);
}
#else
static int __init was4783_init(void)
{
	return 0;
}

static void __exit was4783_exit(void)
{
    //not use typec switch
}
#endif
late_initcall(was4783_init);
module_exit(was4783_exit);

MODULE_DESCRIPTION("WAS4783C I2C driver");
MODULE_LICENSE("GPL v2");
