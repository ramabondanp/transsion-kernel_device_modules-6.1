// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Transsion Inc.
 */

#ifndef __TRAN_AUDIO_CONTROLLER_H__
#define __TRAN_AUDIO_CONTROLLER_H__

#define PA_NAME_SIZE 20
enum tran_pa_type {
	TRAN_PA_TYPE_MIN = -1,
	TRAN_PA_TYPE_DEFAULT,
	TRAN_PA_TYPE_FS1599N,
	TRAN_PA_TYPE_AW87390,
	TRAN_PA_TYPE_SMARTPA_AW883XX,
	TRAN_PA_TYPE_OCA72XXX,
	TRAN_PA_TYPE_MAX,
};

typedef struct {
	enum tran_pa_type id;
	char name[PA_NAME_SIZE];
	int (*open_pa)(int mode);
	int (*close_pa)(void);
	int (*set_pa)(int mode);
	bool has_register;
} pa_control_struct;

int register_to_pa_manager(pa_control_struct *pa);
int unregister_to_pa_manager(enum tran_pa_type id);


struct ext_amp_data {
	int gpio_no;
	int dualspeaker_gpio_no;
	int normal_mode;
	int top_speech_mode;
	int bottom_speech_mode;
	int receiver_mode;
	int top_fm_mode;
	int bottom_fm_mode;
};

struct ext_amp_data g_spk_amp_data = {
	.gpio_no = -1,
	.dualspeaker_gpio_no = -1,
	.normal_mode = -1,
	.top_speech_mode = -1,
	.bottom_speech_mode = -1,
	.receiver_mode = -1,
	.top_fm_mode = -1,
	.bottom_fm_mode = -1,
};

enum tran_pa_mode_t {
	TRAN_MODE_NONE = 0,
	TRAN_MODE_MUSIC = TRAN_MODE_NONE,
	TRAN_MODE_VOICE,
	TRAN_MODE_VOIP,
	TRAN_MODE_RING,
	TRAN_MODE_LOW_PWR,   /* FM mode */
	TRAN_MODE_MMI_ALL,
	TRAN_MODE_MMI_ALL_BYPASS,
	TRAN_MODE_TOP_LEFT,
	TRAN_MODE_TOP_LEFT_BYPASS,
	TRAN_MODE_TOP_RIGHT,
	TRAN_MODE_TOP_RIGHT_BYPASS,
	TRAN_MODE_BOT_LEFT,
	TRAN_MODE_BOT_LEFT_BYPASS,
	TRAN_MODE_BOT_RIGHT,
	TRAN_MODE_BOT_RIGHT_BYPASS,
	TRAN_MODE_RECEIVER,
	TRAN_MODE_MAX,
};

enum aw87xxx_dev_index {
	AW_DEV_0 = 0,
	AW_DEV_1 = 1,
};

enum oca72xxx_dev_index {
	OCA_DEV_0 = 0,
	OCA_DEV_1 = 1,
};



#endif // __TRAN_AUDIO_CONTROLLER_H__
