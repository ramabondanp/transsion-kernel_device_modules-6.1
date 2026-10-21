// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#ifndef _AW_LAMP_INTERFACE_H_
#define _AW_LAMP_INTERFACE_H_

#include "aw_breath_algorithm.h"



typedef struct {
	unsigned int time[6];
	unsigned int  repeat_nums;
	unsigned char  fadeh;
	unsigned char  fadel;
	uint8_t effect;
	unsigned char color_nums;
	const unsigned char *rgb_color_list;

} AW_MULTI_BREATH_DATA_STRUCT;

typedef struct {
	unsigned char col_register;
	unsigned char col_value;
	unsigned char br_register;
	unsigned char br_value;
} AW_RGB_BRIGHTNESS_DATA;

typedef struct{
	GetBrightnessFuncPtr getBrightnessfunc;
	ALGO_DATA_STRUCT *p_algo_data;
	unsigned short cur_frame;
	unsigned short total_frames;
	unsigned char p_color_1;
	unsigned char p_color_2;
} AW_COLORFUL_INTERFACE_STRUCT;

extern void aw_set_colorful_rgb_data(unsigned char rgb_idx, unsigned char *dim_reg,
	AW_COLORFUL_INTERFACE_STRUCT *p_colorful_interface);
extern void aw_set_rgb_brightness(unsigned char rgb_idx,
	unsigned char *fade_reg, unsigned char brightness);

extern unsigned char aw_get_real_dim(unsigned char led_dim);
#endif
