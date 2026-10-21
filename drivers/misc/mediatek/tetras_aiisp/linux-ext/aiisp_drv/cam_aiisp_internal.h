/*
 * (C) Copyright 2024, Imvision Co., Ltd
 * This file is classified as confidential level C4 within Imvision
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date         Author          Notes
 * 2022-03-03   Yao Kun         Initialize.
 */

/**
 * @brief AI ISP Driver Header
 * @date  2022-03-03
 */

#ifndef __CAM_AI_ISP_DEV_H__
#define __CAM_AI_ISP_DEV_H__

#include <media/v4l2-fh.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <asm/atomic.h>
#include "cam_aiisp.h"
#include "comm_drv_io.h"
#include "parameter.h"

#ifdef QCOM_PLATFORM_CRM
#include "cam_aiisp_qcom_dev.h"
#else
#include "cam_aiisp_mtk_dev.h"
#endif

#define CAM_AIISP       (1 << 7)

#define CAM_AIISP_WORKQUEUE_NAME           "HIPRIO_AIISP_WORK_QUEUE"

/* Specific event ids to get notified in user space */
#define CAM_AIISP_V4L_EVENT_ID_CB_TRIG            0

/* Size of opaque payload sent to kernel for safekeeping until signal time */
#define CAM_AIISP_USER_PAYLOAD_SIZE               2

#define CAM_AIISP_GET_PAYLOAD_PTR(ev, type)       \
	(type *)((char *)ev.u.data + sizeof(struct cam_aiisp_ev_header))

#define CAM_AIISP_GET_HEADER_PTR(ev)              \
	((struct cam_aiisp_ev_header *)ev.u.data)

#define CAM_AIISP_STATE_INVALID                   0
#define CAM_AIISP_STATE_ACTIVE                    1
#define CAM_AIISP_STATE_SIGNALED_SUCCESS          2
#define CAM_AIISP_STATE_SIGNALED_ERROR            3
#define CAM_AIISP_STATE_SIGNALED_CANCEL           4

#define CAM_AIISP_DEVICE_NUM                      3

/* data ptr type of ioctl comand */
#define DATA_PTR_USER_TYPE 0
#define DATA_PTR_KERNEL_TYPE 1

/* no aiisp stream id and session id */
#define NO_AIISP_RTOS_SESSION 20
#define NO_AIISP_RTOS_STREAM  50

#define IPC_CHIP_ID  1
#define SEND_TO_RTOS_PNO_ID    20
#define RECV_FROM_RTOS_PNO_ID  20

#ifdef USE_MTK_EXTISP
/* for MTK FRAME SYNC */
enum FRAME_SYNC_STATE {
    FRAME_SYNC_OK,
    FRAME_MISMATCHED,
    FRAME_SYNCING,
    FRAME_SYNC_ERR,
};
#endif

typedef void (*cam_fsync_func_t)(void);

enum SRAM_OPS {
	SRAM_WR,
	SRAM_RD,
};

struct missed_req {
	uint64_t missed_reqid;
	uint32_t missed_cnt;
};

/**
 * struct sensor_req_mgr - Internal struct to hold sensor req info
 *
 * @reqid2fcnt		：reqid - framecnt
 * @update_hint		: flag that identifies reqid2fcnt needs update
 * @fcnt			: number of sof recored by aiisp chip
 * @reqs			: array to store requests
 * @tasks			: array of incomming tasks, both put&get requests.
 */
struct sensor_req_mgr {
	int fcnt;
	int reqid2fcnt;
	uint64_t last_apply_reqid;
	uint64_t last_reg_reqid;
	uint64_t to_apply_reqid;
	spinlock_t reqid_lock;
	bool th_stop;
	struct missed_req reqs[4];
	unsigned long missed_bitmap;
#ifdef QCOM_PLATFORM_CRM
	struct sensor_info info;
#endif
};

struct msg_sync {
	uint32_t seq_num;
	struct completion comp;
	struct list_head node;
	struct cam_cmd_header hinfo;
	uint32_t pno;
	uint32_t cmd;
	int32_t result;
};

struct aiisp_stream_info {
	int32_t stream_id;
	int32_t session_id;
	uint32_t sensor_id;
	uint32_t used;
	uint64_t curr_frame_id;
	struct target_id send_tid;
	struct target_id recv_tid;

	cam_stream_capability_t caps;
	struct pingpong_handle *pingpong;
};

/**
 * struct aiisp_sync_sensor_info
 *
 * @frame_sync_state: frame sync state for MTK
 */
struct aiisp_sync_sensor_info {
	uint32_t sensor_id;
	uint32_t fsync_gpio;
	uint32_t irq_num;
	uint32_t slot_idx;
#ifdef USE_MTK_EXTISP
	atomic_t frame_sync_state;
#endif
	struct aiisp_device *aiisp_dev;
};

/**
 * struct sync_device - Internal struct to book keep sync driver details
 *
 * @vdev            : Video device
 * @v4l2_dev        : V4L2 device
 * @sync_table      : Table of all sync objects
 * @row_spinlocks   : Spinlock array, one for each row in the table
 * @open_cnt        : Count of file open calls made on the sync driver
 * @dentry          : Debugfs entry
 * @work_queue      : Work queue used for dispatching kernel callbacks
 * @cam_sync_eventq : Event queue used to dispatch user payloads to user space
 * @bitmap          : Bitmap representation of all sync objects
 * @params          : Parameters for synx call back registration
 * @version         : version support
 * @sns_req_mgr     : keeps record of sensor reqs enqueued
 * @pipe_delay      : time of delay caused by preisp measure by frames
 *                    and rounded. value is set by hal.
 * @sns_req_mgr     : keeps record of sensor reqs enqueued
 */
struct aiisp_device {
	struct device *dev;
	struct video_device *vdev;
	struct v4l2_device v4l2_dev;
	struct mutex aiisp_lock;
	int open_cnt;
	struct list_head list;
	struct mutex list_lock;
	atomic_t seq_num;
	struct workqueue_struct *work_queue;
	struct v4l2_fh *cam_sync_eventq;
	spinlock_t cam_sync_eventq_lock;
	uint32_t version;
	struct mutex stream_lock;
	struct aiisp_stream_info streams_info[CAM_AIISP_DEVICE_NUM];
	struct aiisp_sync_sensor_info syncs_info[CAM_AIISP_DEVICE_NUM];
	int action_mask;
	/* aiisp connected for switching on/off and debug */
	int aiisp_connected;
	int send_async;
	/* for no aiisp connect provides debug session id and stream id */
	atomic_t no_aiisp_rtos_session;
	atomic_t no_aiisp_rtos_stream;
	int pdelay;
	int fsync_gpio_num;
	cam_fsync_func_t hook;
	uint32_t tuning_addr_for_rtos;
	struct sensor_req_mgr sns_req_mgr;
	uint32_t pipe_delay;

	struct mutex ioctl_lock;
	atomic_t ioctl_running_count;
	int closing;

	/* for reseting rtos */
	atomic_t open_times;
	bool error_hint;
#ifdef QCOM_PLATFORM_CRM
	/* FIXME: @guxiao array */
	struct cam_req_mgr_kmd_ops ops;
	struct cam_req_mgr_crm_cb *crm_cb;
	cam_aiisp_event_cb_func event_cb;
	atomic_t rtos_req_num;
	struct cam_aiisp_hw_config req_list[8];
	int read_p;
	int write_p;
#endif
};

int cam_aiisp_send_rtos_cmd(uint32_t cmd, int ptr_type, void *ptr, uint64_t size);
struct aiisp_device *get_aiisp_dev(void);
void cam_aiisp_send_v4l2_event(uint32_t id, int status, void *payload,
			       int len, uint32_t event_cause);
#endif /* __CAM_AI_ISP_DEV_H__ */
