// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Transsion Inc.
 */

#ifndef __TRAN_SENSOR_CWB__
#define __TRAN_SENSOR_CWB__

 /* if define TRANSSION_CWB_MOCK just for test sensor cwb soft firmware*/

#include "tran_sensor_cust.h"
#include "hf_sensor_type.h"

#if IS_ENABLED(CONFIG_TRANSSION_CWB_API_SUPPORT)
#include "transsion_cwb.h"
extern void transsion_cwb_init(void);
extern int transsion_cwb_enable(bool en, u8 lcm_id);
extern int transsion_cwb_registor(u8 lcm_id, u8 sensor_id, unsigned int  offset_x, unsigned int offset_y, unsigned int clip_w, unsigned int clip_h, unsigned int  Lsensor_x, unsigned int  Lsensor_y, unsigned int  Lsensor_w, unsigned int  Lsensor_h, unsigned int  ratio_in, unsigned int  ratio_out);
extern void transsion_get_cwb_rgb_info(Transsion_CWB_Sensor_Info *rgb_buf);
#endif

#define TRAN_ERROR(fmt, args...) do { \
    printk(KERN_ERR "[tran_sensor_debug]%s %d:"fmt"\n", __func__, __LINE__, ##args); \
} while (0)

void tran_sensor_cwb_init(void);
int tran_sensor_cwb_enable(uint8_t sensor_type, bool en);
void tran_sensor_cwb_register(void);
void tran_sensor_cwb_rgb_get(void);

#if !IS_ENABLED(CONFIG_TRANSSION_CWB_API_SUPPORT)
typedef struct {
	u8 sensor_id;
	u8 r_avg;
	u8 g_avg;
	u8 b_avg;
} Transsion_CWB_Sensor_Info_MOCK;

enum TRANSSION_DISP_CWB_LCM_ID_MOCK {
	TRANSSION_DISP_CWB_MAIN_LCM,
	TRANSSION_DISP_CWB_SUB_LCM,
};
#endif /*TRANSSION_CWB_MOCK*/

#endif /* __TRAN_SENSOR_CWB__ */
