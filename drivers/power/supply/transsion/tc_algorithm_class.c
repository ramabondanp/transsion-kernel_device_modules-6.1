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
#include "tc_algorithm_class.h"


static struct class *tc_algorithm_class;

static const char *const tchg_alg_notify_evt_name[EVT_MAX] = {
	[EVT_PLUG_IN] = "EVT_PLUG_IN",
	[EVT_PLUG_OUT] = "EVT_PLUG_OUT",
	[EVT_FULL] = "EVT_FULL",
	[EVT_RECHARGE] = "EVT_RECHARGE",
	[EVT_DETACH] = "EVT_DETACH",
	[EVT_HARDRESET] = "EVT_HARDRESET",
	[EVT_VBUSOVP] = "EVT_VBUSOVP",
	[EVT_IBUSOCP] = "EVT_IBUSOCP",
	[EVT_IBUSUCP_FALL] = "EVT_IBUSUCP_FALL",
	[EVT_VBATOVP] = "EVT_VBATOVP",
	[EVT_IBATOCP] = "EVT_IBATOCP",
	[EVT_VOUTOVP] = "EVT_VOUTOVP",
	[EVT_VDROVP] = "EVT_VDROVP",
	[EVT_VBATOVP_ALARM] = "EVT_VBATOVP_ALARM",
	[EVT_VBUSOVP_ALARM] = "EVT_VBUSOVP_ALARM",
	[EVT_WLS_FULL] = "EVT_WLS_FULL",
	[EVT_ALGO_STOP] = "EVT_ALGO_STOP",
};

static void tchg_alg_device_release(struct device *dev)
{
	struct tchg_alg_device *tchg_dev = to_tchg_alg_dev(dev);

	kfree(tchg_dev);
}

int tchg_alg_init_algo(struct tchg_alg_device *alg_dev)
{
	if (alg_dev != NULL && alg_dev->ops != NULL &&
	    alg_dev->ops->init_algo)
		return alg_dev->ops->init_algo(alg_dev);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tchg_alg_init_algo);

int tchg_alg_is_algo_ready(struct tchg_alg_device *alg_dev)
{
	if (alg_dev != NULL && alg_dev->ops != NULL &&
	    alg_dev->ops->is_algo_ready)
		return alg_dev->ops->is_algo_ready(alg_dev);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tchg_alg_is_algo_ready);

int tchg_alg_is_algo_running(struct tchg_alg_device *alg_dev)
{
	if (alg_dev != NULL && alg_dev->ops != NULL &&
	    alg_dev->ops->is_algo_running)
		return alg_dev->ops->is_algo_running(alg_dev);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tchg_alg_is_algo_running);

int tchg_alg_start_algo(struct tchg_alg_device *alg_dev)
{
	if (alg_dev != NULL && alg_dev->ops != NULL &&
	    alg_dev->ops->start_algo)
		return alg_dev->ops->start_algo(alg_dev);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tchg_alg_start_algo);

int tchg_alg_get_prop(struct tchg_alg_device *alg_dev,
	enum tchg_alg_props s, int *value)
{
	if (alg_dev != NULL && alg_dev->ops != NULL &&
	    alg_dev->ops->get_prop)
		return alg_dev->ops->get_prop(alg_dev, s, value);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tchg_alg_get_prop);

int tchg_alg_set_prop(struct tchg_alg_device *alg_dev,
	enum tchg_alg_props s, int value)
{
	if (alg_dev != NULL && alg_dev->ops != NULL &&
	    alg_dev->ops->set_prop)
		return alg_dev->ops->set_prop(alg_dev, s, value);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tchg_alg_set_prop);

int tchg_alg_stop_algo(struct tchg_alg_device *alg_dev)
{
	if (alg_dev != NULL && alg_dev->ops != NULL &&
	    alg_dev->ops->stop_algo)
		return alg_dev->ops->stop_algo(alg_dev);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tchg_alg_stop_algo);

int tchg_alg_notifier_call(struct tchg_alg_device *alg_dev,
	struct tchg_alg_notify *notify)
{
	if (alg_dev != NULL && alg_dev->ops != NULL &&
	    alg_dev->ops->notifier_call)
		return alg_dev->ops->notifier_call(alg_dev, notify);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tchg_alg_notifier_call);

int tchg_alg_set_current_limit(struct tchg_alg_device *alg_dev,
	struct tchg_limit_setting *setting)
{
	pr_notice("%s\n", __func__);
	if (alg_dev != NULL && alg_dev->ops != NULL &&
	    alg_dev->ops->set_current_limit)
		return alg_dev->ops->set_current_limit(alg_dev, setting);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tchg_alg_set_current_limit);

int tchg_alg_plugout_reset(struct tchg_alg_device *alg_dev)
{
	pr_notice("%s\n", __func__);
	if (alg_dev != NULL && alg_dev->ops != NULL &&
	    alg_dev->ops->plugout_reset)
		return alg_dev->ops->plugout_reset(alg_dev);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tchg_alg_plugout_reset);

char *tchg_alg_state_to_str(int state)
{
	switch (state) {
	case ALG_INIT_FAIL:
		return "ALG_INIT_FAIL";
	case ALG_TA_CHECKING:
		return "ALG_TA_CHECKING";
	case ALG_TA_NOT_SUPPORT:
		return "ALG_TA_NOT_SUPPORT";
	case ALG_NOT_READY:
		return "ALG_NOT_READY";
	case ALG_READY:
		return "ALG_READY";
	case ALG_RUNNING:
		return "ALG_RUNNING";
	case ALG_DONE:
		return "ALG_DONE";
	default:
		break;
	}
	pr_notice("%s unknown state:%d\n", __func__
		, state);
	return "tchg_alg_state_UNKNOWN";
}
EXPORT_SYMBOL(tchg_alg_state_to_str);

extern const char *const
tchg_alg_notify_evt_tostring(enum tchg_alg_notifier_events evt)
{
	if ((int)evt >= (int)EVT_MAX || (int)evt < 0) {
		pr_notice("%s: tchg_algo error\n", __func__);
		return "tchg_algo_error";
	}
	return tchg_alg_notify_evt_name[evt];

}
EXPORT_SYMBOL(tchg_alg_notify_evt_tostring);

int register_tchg_alg_notifier(struct tchg_alg_device *alg_dev,
				struct notifier_block *nb)
{
	int ret;

	ret = srcu_notifier_chain_register(&alg_dev->evt_nh, nb);
	return ret;
}
EXPORT_SYMBOL(register_tchg_alg_notifier);

int unregister_tchg_alg_notifier(struct tchg_alg_device *alg_dev,
				struct notifier_block *nb)
{
	return srcu_notifier_chain_unregister(&alg_dev->evt_nh, nb);
}
EXPORT_SYMBOL(unregister_tchg_alg_notifier);

/**
 * tchg_alg_device_register - create and register a new object of
 *   charger_device class.
 * @name: the name of the new object
 * @parent: a pointer to the parent device
 * @devdata: an optional pointer to be stored for private driver use.
 * The methods may retrieve it by using charger_get_data(charger_dev).
 * @ops: the charger operations structure.
 *
 * Creates and registers new charger device. Returns either an
 * ERR_PTR() or a pointer to the newly allocated device.
 */
struct tchg_alg_device *tchg_alg_device_register(const char *name,
		struct device *parent, void *devdata,
		const struct tchg_alg_ops *ops,
		const struct tchg_alg_properties *props)
{
	struct tchg_alg_device *tchg_dev;
	static struct lock_class_key key;
	struct srcu_notifier_head *head;
	int rc;
	char *algo_name = NULL;

	pr_debug("%s: name=%s\n", __func__, name);
	tchg_dev = kzalloc(sizeof(*tchg_dev), GFP_KERNEL);
	if (!tchg_dev)
		return ERR_PTR(-ENOMEM);

	head = &tchg_dev->evt_nh;
	srcu_init_notifier_head(head);
	/* Rename srcu's lock to avoid LockProve warning */
	lockdep_init_map(&(&head->srcu)->dep_map, name, &key, 0);
	mutex_init(&tchg_dev->ops_lock);
	tchg_dev->dev.class = tc_algorithm_class;
	tchg_dev->dev.parent = parent;
	tchg_dev->dev.release = tchg_alg_device_release;
	algo_name = kasprintf(GFP_KERNEL, "%s", name);
	dev_set_name(&tchg_dev->dev, algo_name);
	dev_set_drvdata(&tchg_dev->dev, devdata);
	kfree(algo_name);

	/* Copy properties */
	if (props) {
		memcpy(&tchg_dev->props, props,
		       sizeof(struct tchg_alg_properties));
	}
	rc = device_register(&tchg_dev->dev);
	if (rc) {
		kfree(tchg_dev);
		return ERR_PTR(rc);
	}
	tchg_dev->ops = ops;
	return tchg_dev;
}
EXPORT_SYMBOL(tchg_alg_device_register);

/**
 * tchg_alg_device_unregister - unregisters a switching charger device
 * object.
 * @charger_dev: the switching charger device object to be unregistered
 * and freed.
 *
 * Unregisters a previously registered via charger_device_register object.
 */
void tchg_alg_device_unregister(struct tchg_alg_device *tchg_dev)
{
	if (!tchg_dev)
		return;

	mutex_lock(&tchg_dev->ops_lock);
	tchg_dev->ops = NULL;
	mutex_unlock(&tchg_dev->ops_lock);
	device_unregister(&tchg_dev->dev);
}
EXPORT_SYMBOL(tchg_alg_device_unregister);

static int tchg_alg_match_device_by_name(struct device *dev,
	const void *data)
{
	const char *name = data;

	return strcmp(dev_name(dev), name) == 0;
}

struct tchg_alg_device *get_tchg_alg_by_name(const char *name)
{
	struct device *dev;

	if (!name)
		return (struct tchg_alg_device *)NULL;
	dev = class_find_device(tc_algorithm_class, NULL, name,
				tchg_alg_match_device_by_name);

	return dev ? to_tchg_alg_dev(dev) : NULL;

}
EXPORT_SYMBOL(get_tchg_alg_by_name);

static void __exit tc_algorithm_class_exit(void)
{
	class_destroy(tc_algorithm_class);
}

static int __init tc_algorithm_class_init(void)
{
	tc_algorithm_class =
		class_create(THIS_MODULE, "Tc Algorithm");
	if (IS_ERR(tc_algorithm_class)) {
		pr_notice("Unable to create charger algorithm class; errno = %ld\n",
			PTR_ERR(tc_algorithm_class));
		return PTR_ERR(tc_algorithm_class);
	}
	//charger_algorithm_class->dev_groups = adapter_groups;
	//charger_algorithm_class->suspend = charger_algorithm_suspend;
	//charger_algorithm_class->resume = charger_algorithm_resume;
	return 0;
}

#if IS_BUILTIN(CONFIG_TC_CHARGER)
subsys_initcall(tc_algorithm_class_init);
#else
module_init(tc_algorithm_class_init);
#endif
module_exit(tc_algorithm_class_exit);

MODULE_DESCRIPTION("Charger Algorithm Class Device");
MODULE_VERSION("1.0.0");
MODULE_LICENSE("GPL");
