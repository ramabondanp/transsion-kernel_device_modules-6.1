/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Change Logs:
 * Date         Author          Notes
 * 2022-03-03   Yao Kun         Initialize.
 */

#ifndef __CAM_AIISP_DEV_H__
#define __CAM_AIISP_DEV_H__

#define AIISP_HW_MAX_DEV_NAME  64
#define CAM_AIISP_DEVICE_NAME              "cam_aiisp_external_ko"
#define CAM_AIISP_NAME                     "cam_aiisp_external"

#define CAM_AIISP_MAX_V4L2_EVENTS           250
#define CAM_AI_ISP_VC_DT_CFG_MAX            4

/* Device type for sync device needed for device discovery */
#define CAM_AIISP_DEVICE_TYPE                     (MEDIA_ENT_F_OLD_BASE + 18)

/* V4L event which user space will subscribe to */
#define CAM_AIISP_V4L_EVENT                       (V4L2_EVENT_PRIVATE_START + 0)

#define CAM_AIISP_V4L_EVENT_ID              0

/* Top level common sync event reason types */
#define CAM_AIISP_COMMON_EVENT_START        0
#define CAM_AIISP_COMMON_EVENT_UNUSED       (CAM_AIISP_COMMON_EVENT_START + 0)
#define CAM_AIISP_COMMON_EVENT_SUCCESS      (CAM_AIISP_COMMON_EVENT_START + 1)
#define CAM_AIISP_COMMON_EVENT_FLUSH        (CAM_AIISP_COMMON_EVENT_START + 2)
#define CAM_AIISP_COMMON_EVENT_STOP         (CAM_AIISP_COMMON_EVENT_START + 3)
#define CAM_AIISP_COMMON_EVENT_SYNX         (CAM_AIISP_COMMON_EVENT_START + 4)
#define CAM_AIISP_COMMON_REG_PAYLOAD_EVENT  (CAM_AIISP_COMMON_EVENT_START + 5)
#define CAM_AIISP_COMMON_AIISP_SIGNAL_EVENT (CAM_AIISP_COMMON_EVENT_START + 6)
#define CAM_AIISP_COMMON_RELEASE_EVENT      (CAM_AIISP_COMMON_EVENT_START + 7)
#define CAM_AIISP_COMMON_EVENT_END          (CAM_AIISP_COMMON_EVENT_START + 50)

/* ISP Sync event reason types */
#define CAM_AIISP_ISP_EVENT_START           (CAM_AIISP_COMMON_EVENT_END + 1)
#define CAM_AIISP_ISP_EVENT_UNKNOWN         (CAM_AIISP_ISP_EVENT_START + 0)
#define CAM_AIISP_ISP_EVENT_BUBBLE          (CAM_AIISP_ISP_EVENT_START + 1)
#define CAM_AIISP_ISP_EVENT_OVERFLOW        (CAM_AIISP_ISP_EVENT_START + 2)
#define CAM_AIISP_ISP_EVENT_P2I_ERROR       (CAM_AIISP_ISP_EVENT_START + 3)
#define CAM_AIISP_ISP_EVENT_VIOLATION       (CAM_AIISP_ISP_EVENT_START + 4)
#define CAM_AIISP_ISP_EVENT_BUSIF_OVERFLOW  (CAM_AIISP_ISP_EVENT_START + 5)
#define CAM_AIISP_ISP_EVENT_FLUSH           (CAM_AIISP_ISP_EVENT_START + 6)
#define CAM_AIISP_ISP_EVENT_HW_STOP         (CAM_AIISP_ISP_EVENT_START + 7)
#define CAM_AIISP_ISP_EVENT_BAD_FRAME       (CAM_AIISP_ISP_EVENT_START + 8)
#define CAM_AIISP_ISP_EVENT_END             (CAM_AIISP_ISP_EVENT_START + 50)

#define CAM_AIISP_EVENT_CNT                 7
#define CAM_AIISP_EVENT_REASON_CODE_INDEX   0

/**
 * struct cam_private_ioctl_arg - Sync driver ioctl argument
 *
 * @id:         IOCTL command id
 * @size:       Size of command payload
 * @result:     Result of command execution
 * @reserved:   Reserved
 * @ioctl_ptr:  Pointer to user data
 */
struct cam_private_ioctl_arg {
	__u32 id;
	__u32 size;
	__u32 result;
	__u32 reserved;
	__u64 ioctl_ptr;
};

#define CAM_PRIVATE_IOCTL_CMD \
	_IOWR('V', BASE_VIDIOC_PRIVATE, struct cam_private_ioctl_arg)

#define CAM_AIISP_ADD_REQ			0
#define CAM_AIISP_SEND_CMD			1
#define CAM_AIISP_WRITE_SRAM			2
#define CAM_AIISP_READ_SRAM			3
#define CAM_AIISP_POWER_ON			4
#define CAM_AIISP_POWER_OFF			5
#define CAM_AIISP_ACTION_MASK			6
#define CAM_AIISP_CONFIG			7
#define CAM_AIISP_DOWNLOAD_TUNING		8
#define CAM_AIISP_GET_STREAM_CAP		9
#define CAM_AIISP_GET_OPEN_TIMES		10

/**
 * struct cam_aiisp_ev_header - Event header for sync event notification
 *
 * @sync_obj:    Sync object
 * @status:      Status of the object
 * @version:     sync driver version
 * @evt_param:   event parameter
 */
struct cam_aiisp_ev_header {
	int32_t status;
	uint32_t version;
	uint32_t evt_param[CAM_AIISP_EVENT_CNT];
};

#endif //__UAPI_CAM_AI_ISP_H__
