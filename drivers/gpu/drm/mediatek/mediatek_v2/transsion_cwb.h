/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) <2024> Transsion Inc.
 */

#ifndef __TRANSSION_CWB_H__
#define __TRANSSION_CWB_H__
enum TRANSSION_DISP_CWB_LCM_ID {
	TRANSSION_DISP_CWB_MAIN_LCM,
	TRANSSION_DISP_CWB_SUB_LCM,
};
typedef struct {
	u8 sensor_id;
	u8 r_avg;
	u8 g_avg;
	u8 b_avg;
	u8 r_avg_r;
	u8 g_avg_r;
	u8 b_avg_r;
	u8 ring_enable;
} Transsion_CWB_Sensor_Info;

typedef struct {
	u8 index;
	u8 sensor_id;
	u8 lcm_id;
	u8 r_avg;
	u8 g_avg;
	u8 b_avg;
	u8 r_avg_r;
	u8 g_avg_r;
	u8 b_avg_r;
	u8 ring_enable;
	u32 offset_x;
	u32 offset_y;
	u32 clip_w;
	u32 clip_h;
	u32 offset_x_r;
	u32 offset_y_r;
	u32 clip_w_r;
	u32 clip_h_r;
	u32 Lsensor_x;
	u32 Lsensor_y;
	u32 Lsensor_w;
	u32 Lsensor_h;
	u32 ratio_in;
	u32 ratio_out;
} Transsion_Disp_Cwb_Info;
void transsion_cwb_init(void);
int transsion_cwb_enable(bool en, u8 lcm_id);
int transsion_cwb_registor(u8 lcm_id, u8 sensor_id, unsigned int offset_x, unsigned int offset_y, unsigned int clip_w, unsigned int clip_h,
							unsigned int Lsensor_x, unsigned int Lsensor_y, unsigned int Lsensor_w, unsigned int Lsensor_h,
							unsigned int ratio_in, unsigned int ratio_out);
void transsion_get_cwb_rgb_info(Transsion_CWB_Sensor_Info *rgb_buf);
void transsion_cwb_config_ring(u8 sensor_id, unsigned int  offset_x, unsigned int offset_y, unsigned int clip_w, unsigned int clip_h);
#endif
