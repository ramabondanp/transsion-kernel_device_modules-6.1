// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Transsion Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/time.h>
#if IS_ENABLED(CONFIG_TC_CHARGER)
#include "tc_common_class.h"
#include "tc_misc_intf.h"
#endif
#include "tcpm.h"
#include "tcpci_core.h"

struct splitswitch_priv {
	int accdet_status;
	int cable_type;
	struct tcpc_device				*tcpc_dev;
	struct notifier_block			psy_nb;
	struct mutex					notification_lock;
	struct blocking_notifier_head	splitswitch_notifier;
};
struct splitswitch_priv g_splitswitch_data;

extern void accdet_eint_callback_wrapper(unsigned int plug_status);
extern void tran_splitswitch_set_gpio(int cmd);
extern void tran_splitswitch_accdet_set(int accdet_status, int cable_type);
extern u32 tran_get_auxadc(void);

enum tran_splitswitch_status {
	ANA_TYPEC_PLUG_OUT,
	ANA_TYPEC_PLUG_IN,
	ANA_TYPEC_ACCDET_CHECK_0,
	ANA_TYPEC_ACCDET_CHECK_1,
	ANA_TYPEC_NO_MIC,
};

enum accdet_report_state {
	NO_DEVICE =			0,
	HEADSET_MIC =		1,
	HEADSET_NO_MIC =	2,
	HEADSET_FIVE_POLE =	3,
	LINE_OUT_DEVICE =	4,
};

enum accdet_status {
	PLUG_OUT =		0,
	MIC_BIAS =		1,
	HOOK_SWITCH =	2,
	BI_MIC_BIAS =	3,
	LINE_OUT =		4,
	STAND_BY =		5
};

#if IS_ENABLED(CONFIG_USB_SWITCH_DIO1520)
bool tran_dio1520_accdet_check(int vol)
{
	u32 cur_accdet = 0;

	vol ? tran_splitswitch_set_gpio(ANA_TYPEC_ACCDET_CHECK_1)
		: tran_splitswitch_set_gpio(ANA_TYPEC_ACCDET_CHECK_0);
	mdelay(2);
	cur_accdet = tran_get_auxadc();
	pr_info("%s() vol=%d, cur_accdet = %d", __func__, vol, cur_accdet);
	if ((cur_accdet > 400) && (cur_accdet < 1700)) {
		g_splitswitch_data.accdet_status = MIC_BIAS;
		g_splitswitch_data.cable_type = HEADSET_MIC;
		return true;
	} else if (cur_accdet >= 1900) {
		g_splitswitch_data.accdet_status = PLUG_OUT;
		g_splitswitch_data.cable_type = NO_DEVICE;
		tran_splitswitch_set_gpio(ANA_TYPEC_PLUG_OUT);
		return true;
	}

	if (vol) {
		g_splitswitch_data.cable_type = HEADSET_NO_MIC;
		g_splitswitch_data.accdet_status = HOOK_SWITCH;
		tran_splitswitch_set_gpio(ANA_TYPEC_NO_MIC);
	}

	return false;
}
#endif

void tran_splitswitch_accdet_check(void)
{
#if IS_ENABLED(CONFIG_USB_SWITCH_DIO1520)
	tran_dio1520_accdet_check(0)?:tran_dio1520_accdet_check(1);
#endif
/* add check for new switch */

}

static void splitswitch_set_headset_plug_in(unsigned int plug_status)
{
	tran_splitswitch_set_gpio(plug_status);
	accdet_eint_callback_wrapper(plug_status);
	msleep(100);
	if (plug_status) {
		tran_splitswitch_accdet_check();
		tran_splitswitch_accdet_set(g_splitswitch_data.accdet_status, g_splitswitch_data.cable_type);
	}
}

static int splitswitch_tcpc_event_changed(struct notifier_block *nb,
				      unsigned long evt, void *ptr)
{
	struct tcp_notify *noti = ptr;
	if (NULL == noti) {
		pr_err("%s: data is NULL. \n", __func__);
		return NOTIFY_DONE;
	}

	switch (evt) {
	case TCP_NOTIFY_TYPEC_STATE:
		if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
			noti->typec_state.new_state == TYPEC_ATTACHED_AUDIO) {
			/* Audio Plug in */
			pr_info("%s() Audio Plug In \n", __func__);

			splitswitch_set_headset_plug_in(1);
		} else if (noti->typec_state.old_state == TYPEC_ATTACHED_AUDIO &&
			noti->typec_state.new_state == TYPEC_UNATTACHED) {
			/* Audio Plug out */
			pr_info("%s() Audio Plug Out \n", __func__);

			splitswitch_set_headset_plug_in(0);
		}
		break;
	}

	return NOTIFY_OK;
}

static int splitswitch_probe(struct platform_device *pdev)
{
	int rc;

    pr_info("%s() enter", __func__);

	g_splitswitch_data.tcpc_dev = tcpc_dev_get_by_name("type_c_port0");
    if (!g_splitswitch_data.tcpc_dev) {
		rc = -EPROBE_DEFER;
		pr_err("%s get tcpc device type_c_port0 fail \n", __func__);
		goto err_supply;
    }
	g_splitswitch_data.psy_nb.notifier_call = splitswitch_tcpc_event_changed;
	g_splitswitch_data.psy_nb.priority = 0;
	rc = register_tcp_dev_notifier(g_splitswitch_data.tcpc_dev, &g_splitswitch_data.psy_nb, TCP_NOTIFY_TYPE_USB);
	if (rc) {
		pr_err("%s: register_tcp_dev_notifier failed\n", __func__);
		goto err_supply;
	}

	mutex_init(&g_splitswitch_data.notification_lock);

	g_splitswitch_data.splitswitch_notifier.rwsem =
		(struct rw_semaphore)__RWSEM_INITIALIZER
		((g_splitswitch_data.splitswitch_notifier).rwsem);
	g_splitswitch_data.splitswitch_notifier.head = NULL;

	return 0;

err_supply:
	unregister_tcp_dev_notifier(g_splitswitch_data.tcpc_dev, &g_splitswitch_data.psy_nb, TCP_NOTIFY_TYPE_USB);
	return rc;
}

static int splitswitch_remove(struct platform_device *pdev)
{
	if (!pdev)
		return 0; //-EINVAL;

	mutex_destroy(&g_splitswitch_data.notification_lock);

	return 0;
}

static const struct of_device_id splitswitch_match[] = {
	{ .compatible = "transsion,sliptswitch-audioswitch", },
	{ },
};

static struct platform_driver splitswitch_driver = {
	.probe = splitswitch_probe,
	.remove = splitswitch_remove,
	.driver = {
		.name = "splitswitch",
		.owner = THIS_MODULE,
		.of_match_table = splitswitch_match,
	},
};

static int __init splitswitch_init(void)
{
	int ret = 0;
	pr_info("splitswitch_driver driver register error %d", ret);
	ret = platform_driver_register(&splitswitch_driver);
	if (ret)
		pr_info("splitswitch_driver driver register error %d", ret);
	return ret;
}

static void __exit splitswitch_exit(void)
{
	platform_driver_unregister(&splitswitch_driver);
}

module_init(splitswitch_init);
module_exit(splitswitch_exit);

MODULE_DESCRIPTION("splitswitch Driver");
MODULE_AUTHOR("transsion.com");
MODULE_LICENSE("GPL v2");
