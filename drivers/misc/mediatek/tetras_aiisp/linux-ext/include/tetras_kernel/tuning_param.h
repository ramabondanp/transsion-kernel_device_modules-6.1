/*
 * (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C3 within Tetras.AI
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author        Notes
 * 2023-03-08     Guxiao        initialize
 */

/**
 * @brief   ai_isp tuning param interface
 * @date    2023-03-08
 */

#ifndef __TUNING_PARAM_H__
#define __TUNING_PARAM_H__

#ifdef __cplusplus
extern "C" {
#endif

#define SENSOR_MODE_MAX         15
#define SENSOR_COUNT_MAX        3
#define SENSOR_NAME_STRING_CNT  16
#define TUNINGBIN_INFO_LENGTH   32
#define TUNING_BIN_MAGIC        0x20230803

#define IP_VERSION_INVALID      0xFAFAFFAA

enum platform_ver {
    INVALID,
    QUALCOMM,
    MTK,
};

struct param_ver_t {
    uint32_t major;
    uint32_t minor;
    uint32_t incr;
};

struct ip_version {
    uint32_t hw_ver;        /* hardware version of this ip */
    uint32_t algo_ver;      /* auto calc version for this ip */
    struct param_ver_t param_ver;     /* current parameter version of this ip */
};

struct sensor_mode {
    uint32_t id;
    uint32_t pp_mask; /* OR logic with BIT(ISP_PP_XX) */
    uint32_t offset;
    uint32_t size;
};

struct sensor_param {
    uint32_t id;
    char name[SENSOR_NAME_STRING_CNT];
    uint32_t mode_cnt;
    struct sensor_mode mode[SENSOR_MODE_MAX];     /* max 5 mode */
    uint32_t offset;
    uint32_t size;
};

struct ai_isp_param {               /* xfer in ipc cmd */
    uint32_t magic;
    union {
        struct {
            uint32_t minor : 24;
            uint32_t major : 8;
        } version_bits;
        uint32_t param_version;
    };
    uint32_t sensor_cnt;
    char hostname[TUNINGBIN_INFO_LENGTH];
    char compile_timestamp[TUNINGBIN_INFO_LENGTH];
    char gitbranch[TUNINGBIN_INFO_LENGTH];
    char project[TUNINGBIN_INFO_LENGTH];
    struct sensor_param sensor[SENSOR_COUNT_MAX];  /* max 3 sensor */
    uint32_t data_offset;
    uint32_t data_size;
};

static inline int
sensor_id_to_idx(struct ai_isp_param *param, int sensor_id, uint32_t *sensor_idx)
{
    uint32_t i;
    struct sensor_param *sensor;

    if (param->sensor_cnt > SENSOR_COUNT_MAX)
        return -1;

    for (i = 0; i < param->sensor_cnt; i++) {
        sensor = &param->sensor[i];
        if (sensor->id == (uint32_t)sensor_id) {
            *sensor_idx = i;
            break;
        }
    }

    if (i == param->sensor_cnt)
        return -1;

    return 0;
}

static inline int
sensor_mode_id_to_idx(struct ai_isp_param *param,
                      int sensor_id, int mode_id,
                      uint32_t *sensor_idx, uint32_t *mode_idx)
{
    uint32_t i;
    struct sensor_param *sensor;
    struct sensor_mode *mode;

    if (param->sensor_cnt > SENSOR_COUNT_MAX)
        return -1;

    for (i = 0; i < param->sensor_cnt; i++) {
        sensor = &param->sensor[i];
        if (sensor->id == (uint32_t)sensor_id) {
            *sensor_idx = i;
            break;
        }
    }

    if (i == param->sensor_cnt)
        return -1;

    if (mode_id < 0)
        return 0;

    sensor = &param->sensor[*sensor_idx];
    for (i = 0; i < sensor->mode_cnt; i++) {
        mode = &sensor->mode[i];
        if (mode->id == (uint32_t)mode_id) {
            *mode_idx = i;
            break;
        }
    }

    if (i == sensor->mode_cnt)
        return -1;

    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* __TUNING_PARAM_H__ */
