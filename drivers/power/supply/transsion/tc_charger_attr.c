// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#include <linux/cdev.h>
#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/power_supply.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/proc_fs.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include "tc_charger.h"

struct nvram_chg_scene {
	int cycle_cnt;
	int last_bat_cycle;
};


static __maybe_unused void tc_charger_set_algo_log_level(struct tc_charger *info, int level)
{
	struct tc_data *data = info->data;
	struct tchg_alg_device *alg;
	int i = 0, ret = 0;

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = data->alg[i];
		if (alg == NULL)
			continue;

		ret = tchg_alg_set_prop(alg, ALG_LOG_LEVEL, level);
		if (ret < 0)
			tchr_err("%s: set ALG_LOG_LEVEL fail, ret =%d", __func__, ret);
	}
}

static struct tc_charger *tc_get_charger_info(void)
{
	struct tc_charger *info;
	struct tran_device *tc_charger = tran_get_by_name("tc_charger");

	if (IS_ERR_OR_NULL(tc_charger)) {
		pr_err("%s: get tc_charger failed\n",__func__);
		return NULL;
	}

	info = dev_get_drvdata(&tc_charger->dev);

	return info;
}

static ssize_t sw_jeita_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_desc *desc = info->desc;

	tchr_err("%s: %d\n", __func__, desc->enable_sw_jeita);
	return sprintf(buf, "%d\n", desc->enable_sw_jeita);
}

static ssize_t sw_jeita_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_desc *desc = info->desc;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		desc->enable_sw_jeita = (temp == 0) ? false : true;
		_wake_up_charger(info);
	} else {
		tchr_err("%s: format error!\n", __func__);
	}

	return size;
}

static DEVICE_ATTR_RW(sw_jeita);
/* sw jeita end*/

static ssize_t High_voltage_chg_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_desc *desc = info->desc;

	tchr_err("%s: hv_charging = %d\n", __func__, desc->enable_hv_charging);
	return sprintf(buf, "%d\n", desc->enable_hv_charging);
}

static ssize_t High_voltage_chg_enable_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_desc *desc = info->desc;
	int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		desc->enable_hv_charging = (temp == 0) ? false : true;
	} else {
		tchr_err("%s: format error!\n", __func__);
	}

	return size;
}

static DEVICE_ATTR_RW(High_voltage_chg_enable);

static ssize_t pd_type_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;
	char *pd_type_name = "None";

	switch (data->pd_type) {
	case TC_PD_CONNECT_NONE:
		pd_type_name = "None";
		break;
	case TC_PD_CONNECT_PE_READY_SNK:
		pd_type_name = "PD";
		break;
	case TC_PD_CONNECT_PE_READY_SNK_PD30:
		pd_type_name = "PD";
		break;
	case TC_PD_CONNECT_PE_READY_SNK_APDO:
		pd_type_name = "PD with PPS";
		break;
	case TC_PD_CONNECT_TYPEC_ONLY_SNK:
		pd_type_name = "normal";
		break;
	}
	tchr_err("%s: %d\n", __func__, data->pd_type);
	return sprintf(buf, "%s\n", pd_type_name);
}

static DEVICE_ATTR_RO(pd_type);

static ssize_t Pump_Express_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int i;
	int type = 0;
	int alg_id = ALG_NONE;
	int alias_type = TC_UNKNOWN;
	int is_fast_chr = 0;
	struct tchg_alg_device *alg;
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;

	if (IS_ERR_OR_NULL(info)) {
		tchr_err("%s: info is null\n", __func__);
		goto out;
	}
	
	alias_type = tc_get_alias_type();

	if (alias_type == TC_WIRELESS) {
		type = 3;
		goto out;
	}

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = data->alg[i];
		if (alg == NULL)
			continue;

		tchg_alg_get_prop(alg, 
			ALG_IS_FAST_CHR, &is_fast_chr);

		if (is_fast_chr) {
			alg_id = alg->alg_id;
			break;
		}
	}

	switch (alg_id) {
		case PE5_ID:
		case PDC_ID:
			type = 2;
			break;
		case PE2_ID:
			type = 4;
			break;
		case ALG_NONE:
		default:
			type = 0;
			break;

		
	}

out:
	tchr_err("%s: idx = %d, name:%s, type = %d\n",
		__func__, alg_id, alg_name_array[alg_id], type);

	return sprintf(buf, "%d\n", type);
}

static DEVICE_ATTR_RO(Pump_Express);

static ssize_t Charger_Type_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int i;
	enum charger_type type = CHARGER_UNKNOWN;
	int alg_id = ALG_NONE;
	int alias_type = TC_UNKNOWN;
	int is_fast_chr = 0;
	struct tchg_alg_device *alg;
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;

	if (IS_ERR_OR_NULL(info)) {
		tchr_err("%s: info is null\n", __func__);
		goto out;
	}

	alias_type = tc_get_alias_type();

	if (alias_type == TC_UNKNOWN) {
		type = CHARGER_UNKNOWN;
		goto out;
	}

	if (alias_type == TC_WIRELESS) {
		type = WIRELESS_CHARGER;
		goto out;
	}

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = data->alg[i];
		if (alg == NULL)
			continue;

		tchg_alg_get_prop(alg,
			ALG_IS_FAST_CHR, &is_fast_chr);

		if (is_fast_chr) {
			alg_id = alg->alg_id;
			break;
		}
	}

	switch (alg_id) {
		case PE5_ID:
			type = DV2_CHARGER;
			break;
		case PDC_ID:
		case PE2_ID:
			type = PE_CHARGER;
			break;
		case ALG_NONE:
		default:
			type = STANDARD_CHARGER;
			break;
	}

out:
	tchr_err("%s: idx = %d, name:%s, type = %d\n",
		__func__, alg_id, alg_name_array[alg_id], type);

	return sprintf(buf, "%d\n", type);
}

static DEVICE_ATTR_RO(Charger_Type);

static ssize_t adapter_capacity_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int i;
	int adapter_capacity = 0;
	int alias_type = TC_UNKNOWN;
	int is_fast_chr = 0;
	union com_propval tran_val = {0, };
	struct tchg_alg_device *alg;
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;
	struct tran_device *tc_wireless = NULL;
	if (IS_ERR_OR_NULL(info)) {
		tchr_err("%s: info is null\n", __func__);
		goto out;
	}

	if (data->monkey_flag == TRAN_AGING_KOM) {
		tchr_err("%s:in aging KOM mode, not feedback adapter capacity\n", __func__);
		return 0;
	}

	alias_type = tc_get_alias_type();

	if (alias_type == TC_WIRELESS) {
		tc_wireless = tran_get_by_name("tc_wireless");
		if (IS_ERR_OR_NULL(tc_wireless)) {
			tchr_err("%s: tc wireless is null\n", __func__);
			goto out;
		}

		tran_dev_get_prop(tc_wireless, TRAN_PROP_POWER_NOW, &tran_val);
		adapter_capacity = tran_val.intval;
		goto out;
	}

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = data->alg[i];
		if (alg == NULL)
			continue;

		tchg_alg_get_prop(alg, 
			ALG_IS_FAST_CHR, &is_fast_chr);

		if (is_fast_chr) {
			tchg_alg_get_prop(alg,
				ALG_ADAPTER_CAPACITY, &adapter_capacity);
			break;
		}
	}
out:
	tchr_err("%s: adapter_capacity = %d\n", __func__, adapter_capacity);
	return sprintf(buf, "%u\n", adapter_capacity);
}

static DEVICE_ATTR_RO(adapter_capacity);

static ssize_t alg_debug_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int i;
	int alg_id, alg_disabled;
	struct tchg_alg_device *alg;
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;

	if (sscanf(buf, "%d %d", &alg_id,&alg_disabled) == 2) {
		for (i = 0; i < MAX_ALG_NO; i++) {
			alg = data->alg[i];
			if (alg != NULL && alg->alg_id == alg_id)
				alg->is_disabled = !!alg_disabled;
		}
	} else {
		tchr_err("%s: debug param is error\n", __func__);
	}
	return size;
}

static ssize_t alg_debug_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int i, ret = 0;
	struct tchg_alg_device *alg;
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = data->alg[i];
		if (alg != NULL && alg->is_disabled) {
			ret += snprintf(buf + ret, PAGE_SIZE - ret, "alg:%s, is_disabled:%d\n", 
			                alg_name_array[alg->alg_id], alg->is_disabled);
			if (ret >= PAGE_SIZE)
				break;
		}
	}

	if (ret == 0)
		ret = sprintf(buf, "no algo is disabled\n");

	return ret;
}
static DEVICE_ATTR_RW(alg_debug);

static ssize_t bat_warning_cmd_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;
	int ret = 0;
	unsigned int val = 0;

	if (buf != NULL && size != 0) {
		ret = kstrtouint(buf, 10, &val);
		data->bat_warning_cmd = val;
	}
	pr_info("[%s] val: %d\n", __func__,val);

	return size;
}

static ssize_t bat_warning_cmd_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;

	return sprintf(buf, "%d\n",  data->bat_warning_cmd);
}
static DEVICE_ATTR_RW(bat_warning_cmd);

static ssize_t chgspeed_ctl_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;
	struct tchg_alg_device *alg = NULL;
	int is_fast_chr = 0;

	alg = get_tchg_alg_by_name("pe5");
	if(alg)
		tchg_alg_get_prop(alg, ALG_IS_FAST_CHR, &is_fast_chr);


	if(!is_fast_chr) {
		tchr_err("%s:is_fast_chr:%d, speed_owner:%d, chg_speed:%d\n", __func__,
				is_fast_chr, 0, data->chg_speed);
		return sprintf(buf, "%d:%d\n", 0, data->chg_speed);
 	}

	tchr_err("%s:is_fast_chr:%d, speed_owner:%d, chg_speed:%d\n",__func__, is_fast_chr,
				data->speed_owner, data->chg_speed);
	return sprintf(buf, "%d:%d\n", data->speed_owner, data->chg_speed);
}

static ssize_t chgspeed_ctl_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;
	int speed_owner;
	int chg_speed;
	int project_power,adapter_capacity;
	struct tchg_alg_device *alg = NULL;

	if (sscanf(buf, "%d:%d", &speed_owner,&chg_speed) == 2) {
		if(speed_owner >= TRAN_MULTI_OWNER_MAX || chg_speed >= TRAN_MULTI_SPEED_MAX ||
				speed_owner < TRAN_MULTI_OWNER_SYS || chg_speed < TRAN_MULTI_SPEED_HIGH) {
			data->speed_owner = TRAN_MULTI_OWNER_ANI;
			data->chg_speed = TRAN_MULTI_SPEED_MID;
			tchr_err("%s:invaild value, use default value\n",__func__);
			return size;
		}

		if(speed_owner == TRAN_MULTI_OWNER_SYS){
			data->speed_owner_old = speed_owner;
			data->chg_speed_old = chg_speed;
		}
		data->speed_owner = speed_owner;
		data->chg_speed = chg_speed;

		alg = get_tchg_alg_by_name("pe5");
		if (alg) {
			tchg_alg_set_prop(alg, ALG_MULTI_CHG_SPEED, data->chg_speed);
			tchg_alg_set_prop(alg, ALG_MULTI_CHG_SPEED_OWNER, data->speed_owner);

			tchg_alg_get_prop(alg,ALG_PROJECT_POWER, &project_power);
			tchg_alg_get_prop(alg,ALG_ADAPTER_CAPACITY, &adapter_capacity);
			if(adapter_capacity >= project_power)
				tc_chgstat_chgspeed(info);

			tchr_err("%s:info->speed_owner:%d, info->chg_speed:%d\n", __func__,
					data->speed_owner, data->chg_speed);
			tchr_err("%s:info->speed_owner_old:%d, info->chg_speed_old:%d\n", __func__,
					data->speed_owner_old, data->chg_speed_old);
			tchr_err("%s:project_power=%d,adapter_capacity=%d\n", __func__,
					project_power, adapter_capacity);
		}
	} else {
		tchr_err("%s:Invalid command\n",__func__);
	}

	return size;
}
static DEVICE_ATTR_RW(chgspeed_ctl);

static ssize_t hvdcp30_ctrl_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct tran_device *usb_control_dev = NULL;
	union com_propval tran_val = {0, };
	int val = 0;

        usb_control_dev = tran_get_by_name("usb_control");
	if (IS_ERR_OR_NULL(usb_control_dev)) {
		pr_err("get usb_control_dev failed\n");
		return size;
	}

	if (kstrtoint(buf, 10, &val) == 0) {
		if (val == 0) {
			tran_val.intval = HVDCP30_NONE;
			tran_dev_set_prop(usb_control_dev, TRAN_PROP_USB_CTRL_HVDCP30, &tran_val);
		} else if (val == 1) {
			tran_val.intval = HVDCP30_HANDSHAKE;
			tran_dev_set_prop(usb_control_dev, TRAN_PROP_USB_CTRL_HVDCP30, &tran_val);
		} else if (val == 2) {
			tran_val.intval = HVDCP30_DP_PULSE;
			tran_dev_set_prop(usb_control_dev, TRAN_PROP_USB_CTRL_HVDCP30, &tran_val);
		} else if (val == 3) {
			tran_val.intval = HVDCP30_DM_PULSE;
			tran_dev_set_prop(usb_control_dev, TRAN_PROP_USB_CTRL_HVDCP30, &tran_val);
		}
	}

	return size;
}
static DEVICE_ATTR_WO(hvdcp30_ctrl);

static ssize_t Charging_mode_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int ret = 0, i = 0;
	bool is_ta_detected = false;
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;
	struct tchg_alg_device *alg = NULL;

	if (!info) {
		tchr_err("%s: info is null\n", __func__);
		return sprintf(buf, "%d\n", is_ta_detected);
	}

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = data->alg[i];
		if (alg == NULL)
			continue;
		ret = tchg_alg_is_algo_running(alg);
		if (ret == ALG_RUNNING) {
			is_ta_detected = true;
			break;
		}
	}
	if (alg == NULL)
		return sprintf(buf, "ALG_NOT_RUNNING\n");

	tchr_err("%s: charging_mode: %s\n", __func__, alg_name_array[alg->alg_id]);
	return sprintf(buf, "%s\n", alg_name_array[alg->alg_id]);
}

static DEVICE_ATTR_RO(Charging_mode);

static ssize_t ADC_Charger_Voltage_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;
	int vbus = tc_get_vbus(); /* mV */
	int boot_status = charger_dev_get_boost_status(data->chg1_dev);
	if (boot_status == 1 && data->kpoc)
		vbus = 0;
	tchr_err("%s: %d\n", __func__, vbus);
	return sprintf(buf, "%d\n", vbus);
}

static DEVICE_ATTR_RO(ADC_Charger_Voltage);

static ssize_t ADC_Charging_Current_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ibat = tc_get_battery_current(); /* mA */

	tchr_err("%s: %d\n", __func__, ibat);
	return sprintf(buf, "%d\n", ibat);
}

static DEVICE_ATTR_RO(ADC_Charging_Current);

static ssize_t input_current_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;
	int aicr = 0;

	aicr = data->chg_data[CHG1_SETTING].input_current_limit;
	tchr_err("%s: %d\n", __func__, aicr);
	return sprintf(buf, "%d\n", aicr);
}

static ssize_t input_current_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;
	struct charger_data *chg_data;
	signed int temp;

	chg_data = &data->chg_data[CHG1_SETTING];
	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp < 0)
			chg_data->input_current_limit = 0;
		else
			chg_data->input_current_limit = temp;
	} else {
		tchr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(input_current);

static ssize_t charger_log_level_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_desc *desc = info->desc;

	tchr_err("%s: %d\n", __func__, desc->log_level);
	return sprintf(buf, "%d\n", desc->log_level);
}

static ssize_t charger_log_level_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_desc *desc = info->desc;
	int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp < 0) {
			tchr_err("%s: val is invalid: %d\n", __func__, temp);
			temp = 0;
		}
		desc->log_level = temp;
		tchr_err("%s: log_level=%d\n", __func__, desc->log_level);

	} else {
		tchr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(charger_log_level);

static ssize_t BatteryNotify_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;
	unsigned int notify_code = 0;

	if (!data->kpoc) {
		notify_code = data->report_code;
		tchr_info("%s:notify_code 0x%x  report_code:0x%x\n", __func__, data->notify_code, data->report_code);
	} else {
		notify_code = data->notify_code;
		tchr_info("%s:kpoc charge, notify_code:0x%x\n", __func__, data->notify_code);
	}

	return sprintf(buf, "%u\n", notify_code);;
}

static ssize_t BatteryNotify_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;
	unsigned int reg = 0;
	int ret = 0;

	if (buf != NULL && size != 0) {
		ret = kstrtouint(buf, 16, &reg);
		if (ret < 0) {
			tchr_err("%s: failed, ret = %d\n", __func__, ret);
			return ret;
		}
		data->notify_code = reg;
		tchr_info("%s: store code=0x%x\n", __func__, data->notify_code);
		tc_chgstat_notify(info);
	}
	return size;
}

static DEVICE_ATTR_RW(BatteryNotify);

static ssize_t TranBatteryNotify_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return BatteryNotify_show(dev, attr, buf);
}

static ssize_t TranBatteryNotify_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;
	unsigned int reg = 0;
	int ret = 0;

	if (buf != NULL && size != 0) {
		ret = kstrtouint(buf, 16, &reg);
		if (ret < 0) {
			tchr_err("%s: failed, ret = %d\n", __func__, ret);
			return ret;
		}
		data->notify_code = reg;
		tchr_info("%s: store code=0x%x\n", __func__, data->notify_code);
		tc_chgstat_notify(info);
	}
	return size;
}

static DEVICE_ATTR_RW(TranBatteryNotify);


static ssize_t Si_Battery_Poweroff_Voltage_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_desc *desc = info->desc;
	int bat_cycle;
	int si_off_vol;
	int i;

	if(!desc->support_si_bat){
		si_off_vol = -1;
		goto out;
	}

	bat_cycle = tc_get_battery_cycle();
	for(i = 0; i < SI_BAT_CYCLE_MAX; i++){
		if(bat_cycle <= desc->si_bat_cycle_def[i]){
			si_off_vol = desc->si_bat_cycle_vol[i];
			break;
		}else{
			si_off_vol = desc->si_bat_cycle_vol[SI_BAT_CYCLE_MAX - 1];
		}
	}
out:
	tchr_info("%s:si battery power off val:%d\n", __func__,si_off_vol);

	return sprintf(buf, "%d\n", si_off_vol);
}

static DEVICE_ATTR_RO(Si_Battery_Poweroff_Voltage);

static ssize_t adapter_control_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tran_device *ac_ctl_dev = NULL;
	union com_propval tran_val = {0, };
	int adapter_control = 0;

	ac_ctl_dev = tran_get_by_name("adapter_control");
	if (IS_ERR_OR_NULL(ac_ctl_dev)){
		tchr_info("%s Couldn't get adapter_control\n", __func__);
		return sprintf(buf, "%d\n", adapter_control);
	}

	tran_dev_get_prop(ac_ctl_dev, TRAN_PROP_ADAPTER_SWITCH_STATUS, &tran_val);
	adapter_control = tran_val.intval;
	tchr_info("%s = %d\n", __func__, adapter_control);

	return sprintf(buf, "%d\n", adapter_control);
}
static DEVICE_ATTR_RO(adapter_control);

static ssize_t charger_unlimited_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_desc *desc = info->desc;

	tchr_err("%s: %d\n", __func__, desc->charger_unlimited);
	return sprintf(buf, "%d\n", desc->charger_unlimited);
}

static ssize_t charger_unlimited_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_desc *desc = info->desc;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		desc->charger_unlimited = !!temp;
		_wake_up_charger(info);
	} else {
		tchr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(charger_unlimited);

static ssize_t sw_safety_timer_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_desc *desc = info->desc;

	tchr_err("%s: %d, %d\n", __func__,
		desc->enable_sw_safety_timer, desc->max_charging_time);
	return sprintf(buf, "en:%d, max_time:%d\n",
		desc->enable_sw_safety_timer, desc->max_charging_time);
}

static ssize_t sw_safety_timer_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_desc *desc = info->desc;
	int enable_sw_safety_timer = 0;
	int max_charging_time = 0;

	if (sscanf(buf, "%d %d", &enable_sw_safety_timer, &max_charging_time) == 2) {
		desc->enable_sw_safety_timer = !!enable_sw_safety_timer;
		desc->max_charging_time = max_charging_time;
		_wake_up_charger(info);
	} else if (sscanf(buf, "%d", &enable_sw_safety_timer) == 1) {
		desc->enable_sw_safety_timer = !!enable_sw_safety_timer;
		_wake_up_charger(info);
	} else {
		tchr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(sw_safety_timer);

static ssize_t dynamic_mivr_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_desc *desc = info->desc;

	return sprintf(buf, "en: %d\nval: %d, %d, %d, %d\n",
		desc->enable_dynamic_mivr,
		desc->dynamic_mivr_vol[0], desc->dynamic_mivr_vol[1],
		desc->dynamic_mivr_vol[2], desc->dynamic_mivr_vol[3]);
}

static ssize_t dynamic_mivr_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_desc *desc = info->desc;
	int enable_dynamic_mivr = false;
	int dynamic_mivr_vol[DYNAMIC_MIVR_MAX] = {0};

	if (sscanf(buf, "%d %d %d %d %d", &enable_dynamic_mivr,
		    &dynamic_mivr_vol[0], &dynamic_mivr_vol[1],
		    &dynamic_mivr_vol[2], &dynamic_mivr_vol[3]) == 5) {

		desc->enable_dynamic_mivr = enable_dynamic_mivr;
		memcpy(&desc->dynamic_mivr_vol, dynamic_mivr_vol,
				sizeof(desc->dynamic_mivr_vol));
	} else if (sscanf(buf, "%d", &enable_dynamic_mivr) == 1) {
		desc->enable_dynamic_mivr = enable_dynamic_mivr;
	} else {
		tchr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(dynamic_mivr);

static ssize_t enable_hv_charging_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_desc *desc = info->desc;

	tchr_err("%s: %d\n", __func__, desc->enable_hv_charging);
	return sprintf(buf, "%d\n", desc->enable_hv_charging);
}

static ssize_t enable_hv_charging_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_desc *desc = info->desc;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		desc->enable_hv_charging = !!temp;
		_wake_up_charger(info);
	} else {
		tchr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(enable_hv_charging);

static ssize_t ir_comp_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_desc *desc = info->desc;

	return sprintf(buf, "en:%d, r_comp:%d, v_comp_max:%d\n",
		desc->enable_ir_comp, desc->r_comp, desc->v_comp_max);
}

static ssize_t ir_comp_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_desc *desc = info->desc;
	int enable_ir_comp = 0;
	int r_comp = 0;
	int v_comp_max = 0;

	if (sscanf(buf, "%d %d %d", &enable_ir_comp, &r_comp, &v_comp_max) == 3) {
		desc->enable_ir_comp = !!enable_ir_comp;
		desc->r_comp = r_comp;
		desc->v_comp_max = v_comp_max;
	} else if (sscanf(buf, "%d", &enable_ir_comp) == 1) {
		desc->enable_ir_comp = !!enable_ir_comp;
	} else {
		tchr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(ir_comp);

static ssize_t BATTERY_QMAX_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;
	union com_propval prop = {.intval = 0};
	int ret = 0;
	int qmax = 5000;

	ret = tc_charger_check_tran_dev_ptr(&data->gauge_dev, "tc_gauge");
	if (ret < 0) {
		tchr_info("%s Couldn't get tc_gauge\n", __func__);
		goto out;
	}

	tran_dev_get_prop(data->gauge_dev, TRAN_PROP_CHARGE_FULL_DESIGN, &prop);

	qmax = prop.intval / 100;
	
out:
	return sprintf(buf, "%d\n", qmax);
}


static DEVICE_ATTR_RO(BATTERY_QMAX);


static ssize_t CHG_CAPACITY_TEST_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;

	tchr_err("%s: monkey_flag = %d\n", __func__, data->monkey_flag);
	return sprintf(buf, "%d\n", data->monkey_flag);
}

static ssize_t CHG_CAPACITY_TEST_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		data->monkey_flag = temp;
		tchr_err("%s: monkey_flag = %d\n", __func__, data->monkey_flag);
		_wake_up_charger(info);
	} else {
		tchr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(CHG_CAPACITY_TEST);

static ssize_t fg_coulomb_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;
	union com_propval prop = {.intval = 0};
	int ret = 0;
	int hw_car = 0;

	ret = tc_charger_check_tran_dev_ptr(&data->gauge_dev, "tc_gauge");
	if (ret < 0) {
		tchr_info("%s Couldn't get tc_gauge\n", __func__);
		goto out;
	}

#if IS_ENABLED(CONFIG_TC_BATTERY)
	tran_dev_get_prop(data->gauge_dev, TRAN_PROP_BATT_RM, &prop);

	hw_car = prop.intval / 1000;
#else
	tran_dev_get_prop(data->gauge_dev, TRAN_PROP_FG_HW_CAR, &prop);

	hw_car = prop.intval;
#endif

out:
	tchr_err("%s: HW_CAR = %d\n", __func__, hw_car);
	return sprintf(buf, "%d\n", hw_car);
}

static DEVICE_ATTR_RO(fg_coulomb);


static ssize_t tran_bat_temp_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int bat_temp = 25;
	
	bat_temp = tc_get_battery_temperature();

	return sprintf(buf, "%d\n", bat_temp);
}

static DEVICE_ATTR_RO(tran_bat_temp);

static ssize_t tran_set_current_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;
	struct charger_data *total_pdata = &data->total_pdata;

	return sprintf(buf, "%d,%d\n", total_pdata->charging_current_limit,
		total_pdata->input_current_limit);
}

static DEVICE_ATTR_RO(tran_set_current);

static ssize_t tran_cam_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", tc_get_tpcb_temp());
}

static DEVICE_ATTR_RO(tran_cam);

static ssize_t battery_health_percent_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_desc *desc = info->desc;
	int bat_cycle;
	int bat_health_percent;

	bat_cycle = tc_get_battery_cycle();

	if (bat_cycle > desc->bat_health_cycle_base)
	    bat_health_percent = BATTERY_HEALTH_FULL - (bat_cycle - desc->bat_health_cycle_base) / desc->bat_health_dec_times;
	else
		bat_health_percent = BATTERY_HEALTH_FULL;

    tchr_err("bat cycle = %d, health percent = %d\n",
            bat_cycle, bat_health_percent);

    return sprintf(buf, "%d\n", bat_health_percent);
}

static DEVICE_ATTR_RO(battery_health_percent);

static ssize_t battery_health_level_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_desc *desc = info->desc;
	int bat_cycle;
	int bat_level = BAT_HEALTH_LEVEL_EXCELLENT;
	int i;

	bat_cycle = tc_get_battery_cycle();

	for (i = 0; i < BAT_HEALTH_LEVEL_MAX; i++) {
		if (bat_cycle < desc->bat_level_def[i]) {
			bat_level = desc->bat_level_code[i];
			break;
		}
	}

        return sprintf(buf, "%d\n", bat_level);
}

static DEVICE_ATTR_RO(battery_health_level);

static ssize_t OTG_CTL_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	union com_propval val;

	struct tran_device *tc_otg = tran_get_by_name("tc_otg");
	if (IS_ERR_OR_NULL(tc_otg)) {
		dev_err(dev, "get tc_otg dev failed!\n");
		return -ENODEV;
	}
	tran_dev_get_prop(tc_otg, TRAN_PROP_OTG_CTL_TYPE, &val);

	return sprintf(buf, "%u\n", val.intval);
}

static ssize_t OTG_CTL_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	union com_propval set_val;
	unsigned int val;
	int ret;

	struct tran_device *tc_otg = tran_get_by_name("tc_otg");
	if (IS_ERR_OR_NULL(tc_otg)) {
		dev_err(dev, "get tc_otg dev failed!\n");
		return -ENODEV;
	}

	ret = kstrtouint(buf, 10, &val);
	if (ret) {
    	pr_err("Failed to convert string to unsigned int\n");
    	return ret;
	}
	set_val.intval = val;
	tran_dev_set_prop(tc_otg, TRAN_PROP_OTG_CTL_TYPE, &set_val);

	return size;
}

static DEVICE_ATTR_RW(OTG_CTL);

static ssize_t smart_mincur_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t size)
{
	int ret;
	unsigned int val = 0;
	struct tc_charger *info = tc_get_charger_info();
	struct tc_desc *desc = info->desc;


	ret = kstrtouint(buf, 10, &val);
	if (ret) {
		pr_err("Failed to convert string to unsigned int\n");
		return ret;
	}
	pr_info("[%s] set smtchg_curr_min: %d\n", __func__,val);

	desc->smtchg_curr_min = val;

	return size;
}

static ssize_t smart_mincur_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct tc_charger *info = tc_get_charger_info();
	struct tc_desc *desc = info->desc;

	return sprintf(buf, "min_current = %duA\n", desc->smtchg_curr_min);
}

static ssize_t smart_charging_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t size)
{
	int ret;
	unsigned int val = 0;
	struct tc_charger *info = tc_get_charger_info();
	struct tc_data *data = info->data;
	ret = kstrtouint(buf, 10, &val);
	if (ret) {
		pr_err("Failed to convert string to unsigned int\n");
		return ret;
	}
	pr_info("[%s] val: %d\n", __func__,val);

	mutex_lock(&data->wait_protocol_lock);
	data->wait_protocol_array[SMART_CHG].value = val;
	data->wait_protocol_array[SMART_CHG].changed = true;

	schedule_delayed_work(&data->wait_protocol_work, 0);
	mutex_unlock(&data->wait_protocol_lock);
	
	return size;
}

static ssize_t smart_charging_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct tc_charger *info = tc_get_charger_info();
	struct tc_data *data = info->data;

	return sprintf(buf, "%d\n",  data->smtchg_data.smartchg_en);
}

static const struct kobj_attribute smart_mincur_attr =
	__ATTR(smart_mincur, S_IRUGO | S_IWUSR, smart_mincur_show,
	smart_mincur_store);

static const struct kobj_attribute smart_charging_attr =
	__ATTR(smart_charging, S_IRUGO | S_IWUSR, smart_charging_show,
	smart_charging_store);

static ssize_t bypass_charger_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = 0;
	unsigned int val = 0;
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;

	ret = kstrtouint(buf, 10, &val);
	if (ret) {
		pr_err("Failed to convert string to unsigned int\n");
		return ret;
	}
	pr_info("[%s] val: %d\n", __func__, val);

	mutex_lock(&data->wait_protocol_lock);
	data->wait_protocol_array[BYPASS_CHG].value = val;
	data->wait_protocol_array[BYPASS_CHG].changed = true;

	schedule_delayed_work(&data->wait_protocol_work, 0);
	mutex_unlock(&data->wait_protocol_lock);
	return size;
}

static ssize_t bypass_charger_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;

	return sprintf(buf, "%d\n",  data->bypasschg_data.bypass_en);
}
static DEVICE_ATTR_RW(bypass_charger);

static ssize_t tran_aichg_disable_charger_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int ret;
	unsigned int val = 0;
	union com_propval vote_val = {0};
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;
	struct ai_chg_data *aichg_data = &data->aichg_data;

	ret = kstrtouint(buf, 10, &val);
	if (ret) {
		pr_err("Failed to convert string to unsigned int\n");
		return ret;
	}
	aichg_data->ai_dischg = val;
	pr_info("[%s] val: %d\n", __func__, val);

	if (val == AICHG_ENABLE) {
		aichg_data->ai_dischg = true;
	} else if(val == AICHG_DISABLED) {
		aichg_data->ai_dischg = false;
	} else {
		pr_err("error argument!\n");
		goto out;
	}

	vote_val.intval = aichg_data->ai_dischg;
	tran_dev_set_prop(data->tc_charger_dev, TRAN_PROP_SET_AICHG_VOTE, &vote_val);

out:

	return size;
}

static ssize_t tran_aichg_disable_charger_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;

	return sprintf(buf, "%d\n",  data->aichg_data.ai_dischg);
}
static DEVICE_ATTR_RW(tran_aichg_disable_charger);

static ssize_t tran_aichg_bigdata_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int qmax = 0;
	struct ai_chg_data *aichg_data = NULL;
	struct tc_charger *info = dev->driver_data;
	aichg_data = &info->data->aichg_data; 
	qmax = tc_get_qmax();

	return sprintf(buf, "%d %d %d %d %d %d\n", 3460, qmax, info->data->vbat_eoc, aichg_data->real_upload_current, tc_get_uisoc(), aichg_data->upload_tims);
}
static DEVICE_ATTR_RO(tran_aichg_bigdata);


static ssize_t tran_custom_disable_charger_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_data *data = info->data;
	int ret = 0;
	unsigned int val = 0;
	union com_propval com_val = {0, };

	if (buf != NULL && size != 0) {
		ret = kstrtouint(buf, 10, &val);
		if (ret != 0)
			return size;
	}

	if (IS_ERR_OR_NULL(data->tc_charger_dev)) {
	        pr_err("tran chg control device is NULL, please config tran_common_class\n");
	        return size;
	}

	if (val == 1) {
		com_val.intval = TRAN_CUSTOM_CHG;
	} else if (val == 0) {
		com_val.intval = TRAN_CUSTOM_DISCHG;
	}else {
		pr_err("error argument!\n");
		goto out;
	}

	tran_dev_set_prop(data->tc_charger_dev, TRAN_PROP_TRAN_CUSTOM_DISCHG, &com_val);
out:
	return size;
}
static DEVICE_ATTR_WO(tran_custom_disable_charger);

static ssize_t cc_smt_status_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tran_device *tc_tcpc_dev = tran_get_by_name("tc_tcpc");
	union com_propval com_val = {0, };

	tran_dev_get_prop(tc_tcpc_dev, TRAN_PROP_TYPE_CC_SMT, &com_val);

	return sprintf(buf, "%d\n", com_val.intval);
}
static DEVICE_ATTR_RO(cc_smt_status);

static ssize_t shipmode_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct tc_charger *info = dev->driver_data;
	pr_info("store_shipmode_enable\n");
	charger_dev_enable_shipmode(info->data->chg1_dev, true);
	return size;
}

static ssize_t shipmode_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	int shipmode = 0;

	shipmode = charger_dev_get_shipmode_status(info->data->chg1_dev);
	pr_info("%s = %d\n", __func__, shipmode);

	return sprintf(buf, "%d\n", shipmode);
}
static DEVICE_ATTR_RW(shipmode_enable);

static ssize_t charge_decimal_level_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;
	struct real_soc_decimal_data *data = info->data->real_soc_data;
	static int fake_zero_soc = 0;
	u32 raw_soc = data->raw_soc;

	if (IS_ERR_OR_NULL(data))
		return -ENODATA;

	if (IS_ERR_OR_NULL(info->data->tran_batt_dev))
		info->data->tran_batt_dev = tran_get_by_name("tran_batt");

	// start timeout timer
	/* alarm_cancel(&info->charge_decimal_timeout_timer); */
	/* mtk_start_charge_decimal_timeout_alarm_timer(info); */

	// switch soc update freq
	/* val.intval = SOC_TWO_DECIMAL; */
	/* tran_dev_set_prop(info->tran_batt_dev, TRAN_PROP_SET_SOC_DECIMAL_RATE, &val); */

	if (raw_soc == 0) {
		fake_zero_soc = min(99, ++fake_zero_soc);
		raw_soc = fake_zero_soc;
	} else {
		fake_zero_soc = 0;
	}

	return sprintf(buf, "%d\n",  raw_soc);
}
static DEVICE_ATTR_RO(charge_decimal_level);

extern void tc_wakeup_decimal_soc_report_thread(struct real_soc_decimal_data *data);
extern void tc_start_charge_decimal_timeout_alarm_timer(struct real_soc_decimal_data *data, u32 time_out);
static ssize_t tran_start_dec_soc_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct tc_charger *info = dev->driver_data;
	struct tc_desc *desc = info->desc;
	struct real_soc_decimal_data *data = info->data->real_soc_data;
	struct tran_device *tran_batt_dev = NULL;
	unsigned int val = 0;
	union com_propval com_val = {0, };
	int ret;

	if (buf != NULL && size != 0) {
		ret = kstrtouint(buf, 10, &val);
		if (ret != 0)
			return size;
	}

	if (IS_ERR_OR_NULL(data))
		return -ENODATA;

	if (IS_ERR_OR_NULL(info->data->tran_batt_dev))
		info->data->tran_batt_dev = tran_get_by_name("tran_batt");
	tran_batt_dev = info->data->tran_batt_dev;

	if (val == 0) {
		pr_info("stop decimal soc report\n");
		/* stop decimal update */
		// stop report thread
		atomic_set(&data->start_decimal_soc, 0);
		// cancel timeout timer;
		alarm_cancel(&data->charge_decimal_timeout_timer);
		// set fuel gauge soc issue freq to normal state;
		// switch soc update freq
		com_val.intval = SOC_ZERO_DECIMAL;
		tran_dev_set_prop(tran_batt_dev, TRAN_PROP_SET_SOC_DECIMAL_RATE, &com_val);

	} else {
		pr_info("start decimal soc report\n");
		/* start decimal update */
		// start report thread;
		atomic_set(&data->start_decimal_soc, 1);
		tc_wakeup_decimal_soc_report_thread(data);
		// switch soc update freq
		com_val.intval = SOC_TWO_DECIMAL;
		tran_dev_set_prop(tran_batt_dev, TRAN_PROP_SET_SOC_DECIMAL_RATE, &com_val);
		// cancel timeout timer;
		alarm_cancel(&data->charge_decimal_timeout_timer);
		// start timeout timer
		tc_start_charge_decimal_timeout_alarm_timer(data, desc->charge_decimal_timeout_time);
		// set fuel gauge soc issue freq to 0.01 or 200ms;
	}

	return size;
}
static DEVICE_ATTR_WO(tran_start_dec_soc);

extern int tc_send_up_nv_cycle_count(struct tc_charger *info);
static ssize_t tran_battery_cycle_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct tc_charger *info = dev->driver_data;

	int batt_cycle = tc_get_battery_cycle();

	tc_send_up_nv_cycle_count(info);

	return sprintf(buf, "%d\n", batt_cycle);
}

static ssize_t tran_battery_cycle_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	struct nvram_chg_scene chg_scene;

	memcpy((void *)&chg_scene, (void *)buf, sizeof(chg_scene));

	pr_info("%s:  hal set new batt_cycle = %d\n",
		__func__, chg_scene.cycle_cnt);

	tc_set_battery_cycle(chg_scene.cycle_cnt);

	return size;
}
static DEVICE_ATTR_RW(tran_battery_cycle);

static ssize_t reset_nv_battery_cycle_store(
	struct device *dev, struct device_attribute *attr,
	const char *buf, size_t size)
{
	struct tc_charger *info = dev->driver_data;
	char buffer[16] = {0};
	int ret;
	char *env[2] = {NULL, NULL};

	if (kstrtoint(buf, 10, &ret) == 0) {
		if(ret == 1) {
			snprintf(buffer, sizeof(buffer), "RESET_BC:%d,", ret);
			env[0] = buffer;
			kobject_uevent_env(&info->pdev->dev.kobj, KOBJ_CHANGE, env);
		}
	}
	return size;
}
static DEVICE_ATTR_WO(reset_nv_battery_cycle);

static ssize_t bat_mac_date_show(struct device *dev,struct device_attribute *attr,char *buf)
{
	struct tc_charger *info = dev_get_drvdata(dev);
	struct tc_desc *desc = info->desc;
	struct tc_data *data = info->data;
	struct date_struct *mac_date = &data->mac_date;
        int year = desc->base_year;
	int ret;

	if (data->ic_err == 2)
		ret = sprintf(buf, "ERR\n");
	else if ((mac_date->mon + mac_date->date) == 0)
		ret = sprintf(buf, "ERR\n");
	else
		ret = sprintf(buf, "%d-%02d-%02d\n", mac_date->year + year, mac_date->mon, mac_date->date);

	dev_err(dev,"read mac buf = %s\n", buf);
	return ret;
}
static ssize_t bat_mac_date_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	struct tc_charger *info = dev_get_drvdata(dev);
	struct tc_desc *desc = info->desc;
	struct tc_data *data = info->data;
	struct date_struct *mac_date = &data->mac_date;
	struct date_struct temp_date;
        int year = desc->base_year;
	int month, day;

	memcpy((void *)&temp_date, (void *)buf, sizeof(struct date_struct));
	dev_info(dev, "mac date = %d-%02d-%02d", temp_date.year, temp_date.mon, temp_date.date);
	if (temp_date.year == 0)
		year = year + 10;
	else if (temp_date.year >= 1 && temp_date.year <= 9)
		year = year + temp_date.year;
	else if (temp_date.mon == -1 && temp_date.date == -1)
		data->ic_err = 2;

	month = temp_date.mon;
	day = temp_date.date;

	if (data->ic_err)
		return size;
	else if (year < 2020)
		return size;
	else if (year > 2030)
		return size;
	else if (month > 12)
		return size;
	else if (day > 31)
		return size;

	mac_date->year = temp_date.year;
	mac_date->mon = month;
	mac_date->date = day;
	dev_info(dev, "mac date = %d(%d)-%02d-%02d", year, mac_date->year, mac_date->mon, mac_date->date);

	return size;
}
DEVICE_ATTR_RW(bat_mac_date);

static ssize_t phone_active_date_show(struct device *dev,struct device_attribute *attr,char *buf)
{
	struct tc_charger *info = dev_get_drvdata(dev);
	struct tc_desc *desc = info->desc;
	struct tc_data *data = info->data;
	struct date_struct *active_date = &data->active_date;
        int year = desc->base_year;


	if (data->bat_active_reset) {
		return sprintf(buf, "%s\n", "no active");
	}
	else if (active_date->mon == 0 && active_date->date == 0) {
		return sprintf(buf, "%s\n", "no active");
	}
	else if (data->ic_err == 1) {
		return sprintf(buf, "%s\n", "ERR");
	} else if (active_date->year + year < 2000) {
		dev_err(dev, "base year err\n");
		return sprintf(buf, "%s\n", "ERR");
	}

	dev_err(dev, "%s: Year=%d(%d), Month=%d, Day=%d\n", __func__,
		active_date->year, year, active_date->mon, active_date->date);
	return sprintf(buf, "%d-%02d-%02d\n", active_date->year + year, active_date->mon, active_date->date);
}

#define VALID_ACTIVE_DATE_SIZE 8
static ssize_t phone_active_date_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	struct tc_charger *info = dev_get_drvdata(dev);
	struct tc_desc *desc = info->desc;
	struct tc_data *data = info->data;
	struct date_struct *active_date = &data->active_date;
	int year = -1, month = -1, day = -1;
	int base_year = desc->base_year;
	struct date_struct temp_date;
	char eventbuf[64];
	const char *tempbuf = NULL;
	char datebuf[8] = {0,0,0,0,0,0,0,0};
	char *env[2] = { NULL, NULL };

	if (size >= VALID_ACTIVE_DATE_SIZE) {
		/* Phone is active - fixed format: YYYYMMDD */
		tempbuf = buf;
		snprintf(datebuf, 5, "%.4s", tempbuf);
		if (kstrtoint(datebuf, 10, &year))
			return size;

		snprintf(datebuf, 3, "%.2s", tempbuf + 4);
		if (kstrtoint(datebuf, 10, &month))
			return size;

		snprintf(datebuf, 3, "%.2s", tempbuf + 6);
		if (kstrtoint(datebuf, 10, &day))
			return size;
		data->bat_active_reset = false;
		dev_info(dev, "%s phone is acitve buf = %s, size  = %lu, %d-%02d-%02d\n",
				__func__, buf, size, year, month, day);
	} else {
		/* write to kernel for other app read from kernel */
		memcpy((void *)&temp_date, (void *)buf, sizeof(struct date_struct));

		month = temp_date.mon;
		day = temp_date.date;

		if (temp_date.year == 0)
			year = base_year + 10;
		else if (temp_date.year >= 1 && temp_date.year <= 9)
			year = base_year + temp_date.year;
		else if (month == -1 && day == -1)
			/* err */
			data->ic_err = 1;
		dev_info(dev, "%s write to kernel, %d-%02d-%02d\n", __func__, year, month, day);
	}

	/* Define 0 mon 0day as an abnormal date with no activation */
	if ((month + day) == 0) {
		dev_err(dev,"no active or date is reset\n");
		data->bat_active_reset = true;
		return size;
	}
	else if (data->ic_err) {
		dev_info(dev, "ic read or wirte err\n");
		return size;
	}
	else if (year < base_year || year > 2030) {
		dev_info(dev, "write year err  = %d\n", year);
		return size;
	}
	else if (month > 12 || month < 1) {
		dev_info(dev, "write month err  = %d\n", month);
		return size;
	}
	else if (month != 2 && (day > 31 || day < 1)) {
		dev_info(dev, "write day err  = %d\n", day);
		return size;
	}
	else if (month == 2 && (day > 29 || day < 1)) {
		dev_info(dev, "2 month write day err  = %d\n", day);
		return size;
	}

	if (size >= VALID_ACTIVE_DATE_SIZE) {
		data->bat_active = true;
	}

	if (year == 2030)
		year = 0;
	else
		year -= base_year;

	active_date->year = year;
	active_date->mon = month;
	active_date->date = day;

	if (data->bat_active) {
		snprintf(eventbuf, sizeof(eventbuf),"ACTIVE_DATE:%d",1);
		env[0] = eventbuf;
		kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, env);
		dev_err(dev, "phone ative update active date\n");
	}

	dev_err(dev,"acitve date: Year=%d(%d), Month=%d, Day=%d\n",
			active_date->year, base_year, active_date->mon, active_date->date);

	return size;
}
DEVICE_ATTR_RW(phone_active_date);

static ssize_t phone_active_date_reset_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "no support\n");
}
static ssize_t phone_active_date_reset_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct tc_charger *info = dev_get_drvdata(dev);
	char eventbuf[64];
	char *env[2] = { NULL, NULL };
	int ret;

	if(IS_ERR_OR_NULL(info)) {
		pr_info("%s no charger info find\n", __func__);
		return size;
	}

	if (kstrtoint(buf, 10, &ret) == 0){
		if (ret == 1) {
			pr_info("%s\n", __func__);
			/* Immediately update after activation time reset.
			 * but the reset flag lost after reboot
			*/
			snprintf(eventbuf, sizeof(eventbuf),"ACTIVE_DATE:%d",2);
			env[0] = eventbuf;
			kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, env);
		}
	}

	return size;
}
DEVICE_ATTR_RW(phone_active_date_reset);

#define TRAN_OF_GPIO_ACTIVE_LOW 0x1
static int tran_check_eu_board(void)
{
	int ret = 0;
	struct device_node *authon_ic_np = NULL;
	int board_gpio = -1;
	int gpio_value;
	int eu_support = 0;
	unsigned int active_flags = 0;

	authon_ic_np = of_find_compatible_node(NULL, NULL,"optiga_authon,optiga_authon_dev");
	if (IS_ERR_OR_NULL(authon_ic_np)) {
		tchr_err("no authen ic node\n");
		return eu_support;
	}
	board_gpio = of_get_named_gpio(authon_ic_np, "boardver-gpios", 0);
	if (!gpio_is_valid(board_gpio)) {
		tchr_err("invalid gpio\n");
		goto out;
	}
	ret = of_property_read_u32_index(authon_ic_np, "boardver-gpios", 2, &active_flags);
	if (ret) {
		tchr_err("failed to obtain Gpio effective level\n");
		goto out;
	}
	ret = gpio_direction_input(board_gpio);
	if (ret) {
		tchr_err("failed to set GPIO direction (ret=%d)\n", ret);
		goto out;
	}

	gpio_value = gpio_get_value(board_gpio);
	if (gpio_value < 0) {
		tchr_err("faile get gpio val ret:%d\n", gpio_value);
		goto out;
	}

	if (active_flags & TRAN_OF_GPIO_ACTIVE_LOW)
		gpio_value = !gpio_value;

	eu_support = !gpio_value;
	tchr_err("raw GPIO value=%d, eu_support=%d\n",
			gpio_get_value(board_gpio), eu_support);

out:
	of_node_put(authon_ic_np);

	return eu_support;
}

static ssize_t bat_authent_support_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	struct power_supply *psy = NULL;
	union  power_supply_propval psy_val = {0,};
	int ret;

	psy = power_supply_get_by_name("ctx_psy");
	if (!IS_ERR_OR_NULL(psy)) {
		ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_PRESENT, &psy_val);
		if (ret < 0)
			dev_err(dev,"get ctx present failed\n");
	} else {
		tchr_err("%s, authon ic may no probe\n", __func__);
		ret = tran_check_eu_board();
		psy_val.intval = ret;
	}
	tchr_err("%s, support = %d\n", __func__, psy_val.intval);
	return sprintf(buf, "%d\n", psy_val.intval);
}
DEVICE_ATTR_RO(bat_authent_support);

static struct attribute* charger_sysfs_attrs[] = {
	//pre:sys/devices/platform/charger/***
	//cur:sys/charger/charger_sysfs/***
	&dev_attr_Charging_mode.attr,
	&dev_attr_pd_type.attr,
	&dev_attr_sw_jeita.attr,
	&dev_attr_High_voltage_chg_enable.attr,
	&dev_attr_Charger_Type.attr,
	&dev_attr_Pump_Express.attr,
	&dev_attr_adapter_capacity.attr,
	&dev_attr_chgspeed_ctl.attr,
	&dev_attr_alg_debug.attr,
	&dev_attr_bat_warning_cmd.attr,
	&dev_attr_hvdcp30_ctrl.attr,
	&dev_attr_ADC_Charger_Voltage.attr,
	&dev_attr_ADC_Charging_Current.attr,
	&dev_attr_input_current.attr,
	&dev_attr_cc_smt_status.attr,
	&dev_attr_shipmode_enable.attr,
	&dev_attr_charge_decimal_level.attr,
	&dev_attr_tran_start_dec_soc.attr,
	&dev_attr_charger_log_level.attr,
	&dev_attr_BatteryNotify.attr,
	&dev_attr_Si_Battery_Poweroff_Voltage.attr,
	&dev_attr_adapter_control.attr,
	//Add for custom Hal
	&dev_attr_TranBatteryNotify.attr,
	&dev_attr_charger_unlimited.attr,
	&dev_attr_sw_safety_timer.attr,
	&dev_attr_dynamic_mivr.attr,
	&dev_attr_enable_hv_charging.attr,
	&dev_attr_ir_comp.attr,
	//pre:sys/devices/platform/odm/odm:tran_battery/***
	//cur:sys/charger/charger_sysfs/***
	&dev_attr_BATTERY_QMAX.attr,
	&dev_attr_CHG_CAPACITY_TEST.attr,
	&dev_attr_OTG_CTL.attr,
	&dev_attr_bypass_charger.attr,
	&dev_attr_tran_aichg_disable_charger.attr,
	&dev_attr_tran_aichg_bigdata.attr,
	&dev_attr_tran_custom_disable_charger.attr,
	&dev_attr_fg_coulomb.attr,
	&dev_attr_tran_bat_temp.attr,
	&dev_attr_tran_set_current.attr,
	&dev_attr_tran_cam.attr,
	&dev_attr_battery_health_percent.attr,
	&dev_attr_battery_health_level.attr,
	&dev_attr_tran_battery_cycle.attr,
	&dev_attr_reset_nv_battery_cycle.attr,
	&dev_attr_bat_mac_date.attr,
	&dev_attr_phone_active_date.attr,
	&dev_attr_phone_active_date_reset.attr,
	&dev_attr_bat_authent_support.attr,
	NULL,
};

static const struct attribute_group charger_sysfs_group = {
	.attrs = charger_sysfs_attrs,
};
/* procfs */
static int tc_chg_current_cmd_show(struct seq_file *m, void *prv_data)
{
	struct tc_charger *info = m->private;
	struct tc_data *data = info->data;

	seq_printf(m, "%d\n", data->cmd_discharging);
	return 0;
}

static ssize_t tc_chg_current_cmd_write(struct file *file,
		const char *buffer, size_t count, loff_t *prv_data)
{
	int len = 0;
	char buf[32] = {0};
	int cmd_discharging = 0;
	struct tc_charger *info = pde_data(file_inode(file));
	struct tc_data *data = info->data;

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(buf) - 1)) ? count : (sizeof(buf) - 1);
	if (copy_from_user(buf, buffer, len))
		return -EFAULT;

	buf[len] = '\0';

	if (sscanf(buf, "%d", &cmd_discharging) == 1) {
		data->cmd_discharging = !!cmd_discharging;

		tc_charger_cmd_vote(info, !!cmd_discharging);

		tchr_info("%s: cmd_discharging = %d\n", __func__, !!cmd_discharging);

		_wake_up_charger(info);
		return count;
	}

	tchr_err("bad argument\n");
	return count;
}

static int tc_chg_en_power_path_show(struct seq_file *m, void *prv_data)
{
	struct tc_charger *info = m->private;
	struct tc_data *data = info->data;
	bool power_path_en = true;

	charger_dev_is_powerpath_enabled(data->chg1_dev, &power_path_en);
	seq_printf(m, "%d\n", power_path_en);

	return 0;
}

static ssize_t tc_chg_en_power_path_write(struct file *file,
		const char *buffer, size_t count, loff_t *prv_data)
{
	int len = 0, ret = 0;
	char buf[32] = {0};
	unsigned int enable = 0;
	struct tc_charger *info = pde_data(file_inode(file));
	struct tc_data *data = info->data;

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(buf) - 1)) ? count : (sizeof(buf) - 1);
	if (copy_from_user(buf, buffer, len))
		return -EFAULT;

	buf[len] = '\0';

	ret = kstrtou32(buf, 10, &enable);
	if (ret == 0) {
		charger_dev_enable_powerpath(data->chg1_dev, enable);
		_wake_up_charger(info);
		tchr_info("%s: enable power path = %d\n", __func__, enable);
		return count;
	}

	tchr_err("bad argument, echo [enable] > en_power_path\n");
	return count;
}

static int tc_chg_en_safety_timer_show(struct seq_file *m, void *prv_data)
{
	struct tc_charger *info = m->private;
	struct tc_desc *desc = info->desc;

	seq_printf(m, "%d\n", desc->enable_sw_safety_timer);

	return 0;
}

static ssize_t tc_chg_en_safety_timer_write(struct file *file,
	const char *buffer, size_t count, loff_t *prv_data)
{
	int len = 0, ret = 0;
	char buf[32] = {0};
	unsigned int enable = 0;
	struct tc_charger *info = pde_data(file_inode(file));
	struct tc_desc *desc = info->desc;

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(buf) - 1)) ? count : (sizeof(buf) - 1);
	if (copy_from_user(buf, buffer, len))
		return -EFAULT;

	buf[len] = '\0';

	ret = kstrtou32(buf, 10, &enable);
	if (ret == 0) {
		desc->enable_sw_safety_timer = !!enable;
		tchr_info("%s: enable safety timer = %d\n", __func__, !!enable);
		_wake_up_charger(info);

		return count;
	}

	tchr_err("bad argument, echo [enable] > en_safety_timer\n");
	return count;
}

static int tc_chg_limit_current_show(struct seq_file *m, void *prv_data)
{
	struct tc_charger *info = m->private;
	struct tc_data *data = info->data;

	seq_printf(m, "%d\n", data->memtest_current);
	return 0;
}

static ssize_t tc_chg_limit_current_write(struct file *file,
		const char *buffer, size_t count, loff_t *prv_data)
{
	struct tc_charger *info = pde_data(file_inode(file));
	struct tc_data *data = info->data;
	int val = 0;
	char *tmp = kzalloc((count + 1), GFP_KERNEL);
	
	if (!tmp)
		return -ENOMEM;
	
	if (copy_from_user(tmp, buffer, count)) {
		kfree(tmp);
		return -EFAULT;
	}
	
	if (kstrtoint(tmp, 10, &val)) {
		kfree(tmp);
		return -EINVAL;
	}

	data->memtest_current = val;
	_wake_up_charger(info);
	tchr_err("memtest_current proc set: %d\n", data->memtest_current);
	kfree(tmp);
	
	return count;
}

PROC_FOPS_RW(current_cmd);
PROC_FOPS_RW(en_power_path);
PROC_FOPS_RW(en_safety_timer);
PROC_FOPS_RW(limit_current);

#define CHARGER_DEVNAME "charger_ftm"
#define GET_IS_DIVIDER_CHARGER_EXIST _IOW('k', 13, int)
#define GET_IS_SECOND_SWTICH_EXIST _IOW('k', 14, int)
#define GET_PRIMARY_DIVIDER_CHARGER_CURR _IOW('k', 15, int)
#define GET_DIVIDER_CHARGER_NUM _IOW('k', 16, int)

//only for second switch ic
static int is_second_switch_exist(struct tc_charger *info)
{
	if (get_charger_by_name("secondary_chg") == NULL){
		return 0;
	}

	return 1;
}

//for all charge pump ic
static int is_all_divider_charger_exist(struct tc_charger *info)
{
	if (info->desc->ftm_cp_num >= 1 && get_charger_by_name("primary_dvchg") == NULL){
		return 0;
	}

	if (info->desc->ftm_cp_num >= 2 && get_charger_by_name("secondary_dvchg") == NULL){
		return 0;
	}

	if (info->desc->ftm_cp_num >= 3 && get_charger_by_name("third_divider_chg") == NULL){
		return 0;
	}

	return 1;
}

static long charger_ftm_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	int ret = 0;
	int out_data = 0;
	int bat_data;
	void __user *user_data = (void __user *)arg;
	struct charger_device *dvchg1_dev = NULL;
	struct tran_device * tc_charger_dev = NULL;
	struct tc_charger *info = NULL;

	tc_charger_dev = tran_get_by_name("tc_charger");
	if (!tc_charger_dev) {
		tchr_err("can't find tc_charger_dev ***\n");
		return -ENODEV;
	}

	info = tran_get_data(tc_charger_dev);

	switch (cmd) {
	case GET_IS_SECOND_SWTICH_EXIST:
		out_data = is_second_switch_exist(info);
		ret = copy_to_user(user_data, &out_data, sizeof(out_data));
		tchr_err("[%s] is_second_switch_exist: %d\n", __func__, out_data);
		break;
	case GET_IS_DIVIDER_CHARGER_EXIST:
		out_data = is_all_divider_charger_exist(info);
		ret = copy_to_user(user_data, &out_data, sizeof(out_data));
		tchr_err("[%s] PRIMARY_DIVIDER_EXIST: %d\n", __func__, out_data);
		break;
	case GET_PRIMARY_DIVIDER_CHARGER_CURR:
		dvchg1_dev = get_charger_by_name("primary_dvchg");
		if (!dvchg1_dev) {
			tchr_err("[%s] get primary_dvchg failed\n", __func__);
			break;
		}
		ret = charger_dev_get_adc(dvchg1_dev, ADC_CHANNEL_IBUS,
								&out_data, &out_data);
		out_data = out_data/1000;
		bat_data = tc_get_battery_current();
		ret = copy_to_user(user_data, &out_data, sizeof(out_data));
		tchr_err("[%s] PRIMARY_DIVIDER_CHARGER_CURR: %d,%d\n", __func__, out_data,bat_data);
		break;
	case GET_DIVIDER_CHARGER_NUM:
		out_data = info->desc->ftm_cp_num;
		ret = copy_to_user(user_data, &out_data, sizeof(out_data));
		tchr_err("[%s] FTM CP number: %d\n", __func__, out_data);
		break;
	default:
		ret = -EINVAL;
		tchr_err("[%s] Error ID\n", __func__);
		break;
	}

	return ret;
}

static int charger_ftm_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int charger_ftm_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations charger_ftm_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = charger_ftm_ioctl,
	.open = charger_ftm_open,
	.release = charger_ftm_release,
};


static void charger_ftm_init(struct tc_charger *info)
{
	struct tc_data *data = info->data;
	struct class_device *class_dev = NULL;
	int ret = 0;

	ret = alloc_chrdev_region(&data->charger_devno, 0, 1, CHARGER_DEVNAME);
	if (ret < 0) {
		tchr_err("[%s]Can't get major num for charger_ftm\n", __func__);
		return;
	}

	data->charger_cdev = cdev_alloc();
	if (!data->charger_cdev) {
		tchr_err("[%s]cdev_alloc fail\n", __func__);
		goto unregister;
	}
	data->charger_cdev->owner = THIS_MODULE;
	data->charger_cdev->ops = &charger_ftm_fops;

	ret = cdev_add(data->charger_cdev, data->charger_devno, 1);
	if (ret < 0) {
		tchr_err("[%s] cdev_add failed\n", __func__);
		goto free_cdev;
	}

	data->charger_major = MAJOR(data->charger_devno);
	data->charger_class = class_create(THIS_MODULE, CHARGER_DEVNAME);
	if (IS_ERR(data->charger_class)) {
		tchr_err("[%s] class_create failed\n", __func__);
		goto free_cdev;
	}

	class_dev = (struct class_device *)device_create(data->charger_class,
				NULL, data->charger_devno, NULL, CHARGER_DEVNAME);
	if (IS_ERR(class_dev)) {
		tchr_err("[%s] device_create failed\n", __func__);
		goto free_class;
	}

	tchr_err("%s done\n", __func__);
	return;

free_class:
	class_destroy(data->charger_class);
free_cdev:
	cdev_del(data->charger_cdev);
unregister:
	unregister_chrdev_region(data->charger_devno, 1);
}

int tc_charger_setup_files(struct platform_device *pdev)
{
	int ret = 0;
	struct proc_dir_entry *battery_dir = NULL, *entry = NULL;
	struct device_node *np = NULL;
	struct platform_device *pdev_odm = NULL;
	struct tc_charger *info = platform_get_drvdata(pdev);
	struct tc_data *data = info->data;

	ret = sysfs_create_group(&(pdev->dev.kobj), &charger_sysfs_group);
	if (ret < 0){
		tchr_err("%s : sysfs_create_group failed\n", __func__);
		ret = -ENODEV;
		goto out;
	}

	np = of_find_node_by_name(NULL, "odm");
	pdev_odm = of_find_device_by_node(np);

	ret = sysfs_create_link(&pdev_odm->dev.kobj,
			&pdev->dev.kobj, "odm:tran_battery");
	if (ret < 0) {
		pr_err("%s : sysfs_create_link failed\n", __func__);
	}

	data->smtchg_data.smtchg_kobj = kobject_create_and_add("odm:smart_charging", &pdev_odm->dev.kobj);
	if (IS_ERR_OR_NULL(data->smtchg_data.smtchg_kobj)) {
		tchr_err("%s : create smtchg kobj failed\n", __func__);
		goto out;
	}
	ret = sysfs_create_file(data->smtchg_data.smtchg_kobj, &smart_mincur_attr.attr);
	ret = sysfs_create_file(data->smtchg_data.smtchg_kobj, &smart_charging_attr.attr);
	if (ret < 0) {
		tchr_err("%s : sysfs_create failed\n", __func__);
		goto out;
	}

	ret = sysfs_create_link(chg_kobj, &pdev->dev.kobj, "charger_sysfs");
	if (ret < 0){
		tchr_err("%s : sysfs_create_link failed\n", __func__);
		goto out;
	}

	battery_dir = proc_mkdir("tc_charger_cmd", NULL);
	if (!battery_dir) {
		tchr_err("%s: mkdir /proc/tc_battery_cmd failed\n", __func__);
		return -ENOMEM;
	}

	entry = proc_create_data("current_cmd", 0644, battery_dir,
			&tc_chg_current_cmd_fops, info);
	if (!entry) {
		ret = -ENODEV;
		goto fail_procfs;
	}
	entry = proc_create_data("en_power_path", 0644, battery_dir,
			&tc_chg_en_power_path_fops, info);
	if (!entry) {
		ret = -ENODEV;
		goto fail_procfs;
	}
	entry = proc_create_data("en_safety_timer", 0644, battery_dir,
			&tc_chg_en_safety_timer_fops, info);
	if (!entry) {
		ret = -ENODEV;
		goto fail_procfs;
	}

        battery_dir = proc_mkdir("tran_chg_limit_current", NULL);
        if (!battery_dir){
		tchr_err("%s: mkdir /proc/tran_chg_limit_current\n", __func__);
                return -ENOMEM;
        }

        entry = proc_create_data("limit_current", 0644, battery_dir,
                &tc_chg_limit_current_fops, info);
	if (!entry) {
		ret = -ENODEV;
		goto fail_procfs_memtest;
	}

	if (data->bootmode ==1 || data->bootmode == 4
			|| data->bootmode == 5 || data->bootmode == 6) {
		charger_ftm_init(info);
	}

	return 0;

fail_procfs_memtest:
	remove_proc_subtree("tran_chg_limit_current", NULL);
fail_procfs:
	remove_proc_subtree("tc_charger_cmd", NULL);
out:
	return ret;
}
