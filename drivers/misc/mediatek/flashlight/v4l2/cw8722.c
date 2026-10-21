// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 MediaTek Inc.
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/videodev2.h>
#include <linux/pinctrl/consumer.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <linux/thermal.h>
#if IS_ENABLED(CONFIG_MTK_FLASHLIGHT)
#include "flashlight-core.h"
#include <linux/power_supply.h>
#endif

#ifdef CONFIG_MTK_FLASHLIGHT_TWO_SUPPLY
#define CW8722_FLED_NAME "flash_cw8722"
#else
#define CW8722_FLED_NAME "flash"
#endif

#define CW8722_NAME	"cw8722"
#define CW8722_I2C_ADDR	(0x63)
#define CW8722_LOGD(format, args...)\
	pr_debug(CW8722_NAME "[%s] " format, __func__, ##args)
#define CW8722_LOGI(format, args...)\
	pr_info(CW8722_NAME "[%s] " format, __func__, ##args)
/* registers definitions */
#define REG_CHIPID          0x00
#define REG_ENABLE          0x01
#define REG_LED0_FLASH_BR	0x03
#define REG_LED1_FLASH_BR	0x04
#define REG_LED0_TORCH_BR	0x05
#define REG_LED1_TORCH_BR	0x06
#define REG_SW_RESET		0x07
#define REG_FLASH_TOUT		0x08
#define REG_FLAG1           0x0A
#define REG_FLAG2           0x0B
/* fault mask */
#define FAULT_TIMEOUT	(1<<0)
#define FAULT_THERMAL_SHUTDOWN	(1<<2)
#define FAULT_LED0_SHORT_CIRCUIT	(1<<5)
#define FAULT_LED1_SHORT_CIRCUIT	(1<<4)
/*  FLASH Brightness
 *	min 391mA, step 7.83mA, max 1900mA
 */
#define CW8722_FLASH_BRT_MIN 5000
#define CW8722_FLASH_BRT_STEP 7830
#define CW8722_FLASH_BRT_OFFSET 3910
#define CW8722_FLASH_BRT_MAX 1900000
#define CW8722_FLASH_BRT_uA_TO_REG(a)	\
	((a) < CW8722_FLASH_BRT_MIN ? 0 :	\
	 ((((a) - CW8722_FLASH_BRT_OFFSET) / CW8722_FLASH_BRT_STEP) & 0xFF))
#define CW8722_FLASH_BRT_REG_TO_uA(a)		\
	(((a) * CW8722_FLASH_BRT_STEP + CW8722_FLASH_BRT_OFFSET) \ 1000)
/*  FLASH TIMEOUT DURATION
 *	min 32ms, step 32ms, max 1024ms
 */
#define CW8722_FLASH_TOUT_MIN 200
#define CW8722_FLASH_TOUT_STEP 200
#define CW8722_FLASH_TOUT_MAX 1600
/*  TORCH BRT
 *	min 10mA, step 1.96mA, max 500mA
 */
#define CW8722_TORCH_BRT_MIN 10000
#define CW8722_TORCH_BRT_STEP 1960
#define CW8722_TORCH_BRT_OFFSET 980
#define CW8722_TORCH_BRT_MAX 500000
#define CW8722_TORCH_BRT_uA_TO_REG(a)	\
	((a) < CW8722_TORCH_BRT_MIN ? 0 :	\
	 ((((a) - CW8722_TORCH_BRT_OFFSET) / CW8722_TORCH_BRT_STEP) & 0xFF))
#define CW8722_TORCH_BRT_REG_TO_uA(a)		\
	(((a) * CW8722_TORCH_BRT_STEP + CW8722_TORCH_BRT_OFFSET) \ 1000)
static unsigned int cw8722_timeout_ms[2];
#define CW8722_COOLER_MAX_STATE 5
static const int flash_state_to_current_limit[CW8722_COOLER_MAX_STATE] = {
	200000, 150000, 100000, 50000, 25000
};
static const int reduceFlashCurrentmap[CW8722_COOLER_MAX_STATE] = {
	9, 8, 6, 5, 5
};
extern unsigned int g_sysfs_strobe_level[2];
extern unsigned int g_sysfs_inited[2];
extern unsigned int g_flashCurrNow;

static int cw8722_set_driver(int set);

enum cw8722_led_id {
	CW8722_LED0 = 0,
	CW8722_LED1,
	CW8722_LED_MAX
};
/* struct cw8722_platform_data
 *
 * @max_flash_timeout: flash timeout
 * @max_flash_brt: flash mode led brightness
 * @max_torch_brt: torch mode led brightness
 */
struct cw8722_platform_data {
	u32 max_flash_timeout;
	u32 max_flash_brt[CW8722_LED_MAX];
	u32 max_torch_brt[CW8722_LED_MAX];
};
enum led_enable {
	MODE_SHDN = 0x00,
	MODE_TORCH = 0x08,
	MODE_FLASH = 0x0C,
};
/**
 * struct cw8722_flash
 *
 * @dev: pointer to &struct device
 * @pdata: platform data
 * @regmap: reg. map for i2c
 * @lock: muxtex for serial access.
 * @led_mode: V4L2 LED mode
 * @ctrls_led: V4L2 controls
 * @subdev_led: V4L2 subdev
 */
struct cw8722_flash {
	struct device *dev;
	struct cw8722_platform_data *pdata;
	struct regmap *regmap;
	struct mutex lock;
	enum v4l2_flash_led_mode led_mode;
	struct v4l2_ctrl_handler ctrls_led[CW8722_LED_MAX];
	struct v4l2_subdev subdev_led[CW8722_LED_MAX];
	struct device_node *dnode[CW8722_LED_MAX];
	struct pinctrl *cw8722_hwen_pinctrl;
	struct pinctrl_state *cw8722_hwen_high;
	struct pinctrl_state *cw8722_hwen_low;
#if IS_ENABLED(CONFIG_MTK_FLASHLIGHT)
	struct flashlight_device_id flash_dev_id[CW8722_LED_MAX];
#endif
	struct thermal_cooling_device *cdev;
	int need_cooler;
	unsigned long max_state;
	unsigned long target_state;
	unsigned long target_current;
	unsigned long ori_current;
};
/* define usage count */
static int use_count;
static int flash_on;
static struct cw8722_flash *cw8722_flash_data;
static DEFINE_MUTEX(cw8722_mutex);
static DEFINE_MUTEX(cw8722_pinctrl_mutex);
#define to_cw8722_flash(_ctrl, _no)	\
	container_of(_ctrl->handler, struct cw8722_flash, ctrls_led[_no])
/* define pinctrl */
#define CW8722_PINCTRL_PIN_HWEN 0
#define CW8722_PINCTRL_PINSTATE_LOW 0
#define CW8722_PINCTRL_PINSTATE_HIGH 1
#define CW8722_PINCTRL_STATE_HWEN_HIGH "hwen_high"
#define CW8722_PINCTRL_STATE_HWEN_LOW  "hwen_low"
/******************************************************************************
 * Pinctrl configuration
 *****************************************************************************/
static int cw8722_pinctrl_init(struct cw8722_flash *flash)
{
	int ret = 0;
	/* get pinctrl */
	flash->cw8722_hwen_pinctrl = devm_pinctrl_get(flash->dev);
	if (IS_ERR(flash->cw8722_hwen_pinctrl)) {
		CW8722_LOGI("Failed to get flashlight pinctrl.\n");
		ret = PTR_ERR(flash->cw8722_hwen_pinctrl);
		return ret;
	}
	/* Flashlight HWEN pin initialization */
	flash->cw8722_hwen_high = pinctrl_lookup_state(
			flash->cw8722_hwen_pinctrl,
			CW8722_PINCTRL_STATE_HWEN_HIGH);
	if (IS_ERR(flash->cw8722_hwen_high)) {
		CW8722_LOGI("Failed to init (%s)\n",
			CW8722_PINCTRL_STATE_HWEN_HIGH);
		ret = PTR_ERR(flash->cw8722_hwen_high);
	}
	flash->cw8722_hwen_low = pinctrl_lookup_state(
			flash->cw8722_hwen_pinctrl,
			CW8722_PINCTRL_STATE_HWEN_LOW);
	if (IS_ERR(flash->cw8722_hwen_low)) {
		CW8722_LOGI("Failed to init (%s)\n", CW8722_PINCTRL_STATE_HWEN_LOW);
		ret = PTR_ERR(flash->cw8722_hwen_low);
	}
	return ret;
}
static int cw8722_pinctrl_set(struct cw8722_flash *flash, int pin, int state)
{
	int ret = 0;
	if (IS_ERR(flash->cw8722_hwen_pinctrl)) {
		CW8722_LOGI("pinctrl is not available\n");
		return -1;
	}
	mutex_lock(&cw8722_pinctrl_mutex);
	switch (pin) {
	case CW8722_PINCTRL_PIN_HWEN:
		if (state == CW8722_PINCTRL_PINSTATE_LOW &&
				!IS_ERR(flash->cw8722_hwen_low))
			pinctrl_select_state(flash->cw8722_hwen_pinctrl,
					flash->cw8722_hwen_low);
		else if (state == CW8722_PINCTRL_PINSTATE_HIGH &&
				!IS_ERR(flash->cw8722_hwen_high))
			pinctrl_select_state(flash->cw8722_hwen_pinctrl,
					flash->cw8722_hwen_high);
		else
			CW8722_LOGD("set err, pin(%d) state(%d)\n", pin, state);
		break;
	default:
		CW8722_LOGD("set err, pin(%d) state(%d)\n", pin, state);
		break;
	}
	mutex_unlock(&cw8722_pinctrl_mutex);
	return ret;
}
static int cw8722_set_scenario(int scenario)
{
	/* set decouple mode */
	return 0;
}

static int cw8722_mode_ctrl_tran(struct cw8722_flash *flash, enum cw8722_led_id led_no)
{
	int rval = -EINVAL;
	CW8722_LOGI("mode:[%d]", flash->led_mode);
	switch (flash->led_mode) {
	case V4L2_FLASH_LED_MODE_NONE:
		if(led_no == CW8722_LED0){
			rval = regmap_update_bits(flash->regmap,
			REG_ENABLE, 0x01, 0x00);
		}else{
			rval = regmap_update_bits(flash->regmap,
			REG_ENABLE, 0x02, 0x00);
		}
		// regmap_clear_bits(cw8722_flash_data->regmap, REG_ENABLE, 0x7f);
		break;
	case V4L2_FLASH_LED_MODE_TORCH:
		rval = regmap_update_bits(flash->regmap,
					  REG_ENABLE, 0x0C, MODE_TORCH);
		break;
	case V4L2_FLASH_LED_MODE_FLASH:
		rval = regmap_update_bits(flash->regmap,
					  REG_ENABLE, 0x0C, MODE_FLASH);
		break;
	}
	return rval;
}

/* enable mode control */
static int cw8722_mode_ctrl(struct cw8722_flash *flash)
{
	int rval = -EINVAL;
	CW8722_LOGI("mode:[%d]", flash->led_mode);
	switch (flash->led_mode) {
	case V4L2_FLASH_LED_MODE_NONE:
		rval = regmap_update_bits(flash->regmap,
					  REG_ENABLE, 0x0C, MODE_SHDN);
		regmap_clear_bits(cw8722_flash_data->regmap, REG_ENABLE, 0x7f);
		break;
	case V4L2_FLASH_LED_MODE_TORCH:
		rval = regmap_update_bits(flash->regmap,
					  REG_ENABLE, 0x0C, MODE_TORCH);
		break;
	case V4L2_FLASH_LED_MODE_FLASH:
		rval = regmap_update_bits(flash->regmap,
					  REG_ENABLE, 0x0C, MODE_FLASH);
		break;
	}
	return rval;
}

/* led1/2 enable/disable */
static int cw8722_enable_ctrl(struct cw8722_flash *flash,
			      enum cw8722_led_id led_no, bool on)
{
	int rval;
	unsigned int reg_val;
	CW8722_LOGI("led_no:[%d] enable:[%d]", led_no, on);
	flashlight_kicker_pbm(on);
	if (led_no == CW8722_LED0) {
		if (on)
			rval = regmap_update_bits(flash->regmap,
						  REG_ENABLE, 0x01, 0x01);
		else
			rval = regmap_update_bits(flash->regmap,
						  REG_ENABLE, 0x01, 0x00);
	} else {
		if (on)
			rval = regmap_update_bits(flash->regmap,
						  REG_ENABLE, 0x02, 0x02);
		else
			rval = regmap_update_bits(flash->regmap,
						  REG_ENABLE, 0x02, 0x00);
	}
	rval = regmap_read(flash->regmap, REG_ENABLE, &reg_val);
	CW8722_LOGD("reg_val:[0x%x]", reg_val);
	return rval;
}
/* torch1/2 brightness control */
static int cw8722_torch_brt_ctrl(struct cw8722_flash *flash,
				 enum cw8722_led_id led_no, unsigned int brt)
{
	int rval;
	u8 br_bits;
	CW8722_LOGI("led_no:[%d] brt:[%u]", led_no, brt);
	if (brt < CW8722_TORCH_BRT_MIN)
		return cw8722_enable_ctrl(flash, led_no, false);
#if 0
	if (flash->need_cooler == 0) {
		flash->ori_current = brt;
	} else {
		if (brt > flash->target_current) {
			brt = flash->target_current;
			CW8722_LOGI("thermal limit current:%d\n", brt);
		}
	}
#endif

	br_bits = CW8722_TORCH_BRT_uA_TO_REG(brt);
	CW8722_LOGI("br_bit:[%u]", br_bits);
	if (led_no == CW8722_LED0)
		rval = regmap_update_bits(flash->regmap,
					  REG_LED0_TORCH_BR, 0xFF, br_bits);
	else
		rval = regmap_update_bits(flash->regmap,
					  REG_LED1_TORCH_BR, 0xFF, br_bits);
	return rval;
}
/* flash1/2 brightness control */
static int cw8722_flash_brt_ctrl(struct cw8722_flash *flash,
				 enum cw8722_led_id led_no, unsigned int brt)
{
	int rval;
	u8 br_bits;
	CW8722_LOGI("led_no:[%d] brt:[%u]", led_no, brt);
	if (brt < CW8722_FLASH_BRT_MIN)
		return cw8722_enable_ctrl(flash, led_no, false);
#if 0
	if (flash->need_cooler == 1 && brt > flash->target_current) {
		brt = flash->target_current;
		CW8722_LOGD("thermal limit current:%d\n", brt);
	}
#endif
	br_bits = CW8722_FLASH_BRT_uA_TO_REG(brt);
	if (led_no == CW8722_LED0)
		rval = regmap_update_bits(flash->regmap,
					  REG_LED0_FLASH_BR, 0xFF, br_bits);
	else
		rval = regmap_update_bits(flash->regmap,
					  REG_LED1_FLASH_BR, 0xFF, br_bits);
	return rval;
}
/* flash1/2 timeout control */
static int cw8722_flash_tout_ctrl(struct cw8722_flash *flash,
				unsigned int tout)
{
	int rval;
	u8 tout_bits;
	CW8722_LOGD("tout:[%u]", tout);
	if (tout == 200)
		tout_bits = 0x04;
	else
		tout_bits = 0x07 + (tout / CW8722_FLASH_TOUT_STEP);
	rval = regmap_update_bits(flash->regmap,
				  REG_FLASH_TOUT, 0x0F, tout_bits);
	return rval;
}
/* v4l2 controls  */
static int cw8722_get_ctrl(struct v4l2_ctrl *ctrl, enum cw8722_led_id led_no)
{
	struct cw8722_flash *flash = to_cw8722_flash(ctrl, led_no);
	int rval = -EINVAL;
	mutex_lock(&flash->lock);
	if (ctrl->id == V4L2_CID_FLASH_FAULT) {
		s32 fault = 0;
		unsigned int reg_val = 0;
		rval = regmap_read(flash->regmap, REG_FLAG1, &reg_val);
		if (rval < 0)
			goto out;
		if (reg_val & FAULT_LED0_SHORT_CIRCUIT)
			fault |= V4L2_FLASH_FAULT_SHORT_CIRCUIT;
		if (reg_val & FAULT_LED1_SHORT_CIRCUIT)
			fault |= V4L2_FLASH_FAULT_SHORT_CIRCUIT;
		if (reg_val & FAULT_THERMAL_SHUTDOWN)
			fault |= V4L2_FLASH_FAULT_OVER_TEMPERATURE;
		if (reg_val & FAULT_TIMEOUT)
			fault |= V4L2_FLASH_FAULT_TIMEOUT;
		ctrl->cur.val = fault;
	}
out:
	mutex_unlock(&flash->lock);
	return rval;
}
static int cw8722_set_ctrl(struct v4l2_ctrl *ctrl, enum cw8722_led_id led_no)
{
	struct cw8722_flash *flash = to_cw8722_flash(ctrl, led_no);
	int rval = -EINVAL;
	CW8722_LOGD("led:[%d] ID:[%d]", led_no, ctrl->id);
	mutex_lock(&flash->lock);
	switch (ctrl->id) {
	case V4L2_CID_FLASH_LED_MODE:
		flash->led_mode = ctrl->val;
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH)
			rval = cw8722_mode_ctrl_tran(flash,led_no);
		else
			rval = 0;
		if (flash->led_mode == V4L2_FLASH_LED_MODE_NONE){
			if(led_no == 1) {
				g_sysfs_strobe_level[0]= 0;
				g_sysfs_inited[0]=0;
			} else {
				g_sysfs_strobe_level[1]= 0;
				g_sysfs_inited[1]=0;
			}
			cw8722_enable_ctrl(flash, led_no, false);
		}else if (flash->led_mode == V4L2_FLASH_LED_MODE_TORCH) {
			if(led_no == 1) {
				g_sysfs_strobe_level[0]= 2;
				g_sysfs_inited[0]=1;
			} else {
				g_sysfs_strobe_level[1]= 2;
				g_sysfs_inited[1]=1;
			}
			rval = cw8722_enable_ctrl(flash, led_no, true);
		}
		break;
	case V4L2_CID_FLASH_STROBE_SOURCE:
		if (ctrl->val == V4L2_FLASH_STROBE_SOURCE_SOFTWARE) {
			CW8722_LOGD("sw ctrl\n");
			rval = regmap_update_bits(flash->regmap,
					REG_ENABLE, 0x2C, 0x0C);
		} else if (ctrl->val == V4L2_FLASH_STROBE_SOURCE_EXTERNAL) {
			CW8722_LOGD("hw trigger\n");
			rval = regmap_update_bits(flash->regmap,
					REG_ENABLE, 0x2C, 0x20);
			rval = cw8722_enable_ctrl(flash, led_no, true);
		}
		if (rval < 0)
			goto err_out;
		break;
	case V4L2_CID_FLASH_STROBE:
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH) {
			rval = -EBUSY;
			goto err_out;
		}
		flash->led_mode = V4L2_FLASH_LED_MODE_FLASH;
		rval = cw8722_mode_ctrl(flash);
		rval = cw8722_enable_ctrl(flash, led_no, true);
		break;
	case V4L2_CID_FLASH_STROBE_STOP:
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH) {
			rval = -EBUSY;
			goto err_out;
		}
		cw8722_enable_ctrl(flash, led_no, false);
		flash->led_mode = V4L2_FLASH_LED_MODE_NONE;
		rval = cw8722_mode_ctrl(flash);
		break;
	case V4L2_CID_FLASH_TIMEOUT:
		rval = cw8722_flash_tout_ctrl(flash, ctrl->val);
		break;
	case V4L2_CID_FLASH_INTENSITY:
		rval = cw8722_flash_brt_ctrl(flash, led_no, ctrl->val);
		break;
	case V4L2_CID_FLASH_TORCH_INTENSITY:
		rval = cw8722_torch_brt_ctrl(flash, led_no, ctrl->val);
		g_flashCurrNow = ctrl->val;
		CW8722_LOGI("V4L2_CID_FLASH_TORCH_INTENSITY ctrl->val=%d\n",ctrl->val);
		break;
	}
err_out:
	mutex_unlock(&flash->lock);
	return rval;
}
static int cw8722_led1_get_ctrl(struct v4l2_ctrl *ctrl)
{
	return cw8722_get_ctrl(ctrl, CW8722_LED1);
}
static int cw8722_led1_set_ctrl(struct v4l2_ctrl *ctrl)
{
	return cw8722_set_ctrl(ctrl, CW8722_LED1);
}
static int cw8722_led0_get_ctrl(struct v4l2_ctrl *ctrl)
{
	return cw8722_get_ctrl(ctrl, CW8722_LED0);
}
static int cw8722_led0_set_ctrl(struct v4l2_ctrl *ctrl)
{
	return cw8722_set_ctrl(ctrl, CW8722_LED0);
}
static const struct v4l2_ctrl_ops cw8722_led_ctrl_ops[CW8722_LED_MAX] = {
	[CW8722_LED0] = {
			.g_volatile_ctrl = cw8722_led0_get_ctrl,
			.s_ctrl = cw8722_led0_set_ctrl,
			},
	[CW8722_LED1] = {
			.g_volatile_ctrl = cw8722_led1_get_ctrl,
			.s_ctrl = cw8722_led1_set_ctrl,
			}
};
static int cw8722_init_controls(struct cw8722_flash *flash,
				enum cw8722_led_id led_no)
{
	struct v4l2_ctrl *fault;
	u32 max_flash_brt = flash->pdata->max_flash_brt[led_no];
	u32 max_torch_brt = flash->pdata->max_torch_brt[led_no];
	struct v4l2_ctrl_handler *hdl = &flash->ctrls_led[led_no];
	const struct v4l2_ctrl_ops *ops = &cw8722_led_ctrl_ops[led_no];
	v4l2_ctrl_handler_init(hdl, 8);
	/* flash mode */
	v4l2_ctrl_new_std_menu(hdl, ops, V4L2_CID_FLASH_LED_MODE,
			       V4L2_FLASH_LED_MODE_TORCH, ~0x7,
			       V4L2_FLASH_LED_MODE_NONE);
	flash->led_mode = V4L2_FLASH_LED_MODE_NONE;
	/* flash source */
	v4l2_ctrl_new_std_menu(hdl, ops, V4L2_CID_FLASH_STROBE_SOURCE,
			       0x1, ~0x3, V4L2_FLASH_STROBE_SOURCE_SOFTWARE);
	/* flash strobe */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_STROBE, 0, 0, 0, 0);
	/* flash strobe stop */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_STROBE_STOP, 0, 0, 0, 0);
	/* flash strobe timeout */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_TIMEOUT,
			  CW8722_FLASH_TOUT_MIN,
			  flash->pdata->max_flash_timeout,
			  CW8722_FLASH_TOUT_STEP,
			  flash->pdata->max_flash_timeout);
	/* flash brt */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_INTENSITY,
			  CW8722_FLASH_BRT_MIN, max_flash_brt,
			  CW8722_FLASH_BRT_STEP, max_flash_brt);
	/* torch brt */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_TORCH_INTENSITY,
			  CW8722_TORCH_BRT_MIN, max_torch_brt,
			  CW8722_TORCH_BRT_STEP, max_torch_brt);
	/* fault */
	fault = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_FAULT, 0,
				  V4L2_FLASH_FAULT_OVER_VOLTAGE
				  | V4L2_FLASH_FAULT_OVER_TEMPERATURE
				  | V4L2_FLASH_FAULT_SHORT_CIRCUIT
				  | V4L2_FLASH_FAULT_TIMEOUT, 0, 0);
	if (fault != NULL)
		fault->flags |= V4L2_CTRL_FLAG_VOLATILE;
	if (hdl->error)
		return hdl->error;
	if (led_no < 0 || led_no >= CW8722_LED_MAX) {
		CW8722_LOGI("led_no error\n");
		return -1;
	}
	flash->subdev_led[led_no].ctrl_handler = hdl;
	return 0;
}
/* initialize device */
static const struct v4l2_subdev_ops cw8722_ops = {
	.core = NULL,
};
static const struct regmap_config cw8722_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xFF,
};
static void cw8722_v4l2_i2c_subdev_init(struct v4l2_subdev *sd,
		struct i2c_client *client,
		const struct v4l2_subdev_ops *ops)
{
	int ret = 0;
	v4l2_subdev_init(sd, ops);
	sd->flags |= V4L2_SUBDEV_FL_IS_I2C;
	/* the owner is the same as the i2c_client's driver owner */
	sd->owner = client->dev.driver->owner;
	sd->dev = &client->dev;
	/* i2c_client and v4l2_subdev point to one another */
	v4l2_set_subdevdata(sd, client);
	i2c_set_clientdata(client, sd);
	/* initialize name */
	ret = snprintf(sd->name, sizeof(sd->name), "%s %d-%04x",
		client->dev.driver->name, i2c_adapter_id(client->adapter),
		client->addr);
	if (ret < 0)
		CW8722_LOGI("snprintf failed\n");
}
static int cw8722_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	CW8722_LOGD("In\n");
	if(!cw8722_flash_data){
			CW8722_LOGD("cw8722_open failed---");
		return -1;
	}
	cw8722_set_driver(1);
	CW8722_LOGD("Out\n");
	return 0;
}
static int cw8722_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	CW8722_LOGD("In\n");
	if(!cw8722_flash_data){
			CW8722_LOGD("cw8722_close failed---");
		return -1;
	}
	cw8722_set_driver(0);
	return 0;
}
static const struct v4l2_subdev_internal_ops cw8722_int_ops = {
	.open = cw8722_open,
	.close = cw8722_close,
};
static int cw8722_subdev_init(struct cw8722_flash *flash,
			      enum cw8722_led_id led_no, char *led_name)
{
	struct i2c_client *client = to_i2c_client(flash->dev);
	struct device_node *np = flash->dev->of_node, *child;
	const char *fled_name = CW8722_FLED_NAME;
	int rval;
	if (led_no < 0 || led_no >= CW8722_LED_MAX) {
		CW8722_LOGI("led_no error\n");
		return -1;
	}
	CW8722_LOGD("led_no:[%d]", led_no);
	cw8722_v4l2_i2c_subdev_init(&flash->subdev_led[led_no],
				client, &cw8722_ops);
	flash->subdev_led[led_no].flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	flash->subdev_led[led_no].internal_ops = &cw8722_int_ops;
	strscpy(flash->subdev_led[led_no].name, led_name,
		sizeof(flash->subdev_led[led_no].name));
	for (child = of_get_child_by_name(np, fled_name); child;
			child = of_find_node_by_name(child, fled_name)) {
		int rv;
		u32 reg = 0;
		rv = of_property_read_u32(child, "reg", &reg);
		if (rv)
			continue;
		if (reg == led_no) {
			flash->dnode[led_no] = child;
			flash->subdev_led[led_no].fwnode =
				of_fwnode_handle(flash->dnode[led_no]);
		}
	}
	rval = cw8722_init_controls(flash, led_no);
	if (rval)
		goto err_out;
	rval = media_entity_pads_init(&flash->subdev_led[led_no].entity, 0, NULL);
	if (rval < 0)
		goto err_out;
	flash->subdev_led[led_no].entity.function = MEDIA_ENT_F_FLASH;
	rval = v4l2_async_register_subdev(&flash->subdev_led[led_no]);
	if (rval < 0)
		goto err_out;
	return rval;
err_out:
	v4l2_ctrl_handler_free(&flash->ctrls_led[led_no]);
	return rval;
}
static int cw8722_get_hw_fault(struct cw8722_flash *flash)
{
	int enableregval = 0;
	int flag1 = 0;
	int flag2 = 0;
	regmap_read(flash->regmap, REG_FLAG1, &flag1);
	regmap_read(flash->regmap, REG_FLAG2, &flag2);
	enableregval=flag1+flag2;
	CW8722_LOGI("flag1_0x0A=%d flag2_0x0B =%d enableregval=%d\n",flag1,flag2,enableregval);
	return enableregval;
}
/* flashlight init */
static int cw8722_init(struct cw8722_flash *flash)
{
	int rval = 0;
	unsigned int flag1 = 0;
	unsigned int flag2 = 0;
	CW8722_LOGD("In\n");
	cw8722_pinctrl_set(flash,
			CW8722_PINCTRL_PIN_HWEN, CW8722_PINCTRL_PINSTATE_HIGH);
	/* set timeout */
	rval = cw8722_flash_tout_ctrl(flash, 400);
	if (rval < 0)
		return rval;
	/* output disable */
	flash->led_mode = V4L2_FLASH_LED_MODE_NONE;
	rval = cw8722_mode_ctrl(flash);
	if (rval < 0)
		return rval;
	rval = regmap_update_bits(flash->regmap,
				  REG_LED0_TORCH_BR, 0x80, 0x00);
	if (rval < 0)
		return rval;
	rval = regmap_update_bits(flash->regmap,
				  REG_LED0_FLASH_BR, 0x80, 0x00);
	if (rval < 0)
		return rval;
	rval = regmap_read(flash->regmap, REG_FLAG1, &flag1);
	rval = regmap_read(flash->regmap, REG_FLAG2, &flag2);
	/* reset faults */
	if ((flag1 != 0) || (flag2 != 0)) {
		CW8722_LOGI("REG_FLAG1:[0x%x], REG_FLAG2:[0x%x]\n", flag1, flag2);
		rval = regmap_update_bits(flash->regmap,
				  REG_SW_RESET, 0x80, 0x80);
		mdelay(2);
	}
	CW8722_LOGD("Out\n");
	return rval;
}
/* flashlight uninit */
static int cw8722_uninit(struct cw8722_flash *flash)
{
	CW8722_LOGD("In\n");
	cw8722_get_hw_fault(flash);
	cw8722_pinctrl_set(flash,
			CW8722_PINCTRL_PIN_HWEN, CW8722_PINCTRL_PINSTATE_LOW);
	CW8722_LOGD("Out\n");
	return 0;
}
static int cw8722_flash_open(void)
{
	return 0;
}
static int cw8722_flash_release(void)
{
	/* uninit chip and clear usage count */
	mutex_lock(&cw8722_mutex);
	use_count--;
	if (!use_count)
		cw8722_uninit(cw8722_flash_data);
	if (use_count < 0)
		use_count = 0;
	mutex_unlock(&cw8722_mutex);
	CW8722_LOGI("use_count:[%d]\n", use_count);
	return 0;
}
static int cw8722_ioctl(unsigned int cmd, unsigned long arg)
{
	int channel;
	int curr_uA;
	struct flashlight_dev_arg *fl_arg;
	fl_arg = (struct flashlight_dev_arg *)arg;
	channel = fl_arg->channel;
	switch (cmd) {
	case FLASH_IOC_SET_TIME_OUT_TIME_MS:
		CW8722_LOGD("FLASH_IOC_SET_TIME_OUT_TIME_MS(%d): %d\n",
				channel, (int)fl_arg->arg);
		cw8722_timeout_ms[channel] = fl_arg->arg;
		break;
	case FLASH_IOC_SET_SCENARIO:
		CW8722_LOGD("FLASH_IOC_SET_SCENARIO(%d): %d\n",
				channel, (int)fl_arg->arg);
		cw8722_set_scenario(fl_arg->arg);
		break;
	case FLASH_IOC_SET_CURRENT:
		CW8722_LOGD("FLASH_IOC_SET_CURRENT(%d): %d\n",
				channel, (int)fl_arg->arg);
		curr_uA = (int)fl_arg->arg * 1000;
		{
			if(fl_arg->arg >  CW8722_FLASH_BRT_MIN){
				cw8722_flash_brt_ctrl(cw8722_flash_data, channel, curr_uA);
				cw8722_flash_data->led_mode = V4L2_FLASH_LED_MODE_FLASH;
				cw8722_mode_ctrl(cw8722_flash_data);
			}else{
				g_flashCurrNow = curr_uA;
				cw8722_torch_brt_ctrl(cw8722_flash_data, channel, curr_uA);
				cw8722_flash_data->led_mode = V4L2_FLASH_LED_MODE_TORCH;
				cw8722_mode_ctrl(cw8722_flash_data);
			}
		}
		break;
	case FLASH_IOC_SET_ONOFF:
		CW8722_LOGD("FLASH_IOC_SET_ONOFF(%d): %d\n",
				channel, (int)fl_arg->arg);
		if ((int)fl_arg->arg) {
			cw8722_enable_ctrl(cw8722_flash_data, channel, true);
		} else {
			if (cw8722_flash_data->led_mode != V4L2_FLASH_LED_MODE_NONE) {
				cw8722_flash_data->led_mode = V4L2_FLASH_LED_MODE_NONE;
				cw8722_mode_ctrl(cw8722_flash_data);
				cw8722_enable_ctrl(cw8722_flash_data, channel, false);
			}
		}
		break;
	case FLASH_IOC_GET_HW_FAULT:
		CW8722_LOGI("FLASH_IOC_GET_HW_FAULT(%d)\n", channel);
		fl_arg->arg = cw8722_get_hw_fault(cw8722_flash_data);
		break;
	default:
		CW8722_LOGD("No such command and arg(%d): (%d, %d)\n",
				channel, _IOC_NR(cmd), (int)fl_arg->arg);
		return -ENOTTY;
	}
	return 0;
}
static int cw8722_set_driver(int set)
{
	int ret = 0;
	/* set chip and usage count */
	mutex_lock(&cw8722_mutex);
	if (set) {
		if (!use_count)
			ret = cw8722_init(cw8722_flash_data);
		use_count++;
		CW8722_LOGD("Set driver: %d\n", use_count);
	} else {
		use_count--;
		if (!use_count)
			ret = cw8722_uninit(cw8722_flash_data);
		if (use_count < 0)
			use_count = 0;
		CW8722_LOGD("Unset driver: %d\n", use_count);
	}
	mutex_unlock(&cw8722_mutex);
	return 0;
}
static ssize_t cw8722_strobe_store(struct flashlight_arg arg)
{
	CW8722_LOGD("In\n");
	cw8722_set_driver(1);
	cw8722_torch_brt_ctrl(cw8722_flash_data, arg.channel,
				arg.level * 25000);
	cw8722_enable_ctrl(cw8722_flash_data, arg.channel, true);
	cw8722_flash_data->led_mode = V4L2_FLASH_LED_MODE_TORCH;
	cw8722_mode_ctrl(cw8722_flash_data);
	msleep(arg.dur);
	cw8722_flash_data->led_mode = V4L2_FLASH_LED_MODE_NONE;
	cw8722_mode_ctrl(cw8722_flash_data);
	cw8722_enable_ctrl(cw8722_flash_data, arg.channel, false);
	cw8722_set_driver(0);
	CW8722_LOGD("Out\n");
	return 0;
}
static int cw8722_cooling_get_max_state(struct thermal_cooling_device *cdev,
					unsigned long *state)
{
	struct cw8722_flash *flash = cdev->devdata;
	*state = flash->max_state;
	return 0;
}
static int cw8722_cooling_get_cur_state(struct thermal_cooling_device *cdev,
					unsigned long *state)
{
	struct cw8722_flash *flash = cdev->devdata;
	*state = flash->target_state;
	return 0;
}
static int cw8722_cooling_set_cur_state(struct thermal_cooling_device *cdev,
					unsigned long state)
{
	struct cw8722_flash *flash = cdev->devdata;
	int ret = 0;
	/* Request state should be less than max_state */
	if (state > flash->max_state)
		state = flash->max_state;
	if (state < 0)
		state = 0;
	if (flash->target_state == state)
		return 0;
	flash->target_state = state;
	CW8722_LOGI("set thermal current:%d\n", (int)flash->target_state);
# if 0
	if (flash->target_state == 0) {
		flash->need_cooler = 0;
		flash->target_current = CW8722_FLASH_BRT_MAX;
//		ret = cw8722_torch_brt_ctrl(flash, CW8722_LED0,
//					CW8722_TORCH_BRT_MAX);
//		ret = cw8722_torch_brt_ctrl(flash, CW8722_LED1,
//					CW8722_TORCH_BRT_MAX);
	} else {
		flash->need_cooler = 1;
		if(g_flashCurrNow == 0)
			flash->target_current = flash_state_to_current_limit[flash->target_state - 1];
		else
			flash->target_current = g_flashCurrNow*reduceFlashCurrentmap[flash->target_state - 1]/10;
		ret = cw8722_torch_brt_ctrl(flash, CW8722_LED0,
						flash->target_current);
		ret = cw8722_torch_brt_ctrl(flash, CW8722_LED1,
						flash->target_current);
	}
#endif
	return ret;
}
static struct thermal_cooling_device_ops cw8722_cooling_ops = {
	.get_max_state		= cw8722_cooling_get_max_state,
	.get_cur_state		= cw8722_cooling_get_cur_state,
	.set_cur_state		= cw8722_cooling_set_cur_state,
};
static struct flashlight_operations cw8722_flash_ops = {
	cw8722_flash_open,
	cw8722_flash_release,
	cw8722_ioctl,
	cw8722_strobe_store,
	cw8722_set_driver
};
static int cw8722_parse_dt(struct cw8722_flash *flash)
{
	struct device_node *np, *cnp;
	struct device *dev = flash->dev;
	u32 decouple = 0;
	u32 flashLedNo = 0;
	int i = 0;
	if (!dev || !dev->of_node)
		return -ENODEV;
	np = dev->of_node;
	for_each_child_of_node(np, cnp) {
		if (of_property_read_u32(cnp, "type",
					&flash->flash_dev_id[i].type))
			goto err_node_put;
		if (of_property_read_u32(cnp,
					"ct", &flash->flash_dev_id[i].ct))
			goto err_node_put;
		if (of_property_read_u32(cnp,
					"part", &flash->flash_dev_id[i].part))
			goto err_node_put;
		if (of_property_read_u32(np, "flashledno", &flashLedNo)) {
			snprintf(flash->flash_dev_id[i].name, FLASHLIGHT_NAME_SIZE,
					flash->subdev_led[i].name);
			flash->flash_dev_id[i].channel = i;
		} else {
			CW8722_LOGI("flashledno:[%d]\n", flashLedNo);
			snprintf(flash->flash_dev_id[i].name, FLASHLIGHT_NAME_SIZE,
					flash->subdev_led[flashLedNo].name);
			flash->flash_dev_id[i].channel = flashLedNo;
		}
		flash->flash_dev_id[i].decouple = decouple;
		CW8722_LOGD("Parse dt (type,ct,part,name,channel,decouple)=(%d,%d,%d,%s,%d,%d).\n",
				flash->flash_dev_id[i].type,
				flash->flash_dev_id[i].ct,
				flash->flash_dev_id[i].part,
				flash->flash_dev_id[i].name,
				flash->flash_dev_id[i].channel,
				flash->flash_dev_id[i].decouple);
		if (flashlight_dev_register_by_device_id(&flash->flash_dev_id[i],
			&cw8722_flash_ops))
			return -EFAULT;
		i++;
	}
	return 0;
err_node_put:
	of_node_put(cnp);
	return -EINVAL;
}
static int cw8722_probe(struct i2c_client *client,
			const struct i2c_device_id *devid)
{
	struct cw8722_flash *flash = NULL;
	struct cw8722_platform_data *pdata = dev_get_platdata(&client->dev);
	int rval = 0;
	unsigned int device_id = 0;
	unsigned int chip_id = 0;
	client->addr = CW8722_I2C_ADDR;
	CW8722_LOGD("In\n");
	flash = devm_kzalloc(&client->dev, sizeof(*flash), GFP_KERNEL);
	if (flash == NULL)
		return -ENOMEM;
	flash->regmap = devm_regmap_init_i2c(client, &cw8722_regmap);
	if (IS_ERR(flash->regmap)) {
		rval = PTR_ERR(flash->regmap);
		return rval;
	}
	/* if there is no platform data, use chip default value */
	if (pdata == NULL) {
		pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
		if (pdata == NULL)
			return -ENODEV;
		pdata->max_flash_timeout = CW8722_FLASH_TOUT_MAX;
		/* led 1 */
		pdata->max_flash_brt[CW8722_LED0] = CW8722_FLASH_BRT_MAX;
		pdata->max_torch_brt[CW8722_LED0] = CW8722_TORCH_BRT_MAX;
		/* led 2 */
		pdata->max_flash_brt[CW8722_LED1] = CW8722_FLASH_BRT_MAX;
		pdata->max_torch_brt[CW8722_LED1] = CW8722_TORCH_BRT_MAX;
	}
	flash->pdata = pdata;
	flash->dev = &client->dev;
	mutex_init(&flash->lock);
	cw8722_flash_data = flash;
	rval = cw8722_pinctrl_init(flash);
	if (rval < 0){
		//return rval;
		pr_info("no gpio control");
	}
	cw8722_pinctrl_set(flash,
			CW8722_PINCTRL_PIN_HWEN, CW8722_PINCTRL_PINSTATE_HIGH);
	msleep(3);
	regmap_read(flash->regmap, 0X0C, &device_id);
	regmap_read(flash->regmap, REG_CHIPID, &chip_id);
	CW8722_LOGI("device_id =0x%x chip_id=0x%x\n",device_id,chip_id);
	if (device_id == 0x11) {
		CW8722_LOGI("CW8722 DEVICE_ID is match device_id =0x%x\n",device_id);

		rval = cw8722_subdev_init(flash, CW8722_LED0, "cw8722-led0");
		if (rval < 0)
			return rval;
		rval = cw8722_subdev_init(flash, CW8722_LED1, "cw8722-led1");
		if (rval < 0)
			return rval;
	}else if(chip_id == 0x22){
		CW8722_LOGI("CW8722 CHIP_ID is match chip_id =0x%x\n",chip_id);

		rval = cw8722_subdev_init(flash, CW8722_LED0, "cw8722-led0");
		if (rval < 0)
			return rval;
		rval = cw8722_subdev_init(flash, CW8722_LED1, "cw8722-led1");
		if (rval < 0)
			return rval;
	}else {
		pr_err("CW8722 DEVICE_ID is mismatch device_id =0x%x\n",device_id);
		rval = cw8722_subdev_init(flash, CW8722_LED0, "cw8722-led0-f");
		CW8722_LOGI("device_id cw8722-led0-f\n");
		if (rval < 0)
			return rval;
		rval = cw8722_subdev_init(flash, CW8722_LED1, "cw8722-led1-f");
		CW8722_LOGI("idevice_id cw8722-led1-f\n");
		if (rval < 0)
			return rval;
		cw8722_pinctrl_set(flash,
			CW8722_PINCTRL_PIN_HWEN, CW8722_PINCTRL_PINSTATE_LOW);
		if (!IS_ERR(flash->cw8722_hwen_pinctrl)){
			CW8722_LOGI("flash->cw8722_hwen_pinctrl release---");
			devm_pinctrl_put(flash->cw8722_hwen_pinctrl);
		}
		cw8722_flash_data = NULL;
		return 0;
	}
	rval = cw8722_parse_dt(flash);
	i2c_set_clientdata(client, flash);
	flash->max_state = CW8722_COOLER_MAX_STATE;
	flash->target_state = 0;
	flash->need_cooler = 0;
	flash->target_current = CW8722_FLASH_BRT_MAX;
	flash->ori_current = CW8722_TORCH_BRT_MAX;
	flash->cdev = thermal_of_cooling_device_register(client->dev.of_node,
			"flashlight_cooler", flash, &cw8722_cooling_ops);
	if (IS_ERR(flash->cdev))
		CW8722_LOGI("register thermal failed\n");
	cw8722_pinctrl_set(flash,
			CW8722_PINCTRL_PIN_HWEN, CW8722_PINCTRL_PINSTATE_HIGH);
	//sw reset
	rval = regmap_update_bits(flash->regmap,
				  REG_SW_RESET, 0x80, 0x80);
	mdelay(2);
	regmap_clear_bits(cw8722_flash_data->regmap, REG_ENABLE, 0x7f);
	// rval = regmap_read(flash->regmap, REG_CHIPID, &reg_val);
	// CW8722_LOGD("read chip id:[0x%x]\n",reg_val);
	cw8722_pinctrl_set(flash,
			CW8722_PINCTRL_PIN_HWEN, CW8722_PINCTRL_PINSTATE_LOW);
	return 0;
}
static void cw8722_remove(struct i2c_client *client)
{
	struct cw8722_flash *flash = i2c_get_clientdata(client);
	unsigned int i;
	thermal_cooling_device_unregister(flash->cdev);
	for (i = CW8722_LED0; i < CW8722_LED_MAX; i++) {
		v4l2_device_unregister_subdev(&flash->subdev_led[i]);
		v4l2_ctrl_handler_free(&flash->ctrls_led[i]);
		media_entity_cleanup(&flash->subdev_led[i].entity);
	}
}
static void cw8722_shutdown(struct i2c_client *client)
{
	//cw8722_flash_data->led_mode = 0;
	//cw8722_mode_ctrl(cw8722_flash_data);
	pr_info("cw8722_shutdown IN");
	if(cw8722_flash_data != NULL){
		pr_info("cw8722_shutdown close torch");
		cw8722_enable_ctrl(cw8722_flash_data, 1, false);
	}
	pr_info("cw8722_shutdown OUT");
}
static int __maybe_unused cw8722_suspend(struct device *dev)
{
	unsigned int reg_val;
	struct i2c_client *client = to_i2c_client(dev);
	struct cw8722_flash *flash = i2c_get_clientdata(client);
	regmap_read(flash->regmap, REG_ENABLE, &reg_val);
	CW8722_LOGI("flash status:[0x%x]", reg_val);
	if ((reg_val&0xF) == 0) {
		flash_on = 0;
		return cw8722_uninit(flash);
	} else {
		flash_on = 1;
		CW8722_LOGI("flash on, return\n");
		return 0;
	}
}
static int __maybe_unused cw8722_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cw8722_flash *flash = i2c_get_clientdata(client);
	if (flash_on == 0) {
		return cw8722_init(flash);
	} else {
		CW8722_LOGI("flash on, return\n");
		return 0;
	}
}
static const struct i2c_device_id cw8722_id_table[] = {
	{CW8722_NAME, 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, cw8722_id_table);
static const struct of_device_id cw8722_of_table[] = {
	{ .compatible = "mediatek,cw8722" },
	{ },
};
MODULE_DEVICE_TABLE(of, cw8722_of_table);

static struct i2c_driver cw8722_i2c_driver = {
	.driver = {
		   .name = CW8722_NAME,
		   .of_match_table = cw8722_of_table,
		   },
	.probe = cw8722_probe,
	.remove = cw8722_remove,
	.shutdown = cw8722_shutdown,
	.id_table = cw8722_id_table,
};
module_i2c_driver(cw8722_i2c_driver);
MODULE_DESCRIPTION("Texas Instruments cw8722 flash driver");
MODULE_LICENSE("GPL");
