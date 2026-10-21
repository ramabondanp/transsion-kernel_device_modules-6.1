// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 Transsion Inc.
 */

#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeup.h>
#include <linux/phy/phy.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_address.h>

/* PD */
#include <tcpm.h>
#include "tc_tcpc.h"
#include "tc_adapter_class.h"
#include "tc_misc_intf.h"

#define PHY_MODE_DPDMPULLDOWN_SET 3
#define PHY_MODE_DPDMPULLDOWN_CLR 4


struct tc_pd_adapter_info {
	struct platform_device *pdev;
	struct tcpc_device *tcpc;
	struct notifier_block pd_nb;
	struct tadapter_device *adapter_dev;
	struct task_struct *adapter_task;
	struct timespec64 prev_time;
	const char *adapter_dev_name;
	bool enable_kpoc_shdn;
	int pd_type;
	int pd_type_timeout;
	bool force_cv;
	u32 ita_min;
	u32 apdo_cap_min_vol;
	u32 apdo_cap_max_cur;
	u32 bootmode;
	u32 boottype;
	bool enable_pp;
};

struct apdo_pps_range {
	u32 prog_mv;
	u32 min_mv;
	u32 max_mv;
};

static struct apdo_pps_range apdo_pps_tbl[] = {
	{5000, 3300, 5900},	/* 5VProg */
	{9000, 3300, 11000},	/* 9VProg */
	{15000, 3300, 16000},	/* 15VProg */
	{20000, 3300, 21000},	/* 20VProg */
};

static inline int to_tc_adapter_ret(int tcpm_ret)
{
	switch (tcpm_ret) {
	case TCP_DPM_RET_SUCCESS:
		return TC_ADAPTER_OK;
	case TCP_DPM_RET_NOT_SUPPORT:
		return TC_ADAPTER_NOT_SUPPORT;
	case TCP_DPM_RET_TIMEOUT:
		return TC_ADAPTER_TIMEOUT;
	case TCP_DPM_RET_REJECT:
		return TC_ADAPTER_REJECT;
	default:
		return TC_ADAPTER_ERROR;
	}
}

static inline int check_typec_attached_snk(struct tcpc_device *tcpc)
{
	if (tcpm_inquire_typec_attach_state(tcpc) != TYPEC_ATTACHED_SNK)
		return -EINVAL;
	return 0;
}

static __maybe_unused int usb_dpdm_pulldown(struct tadapter_device *adapter,
						bool dpdm_pulldown)
{
		struct phy *phy;
		int mode = 0;
		int ret;

		mode = dpdm_pulldown ? PHY_MODE_DPDMPULLDOWN_SET : PHY_MODE_DPDMPULLDOWN_CLR;
		phy = phy_get(adapter->dev.parent, "usb2-phy");
		if (IS_ERR_OR_NULL(phy)) {
			dev_info(&adapter->dev, "phy_get fail\n");
			return -EINVAL;
		}

		ret = phy_set_mode_ext(phy, PHY_MODE_USB_DEVICE, mode);
		if (ret)
			dev_info(&adapter->dev, "phy_set_mode_ext fail\n");

		phy_put(&adapter->dev, phy);

		return 0;
}

static int tc_pd_connect_changed(struct tc_pd_adapter_info *pinfo, bool connect)
{
	int ret = 0;
	char *env[2] = {NULL,NULL};
	char buff[] = "PD_CONNET=1";

	sprintf(buff, "PD_CONNET=%d",connect);
	env[0] = buff;
	pr_info("%s:%s\n", __func__,env[0]);

	ret = kobject_uevent_env(&pinfo->pdev->dev.kobj, KOBJ_CHANGE, env);
	if (ret)
		pr_info("%s: kobject uevent fail, ret  =%d\n", __func__, ret);
	return ret;
}

static int adpt_pd_tcp_notifier_call(struct notifier_block *pnb,
				unsigned long event, void *data)
{
	struct tc_tcpc_noti *noti = data;
	struct tc_pd_adapter_info *info;
	struct tadapter_device *adapter;
	int ret = 0;

	info = container_of(pnb, struct tc_pd_adapter_info, pd_nb);
	adapter = info->adapter_dev;

	pr_notice("PD charger event:%d\n", (int)event);

	switch (event) {
	case TC_PD_TYPE:
		info->pd_type = noti->pd_type;
		pr_info("pd_type = %d\n", info->pd_type);
		if (info->pd_type == TC_PD_CONNECT_NONE) {
			tc_pd_connect_changed(info, false);
		} else if (info->pd_type == TC_PD_CONNECT_PE_READY_SNK_PD30 ||
			info->pd_type == TC_PD_CONNECT_PE_READY_SRC_PD30) {
			tc_pd_connect_changed(info, true);
		}
		break;
	case TC_PD_CONNECT_HARD_RESET:
		info->pd_type = noti->pd_type;
		memset(&info->prev_time, 0, sizeof(struct timespec64));
		info->pd_type_timeout = TC_PD_CONNECT_NONE;
		pr_info("pd_type = %d\n", info->pd_type);
		break;
	case TC_TYPEC_USB_PLUG_IN:
	case TC_PD_SRC_TO_SNK:
		memset(&info->prev_time, 0, sizeof(struct timespec64));
		info->pd_type_timeout = TC_PD_CONNECT_NONE;
		break;
	}
	return ret;
}

static int pd_get_property(struct tadapter_device *dev,
	enum tadapter_property sta)
{
	struct tc_pd_adapter_info *info;
	struct timespec64 diff_time, curr_time;
	int ret = 0;

	info = (struct tc_pd_adapter_info *)tadapter_dev_get_drvdata(dev);
	if (info == NULL || info->tcpc == NULL)
		return -1;

	switch (sta) {
	case TYPEC_RP_LEVEL:
		return tcpm_inquire_typec_remote_rp_curr(info->tcpc);
	case PD_TYPE:
		return info->pd_type;
	case PD_TYPE_TIMEOUT:
		if (info->pd_type_timeout != TC_PD_CONNECT_NONE)
			return info->pd_type_timeout;
	
		if (info->prev_time.tv_sec == 0)
			ktime_get_boottime_ts64(&info->prev_time);

		ktime_get_boottime_ts64(&curr_time);
		diff_time = timespec64_sub(curr_time, info->prev_time);

		if (diff_time.tv_sec <= 5) {
			if (info->pd_type == TC_PD_CONNECT_PE_READY_SNK ||
				info->pd_type == TC_PD_CONNECT_PE_READY_SNK_PD30 ||
				info->pd_type == TC_PD_CONNECT_PE_READY_SNK_APDO ||
				info->pd_type == TC_PD_CONNECT_TYPEC_ONLY_SNK)

				info->pd_type_timeout = info->pd_type;
		} else {
			info->pd_type_timeout = TC_PD_CONNECT_TIMEOUT;
		}

		pr_info("pd_type_timeout = %d\n", info->pd_type_timeout);
		return info->pd_type_timeout;
	case PD_ATTACHED_TIME:
		if (info->prev_time.tv_sec == 0)
			ktime_get_boottime_ts64(&info->prev_time);
		ktime_get_boottime_ts64(&curr_time);
		diff_time = timespec64_sub(curr_time, info->prev_time);
		return diff_time.tv_sec;
	case PD_SRC_PDO_SUPPORT_USB_SUSPEND:
		ret = tcpm_inquire_dpm_flags(info->tcpc)
		& DPM_FLAGS_PARTNER_USB_SUSPEND;
		pr_info("usb suspend flag = %d\n", ret);
		return ret;
	default:
		break;
	}
	return -1;
}

static int pd_set_cap(struct tadapter_device *dev, enum tadapter_cap_type type,
		int mV, int mA)
{
	int ret = TC_ADAPTER_OK;
	int tcpm_ret = TCPM_SUCCESS;
	struct tc_pd_adapter_info *info;

	pr_notice("[%s] type:%d mV:%d mA:%d\n",
		__func__, type, mV, mA);


	info = (struct tc_pd_adapter_info *)tadapter_dev_get_drvdata(dev);
	if (info == NULL || info->tcpc == NULL) {
		pr_notice("[%s] info null\n", __func__);
		return -1;
	}

	if (mV < info->apdo_cap_min_vol && type != TC_PD) {
		mV = info->apdo_cap_min_vol;
		pr_err("out voltage min\n");
	}

	if (mA > info->apdo_cap_max_cur && type != TC_PD) {
		mA = info->apdo_cap_max_cur;
		pr_err("out current max\n");
	}

	pr_info("[%s] type:%d mV:%d mA:%d\n",
			__func__, type, mV, mA);

	if (type == TC_PD_APDO_START) {
		tcpm_ret = tcpm_set_apdo_charging_policy(info->tcpc,
			DPM_CHARGING_POLICY_PPS, mV, mA, NULL);
	} else if (type == TC_PD_APDO_END) {
		tcpm_ret = tcpm_set_pd_charging_policy(info->tcpc,
			DPM_CHARGING_POLICY_VSAFE5V, NULL);
	} else if (type == TC_PD_APDO) {
		tcpm_ret = tcpm_dpm_pd_request(info->tcpc, mV, mA, NULL);
	} else if (type == TC_PD) {
		tcpm_ret = tcpm_dpm_pd_request(info->tcpc, mV,
					mA, NULL);
	}

	pr_notice("[%s] type:%d mV:%d mA:%d ret:%d\n",
		__func__, type, mV, mA, tcpm_ret);


	if (tcpm_ret == TCP_DPM_RET_REJECT)
		return TC_ADAPTER_REJECT;
	else if (tcpm_ret != 0)
		return TC_ADAPTER_ERROR;

	return ret;
}

int pd_get_output(struct tadapter_device *dev, int *mV, int *mA)
{
	int ret = TC_ADAPTER_OK;
	int tcpm_ret = TCPM_SUCCESS;
	struct pd_pps_status pps_status;
	struct tc_pd_adapter_info *info;

	info = (struct tc_pd_adapter_info *)tadapter_dev_get_drvdata(dev);
	if (info == NULL || info->tcpc == NULL)
		return TC_ADAPTER_NOT_SUPPORT;


	tcpm_ret = tcpm_dpm_pd_get_pps_status(info->tcpc, NULL, &pps_status);
	if (tcpm_ret == TCP_DPM_RET_NOT_SUPPORT)
		return TC_ADAPTER_NOT_SUPPORT;
	else if (tcpm_ret != 0)
		return TC_ADAPTER_ERROR;

	*mV = pps_status.output_mv;
	*mA = pps_status.output_ma;

	return ret;
}

int pd_get_status(struct tadapter_device *dev,
	struct tadapter_status *sta)
{
	struct pd_status TAstatus = {0,};
	int ret = TC_ADAPTER_OK;
	int tcpm_ret = TCPM_SUCCESS;
	struct tc_pd_adapter_info *info;

	info = (struct tc_pd_adapter_info *)tadapter_dev_get_drvdata(dev);
	if (info == NULL || info->tcpc == NULL)
		return TC_ADAPTER_ERROR;

	tcpm_ret = tcpm_dpm_pd_get_status(info->tcpc, NULL, &TAstatus);

	sta->temperature = TAstatus.internal_temp;
	sta->ocp = TAstatus.event_flags & PD_STATUS_EVENT_OCP;
	sta->otp = TAstatus.event_flags & PD_STATUS_EVENT_OTP;
	sta->ovp = TAstatus.event_flags & PD_STATUS_EVENT_OVP;

	if (tcpm_ret == TCP_DPM_RET_NOT_SUPPORT)
		return TC_ADAPTER_NOT_SUPPORT;
	else if (tcpm_ret == TCP_DPM_RET_TIMEOUT)
		return TC_ADAPTER_TIMEOUT;
	else if (tcpm_ret == TCP_DPM_RET_SUCCESS)
		return TC_ADAPTER_OK;
	else
		return TC_ADAPTER_ERROR;

	return ret;

}

static int pd_get_cap(struct tadapter_device *dev,
	enum tadapter_cap_type type,
	struct tadapter_power_cap *tacap)
{
	struct tcpm_power_cap_val apdo_cap = {};
	struct tcpm_remote_power_cap pd_cap = {};
	struct pd_source_cap_ext cap_ext = {};

	uint8_t cap_i = 0;
	int ret;
	unsigned int idx = 0;
	unsigned int i, j;
	struct tc_pd_adapter_info *info;

	memset(&pd_cap, 0, sizeof(pd_cap));
	info = (struct tc_pd_adapter_info *)tadapter_dev_get_drvdata(dev);
	if (info == NULL || info->tcpc == NULL)
		return TC_ADAPTER_ERROR;

	if (type == TC_PD_APDO) {
		while (1) {
			ret = tcpm_inquire_pd_source_apdo(info->tcpc,
					TCPM_POWER_CAP_APDO_TYPE_PPS,
					&cap_i, &apdo_cap);
			if (ret == TCPM_ERROR_NOT_FOUND) {
				break;
			} else if (ret != TCPM_SUCCESS) {
				pr_notice("[%s] tcpm_inquire_pd_source_apdo failed(%d)\n",
					__func__, ret);
				break;
			}

			ret = tcpm_dpm_pd_get_source_cap_ext(info->tcpc,
					NULL, &cap_ext);
			if (ret == TCPM_SUCCESS)
				tacap->pdp = cap_ext.source_pdp;
			else {
				tacap->pdp = 0;
				pr_notice("[%s] tcpm_dpm_pd_get_source_cap_ext failed(%d)\n",
					__func__, ret);
			}

			tacap->pwr_limit[idx] = apdo_cap.pwr_limit;
			/* If TA has PDP, we set pwr_limit as true */
			if (tacap->pdp > 0 && !tacap->pwr_limit[idx])
				tacap->pwr_limit[idx] = 1;
			tacap->ma[idx] = apdo_cap.ma;
			tacap->max_mv[idx] = apdo_cap.max_mv;
			tacap->min_mv[idx] = apdo_cap.min_mv;
			tacap->maxwatt[idx] = apdo_cap.max_mv * apdo_cap.ma;
			tacap->minwatt[idx] = apdo_cap.min_mv * apdo_cap.ma;
			tacap->type[idx] = TC_PD_APDO;

			idx++;
			pr_notice("pps_boundary[%d], %d mv ~ %d mv, %d ma pl:%d\n",
				cap_i,
				apdo_cap.min_mv, apdo_cap.max_mv,
				apdo_cap.ma, apdo_cap.pwr_limit);
			if (idx >= ADAPTER_CAP_MAX_NR) {
				pr_notice("CAP NR > %d\n", ADAPTER_CAP_MAX_NR);
				break;
			}
		}
		tacap->nr = idx;

		for (i = 0; i < tacap->nr; i++) {
			pr_notice("pps_cap[%d:%d], %d mv ~ %d mv, %d ma pl:%d pdp:%d\n",
				i, (int)tacap->nr, tacap->min_mv[i],
				tacap->max_mv[i], tacap->ma[i],
				tacap->pwr_limit[i], tacap->pdp);
		}

		if (cap_i == 0)
			pr_notice("no APDO for pps\n");

	} else if (type == TC_PD) {
		pd_cap.nr = 0;
		pd_cap.selected_cap_idx = 0;
		tcpm_get_remote_power_cap(info->tcpc, &pd_cap);

		if (pd_cap.nr != 0) {

			tacap->selected_cap_idx = pd_cap.selected_cap_idx - 1;
			pr_notice("[%s] nr:%d idx:%d\n",
			__func__, pd_cap.nr, pd_cap.selected_cap_idx - 1);

			j = 0;
			pr_notice("adapter cap: nr:%d\n", pd_cap.nr);
			for (i = 0; i < pd_cap.nr; i++) {
				if (!pd_cap.type[i] &&
					j < ADAPTER_CAP_MAX_NR) {
					tacap->type[j] = TC_PD;
					tacap->ma[j] = pd_cap.ma[i];
					tacap->max_mv[j] = pd_cap.max_mv[i];
					tacap->min_mv[j] = pd_cap.min_mv[i];
					tacap->maxwatt[j] =
					tacap->max_mv[j] * tacap->ma[i];
					tacap->minwatt[j] =
					tacap->min_mv[j] * tacap->ma[i];
					j++;
				}

				pr_notice("[%s]:%d mv:[%d,%d] mA:%d type:%d %d\n",
					__func__, i, pd_cap.min_mv[i],
					pd_cap.max_mv[i], pd_cap.ma[i],
					pd_cap.type[i], pd_cap.type[i]);
			}

			tacap->nr = j;
			pr_notice("pd cap: nr:%d\n", tacap->nr);
			for (i = 0; i < tacap->nr; i++) {
				pr_notice("[%s]:%d mv:[%d,%d] mA:%d max:%d min:%d type:%d %d\n",
					__func__, i, tacap->min_mv[i],
					tacap->max_mv[i], tacap->ma[i],
					tacap->maxwatt[i], tacap->minwatt[i],
					tacap->type[i], tacap->type[i]);
			}
		}
	}

	return TC_ADAPTER_OK;
}

#define PPS_STATUS_VTA_NOTSUPP	(-1)
#define PPS_STATUS_ITA_NOTSUPP	(-1)
static int pd_authentication(struct tadapter_device *dev,
			     struct tadapter_auth_data *data)
{
	int ret = 0, ret_check = 0, apdo_idx = -1, i;
	struct tc_pd_adapter_info *info = tadapter_dev_get_drvdata(dev);
	struct tcpm_power_cap_val apdo_cap;
	struct tcpm_power_cap_val selected_apdo_cap;
	struct pd_source_cap_ext src_cap_ext;
	struct tadapter_status status;
	u8 cap_idx;
	u32 vta_meas, ita_meas, prog_mv;
	int apdo_pps_cnt = ARRAY_SIZE(apdo_pps_tbl);

	pr_info("%s ++\n", __func__);
	if (check_typec_attached_snk(info->tcpc) < 0)
		return TC_ADAPTER_ERROR;

	if (info->pd_type != TC_PD_CONNECT_PE_READY_SNK_APDO) {
		pr_info("%s pd type is not snk apdo\n", __func__);
		return TC_ADAPTER_ERROR;
	}

	if (!tcpm_inquire_pd_pe_ready(info->tcpc)) {
		pr_info("%s PD PE not ready\n", __func__);
		return TC_ADAPTER_ERROR;
	}

	/* select TA boundary */
	cap_idx = 0;
	while (1) {
		ret_check = (int)tcpm_inquire_pd_source_apdo(info->tcpc,
						  TCPM_POWER_CAP_APDO_TYPE_PPS,
						  &cap_idx, &apdo_cap);
		if (ret_check != (int)TCPM_SUCCESS) {
			if (apdo_idx == -1)
				pr_info("%s inquire pd apdo fail(%d)\n",
				       __func__, ret_check);
			break;
		}

		pr_info("%s cap_idx[%d], %d mv ~ %d mv, %d ma\n", __func__,
			cap_idx, apdo_cap.min_mv, apdo_cap.max_mv, apdo_cap.ma);

		/*
		 * !(apdo_cap.min_mv <= data->vcap_min &&
		 *   apdo_cap.max_mv >= data->vcap_max &&
		 *   apdo_cap.ma >= data->icap_min)
		 */
		if (apdo_cap.min_mv > data->vcap_min ||
		    apdo_cap.max_mv < data->vcap_max ||
		    apdo_cap.ma < data->icap_min)
			continue;
		if (apdo_idx == -1 || apdo_cap.ma > selected_apdo_cap.ma) {
			memcpy(&selected_apdo_cap, &apdo_cap,
			       sizeof(struct tcpm_power_cap_val));
			apdo_idx = cap_idx;
			pr_info("%s select potential cap_idx[%d]\n", __func__,
				cap_idx);
		}
	}
	if (apdo_idx != -1) {
		data->vta_min = selected_apdo_cap.min_mv;
		data->vta_max = selected_apdo_cap.max_mv;
		data->ita_max = selected_apdo_cap.ma;
		data->ita_min = info->ita_min;
		data->pwr_lmt = selected_apdo_cap.pwr_limit;
		data->support_cc = true;
		data->support_meas_cap = true;
		data->support_status = true;
		data->vta_step = 20;
		data->ita_step = 50;
		data->ita_gap_per_vstep = 200;
		ret_check = tcpm_dpm_pd_get_source_cap_ext(info->tcpc, NULL,
						     &src_cap_ext);
		if (ret_check != (int)TCP_DPM_RET_SUCCESS) {
			pr_info("%s inquire pdp fail(%d)\n", __func__, ret);
			if (data->pwr_lmt) {
				for (i = 0; i < apdo_pps_cnt; i++) {
					if (apdo_pps_tbl[i].max_mv <
					    data->vta_max)
						continue;
					prog_mv = min(apdo_pps_tbl[i].prog_mv,
						      (u32)data->vta_max);
					data->pdp = prog_mv * data->ita_max /
						    1000000;
				}
			}
		} else {
			data->pdp = src_cap_ext.source_pdp;
			if (data->pdp > 0 && !data->pwr_lmt)
				data->pwr_lmt = true;
		}
		info->apdo_cap_min_vol = selected_apdo_cap.min_mv;
		info->apdo_cap_max_cur = selected_apdo_cap.ma;
		/* Check whether TA supports getting pps status */
		ret = pd_set_cap(dev, TC_PD_APDO_START, 5000, 3000);
		if (ret != (int)TC_ADAPTER_OK)
			goto out;
		ret = pd_get_output(dev, &vta_meas, &ita_meas);
		if (ret != (int)TC_ADAPTER_OK &&
		    ret != (int)TC_ADAPTER_NOT_SUPPORT)
			goto out;
		if (ret == (int)TC_ADAPTER_NOT_SUPPORT ||
		    vta_meas == PPS_STATUS_VTA_NOTSUPP ||
		    ita_meas == PPS_STATUS_ITA_NOTSUPP) {
			data->support_cc = false;
			data->support_meas_cap = false;
			ret = (int)TC_ADAPTER_OK;
		}
		ret = pd_get_status(dev, &status);
		if (ret == (int)TC_ADAPTER_NOT_SUPPORT) {
			data->support_status = false;
			ret = (int)TC_ADAPTER_OK;
		} else if (ret != (int)TC_ADAPTER_OK)
			goto out;
		if (info->force_cv)
			data->support_cc = false;
		pr_info("%s select cap_idx[%d], power limit[%d,%dW]\n",
			__func__, apdo_idx, data->pwr_lmt, data->pdp);
	} else {
		pr_info("%s cannot find apdo for pps algo\n", __func__);
		return (int)TC_ADAPTER_ERROR;
	}
out:
	if (ret != (int)TC_ADAPTER_OK)
		pr_info("%s fail(%d)\n", __func__, ret);
	return ret;
}

static int pd_is_cc(struct tadapter_device *dev, bool *cc)
{
	struct tc_pd_adapter_info *info = tadapter_dev_get_drvdata(dev);
	int ret;
	struct pd_pps_status pps_status;

	ret = tcpm_dpm_pd_get_pps_status(info->tcpc, NULL, &pps_status);
	if (ret == TCP_DPM_RET_SUCCESS)
		*cc = !!(pps_status.real_time_flags & PD_PPS_FLAGS_CFF);
	else
		pr_info("%s fail(%d)\n", __func__, ret);
	return to_tc_adapter_ret(ret);
}

int pd_set_wdt(struct tadapter_device *dev, u32 wdt)
{
	return TC_ADAPTER_OK;
}

int pd_enable_wdt(struct tadapter_device *dev, bool en)
{
	return TC_ADAPTER_OK;
}

int pd_send_hardreset(struct tadapter_device *dev)
{
	int ret = 0;
	uint8_t  cnt = 0;
	struct tc_pd_adapter_info *info = tadapter_dev_get_drvdata(dev);
	uint8_t attach_state = tcpm_inquire_typec_attach_state(info->tcpc);
	
	if ((attach_state ==TYPEC_ATTACHED_SNK) ||
		(attach_state == TYPEC_ATTACHED_SRC)) {
		do {
			ret = tcpm_dpm_pd_hard_reset(info->tcpc, NULL);
			cnt++;
		} while (ret != TCP_DPM_RET_SUCCESS && cnt < 2);
	}
	pr_err("%s:tcpm ret =%d\n",__func__,ret);
	if (ret != TCP_DPM_RET_SUCCESS)
		pr_info("fail(%d)\n", ret);
	return to_tc_adapter_ret(ret);
}

static struct tadapter_ops adapter_ops = {
	.get_status = pd_get_status,
	.set_cap = pd_set_cap,
	.get_output = pd_get_output,
	.get_property = pd_get_property,
	.get_cap = pd_get_cap,
	.authentication = pd_authentication,
	.is_cc = pd_is_cc,
	.set_wdt = pd_set_wdt,
	.enable_wdt = pd_enable_wdt,
	.send_hardreset = pd_send_hardreset,
};

static int adapter_parse_dt(struct tc_pd_adapter_info *info,
	struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct device_node *boot_np = NULL;
	const struct {
		u32 size;
		u32 tag;
		u32 boot_mode;
		u32 boot_type;
	} *tag;

	if (of_property_read_string(np, "adapter_name", &info->adapter_dev_name) < 0)
		pr_notice("%s: no adapter name\n", __func__);
	info->force_cv = of_property_read_bool(np, "force_cv");
	of_property_read_u32(np, "ita_min", &info->ita_min);

	pr_notice("%s\n", __func__);

	if (!np) {
		pr_notice("%s: no device node\n", __func__);
		return -EINVAL;
	}

	/* mediatek boot mode */
	boot_np = of_parse_phandle(np, "boot_mode", 0);
	if (!boot_np)
		pr_info("%s: failed to get bootmode phandle\n", __func__);

	tag = of_get_property(boot_np, "atag,boot", NULL);
	if (!tag) {
		pr_info("%s: failed to get atag,boot\n", __func__);
		info->bootmode = 8;
		info->boottype = 2;
		pr_notice("%s: set bootmode=8, boottype=2\n", __func__);
	} else {
		info->bootmode = tag->boot_mode;
		info->boottype = tag->boot_type;
		pr_notice("%s: sz:0x%x tag:0x%x mode:0x%x type:0x%x\n",
	__func__, tag->size, tag->tag, tag->boot_mode, tag->boot_type);
	}

	return 0;
}

static int tc_pd_adapter_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct tc_pd_adapter_info *info = NULL;
	static bool is_deferred;

	pr_info("%s\n", __func__);

	info = devm_kzalloc(&pdev->dev, sizeof(struct tc_pd_adapter_info),
			GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->pdev = pdev;
	adapter_parse_dt(info, &pdev->dev);

	info->adapter_dev = tadapter_device_register(info->adapter_dev_name,
		&pdev->dev, info, &adapter_ops, NULL);
	if (IS_ERR_OR_NULL(info->adapter_dev)) {
		ret = PTR_ERR(info->adapter_dev);
		pr_info("%s: adapter device not ready\n", __func__);
		goto err_register_adapter_dev;
	}

	tadapter_dev_set_drvdata(info->adapter_dev, info);

	info->tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (info->tcpc == NULL) {
		if (is_deferred == false) {
			pr_info("%s: tcpc device not ready, defer\n", __func__);
			is_deferred = true;
			ret = -EPROBE_DEFER;
		} else {
			pr_info("%s: failed to get tcpc device\n", __func__);
			ret = -EINVAL;
		}
		goto err_get_tcpc_dev;
	}

	info->pd_nb.notifier_call = adpt_pd_tcp_notifier_call;
	ret = register_tc_tcpc_notifier(&info->pd_nb);
	if (ret < 0) {
		pr_info("%s: register tcpc notifer fail\n", __func__);
		ret = -EINVAL;
		goto err_get_tcpc_dev;
	}

	pr_info("%s done\n", __func__);
	return 0;

err_get_tcpc_dev:
	tadapter_device_unregister(info->adapter_dev);
err_register_adapter_dev:
	devm_kfree(&pdev->dev, info);
	pr_info("%s fail\n", __func__);
	return ret;
}

static int tc_pd_adapter_remove(struct platform_device *dev)
{
	return 0;
}

static void tc_pd_adapter_shutdown(struct platform_device *dev)
{
}

static const struct of_device_id tc_pd_adapter_of_match[] = {
	{.compatible = "tc,pd_adapter",},
	{},
};

MODULE_DEVICE_TABLE(of, tc_pd_adapter_of_match);


static struct platform_driver tc_pd_adapter_driver = {
	.probe = tc_pd_adapter_probe,
	.remove = tc_pd_adapter_remove,
	.shutdown = tc_pd_adapter_shutdown,
	.driver = {
		   .name = "tc_pd_adapter",
		   .of_match_table = tc_pd_adapter_of_match,
	},
};

static int __init tc_pd_adapter_init(void)
{
	return platform_driver_register(&tc_pd_adapter_driver);
}
module_init(tc_pd_adapter_init);

static void __exit tc_pd_adapter_exit(void)
{
	platform_driver_unregister(&tc_pd_adapter_driver);
}
module_exit(tc_pd_adapter_exit);


MODULE_DESCRIPTION("PD Adapter Hal Driver");
MODULE_LICENSE("GPL");

