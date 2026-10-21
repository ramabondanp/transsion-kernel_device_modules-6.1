// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Transsion Inc.
 */

#include <linux/module.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "tc_common_class.h"

static struct class *tran_class;

static ssize_t tran_show_name(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct tran_device *tran_dev = to_tran_device(dev);

	return snprintf(buf, 20, "%s\n",
		       tran_dev->props.alias_name ?
		       tran_dev->props.alias_name : "anonymous");
}

static ssize_t  show_tran_wd_cmd(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 20, "null\n");
}

static ssize_t  store_tran_wd_cmd(struct device *dev,
				    struct device_attribute *attr, const char *buf, size_t size)
{
	struct tran_device *tran_dev = to_tran_device(dev);
	int ret;
	int cmd;
	union com_propval val = {0, };

	ret = kstrtoint(buf, 0, &cmd);
	if (ret < 0)
		return ret;

	pr_err("%s: cmd:%d\n", __func__, cmd);

	val.intval = cmd;
	tran_dev_set_prop(tran_dev, TRAN_PROP_WD_USER_CMD, &val);

	return size;
}


static void tran_device_release(struct device *dev)
{
	struct tran_device *tran_dev = to_tran_device(dev);

	kfree(tran_dev);
}

int tran_dev_set_prop(struct tran_device *tran_dev,
	enum tran_common_prop prop, const union com_propval *val)
{
	if (tran_dev != NULL && tran_dev->ops != NULL &&
	    tran_dev->ops->set_prop)
		return tran_dev->ops->set_prop(tran_dev, prop, val);

	return -ENOTSUPP;
}
EXPORT_SYMBOL(tran_dev_set_prop);

int tran_dev_get_prop(struct tran_device *tran_dev,
	enum tran_common_prop prop, union com_propval *val)
{
	if (tran_dev != NULL && tran_dev->ops != NULL &&
	    tran_dev->ops->get_prop)
		return tran_dev->ops->get_prop(tran_dev, prop, val);

	return -ENOTSUPP;
}
EXPORT_SYMBOL(tran_dev_get_prop);

static ssize_t battery_show_status(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct tran_device *batt_dev = to_tran_device(dev);
	union com_propval val = {0, };
	int batt_status;

	tran_dev_get_prop(batt_dev, TRAN_PROP_BATT_STATUS, &val);
	pr_err("%s: get %s status:%d\n", __func__, batt_dev->props.alias_name, val.intval);

	if (val.intval < 0)
		batt_status = 0;
	else
		batt_status = 1;

	return snprintf(buf, 20, "%s = %d\n",
		       batt_dev->props.alias_name ?
		       batt_dev->props.alias_name : "anonymous", batt_status);
}


static DEVICE_ATTR(name, 0444, tran_show_name, NULL);
static DEVICE_ATTR(tran_wd_cmd, 0644, show_tran_wd_cmd, store_tran_wd_cmd);
static DEVICE_ATTR(batt_status, 0640, battery_show_status, NULL);


static struct attribute *tran_class_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_tran_wd_cmd.attr,
	&dev_attr_batt_status.attr,
	NULL,
};

static const struct attribute_group tran_group = {
	.attrs = tran_class_attrs,
};

static const struct attribute_group *tran_groups[] = {
	&tran_group,
	NULL,
};

int tran_dev_notify(struct tran_device *tran_dev, int event, void *data)
{
	return srcu_notifier_call_chain(&tran_dev->evt_nh,
		event, data);
}
EXPORT_SYMBOL(tran_dev_notify);

int register_tran_device_notifier(struct tran_device *tran_dev,
				struct notifier_block *nb)
{
	int ret;

	ret = srcu_notifier_chain_register(&tran_dev->evt_nh, nb);
	return ret;
}
EXPORT_SYMBOL(register_tran_device_notifier);

int unregister_tran_device_notifier(struct tran_device *tran_dev,
				struct notifier_block *nb)
{
	return srcu_notifier_chain_unregister(&tran_dev->evt_nh, nb);
}
EXPORT_SYMBOL(unregister_tran_device_notifier);

/**
 * tran_device_register - create and register a new object of
 *   tran_device class.
 * @name: the name of the new object
 * @parent: a pointer to the parent device
 * @devdata: an optional pointer to be stored for private driver use.
 * The methods may retrieve it by using tran_get_data(tran_dev).
 * @ops: the transsion operations structure.
 *
 * Creates and registers new transsion device. Returns either an
 * ERR_PTR() or a pointer to the newly allocated device.
 */
struct tran_device *tran_device_register(const char *name,
		struct device *parent, void *devdata,
		const struct tran_ops *ops,
		const struct tran_properties *props)
{
	struct tran_device *tran_dev = NULL;
	static struct lock_class_key key;
	struct srcu_notifier_head *head = NULL;
	int rc;
	char *tran_name = NULL;

	pr_debug("%s: name=%s\n", __func__, name);
	tran_dev = kzalloc(sizeof(*tran_dev), GFP_KERNEL);
	if (!tran_dev)
		return ERR_PTR(-ENOMEM);

	head = &tran_dev->evt_nh;
	srcu_init_notifier_head(head);
	/* Rename srcu's lock to avoid LockProve warning */
	lockdep_init_map(&(&head->srcu)->dep_map, name, &key, 0);
	mutex_init(&tran_dev->ops_lock);
	tran_dev->dev.class = tran_class;
	tran_dev->dev.parent = parent;
	tran_dev->dev.release = tran_device_release;
	tran_name = kasprintf(GFP_KERNEL, "%s", name);
	dev_set_name(&tran_dev->dev, tran_name);
	dev_set_drvdata(&tran_dev->dev, devdata);
	kfree(tran_name);

	/* Copy properties */
	if (props) {
		memcpy(&tran_dev->props, props,
		       sizeof(struct tran_properties));
	}
	rc = device_register(&tran_dev->dev);
	if (rc) {
		kfree(tran_dev);
		return ERR_PTR(rc);
	}
	tran_dev->ops = ops;
	return tran_dev;
}
EXPORT_SYMBOL(tran_device_register);

/**
 * tran_device_unregister - unregisters a switching transsion device
 * object.
 * @tran_dev: the switching transsion device object to be unregistered
 * and freed.
 *
 * Unregisters a previously registered via tran_device_register object.
 */
void tran_device_unregister(struct tran_device *tran_dev)
{
	if (!tran_dev)
		return;

	mutex_lock(&tran_dev->ops_lock);
	tran_dev->ops = NULL;
	mutex_unlock(&tran_dev->ops_lock);
	device_unregister(&tran_dev->dev);
}
EXPORT_SYMBOL(tran_device_unregister);


static int tran_match_device_by_name(struct device *dev,
	const void *data)
{
	const char *name = data;

	return strcmp(dev_name(dev), name) == 0;
}

struct tran_device *tran_get_by_name(const char *name)
{
	struct device *dev = NULL;

	if (!name)
		return (struct tran_device *)NULL;
	dev = class_find_device(tran_class, NULL, name,
				tran_match_device_by_name);

	return dev ? to_tran_device(dev) : NULL;

}
EXPORT_SYMBOL(tran_get_by_name);

static void __exit tran_class_exit(void)
{
	class_destroy(tran_class);
}

static int __init tran_class_init(void)
{
	tran_class = class_create(THIS_MODULE, "tran_class");
	if (IS_ERR(tran_class)) {
		pr_notice("Unable to create transsion class; errno = %ld\n",
			PTR_ERR(tran_class));
		return PTR_ERR(tran_class);
	}
	tran_class->dev_groups = tran_groups;

	return 0;
}

subsys_initcall(tran_class_init);
module_exit(tran_class_exit);

MODULE_DESCRIPTION("Switching Charger Class Device");
MODULE_AUTHOR("Transsion Inc.");
MODULE_VERSION("1.0.0_G");
MODULE_LICENSE("GPL v2");
