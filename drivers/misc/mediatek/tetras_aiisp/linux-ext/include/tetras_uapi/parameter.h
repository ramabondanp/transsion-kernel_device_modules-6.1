/*
 * (C) Copyright 2024, Imvision Co., Ltd
 * This file is classified as confidential level C3 within Imvision
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *
 * Change Logs:
 * Date         Author          Notes
 * 2022-04-05   Zhao MingHui    Initialize.
 */

/**
 * @brief just for share param functions
 * @date  2022-04-012
 */

#ifndef _PARAMETER_SHARE_H__
#define _PARAMETER_SHARE_H__

#ifdef LINUX_KERNEL
#include <linux/types.h>
#else
#include <stdint.h>
#endif /* LINUX_KERNEL */

/* Stream macro for handle */
#define STREAM_SENSOR_ID_OFFSET     0
#define STREAM_SENSOR_ID_MSK        0xf
#define STREAM_STREAM_CNT_OFFSET    4
#define STREAM_STREAM_CNT_MSK       0xf
#define STREAM_MODE_ID_OFFSET       8
#define STREAM_MODE_ID_MSK          0xff
#define STREAM_PNO_OFFSET           16
#define STREAM_PNO_MSK              0xff

#define STREAM_MAGIC_OFFSET         28
#define STREAM_MAGIC_MSK            0xf
#define STREAM_MAGIC_NUM            0x6

#define CAM_STREAM_HANDLE(_sid, _cnt, _mid, _pno) \
    ((((_sid) & STREAM_SENSOR_ID_MSK) << STREAM_SENSOR_ID_OFFSET) | \
     (((_cnt) & STREAM_STREAM_CNT_MSK) << STREAM_STREAM_CNT_OFFSET) | \
     (((_mid) & STREAM_MODE_ID_MSK) << STREAM_MODE_ID_OFFSET) | \
     (((_pno) & STREAM_PNO_MSK) << STREAM_PNO_OFFSET) | \
     ((STREAM_MAGIC_NUM & STREAM_MAGIC_MSK) << STREAM_MAGIC_OFFSET))

#define CAM_STREAM_PNO(_handle) (((_handle) >> STREAM_PNO_OFFSET) & STREAM_PNO_MSK)
#define CAM_STREAM_SENSOR_ID(_handle) \
    (((_handle) >> STREAM_SENSOR_ID_OFFSET) & STREAM_SENSOR_ID_MSK)
#define CAM_STREAM_MODE_ID(_handle) (((_handle) >> STREAM_MODE_ID_OFFSET) & STREAM_MODE_ID_MSK)
#define CAM_STREAM_STREAM_CNT(_handle) \
    (((_handle) >> STREAM_STREAM_CNT_OFFSET) & STREAM_STREAM_CNT_MSK)

#define MEDIA_CAM_3A_ZONE_CNT_MAX 8

#define MEDIA_CAM_STRM_SRC_CNT_MAX 6

#define CSC_CCM_MTX_SIZE        9
#define CSC_CCM_OFT_SIZE        3
#define MAX_NAME_LEN            (80)

#define V2_2DOL_HIST_THRE (2)

enum ae_exposure_type {
    EXPOSURE_LONG  = 0,
    EXPOSURE_SHORT,
    EXPOSURE_MIDDLE,
    EXPOSURE_MAX,
};

enum af_scenes_type {
    AF_SCENES_DEFAULT  = 0,
    AF_SCENES_FACE,
    AF_SCENES_POINTLIGHT,
    AF_SCENES_LOWLIGHT,
    AF_SCENES_ERROR
};

struct msg_sender_info {
    uint16_t pno;
    uint16_t chipid;
    uint16_t pid;
};

struct msg_context_header {
    uint16_t event_id;
    uint32_t version;
    uint32_t seq_num;
    uint32_t sub_type;
};

struct cam_msg_header {
    struct msg_sender_info sender;
    struct msg_context_header context;
};

struct cam_cmd_header {
    int32_t  engine_handle;
    int32_t  stream_handle;
    int32_t  result;
    uint32_t payload_len;
};

struct cam_cmd_file_config {
    char name[MAX_NAME_LEN];
    uint32_t file_addr;
    uint32_t file_length;
    uint32_t is_merged;
    uint32_t crc32; /* File crc32 for check, if is 0, no need check */
};

typedef union {
    struct {
        uint32_t stream_support_3a: 1;
        uint32_t ddr_use: 1;
    } b;
    uint32_t w;
} cam_stream_capability_t;

#define STREAM_IS_SUPPORT_3A(__caps)      \
    (__caps.b.stream_support_3a)

struct cam_cmd_stream_create_result {
    uint32_t stream_pno;
    cam_stream_capability_t caps;
    uint32_t pingpong_addr;
    uint32_t pingpong_single_buf_size;
    uint32_t pingpong_single_buf_num;
};

struct cam_cmd_download_tuning {
    struct cam_cmd_header hinfo;
    uint32_t sensor_id;
    uint32_t phy_addr;
    uint32_t size;
};

struct cam_cmd_stream_config {
    uint32_t sensor_id;
    uint32_t model;
    uint32_t size;
    uint32_t data;
};

enum _cam_cmd_buff_type {
    CAM_CMD_BUFF_TYPE_SHRAM = 0,
    CAM_CMD_BUFF_TYPE_DDR,
    CAM_CMD_BUFF_TYPE_SNV,
    CAM_CMD_BUFF_TYPE_RANDOM,
};

struct cam_cmd_buff {
    uint32_t type;
    uint32_t size;
};

struct cam_cmd_stream_source {
    uint32_t type;      /* use macro STREAM_COMMON_META_TYPE */
    uint32_t addr;
};

struct cam_cmd_stream_swtrig {
    uint32_t frame_id;
    uint32_t src_cnt;
    struct cam_cmd_stream_source sources[MEDIA_CAM_STRM_SRC_CNT_MAX];
};

struct cam_ae_result {
    uint32_t ae_update_states;
    float    total_gain;
    float    again;
    float    dgain;
    float    brightness;
    float    luxindex;
    float    iso100gain;
    float    expratio;
    uint32_t iso;
    int32_t  sensitivity;
    uint64_t exptime;
    float    ispgain;
    uint64_t max_exptime;
};

enum cam_ae_result_id {
    CAM_AE_RID_TOTAL_GAIN = 0,
    CAM_AE_RID_AGAIN,
    CAM_AE_RID_DGAIN,
    CAM_AE_RID_BRIGHTNESS,
    CAM_AE_RID_LUXINDEX,
    CAM_AE_RID_ISO100GAIN,
    CAM_AE_RID_EXPRATIO,
    CAM_AE_RID_ISO,
    CAM_AE_RID_SENSITIVITY,
    CAM_AE_RID_EXPTIME,
    CAM_AE_RID_ISPGAIN,
    CAM_AE_RID_MAX,
};

struct cam_awb_result {
    uint32_t  awb_update_states;
    uint32_t  color_temperature;
    float     rgain;
    float     grgain;
    float     gbgain;
    float     bgain;
    float     ccm_mtx[CSC_CCM_MTX_SIZE];
    float     ccm_oft[CSC_CCM_OFT_SIZE];
};

enum cam_awb_result_id {
    CAM_AWB_RID_COLOR_TEMP = 0,
    CAM_AWB_RID_RGAIN,
    CAM_AWB_RID_GRGAIN,
    CAM_AWB_RID_GBGAIN,
    CAM_AWB_RID_BGAIN,
    CAM_AWB_RID_CCM_MTX_0,
    CAM_AWB_RID_CCM_MTX_1,
    CAM_AWB_RID_CCM_MTX_2,
    CAM_AWB_RID_CCM_MTX_3,
    CAM_AWB_RID_CCM_MTX_4,
    CAM_AWB_RID_CCM_MTX_5,
    CAM_AWB_RID_CCM_MTX_6,
    CAM_AWB_RID_CCM_MTX_7,
    CAM_AWB_RID_CCM_MTX_8,
    CAM_AWB_RID_CCM_OFT_0,
    CAM_AWB_RID_CCM_OFT_1,
    CAM_AWB_RID_CCM_OFT_2,
    CAM_AWB_RID_MAX,
};

struct cam_af_result {
    uint32_t af_update_states;      /* afst states update */
    uint32_t af_scenes_types;       /* af current scenes type */
    int32_t lens_pos;               /* af current lens pos */
};

enum cam_af_result_id {
    CAM_AF_RID_SCENES_TYPES = 0,
    CAM_AF_RID_LENS_POS,
    CAM_AF_RID_MAX,
};

struct cam_region {
    uint32_t update_states;
    uint32_t left;
    uint32_t top;
    uint32_t width;
    uint32_t heigh;
};

struct cam_axi_wr_dump {
    uint32_t            type;
#define DUMP_IMG        BIT(0)
#define DUMP_STAT       BIT(1)
#define DUMP_TYPE_MAX   2

    uint32_t            ch;
#define DUMP_BAYER      BIT(0)
#define DUMP_W          BIT(1)
#define DUMP_PD         BIT(2)
#define DUMP_CH_MAX     3

    uint32_t            pos;
    uint32_t            expo;
    uint32_t            frame_id;
    uint32_t            status;
};

enum cam_wr_dump_pos {
    SENSOR_RAW           = 0,
    OUTPUT_RAW,
    RAW_MAX
};

enum cam_wr_dump_ch {
    WR_BAYER_CH            = 0,
    WR_W_CH,
    WR_PD_CH
};

enum wr_dump_status {
    WR_DUM_STOP         = 0,
    WR_DUM_START,
    WR_DUM_CFG
};

struct preisp_ae_align {
    uint32_t preisp_update_states;
    uint32_t dynamic_output_weight;
    uint32_t wt_tbl_idx;
    uint32_t long_opt_k;
    uint32_t mid_opt_k;
    uint32_t short_opt_k;
    uint32_t tripod_thres;
    float    real_dynamic_ratio;
};

enum preisp_align_id {
    PREISP_ALIGN_ID_DY_OUT_WEIGHT = 0,
    PREISP_ALIGN_ID_WT_TBL_IDX,
    PREISP_ALIGN_ID_LONG_OPT_K,
    PREISP_ALIGN_ID_MID_OPT_K,
    PREISP_ALIGN_ID_SHORT_OPT_K,
    PREISP_ALIGN_ID_REAL_DY_RATIO,
    PREISP_ALIGN_ID_MAX,
};

struct imu_info {
    uint32_t update_states;
    uint64_t tick;
    uint64_t fsync_tick;
    double acc_x;
    double acc_y;
    double acc_z;
    double gyro_x;
    double gyro_y;
    double gyro_z;
    uint8_t header;
    uint8_t temperature;
};

struct cam_imu_info {
    struct imu_info *info;
    uint32_t dyn_count;      /* how many imu count when dyn config */
    uint32_t start_idx;      /* start imu index when dyn config */
    uint32_t buf_num;        /* all buf count */
};

enum cam_calc_type {
    TYPE_3A_RESULT,
    TYPE_IMU,
    TYPE_INVALID,
};

struct cam_config_info {
    uint32_t cmd_type;
    void *info;
    uint32_t cur_fcnt;
};

struct preisp_aest_info {
    uint32_t aest_info_update;
    uint16_t gstat_seg_y_low_thre[V2_2DOL_HIST_THRE];
    uint16_t gstat_seg_y_mid_thre[V2_2DOL_HIST_THRE];
    uint16_t gstat_seg_y_high_thre[V2_2DOL_HIST_THRE];
};

struct preisp_aie_info {
    uint32_t aie_info_update;
    uint16_t aie_param[8];
};

struct cam_mode_result {
    uint32_t mode_update_states;
    uint32_t target_mode;
};

struct cam_3a_info {
    struct cam_ae_result       ae_results[EXPOSURE_MAX];
    struct cam_awb_result      awb_result;
    struct cam_af_result       af_result;
    uint64_t                   frame_id;
    uint64_t                   target_framid;
    uint32_t                   stats_framid;
    struct cam_region          window;
    struct cam_region          rois[MEDIA_CAM_3A_ZONE_CNT_MAX];
    struct preisp_ae_align     preisp_align_result;
    struct preisp_aest_info    aest_info;
    struct preisp_aie_info     aie_info;
    struct cam_mode_result     mode_results;
};

enum cam_3a_info_id {
    CAM_3A_INFO_ID_AE_LONG = 0,
    CAM_3A_INFO_ID_AE_SHORT,
    CAM_3A_INFO_ID_AE_MIDDLE,
    CAM_3A_INFO_ID_AWB,
    CAM_3A_INFO_ID_AF,
    CAM_3A_INFO_ID_ALIGN,
    CAM_3A_INFO_ID_MAX,
};

enum cam_3a_info_type {
    CAM_3A_INFO_TYPE_AE = 0,
    CAM_3A_INFO_TYPE_AWB,
    CAM_3A_INFO_TYPE_AF,
    CAM_3A_INFO_TYPE_ALIGN,
    CAM_3A_INFO_TYPE_MAX,
};

enum cam_3a_debug_cmd {
    CAM_3A_DBG_CMD_INVAL = 0,
    CAM_3A_DBG_CMD_SUSPEND_IP,
    CAM_3A_DBG_CMD_RESUME_IP,
    CAM_3A_DBG_CMD_SHOW_IP,
    CAM_3A_DBG_CMD_FIX_PARAM,
    CAM_3A_DBG_CMD_CLEAR_PARAM,
    CAM_3A_DBG_CMD_SHOW_PARAM,
    CAM_3A_DBG_CMD_ACTIVE,
};

struct cam_3a_debug {
    uint32_t cmd;
    char     *first_name;
    char     *second_name;
    void     *val;
};

struct cam_3a_active {
    uint32_t is_active;
    uint32_t is_compare;
};

enum cam_3a_help_cmd {
    CAM_3A_HELP_CMD_INVAL = 0,
    CAM_3A_HELP_CMD_DYP_IP,
    CAM_3A_HELP_CMD_DYP_PARAM,
};

struct cam_cmd_config {
    struct cam_cmd_header hinfo;
    struct cam_3a_info    info;
};

enum cam_frame_info_tag {
    CAM_FRAME_BLACK_LEVEL = 0,
    CAM_FRAME_INFO_MAX,
};

struct cam_frame_info_data {
    enum cam_frame_info_tag      tag;
    uint8_t                      is_valid;
    void                         *data;
    uint32_t                     data_size;
};

struct cam_frame_info {
    uint64_t                      frame_id;
    uint32_t                      node_id;
    uint16_t                      stream_idx;
    struct cam_frame_info_data    *data;
};

#endif /*_PARAMETER_SHARE_H__*/
