// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Transsion Inc.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/leds.h>
#include <linux/firmware.h>
#include <linux/i2c-dev.h>
#include "leds-hk32f0301.h"
#include "tc_led_class.h"

#define HK32FXXXX_CHIPID		0x20
#define HK32FXXXX_CHIPID_01		0x21
#define HK32FXXXX_CHIPID_02		0x22
#define HK32FXXXX_CHIPID_RGB		0x30
#define HK32FXXXX_CHIPID_RGB_02		0x31
#define HK32FXXXX_CHIPID_RGB_03		0x32
#define HK32FXXXX_I2C_NAME		"hk32f0301_led"
#define HK32FXXXX_DRIVER_VERSION		"v0.1.0"
#define HK32FXXXX_READ_CHIPID_RETRIES	5
#define HK32FXXXX_I2C_RETRIES		3

#define HK32FXXXX_I2C_FAIL		-1
#define HK32FXXXX_I2C_SUCCESS		0

#define HK32FXXXX_1K_BYTE		1024
#define MCU_FWVER_ADDRESS		0x08000D00
#define MCU_FWVER_ADDRESS_64K		0x08001000
#define FWVER_UPDATE_DATA_MAX		1032
#define FWVER_IAP_STATUS		0x55
#define FWVER_APP_STATUS		0xAA

/*******************************************************************************
 *
 * hk32f0301 leds command
 *
 ******************************************************************************/
static unsigned char leds_open_all[] = {0x02, 0x0F};
static unsigned char leds_close_all[] = {0x05};
static unsigned char leds_audio[] = {0x03, 0x00};
static unsigned char leds_read_chipid[] = {0xA1, 0x1F, 0xFF, 0xF1, 0x3E, 0x00, 0x01};
static unsigned char leds_read_firmwareid[] = {0xA1, 0x08, 0x00, 0x3F, 0x00, 0x00, 0x01};
static unsigned char leds_read_firmwareid_64K[] = {0xA1, 0x08, 0x00, 0xF8, 0x00, 0x00, 0x01};
static unsigned char leds_set_brightness_all[] = {0x04,0x01};
/* fw update use */
static unsigned char leds_fwver_update_iap[] = {0xAE};
//static unsigned char leds_fwver_update_userdata[] = {0xA3, 0x08, 0x00, 0x14, 0x00, 0x04, 0x00, 0x04, 0x00, 0xff};
static unsigned char leds_fwver_update_fwver_id[] = {0xA3, 0x08, 0x00, 0x3F, 0x00, 0x00, 0x01, 0xFF,0x00};
static unsigned char leds_fwver_update_fwver_id_64K[] = {0xA3, 0x08, 0x00, 0xF8, 0x00, 0x00, 0x01, 0xFF,0x00};
static unsigned char leds_fwver_update_crc[] = {0xA3, 0x08, 0x00, 0x3E, 0x00, 0x00, 0x04, 0xff,0xff,0xff,0xff,0xff};
static unsigned char leds_fwver_update_crc_64K[] = {0xA3, 0x08, 0x00, 0xFC, 0x00, 0x00, 0x04, 0xff,0xff,0xff,0xff,0xff};
static unsigned char leds_fwver_get_crc_result[] = {0xAC};
static unsigned char leds_fwver_running[] = {0xAB};
static unsigned char leds_fwver_read_program_status[] = {0xAD};
static u8 *hk32fxxxx_leds_userdata = NULL;

enum hk32fxxxx_effect {
	HK32FXXXX_PARTY_BREATHMODE,
	HK32FXXXX_PARTY_WATERMODE,
	HK32FXXXX_PARTY_FLASHMODE,
	HK32FXXXX_CHARGE_PREVIEW,
	HK32FXXXX_CHARGE,
	HK32FXXXX_CHARGE_FULL,
	HK32FXXXX_PHOTO_3S,
	HK32FXXXX_PHOTO_5S,
	HK32FXXXX_PHOTO_10S,
	HK32FXXXX_CALL,
	HK32FXXXX_RECORDING,
	HK32FXXXX_NOTICE,
	HK32FXXXX_BOOT,
	HK32FXXXX_AUDIO_MODE_PREVIEW,
	HK32FXXXX_AUDIO_MODE,
	HK32FXXXX_GAME_START,
	HK32FXXXX_FLOAX_PREVIEW,
	HK32FXXXX_AWAKE,
	HK32FXXXX_ANALYSYS,
	HK32FXXXX_ANSWER,
	/*MLBB GAME*/
	HK32FXXXX_MLBB_GAME,
	HK32FXXXX_MLBB_GAME_FIRSTBLOOD = HK32FXXXX_MLBB_GAME,
	HK32FXXXX_MLBB_GAME_SINGLEKILL,
	HK32FXXXX_MLBB_GAME_KILLSTREAKS,
	/*HOK GAME*/
	HK32FXXXX_HOK_GAME,
	HK32FXXXX_HOK_GAME_FIRSTBLOOD = HK32FXXXX_HOK_GAME,
	HK32FXXXX_HOK_GAME_SINGLEKILL,
	HK32FXXXX_HOK_GAME_KILLSTREAKS,
	/*PUBG GAME*/
	HK32FXXXX_PUBG_GAME,
	HK32FXXXX_PUBG_GAME_FIRE = HK32FXXXX_PUBG_GAME,
	HK32FXXXX_PUBG_GAME_CLOSESCOPE,
	HK32FXXXX_PUBG_GAME_OPENSCOPE,
	HK32FXXXX_PUBG_GAME_DRIVING,
	/*FF GAME*/
	HK32FXXXX_FF_GAME,
	HK32FXXXX_FF_GAME_FIRE = HK32FXXXX_FF_GAME,
	HK32FXXXX_FF_GAME_CLOSESCOPE,
	HK32FXXXX_FF_GAME_OPENSCOPE,

	HK32FXXXX_EFFECT_MAX,
};

static char hk32fxxxx_effect_array[] = {
	0x0A,
	0x0B,
	0x0C,
	0x23,
	0x0D,
	0x0E,
	0x0F,
	0x10,
	0x11,
	0x12,
	0x14,
	0x15,
	0x25,
	0x26,
	0x16,
	0x17,
	0x24,
	0x18,
	0x18,
	0x19,
	0x1A,
	0x1B,
	0x1C,
	0x1A,
	0x1B,
	0x1C,
	0x1D,
	0x1F,
	0x20,
	0x22,
	0x1D,
	0x1F,
	0x20,
};

/*******************************************************************************
 *
 * hk32f0301 i2c read/write
 *
 ******************************************************************************/
static int hk32fxxxx_rw_multi_data(struct hk32fxxxx *chip,
		struct i2c_msg *regs, int num_regs)
{
    int ret = -1;
	int cnt = 0;

    mutex_lock(&chip->i2c_lock);

	while(cnt < HK32FXXXX_I2C_RETRIES) {
		ret = i2c_transfer(chip->i2c->adapter, regs, num_regs);
    	if (ret < 0) {
        	dev_err(chip->dev, "[%s] i2c transfer error:cnt:%d!\n", __func__, cnt);
		} else {
			dev_err(chip->dev, "[%s] i2c transfer success!\n", __func__);
			break;
		}
		cnt++;
		msleep(10);
	}

	mutex_unlock(&chip->i2c_lock);

	return ret;
}

/*****************************************************
 *
 * hk32fxxxx leds interface
 *
 *****************************************************/
static int hk32fxxxx_leds_open_all(struct hk32fxxxx *chip)
{
    struct i2c_msg msg_leds_open_all[] = {
        {
            .addr = chip->i2c->addr,
            .flags = 0, //write
            .buf = leds_open_all,
            .len = ARRAY_SIZE(leds_open_all),
        },
    };

    return hk32fxxxx_rw_multi_data(chip, msg_leds_open_all, ARRAY_SIZE(msg_leds_open_all));
}

static int hk32fxxxx_leds_close_all(struct hk32fxxxx *chip)
{
    struct i2c_msg msg_leds_close_all[] = {
        {
            .addr = chip->i2c->addr,
            .flags = 0,
            .buf = leds_close_all,
            .len = ARRAY_SIZE(leds_close_all),
        },
    };

    return hk32fxxxx_rw_multi_data(chip, msg_leds_close_all, ARRAY_SIZE(msg_leds_close_all));
}

static int hk32fxxxx_leds_audio_gain(struct hk32fxxxx *chip, int *gain)
{
	char gain_temp[1] = {0};
    struct i2c_msg msg_leds_audio[] = {
        {
            .addr = chip->i2c->addr,
            .flags = 0,
            .buf = leds_audio,
            .len = ARRAY_SIZE(leds_audio),
        },
    };

	gain_temp[0] = (*gain) & 0xff;
	memcpy(leds_audio + 1, gain_temp, 1);

	return hk32fxxxx_rw_multi_data(chip, msg_leds_audio, ARRAY_SIZE(msg_leds_audio));
}

static int hk32fxxxx_read_chipid(struct hk32fxxxx *chip)
{
	u8 read_buf[1];
	int ret;
	struct i2c_msg msg_leds_read_chipid[] = {
        {
            .addr = chip->i2c->addr,
            .flags = 0,
            .buf = leds_read_chipid,
            .len = ARRAY_SIZE(leds_read_chipid),
        },
    };

	struct i2c_msg msg_leds_read_chipid_2nd[] = {
		{
            .addr = chip->i2c->addr,
            .flags = I2C_M_RD,
            .buf = read_buf,
            .len = ARRAY_SIZE(read_buf),
        },
    };

    ret = hk32fxxxx_rw_multi_data(chip, msg_leds_read_chipid, ARRAY_SIZE(msg_leds_read_chipid));
	if(ret < 0)
		return ret;

	msleep(1);
	hk32fxxxx_rw_multi_data(chip, msg_leds_read_chipid_2nd, ARRAY_SIZE(msg_leds_read_chipid_2nd));

	chip->chipid = read_buf[0];
	if(read_buf[0] == HK32FXXXX_CHIPID || read_buf[0] == HK32FXXXX_CHIPID_01 ||read_buf[0] == HK32FXXXX_CHIPID_02|| read_buf[0] == HK32FXXXX_CHIPID_RGB || read_buf[0] == HK32FXXXX_CHIPID_RGB_02 || read_buf[0] == HK32FXXXX_CHIPID_RGB_03) {
		dev_err(chip->dev, "[%s] chipid read successful:0x%02x!\n", __func__, read_buf[0]);
	} else {
		dev_err(chip->dev, "[%s] chipid is wrong:0x%02x!\n", __func__, read_buf[0]);
		return HK32FXXXX_I2C_FAIL;
	}

	return 0;
}



static int hk32fxxxx_read_program_status(struct hk32fxxxx *chip)
{
	u8 read_buf[1];
	struct i2c_msg msg_leds_read_program_status[] = {
        {
            .addr = chip->i2c->addr,
            .flags = 0,
            .buf = leds_fwver_read_program_status,
            .len = ARRAY_SIZE(leds_fwver_read_program_status),
        },
    };

	struct i2c_msg msg_leds_read_program_status_2nd[] = {
		{
            .addr = chip->i2c->addr,
            .flags = I2C_M_RD,
            .buf = read_buf,
            .len = ARRAY_SIZE(read_buf),
        },
    };

    hk32fxxxx_rw_multi_data(chip, msg_leds_read_program_status, ARRAY_SIZE(msg_leds_read_program_status));
	msleep(1);
	hk32fxxxx_rw_multi_data(chip, msg_leds_read_program_status_2nd, ARRAY_SIZE(msg_leds_read_program_status_2nd));
	dev_err(chip->dev, "[%s]:%d!\n", __func__, read_buf[0]);

	if(read_buf[0] == FWVER_IAP_STATUS) {
		dev_err(chip->dev, "[%s] The program is running in IAP, need to changed!\n", __func__);
		return HK32FXXXX_I2C_FAIL;
	}

	return 0;
}

/*******************************************************************************
 *
 * hk32fxxxx led set brightness
 *
 ******************************************************************************/
static int hk32fxxxx_fwver_set_brightness(struct hk32fxxxx *chip, int value)
{
	u8 write_buf[1] = {value};
	struct i2c_msg msg_leds_fwver_set_brightness[] = {
        {
            .addr = chip->i2c->addr,
            .flags = 0,
            .buf = leds_set_brightness_all,
            .len = ARRAY_SIZE(leds_set_brightness_all),
        },
    };

	dev_info(chip->dev, "[%s] imax is:%d!\n", __func__, write_buf[0]);
	memcpy(leds_set_brightness_all + (sizeof(leds_set_brightness_all) - 1), write_buf, sizeof(write_buf));
    hk32fxxxx_rw_multi_data(chip, msg_leds_fwver_set_brightness, ARRAY_SIZE(msg_leds_fwver_set_brightness));

	return 0;
}

/*******************************************************************************
 *
 * hk32fxxxx read fwver id
 *
 ******************************************************************************/
static int hk32fxxxx_check_firmwareid(struct hk32fxxxx *chip)
{
	u8 read_buf[1];

	struct i2c_msg msg_leds_read_firmwareid[] = {
        {
            .addr = chip->i2c->addr,
            .flags = 0,
            .buf = leds_read_firmwareid,
            .len = ARRAY_SIZE(leds_read_firmwareid),
        },
    };

	struct i2c_msg msg_leds_read_firmwareid_2nd[] = {
        {
            .addr = chip->i2c->addr,
            .flags = I2C_M_RD,
            .buf = read_buf,
            .len = ARRAY_SIZE(read_buf),
        },
	};

	if(chip->chipid == HK32FXXXX_CHIPID_RGB || chip->chipid == HK32FXXXX_CHIPID_RGB_02 || chip->chipid == HK32FXXXX_CHIPID_RGB_03)
		memcpy(msg_leds_read_firmwareid[0].buf , leds_read_firmwareid_64K, sizeof(leds_read_firmwareid_64K));

    hk32fxxxx_rw_multi_data(chip, msg_leds_read_firmwareid, ARRAY_SIZE(msg_leds_read_firmwareid));
	msleep(1);
	hk32fxxxx_rw_multi_data(chip, msg_leds_read_firmwareid_2nd, ARRAY_SIZE(msg_leds_read_firmwareid_2nd));

	if(read_buf[0] == chip->fwver_version) {
		dev_err(chip->dev, "[%s] firmwareid is right:0x%02x!\n", __func__, read_buf[0]);
	} else {
		dev_err(chip->dev, "[%s] firmwareid is old:0x%02x,need to update leds firmware!\n", __func__, read_buf[0]);
		return HK32FXXXX_I2C_FAIL;
	}

	return 0;
}

/*******************************************************************************
 *
 * hk32fxxxx fw update
 *
 ******************************************************************************/
static int hk32fxxxx_fwver_update_iap(struct hk32fxxxx *chip)
{
	struct i2c_msg msg_leds_fwver_update_iap[] = {
        {
            .addr = chip->i2c->addr,
            .flags = 0,
            .buf = leds_fwver_update_iap,
            .len = ARRAY_SIZE(leds_fwver_update_iap),
        },
    };

    hk32fxxxx_rw_multi_data(chip, msg_leds_fwver_update_iap, ARRAY_SIZE(msg_leds_fwver_update_iap));

	return 0;
}

static int hk32fxxxx_fwver_update_userdata(struct hk32fxxxx *chip)
{
	int count = 0;
	unsigned char temp[FWVER_UPDATE_DATA_MAX];
	int NumbOfSingle;
	int NumbOf1K;
	unsigned int McuFlashAddress = MCU_FWVER_ADDRESS;

    struct i2c_msg msg_leds_update_userdata[] = {
        {
            .addr = chip->i2c->addr,
            .flags = 0,
        },
    };

	dev_info(chip->dev,"%s:enter\n", __func__);
	NumbOf1K = chip->fwver_length/HK32FXXXX_1K_BYTE;
	NumbOfSingle = chip->fwver_length % HK32FXXXX_1K_BYTE;
	if(chip->chipid == HK32FXXXX_CHIPID_RGB || chip->chipid == HK32FXXXX_CHIPID_RGB_02 || chip->chipid == HK32FXXXX_CHIPID_RGB_03) {
		McuFlashAddress = MCU_FWVER_ADDRESS_64K;
	}

	for(count = 0; count < NumbOf1K; count ++) {
        temp[0] = 0xA3;
		temp[1] = (McuFlashAddress&0xFF000000)>>24;
		temp[2] = (McuFlashAddress&0x00FF0000)>>16;
		temp[3] = (McuFlashAddress&0x0000FF00)>>8;
		temp[4] = (McuFlashAddress&0x000000FF);
		temp[5] = HK32FXXXX_1K_BYTE/256;
		temp[6] = HK32FXXXX_1K_BYTE%256;
		temp[7] = 0xFF;

		memcpy(temp + 8, (hk32fxxxx_leds_userdata + count * HK32FXXXX_1K_BYTE), HK32FXXXX_1K_BYTE);

		msg_leds_update_userdata[0].buf = temp;
		msg_leds_update_userdata[0].len = 8 + HK32FXXXX_1K_BYTE;
		hk32fxxxx_rw_multi_data(chip, msg_leds_update_userdata, ARRAY_SIZE(msg_leds_update_userdata));
		if(chip->chipid == HK32FXXXX_CHIPID_RGB || chip->chipid == HK32FXXXX_CHIPID_RGB_02 || chip->chipid == HK32FXXXX_CHIPID_RGB_03)
			msleep(75);
		else
			msleep(500);

		McuFlashAddress += HK32FXXXX_1K_BYTE;
	}

	temp[1] = (McuFlashAddress&0xFF000000)>>24;
	temp[2] = (McuFlashAddress&0x00FF0000)>>16;
	temp[3] = (McuFlashAddress&0x0000FF00)>>8;
	temp[4] = (McuFlashAddress&0x000000FF);
	temp[5] = NumbOfSingle / 256;
    temp[6] = NumbOfSingle % 256;

	memcpy(temp + 8, (hk32fxxxx_leds_userdata + count * HK32FXXXX_1K_BYTE), NumbOfSingle);

	msg_leds_update_userdata[0].buf = temp;
	msg_leds_update_userdata[0].len = 8 + NumbOfSingle;
	hk32fxxxx_rw_multi_data(chip, msg_leds_update_userdata, ARRAY_SIZE(msg_leds_update_userdata));

	return 0;
}

static int hk32fxxxx_fwver_update_fwver_id(struct hk32fxxxx *chip)
{
	unsigned char temp[1] = {chip->fwver_version};
	struct i2c_msg msg_leds_fwver_update_fwver_id[] = {
        {
            .addr = chip->i2c->addr,
            .flags = 0,
            .buf = leds_fwver_update_fwver_id,
            .len = ARRAY_SIZE(leds_fwver_update_fwver_id),
        },
    };

	if(chip->chipid == HK32FXXXX_CHIPID_RGB || chip->chipid == HK32FXXXX_CHIPID_RGB_02 || chip->chipid == HK32FXXXX_CHIPID_RGB_03)
		memcpy(msg_leds_fwver_update_fwver_id[0].buf , leds_fwver_update_fwver_id_64K, sizeof(leds_fwver_update_fwver_id_64K));

	memcpy(leds_fwver_update_fwver_id + (sizeof(leds_fwver_update_fwver_id) - 1), temp, sizeof(temp));
    hk32fxxxx_rw_multi_data(chip, msg_leds_fwver_update_fwver_id, ARRAY_SIZE(msg_leds_fwver_update_fwver_id));

	return 0;
}

static int hk32fxxxx_fwver_update_crc(struct hk32fxxxx *chip)
{
	unsigned char temp[4];
	struct i2c_msg msg_leds_fwver_update_crc[] = {
        {
            .addr = chip->i2c->addr,
            .flags = 0,
            .buf = leds_fwver_update_crc,
            .len = ARRAY_SIZE(leds_fwver_update_crc),
        },
    };

	if(chip->chipid == HK32FXXXX_CHIPID_RGB || chip->chipid == HK32FXXXX_CHIPID_RGB_02 || chip->chipid == HK32FXXXX_CHIPID_RGB_03)
		memcpy(msg_leds_fwver_update_crc[0].buf , leds_fwver_update_crc_64K, sizeof(leds_fwver_update_crc_64K));

	temp[0] = chip->fwver_length >> 8;
	temp[1] = chip->fwver_length & 0xff;
	temp[2] = chip->fwver_crc >> 8;
	temp[3] = chip->fwver_crc & 0xff;

	dev_info(chip->dev,"%s:enter\n", __func__);
	memcpy(leds_fwver_update_crc + (sizeof(leds_fwver_update_crc) - 4) , temp, sizeof(temp));
    hk32fxxxx_rw_multi_data(chip, msg_leds_fwver_update_crc, ARRAY_SIZE(msg_leds_fwver_update_crc));

	return 0;
}

static int hk32fxxxx_fwver_get_crc_result(struct hk32fxxxx *chip)
{
	u8 read_buf[1];
	struct i2c_msg msg_leds_fwver_get_crc_result[] = {
        {
            .addr = chip->i2c->addr,
            .flags = 0,
            .buf = leds_fwver_get_crc_result,
            .len = ARRAY_SIZE(leds_fwver_get_crc_result),
        },
    };

	struct i2c_msg msg_leds_fwver_get_crc_result_2nd[] = {
		{
            .addr = chip->i2c->addr,
            .flags = I2C_M_RD,
            .buf = read_buf,
            .len = ARRAY_SIZE(read_buf),
        },
    };

    hk32fxxxx_rw_multi_data(chip, msg_leds_fwver_get_crc_result, ARRAY_SIZE(msg_leds_fwver_get_crc_result));
	msleep(10);
	hk32fxxxx_rw_multi_data(chip, msg_leds_fwver_get_crc_result_2nd, ARRAY_SIZE(msg_leds_fwver_get_crc_result_2nd));

	dev_err(chip->dev,"%s:get crc result:%d\n", __func__, read_buf[0]);
	if(read_buf[0] > 0)
		return 1;
	else
		return 0;
}


static int hk32fxxxx_fwver_running(struct hk32fxxxx *chip)
{
	struct i2c_msg msg_leds_fwver_running[] = {
        {
            .addr = chip->i2c->addr,
            .flags = 0,
            .buf = leds_fwver_running,
            .len = ARRAY_SIZE(leds_fwver_running),
        },
    };

	dev_info(chip->dev,"%s:enter\n", __func__);
    hk32fxxxx_rw_multi_data(chip, msg_leds_fwver_running, ARRAY_SIZE(msg_leds_fwver_running));

	return 0;
}

static void hk32fxxxx_fwver_update_process(struct hk32fxxxx *hk32fxxxx)
{
	dev_info(hk32fxxxx->dev,"%s:enter\n", __func__);
	hk32fxxxx_fwver_update_iap(hk32fxxxx);
	msleep(200);
	hk32fxxxx_fwver_update_userdata(hk32fxxxx);
	msleep(500);
	hk32fxxxx_fwver_update_fwver_id(hk32fxxxx);
	msleep(200);
	hk32fxxxx_fwver_update_crc(hk32fxxxx);
	msleep(200);
	if(hk32fxxxx_fwver_get_crc_result(hk32fxxxx) == 0) {
		pr_err("%s: crc test result failed!\n", __func__);
		return;
	}
	msleep(200);
	hk32fxxxx_fwver_running(hk32fxxxx);
	msleep(500);
	dev_info(hk32fxxxx->dev, "%s: complete end!\n", __func__);
}

/*fw update work */
static void hk32fxxxx_leds_fwver_update_work(struct work_struct *work)
{
	struct hk32fxxxx *hk32fxxxx = container_of(to_delayed_work(work),
			struct hk32fxxxx, wait_fw_update_work);

	mutex_lock(&hk32fxxxx->fw_lock);
	hk32fxxxx_fwver_update_process(hk32fxxxx);
	msleep(100);
	hk32fxxxx_fwver_set_brightness(hk32fxxxx, hk32fxxxx->imax);

	mutex_unlock(&hk32fxxxx->fw_lock);
}

/*******************************************************************************
 *
 * hk32fxxxx hardware enable
 *
 ******************************************************************************/
static int hk32fxxxx_hw_enable(struct hk32fxxxx *chip, bool flag)
{
	if (chip && gpio_is_valid(chip->enable_gpio)) {
		if (flag) {
			gpio_set_value_cansleep(chip->enable_gpio, 1);
			dev_err(chip->dev,"%s:set gpio high\n", __func__);
			usleep_range(1000, 1500);
		} else {
			gpio_set_value_cansleep(chip->enable_gpio, 0);
		}
	} else {
		dev_err(chip->dev,"%s:failed\n", __func__);
	}

	return 0;
}

static void hk32fxxxx_effect_mode_select(struct hk32fxxxx *chip, int effect)
{
	struct i2c_msg msg_leds_fwver_effect_mode_select[] = {
        {
            .addr = chip->i2c->addr,
            .flags = 0,
            .buf = &hk32fxxxx_effect_array[effect],
            .len = 1,
        },
    };

	hk32fxxxx_rw_multi_data(chip, msg_leds_fwver_effect_mode_select, ARRAY_SIZE(msg_leds_fwver_effect_mode_select));
}

static void hk32fxxxx_game_mode_select(struct hk32fxxxx *chip, int game_scene, int effect)
{
	if(game_scene + effect >= HK32FXXXX_EFFECT_MAX) {
		dev_err(chip->dev,"%s:invaild cmd\n", __func__);
		return;
	}

	hk32fxxxx_effect_mode_select(chip, game_scene + effect);
}


/*******************************************************************************
 *
 * sysfs attribute group: tran_led_cmd store/show
 *
 ******************************************************************************/
static int hk32fxxxx_store_tran_led_cmd(struct tc_led_device *dev, const char *cmd)
{
	struct hk32fxxxx *hk32fxxxx = dev->dev.driver_data;
	int num[10];

	memset(num, 0, 10);
	if (sscanf(cmd, "%x %x %x %x %x %x", &num[0], &num[1], &num[2], &num[3],&num[4],&num[5]) == 6) {
		dev_err(hk32fxxxx->dev,"%s 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",__func__,num[0], num[1], num[2], num[3],num[4],num[5]);
		hk32fxxxx->latest_cmd = num[1];
		hk32fxxxx_hw_enable(hk32fxxxx, true);
		switch(num[0]) {//type
			case 0x00:
				switch(num[1]) {//mode
					case 0x00://close
						hk32fxxxx_leds_close_all(hk32fxxxx);
						break;
					case 0x01://open
					case 0x09://midtest
						hk32fxxxx_leds_open_all(hk32fxxxx);
						break;
					//case 0x02://Preview
						//hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_PREVIEW);
						//break;
					case 0x03://incoming
						hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_CALL);
						break;
					case 0x04://notify
						hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_NOTICE);
						break;
					case 0x05://boot
						if(hk32fxxxx->chipid == HK32FXXXX_CHIPID || hk32fxxxx->chipid == HK32FXXXX_CHIPID_01 ||hk32fxxxx->chipid == HK32FXXXX_CHIPID_02)
							hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_PARTY_WATERMODE);
						else 
							hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_BOOT);
						break;		
					case 0x20://charing preview
						hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_CHARGE_PREVIEW);
						break;
					case 0x21://charing
						hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_CHARGE);
						break;
					case 0x22://charing full
						hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_CHARGE_FULL);
						break;
					case 0x51://3s time-lapse
						hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_PHOTO_3S);
						break;
					case 0x52:
						hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_PHOTO_5S);
						break;
					case 0x53:
						hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_PHOTO_10S);
						break;
					case 0x54://video record
						hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_RECORDING);
						break;
					case 0x61://game start
						hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_GAME_START);
						break;
					case 0x62://MLBB
						hk32fxxxx_game_mode_select(hk32fxxxx, num[2], HK32FXXXX_MLBB_GAME);
						break;
					case 0x63://HOK
						hk32fxxxx_game_mode_select(hk32fxxxx, num[2], HK32FXXXX_HOK_GAME);
						break;
					case 0x64://PUBG
						hk32fxxxx_game_mode_select(hk32fxxxx, num[2], HK32FXXXX_PUBG_GAME);
						break;
					case 0x65://FF
						hk32fxxxx_game_mode_select(hk32fxxxx, num[2], HK32FXXXX_FF_GAME);
						break;
					case 0x80://Floax preview
						hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_FLOAX_PREVIEW);
						break;
					case 0x81://Floax
						hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_AWAKE);
						break;
					case 0x82:
						hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_ANALYSYS);
						break;
					case 0x83:
						hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_ANSWER);
						break;
					case 0x91://Party
						hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_PARTY_BREATHMODE);
						break;
					case 0x92:
						hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_PARTY_WATERMODE);
						break;
					case 0x93:
						hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_PARTY_FLASHMODE);
						break;
					case 0x0A://audio
						hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_AUDIO_MODE);
						break;
					case 0x0B://audio preview
						hk32fxxxx_effect_mode_select(hk32fxxxx, HK32FXXXX_AUDIO_MODE_PREVIEW);
						break;
					case 0x0C://audio gain
						hk32fxxxx_leds_audio_gain(hk32fxxxx, &num[2]);
						break;
					case 0x0D://led setting debuging
						hk32fxxxx_fwver_set_brightness(hk32fxxxx, num[2]);
						break;
					default:
						hk32fxxxx->latest_cmd = 0;
						dev_err(hk32fxxxx->dev,"%s(no para) %d\n", __func__,__LINE__);
						break;
				}
				break;
			default:
				dev_err(hk32fxxxx->dev,"%s(no para) %d\n", __func__,__LINE__);
				break;
		}
	}

	return 0;
}

static int hk32fxxxx_show_tran_led_cmd(struct tc_led_device *dev, char *buf)
{
	struct hk32fxxxx *hk32fxxxx = dev->dev.driver_data;
	return sprintf(buf, "%d\n",  hk32fxxxx->latest_cmd);
}

static struct attribute *hk32fxxxx_attributes[] = {
	NULL,
};

static struct attribute_group hk32fxxxx_attribute_group = {
	.attrs = hk32fxxxx_attributes
};

/*******************************************************************************
 *
 * parse device tree
 *
 ******************************************************************************/

static int hk32fxxxx_parse_dts(struct device *dev, struct hk32fxxxx *hk32fxxxx,
						struct device_node *np)
{
	int ret = -1;

	hk32fxxxx->enable_gpio = of_get_named_gpio(np, "enable-gpio", 0);
	if(hk32fxxxx->enable_gpio < 0) {
		dev_err(hk32fxxxx->dev, "%s: parse enable_gpio failed\n", __func__);
		return ret;
	}

	ret = of_property_read_u32(np, "hk32fxxxx,max_brightness",
					&hk32fxxxx->max_brightness);
	if (ret < 0) {
		dev_err(hk32fxxxx->dev,
		"%s: parse max-brightness err, ret = %d\n", __func__, ret);
	}

	ret = of_property_read_u32(np, "hk32fxxxx,imax", &hk32fxxxx->imax);
	if (ret < 0) {
		dev_err(hk32fxxxx->dev,
			"%s: parse imax err, ret = %d\n", __func__, ret);
	}

	hk32fxxxx->imax = min(hk32fxxxx->imax,hk32fxxxx->max_brightness);
	dev_err(hk32fxxxx->dev,"%s: imax:%d, max_brightness:%d\n", __func__,hk32fxxxx->imax, hk32fxxxx->max_brightness);

	return 0;
}

static int hk32fxxxx_parse_led_cdev(struct hk32fxxxx *hk32fxxxx,
						struct device_node *np)
{
	int ret = -1;

	hk32fxxxx->cdev.name = "hk32fxxxx_led";
	ret = led_classdev_register(hk32fxxxx->dev, &hk32fxxxx->cdev);
	if (ret) {
		dev_err(hk32fxxxx->dev, "%s:unable to register led ret=%d\n", __func__, ret);
		goto free_pdata;
	}

	ret = sysfs_create_group(&hk32fxxxx->cdev.dev->kobj, &hk32fxxxx_attribute_group);
	if (ret) {
		dev_err(hk32fxxxx->dev, "%s:led sysfs ret=%d\n", __func__, ret);
		goto free_class;
	}

	return 0;

free_class:
	led_classdev_unregister(&hk32fxxxx->cdev);
free_pdata:
	return ret;
}

static int hk32fxxxx_parse_fwverdata_dts(struct hk32fxxxx *hk32fxxxx,
						struct device_node *np)
{
	int ret = 0;
	char fw_mtp_length_name[64] = {0};
	char fw_mtp_crc_name[64] = {0};
	char fw_fwversion_name[64] = {0};
	char fw_mtp_bin_name[64] = {0};

	if(hk32fxxxx->chipid == HK32FXXXX_CHIPID_RGB) {
		snprintf(fw_mtp_length_name, sizeof(fw_mtp_length_name), "%s", "hk320fxxxx_mtp_length_rgb");
		snprintf(fw_mtp_crc_name, sizeof(fw_mtp_crc_name), "%s", "hk320fxxxx_mtp_crc_rgb");
		snprintf(fw_fwversion_name, sizeof(fw_fwversion_name), "%s", "hk320fxxxx_fwversion_rgb");
		snprintf(fw_mtp_bin_name, sizeof(fw_mtp_bin_name), "%s", "hk32fxxxx_leds_mtp_bin_rgb");
	} else if(hk32fxxxx->chipid == HK32FXXXX_CHIPID_RGB_02) {
		snprintf(fw_mtp_length_name, sizeof(fw_mtp_length_name), "%s", "hk320fxxxx_mtp_length_rgb_02");
		snprintf(fw_mtp_crc_name, sizeof(fw_mtp_crc_name), "%s", "hk320fxxxx_mtp_crc_rgb_02");
		snprintf(fw_fwversion_name, sizeof(fw_fwversion_name), "%s", "hk320fxxxx_fwversion_rgb_02");
		snprintf(fw_mtp_bin_name, sizeof(fw_mtp_bin_name), "%s", "hk32fxxxx_leds_mtp_bin_rgb_02");
	} else if(hk32fxxxx->chipid == HK32FXXXX_CHIPID_RGB_03) {
		snprintf(fw_mtp_length_name, sizeof(fw_mtp_length_name), "%s", "hk320fxxxx_mtp_length_rgb_03");
		snprintf(fw_mtp_crc_name, sizeof(fw_mtp_crc_name), "%s", "hk320fxxxx_mtp_crc_rgb_03");
		snprintf(fw_fwversion_name, sizeof(fw_fwversion_name), "%s", "hk320fxxxx_fwversion_rgb_03");
		snprintf(fw_mtp_bin_name, sizeof(fw_mtp_bin_name), "%s", "hk32fxxxx_leds_mtp_bin_rgb_03");
	} else {
		snprintf(fw_mtp_length_name, sizeof(fw_mtp_length_name), "%s", "hk320fxxxx_mtp_length");
		snprintf(fw_mtp_crc_name, sizeof(fw_mtp_crc_name), "%s", "hk320fxxxx_mtp_crc");
		snprintf(fw_fwversion_name, sizeof(fw_fwversion_name), "%s", "hk320fxxxx_fwversion");
		snprintf(fw_mtp_bin_name, sizeof(fw_mtp_bin_name), "%s", "hk32fxxxx_leds_mtp_bin");
	}

	ret  = of_property_read_u32(np, fw_mtp_length_name, &hk32fxxxx->fwver_length);
	if (ret)
		return -EINVAL;

	ret  = of_property_read_u32(np, fw_mtp_crc_name, &hk32fxxxx->fwver_crc);
	if (ret)
		return -EINVAL;

	ret  = of_property_read_u8(np, fw_fwversion_name, &hk32fxxxx->fwver_version);
	if (ret)
		return -EINVAL;
	
	
	hk32fxxxx_leds_userdata = kzalloc(hk32fxxxx->fwver_length, GFP_KERNEL);
	if (IS_ERR_OR_NULL(hk32fxxxx_leds_userdata))
		return -EINVAL;
	
	ret = of_property_read_variable_u8_array(np, fw_mtp_bin_name, hk32fxxxx_leds_userdata,
			hk32fxxxx->fwver_length, hk32fxxxx->fwver_length);
	if (ret < 0) {
		pr_err("hk32fxxxx_leds_userdata failed\n");
		return -EINVAL;
	}

	dev_err(hk32fxxxx->dev,"%s: fwver_length:0x%x\n", __func__, hk32fxxxx->fwver_length);
	dev_err(hk32fxxxx->dev,"%s: hk320fxxxx_mtp_crc:0x%x\n", __func__, hk32fxxxx->fwver_crc);
	dev_err(hk32fxxxx->dev,"%s: hk320fxxxx_fwversion:0x%x\n", __func__,hk32fxxxx->fwver_version);

	return ret;
}

static const struct tc_led_ops leds_hk32fxxxx_ops = {
	.store_tran_led_cmd = hk32fxxxx_store_tran_led_cmd,
	.show_tran_led_cmd = hk32fxxxx_show_tran_led_cmd,
};

static void hk32fxxxx_i2c_driver_ability_init(struct hk32fxxxx *hk32fxxxx)
{
	hk32fxxxx->pinctrl = devm_pinctrl_get(hk32fxxxx->dev);
	if (IS_ERR(hk32fxxxx->pinctrl)) {
		dev_err(hk32fxxxx->dev,"%s: Cannot find pinctrl\n", __func__);
		return;
	}

	hk32fxxxx->pinctrl_i2c_clk_gpio = pinctrl_lookup_state(hk32fxxxx->pinctrl, "leds_i2c_clk_mode");
	if (IS_ERR(hk32fxxxx->pinctrl_i2c_clk_gpio)) {
		dev_err(hk32fxxxx->dev,"%s: Cannot find i2c_clk_mode\n", __func__);
		return;
	} else {
		pinctrl_select_state(hk32fxxxx->pinctrl, hk32fxxxx->pinctrl_i2c_clk_gpio);
	}

	hk32fxxxx->pinctrl_i2c_sda_gpio = pinctrl_lookup_state(hk32fxxxx->pinctrl, "leds_i2c_sda_mode");
	if (IS_ERR(hk32fxxxx->pinctrl_i2c_sda_gpio)) {
		dev_err(hk32fxxxx->dev,"%s: Cannot find i2c_sda_mode\n", __func__);
		return;
	} else {
		pinctrl_select_state(hk32fxxxx->pinctrl, hk32fxxxx->pinctrl_i2c_sda_gpio);
	}
}

/*******************************************************************************
 *
 * i2c driver probe
 *
 ******************************************************************************/
static int hk32fxxxx_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct hk32fxxxx *hk32fxxxx;
	struct device_node *np = i2c->dev.of_node;
	int ret;

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C)) {
		pr_err("check_functionality failed\n");
		return -EIO;
	}

	hk32fxxxx = devm_kzalloc(&i2c->dev, sizeof(struct hk32fxxxx), GFP_KERNEL);
	if (hk32fxxxx == NULL)
		return -ENOMEM;

	hk32fxxxx->dev = &i2c->dev;
	hk32fxxxx->i2c = i2c;
	i2c_set_clientdata(i2c, hk32fxxxx);
	mutex_init(&hk32fxxxx->i2c_lock);
	mutex_init(&hk32fxxxx->fw_lock);

	dev_info(hk32fxxxx->dev, "%s:enter\n", __func__);
	hk32fxxxx_i2c_driver_ability_init(hk32fxxxx);

	if (np) {
		ret = hk32fxxxx_parse_dts(&i2c->dev, hk32fxxxx, np);
		if (ret) {
			pr_err("failed to parse device tree node\n");
			goto err_parse_dt;
		}
	}

	if (gpio_is_valid(hk32fxxxx->enable_gpio)) {
		ret = devm_gpio_request_one(&i2c->dev, hk32fxxxx->enable_gpio,
				GPIOF_OUT_INIT_LOW, "hk32fxxxx_en");
		if (ret < 0) {
			pr_err("enable gpio request failed\n");
			goto err_gpio_request;
		}
	}

	/* hardware enable */
	hk32fxxxx_hw_enable(hk32fxxxx, true);

	//msleep(10);
	//hk32fxxxx_fwver_update_iap(hk32fxxxx);
	msleep(200);
	ret = hk32fxxxx_read_chipid(hk32fxxxx);
	if (ret < 0) {
		pr_err("hk32fxxxx_read_chipid failed ret=%d\n", ret);
		goto err_id;
	}

	dev_set_drvdata(&i2c->dev, hk32fxxxx);

	ret = hk32fxxxx_parse_led_cdev(hk32fxxxx, np);
	if (ret < 0) {
		pr_err("error creating led class dev\n");
		goto err_sysfs;
	}

	ret = hk32fxxxx_parse_fwverdata_dts(hk32fxxxx, np);
	if(ret < 0) {
		pr_err("error parse_userdata_dts\n");
		goto err_parse_dt;
	}

	INIT_DELAYED_WORK(&hk32fxxxx->wait_fw_update_work, hk32fxxxx_leds_fwver_update_work);
	hk32fxxxx->led_dev = tc_led_device_register("hk32fxxxx", hk32fxxxx->dev, hk32fxxxx, &leds_hk32fxxxx_ops, NULL);

	if(hk32fxxxx_check_firmwareid(hk32fxxxx) < 0)
		schedule_delayed_work(&hk32fxxxx->wait_fw_update_work, 100);
	else if(hk32fxxxx_read_program_status(hk32fxxxx) < 0)
		hk32fxxxx_fwver_running(hk32fxxxx);

	msleep(100);
	hk32fxxxx_fwver_set_brightness(hk32fxxxx, hk32fxxxx->imax);

	dev_err(hk32fxxxx->dev, "%s:probe completed successful\n", __func__);
	return 0;

err_sysfs:
err_gpio_request:
err_parse_dt:
err_id:
	devm_kfree(&i2c->dev, hk32fxxxx);
	hk32fxxxx = NULL;
	return ret;
}

/*******************************************************************************
 *
 * i2c driver remove
 *
 ******************************************************************************/

static void hk32fxxxx_i2c_remove(struct i2c_client *i2c)
{
	struct hk32fxxxx *hk32fxxxx = i2c_get_clientdata(i2c);

	pr_info("enter %s\n", __func__);
	sysfs_remove_group(&hk32fxxxx->cdev.dev->kobj, &hk32fxxxx_attribute_group);
	led_classdev_unregister(&hk32fxxxx->cdev);
	devm_kfree(&i2c->dev, hk32fxxxx);
	hk32fxxxx = NULL;
}

static const struct i2c_device_id hk32fxxxx_i2c_id[] = {
	{HK32FXXXX_I2C_NAME, 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, hk32fxxxx_i2c_id);

static const struct of_device_id hk32fxxxx_dt_match[] = {
	{.compatible = "suijing,hk320f0301_led"},
	{},
};

static struct i2c_driver hk32fxxxx_i2c_driver = {
	.driver = {
		.name = HK32FXXXX_I2C_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(hk32fxxxx_dt_match),
	},
	.probe = hk32fxxxx_i2c_probe,
	.remove = hk32fxxxx_i2c_remove,
	.id_table = hk32fxxxx_i2c_id,
};

static int __init hk32fxxxx_i2c_init(void)
{
	int ret = -1;

	pr_info("driver version %s\n", HK32FXXXX_DRIVER_VERSION);

	ret = i2c_add_driver(&hk32fxxxx_i2c_driver);
	if (ret) {
		pr_err("add hk32fxx driver failed\n");
		return ret;
	}

	return 0;
}

module_init(hk32fxxxx_i2c_init);

static void __exit hk32fxxxx_i2c_exit(void)
{
	i2c_del_driver(&hk32fxxxx_i2c_driver);
}

module_exit(hk32fxxxx_i2c_exit);

MODULE_DESCRIPTION("HK32F0301 LED Driver");
MODULE_LICENSE("GPL v2");

