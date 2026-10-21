// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Transsion Inc.
 */

#define pr_fmt(fmt)     "[TC_TCPC] %s: " fmt, __func__
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/ktime.h>
#include <tcpm.h>
#include "tc_tcpc.h"
#include "tc_charger_class.h"

struct tc_tcpc {
	struct device *dev;
	struct platform_device *pdev;
	struct tcpc_device *plat_tcpc;
	struct charger_device *wls_dev;
	struct tran_device *tcpc_dev;
	struct tran_properties tcpc_props;
	struct power_supply *bat_psy;
	struct iio_channel *chan_vbus;
	struct tc_tcpc_noti tcpc_noti;
	struct notifier_block pd_nb;
	
	int cc1_status;
	int cc2_status;

	bool is_usb_dock;
	bool usb_docking_judge;
};

struct srcu_notifier_head *tcpc_notifier_head = NULL;

static const char *const tc_pd_type_name[TC_PD_CONNECT_MAX] = {
	[TC_PD_CONNECT_NONE] = "TC_PD_CONNECT_NONE",
	[TC_PD_CONNECT_PE_READY_SNK] = "TC_PD_CONNECT_PE_READY_SNK",
	[TC_PD_CONNECT_PE_READY_SNK_PD30] = "TC_PD_CONNECT_PE_READY_SNK_PD30",
	[TC_PD_CONNECT_PE_READY_SNK_APDO] = "TC_PD_CONNECT_PE_READY_SNK_APDO",
	[TC_PD_CONNECT_TYPEC_ONLY_SNK] = "TC_PD_CONNECT_TYPEC_ONLY_SNK",
	[TC_PD_CONNECT_TIMEOUT] = "TC_PD_CONNECT_TIMEOUT",
};

const char *const tc_pd_type_tostring(int type)
{
	if (type >= (int)TC_PD_CONNECT_MAX || type < 0) {
		pr_notice("%s: pd type error\n", __func__);
		return "PD_TYPE_ERROR";
	}
	return tc_pd_type_name[type];

}
EXPORT_SYMBOL(tc_pd_type_tostring);

int register_tc_tcpc_notifier(struct notifier_block *nb)
{
	int ret;

	if (IS_ERR_OR_NULL(tcpc_notifier_head))
		return -ENODEV;

	ret = srcu_notifier_chain_register(tcpc_notifier_head, nb);
	return ret;
}
EXPORT_SYMBOL(register_tc_tcpc_notifier);

int unregister_tc_tcpc_notifier(struct notifier_block *nb)
{
	if (IS_ERR_OR_NULL(tcpc_notifier_head))
		return -ENODEV;

	return srcu_notifier_chain_unregister(tcpc_notifier_head, nb);
}
EXPORT_SYMBOL(unregister_tc_tcpc_notifier);

int tc_tcpc_notify(int event, struct tc_tcpc_noti *noti)
{
	if (IS_ERR_OR_NULL(tcpc_notifier_head))
		return -ENODEV;

	return srcu_notifier_call_chain(tcpc_notifier_head,
		event, noti);
}
EXPORT_SYMBOL(tc_tcpc_notify);

bool tc_tcpc_detect_dual_rp_cable(void)
{
	struct tcpc_device *tcpc_dev = NULL;
	uint8_t cc1, cc2;
	bool detected = false;

	tcpc_dev = tcpc_dev_get_by_name("type_c_port0");
	if (!tcpc_dev) {
		pr_info("[TYPEC] get device type_c_port0 fail\n");
		goto out;
	}

	tcpm_inquire_remote_cc(tcpc_dev, &cc1, &cc2, false);
	pr_info("[TYPEC] cc1=%d, cc2=%d\n", cc1, cc2);

	if ((cc1 == TYPEC_CC_VOLT_SNK_DFT || cc1 == TYPEC_CC_VOLT_SNK_1_5 ||
	       	cc1 == TYPEC_CC_VOLT_SNK_3_0) && (cc2 == TYPEC_CC_VOLT_SNK_DFT ||
		cc2 == TYPEC_CC_VOLT_SNK_1_5 || cc2 == TYPEC_CC_VOLT_SNK_3_0)) {

		detected = true;
	}

out:
	return detected;

}
EXPORT_SYMBOL(tc_tcpc_detect_dual_rp_cable);

bool tc_tcpc_detect_rp_ra_cable(void)
{
	struct tcpc_device *tcpc_dev = NULL;
	int cc1 = -1;
	int cc2 = -1;
	bool detected = false;

	tcpc_dev = tcpc_dev_get_by_name("type_c_port0");
	if (!tcpc_dev) {
		pr_info("[TYPEC] get device type_c_port0 fail\n");
		goto out;
	}

	tcpm_tran_inquire_remote_cc(tcpc_dev, &cc1, &cc2);

	pr_info("[TYPEC] cc1=%d, cc2=%d\n", cc1, cc2);

	if ((cc2 == TYPEC_CC_VOLT_RA && (cc1 == TYPEC_CC_VOLT_SNK_DFT ||
		cc1 == TYPEC_CC_VOLT_SNK_1_5 || cc1 == TYPEC_CC_VOLT_SNK_3_0)) ||
		(cc1 == TYPEC_CC_VOLT_RA && (cc2 == TYPEC_CC_VOLT_SNK_DFT ||
		cc2 == TYPEC_CC_VOLT_SNK_1_5 || cc2 == TYPEC_CC_VOLT_SNK_3_0))) {

		detected = true;
	}

out:
	return detected;

}
EXPORT_SYMBOL(tc_tcpc_detect_rp_ra_cable);

bool wireless_state_check(struct tc_tcpc *tc_tcpc)
{
	int ret;
	bool boost = false;
	int count = 20;

	tc_tcpc->wls_dev = get_charger_by_name("wireless_manager");
	if (IS_ERR_OR_NULL(tc_tcpc->wls_dev)){
		pr_err("%s: wls_dev get fail",__func__);
		return boost;
	}
			
	/*Get wirless reverse charge status*/
	do {
		ret = wireless_manager_reverse_state(tc_tcpc->wls_dev, &boost);
		if (ret) {
			pr_err("%s:get wireless boost status fail\n",__func__);
			return boost;
		}
		if (!boost)
			return false;
		msleep(50);
	} while (count--);

	return true;
}

int tc_typec_change_role_postpone(
	struct tran_device *dev, uint8_t typec_role, bool postpone)
{
	struct tc_tcpc *info = tran_get_data(dev);

	return tcpm_typec_change_role_postpone(info->plat_tcpc, typec_role, postpone);
}
EXPORT_SYMBOL(tc_typec_change_role_postpone);

bool tc_maybe_dock(void)
{
	static struct tran_device *tcpc_dev;
	static struct tc_tcpc *info;

	if (IS_ERR_OR_NULL(tcpc_dev)) {
		tcpc_dev = tran_get_by_name("tc_tcpc");
		if (IS_ERR_OR_NULL(tcpc_dev))
			return false;
	}
	if (IS_ERR_OR_NULL(info)) {
		info = tran_get_data(tcpc_dev);
		if (IS_ERR_OR_NULL(info))
			return false;
	}

	if (!info->usb_docking_judge)
		return false;

	return info->is_usb_dock;
}
EXPORT_SYMBOL(tc_maybe_dock);

static int tcpc_get_property(struct tran_device *dev,
				enum tran_common_prop prop,
				union com_propval *val)
{
	int ret = 0;
	uint8_t cc1, cc2;
	struct tc_tcpc *info = tran_get_data(dev);

	cc1 = info->tcpc_noti.cc1_status;
	cc2 = info->tcpc_noti.cc2_status;

	switch (prop) {
	case TRAN_PROP_TYPE_CC1:
		val->intval = cc1;
		break;
	case TRAN_PROP_TYPE_CC2:
		val->intval = cc2;
		break;
	case TRAN_PROP_TYPE_CC_SMT:
		if (cc1 == TYPEC_CC_VOLT_SNK_DFT
				|| cc1 == TYPEC_CC_VOLT_SNK_1_5
				|| cc1 == TYPEC_CC_VOLT_SNK_3_0
				|| cc2 == TYPEC_CC_VOLT_SNK_DFT
				|| cc2 == TYPEC_CC_VOLT_SNK_1_5
				|| cc2 == TYPEC_CC_VOLT_SNK_3_0)
			val->intval = 1;
		else
			val->intval = 0;
 		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int tcpc_set_property(struct tran_device *dev,
					   enum tran_common_prop prop,
					   const union com_propval *val)
{
	int ret = 0;
	/*struct tc_tcpc *info = tran_get_data(dev);*/ 

	switch (prop) {
	default:
		ret = -EINVAL;
	}

	return ret;
}

static struct tran_ops tc_tcpc_ops = {
	.get_prop = tcpc_get_property,
	.set_prop = tcpc_set_property,
};

static int tc_tcpc_prop_init(struct tc_tcpc *info)
{

        info->tcpc_props.alias_name = "tc_tcpc";
	info->tcpc_dev = tran_device_register("tc_tcpc",
						info->dev, info,
						&tc_tcpc_ops,
						&info->tcpc_props);
	if (IS_ERR_OR_NULL(info->tcpc_dev))
		return -ENODEV;

	return 0;
}


static int tc_pd_tcp_notifier_call(struct notifier_block *nb,
				unsigned long event, void *data)
{
	struct tcp_notify *noti = data;
	struct tc_tcpc *info = (struct tc_tcpc *)container_of(nb,
		struct tc_tcpc, pd_nb);
	static ktime_t usb_plug_in_time = 0;
	static ktime_t otg_plug_out_time = 0;
	signed long long time_diff;
	int ret = 0, sink_mv, sink_ma;
	bool wireless_state = false;
	
	switch (event) {
	case TCP_NOTIFY_SINK_VBUS:
		sink_mv = noti->vbus_state.mv;
		sink_ma = noti->vbus_state.ma;
		memcpy(&info->tcpc_noti.vbus_state, &noti->vbus_state, sizeof(struct tc_tcpc_vbus_state));
		ret = tc_tcpc_notify(TC_TYPEC_SNK_VBUS, &info->tcpc_noti);
		pr_info("%s: sink vbus %dmV %dmA type(0x%02x)\n", __func__,
			sink_mv, sink_ma, noti->vbus_state.type);
		break;
	case TCP_NOTIFY_SOURCE_VBUS:
		memcpy(&info->tcpc_noti.vbus_state,
			&noti->vbus_state, sizeof(struct tc_tcpc_vbus_state));
		pr_info("source vbus = %dmv, ibus = %dma\n",
				 noti->vbus_state.mv, noti->vbus_state.ma);
		
		tc_tcpc_notify(TC_TYPEC_SRC_VBUS, &info->tcpc_noti);
		break;
	case TCP_NOTIFY_PD_STATE:
		switch (noti->pd_state.connected) {
		case PD_CONNECT_NONE:
			info->tcpc_noti.pd_type = TC_PD_CONNECT_NONE;
			ret = tc_tcpc_notify(TC_PD_TYPE, &info->tcpc_noti);
			break;

		case PD_CONNECT_TYPEC_ONLY_SNK_DFT:
			/* fall-through */
		case PD_CONNECT_TYPEC_ONLY_SNK:
			info->tcpc_noti.pd_type = TC_PD_CONNECT_TYPEC_ONLY_SNK;
			ret = tc_tcpc_notify(TC_PD_TYPE, &info->tcpc_noti);
			break;
		case PD_CONNECT_PE_READY_SNK:
			info->tcpc_noti.pd_type = TC_PD_CONNECT_PE_READY_SNK;
			ret = tc_tcpc_notify(TC_PD_TYPE, &info->tcpc_noti);
			break;

		case PD_CONNECT_PE_READY_SNK_PD30:
			info->tcpc_noti.pd_type = TC_PD_CONNECT_PE_READY_SNK_PD30;
			ret = tc_tcpc_notify(TC_PD_TYPE, &info->tcpc_noti);
			break;

		case PD_CONNECT_PE_READY_SRC_PD30:
			info->tcpc_noti.pd_type = TC_PD_CONNECT_PE_READY_SRC_PD30;
			ret = tc_tcpc_notify(TC_PD_TYPE, &info->tcpc_noti);
			break;

		case PD_CONNECT_PE_READY_SNK_APDO:
			info->tcpc_noti.pd_type = TC_PD_CONNECT_PE_READY_SNK_APDO;
			ret = tc_tcpc_notify(TC_PD_TYPE, &info->tcpc_noti);
			break;

		case PD_CONNECT_HARD_RESET:
			info->tcpc_noti.pd_type = TC_PD_CONNECT_NONE;
			ret = tc_tcpc_notify(TC_PD_CONNECT_HARD_RESET, &info->tcpc_noti);
			break;

		};
		break;
	case TCP_NOTIFY_TYPEC_STATE:
		if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
		    (noti->typec_state.new_state == TYPEC_ATTACHED_SNK ||
		    noti->typec_state.new_state == TYPEC_ATTACHED_CUSTOM_SRC ||
		    noti->typec_state.new_state == TYPEC_ATTACHED_NORP_SRC ||
		    noti->typec_state.new_state == TYPEC_ATTACHED_DBGACC_SNK)) {

			tcpm_tran_inquire_remote_cc(info->plat_tcpc,
				&info->tcpc_noti.cc1_status, &info->tcpc_noti.cc2_status);
			wireless_state = wireless_state_check(info);
			if(!wireless_state){
				ret = tc_tcpc_notify(TC_TYPEC_USB_PLUG_IN, &info->tcpc_noti);
				pr_info("USB Plug in, pol = %d\n",
							noti->typec_state.polarity);
			}else{
				pr_info("wireless boost is open\n");
				break;
			}

			if (info->usb_docking_judge && ktime_to_ms(otg_plug_out_time) > 0) {
				usb_plug_in_time = ktime_get();
				time_diff = ktime_to_ms(ktime_sub(usb_plug_in_time, otg_plug_out_time));
				if (time_diff < 300) {
					pr_info("typec docking!!\n");
					info->is_usb_dock = true;
				} else
					info->is_usb_dock = false;
				otg_plug_out_time = 0;
				usb_plug_in_time = 0;
			}
		} else if ((noti->typec_state.old_state == TYPEC_ATTACHED_SNK ||
		    noti->typec_state.old_state == TYPEC_ATTACHED_CUSTOM_SRC ||
		    noti->typec_state.old_state == TYPEC_ATTACHED_NORP_SRC ||
		    noti->typec_state.old_state == TYPEC_ATTACHED_DBGACC_SNK ||
		    noti->typec_state.old_state == TYPEC_ATTACHED_AUDIO) &&
		    noti->typec_state.new_state == TYPEC_UNATTACHED) {

			pr_info("USB Plug out\n");
			ret = tc_tcpc_notify(TC_TYPEC_USB_PLUG_OUT, &info->tcpc_noti);
			info->is_usb_dock = false;
		} else if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
			noti->typec_state.new_state == TYPEC_ATTACHED_SRC) {

			pr_info("OTG Plug in\n");
			ret = tc_tcpc_notify(TC_TYPEC_OTG_PLUG_IN, &info->tcpc_noti);
			info->is_usb_dock = false;
		} else if (noti->typec_state.old_state == TYPEC_ATTACHED_SRC &&
			noti->typec_state.new_state == TYPEC_UNATTACHED) {

			pr_info("OTG Plug out\n");
			ret = tc_tcpc_notify(TC_TYPEC_OTG_PLUG_OUT, &info->tcpc_noti);
			if (info->usb_docking_judge)
				otg_plug_out_time = ktime_get();
			/* Audio not using, only for debug log*/
		} else if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
			noti->typec_state.new_state == TYPEC_ATTACHED_AUDIO) {

			pr_info("Audio Plug in\n");
			ret = tc_tcpc_notify(TC_TYPEC_AUDIO_PLUG_IN, &info->tcpc_noti);
		} else if (noti->typec_state.old_state == TYPEC_ATTACHED_AUDIO &&
			noti->typec_state.new_state == TYPEC_UNATTACHED) {

			pr_info("Audio Plug out\n");
			ret = tc_tcpc_notify(TC_TYPEC_AUDIO_PLUG_OUT, &info->tcpc_noti);
		} else if (noti->typec_state.old_state == TYPEC_ATTACHED_SRC &&
			noti->typec_state.new_state == TYPEC_ATTACHED_SNK) {

			pr_info("Source_to_Sink\n");
			ret = tc_tcpc_notify(TC_PD_SRC_TO_SNK, &info->tcpc_noti);
		} else if (noti->typec_state.old_state == TYPEC_ATTACHED_SNK &&
			noti->typec_state.new_state == TYPEC_ATTACHED_SRC) {

			pr_info("Sink_to_Source\n");
			ret = tc_tcpc_notify(TC_PD_SNK_TO_SRC, &info->tcpc_noti);
		}

		if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
		   (noti->typec_state.new_state == TYPEC_ATTACHED_CUSTOM_SRC ||
		    noti->typec_state.new_state == TYPEC_ATTACHED_NORP_SRC)) {

			info->tcpc_noti.pd_type = TC_PD_CONNECT_TYPEC_ONLY_SNK;
			ret = tc_tcpc_notify(TC_PD_TYPE, &info->tcpc_noti);
		} else if ((noti->typec_state.old_state ==
			TYPEC_ATTACHED_CUSTOM_SRC ||
			noti->typec_state.old_state == TYPEC_ATTACHED_NORP_SRC)
			&& noti->typec_state.new_state == TYPEC_UNATTACHED) {

			info->tcpc_noti.pd_type = TC_PD_CONNECT_NONE;
			ret = tc_tcpc_notify(TC_PD_TYPE, &info->tcpc_noti);
		}
		break;
	default:
		break;
	};
	return NOTIFY_OK;
}

static int tc_tcpc_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct tc_tcpc *info = NULL;
	struct tcpc_device *plat_tcpc = NULL;
	static bool is_deferred;

	pr_info("%s: starts\n", __func__);

	plat_tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (plat_tcpc == NULL) {
		if (is_deferred == false) {
			pr_info("%s: tcpc device not ready, defer\n", __func__);
			is_deferred = true;
			ret = -EPROBE_DEFER;
		} else {
			pr_info("%s: failed to get tcpc device\n", __func__);
			ret = -EINVAL;
		}
		goto out_defer;
	}

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	tcpc_notifier_head = devm_kzalloc(&pdev->dev, sizeof(*tcpc_notifier_head), GFP_KERNEL);
	if (!tcpc_notifier_head)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, info);
	info->pdev = pdev;
	info->dev = &pdev->dev;

	info->plat_tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (info->plat_tcpc == NULL) {
		pr_info("%s: failed to get tcpc device\n", __func__);
		ret = -EINVAL;
		goto err_get_tcpc_dev;
	}

	info->usb_docking_judge = of_property_read_bool(info->dev->of_node, "usb_docking_judge");
	info->is_usb_dock = false;
	pr_info("%s: docking judge support %d\n", __func__, info->usb_docking_judge);

	srcu_init_notifier_head(tcpc_notifier_head);

	tc_tcpc_prop_init(info);

	info->pd_nb.notifier_call = tc_pd_tcp_notifier_call;
	ret = register_tcp_dev_notifier(info->plat_tcpc, &info->pd_nb,
					TCP_NOTIFY_TYPE_ALL);
	if (ret < 0) {
		pr_err("register tcpc notifier failed, ret:%d", ret);
		return ret;
	}

	pr_info("%s: done\n", __func__);

	return 0;
err_get_tcpc_dev:
	devm_kfree(&pdev->dev, info);
out_defer:
	return ret;
}

static const struct of_device_id tc_tcpc_of_match[] = {
	{.compatible = "tc_tcpc",},
	{.compatible = "tc_mod_2",},
	{},
};

static int tc_tcpc_remove(struct platform_device *pdev)
{
	return 0;
}

MODULE_DEVICE_TABLE(of, tc_tcpc_of_match);

static struct platform_driver tc_tcpc_driver = {
	.probe = tc_tcpc_probe,
	.remove = tc_tcpc_remove,
	.driver = {
		.name = "tc_tcpc",
		.of_match_table = tc_tcpc_of_match,
	},
};
module_platform_driver(tc_tcpc_driver);

MODULE_DESCRIPTION("TC tcpc Hal Device Driver");
MODULE_LICENSE("GPL");
