// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 MediaTek Inc.
 */

#define pr_fmt(fmt) "transceiver " fmt

#include <linux/err.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <uapi/linux/sched/types.h>
#include <linux/time.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/suspend.h>


#include <linux/of.h>
#include <linux/of_fdt.h>
#if defined(CONFIG_SENSOR_GET_SCREEN_POWER_STATE) || defined(CONFIG_SENSOR_GET_LCM_INFO)
#include <linux/notifier.h>
#ifdef CONFIG_DRM_MEDIATEK_V2
#include "mtk_panel_ext.h"
#include "mtk_disp_notify.h"
#endif
#endif


#include "ready.h"
#include "sensor_comm.h"
#include "sensor_list.h"
#include "share_memory.h"
#include "timesync.h"
#include "debug.h"
#include "custom_cmd.h"

#include "hf_sensor_type.h"
#include "sensor_freq.h"
#include "tran_sensor_cust.h"


#if IS_ENABLED(CONFIG_TRAN_SENSOR_DEBUG)
#include "../sensor_debug/tran_sensor_debug.h"
#endif


extern struct tran_sensor_cust_lcm_info cust_sensor_info[TRAN_UNDER_SENSOR_MAX];
extern struct tran_sensor_cwb_info als_cwb_info[TRAN_UNDER_SENSOR_MAX];
extern bool tran_sensor_un_als_support;
extern bool tran_sensor_ml_nv_state_get;
extern bool tran_sensor_sl_nv_state_get;
extern int32_t g_tran_debug_mode;
extern uint32_t nv_restore_times;
struct sensor_param tran_lcm_info;
// uint64_t tran_get_lcm_info_timer; //ms

uint64_t tran_als_nv_time = 1;
uint64_t tran_padals_nv_time = 1;
uint64_t tran_ps_nv_time = 1;

#define ALS_NV_TRANSFER_MAX_BYTE 9*4
#define PS_NV_TRANSFER_MAX_BYTE 2*4
#define PS_NV_TRANSFER_TIME 2

#if IS_ENABLED(CONFIG_TRAN_CWB_SUPPORT)
int sensor_location = 0;
#define CWB_POLLING_DELAY 20
#define CWB_EN_RETRY_CNT 50
struct tran_cwb_ctrl {
    int sensor_type;
    int timer_count;
    int qw_flag;
    int statue;
    bool als_on;
};
struct tran_cwb_ctrl cwb_ctrl;
enum Tran_Cwb_En_Lock_Status {
    CWB_EN_UNLOCK = 0,
    CWB_EN_LOCK = 1,
};
enum Tran_Cwb_En_State {
    CWB_EN_FAIL = 0,
    CWB_EN_SUCESS = 1,
};
#endif

struct tran_als_nvcfg {
    uint8_t length;
    uint8_t data[0] __aligned(4);
};
struct tran_ps_nvcfg {
    uint8_t length;
    uint8_t data[0] __aligned(4);
};
struct tran_als_nvcfg *nvcfg = NULL;
struct tran_ps_nvcfg *ps_nvcfg = NULL;

#ifdef CONFIG_TRAN_CHARGER_POLL
#include <linux/power_supply.h>
#define CHARGER_POLLING_DELAY 200
uint32_t tran_charger_val = 0;
uint32_t tran_last_charger_val = 0;
int tran_charger_sensor_type = 0;
//int tran_send_charger_to_hub(void *data, uint8_t length);
#endif

#ifdef CONFIG_AW_ACCDET_PLUG_CAIL
#define ACCDET_NOTIFIER_IN 0x98
#define ACCDET_NOTIFIER_OUT 0x99
#endif

#ifdef CONFIG_TRAN_GET_BRIGHTNESS_LEVEL_SUPPORT
// #define BRIGHTNESS_POLLING_DELAY 25 //
#if IS_ENABLED(CONFIG_TRAN_CWB_SUPPORT)
    uint64_t tran_get_lcm_info_timer = 25;
#endif
#if !IS_ENABLED(CONFIG_TRAN_CWB_SUPPORT)
    uint64_t tran_get_lcm_info_timer = 90;
#endif

#include <linux/leds-mtk.h>

extern int g_tran_brightness_level;

static int g_tran_last_brightness_level = 0;
int g_tran_brightness_sensor_type = 0;
#endif

#ifdef CONFIG_TRAN_GET_VREFRESH_SUPPORT
#include <drm/drm_modes.h>
#define VREFRESH_POLLING_DELAY 20
extern int g_tran_get_vrefresh;
extern int g_tran_get_vrefresh_main;
static int g_tran_last_get_vrefresh = 0;
static int g_tran_last_get_vrefresh_main = 0;
int g_tran_vrefresh_sensor_type = 0;
#endif

#ifdef CONFIG_TRAN_GET_HALL
#define HALL_POLLING_DELAY 90
extern int hall_get_state(void);
static int g_tran_last_hall_level = -1;
static int g_tran_hall_level = -1;
int g_tran_hall_sensor_type = 0;
#endif


#ifdef CONFIG_TRAN_360_ALS_SUPPORT
uint8_t tran_cwb_flip_lightals_enable = false;
uint8_t tran_cwb_flip_subals_enable = false;
#else
uint8_t tran_cwb_flip_lightals_enable = false;
uint8_t tran_cwb_flip_atu_enable = false;
#endif


#ifdef CONFIG_TRAN_TORCH_ST_GET
#include "tran_torch_st_get.h"
static uint32_t tran_torch_last_status[2] = { 0 };
static uint8_t g_tran_orientation_enable = false;
static uint8_t  tran_torch_errflag = true;
#endif

#if defined(CONFIG_SENSOR_GET_SCREEN_POWER_STATE) || defined(CONFIG_SENSOR_GET_LCM_INFO)
enum {
    TRAN_INTERNAL_SCREEN_POWER_ON, //Internal screen power on
    TRAN_EXTERNAL_SCREEN_POWER_ON,//External screen power on
    TRAN_INTERNAL_SCREEN_POWER_OFF,//Internal screen power off
    TRAN_EXTERNAL_SCREEN_POWER_OFF,//External screen power off
};
int tran_inter_display_st = TRAN_INTERNAL_SCREEN_POWER_ON;
int tran_exter_display_st = TRAN_EXTERNAL_SCREEN_POWER_ON;
int tran_display_st = TRAN_INTERNAL_SCREEN_POWER_ON;
#ifdef CONFIG_SENSOR_GET_LCM_INFO
extern char tran_supplier_lcm0[32];
extern char tran_supplier_lcm1[32];
extern char tran_supplier_lcm2[32];
extern int tran_supplier_lcm_num;
#endif
#endif


struct transceiver_config {
	uint8_t length;
	uint8_t data[0] __aligned(4);
};

struct transceiver_state {
	bool enable;
	uint8_t flush;
	struct sensor_comm_batch batch;
	struct transceiver_config *config;
};

struct transceiver_device {
	struct hf_device hf_dev;

	struct timesync_filter filter;

	struct mutex enable_lock;
	struct mutex flush_lock;
	struct mutex config_lock;
	struct transceiver_state state[SENSOR_TYPE_SENSOR_MAX];

	struct sensor_info support_list[SENSOR_TYPE_SENSOR_MAX];
	unsigned int support_size;

	struct share_mem shm_reader;
	struct share_mem_data shm_buffer[8];
	struct share_mem shm_super_reader;
	struct share_mem_super_data shm_super_buffer[4];
	struct wakeup_source *wakeup_src;
	int64_t raw_ts_reverse_debug[SENSOR_TYPE_SENSOR_MAX];
	int64_t comp_ts_reverse_debug[SENSOR_TYPE_SENSOR_MAX];

	atomic_t first_bootup;
	atomic_t normal_wp_dropped;
	atomic_t super_wp_dropped;
	struct task_struct *task;

#ifdef CONFIG_TRAN_CHARGER_POLL
    struct workqueue_struct *charger_wq;
    struct work_struct work_charger;
    struct hrtimer charger_timer;
    ktime_t charger_poll_delay;
#endif

#ifdef CONFIG_TRAN_GET_BRIGHTNESS_LEVEL_SUPPORT
    struct workqueue_struct *brightness_wq;
    struct work_struct work_brightness;
    struct hrtimer brightness_timer;
    ktime_t brightness_poll_delay;
#endif

#if IS_ENABLED(CONFIG_TRAN_CWB_SUPPORT)
    struct workqueue_struct *cwb_wq;
    struct work_struct work_cwb;
    struct hrtimer cwb_timer;
    ktime_t cwb_poll_delay;
#endif

#ifdef CONFIG_TRAN_GET_HALL
    struct workqueue_struct *hall_wq;
    struct work_struct work_hall;
    struct hrtimer hall_timer;
    ktime_t hall_poll_delay;
#endif

#ifdef CONFIG_TRAN_GET_VREFRESH_SUPPORT
    struct workqueue_struct *vrefresh_wq;
    struct work_struct work_vrefresh;
    struct hrtimer vrefresh_timer;
    ktime_t vrefresh_poll_delay;
#endif

#ifdef CONFIG_TRAN_TORCH_ST_GET
    struct notifier_block torch_st_notif;
    struct workqueue_struct *torch_wq;
    struct work_struct work_torch;
#endif

#if defined(CONFIG_SENSOR_GET_SCREEN_POWER_STATE) || defined(CONFIG_SENSOR_GET_LCM_INFO)
    struct notifier_block sensor_disp_notif;
    struct workqueue_struct *disp_wq;
    struct work_struct work_disp;
#endif


#if IS_ENABLED(CONFIG_TRAN_SENSOR_DEBUG)
	struct tran_sensor_debug_device tran_sensor_debug_dev;
#endif


};

static int transceiver_comm_with(int sensor_type, int cmd, void *data, uint8_t length);
static struct transceiver_device transceiver_dev;
static DEFINE_RATELIMIT_STATE(ratelimit, 5 * HZ, 10);
DEFINE_SPINLOCK(transceiver_fifo_lock);
DECLARE_COMPLETION(transceiver_done);
DEFINE_KFIFO(transceiver_fifo, uint32_t, 32);
DEFINE_KFIFO(transceiver_super_fifo, uint32_t, 32);
#ifdef CONFIG_TRAN_GET_HALL
int kernel_hinge;
#endif

static void transceiver_notify_func(struct sensor_comm_notify *n,
		void *private_data)
{
	uint32_t wp = 0;
	struct transceiver_device *dev = private_data;
	struct data_notify *dnotify = (struct data_notify *)n->value;
	uint64_t start_time = 0, end_time = 0, timeout_ns = 5000000;
	uint64_t wait_spin_lock_end_time = 0, timesync_end_time = 0;
	uint64_t kfifo_end_time = 0, complete_end_time = 0;

	if (n->command != SENS_COMM_NOTIFY_DATA_CMD &&
	    n->command != SENS_COMM_NOTIFY_FULL_CMD)
		return;

	start_time = ktime_get_boottime_ns();
	spin_lock(&transceiver_fifo_lock);
	wait_spin_lock_end_time = ktime_get_boottime_ns();
	timesync_filter_set(&dev->filter,
		dnotify->scp_timestamp, dnotify->scp_archcounter);
	timesync_end_time = ktime_get_boottime_ns();
	if (kfifo_is_full(&transceiver_fifo)) {
		if (kfifo_out(&transceiver_fifo, &wp, 1))
			atomic_inc(&dev->normal_wp_dropped);
	}
	wp = dnotify->write_position;
	kfifo_in(&transceiver_fifo, &wp, 1);
	kfifo_end_time = ktime_get_boottime_ns();
	complete(&transceiver_done);
	complete_end_time = ktime_get_boottime_ns();
	spin_unlock(&transceiver_fifo_lock);
	end_time = ktime_get_boottime_ns();
	if (end_time - start_time > timeout_ns && __ratelimit(&ratelimit))
		printk_deferred("time monitor:%llu, %llu, %llu, %llu, %llu, %llu\n",
			start_time, wait_spin_lock_end_time, timesync_end_time,
			kfifo_end_time, complete_end_time, end_time);
}

static void transceiver_super_notify_func(struct sensor_comm_notify *n,
		void *private_data)
{
	uint32_t wp = 0;
	struct transceiver_device *dev = private_data;
	struct data_notify *dnotify = (struct data_notify *)n->value;

	if (n->command != SENS_COMM_NOTIFY_SUPER_DATA_CMD &&
	    n->command != SENS_COMM_NOTIFY_SUPER_FULL_CMD)
		return;

	spin_lock(&transceiver_fifo_lock);
	timesync_filter_set(&dev->filter,
		dnotify->scp_timestamp, dnotify->scp_archcounter);
	if (kfifo_is_full(&transceiver_super_fifo)) {
		if (kfifo_out(&transceiver_super_fifo, &wp, 1))
			atomic_inc(&dev->super_wp_dropped);
	}
	wp = dnotify->write_position;
	kfifo_in(&transceiver_super_fifo, &wp, 1);
	complete(&transceiver_done);
	spin_unlock(&transceiver_fifo_lock);
}

static bool transceiver_wakeup_check(uint8_t action, uint8_t sensor_type)
{
	/*
	 * oneshot proximity tiledetect should wakeup source when data action
	 */
	if (action == DATA_ACTION && (sensor_type == SENSOR_TYPE_PROXIMITY ||
            sensor_type == SENSOR_TYPE_ANTI_TOUCH_PS ||
			sensor_type == SENSOR_TYPE_STEP_DETECTOR ||
			sensor_type == SENSOR_TYPE_SIGNIFICANT_MOTION ||
			sensor_type == SENSOR_TYPE_WAKE_GESTURE ||
			sensor_type == SENSOR_TYPE_GLANCE_GESTURE ||
			sensor_type == SENSOR_TYPE_PICK_UP_GESTURE ||
			sensor_type == SENSOR_TYPE_STATIONARY_DETECT ||
			sensor_type == SENSOR_TYPE_MOTION_DETECT ||
			sensor_type == SENSOR_TYPE_IN_POCKET ||
			sensor_type == SENSOR_TYPE_ANSWER_CALL ||

			sensor_type == SENSOR_TYPE_PICKUP ||

			sensor_type == SENSOR_TYPE_FLAT))
		return true;

	return false;
}

static void transceiver_copy_config(struct transceiver_config *dst,
		struct hf_manager_event *src, uint8_t bias_len,
		uint8_t cali_len, uint8_t temp_len)
{
	if (dst->length < (bias_len + cali_len + temp_len)) {
		pr_err_ratelimited("can't copy config %u %u %u %u %u %u\n",
			src->sensor_type, src->action, dst->length,
			bias_len, cali_len, temp_len);
		return;
	}
	if (bias_len > sizeof(src->word) ||
		cali_len > sizeof(src->word) ||
			temp_len > sizeof(src->word)) {
		pr_err_ratelimited("can't copy config %u %u %u %u %u\n",
			src->sensor_type, src->action,
			bias_len, cali_len, temp_len);
		return;
	}
	if (src->action == BIAS_ACTION)
		memcpy(dst->data, src->word, bias_len);
	else if (src->action == CALI_ACTION)
		memcpy(dst->data + bias_len, src->word, cali_len);
	else if (src->action == TEMP_ACTION)
		memcpy(dst->data + bias_len + cali_len, src->word, temp_len);
}

static void transceiver_update_config(struct transceiver_device *dev,
		struct hf_manager_event *src)
{
	struct transceiver_config *dst = NULL;

	mutex_lock(&dev->config_lock);
	dst = dev->state[src->sensor_type].config;
	if (!dst) {
		mutex_unlock(&dev->config_lock);
		return;
	}
	switch (src->sensor_type) {
	case SENSOR_TYPE_ACCELEROMETER:
		transceiver_copy_config(dst, src, 12, 12, 0);
		break;
	case SENSOR_TYPE_MAGNETIC_FIELD:
		transceiver_copy_config(dst, src, 12, 24, 0);
		break;
	case SENSOR_TYPE_GYROSCOPE:
		transceiver_copy_config(dst, src, 12, 12, 24);
		break;
	default:
		/*
		 * NOTE: default branch only handle CALI_ACTION.
		 * if you add new sensor type that only cali need store, you
		 * can use default branch.
		 * for example:
		 * SENSOR_TYPE_LIGHT,SENSOR_TYPE_PRESSURE,
		 * SENSOR_TYPE_PROXIMITY, SENSOR_TYPE_SAR, SENSOR_TYPE_OIS
		 * and so on can use this branch.
		 */
		if (src->action == CALI_ACTION &&
				dst->length <= sizeof(src->word))
			transceiver_copy_config(dst, src, 0, dst->length, 0);
		else
			pr_err_ratelimited("can't update config %u %u %u\n",
				src->sensor_type, src->action, dst->length);
		break;
	}
	mutex_unlock(&dev->config_lock);
}


#ifdef CONFIG_TRAN_SAR_DEBUG
extern uint32_t *awinic_get_global_val(void);
#endif

#ifdef CONFIG_AW_ACCDET_PLUG_CAIL
static RAW_NOTIFIER_HEAD(accdet_notifier_chain);

/* define our own notifier_call_chain */
int call_accdet_notifiers(unsigned long val, void *v)
{
    return raw_notifier_call_chain(&accdet_notifier_chain, val, v);
}
EXPORT_SYMBOL(call_accdet_notifiers);

/* define our own notifier_chain_register func */
int register_accdet_notifier(struct notifier_block *nb)
{
    int err;
    err = raw_notifier_chain_register(&accdet_notifier_chain, nb);
    if(err)
        goto out;
out:
    return err;
}
EXPORT_SYMBOL(register_accdet_notifier);

static int aw_sar_accdet_inout_event(struct notifier_block *nb, unsigned long event, void *v)
{
    switch(event){
    case ACCDET_NOTIFIER_IN:
        printk("accdet_notifier event in");
        transceiver_comm_with(SENSOR_TYPE_SAR, SENS_COMM_CTRL_CALI_CMD, NULL, 0);
        break;

    case ACCDET_NOTIFIER_OUT:
        printk("accdet_notifier event out");
        transceiver_comm_with(SENSOR_TYPE_SAR, SENS_COMM_CTRL_CALI_CMD, NULL, 0);
        break;

    default:
        break;
    }
    return 0;
}

static struct notifier_block accdet_init_notifier = {
    .notifier_call = aw_sar_accdet_inout_event,
};
#endif


static void transceiver_report(struct transceiver_device *dev,
		struct hf_manager_event *event)
{
	int ret = 0;
	bool need_wakeup = false;
	uint8_t sensor_type = 0, action = 0;
	struct hf_manager *manager = dev->hf_dev.manager;
	struct transceiver_state *state = NULL;

	if (!manager)
		return;

	action = event->action;
	sensor_type = event->sensor_type;
	state = &dev->state[sensor_type];
	need_wakeup = transceiver_wakeup_check(action, sensor_type);

	if (action == BIAS_ACTION || action == CALI_ACTION ||
			action == TEMP_ACTION)
		transceiver_update_config(dev, event);

	do {
		if (action != FLUSH_ACTION) {
			if (need_wakeup)
				__pm_wakeup_event(dev->wakeup_src, 250);
			ret = manager->report(manager, event);
		} else {
			/*
			 * NOTE: only for flush ret = 0 we decrease
			 * ret must reset to 0 for each loop
			 */
			ret = 0;
			mutex_lock(&dev->flush_lock);
			if (state->flush > 0) {
				ret = manager->report(manager, event);
				if (!ret)
					state->flush--;
			}
			mutex_unlock(&dev->flush_lock);
		}
		if (ret < 0)
			usleep_range(2000, 4000);
	} while (ret < 0);
}

#ifdef CONFIG_TRAN_GET_HALL
int kernel_get_hinge(void)
{
    pr_err("kernel_get_hinge: %d \n", kernel_hinge);
    return kernel_hinge;
}
EXPORT_SYMBOL_GPL(kernel_get_hinge);
#endif

static int transceiver_translate(struct transceiver_device *dev,
		struct hf_manager_event *dst,
		const struct share_mem_data *src)
{
	int64_t remap_timestamp = 0;

	if (src->sensor_type >= SENSOR_TYPE_SENSOR_MAX ||
			src->action >= MAX_ACTION) {
		pr_err_ratelimited("invalid sensor event %u %u\n",
			src->sensor_type, src->action);
		return -EINVAL;
	}

	remap_timestamp = src->timestamp + timesync_filter_get(&dev->filter);
	if (src->action == DATA_ACTION) {
		dst->timestamp = remap_timestamp;
		dst->sensor_type = src->sensor_type;
		dst->accurancy = src->accurancy;
		dst->action = src->action;
		switch (src->sensor_type) {
		case SENSOR_TYPE_ACCELEROMETER:
		case SENSOR_TYPE_MAGNETIC_FIELD:
		case SENSOR_TYPE_GYROSCOPE:

        case SENSOR_TYPE_LINEAR_HALL:
        case SENSOR_TYPE_SUBACCEL:
        case SENSOR_TYPE_SUBGYRO:

			dst->word[0] = src->value[0];
			dst->word[1] = src->value[1];
			dst->word[2] = src->value[2];
			break;
		case SENSOR_TYPE_ORIENTATION:
		case SENSOR_TYPE_ROTATION_VECTOR:
		case SENSOR_TYPE_GAME_ROTATION_VECTOR:
		case SENSOR_TYPE_GEOMAGNETIC_ROTATION_VECTOR:
			dst->word[0] = src->value[0];
			dst->word[1] = src->value[1];
			dst->word[2] = src->value[2];
			dst->word[3] = src->value[3];
			break;
		case SENSOR_TYPE_LINEAR_ACCELERATION:
		case SENSOR_TYPE_GRAVITY:
			dst->word[0] = src->value[0];
			dst->word[1] = src->value[1];
			dst->word[2] = src->value[2];
			break;
		case SENSOR_TYPE_ACCELEROMETER_UNCALIBRATED:
		case SENSOR_TYPE_MAGNETIC_FIELD_UNCALIBRATED:
		case SENSOR_TYPE_GYROSCOPE_UNCALIBRATED:
			dst->word[0] = src->value[0];
			dst->word[1] = src->value[1];
			dst->word[2] = src->value[2];
			dst->word[3] = src->value[3];
			dst->word[4] = src->value[4];
			dst->word[5] = src->value[5];
			break;
		case SENSOR_TYPE_GYRO_SECONDARY:
			dst->word[0] = src->value[0];
			dst->word[1] = src->value[1];
			dst->word[2] = src->value[2];
			break;

        case SENSOR_TYPE_HEAD_POSTURE:
            dst->word[0] = src->value[0];
            dst->word[1] = src->value[1];
            dst->word[2] = src->value[2];
            break;

        case SENSOR_TYPE_LIGHT:

        case SENSOR_TYPE_SUBALS:
        case SENSOR_TYPE_PADALS:
            dst->word[0] = src->value[0];
            dst->word[1] = src->value[1];
            dst->word[2] = src->value[2];
            dst->word[3] = src->value[3];
            dst->word[4] = src->value[4];
//          pr_info("[scp_to_kernel][light] %f, %f, %f, %f, %f.\n", dst->word[0], dst->word[1], dst->word[2], dst->word[3], dst->word[4]);
            break;
        case SENSOR_TYPE_PRESSURE:
        case SENSOR_TYPE_PROXIMITY:
        case SENSOR_TYPE_ANTI_TOUCH_PS:
        case SENSOR_TYPE_STEP_COUNTER:
#ifdef CONFIG_TRAN_GET_HALL
            dst->word[0] = src->value[0];
            break;
#endif
        case SENSOR_TYPE_HINGE:
            dst->word[0] = src->value[0];
#ifdef CONFIG_TRAN_GET_HALL
            kernel_hinge = src->value[0];
#endif
            break;
            //pr_err("kernel_hinge = %ld \n", dst->word[0]);
        case SENSOR_TYPE_MOTION_SENSING_CONTROL:
        case SENSOR_TYPE_ANTI_TOUCH:
        case SENSOR_TYPE_TENT:

        case SENSOR_TYPE_POSTURE:


        case SENSOR_TYPE_ANTI_TOUCH_ULTRASOUND:

            dst->word[0] = src->value[0];
            break;
		default:
#ifdef CONFIG_TRAN_SAR_DEBUG
            if (src->sensor_type == SENSOR_TYPE_SAR) {
                if (src->value[0] == 0xff) {
                    uint32_t *awinic_debug_data = awinic_get_global_val();
                    awinic_debug_data[0] = src->value[0];
                    awinic_debug_data[1] = src->value[1];
                    awinic_debug_data[2] = src->value[2];
                }
                pr_info("sar value[0]:%x,value[1]:%x,value[2]:%x",
                    src->value[0], src->value[1], src->value[2]);
            }
#endif
/*
        case SENSOR_TYPE_ALSFLICKER:
            dst->word[0] = src->value[0];
            dst->word[1] = src->value[1];
            dst->word[2] = src->value[2];
            dst->word[3] = src->value[3];
            dst->word[4] = src->value[4];
            dst->word[5] = src->value[5];
            dst->word[6] = src->value[6];
            dst->word[7] = src->value[7];
    
            //pr_notice("%s:alsflicker:%d,%d,%d,%d,%d,%d,%d,%d.\n",__func__, event.word[0],event.word[1],event.word[2],event.word[3],event.word[4],event.word[5],event.word[6],event.word[7]);
            break;*/
			memcpy(dst->word, src->value,
				min(sizeof(dst->word), sizeof(src->value)));
			break;
		}

	} else if (src->action == FLUSH_ACTION) {
		dst->timestamp = remap_timestamp;
		dst->sensor_type = src->sensor_type;
		dst->action = src->action;

	} else {
		/*
		 * BIAS_ACTION, CALI_ACTION, TEMP_ACTION,
		 * TEST_ACTION and RAW_ACTION
		 */
#ifdef CONFIG_TRAN_SAR_DEBUG
        if (src->sensor_type == SENSOR_TYPE_SAR) {
            uint32_t *awinic_debug_data = awinic_get_global_val();
            awinic_debug_data[0] = src->value[0];
            awinic_debug_data[1] = src->value[1];
            awinic_debug_data[2] = src->value[2];
            pr_info("sar RAW_ACTION value[0]:%x,value[1]:%x,value[2]:%x",
                src->value[0], src->value[1], src->value[2]);
        }
#endif
		dst->timestamp = remap_timestamp;
		dst->sensor_type = src->sensor_type;
		dst->accurancy = src->accurancy;
		dst->action = src->action;
		memcpy(dst->word, src->value,
			min(sizeof(dst->word), sizeof(src->value)));
	}
	return 0;
}

static void transceiver_read(struct transceiver_device *dev,
		uint32_t write_position)
{
	int ret = 0;
	int i = 0;
	struct share_mem *shm = &dev->shm_reader;
	struct share_mem_data *buffer = dev->shm_buffer;
	uint32_t size = sizeof(dev->shm_buffer);
	uint32_t item_size = sizeof(dev->shm_buffer[0]);
	struct hf_manager_event evt;

	ret = share_mem_seek(shm, write_position);
	if (ret < 0) {
		pr_err("%s seek fail %d\n", shm->name, ret);
		return;
	}

	while (1) {
		ret = share_mem_read(shm, buffer, size);
		if (ret < 0 || ret > size) {
			pr_err("%s read fail %d\n", shm->name, ret);
			break;
		}
		if (ret == 0)
			break;
		if (ret % item_size) {
			pr_err("%s times greater fail %d\n", shm->name, ret);
			break;
		}
		for (i = 0; i < (ret / item_size); i++) {
			if (transceiver_translate(dev, &evt, &buffer[i]) < 0)
				continue;
			transceiver_report(dev, &evt);
		}
	}
}

static void transceiver_process(struct transceiver_device *dev)
{
	unsigned int ret = 0;
	uint32_t wp = 0;
	unsigned long flags = 0;

	while (1) {
		spin_lock_irqsave(&transceiver_fifo_lock, flags);
		ret = kfifo_out(&transceiver_fifo, &wp, 1);
		spin_unlock_irqrestore(&transceiver_fifo_lock, flags);
		if (!ret)
			break;
		transceiver_read(dev, wp);
	}
}

static int transceiver_translate_super(struct transceiver_device *dev,
		struct hf_manager_event *dst,
		const struct share_mem_super_data *src)
{
	if (src->sensor_type >= SENSOR_TYPE_SENSOR_MAX ||
			src->action >= MAX_ACTION) {
		pr_err_ratelimited("invalid sensor event %u %u\n",
			src->sensor_type, src->action);
		return -EINVAL;
	}

	dst->timestamp = src->timestamp + timesync_filter_get(&dev->filter);
	dst->sensor_type = src->sensor_type;
	dst->accurancy = src->accurancy;
	dst->action = src->action;
	memcpy(dst->word, src->value,
		min(sizeof(dst->word), sizeof(src->value)));
	return 0;
}

static void transceiver_read_super(struct transceiver_device *dev,
		uint32_t write_position)
{
	int ret = 0;
	int i = 0;
	struct share_mem *shm = &dev->shm_super_reader;
	struct share_mem_super_data *buffer = dev->shm_super_buffer;
	uint32_t size = sizeof(dev->shm_super_buffer);
	uint32_t item_size = sizeof(dev->shm_super_buffer[0]);
	struct hf_manager_event evt;

	ret = share_mem_seek(shm, write_position);
	if (ret < 0) {
		pr_err("%s seek fail %d\n", shm->name, ret);
		return;
	}

	while (1) {
		ret = share_mem_read(shm, buffer, size);
		if (ret < 0 || ret > size) {
			pr_err("%s read fail %d\n", shm->name, ret);
			break;
		}
		if (ret == 0)
			break;
		if (ret % item_size) {
			pr_err("%s times greater fail %d\n", shm->name, ret);
			break;
		}
		for (i = 0; i < (ret / item_size); i++) {
			if (transceiver_translate_super(dev,
					&evt, &buffer[i]) < 0)
				continue;
			transceiver_report(dev, &evt);
		}
	}
}

#ifdef CONFIG_TRAN_CHARGER_POLL
static int tran_update_charger_state(void)
{
    int ret = 0;
    struct power_supply *psy = NULL;
    union power_supply_propval val;
    int tran_battery_current = 0;
    int tran_battery_status = 0;
    //int tran_ac_online = 0;
    //int tran_usb_online = 0;

    /*get charger information start*/
    psy = power_supply_get_by_name("battery");
    if (!psy) {
        pr_err("%s: get battery supply failed!\n", __func__);
        return -1;
    }else{
        ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_CURRENT_NOW, &val); //get charger current /sys/class/power_supply/battery/current_now
        if (ret){
            pr_err("Cannot get current!\n");
            return -1;
        }
        tran_battery_current = val.intval;

        ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_STATUS, &val); //get battery status /sys/class/power_supply/battery/status
        if (ret){
            pr_err("Cannot get battery status!\n");
            return -1;
        }
        tran_battery_status = val.intval;
    }

    pr_err("[setchg] %s tran_current: %d, tran_status: %d\n", __func__,
            tran_battery_current, tran_battery_status);

    tran_charger_val = tran_battery_current;
    return 0;
}
#endif

static void transceiver_process_super(struct transceiver_device *dev)
{
	unsigned int ret = 0;
	uint32_t wp = 0;
	unsigned long flags = 0;

	while (1) {
		spin_lock_irqsave(&transceiver_fifo_lock, flags);
		ret = kfifo_out(&transceiver_super_fifo, &wp, 1);
		spin_unlock_irqrestore(&transceiver_fifo_lock, flags);
		if (!ret)
			break;
		transceiver_read_super(dev, wp);
	}
}

static int transceiver_thread(void *data)
{
	int ret = 0;
	struct transceiver_device *dev = data;
	int32_t normal_wp_dropped = 0, super_wp_dropped = 0;

	while (!kthread_should_stop()) {
		ret = wait_for_completion_interruptible(&transceiver_done);
		if (ret)
			continue;

		normal_wp_dropped = atomic_xchg(&dev->normal_wp_dropped, 0);
		super_wp_dropped = atomic_xchg(&dev->super_wp_dropped, 0);
		if (unlikely(normal_wp_dropped))
			pr_err_ratelimited("drop normal write position:%u\n",
				normal_wp_dropped);
		if (unlikely(super_wp_dropped))
			pr_err_ratelimited("drop super write position:%u\n",
				super_wp_dropped);
		transceiver_process(dev);
		transceiver_process_super(dev);
	}

	return 0;
}

static int transceiver_comm_with(int sensor_type, int cmd,
		void *data, uint8_t length)
{
	int ret = 0;
	struct sensor_comm_ctrl *ctrl = NULL;
	uint32_t ctrl_size = 0;

	ctrl_size = ipi_comm_size(sizeof(*ctrl) + length);
	ctrl = kzalloc(ctrl_size, GFP_KERNEL);
	ctrl->sensor_type = sensor_type;
	ctrl->command = cmd;
	ctrl->length = length;
	if (length)
		memcpy(ctrl->data, data, length);
	ret = sensor_comm_ctrl_send(ctrl, ctrl_size);
	kfree(ctrl);
	return ret;
}


#ifdef CONFIG_TRAN_GET_BRIGHTNESS_LEVEL_SUPPORT
int als_set_brightness(void *data, uint32_t count)
{
    int tran_sensor_type = 0;
#if !IS_ENABLED(CONFIG_TRAN_CWB_SUPPORT)
    tran_sensor_type = SENSOR_TYPE_LIGHT;
#endif
#if IS_ENABLED(CONFIG_TRAN_CWB_SUPPORT)
    tran_sensor_type = cwb_ctrl.sensor_type;
#endif
    return transceiver_comm_with(tran_sensor_type, SENS_COMM_CTRL_DEBUG_CMD, (uint32_t *)data, sizeof(count));
}
EXPORT_SYMBOL_GPL(als_set_brightness);
#endif

#ifdef CONFIG_TRAN_GET_VREFRESH_SUPPORT
int als_get_vrefresh(void *data, uint32_t count)
{
    return transceiver_comm_with(SENSOR_TYPE_LIGHT, SENS_COMM_CTRL_DEBUG_CMD, (uint32_t *)data, sizeof(count));
}
EXPORT_SYMBOL_GPL(als_get_vrefresh);
#endif


#ifdef CONFIG_TRAN_CHARGER_POLL
static enum hrtimer_restart charger_timer_func(struct hrtimer *timer)
{
    struct transceiver_device *device = &transceiver_dev;

    queue_work(device->charger_wq, &device->work_charger);
    hrtimer_forward_now(&device->charger_timer, device->charger_poll_delay);

    return HRTIMER_RESTART;
}
static void charger_work_func(struct work_struct *work)
{
    if (tran_update_charger_state()) {
        pr_err("[setchg] %s: update charger state failed!\n", __func__);
        return;
    }
    pr_err("[setchg] %s: start tran_charger_val = %d\n", __func__, tran_charger_val);

    if(tran_last_charger_val != tran_charger_val)
    {
        tran_last_charger_val = tran_charger_val;
        transceiver_comm_with(SENSOR_TYPE_MAGNETIC_FIELD, SENS_COMM_CTRL_DEBUG_CMD, &tran_last_charger_val, sizeof(tran_last_charger_val));
        pr_err("[setchg] %s: tran_send_charger_to_hub done! tran_charger_val = %d\n", __func__, tran_charger_val);
    }
}
#endif


#if IS_ENABLED(CONFIG_TRAN_CWB_SUPPORT)
static enum hrtimer_restart cwb_timer_func(struct hrtimer *timer)
{
    struct transceiver_device *device = &transceiver_dev;
    int retry_cnt = CWB_EN_RETRY_CNT;

    if((cwb_ctrl.statue == CWB_EN_FAIL) && (cwb_ctrl.timer_count < retry_cnt) && (cwb_ctrl.qw_flag == CWB_EN_UNLOCK)) {
        queue_work(device->cwb_wq, &device->work_cwb);
        pr_err("%s [tran_sensor]retry queue_work start for cwb enable, retry count:%d, cwb_ctrl.statue = %d, cwb_ctrl.qw_flag = %d\n", __func__, cwb_ctrl.timer_count, cwb_ctrl.statue, cwb_ctrl.qw_flag);
    }

    if (cwb_ctrl.timer_count < retry_cnt) {
        cwb_ctrl.timer_count++;
    }

    if((cwb_ctrl.statue == CWB_EN_SUCESS) && (cwb_ctrl.timer_count < retry_cnt) && (cwb_ctrl.qw_flag == CWB_EN_UNLOCK)) {
        pr_err("%s [tran_sensor] The cwb has been enabled\n", __func__);
        return HRTIMER_NORESTART;
    }

    if(cwb_ctrl.timer_count == retry_cnt) {
        pr_err("%s [tran_sensor]retry queue_work end cwb_ctrl.timer_count >= %d, cwb_ctrl.statue = %d, cwb_ctrl.qw_flag = %d\n", __func__, retry_cnt, cwb_ctrl.statue, cwb_ctrl.qw_flag);
        return HRTIMER_NORESTART;
    }

    hrtimer_forward_now(&device->cwb_timer, device->cwb_poll_delay);

    return HRTIMER_RESTART;
}

static void cwb_work_func(struct work_struct *work)
{
    int ret = 0;
    if (cwb_ctrl.als_on) {
        cwb_ctrl.qw_flag = CWB_EN_LOCK;
        pr_err("%s [tran_sensor]cwb enable start, cwb_ctrl.qw_flag = %d\n",__func__, cwb_ctrl.qw_flag);
        ret = tran_sensor_cwb_enable(cwb_ctrl.sensor_type, true);
        cwb_ctrl.qw_flag = CWB_EN_UNLOCK;
        if (ret < 0) {
            cwb_ctrl.statue = CWB_EN_FAIL;
            pr_err("%s [tran_sensor]cwb enable end [fail], cwb_ctrl.statue = %d, cwb_ctrl.qw_flag = %d\n",__func__, cwb_ctrl.statue, cwb_ctrl.qw_flag);
        } else {
            cwb_ctrl.statue = CWB_EN_SUCESS;
            pr_err("%s [tran_sensor]cwb enable end [sucess], cwb_ctrl.statue = %d, cwb_ctrl.qw_flag = %d\n",__func__, cwb_ctrl.statue, cwb_ctrl.qw_flag);
        }
    } else {
        pr_err("%s [tran_sensor]cwb enable Junmp, cwb_ctrl.statue = %d, cwb_ctrl.qw_flag = %d, als_on = %d\n",__func__, cwb_ctrl.statue, cwb_ctrl.qw_flag, cwb_ctrl.als_on);
    }
}
#endif

#ifdef CONFIG_TRAN_GET_BRIGHTNESS_LEVEL_SUPPORT
static enum hrtimer_restart brightness_timer_func(struct hrtimer *timer)
{
    struct transceiver_device *device = &transceiver_dev;

    queue_work(device->brightness_wq, &device->work_brightness);
    hrtimer_forward_now(&device->brightness_timer, device->brightness_poll_delay);

    return HRTIMER_RESTART;
}

static void brightness_work_func(struct work_struct *work)
{
    int ret = 0;
#if IS_ENABLED(CONFIG_TRAN_CWB_SUPPORT)
    uint8_t i = 0;
#endif
    if (g_tran_debug_mode == 2) {
        printk("[get_brightness] %s, g_tran_brightness_level= %d.\n ",__func__, g_tran_brightness_level);
    }
    if(g_tran_last_brightness_level != g_tran_brightness_level)
    {
        g_tran_last_brightness_level = g_tran_brightness_level;
        ret = als_set_brightness(&g_tran_last_brightness_level, sizeof(g_tran_last_brightness_level));
//        printk("[get_brightness] %s: als_set_brightness done! g_tran_brightness_level = %d\n", __func__, g_tran_brightness_level);
    }
    //printk("[tran_sensor] [will do tran_sensor_cwb_rgb_get]%s\n ",__func__);
#if IS_ENABLED(CONFIG_TRAN_CWB_SUPPORT)
    if ((tran_sensor_un_als_support)&&(cwb_ctrl.statue == CWB_EN_SUCESS)) {
        memset(&tran_lcm_info, 0, sizeof(struct sensor_param));
        tran_sensor_cwb_rgb_get();
        for (i = 0; i < TRAN_UNDER_SENSOR_MAX; i++) {
            if (cust_sensor_info[i].support_state && (cust_sensor_info[i].cwb_state == CWB_INFO_UPDATE)) {
                tran_lcm_info.cust_param[0] = als_cwb_info[i].r_avg;
                tran_lcm_info.cust_param[1] = als_cwb_info[i].g_avg;
                tran_lcm_info.cust_param[2] = als_cwb_info[i].b_avg;
                tran_lcm_info.cust_param[3] = cwb_ctrl.sensor_type;
                transceiver_comm_with(cwb_ctrl.sensor_type, SENS_COMM_CTRL_CUST_PARAM_CMD, &tran_lcm_info, sizeof(struct sensor_param));
                cust_sensor_info[i].cwb_state = CWB_INFO_NOT_UPDATE;
            }
        }
    }
#endif

}
#endif

#ifdef CONFIG_TRAN_GET_VREFRESH_SUPPORT
static enum hrtimer_restart vrefresh_timer_func(struct hrtimer *timer)
{
    struct transceiver_device *device = &transceiver_dev;

    queue_work(device->vrefresh_wq, &device->work_vrefresh);
    hrtimer_forward_now(&device->vrefresh_timer, device->vrefresh_poll_delay);

    return HRTIMER_RESTART;
}

static void vrefresh_work_func(struct work_struct *work)
{
    int ret = 0;
    if( g_tran_debug_mode == 3) {
        printk("[vrefresh] %s, g_tran_get_vrefresh= %d, g_tran_get_vrefresh_main = %d.\n ",__func__, g_tran_get_vrefresh, g_tran_get_vrefresh_main);
    }

    if ((g_tran_get_vrefresh != 0) || (g_tran_get_vrefresh_main != 0)) {
        if((g_tran_last_get_vrefresh != -g_tran_get_vrefresh)) {
            g_tran_last_get_vrefresh = -g_tran_get_vrefresh;
            if( g_tran_debug_mode == 3) {
                printk("[vrefresh] %s: als_get_vrefresh done! g_tran_get_vrefresh = %d\n", __func__, g_tran_get_vrefresh);
            }
            if(g_tran_last_get_vrefresh != 0){
                ret = als_get_vrefresh(&g_tran_last_get_vrefresh, sizeof(g_tran_last_get_vrefresh));
            }
            g_tran_get_vrefresh = 0;
        } else if (g_tran_last_get_vrefresh_main != -g_tran_get_vrefresh_main){
            g_tran_last_get_vrefresh_main = -g_tran_get_vrefresh_main;
            if( g_tran_debug_mode == 3) {
                printk("[vrefresh] %s: als_get_main_vrefresh done! g_tran_get_vrefresh_main = %d\n", __func__, g_tran_get_vrefresh_main);
            }
            if(g_tran_last_get_vrefresh_main != 0){
                ret = als_get_vrefresh(&g_tran_last_get_vrefresh_main, sizeof(g_tran_last_get_vrefresh_main));
            }
            g_tran_get_vrefresh_main = 0;
        }
    }
}
#endif

#ifdef CONFIG_TRAN_GET_HALL
static enum hrtimer_restart hall_timer_func(struct hrtimer *timer)
{
    struct transceiver_device *device = &transceiver_dev;

    queue_work(device->hall_wq, &device->work_hall);
    hrtimer_forward_now(&device->hall_timer, device->hall_poll_delay);

    return HRTIMER_RESTART;
}

static void hall_work_func(struct work_struct *work)
{
    int ret = 0;
    g_tran_last_hall_level = hall_get_state();
//    printk("[hinge] %s, g_tran_last_hall_level= %d.\n ",__func__, g_tran_last_hall_level);
    ret = transceiver_comm_with(SENSOR_TYPE_LINEAR_HALL, SENS_COMM_CTRL_DEBUG_CMD, &g_tran_last_hall_level, sizeof(g_tran_last_hall_level));
    if(g_tran_last_hall_level == 0) {
        if(g_tran_last_hall_level != g_tran_hall_level) {
            g_tran_hall_level = -1;
            ret = transceiver_comm_with(SENSOR_TYPE_LIGHT, SENS_COMM_CTRL_DEBUG_CMD, &g_tran_hall_level, sizeof(g_tran_hall_level));
            g_tran_hall_level = 0;
        }
    } else if (g_tran_last_hall_level == 1) {
        if (g_tran_last_hall_level != g_tran_hall_level) {
            g_tran_hall_level = -5;
            ret = transceiver_comm_with(SENSOR_TYPE_LIGHT, SENS_COMM_CTRL_DEBUG_CMD, &g_tran_hall_level, sizeof(g_tran_hall_level));
            g_tran_hall_level = 1;
        }
    }
}
#endif


#ifdef CONFIG_TRAN_TORCH_ST_GET
static int tran_torch_st_notifier_callback(struct notifier_block* nb, unsigned long value, void* v)
{
    int* data = v;
    struct transceiver_device *device = &transceiver_dev;

    if(data)
    {
        switch (value)
        {
        case TRAN_BACK_TORCH_SENSOR:
            if ((*data != tran_torch_last_status[0]) && (tran_torch_errflag == true))
            {
                tran_torch_last_status[0] = *data;
                queue_work(device->torch_wq, &device->work_torch);
            }
            break;
        case TRAN_FRONT_TORCH_SENSOR:
            if ((*data != tran_torch_last_status[1]) && (tran_torch_errflag == true))
            {
                tran_torch_last_status[1] = *data;
                queue_work(device->torch_wq, &device->work_torch);
            }
            break;
        default:
            pr_err("%s Unsupported Torch %d\n", __func__, value);
            break;
        }
    }
    pr_info("tran_torch_st_notifier_callback, value = %d, *data = %d\n", value, *data);
    return 0;
}
static void tran_torch_work_func(struct work_struct* work)
{
    int ret = 0;
    pr_info("%s %d %d\n", __func__, tran_torch_last_status[0], tran_torch_last_status[1]);
    if (g_tran_orientation_enable == true)
    {
        ret = transceiver_comm_with(SENSOR_TYPE_ORIENTATION, SENS_COMM_CTRL_TORCH_STATUS_CMD, (uint32_t*)tran_torch_last_status, sizeof(tran_torch_last_status));
        if (ret < 0)
            pr_err("tran_torch_work_func fail,(ID:%d)\n", SENSOR_TYPE_ORIENTATION);
    }
    return;
}
#endif



#ifdef CONFIG_SENSOR_GET_LCM_INFO
static int get_bootargs(char *current_mode, char *boot_param)
{
    struct device_node *np;
    const char *cmd_line;
    char *s = NULL;
    int ret = 0;
    int i = 0;

    if(current_mode == NULL)
        return -1;

    np = of_find_node_by_path("/chosen");
    if (!np) {
        pr_err("Can't get the /chosen");
        return -EIO;
    }
    if (!strcmp(boot_param, "lcm_name=")) {
        ret = of_property_read_string(np, "touch,lcm_name", &cmd_line);
        if (ret < 0) {
            pr_err("Can't get the touch,ret=%d", ret);
        } else {
            strcpy(current_mode, cmd_line);
            pr_err("get lcm_name: %s  len:%lu", cmd_line, strlen(cmd_line));
            return 0;
        }
    }
    ret = of_property_read_string(np, "bootargs", &cmd_line);
    if (ret < 0) {
        pr_err("Can't get the bootargs");
        return ret;
    }

    s = strstr(cmd_line, boot_param);
    if(s != NULL) {
        s += strlen(boot_param);
        while(*s != ' ' && i < 64) {
            *current_mode++ = *s++;
            i++;
        }
        *current_mode = '\0';
    } else {
        return -1;
    }

    return 0;
}

static int mtk_find_lcm_name(char * p_name)
{
    if(p_name != NULL)
        get_bootargs(p_name,"lcm_name=");

    return 1;
}

static int tran_sensor_lcm_info_comparison(void)
{
    char lcm_name[64]= {0};
    /*get current lcm name*/
    mtk_find_lcm_name(lcm_name);
    pr_info("tran_disp: get_lcm_name %s",lcm_name);

    /*Screen information comparison*/
    if(strcmp(lcm_name,tran_supplier_lcm0) == 0) {
        return TRAN_SUPPLIER_LCM_0;
    } else if(strcmp(lcm_name,tran_supplier_lcm1) == 0) {
        return TRAN_SUPPLIER_LCM_1;
    } else if(strcmp(lcm_name,tran_supplier_lcm2) == 0) {
        return TRAN_SUPPLIER_LCM_2;
    } else {
        return TRAN_SUPPLIER_LCM_MAX;
    }
}

static void tran_sensor_send_lcm_supplier_info(void)
{
    struct tran_sensor_cust_param tran_lcm_info;
    memset(&tran_lcm_info, 0, sizeof(struct tran_sensor_cust_param));

    /* init tran_lcm_info */
    tran_lcm_info.data_type = TRAN_LCM_SUPPLIER_INFO;
    tran_lcm_info.data[0] = TRAN_SUPPLIER_LCM_MAX;

    /* get lcm info & comparison*/
    tran_lcm_info.data[0] = tran_sensor_lcm_info_comparison();
    pr_info("tran_disp: lcm_supplier_info:%d",tran_lcm_info.data[0]);

    /* send lcm info */
    if (tran_lcm_info.data[0] != TRAN_SUPPLIER_LCM_MAX) {
        transceiver_comm_with(SENSOR_TYPE_LIGHT, SENS_COMM_CTRL_ONCHANGE_CMD, &tran_lcm_info, sizeof(struct tran_sensor_cust_param));
    } else {
        pr_err("tran_disp: lcm_supplier_info is max, check dts file");
    }
}
#endif

#if defined(CONFIG_SENSOR_GET_SCREEN_POWER_STATE) || defined(CONFIG_SENSOR_GET_LCM_INFO)
void tran_disp_work_func(struct work_struct* work)
{
    pr_info("[tran_disp:%s]get the data form Internal display tran_inter_display_st = %d , tran_display_st=%d", __func__, tran_inter_display_st , tran_display_st);
#ifdef CONFIG_SENSOR_GET_SCREEN_POWER_STATE
    struct transceiver_device *device = &transceiver_dev;

    transceiver_comm_with(SENSOR_TYPE_LIGHT, SENS_COMM_CTRL_ONCHANGE_CMD,
        &tran_inter_display_st, sizeof(tran_inter_display_st));
#endif
    if (tran_inter_display_st == TRAN_INTERNAL_SCREEN_POWER_ON) {
#ifdef CONFIG_SENSOR_GET_LCM_INFO
        tran_sensor_send_lcm_supplier_info();
#endif
#ifdef CONFIG_SENSOR_GET_SCREEN_POWER_STATE
        hrtimer_start(&device->vrefresh_timer, ns_to_ktime(VREFRESH_POLLING_DELAY * NSEC_PER_MSEC), HRTIMER_MODE_REL);
        pr_err("[get_vrefresh] %s is hrtimer_start poll!!!\n", __func__);
#endif
    } else if(tran_inter_display_st == TRAN_INTERNAL_SCREEN_POWER_OFF) {
#ifdef CONFIG_SENSOR_GET_SCREEN_POWER_STATE
        hrtimer_cancel(&device->vrefresh_timer);
        pr_err("[get_vrefresh] %s is hrtimer_end poll!!!\n", __func__);
        cancel_work_sync(&device->work_vrefresh);
        g_tran_get_vrefresh = 0;
#endif
    }
}

static void tran_a_g_cvt_map(void)
{
    if ((tran_inter_display_st == TRAN_INTERNAL_SCREEN_POWER_ON) && (tran_exter_display_st == TRAN_EXTERNAL_SCREEN_POWER_OFF)) {
        tran_display_st = TRAN_INTERNAL_SCREEN_POWER_ON;
    } else if ((tran_inter_display_st == TRAN_INTERNAL_SCREEN_POWER_OFF) && (tran_exter_display_st == TRAN_EXTERNAL_SCREEN_POWER_ON)){
        tran_display_st = TRAN_EXTERNAL_SCREEN_POWER_ON;
    } else if ((tran_inter_display_st == TRAN_INTERNAL_SCREEN_POWER_ON) && (tran_exter_display_st == TRAN_EXTERNAL_SCREEN_POWER_ON)){
        tran_display_st = TRAN_INTERNAL_SCREEN_POWER_ON;
    }
    pr_err("[tran_disp:%s]get the data tran_display_st = %d", __func__, tran_display_st);
}

static int sensor_disp_notifier_callback(struct notifier_block *nb,
    unsigned long value, void *v)
{
    struct transceiver_device *dev = &transceiver_dev;
    int *data = (int *)v;
    if (v) {
        /*dsi on off power MTK_DISP_EVENT_BLANK: 0x01*/
        if (value == MTK_DISP_EVENT_BLANK) {
            if (*data == MTK_DISP_BLANK_UNBLANK) {
                /*Internal display on MTK_DISP_BLANK_UNBLANK: 0x00*/
                pr_err("[tran_disp:%s]get the data form Internal display ON: value = 0x%lx , data = 0x%x", __func__, value , *data);
                tran_inter_display_st = TRAN_INTERNAL_SCREEN_POWER_ON;
                tran_a_g_cvt_map();
                /* Check if disp_wq and work_disp are not NULL before queuing work */
                if (dev->disp_wq) {
                    queue_work(dev->disp_wq, &dev->work_disp);
                } else {
                    pr_err("[tran_disp:%s] Work queue is NULL", __func__);
                }
            // } else if (*data == TRAN_DISP_BLANK_UNBLANK_DSI1) {
            //     /*External display on MTK_DISP_BLANK_UNBLANK: 0x03*/
            //     tran_exter_display_st = TRAN_EXTERNAL_SCREEN_POWER_ON;
            //     pr_err("[tran_sensor_cvt]get the data form External display ON: value = 0x%lx , data = 0x%x",value , *data);
            //     tran_a_g_cvt_map();
            } else if (*data == MTK_DISP_BLANK_POWERDOWN) {
                /*Internal display off MTK_DISP_BLANK_UNBLANK: 0x01*/
                pr_err("[tran_disp:%s]get the data form Internal display OFF: value = 0x%lx , data = 0x%x", __func__, value , *data);
                tran_inter_display_st = TRAN_INTERNAL_SCREEN_POWER_OFF;
                /* Check if disp_wq and work_disp are not NULL before queuing work */
                if (dev->disp_wq) {
                    queue_work(dev->disp_wq, &dev->work_disp);
                }else {
                    pr_err("[tran_disp:%s] Work queue is NULL", __func__);
                }
            // } else if (*data == TRAN_DISP_BLANK_POWERDOWN_DSI1) {
            //     /*External display off MTK_DISP_BLANK_UNBLANK: 0x04*/
            //     pr_err("[tran_sensor_cvt]get the data form External display OFF: value = 0x%lx , data = 0x%x",value , *data);
            //     tran_exter_display_st = TRAN_EXTERNAL_SCREEN_POWER_OFF;
            }
        }
    } else {
        pr_err("[tran_disp:%s] can't get the data from display", __func__);
        return 0;
    }
    return 0;
}
#endif


#if IS_ENABLED(CONFIG_TRAN_CWB_SUPPORT)
void dump_cwb_coordinate(int location)
{
    pr_err("%s [tran_sensor] dts info offset_x=%d, offset_y=%d, clip_w=%d, clip_h=%d\n", \
                __func__, \
                cust_sensor_info[location].offset_x, \
                cust_sensor_info[location].offset_y, \
                cust_sensor_info[location].clip_w, \
                cust_sensor_info[location].clip_h);
}
#endif
static int transceiver_enable(struct hf_device *hf_dev,
		int sensor_type, int en)
{

#if defined(CONFIG_TRAN_CHARGER_POLL) || defined(CONFIG_TRAN_GET_BRIGHTNESS_LEVEL_SUPPORT) || defined(CONFIG_TRAN_GET_VREFRESH_SUPPORT) || defined(CONFIG_TRAN_TORCH_ST_GET)
    struct transceiver_device *device = &transceiver_dev;
#endif

#if IS_ENABLED(CONFIG_TRAN_CWB_SUPPORT)
    int cwb_disable_ret = 0;
#endif

	int ret = 0;
	struct transceiver_device *dev = hf_dev->private_data;
	struct transceiver_state *state = NULL;
    struct hf_manager_event evt;

    if (sensor_type == SENSOR_TYPE_PROXIMITY) {
        if (en) {
            pr_err("%d %s: distance default first report far!!!\n", __LINE__, __func__);
            evt.sensor_type = SENSOR_TYPE_PROXIMITY;
            evt.action = DATA_ACTION;
            evt.accurancy = 0;
            evt.timestamp = ktime_get_boottime_ns();
            evt.word[0] = 5;

            transceiver_report(dev, &evt);
            pr_err("%d %s: distance default first report far!!!\n", __LINE__, __func__);
        }
    }
    if (sensor_type == SENSOR_TYPE_ANTI_TOUCH_PS) {
        if (en) {
            pr_err("%d %s: SENSOR_TYPE_ANTI_TOUCH_PS default first report far!!!\n", __LINE__, __func__);
            evt.sensor_type = SENSOR_TYPE_ANTI_TOUCH_PS;
            evt.action = DATA_ACTION;
            evt.accurancy = 0;
            evt.timestamp = ktime_get_boottime_ns();
            evt.word[0] = 5;

            transceiver_report(dev, &evt);
            pr_err("%d %s: distance default first report far!!!\n", __LINE__, __func__);
        }
    }
	state = &dev->state[sensor_type];
	mutex_lock(&dev->enable_lock);
	if (en) {
		sensor_register_freq(sensor_type);
            /*Check whether cwb is supported */
            if (tran_sensor_un_als_support) {
#ifdef CONFIG_TRAN_GET_BRIGHTNESS_LEVEL_SUPPORT
                printk("[yzg get_brightness] %s, g_tran_brightness_level= %d.\n ",__func__, g_tran_brightness_level);
#endif
#if IS_ENABLED(CONFIG_TRAN_CWB_SUPPORT)

#ifdef CONFIG_TRAN_360_ALS_SUPPORT
        if ((cust_sensor_info[TRAN_ML].support_state) && (sensor_type == SENSOR_TYPE_LIGHT))
        {
            tran_cwb_flip_lightals_enable = true;
            if (cwb_ctrl.als_on == false)
            {
                sensor_location = TRAN_ML;
                dump_cwb_coordinate(sensor_location);
                cust_sensor_info[TRAN_ML].enable_state = en;
                cwb_ctrl.sensor_type = SENSOR_TYPE_LIGHT;
                cwb_ctrl.als_on = true;
                g_tran_last_brightness_level = 1000;
                /*un als enable, do queue work for tran_sensor_cwb_enable on*/
                if (cwb_ctrl.qw_flag == CWB_EN_UNLOCK) {
                    queue_work(device->cwb_wq, &device->work_cwb);
                    }
                hrtimer_start(&device->brightness_timer, ns_to_ktime(tran_get_lcm_info_timer * NSEC_PER_MSEC), HRTIMER_MODE_REL);
                hrtimer_start(&device->cwb_timer, ns_to_ktime(CWB_POLLING_DELAY * NSEC_PER_MSEC), HRTIMER_MODE_REL);
                pr_err("%s-AlsEnable-[tran_sensor] sensor_type[%d], en[%d]do queue_work and hrtimer_start !!!\n", __func__, sensor_type, en);
            }
        }
        if((cust_sensor_info[TRAN_ML].support_state)  && (sensor_type == SENSOR_TYPE_SUBALS))
        {
            tran_cwb_flip_subals_enable = true;
            if (cwb_ctrl.als_on == false)
            {
                sensor_location = TRAN_ML;
                dump_cwb_coordinate(sensor_location);
                cust_sensor_info[TRAN_ML].enable_state = en;
                cwb_ctrl.sensor_type = SENSOR_TYPE_LIGHT;
                cwb_ctrl.als_on = true;
                g_tran_last_brightness_level = 1000;
                /*un als enable, do queue work for tran_sensor_cwb_enable on*/
                if (cwb_ctrl.qw_flag == CWB_EN_UNLOCK)
                {
                    queue_work(device->cwb_wq, &device->work_cwb);
                }
                hrtimer_start(&device->brightness_timer, ns_to_ktime(tran_get_lcm_info_timer * NSEC_PER_MSEC), HRTIMER_MODE_REL);
                hrtimer_start(&device->cwb_timer, ns_to_ktime(CWB_POLLING_DELAY * NSEC_PER_MSEC), HRTIMER_MODE_REL);
                pr_err("%s-AlsEnable-[tran_sensor] sensor_type[%d], en[%d]do queue_work and hrtimer_start !!!\n", __func__, sensor_type, en);
            }
        }
#else
                if ((cust_sensor_info[TRAN_ML].support_state) && (sensor_type == SENSOR_TYPE_LIGHT)){
                    tran_cwb_flip_lightals_enable = true;
                    if (cwb_ctrl.als_on == false)
                    {
                        sensor_location = TRAN_ML;
                        dump_cwb_coordinate(sensor_location);
                        cust_sensor_info[TRAN_ML].enable_state = en;
                        cwb_ctrl.sensor_type = SENSOR_TYPE_LIGHT;
                        cwb_ctrl.als_on = true;
                        g_tran_last_brightness_level = 1000;
                        /*un als enable, do queue work for tran_sensor_cwb_enable on*/
                        if (cwb_ctrl.qw_flag == CWB_EN_UNLOCK) {
                            queue_work(device->cwb_wq, &device->work_cwb);
                        }
                    }
                    hrtimer_start(&device->brightness_timer, ns_to_ktime(tran_get_lcm_info_timer * NSEC_PER_MSEC), HRTIMER_MODE_REL);
                    hrtimer_start(&device->cwb_timer, ns_to_ktime(CWB_POLLING_DELAY * NSEC_PER_MSEC), HRTIMER_MODE_REL);
                    pr_err("%s-AlsEnable-[tran_sensor] sensor_type[%d], en[%d]do queue_work and hrtimer_start !!!\n", __func__, sensor_type, en);
                }

		if ((cust_sensor_info[TRAN_ML].support_state) && (sensor_type == SENSOR_TYPE_ANTI_TOUCH_ULTRASOUND)){
                    tran_cwb_flip_atu_enable = true;
                    if (cwb_ctrl.als_on == false)
                    {
                        sensor_location = TRAN_ML;
                        dump_cwb_coordinate(sensor_location);
                        cust_sensor_info[TRAN_ML].enable_state = en;
                        cwb_ctrl.sensor_type = SENSOR_TYPE_LIGHT;
                        cwb_ctrl.als_on = true;
                        g_tran_last_brightness_level = 1000;
                        /*un als enable, do queue work for tran_sensor_cwb_enable on*/
                        if (cwb_ctrl.qw_flag == CWB_EN_UNLOCK) {
                            queue_work(device->cwb_wq, &device->work_cwb);
                        }
                    }
                    hrtimer_start(&device->brightness_timer, ns_to_ktime(tran_get_lcm_info_timer * NSEC_PER_MSEC), HRTIMER_MODE_REL);
                    hrtimer_start(&device->cwb_timer, ns_to_ktime(CWB_POLLING_DELAY * NSEC_PER_MSEC), HRTIMER_MODE_REL);
                    pr_err("%s-AlsEnable-[tran_sensor] sensor_type[%d], en[%d]do queue_work and hrtimer_start !!!\n", __func__, sensor_type, en);
                }

#endif

                if ((cust_sensor_info[TRAN_SL].support_state) &&(sensor_type == SENSOR_TYPE_PADALS)){
                    sensor_location = TRAN_SL;
                    dump_cwb_coordinate(sensor_location);
                    cust_sensor_info[TRAN_SL].enable_state = en;
                    cwb_ctrl.sensor_type = SENSOR_TYPE_PADALS;
                    cwb_ctrl.als_on = true;
                    g_tran_last_brightness_level = 1000;
                    /*un als enable, do queue work for tran_sensor_cwb_enable on*/
                    if (cwb_ctrl.qw_flag == CWB_EN_UNLOCK) {
                        queue_work(device->cwb_wq, &device->work_cwb);
                    }
                    hrtimer_start(&device->brightness_timer, ns_to_ktime(tran_get_lcm_info_timer * NSEC_PER_MSEC), HRTIMER_MODE_REL);
                    hrtimer_start(&device->cwb_timer, ns_to_ktime(CWB_POLLING_DELAY * NSEC_PER_MSEC), HRTIMER_MODE_REL);
                    pr_err("%s-AlsEnable-[tran_sensor] sensor_type[%d], en[%d]do queue_work and hrtimer_start !!!\n", __func__, sensor_type, en);
                }
#endif
            }
		ret = transceiver_comm_with(sensor_type,
			SENS_COMM_CTRL_ENABLE_CMD,
			&state->batch, sizeof(state->batch));
		if (ret >= 0)
			state->enable = true;
		else
			sensor_deregister_freq(sensor_type);
	} else {
        if (tran_sensor_un_als_support) {
#if IS_ENABLED(CONFIG_TRAN_CWB_SUPPORT)

#ifdef CONFIG_TRAN_360_ALS_SUPPORT
            if((cust_sensor_info[TRAN_ML].support_state) &&(sensor_type == SENSOR_TYPE_LIGHT))
                tran_cwb_flip_lightals_enable = false;
            if((cust_sensor_info[TRAN_ML].support_state) &&(sensor_type == SENSOR_TYPE_SUBALS))
                tran_cwb_flip_subals_enable = false;
            if((tran_cwb_flip_subals_enable == false) && (tran_cwb_flip_lightals_enable == false) && ((sensor_type == SENSOR_TYPE_LIGHT) || (sensor_type == SENSOR_TYPE_SUBALS)))
            {
                cust_sensor_info[TRAN_ML].enable_state = en;
                cwb_ctrl.als_on = false;
                hrtimer_cancel(&device->brightness_timer);
                /*un als disable, stop cwb hrtimer, cancel cwb en work*/
                hrtimer_cancel(&device->cwb_timer);
                cwb_ctrl.timer_count = 0;
                cancel_work_sync(&device->work_cwb);
                pr_err("%s-AlsDisable-[tran_sensor] sensor_type[%d], en[%d]do cancel_work_sync and hrtimer_cancel !!!\n", __func__, sensor_type, en);
            }
#else

            if((cust_sensor_info[TRAN_ML].support_state) &&(sensor_type == SENSOR_TYPE_LIGHT))
                tran_cwb_flip_lightals_enable = false;
            if((cust_sensor_info[TRAN_ML].support_state) &&(sensor_type == SENSOR_TYPE_ANTI_TOUCH_ULTRASOUND))
                tran_cwb_flip_atu_enable = false;
            if((tran_cwb_flip_atu_enable == false) && (tran_cwb_flip_lightals_enable == false)){
                if ((cust_sensor_info[TRAN_ML].support_state) && ((sensor_type == SENSOR_TYPE_LIGHT) || (sensor_type == SENSOR_TYPE_ANTI_TOUCH_ULTRASOUND))) {
                    cust_sensor_info[TRAN_ML].enable_state = en;
                    cwb_ctrl.als_on = false;
                    hrtimer_cancel(&device->brightness_timer);
                     /*un als disable, stop cwb hrtimer, cancel cwb en work*/
                    hrtimer_cancel(&device->cwb_timer);
                    cwb_ctrl.timer_count = 0;
                    cancel_work_sync(&device->work_cwb);
                    pr_err("%s-AlsDisable-[tran_sensor] sensor_type[%d], en[%d]do cancel_work_sync and hrtimer_cancel !!!\n", __func__, sensor_type, en);
                }
            }

#endif

                if ((cust_sensor_info[TRAN_SL].support_state) &&(sensor_type == SENSOR_TYPE_PADALS)){
                    cust_sensor_info[TRAN_SL].enable_state = en;
                    cwb_ctrl.als_on = false;
                    hrtimer_cancel(&device->brightness_timer);
                     /*un als disable, stop cwb hrtimer, cancel cwb en work*/
                    hrtimer_cancel(&device->cwb_timer);
                    cwb_ctrl.timer_count = 0;
                    cancel_work_sync(&device->work_cwb);
                    pr_err("%s-AlsDisable-[tran_sensor] sensor_type[%d], en[%d]do cancel_work_sync and hrtimer_cancel !!!\n", __func__, sensor_type, en);
                }
#endif
        }
		ret = transceiver_comm_with(sensor_type,
			SENS_COMM_CTRL_DISABLE_CMD, NULL, 0);
		state->batch.delay = S64_MAX;
		state->batch.latency = S64_MAX;
		state->enable = false;
		sensor_deregister_freq(sensor_type);
#if IS_ENABLED(CONFIG_TRAN_CWB_SUPPORT)
        if (tran_sensor_un_als_support) {

#ifdef CONFIG_TRAN_360_ALS_SUPPORT
                if((tran_cwb_flip_subals_enable == false) && (tran_cwb_flip_lightals_enable == false) && ((sensor_type == SENSOR_TYPE_LIGHT) || (sensor_type == SENSOR_TYPE_SUBALS)))
                {
                    cwb_disable_ret = tran_sensor_cwb_enable(sensor_type, false);
                    pr_err("%s-AlsDisable-[tran_sensor] sensor_type[%d], en[%d] cwb disable done, cwb_disable_ret:%d\n", __func__, sensor_type, cwb_disable_ret, en);
                }
#else
                if ((cust_sensor_info[TRAN_ML].support_state) && (sensor_type == SENSOR_TYPE_LIGHT)){
                    cwb_disable_ret = tran_sensor_cwb_enable(sensor_type, false);
                    pr_err("%s-AlsDisable-[tran_sensor] sensor_type[%d], en[%d] cwb disable done, cwb_disable_ret:%d\n", __func__, sensor_type, cwb_disable_ret, en);
                }
#endif

                if ((cust_sensor_info[TRAN_SL].support_state) && (sensor_type == SENSOR_TYPE_PADALS)){
                    cwb_disable_ret = tran_sensor_cwb_enable(sensor_type, false);
                    pr_err("%s-AlsDisable-[tran_sensor] sensor_type[%d], en[%d] cwb disable done, cwb_disable_ret:%d\n", __func__, sensor_type, cwb_disable_ret, en);
                }
        }
#endif
    }
#ifdef CONFIG_TRAN_CHARGER_POLL
    if (sensor_type == SENSOR_TYPE_ORIENTATION) {
        pr_err("[setchg] %s sensor_type = %d\n", __func__, sensor_type);
        tran_charger_sensor_type = sensor_type;
        if (en) {
            queue_work(device->charger_wq, &device->work_charger);    //Enable the Msensor and deliver the charging status
            hrtimer_start(&device->charger_timer, ns_to_ktime(CHARGER_POLLING_DELAY * NSEC_PER_MSEC), HRTIMER_MODE_REL);
            pr_err("[setchg] %s is hrtimer_start poll!!!\n", __func__);
        } else {
            hrtimer_cancel(&device->charger_timer);
            pr_err("[setchg] %s is hrtimer_end poll!!!\n", __func__);
            cancel_work_sync(&device->work_charger);
            tran_charger_val = 0;
        }
    }
#endif

#ifdef CONFIG_TRAN_GET_BRIGHTNESS_LEVEL_SUPPORT
#if !IS_ENABLED(CONFIG_TRAN_CWB_SUPPORT)
    if (sensor_type == SENSOR_TYPE_LIGHT) {
//        pr_err("[get_brightness] %s sensor_type = %d\n", __func__, sensor_type);
        g_tran_brightness_sensor_type = sensor_type;
        if (en) {
            hrtimer_start(&device->brightness_timer, ns_to_ktime(tran_get_lcm_info_timer * NSEC_PER_MSEC), HRTIMER_MODE_REL);
//            pr_err("[get_brightness] %s is hrtimer_start poll!!!\n", __func__);
        } else {
            hrtimer_cancel(&device->brightness_timer);
//            pr_err("[get_brightness] %s is hrtimer_end poll!!!\n", __func__);
            cancel_work_sync(&device->work_brightness);
            g_tran_brightness_level = 0;
        }
    }
#endif
#endif

#ifdef CONFIG_TRAN_GET_HALL
    if (sensor_type == SENSOR_TYPE_HINGE) {
//        pr_err("[get_hall] %s sensor_type = %d\n", __func__, sensor_type);
        g_tran_hall_sensor_type = sensor_type;
        if (en) {
            hrtimer_start(&device->hall_timer, ns_to_ktime(HALL_POLLING_DELAY * NSEC_PER_MSEC), HRTIMER_MODE_REL);
//            pr_err("[get_hall] %s is hrtimer_start poll!!!\n", __func__);
        } else {
            hrtimer_cancel(&device->hall_timer);
//            pr_err("[get_hall] %s is hrtimer_end poll!!!\n", __func__);
            cancel_work_sync(&device->work_hall);
            g_tran_last_hall_level = 0;
        }
    }
#endif

// if 0
// #ifdef CONFIG_TRAN_GET_VREFRESH_SUPPORT
//     if (sensor_type == SENSOR_TYPE_LIGHT) {
// //        pr_err("[get_vrefresh] %s sensor_type = %d\n", __func__, sensor_type);
//         g_tran_vrefresh_sensor_type = sensor_type;
//         if (en) {
//             hrtimer_start(&device->vrefresh_timer, ns_to_ktime(VREFRESH_POLLING_DELAY * NSEC_PER_MSEC), HRTIMER_MODE_REL);
// //           pr_err("[get_vrefresh] %s is hrtimer_start poll!!!\n", __func__);
//         } else {
//             hrtimer_cancel(&device->vrefresh_timer);
// //            pr_err("[get_vrefresh] %s is hrtimer_end poll!!!\n", __func__);
//             cancel_work_sync(&device->work_vrefresh);
//             g_tran_get_vrefresh = 0;
//         }
//     }
// #endif
// endif


#ifdef CONFIG_TRAN_TORCH_ST_GET
    if (sensor_type == SENSOR_TYPE_ORIENTATION) {
        if (en) {
            g_tran_orientation_enable = true;
            queue_work(device->torch_wq, &device->work_torch);
        } else {
            g_tran_orientation_enable = false;
        }
    }
#endif


	mutex_unlock(&dev->enable_lock);
	return ret;
}

static int transceiver_batch(struct hf_device *hf_dev,
		int sensor_type, int64_t delay, int64_t latency)
{
	int ret = 0;
	struct transceiver_device *dev = hf_dev->private_data;
	struct transceiver_state *state = NULL;
	struct sensor_comm_batch batch = {.delay = delay, .latency = latency};

	state = &dev->state[sensor_type];
	mutex_lock(&dev->enable_lock);
	if (!state->enable) {
		state->batch = batch;
		mutex_unlock(&dev->enable_lock);
		return ret;
	}

	ret = transceiver_comm_with(sensor_type,
		SENS_COMM_CTRL_ENABLE_CMD, &batch, sizeof(batch));
	if (ret >= 0)
		state->batch = batch;
	mutex_unlock(&dev->enable_lock);
	return ret;
}

static int transceiver_flush(struct hf_device *hf_dev,
		int sensor_type)
{
	int ret = 0;
	struct transceiver_device *dev = hf_dev->private_data;
	struct transceiver_state *state = NULL;

	state = &dev->state[sensor_type];
	mutex_lock(&dev->flush_lock);
	state->flush++;
	mutex_unlock(&dev->flush_lock);
	ret = transceiver_comm_with(sensor_type,
		SENS_COMM_CTRL_FLUSH_CMD, NULL, 0);
	if (ret < 0) {
		mutex_lock(&dev->flush_lock);
		if (state->flush > 0)
			state->flush--;
		mutex_unlock(&dev->flush_lock);
	}
	return ret;
}

static int transceiver_calibration(struct hf_device *hf_dev,
		int sensor_type)
{
	return transceiver_comm_with(sensor_type,
		SENS_COMM_CTRL_CALI_CMD, NULL, 0);
}

static int transceiver_config(struct hf_device *hf_dev,
		int sensor_type, void *data, uint8_t length)
{
	struct transceiver_device *dev = hf_dev->private_data;
	struct transceiver_config *cfg = NULL;
    if (!nvcfg) {
        nvcfg = kzalloc(sizeof(struct tran_als_nvcfg) + (ALS_NV_TRANSFER_MAX_BYTE * nv_restore_times), GFP_KERNEL);
        pr_err("nvcfg creat success\n");
    }
    if (!ps_nvcfg) {
        ps_nvcfg = kzalloc(sizeof(struct tran_ps_nvcfg) + (PS_NV_TRANSFER_MAX_BYTE * PS_NV_TRANSFER_TIME), GFP_KERNEL);
        pr_err("ps_nvcfg creat success\n");
    }
	mutex_lock(&dev->config_lock);
	cfg = dev->state[sensor_type].config;
	if (!cfg) {
		cfg = kzalloc(sizeof(*cfg) + length, GFP_KERNEL);
		if (!cfg) {
			mutex_unlock(&dev->config_lock);
			return -ENOMEM;
		}
		dev->state[sensor_type].config = cfg;
	} else {
		if (cfg->length != length) {
			pr_err("length not equal to prev length\n");
			mutex_unlock(&dev->config_lock);
			return -EINVAL;

		}
	}
	cfg->length = length;

if ((cust_sensor_info[TRAN_ML].support_state) && (tran_sensor_ml_nv_state_get)){ 
    if((sensor_type == SENSOR_TYPE_LIGHT) && (tran_als_nv_time % nv_restore_times == 1))
    {
        if(cust_sensor_info[TRAN_ML].vendor == SensorTek)  //SensorTek
        {
            if(cust_sensor_info[TRAN_ML].algo == CutOut)  //CutOut
            {
                if(cust_sensor_info[TRAN_ML].ver == V1_0_0)  //V1_0_0
                {
                    memcpy(nvcfg->data, data, ALS_NV_TRANSFER_MAX_BYTE);
                    pr_err("memcpy data1:%d, %d, %d, %d, %d, %d, %d, %d, %d\n", nvcfg->data[0], \
                    nvcfg->data[1], nvcfg->data[2], nvcfg->data[3], \
                    nvcfg->data[4], nvcfg->data[5], nvcfg->data[6],\
                     nvcfg->data[7], nvcfg->data[8]);
                    tran_als_nv_time ++;
                }
            }
        }
    }else if((sensor_type == SENSOR_TYPE_LIGHT) && (tran_als_nv_time % nv_restore_times == 2))
    {
        if(cust_sensor_info[TRAN_ML].vendor == SensorTek)  //SensorTek
        {
            if(cust_sensor_info[TRAN_ML].algo == CutOut)  //CutOut
            {
                if(cust_sensor_info[TRAN_ML].ver == V1_0_0)  //V1_0_0
                {
                    memcpy(nvcfg->data + ALS_NV_TRANSFER_MAX_BYTE, data, ALS_NV_TRANSFER_MAX_BYTE);
                    pr_err("memcpy data2:%d, %d, %d, %d, %d, %d, %d, %d, %d\n", nvcfg->data[9], \
                    nvcfg->data[10], nvcfg->data[11], nvcfg->data[12], \
                    nvcfg->data[13], nvcfg->data[14], nvcfg->data[15],\
                     nvcfg->data[16], nvcfg->data[17]);
                    tran_als_nv_time ++;
                }
            }
        }
    }else if((sensor_type == SENSOR_TYPE_LIGHT) && (tran_als_nv_time % nv_restore_times == 0))
    {
        if(cust_sensor_info[TRAN_ML].vendor == SensorTek)  //SensorTek
        {
            if(cust_sensor_info[TRAN_ML].algo == CutOut)  //CutOut
            {
                if(cust_sensor_info[TRAN_ML].ver == V1_0_0)  //V1_0_0
                {
                    memcpy(nvcfg->data + ALS_NV_TRANSFER_MAX_BYTE + ALS_NV_TRANSFER_MAX_BYTE, data, ALS_NV_TRANSFER_MAX_BYTE);
                    pr_err("memcpy data3:%d, %d, %d, %d, %d, %d, %d, %d, %d\n", nvcfg->data[18], \
                    nvcfg->data[19], nvcfg->data[20], nvcfg->data[21], \
                    nvcfg->data[22], nvcfg->data[23], nvcfg->data[24],\
                     nvcfg->data[25], nvcfg->data[26]);
                    tran_als_nv_time ++;
                }
            }
        }
    }
    }
if ((cust_sensor_info[TRAN_SL].support_state) && (tran_sensor_sl_nv_state_get)){
    if((sensor_type == SENSOR_TYPE_PADALS) && (tran_padals_nv_time % nv_restore_times == 1))
    {
        if(cust_sensor_info[TRAN_SL].vendor == SensorTek)  //SensorTek
        {
            if(cust_sensor_info[TRAN_SL].algo == CutOut)  //CutOut
            { 
                if(cust_sensor_info[TRAN_SL].ver == V1_0_0)  //V1_0_0
                {
                    memcpy(nvcfg->data, data, ALS_NV_TRANSFER_MAX_BYTE);
                    pr_err("memcpy data1:%d, %d, %d, %d, %d, %d, %d, %d, %d\n", nvcfg->data[0], \
                    nvcfg->data[1], nvcfg->data[2], nvcfg->data[3], \
                    nvcfg->data[4], nvcfg->data[5], nvcfg->data[6],\
                     nvcfg->data[7], nvcfg->data[8]);
                    tran_padals_nv_time ++;
                }
            }
        }
    }else if((sensor_type == SENSOR_TYPE_PADALS) && (tran_padals_nv_time % nv_restore_times == 2))
    {
        if(cust_sensor_info[TRAN_SL].vendor == SensorTek)  //SensorTek
        {
            if(cust_sensor_info[TRAN_SL].algo == CutOut)  //CutOut
            {
                if(cust_sensor_info[TRAN_SL].ver == V1_0_0)  //V1_0_0
                {
                    memcpy(nvcfg->data + ALS_NV_TRANSFER_MAX_BYTE, data, ALS_NV_TRANSFER_MAX_BYTE);
                    pr_err("memcpy data2:%d, %d, %d, %d, %d, %d, %d, %d, %d\n", nvcfg->data[9], \
                    nvcfg->data[10], nvcfg->data[11], nvcfg->data[12], \
                    nvcfg->data[13], nvcfg->data[14], nvcfg->data[15],\
                     nvcfg->data[16], nvcfg->data[17]);
                    tran_padals_nv_time ++;
                }
            }
        }
    }else if((sensor_type == SENSOR_TYPE_PADALS) && (tran_padals_nv_time % nv_restore_times == 0))
    {
        if(cust_sensor_info[TRAN_SL].vendor == SensorTek)  //SensorTek
        {
            if(cust_sensor_info[TRAN_SL].algo == CutOut)  //CutOut
            {
                if(cust_sensor_info[TRAN_SL].ver == V1_0_0)  //V1_0_0
                {
                    memcpy(nvcfg->data + ALS_NV_TRANSFER_MAX_BYTE + ALS_NV_TRANSFER_MAX_BYTE, data, ALS_NV_TRANSFER_MAX_BYTE);
                    pr_err("memcpy data3:%d, %d, %d, %d, %d, %d, %d, %d, %d\n", nvcfg->data[18], \
                    nvcfg->data[19], nvcfg->data[20], nvcfg->data[21], \
                    nvcfg->data[22], nvcfg->data[23], nvcfg->data[24],\
                     nvcfg->data[25], nvcfg->data[26]);
                    tran_padals_nv_time ++;
                }
            }
        }
    }
}
    if((sensor_type == SENSOR_TYPE_PROXIMITY) && (tran_ps_nv_time % PS_NV_TRANSFER_TIME == 1)){
        memcpy(ps_nvcfg->data, data, PS_NV_TRANSFER_MAX_BYTE);
        tran_ps_nv_time ++;
    }else if ((sensor_type == SENSOR_TYPE_PROXIMITY) && (tran_ps_nv_time % PS_NV_TRANSFER_TIME == 0)){
        memcpy(ps_nvcfg->data + PS_NV_TRANSFER_MAX_BYTE, data, PS_NV_TRANSFER_MAX_BYTE);
        tran_ps_nv_time ++;
    }

	memcpy(cfg->data, data, length);
	mutex_unlock(&dev->config_lock);

    pr_err("transceiver_config: %d\n", sensor_type);

	return transceiver_comm_with(sensor_type,
		SENS_COMM_CTRL_CONFIG_CMD, data, length);
}

static int transceiver_selftest(struct hf_device *hf_dev,
		int sensor_type)
{
	return transceiver_comm_with(sensor_type,
		SENS_COMM_CTRL_SELF_TEST_CMD, NULL, 0);
}

static int transceiver_rawdata(struct hf_device *hf_dev,
		int sensor_type, int en)
{
	int ret = 0;

	if (en)
		ret = transceiver_comm_with(sensor_type,
			SENS_COMM_CTRL_ENABLE_RAW_CMD, NULL, 0);
	else
		ret = transceiver_comm_with(sensor_type,
			SENS_COMM_CTRL_DISABLE_RAW_CMD, NULL, 0);
	return ret;
}

static int transceiver_debug(struct hf_device *hfdev, int sensor_type,
		uint8_t *buffer, unsigned int len)
{
	return debug_get_debug(sensor_type, buffer, len);
}

static int transceiver_custom_cmd(struct hf_device *hfdev, int sensor_type,
		struct custom_cmd *cust_cmd)
{
	return custom_cmd_comm_with(sensor_type, cust_cmd);
}

static void transceiver_restore_sensor(struct transceiver_device *dev)
{
	int ret = 0, index = 0, flush = 0;
	uint8_t sensor_type = 0;
	struct transceiver_state *state = NULL;

	/* NOTE: config restore all firstly, don't put in one for loop */
	mutex_lock(&dev->config_lock);
	for (index = 0; index < dev->support_size; index++) {
		sensor_type = dev->support_list[index].sensor_type;
		state = &dev->state[sensor_type];
		if (!state->config)
			continue;

    if((sensor_type == SENSOR_TYPE_LIGHT) && (tran_sensor_ml_nv_state_get))
    {
        if(cust_sensor_info[TRAN_ML].vendor == SensorTek)//SensorTek
        {
            if(cust_sensor_info[TRAN_ML].algo == CutOut)//CutOut
            {
                if(cust_sensor_info[TRAN_ML].ver == V1_0_0)//V1_0_0
                {
                    ret = transceiver_comm_with(sensor_type,
                        SENS_COMM_CTRL_CONFIG_CMD,
                        nvcfg->data, ALS_NV_TRANSFER_MAX_BYTE);
                    pr_err("restore data1:%d, %d, %d, %d, %d, %d, %d, %d, %d\n", nvcfg->data[0], \
                    nvcfg->data[1], nvcfg->data[2], nvcfg->data[3], \
                    nvcfg->data[4], nvcfg->data[5], nvcfg->data[6],\
                     nvcfg->data[7], nvcfg->data[8]);
                    ret = transceiver_comm_with(sensor_type,
                        SENS_COMM_CTRL_CONFIG_CMD,
                        nvcfg->data + ALS_NV_TRANSFER_MAX_BYTE, ALS_NV_TRANSFER_MAX_BYTE);
                    pr_err("restore data2:%d, %d, %d, %d, %d, %d, %d, %d, %d\n", nvcfg->data[9], \
                    nvcfg->data[10], nvcfg->data[11], nvcfg->data[12], \
                    nvcfg->data[13], nvcfg->data[14], nvcfg->data[15],\
                     nvcfg->data[16], nvcfg->data[17]);
                    ret = transceiver_comm_with(sensor_type,
                        SENS_COMM_CTRL_CONFIG_CMD,
                        nvcfg->data + ALS_NV_TRANSFER_MAX_BYTE + ALS_NV_TRANSFER_MAX_BYTE, ALS_NV_TRANSFER_MAX_BYTE);
                    pr_err("restore data3:%d, %d, %d, %d, %d, %d, %d, %d, %d\n", nvcfg->data[18], \
                    nvcfg->data[19], nvcfg->data[20], nvcfg->data[21], \
                    nvcfg->data[22], nvcfg->data[23], nvcfg->data[24],\
                     nvcfg->data[25], nvcfg->data[26]);
                }
            }
        }
    }else if((sensor_type == SENSOR_TYPE_PADALS) && (tran_sensor_sl_nv_state_get))
    {
        if(cust_sensor_info[TRAN_SL].vendor == SensorTek)//SensorTek
        {
            if(cust_sensor_info[TRAN_SL].algo == CutOut)//CutOut
            {
                if(cust_sensor_info[TRAN_SL].ver == V1_0_0)//V1_0_0
                {
                    ret = transceiver_comm_with(sensor_type,
                        SENS_COMM_CTRL_CONFIG_CMD,
                        nvcfg->data, ALS_NV_TRANSFER_MAX_BYTE);
                    pr_err("restore data1:%d, %d, %d, %d, %d, %d, %d, %d, %d\n", nvcfg->data[0], \
                    nvcfg->data[1], nvcfg->data[2], nvcfg->data[3], \
                    nvcfg->data[4], nvcfg->data[5], nvcfg->data[6],\
                     nvcfg->data[7], nvcfg->data[8]);
                    ret = transceiver_comm_with(sensor_type,
                        SENS_COMM_CTRL_CONFIG_CMD,
                        nvcfg->data + ALS_NV_TRANSFER_MAX_BYTE, ALS_NV_TRANSFER_MAX_BYTE);
                    pr_err("restore data2:%d, %d, %d, %d, %d, %d, %d, %d, %d\n", nvcfg->data[9], \
                    nvcfg->data[10], nvcfg->data[11], nvcfg->data[12], \
                    nvcfg->data[13], nvcfg->data[14], nvcfg->data[15],\
                     nvcfg->data[16], nvcfg->data[17]);
                    ret = transceiver_comm_with(sensor_type,
                        SENS_COMM_CTRL_CONFIG_CMD,
                        nvcfg->data + ALS_NV_TRANSFER_MAX_BYTE + ALS_NV_TRANSFER_MAX_BYTE, ALS_NV_TRANSFER_MAX_BYTE);
                    pr_err("restore data3:%d, %d, %d, %d, %d, %d, %d, %d, %d\n", nvcfg->data[18], \
                    nvcfg->data[19], nvcfg->data[20], nvcfg->data[21], \
                    nvcfg->data[22], nvcfg->data[23], nvcfg->data[24],\
                     nvcfg->data[25], nvcfg->data[26]);
                }
            }
        }
    }else if(sensor_type == SENSOR_TYPE_PROXIMITY){
        ret = transceiver_comm_with(sensor_type, SENS_COMM_CTRL_CONFIG_CMD, ps_nvcfg->data, PS_NV_TRANSFER_MAX_BYTE);
        pr_err("ps_restore data1: %d, %d", ps_nvcfg->data[0], ps_nvcfg->data[1]);
            if (ret < 0){
                pr_err("restore config %u fail %d\n", sensor_type, ret);
            }
        ret = transceiver_comm_with(sensor_type, SENS_COMM_CTRL_CONFIG_CMD, ps_nvcfg->data + PS_NV_TRANSFER_MAX_BYTE, PS_NV_TRANSFER_MAX_BYTE);
        pr_err("ps_restore data2: %d, %d", ps_nvcfg->data[2], ps_nvcfg->data[3]);
            if (ret < 0){
                pr_err("restore config %u fail %d\n", sensor_type, ret);
            }
    }else{
		ret = transceiver_comm_with(sensor_type,
			SENS_COMM_CTRL_CONFIG_CMD,
			state->config->data, state->config->length);
    }

		if (ret < 0)
			pr_err("restore config %u fail %d\n",
			       sensor_type, ret);
	}
	mutex_unlock(&dev->config_lock);
	/* enable restore */
	mutex_lock(&dev->enable_lock);
	for (index = 0; index < dev->support_size; index++) {
		sensor_type = dev->support_list[index].sensor_type;
		state = &dev->state[sensor_type];
		if (!state->enable)
			continue;
		ret = transceiver_comm_with(sensor_type,
			SENS_COMM_CTRL_ENABLE_CMD,
			&state->batch, sizeof(state->batch));
		if (ret < 0)
			pr_err("restore enable %u fail %d\n",
			       sensor_type, ret);
	}
	mutex_unlock(&dev->enable_lock);
	/* flush restore */
	mutex_lock(&dev->flush_lock);
	for (index = 0; index < dev->support_size; index++) {
		sensor_type = dev->support_list[index].sensor_type;
		state = &dev->state[sensor_type];
		flush = state->flush;
		while (flush-- > 0) {
			ret = transceiver_comm_with(sensor_type,
				SENS_COMM_CTRL_FLUSH_CMD, NULL, 0);
			if (ret < 0)
				pr_err("restore flush %u remain %u fail %d\n",
				       sensor_type, flush, ret);
		}
	}
	mutex_unlock(&dev->flush_lock);
}

static int transceiver_create_manager(struct transceiver_device *dev)
{
	int ret = 0;
	struct hf_device *hf_dev = &dev->hf_dev;

	memset(dev->support_list, 0, sizeof(dev->support_list));
	ret = sensor_list_get_list(dev->support_list,
		ARRAY_SIZE(dev->support_list));
	if (ret < 0)
		return ret;

	dev->support_size = ret;
	/* refill support_list and support_size then create manager */
	dev->hf_dev.support_list = dev->support_list;
	dev->hf_dev.support_size = dev->support_size;
	return hf_manager_create(hf_dev);
}

static void transceiver_destroy_manager(struct transceiver_device *dev)
{
	hf_manager_destroy(dev->hf_dev.manager);
}

static void transceiver_sensor_bootup(struct transceiver_device *dev)
{
	int ret = 0;

	ret = share_mem_config();
	if (ret < 0) {
		pr_err("share mem config fail %d\n", ret);
		return;
	}
	if (likely(atomic_xchg(&dev->first_bootup, false))) {
		timesync_start();
		ret = transceiver_create_manager(dev);
		if (ret < 0) {
			pr_err("create manager fail %d\n", ret);
			return;
		}
	} else {
		transceiver_restore_sensor(dev);
	}
}

static int transceiver_ready_notifier_call(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	if (event)
		transceiver_sensor_bootup(&transceiver_dev);

	return NOTIFY_DONE;
}

static struct notifier_block transceiver_ready_notifier = {
	.notifier_call = transceiver_ready_notifier_call,
	.priority = READY_HIGHPRI,
};

static int transceiver_pm_notifier_call(struct notifier_block *notifier,
		unsigned long pm_event, void *unused)
{
	switch (pm_event) {
	case PM_POST_SUSPEND:
		transceiver_comm_with(SENSOR_TYPE_INVALID,
			SENS_COMM_CTRL_UNMASK_NOTIFY_CMD, NULL, 0);
		timesync_resume();
		return NOTIFY_DONE;
	case PM_SUSPEND_PREPARE:
		transceiver_comm_with(SENSOR_TYPE_INVALID,
			SENS_COMM_CTRL_MASK_NOTIFY_CMD, NULL, 0);
		timesync_suspend();
		return NOTIFY_DONE;
	default:
		return NOTIFY_OK;
	}
	return NOTIFY_OK;
}

static struct notifier_block transceiver_pm_notifier = {
	.notifier_call = transceiver_pm_notifier_call,
};

static int transceiver_shm_cfg(struct share_mem_config *cfg,
		void *private_data)
{
	unsigned long flags = 0;
	struct transceiver_device *dev = private_data;

	spin_lock_irqsave(&transceiver_fifo_lock, flags);
	kfifo_reset(&transceiver_fifo);
	spin_unlock_irqrestore(&transceiver_fifo_lock, flags);

	dev->shm_reader.name = "trans_r";
	dev->shm_reader.item_size = sizeof(struct share_mem_data);
	dev->shm_reader.buffer_full_detect = false;
	return share_mem_init(&dev->shm_reader, cfg);
}

static int transceiver_shm_super_cfg(struct share_mem_config *cfg,
		void *private_data)
{
	unsigned long flags = 0;
	struct transceiver_device *dev = private_data;

	spin_lock_irqsave(&transceiver_fifo_lock, flags);
	kfifo_reset(&transceiver_super_fifo);
	spin_unlock_irqrestore(&transceiver_fifo_lock, flags);

	dev->shm_super_reader.name = "trans_super_r";
	dev->shm_super_reader.item_size = sizeof(struct share_mem_super_data);
	dev->shm_super_reader.buffer_full_detect = false;
	return share_mem_init(&dev->shm_super_reader, cfg);
}


#if IS_ENABLED(CONFIG_TRAN_SENSOR_DEBUG)
static int transceiver_sensor_debug(struct tran_sensor_debug_device *trdev,
                                uint32_t sensor_type, uint32_t debug_enable)
{
	return transceiver_comm_with(sensor_type,
			SENS_COMM_CTRL_TRAN_DEBUG_CMD, &debug_enable, sizeof(debug_enable));
}
#endif

static int __init transceiver_init(void)
{
	int ret = 0;
	struct transceiver_device *dev = &transceiver_dev;
	struct sched_param param = { .sched_priority = MAX_RT_PRIO / 2 };

	mutex_init(&dev->enable_lock);
	mutex_init(&dev->flush_lock);
	mutex_init(&dev->config_lock);
	dev->wakeup_src = wakeup_source_register(NULL, "trans_data");
	if (!dev->wakeup_src) {
		pr_err("trans_data wakeup source register fail\n");
		return -ENOMEM;
	}

	atomic_set(&dev->first_bootup, true);
	atomic_set(&dev->normal_wp_dropped, 0);
	atomic_set(&dev->super_wp_dropped, 0);

#ifdef CONFIG_AW_ACCDET_PLUG_CAIL
    register_accdet_notifier(&accdet_init_notifier);
#endif
    tran_sensor_init();

	memset(&dev->hf_dev, 0, sizeof(dev->hf_dev));
	dev->hf_dev.dev_name = "transceiver";
	dev->hf_dev.device_poll = HF_DEVICE_IO_INTERRUPT;
	dev->hf_dev.device_bus = HF_DEVICE_IO_ASYNC;
	dev->hf_dev.support_list = dev->support_list;
	dev->hf_dev.support_size = dev->support_size;
	dev->hf_dev.enable = transceiver_enable;
	dev->hf_dev.batch = transceiver_batch;
	dev->hf_dev.flush = transceiver_flush;
	dev->hf_dev.calibration = transceiver_calibration;
	dev->hf_dev.config_cali = transceiver_config;
	dev->hf_dev.selftest = transceiver_selftest;
	dev->hf_dev.rawdata = transceiver_rawdata;

	dev->hf_dev.debug = transceiver_debug;
	dev->hf_dev.custom_cmd = transceiver_custom_cmd;
	dev->hf_dev.private_data = dev;
	ret = hf_device_register(&dev->hf_dev);
	if (ret < 0) {
		pr_err("register hf device fail %d\n", ret);
		return ret;
	}

#if IS_ENABLED(CONFIG_TRAN_SENSOR_DEBUG)
	dev->tran_sensor_debug_dev.tran_sensor_debug = transceiver_sensor_debug;
	ret = tran_sensor_debug_register(&dev->tran_sensor_debug_dev);
	if (ret < 0) {
		pr_err("[TRAN_SENSOR_DEBUG]register by transceiver fail!");
	}
#endif


	ret = sensor_comm_init();
	if (ret < 0) {
		pr_err("sensor comm init fail %d\n", ret);
		goto out_device;
	}

	ret = sensor_list_init();
	if (ret < 0) {
		pr_err("sensor list init fail %d\n", ret);
		goto out_sensor_comm;
	}

	ret = debug_init();
	if (ret < 0) {
		pr_err("debug init fail %d\n", ret);
		goto out_sensor_list;
	}

	ret = custom_cmd_init();
	if (ret < 0) {
		pr_err("custom command init fail %d\n", ret);
		goto out_debug;
	}

	dev->filter.max_diff = 10000000000LL;
	dev->filter.min_diff = 10000000LL;
	dev->filter.bufsize = 16;
	dev->filter.name = "transceiver";
	ret = timesync_filter_init(&dev->filter);
	if (ret < 0) {
		pr_err("timesync filter init fail %d\n", ret);
		goto out_cust_cmd;
	}

	ret = timesync_init();
	if (ret < 0) {
		pr_err("timesync init fail %d\n", ret);
		goto out_timesync_filter;
	}

	ret = register_pm_notifier(&transceiver_pm_notifier);
	if (ret < 0) {
		pr_err("register pm notifier fail %d\n", ret);
		goto out_timesync;
	}

	dev->task = kthread_run(transceiver_thread, dev, "transceiver");
	if (IS_ERR(dev->task)) {
		ret = -ENOMEM;
		pr_err("create thread fail %d\n", ret);
		goto out_pm_notify;
	}
	sched_setscheduler_nocheck(dev->task, SCHED_FIFO, &param);

#ifdef CONFIG_TRAN_CHARGER_POLL
    /* init hrtimer */
    pr_err("[setchg] %s init hrtimer\n", __func__);
    hrtimer_init(&dev->charger_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    dev->charger_poll_delay = ns_to_ktime(CHARGER_POLLING_DELAY * NSEC_PER_MSEC);
    dev->charger_timer.function = charger_timer_func;

    dev->charger_wq = create_singlethread_workqueue("charger_wq");
    if (!dev->charger_wq) {
        ret = -ENOMEM;
        pr_err("%s: can't create charger workqueue\n", __func__);
    }

    /* this is the thread function we run on the work queue */
    INIT_WORK(&dev->work_charger, charger_work_func);
#endif
#ifdef CONFIG_TRAN_GET_BRIGHTNESS_LEVEL_SUPPORT
    /* init hrtimer */
//    pr_err("[get_brightness] %s init hrtimer\n", __func__);
    hrtimer_init(&dev->brightness_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    dev->brightness_poll_delay = ns_to_ktime(tran_get_lcm_info_timer * NSEC_PER_MSEC);
    dev->brightness_timer.function = brightness_timer_func;

    dev->brightness_wq = create_singlethread_workqueue("brightness_wq");
    if (!dev->brightness_wq) {
        ret = -ENOMEM;
        pr_err("%s: can't create brightness workqueue\n", __func__);
    }

    /* this is the thread function we run on the work queue */
    INIT_WORK(&dev->work_brightness, brightness_work_func);
#endif

#if IS_ENABLED(CONFIG_TRAN_CWB_SUPPORT)
    /* init hrtimer */
    pr_err("tran_sensor %s init hrtimer\n", __func__);
    hrtimer_init(&dev->cwb_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    dev->cwb_poll_delay = ns_to_ktime(CWB_POLLING_DELAY * NSEC_PER_MSEC);
    dev->cwb_timer.function = cwb_timer_func;

    /* reset struct tran_cwb_ctrl  */
    memset(&cwb_ctrl, 0, sizeof(struct tran_cwb_ctrl));
    pr_err("tran_sensor %s reset cwb_ctrl, timer_count:%d, qw_flag:%d, statue=%d, sensor_type:%d\n", \
                __func__, cwb_ctrl.timer_count, cwb_ctrl.qw_flag, cwb_ctrl.statue, cwb_ctrl.sensor_type);

    /*create cwb singlethread workqueue*/
    dev->cwb_wq = create_singlethread_workqueue("cwb_wq");
    if (!dev->cwb_wq) {
        ret = -ENOMEM;
        pr_err("tran_sensor %s: can't create cwb workqueue\n", __func__);
    }

    /* this is the thread function we run on the work queue */
    INIT_WORK(&dev->work_cwb, cwb_work_func);
#endif

#ifdef CONFIG_TRAN_GET_HALL
    /* init hrtimer */
//    pr_err("[get_hall] %s init hrtimer\n", __func__);
    hrtimer_init(&dev->hall_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    dev->hall_poll_delay = ns_to_ktime(HALL_POLLING_DELAY * NSEC_PER_MSEC);
    dev->hall_timer.function = hall_timer_func;

    dev->hall_wq = create_singlethread_workqueue("hall_wq");
    if (!dev->hall_wq) {
        ret = -ENOMEM;
//      pr_err("[get_hall]%s: can't create hall workqueue\n", __func__);
    }

    /* this is the thread function we run on the work queue */
    INIT_WORK(&dev->work_hall, hall_work_func);
#endif

#ifdef CONFIG_TRAN_GET_VREFRESH_SUPPORT
    /* init hrtimer */
//    pr_err("[get_vrefresh] %s init hrtimer\n", __func__);
    hrtimer_init(&dev->vrefresh_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    dev->vrefresh_poll_delay = ns_to_ktime(VREFRESH_POLLING_DELAY * NSEC_PER_MSEC);
    dev->vrefresh_timer.function = vrefresh_timer_func;

    dev->vrefresh_wq = create_singlethread_workqueue("vrefresh_wq");
    if (!dev->vrefresh_wq) {
        ret = -ENOMEM;
        pr_err("%s: can't create vrefresh workqueue\n", __func__);
    }

    /* this is the thread function we run on the work queue */
    INIT_WORK(&dev->work_vrefresh, vrefresh_work_func);
#endif

#ifdef CONFIG_TRAN_TORCH_ST_GET
    dev->torch_st_notif.notifier_call = tran_torch_st_notifier_callback;
    if (tran_torch_status_notifier_register("Sensor", &dev->torch_st_notif))
    {
        pr_err("[tran_sensor]Unable to register torch_st_notifier\n");
        tran_torch_errflag = false;
    }
    else
    {
        pr_err("[tran_sensor]register torch_st_notifier success\n");

        dev->torch_wq = create_singlethread_workqueue("torch_wq");
        if (!dev->torch_wq) {
            pr_err("%s: can't create torch workqueue\n", __func__);
            tran_torch_errflag = false;
        }
        INIT_WORK(&dev->work_torch, tran_torch_work_func);
    }
#endif

#if defined(CONFIG_SENSOR_GET_SCREEN_POWER_STATE) || defined(CONFIG_SENSOR_GET_LCM_INFO)

    dev->disp_wq = create_singlethread_workqueue("disp_wq");
    if (!dev->disp_wq) {
        pr_err("%s: can't create disp workqueue\n", __func__);
    }
    INIT_WORK(&dev->work_disp, tran_disp_work_func);

    dev->sensor_disp_notif.notifier_call = sensor_disp_notifier_callback;
    ret = mtk_disp_notifier_register("Sensor", &dev->sensor_disp_notif);
    if (ret) {
        pr_err("[tran_disp]Unable to register sensor_disp_notif, ret:%d\n", ret);
    }
    pr_err("[tran_disp]Sucess to register sensor_disp_notif, ret:%d\n", ret);
#endif


	/*
	 * NOTE: handler resgiter must before host ready to avoid lost
	 * share mem config etc.
	 */
	sensor_comm_notify_handler_register(SENS_COMM_NOTIFY_DATA_CMD,
		transceiver_notify_func, dev);
	sensor_comm_notify_handler_register(SENS_COMM_NOTIFY_FULL_CMD,
		transceiver_notify_func, dev);
	sensor_comm_notify_handler_register(SENS_COMM_NOTIFY_SUPER_DATA_CMD,
		transceiver_super_notify_func, dev);
	sensor_comm_notify_handler_register(SENS_COMM_NOTIFY_SUPER_FULL_CMD,
		transceiver_super_notify_func, dev);
	share_mem_config_handler_register(SHARE_MEM_DATA_PAYLOAD_TYPE,
		transceiver_shm_cfg, dev);
	share_mem_config_handler_register(SHARE_MEM_SUPER_DATA_PAYLOAD_TYPE,
		transceiver_shm_super_cfg, dev);

	/*
	 * NOTE: sensor ready must before host ready to avoid lost ready notify
	 * host ready init must at the end of function.
	 */
	sensor_ready_notifier_chain_register(&transceiver_ready_notifier);
	ret = host_ready_init();
	if (ret < 0) {
		pr_err("host ready init fail %d\n", ret);
		goto out_ready;
	}

	return 0;

out_ready:
	host_ready_exit();
	sensor_ready_notifier_chain_unregister(&transceiver_ready_notifier);
	share_mem_config_handler_unregister(SHARE_MEM_SUPER_DATA_PAYLOAD_TYPE);
	share_mem_config_handler_unregister(SHARE_MEM_DATA_PAYLOAD_TYPE);
	sensor_comm_notify_handler_unregister(SENS_COMM_NOTIFY_SUPER_FULL_CMD);
	sensor_comm_notify_handler_unregister(SENS_COMM_NOTIFY_SUPER_DATA_CMD);
	sensor_comm_notify_handler_unregister(SENS_COMM_NOTIFY_FULL_CMD);
	sensor_comm_notify_handler_unregister(SENS_COMM_NOTIFY_DATA_CMD);
out_pm_notify:

#ifdef CONFIG_TRAN_CHARGER_POLL
    destroy_workqueue(dev->charger_wq);
#endif

#ifdef CONFIG_TRAN_GET_BRIGHTNESS_LEVEL_SUPPORT
    destroy_workqueue(dev->brightness_wq);
#endif

#if IS_ENABLED(CONFIG_TRAN_CWB_SUPPORT)
    destroy_workqueue(dev->cwb_wq);
#endif 

#ifdef CONFIG_TRAN_GET_HALL
    destroy_workqueue(dev->hall_wq);
#endif
#ifdef CONFIG_TRAN_GET_VREFRESH_SUPPORT
    destroy_workqueue(dev->vrefresh_wq);
#endif

#if defined(CONFIG_SENSOR_GET_SCREEN_POWER_STATE) || defined(CONFIG_SENSOR_GET_LCM_INFO)
    if (mtk_disp_notifier_unregister(&dev->sensor_disp_notif))
        pr_err("[tran_sensor]Error occurred while unregistering sensor_disp_notif.");
#endif


	unregister_pm_notifier(&transceiver_pm_notifier);
out_timesync:
	timesync_exit();
out_timesync_filter:
	timesync_filter_exit(&dev->filter);
out_cust_cmd:
	custom_cmd_exit();
out_debug:
	debug_exit();
out_sensor_list:
	sensor_list_exit();
out_sensor_comm:
	sensor_comm_exit();
out_device:
	hf_device_unregister(&dev->hf_dev);
	wakeup_source_unregister(dev->wakeup_src);

#if IS_ENABLED(CONFIG_TRAN_SENSOR_DEBUG)
	tran_sensor_debug_unregister(&dev->tran_sensor_debug_dev);
#endif

	return ret;
}

static void __exit transceiver_exit(void)
{
	struct transceiver_device *dev = &transceiver_dev;

	share_mem_config_handler_unregister(SHARE_MEM_SUPER_DATA_PAYLOAD_TYPE);
	share_mem_config_handler_unregister(SHARE_MEM_DATA_PAYLOAD_TYPE);
	sensor_comm_notify_handler_unregister(SENS_COMM_NOTIFY_SUPER_FULL_CMD);
	sensor_comm_notify_handler_unregister(SENS_COMM_NOTIFY_SUPER_DATA_CMD);
	sensor_comm_notify_handler_unregister(SENS_COMM_NOTIFY_FULL_CMD);
	sensor_comm_notify_handler_unregister(SENS_COMM_NOTIFY_DATA_CMD);

#ifdef CONFIG_TRAN_CHARGER_POLL
    destroy_workqueue(dev->charger_wq);
#endif
#ifdef CONFIG_TRAN_GET_BRIGHTNESS_LEVEL_SUPPORT
    destroy_workqueue(dev->brightness_wq);
#endif

#if IS_ENABLED(CONFIG_TRAN_CWB_SUPPORT)
    destroy_workqueue(dev->cwb_wq);
#endif 

#ifdef CONFIG_TRAN_GET_HALL
    destroy_workqueue(dev->hall_wq);
#endif

#ifdef CONFIG_TRAN_GET_VREFRESH_SUPPORT
    destroy_workqueue(dev->vrefresh_wq);
#endif

#ifdef CONFIG_TRAN_TORCH_ST_GET
    if (tran_torch_status_notifier_unregister(&dev->torch_st_notif)) {
        pr_err("[tran_sensor]unregister torch_st_notifier fail\n");
    }
#endif


	if (!IS_ERR(dev->task))
		kthread_stop(dev->task);
	unregister_pm_notifier(&transceiver_pm_notifier);
	timesync_exit();
	custom_cmd_exit();
	debug_exit();
	sensor_list_exit();
	sensor_ready_notifier_chain_unregister(&transceiver_ready_notifier);
	host_ready_exit();
	sensor_comm_exit();
	hf_device_unregister(&dev->hf_dev);
	wakeup_source_unregister(dev->wakeup_src);
	transceiver_destroy_manager(dev);
}

module_init(transceiver_init);
module_exit(transceiver_exit);
MODULE_AUTHOR("Mediatek");
MODULE_DESCRIPTION("transceiver driver");
MODULE_LICENSE("GPL");

