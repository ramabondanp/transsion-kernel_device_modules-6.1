// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#include <linux/delay.h>
#include "wireless_manager.h"
#include "wireless_class.h"
#include "tc_voter.h"
#include "wireless_charger.h"
#include "tc_charger.h"

/* static int tc_get_wireless_protocol(struct wireless_manager *info, int *txp) */
/* { */
/*         int ret = 0; */

/*         ret = wireless_ta_get_epp_power(info->wireless_chg, txp); */
/*         if (ret) { */
/*                 pr_err("failed get epp power\n"); */
/*                 return -EINVAL; */
/*         } */

/*         if (*txp > STANDARD_BPP_POWER) { */
/*                 return EPP; */
/*         } else if (*txp == STANDARD_BPP_POWER) { */
/*                 return BPP; */
/*         } */

/*         return UNKNOWN; */
/* } */

static int set_aicr_max(int power)
{
	int aicr_max = 0;

	if (power > 10) {
		aicr_max = power ;
	} else if (power <= 10 && power >= 8) {
		aicr_max = (power * 9 * 10) / 75;
	} else {
		aicr_max = (power * 9 * 10) / 55;
	}

	return aicr_max;
}

static void tx_set_current(struct wireless_manager *info, int ptx)
{
	int vbus = 0;
	int aicr_max = 0;
	short ce = 0;
	int curr_aicr = 0;
	int curr_ichg = 0;
	union com_propval prop = {.intval = 0};
	u32 ichg = (ptx * 9 * 1000000) / 42;

	if (ptx <= 0)
		return;

	vote(info->total_ichg_vote, WIRELESS_VOTER, true, ichg);

	curr_aicr = get_client_vote(info->total_aicr_vote, WIRELESS_VOTER);
	if (curr_aicr < 0) {
		pr_info("wireless vote Code confusion\n");
		curr_aicr = info->desc.wireless_init_input_current;
	}

	curr_ichg = get_effective_result_locked(info->total_ichg_vote);
	if (curr_ichg < 0) {
		pr_info("wireless vote Code confusion\n");
		curr_aicr = info->desc.wireless_init_charger_current;
	}

	wireless_ta_get_vbus(info->wireless_chg, &vbus);
	aicr_max = set_aicr_max(ptx);
	if (aicr_max >= ((curr_ichg * 7) / (10 * 100000)))
		aicr_max = (curr_ichg * 7) / (10 * 100000);

	if (aicr_max < 5)
		aicr_max = 5;
	ce = wireless_ta_get_ce(info->wireless_chg);
	pr_info("%s: ce:%d, vbus:%d, aicr_max:%d, curr_aicr:%d, curr_ichg:%d\n",
		__func__, ce, vbus, aicr_max, curr_aicr / 100000, curr_ichg / 100000);

	if (aicr_max < curr_aicr / 100000) {
		vote(info->total_aicr_vote, WIRELESS_VOTER, true, aicr_max * 100000);
	} else if (ce > 10 && ce < 20) {
		pr_info("%s do nothing\n", __func__);
		return;
	} else if (ce >= 20) {
		if (curr_aicr > 500000) {
			vote(info->total_aicr_vote, WIRELESS_VOTER, true, curr_aicr - 100000);
		}
	} else if (aicr_max > curr_aicr / 100000) {
		vote(info->total_aicr_vote, WIRELESS_VOTER, true, curr_aicr + 100000);

		msleep(420);

		ce = wireless_ta_get_ce(info->wireless_chg);
		if (ce >= 15)
			vote(info->total_aicr_vote, WIRELESS_VOTER, true, curr_aicr);

		prop.intval = CHARGING_ALG_CHECK_INTERVAL;
		tran_dev_set_prop(info->tc_chg_dev, TRAN_PROP_SET_POLLING_INTERVAL, &prop);
	} else {
		prop.intval = CHARGING_INTERVAL;
		tran_dev_set_prop(info->tc_chg_dev, TRAN_PROP_SET_POLLING_INTERVAL, &prop);
	}
}
static int tx_bridge_set_mode(struct wireless_manager *info)
{
	if (!info->wireless_chg->ops || !info->wireless_chg->ops->set_bridge_mode)
		return -EINVAL;

	return info->wireless_chg->ops->set_bridge_mode(info->wireless_chg);
}

static void tc_bpp_wireless_charger_algo(struct wireless_manager *info)
{
	int ce;
	pr_info("%s\n", __func__);

	if (!info->wls.config_vol) {
		wireless_set_tx_vol(info->wireless_chg, 5500);
	}
	ce = wireless_ta_get_ce(info->wireless_chg);
	pr_info("cj add ce---->:%d\n", ce);
	if (ce > 20)
		tx_bridge_set_mode(info);	

	tx_set_current(info, STANDARD_BPP_POWER);
	info->wls.config_vol = true;
}

static int tx_bridge_mode(struct wireless_manager *info)
{
	if (!info->wireless_chg->ops || !info->wireless_chg->ops->get_bridge_mode)
		return -EINVAL;

	return info->wireless_chg->ops->get_bridge_mode(info->wireless_chg);
}

static u8 __wireless_get_wls_protocol(struct wireless_manager *info)
{
	if (!info->wireless_chg->ops || !info->wireless_chg->ops->get_wls_protocol)
		return -EINVAL;

	return info->wireless_chg->ops->get_wls_protocol(info->wireless_chg);
}

static bool private_authentication_done(struct wireless_manager *info)
{
	return info->pa_auth_done;
}

static bool wls_get_config_inductions(struct wireless_manager *info)
{
	return info->desc.low_inductions;
}

static int epp_current_setup(struct wireless_manager *info)
{
	int mode = 0, power = 0;
	union com_propval prop = {.intval = 0};
	u8 inductions = wls_get_config_inductions(info);
	int ret = wireless_get_tx_power(info->wireless_chg, &power);
	if (ret) {
		power = 15;
	}

	vote(info->total_ichg_vote, WIRELESS_VOTER, true, 2500000);

	mode = tx_bridge_mode(info);
	switch(mode) {
	case FULL_BRIDGE:
		vote(info->total_aicr_vote, WIRELESS_VOTER, true, 500000);

		if (power > 10) {
			wireless_set_tx_vol(info->wireless_chg, 9000);
			vote(info->total_aicr_vote, WIRELESS_VOTER, true, 1000000);
			//vote(info->chg1_mivr_vote, WIRELESS_VOTER, true, 7000000);
		} else if((power <= 10 && power >= 8) || inductions){
			wireless_set_tx_vol(info->wireless_chg, 7500);
			//vote(info->chg1_mivr_vote, WIRELESS_VOTER, true, 5500000);
		} else {
			wireless_set_tx_vol(info->wireless_chg, 5500);
		}

		prop.intval = CHARGING_ALG_CHECK_INTERVAL;
		tran_dev_set_prop(info->tc_chg_dev, TRAN_PROP_SET_POLLING_INTERVAL, &prop);
		break;
	case HALF_BRIDGE:
		vote(info->total_aicr_vote, WIRELESS_VOTER, true, 300000);
		wireless_set_tx_vol(info->wireless_chg, 9000);
		prop.intval = CHARGING_WIRELESS_INTERVAL;
		tran_dev_set_prop(info->tc_chg_dev, TRAN_PROP_SET_POLLING_INTERVAL, &prop);
		break;
	default:
		pr_info("unknown bridge, check config\n");
		return -EINVAL;
	}

	pr_info("%s: mode:%d\n", __func__, mode);

	return mode;
}

static int wireless_epp_config_setup(struct wireless_manager *info)
{
	int mode = 0;
	static int tx_config_setup = -1;

	pr_info("%s: %d, %d\n", __func__, tx_config_setup, info->wls.epp_setup);

	if (!private_authentication_done(info)) {
		return tx_config_setup;
	}

	if (!info->wls.epp_setup) {
		mode = epp_current_setup(info);
		if (mode == HALF_BRIDGE) {
			if (info->wls.rd_tx_mode_cnt) {
				info->wls.epp_setup = true;
				tx_config_setup = 2;
				return tx_config_setup;
			}

			info->wls.rd_tx_mode_cnt++;
			tx_config_setup = 1;
			return tx_config_setup;
		}
		info->wls.epp_setup = true;
		tx_config_setup = 0;
		return tx_config_setup;
	}

	return tx_config_setup;
}

static void __epp_wireless_charger_algo(struct wireless_manager *info)
{
	int ret = 0;
	int power = 0;

	pr_info("%s\n", __func__);

	ret = wireless_get_tx_power(info->wireless_chg, &power);
	if (ret) {
		power = 15;
	}

	tx_set_current(info, power);
} 

static void tc_epp_wireless_charger_algo(struct wireless_manager *info)
{
	switch (wireless_epp_config_setup(info)) {
	case 0:
		__epp_wireless_charger_algo(info);
		break;
	case 1:
		break;
	case 2:
		tc_bpp_wireless_charger_algo(info);
		break;
	}
}

int __wireless_charger_plug_out_reset(struct wireless_manager *info)
{
	pr_info("%s\n", __func__);
	
	vote(info->total_aicr_vote, WIRELESS_VOTER, false, 0);
	vote(info->total_ichg_vote, WIRELESS_VOTER, false, 0);
	vote(info->chg1_mivr_vote, WIRELESS_VOTER, false, 0);
	memset(&info->wls, 0, sizeof(info->wls));

	return 0;
}

static bool wireless_setup_current(struct wireless_manager *info)
{
	union com_propval prop = {.intval = 0};

	pr_info("wireless access, first setting\n");
	vote(info->total_aicr_vote, WIRELESS_VOTER, true, info->desc.wireless_init_input_current);
	vote(info->total_ichg_vote, WIRELESS_VOTER, true, info->desc.wireless_init_charger_current);
	prop.intval = CHARGING_ALG_CHECK_INTERVAL;
	tran_dev_set_prop(info->tc_chg_dev, TRAN_PROP_SET_POLLING_INTERVAL, &prop);

	pr_info("%s: protocol:%d\n", __func__, info->wls.protocol);

	if (info->wls.protocol >= MPP_RESTRICT && info->wls.protocol < EPP) {
		info->wls.config_setup = true;
		return false;
	}

	if (info->wls.protocol != UNKNOWN) {
		info->wls.config_setup = true;
		wireless_set_tx_vol(info->wireless_chg, 5500);
	}

	return true;
}

static bool is_tx_magnetism(struct wireless_manager *info)
{
	int ret = 0;
	struct wls_hw_info hw_info = {0, };

	ret = wireless_get_ta_tx_cap(info->wireless_chg, &hw_info);
	if (ret < 0)
		return ret;

	return hw_info.tx_magnetism;
}

static void tc_mpp_wireless_charger_algo(struct wireless_manager *info)
{
	int vbus = 0;
	u32 ichg = 0;
	int power = 0;
	int ret = 0;

	ret = wireless_get_tx_power(info->wireless_chg, &power);
	if (ret) {
		power = 15;
	}

	wireless_ta_get_vbus(info->wireless_chg, &vbus);
	ichg = (power * 9 * 1000000) / 42;

	pr_info("%s: vbus:%d, aicr_max:%d, ichg:%d\n", __func__,vbus, power * 100000, ichg);

	//vote(info->chg1_mivr_vote, WIRELESS_VOTER, true, 8500000);
	vote(info->total_ichg_vote, WIRELESS_VOTER, true, ichg);
	vote(info->total_aicr_vote, WIRELESS_VOTER, true, power * 100000);
}

int __wireless_select_current_limit(struct wireless_manager *info)
{
	info->total_aicr_vote = find_votable("total_aicr");
	info->total_ichg_vote = find_votable("total_ichg");
	info->chg1_mivr_vote = find_votable("chg1_mivr");

	if (IS_ERR_OR_NULL(info->total_aicr_vote) ||
		IS_ERR_OR_NULL(info->total_ichg_vote) ||
		IS_ERR_OR_NULL(info->chg1_mivr_vote))
		return -EINVAL;

	info->wls.protocol = __wireless_get_wls_protocol(info);

	if (!info->wls.config_setup && wireless_setup_current(info))
		return 0;

	switch (info->wls.protocol) {
	case BPP:
		tc_bpp_wireless_charger_algo(info);
		break;
	case EPP:
		if (is_tx_magnetism(info) && wls_get_config_inductions(info)) {
			tc_bpp_wireless_charger_algo(info);
			return 0;
		}

		tc_epp_wireless_charger_algo(info);
		break;
	case MPP_FULL:
	case MPP_RESTRICT:
		tc_mpp_wireless_charger_algo(info);
		break;
	default:
		pr_err("unknown wireless protocol\n");
		memset(&info->wls, 0, sizeof(info->wls));
		return -EINVAL;
	}

	return 0;
}
