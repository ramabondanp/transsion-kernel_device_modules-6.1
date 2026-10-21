// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Transsion Inc.
 */

#include <linux/module.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/slab.h>

#include "tc_adapter_class.h"

static struct class *tadapter_class;

static ssize_t name_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct tadapter_device *tadapter_dev = to_tadapter_device(dev);

	return snprintf(buf, 20, "%s\n",
		       tadapter_dev->props.alias_name ?
		       tadapter_dev->props.alias_name : "anonymous");
}

static void tadapter_device_release(struct device *dev)
{
	struct tadapter_device *tadapter_dev = to_tadapter_device(dev);

	kfree(tadapter_dev);
}

int tadapter_dev_get_property(struct tadapter_device *tadapter_dev,
	enum tadapter_property sta)
{
	if (tadapter_dev != NULL && tadapter_dev->ops != NULL &&
	    tadapter_dev->ops->get_property)
		return tadapter_dev->ops->get_property(tadapter_dev, sta);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tadapter_dev_get_property);

int tadapter_dev_get_status(struct tadapter_device *tadapter_dev,
	struct tadapter_status *sta)
{
	if (tadapter_dev != NULL && tadapter_dev->ops != NULL &&
	    tadapter_dev->ops->get_status)
		return tadapter_dev->ops->get_status(tadapter_dev, sta);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tadapter_dev_get_status);

int tadapter_dev_get_output(struct tadapter_device *tadapter_dev, int *mV, int *mA)
{
	if (tadapter_dev != NULL && tadapter_dev->ops != NULL &&
	    tadapter_dev->ops->get_output)
		return tadapter_dev->ops->get_output(tadapter_dev, mV, mA);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tadapter_dev_get_output);

int tadapter_dev_set_cap(struct tadapter_device *tadapter_dev,
	enum tadapter_cap_type type,
	int mV, int mA)
{
	if (tadapter_dev != NULL && tadapter_dev->ops != NULL &&
	    tadapter_dev->ops->set_cap)
		return tadapter_dev->ops->set_cap(tadapter_dev, type, mV, mA);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tadapter_dev_set_cap);

int tadapter_dev_get_cap(struct tadapter_device *tadapter_dev,
	enum tadapter_cap_type type,
	struct tadapter_power_cap *cap)
{
	if (tadapter_dev != NULL && tadapter_dev->ops != NULL &&
		tadapter_dev->ops->get_cap)
		return tadapter_dev->ops->get_cap(tadapter_dev, type, cap);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tadapter_dev_get_cap);

int tadapter_dev_plug_out_reset(struct tadapter_device *tadapter_dev)
{
	if (tadapter_dev != NULL && tadapter_dev->ops != NULL &&
	    tadapter_dev->ops->plug_out_reset)
		return tadapter_dev->ops->plug_out_reset(tadapter_dev);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tadapter_dev_plug_out_reset);

int tadapter_dev_get_det_done(struct tadapter_device *tadapter_dev, bool *det_done)
{
	if (tadapter_dev != NULL && tadapter_dev->ops != NULL &&
	    tadapter_dev->ops->get_det_done)
		return tadapter_dev->ops->get_det_done(tadapter_dev, det_done);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tadapter_dev_get_det_done);

int tadapter_dev_exit_protocol(struct tadapter_device *tadapter_dev)
{
	if (tadapter_dev != NULL && tadapter_dev->ops != NULL &&
	    tadapter_dev->ops->exit_protocol)
		return tadapter_dev->ops->exit_protocol(tadapter_dev);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tadapter_dev_exit_protocol);

int tadapter_dev_get_epp(struct tadapter_device *tadapter_dev)
{
	if (tadapter_dev != NULL && tadapter_dev->ops != NULL &&
	    tadapter_dev->ops->get_epp)
		return tadapter_dev->ops->get_epp(tadapter_dev);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tadapter_dev_get_epp);

int tadapter_dev_get_ce(struct tadapter_device *tadapter_dev)
{
	if (tadapter_dev != NULL && tadapter_dev->ops != NULL &&
	    tadapter_dev->ops->get_ce)
		return tadapter_dev->ops->get_ce(tadapter_dev);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tadapter_dev_get_ce);

int tadapter_dev_authentication(struct tadapter_device *tadapter_dev,
			       struct tadapter_auth_data *data)
{
	if (tadapter_dev != NULL && tadapter_dev->ops != NULL &&
	    tadapter_dev->ops->authentication)
		return tadapter_dev->ops->authentication(tadapter_dev, data);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tadapter_dev_authentication);

int tadapter_dev_is_cc(struct tadapter_device *tadapter_dev, bool *cc)
{
	if (tadapter_dev != NULL && tadapter_dev->ops != NULL &&
	    tadapter_dev->ops->is_cc)
		return tadapter_dev->ops->is_cc(tadapter_dev, cc);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tadapter_dev_is_cc);

int tadapter_dev_set_wdt(struct tadapter_device *tadapter_dev, u32 ms)
{
	if (tadapter_dev != NULL && tadapter_dev->ops != NULL &&
	    tadapter_dev->ops->set_wdt)
		return tadapter_dev->ops->set_wdt(tadapter_dev, ms);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tadapter_dev_set_wdt);

int tadapter_dev_enable_wdt(struct tadapter_device *tadapter_dev, bool en)
{
	if (tadapter_dev != NULL && tadapter_dev->ops != NULL &&
	    tadapter_dev->ops->enable_wdt)
		return tadapter_dev->ops->enable_wdt(tadapter_dev, en);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tadapter_dev_enable_wdt);

int tadapter_dev_sync_volt(struct tadapter_device *tadapter_dev, u32 mV)
{
	if (tadapter_dev != NULL && tadapter_dev->ops != NULL &&
	    tadapter_dev->ops->sync_volt)
		return tadapter_dev->ops->sync_volt(tadapter_dev, mV);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tadapter_dev_sync_volt);

int tadapter_dev_send_hardreset(struct tadapter_device *tadapter_dev)
{
	if (tadapter_dev != NULL && tadapter_dev->ops != NULL &&
	    tadapter_dev->ops->send_hardreset)
		return tadapter_dev->ops->send_hardreset(tadapter_dev);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tadapter_dev_send_hardreset);

int tadapter_dev_get_ta_fw(struct tadapter_device *tadapter_dev,
		                  u8 *code,u32 length)
{
         if (tadapter_dev != NULL && tadapter_dev->ops != NULL &&
            tadapter_dev->ops->get_ta_fw)
		return tadapter_dev->ops->get_ta_fw(tadapter_dev, code,length);
		 
         return -EOPNOTSUPP;	      
}
EXPORT_SYMBOL(tadapter_dev_get_ta_fw);

static DEVICE_ATTR_RO(name);

static struct attribute *tadapter_class_attrs[] = {
	&dev_attr_name.attr,
	NULL,
};

static const struct attribute_group tadapter_group = {
	.attrs = tadapter_class_attrs,
};

static const struct attribute_group *tadapter_groups[] = {
	&tadapter_group,
	NULL,
};

int register_tadapter_device_notifier(struct tadapter_device *tadapter_dev,
				struct notifier_block *nb)
{
	int ret = 0;

	if (!tadapter_dev)
		return -ENODEV;

	ret = srcu_notifier_chain_register(&tadapter_dev->evt_nh, nb);
	return ret;
}
EXPORT_SYMBOL(register_tadapter_device_notifier);

int unregister_tadapter_device_notifier(struct tadapter_device *tadapter_dev,
				struct notifier_block *nb)
{
	if (!tadapter_dev)
		return -ENODEV;

	return srcu_notifier_chain_unregister(&tadapter_dev->evt_nh, nb);
}
EXPORT_SYMBOL(unregister_tadapter_device_notifier);

/**
 * tadapter_device_register - create and register a new object of
 *   tadapter_device class.
 * @name: the name of the new object
 * @parent: a pointer to the parent device
 * @devdata: an optional pointer to be stored for private driver use.
 * The methods may retrieve it by using tadapter_get_data(tadapter_dev).
 * @ops: the charger operations structure.
 *
 * Creates and registers new charger device. Returns either an
 * ERR_PTR() or a pointer to the newly allocated device.
 */
struct tadapter_device *tadapter_device_register(const char *name,
		struct device *parent, void *devdata,
		const struct tadapter_ops *ops,
		const struct tadapter_properties *props)
{
	struct tadapter_device *tadapter_dev;
	static struct lock_class_key key;
	struct srcu_notifier_head *head;
	int rc;

	pr_notice("%s: name=%s\n", __func__, name);
	tadapter_dev = kzalloc(sizeof(*tadapter_dev), GFP_KERNEL);
	if (!tadapter_dev)
		return ERR_PTR(-ENOMEM);

	head = &tadapter_dev->evt_nh;
	srcu_init_notifier_head(head);
	/* Rename srcu's lock to avoid LockProve warning */
	lockdep_init_map(&(&head->srcu)->dep_map, name, &key, 0);
	mutex_init(&tadapter_dev->ops_lock);
	tadapter_dev->dev.class = tadapter_class;
	tadapter_dev->dev.parent = parent;
	tadapter_dev->dev.release = tadapter_device_release;
	dev_set_name(&tadapter_dev->dev, name);
	dev_set_drvdata(&tadapter_dev->dev, devdata);

	/* Copy properties */
	if (props) {
		memcpy(&tadapter_dev->props, props,
		       sizeof(struct tadapter_properties));
	}
	rc = device_register(&tadapter_dev->dev);
	if (rc) {
		kfree(tadapter_dev);
		return ERR_PTR(rc);
	}
	tadapter_dev->ops = ops;
	return tadapter_dev;
}
EXPORT_SYMBOL(tadapter_device_register);

/**
 * tadapter_device_unregister - unregisters a switching charger device
 * object.
 * @tadapter_dev: the switching charger device object to be unregistered
 * and freed.
 *
 * Unregisters a previously registered via tadapter_device_register object.
 */
void tadapter_device_unregister(struct tadapter_device *tadapter_dev)
{
	if (!tadapter_dev)
		return;

	mutex_lock(&tadapter_dev->ops_lock);
	tadapter_dev->ops = NULL;
	mutex_unlock(&tadapter_dev->ops_lock);
	device_unregister(&tadapter_dev->dev);
}
EXPORT_SYMBOL(tadapter_device_unregister);


static int tadapter_match_device_by_name(struct device *dev,
	const void *data)
{
	const char *name = data;

	return strcmp(dev_name(dev), name) == 0;
}

struct tadapter_device *get_tadapter_by_name(const char *name)
{
	struct device *dev;

	if (!name)
		return (struct tadapter_device *)NULL;
	dev = class_find_device(tadapter_class, NULL, name,
				tadapter_match_device_by_name);

	return dev ? to_tadapter_device(dev) : NULL;

}
EXPORT_SYMBOL(get_tadapter_by_name);

static void __exit tadapter_class_exit(void)
{
	class_destroy(tadapter_class);
}

static int __init tadapter_class_init(void)
{
	tadapter_class = class_create(THIS_MODULE, "Tc_Adapter");
	if (IS_ERR(tadapter_class)) {
		pr_notice("Unable to create Charging Adapter class; errno = %ld\n",
			PTR_ERR(tadapter_class));
		return PTR_ERR(tadapter_class);
	}
	tadapter_class->dev_groups = tadapter_groups;
	return 0;
}

module_init(tadapter_class_init);
module_exit(tadapter_class_exit);

MODULE_DESCRIPTION("Adapter Class Device");
MODULE_LICENSE("GPL");
