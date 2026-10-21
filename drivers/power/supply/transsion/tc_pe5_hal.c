
// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Transsion Inc.
 */

#include "tc_charger_class.h"
#include "tc_charger.h"
#include "tc_common_class.h"
#include "tc_pe5.h"
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include "tc_misc_intf.h"

static inline int to_chgtyp(enum tchg_idx idx)
{
	switch (idx) {
	case CHG1:
		return MTK_CHGTYP_SWCHG;
	case DVCHG1:
		return MTK_CHGTYP_DVCHG;
	case DVCHG2:
		return MTK_CHGTYP_DVCHG_SLAVE;
	case DVCHG3:
		return MTK_CHGTYP_DVCHG_THIRD;
	case HVCHG:
		return MTK_CHGTYP_HV_DVCHG;
	default:
		return -ENOTSUPP;
	}
}

static inline int to_chgclass_adc(enum pe50_adc_channel chan)
{
	switch (chan) {
	case PE50_ADCCHAN_VBUS:
		return ADC_CHANNEL_VBUS;
	case PE50_ADCCHAN_IBUS:
		return ADC_CHANNEL_IBUS;
	case PE50_ADCCHAN_VBAT:
		return ADC_CHANNEL_VBAT;
	case PE50_ADCCHAN_IBAT:
		return ADC_CHANNEL_IBAT;
	case PE50_ADCCHAN_TBAT:
		return ADC_CHANNEL_TBAT;
	case PE50_ADCCHAN_TPCB:
		return ADC_CHANNEL_TPCB;
	case PE50_ADCCHAN_TPA:
		return ADC_CHANNEL_TPA;
	case PE50_ADCCHAN_TCHG:
		return ADC_CHANNEL_TEMP_JC;
	case PE50_ADCCHAN_VOUT:
		return ADC_CHANNEL_VOUT;
	case PE50_ADCCHAN_VSYS:
		return ADC_CHANNEL_VSYS;
	default:
		break;
	}
	return -ENOTSUPP;
}

int pe50_hal_get_ta_fw(struct tchg_alg_device *alg, u8 *code, u32 length)
{
        int ret;
        struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);
	ret = tadapter_dev_get_ta_fw(hal->adapter,code,length);
        if (ret < 0)
                return ret;
        return (ret == TC_ADAPTER_OK) ? 0 : -ret;
}


int pe50_hal_get_ta_output(struct tchg_alg_device *alg, int *mV, int *mA)
{
	int ret;
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	ret = tadapter_dev_get_output(hal->adapter, mV, mA);
	if (ret < 0)
		return ret;
	return (ret == TC_ADAPTER_OK) ? 0 : -ret;
}

int pe50_hal_get_ta_status(struct tchg_alg_device *alg,
			   struct pe50_ta_status *status)
{
	int ret;
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);
	struct tadapter_status _status;
	_status.temperature = 0;
	_status.ocp = 0;
	_status.ovp = 0;
	_status.otp = 0;

	ret = tadapter_dev_get_status(hal->adapter, &_status);
	if (ret < 0)
		return ret;
	if (ret != TC_ADAPTER_OK)
		return -ret;
	/* 0 means NOT SUPPORT */
	status->temperature = (_status.temperature == 0) ? 25 :
							   _status.temperature;
	status->ocp = _status.ocp;
	status->ovp = _status.ovp;
	status->otp = _status.otp;
	status->cable_capability = _status.cable_capability;
	return 0;
}

int pe50_hal_set_ta_cap(struct tchg_alg_device *alg, int mV, int mA)
{
	int ret;
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	ret = tadapter_dev_set_cap(hal->adapter, TC_PD_APDO, mV, mA);
	return (ret <= TC_ADAPTER_OK) ? ret : -ret;
}

int pe50_hal_set_ta_wdt(struct tchg_alg_device *alg, u32 ms)
{
	int ret;
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	ret = tadapter_dev_set_wdt(hal->adapter, ms);
	return (ret <= TC_ADAPTER_OK) ? ret : -ret;
}

int pe50_hal_enable_ta_wdt(struct tchg_alg_device *alg, bool en)
{
	int ret;
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	ret = tadapter_dev_enable_wdt(hal->adapter, en);
	return (ret <= TC_ADAPTER_OK) ? ret : -ret;
}

int pe50_hal_enable_ta_charging(struct tchg_alg_device *alg, bool en, int mV,
				int mA)
{
	int ret;
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);
	enum tadapter_cap_type type = en ? TC_PD_APDO_START : TC_PD_APDO_END;

	ret = tadapter_dev_set_cap(hal->adapter, type, mV, mA);
	return (ret <= TC_ADAPTER_OK) ? ret : -ret;
}

int pe50_hal_sync_ta_volt(struct tchg_alg_device *alg, u32 mV)
{
	int ret;
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	ret = tadapter_dev_sync_volt(hal->adapter, mV);
	return (ret <= TC_ADAPTER_OK) ? ret : -ret;
}

void pe50_hal_ta_reset(struct tchg_alg_device *alg)
{
	int i;
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	for (i = 0; i < hal->support_ta_cnt; i++) {
		if (!hal->adapters[i])
			continue;
		tadapter_dev_plug_out_reset(hal->adapters[i]);
	}

	return;
}

int pe50_hal_authenticate_ta(struct tchg_alg_device *alg,
			     struct pe50_ta_auth_data *data,
			     int adapter_num)
{
	int ret, i = adapter_num;
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);
	/* struct pe50_algo_info *info = tchg_alg_dev_get_drvdata(alg); */
	/* struct pe50_algo_desc *desc = info->desc; */
	struct tadapter_auth_data _data = {
		.vcap_min = data->vcap_min,
		.vcap_max = data->vcap_max,
		.icap_min = data->icap_min,
	};

	/* for (i = 0; i < hal->support_ta_cnt; i++) { */
	/*         if (!hal->adapters[i]) */
	/*                 continue; */

		/* if (strcmp(hal->adapters[i]->dev.kobj.name, "wireless_adapter") == 0) */
		/*         _data.vcap_max = desc->wireless_vta_cap_max; */

		/* if (strcmp(hal->adapters[i]->dev.kobj.name, "pd_adapter") == 0) { */
		/*         ret = pe50_hal_is_pd_adapter_ready(alg); */
		/*         if (ret == ALG_READY) */
		/*                 PE50_INFO("pd type ready!"); */
		/*         else if (ret == ALG_TA_CHECKING) */
		/*                 return ret; */
		/*         else */
		/*                 continue; */

		/* } */

		ret = tadapter_dev_authentication(hal->adapters[i], &_data);
		if (ret < 0 || ret != TC_ADAPTER_OK) {
			PE50_DBG("authenticate fail(%d)\n", ret);
			return ALG_TA_NOT_SUPPORT;
			/* continue; */
		}
		hal->adapter = hal->adapters[i];

		if (strcmp(hal->adapter->dev.kobj.name, "pd_adapter") == 0) {
			data->vta_min = _data.vta_min;
			data->vta_max = min_t(int, (_data.vta_max - 500), data->vcap_max);
			data->ita_min = _data.ita_min;
			data->ita_max = percent(_data.ita_max, 80);
			data->pwr_lmt = _data.pwr_lmt;
			if (data->pwr_lmt || (_data.pdp == 0))
				data->pdp = data->vta_max * _data.ita_max / 1000000;
			else
				data->pdp = _data.pdp;

			PE50_INFO("%s: v_lmt(%d ~ %d), i_lmt(%d ~ %d), p_lmt(%d, %dW)\n",
				hal->adapter->dev.kobj.name,
				data->vta_min, data->vta_max,
				data->ita_min, data->ita_max,
				data->pwr_lmt, data->pdp);
		} else {
			data->vta_min = _data.vta_min;
			data->vta_max = _data.vta_max;
			data->ita_min = _data.ita_min;
			data->ita_max = _data.ita_max;
			data->pwr_lmt = _data.pwr_lmt;
			data->pdp = _data.pdp;
			PE50_INFO("%s: v_lmt(%d ~ %d), i_lmt(%d ~ %d), p_lmt(%d, %dW)\n",
				hal->adapter->dev.kobj.name,
				data->vta_min, data->vta_max,
				data->ita_min, data->ita_max,
				data->pwr_lmt, data->pdp);
		}

		data->support_meas_cap = _data.support_meas_cap;
		data->support_status = _data.support_status;
		data->vta_step = _data.vta_step;
		data->ita_step = _data.ita_step;
		data->ita_gap_per_vstep = _data.ita_gap_per_vstep;
		data->ita_gap_cv_mode = _data.ita_gap_cv_mode;
		data->full_power_run_time = _data.full_power_run_time;
		PE50_INFO("%s:lmt(%d,%dW),step(%d,%d),cap=%d,status=%d,gap=%d\n",
			  hal->adapter->dev.kobj.name,data->pwr_lmt, data->pdp, data->vta_step,
			  data->ita_step, data->support_meas_cap,
			  data->support_status,data->ita_gap_cv_mode);
		return ALG_READY;
	/* } */
	/* return ALG_TA_NOT_SUPPORT; */
}

int pe50_hal_send_ta_hardreset(struct tchg_alg_device *alg)
{
	int ret;
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	ret = tadapter_dev_send_hardreset(hal->adapter);
	return (ret <= TC_ADAPTER_OK) ? ret : -ret;
}

int pe50_hal_init_hardware(struct tchg_alg_device *alg, const char **support_ta,
			   int support_ta_cnt)
{
	struct pe50_algo_info *info = tchg_alg_dev_get_drvdata(alg);
	struct pe50_algo_data *data = info->data;
	struct pe50_hal *hal;
	int i, ret;
	bool has_ta = false;

	if (support_ta_cnt <= 0)
		return -EINVAL;

	hal = devm_kzalloc(info->dev, sizeof(*hal), GFP_KERNEL);
	if (!hal)
		return -ENOMEM;
	hal->adapters = devm_kzalloc(info->dev,
				     sizeof(struct tadapter_device *) *
				     support_ta_cnt, GFP_KERNEL);
	if (!hal->adapters)
		return -ENOMEM;

	hal->support_ta = support_ta;
	hal->support_ta_cnt = support_ta_cnt;
	/* get TA */
	for (i = 0; i < hal->support_ta_cnt; i++) {
		hal->adapters[i] = get_tadapter_by_name(support_ta[i]);
		if (hal->adapters[i]) {
			has_ta = true;
			continue;
		}
		PE50_ERR("zsa no %s\n", hal->support_ta[i]);
	}
	if (!has_ta) {
		ret = -ENODEV;
		goto err;
	}

	/* get charger device */
	for (i = 0; i < MTK_CHGTYP_MAX; i++) {
		hal->chgdevs[i] =
			get_charger_by_name(mtk_chgdev_desc_tbl[i].name);
		if (!hal->chgdevs[i]) {
			PE50_ERR("get %s fail\n", mtk_chgdev_desc_tbl[i].name);
			if (mtk_chgdev_desc_tbl[i].must_exist) {
				ret = -ENODEV;
				goto err;
			}
		} else {
			PE50_ERR("get %s succ\n", mtk_chgdev_desc_tbl[i].name);
		}
	}
	data->is_dvchg_exist[PE50_DVCHG_MASTER] = true;
	if (hal->chgdevs[MTK_CHGTYP_DVCHG_SLAVE])
		data->is_dvchg_exist[PE50_DVCHG_SLAVE] = true;
	if (hal->chgdevs[MTK_CHGTYP_DVCHG_THIRD])
		data->is_dvchg_exist[PE50_DVCHG_THIRD] = true;
	tchg_alg_dev_set_drv_hal_data(alg, hal);
	hal->dev = info->dev;
	hal->bat_psy = power_supply_get_by_name("battery");
	PE50_INFO("successfully\n");
	return 0;
err:
	return ret;
}

int pe50_hal_enable_sw_vbusovp(struct tchg_alg_device *alg, bool en)
{
	enum charger_voltage_max idx;

	idx = CHARGER_VOLTAGE_CP_FC;

	tc_chg_switch_vbus_ovp(idx, PE50_VOTER, !en);

	return 0;
}

int pe50_hal_enable_charging(struct tchg_alg_device *alg, enum tchg_idx chgidx,
			     bool en)
{
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);
	struct votable *chg1_disable_vote = NULL;

	if (chgtyp < 0)
		return chgtyp;

	if (chgidx == CHG1) {
		chg1_disable_vote = find_votable("chg1_disable");
		if (chg1_disable_vote == NULL)
			return -ENODEV;
		return vote(chg1_disable_vote, PE50_VOTER, !en, !en);
	}
		
	return charger_dev_enable(hal->chgdevs[chgtyp], en);
}

int pe50_hal_enable_hz(struct tchg_alg_device *alg, bool en)
{
	int ret = 0;
	struct votable *chg1_hiz_vote = NULL;

	chg1_hiz_vote = find_votable("chg1_hiz");
	if (chg1_hiz_vote == NULL)
		return -ENODEV;
	
	ret = vote(chg1_hiz_vote, PE50_VOTER, en, en);
	if (ret < 0) {
		PE50_ERR("pe50 set hiz failed, ret = %d\n", ret);
	}

	return ret;
}

int pe50_hal_get_vac1_status(struct tchg_alg_device *alg, enum tchg_idx chgidx, bool *online)
{
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;

	return charger_dev_get_vac1_status(hal->chgdevs[chgtyp], online);
}

int pe50_hal_set_ibusucp_enable(struct tchg_alg_device *alg, enum tchg_idx chgidx,
			 bool enable)
{
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_set_ibusucp_enable(hal->chgdevs[chgtyp], enable);
}

int pe50_hal_set_vbusovp(struct tchg_alg_device *alg, enum tchg_idx chgidx,
			 u32 mV)
{
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_set_vbusovp(hal->chgdevs[chgtyp],
				       milli_to_micro(mV));
}

int pe50_hal_set_ibusocp(struct tchg_alg_device *alg, enum tchg_idx chgidx,
			 u32 mA)
{
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_set_ibusocp(hal->chgdevs[chgtyp],
				       milli_to_micro(mA));
}

int pe50_hal_set_vbatovp(struct tchg_alg_device *alg, enum tchg_idx chgidx,
			 u32 mV)
{
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_set_vbatovp(hal->chgdevs[chgtyp],
				       milli_to_micro(mV));
}

int pe50_hal_set_ibatocp(struct tchg_alg_device *alg, enum tchg_idx chgidx,
			 u32 mA)
{
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_set_ibatocp(hal->chgdevs[chgtyp],
				       milli_to_micro(mA));
}

int pe50_hal_set_vbatovp_alarm(struct tchg_alg_device *alg, enum tchg_idx chgidx,
			       u32 mV)
{
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_set_vbatovp_alarm(hal->chgdevs[chgtyp],
					     milli_to_micro(mV));
}

int pe50_hal_reset_vbatovp_alarm(struct tchg_alg_device *alg,
				 enum tchg_idx chgidx)
{
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_reset_vbatovp_alarm(hal->chgdevs[chgtyp]);
}

int pe50_hal_set_vbusovp_alarm(struct tchg_alg_device *alg, enum tchg_idx chgidx,
			       u32 mV)
{
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_set_vbusovp_alarm(hal->chgdevs[chgtyp],
					     milli_to_micro(mV));
}

int pe50_hal_reset_vbusovp_alarm(struct tchg_alg_device *alg,
				 enum tchg_idx chgidx)
{
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_reset_vbusovp_alarm(hal->chgdevs[chgtyp]);
}

static int pe50_get_tbat(void)
{
	return tc_get_battery_temperature();
}

static int pe50_get_tpcb(void)
{
	return tc_get_tpcb_temp();
}

static int pe50_get_tpa(void)
{
	return tc_get_tpa_temp_max();
}

int pe50_hal_get_adc(struct tchg_alg_device *alg, enum tchg_idx chgidx,
		     enum pe50_adc_channel chan, int *val)
{
	int ret;
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);
	int _chan = to_chgclass_adc(chan);

	if (chgtyp < 0)
		return chgtyp;

	if (_chan < 0)
		return _chan;

	switch (_chan) {
		case ADC_CHANNEL_TBAT:
			*val = pe50_get_tbat();
			return 0;
		case ADC_CHANNEL_TPCB:
			*val = pe50_get_tpcb();
			return 0;
		case ADC_CHANNEL_TPA:
			*val = pe50_get_tpa();
			return 0;
		default:
			break;
	}

	ret = charger_dev_get_adc(hal->chgdevs[chgtyp], _chan, val, val);
	if (ret < 0)
		return ret;
	if (_chan == ADC_CHANNEL_VBAT || _chan == ADC_CHANNEL_IBAT ||
	    _chan == ADC_CHANNEL_VBUS || _chan == ADC_CHANNEL_IBUS ||
	    _chan == ADC_CHANNEL_VOUT || _chan == ADC_CHANNEL_VSYS)
		*val = micro_to_milli(*val);
	return 0;
}

int pe50_hal_get_soc(struct tchg_alg_device *alg, u32 *soc)
{
	*soc = tc_get_uisoc();
	PE50_DBG("%d\n", *soc);

	return 0;
}

int pe50_hal_is_pd_adapter_ready(struct tchg_alg_device *alg)
{
	struct pe50_hal *hal;
	int type = 0;
	int i;

	if (alg == NULL) {
		PE50_INFO("%s: alg is null\n", __func__);
		return -EINVAL;
	}

	hal = tchg_alg_dev_get_drv_hal_data(alg);
	if(IS_ERR_OR_NULL(hal)){
		PE50_INFO("%s: hal is null\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < hal->support_ta_cnt; i++) {
		if (!hal->adapters[i])
			continue;

		if (strcmp(hal->adapters[i]->dev.kobj.name, "pd_adapter") == 0) {
			type = tadapter_dev_get_property(hal->adapters[i], PD_TYPE_TIMEOUT);
			break;
		}
	}

	PE50_INFO("%s type:%d\n", __func__, type);

	if (type == TC_PD_CONNECT_PE_READY_SNK_APDO)
		return ALG_READY;
	else if (type == TC_PD_CONNECT_NONE)
		return ALG_TA_CHECKING;
	else
		return ALG_TA_NOT_SUPPORT;
}

int pe50_hal_set_ichg(struct tchg_alg_device *alg, bool enable, u32 mA)
{
	int ret = 0;
	struct votable *total_ichg_vote = NULL;

	total_ichg_vote = find_votable("total_ichg");
	if (IS_ERR_OR_NULL(total_ichg_vote))
		return -ENODEV;

	ret = vote(total_ichg_vote, PE50_VOTER, enable, milli_to_micro(mA));
	if (ret < 0) {
		PE50_ERR("pe50 set ichg failed, ret = %d\n", ret);
	}

	return ret;
}

int pe50_hal_set_aicr(struct tchg_alg_device *alg, bool enable, u32 mA)
{
	int ret = 0;
	struct votable *total_aicr_vote = NULL;

	total_aicr_vote = find_votable("total_aicr");
	if (IS_ERR_OR_NULL(total_aicr_vote))
		return -ENODEV;

	ret = vote(total_aicr_vote, PE50_VOTER, enable, milli_to_micro(mA));
	if (ret < 0) {
		PE50_ERR("pe50 set aicr failed, ret = %d\n", ret);
	}

	return ret;
}

int pe50_hal_get_ichg(struct tchg_alg_device *alg, enum tchg_idx chgidx, u32 *mA)
{
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);
	int ret, uA = 0;

	if (chgtyp < 0)
		return chgtyp;

	ret = charger_dev_get_charging_current(hal->chgdevs[chgtyp], &uA);
	*mA = micro_to_milli(uA);

	return ret;
}

int pe50_hal_get_aicr(struct tchg_alg_device *alg, enum tchg_idx chgidx, u32 *mA)
{
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);
	int ret, uA = 0;

	if (chgtyp < 0)
		return chgtyp;

	ret = charger_dev_get_input_current(hal->chgdevs[chgtyp], &uA);
	*mA = micro_to_milli(uA);

	return ret;
}

int pe50_hal_is_vbuslowerr(struct tchg_alg_device *alg, enum tchg_idx chgidx,
			   bool *err)
{
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_is_vbuslowerr(hal->chgdevs[chgtyp], err);
}

int pe50_hal_get_adc_accuracy(struct tchg_alg_device *alg, enum tchg_idx chgidx,
			      enum pe50_adc_channel chan, int *val)
{
	int ret;
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);
	int _chan = to_chgclass_adc(chan);

	if (chgtyp < 0)
		return chgtyp;
	if (_chan < 0)
		return _chan;

	ret = charger_dev_get_adc_accuracy(hal->chgdevs[chgtyp], _chan, val,
					   val);
	if (ret < 0)
		return ret;
	if (_chan == ADC_CHANNEL_VBAT || _chan == ADC_CHANNEL_IBAT ||
	    _chan == ADC_CHANNEL_VBUS || _chan == ADC_CHANNEL_IBUS ||
	    _chan == ADC_CHANNEL_VOUT)
		*val = micro_to_milli(*val);
	return 0;
}

int pe50_hal_set_run_spec(struct tchg_alg_device *alg, enum tchg_idx chgidx, enum support_spec run_sepc)
{
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_set_run_spec(hal->chgdevs[chgtyp], run_sepc);
}

int pe50_hal_get_run_spec(struct tchg_alg_device *alg, enum tchg_idx chgidx, enum support_spec run_sepc)
{
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_get_run_spec(hal->chgdevs[chgtyp], run_sepc);
}

int pe50_hal_init_chip(struct tchg_alg_device *alg, enum tchg_idx chgidx)
{
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_init_chip(hal->chgdevs[chgtyp]);
}

int pe50_hal_is_chip_enabled(struct tchg_alg_device *alg, enum tchg_idx chgidx,
			   bool *err)
{
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_is_enabled(hal->chgdevs[chgtyp], err);
}

int pe50_enable_special_function(struct tchg_alg_device *alg, enum tchg_idx chgidx, int function)
{
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0)
		return chgtyp;
	return charger_dev_enable_special_function(hal->chgdevs[chgtyp],function);
}

const char * pe50_hal_get_adapter_name(struct tchg_alg_device *alg)
{
	struct pe50_hal *hal;

	if (alg == NULL) {
		PE50_INFO("%s: alg is null\n", __func__);
		return NULL;
	}

	hal = tchg_alg_dev_get_drv_hal_data(alg);
	if (IS_ERR_OR_NULL(hal)||IS_ERR_OR_NULL(hal->adapter)){
		pr_info("%s:get adapter name (NULL)\n", __func__);
		return NULL;
	}else{
		pr_info("%s:get adapter name (%s)\n", __func__,hal->adapter->dev.kobj.name);
		return hal->adapter->dev.kobj.name;
	}
}

int pe50_hal_init_adc(struct tchg_alg_device *alg, enum tchg_idx chgidx, bool en)
{
	int ret = 0;
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0 || hal == NULL)
		return -EINVAL;
	ret = charger_dev_init_adc(hal->chgdevs[chgtyp],en);
	pr_info("%s:adc init done(%d)!\n",__func__,ret);

	return ret;
}

int pe50_hal_dump_register(struct tchg_alg_device *alg, enum tchg_idx chgidx)
{
	int ret = 0;
	int chgtyp = to_chgtyp(chgidx);
	struct pe50_hal *hal = tchg_alg_dev_get_drv_hal_data(alg);

	if (chgtyp < 0 || hal == NULL)
		return -EINVAL;
	ret = charger_dev_dump_registers(hal->chgdevs[chgtyp]);
	pr_info("%s:dump_registers done(%d)!\n",__func__,ret);

	return ret;
}

int pe5_hal_get_bypass_energy(struct pe50_algo_info *info)
{
	int ret = tc_get_bypass_energy();

	PE50_INFO("%s :%d\n", __func__, ret);

	return ret;
}

int pe50_hal_set_tran_dev_prop(struct pe50_algo_info *info,enum pe50_tran_dev dev_name,enum tran_common_prop prop,const union com_propval *val)
{
	int ret = 0;
	struct tran_device *dev = NULL; 
	switch(dev_name){
	case PE50_TRAN_TC_CHG_DEV:
		if (IS_ERR_OR_NULL(dev)) {
			dev = tran_get_by_name("tc_charger");
			if (IS_ERR_OR_NULL(dev)){
				PE50_ERR("get tc_charger dev fail\n");
				return -EINVAL;
			}
		}
		break;
	default:
		return -EINVAL;
	}
	ret = tran_dev_set_prop(dev,prop,val);
	return ret;
}
