// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Transsion Inc.
 */

#include <linux/proc_fs.h>
#include <linux/slab.h>
#include "tran_sensor_cust.h"
#include "tran_sensor_cwb.h"


int32_t g_tran_debug_mode = 0;


struct tran_sensor_cwb_info als_cwb_info[TRAN_UNDER_SENSOR_MAX];
struct tran_sensor_cwb_info last_als_cwb_info[TRAN_UNDER_SENSOR_MAX];
extern struct tran_sensor_cust_lcm_info cust_sensor_info[TRAN_UNDER_SENSOR_MAX];


#if IS_ENABLED(CONFIG_TRANSSION_CWB_API_SUPPORT)
Transsion_CWB_Sensor_Info cust_sensor_rgb_info[TRAN_UNDER_SENSOR_MAX];
#endif

#if !IS_ENABLED(CONFIG_TRANSSION_CWB_API_SUPPORT)
Transsion_CWB_Sensor_Info_MOCK cust_sensor_rgb_info[TRAN_UNDER_SENSOR_MAX];

static void transsion_cwb_init(void)
{
    pr_err("[%s] [tran_sensor][Mock Test Function]\n", __func__);
}
static int transsion_cwb_enable(bool en, u8 lcm_id)
{
    (void)en;
    (void)lcm_id;
    pr_err("[%s] [tran_sensor][Mock Test Function]\n", __func__);
    return 0;
}

static int transsion_cwb_registor(u8 lcm_id, u8 sensor_id, unsigned int  offset_x, unsigned int offset_y, unsigned int clip_w, unsigned int clip_h, \
                unsigned int  Lsensor_x, unsigned int  Lsensor_y, unsigned int  Lsensor_w, unsigned int  Lsensor_h, unsigned int  ratio_in, unsigned int  ratio_out)
{
    (void)lcm_id;
    (void)sensor_id;
    (void)offset_x; //912
    (void)offset_y; //49
    (void)clip_w;   //48
    (void)clip_h;   //55
    (void)Lsensor_x; //920  IC感光区X
    (void)Lsensor_y; //75   IC感光区Y
    (void)Lsensor_w; //27   IC宽
    (void)Lsensor_h; //25   IC高
    (void)ratio_in;  //80   IC内占比，与供应商确认 默认值80
    (void)ratio_out; //20   IC外占比，与供应商确认 默认值20

    pr_err("[%s] [tran_sensor][Mock Test Function]\n", __func__);
    return 0;
}

static void transsion_get_cwb_rgb_info(Transsion_CWB_Sensor_Info_MOCK *rgb_buf)
{
    (void)rgb_buf;
    pr_err("[%s] [tran_sensor][Mock Test Function]\n", __func__);
}
#endif

#if IS_ENABLED(CONFIG_TRANSSION_CWB_API_SUPPORT)
Transsion_CWB_Sensor_Info cwb_rgb_to_mid[TRAN_UNDER_SENSOR_MAX];
#endif

#if !IS_ENABLED(CONFIG_TRANSSION_CWB_API_SUPPORT)
Transsion_CWB_Sensor_Info_MOCK cwb_rgb_to_mid[TRAN_UNDER_SENSOR_MAX];
#endif

static ssize_t cwb_status_write(struct file *filp, const char __user * buf, size_t count, loff_t * off)
{
    int32_t ret;
    int32_t tmp;
    uint8_t tran_debug_mode;
    char buff[2]={'0','\0'};

    if (count == 0 || count > 2) {
        TRAN_ERROR("Invalid value! count = %zu", count);
        ret = -EINVAL;
        goto out;
    }
    ret = copy_from_user(buff, buf, 1);
    if (ret) {
        TRAN_ERROR("copy_from_user fail.");
        return -EPERM;
    }

    ret = sscanf(buff, "%d", &tmp);
    if (ret != 1) {
        TRAN_ERROR("Invalid value! ret = %d", ret);
        ret = -EINVAL;
        goto out;
    }
    if (tmp != 0 && tmp != 1 && tmp != 2 && tmp != 3 && tmp != 4 && tmp != 5) {
        TRAN_ERROR("Invalid value! ret = %d, tran_debug_mode val = %d", ret, tmp);
        return -EINVAL;
        goto out;
    }

    tran_debug_mode = tmp;
    TRAN_ERROR("tran_debug_mode = %d", tran_debug_mode);

    g_tran_debug_mode = tran_debug_mode;

    ret = count;

out:
    return ret;
}
/*
	u8 sensor_id;
	u8 r_avg;
	u8 g_avg;
	u8 b_avg;
    */
static ssize_t cwb_status_read(struct file *file, char * buffer, size_t size, loff_t * ppos)
{
    int len,ret = -1;
    char *page = NULL;
    char *ptr = NULL;
    uint8_t i = 0;
#if IS_ENABLED(CONFIG_TRANSSION_CWB_API_SUPPORT)
    Transsion_CWB_Sensor_Info adb_rgb_map[TRAN_UNDER_SENSOR_MAX];
    memset(cwb_rgb_to_mid, 0, sizeof(Transsion_CWB_Sensor_Info) * TRAN_UNDER_SENSOR_MAX);
    memset(adb_rgb_map, 0, sizeof(Transsion_CWB_Sensor_Info) * TRAN_UNDER_SENSOR_MAX);
#endif

#if !IS_ENABLED(CONFIG_TRANSSION_CWB_API_SUPPORT)
    Transsion_CWB_Sensor_Info_MOCK adb_rgb_map[TRAN_UNDER_SENSOR_MAX];
    memset(cwb_rgb_to_mid, 0, sizeof(Transsion_CWB_Sensor_Info_MOCK) * TRAN_UNDER_SENSOR_MAX);
    memset(adb_rgb_map, 0, sizeof(Transsion_CWB_Sensor_Info_MOCK) * TRAN_UNDER_SENSOR_MAX);
#endif
    transsion_get_cwb_rgb_info(cwb_rgb_to_mid);
    page = kmalloc(PAGE_SIZE, GFP_KERNEL);
    if (!page)
    {
        kfree(page);
        return -ENOMEM;
    }
    ptr = page;

    /*抠图数据给出丁银是按照注册顺序放到数组中给出来的,并不是按照注册的时候填充的sensor id(ML/MSL/SL/SSL)顺序，
    但是一个Transsion_CWB_Sensor_Info结构体成员中的sensor_id会告知当前rgb数据是哪个sensor id（ML/MSL/SL/SSL）的 抠图数据*/

    /*map lcm rgb info to sensor als_cwb_info  */
    for (i = 0; i < TRAN_UNDER_SENSOR_MAX; i++) {
        if (cwb_rgb_to_mid[i].sensor_id == TRAN_ML) {
            if (cust_sensor_info[TRAN_ML].support_state) {
                adb_rgb_map[TRAN_ML].sensor_id = SENSOR_TYPE_LIGHT;
            } else {
                adb_rgb_map[TRAN_ML].sensor_id = cwb_rgb_to_mid[i].sensor_id;
            }
            adb_rgb_map[TRAN_ML].r_avg = cwb_rgb_to_mid[i].r_avg;
            adb_rgb_map[TRAN_ML].g_avg = cwb_rgb_to_mid[i].g_avg;
            adb_rgb_map[TRAN_ML].b_avg = cwb_rgb_to_mid[i].b_avg;
        }else if (cwb_rgb_to_mid[i].sensor_id == TRAN_MSL) {
            adb_rgb_map[TRAN_MSL].sensor_id = cwb_rgb_to_mid[i].sensor_id;
            adb_rgb_map[TRAN_MSL].r_avg = cwb_rgb_to_mid[i].r_avg;
            adb_rgb_map[TRAN_MSL].g_avg = cwb_rgb_to_mid[i].g_avg;
            adb_rgb_map[TRAN_MSL].b_avg = cwb_rgb_to_mid[i].b_avg;
        }else if (cwb_rgb_to_mid[i].sensor_id == TRAN_SL) {
            if (cust_sensor_info[TRAN_SL].support_state) {
                adb_rgb_map[TRAN_SL].sensor_id = SENSOR_TYPE_PADALS;
            } else {
                adb_rgb_map[TRAN_SL].sensor_id = cwb_rgb_to_mid[i].sensor_id;
            }
            adb_rgb_map[TRAN_SL].r_avg = cwb_rgb_to_mid[i].r_avg;
            adb_rgb_map[TRAN_SL].g_avg = cwb_rgb_to_mid[i].g_avg;
            adb_rgb_map[TRAN_SL].b_avg = cwb_rgb_to_mid[i].b_avg;
        }else if (cwb_rgb_to_mid[i].sensor_id == TRAN_SSL) {
            adb_rgb_map[TRAN_SSL].sensor_id = cwb_rgb_to_mid[i].sensor_id;
            adb_rgb_map[TRAN_SSL].r_avg = cwb_rgb_to_mid[i].r_avg;
            adb_rgb_map[TRAN_SSL].g_avg = cwb_rgb_to_mid[i].g_avg;
            adb_rgb_map[TRAN_SSL].b_avg = cwb_rgb_to_mid[i].b_avg;
        }
    }
    /**/
    if ((cust_sensor_info[TRAN_ML].support_state == 1) && (cust_sensor_info[TRAN_SL].support_state == 0)) {
        ptr += sprintf(ptr,"%d:%d:%d:%d\n%d:%d:%d:%d\n%d:%d:%d:%d\n%d:%d:%d:%d\n",adb_rgb_map[TRAN_ML].sensor_id,adb_rgb_map[TRAN_ML].r_avg,adb_rgb_map[TRAN_ML].g_avg,adb_rgb_map[TRAN_ML].b_avg,
                    adb_rgb_map[TRAN_MSL].sensor_id,adb_rgb_map[TRAN_MSL].r_avg,adb_rgb_map[TRAN_MSL].g_avg,adb_rgb_map[TRAN_MSL].b_avg,
                    adb_rgb_map[TRAN_SL].sensor_id,adb_rgb_map[TRAN_SL].r_avg,adb_rgb_map[TRAN_SL].g_avg,adb_rgb_map[TRAN_SL].b_avg,
                    adb_rgb_map[TRAN_SSL].sensor_id,adb_rgb_map[TRAN_SSL].r_avg,adb_rgb_map[TRAN_SSL].g_avg,adb_rgb_map[TRAN_SSL].b_avg);
    } else if ((cust_sensor_info[TRAN_ML].support_state == 0) && (cust_sensor_info[TRAN_SL].support_state == 1)) {
        ptr += sprintf(ptr,"%d:%d:%d:%d\n%d:%d:%d:%d\n%d:%d:%d:%d\n%d:%d:%d:%d\n",adb_rgb_map[TRAN_SL].sensor_id,adb_rgb_map[TRAN_SL].r_avg,adb_rgb_map[TRAN_SL].g_avg,adb_rgb_map[TRAN_SL].b_avg,
                    adb_rgb_map[TRAN_ML].sensor_id,adb_rgb_map[TRAN_ML].r_avg,adb_rgb_map[TRAN_ML].g_avg,adb_rgb_map[TRAN_ML].b_avg,
                    adb_rgb_map[TRAN_MSL].sensor_id,adb_rgb_map[TRAN_MSL].r_avg,adb_rgb_map[TRAN_MSL].g_avg,adb_rgb_map[TRAN_MSL].b_avg,
                    adb_rgb_map[TRAN_SSL].sensor_id,adb_rgb_map[TRAN_SSL].r_avg,adb_rgb_map[TRAN_SSL].g_avg,adb_rgb_map[TRAN_SSL].b_avg);
    }
    len = ptr - page;
    if(*ppos >= len)
    {
        kfree(page);
        return 0;
    }
    ret = copy_to_user(buffer,(char *)page,len);
    *ppos += len;
    if(ret)
    {
        kfree(page);
        return ret;
    }
    kfree(page);
    return len;

}

static const struct proc_ops cwb_fops = {
    .proc_read = cwb_status_read,
    .proc_write = cwb_status_write,
};


/*                debug interface                  */
void tran_sensor_dump_get_cwb_info(void)
{
    uint8_t i = 0;
    if (g_tran_debug_mode) {
        for (i = 0; i < TRAN_UNDER_SENSOR_MAX; i++) {
            if (cust_sensor_info[i].support_state) {
                pr_info("[%s] cust_sensor_rgb_info[%d], sensor_id[%d], r_avg=%d, g_avg=%d, b_avg=%d\n", \
                        __func__, i, \
                        cust_sensor_rgb_info[i].sensor_id, \
                        cust_sensor_rgb_info[i].r_avg, \
                        cust_sensor_rgb_info[i].g_avg, \
                        cust_sensor_rgb_info[i].b_avg);
                pr_info("[%s] als_cwb_info[%d], r_avg=%d, g_avg=%d, b_avg=%d\n", \
                        __func__, i, \
                        als_cwb_info[i].r_avg, \
                        als_cwb_info[i].g_avg, \
                        als_cwb_info[i].b_avg);
            }
        }
    }
}

void tran_sensor_cwb_register(void)
{
    uint8_t i = 0;
    uint8_t ret = 0;
    for (i = 0; i < TRAN_UNDER_SENSOR_MAX; i++) {

        /*Settings lcm id*/
        if ((i == TRAN_ML) || (i == TRAN_MSL)) {
            cust_sensor_info[i].lcm_id = TRANSSION_DISP_CWB_MAIN_LCM;
        } else if ((i == TRAN_SL) || (i == TRAN_SSL)) {
            cust_sensor_info[i].lcm_id = TRANSSION_DISP_CWB_SUB_LCM;
        }

        /*Dynamic registration according to dts*/
        if (cust_sensor_info[i].support_state) {
            ret = transsion_cwb_registor(cust_sensor_info[i].lcm_id, i, \
                                            cust_sensor_info[i].offset_x, cust_sensor_info[i].offset_y, \
                                            cust_sensor_info[i].clip_w, cust_sensor_info[i].clip_h, \
                                            cust_sensor_info[i].Lsensor_x, cust_sensor_info[i].Lsensor_y,\
                                            cust_sensor_info[i].Lsensor_w, cust_sensor_info[i].Lsensor_h, \
                                            cust_sensor_info[i].ratio_in, cust_sensor_info[i].ratio_out);
            if (ret == 0) {
                pr_info("[%s][Sucess]:lcm_id[%d],cust_sensor_info[%d].support_state = %u\n", __func__, cust_sensor_info[i].lcm_id, \
                        i, cust_sensor_info[i].support_state);
            } else {
                pr_err("[%s][fail]:lcm_id[%d],cust_sensor_info[%d].support_state = %u\n", __func__, cust_sensor_info[i].lcm_id, \
                        i, cust_sensor_info[i].support_state);
            }

        } else {
            pr_info("[%s][fail because it does not support cwb]:lcm_id[%d],cust_sensor_info[%d].support_state = %u\n", __func__, cust_sensor_info[i].lcm_id, \
                        i, cust_sensor_info[i].support_state);
        }
    }
}

int tran_sensor_cwb_enable(uint8_t sensor_type, bool en)
{
    uint8_t sensor_id = 0;
    int ret  = 0;
    uint8_t i = 0;

    /*reset cwb info buffer*/
#if IS_ENABLED(CONFIG_TRANSSION_CWB_API_SUPPORT)
    memset(cust_sensor_rgb_info, 0, sizeof(Transsion_CWB_Sensor_Info) * TRAN_UNDER_SENSOR_MAX);
#endif

#if !IS_ENABLED(CONFIG_TRANSSION_CWB_API_SUPPORT)
    memset(cust_sensor_rgb_info, 0, sizeof(Transsion_CWB_Sensor_Info_MOCK) * TRAN_UNDER_SENSOR_MAX);
#endif

    /*reset cwb info buffer for scp*/
    //memset(als_cwb_info, 0, sizeof(struct tran_sensor_cwb_info) * TRAN_UNDER_SENSOR_MAX);
    //memset(last_als_cwb_info, 0, sizeof(struct tran_sensor_cwb_info) * TRAN_UNDER_SENSOR_MAX);
    for (i = 0; i < TRAN_UNDER_SENSOR_MAX; i++) {
        als_cwb_info[i].r_avg = 300;
        als_cwb_info[i].g_avg = 300;
        als_cwb_info[i].b_avg = 300;
        last_als_cwb_info[i].r_avg = 300;
        last_als_cwb_info[i].g_avg = 300;
        last_als_cwb_info[i].b_avg = 300;
    }

    /*enable main als cwb*/
    if (sensor_type == SENSOR_TYPE_LIGHT) {
        sensor_id = TRAN_ML;
        ret = transsion_cwb_enable(en, cust_sensor_info[sensor_id].lcm_id);
        pr_info("[%s][done]:sensor_type[%d], en[%d]\n", __func__, sensor_type, en);
    }
    /*enable main sub als cwb*/
    //TDB

    /*enable sub als cwb*/
    if (sensor_type == SENSOR_TYPE_PADALS) {
        sensor_id = TRAN_SL;
        ret = transsion_cwb_enable(en, cust_sensor_info[sensor_id].lcm_id);
        pr_info("[%s][done]:sensor_type[%d], en[%d]\n", __func__, sensor_type, en);
    }

    /*enable sub sub als cwb*/
    //TDB

    return ret;
}

void tran_sensor_cwb_init(void)
{

    struct proc_dir_entry *tran_cwb_msg = NULL;
    tran_cwb_msg = proc_create("rgb_screeninfo", 0666, NULL, &cwb_fops);
    if (tran_cwb_msg == NULL) {
        pr_err("[Sensor CWB PROC]Couldn't create proc entry[tran_cwb_msg]!");
    } else {
        pr_info("[Sensor CWB PROC]Create proc entry[tran_cwb_msg] success!");
    }


#if IS_ENABLED(CONFIG_TRANSSION_CWB_API_SUPPORT)
    memset(cust_sensor_rgb_info, 0, sizeof(Transsion_CWB_Sensor_Info) * TRAN_UNDER_SENSOR_MAX);
#endif
#if !IS_ENABLED(CONFIG_TRANSSION_CWB_API_SUPPORT)
    memset(cust_sensor_rgb_info, 0, sizeof(Transsion_CWB_Sensor_Info_MOCK) * TRAN_UNDER_SENSOR_MAX);
#endif
    tran_sensor_dump_get_cwb_info();

    transsion_cwb_init();
    pr_info("[%s][done]\n", __func__);
}

void tran_sensor_cwb_rgb_get(void)
{
    uint8_t i = 0;
#if IS_ENABLED(CONFIG_TRANSSION_CWB_API_SUPPORT)
    Transsion_CWB_Sensor_Info rgb_map[TRAN_UNDER_SENSOR_MAX];
    memset(rgb_map, 0, sizeof(Transsion_CWB_Sensor_Info) * TRAN_UNDER_SENSOR_MAX);
#endif

#if !IS_ENABLED(CONFIG_TRANSSION_CWB_API_SUPPORT)
    Transsion_CWB_Sensor_Info_MOCK rgb_map[TRAN_UNDER_SENSOR_MAX];
    memset(rgb_map, 0, sizeof(Transsion_CWB_Sensor_Info_MOCK) * TRAN_UNDER_SENSOR_MAX);
#endif

    //get cwb info for 4 position
    transsion_get_cwb_rgb_info(cust_sensor_rgb_info);

    /*抠图数据给出丁银是按照注册顺序放到数组中给出来的,并不是按照注册的时候填充的sensor id(ML/MSL/SL/SSL)顺序，
    但是一个Transsion_CWB_Sensor_Info结构体成员中的sensor_id会告知当前rgb数据是哪个sensor id（ML/MSL/SL/SSL）的 抠图数据*/

    /*map lcm rgb info to sensor als_cwb_info  */
    for (i = 0; i < TRAN_UNDER_SENSOR_MAX; i++) {
        if (cust_sensor_rgb_info[i].sensor_id == TRAN_ML) {
            rgb_map[TRAN_ML].r_avg = cust_sensor_rgb_info[i].r_avg;
            rgb_map[TRAN_ML].g_avg = cust_sensor_rgb_info[i].g_avg;
            rgb_map[TRAN_ML].b_avg = cust_sensor_rgb_info[i].b_avg;
        }else if (cust_sensor_rgb_info[i].sensor_id == TRAN_MSL) {
            rgb_map[TRAN_MSL].r_avg = cust_sensor_rgb_info[i].r_avg;
            rgb_map[TRAN_MSL].g_avg = cust_sensor_rgb_info[i].g_avg;
            rgb_map[TRAN_MSL].b_avg = cust_sensor_rgb_info[i].b_avg;
        }else if (cust_sensor_rgb_info[i].sensor_id == TRAN_SL) {
            rgb_map[TRAN_SL].r_avg = cust_sensor_rgb_info[i].r_avg;
            rgb_map[TRAN_SL].g_avg = cust_sensor_rgb_info[i].g_avg;
            rgb_map[TRAN_SL].b_avg = cust_sensor_rgb_info[i].b_avg;
        }else if (cust_sensor_rgb_info[i].sensor_id == TRAN_SSL) {
            rgb_map[TRAN_SSL].r_avg = cust_sensor_rgb_info[i].r_avg;
            rgb_map[TRAN_SSL].g_avg = cust_sensor_rgb_info[i].g_avg;
            rgb_map[TRAN_SSL].b_avg = cust_sensor_rgb_info[i].b_avg;
        }
    }

    for (i = 0; i < TRAN_UNDER_SENSOR_MAX; i++) {
        als_cwb_info[i].r_avg = rgb_map[i].r_avg;
        als_cwb_info[i].g_avg = rgb_map[i].g_avg;
        als_cwb_info[i].b_avg = rgb_map[i].b_avg;

        if (cust_sensor_info[i].support_state) {
            tran_sensor_dump_get_cwb_info();
            if ((als_cwb_info[i].r_avg != last_als_cwb_info[i].r_avg) ||
                (als_cwb_info[i].g_avg != last_als_cwb_info[i].g_avg) ||
                (als_cwb_info[i].b_avg != last_als_cwb_info[i].b_avg)) {
                    cust_sensor_info[i].cwb_state = CWB_INFO_UPDATE;
                    //tran_sensor_dump_get_cwb_info();
                }
        }

        last_als_cwb_info[i].r_avg = als_cwb_info[i].r_avg;
        last_als_cwb_info[i].g_avg = als_cwb_info[i].g_avg;
        last_als_cwb_info[i].b_avg = als_cwb_info[i].b_avg;
    }
}
