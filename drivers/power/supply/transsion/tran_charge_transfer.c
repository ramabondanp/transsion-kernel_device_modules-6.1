// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#define pr_fmt(fmt)     "[CHARGE_TRANSFER] %s: " fmt, __func__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/alarmtimer.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include "tc_charger_class.h"
#include "tc_adapter_class.h"
#include "tc_ta_class.h"
#include "tc_common_class.h"
#include "tc_charger.h"

static int g_charge_transfer_plan = -1;
module_param(g_charge_transfer_plan, int, 0664);

static bool g_charge_transfer_en = false;
module_param(g_charge_transfer_en, bool, 0664);
static int g_raw_ui_soc = 0;
module_param(g_raw_ui_soc, int, 0664);

#define FFC_SMOOTH_LEN   4
#define VALID_CHAGER_THRESHOLD 	5
#define ENABLE_ALGO_THRESHOLD 	(90 * VALID_CHAGER_THRESHOLD) / 100
#define LOW_COUNT_SECTION_THRESHOLD 	(90 * VALID_CHAGER_THRESHOLD) / 100
#define HIGH_COUNT_SECTION_THRESHOLD 	(70 * VALID_CHAGER_THRESHOLD) / 100

struct nvram_chg_scene
{
	char soc;
	char bat_algo;
	int soc_map[6];
	int temp[4];/*low,hight,avg,cnt*/
	int cycle_cnt;
};

struct charge_transfer_info {
	struct device *dev;
	struct tran_device *charge_transfer_dev;
	struct tran_properties charge_transfer_props;
	struct tran_device *tcc_dev;
	struct power_supply *batt_psy;
	struct tran_device *batt_dev;

	struct work_struct charge_transfer_work;
	struct mutex charge_transfer_lock;

	int chg_status;
	int batt_soc;
	int raw_soc;
	int ui_soc;
	int old_soc;
	int start_soc;
	int middle_soc;
	int end_soc;
	int hidden_soc;

	bool charge_transfer_en;
	bool nv_boot_complete;

	int plug_in_soc;
	int plug_out_soc;
	int plug_in_temp;
	struct nvram_chg_scene scene;
	int manual_scheme;
	int middle_soc_1;
	int middle_soc_2;
	struct delayed_work init_real_soc_work;
	int init_complete_flag;
};

enum {
	CHARGE_TRANSFER_PLAN_0 = 0,
	CHARGE_TRANSFER_PLAN_1,
	CHARGE_TRANSFER_PLAN_2,
	CHARGE_TRANSFER_PLAN_3,
	CHARGE_TRANSFER_PLAN_4,
	CHARGE_TRANSFER_MANUAL_PLAN_0 = 0,
	CHARGE_TRANSFER_MANUAL_PLAN_1,
	CHARGE_TRANSFER_MANUAL_PLAN_2,
};

//static void check_whether_start_nonlinearity

enum {
	BATTERY_SOC_SECTION_0 = 0,
	BATTERY_SOC_SECTION_1,
	BATTERY_SOC_SECTION_2,
	BATTERY_SOC_SECTION_3,
};

struct ffc_smooth {
	int curr_lim;
	int time;
};

struct ffc_smooth ffc_dischg_smooth[FFC_SMOOTH_LEN] = {
	{50,   200},
	{300,  100},
	{600,   50},
	{1000,  25},
};

static int tran_charge_transfer_manual(struct charge_transfer_info *info);

static void charge_transfer_send_uevent_env(struct charge_transfer_info *info, int section, int temp)
{
	char prop_buf[32] = {0};
	char *envp[2] = {NULL, NULL};
	
	snprintf(prop_buf, sizeof(prop_buf), "SOC_SECTION:%d,TEMP:%d", section, temp);
	envp[0] = prop_buf;
	kobject_uevent_env(&info->dev->kobj, KOBJ_CHANGE, envp);

	return;
}

static void send_bat_algo_uevent(struct charge_transfer_info *info, char bat_algo)
{
	char prop_buf[32] = {0};
	char *envp[2] = {NULL, NULL};
	
	snprintf(prop_buf, sizeof(prop_buf), "BAT_ALGO:%d,", bat_algo);
	envp[0] = prop_buf;
	kobject_uevent_env(&info->dev->kobj, KOBJ_CHANGE, envp);

	return;
}

static int calculate_power_loss_frequency(struct charge_transfer_info *info)
{
	int i;
	int power_loss_feq;
	int batt_ma_avg;
	union com_propval tran_val = {0, };

	if (!info->tcc_dev)
		info->tcc_dev = tran_get_by_name("tc_charger");

	tran_dev_get_prop(info->tcc_dev, TRAN_PROP_GET_CHARGER_CURRENT, &tran_val);

	batt_ma_avg = tran_val.intval;

	pr_info("batt_ma_avg:%d\n", batt_ma_avg);

	for (i = 0; i < FFC_SMOOTH_LEN; i++) {
		if (abs(batt_ma_avg) < ffc_dischg_smooth[i].curr_lim) {
			power_loss_feq = ffc_dischg_smooth[i].time;
			break;
		}
	}

	if (i == FFC_SMOOTH_LEN) {
		power_loss_feq = ffc_dischg_smooth[FFC_SMOOTH_LEN - 1].time;
	}

	return power_loss_feq;
}

static int charge_transfer_algo(struct charge_transfer_info *info)
{
	int ui_soc = 0;
	int ret_uisoc = 0;
	int forward_compensation_coefficient = 0;
	int max_forward_compensation_coefficient = 0;
	int reverse_compensation_coefficient = 0;
	union com_propval tran_val = {0, };

	if (g_charge_transfer_plan == CHARGE_TRANSFER_PLAN_0) {
		if (info->batt_soc > info->start_soc) {
			ui_soc = info->raw_soc;
		} else if (info->batt_soc > info->middle_soc && info->batt_soc <= info->start_soc) {
			forward_compensation_coefficient = (info->start_soc - info->batt_soc) * ((info->hidden_soc * 100) / (info->start_soc - info->middle_soc));
			ui_soc = info->raw_soc - forward_compensation_coefficient;
		} else if (info->batt_soc <= info->middle_soc && info->batt_soc > info->end_soc) {
			max_forward_compensation_coefficient = (info->start_soc - info->middle_soc) * ((info->hidden_soc * 100) / (info->start_soc - info->middle_soc));
			reverse_compensation_coefficient = (info->middle_soc - info->batt_soc) * ((info->hidden_soc * 100) / (info->middle_soc - info->end_soc));
			ui_soc = info->raw_soc - max_forward_compensation_coefficient + reverse_compensation_coefficient;
		} else {
			ui_soc = info->raw_soc;
		}
	} else {
		if (info->batt_soc > info->start_soc) {
			ui_soc = info->raw_soc;
		} else if (info->batt_soc > info->middle_soc && info->batt_soc <= info->start_soc) {
			forward_compensation_coefficient = (info->start_soc - info->batt_soc) * ((info->hidden_soc * 100) / (info->start_soc - info->middle_soc));
			ui_soc = info->raw_soc + forward_compensation_coefficient;
		} else if (info->batt_soc <= info->middle_soc && info->batt_soc > info->end_soc) {
			max_forward_compensation_coefficient = (info->start_soc - info->middle_soc) * ((info->hidden_soc * 100) / (info->start_soc - info->middle_soc));
			reverse_compensation_coefficient = (info->middle_soc - info->batt_soc) * ((info->hidden_soc * 100) / (info->middle_soc - info->end_soc));
			ui_soc = info->raw_soc + max_forward_compensation_coefficient - reverse_compensation_coefficient;
		} else {
			ui_soc = info->raw_soc;
		}
	}

	pr_info("g_charge_transfer_plan:%d\n", g_charge_transfer_plan);

	pr_info("batt_soc:%d, raw_soc:%d, forward_compensation_coefficient:%d, max_forward_compensation_coefficient:%d, reverse_compensation_coefficient:%d, ui_soc:%d\n",
		info->batt_soc, info->raw_soc, forward_compensation_coefficient, max_forward_compensation_coefficient, reverse_compensation_coefficient, ui_soc);

	if (IS_ERR_OR_NULL(info->tcc_dev))
		info->tcc_dev = tran_get_by_name("tc_charger");
	if (ui_soc == 0) {
		tran_val.intval = ui_soc;
		tran_dev_set_prop(info->tcc_dev,
				TRAN_PROP_SET_BATT_RAW_SOC, &tran_val);
		ret_uisoc = 0;
	} else if (ui_soc == 10000) {
		tran_val.intval = ui_soc;
		tran_dev_set_prop(info->tcc_dev,
				TRAN_PROP_SET_BATT_RAW_SOC, &tran_val);
		ret_uisoc = 100;
	} else {
		ui_soc += 99;
		tran_val.intval = ui_soc;
		tran_dev_set_prop(info->tcc_dev,
				TRAN_PROP_SET_BATT_RAW_SOC, &tran_val);
		ret_uisoc = ui_soc / 100;
	}

#if IS_ENABLED(CONFIG_TC_BATTERY)
	if (ret_uisoc == 0 && ui_soc > 0)
		ret_uisoc = 1;
#endif
	return ret_uisoc;
}

static int charge_transfer_manual_algo(struct charge_transfer_info *info)
{
	int ui_soc;
	int ret_uisoc;
	int fcc = 0;
	int rcc = 0;
	int forward_compensation_coefficient = 0;
	int max_forward_compensation_coefficient = 0;
	int reverse_compensation_coefficient = 0;
	union com_propval tran_val = {0, };

	if (g_charge_transfer_plan == CHARGE_TRANSFER_MANUAL_PLAN_0) {
		if (info->raw_soc >= info->middle_soc_1) {
			ui_soc = min(10000, info->raw_soc + (info->hidden_soc / 2));
		} else if (info->raw_soc < info->middle_soc_1 && info->raw_soc >= (info->middle_soc_2 + (info->hidden_soc / 2))) {
			forward_compensation_coefficient = (info->middle_soc_1 - info->raw_soc) * ((info->hidden_soc * 100) / (info->middle_soc_1 - info->middle_soc_2));
			forward_compensation_coefficient = forward_compensation_coefficient / 100;
			ui_soc = min(10000, info->raw_soc + (info->hidden_soc / 2) - forward_compensation_coefficient);
		} else {
			ui_soc = min(100, info->raw_soc);
		}
	} else if (g_charge_transfer_plan == CHARGE_TRANSFER_MANUAL_PLAN_2) {
		if (info->raw_soc >= (info->start_soc + info->hidden_soc)) {
			fcc = (10000 - info->raw_soc) * ((info->hidden_soc * 10000) / (10000 - info->start_soc - info->hidden_soc));
			fcc = fcc / 10000;
			ui_soc = info->raw_soc - fcc;
		} else {
			rcc = (info->start_soc + info->hidden_soc - info->raw_soc) * (info->hidden_soc * 10000) / (info->start_soc + info->hidden_soc);
			rcc = rcc / 10000;
			ui_soc = info->raw_soc - info->hidden_soc + rcc;
		}
	} else {
		ui_soc = info->raw_soc;
	}

	pr_info("g_charge_transfer_plan:%d\n", g_charge_transfer_plan);

	pr_info("batt_soc:%d, raw_soc:%d, ui_soc:%d, forward_compensation_coefficient:%d, max_forward_compensation_coefficient:%d, reverse_compensation_coefficient:%d\n",
		info->batt_soc, info->raw_soc, ui_soc, forward_compensation_coefficient, max_forward_compensation_coefficient, reverse_compensation_coefficient);

	if (IS_ERR_OR_NULL(info->tcc_dev))
		info->tcc_dev = tran_get_by_name("tc_charger");
	if (ui_soc == 0) {
		tran_val.intval = ui_soc;
		tran_dev_set_prop(info->tcc_dev,
				TRAN_PROP_SET_BATT_RAW_SOC, &tran_val);
		ret_uisoc = 0;
	} else if (ui_soc == 10000) {
		tran_val.intval = ui_soc;
		tran_dev_set_prop(info->tcc_dev,
				TRAN_PROP_SET_BATT_RAW_SOC, &tran_val);
		ret_uisoc = 100;
	} else {
		ui_soc += 99;
		ui_soc = min(10000, ui_soc);
		tran_val.intval = ui_soc;
		tran_dev_set_prop(info->tcc_dev,
				TRAN_PROP_SET_BATT_RAW_SOC, &tran_val);
		ret_uisoc = ui_soc / 100;
	}

#if IS_ENABLED(CONFIG_TC_BATTERY)
	if (ret_uisoc == 0 && ui_soc > 0)
		ret_uisoc = 1;
#endif
	return ret_uisoc;
}

static void charge_transfer_init_param(struct charge_transfer_info *info)
{
	static int last_charge_transfer_plan = -1;

	if (last_charge_transfer_plan == g_charge_transfer_plan)
		return;

	if (info->manual_scheme != -1) {
		if (g_charge_transfer_plan == CHARGE_TRANSFER_MANUAL_PLAN_0) {
			info->start_soc = 10000;
			info->middle_soc_1 = 9900;
			info->middle_soc_2 = 100;
			info->end_soc = 0;
			info->hidden_soc = 200;
		}else if (g_charge_transfer_plan == CHARGE_TRANSFER_MANUAL_PLAN_2) {
			info->start_soc = 100;
			info->end_soc = 0;
			info->hidden_soc = 200;
		}
	} else {
		if (g_charge_transfer_plan == CHARGE_TRANSFER_PLAN_0) {
			info->start_soc = 90;
			info->middle_soc = 20;
			info->end_soc = 3;
			info->hidden_soc = 10;
		} else if (g_charge_transfer_plan == CHARGE_TRANSFER_PLAN_1) {
			info->start_soc = 90;
			info->middle_soc = 20;
			info->end_soc = 3;
			info->hidden_soc = 4;
		} else if (g_charge_transfer_plan == CHARGE_TRANSFER_PLAN_2) {
			info->start_soc = 90;
			info->middle_soc = 28;
			info->end_soc = 3;
			info->hidden_soc = 6;
		} else if (g_charge_transfer_plan == CHARGE_TRANSFER_PLAN_3) {
			info->start_soc = 90;
			info->middle_soc = 38;
			info->end_soc = 3;
			info->hidden_soc = 8;
		} else if (g_charge_transfer_plan == CHARGE_TRANSFER_PLAN_4) {
			info->start_soc = 90;
			info->middle_soc = 40;
			info->end_soc = 3;
			info->hidden_soc = 10;
		}
	}

	last_charge_transfer_plan = g_charge_transfer_plan;
}

static void choose_charge_transfer_plan(struct charge_transfer_info *info)
{
	int *soc_section = info->scene.soc_map;

	if (soc_section[0] > LOW_COUNT_SECTION_THRESHOLD) {
		g_charge_transfer_plan = CHARGE_TRANSFER_PLAN_0;
	} else if (soc_section[1] > HIGH_COUNT_SECTION_THRESHOLD) {
		g_charge_transfer_plan = CHARGE_TRANSFER_PLAN_2;
	} else if (soc_section[2] > HIGH_COUNT_SECTION_THRESHOLD) {
		g_charge_transfer_plan = CHARGE_TRANSFER_PLAN_3;
	} else if (soc_section[3] > HIGH_COUNT_SECTION_THRESHOLD) {
		g_charge_transfer_plan = CHARGE_TRANSFER_PLAN_4;
	} else {
		g_charge_transfer_plan = CHARGE_TRANSFER_PLAN_1;
	}
}

static void check_bat_nonlinearity_algo(struct charge_transfer_info *info)
{
	int i = 0;
	int count = 0;

	if (info->charge_transfer_en)
		return;

	for (i = 0; i < ARRAY_SIZE(info->scene.soc_map); i++) {
		count = count + info->scene.soc_map[i];
	}

	if (count > VALID_CHAGER_THRESHOLD) {
		info->charge_transfer_en = (info->scene.bat_algo >> 4) & 0x1;
		g_charge_transfer_plan = info->scene.bat_algo & 0xf;
		return;
	} else if (count < VALID_CHAGER_THRESHOLD) {
		info->charge_transfer_en = false;
		return;
	}

	if (info->scene.soc_map[0] > ENABLE_ALGO_THRESHOLD) {
		info->charge_transfer_en = true;
	} else if ((VALID_CHAGER_THRESHOLD - info->scene.soc_map[0]) > ENABLE_ALGO_THRESHOLD) {
		info->charge_transfer_en = true;
	} else {
		info->charge_transfer_en = false;
	}

	if (info->charge_transfer_en) {
		choose_charge_transfer_plan(info);
		send_bat_algo_uevent(info, (1 << 4) | g_charge_transfer_plan);
	}

	return;
}

static int tran_charge_transfer(struct charge_transfer_info *info)
{
	static int last_ui_soc = -1;
	static int last_batt_soc = -1;
	static bool up_break_point;
	static bool down_break_point;
	static ktime_t soc_oldtime;
	int ui_soc = 0;
	int dischg_time = 0;
	int batt_soc = info->batt_soc;

	ktime_t now_time, diff;
	struct timespec64 soc_difftime;

	/* error soc */
	if (batt_soc == -1 || !info->nv_boot_complete) {
		pr_err("err batt_soc or nv_boot_complete failed\n");
		return -1; // default use 50%
	}
	/* static int charge_transfer_algo(struct charge_transfer_info *info) */

	check_bat_nonlinearity_algo(info);

	if (!info->charge_transfer_en && !g_charge_transfer_en) {
		pr_info("algo is disable\n");
		last_ui_soc = batt_soc;
		last_batt_soc = batt_soc;
		return batt_soc;
	}

	now_time = ktime_get_boottime();
	diff = ktime_sub(now_time, soc_oldtime);
	soc_difftime = ktime_to_timespec64(diff);

	dischg_time = calculate_power_loss_frequency(info);

	if (last_batt_soc == -1)
		last_batt_soc = batt_soc;

	/* if (info->batt_soc == 0 || info->batt_soc == 100) { */
	/*         last_batt_soc = info->batt_soc; */
	/*         last_ui_soc = info->batt_soc; */
	/*         return batt_soc; */
	/* } */

	charge_transfer_init_param(info);

	ui_soc = charge_transfer_algo(info);

	pr_info("%s: batt_soc:%d, ui_soc:%d, last_batt_soc:%d, last_ui_soc:%d\n",
		__func__, batt_soc, ui_soc, last_batt_soc, last_ui_soc);

	if (last_ui_soc == -1)
		last_ui_soc = ui_soc;

	if (last_batt_soc - batt_soc > 0) {
		/* discharging */
		if (last_ui_soc - ui_soc > 1) {
			ui_soc = last_ui_soc - 1;
			down_break_point = true;
		}
		soc_oldtime = ktime_get_boottime();

		goto ret_uisoc;
	} else if (last_batt_soc - batt_soc < 0) {
		/* charging */
		if (ui_soc - last_ui_soc > 1) {
			ui_soc = last_ui_soc + 1;
			up_break_point = true;
		}
		soc_oldtime = ktime_get_boottime();

		goto ret_uisoc;
	} else {
		/* not change */
		if (last_ui_soc == ui_soc) {
			down_break_point = false;
			up_break_point = false;
		}

		if (down_break_point == true &&
			soc_difftime.tv_sec > dischg_time) {
			ui_soc = last_ui_soc - 1;
			down_break_point = false;
			soc_oldtime = ktime_get_boottime();
			goto ret_uisoc;
		}

		if (up_break_point == true &&
			soc_difftime.tv_sec > 15) {
			ui_soc = last_ui_soc + 1;
			up_break_point = false;
			soc_oldtime = ktime_get_boottime();
			goto ret_uisoc;
		}

		goto ret_last_uisoc;
	}

ret_uisoc:
	if (!IS_ERR_OR_NULL(info->batt_psy) && last_ui_soc != ui_soc)
		power_supply_changed(info->batt_psy);

	last_batt_soc = batt_soc;
	last_ui_soc = ui_soc;
	pr_info("%s: ret_uisoc last_batt_soc:%d, last_ui_soc:%d,"
		" up_break_point:%d, down_break_point:%d,"
		" soc_difftime:%lld, dischg_time:%d", __func__,
		last_batt_soc, last_ui_soc,
		up_break_point, down_break_point,
		soc_difftime.tv_sec, dischg_time);
	return ui_soc;
ret_last_uisoc:
	last_batt_soc = batt_soc;
	pr_info("%s: ret_last_uisoc last_batt_soc:%d, last_ui_soc:%d,"
		" up_break_point:%d, down_break_point:%d,"
		" soc_difftime:%lld", __func__,
		last_batt_soc, last_ui_soc,
		up_break_point, down_break_point,
		soc_difftime.tv_sec);
	return last_ui_soc;
}

static void charge_transfer_work(struct work_struct *work)
{
	struct charge_transfer_info *info =
	    container_of(work, struct charge_transfer_info, charge_transfer_work);

	mutex_lock(&info->charge_transfer_lock);
		if (info->manual_scheme != -1)
			info->ui_soc = tran_charge_transfer_manual(info);
		else
			info->ui_soc = tran_charge_transfer(info);
	mutex_unlock(&info->charge_transfer_lock);
}

static void charge_transfer_record_user_habit(struct charge_transfer_info *info)
{
	int soc_section = -1;
	int temp = info->plug_in_temp;

	if ((info->plug_in_soc >= info->plug_out_soc) ||
		((info->plug_out_soc - info->plug_in_soc) < 50)) {
		pr_err("invalid charger, ignore, plug_in_soc:%d, plug_out_soc:%d\n",
			info->plug_in_soc, info->plug_out_soc);
		return;
	}

	if (info->plug_in_soc <= 20) {
		soc_section = BATTERY_SOC_SECTION_0;
	} else {
		if (info->plug_in_soc > 20 && info->plug_in_soc <= 30) {
			soc_section = BATTERY_SOC_SECTION_1;
		} else if (info->plug_in_soc > 30 && info->plug_in_soc <= 40) {
			soc_section = BATTERY_SOC_SECTION_2;
		} else if (info->plug_in_soc > 40) {
			soc_section = BATTERY_SOC_SECTION_3;
		}
	}

	charge_transfer_send_uevent_env(info, soc_section, temp);
}

static int charge_transfer_check_tran_dev_ptr(struct tran_device **dev, const char *name)
{
	if (IS_ERR_OR_NULL(*dev)) {
		*dev = tran_get_by_name(name);
		if (IS_ERR_OR_NULL(*dev)) {
			pr_err("%s Couldn't get dev(%s)\n", __func__, name);
			return -EINVAL;
		}
	}

	return 0;
}

int ui1_batt_send_uevent_env(struct charge_transfer_info *info, int real_soc)
{
	char prop_buf[32] = {0};
	char *envp[2] = {NULL, NULL};

	pr_info("ui1_batt_send_uevent_env init_complete_flag=%d\n",info->init_complete_flag);
	if(info->init_complete_flag == 1) {
		snprintf(prop_buf, sizeof(prop_buf), "REAL_UI_SOC=%d",real_soc);
		envp[0] = prop_buf;
		kobject_uevent_env(&info->dev->kobj, KOBJ_CHANGE, envp);
		pr_info("REAL_UI_SOC EVENT send\n");
	}
	return 0;
}

int charge_transfer_get_soc_ready(struct charge_transfer_info *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = charge_transfer_check_tran_dev_ptr(&info->batt_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get batt_dev\n", __func__);
		return 0;
	}

	tran_dev_get_prop(info->batt_dev, TRAN_PROP_BATT_SOC_READY, &prop);

	return !!prop.intval;
}

#if IS_ENABLED(CONFIG_TC_BATTERY)
int charge_transfer_get_uisoc(struct charge_transfer_info *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = charge_transfer_check_tran_dev_ptr(&info->batt_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get batt_dev\n", __func__);
		return 0;
	}

	tran_dev_get_prop(info->batt_dev, TRAN_PROP_BATT_SOC, &prop);

	info->batt_soc = prop.intval;
	g_raw_ui_soc = prop.intval;
	pr_info("TRAN_PROP_SET_BATT_UI_SOC val->intval=%d old_soc=%d\n",prop.intval,info->old_soc);
	if((info->old_soc > 1  && prop.intval == 0)
	|| (info->old_soc > 1  && prop.intval == 1)
	|| (info->old_soc < 2  && prop.intval == 2)){
		pr_info("ui1_batt_send_uevent_env val->intval=%d old_soc=%d\n",prop.intval,info->old_soc);
		ui1_batt_send_uevent_env(info,prop.intval);
	}
	info->old_soc = prop.intval;
	/*schedule_work(&info->charge_transfer_work);*/

	return prop.intval;
}

int charge_transfer_get_accuracy_uisoc(struct charge_transfer_info *info)
{
	int ret = 0;
	union com_propval prop = {.intval = 0};

	ret = charge_transfer_check_tran_dev_ptr(&info->batt_dev, "tran_batt");
	if (ret < 0) {
		pr_info("%s Couldn't get batt_dev\n", __func__);
		return 0;
	}

	tran_dev_get_prop(info->batt_dev, TRAN_PROP_ACCURACY_UISOC, &prop);
	pr_debug("%s: %d\n", __func__, prop.intval);

	info->raw_soc = prop.intval;

	return prop.intval;
}
#endif

static int tran_charge_transfer_manual(struct charge_transfer_info *info)
{
	static int last_ui_soc = -1;
	static int last_batt_soc = -1;
	static int last_raw_soc = -1;
	static bool up_break_point;
	static bool down_break_point;
	static ktime_t soc_oldtime;
	int ui_soc = 0;
	int dischg_time = 0;
	int batt_soc = info->batt_soc;
	int raw_soc = info->raw_soc;

	ktime_t now_time, diff;
	struct timespec64 soc_difftime;

	/* error soc */
	if (batt_soc == -1) {
		pr_err("err batt_soc\n");
		return -1; // default use 50%
	}

	if (!info->charge_transfer_en && !g_charge_transfer_en) {
		pr_info("algo is disable\n");
		last_ui_soc = batt_soc;
		last_batt_soc = batt_soc;
		return batt_soc;
	}

	now_time = ktime_get_boottime();
	diff = ktime_sub(now_time, soc_oldtime);
	soc_difftime = ktime_to_timespec64(diff);

	dischg_time = calculate_power_loss_frequency(info);

	if (last_batt_soc == -1)
		last_batt_soc = batt_soc;

	if (last_raw_soc == -1)
		last_raw_soc = raw_soc;

	charge_transfer_init_param(info);

	ui_soc = charge_transfer_manual_algo(info);

	pr_info("%s: batt_soc:%d, raw_soc:%d, ui_soc:%d, last_batt_soc:%d, last_ui_soc:%d\n",
		__func__, batt_soc, raw_soc, ui_soc, last_batt_soc, last_ui_soc);

	if (last_ui_soc == -1)
		last_ui_soc = ui_soc;

	if (last_batt_soc - batt_soc > 0 || last_raw_soc - raw_soc > 0) {
		/* discharging */
		if (last_ui_soc - ui_soc > 1) {
			ui_soc = last_ui_soc - 1;
			down_break_point = true;
		}
		soc_oldtime = ktime_get_boottime();

		goto ret_uisoc;
	} else if (last_batt_soc - batt_soc < 0 || last_raw_soc - raw_soc < 0) {
		/* charging */
		if (ui_soc - last_ui_soc > 1) {
			ui_soc = last_ui_soc + 1;
			up_break_point = true;
		}
		soc_oldtime = ktime_get_boottime();

		goto ret_uisoc;
	} else {
		/* not change */
		if (last_ui_soc == ui_soc) {
			down_break_point = false;
			up_break_point = false;
		}

		if (down_break_point == true &&
			soc_difftime.tv_sec > dischg_time) {
			ui_soc = last_ui_soc - 1;
			down_break_point = false;
			soc_oldtime = ktime_get_boottime();
			goto ret_uisoc;
		}

		if (up_break_point == true &&
			soc_difftime.tv_sec > 15) {
			ui_soc = last_ui_soc + 1;
			up_break_point = false;
			soc_oldtime = ktime_get_boottime();
			goto ret_uisoc;
		}

		goto ret_last_uisoc;
	}

ret_uisoc:
	if (!IS_ERR_OR_NULL(info->batt_psy) && last_ui_soc != ui_soc)
		power_supply_changed(info->batt_psy);

	last_batt_soc = batt_soc;
	last_raw_soc = raw_soc;
	last_ui_soc = ui_soc;
	pr_info("%s: ret_uisoc last_batt_soc:%d, last_raw_soc:%d, last_ui_soc:%d,"
		" up_break_point:%d, down_break_point:%d,"
		" soc_difftime:%lld, dischg_time:%d", __func__,
		last_batt_soc, last_raw_soc, last_ui_soc,
		up_break_point, down_break_point,
		soc_difftime.tv_sec, dischg_time);
	return ui_soc;
ret_last_uisoc:
	last_batt_soc = batt_soc;
	last_raw_soc = raw_soc;
	pr_info("%s: ret_last_uisoc last_batt_soc:%d, last_raw_soc:%d, last_ui_soc:%d,"
		" up_break_point:%d, down_break_point:%d,"
		" soc_difftime:%lld", __func__,
		last_batt_soc, last_raw_soc, last_ui_soc,
		up_break_point, down_break_point,
		soc_difftime.tv_sec);
	return last_ui_soc;
}

static int charge_transfer_get_property(struct tran_device *dev,
				enum tran_common_prop prop,
				union com_propval *val)
{
	struct charge_transfer_info *info = tran_get_data(dev);
	int ret = 0;

	switch (prop) {
	case TRAN_PROP_GET_BATT_UI_SOC:
		mutex_lock(&info->charge_transfer_lock);
		if(charge_transfer_get_soc_ready(info) == true)
		{
#if IS_ENABLED(CONFIG_TC_BATTERY)
			charge_transfer_get_uisoc(info);
			charge_transfer_get_accuracy_uisoc(info);
#endif
			if (info->manual_scheme != -1)
				info->ui_soc = tran_charge_transfer_manual(info);
			else
				info->ui_soc = tran_charge_transfer(info);
			val->intval = info->ui_soc;
		} else
			val->intval = -1;
		mutex_unlock(&info->charge_transfer_lock);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static void init_real_soc_work(struct work_struct *work)
{
	struct charge_transfer_info *info = container_of(work, struct charge_transfer_info, init_real_soc_work.work);
	pr_info("init_real_soc_work in");
	if(tc_get_boot_finish()){
		info->init_complete_flag = 1;
		if(info->old_soc == 0 || info->old_soc == 1) {
			pr_info("ui1_batt_send_uevent_env init_real_soc_work");
			ui1_batt_send_uevent_env(info, info->old_soc);
		}
	} else {
		schedule_delayed_work(&info->init_real_soc_work, 5000);
	}
}

static int charge_transfer_set_property(struct tran_device *dev,
					   enum tran_common_prop prop,
					   const union com_propval *val)
{
	struct charge_transfer_info *info = tran_get_data(dev);
	int ret = 0;
	union com_propval tran_val = {0, };

	pr_info("prop=%d, val=%d\n", prop, val->intval);

	if (!info->tcc_dev)
		info->tcc_dev = tran_get_by_name("tc_charger");

	switch (prop) {
#if !IS_ENABLED(CONFIG_TC_BATTERY)
	case TRAN_PROP_SET_BATT_RAW_SOC:
		mutex_lock(&info->charge_transfer_lock);
		info->raw_soc = val->intval;
		mutex_unlock(&info->charge_transfer_lock);
		break;
	case TRAN_PROP_SET_BATT_UI_SOC:
		mutex_lock(&info->charge_transfer_lock);
		info->batt_soc = val->intval;
		g_raw_ui_soc = val->intval;
		mutex_unlock(&info->charge_transfer_lock);
		pr_info("TRAN_PROP_SET_BATT_UI_SOC val->intval=%d old_soc=%d\n",val->intval, info->old_soc);
		if((info->old_soc > 1  && val->intval == 0)
		|| (info->old_soc > 1  && val->intval == 1)
		|| (info->old_soc < 2  && val->intval == 2)){
			pr_info("ui1_batt_send_uevent_env val->intval=%d old_soc=%d\n",val->intval, info->old_soc);
			ui1_batt_send_uevent_env(info,val->intval);
		}
		info->old_soc = val->intval;
		/*schedule_work(&info->charge_transfer_work);*/
		break;
#endif
	case TRAN_PROP_USB_PLUG_IN:
		info->plug_in_soc = info->batt_soc;
		tran_dev_get_prop(info->tcc_dev, TRAN_PROP_GET_BATTERY_TEMPERATURE, &tran_val);
		info->plug_in_temp = tran_val.intval;
		break;
	case TRAN_PROP_USB_PLUG_OUT:
		info->plug_out_soc = info->batt_soc;
		charge_transfer_record_user_habit(info);
		if(info->old_soc == 0 || info->old_soc == 1){
			pr_info("ui1_batt_send_uevent_env plugout old_soc=%d\n", info->old_soc);
			ui1_batt_send_uevent_env(info,info->old_soc);
		}
		//schedule_work(&info->charge_transfer_work);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static struct tran_ops charge_transfer_ops = {
	.get_prop = charge_transfer_get_property,
	.set_prop = charge_transfer_set_property,
};

static int adapter_init_chg(struct charge_transfer_info *info)
{
	info->charge_transfer_props.alias_name = "charge_transfer";
	info->charge_transfer_dev = tran_device_register("charge_transfer",
						info->dev, info,
						&charge_transfer_ops,
						&info->charge_transfer_props);
	if (IS_ERR_OR_NULL(info->charge_transfer_dev))
		return -EINVAL;

	return 0;
}

static ssize_t show_charger_scene(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct charge_transfer_info *info = dev->driver_data;
	struct nvram_chg_scene *chg_scene = &info->scene;

	return sprintf(buf, "n_0:%d, n_1:%d, n_2:%d, n_3:%d, T_lo:%d, T_hi:%d, T_avg:%d, T_cnt:%d, algo:0x%x\n",
		chg_scene->soc_map[0], chg_scene->soc_map[1], chg_scene->soc_map[2], chg_scene->soc_map[3],
		chg_scene->temp[0], chg_scene->temp[1], chg_scene->temp[2], chg_scene->temp[3], chg_scene->bat_algo);
}

static ssize_t store_charger_scene(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct charge_transfer_info *info = dev->driver_data;
	struct nvram_chg_scene *scene = &info->scene;

	memcpy((void *)scene, (void *)buf, sizeof(*scene));
	return count;
}

static ssize_t show_nv_boot_complete(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t store_nv_boot_complete(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct charge_transfer_info *info = dev->driver_data;
	int val = 0;

	if (kstrtoint(buf, 10, &val) == 0) {
	        pr_err("val = %d\n", val);
		if (val == 1) {
			info->nv_boot_complete = true;
			if (!IS_ERR_OR_NULL(info->batt_psy))
				power_supply_changed(info->batt_psy);
		}
	}

	return count;
}

static DEVICE_ATTR(charger_scene, 0444, show_charger_scene, store_charger_scene);
static DEVICE_ATTR(nv_boot_complete, 0444, show_nv_boot_complete, store_nv_boot_complete);

static void charge_transfer_parse_dt(struct charge_transfer_info *info)
{
	int ret = 0;
	struct device_node *np = info->dev->of_node;

	ret = of_property_read_u32(np, "manual_scheme", &info->manual_scheme);
	if (ret < 0) {
		pr_err("parse manual_scheme failed\n");
		info->manual_scheme = -1;
	}
}

static int charge_transfer_probe(struct platform_device *pdev)
{
	int ret;
	struct charge_transfer_info *info;
	struct power_supply *batt_psy = NULL;

	pr_info("%s enter!\n", __func__);

	batt_psy = power_supply_get_by_name("battery");
	if (IS_ERR_OR_NULL(batt_psy)) {
		pr_err("failed to get batt_psy, need regain\n");
		return -EPROBE_DEFER;
	}

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);

	info->batt_soc = -1;
	info->manual_scheme = -1;

	info->batt_psy = power_supply_get_by_name("battery");
	if (IS_ERR_OR_NULL(info->batt_psy)) {
		pr_err("failed to get batt_psy, need regain\n");
		return -ENODEV;
	}

	charge_transfer_parse_dt(info);
	if (info->manual_scheme != -1) {
		pr_info("tran charge transfer manual_scheme:%d\n", info->manual_scheme);
		info->charge_transfer_en = true;
		g_charge_transfer_plan = info->manual_scheme;
	}

	ret = device_create_file(&(pdev->dev), &dev_attr_charger_scene);
	if (ret)
		return ret;

	ret = device_create_file(&(pdev->dev), &dev_attr_nv_boot_complete);
	if (ret)
		return ret;

	mutex_init(&info->charge_transfer_lock);
	INIT_WORK(&info->charge_transfer_work, charge_transfer_work);
	info->init_complete_flag = 0;
	INIT_DELAYED_WORK(&info->init_real_soc_work, init_real_soc_work);
	schedule_delayed_work(&info->init_real_soc_work, 5000);

	ret = adapter_init_chg(info);
	if (ret < 0) {
		pr_info("adapter control device failed\n");
		goto err_register_dev;
	}

	pr_info("%s end!\n", __func__);

	return 0;

err_register_dev:
	tran_device_unregister(info->charge_transfer_dev);
	mutex_destroy(&info->charge_transfer_lock);
	return ret;
}

static int charge_transfer_remove(struct platform_device *pdev)
{
	struct charge_transfer_info *info = platform_get_drvdata(pdev);

	pr_info("%s\n", __func__);

	if (!IS_ERR_OR_NULL(info->charge_transfer_dev))
		tran_device_unregister(info->charge_transfer_dev);

	return 0;
}

static void charge_transfer_shutdown(struct platform_device *dev)
{
	/* struct charge_transfer_info *info = platform_get_drvdata(dev); */
}

static const struct of_device_id charge_transfer_of_match[] = {
	{.compatible = "transsion, charge_transfer",},
	{},
};
MODULE_DEVICE_TABLE(of, charge_transfer_of_match);

static struct platform_driver charge_transfer_platdrv = {
	.probe = charge_transfer_probe,
	.remove = charge_transfer_remove,
	.shutdown = charge_transfer_shutdown,
	.driver = {
		.name = "charge_transfer",
		.owner = THIS_MODULE,
		.of_match_table = charge_transfer_of_match,
	},
};

static int __init charge_transfer_init(void)
{
	return platform_driver_register(&charge_transfer_platdrv);
}
late_initcall(charge_transfer_init);

static void __exit charge_transfer_exit(void)
{
	platform_driver_unregister(&charge_transfer_platdrv);
}
module_exit(charge_transfer_exit);

MODULE_DESCRIPTION("Transsion TA Control For AI charger");
MODULE_AUTHOR("Schack");
MODULE_VERSION("1.0.0_G");
MODULE_LICENSE("GPL");


