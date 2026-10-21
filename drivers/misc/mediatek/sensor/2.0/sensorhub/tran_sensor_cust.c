// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Transsion Inc.
 */

/*
 * Get the display location from dts
 * Created on 2023-08-18
*/
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include "tran_sensor_cust.h"


/*
 * Transsion cust device get dts info
 * Created on 2023-08-18
*/
#define CUST_LCM_INFO__ELEM_CNT 4

struct tran_sensor_cust_lcm_info  cust_sensor_info[TRAN_UNDER_SENSOR_MAX];
bool tran_sensor_un_als_support;
bool tran_sensor_ml_nv_state_get;
bool tran_sensor_sl_nv_state_get;
uint32_t nv_restore_times;


#ifdef CONFIG_SENSOR_GET_LCM_INFO
char tran_supplier_lcm0[64] = {0};
char tran_supplier_lcm1[64] = {0};
char tran_supplier_lcm2[64] = {0};
int tran_supplier_lcm_num = 0;
static int tran_lcm_info_parse_dt(struct platform_device *pdev)
{
    int i,len,ret;
    int supplier_num = 0;

    const char *supplier_info = NULL;

    char supplierx[64] = {0};

    struct device_node *np = pdev->dev.of_node;;

    printk("[tran_sensor]++++++++++%s: In(%d)++++++++++\n", __func__, __LINE__);

    /* get tp supplier map */
    ret = of_property_read_u32(np, "supplier-num", &supplier_num);
    if(0 > ret) {
        pr_err("supplier-num: can not find!");
        return ret;
    }
    tran_supplier_lcm_num = supplier_num;
    pr_info("supplier-num: %d",supplier_num);

    /* get tp supplier info */
    for(i=0; i<supplier_num; i++) {
        /* clear data */
        supplier_info = NULL;
        memset(supplierx,0,64);

        /* get supplier info */
        snprintf(supplierx, sizeof("supplier-x"), "supplier-%d", i);
        supplier_info = of_get_property(np, (const char*)supplierx, &len);
        if(supplier_info == NULL) {
            pr_err("supplier-%d: can not find!", i);
            continue;
        } else {
            pr_info("supplier_info: %s, len=%d", supplier_info, len);
            switch(i) {
                case 0:
                    if (len < sizeof(tran_supplier_lcm0)) {
                        strncpy(tran_supplier_lcm0, supplier_info, len);
                        tran_supplier_lcm0[len] = '\0';
                    } else {
                        pr_err("supplier-%d: info length exceeds buffer size!", i);
                    }
                    break; 
                case 1:
                    if (len < sizeof(tran_supplier_lcm1)) {
                        strncpy(tran_supplier_lcm1, supplier_info, len);
                        tran_supplier_lcm1[len] = '\0';
                    } else {
                        pr_err("supplier-%d: info length exceeds buffer size!", i);
                    }
                    break;
                case 2:
                    if (len < sizeof(tran_supplier_lcm2)) {
                        strncpy(tran_supplier_lcm2, supplier_info, len);
                        tran_supplier_lcm2[len] = '\0';
                    } else {
                        pr_err("supplier-%d: info length exceeds buffer size!", i);
                    }
                    break;
            }
        }
    }
    pr_info("tran_supplier_lcm0: %s, len=%d", tran_supplier_lcm0, len);
    pr_info("tran_supplier_lcm1: %s, len=%d", tran_supplier_lcm1, len);
    pr_info("tran_supplier_lcm2: %s, len=%d", tran_supplier_lcm2, len);
    printk("[tran_sensor]----------%s: Exit(%d)----------\n", __func__, __LINE__);

    return 0;
}
#endif


static int tran_sensor_get_dts_info(struct platform_device *pdev)
{
    int ret = 0;
    int i = 0;
    struct device_node *node;
    if (pdev == NULL) {
        pr_err("[%s] Exit:  pdev is a null pointer, \n", __func__);
        return -ENODEV;
    }
    pr_info("[%s]: start\n", __func__);
    node = pdev->dev.of_node;
    if (!node) {
        pr_err("[%s]Exit: fail to find tran sensor cuts node\n", __func__);
        return -ENODEV;
    }

    /*reset cwb info buffer*/
    memset(cust_sensor_info, 0, sizeof(struct tran_sensor_cust_lcm_info) * TRAN_UNDER_SENSOR_MAX);

    /********************find all under screen als sensor lcm info*******************/
    /* get under screen sensor support info */
    ret = of_property_count_u32_elems(node, "un_als_support");
    if ((ret / TRAN_UNDER_SENSOR_MAX) <= 0 || (ret % TRAN_UNDER_SENSOR_MAX) != 0) {
        tran_sensor_un_als_support = false;
        pr_err("[%s]:  get under screen sensor support info, count: %d\n", __func__, ret);
        return ret;
    } else if(ret == TRAN_UNDER_SENSOR_MAX) {
        tran_sensor_un_als_support = true;
        tran_sensor_cwb_init();
    }

    for (i = 0; i < TRAN_UNDER_SENSOR_MAX; i++) {
        ret = of_property_read_u32_index(node, "un_als_support", i, &cust_sensor_info[i].support_state);
        if (ret) {
            pr_err("%s: get under screen sensor[%d] support info fail\n", __func__, i);
        } else {
            pr_info("[%s]:cust_sensor_info[%d].support_state = %u\n", __func__, i, cust_sensor_info[i].support_state);
        }
    }
    /**********************************main als lcm info*******************************/
    if (cust_sensor_info[TRAN_ML].support_state) {
        ret = of_property_count_u32_elems(node, "ml_lcm_info");
        if ((ret / CUST_LCM_INFO__ELEM_CNT) <= 0 || (ret % CUST_LCM_INFO__ELEM_CNT) != 0) {
            pr_err("[%s]:  get [TRAN_ML]ml_lcm_info node elems failed, count: %d\n", __func__, ret);
        }

        ret = of_property_read_u32_index(node, "ml_lcm_info", 0, &cust_sensor_info[TRAN_ML].offset_x);
        if (ret) {
            pr_err("[%s]:  get [TRAN_ML].offset_x failed with err: %d\n", __func__, ret);
        }

        ret = of_property_read_u32_index(node, "ml_lcm_info", 1, &cust_sensor_info[TRAN_ML].offset_y);
        if (ret) {
            pr_err("[%s]:  get [TRAN_ML].offset_y failed with err: %d\n", __func__, ret);
        }

        ret = of_property_read_u32_index(node, "ml_lcm_info", 2, &cust_sensor_info[TRAN_ML].clip_w);
        if (ret) {
            pr_err("[%s]:  get [TRAN_ML].clip_w failed with err: %d\n", __func__, ret);
        }

        ret = of_property_read_u32_index(node, "nv_restore_time", 0, &nv_restore_times);
        if (ret) {
            pr_err("[%s]:  get [TRAN_ML].nv_restore_times failed with err: %d\n", __func__, ret);
        }

        ret = of_property_read_u32_index(node, "ml_lcm_info", 3, &cust_sensor_info[TRAN_ML].clip_h);
        if (ret) {
            pr_err("[%s]:  get [TRAN_ML].clip_h failed with err: %d\n", __func__, ret);
        }

        ret = of_property_read_u32_index(node, "extra_ml_lcm_info", 0, &cust_sensor_info[TRAN_ML].Lsensor_x);
        if (ret) {
            cust_sensor_info[TRAN_ML].Lsensor_x = 0;
            pr_err("[%s]:  get [TRAN_ML].Lsensor_x failed with err: %d\n", __func__, ret);
        }

        ret = of_property_read_u32_index(node, "extra_ml_lcm_info", 1, &cust_sensor_info[TRAN_ML].Lsensor_y);
        if (ret) {
            cust_sensor_info[TRAN_ML].Lsensor_y = 0;
            pr_err("[%s]:  get [TRAN_ML].Lsensor_y failed with err: %d\n", __func__, ret);
        }

        ret = of_property_read_u32_index(node, "extra_ml_lcm_info", 2, &cust_sensor_info[TRAN_ML].Lsensor_w);
        if (ret) {
            cust_sensor_info[TRAN_ML].Lsensor_w = 0;
            pr_err("[%s]:  get [TRAN_ML].Lsensor_w failed with err: %d\n", __func__, ret);
        }

        ret = of_property_read_u32_index(node, "extra_ml_lcm_info", 3, &cust_sensor_info[TRAN_ML].Lsensor_h);
        if (ret) {
            cust_sensor_info[TRAN_ML].Lsensor_h = 0;
            pr_err("[%s]:  get [TRAN_ML].Lsensor_h failed with err: %d\n", __func__, ret);
        }

        ret = of_property_read_u32_index(node, "extra_ml_lcm_info", 4, &cust_sensor_info[TRAN_ML].ratio_in);
        if (ret) {
            cust_sensor_info[TRAN_ML].ratio_in = 0;
            pr_err("[%s]:  get [TRAN_ML].ratio_in failed with err: %d\n", __func__, ret);
        }

        ret = of_property_read_u32_index(node, "extra_ml_lcm_info", 5, &cust_sensor_info[TRAN_ML].ratio_out);
        if (ret) {
            cust_sensor_info[TRAN_ML].ratio_out = 0;
            pr_err("[%s]:  get [TRAN_ML].ratio_out failed with err: %d\n", __func__, ret);
        }

        pr_info("%s  ML dts info offset_x=%d, offset_y=%d, clip_w=%d, clip_h=%d, extra: Lsensor_x = %d, Lsensor_y = %d, Lsensor_w = %d, \
         Lsensor_h = %d, ratio_in = %d, ratio_out = %d, nv_restore_times=%d\n", \
                __func__, \
                cust_sensor_info[TRAN_ML].offset_x, \
                cust_sensor_info[TRAN_ML].offset_y, \
                cust_sensor_info[TRAN_ML].clip_w, \
                cust_sensor_info[TRAN_ML].clip_h, \
                cust_sensor_info[TRAN_ML].Lsensor_x, \
                cust_sensor_info[TRAN_ML].Lsensor_y, \
                cust_sensor_info[TRAN_ML].Lsensor_w, \
                cust_sensor_info[TRAN_ML].Lsensor_h, \
                cust_sensor_info[TRAN_ML].ratio_in, \
                cust_sensor_info[TRAN_ML].ratio_out, \
                nv_restore_times);

        ret = of_property_count_u32_elems(node, "ml_nv_info");
        if ((ret / TRAN_ML_NV_INFO_MAX) <= 0 || (ret % TRAN_ML_NV_INFO_MAX) != 0) {
            tran_sensor_ml_nv_state_get = false;
            pr_err("[%s]:  get ml_nv_info, count: %d\n", __func__, ret);
        }else if(ret == TRAN_ML_NV_INFO_MAX){
            tran_sensor_ml_nv_state_get = true;
            pr_err("[%s]:  get ml_nv_info, success!: %d\n", __func__, ret);
            ret = of_property_read_u32_index(node, "ml_nv_info", 0, &cust_sensor_info[TRAN_ML].vendor);
            if (ret) {
                pr_err("[%s]:  get [TRAN_ML].vendor failed with err: %d\n", __func__, ret);
            }

            ret = of_property_read_u32_index(node, "ml_nv_info", 1, &cust_sensor_info[TRAN_ML].algo);
            if (ret) {
                pr_err("[%s]:  get [TRAN_ML].algo failed with err: %d\n", __func__, ret);
            }

            ret = of_property_read_u32_index(node, "ml_nv_info", 2, &cust_sensor_info[TRAN_ML].ver);
            if (ret) {
                pr_err("[%s]:  get [TRAN_ML].ver failed with err: %d\n", __func__, ret);
            }

        pr_info("%s  ML dts info vendor=%d, algo=%d, ver=%d\n", \
                __func__, \
                cust_sensor_info[TRAN_ML].vendor, \
                cust_sensor_info[TRAN_ML].algo, \
                cust_sensor_info[TRAN_ML].ver);

        }
    }
    /**********************************main sub als lcm info*******************************/
    if (cust_sensor_info[TRAN_MSL].support_state) {
        ret = of_property_count_u32_elems(node, "msl_lcm_info");
        if ((ret / CUST_LCM_INFO__ELEM_CNT) <= 0 || (ret % CUST_LCM_INFO__ELEM_CNT) != 0) {
            pr_err("[%s]:  get [TRAN_MSL]msl_lcm_info node elems failed, count: %d\n", __func__, ret);
        }

        ret = of_property_read_u32_index(node, "msl_lcm_info", 0, &cust_sensor_info[TRAN_MSL].offset_x);
        if (ret) {
            pr_err("[%s]:  get [TRAN_MSL].offset_x failed with err: %d\n", __func__, ret);
        }

        ret = of_property_read_u32_index(node, "msl_lcm_info", 1, &cust_sensor_info[TRAN_MSL].offset_y);
        if (ret) {
            pr_err("[%s]:  get [TRAN_MSL].offset_y failed with err: %d\n", __func__, ret);
        }

        ret = of_property_read_u32_index(node, "msl_lcm_info", 2, &cust_sensor_info[TRAN_MSL].clip_w);
        if (ret) {
            pr_err("[%s]:  get [TRAN_MSL].clip_w failed with err: %d\n", __func__, ret);
        }

        ret = of_property_read_u32_index(node, "msl_lcm_info", 3, &cust_sensor_info[TRAN_MSL].clip_h);
        if (ret) {
            pr_err("[%s]:  get [TRAN_MSL].clip_h failed with err: %d\n", __func__, ret);
        }

        pr_info("%s  MSL dts info offset_x=%d, offset_y=%d, clip_w=%d, clip_h=%d\n", \
                __func__, \
                cust_sensor_info[TRAN_MSL].offset_x, \
                cust_sensor_info[TRAN_MSL].offset_y, \
                cust_sensor_info[TRAN_MSL].clip_w, \
                cust_sensor_info[TRAN_MSL].clip_h);
    }
    /**********************************sub als lcm info***********************************/
    if (cust_sensor_info[TRAN_SL].support_state) {
        ret = of_property_count_u32_elems(node, "sl_lcm_info");
        if ((ret / CUST_LCM_INFO__ELEM_CNT) <= 0 || (ret % CUST_LCM_INFO__ELEM_CNT) != 0) {
            pr_err("[%s]:  get [TRAN_SL]sl_lcm_info node elems failed, count: %d\n", __func__, ret);
        }

        ret = of_property_read_u32_index(node, "sl_lcm_info", 0, &cust_sensor_info[TRAN_SL].offset_x);
        if (ret) {
            pr_err("[%s]:  get [TRAN_SL].offset_x failed with err: %d\n", __func__, ret);
        }

        ret = of_property_read_u32_index(node, "sl_lcm_info", 1, &cust_sensor_info[TRAN_SL].offset_y);
        if (ret) {
            pr_err("[%s]:  get [TRAN_SL].offset_y failed with err: %d\n", __func__, ret);
        }

        ret = of_property_read_u32_index(node, "sl_lcm_info", 2, &cust_sensor_info[TRAN_SL].clip_w);
        if (ret) {
            pr_err("[%s]:  get [TRAN_SL].clip_w failed with err: %d\n", __func__, ret);
        }

        ret = of_property_read_u32_index(node, "sl_lcm_info", 3, &cust_sensor_info[TRAN_SL].clip_h);
        if (ret) {
            pr_err("[%s]:  get [TRAN_SL].clip_h failed with err: %d\n", __func__, ret);
        }

        ret = of_property_read_u32_index(node, "nv_restore_time", 0, &nv_restore_times);
        if (ret) {
            pr_err("[%s]:  get [TRAN_SL].nv_restore_times failed with err: %d\n", __func__, ret);
        }

        pr_info("%s  TRAN_SL dts info offset_x=%d, offset_y=%d, clip_w=%d, clip_h=%d, nv_restore_times=%d\n", \
                __func__, \
                cust_sensor_info[TRAN_SL].offset_x, \
                cust_sensor_info[TRAN_SL].offset_y, \
                cust_sensor_info[TRAN_SL].clip_w, \
                cust_sensor_info[TRAN_SL].clip_h, \
                nv_restore_times);

        ret = of_property_count_u32_elems(node, "sl_nv_info");
        if ((ret / TRAN_ML_NV_INFO_MAX) <= 0 || (ret % TRAN_ML_NV_INFO_MAX) != 0) {
            tran_sensor_sl_nv_state_get = false;
            pr_err("[%s]:  get sl_nv_info, count: %d\n", __func__, ret);
        }else if(ret == TRAN_ML_NV_INFO_MAX){
            tran_sensor_sl_nv_state_get = true;
            pr_err("[%s]:  get sl_nv_info, success!: %d\n", __func__, ret);
            ret = of_property_read_u32_index(node, "sl_nv_info", 0, &cust_sensor_info[TRAN_SL].vendor);
            if (ret) {
                pr_err("[%s]:  get [TRAN_SL].vendor failed with err: %d\n", __func__, ret);
            }

            ret = of_property_read_u32_index(node, "sl_nv_info", 1, &cust_sensor_info[TRAN_SL].algo);
            if (ret) {
                pr_err("[%s]:  get [TRAN_SL].algo failed with err: %d\n", __func__, ret);
            }

            ret = of_property_read_u32_index(node, "sl_nv_info", 2, &cust_sensor_info[TRAN_SL].ver);
            if (ret) {
                pr_err("[%s]:  get [TRAN_SL].ver failed with err: %d\n", __func__, ret);
            }

        pr_info("%s  SL dts info vendor=%d, algo=%d, ver=%d\n", \
                __func__, \
                cust_sensor_info[TRAN_SL].vendor, \
                cust_sensor_info[TRAN_SL].algo, \
                cust_sensor_info[TRAN_SL].ver);

        }
    }
    /**********************************sub sub als lcm info*******************************/
    //TDB

    tran_sensor_cwb_register();

#ifdef CONFIG_SENSOR_GET_LCM_INFO
    tran_lcm_info_parse_dt(pdev);
#endif


    return ret;
}

static int tran_sensor_probe(struct platform_device *pdev)
{
    int ret = 0;

    pr_info("tran_sensor %s: start\n", __func__);
    ret = tran_sensor_get_dts_info(pdev);
    pr_info("tran_sensor %s: end ,ret = %d\n", __func__, ret);

    return ret;
}
static int tran_sensor_remove(struct platform_device *pdev)
{
    return 0;
}

static void tran_sensor_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id tran_sensor_of_ids[] = {
    { .compatible = "mediatek,tran_sensor", },
    {}
};

static struct platform_driver tran_sensor_pdrv = {
    .probe = tran_sensor_probe,
    .remove = tran_sensor_remove,
    .shutdown = tran_sensor_shutdown,
    .driver = {
        .name = "tran_sensor",
        .owner = THIS_MODULE,
        .of_match_table = tran_sensor_of_ids,
    },
};

int __init tran_sensor_init(void)
{
    int ret = 0;
    pr_info("tran_sensor tran_sensor_init start\n");

    /*reset support flag*/
    tran_sensor_un_als_support = false;
    tran_sensor_ml_nv_state_get = false;
    tran_sensor_sl_nv_state_get = false;
    ret = platform_driver_register(&tran_sensor_pdrv);
    if (ret) {
        pr_err("tran_sensor platform drv reg fail,ret:%d\n", ret);
        goto err_exit;
    }

    pr_notice("[%s]: init done\n", __func__);

    return 0;

err_exit:
    return ret;
}

void tran_sensor_exit(void)
{
    platform_driver_unregister(&tran_sensor_pdrv);
}
