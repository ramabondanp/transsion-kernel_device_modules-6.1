// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#include <generated/autoconf.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/kthread.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/writeback.h>
#include <linux/seq_file.h>
#include <linux/power_supply.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/reboot.h>
#include <linux/of.h>
#include <linux/alarmtimer.h>

#include "tc_charger.h"
#include "tc_auto_test.h"
#include "tc_charger_class.h"
#include "tc_pe5.h"
#include "tc_common_class.h"
#include "tc_algorithm_class.h"
#include "tc_misc_intf.h"
#include "tc_tcpc.h"
#include "tc_pd.h"
#include "tc_battery.h"
#include "tc_gauge.h"

static char *run_path;
static int offset;

enum tran_cc_status {
	TRAN_TEST_CC_VOLT_OPEN = 0,
	TRAN_TEST_CC_VOLT_RA = 1,
	TRAN_TEST_CC_VOLT_RD = 2,
	TRAN_TEST_CC_VOLT_SNK_DFT = 5,
	TRAN_TEST_CC_VOLT_SNK_1_5 = 6,
	TRAN_TEST_CC_VOLT_SNK_3_0 = 7,
	TRAN_TEST_CC_VOLT_TOGGLING = 15,
};

static const char * const TRAN_CC_STATUS_TEXT[] = {
	[TRAN_TEST_CC_VOLT_OPEN]         = "TYPEC_CC_VOLT_OPEN",
	[TRAN_TEST_CC_VOLT_RA]           = "TYPEC_CC_VOLT_RA",
	[TRAN_TEST_CC_VOLT_RD]           = "TYPEC_CC_VOLT_RD",
	[TRAN_TEST_CC_VOLT_SNK_DFT]      = "TYPEC_CC_VOLT_SNK_DFT",
	[TRAN_TEST_CC_VOLT_SNK_1_5]      = "TYPEC_CC_VOLT_SNK_1_5",
	[TRAN_TEST_CC_VOLT_SNK_3_0]      = "TYPEC_CC_VOLT_SNK_3_0",
	[TRAN_TEST_CC_VOLT_TOGGLING]     = "TYPEC_CC_DRP_TOGGLING",
};

static const char * const TRAN_CHG_STATUS_TEXT[] = {
	[POWER_SUPPLY_STATUS_UNKNOWN]		 = "POWER_SUPPLY_STATUS_UNKNOWN",
	[POWER_SUPPLY_STATUS_NOT_CHARGING]       = "POWER_SUPPLY_STATUS_NOT_CHARGING",
	[POWER_SUPPLY_STATUS_CHARGING]           = "POWER_SUPPLY_STATUS_CHARGING",
	[POWER_SUPPLY_STATUS_DISCHARGING]	 = "POWER_SUPPLY_STATUS_DISCHARGING",
	[POWER_SUPPLY_STATUS_FULL]		 = "POWER_SUPPLY_STATUS_FULL",
};

enum tran_chg_ta_power {
	TRAN_TEST_CHG_POWER_0W = 0,
	TRAN_TEST_CHG_POWER_2P5W = 2,
	TRAN_TEST_CHG_POWER_6W = 6,
	TRAN_TEST_CHG_POWER_7P5W = 7,
	TRAN_TEST_CHG_POWER_10W = 10,
	TRAN_TEST_CHG_POWER_15W = 15,
	TRAN_TEST_CHG_POWER_18W = 18,
};


static enum power_supply_property tran_psy_props[] = {
	POWER_SUPPLY_PROP_STATUS,
};

static bool rfc_is_running(struct tran_auto_test *tat)
{
	struct tchg_alg_device *pe5_alg = get_tchg_alg_by_name("pe5");

	if (pe5_alg && tchg_alg_is_algo_running(pe5_alg))
		return true;

	return false;
}

static void tran_irq_test(struct tran_auto_test *tat, int val)
{
	int event = -1;
	struct tchg_alg_device *alg = get_tchg_alg_by_name("pe5");
	switch (val) {
	case TRAN_IRQ_NOTIFY_VBUS_OVP:
	    event = EVT_VBUSOVP;
	    break;
	case TRAN_IRQ_NOTIFY_IBUSOCP:
	    event = EVT_IBUSOCP;
	    break;
	case TRAN_IRQ_NOTIFY_IBUSUCP_FALL:
	    event = EVT_IBUSUCP_FALL;
	    break;
	case TRAN_IRQ_NOTIFY_BAT_OVP:
	    event = EVT_VBATOVP;
	    break;
	case TRAN_IRQ_NOTIFY_IBATOCP:
	    event = EVT_IBATOCP;
	    break;
	case TRAN_IRQ_NOTIFY_VBATOVP_ALARM:
	    event = EVT_VBATOVP_ALARM;
	    break;
	case TRAN_IRQ_NOTIFY_VBUSOVP_ALARM:
	    event = EVT_VBUSOVP_ALARM;
	    break;
	case TRAN_IRQ_NOTIFY_VOUTOVP:
	    event = EVT_VOUTOVP;
	    break;
	case TRAN_IRQ_NOTIFY_VDROVP:
	    event = EVT_VDROVP;
	    break;
	case TRAN_IRQ_START_TEST:
	    if(alg != NULL)
		tchg_alg_set_prop(alg, ALG_AUTO_TEST_IRQ, true);
	    break;
	case TRAN_IRQ_END_TEST:
	    if(alg != NULL)
		tchg_alg_set_prop(alg, ALG_AUTO_TEST_IRQ, false);
	    break;
	default:
	    break;
	}

	if (event != -1 && tat->dv2_chg1) {
		pr_info("%s: send event:%d\n", __func__, event);
		charger_dev_notify(tat->dv2_chg1, event);
	}
}

static int tran_test_check_psy(struct tran_auto_test *tat)
{
	if (!tat->chg_psy) {
		tat->chg_psy = power_supply_get_by_name("charger");
		if (!tat->chg_psy) {
			pr_err("%s: get chg_psy failed\n", __func__);
			return -ENODEV;
		}
	}

	if (!tat->chg_dev) {
		tat->chg_dev = get_charger_by_name("primary_chg");
		if (!tat->chg_dev) {
			pr_err("%s: get primary_chg failed\n", __func__);
			return -ENODEV;
		}
	}

	if (!tat->dv2_chg1) {
		tat->dv2_chg1 = get_charger_by_name("primary_dvchg");
		if (!tat->dv2_chg1) {
			pr_err("%s: get primary_dvchg failed\n", __func__);
			//return -ENODEV;
		}
	}

	if (!tat->fg_a_dev)
		tat->fg_a_dev = tran_get_by_name("tran_fg_master");

	if (!tat->fg_b_dev)
		tat->fg_b_dev = tran_get_by_name("tran_fg_slave");


	return 0;
}


static int info_set(struct tran_auto_test *tat,
	struct tran_test_sysfs_field_info *attr, int val)
{
	ssize_t count;
	time64_t now = ktime_get_real_seconds();
	struct tran_device *tran_battery_dev = NULL;
	union com_propval com_val = {0, };


	tran_test_check_psy(tat);
	pr_info("%s: set prop attr->prop:%d val:%d\n",
		__func__, attr->prop, val);

	switch (attr->prop) {
	case TRAN_TEST_PROP_STOP_CHG:
		tat->tran_stop_charging = val;
		break;
	case TRAN_TEST_PROP_FIX_CHG_TYPE:
		tat->tran_fix_chg_type = val;
		break;
	case TRAN_TEST_PROP_QUICK_CHG_BACK:
		tat->tran_quick_chg_back = val;
		break;
	case TRAN_TEST_PROP_IRQ_TEST:
		if (val >= TRAN_IRQ_NOT_TEST && val <= TRAN_IRQ_MAX_TEST)
			tran_irq_test(tat, val);
		else
			pr_info("%s: irq test cmd not support:%d\n", __func__, val);
		break;
	case TRAN_TEST_PROP_CHG_RUN_PATH:
		count = scnprintf(run_path + offset, PAGE_SIZE, "%lld, %s\n", now, TRAN_RFC_RUN_PATH[val]);
		offset += count;
		if (offset >= (sizeof(char) * 100 * 100 - 100)) {
			pr_info("%s: reset memset, offset:%d\n", __func__, offset);
			offset = 0;
			memset(run_path, 0, sizeof(char) * 100 * 100);
		}
		break;
	case TRAN_TEST_PROP_FG_OLD_CAR:
		tran_battery_dev = tran_get_by_name("tran_batt");
		tran_dev_get_prop(tran_battery_dev, TRAN_PROP_FG_HW_CAR, &com_val);
		if (val == -1)
			tat->mtk_fg_old_car += com_val.intval;
		else if (val == 0)
			tat->mtk_fg_old_car = val;
		pr_info("%s: mtk_fg_old_car:%d, intval:%d\n", __func__, tat->mtk_fg_old_car, com_val.intval);
		break;
	case TRAN_TEST_PROP_MASTER_BATT_EN:
		if (tat->fg_a_dev) {
			com_val.intval = !!val;
			tran_dev_set_prop(tat->fg_a_dev, TRAN_PROP_BATT_EN, &com_val);
		}
		break;
	case TRAN_TEST_PROP_SLAVE_BATT_EN:
		if (tat->fg_b_dev) {
			com_val.intval = !!val;
			tran_dev_set_prop(tat->fg_b_dev, TRAN_PROP_BATT_EN, &com_val);
		}
		break;

	default:
		break;
	}
	pr_info("%s: set prop attr->prop:%d val:%d, tran_stop_charging:%d, tran_fix_chg_type:%d, tran_quick_chg_back:%d\n",
		__func__, attr->prop, val, tat->tran_stop_charging, tat->tran_fix_chg_type, tat->tran_quick_chg_back);

	return 0;
}

static int info_get(struct tran_auto_test *tat,
	struct tran_test_sysfs_field_info *attr, int *val)
{
	struct tchg_alg_device *pe5_alg = get_tchg_alg_by_name("pe5");
	struct tchg_alg_device *pdc_alg = get_tchg_alg_by_name("pd");
	struct tran_device *temp_forecast_dev = tran_get_by_name("temp_forecast");
	struct tran_device *ambient_dev = tran_get_by_name("ambient_detect");
	struct tran_device *gauge_dev = tran_get_by_name("tc_gauge");
	struct tran_device *tc_chg_dev = tran_get_by_name("tc_charger");
	struct tran_device *tc_tcpc_dev = tran_get_by_name("tc_tcpc");
	struct tran_device *tc_chg_transfer_dev = tran_get_by_name("charge_transfer");
	struct tran_device *tc_chg_pid_dev = tran_get_by_name("pid_chg_algo");
	struct tran_device *tc_wd_dev = tran_get_by_name("water_detect");
	struct tran_device *tc_ac_ctl_dev = tran_get_by_name("adapter_control");
	struct tran_device *tran_battery_dev = tran_get_by_name("tran_batt");
	union power_supply_propval pval = {0, };
	union com_propval tran_val = {0, };
	int fg_a_vbat = 0, fg_b_vbat = 0;
	
	struct device_node *np = NULL;
	struct pe50_algo_info *pe5_info = NULL;

	bool en;

    int ret = 0;
	int value = -1;
	int type = 0;
	int chg_type = 0;

	ret = tran_test_check_psy(tat);
	if (ret != 0) {
		pr_err("get psy failed, ret = %d\n", ret);
		return ret;
	}

	if (pe5_alg)
		pe5_info = tchg_alg_dev_get_drvdata(pe5_alg);
	
	switch (attr->prop) {
	case TRAN_TEST_PROP_VBUS:
		*val = tc_get_vbus();
		break;
	case TRAN_TEST_PROP_IBAT:
		ret =tran_dev_get_prop(gauge_dev, TRAN_PROP_CURRENT_NOW, &tran_val);
		if (ret < 0) {
			pr_err("get ibat failed, ret = %d\n", ret);
		}
		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_CHG_TYPE:
		ret =tran_dev_get_prop(tc_chg_dev, TRAN_PROP_TC_CHG_TYPE, &tran_val);
		if (ret < 0) {
			pr_err("get ibat failed, ret = %d\n", ret);
		}
		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_FIX_CHG_TYPE:
		*val = tat->tran_fix_chg_type;
		break;
	case TRAN_TEST_PROP_STOP_CHG:
		*val = tat->tran_stop_charging;
		break;
	case TRAN_TEST_PROP_QUICK_CHG_BACK:
		*val = tat->tran_quick_chg_back;
		break;
	case TRAN_TEST_PROP_IBUS:
		if (pe5_info && pe5_info->data && rfc_is_running(tat))
			*val = pe5_info->data->ibus_total;
		else
			*val = TC_GET_ERR;
		break; 
	case TRAN_TEST_PROP_VBAT_CP:
		if (pe5_alg && rfc_is_running(tat) && tat->dv2_chg1) {
			ret = charger_dev_get_adc(tat->dv2_chg1, ADC_CHANNEL_VBAT, &value, &value);
			if (ret < 0) {
				pr_err("pe50 get vbat failed, ret = %d\n", ret);
			}
		}
		*val = value;
		break;
	case TRAN_TEST_PROP_VBAT_GAUGE:
		ret = tran_dev_get_prop(gauge_dev, TRAN_PROP_VOLTAGE_NOW, &tran_val);
		if (ret < 0) {
			pr_err("get vbus failed, ret = %d\n", ret);
		}
		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_VTA_SETTING:
		if (pe5_info && pe5_info->data && rfc_is_running(tat))
			*val = pe5_info->data->vta_setting;
		else
			*val = TC_GET_ERR;
		break; 
	case TRAN_TEST_PROP_ITA_SETTING:
		if (pe5_info && pe5_info->data && rfc_is_running(tat))
			*val = pe5_info->data->ita_setting;
		else
			*val = TC_GET_ERR;
		break; 
	case TRAN_TEST_PROP_VTA_MEASURE:
		if (pe5_info && pe5_info->data && rfc_is_running(tat))
			*val = pe5_info->data->vta_measure;
		else
			*val = TC_GET_ERR;
		break;
	case TRAN_TEST_PROP_ITA_MEASURE:
		if (pe5_info && pe5_info->data && rfc_is_running(tat))
			*val = pe5_info->data->ita_measure;
		else
			*val = TC_GET_ERR;
		break;
	case TRAN_TEST_PROP_PA_TEMP:
		*val = max(tc_get_tpa_temp_4g(),
					tc_get_tpa_temp_5g());
		break;
	case TRAN_TEST_PROP_MACHINE_TEMP:
		tran_dev_get_prop(temp_forecast_dev,
				TRAN_PROP_FORECAST_MACHINE_TEMP, &tran_val);
		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_AMBIENT_TEMP:
		tran_dev_get_prop(ambient_dev,
				TRAN_PROP_AMBIENT_TEMP, &tran_val);
		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_AMBIENT_TEMP_UPDATE:
		tran_dev_get_prop(ambient_dev,
				TRAN_PROP_AMBIENT_TEMP_UPDATE, &tran_val);
		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_AMBIENT_TEMP_UPDATE_TIME:
		tran_dev_get_prop(ambient_dev,
				TRAN_PROP_AMBIENT_TEMP_UPDATE_TIME, &tran_val);
		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_BAT_TEMP:
		ret = tran_dev_get_prop(gauge_dev, TRAN_PROP_TEMP, &tran_val);
		if (ret < 0) {
			pr_err("get bat temp failed, ret = %d\n", ret);
		}
		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_PCB_TEMP:
		*val = tc_get_tpcb_temp();
		break;
	case TRAN_TEST_PROP_SW_TEMP:
		*val = TC_GET_ERR;
		break;
	case TRAN_TEST_PROP_CP_TEMP:
		*val = TC_GET_ERR;
		break;
	case TRAN_TEST_PROP_USB_TEMP:
		tc_get_tusb_temp(val);
		break;
	case TRAN_TEST_PROP_TA_TEMP:
		if (pe5_alg && rfc_is_running(tat)) {
			ret = tchg_alg_get_prop(pe5_alg, ALG_TA_TEMP, &value);
			if (ret < 0) {
				pr_err("get tta fail(%d)\n", ret);
			}
			*val = value;
		} else {
			*val = TC_GET_ERR;
		}
		break;
	case TRAN_TEST_PROP_CHG_STATUS:
		ret =tran_dev_get_prop(gauge_dev, TRAN_PROP_BATT_STATUS, &tran_val);
		if (ret < 0) {
			pr_err("get chg status failed, ret = %d\n", ret);
		}
		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_SOC:
		ret = tran_dev_get_prop(gauge_dev, TRAN_PROP_CAPACITY, &tran_val);
		if (ret < 0) {
			pr_err("get uisoc failed, ret = %d\n", ret);
		}
		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_R_CABLE:
		if (pe5_info && pe5_info->data && rfc_is_running(tat))
			*val = pe5_info->data->r_cable;
		else
			*val = TC_GET_ERR;
		break; 
	case TRAN_TEST_PROP_GAME_MODE:
		*val = TC_GET_ERR;
		break;
	case TRAN_TEST_PROP_CV:
		ret = charger_dev_get_constant_voltage(tat->chg_dev, &value);
		if (ret < 0) {
			pr_err("get chg_dev cv failed, ret = %d\n", ret);
		}
		*val = value;
		break;
	case TRAN_TEST_PROP_ITERM:
		ret = power_supply_get_property(tat->chg_psy,
				POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT, &pval); 
		if (ret < 0) {
			pr_err("get chg_dev term cur failed, ret = %d\n", ret);
		}
		*val = pval.intval;
		break;
	case TRAN_TEST_PROP_MIVR:
		ret = charger_dev_get_mivr(tat->chg_dev, &value);
		if (ret < 0) {
			pr_err("get chg_dev mivr failed, ret = %d\n", ret);
		}
		*val = value;
		break;
	case TRAN_TEST_PROP_ILMT:
		ret = charger_dev_get_input_current(tat->chg_dev, &value);
		if (ret < 0) {
			pr_err("get chg_dev ilim failed, ret = %d\n", ret);
		}
		*val = value;
		break;
	/* dual battery param */

	case TRAN_TEST_PROP_DUAL_BATT_VOL_DIFF:
		if (!tat->fg_a_dev || !tat->fg_b_dev) {
			*val = TC_GET_ERR;
			break;
		}

		tran_dev_get_prop(tat->fg_a_dev, TRAN_PROP_BATT_VOLT, &tran_val);
		fg_a_vbat = tran_val.intval;
		tran_dev_get_prop(tat->fg_b_dev, TRAN_PROP_BATT_VOLT, &tran_val);
		fg_b_vbat = tran_val.intval;

		*val = fg_a_vbat - fg_b_vbat;
		break;
	case TRAN_TEST_PROP_DUAL_BATT_ONLINE_STATUS:
		tran_dev_get_prop(gauge_dev, TRAN_PROP_BATT_WORKING_STATUS, &tran_val);
	
		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_MASTER_VBAT:
		if (tat->fg_a_dev) {
			tran_dev_get_prop(tat->fg_a_dev, TRAN_PROP_BATT_VOLT, &tran_val);
			*val = tran_val.intval;
		} else
			*val = TC_GET_ERR;
		break;
	case TRAN_TEST_PROP_MASTER_IBAT:
		if (tat->fg_a_dev) {
			tran_dev_get_prop(tat->fg_a_dev, TRAN_PROP_BATT_NOW_CURR, &tran_val);
			*val = tran_val.intval;
		} else
			*val = TC_GET_ERR;
		break;
	case TRAN_TEST_PROP_MASTER_TEMP:
		if (tat->fg_a_dev) {
			tran_dev_get_prop(tat->fg_a_dev, TRAN_PROP_BATT_TEMP, &tran_val);
			*val = tran_val.intval;
		} else 
			*val = TC_GET_ERR;
		break;
	case TRAN_TEST_PROP_MASTER_SOC:
		if (tat->fg_a_dev) {
			tran_dev_get_prop(tat->fg_a_dev, TRAN_PROP_BATT_SOC, &tran_val);
			*val = tran_val.intval;
		} else
			*val = TC_GET_ERR;
		break;
	case TRAN_TEST_PROP_MASTER_FCC:
		if (tat->fg_a_dev) {
			tran_dev_get_prop(tat->fg_a_dev, TRAN_PROP_BATT_FCC, &tran_val);
			*val = tran_val.intval;
		} else
			*val = TC_GET_ERR;
		break;
	case TRAN_TEST_PROP_MASTER_RC:
		if (tat->fg_a_dev) {
			tran_dev_get_prop(tat->fg_a_dev, TRAN_PROP_BATT_RM, &tran_val);
			*val = tran_val.intval;
		} else
			*val = TC_GET_ERR;
		break;
	case TRAN_TEST_PROP_MASTER_BATT_EN:
		if (tat->fg_a_dev) {
			tran_dev_get_prop(tat->fg_a_dev, TRAN_PROP_BATT_EN, &tran_val);
			*val = tran_val.intval;
		} else
			*val = TC_GET_ERR;
		break;
	case TRAN_TEST_PROP_SLAVE_VBAT:
		if (tat->fg_b_dev) {
			tran_dev_get_prop(tat->fg_b_dev, TRAN_PROP_BATT_VOLT, &tran_val);
			*val = tran_val.intval;
		} else
	 		*val = TC_GET_ERR;
		break;
	case TRAN_TEST_PROP_SLAVE_IBAT:
		if (tat->fg_b_dev) {
			tran_dev_get_prop(tat->fg_b_dev, TRAN_PROP_BATT_NOW_CURR, &tran_val);
			*val = tran_val.intval;
		} else
			*val = TC_GET_ERR;
		break;
	case TRAN_TEST_PROP_SLAVE_TEMP:
		if (tat->fg_b_dev) {
			tran_dev_get_prop(tat->fg_b_dev, TRAN_PROP_BATT_TEMP, &tran_val);
			*val = tran_val.intval;
		} else
			*val = TC_GET_ERR;
		break;
	case TRAN_TEST_PROP_SLAVE_SOC:
		if (tat->fg_b_dev) {
			tran_dev_get_prop(tat->fg_b_dev, TRAN_PROP_BATT_SOC, &tran_val);
			*val = tran_val.intval;
		} else
			*val = TC_GET_ERR;
		break;
	case TRAN_TEST_PROP_SLAVE_FCC:
		tran_dev_get_prop(tat->fg_b_dev, TRAN_PROP_BATT_FCC, &tran_val);
		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_SLAVE_RC:
		tran_dev_get_prop(tat->fg_b_dev, TRAN_PROP_BATT_RM, &tran_val);
		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_SLAVE_BATT_EN:
		if (tat->fg_b_dev) {
			tran_dev_get_prop(tat->fg_b_dev, TRAN_PROP_BATT_EN, &tran_val);
			*val = tran_val.intval;
		} else
			*val = TC_GET_ERR;
		break;

	/* typec */
	case TRAN_TEST_PROP_CC1:
		tran_dev_get_prop(tc_tcpc_dev, TRAN_PROP_TYPE_CC1, &tran_val);
		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_CC2:
		tran_dev_get_prop(tc_tcpc_dev, TRAN_PROP_TYPE_CC2, &tran_val);
		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_FUNC_IMPORT:
		if(tc_chg_pid_dev)
			type |= PID_FUNC;

		if(temp_forecast_dev)
			type |= SYS_POWER_FUNC;

		if(ambient_dev)
			type |= AMBIENT_DET_FUNC;

		if(tc_wd_dev)
			type |= WATER_DET_FUNC;

		if(tc_chg_transfer_dev)
			type |= NONLINEAR_UI_FUNC;

		if(tc_ac_ctl_dev)
			type |= TA_OFF_FUNC;
		
		if(temp_forecast_dev){
	    	np = of_find_node_by_name(NULL, "temp_forecast");
			if (np) {
				bool set_not_support;
				of_property_read_u32(np, "orgin_rate", &value);
				set_not_support = of_property_read_bool(np, "set_not_support");
				pr_info(" orgin_rate = %d  %d\n", value, set_not_support);
			if ((set_not_support == false) && (value > 0 && value < 100)) {
				type |= BATT_TEMP_FORECAST_FUNC;
				}
			}
		}
		if(tc_chg_dev){
	    	np = of_find_node_by_name(NULL, "charger");
			if (np) {
				en = of_property_read_bool(np, "support_long_life_recharger");
				pr_info("support_long_life_recharger = %d\n", en);
				if (en) {
					type |= LONG_BATT_LIFE_FUNC;
				}
			}
		}
		*val = type;
		break;
	case TRAN_TEST_PROP_CAPACITY:
		ret =tran_dev_get_prop(gauge_dev, TRAN_PROP_CAPACITY, &tran_val);
		if (ret < 0) {
			pr_err("get ui soc failed, ret = %d\n", ret);
		}
		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_ALG_RUNNING_VOL:
		ret = tran_dev_get_prop(tc_chg_dev, TRAN_PROP_RUNNING_VOL, &tran_val);
		if (ret < 0) {
			pr_err("get vbus failed, ret = %d\n", ret);
		}
		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_TA_POWER:
		ret =tran_dev_get_prop(tc_chg_dev, TRAN_PROP_TC_CHG_TYPE, &tran_val);
		if (ret < 0) {
			pr_err("get ibat failed, ret = %d\n", ret);
		}
		chg_type = tran_val.intval;
		if(chg_type == TRAN_USB_TYPE_SDP) {
			*val = TRAN_TEST_CHG_POWER_2P5W;
		} else if(chg_type == TRAN_USB_TYPE_CDP) {
			*val = TRAN_TEST_CHG_POWER_7P5W;
		} else if (chg_type == TRAN_USB_TYPE_NONSTAND) {
			*val = TRAN_TEST_CHG_POWER_6W;
		} else if(chg_type == TRAN_USB_TYPE_DCP) {
			*val = TRAN_TEST_CHG_POWER_10W;
		} else if(chg_type == TRAN_USB_TYPE_RFC) {
			if(pe5_alg && rfc_is_running(tat)) {
				tchg_alg_get_prop(pe5_alg, ALG_ADAPTER_CAPACITY, &value);
				*val = value;
			} else
				*val = TC_GET_ERR;

		} else if (chg_type == TRAN_USB_TYPE_WIRELESS) {
			*val = TRAN_TEST_CHG_POWER_15W;
		} else if(chg_type == TRAN_USB_TYPE_WIRELESS_PE50) {
			if(pe5_alg && rfc_is_running(tat)) {
				tchg_alg_get_prop(pe5_alg, ALG_ADAPTER_CAPACITY, &value);
				*val = value;
			} else
				*val= TC_GET_ERR;
		} else if(chg_type == TRAN_USB_TYPE_PDC) {
			if(pdc_alg && tchg_alg_is_algo_running(pdc_alg)) {
				tchg_alg_get_prop(pdc_alg, ALG_ADAPTER_CAPACITY, &value);
				*val = value;
			} else
				*val= TC_GET_ERR;
		} else if(chg_type == TRAN_USB_TYPE_PD_PPS){
			if(pe5_alg && rfc_is_running(tat)){
				tchg_alg_get_prop(pe5_alg, ALG_ADAPTER_CAPACITY, &value);
				*val = value;
			} else
				*val = TC_GET_ERR;
		} else if((chg_type == TRAN_USB_TYPE_TC30) || (chg_type == TRAN_USB_TYPE_HVDCP)
			|| (chg_type == TRAN_USB_TYPE_PE4) || (chg_type == TRAN_USB_TYPE_PE2) || (chg_type == TRAN_USB_TYPE_PE)) {
			*val = TRAN_TEST_CHG_POWER_18W;
		} else {
			*val = TRAN_TEST_CHG_POWER_0W;
		}
		break;
	case TRAN_TEST_PROP_CHGSPEED:
		if (pe5_alg && rfc_is_running(tat)) {
			tchg_alg_get_prop(pe5_alg, ALG_MULTI_CHG_SPEED, &value);
			*val = value;
		} else 
			*val = 0;
		break;
	case TRAN_TEST_PROP_REAL_SOC:
		if(gauge_dev)
			tran_dev_get_prop(gauge_dev, TRAN_PROP_FG_REAL_SOC, &tran_val);
		
		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_FG_VSOC:
		if(gauge_dev)
			tran_dev_get_prop(gauge_dev, TRAN_PROP_FG_VSOC, &tran_val);

		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_FG_CSOC:		
		if(gauge_dev)
			tran_dev_get_prop(gauge_dev, TRAN_PROP_FG_CSOC, &tran_val);

		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_FG_DO_C:
		if(gauge_dev)
			tran_dev_get_prop(gauge_dev, TRAN_PROP_FG_DO_C, &tran_val);

		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_FG_DO_V:
		if(gauge_dev)
			tran_dev_get_prop(gauge_dev, TRAN_PROP_FG_DO_V, &tran_val);

		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_FG_UI_SOC:
		if(gauge_dev)
			tran_dev_get_prop(gauge_dev, TRAN_PROP_FG_UI_SOC, &tran_val);

		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_FG_AGING:
		if(gauge_dev)
			tran_dev_get_prop(gauge_dev, TRAN_PROP_FG_AGING, &tran_val);

		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_FG_C_CAR:
		if(gauge_dev)
			tran_dev_get_prop(gauge_dev, TRAN_PROP_FG_HW_CAR, &tran_val);

		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_FG_BAT_CYCLE:
		if(gauge_dev)
			tran_dev_get_prop(gauge_dev, TRAN_PROP_BATTERY_CYCLE, &tran_val);

		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_FG_QMAX:
		if(gauge_dev)
			tran_dev_get_prop(gauge_dev, TRAN_PROP_GET_Q_MAX, &tran_val);

		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_FG_RM_CAPACITY:
		if(gauge_dev)
			tran_dev_get_prop(gauge_dev, TRAN_PROP_BATT_RM, &tran_val);

		#if IS_ENABLED(CONFIG_TC_BATTERY)
			*val = tran_val.intval / 1000;
		#else
			*val = tran_val.intval;
		#endif

		break;
	case TRAN_TEST_PROP_BATTERY_ID:
		if(tran_battery_dev)
			tran_dev_get_prop(tran_battery_dev, TRAN_PROP_BATTERY_ID, &tran_val);

		*val = tran_val.intval;
		break;
	case TRAN_TEST_PROP_FG_OLD_CAR:
		*val = tat->mtk_fg_old_car;
		break;
	default:
		*val = -1;
		break;
	}
	return ret;
}

static ssize_t tran_sysfs_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct power_supply *psy;
	struct tran_auto_test *tat;
	struct tran_test_sysfs_field_info *tran_attr;
	int val;
	ssize_t ret;

	ret = kstrtoint(buf, 0, &val);
	if (ret < 0)
		return ret;

	psy = dev_get_drvdata(dev);
	tat = (struct tran_auto_test *)power_supply_get_drvdata(psy);

	tran_attr = container_of(attr,
		struct tran_test_sysfs_field_info, attr);
	if (tran_attr->set != NULL) {
		mutex_lock(&tat->ops_lock);
		tran_attr->set(tat, tran_attr, val);
		mutex_unlock(&tat->ops_lock);
	}

	return count;
}

static int tran_sysfs_get_str(char *str, int val)
{
	int i, len = 0;

	for (i = 0; i < ARRAY_SIZE(tran_func_array); i++) {
		strcat(str, tran_func_array[i].string);
		if (val & tran_func_array[i].id) {
			strcat(str, "Y\n");
		} else {
			strcat(str, "N\n");
		}
	}
	strcat(str, "\t");

	return len;
}

static ssize_t tran_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct power_supply *psy;
	struct tran_auto_test *tat;
	struct tran_test_sysfs_field_info *tran_attr;
	int val = 0;
	ssize_t count;
	char str[500] = {0};

	psy = dev_get_drvdata(dev);
	tat = (struct tran_auto_test *)power_supply_get_drvdata(psy);

	tran_attr = container_of(attr,
		struct tran_test_sysfs_field_info, attr);
	if (tran_attr->get != NULL) {
		mutex_lock(&tat->ops_lock);
		tran_attr->get(tat, tran_attr, &val);
		mutex_unlock(&tat->ops_lock);
	}

	if (tran_attr->prop == TRAN_TEST_PROP_CHG_RUN_PATH) {
		count = scnprintf(buf, PAGE_SIZE, "%s\n", run_path);
		pr_err("run_path\n");
		memset(run_path, 0, sizeof(char) * 100 * 100);
		offset = 0;
	} else if (tran_attr->prop == TRAN_TEST_PROP_CHG_TYPE) {
		count = scnprintf(buf, PAGE_SIZE, "%s\n", TRAN_USB_TYPE_TEXT[val]);
	} else if (tran_attr->prop == TRAN_TEST_PROP_CC1 || tran_attr->prop == TRAN_TEST_PROP_CC2) {
		count = scnprintf(buf, PAGE_SIZE, "%s\n", TRAN_CC_STATUS_TEXT[val]);
	} else if (tran_attr->prop == TRAN_TEST_PROP_CHG_STATUS) {
		count = scnprintf(buf, PAGE_SIZE, "%s\n", TRAN_CHG_STATUS_TEXT[val]);
	} else if (tran_attr->prop == TRAN_TEST_PROP_FUNC_IMPORT) {
		tran_sysfs_get_str(str, val);
		count = scnprintf(buf, PAGE_SIZE, "%s\n", str);
	} else if (tran_attr->prop == TRAN_TEST_PROP_DUAL_BATT_ONLINE_STATUS) {
		count = scnprintf(buf, PAGE_SIZE, "%s\n", BATTERY_ONLINE_STATUS[val]);
	} else {
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
	}
	return count;
}

int tran_auto_test_set_property(enum tran_property bp, int val)
{
	struct tran_auto_test *tat;
	struct power_supply *psy;
	struct tran_test_sysfs_field_info *attr;

	int ret = 0;

	psy = power_supply_get_by_name("tran-auto-test");
	if (psy == NULL) {
		pr_err("%s no dev\n", __func__);
		return -ENODEV;
	}

	tat = (struct tran_auto_test *)power_supply_get_drvdata(psy);

	attr = tat->attr;
	if (attr == NULL) {
		pr_err("%s attr =NULL\n", __func__);
		return -ENODEV;
	}

	if (attr[bp].prop == bp) {
		pr_err("%s\n", __func__);
		mutex_lock(&tat->ops_lock);
		ret = attr[bp].set(tat, &attr[bp], val);
		mutex_unlock(&tat->ops_lock);
	} else {
		pr_err("%s bp:%d idx error\n", __func__, bp);
		return -ENOTSUPP;
	}

	return ret;
}
EXPORT_SYMBOL(tran_auto_test_set_property);

static struct tran_test_sysfs_field_info tran_sysfs_field_tbl[] = {
	TRAN_TEST_SYSFS_INFO_FIELD_RW(vbus,         TRAN_TEST_PROP_VBUS),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(ibus,         TRAN_TEST_PROP_IBUS),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(vbat_cp,      TRAN_TEST_PROP_VBAT_CP),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(vbat_gauge,   TRAN_TEST_PROP_VBAT_GAUGE),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(ibat,         TRAN_TEST_PROP_IBAT),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(vta_setting,  TRAN_TEST_PROP_VTA_SETTING),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(ita_setting,  TRAN_TEST_PROP_ITA_SETTING),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(vta_measure,  TRAN_TEST_PROP_VTA_MEASURE),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(ita_measure,  TRAN_TEST_PROP_ITA_MEASURE),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(bat_temp,     TRAN_TEST_PROP_BAT_TEMP),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(pcb_temp,     TRAN_TEST_PROP_PCB_TEMP),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(sw_temp,      TRAN_TEST_PROP_SW_TEMP),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(cp_temp,      TRAN_TEST_PROP_CP_TEMP),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(usb_temp,     TRAN_TEST_PROP_USB_TEMP),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(ta_temp,      TRAN_TEST_PROP_TA_TEMP),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(machine_temp, TRAN_TEST_PROP_MACHINE_TEMP),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(ambient_temp, TRAN_TEST_PROP_AMBIENT_TEMP),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(ambient_temp_update, TRAN_TEST_PROP_AMBIENT_TEMP_UPDATE),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(ambient_temp_update_time, TRAN_TEST_PROP_AMBIENT_TEMP_UPDATE_TIME),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(pa_temp,      TRAN_TEST_PROP_PA_TEMP),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(chg_type,     TRAN_TEST_PROP_CHG_TYPE),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(chg_status,   TRAN_TEST_PROP_CHG_STATUS),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(soc,          TRAN_TEST_PROP_SOC),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(r_cable,      TRAN_TEST_PROP_R_CABLE),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(game_mode,    TRAN_TEST_PROP_GAME_MODE),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(cv,           TRAN_TEST_PROP_CV),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(iterm,        TRAN_TEST_PROP_ITERM),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(mivr,         TRAN_TEST_PROP_MIVR),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(ilmt,         TRAN_TEST_PROP_ILMT),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(cc1_status,   TRAN_TEST_PROP_CC1),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(cc2_status,   TRAN_TEST_PROP_CC2),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(func_import,    TRAN_TEST_PROP_FUNC_IMPORT),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(alg_running_vol,TRAN_TEST_PROP_ALG_RUNNING_VOL),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(capacity,   TRAN_TEST_PROP_CAPACITY),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(real_soc,         TRAN_TEST_PROP_REAL_SOC),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(ta_power,         TRAN_TEST_PROP_TA_POWER),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(chg_speed,         TRAN_TEST_PROP_CHGSPEED),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(v_soc,         TRAN_TEST_PROP_FG_VSOC),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(c_soc,         TRAN_TEST_PROP_FG_CSOC),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(do_c,         TRAN_TEST_PROP_FG_DO_C),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(do_v,         TRAN_TEST_PROP_FG_DO_V),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(ui_soc,         TRAN_TEST_PROP_FG_UI_SOC),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(aging,         TRAN_TEST_PROP_FG_AGING),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(c_car,         TRAN_TEST_PROP_FG_C_CAR),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(bat_cycle,         TRAN_TEST_PROP_FG_BAT_CYCLE),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(qmax,         TRAN_TEST_PROP_FG_QMAX),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(rm_capacity,         TRAN_TEST_PROP_FG_RM_CAPACITY),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(battery_id,         TRAN_TEST_PROP_BATTERY_ID),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(old_car,         TRAN_TEST_PROP_FG_OLD_CAR),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(fix_chg_type,   TRAN_TEST_PROP_FIX_CHG_TYPE),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(stop_chg,       TRAN_TEST_PROP_STOP_CHG),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(quick_chg_back, TRAN_TEST_PROP_QUICK_CHG_BACK),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(irq_test,       TRAN_TEST_PROP_IRQ_TEST),

	TRAN_TEST_SYSFS_INFO_FIELD_RW(dual_batt_vol_diff, TRAN_TEST_PROP_DUAL_BATT_VOL_DIFF),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(dual_batt_online_status, TRAN_TEST_PROP_DUAL_BATT_ONLINE_STATUS),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(master_vbat, TRAN_TEST_PROP_MASTER_VBAT),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(master_ibat, TRAN_TEST_PROP_MASTER_IBAT),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(master_temp, TRAN_TEST_PROP_MASTER_TEMP),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(master_soc,  TRAN_TEST_PROP_MASTER_SOC),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(master_fcc,  TRAN_TEST_PROP_MASTER_FCC),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(master_rc,   TRAN_TEST_PROP_MASTER_RC),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(master_batt_en,   TRAN_TEST_PROP_MASTER_BATT_EN),
	
	TRAN_TEST_SYSFS_INFO_FIELD_RW(slave_vbat,  TRAN_TEST_PROP_SLAVE_VBAT),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(slave_ibat,  TRAN_TEST_PROP_SLAVE_IBAT),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(slave_temp,  TRAN_TEST_PROP_SLAVE_TEMP),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(slave_soc,   TRAN_TEST_PROP_SLAVE_SOC),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(slave_fcc,   TRAN_TEST_PROP_SLAVE_FCC),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(slave_rc,    TRAN_TEST_PROP_SLAVE_RC),
	TRAN_TEST_SYSFS_INFO_FIELD_RW(slave_batt_en,   TRAN_TEST_PROP_SLAVE_BATT_EN),

	TRAN_TEST_SYSFS_INFO_FIELD_RW(run_path,    TRAN_TEST_PROP_CHG_RUN_PATH),
};

static struct attribute *tran_test_sysfs_attrs[TRAN_TEST_PROP_MAX + 1];

static const struct attribute_group tran_test_sysfs_attr_group = {
	.attrs = tran_test_sysfs_attrs,
};

static void tran_test_sysfs_init_attrs(void)
{
	int i, limit = ARRAY_SIZE(tran_sysfs_field_tbl);

	for (i = 0; i < limit; i++)
		tran_test_sysfs_attrs[i] = &tran_sysfs_field_tbl[i].attr.attr;

	tran_test_sysfs_attrs[limit] = NULL; /* Has additional entry for this */
}

static int tran_test_sysfs_create_group(struct tran_auto_test *tat)
{
	tran_test_sysfs_init_attrs();

	return sysfs_create_group(&tat->tran_psy->dev.kobj,
			&tran_test_sysfs_attr_group);
}

static int tran_psy_get_property(struct power_supply *psy, enum power_supply_property psp,
					union power_supply_propval *val)
{
	/* struct tran_auto_test *tat = power_supply_get_drvdata(psy); */
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

static int tran_psy_set_property(struct power_supply *psy,
			       enum power_supply_property prop,
			       const union power_supply_propval *val)
{
	/* struct tran_auto_test *tat = power_supply_get_drvdata(psy); */

	return 0;
}


static int tran_prop_is_writeable(struct power_supply *psy,
				       enum power_supply_property prop)
{
	return 0;
}

static int tran_test_psy_register(struct tran_auto_test *tat)
{
	struct power_supply_config tran_psy_cfg = {};

	tat->tran_psy_d.name = "tran-auto-test";
	tat->tran_psy_d.type = POWER_SUPPLY_TYPE_UNKNOWN;
	tat->tran_psy_d.properties = tran_psy_props;
	tat->tran_psy_d.num_properties = ARRAY_SIZE(tran_psy_props);
	tat->tran_psy_d.get_property = tran_psy_get_property;
	tat->tran_psy_d.set_property = tran_psy_set_property;
	tat->tran_psy_d.property_is_writeable = tran_prop_is_writeable;

	tran_psy_cfg.drv_data = tat;
	tran_psy_cfg.num_supplicants = 0;
	tat->tran_psy = devm_power_supply_register(tat->dev,
						&tat->tran_psy_d,
						&tran_psy_cfg);
	if (IS_ERR(tat->tran_psy)) {
		pr_err("Failed to register battery psy\n");
		return PTR_ERR(tat->tran_psy);
	}

	return 0;
}

static int tran_test_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct tran_auto_test *tat = NULL;

	pr_info("%s\n", __func__);

	tat = devm_kzalloc(&pdev->dev, sizeof(*tat), GFP_KERNEL);
	if (!tat)
		return -ENOMEM;

	run_path = devm_kzalloc(&pdev->dev, sizeof(char) * 100 * 100, GFP_KERNEL);
	if (!run_path)
		return -ENOMEM;

	tat->dev = &pdev->dev;
	tat->attr = tran_sysfs_field_tbl;

	mutex_init(&tat->ops_lock);

	ret = tran_test_psy_register(tat);
	if (ret != 0) {
		pr_err("%s: Failed to register tat psy\n", __func__);
		goto err;
	}

	ret = tran_test_check_psy(tat);
	if (ret != 0) {
		pr_err("%s: tat check psy fail, defer\n", __func__);
	}

	tran_test_sysfs_create_group(tat);

	pr_info("%s done\n", __func__);

	return 0;

err:
	mutex_destroy(&tat->ops_lock);
	return ret;
}

static int tran_test_remove(struct platform_device *pdev)
{
	pr_info("%s\n", __func__);

	return 0;
}

static int tran_test_suspend(struct device *dev)
{
	return 0;
}

static int tran_test_resume(struct device *dev)
{
	return 0;
}

static SIMPLE_DEV_PM_OPS(tran_auto_test_pm_ops, tran_test_suspend,
	tran_test_resume);

static const struct of_device_id tran_auto_test_match[] = {
	{ .compatible = "tc,auto_test", },
	{ },
};

static struct platform_driver tran_auto_test_driver = {
	.probe = tran_test_probe,
	.remove = tran_test_remove,
	.driver = {
		.name = "tran-auto-test",
		.owner = THIS_MODULE,
		.pm = &tran_auto_test_pm_ops,
		.of_match_table = tran_auto_test_match,
	},
};

static s32 __init tc_auto_test_det_init(void)
{
	return platform_driver_register(&tran_auto_test_driver);
}

static void __exit tc_auto_test_det_exit(void)
{
	platform_driver_unregister(&tran_auto_test_driver);
}

late_initcall(tc_auto_test_det_init);
module_exit(tc_auto_test_det_exit);

MODULE_DESCRIPTION("mt-charger-detection");
MODULE_AUTHOR("MediaTek");
MODULE_LICENSE("GPL v2");
