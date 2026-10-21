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

#include <linux/pinctrl/consumer.h>
#if IS_ENABLED(CONFIG_TC_CHARGER)
#include "tc_common_class.h"
#include "tc_misc_intf.h"
#endif

/*
 * use tcpc dev to detect audio plug in
 */
#include "./../../../drivers/misc/mediatek/typec/tcpc/inc/tcpci_core.h"
#include "./../../../drivers/misc/mediatek/typec/tcpc/inc/tcpm.h"

#define USE_POWER_SUPPLY_NOTIFIER    0
#define USE_TCPC_NOTIFIER            1
#define HL1280_REG_DUMP              0

enum et_function {
	ET_MIC_GND_SWAP,
	ET_USBC_ORIENTATION_CC1,
	ET_USBC_ORIENTATION_CC2,
	ET_USBC_DISPLAYPORT_DISCONNECTED,
	ET_EVENT_MAX,
};

enum SWITCH_STATUS {
	SWITCH_STATUS_INVALID = 0,
	SWITCH_STATUS_NOT_CONNECTED,
	SWITCH_STATUS_USB_MODE,
	SWITCH_STATUS_HEADSET_MODE,
	SWITCH_STATUS_MAX
};

char *switch_status_string[SWITCH_STATUS_MAX] = {
	"switch invalid",
	"switch not connected",
	"switch usb mode",
	"switch headset mode",
};

#define HL1280_I2C_NAME	"hl1280-driver"

#define HL1280_ID              0x00
#define HL1280_SWITCH_SETTINGS 0x04
#define HL1280_SWITCH_CONTROL  0x05
#define HL1280_SWITCH_STATUS0  0x06
#define HL1280_SWITCH_STATUS1  0x07
#define HL1280_SLOW_L          0x08
#define HL1280_SLOW_R          0x09
#define HL1280_SLOW_MIC        0x0A
#define HL1280_SLOW_SENSE      0x0B
#define HL1280_SLOW_GND        0x0C
#define HL1280_DELAY_L_R       0x0D
#define HL1280_DELAY_L_MIC     0x0E
#define HL1280_DELAY_L_SENSE   0x0F
#define HL1280_DELAY_L_AGND    0x10
#define HL1280_FUNCTION_ENABLE 0x12
#define HL1280_RES_PIN_SEL     0x13
#define HL1280_RES_DATA        0x14
#define HL1280_JACK_STATUS     0x17
#define HL1280_DETECTION_INT   0x18
#define HL1280_RESET           0x1E
#define HL1280_CURRENT_SOURCE  0x1F

#define ET_DBG_TYPE_MODE          0
#define ET_DBG_REG_MODE           1

#define HL1280_DELAY_INIT_TIME     (2 * HZ)

#define HL1280_ID_VALUE   0x49 //modify chip id for tran

static int comp_ohm = -1;
module_param(comp_ohm, int, 0644);

static struct timer_list hl1280_enable_timer;
static struct work_struct hl1280_enable_switch_work;
static struct workqueue_struct *hl1280_enable_switch_workqueue;

static struct timer_list hl1280_delay_init_timer;
static struct work_struct hl1280_delay_init_work;
static struct workqueue_struct *hl1280_delay_init_workqueue;

struct hl1280_priv {
	struct regmap *regmap;
	struct device *dev;
	struct power_supply *usb_psy;
	struct tcpc_device *tcpc_dev;
	struct notifier_block psy_nb;
	atomic_t usbc_mode;
	struct work_struct usbc_analog_work;//not used
	struct blocking_notifier_head hl1280_notifier;
	struct mutex notification_lock;
//	struct pinctrl *uart_en_gpio_pinctrl;
	struct pinctrl_state *pinctrl_state_enable;
	struct pinctrl_state *pinctrl_state_disable;
#if IS_ENABLED(CONFIG_TC_CHARGER)
	struct tran_device *usbc_analog_switch_dev;
	struct tran_properties usbc_analog_switch_props;
#endif
	bool hl1280;
};

static struct hl1280_priv *global_hl1280_data;

struct hl1280_reg_val {
	u16 reg;
	u8 val;
};

static const struct regmap_config hl1280_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = HL1280_CURRENT_SOURCE,
};

static const struct hl1280_reg_val et_reg_i2c_defaults[] = {
	{HL1280_SWITCH_SETTINGS, 0x98},
	{HL1280_SWITCH_CONTROL, 0x18},
	{HL1280_SLOW_L, 0x00},
	{HL1280_SLOW_R, 0x00},
	{HL1280_SLOW_MIC, 0x00},
	{HL1280_SLOW_SENSE, 0x00},
	{HL1280_SLOW_GND, 0x00},
	{HL1280_DELAY_L_R, 0x00},
	{HL1280_DELAY_L_MIC, 0x00},
	{HL1280_DELAY_L_SENSE, 0x00},
	{HL1280_DELAY_L_AGND, 0x00},
	{HL1280_FUNCTION_ENABLE, 0x48},
	{HL1280_CURRENT_SOURCE, 0x07},
};

enum SWITCH_STATUS hl1280_get_switch_mode(void);

extern void accdet_eint_callback_wrapper(unsigned int plug_status);
#if HL1280_REG_DUMP
static void dump_register(void);
#endif

static void hl1280_usbc_update_settings(struct hl1280_priv *et_priv, u32 switch_control, u32 switch_enable)
{
	if (!et_priv) {
		pr_err("%s: invalid et_priv %p\n", __func__, et_priv);
		return;
	}
	if (!et_priv->regmap) {
		dev_err(et_priv->dev, "%s: regmap invalid\n", __func__);
		return;
	}
	regmap_write(et_priv->regmap, HL1280_SWITCH_SETTINGS, 0x80);
	regmap_write(et_priv->regmap, HL1280_SWITCH_CONTROL, switch_control);
	/* HL1280 chip hardware requirement */
	usleep_range(50, 55);
	regmap_write(et_priv->regmap, HL1280_SWITCH_SETTINGS, switch_enable);
}

static int hl1280_tcpc_event_changed(struct notifier_block *nb,
				      unsigned long evt, void *ptr)
{
	struct tcp_notify *noti = ptr;
	struct hl1280_priv *et_priv = container_of(nb, struct hl1280_priv, psy_nb);

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

			regmap_write(et_priv->regmap, HL1280_SWITCH_SETTINGS, 0x9F);
			regmap_write(et_priv->regmap, HL1280_SWITCH_CONTROL, 0x00);
			regmap_write(et_priv->regmap, HL1280_FUNCTION_ENABLE, 0x49);
			mod_timer(&hl1280_enable_timer, jiffies + (int)(0.2 * HZ));
			mdelay(5);

			accdet_eint_callback_wrapper(1);
		} else if (noti->typec_state.old_state == TYPEC_ATTACHED_AUDIO &&
			noti->typec_state.new_state == TYPEC_UNATTACHED) {
			/* Audio Plug out */
			pr_info("%s: Audio Plug Out \n", __func__);

			hl1280_usbc_update_settings(et_priv, 0x18, 0x98);
			msleep(200);
			accdet_eint_callback_wrapper(0);
		} else if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
			noti->typec_state.new_state != TYPEC_ATTACHED_AUDIO) {
			if (hl1280_get_switch_mode() != SWITCH_STATUS_USB_MODE) {
				hl1280_usbc_update_settings(et_priv, 0x18, 0x98);
			}
		}
		break;
	}
	return NOTIFY_OK;
}


/*
 * hl1280_unreg_notifier - unregister notifier block with et driver
 *
 * @nb - notifier block of hl1280
 * @node - phandle node to hl1280 device
 *
 * Returns 0 on pass, or error code
 */
int hl1280_unreg_notifier(struct notifier_block *nb,
			     struct device_node *node)
{
	struct i2c_client *client = of_find_i2c_device_by_node(node);
	struct hl1280_priv *et_priv;

	if (!client)
		return -EINVAL;

	et_priv = (struct hl1280_priv *)i2c_get_clientdata(client);
	if (!et_priv)
		return -EINVAL;

	hl1280_usbc_update_settings(et_priv, 0x18, 0x98);
	return blocking_notifier_chain_unregister
					(&et_priv->hl1280_notifier, nb);
}
EXPORT_SYMBOL(hl1280_unreg_notifier);


static int hl1280_validate_display_port_settings(struct hl1280_priv *et_priv)
{
	u32 switch_status = 0;

	regmap_read(et_priv->regmap, HL1280_SWITCH_STATUS1, &switch_status);

	if ((switch_status != 0x23) && (switch_status != 0x1C)) {
		pr_err("AUX SBU1/2 switch status is invalid = %u\n",
				switch_status);
		return -EIO;
	}

	return 0;
}


/*
 * hl1280_switch_event - configure ET switch position based on event
 *
 * @node - phandle node to hl1280 device
 * @event - et_function enum
 *
 * Returns int on whether the switch happened or not
 */
int hl1280_switch_event(struct device_node *node,
			 enum et_function event)
{
	int switch_control = 0;
	struct i2c_client *client = of_find_i2c_device_by_node(node);
	struct hl1280_priv *et_priv;

	if (!client)
		return -EINVAL;

	et_priv = (struct hl1280_priv *)i2c_get_clientdata(client);
	if (!et_priv)
		return -EINVAL;
	if (!et_priv->regmap)
		return -EINVAL;

	switch (event) {
	case ET_MIC_GND_SWAP:
		regmap_read(et_priv->regmap, HL1280_SWITCH_CONTROL,
				&switch_control);
		if ((switch_control & 0x07) == 0x07)
			switch_control = 0x0;
		else
			switch_control = 0x7;
		hl1280_usbc_update_settings(et_priv, switch_control, 0x9F);
		return 1;
	case ET_USBC_ORIENTATION_CC1:
		hl1280_usbc_update_settings(et_priv, 0x18, 0xF8);
		return hl1280_validate_display_port_settings(et_priv);
	case ET_USBC_ORIENTATION_CC2:
		hl1280_usbc_update_settings(et_priv, 0x78, 0xF8);
		return hl1280_validate_display_port_settings(et_priv);
	case ET_USBC_DISPLAYPORT_DISCONNECTED:
		hl1280_usbc_update_settings(et_priv, 0x18, 0x98);
		break;
	default:
		break;
	}
#if HL1280_REG_DUMP
	dump_register();
#endif
	return 0;
}
EXPORT_SYMBOL(hl1280_switch_event);


static void hl1280_update_reg_defaults(struct regmap *regmap)
{
	u8 i;

	for (i = 0; i < ARRAY_SIZE(et_reg_i2c_defaults); i++)
		regmap_write(regmap, et_reg_i2c_defaults[i].reg,
					et_reg_i2c_defaults[i].val);
}

/* add hl1280 info node */
enum SWITCH_STATUS hl1280_get_switch_mode(void)
{
	uint val = 0;
	enum SWITCH_STATUS state = SWITCH_STATUS_INVALID;
	regmap_read(global_hl1280_data->regmap, HL1280_SWITCH_STATUS0, &val);

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
	default:
		state = SWITCH_STATUS_INVALID;
		break;
	}

	return state;
}

int hl1280_switch_mode(enum SWITCH_STATUS val)
{
	if (val == SWITCH_STATUS_HEADSET_MODE) {
		hl1280_usbc_update_settings(global_hl1280_data, 0x00, 0x9F); // switch to headset
	} else if (val == SWITCH_STATUS_USB_MODE) {
		hl1280_usbc_update_settings(global_hl1280_data, 0x18, 0x98); // switch to USB
	}

	return 0;
}

static ssize_t sysfs_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf, u32 type)
{
	int value = 0;
	char *mode = "Unknown mode";
	int i = 0;
	ssize_t ret_size = 0;

	switch (type) {
	case ET_DBG_TYPE_MODE:
		value = hl1280_get_switch_mode();
		mode = switch_status_string[value];
		ret_size += sprintf(buf, "%s: %d \n", mode, value);
		break;

	case ET_DBG_REG_MODE:
		for (i = 0; i <= HL1280_CURRENT_SOURCE; i++) {
			regmap_read(global_hl1280_data->regmap, i, &value);
			ret_size += sprintf(buf + ret_size, "Reg: 0x%x, Value: 0x%x \n", i, value);
		}
	    break;
	default:
		pr_warn("%s: invalid type %d\n", __func__, type);
		break;
	}
	return ret_size;
}

static ssize_t sysfs_set(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count, u32 type)
{
	int err;
	unsigned long value;

	err = kstrtoul(buf, 10, &value);
	if (err) {
		pr_warn("%s: get data of type %d failed\n", __func__, type);
		return err;
	}

	pr_info("%s: set type %d, data %ld\n", __func__, type, value);
	switch (type) {
	case ET_DBG_TYPE_MODE:
		hl1280_switch_mode((enum SWITCH_STATUS)value);
		break;
	default:
		pr_warn("%s: invalid type %d\n", __func__, type);
		break;
	}
	return count;
}

#define hl1280_DEVICE_SHOW(_name, _type) static ssize_t \
show_##_name(struct device *dev, \
			  struct device_attribute *attr, char *buf) \
{ \
	return sysfs_show(dev, attr, buf, _type); \
}

#define hl1280_DEVICE_SET(_name, _type) static ssize_t \
set_##_name(struct device *dev, \
			 struct device_attribute *attr, \
			 const char *buf, size_t count) \
{ \
	return sysfs_set(dev, attr, buf, count, _type); \
}

hl1280_DEVICE_SHOW(hl1280_switch_mode, ET_DBG_TYPE_MODE);
hl1280_DEVICE_SET(hl1280_switch_mode, ET_DBG_TYPE_MODE);
static DEVICE_ATTR(hl1280_switch_mode, S_IWUSR | S_IRUGO,
	show_hl1280_switch_mode, set_hl1280_switch_mode);

hl1280_DEVICE_SHOW(hl1280_reg, ET_DBG_REG_MODE);
hl1280_DEVICE_SET(hl1280_reg, ET_DBG_REG_MODE);
static DEVICE_ATTR(hl1280_reg, S_IWUSR | S_IRUGO,
	show_hl1280_reg, set_hl1280_reg);


static struct attribute *hl1280_attrs[] = {
	&dev_attr_hl1280_switch_mode.attr,
	&dev_attr_hl1280_reg.attr,
	NULL
};

static const struct attribute_group hl1280_group = {
	.attrs = hl1280_attrs,
};
/* end of info node */



static void hl1280_enable_switch_handler(struct timer_list *t)
{
	int ret = 0;

	ret = queue_work(hl1280_enable_switch_workqueue, &hl1280_enable_switch_work);
	if (!ret)
		pr_info("%s, queue work return: %d!\n", __func__, ret);
}

static void hl1280_enable_switch_work_callback(struct work_struct *work)
{
	u32 int_status = 0;
	u32 jack_status = 0;
	u32 switch_control = 0;
	u32 switch_setting = 0;
	pr_info("%s()\n", __func__);
	regmap_read(global_hl1280_data->regmap, HL1280_DETECTION_INT, &int_status);
	regmap_read(global_hl1280_data->regmap, HL1280_JACK_STATUS, &jack_status);
	if ((global_hl1280_data->hl1280) && (int_status & (1 << 2)) && (jack_status & (1 << 1))) {
		pr_info("%s: 3-pole detect\n", __func__);
		regmap_read(global_hl1280_data->regmap, HL1280_SWITCH_SETTINGS, &switch_setting);
		regmap_read(global_hl1280_data->regmap, HL1280_SWITCH_CONTROL, &switch_control);

		/* Set Bit 1 to 0, Enable Mic <---> SBU2 */
		regmap_write(global_hl1280_data->regmap, HL1280_SWITCH_CONTROL, switch_control & (~(1 << 1)));
		/* HL1280 chip hardware requirement */
		usleep_range(50, 55);
		/* Enable Bit 1, Enable Mic <---> SBU2 Switch */
		regmap_write(global_hl1280_data->regmap, HL1280_SWITCH_SETTINGS, switch_setting | (1 << 1));
	}
	accdet_eint_callback_wrapper(1);
#if HL1280_REG_DUMP
	dump_register();
#endif
}

static void delay_init_timer_callback(struct timer_list *t)
{
	int ret = 0;

	ret = queue_work(hl1280_delay_init_workqueue, &hl1280_delay_init_work);
	pr_info("%s \n", __func__);

	if (!ret)
		pr_info("%s, queue work return: %d!\n", __func__, ret);
}

static void hl1280_delay_init_work_callback(struct work_struct *work)
{
#if USE_TCPC_NOTIFIER
	if (global_hl1280_data && global_hl1280_data->tcpc_dev) {
		/* check tcpc status at startup */
		pr_info("%s(), Typec Plug In in power on.", __func__);
		if (TYPEC_ATTACHED_AUDIO == tcpm_inquire_typec_attach_state(global_hl1280_data->tcpc_dev)) {
			regmap_write(global_hl1280_data->regmap, HL1280_SWITCH_SETTINGS, 0x9F);
			regmap_write(global_hl1280_data->regmap, HL1280_SWITCH_CONTROL, 0x00);
			regmap_write(global_hl1280_data->regmap, HL1280_FUNCTION_ENABLE, 0x49);

			mod_timer(&hl1280_enable_timer, jiffies + (int)(0.2 * HZ));
			accdet_eint_callback_wrapper(1);
		}
	}
#if HL1280_REG_DUMP
	dump_register();
#endif
#endif
}

#if HL1280_REG_DUMP
static void dump_register(void)
{
	int adr = 0, value = 0;
	//pr_info("%s:dump %s reg\n",__func__,global_hl1280_data->hl1280);
	for (adr = 0; adr <= HL1280_CURRENT_SOURCE; adr++) {
		regmap_read(global_hl1280_data->regmap, adr, &value);
		pr_info("%s: (0x%x)=0x%x", __func__, adr, value);
	}
}
#endif

#if IS_ENABLED(CONFIG_TC_CHARGER)
#define SPECIAL_CABLE_L 20
#define SPECIAL_CABLE_H 55
static bool hl1280_water_detect(struct hl1280_priv *was_priv)
{
	int i = 0;
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
	regmap_update_bits(was_priv->regmap, HL1280_RES_PIN_SEL, 0x07, 0x03);
	regmap_update_bits(was_priv->regmap, HL1280_FUNCTION_ENABLE, 0x22, 0x22);

	for (i = 0; i < 10; i++) {
		regmap_read(was_priv->regmap, HL1280_FUNCTION_ENABLE, &value);
		if ((value & 0x02) == 0) {
			pr_info("%s: SBU1 res detect done:0x%x\n", __func__, value);
			break;
		}
		msleep(5);
	}

	regmap_read(was_priv->regmap, HL1280_RES_DATA, &value);
	pr_info("%s: SBU1 res data:%d\n", __func__, value * 10);
	value  = value * 10 + comp_resistance;
	/* maybe some other manufacture cable, so exclude the range */
	if (value >= SPECIAL_CABLE_L && value <= SPECIAL_CABLE_H) {
		special_cable_sbu1 = 1;
		pr_info("%s: special cable SBU1 check value %d\n", __func__, value);
	} else if (value < 300) {
		pr_info("%s: SBU1 check have water:%d\n", __func__, value);
		return true;
	}

	/* detect SBU2 */
	regmap_update_bits(was_priv->regmap, HL1280_RES_PIN_SEL, 0x07, 0x04);
	regmap_update_bits(was_priv->regmap, HL1280_FUNCTION_ENABLE, 0x22, 0x22);

	for (i = 0; i < 10; i++) {
		regmap_read(was_priv->regmap, HL1280_FUNCTION_ENABLE, &value);
		if ((value & 0x02) == 0) {
			pr_info("%s: SBU2 res detect done:0x%x\n", __func__, value);
			break;
		}
		msleep(5);
	}

	regmap_read(was_priv->regmap, HL1280_RES_DATA, &value);
	pr_info("%s: SBU2 res data:%d\n", __func__, value * 10);
	value  = value * 10 + comp_resistance;
	/* maybe some other manufacture cable, so exclude the range */
	if (value >= SPECIAL_CABLE_L && value <= SPECIAL_CABLE_H) {
		special_cable_sbu2 = 1;
		pr_info("%s: special cable SBU2 check value %d\n", __func__, value);
	} else if (value < 300) {
		pr_info("%s: SBU2 check have water:%d\n", __func__, value);
		return true;
	}

	/* only two sbu pin detected that been defined special_cable */
	if (special_cable_sbu1 != special_cable_sbu2)
		return true;

	return false;
}

static int usbc_analog_switch_get_property(struct tran_device *dev,
				enum tran_common_prop prop,
				union com_propval *val)
{
	struct hl1280_priv *was_priv = tran_get_data(dev);
	int ret = 0;

	switch (prop) {
	case TRAN_PROP_WATER_DETECT:
		val->intval = hl1280_water_detect(was_priv);
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
	struct hl1280_priv *was_priv = tran_get_data(dev);
	int ret = 0;

	switch (prop) {
	case TRAN_PROP_USB_PLUG_IN:
		break;
	case TRAN_PROP_USB_PLUG_OUT:
		break;
	case TRAN_PROP_USB_CTRL_TA_OFF:
	case TRAN_PROP_USB_CTRL_TC30_TA:
	case TRAN_PROP_USB_CTRL_RFC:
		if (val->intval) {
			hl1280_usbc_update_settings(was_priv, 0x98, 0x98);
		} else {
			hl1280_usbc_update_settings(was_priv, 0x18, 0x98);
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
#endif


int get_type_c_hph_direction(void)
{
	u32 jack_status;
	regmap_read(global_hl1280_data->regmap, HL1280_JACK_STATUS, &jack_status);
	pr_info("%s:jack status is 0x%x\n", __func__, jack_status);
	return jack_status == 0x04;
}
EXPORT_SYMBOL(get_type_c_hph_direction);


static int hl1280_probe(struct i2c_client *i2c,
			 const struct i2c_device_id *id)
{
	struct hl1280_priv *et_priv;
	int rc = 0;
	int reg_val = 0;

	et_priv = devm_kzalloc(&i2c->dev, sizeof(*et_priv),
				GFP_KERNEL);
	if (!et_priv)
		return -ENOMEM;

	global_hl1280_data = et_priv; // add for debug
	et_priv->dev = &i2c->dev;
	et_priv->tcpc_dev = tcpc_dev_get_by_name("type_c_port0");
	if (!et_priv->tcpc_dev) {
		rc = -EPROBE_DEFER;
		pr_err("%s get tcpc device type_c_port0 fail \n", __func__);
		goto err_data;
	}

	et_priv->regmap = devm_regmap_init_i2c(i2c, &hl1280_regmap_config);
	if (IS_ERR_OR_NULL(et_priv->regmap)) {
		dev_err(et_priv->dev, "%s: Failed to initialize regmap: %d\n",
			__func__, rc);
		if (!et_priv->regmap) {
			rc = -EINVAL;
			goto err_supply;
		}
		rc = PTR_ERR(et_priv->regmap);
		goto err_supply;
	}

	hl1280_update_reg_defaults(et_priv->regmap);

	/* register tcpc_event */
	global_hl1280_data->psy_nb.notifier_call = hl1280_tcpc_event_changed;
	global_hl1280_data->psy_nb.priority = 0;
	rc = register_tcp_dev_notifier(global_hl1280_data->tcpc_dev, &global_hl1280_data->psy_nb, TCP_NOTIFY_TYPE_USB);
	if (rc) {
		pr_err("%s: register_tcp_dev_notifier failed\n", __func__);
	}

	mutex_init(&et_priv->notification_lock);
	i2c_set_clientdata(i2c, et_priv);
	et_priv->hl1280_notifier.rwsem =
		(struct rw_semaphore)__RWSEM_INITIALIZER
		((et_priv->hl1280_notifier).rwsem);
	et_priv->hl1280_notifier.head = NULL;

	rc = sysfs_create_group(&i2c->dev.kobj, &hl1280_group);
	if (rc) {
		pr_err("%s: create attr error %d\n", __func__, rc);
	}

	hl1280_enable_switch_workqueue = create_singlethread_workqueue("enableSwitchQueue");
	INIT_WORK(&hl1280_enable_switch_work, hl1280_enable_switch_work_callback);
	if (!hl1280_enable_switch_workqueue) {
		rc = -1;
		pr_notice("%s create hl1280_enable_switch workqueue fail.\n", __func__);
		goto err_data;
	}

	pr_info("%s(), setup enable timer", __func__);
	timer_setup(&hl1280_enable_timer, hl1280_enable_switch_handler, (unsigned long)et_priv);

	hl1280_delay_init_workqueue = create_singlethread_workqueue("delayInitQueue");
	INIT_WORK(&hl1280_delay_init_work, hl1280_delay_init_work_callback);

	/* delay 2s to register tcpc event change, after accdet init done */
	timer_setup(&hl1280_delay_init_timer, delay_init_timer_callback, 0);
	mod_timer(&hl1280_delay_init_timer, jiffies + HL1280_DELAY_INIT_TIME);

	regmap_read(et_priv->regmap, HL1280_ID, &reg_val);
	if (reg_val == HL1280_ID_VALUE) {
		et_priv->hl1280 = true;
		pr_info("%s audio switch use hl1280 reg_val is %x", __func__, reg_val);
	} else {
		et_priv->hl1280 = false;
		pr_info("%s audio switch use dio4480 reg_val is %x", __func__, reg_val);
	}

#if IS_ENABLED(CONFIG_TC_CHARGER)
	et_priv->usbc_analog_switch_props.alias_name = "usbc_analog_switch";
	et_priv->usbc_analog_switch_dev = tran_device_register("usbc_analog_switch",
								et_priv->dev, et_priv,
								&usbc_analog_switch_ops,
								&et_priv->usbc_analog_switch_props);
	if (IS_ERR_OR_NULL(et_priv->usbc_analog_switch_dev)) {
		pr_err("%s: failed to register tran common class\n", __func__);
	}
#endif
	return 0;

err_supply:
err_data:
	devm_kfree(&i2c->dev, et_priv);
	return rc;
}


static int hl1280_remove(struct i2c_client *i2c)
{
	struct hl1280_priv *et_priv =
			(struct hl1280_priv *)i2c_get_clientdata(i2c);

	if (!et_priv)
		return -EINVAL;

	hl1280_usbc_update_settings(et_priv, 0x18, 0x98);
	mutex_destroy(&et_priv->notification_lock);
	dev_set_drvdata(&i2c->dev, NULL);

	return 0;
}

static const struct of_device_id hl1280_i2c_dt_match[] = {
	{
		.compatible = "mediatek,hl1280-audioswitch",
	},
	{}
};

static struct i2c_driver hl1280_i2c_driver = {
	.driver = {
		.name = HL1280_I2C_NAME,
		.of_match_table = hl1280_i2c_dt_match,
	},
	.probe = hl1280_probe,
	.remove = (void *)&hl1280_remove,
};

static int __init hl1280_init(void)
{
	int rc;
	rc = i2c_add_driver(&hl1280_i2c_driver);
	if (rc)
		pr_err("hl1280: Failed to register I2C driver: %d\n", rc);

	return rc;
}
late_initcall(hl1280_init);

static void __exit hl1280_exit(void)
{
	i2c_del_driver(&hl1280_i2c_driver);
}
module_exit(hl1280_exit);

MODULE_DESCRIPTION("HL1280 I2C driver");
MODULE_LICENSE("GPL v2");
