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
#include <linux/pm_runtime.h>
#include <linux/thermal.h>
#if IS_ENABLED(CONFIG_MTK_FLASHLIGHT)
#include "flashlight-core.h"
#include <linux/power_supply.h>
#endif
#define AWT36515_NAME	"awt36515"
#define AWT36515_I2C_ADDR	(0x63)
#define AWT36515_LOGD(format, args...)\
	pr_debug(AWT36515_NAME "[%s] " format, __func__, ##args)
#define AWT36515_LOGI(format, args...)\
	pr_info(AWT36515_NAME "[%s] " format, __func__, ##args)
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
#define AWT36515_FLASH_BRT_MIN 5000
#define AWT36515_FLASH_BRT_STEP 7830
#define AWT36515_FLASH_BRT_OFFSET 3910
#define AWT36515_FLASH_BRT_MAX 1900000
#define AWT36515_FLASH_BRT_uA_TO_REG(a)	\
	((a) < AWT36515_FLASH_BRT_MIN ? 0 :	\
	 ((((a) - AWT36515_FLASH_BRT_OFFSET) / AWT36515_FLASH_BRT_STEP) & 0xFF))
#define AWT36515_FLASH_BRT_REG_TO_mA(a)		\
	(((a) * AWT36515_FLASH_BRT_STEP + AWT36515_FLASH_BRT_OFFSET) \ 1000)
/*  FLASH TIMEOUT DURATION
 *	min 32ms, step 32ms, max 1024ms
 */
#define AWT36515_FLASH_TOUT_MIN 200
#define AWT36515_FLASH_TOUT_STEP 200
#define AWT36515_FLASH_TOUT_MAX 1600
/*  TORCH BRT
 *	min 10mA, step 1.96mA, max 500mA
 */
#define AWT36515_TORCH_BRT_MIN 10000
#define AWT36515_TORCH_BRT_STEP 1960
#define AWT36515_TORCH_BRT_OFFSET 980
#define AWT36515_TORCH_BRT_MAX 500000
#define AWT36515_TORCH_BRT_uA_TO_REG(a)	\
	((a) < AWT36515_TORCH_BRT_MIN ? 0 :	\
	 ((((a) - AWT36515_TORCH_BRT_OFFSET) / AWT36515_TORCH_BRT_STEP) & 0xFF))
#define AWT36515_TORCH_BRT_REG_TO_uA(a)		\
	(((a) * AWT36515_TORCH_BRT_STEP + AWT36515_TORCH_BRT_OFFSET) \ 1000)
static unsigned int awt36515_timeout_ms[3];
#define AWT36515_COOLER_MAX_STATE 5
static const int flash_state_to_current_limit[AWT36515_COOLER_MAX_STATE] = {
	200000, 150000, 100000, 50000, 25000
};
static const int reduceFlashCurrentmap[AWT36515_COOLER_MAX_STATE] = {
	9, 8, 6, 5, 5
};
extern unsigned int g_sysfs_strobe_level[3];
extern unsigned int g_sysfs_inited[3];
extern unsigned int g_flashCurrNow;
enum awt36515_led_id {
	AWT36515_LED0 = 0,//front_CHANNEL
	AWT36515_LED1,//BACK_CHANNEL
	AWT36515_LED2,//BACK_TELE_CHANNEL
	AWT36515_LED3,//
	AWT36515_LED_MAX
};
/* struct awt36515_platform_data
 *
 * @max_flash_timeout: flash timeout
 * @max_flash_brt: flash mode led brightness
 * @max_torch_brt: torch mode led brightness
 */
struct awt36515_platform_data {
	u32 max_flash_timeout;
	u32 max_flash_brt[AWT36515_LED_MAX];
	u32 max_torch_brt[AWT36515_LED_MAX];
};
enum led_enable {
	MODE_SHDN = 0x00,
	MODE_TORCH = 0x08,
	MODE_FLASH = 0x0C,
};
/**
 * struct awt36515_flash
 *
 * @dev: pointer to &struct device
 * @pdata: platform data
 * @regmap: reg. map for i2c
 * @lock: muxtex for serial access.
 * @led_mode: V4L2 LED mode
 * @ctrls_led: V4L2 controls
 * @subdev_led: V4L2 subdev
 */
struct awt36515_flash {
	struct device *dev;
	struct awt36515_platform_data *pdata;
	struct regmap *regmap;
	struct mutex lock;
	enum v4l2_flash_led_mode led_mode;
	struct v4l2_ctrl_handler ctrls_led[AWT36515_LED_MAX];
	struct v4l2_subdev subdev_led[AWT36515_LED_MAX];
	struct device_node *dnode[AWT36515_LED_MAX];
	struct pinctrl *awt36515_hwen_pinctrl;
	struct pinctrl_state *awt36515_hwen_high;
	struct pinctrl_state *awt36515_hwen_low;
	struct pinctrl_state *awt36515_hwen_high_back;
	struct pinctrl_state *awt36515_hwen_low_back;
	struct pinctrl_state *awt36515_hwen_high_tele;
	struct pinctrl_state *awt36515_hwen_low_tele;
#if IS_ENABLED(CONFIG_MTK_FLASHLIGHT)
	struct flashlight_device_id flash_dev_id[AWT36515_LED_MAX];
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
static struct awt36515_flash *awt36515_flash_data;
static DEFINE_MUTEX(awt36515_mutex);
#define to_awt36515_flash(_ctrl, _no)	\
	container_of(_ctrl->handler, struct awt36515_flash, ctrls_led[_no])
/* define pinctrl */
#define AWT36515_PINCTRL_PIN_HWEN 0
#define AWT36515_PINCTRL_PINSTATE_LOW 0
#define AWT36515_PINCTRL_PINSTATE_HIGH 1
#define AWT36515_PINCTRL_STATE_HWEN_HIGH "hwen_high"
#define AWT36515_PINCTRL_STATE_HWEN_LOW  "hwen_low"
#define AWT36515_PINCTRL_PIN_HWEN_BACK 1
#define AWT36515_PINCTRL_PIN_HWEN_TELE 2
#define AWT36515_PINCTRL_STATE_HWEN_HIGH_BACK "back_hwen_high"
#define AWT36515_PINCTRL_STATE_HWEN_LOW_BACK  "back_hwen_low"
#define AWT36515_PINCTRL_STATE_HWEN_HIGH_TELE "tele_hwen_high"
#define AWT36515_PINCTRL_STATE_HWEN_LOW_TELE  "tele_hwen_low"
/******************************************************************************
 * Pinctrl configuration
 *****************************************************************************/
static int awt36515_pinctrl_init(struct awt36515_flash *flash)
{
	int ret = 0;
	/* get pinctrl */
	flash->awt36515_hwen_pinctrl = devm_pinctrl_get(flash->dev);
	if (IS_ERR(flash->awt36515_hwen_pinctrl)) {
		AWT36515_LOGI("Failed to get flashlight pinctrl.\n");
		ret = PTR_ERR(flash->awt36515_hwen_pinctrl);
		return ret;
	}
	/* Flashlight HWEN pin initialization */
    //enable
	flash->awt36515_hwen_high = pinctrl_lookup_state(
			flash->awt36515_hwen_pinctrl,
			AWT36515_PINCTRL_STATE_HWEN_HIGH);
	if (IS_ERR(flash->awt36515_hwen_high)) {
		AWT36515_LOGI("Failed to init (%s)\n",
			AWT36515_PINCTRL_STATE_HWEN_HIGH);
		ret = PTR_ERR(flash->awt36515_hwen_high);
	}
	flash->awt36515_hwen_low = pinctrl_lookup_state(
			flash->awt36515_hwen_pinctrl,
			AWT36515_PINCTRL_STATE_HWEN_LOW);
	if (IS_ERR(flash->awt36515_hwen_low)) {
		AWT36515_LOGI("Failed to init (%s)\n", AWT36515_PINCTRL_STATE_HWEN_LOW);
		ret = PTR_ERR(flash->awt36515_hwen_low);
	}
    //back
	flash->awt36515_hwen_high_back = pinctrl_lookup_state(flash->awt36515_hwen_pinctrl,AWT36515_PINCTRL_STATE_HWEN_HIGH_BACK);
	if (IS_ERR(flash->awt36515_hwen_high_back)) {
		AWT36515_LOGI("Failed to init (%s)\n",
			AWT36515_PINCTRL_STATE_HWEN_HIGH_BACK);
		ret = PTR_ERR(flash->awt36515_hwen_high_back);
	}
	flash->awt36515_hwen_low_back = pinctrl_lookup_state(flash->awt36515_hwen_pinctrl,AWT36515_PINCTRL_STATE_HWEN_LOW_BACK);
	if (IS_ERR(flash->awt36515_hwen_low_back)) {
		AWT36515_LOGI("Failed to init (%s)\n", AWT36515_PINCTRL_STATE_HWEN_LOW_BACK);
		ret = PTR_ERR(flash->awt36515_hwen_low_back);
	}
    //tele
	flash->awt36515_hwen_high_tele = pinctrl_lookup_state(flash->awt36515_hwen_pinctrl,AWT36515_PINCTRL_STATE_HWEN_HIGH_TELE);
	if (IS_ERR(flash->awt36515_hwen_high_tele)) {
		AWT36515_LOGI("Failed to init (%s)\n",
			AWT36515_PINCTRL_STATE_HWEN_HIGH_TELE);
		ret = PTR_ERR(flash->awt36515_hwen_high_tele);
	}
	flash->awt36515_hwen_low_tele = pinctrl_lookup_state(flash->awt36515_hwen_pinctrl,AWT36515_PINCTRL_STATE_HWEN_LOW_TELE);
	if (IS_ERR(flash->awt36515_hwen_low_tele)) {
		AWT36515_LOGI("Failed to init (%s)\n", AWT36515_PINCTRL_STATE_HWEN_LOW_TELE);
		ret = PTR_ERR(flash->awt36515_hwen_low_tele);
	}
	return ret;
}
static int awt36515_pinctrl_set(struct awt36515_flash *flash, int pin, int state)
{
	int ret = 0;
	if (IS_ERR(flash->awt36515_hwen_pinctrl)) {
		AWT36515_LOGI("pinctrl is not available\n");
		return -1;
	}
	switch (pin) {
	case AWT36515_PINCTRL_PIN_HWEN://enable
		if (state == AWT36515_PINCTRL_PINSTATE_LOW &&
				!IS_ERR(flash->awt36515_hwen_low))
			pinctrl_select_state(flash->awt36515_hwen_pinctrl,
					flash->awt36515_hwen_low);
		else if (state == AWT36515_PINCTRL_PINSTATE_HIGH &&
				!IS_ERR(flash->awt36515_hwen_high))
			pinctrl_select_state(flash->awt36515_hwen_pinctrl,
					flash->awt36515_hwen_high);
		else
			AWT36515_LOGD("set err, pin(%d) state(%d)\n", pin, state);
		break;
	case AWT36515_PINCTRL_PIN_HWEN_BACK://back
		if (state == AWT36515_PINCTRL_PINSTATE_LOW &&
				!IS_ERR(flash->awt36515_hwen_low_back))
			pinctrl_select_state(flash->awt36515_hwen_pinctrl,
					flash->awt36515_hwen_low_back);
		else if (state == AWT36515_PINCTRL_PINSTATE_HIGH &&
				!IS_ERR(flash->awt36515_hwen_high_back))
			pinctrl_select_state(flash->awt36515_hwen_pinctrl,
					flash->awt36515_hwen_high_back);
		else
			AWT36515_LOGD("set err, pin(%d) state(%d)\n", pin, state);
		break;
	case AWT36515_PINCTRL_PIN_HWEN_TELE://tele
		if (state == AWT36515_PINCTRL_PINSTATE_LOW &&
				!IS_ERR(flash->awt36515_hwen_low_tele))
			pinctrl_select_state(flash->awt36515_hwen_pinctrl,
					flash->awt36515_hwen_low_tele);
		else if (state == AWT36515_PINCTRL_PINSTATE_HIGH &&
				!IS_ERR(flash->awt36515_hwen_high_tele))
			pinctrl_select_state(flash->awt36515_hwen_pinctrl,
					flash->awt36515_hwen_high_tele);
		else
			AWT36515_LOGD("set err, pin(%d) state(%d)\n", pin, state);
		break;
	default:
		AWT36515_LOGD("set err, pin(%d) state(%d)\n", pin, state);
		break;
	}
	return ret;
}
static int awt36515_set_scenario(int scenario)
{
	/* set decouple mode */
	return 0;
}
/* enable mode control */
static int awt36515_mode_ctrl_tran(struct awt36515_flash *flash,enum awt36515_led_id led_no)
{
	int rval = -EINVAL;
	AWT36515_LOGI("mode:[%d] led_no=%d", flash->led_mode,led_no);
	switch (flash->led_mode) {
	case V4L2_FLASH_LED_MODE_NONE:
		if (led_no == AWT36515_LED0) {
			rval = regmap_update_bits(flash->regmap,
						  REG_ENABLE, 0x01, 0x00);
		} else {
			rval = regmap_update_bits(flash->regmap,
						  REG_ENABLE, 0x02, 0x00);
		}
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
static int awt36515_mode_ctrl(struct awt36515_flash *flash)
{
	int rval = -EINVAL;
	AWT36515_LOGI("mode:[%d]", flash->led_mode);
	switch (flash->led_mode) {
	case V4L2_FLASH_LED_MODE_NONE:
		rval = regmap_update_bits(flash->regmap,
					  REG_ENABLE, 0x0C, MODE_SHDN);
		regmap_clear_bits(awt36515_flash_data->regmap, REG_ENABLE, 0x7f);
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
static int awt36515_enable_ctrl(struct awt36515_flash *flash,
			      enum awt36515_led_id led_no, bool on)
{
	int rval;
	unsigned int reg_val=0;
	AWT36515_LOGI("led_no:[%d] enable:[%d]", led_no, on);
	flashlight_kicker_pbm(on);
    if(led_no == AWT36515_LED2 && on){
        awt36515_pinctrl_set(flash,
                AWT36515_PINCTRL_PIN_HWEN_BACK, AWT36515_PINCTRL_PINSTATE_LOW);
        awt36515_pinctrl_set(flash,
                AWT36515_PINCTRL_PIN_HWEN_TELE, AWT36515_PINCTRL_PINSTATE_HIGH);
    }else if(led_no == AWT36515_LED1 && on){
        awt36515_pinctrl_set(flash,
                AWT36515_PINCTRL_PIN_HWEN_BACK, AWT36515_PINCTRL_PINSTATE_HIGH);
        awt36515_pinctrl_set(flash,
                AWT36515_PINCTRL_PIN_HWEN_TELE, AWT36515_PINCTRL_PINSTATE_LOW);
    }
	AWT36515_LOGD("reg_val_before=0x%x", reg_val);
	if (led_no == AWT36515_LED0) {
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
	AWT36515_LOGD("reg_val:[0x%x]", reg_val);
	return rval;
}
/* torch1/2 brightness control */
static int awt36515_torch_brt_ctrl(struct awt36515_flash *flash,
				 enum awt36515_led_id led_no, unsigned int brt)
{
	int rval;
	u8 br_bits;
	AWT36515_LOGI("led_no:[%d] brt:[%u]", led_no, brt);
	if (brt < AWT36515_TORCH_BRT_MIN)
		return awt36515_enable_ctrl(flash, led_no, false);
#if 0
	if (flash->need_cooler == 0) {
		flash->ori_current = brt;
	} else {
		if (brt > flash->target_current) {
			brt = flash->target_current;
			AWT36515_LOGI("thermal limit current:%d\n", brt);
		}
	}
#endif
	br_bits = AWT36515_TORCH_BRT_uA_TO_REG(brt);
	if (led_no == AWT36515_LED0)
		rval = regmap_update_bits(flash->regmap,
					  REG_LED0_TORCH_BR, 0xFF, br_bits);
	else
		rval = regmap_update_bits(flash->regmap,
					  REG_LED1_TORCH_BR, 0xFF, br_bits);
	return rval;
}
/* flash1/2 brightness control */
static int awt36515_flash_brt_ctrl(struct awt36515_flash *flash,
				 enum awt36515_led_id led_no, unsigned int brt)
{
	int rval;
	u8 br_bits;
	AWT36515_LOGI("led_no:[%d] brt:[%u]", led_no, brt);
	if (brt < AWT36515_FLASH_BRT_MIN)
		return awt36515_enable_ctrl(flash, led_no, false);
#if 0
	if (flash->need_cooler == 1 && brt > flash->target_current) {
		brt = flash->target_current;
		AWT36515_LOGD("thermal limit current:%d\n", brt);
	}
#endif
	br_bits = AWT36515_FLASH_BRT_uA_TO_REG(brt);
	if (led_no == AWT36515_LED0)
		rval = regmap_update_bits(flash->regmap,
					  REG_LED0_FLASH_BR, 0xFF, br_bits);
	else
		rval = regmap_update_bits(flash->regmap,
					  REG_LED1_FLASH_BR, 0xFF, br_bits);
	return rval;
}
/* flash1/2 timeout control */
static int awt36515_flash_tout_ctrl(struct awt36515_flash *flash,
				unsigned int tout)
{
	int rval;
	u8 tout_bits;
	AWT36515_LOGD("tout:[%u]", tout);
	if (tout == 200)
		tout_bits = 0x04;
	else
		tout_bits = 0x07 + (tout / AWT36515_FLASH_TOUT_STEP);
	rval = regmap_update_bits(flash->regmap,
				  REG_FLASH_TOUT, 0x1F, tout_bits);
	return rval;
}
/* v4l2 controls  */
static int awt36515_get_ctrl(struct v4l2_ctrl *ctrl, enum awt36515_led_id led_no)
{
	struct awt36515_flash *flash = to_awt36515_flash(ctrl, led_no);
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
static int awt36515_set_ctrl(struct v4l2_ctrl *ctrl, enum awt36515_led_id led_no)
{
	struct awt36515_flash *flash = to_awt36515_flash(ctrl, led_no);
	int rval = -EINVAL;
	AWT36515_LOGD("led:[%d] ID:[%d]", led_no, ctrl->id);
	mutex_lock(&flash->lock);
	switch (ctrl->id) {
	case V4L2_CID_FLASH_LED_MODE:
		flash->led_mode = ctrl->val;
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH)
			rval = awt36515_mode_ctrl_tran(flash,led_no);
		else
			rval = 0;
		if (flash->led_mode == V4L2_FLASH_LED_MODE_NONE){
			if(led_no == 1) {
				g_sysfs_strobe_level[0]= 0;
				g_sysfs_inited[0]=0;
			}else if(led_no == 2){
				g_sysfs_strobe_level[2]= 0;
				g_sysfs_inited[2]=0;
            }
            else {
				g_sysfs_strobe_level[1]= 0;
				g_sysfs_inited[1]=0;
			}
			awt36515_enable_ctrl(flash, led_no, false);
		}else if (flash->led_mode == V4L2_FLASH_LED_MODE_TORCH) {
			if(led_no == 1) {
				g_sysfs_strobe_level[0]= 2;
				g_sysfs_inited[0]=1;
			}else if(led_no == 2){
				g_sysfs_strobe_level[2]= 2;
				g_sysfs_inited[2]=1;
            }
            else {
				g_sysfs_strobe_level[1]= 2;
				g_sysfs_inited[1]=1;
			}
			rval = awt36515_enable_ctrl(flash, led_no, true);
		}
		break;
	case V4L2_CID_FLASH_STROBE_SOURCE:
		if (ctrl->val == V4L2_FLASH_STROBE_SOURCE_SOFTWARE) {
			AWT36515_LOGD("sw ctrl\n");
			rval = regmap_update_bits(flash->regmap,
					REG_ENABLE, 0x2C, 0x0C);
		} else if (ctrl->val == V4L2_FLASH_STROBE_SOURCE_EXTERNAL) {
			AWT36515_LOGD("hw trigger\n");
			rval = regmap_update_bits(flash->regmap,
					REG_ENABLE, 0x2C, 0x20);
			rval = awt36515_enable_ctrl(flash, led_no, true);
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
		rval = awt36515_mode_ctrl(flash);
		rval = awt36515_enable_ctrl(flash, led_no, true);
		break;
	case V4L2_CID_FLASH_STROBE_STOP:
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH) {
			rval = -EBUSY;
			goto err_out;
		}
		awt36515_enable_ctrl(flash, led_no, false);
		flash->led_mode = V4L2_FLASH_LED_MODE_NONE;
		rval = awt36515_mode_ctrl(flash);
		break;
	case V4L2_CID_FLASH_TIMEOUT:
		rval = awt36515_flash_tout_ctrl(flash, ctrl->val);
		break;
	case V4L2_CID_FLASH_INTENSITY:
		rval = awt36515_flash_brt_ctrl(flash, led_no, ctrl->val);
		break;
	case V4L2_CID_FLASH_TORCH_INTENSITY:
		rval = awt36515_torch_brt_ctrl(flash, led_no, ctrl->val);
		g_flashCurrNow = ctrl->val;
		AWT36515_LOGI("V4L2_CID_FLASH_TORCH_INTENSITY ctrl->val=%d\n",ctrl->val);
		break;
	}
err_out:
	mutex_unlock(&flash->lock);
	return rval;
}
static int awt36515_led3_get_ctrl(struct v4l2_ctrl *ctrl)
{
	return awt36515_get_ctrl(ctrl, AWT36515_LED3);
}
static int awt36515_led3_set_ctrl(struct v4l2_ctrl *ctrl)
{
	return awt36515_set_ctrl(ctrl, AWT36515_LED3);
}
static int awt36515_led2_get_ctrl(struct v4l2_ctrl *ctrl)
{
	return awt36515_get_ctrl(ctrl, AWT36515_LED2);
}
static int awt36515_led2_set_ctrl(struct v4l2_ctrl *ctrl)
{
	return awt36515_set_ctrl(ctrl, AWT36515_LED2);
}
static int awt36515_led1_get_ctrl(struct v4l2_ctrl *ctrl)
{
	return awt36515_get_ctrl(ctrl, AWT36515_LED1);
}
static int awt36515_led1_set_ctrl(struct v4l2_ctrl *ctrl)
{
	return awt36515_set_ctrl(ctrl, AWT36515_LED1);
}
static int awt36515_led0_get_ctrl(struct v4l2_ctrl *ctrl)
{
	return awt36515_get_ctrl(ctrl, AWT36515_LED0);
}
static int awt36515_led0_set_ctrl(struct v4l2_ctrl *ctrl)
{
	return awt36515_set_ctrl(ctrl, AWT36515_LED0);
}
static const struct v4l2_ctrl_ops awt36515_led_ctrl_ops[AWT36515_LED_MAX] = {
	[AWT36515_LED0] = {
			.g_volatile_ctrl = awt36515_led0_get_ctrl,
			.s_ctrl = awt36515_led0_set_ctrl,
			},
	[AWT36515_LED1] = {
			.g_volatile_ctrl = awt36515_led1_get_ctrl,
			.s_ctrl = awt36515_led1_set_ctrl,
			},
	[AWT36515_LED2] = {
			.g_volatile_ctrl = awt36515_led2_get_ctrl,
			.s_ctrl = awt36515_led2_set_ctrl,
			},
	[AWT36515_LED3] = {
			.g_volatile_ctrl = awt36515_led3_get_ctrl,
			.s_ctrl = awt36515_led3_set_ctrl,
			}   
};
static int awt36515_init_controls(struct awt36515_flash *flash,
				enum awt36515_led_id led_no)
{
	struct v4l2_ctrl *fault;
	u32 max_flash_brt = flash->pdata->max_flash_brt[led_no];
	u32 max_torch_brt = flash->pdata->max_torch_brt[led_no];
	struct v4l2_ctrl_handler *hdl = &flash->ctrls_led[led_no];
	const struct v4l2_ctrl_ops *ops = &awt36515_led_ctrl_ops[led_no];
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
			  AWT36515_FLASH_TOUT_MIN,
			  flash->pdata->max_flash_timeout,
			  AWT36515_FLASH_TOUT_STEP,
			  flash->pdata->max_flash_timeout);
	/* flash brt */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_INTENSITY,
			  AWT36515_FLASH_BRT_MIN, max_flash_brt,
			  AWT36515_FLASH_BRT_STEP, max_flash_brt);
	/* torch brt */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_TORCH_INTENSITY,
			  AWT36515_TORCH_BRT_MIN, max_torch_brt,
			  AWT36515_TORCH_BRT_STEP, max_torch_brt);
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
	if (led_no < 0 || led_no >= AWT36515_LED_MAX) {
		AWT36515_LOGI("led_no error\n");
		return -1;
	}
	flash->subdev_led[led_no].ctrl_handler = hdl;
	return 0;
}
/* initialize device */
static const struct v4l2_subdev_ops awt36515_ops = {
	.core = NULL,
};
static const struct regmap_config awt36515_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xFF,
};
static void awt36515_v4l2_i2c_subdev_init(struct v4l2_subdev *sd,
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
		AWT36515_LOGI("snprintf failed\n");
}
static int awt36515_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	int ret;
	AWT36515_LOGD("In\n");
	ret = pm_runtime_get_sync(sd->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(sd->dev);
		return ret;
	}
	AWT36515_LOGD("Out\n");
	return 0;
}
static int awt36515_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	AWT36515_LOGD("In\n");
	pm_runtime_put(sd->dev);
	return 0;
}
static const struct v4l2_subdev_internal_ops awt36515_int_ops = {
	.open = awt36515_open,
	.close = awt36515_close,
};
static int awt36515_subdev_init(struct awt36515_flash *flash,
			      enum awt36515_led_id led_no, char *led_name)
{
	struct i2c_client *client = to_i2c_client(flash->dev);
	struct device_node *np = flash->dev->of_node, *child;
	const char *fled_name = "flash";
	int rval;
	if (led_no < 0 || led_no >= AWT36515_LED_MAX) {
		AWT36515_LOGI("led_no error\n");
		return -1;
	}
	AWT36515_LOGD("led_no:[%d]", led_no);
	awt36515_v4l2_i2c_subdev_init(&flash->subdev_led[led_no],
				client, &awt36515_ops);
	flash->subdev_led[led_no].flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	flash->subdev_led[led_no].internal_ops = &awt36515_int_ops;
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
	rval = awt36515_init_controls(flash, led_no);
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
/* flashlight init */
static int awt36515_init(struct awt36515_flash *flash)
{
	int rval = 0;
	unsigned int flag1 = 0;
	unsigned int flag2 = 0;
	AWT36515_LOGD("In\n");
	awt36515_pinctrl_set(flash,
			AWT36515_PINCTRL_PIN_HWEN, AWT36515_PINCTRL_PINSTATE_HIGH);
	awt36515_pinctrl_set(flash,
			AWT36515_PINCTRL_PIN_HWEN_BACK, AWT36515_PINCTRL_PINSTATE_HIGH);
	awt36515_pinctrl_set(flash,
			AWT36515_PINCTRL_PIN_HWEN_TELE, AWT36515_PINCTRL_PINSTATE_LOW);
	/* set timeout */
	rval = awt36515_flash_tout_ctrl(flash, 400);
	if (rval < 0)
		return rval;
	/* output disable */
	flash->led_mode = V4L2_FLASH_LED_MODE_NONE;
	rval = awt36515_mode_ctrl(flash);
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
		AWT36515_LOGI("REG_FLAG1:[0x%x], REG_FLAG2:[0x%x]\n", flag1, flag2);
		rval = regmap_update_bits(flash->regmap,
				  REG_SW_RESET, 0x80, 0x80);
		mdelay(2);
	}
	AWT36515_LOGD("Out\n");
	return rval;
}
/* flashlight uninit */
static int awt36515_uninit(struct awt36515_flash *flash)
{
	AWT36515_LOGD("In\n");
	awt36515_pinctrl_set(flash,
			AWT36515_PINCTRL_PIN_HWEN, AWT36515_PINCTRL_PINSTATE_LOW);
	awt36515_pinctrl_set(flash,
			AWT36515_PINCTRL_PIN_HWEN_BACK, AWT36515_PINCTRL_PINSTATE_LOW);
	awt36515_pinctrl_set(flash,
			AWT36515_PINCTRL_PIN_HWEN_TELE, AWT36515_PINCTRL_PINSTATE_LOW);
	AWT36515_LOGD("Out\n");
	return 0;
}
static int awt36515_flash_open(void)
{
	return 0;
}
static int awt36515_flash_release(void)
{
	/* uninit chip and clear usage count */
/*	mutex_lock(&awt36515_mutex);
	use_count--;
	if (!use_count)
		awt36515_uninit(awt36515_flash_data);
	if (use_count < 0)
		use_count = 0;
	mutex_unlock(&awt36515_mutex);*/
	AWT36515_LOGI("use_count:[%d]\n", use_count);
	return 0;
}
static int awt36515_ioctl(unsigned int cmd, unsigned long arg)
{
	int channel;
	int curr_uA;
	struct flashlight_dev_arg *fl_arg;
	fl_arg = (struct flashlight_dev_arg *)arg;
	channel = fl_arg->channel;
	switch (cmd) {
	case FLASH_IOC_SET_TIME_OUT_TIME_MS:
		AWT36515_LOGD("FLASH_IOC_SET_TIME_OUT_TIME_MS(%d): %d\n",
				channel, (int)fl_arg->arg);
		awt36515_timeout_ms[channel] = fl_arg->arg;
		break;
	case FLASH_IOC_SET_SCENARIO:
		AWT36515_LOGD("FLASH_IOC_SET_SCENARIO(%d): %d\n",
				channel, (int)fl_arg->arg);
		awt36515_set_scenario(fl_arg->arg);
		break;
	case FLASH_IOC_SET_CURRENT:
		AWT36515_LOGD("FLASH_IOC_SET_CURRENT(%d): %d\n",
				channel, (int)fl_arg->arg);
		curr_uA = (int)fl_arg->arg * 1000;
		{
			if(fl_arg->arg >  AWT36515_FLASH_BRT_MIN){
				awt36515_flash_brt_ctrl(awt36515_flash_data, channel, curr_uA);
				awt36515_flash_data->led_mode = V4L2_FLASH_LED_MODE_FLASH;
				awt36515_mode_ctrl(awt36515_flash_data);
			}else{
				g_flashCurrNow = curr_uA;
				awt36515_torch_brt_ctrl(awt36515_flash_data, channel, curr_uA);
				awt36515_flash_data->led_mode = V4L2_FLASH_LED_MODE_TORCH;
				awt36515_mode_ctrl(awt36515_flash_data);
			}
		}
		break;
	case FLASH_IOC_SET_ONOFF:
		AWT36515_LOGD("FLASH_IOC_SET_ONOFF(%d): %d\n",
				channel, (int)fl_arg->arg);
		if ((int)fl_arg->arg) {
			awt36515_enable_ctrl(awt36515_flash_data, channel, true);
		} else {
			if (awt36515_flash_data->led_mode != V4L2_FLASH_LED_MODE_NONE) {
				awt36515_flash_data->led_mode = V4L2_FLASH_LED_MODE_NONE;
				awt36515_mode_ctrl(awt36515_flash_data);
				awt36515_enable_ctrl(awt36515_flash_data, channel, false);
			}
		}
		break;
	default:
		AWT36515_LOGD("No such command and arg(%d): (%d, %d)\n",
				channel, _IOC_NR(cmd), (int)fl_arg->arg);
		return -ENOTTY;
	}
	return 0;
}
static int awt36515_set_driver(int set)
{
	int ret = 0;
	/* set chip and usage count */
	//mutex_lock(&awt36515_mutex);
	if (set) {
		if (!use_count)
			ret = awt36515_init(awt36515_flash_data);
		use_count++;
		AWT36515_LOGD("Set driver: %d\n", use_count);
	} else {
		use_count--;
		if (!use_count)
			ret = awt36515_uninit(awt36515_flash_data);
		if (use_count < 0)
			use_count = 0;
		AWT36515_LOGD("Unset driver: %d\n", use_count);
	}
	//mutex_unlock(&awt36515_mutex);
	return 0;
}
static ssize_t awt36515_strobe_store(struct flashlight_arg arg)
{
	AWT36515_LOGD("In\n");
	awt36515_set_driver(1);
	awt36515_torch_brt_ctrl(awt36515_flash_data, arg.channel,
				arg.level * 25000);
	awt36515_enable_ctrl(awt36515_flash_data, arg.channel, true);
	awt36515_flash_data->led_mode = V4L2_FLASH_LED_MODE_TORCH;
	awt36515_mode_ctrl(awt36515_flash_data);
	msleep(arg.dur);
	awt36515_flash_data->led_mode = V4L2_FLASH_LED_MODE_NONE;
	awt36515_mode_ctrl(awt36515_flash_data);
	awt36515_enable_ctrl(awt36515_flash_data, arg.channel, false);
	awt36515_set_driver(0);
	AWT36515_LOGD("Out\n");
	return 0;
}
static int awt36515_cooling_get_max_state(struct thermal_cooling_device *cdev,
					unsigned long *state)
{
	struct awt36515_flash *flash = cdev->devdata;
	*state = flash->max_state;
	return 0;
}
static int awt36515_cooling_get_cur_state(struct thermal_cooling_device *cdev,
					unsigned long *state)
{
	struct awt36515_flash *flash = cdev->devdata;
	*state = flash->target_state;
	return 0;
}
static int awt36515_cooling_set_cur_state(struct thermal_cooling_device *cdev,
					unsigned long state)
{
	struct awt36515_flash *flash = cdev->devdata;
	int ret = 0;
	/* Request state should be less than max_state */
	if (state > flash->max_state)
		state = flash->max_state;
	if (state < 0)
		state = 0;
	if (flash->target_state == state)
		return 0;
	flash->target_state = state;
	AWT36515_LOGI("set thermal current:%d\n", (int)flash->target_state);
# if 0
	if (flash->target_state == 0) {
		flash->need_cooler = 0;
		flash->target_current = AWT36515_FLASH_BRT_MAX;
//		ret = awt36515_torch_brt_ctrl(flash, AWT36515_LED0,
//					AWT36515_TORCH_BRT_MAX);
//		ret = awt36515_torch_brt_ctrl(flash, AWT36515_LED1,
//					AWT36515_TORCH_BRT_MAX);
	} else {
		flash->need_cooler = 1;
		if(g_flashCurrNow == 0)
			flash->target_current = flash_state_to_current_limit[flash->target_state - 1];
		else
			flash->target_current = g_flashCurrNow*reduceFlashCurrentmap[flash->target_state - 1]/10;
		ret = awt36515_torch_brt_ctrl(flash, AWT36515_LED0,
						flash->target_current);
		ret = awt36515_torch_brt_ctrl(flash, AWT36515_LED1,
						flash->target_current);
	}
#endif
	return ret;
}
static struct thermal_cooling_device_ops awt36515_cooling_ops = {
	.get_max_state		= awt36515_cooling_get_max_state,
	.get_cur_state		= awt36515_cooling_get_cur_state,
	.set_cur_state		= awt36515_cooling_set_cur_state,
};
static struct flashlight_operations awt36515_flash_ops = {
	awt36515_flash_open,
	awt36515_flash_release,
	awt36515_ioctl,
	awt36515_strobe_store,
	awt36515_set_driver
};
static int awt36515_parse_dt(struct awt36515_flash *flash)
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
			AWT36515_LOGI("flashledno:[%d]\n", flashLedNo);
			snprintf(flash->flash_dev_id[i].name, FLASHLIGHT_NAME_SIZE,
					flash->subdev_led[flashLedNo].name);
			flash->flash_dev_id[i].channel = flashLedNo;
		}
		flash->flash_dev_id[i].decouple = decouple;
		AWT36515_LOGD("Parse dt (type,ct,part,name,channel,decouple)=(%d,%d,%d,%s,%d,%d).\n",
				flash->flash_dev_id[i].type,
				flash->flash_dev_id[i].ct,
				flash->flash_dev_id[i].part,
				flash->flash_dev_id[i].name,
				flash->flash_dev_id[i].channel,
				flash->flash_dev_id[i].decouple);
		if (flashlight_dev_register_by_device_id(&flash->flash_dev_id[i],
			&awt36515_flash_ops))
			return -EFAULT;
		i++;
	}
	return 0;
err_node_put:
	of_node_put(cnp);
	return -EINVAL;
}
static int awt36515_probe(struct i2c_client *client,
			const struct i2c_device_id *devid)
{
	struct awt36515_flash *flash = NULL;
	struct awt36515_platform_data *pdata = dev_get_platdata(&client->dev);
	int rval = 0;
	unsigned int reg_val = 0;
	AWT36515_LOGD("In\n");
	flash = devm_kzalloc(&client->dev, sizeof(*flash), GFP_KERNEL);
	if (flash == NULL)
		return -ENOMEM;
	flash->regmap = devm_regmap_init_i2c(client, &awt36515_regmap);
	if (IS_ERR(flash->regmap)) {
		rval = PTR_ERR(flash->regmap);
		return rval;
	}
	/* if there is no platform data, use chip default value */
	if (pdata == NULL) {
		pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
		if (pdata == NULL)
			return -ENODEV;
		pdata->max_flash_timeout = AWT36515_FLASH_TOUT_MAX;
		/* led 0 */
		pdata->max_flash_brt[AWT36515_LED0] = AWT36515_FLASH_BRT_MAX;
		pdata->max_torch_brt[AWT36515_LED0] = AWT36515_TORCH_BRT_MAX;
		/* led 1 */
		pdata->max_flash_brt[AWT36515_LED1] = AWT36515_FLASH_BRT_MAX;
		pdata->max_torch_brt[AWT36515_LED1] = AWT36515_TORCH_BRT_MAX;
		/* led 2 */
		pdata->max_flash_brt[AWT36515_LED2] = AWT36515_FLASH_BRT_MAX;
		pdata->max_torch_brt[AWT36515_LED2] = AWT36515_TORCH_BRT_MAX;
		/* led 3 */
		pdata->max_flash_brt[AWT36515_LED3] = AWT36515_FLASH_BRT_MAX;
		pdata->max_torch_brt[AWT36515_LED3] = AWT36515_TORCH_BRT_MAX;
	}
	flash->pdata = pdata;
	flash->dev = &client->dev;
	mutex_init(&flash->lock);
	awt36515_flash_data = flash;
	use_count = 0;
	rval = awt36515_pinctrl_init(flash);
	if (rval < 0){
		//return rval;
		pr_info("no gpio control");
	}
	rval = awt36515_subdev_init(flash, AWT36515_LED0, "awt36515-led0");
	if (rval < 0)
		return rval;
	rval = awt36515_subdev_init(flash, AWT36515_LED1, "awt36515-led1");
	if (rval < 0)
		return rval;
	rval = awt36515_subdev_init(flash, AWT36515_LED2, "awt36515-led2");
	if (rval < 0)
		return rval;
	rval = awt36515_subdev_init(flash, AWT36515_LED3, "awt36515-led3");
	if (rval < 0)
		return rval;
	pm_runtime_enable(flash->dev);
	rval = awt36515_parse_dt(flash);
	i2c_set_clientdata(client, flash);
	flash->max_state = AWT36515_COOLER_MAX_STATE;
	flash->target_state = 0;
	flash->need_cooler = 0;
	flash->target_current = AWT36515_FLASH_BRT_MAX;
	flash->ori_current = AWT36515_TORCH_BRT_MAX;
	flash->cdev = thermal_of_cooling_device_register(client->dev.of_node,
			"flashlight_cooler", flash, &awt36515_cooling_ops);
	if (IS_ERR(flash->cdev))
		AWT36515_LOGI("register thermal failed\n");
	awt36515_pinctrl_set(flash,
			AWT36515_PINCTRL_PIN_HWEN, AWT36515_PINCTRL_PINSTATE_HIGH);
	awt36515_pinctrl_set(flash,
			AWT36515_PINCTRL_PIN_HWEN_BACK, AWT36515_PINCTRL_PINSTATE_HIGH);
	awt36515_pinctrl_set(flash,
			AWT36515_PINCTRL_PIN_HWEN_TELE, AWT36515_PINCTRL_PINSTATE_LOW);
	//sw reset
	rval = regmap_update_bits(flash->regmap,
				  REG_SW_RESET, 0x80, 0x80);
	mdelay(2);
	regmap_clear_bits(awt36515_flash_data->regmap, REG_ENABLE, 0x7f);
	rval = regmap_read(flash->regmap, REG_CHIPID, &reg_val);
	AWT36515_LOGD("read chip id:[0x%x]\n",reg_val);
	awt36515_pinctrl_set(flash,
			AWT36515_PINCTRL_PIN_HWEN, AWT36515_PINCTRL_PINSTATE_LOW);
	awt36515_pinctrl_set(flash,
			AWT36515_PINCTRL_PIN_HWEN_BACK, AWT36515_PINCTRL_PINSTATE_LOW);
	return 0;
}
static void awt36515_remove(struct i2c_client *client)
{
	struct awt36515_flash *flash = i2c_get_clientdata(client);
	unsigned int i;
	thermal_cooling_device_unregister(flash->cdev);
	for (i = AWT36515_LED0; i < AWT36515_LED_MAX; i++) {
		v4l2_device_unregister_subdev(&flash->subdev_led[i]);
		v4l2_ctrl_handler_free(&flash->ctrls_led[i]);
		media_entity_cleanup(&flash->subdev_led[i].entity);
	}
	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}
static int __maybe_unused awt36515_suspend(struct device *dev)
{
	unsigned int reg_val;
	struct i2c_client *client = to_i2c_client(dev);
	struct awt36515_flash *flash = i2c_get_clientdata(client);
	regmap_read(flash->regmap, REG_ENABLE, &reg_val);
	AWT36515_LOGD("reg_val=0x%x flash_on=0x%x", reg_val,flash_on);
	if ((reg_val&0x3) == 0) {
		flash_on = 0;
		AWT36515_LOGI("flash already Off,reg_val=0x%x flash_on=%d", reg_val,flash_on);
		return awt36515_uninit(flash);
	} else {
		flash_on = 1;
		AWT36515_LOGI("flash  still on,  reg_val=0x%x flash_on=%d",reg_val,flash_on);
		return 0;
	}
}
static int __maybe_unused awt36515_resume(struct device *dev)
{
	unsigned int reg_val;
	struct i2c_client *client = to_i2c_client(dev);
	struct awt36515_flash *flash = i2c_get_clientdata(client);
	regmap_read(flash->regmap, REG_ENABLE, &reg_val);
	AWT36515_LOGD("reg_val=0x%x flash_on=%d \n",reg_val,flash_on);
	if (flash_on == 0) {
		if((reg_val&0x3) == 0){
		   AWT36515_LOGI("flash already Off,reg_val=0x%x flash_on=%d",reg_val,flash_on);
		   return awt36515_init(flash);
		}
		return 0;
	} else {
		AWT36515_LOGI("flash still on, reg_val=0x%x flash_on=%d",reg_val,flash_on);
		return 0;
	}
}
static const struct i2c_device_id awt36515_id_table[] = {
	{AWT36515_NAME, 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, awt36515_id_table);
static const struct of_device_id awt36515_of_table[] = {
	{ .compatible = "mediatek,awt36515" },
	{ },
};
MODULE_DEVICE_TABLE(of, awt36515_of_table);
static const struct dev_pm_ops awt36515_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(awt36515_suspend, awt36515_resume, NULL)
};
static struct i2c_driver awt36515_i2c_driver = {
	.driver = {
		   .name = AWT36515_NAME,
		   .pm = &awt36515_pm_ops,
		   .of_match_table = awt36515_of_table,
		   },
	.probe = awt36515_probe,
	.remove = awt36515_remove,
	.id_table = awt36515_id_table,
};
module_i2c_driver(awt36515_i2c_driver);
MODULE_DESCRIPTION("Texas Instruments awt36515 flash driver");
MODULE_LICENSE("GPL");
