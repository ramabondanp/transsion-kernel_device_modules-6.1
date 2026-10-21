/*
 *
 * FocalTech TouchScreen driver.
 *
 * Copyright (c) 2012-2020, Focaltech Ltd. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
/*****************************************************************************
*
* File Name: focaltech_core.h

* Author: Focaltech Driver Team
*
* Created: 2016-08-08
*
* Abstract:
*
* Reference:
*
*****************************************************************************/

#ifndef __LINUX_FOCALTECH_CORE_H__
#define __LINUX_FOCALTECH_CORE_H__
/*****************************************************************************
* Included header files
*****************************************************************************/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/uaccess.h>
#include <linux/firmware.h>
#include <linux/debugfs.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/dma-mapping.h>
#include "focaltech_common.h"
#include <linux/platform_device.h>
#include "focaltech_transsion.h"
#if IS_ENABLED(CONFIG_DRM_MEDIATEK)
#include "mtk_panel_ext.h"
#include "mtk_disp_notify.h"
#define	CONFIG_DRM2			1
//#define	CONFIG_DRM_PANEL2	1
#endif

#ifdef KEY_FINGER
#else
#define KEY_FINGER 195
#endif

/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#define FTS_DRIVER_NAME                     "mtk-tpd"

#define FTS_MAX_POINTS_SUPPORT              10 /* constant value, can't be changed */
#define FTS_MAX_KEYS                        4
#define FTS_KEY_DIM                         10
#define FTS_COORDS_ARR_SIZE                 4
#define FTS_ONE_TCH_LEN                     6
#define FTS_ONE_TCH_LEN_V2                  8
#define FTS_TOUCH_DATA_LEN_V2  (FTS_MAX_POINTS_SUPPORT * FTS_ONE_TCH_LEN_V2 + 4)

#define FTS_TOUCH_DATA_LEN  (FTS_MAX_POINTS_SUPPORT * FTS_ONE_TCH_LEN + 2)

#define FTS_GESTURE_POINTS_MAX              6
#define FTS_GESTURE_DATA_LEN               (FTS_GESTURE_POINTS_MAX * 4 + 4)

#define FTS_SIZE_PEN                        15
#define FTS_SIZE_DEFAULT                    62
#define FTS_SIZE_DEFAULT_V2                 21

#define FTS_MAX_ID                          0x0A
#define FTS_TOUCH_OFF_E_XH                  0
#define FTS_TOUCH_OFF_XL                    1
#define FTS_TOUCH_OFF_ID_YH                 2
#define FTS_TOUCH_OFF_YL                    3
#define FTS_TOUCH_OFF_PRE                   4
#define FTS_TOUCH_OFF_AREA                  5
#define FTS_TOUCH_OFF_MINOR                 6

#define FTS_TOUCH_E_NUM                     1
#define FTS_X_MIN_DISPLAY_DEFAULT           0
#define FTS_Y_MIN_DISPLAY_DEFAULT           0
#define FTS_X_MAX_DISPLAY_DEFAULT           (720 - 1)
#define FTS_Y_MAX_DISPLAY_DEFAULT           (1280 - 1)

#define FTS_TOUCH_DOWN                      0
#define FTS_TOUCH_UP                        1
#define FTS_TOUCH_CONTACT                   2
#define EVENT_DOWN(flag)                    ((FTS_TOUCH_DOWN == flag) || (FTS_TOUCH_CONTACT == flag))
#define EVENT_UP(flag)                      (FTS_TOUCH_UP == flag)

#define FTS_MAX_COMPATIBLE_TYPE             8
#define FTS_MAX_COMMMAND_LENGTH             16

#define FTS_MAX_TOUCH_BUF                   4096
#define FTS_MAX_BUS_BUF                     4096
#define FTS_FOD_MODE                                            0xCF
#define FTS_FOD_OPEN                                            0x02
#define FTS_FOD_CLOSE                                           0x00

#ifndef KEY_LARGE_AREA
#define KEY_LARGE_AREA      251
#endif
#define FTS_POCKET_SWITCH                                          0x9A
#define FTS_POCKET_MODE                                            0x9B
#define FTS_POCKET_OPEN                                            0x05
#define FTS_POCKET_CLOSE                                           0x00

/*****************************************************************************
*  Alternative mode (When something goes wrong, the modules may be able to solve the problem.)
*****************************************************************************/
/*
 * For commnication error in PM(deep sleep) state
 */
#define FTS_PATCH_COMERR_PM                 1
#define FTS_TIMEOUT_COMERR_PM               700

/*
 * For high resolution
 * Set FTS_TOUCH_HIRES_EN to 1 to support high resolution reporting of touch finger.
 * Set FTS_PEN_HIRES_EN to 1 to support high resolution reporting of stylus pen.
 *
 * FTS_XXX_HIRES_X, a multiple relative to the original resolution
 * FTS_HI_RES_X_MAX, const value, can't be modified
 */
#define FTS_TOUCH_HIRES_EN                  0
#define FTS_TOUCH_HIRES_X                   10

#define FTS_PEN_HIRES_EN                    1
#define FTS_PEN_HIRES_X                     10

#define FTS_HI_RES_X_MAX                    16

/*****************************************************************************
* Private enumerations, structures and unions using typedef
*****************************************************************************/
struct ftxxxx_proc {
    struct proc_dir_entry *proc_entry;
    u8 opmode;
    u8 cmd_len;
    u8 cmd[FTS_MAX_COMMMAND_LENGTH];
};

struct fts_ts_platform_data {
    u32 irq_gpio;
    u32 irq_gpio_flags;
    u32 iovdd_gpio;
    u32 iovdd_gpio_flags;
    u32 avdd_gpio;
    u32 avdd_gpio_flags;
    u32 reset_gpio;
    u32 reset_gpio_flags;
    bool have_key;
    bool iovdd_timing;
    bool fod_func_support;
    bool switch_report_support;
    bool pocket_func_support;
    bool custom_mistouch_support;
    u32 key_number;
    u32 keys[FTS_MAX_KEYS];
    u32 key_y_coords[FTS_MAX_KEYS];
    u32 key_x_coords[FTS_MAX_KEYS];
    u32 x_max;
    u32 y_max;
    u32 x_min;
    u32 y_min;
    u32 max_touch_number;
};

struct ts_event {
    int x;      /*x coordinate */
    int y;      /*y coordinate */
    int p;      /* pressure */
    int flag;   /* touch event flag: 0 -- down; 1-- up; 2 -- contact */
    int id;     /*touch ID */
    int area;
    int minor;
};

struct pen_event {
    int down;
    int inrange;
    int tip;
    int x;      /*x coordinate */
    int y;      /*y coordinate */
    int p;      /* pressure */
    int flag;   /* touch event flag: 0 -- down; 1-- up; 2 -- contact */
    int id;     /*touch ID */
    int tilt_x;
    int tilt_y;
    int azimuth;
    int tool_type;
};

struct fts_ts_data {
    struct i2c_client *client;
    struct spi_device *spi;
    struct device *dev;
    struct input_dev *input_dev;
    struct input_dev *pen_dev;
    struct fts_ts_platform_data *pdata;
    struct ts_ic_info ic_info;
    struct workqueue_struct *ts_workqueue;
    struct workqueue_struct *tran_resume_workqueue;
    struct work_struct tran_charger_work;
    struct work_struct fwupg_work;
    struct delayed_work esdcheck_work;
    struct delayed_work prc_work;
    struct work_struct resume_work;
    wait_queue_head_t ts_waitqueue;
    struct ftxxxx_proc proc;
    struct ftxxxx_proc proc_ta;
    spinlock_t irq_lock;
    struct mutex report_mutex;
    struct mutex bus_lock;
    unsigned long intr_jiffies;
    const struct firmware *fw;
    int irq;
    int log_level;
    int fw_is_running;      /* confirm fw is running when using spi:default 0 */
    int dummy_byte;
#if IS_ENABLED(CONFIG_PM) && FTS_PATCH_COMERR_PM
    struct completion pm_completion;
    bool pm_suspend;
#endif
    bool suspended;
    bool fw_loading;
    bool irq_disabled;
    bool power_disabled;
    bool glove_mode;
    bool cover_mode;
    bool charger_mode;
    bool touch_analysis_support;
    bool prc_support;
    bool prc_mode;
    bool esd_support;
    bool special_area_on;

    bool gesture_support;   /* gesture enable or disable, default: disable */
    u8 gesture_bmode;       /*gesture buffer mode*/

    u8 pen_etype;
    struct pen_event pevent;
    struct ts_event events[FTS_MAX_POINTS_SUPPORT];    /* multi-touch */
    u8 touch_addr;
    u32 touch_size;
    u8 *touch_buf;
    int touch_event_num;
    int touch_points;
    int key_state;
    int ta_flag;
    u32 ta_size;
    u8 *ta_buf;
    u8 bv;
    unsigned char g_tran_switch;
    u8 tran_esprots_switch;
    u8 tran_report_rate_value;
    u8 tran_lock_point_value;
    u8 tran_transient_report_rate_value;
    u8 esport3_micro_op_accuracy;
    u8 esport3_move_follow;
    u8 esport3_click_sensitivity;
    bool palmflag;

    u8 *bus_tx_buf;
    u8 *bus_rx_buf;
    int bus_type;
    int bus_ver;
    struct regulator *vdd;
    struct regulator *vcc_i2c;
#if FTS_PINCTRL_EN
    struct pinctrl *pinctrl;
    struct pinctrl_state *pins_active;
    struct pinctrl_state *pins_suspend;
    struct pinctrl_state *pins_release;
#endif
#if defined(CONFIG_FB) || defined(CONFIG_DRM)
    struct notifier_block fb_notif;
#elif defined(CONFIG_HAS_EARLYSUSPEND)
    struct early_suspend early_suspend;
#endif
};

enum _FTS_BUS_TYPE {
    BUS_TYPE_NONE,
    BUS_TYPE_I2C,
    BUS_TYPE_SPI,
};

enum _FTS_BUS_VER {
    BUS_VER_DEFAULT = 1,
    BUS_VER_V2,
};

enum _FTS_TOUCH_ETYPE {
    TOUCH_DEFAULT = 0x00,
    TOUCH_PROTOCOL_v2 = 0x02,
    TOUCH_EXTRA_MSG = 0x08,
    TOUCH_PEN = 0x0B,
    TOUCH_GESTURE = 0x80,
    TOUCH_FW_INIT = 0x81,
    TOUCH_DEFAULT_HI_RES = 0x82,
    TOUCH_IGNORE = 0xFE,
    TOUCH_ERROR = 0xFF,
};

enum _FTS_STYLUS_ETYPE {
    STYLUS_DEFAULT,
    STYLUS_HOVER,
};

enum _FTS_GESTURE_BMODE {
    GESTURE_BM_REG,
    GESTURE_BM_TOUCH,
};

/******************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#define KEY_GESTURE_U                           KEY_U
#define KEY_GESTURE_UP                          KEY_UP
#define KEY_GESTURE_DOWN                        KEY_DOWN
#define KEY_GESTURE_LEFT                        KEY_LEFT
#define KEY_GESTURE_RIGHT                       KEY_RIGHT
#define KEY_GESTURE_O                           KEY_O
#define KEY_GESTURE_E                           KEY_E
#define KEY_GESTURE_M                           KEY_M
#define KEY_GESTURE_L                           KEY_L
#define KEY_GESTURE_W                           KEY_W
#define KEY_GESTURE_S                           KEY_S
#define KEY_GESTURE_V                           KEY_V
#define KEY_GESTURE_C                           KEY_C
#define KEY_GESTURE_Z                           KEY_Z

#define GESTURE_LEFT                            0x20
#define GESTURE_RIGHT                           0x21
#define GESTURE_UP                              0x22
#define GESTURE_DOWN                            0x23
#define GESTURE_DOUBLECLICK                     0x24
#define GESTURE_ONECLICK                        0x25
#define GESTURE_O                               0x30
#define GESTURE_W                               0x31
#define GESTURE_M                               0x32
#define GESTURE_E                               0x33
#define GESTURE_L                               0x44
#define GESTURE_S                               0x46
#define GESTURE_V                               0x54
#define GESTURE_Z                               0x65
#define GESTURE_C                               0x34
#define GESTURE_FOD                             0x26
/*****************************************************************************
* Private enumerations, structures and unions using typedef
*****************************************************************************/
/*
* gesture_id    - mean which gesture is recognised
* point_num     - points number of this gesture
* coordinate_x  - All gesture point x coordinate
* coordinate_y  - All gesture point y coordinate
* mode          - gesture enable/disable, need enable by host
*               - 1:enable gesture function(default)  0:disable
* active        - gesture work flag,
*                 always set 1 when suspend, set 0 when resume
*/
struct fts_gesture_st {
    u8 gesture_id;
    u8 point_num;
    u16 coordinate_x[FTS_GESTURE_POINTS_MAX];
    u16 coordinate_y[FTS_GESTURE_POINTS_MAX];
};

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/
extern struct fts_ts_data *fts_data;

/* communication interface */
int fts_read(u8 *cmd, u32 cmdlen, u8 *data, u32 datalen);
int fts_read_reg(u8 addr, u8 *value);
int fts_write(u8 *writebuf, u32 writelen);
int fts_write_reg(u8 addr, u8 value);
int fts_bus_configure(struct fts_ts_data *ts_data, u8 *buf, u32 size);
int fts_bus_transfer_direct(u8 *writebuf, u32 writelen, u8 *readbuf, u32 readlen);
void fts_hid2std(void);
int fts_ts_probe_entry(struct fts_ts_data *ts_data);
int fts_ts_remove_entry(struct fts_ts_data *ts_data);

/* Apk and functions */
int fts_create_apk_debug_channel(struct fts_ts_data *);
void fts_release_apk_debug_channel(struct fts_ts_data *);

/* ADB functions */
int fts_create_sysfs(struct fts_ts_data *ts_data);
int fts_remove_sysfs(struct fts_ts_data *ts_data);

/* ESD */
int fts_esdcheck_init(struct fts_ts_data *ts_data);
int fts_esdcheck_exit(struct fts_ts_data *ts_data);
void fts_esdcheck_switch(struct fts_ts_data *ts_data, bool enable);
void fts_esdcheck_proc_busy(struct fts_ts_data *ts_data, bool proc_debug);
void fts_esdcheck_suspend(struct fts_ts_data *ts_data);
void fts_esdcheck_resume(struct fts_ts_data *ts_data);
bool fts_esd_is_disable(void);

/* Point Report Check*/
int fts_point_report_check_init(struct fts_ts_data *ts_data);
int fts_point_report_check_exit(struct fts_ts_data *ts_data);
void fts_prc_queue_work(struct fts_ts_data *ts_data);

/* FW upgrade */
int fts_fwupg_init(struct fts_ts_data *ts_data);
int fts_fwupg_exit(struct fts_ts_data *ts_data);
int fts_fw_recovery(void);
int fts_upgrade_bin(char *fw_name, bool force);
int fts_enter_test_environment(bool test_state);
int fts_enter_gesture_fw(void);
int fts_enter_normal_fw(void);

/* Other */
int fts_reset_proc(int hdelayms);
int fts_check_cid(struct fts_ts_data *ts_data, u8 id_h);
int fts_wait_tp_to_valid(void);
void fts_release_all_finger(void);
void fts_tp_state_recovery(struct fts_ts_data *ts_data);

void fts_irq_disable(void);
void fts_irq_enable(void);

int ft3681_init(void);
void ft3681_exit(void);

extern void fts_esdcheck_proc_busy(struct fts_ts_data *ts_data, bool proc_debug);
extern int fts_ts_suspend(struct device *dev);
extern int fts_ts_resume(struct device *dev);

#endif /* __LINUX_FOCALTECH_CORE_H__ */
