// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Transsion Inc.
 */

#define pr_fmt(fmt)  "[tc_battery] %s:" fmt, __func__

#include <linux/wait.h>
#include "tc_battery.h"
#include "tc_charger_class.h"
#include "tc_algorithm_class.h"
#include "tc_misc_intf.h"

static bool bms_check_batt_working_status(struct battery_manager *bms, enum batt_enum batt_flag);
void bms_start_alarm_timer(struct battery_manager *bms, int polling_interval);
void bms_battery_monitor_wakeup(struct battery_manager *bms);

static u8 bms_get_rtc_ui_soc(struct battery_manager *bms)
{
	struct nvmem_cell *cell;
	u8 *buf, data;

	cell = nvmem_cell_get(bms->dev, "state-of-charge");
	if (IS_ERR(cell)) {
		pr_err("[%s]get rtc cell fail\n", __func__);
		return 0;
	}

	buf = nvmem_cell_read(cell, NULL);
	nvmem_cell_put(cell);

	if (IS_ERR(buf)) {
		pr_err("[%s]read rtc cell fail\n", __func__);
		return 0;
	}

	pr_info("[%s] get rtc ui_soc val=%d\n", __func__, *buf);
	data = *buf;
	kfree(buf);

	return data;
}

static void bms_set_rtc_batt_soc(struct battery_manager *bms, u8 val)
{
	struct nvmem_cell *cell;
	u32 length = 1;
	int ret;

	cell = nvmem_cell_get(bms->dev, "state-of-charge");
	if (IS_ERR(cell)) {
		pr_err("[%s]get rtc cell fail\n", __func__);
		return;
	}

	ret = nvmem_cell_write(cell, &val, length);
	nvmem_cell_put(cell);

	if (ret != length)
		pr_err("[%s] write rtc cell fail\n", __func__);

	pr_info("[%s] set rtc ui_soc val=%d\n", __func__, val);
}

static enum power_supply_property bms_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
};

static int bms_psy_get_property(struct power_supply *psy, enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct battery_manager *bms = power_supply_get_drvdata(psy);
	union com_propval tran_val = {.intval = 0};
	struct tran_device *gauge_dev = tran_get_by_name("tc_gauge");
	int ret = 0;
#if IS_ENABLED(CONFIG_TC_TEMP_FORECAST)
	struct tran_device *temp_forecast_dev = NULL;
#endif
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		tran_val.intval = POWER_SUPPLY_STATUS_DISCHARGING;
		ret = tran_dev_get_prop(gauge_dev,
			TRAN_PROP_PSY_STATUS, &tran_val);
		if (ret != 0) {
			pr_err("batt get TRAN_PROP_PSY_STATUS failed(%d)\n", ret);
		}
		val->intval = tran_val.intval;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		tran_val.intval = POWER_SUPPLY_HEALTH_GOOD;
		ret = tran_dev_get_prop(gauge_dev,
			TRAN_PROP_PSY_HEALTH, &tran_val);
		if (ret != 0) {
			pr_err("batt get TRAN_PROP_PSY_HEALTH failed(%d)\n", ret);
		}
		val->intval = tran_val.intval;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		tran_val.intval = 1;
		ret = tran_dev_get_prop(gauge_dev,
			TRAN_PROP_PSY_PRESENT, &tran_val);
		if (ret != 0) {
			pr_err("batt get TRAN_PROP_PSY_PRESENT failed(%d)\n", ret);
		}
		val->intval = tran_val.intval;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		tran_val.intval = POWER_SUPPLY_TECHNOLOGY_LION;
		ret = tran_dev_get_prop(gauge_dev,
			TRAN_PROP_PSY_TECHNOLOGY, &tran_val);
		if (ret != 0) {
			pr_err("batt get TRAN_PROP_PSY_TECHNOLOGY failed(%d)\n", ret);
		}
		val->intval = tran_val.intval;
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		tran_val.intval = 1;
		ret = tran_dev_get_prop(gauge_dev,
			TRAN_PROP_PSY_CYCLE_COUNT, &tran_val);
		if (ret != 0) {
			pr_err("batt get TRAN_PROP_PSY_CYCLE_COUNT failed(%d)\n", ret);
		}
		val->intval = tran_val.intval;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = tran_dev_get_prop(bms->bms_dev, TRAN_PROP_BATT_SOC, &tran_val);
		if (ret < 0 || tran_val.intval < 0) {
			tran_val.intval = 50;
			pr_info("%s: UI_SOC:%d, read soc:%d, ret:%d\n",
				__func__, val->intval, tran_val.intval, ret);
		}
		ret = tran_dev_get_prop(gauge_dev,
			TRAN_PROP_PSY_CAPACITY, &tran_val);
		if (ret != 0) {
			pr_err("batt get TRAN_PROP_PSY_CAPACITY failed(%d)\n", ret);
		}
		val->intval = tran_val.intval;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		tran_dev_get_prop(bms->bms_dev, TRAN_PROP_BATT_NOW_CURR, &tran_val);

		ret = tran_dev_get_prop(gauge_dev,
			TRAN_PROP_PSY_CURRENT_NOW, &tran_val);
		if (ret != 0) {
			pr_err("batt get TRAN_PROP_PSY_CURRENT_NOW failed(%d)\n", ret);
		}
		val->intval = tran_val.intval;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		tran_dev_get_prop(bms->bms_dev, TRAN_PROP_BATT_AI, &tran_val);

		ret = tran_dev_get_prop(gauge_dev,
			TRAN_PROP_PSY_CURRENT_AVG, &tran_val);
		if (ret != 0) {
			pr_err("batt get TRAN_PROP_PSY_CURRENT_AVG failed(%d)\n", ret);
		}
		val->intval = tran_val.intval;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		tran_dev_get_prop(bms->bms_dev, TRAN_PROP_BATT_FCC, &tran_val);
		ret = tran_dev_get_prop(gauge_dev,
			TRAN_PROP_PSY_CHARGE_FULL, &tran_val);
		if (ret != 0) {
			pr_err("batt get TRAN_PROP_PSY_CHARGE_FULL failed(%d)\n", ret);
		}
		val->intval = tran_val.intval;
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		tran_dev_get_prop(bms->bms_dev, TRAN_PROP_BATT_RM, &tran_val);
		ret = tran_dev_get_prop(gauge_dev,
			TRAN_PROP_PSY_CHARGE_COUNTER, &tran_val);
		if (ret != 0) {
			pr_err("batt get TRAN_PROP_PSY_CHARGE_COUNTER failed(%d)\n", ret);
		}
		val->intval = tran_val.intval;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		tran_dev_get_prop(bms->bms_dev, TRAN_PROP_BATT_VOLT, &tran_val);
		ret = tran_dev_get_prop(gauge_dev,
			TRAN_PROP_PSY_VOLTAGE_NOW, &tran_val);
		if (ret != 0) {
			pr_err("batt get TRAN_PROP_PSY_VOLTAGE_NOW failed(%d)\n", ret);
		}
		val->intval = tran_val.intval;

		break;
	case POWER_SUPPLY_PROP_TEMP:
		tran_dev_get_prop(bms->bms_dev, TRAN_PROP_BATT_TEMP, &tran_val);
#if IS_ENABLED(CONFIG_TC_TEMP_FORECAST)
		temp_forecast_dev = tran_get_by_name("temp_forecast");
		ret = tran_dev_get_prop(temp_forecast_dev,TRAN_PROP_FORECAST_BATT_TEMP, &tran_val);
		if (ret == 0) {
			val->intval = tran_val.intval;
		}
#endif		
		ret = tran_dev_get_prop(gauge_dev, TRAN_PROP_PSY_TEMP, &tran_val);
		if (ret != 0) {
			pr_err("batt get TRAN_PROP_PSY_TEMP failed(%d)\n", ret);
		}
		val->intval = tran_val.intval;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		tran_val.intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
		ret = tran_dev_get_prop(gauge_dev,
			TRAN_PROP_PSY_CAPACITY_LEVEL, &tran_val);
		if (ret != 0) {
			pr_err("batt get TRAN_PROP_PSY_CAPACITY_LEVEL failed(%d)\n", ret);
		}
		val->intval = tran_val.intval;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		tran_val.intval = 0;
		ret = tran_dev_get_prop(gauge_dev,
			TRAN_PROP_PSY_TIME_TO_FULL_NOW, &tran_val);
		if (ret != 0) {
			pr_err("batt get TRAN_PROP_PSY_TIME_TO_FULL_NOW failed(%d)\n", ret);
		}
		val->intval = tran_val.intval;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		ret = tran_dev_get_prop(bms->bms_dev, TRAN_PROP_BATT_DC, &tran_val);
		if (tran_val.intval == 0)
			tran_val.intval = 450000;

		ret = tran_dev_get_prop(gauge_dev,
			TRAN_PROP_PSY_CHARGE_FULL_DESIGN, &tran_val);
		if (ret != 0) {
			pr_err("batt get TRAN_PROP_PSY_CHARGE_FULL_DESIGN failed(%d)\n", ret);
		}
		val->intval = tran_val.intval;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = tran_dev_get_prop(gauge_dev,
			TRAN_PROP_PSY_CONSTANT_CHARGE_VOLTAGE, &tran_val);
		if (ret != 0) {
			pr_err("batt get TRAN_PROP_PSY_CONSTANT_CHARGE_VOLTAGE failed(%d)\n", ret);
		}
		val->intval = tran_val.intval;
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

static int bms_psy_set_property(struct power_supply *psy,
			       enum power_supply_property prop,
			       const union power_supply_propval *val)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_TEMP:
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}


static int bms_prop_is_writeable(struct power_supply *psy,
				       enum power_supply_property prop)
{
	int ret;

	switch (prop) {
	case POWER_SUPPLY_PROP_TEMP:
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = 1;
		break;
	default:
		ret = 0;
		break;
	}
	return ret;
}

static void tc_battery_external_power_changed(struct power_supply *psy)
{
	struct battery_manager *bms = 
		(struct battery_manager *)power_supply_get_drvdata(psy);

	bms_battery_monitor_wakeup(bms);
}

static int bms_psy_register(struct battery_manager *bms)
{
	bms->bms_psy_desc.name = "battery";
	bms->bms_psy_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	bms->bms_psy_desc.properties = bms_props;
	bms->bms_psy_desc.num_properties = ARRAY_SIZE(bms_props);
	bms->bms_psy_desc.get_property = bms_psy_get_property;
	bms->bms_psy_desc.set_property = bms_psy_set_property;
	bms->bms_psy_desc.external_power_changed = tc_battery_external_power_changed;
	bms->bms_psy_desc.property_is_writeable = bms_prop_is_writeable;

	bms->bms_psy_cfg.drv_data = bms;
	bms->bms_psy = devm_power_supply_register(bms->dev,
						&bms->bms_psy_desc,
						&bms->bms_psy_cfg);
	if (IS_ERR(bms->bms_psy)) {
		pr_err("Failed to register battery psy\n");
		return PTR_ERR(bms->bms_psy);
	}

	return 0;
}

static int bms_check_device(struct battery_manager *bms)
{
	if (!bms->fg_a_dev)
		bms->fg_a_dev = tran_get_by_name(bms->fg_a_name);

	if (bms->dual_fg && !bms->fg_b_dev) {
		bms->fg_b_dev = tran_get_by_name(bms->fg_b_name);
	}

	if (!bms->tc_chg_dev)
		bms->tc_chg_dev = tran_get_by_name("tc_charger");

	if (IS_ERR_OR_NULL(bms->tc_chg_dev)) {
		pr_err("%s: get tc_chg_dev failed\n", __func__);
		return -ENODEV;
	}

	bms->chg_psy = power_supply_get_by_name("charger");
	if (IS_ERR_OR_NULL(bms->chg_psy)) {
		pr_err("get chg_psy failed\n");
	}

	bms->chg1_dev = get_charger_by_name("primary_chg");
	if (IS_ERR_OR_NULL(bms->chg1_dev)) {
		pr_err("get chg1_dev failed\n");
	}

	return 0;
}

struct tran_device *to_batt_dev(struct battery_manager *bms, enum batt_enum idx)
{
	struct tran_device *dev = NULL;

	switch (idx) {
	case BATTERY_MASTER:
		dev = bms->fg_a_dev;
		break;
	case BATTERY_SLAVE:
		dev = bms->fg_b_dev;
		break;
	default:
		return NULL;
	}

	return dev;
}

static int bms_dual_batt_control(struct battery_manager *bms, enum batt_enum idx, bool en)
{
	int ret = 0;
	struct tran_device *dev = to_batt_dev(bms, idx);
	union com_propval temp_val = {0, };

	pr_info("bms set %s to %d\n", batt_name[idx], en);

	if (IS_ERR_OR_NULL(dev)) {
		pr_info("battery error or null\n");
		return -ENODEV;
	}

	if (bms->is_batt_en[idx] == en) {
		pr_info("%s already %d, return\n", batt_name[idx], en);
		return ret;
	}

	temp_val.intval = en;
	ret = tran_dev_set_prop(dev, TRAN_PROP_BATT_EN, &temp_val);
	if (ret < 0) {
		pr_err("%s set %d failed, return\n", batt_name[idx], en);
		return ret;
	}

	ret = tran_dev_get_prop(dev, TRAN_PROP_BATT_EN, &temp_val);
	if (ret < 0) {
		pr_err("%s set %d failed, return\n", batt_name[idx], en);
		return ret;
	}

	if (temp_val.intval == en) {
		bms->is_batt_en[idx] = en;
		tran_dev_notify(bms->bms_dev,
			TRAN_DEV_NOTIFY_BATT_ONLINE_CHANGE, NULL);
	} else {
		ret = -EINVAL;
		pr_err("set batt en error, set:%d, get:%d!\n", en, temp_val.intval);
	}

	return ret;
}


static int bms_select_cv(struct battery_manager *bms, enum batt_enum idx)
{
	bool is_ffc = -1;
	int cv = 0;
	int ffc_alg_id = ALG_NONE;
	union com_propval prop = {.intval = 0};

        tran_dev_get_prop(bms->tc_chg_dev, TRAN_PROP_IS_FFC_CHR, &prop); 
	is_ffc = prop.intval;

        tran_dev_get_prop(bms->tc_chg_dev, TRAN_PROP_FFC_ALG_ID, &prop); 
	ffc_alg_id = prop.intval;

	if (is_ffc && ffc_alg_id == PE5_ID) {
		switch (idx) {
		case BATTERY_MASTER:
			tc_get_alg_prop(alg_name_array[PE5_ID],
					ALG_GET_MASTER_FFC_CV, &cv);
			break;
		case BATTERY_SLAVE:
			tc_get_alg_prop(alg_name_array[PE5_ID],
					ALG_GET_SLAVE_FFC_CV, &cv);
			break;
		default:
			break;
		}
	} else {
		switch (idx) {
		case BATTERY_MASTER:
			tran_dev_get_prop(bms->tc_chg_dev,
					TRAN_PROP_CHG_MASTER_CV, &prop); 
			break;
		case BATTERY_SLAVE:
			tran_dev_get_prop(bms->tc_chg_dev,
					TRAN_PROP_CHG_SLAVE_CV, &prop); 
			break;
		default:
			break;
		}
		cv = prop.intval / 1000;
	} 
	return cv;
}

static int bms_select_eoc(struct battery_manager *bms, enum batt_enum idx)
{
	bool is_ffc = -1;
	int eoc = 0;
	int ffc_alg_id = ALG_NONE;
	union com_propval prop = {.intval = 0};

        tran_dev_get_prop(bms->tc_chg_dev, TRAN_PROP_IS_FFC_CHR, &prop); 
	is_ffc = prop.intval;

        tran_dev_get_prop(bms->tc_chg_dev, TRAN_PROP_FFC_ALG_ID, &prop); 
	ffc_alg_id = prop.intval;

	if (is_ffc && ffc_alg_id == PE5_ID) {
		switch (idx) {
		case BATTERY_MASTER:
			tc_get_alg_prop(alg_name_array[PE5_ID],
					ALG_GET_MASTER_FFC_EOC, &eoc);
			break;
		case BATTERY_SLAVE:
			tc_get_alg_prop(alg_name_array[PE5_ID],
					ALG_GET_SLAVE_FFC_EOC, &eoc);
			break;
		default:
			break;
		
		}
	} else {
		switch (idx) {
		case BATTERY_MASTER:
			tran_dev_get_prop(bms->tc_chg_dev,
					TRAN_PROP_CHG_MASTER_EOC, &prop); 
			break;
		case BATTERY_SLAVE:
			tran_dev_get_prop(bms->tc_chg_dev,
					TRAN_PROP_CHG_SLAVE_EOC, &prop); 
			break;
		default:
			break;
		}
		eoc = prop.intval / 1000;
	} 
	return eoc;
}

static int bms_select_master_cc(struct battery_manager *bms)
{
	bool is_ffc = -1;
	int chg_cc = 0;
	int ffc_alg_id = ALG_NONE;
	union com_propval prop = {.intval = 0};

        tran_dev_get_prop(bms->tc_chg_dev, TRAN_PROP_IS_FFC_CHR, &prop); 
	is_ffc = prop.intval;

        tran_dev_get_prop(bms->tc_chg_dev, TRAN_PROP_FFC_ALG_ID, &prop); 
	ffc_alg_id = prop.intval;

	if (is_ffc && ffc_alg_id == PE5_ID) {
		tc_get_alg_prop(alg_name_array[PE5_ID],
				ALG_MASTER_STEP_CC, &chg_cc);
	} else {
                tran_dev_get_prop(bms->tc_chg_dev, TRAN_PROP_CHG_MASTER_CC, &prop); 
		chg_cc = prop.intval / 1000;
	} 
	return chg_cc;
}

static void bms_check_master_charging_done(struct battery_manager *bms)
{
	int eoc, cv;
	static int eoc_trigger_cnt = 0;
	struct battery_info *fg_a_data = bms->fg_a_data;

	if (bms->chg_status != POWER_SUPPLY_STATUS_CHARGING ||
		!bms->is_batt_en[BATTERY_MASTER] ||
		!bms->is_batt_en[BATTERY_SLAVE])
		return;

	cv = bms_select_cv(bms, BATTERY_MASTER);
	eoc = bms_select_eoc(bms, BATTERY_MASTER);

	pr_info("fg_a volt:%d, cv:%d, cv_gap:%d, fg_a cur:%d, eoc:%d",
		fg_a_data->voltage, cv, bms->master_sw_cv_gap, fg_a_data->batt_cur, eoc);

	if (fg_a_data->voltage >= cv - bms->master_sw_cv_gap && 
		fg_a_data->batt_cur > 0 &&
		fg_a_data->batt_cur <= eoc) { 
	
		if (++eoc_trigger_cnt < 3) {
			pr_info("master eoc_trigger_cnt++, %d\n", eoc_trigger_cnt);
			return;
		}
	
		eoc_trigger_cnt = 0;
		bms_dual_batt_control(bms, BATTERY_MASTER, false);
		tran_dev_notify(bms->bms_dev,
			TRAN_DEV_NOTIFY_BATT_MASTER_CHG_FULL, NULL);
	} else {
	       eoc_trigger_cnt = 0;
	}
}

static void bms_check_slave_charging_done(struct battery_manager *bms)
{
	int eoc, cv;
	static int eoc_trigger_cnt = 0;
	struct battery_info *fg_b_data = bms->fg_b_data;

	if (bms->chg_status != POWER_SUPPLY_STATUS_CHARGING ||
		!bms->is_batt_en[BATTERY_MASTER] ||
		!bms->is_batt_en[BATTERY_SLAVE])
		return;

	cv = bms_select_cv(bms, BATTERY_SLAVE);
	eoc = bms_select_eoc(bms, BATTERY_SLAVE);

	pr_info("fg_b volt:%d, cv:%d, cv_gap:%d, fg_b cur:%d, eoc:%d",
		fg_b_data->voltage, cv, bms->slave_sw_cv_gap, fg_b_data->batt_cur, eoc);

	if (fg_b_data->voltage >= cv - bms->slave_sw_cv_gap && 
		fg_b_data->batt_cur > 0 &&
		fg_b_data->batt_cur <= eoc) { 
	
		if (++eoc_trigger_cnt < 3) {
			pr_info("slave eoc_trigger_cnt++, %d\n", eoc_trigger_cnt);
			return;
		}
	
		eoc_trigger_cnt = 0;
		bms_dual_batt_control(bms, BATTERY_SLAVE, false);
		tran_dev_notify(bms->bms_dev,
			TRAN_DEV_NOTIFY_BATT_SLAVE_CHG_FULL, NULL);
	} else {
	       eoc_trigger_cnt = 0;
	}
}

static void bms_check_dual_batt_status(struct battery_manager *bms)
{
	int vol_diff = 0;
	struct battery_info *fg_a_data = bms->fg_a_data;
	struct battery_info *fg_b_data = bms->fg_b_data;

	if (bms->is_batt_en[BATTERY_MASTER] && bms->is_batt_en[BATTERY_SLAVE])
		return;

	if (bms->chg_status == POWER_SUPPLY_STATUS_DISCHARGING ||
	    bms->chg_status == POWER_SUPPLY_STATUS_NOT_CHARGING ||
	    bms->chg_status == POWER_SUPPLY_STATUS_FULL) {

		pr_err("%s: reopen all batt for charging off(%d)\n",
			__func__, bms->chg_status);
		bms_dual_batt_control(bms, BATTERY_MASTER, true);
		bms_dual_batt_control(bms, BATTERY_SLAVE, true);
		return;
	}

	vol_diff = fg_a_data->voltage - fg_b_data->voltage;

	if (abs(vol_diff) >= bms->max_voltage_diff) {
		pr_err("%s: reopen all batt for vol diff(%d)\n",
			__func__, vol_diff);
		bms_dual_batt_control(bms, BATTERY_MASTER, true);
		bms_dual_batt_control(bms, BATTERY_SLAVE, true);
		return;
	}

}

static void bms_balance_dual_batt_voltage(struct battery_manager *bms)
{
	int vol_diff = 0;
	bool batt_a_working = false;
	bool batt_b_working = false;
	int chg_lmt_curr = bms->min_lmt_chg_cur;
	struct battery_info *fg_a_data = bms->fg_a_data;
	struct battery_info *fg_b_data = bms->fg_b_data;

	if (!bms->lmt_chg_dev) {
		bms->lmt_chg_dev = get_charger_by_name("master_lmt_chg");
		if (!bms->lmt_chg_dev) {
			pr_info("Fail to get master_lmt_chg!");
			return;
		}
	}

	if (bms->chg_status == POWER_SUPPLY_STATUS_DISCHARGING ||
	    bms->chg_status == POWER_SUPPLY_STATUS_NOT_CHARGING ||
	    bms->chg_status == POWER_SUPPLY_STATUS_FULL) {

		pr_err("%s: not charging(%d), set lmt chg to lowpower mode\n",
			__func__, bms->chg_status);
		charger_dev_set_low_power_mode(bms->lmt_chg_dev, true);
		return;
	}

	charger_dev_set_low_power_mode(bms->lmt_chg_dev, false);

	batt_a_working = bms_check_batt_working_status(bms, BATTERY_MASTER);
	batt_b_working = bms_check_batt_working_status(bms, BATTERY_SLAVE);
	if (!batt_a_working || !batt_b_working) {
		pr_info("not in dual batt working!");
		goto out;
	}

	chg_lmt_curr = bms_select_master_cc(bms);

	vol_diff = fg_a_data->voltage - fg_b_data->voltage;
	if (vol_diff >= bms->start_balance_vol_diff) {
		chg_lmt_curr = chg_lmt_curr -
				(vol_diff - bms->start_balance_vol_diff) /
				bms->balance_step * bms->balance_step_cur;
	}

	chg_lmt_curr = max(chg_lmt_curr, bms->min_lmt_chg_cur);

	if (bms->chg_lmt_curr == chg_lmt_curr)
		return;

	charger_dev_dump_registers(bms->lmt_chg_dev);
out:
	charger_dev_set_charging_current(bms->lmt_chg_dev, chg_lmt_curr * 1000);

	bms->chg_lmt_curr = chg_lmt_curr;
}

static void bms_dual_batt_manage(struct battery_manager *bms)
{
	if (bms->run_mode != DUAL_BATT_MODE) {
		pr_err("%s single batt or no batt, return\n", __func__);
		return;
	}

	bms_check_master_charging_done(bms);
	bms_check_slave_charging_done(bms);
	bms_check_dual_batt_status(bms);
	bms_balance_dual_batt_voltage(bms);
}

static void bms_fg_mode_manage(struct battery_manager *bms)
{
	bool is_ffc = false;
	union com_propval prop = {.intval = 0};

	if (bms->chg_status != POWER_SUPPLY_STATUS_CHARGING &&
	    bms->chg_status != POWER_SUPPLY_STATUS_FULL)
		return;

        tran_dev_get_prop(bms->tc_chg_dev, TRAN_PROP_IS_FFC_CHR, &prop); 
	is_ffc = prop.intval;
	if (is_ffc) {
		prop.intval = 1;
		tran_dev_set_prop(bms->fg_a_dev, TRAN_PROP_BATT_FAST_CHG, &prop);
		tran_dev_set_prop(bms->fg_b_dev, TRAN_PROP_BATT_FAST_CHG, &prop);

	} else {
		prop.intval = 0;
		tran_dev_set_prop(bms->fg_a_dev, TRAN_PROP_BATT_FAST_CHG, &prop);
		tran_dev_set_prop(bms->fg_b_dev, TRAN_PROP_BATT_FAST_CHG, &prop);
	}

	pr_info("fast_mode:%d\n", is_ffc);
}

static int bms_battery_avg_current(struct battery_manager *bms)
{
	int fg_a_cur = 0;
	int fg_b_cur = 0;
	int avg_cur = 0;
	union com_propval val = {0, };
	int ret;

	if (bms->fg_a_dev)
		bms->fg_a_dev = tran_get_by_name(bms->fg_a_name);

	if (bms->fg_b_dev)
		bms->fg_b_dev = tran_get_by_name(bms->fg_b_name);

	ret = tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATT_AI, &val);
	if (!ret)
		fg_a_cur = val.intval;

	if (bms->dual_fg == true) {
		ret = tran_dev_get_prop(bms->fg_b_dev, TRAN_PROP_BATT_AI, &val);
		if (!ret)
			fg_b_cur = val.intval;
		avg_cur = ((fg_a_cur + fg_b_cur) / 1000);
	} else {
		avg_cur = fg_a_cur / 1000;
	}

	return avg_cur;
}

static void bms_cal_update_freq(struct battery_manager *bms, int raw_soc)
{
	int update_per_soc_time;
	int batt_ma_avg = 0;
	int design_full = bms->data.charge_design_full / 1000;

	if (design_full == 0 || raw_soc == 0) {
		update_per_soc_time = bms->update_per_soc_time_min;
	} else {
		batt_ma_avg = bms_battery_avg_current(bms);

		/* Calculate mAs for per soc:
		 *		per_mAs = design_full * 3600 /100 
		 * Calculate time for per soc based on the average current:
		 *		time = per_mAs / abs(batt_ma_avg)
		 * One third of the time is calculated as frequency:
		 *		time = time / 3 */
		update_per_soc_time = design_full * 36 / abs(batt_ma_avg) / 3;
		if (raw_soc < 2000)
			update_per_soc_time /= 2;
	}

	update_per_soc_time = max(update_per_soc_time, bms->update_per_soc_time_min);
	update_per_soc_time = min(update_per_soc_time, bms->update_per_soc_time_max);
	bms->update_per_soc_time = update_per_soc_time;

	bms->resume_polling_inerval = (abs(batt_ma_avg) >= 500) ?
			HIGH_UPDATE_FREQ : LOW_UPDATE_FREQ;

	pr_info("update_per_soc_time :%d resume_polling_inerval:%d, batt_ma_avg:%dmA\n",
		bms->update_per_soc_time, bms->resume_polling_inerval, batt_ma_avg);
}

static int check_low_cv_status(struct battery_manager *bms)
{
	int low_cv = false;
	int constant_voltage = 0;
	int ret = 0;

	if (IS_ERR_OR_NULL(bms->chg1_dev))
		bms->chg1_dev = get_charger_by_name("primary_chg");

	ret = charger_dev_get_constant_voltage(bms->chg1_dev, &constant_voltage);
	if (ret < 0) {
		pr_err("get constant_voltage fail ret:%d\n", ret);
		return low_cv;
	}
	constant_voltage = constant_voltage / 1000;
	pr_info("get constant_voltage succ, constant_voltage:%d, bms->smooth_soc_full_vol:%d\n",
		constant_voltage, bms->smooth_soc_full_vol);

	if (constant_voltage < bms->smooth_soc_full_vol)
		low_cv = true;

	return low_cv;
}

static int calculate_hidden_param(struct battery_manager *bms, int raw_soc)
{
	int i;
	int soc_up_gap, max_hidden_point;
	int hidden_param = 100 - bms->hidden_point_gap / 100;
	int hidden_point_gap = min((FULL_CAPACITY - bms->last_smooth_soc - 100), bms->hidden_point_gap);

	for (i = 99; i >= 90; i--) {
		soc_up_gap = raw_soc * 100 / i - bms->last_smooth_soc;
		max_hidden_point = FULL_CAPACITY * 100 / i - FULL_CAPACITY;
		if (max_hidden_point >= hidden_point_gap && soc_up_gap <= 100 && soc_up_gap >= 0) {
			hidden_param = i;
			pr_info("%s: can use hidden_param:%d\n", __func__, hidden_param);
			break;
		}
	}

	pr_debug("%s: use hidden_param:%d\n", __func__, hidden_param);

	return hidden_param;
}

static int bq_battery_soc_smooth_tracking(struct battery_manager *bms, int raw_soc)
{
	bool pull_full = false;
	bool pull_down = false;
	static int hidden_param = -1, hidden_rate = -1;
	static int last_chg_status = 0;
	int low_cv = false;
	int batt_ma_avg;
	int max_updata_soc = 0;
	int diff_soc = 0;
	int rtc_soc = -1;
	int smooth_soc = raw_soc;
	int last_smooth_soc = bms->last_smooth_soc;
	int last_raw_soc = bms->last_raw_soc;
	struct timespec64 time, diff_time;

	if (last_smooth_soc < 0) {
		rtc_soc = bms_get_rtc_ui_soc(bms);

		last_smooth_soc = rtc_soc * 100;

		if (last_smooth_soc <= 0 || last_smooth_soc > 
			(raw_soc + bms->boot_point_gap + bms->hidden_point_gap)) {

			last_smooth_soc = raw_soc * 10000 / (10000 - bms->hidden_point_gap);
		}


		bms->last_smooth_soc = last_smooth_soc;
		pr_info("last_smooth_soc:%d, raw_soc:%d, boot_gap:%d, hidden_point_gap:%d, rtc:%d\n",
			last_smooth_soc, raw_soc, bms->boot_point_gap, bms->hidden_point_gap, rtc_soc);
	}

	if (bms->last_raw_soc < 0) {
		bms->last_raw_soc = raw_soc;
		last_raw_soc = raw_soc;
	}

	ktime_get_boottime_ts64(&time);
	diff_time = timespec64_sub(time, bms->soc_update_time);

	bms_cal_update_freq(bms, raw_soc);

	max_updata_soc = diff_time.tv_sec * 100 / bms->update_per_soc_time;
	max_updata_soc = min(max_updata_soc, 100);

	/*hidden point start*/
	if (bms->hidden_point_gap > 0) {
		if ((bms->chg_status == POWER_SUPPLY_STATUS_CHARGING ||
			bms->chg_status == POWER_SUPPLY_STATUS_FULL) &&
			raw_soc >= last_raw_soc) {
	
			// calculate hidden parame
			hidden_rate = -1;
			if (hidden_param < 0)
				hidden_param = calculate_hidden_param(bms, raw_soc);

			smooth_soc = raw_soc * 100 / (hidden_param -bms->hidden_point_gap_accelerate);
			smooth_soc = min(smooth_soc, (raw_soc + bms->hidden_point_gap));
			pr_info("hidden_param:%d, raw_soc:%d, smooth_soc:%d",
					hidden_param, raw_soc, smooth_soc);
		} else {
			hidden_param = -1;
			last_raw_soc = min(last_raw_soc, 9800);
			if (hidden_rate < 0){
				hidden_rate = last_smooth_soc * 1000 / last_raw_soc;
				pr_info("current hidden point:%d\n", last_smooth_soc - last_raw_soc);
				if (abs(last_smooth_soc - last_raw_soc) > bms->hidden_point_gap)
						hidden_rate = min(hidden_rate, 1000 + bms->hidden_point_gap / 10);
			}
			smooth_soc = hidden_rate * raw_soc / 1000;
			pr_info("hidden_rate:%d, last_smooth_soc:%d, last_raw_soc:%d, smooth_soc:%d, raw_soc:%d",
				hidden_rate, last_smooth_soc, last_raw_soc, smooth_soc, raw_soc);
		}
	}
	/*hidden point end*/
	
	diff_soc = smooth_soc - last_smooth_soc;
	/* Forward drawing capacity */
	low_cv = check_low_cv_status(bms);

	/* charger down, pull full */
	if (!low_cv && bms->chg_status == POWER_SUPPLY_STATUS_FULL &&
		smooth_soc < FULL_CAPACITY &&
		smooth_soc >= bms->pull_full_soc_min) {
		pull_full = true;
		pr_err("should pull full!");
	}
	
	/* full down below gap, pull down */
	if (bms->chg_status == POWER_SUPPLY_STATUS_FULL &&
	    (bms->first_full_soc - smooth_soc) >= (FULL_CAPACITY -
		bms->pull_full_soc_min + bms->pull_full_soc_gap)) {
		pull_down = true;
		pr_err("should pull down!");
	}

	if (bms->chg_status == POWER_SUPPLY_STATUS_CHARGING) {
		if (smooth_soc >= last_smooth_soc) {
			smooth_soc = min((last_smooth_soc + max_updata_soc), smooth_soc);
		} else {
			
			batt_ma_avg = bms_battery_avg_current(bms);
			if (batt_ma_avg > 0) {
				/* Positive current , hold smooth_soc */
				smooth_soc = bms->last_smooth_soc;
				goto out;
			}
			smooth_soc = max((last_smooth_soc - max_updata_soc), smooth_soc);
		}
	
	} else if (bms->chg_status == POWER_SUPPLY_STATUS_DISCHARGING ||
		bms->chg_status == POWER_SUPPLY_STATUS_NOT_CHARGING) {

		if (smooth_soc <= last_smooth_soc) {
			smooth_soc = max((last_smooth_soc - max_updata_soc), smooth_soc);
		} else {
			smooth_soc = last_smooth_soc;
			pr_err("no charging, but soc increase, check!");
		}
	
	} else if (bms->chg_status == POWER_SUPPLY_STATUS_FULL) {
		if (last_chg_status != POWER_SUPPLY_STATUS_FULL)
			bms->first_full_soc = smooth_soc;

		if (pull_full && last_smooth_soc != FULL_CAPACITY &&
			diff_time.tv_sec >= bms->pull_full_polling_interval) {

			smooth_soc = last_smooth_soc + 100;
		} else if (pull_down && last_smooth_soc > smooth_soc &&
			diff_time.tv_sec >= bms->pull_full_polling_interval) {

			smooth_soc = max(smooth_soc, last_smooth_soc - 100);
			pr_err("smooth_soc too low, chase smooth_soc!");
		} else {
			smooth_soc = last_smooth_soc;
		}
	
	} else if (bms->chg_status == POWER_SUPPLY_STATUS_UNKNOWN) {
		pr_err("power supply status error, check!");
		smooth_soc = last_smooth_soc;
		goto out;
	}

out:
	last_chg_status = bms->chg_status;
	smooth_soc = max(0, min(smooth_soc, FULL_CAPACITY));
	if (smooth_soc != last_smooth_soc) {
		ktime_get_boottime_ts64(&bms->soc_update_time);
		bms->last_smooth_soc = smooth_soc;
	}
	bms->last_raw_soc = raw_soc;

	pr_err("smooth_soc:%d, last_smooth_soc:%d, raw_soc:%d,"
		"diff_soc:%d, soc_difftime:%lld, max_updata_soc:%d,"
		"chg_status:%d, pull_full:%d, low_cv:%d",
		smooth_soc, last_smooth_soc, raw_soc,
		diff_soc, (long long)diff_time.tv_sec, max_updata_soc,
		bms->chg_status, pull_full, low_cv);

	return smooth_soc;
}

static void bms_check_run_mode(struct battery_manager *bms)
{
	struct battery_info *fg_a_data = bms->fg_a_data;
	struct battery_info *fg_b_data = bms->fg_b_data;

	if (fg_a_data->present && fg_b_data->present)
		bms->run_mode = DUAL_BATT_MODE;
	else if (fg_a_data->present)
		bms->run_mode = SINGLE_MASTER_BATT_MODE;
	else if (fg_b_data->present)
		bms->run_mode = SINGLE_SLAVE_BATT_MODE;
	else
		bms->run_mode = NON_BATT_MODE;
}


static bool bms_check_batt_working_status(struct battery_manager *bms, enum batt_enum batt_flag)
{
	int ret = 0;
	union com_propval val = {0, };
	struct tran_device *dev = to_batt_dev(bms, batt_flag);

	if (IS_ERR_OR_NULL(dev)) {
		pr_info("battery error or null\n");
		return false;
	}

	ret = tran_dev_get_prop(dev,
			TRAN_PROP_BATT_STATUS, &val);
	if (val.intval < 0 || ret < 0) {
		pr_info("battery not online\n");
		return false;
	}

	ret = tran_dev_get_prop(dev,
			TRAN_PROP_BATT_EN, &val);
	if (ret != 0) {
		pr_info("error occur!\n");
		return false;
	}

	return !!val.intval;
}

static void bms_check_batt_info(struct battery_manager *bms, struct battery_info *data)
{
	struct battery_info *fg_a_data = bms->fg_a_data;
	struct battery_info *fg_b_data = bms->fg_b_data;
	union com_propval val = {0, };
	union power_supply_propval status = {0, };
	int ret;

	power_supply_get_property(bms->chg_psy,
		POWER_SUPPLY_PROP_STATUS, &status);
	bms->chg_status = status.intval;

	/* fg a batt info*/
	ret = tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_PRESENT, &val);
	if (ret == 0)
		fg_a_data->present = val.intval;
	else
		fg_a_data->present = false;

	if (fg_a_data->present) {
		ret = tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATT_TEMP, &val);
		if (!ret)
			fg_a_data->temperature = val.intval;
	
		ret = tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATT_FCC, &val);
		if (!ret)
			fg_a_data->charge_full = val.intval;
	
		ret = tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATT_SOC, &val);
		if (!ret) {
			fg_a_data->batt_soc = val.intval;
		}
	
		ret = tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATT_RM, &val);
		if (!ret)
			fg_a_data->rc = val.intval;
	
		ret = tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATT_DC, &val);
		if (!ret)
			fg_a_data->charge_design_full = val.intval;
	
		ret = tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATT_VOLT, &val);
		if (!ret)
			fg_a_data->voltage = val.intval / 1000;
	
		ret = tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATT_NOW_CURR, &val);
		if (!ret)
			fg_a_data->batt_cur = val.intval / 1000;

		fg_a_data->raw_soc = fg_a_data->rc * 10000 / (fg_a_data->charge_full / 1000);
		pr_info("fg_a data --- "
			"temperature:%d, charge_full:%d, "
			"voltage:%d, now_current:%d, "
			"rc:%d, raw_soc:%d, batt_soc:%d, "
			"charge_design_full:%d\n",
			fg_a_data->temperature, fg_a_data->charge_full,
			fg_a_data->voltage, fg_a_data->batt_cur,
			fg_a_data->rc, fg_a_data->raw_soc, fg_a_data->batt_soc,
			fg_a_data->charge_design_full);

	}

	/* fg b batt info*/
	ret = tran_dev_get_prop(bms->fg_b_dev, TRAN_PROP_PRESENT, &val);
	if (ret == 0)
		fg_b_data->present = val.intval;
	else
		fg_b_data->present = false;

	if (fg_b_data->present) {
		ret = tran_dev_get_prop(bms->fg_b_dev, TRAN_PROP_BATT_TEMP, &val);
		if (!ret)
			fg_b_data->temperature = val.intval;
	
		ret = tran_dev_get_prop(bms->fg_b_dev, TRAN_PROP_BATT_FCC, &val);
		if (!ret)
			fg_b_data->charge_full = val.intval;
	
		ret = tran_dev_get_prop(bms->fg_b_dev, TRAN_PROP_BATT_SOC, &val);
		if (!ret) {
			fg_b_data->batt_soc = val.intval;
		}
	
		ret = tran_dev_get_prop(bms->fg_b_dev, TRAN_PROP_BATT_RM, &val);
		if (!ret)
			fg_b_data->rc = val.intval;
	
		ret = tran_dev_get_prop(bms->fg_b_dev, TRAN_PROP_BATT_DC, &val);
		if (!ret)
			fg_b_data->charge_design_full = val.intval;
	
		ret = tran_dev_get_prop(bms->fg_b_dev, TRAN_PROP_BATT_VOLT, &val);
		if (!ret)
			fg_b_data->voltage = val.intval / 1000;
	
		ret = tran_dev_get_prop(bms->fg_b_dev, TRAN_PROP_BATT_NOW_CURR, &val);
		if (!ret)
			fg_b_data->batt_cur = val.intval / 1000;

		fg_b_data->raw_soc = fg_b_data->rc * 10000 / (fg_b_data->charge_full / 1000);
		pr_info("fg_b data ---"
			"temperature:%d, charge_full:%d, "
			"voltage:%d, now_current:%d, "
			"rc:%d, raw_soc:%d, batt_soc:%d, "
			"charge_design_full:%d\n",
			fg_b_data->temperature, fg_b_data->charge_full,
			fg_b_data->voltage, fg_b_data->batt_cur,
			fg_b_data->rc, fg_b_data->raw_soc, fg_b_data->batt_soc,
			fg_b_data->charge_design_full);
	}

	/* check run mode */
	bms_check_run_mode(bms);

	if (bms->run_mode == DUAL_BATT_MODE) {
		data->charge_design_full = fg_a_data->charge_design_full + fg_b_data->charge_design_full;
		data->temperature = max(fg_a_data->temperature, fg_b_data->temperature);
		data->charge_full = fg_a_data->charge_full + fg_b_data->charge_full;
		data->rc = fg_a_data->rc + fg_b_data->rc;
		data->raw_soc = min(data->rc * 10000 / (data->charge_full / 1000), 10000);
	} else if (bms->run_mode == SINGLE_MASTER_BATT_MODE) {
		data->charge_design_full = fg_a_data->charge_design_full;
		data->temperature = fg_a_data->temperature;
		data->charge_full = fg_a_data->charge_full;
		data->rc = fg_a_data->rc;
		data->raw_soc = min(data->rc * 10000 / (data->charge_full / 1000), 10000);

	} else if (bms->run_mode == SINGLE_SLAVE_BATT_MODE) {
		data->charge_design_full = fg_b_data->charge_design_full;
		data->temperature = fg_b_data->temperature;
		data->charge_full = fg_b_data->charge_full;
		data->rc = fg_b_data->rc;
		data->raw_soc = min(data->rc * 10000 / (data->charge_full / 1000), 10000);

	} else if (bms->run_mode == NON_BATT_MODE) {
		pr_info("%s: run_mode:%d\n", __func__, bms->run_mode);

	} else {
		pr_err("%s: unknow run mode, bms->run_mode:%d\n", __func__, bms->run_mode);

	}

	pr_info("total data --- "
		"temperature:%d, charge_full:%d, "
		"charge_design_full:%d, raw_soc:%d, "
		"run_mode:%d, chg_status:%d\n",
		data->temperature, data->charge_full,
		data->charge_design_full, data->raw_soc,
		bms->run_mode, bms->chg_status);
}

static int bms_update_status(struct battery_manager *bms)
{
	int ret = 0;
	struct battery_info data = {0, };
#if !IS_ENABLED(CONFIG_TC_CHARGE_TRANSFER)
	union com_propval tran_val = {.intval = 0};
#endif
	ret = bms_check_device(bms);
	if (ret < 0) {
		pr_info("bms check device failed, return;");
		return 0;
	}

	bms_check_batt_info(bms, &data);
	bms_dual_batt_manage(bms);
	bms_fg_mode_manage(bms);

	if (bms->run_mode != NON_BATT_MODE && data.raw_soc >= 0) {
		/* smooth tracking */
		data.smooth_soc = bq_battery_soc_smooth_tracking(bms, data.raw_soc);
#if IS_ENABLED(CONFIG_TC_ADAPTER_CONTROL)
		if(data.smooth_soc >= 9900) 
			data.smooth_soc = min(10000, data.smooth_soc+40);
#endif
		pr_info("old_smooth_soc:%d, new_smooth_soc:%d\n",
			bms->data.smooth_soc, data.smooth_soc);

	} else {
		data.smooth_soc = 5000;
		data.temperature = 250;
		goto out;
	}
out:
#if !IS_ENABLED(CONFIG_TC_CHARGE_TRANSFER)
		if (IS_ERR_OR_NULL(bms->tc_chg_dev))
			bms->tc_chg_dev = tran_get_by_name("tc_charger");
		tran_dev_get_prop(bms->tc_chg_dev, TRAN_PROP_GET_SOC_DECIMAL_SUPPORT, &tran_val);
		if(tran_val.intval){
			if (data.smooth_soc == 0) {
					data.batt_soc = 1;
				} else if (data.smooth_soc == 10000) {
					data.batt_soc = 100;
				} else {
					data.smooth_soc = data.smooth_soc + 99;
					data.batt_soc = (data.smooth_soc) / 100;
				}
			tran_val.intval = data.smooth_soc;
			tran_dev_set_prop(bms->tc_chg_dev,TRAN_PROP_SET_BATT_RAW_SOC, &tran_val);
		}
#endif
	//data.batt_soc = (data.smooth_soc + 50) / 100;
	/* keep 1 */
	if (data.smooth_soc > 0) {
		data.batt_soc = max(data.batt_soc, 1);
	}

	if (bms->data.batt_soc != data.batt_soc ||
		bms->data.temperature != data.temperature) {
		bms->data = data;
		power_supply_changed(bms->bms_psy);
		bms_set_rtc_batt_soc(bms, data.batt_soc);
		pr_info("%s: set rtc batt_soc:%d\n", __func__, data.batt_soc);
	}

	bms->data = data;

	return 0;
}

int bms_battery_monitor(void *data)
{
	int polling_interval;
	struct battery_manager *bms = data;

	while (!kthread_should_stop()) {
		wait_event_interruptible(bms->wait_que, bms->monitor_flag > 0 || kthread_should_stop());

		if (kthread_should_stop())
			goto out;

		__pm_stay_awake(bms->suspend_lock);
		mutex_lock(&bms->thread_lock);

		if (bms->monitor_flag > 0) {
			bms->monitor_flag = 0;
			bms_update_status(bms);
			bms->soc_ready = true;
		}

		if (bms->chg_status == POWER_SUPPLY_STATUS_CHARGING)
			polling_interval = bms->charging_interval;
		else
			polling_interval = bms->discharging_interval;

		bms_start_alarm_timer(bms, polling_interval);

		mutex_unlock(&bms->thread_lock);
		__pm_relax(bms->suspend_lock);
	}

out:
	return 0;
}

void bms_battery_monitor_wakeup(struct battery_manager *bms)
{
	bms->monitor_flag = 1;
	wake_up_interruptible(&bms->wait_que);
}

void bms_start_alarm_timer(struct battery_manager *bms, int polling_interval)
{
	struct timespec64 time, time_now;
	ktime_t temp_time;
	ktime_t ktime;
	int ret;

	/* If the timer was already set, cancel it */
	ret = alarm_try_to_cancel(&bms->alarm_timer);
	if (ret < 0) {
		pr_err("%s: callback was running, skip timer\n", __func__);
		return;
	}

	temp_time = ktime_get_boottime();
	time_now = ktime_to_timespec64(temp_time);

	time.tv_sec = time_now.tv_sec + polling_interval;
	time.tv_nsec = 0;

	ktime = ktime_set(time.tv_sec, time.tv_nsec);

	alarm_start(&bms->alarm_timer, ktime);

	pr_info("%s: alarm resume timer start:%d, %lld %ld\n", __func__,
		ret, (long long)time.tv_sec, time.tv_nsec);
}

static enum alarmtimer_restart
	bms_alarm_timer_func(struct alarm *alarm, ktime_t now)
{
	struct battery_manager *bms =
		container_of(alarm, struct battery_manager, alarm_timer);

	pr_info("%s: alarm timer func run succ\n", __func__);

	bms_battery_monitor_wakeup(bms);

	return ALARMTIMER_NORESTART;
}

static void bms_init_alarm_timer(struct battery_manager *bms)
{
	alarm_init(&bms->alarm_timer, ALARM_BOOTTIME,
		bms_alarm_timer_func);

	bms_start_alarm_timer(bms, 0);
	pr_info("%s: alarm timer init\n", __func__);
}

static void bms_parse_dt(struct battery_manager *bms)
{
	struct device_node *node = bms->dev->of_node;
	int ret;

	bms->dual_fg = of_property_read_bool(node, "dual_fuel_gauge");

	ret = of_property_read_u32(node, "master_sw_cv_gap", &bms->master_sw_cv_gap);
	if (ret < 0) {
		pr_err("%s: not config master_sw_cv_gap, use default\n", __func__);
		bms->master_sw_cv_gap = 15;
	}

	ret = of_property_read_u32(node, "slave_sw_cv_gap", &bms->slave_sw_cv_gap);
	if (ret < 0) {
		pr_err("%s: not config slave_sw_cv_gap, use default\n", __func__);
		bms->slave_sw_cv_gap = 5;
	}

	ret = of_property_read_u32(node, "max_voltage_diff", &bms->max_voltage_diff);
	if (ret < 0) {
		pr_err("%s: not config max_voltage_diff, use default\n", __func__);
		bms->max_voltage_diff = 250;
	}

	ret = of_property_read_u32(node, "start_balance_vol_diff", &bms->start_balance_vol_diff);
	if (ret < 0) {
		pr_err("%s: not config start_balance_vol_diff, use default\n", __func__);
		bms->start_balance_vol_diff = 150;
	}

	ret = of_property_read_u32(node, "balance_step", &bms->balance_step);
	if (ret < 0) {
		pr_err("%s: not config balance_step, use default\n", __func__);
		bms->balance_step = 5;
	}

	ret = of_property_read_u32(node, "balance_step_cur", &bms->balance_step_cur);
	if (ret < 0) {
		pr_err("%s: not config balance_step_cur, use default\n", __func__);
		bms->balance_step_cur = 700;
	}

	ret = of_property_read_u32(node, "min_lmt_chg_cur", &bms->min_lmt_chg_cur);
	if (ret < 0) {
		pr_err("%s: not config min_lmt_chg_cur, use default\n", __func__);
		bms->min_lmt_chg_cur = 500;
	}

	ret = of_property_read_u32(node, "charging_interval",
			&bms->charging_interval);
	if (ret < 0) {
		pr_err("%s: not config charging_interval, use default\n", __func__);
		bms->charging_interval = 5;
	}
	ret = of_property_read_u32(node, "discharging_interval",
			&bms->discharging_interval);
	if (ret < 0) {
		pr_err("%s: not config discharging_interval, use default\n", __func__);
		bms->discharging_interval = 10;
	}

	ret = of_property_read_u32(node, "pull_full_polling_interval",
			&bms->pull_full_polling_interval);
	if (ret < 0) {
		pr_err("%s: not config pull_full_polling_interval, use default\n", __func__);
		bms->pull_full_polling_interval = 15;
	}

	ret = of_property_read_u32(node, "pull_full_soc_min",
			&bms->pull_full_soc_min);
	if (ret < 0) {
		pr_err("%s: not config pull_full_soc_min, use default\n", __func__);
		bms->pull_full_soc_min = 8500;
	}

	ret = of_property_read_u32(node, "pull_full_soc_gap",
			&bms->pull_full_soc_gap);
	if (ret < 0) {
		pr_err("%s: not config pull_full_soc_gap, use default\n", __func__);
		bms->pull_full_soc_gap = 500;
	}

	ret = of_property_read_u32(node, "update_per_soc_time_min",
			&bms->update_per_soc_time_min);
	if (ret < 0) {
		pr_err("%s: not config update_per_soc_time_min, use default\n", __func__);
		bms->update_per_soc_time_min = 5;
	}

	ret = of_property_read_u32(node, "update_per_soc_time_max",
			&bms->update_per_soc_time_max);
	if (ret < 0) {
		pr_err("%s: not config update_per_soc_time_max, use default\n", __func__);
		bms->update_per_soc_time_max = 45;
	}

	ret = of_property_read_u32(node, "smooth_soc_full_vol",
			&bms->smooth_soc_full_vol);
	if (ret < 0) {
		pr_err("%s: not config smooth_soc_full_vol, use default\n", __func__);
		bms->smooth_soc_full_vol = 4300;
	}

	ret = of_property_read_u32(node, "boot_point_gap", &bms->boot_point_gap);
	if (ret < 0) {
		pr_err("%s: not config boot_point_gap, use default\n", __func__);
		bms->boot_point_gap = 500;
	}

	ret = of_property_read_u32(node, "hidden_point_gap", &bms->hidden_point_gap);
	if (ret < 0) {
		pr_err("%s: not config hidden_point_gap, use default\n", __func__);
		bms->hidden_point_gap = 100;
	}

	ret = of_property_read_u32(node, "hidden_point_gap_accelerate", &bms->hidden_point_gap_accelerate);
	if (ret < 0) {
		pr_err("%s: not config hidden_point_gap_accelerate, use default\n", __func__);
		bms->hidden_point_gap_accelerate = 0;
	}

	ret = of_property_read_string(node, "fg_a_name", &bms->fg_a_name);
	if (ret < 0)
		pr_err("read fg_a_name failed\n");


	ret = of_property_read_string(node, "fg_b_name", &bms->fg_b_name);
	if (ret < 0)
		pr_err("read fg_b_name failed\n");
}

static int bms_battery_get_property(struct tran_device *dev,
				enum tran_common_prop prop,
				union com_propval *val)
{
	struct battery_manager *bms = tran_get_data(dev);
	union com_propval fga_val = {0, };
	union com_propval fgb_val = {0, };
	union com_propval temp_val = {0, };

	if (bms == NULL)
		return 0;

	if (!bms->fg_a_dev)
		bms->fg_a_dev = tran_get_by_name(bms->fg_a_name);
	if (!bms->fg_b_dev)
		bms->fg_b_dev = tran_get_by_name(bms->fg_b_name);

	switch (prop) {
	case TRAN_PROP_BATT_TEMP:
		val->intval = bms->data.temperature;
		break;
	case TRAN_PROP_BATT_VOLT:
		tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATT_EN, &fga_val);
		if (fga_val.intval != 0)
			tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATT_VOLT, &temp_val);
		else
			tran_dev_get_prop(bms->fg_b_dev, TRAN_PROP_BATT_VOLT, &temp_val);
		val->intval = temp_val.intval;
		break;
	case TRAN_PROP_BATT_AI: // Average Current
		tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATT_AI, &fga_val);
		tran_dev_get_prop(bms->fg_b_dev, TRAN_PROP_BATT_AI, &fgb_val);
		val->intval = fga_val.intval + fgb_val.intval;
		break;
	case TRAN_PROP_BATT_STATUS:
		break;
	case TRAN_PROP_BATT_TTE: // time to Empty
		break;
	case TRAN_PROP_BATT_TTF: // time to Full
		break;
	case TRAN_PROP_BATT_FCC:
		val->intval = bms->data.charge_full;
		break;
	case TRAN_PROP_BATT_RM:
		tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATT_RM, &fga_val);
		tran_dev_get_prop(bms->fg_b_dev, TRAN_PROP_BATT_RM, &fgb_val);
		val->intval = (fga_val.intval + fgb_val.intval) * 1000;
		break;
	case TRAN_PROP_BATT_CC: // Cycle Count
		tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATT_CC, &fga_val);
		tran_dev_get_prop(bms->fg_b_dev, TRAN_PROP_BATT_CC, &fgb_val);
		if (bms->dual_fg) {
		        val->intval = (fga_val.intval + fgb_val.intval) / 2;
		} else {
		        val->intval = fga_val.intval;
		}
		break;
	case TRAN_PROP_BATT_SOC: // Relative State of Charge
		val->intval = bms->data.batt_soc;
		break;
	case TRAN_PROP_BATT_SOC_READY:
		val->intval = bms->soc_ready;
		break;
	case TRAN_PROP_BATT_SOH:
		break;
	case TRAN_PROP_BATT_DC: // Full Design
		val->intval = bms->data.charge_design_full;
		break;
	case TRAN_PROP_BATT_ALT_MAC:
		break;
	case TRAN_PROP_BATT_MAC_CHKSUM:
		break;
	case TRAN_PROP_BATT_A_WORKING:
		val->intval = bms_check_batt_working_status(bms, BATTERY_MASTER);
		break;
	case TRAN_PROP_BATT_B_WORKING:
		val->intval = bms_check_batt_working_status(bms, BATTERY_SLAVE);
		break;
	case TRAN_PROP_BATT_CHG_DONE:
		val->intval = temp_val.intval;
		break;
	case TRAN_PROP_BATT_RUN_MODE:
		val->intval = bms->run_mode;
		break;
	case TRAN_PROP_BATT_NOW_CURR:
		tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATT_NOW_CURR, &fga_val);
		tran_dev_get_prop(bms->fg_b_dev, TRAN_PROP_BATT_NOW_CURR, &fgb_val);
		val->intval = fga_val.intval + fgb_val.intval;
		break;
	case TRAN_PROP_BATTERY_RAW_CYCLE:
		if (!bms->dual_fg) {
			tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATTERY_RAW_CYCLE, &fga_val);
			val->intval = fga_val.intval;
			break;
		}
		tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATTERY_RAW_CYCLE, &fga_val);
		tran_dev_get_prop(bms->fg_b_dev, TRAN_PROP_BATTERY_RAW_CYCLE, &fgb_val);
		val->intval = fga_val.intval + fgb_val.intval;
		break;
	case TRAN_PROP_ACCURACY_UISOC:
		val->intval = bms->data.smooth_soc;
		break;
	case TRAN_PROP_MASTER_BATT_VOLT:
		tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATT_VOLT, &fga_val);
		val->intval = fga_val.intval;
		break;
	case TRAN_PROP_MASTER_BATT_NOW_CURR:
		tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATT_NOW_CURR, &fga_val);
		val->intval = fga_val.intval;
		break;
	case TRAN_PROP_MASTER_BATT_TEMP:
		tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATT_TEMP, &fga_val);
		val->intval = fga_val.intval;
		break;
	case TRAN_PROP_MASTER_BATT_EN:
		tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATT_EN, &fga_val);
		val->intval = fga_val.intval;
		break;
	case TRAN_PROP_SLAVE_BATT_VOLT:
		tran_dev_get_prop(bms->fg_b_dev, TRAN_PROP_BATT_VOLT, &fgb_val);
		val->intval = fgb_val.intval;
		break;
	case TRAN_PROP_SLAVE_BATT_NOW_CURR:
		tran_dev_get_prop(bms->fg_b_dev, TRAN_PROP_BATT_NOW_CURR, &fgb_val);
		val->intval = fgb_val.intval;
		break;
	case TRAN_PROP_SLAVE_BATT_TEMP:
		tran_dev_get_prop(bms->fg_b_dev, TRAN_PROP_BATT_TEMP, &fgb_val);
		val->intval = fgb_val.intval;
		break;
	case TRAN_PROP_SLAVE_BATT_EN:
		tran_dev_get_prop(bms->fg_b_dev, TRAN_PROP_BATT_EN, &fgb_val);
		val->intval = fgb_val.intval;
		break;
	case TRAN_PROP_FG_REAL_SOC:
	case TRAN_PROP_FG_VSOC:
	case TRAN_PROP_FG_CSOC:
		val->intval = bms->data.batt_soc;
		break;
	case TRAN_PROP_FG_DO_C:
	case TRAN_PROP_FG_DO_V:
	case TRAN_PROP_FG_AGING:
		val->intval = -1;
		break;
	case TRAN_PROP_BATTERY_CYCLE:
		if (!bms->dual_fg) {
			tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATTERY_RAW_CYCLE, &fga_val);
			val->intval = fga_val.intval;
			break;
		}
		tran_dev_get_prop(bms->fg_a_dev, TRAN_PROP_BATTERY_RAW_CYCLE, &fga_val);
		tran_dev_get_prop(bms->fg_b_dev, TRAN_PROP_BATTERY_RAW_CYCLE, &fgb_val);
		val->intval = fga_val.intval + fgb_val.intval;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int bms_battery_set_property(struct tran_device *dev,
					   enum tran_common_prop prop,
					   const union com_propval *val)
{
	struct battery_manager *bms = tran_get_data(dev);

	if (!bms->fg_a_dev)
		bms->fg_a_dev = tran_get_by_name(bms->fg_a_name);

	if (bms == NULL)
		return 0;

	switch (prop) {
	case TRAN_PROP_BATT_TEMP:
		pr_err("%s: val->intval:%d\n", __func__, val->intval);
		break;
	case TRAN_PROP_BATT_EN:
		tran_dev_set_prop(bms->fg_a_dev, prop, val);
		pr_err("%s: set fg en:%d\n", __func__, val->intval);
		break;
	case TRAN_PROP_SET_SOC_DECIMAL_RATE:
		if(val->intval == 100)
			bms->charging_interval = CHG_INTERVAL;
		else 
			bms->charging_interval = CHG_DEINTERVAL;
		bms_battery_monitor_wakeup(bms);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct tran_ops bms_batt_ops = {
	.set_prop = bms_battery_set_property,
	.get_prop = bms_battery_get_property,
};

static int bms_register_battdev(struct battery_manager *bms)
{
	bms->batt_prop.alias_name = "tran_batt";
	bms->bms_dev = tran_device_register("tran_batt", bms->dev,
						bms, &bms_batt_ops,
						&bms->batt_prop);

	if (!bms->bms_dev)
		return -EINVAL;

	return 0;
}

static int bms_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct battery_manager *bms = NULL;
	struct battery_info *fg_a_data = NULL;
	struct battery_info *fg_b_data = NULL;

	pr_info("%s\n", __func__);

	bms = devm_kzalloc(&pdev->dev, sizeof(*bms), GFP_KERNEL);
	if (!bms)
		return -ENOMEM;

	fg_a_data = devm_kzalloc(&pdev->dev, sizeof(*fg_a_data), GFP_KERNEL);
	if (!bms)
		return -ENOMEM;

	fg_b_data = devm_kzalloc(&pdev->dev, sizeof(*fg_b_data), GFP_KERNEL);
	if (!bms)
		return -ENOMEM;

	bms->dev = &pdev->dev;
	bms->fg_a_data = fg_a_data;
	bms->fg_b_data = fg_b_data;
	bms->data.batt_soc = 50;
	bms->data.temperature = 250;
	bms->run_mode = NON_BATT_MODE;
	bms->resume_polling_inerval = LOW_UPDATE_FREQ;
	bms->last_smooth_soc = -1;
	bms->last_raw_soc = -1;

	mutex_init(&bms->ops_lock);
	mutex_init(&bms->thread_lock);

	bms->suspend_lock =
		wakeup_source_register(NULL, "battery manager seriver");

	platform_set_drvdata(pdev, bms);

	bms_parse_dt(bms);

	init_waitqueue_head(&bms->wait_que);
	memset(bms->is_batt_en, 1, sizeof(bms->is_batt_en));
	bms->bms_monitor = kthread_run(bms_battery_monitor,
			bms, "battery_manager_serivce");
	if (IS_ERR(bms->bms_monitor)) {
		ret = PTR_ERR(bms->bms_monitor);
		pr_err("%s: fail to register bms kthread, ret:%d\n",
			__func__, ret);
		return ret;
	}

	ret = bms_check_device(bms);
	if (ret != 0) {
		pr_err("%s: bms check psy fail\n", __func__);
	}

	ret = bms_register_battdev(bms);
	if (ret < 0) {
		pr_err("%s: register battery device fail\n", __func__);
		return ret;
	}

	ret = bms_psy_register(bms);
	if (ret != 0) {
		pr_err("%s: Failed to register bms psy\n", __func__);
		return ret;
	}

	/* bms_init_alarm_hweoc_timer(bms); */
	bms_init_alarm_timer(bms);

	pr_info("%s done\n", __func__);

	return 0;
}

static int bms_remove(struct platform_device *pdev)
{
	struct battery_manager *bms = platform_get_drvdata(pdev);

	power_supply_unregister(bms->bms_psy);

	pr_info("%s\n", __func__);

	return 0;
}

static int bms_suspend(struct device *dev)
{
	pr_info("%s\n", __func__);
	return 0;
}

static int bms_resume(struct device *dev)
{
	pr_info("%s\n", __func__);
	return 0;
}

static int bms_prepare_suspend(struct device *dev)
{
	struct battery_manager *bms = dev_get_drvdata(dev);
	if (bms == NULL) {
		pr_err("%s: bms is null\n", __func__);
		return 0;
	}
	pr_info("%s\n", __func__);

	mutex_lock(&bms->thread_lock);

	bms_start_alarm_timer(bms, bms->resume_polling_inerval);
	return 0;
}

static void bms_complete_resume(struct device *dev)
{
	struct battery_manager *bms = dev_get_drvdata(dev);

	if (bms == NULL) {
		pr_err("%s: bms is null\n", __func__);
		return;
	}
	pr_info("%s\n", __func__);

	mutex_unlock(&bms->thread_lock);

	bms_battery_monitor_wakeup(bms);
}

static const struct dev_pm_ops battery_manager_pm_ops = {
	.prepare        = bms_prepare_suspend,
	.complete       = bms_complete_resume,
	.resume		= bms_resume,
	.suspend	= bms_suspend,
};

static const struct of_device_id battery_manager_match[] = {
	{ .compatible = "tran,battery_manager", },
	{ },
};

static struct platform_driver battery_manager_driver = {
	.probe = bms_probe,
	.remove = bms_remove,
	.driver = {
		.name = "battery-manager",
		.owner = THIS_MODULE,
		.pm = &battery_manager_pm_ops,
		.of_match_table = battery_manager_match,
	},
};

static s32 __init battery_manager_det_init(void)
{
	return platform_driver_register(&battery_manager_driver);
}

static void __exit battery_manager_det_exit(void)
{
	platform_driver_unregister(&battery_manager_driver);
}

module_init(battery_manager_det_init);
module_exit(battery_manager_det_exit);

MODULE_DESCRIPTION("mt-charger-detection");
MODULE_AUTHOR("MediaTek");
MODULE_LICENSE("GPL v2");

