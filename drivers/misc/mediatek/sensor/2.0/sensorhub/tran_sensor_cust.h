// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Transsion Inc.
 */
#ifndef _TRAN_SENSOR_CUST_
#define _TRAN_SENSOR_CUST_

/*
 * Transsion cust device 
 * Created on 2023-08-18
*/
#include <linux/kernel.h>
#include <linux/of.h>
#include "tran_sensor_cwb.h"

struct tran_sensor_cust_lcm_info {
    u32 support_state;
    u32 offset_x;
    u32 offset_y;
    u32 clip_w;
    u32 clip_h;
    u8 r_avg;
    u8 g_avg;
    u8 b_avg;
    u8 lcm_id;
    u8 cwb_state;
    u32 vendor;
    u32 algo;
    u32 ver;
    u8 enable_state;
    u32 Lsensor_x;
    u32 Lsensor_y;
    u32 Lsensor_w;
    u32 Lsensor_h;
    u32 ratio_in;
    u32 ratio_out;
};

struct tran_sensor_cwb_info {
	uint32_t r_avg;
	uint32_t g_avg;
	uint32_t b_avg;
};

struct tran_sensor_cust_param  {
    int32_t data_type;
    int8_t data[8];
} __packed __aligned(4);

enum Tran_Sensor_Data_Type {
    TRAN_LCM_POWERONOFF_INFO = 0,
    TRAN_LCM_SUPPLIER_INFO = 1,
    TRAN_LCM_INFO_MAX,
};

enum Tran_Lcm_Info {
    TRAN_SUPPLIER_LCM_0 = 0,
    TRAN_SUPPLIER_LCM_1 = 1,
    TRAN_SUPPLIER_LCM_2 = 2,
    TRAN_SUPPLIER_LCM_MAX,
};


enum Tran_Under_Screen_Sensor {
    TRAN_ML = 0,        //SENSOR_TYPE_LIGHT
    TRAN_MSL = 1,       //SENSOR_TYPE_VICE_LIGHT
    TRAN_SL = 2,        //SENSOR_TYPE_PADALS
    TRAN_SSL = 3,       //SENSOR_TYPE_VICE_PADALS
    TRAN_UNDER_SENSOR_MAX,
};

enum Tran_ml_nv_info {
    VENDORTYPE = 0,
    ALGOTYPE = 1,
    VERSION = 2,
    TRAN_ML_NV_INFO_MAX,
};

enum VENDORTYPE {
    SensorTek = 1,
    VENDORTYPE_MAX,
};

enum ALGOTYPE {
    CutOut = 1,
    ALGOTYPE_MAX,
};

enum VERSION {
    V1_0_0 = 1,
    VERSION_MAX,
};

enum Tran_CWB_UPDATE_STATE {
    CWB_INFO_ERROR = 0,         //cwb info error
    CWB_INFO_NOT_UPDATE = 1,    //cwb info do not update
    CWB_INFO_UPDATE = 2,        //cwb info update
};

extern struct tran_sensor_cust_lcm_info cust_sensor_info[TRAN_UNDER_SENSOR_MAX];
extern int __init tran_sensor_init(void);
extern void tran_sensor_exit(void);

#endif /* _TRAN_SENSOR_CUST_ */
