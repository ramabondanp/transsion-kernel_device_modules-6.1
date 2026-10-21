// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#define pr_fmt(fmt)     "[port_corr] %s: " fmt, __func__
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include "tc_charger.h"

struct port_corr_info {
	struct platform_device *pdev;
	struct device *dev;
	struct tran_device *tc_tcpc;
	struct tran_device *tc_lcd;
	struct tran_device *tc_phy_det;
	struct notifier_block port_corr_notifier;
	struct notifier_block phy_det_notifier;
	bool screen_on;
	bool port_corrosion_flag;
	bool phy_det_support;
};

static int port_corr_notifier_callback(struct notifier_block *nb,
                    unsigned long event, void *data)
{
	struct port_corr_info *info = container_of(nb,
			struct port_corr_info, port_corr_notifier);
	
	if (info->phy_det_support)
		return 0;

	switch (event) {
	case TRAN_DEV_NOTIFY_SCREEN_OFF:
		info->screen_on = false;
		break;
	case TRAN_DEV_NOTIFY_SCREEN_ON:
		info->screen_on = true;
		if (info->port_corrosion_flag) {
			tc_typec_change_role_postpone(info->tc_tcpc, TC_TYPEC_ROLE_TRY_SNK, true);
			pr_info("port corrosion switch role drp\n");
		}
		info->port_corrosion_flag = false;
		break;
	default:
		break;
	}
	return 0;
}

static int port_screen_notifier_init(struct port_corr_info *info)
{
	int ret = 0;
	info->tc_lcd = tran_get_by_name("tc_lcd");
	if (IS_ERR_OR_NULL(info->tc_lcd)) {
		pr_err("%s: get tc_lcd_dev fail\n", __func__);
		ret = -ENODEV;
		goto out;
	}
	info->port_corr_notifier.notifier_call = port_corr_notifier_callback;
	ret = register_tran_device_notifier(info->tc_lcd,
				&info->port_corr_notifier);
	if (ret != 0) {
		pr_err("register lcd notify failed, ret = %d\n", ret);
		goto out;
	}
out:
	return ret;
}

static int phy_det_notifier_callback(struct notifier_block *nb,
                    unsigned long event, void *data)
{
	struct port_corr_info *info = container_of(nb,
			struct port_corr_info, phy_det_notifier);
	
	if (!info->phy_det_support) {
		pr_info("not support phy_det\n");
		return 0;
	}
	switch (event) {
	case TRAN_DEV_NOTIFY_PHY_PLUG_IN:
		if (info->port_corrosion_flag) {
			tc_typec_change_role_postpone(info->tc_tcpc, TC_TYPEC_ROLE_TRY_SNK, true);
			info->port_corrosion_flag = false;
			pr_info("phy det port corrosion switch role drp\n");
		}
		break;
	case TRAN_DEV_NOTIFY_PHY_PLUG_OUT:
		if (!info->port_corrosion_flag) {
			tc_typec_change_role_postpone(info->tc_tcpc, TC_TYPEC_ROLE_SNK, true);
			info->port_corrosion_flag = true;
			pr_info("phy_det port corrosion switch role snk\n");
		}
		break;
	default:
		break;
	}
	return 0;
}

static int tc_phy_det_notifier_init(struct port_corr_info *info)
{
	int ret = 0;
	info->tc_phy_det = tran_get_by_name("phy_det");
	if (IS_ERR_OR_NULL(info->tc_phy_det)) {
		pr_err("%s get tc phy_det device fail!\n", __func__);
		return -ENODEV;
		goto out;
	}
	info->phy_det_notifier.notifier_call = phy_det_notifier_callback;
	ret = register_tran_device_notifier(info->tc_phy_det,
				&info->phy_det_notifier);
	if (ret != 0) {
		pr_err("register lcd notify failed, ret = %d\n", ret);
		goto out;
	}
	info->phy_det_support = true;
out:
	return ret;
}

static int port_corrosion_probe(struct platform_device *pdev)
{
	int ret;
	static int defer_cnt = 3;
	struct port_corr_info *info = NULL;
	pr_info("enter\n");
	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);
	info->pdev = pdev;
	info->tc_tcpc = tran_get_by_name("tc_tcpc");
	if (IS_ERR_OR_NULL(info->tc_tcpc) && defer_cnt-- > 0) {
		pr_err("%s get tc tcpc device fail!\n", __func__);
		return -EPROBE_DEFER;
	}
	ret = port_screen_notifier_init(info);
	if (ret != 0) {
		pr_err("%s register screen notify fail!\n", __func__);
	}

	ret = tc_phy_det_notifier_init(info);
	if (ret != 0) {
		pr_err("%s register phy_det notify fail!\n", __func__);
	}

	pr_info("end\n");
	return ret;
}

static int port_corrosion_prepare_suspend(struct device *dev)
{
	struct port_corr_info *info = dev_get_drvdata(dev);
	pr_info("%s:%d\n", __func__, info->port_corrosion_flag);
	if (info->phy_det_support)
		return 0;
	if (IS_ERR_OR_NULL(info->tc_tcpc))
		info->tc_tcpc = tran_get_by_name("tc_tcpc");
	if (!info->port_corrosion_flag) {
		tc_typec_change_role_postpone(info->tc_tcpc, TC_TYPEC_ROLE_SNK, true);
		info->port_corrosion_flag = true;
		pr_info("port corrosion switch role snk\n");
	}
	return 0;
}

static void port_corrosion_complete_resume(struct device *dev)
{
	pr_info("%s\n", __func__);
	return;
}

static const struct dev_pm_ops port_corrosion_pm_ops = {
	.prepare	= port_corrosion_prepare_suspend,
	.complete       = port_corrosion_complete_resume,
};

static int port_corrosion_remove(struct platform_device *pdev)
{
	return 0;
}

static void port_corrosion_shutdown(struct platform_device *dev)
{
	return;
}

static const struct of_device_id port_corrosion_of_match[] = {
	{.compatible = "transsion,port_corrosion",},
	{},
};

MODULE_DEVICE_TABLE(of, port_corrosion_of_match);
static struct platform_driver port_corrosion_platdrv = {
	.probe = port_corrosion_probe,
	.remove = port_corrosion_remove,
	.shutdown = port_corrosion_shutdown,
	.driver = {
		.name = "port_corrosion",
		.owner = THIS_MODULE,
		.pm = &port_corrosion_pm_ops,
		.of_match_table = port_corrosion_of_match,
	},
};

static int __init port_corrosion_init(void)
{
	return platform_driver_register(&port_corrosion_platdrv);
}

late_initcall(port_corrosion_init);

static void __exit port_corrosion_exit(void)
{
	platform_driver_unregister(&port_corrosion_platdrv);
}

module_exit(port_corrosion_exit);
MODULE_DESCRIPTION("TRANSSION PORT CORROSION PREVENTION");
MODULE_AUTHOR("Duck");
MODULE_VERSION("1.0.0_G");
MODULE_LICENSE("GPL");
