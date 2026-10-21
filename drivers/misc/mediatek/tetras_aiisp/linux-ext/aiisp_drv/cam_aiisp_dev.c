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

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <media/v4l2-ioctl.h>
#include <linux/debugfs.h>
#include <linux/version.h>

#include "comm_drv_io.h"
#include "package_handle.h"
#include "tis_plat_evt_def.h"
#include "cam_aiisp.h"
#include "cam_aiisp_dbg.h"
#include "cam_aiisp_dev.h"
#include "cam_aiisp_internal.h"
#include "cam_aiisp_pingpong.h"

/* wait for callback 10000ms */
#define IPC_RESPONSE_TIMEOUT 10000

/* wait for rtos command completion */
#define CMD_COMPLITION_TIMEOUT 30

/* wait for rtos command completion check times */
#define CMD_COMPLITION_CHK_TIMES 5

static struct aiisp_device *aiisp_dev;
static struct dentry *debugfs_root;

/* send tid */
struct target_id send_to_tid = {
	.pno = SEND_TO_RTOS_PNO_ID,
	.chip_id = IPC_CHIP_ID,
};

/* recv tid */
static struct target_id recv_tid = {
	.pno = RECV_FROM_RTOS_PNO_ID,
	.chip_id = IPC_CHIP_ID,
};

static const struct of_device_id cam_ai_isp_dt_match[] = {
	{
		.compatible = "tetras,cam-aiisp-dev"
	},
	{}
};

static int
cam_aiisp_pingpong_buf_init(struct aiisp_stream_info *stream,
			    uint32_t base_addr, uint32_t buf_num, uint32_t buf_size)
{
	stream->pingpong = pingpong_buf_init(base_addr, buf_num, buf_size);
	if (NULL == stream->pingpong) {
		AIISP_ERR(CAM_AIISP, "init pingpong fail!");
		return -1;
	}

	return 0;
}

static int
cam_aiisp_pingpong_buf_write(struct aiisp_stream_info *stream, void *msg, uint64_t size)
{
	int ret;

	if (NULL == stream->pingpong) {
		AIISP_ERR(CAM_AIISP, "write pingpong: not init!");
		return -ENODEV;
	}

	ret = pingpong_buf_write(stream->pingpong, msg, size);

	return ret;
}

static int
cam_aiisp_pingpong_buf_start(struct aiisp_stream_info *stream)
{
	int ret;

	if (NULL == stream->pingpong) {
		AIISP_ERR(CAM_AIISP, "write pingpong: not init!");
		return -ENODEV;
	}

	ret = pingpong_buf_start(stream->pingpong);

	return ret;
}

static int cam_aiisp_pingpong_buf_deinit(struct aiisp_stream_info *stream)
{
	if (NULL == stream->pingpong) {
		AIISP_ERR(CAM_AIISP, "deinit pingpong input NULL!");
		return -1;
	}

	pingpong_buf_deinit(stream->pingpong);

	return 0;
}

struct aiisp_device *get_aiisp_dev(void)
{
	return aiisp_dev;
}

static struct aiisp_stream_info *cam_aiisp_get_empty_stream(void)
{
	int i = 0;
	struct aiisp_stream_info *pinfo = NULL;

	mutex_lock(&aiisp_dev->stream_lock);
	for (i = 0; i < CAM_AIISP_DEVICE_NUM; i++) {
		if (aiisp_dev->streams_info[i].used) {
			continue;
		}
		aiisp_dev->streams_info[i].used = 1;
		pinfo = &aiisp_dev->streams_info[i];
		break;
	}
	mutex_unlock(&aiisp_dev->stream_lock);

	return pinfo;
}

static struct aiisp_stream_info *cam_aiisp_find_stream(int session, int stream_handle)
{
	int i = 0;
	struct aiisp_stream_info *pinfo = NULL;

	mutex_lock(&aiisp_dev->stream_lock);
	for (i = 0; i < CAM_AIISP_DEVICE_NUM; i++) {
		if (aiisp_dev->streams_info[i].used &&
		    aiisp_dev->streams_info[i].stream_id == stream_handle &&
		    aiisp_dev->streams_info[i].session_id == session) {
			pinfo = &aiisp_dev->streams_info[i];
			break;
		}
	}
	mutex_unlock(&aiisp_dev->stream_lock);

	return pinfo;
}

static void cam_aiisp_remove_stream(int session, int stream_handle)
{
	int i = 0;

	mutex_lock(&aiisp_dev->stream_lock);
	for (i = 0; i < CAM_AIISP_DEVICE_NUM; i++) {
		if (aiisp_dev->streams_info[i].used &&
		    aiisp_dev->streams_info[i].stream_id == stream_handle &&
		    aiisp_dev->streams_info[i].session_id == session) {
			if (STREAM_IS_SUPPORT_3A(aiisp_dev->streams_info[i].caps))
				cam_aiisp_pingpong_buf_deinit(&aiisp_dev->streams_info[i]);

			memset(&aiisp_dev->streams_info[i], 0,
			       sizeof(aiisp_dev->streams_info[i]));
			break;
		}
	}
	mutex_unlock(&aiisp_dev->stream_lock);
}

void cam_aiisp_check_streams_and_recover(void)
{
	int i = 0;
	int result = 0;
	mutex_lock(&aiisp_dev->stream_lock);
	/* check and send recover to the stream */
	for (i = 0; i < CAM_AIISP_DEVICE_NUM; i++) {
		if (aiisp_dev->streams_info[i].used) {
			struct cam_cmd_header hinfo;
			memset(&hinfo, 0, sizeof(hinfo));
			hinfo.engine_handle = aiisp_dev->streams_info[i].session_id;
			hinfo.stream_handle = aiisp_dev->streams_info[i].stream_id;
			/* hint force recover stream resource */
			AIISP_INFO(CAM_AIISP, "esd recover session %d  %d ",
					hinfo.engine_handle, hinfo.stream_handle);
			mutex_unlock(&aiisp_dev->stream_lock);
			/* send stream recover msg to rtos */
			result = cam_aiisp_send_rtos_cmd(
					TIS_EVT_CAMERA_STREAM_RECOVERY,
					DATA_PTR_KERNEL_TYPE,
					&hinfo,
					sizeof(hinfo));
			mutex_lock(&aiisp_dev->stream_lock);
			AIISP_INFO(CAM_AIISP, "stream recover %d result %d",
					hinfo.stream_handle, result);
		}
	}
    mutex_unlock(&aiisp_dev->stream_lock);
}
EXPORT_SYMBOL(cam_aiisp_check_streams_and_recover);

static void cam_aiisp_check_stream_and_reset(void)
{
	int i = 0;
	int result = 0;
	mutex_lock(&aiisp_dev->stream_lock);
	/* check and release session */
	for (i = 0; i < CAM_AIISP_DEVICE_NUM; i++) {
		if (aiisp_dev->streams_info[i].used) {
			struct cam_cmd_header hinfo;

			/* release ping-pong buffer first */
			if (STREAM_IS_SUPPORT_3A(aiisp_dev->streams_info[i].caps))
				cam_aiisp_pingpong_buf_deinit(&aiisp_dev->streams_info[i]);

			memset(&hinfo, 0, sizeof(hinfo));
			hinfo.engine_handle = aiisp_dev->streams_info[i].session_id;
			hinfo.stream_handle = 0;
			/* hint force release stream resource */
			AIISP_INFO(CAM_AIISP, "hal error session %d not release",
					hinfo.engine_handle);
			/* send release session to rtos */
			result = cam_aiisp_send_rtos_cmd(
					TIS_EVT_CAMERA_DEVICE_CLOSE,
					DATA_PTR_KERNEL_TYPE,
					&hinfo,
					sizeof(hinfo));
			AIISP_INFO(CAM_AIISP, "session release %d result %d",
					hinfo.engine_handle,
					result);
		}
	}
	/* clean streams info */
	memset(aiisp_dev->streams_info, 0, sizeof(aiisp_dev->streams_info));
	mutex_unlock(&aiisp_dev->stream_lock);
}

#ifdef QCOM_PLATFORM_CRM
extern int cam_fsync_qcom_gpio_init(struct aiisp_device *aiisp_dev);
#else
extern int cam_fsync_mtk_gpio_init(struct aiisp_device *aiisp_dev);
#endif

void cam_aiisp_send_v4l2_event(uint32_t id, int status, void *payload,
			       int len, uint32_t event_cause)
{
	struct v4l2_event event;
	__u64 *payload_data = NULL;

	if (aiisp_dev->version == CAM_AIISP_V4L_EVENT) {
		struct cam_aiisp_ev_header *ev_header = NULL;

		event.id = id;
		event.type = CAM_AIISP_V4L_EVENT;

		ev_header = CAM_AIISP_GET_HEADER_PTR(event);
		ev_header->status = status;
		ev_header->version = aiisp_dev->version;
		ev_header->evt_param[CAM_AIISP_EVENT_REASON_CODE_INDEX] = event_cause;
		payload_data = CAM_AIISP_GET_PAYLOAD_PTR(event, __u64);
	}

	memcpy(payload_data, payload, len);
	v4l2_event_queue(aiisp_dev->vdev, &event);
	AIISP_ERR(CAM_AIISP, "send v4l2 event version %d for sync_obj :%u",
		  aiisp_dev->version,
		  event.id);
}

static void cam_aiisp_ipcm_callback_func(uint16_t event, void *data, uint32_t data_len)
{
	void *msg = NULL;
	struct pkg_info info;
	struct msg_sync *sync = NULL, *temp;
	int found = 0;
	struct aiisp_stream_info *stream_info = NULL;

	AIISP_INFO(CAM_AIISP, "msg recved, event id = %d.", event);

	msg = parse_recved_data((char *)data, data_len, &info);
	if (!msg) {
		AIISP_INFO(CAM_AIISP,
			   "msg parse error, event id = %d, seq_num = %d.",
			   event, info.seq_num);
		return;
	}

	//notify recv ack or msg
	mutex_lock(&aiisp_dev->list_lock);
	list_for_each_entry_safe(sync, temp, &aiisp_dev->list, node) {
		if (sync->seq_num == info.seq_num) {
			AIISP_INFO(CAM_AIISP, "seq_num %d is handled.", info.seq_num);
			memcpy(&sync->hinfo, msg, sizeof(sync->hinfo));
			if (TIS_EVT_CAMERA_DEVICE_CREATE_STREAM == sync->cmd) {
				struct cam_cmd_stream_create_result *result = NULL;

				result = (struct cam_cmd_stream_create_result *)
					 (msg + sizeof(sync->hinfo));

				if (sync->hinfo.payload_len == 0
				    || sync->hinfo.stream_handle == 0) {
					AIISP_ERR(CAM_AIISP, "fatal communication with rtos");
					mutex_unlock(&aiisp_dev->list_lock);
					sync->result = -ENODATA;
					complete(&sync->comp);
					return;
				}

				stream_info = cam_aiisp_get_empty_stream();
				if (stream_info == NULL) {
					AIISP_ERR(CAM_AIISP, "No empty stream info");
					mutex_unlock(&aiisp_dev->list_lock);
					sync->result = -EBUSY;
					complete(&sync->comp);
					return;
				}
				stream_info->stream_id = sync->hinfo.stream_handle;
				stream_info->session_id = sync->hinfo.engine_handle;
				stream_info->curr_frame_id = 0;
				stream_info->send_tid.pno = result->stream_pno;
				stream_info->send_tid.chip_id = IPC_CHIP_ID;

				stream_info->recv_tid.pno = result->stream_pno;
				stream_info->recv_tid.chip_id = IPC_CHIP_ID;

				stream_info->caps = result->caps;

				AIISP_INFO(CAM_AIISP, "create stream_hd 0x%x pno %u",
					   sync->hinfo.stream_handle, result->stream_pno);

				if (STREAM_IS_SUPPORT_3A(stream_info->caps))
					cam_aiisp_pingpong_buf_init
						(stream_info,
						 result->pingpong_addr,
						 result->pingpong_single_buf_num,
						 result->pingpong_single_buf_size);
				else
					stream_info->pingpong = NULL;
			} else if (TIS_EVT_CAMERA_DEVICE_DESTROY_STREAM == sync->cmd) {
				cam_aiisp_remove_stream(sync->hinfo.engine_handle,
							sync->hinfo.stream_handle);
			}

			sync->result = 0;
			complete(&sync->comp);
			found = 1;
			break;
		}
	}
	mutex_unlock(&aiisp_dev->list_lock);

	if (!found) {
		//fix me for rtos notify msg
		AIISP_WARN(CAM_AIISP, "event %d seq_num %d msg not found in the message queue",
			   event, info.seq_num);
	} else {
		//ack msg
		AIISP_DBG(CAM_AIISP, "msg recved, event id = %d, seq_num = %d.",
			  event, info.seq_num);
	}
}

static int send_to_stream(int session_handle, int stream_handle,
			  void *msg, uint64_t size, uint32_t cmd)
{
	int ret = 0;

	struct aiisp_stream_info *stream_info =
		cam_aiisp_find_stream(session_handle, stream_handle);

	if (stream_info == NULL) {
		AIISP_ERR(CAM_AIISP, "no stream info for session 0x%x stream handle 0x%x",
			  session_handle, stream_handle);
		ret = -1;
	} else {
		AIISP_INFO(CAM_AIISP, "send to session 0x%x stream 0x%x pno 0x%x",
			   session_handle, stream_handle, stream_info->send_tid.pno);
		ret = send_msg_for_kernel_thread(cmd, (char *)msg, size, &recv_tid,
						 &stream_info->send_tid);
		if (ret) {
			AIISP_ERR(CAM_AIISP, "send_msg_for_kernel_thread failed ret %d .", ret);
			ret = -1;
		}
	}
	return ret;
}

static int send_to_rtos(void *msg, uint64_t size, uint32_t cmd)
{
	int ret = 0;
	ret = send_msg_for_kernel_thread(cmd, (char *)msg, size, &recv_tid, &send_to_tid);
	if (ret) {
		AIISP_ERR(CAM_AIISP, "send_msg_for_kernel_thread failed ret %d .", ret);
		//fix me ret code
		ret = -1;
		return ret;
	}
	return ret;
}

uint32_t get_buffer_no_by_stream_id(uint32_t stream_id)
{
	uint32_t stream_idx = 0;

	/* FIXME: find idx from stream_id */

	return stream_idx;
}

#ifdef USE_MTK_EXTISP
extern void mtk_cam_extisp_cb_register(void (*mismatched_cnt_cb)(void *handle), void *handle);
#endif
int cam_aiisp_send_rtos_cmd(uint32_t cmd, int ptr_type, void *ptr, uint64_t size)
{
	int result = 0;
	int ret = 0;
	struct pkg_info info;
	struct msg_sync *sync = NULL;
	struct cam_cmd_header *media_info = NULL;
	struct cam_aiisp_stream_config *stream_config = NULL;
	struct aiisp_stream_info *stream_info = NULL;
	void *pkg = NULL;
	int sensor_id = -1;
	int i = 0;

	/* Set package header info */
	info.sub_type = 0;
	info.size = size;
	info.version = VERSION;
	info.type = cmd;

	if (TIS_EVT_CAMERA_INVALID <= cmd) {
		AIISP_ERR(CAM_AIISP, "unknown cmd id is %d", cmd);
		return -EINVAL;
	}

	/* Set package header seq number */
	/* Note: Seq num 0xFFFFFFFF is reserved for error */
	info.seq_num = atomic_inc_return(&aiisp_dev->seq_num) & 0xFFFFFFFF;
	if (atomic_read(&aiisp_dev->seq_num) == 0xFFFFFFFF) {
		atomic_set(&aiisp_dev->seq_num, 1);
		info.seq_num = 1;
	}

	/* Orgnize the package memory */
	pkg = orgnize_send_data(&info);
	if (!pkg) {
		ret = -EINVAL;
		goto end;
	}
	/* Copy user data from user space, note that the data is returned
	 * by the orgnize_send_data()*/
	if (ptr_type == DATA_PTR_USER_TYPE) {
		if (copy_from_user(info.data, ptr, size)) {
			AIISP_ERR(CAM_AIISP, "copy_from_user failed ptr %p  size %llu.", ptr, size);
			ret = -EFAULT;
			goto end;
		}
	} else if (ptr_type == DATA_PTR_KERNEL_TYPE) {
		memcpy(info.data, ptr, size);
	}

	//fix for debug check stream and rtos session
	media_info = (struct cam_cmd_header *)info.data;
	AIISP_INFO(CAM_AIISP, "session 0x%x stream_handle 0x%x cmd %d ", media_info->engine_handle,
		   media_info->stream_handle, cmd);

	/* malloc the sync memory, set seq_num and insert into list */
	sync = (struct msg_sync *)kzalloc(sizeof(struct msg_sync), GFP_KERNEL);
	if (!sync) {
		ret = -ENOMEM;
		goto end;
	}

	/* copy session info */
	memcpy(&sync->hinfo, info.data, sizeof(sync->hinfo));

	if (TIS_EVT_CAMERA_DEVICE_CREATE_STREAM == cmd) {
		stream_config = (struct cam_aiisp_stream_config *)(info.data);
		sensor_id = stream_config->config.sensor_id;
		AIISP_INFO(CAM_AIISP, "config stream sensor id %d ", sensor_id);
	}

	sync->seq_num = info.seq_num;
	sync->cmd = cmd;
	init_completion(&sync->comp);
	mutex_lock(&aiisp_dev->list_lock);
	list_add(&sync->node, &aiisp_dev->list);
	mutex_unlock(&aiisp_dev->list_lock);

	AIISP_INFO(CAM_AIISP, "send to rtos seq num %d cmd 0x%x, size = %llu",
		   sync->seq_num, cmd, size);
	if (aiisp_dev->aiisp_connected) {
		if (TIS_EVT_CAMERA_STREAM_CONFIG == cmd) {
			stream_info = cam_aiisp_find_stream(sync->hinfo.engine_handle,
							    sync->hinfo.stream_handle);

			if (NULL == stream_info) {
				ret = -EFAULT;
				AIISP_ERR(CAM_AIISP, "send to rtos 3a info failed! No stream");

				goto insert_end;
			}

			ret = cam_aiisp_pingpong_buf_write(stream_info, info.data, size);
			goto insert_end;
		}

		/* Send the package to RTOS */
		if (cmd == TIS_EVT_CAMERA_STREAM_START || cmd == TIS_EVT_CAMERA_STREAM_STOP
				|| cmd == TIS_EVT_CAMERA_STREAM_RECOVERY) {
			stream_info = cam_aiisp_find_stream(sync->hinfo.engine_handle,
							    sync->hinfo.stream_handle);

			if (NULL == stream_info) {
				ret = -EFAULT;
				AIISP_ERR(CAM_AIISP, "No stream, start/stop error");

				goto insert_end;
			}

			if (cmd == TIS_EVT_CAMERA_STREAM_START) {
				cam_aiisp_pingpong_buf_start(stream_info);
#ifdef USE_MTK_EXTISP
				aiisp_dev->sns_req_mgr.fcnt = 0;
				for (i = 0; i < aiisp_dev->fsync_gpio_num; i++)
					if (aiisp_dev->syncs_info[i].sensor_id
					    == stream_info->sensor_id) {
						atomic_set(
							&aiisp_dev->syncs_info[i].frame_sync_state,
							FRAME_SYNC_OK);
					goto found;
				}
				AIISP_INFO(CAM_AIISP, "sensor id %d not found",
					   stream_info->sensor_id);
				goto notfound;
found:
				mtk_cam_extisp_cb_register(cam_notify_fcnt_mismatch,
							   &aiisp_dev->syncs_info[i]);
#endif
			}
#ifdef USE_MTK_EXTISP
notfound:
#endif
			ret = send_to_stream(sync->hinfo.engine_handle,
					     sync->hinfo.stream_handle, pkg, info.size, cmd);
		} else {
			ret = send_to_rtos(pkg, info.size, cmd);
		}

		if (ret) {
			ret = -EFAULT;
			goto insert_end;
		}

		/* Wait for the reply from RTOS */
		result = wait_for_completion_interruptible_timeout(&sync->comp, msecs_to_jiffies(IPC_RESPONSE_TIMEOUT));

		if (result <= 0) {
			ret = -EFAULT;
		} else {
			if (TIS_EVT_CAMERA_DEVICE_CREATE_STREAM == cmd) {
				if (sync->result < 0) {
					ret = sync->result;
					goto insert_end;
				}
				for (i = 0; i < aiisp_dev->fsync_gpio_num; i++) {
					if (aiisp_dev->syncs_info[i].sensor_id == sensor_id) {
						stream_info = cam_aiisp_find_stream(sync->hinfo.engine_handle, sync->hinfo.stream_handle);
						if (NULL == stream_info) {
							ret = -EFAULT;
							goto insert_end;
						}

						stream_info->sensor_id = sensor_id;
						aiisp_dev->syncs_info[i].slot_idx = (stream_info - aiisp_dev->streams_info) / sizeof(*stream_info);
						break;
					}
				}
				sync->hinfo.payload_len = i;
				AIISP_INFO(CAM_AIISP, "slot %d sensor %d stream 0x%x session 0x%x ",
					   i, sensor_id,
					   sync->hinfo.stream_handle, sync->hinfo.engine_handle);
			}
			/* copy session info to user*/
			if (ptr_type == DATA_PTR_USER_TYPE) {
				if (copy_to_user(ptr, &sync->hinfo, sizeof(sync->hinfo))) {
					AIISP_ERR(CAM_AIISP, "copy_to_user failed ptr %p size %zu.",
						ptr, sizeof(sync->hinfo));
					ret = -EFAULT;
				}
			} else if (ptr_type == DATA_PTR_KERNEL_TYPE) {
				memcpy(ptr, &sync->hinfo, sizeof(sync->hinfo));
			}
			ret = 0;
		}
	} else {
		if (TIS_EVT_CAMERA_DEVICE_OPEN == cmd) {
			sync->hinfo.engine_handle =
				atomic_inc_return(&aiisp_dev->no_aiisp_rtos_session);
			sync->hinfo.result = 0;
		}

		if (TIS_EVT_CAMERA_DEVICE_CREATE_STREAM == cmd) {
			sync->hinfo.stream_handle = atomic_inc_return(&aiisp_dev->no_aiisp_rtos_stream);
			for (i = 0; i < aiisp_dev->fsync_gpio_num; i++) {
				if (aiisp_dev->syncs_info[i].sensor_id  == sensor_id) {
					aiisp_dev->streams_info[i].sensor_id = sensor_id;
					aiisp_dev->streams_info[i].stream_id = sync->hinfo.stream_handle;
					aiisp_dev->streams_info[i].session_id = sync->hinfo.engine_handle;
					aiisp_dev->streams_info[i].curr_frame_id = 0;
					break;
				}
			}

			sync->hinfo.payload_len = i;
			AIISP_INFO(CAM_AIISP, "slot %d sensor %d stream 0x%x session 0x%x ",
				   i, sensor_id,
				   sync->hinfo.stream_handle, sync->hinfo.engine_handle);
		}

		/* copy session info to user*/
		if (ptr_type == DATA_PTR_USER_TYPE) {
			if (copy_to_user(ptr, &sync->hinfo, sizeof(sync->hinfo))) {
				AIISP_ERR(CAM_AIISP, "copy_to_user failed ptr %p  size %zu.",
					  ptr, sizeof(sync->hinfo));
				ret = -EFAULT;
			}
		} else if (ptr_type == DATA_PTR_KERNEL_TYPE) {
			memcpy(ptr, &sync->hinfo, sizeof(sync->hinfo));
		}
		ret = 0;
	}

insert_end:
	if (sync != NULL) {
		mutex_lock(&aiisp_dev->list_lock);
		list_del(&sync->node);
		mutex_unlock(&aiisp_dev->list_lock);
	}
end:
	if (pkg != NULL) {
		kfree(pkg);
	}

	if (sync != NULL) {
		kfree(sync);
	}
	if (ret < 0) {
		aiisp_dev->error_hint = true;
	}
	return ret;
}

static int cam_aiisp_get_open_times(struct aiisp_device *aiisp_dev,
				     struct cam_private_ioctl_arg *k_ioctl)
{
	int result = 0;
	struct cam_aiisp_open_times times;

	if (k_ioctl->size != sizeof(struct cam_aiisp_open_times))
		return -EINVAL;

	if (!k_ioctl->ioctl_ptr)
		return -EINVAL;

	if (copy_from_user(&times, u64_to_user_ptr(k_ioctl->ioctl_ptr),
				k_ioctl->size))
		return -EFAULT;

	times.open_times = atomic_read(&aiisp_dev->open_times);
	if (times.open_times >= times.threshold) {
		AIISP_INFO(CAM_AIISP, "open_times %d threshold %d",
				times.open_times,
				times.threshold);
		atomic_set(&aiisp_dev->open_times, 0);
	}

	if (copy_to_user(u64_to_user_ptr(k_ioctl->ioctl_ptr),
				 &times,
				 k_ioctl->size)) {
		return -EFAULT;
	}
	return result;
}

static int cam_aiisp_get_stream_capability(struct aiisp_device *aiisp_dev,
				     struct cam_private_ioctl_arg *k_ioctl)
{
	int result = 0;
	struct cam_aiisp_stream_cap cap;
	struct aiisp_stream_info *stream_info = NULL;

	if (k_ioctl->size != sizeof(struct cam_aiisp_stream_cap))
		return -EINVAL;

	if (!k_ioctl->ioctl_ptr)
		return -EINVAL;

	if (copy_from_user(&cap, u64_to_user_ptr(k_ioctl->ioctl_ptr),
				k_ioctl->size))
		return -EFAULT;

	stream_info = cam_aiisp_find_stream(cap.hinfo.engine_handle,
			cap.hinfo.stream_handle);
	if (stream_info == NULL) {
		AIISP_INFO(CAM_AIISP, "find stream %d failed ", cap.hinfo.stream_handle);
		return -EINVAL;
	}
	cap.caps.w = stream_info->caps.w;
	if (copy_to_user(u64_to_user_ptr(k_ioctl->ioctl_ptr),
				 &cap,
				 k_ioctl->size)) {
		return -EFAULT;
	}
	return result;
}


static int cam_aiisp_set_action_mask(struct aiisp_device *aiisp_dev,
				     struct cam_private_ioctl_arg *k_ioctl)
{
	struct cam_aiisp_action_mask mask;
	int result = 0;

	if (k_ioctl->size != sizeof(struct cam_aiisp_action_mask))
		return -EINVAL;

	if (!k_ioctl->ioctl_ptr)
		return -EINVAL;

	if (copy_from_user(&mask, u64_to_user_ptr(k_ioctl->ioctl_ptr), k_ioctl->size))
		return -EFAULT;

	aiisp_dev->action_mask = mask.mask;
	return result;
}

static int cam_aiisp_config(struct aiisp_device *aiisp_dev, struct cam_private_ioctl_arg *k_ioctl)
{
	struct cam_aiisp_config_item config;

	if (k_ioctl->size != sizeof(struct cam_aiisp_config_item))
		return -EINVAL;

	if (!k_ioctl->ioctl_ptr)
		return -EINVAL;

	if (copy_from_user(&config, u64_to_user_ptr(k_ioctl->ioctl_ptr), k_ioctl->size))
		return -EFAULT;

	aiisp_dev->pipe_delay = config.value;

	AIISP_INFO(CAM_AIISP, "set snv to %u", aiisp_dev->pipe_delay);

	return 0;
}

static int cam_aiisp_add_req(struct aiisp_device *aiisp_dev, struct cam_private_ioctl_arg *k_ioctl)
{
	struct cam_aiisp_add_req req;
	struct cam_aiisp_hw_config config;
	int result;

	if (k_ioctl->size != sizeof(struct cam_aiisp_add_req))
		return -EINVAL;

	if (!k_ioctl->ioctl_ptr)
		return -EINVAL;

	if (copy_from_user(&req, u64_to_user_ptr(k_ioctl->ioctl_ptr), k_ioctl->size))
		return -EFAULT;

	if (req.size != sizeof(struct cam_aiisp_hw_config))
		return -EINVAL;

	if (!req.ptr)
		return -EINVAL;

	if (copy_from_user(&config, u64_to_user_ptr(req.ptr), req.size))
		return -EFAULT;

#ifdef QCOM_PLATFORM_CRM
	if (aiisp_dev->pipe_delay) {
		if (config.params.frame_id < 2)
			return 0;
		else if (config.params.frame_id == 2) {
			config.params.frame_id = 3;
			AIISP_INFO(CAM_AIISP, "config request id %llu => frame id %llu",
				   config.params.frame_id, config.params.target_framid);

			result = cam_aiisp_send_rtos_cmd(TIS_EVT_CAMERA_STREAM_CONFIG,
							 DATA_PTR_KERNEL_TYPE, &config,
							 req.size);
			return result;
		} else {
			memcpy(aiisp_dev->req_list + aiisp_dev->write_p++, &config,
			       sizeof(struct cam_aiisp_hw_config));
			if (aiisp_dev->write_p == 8)
				aiisp_dev->write_p = 0;
			if (aiisp_dev->write_p == aiisp_dev->read_p) {
				AIISP_ERR(CAM_AIISP, "aiisp req_list overflow");
				return -EBUSY;
			}
			AIISP_INFO(CAM_AIISP, "Q %llu", config.params.frame_id);
			return 0;
		}
	} else {
		AIISP_INFO(CAM_AIISP, "config request id %llu => frame id %llu",
			   config.params.frame_id, config.params.target_framid);
		return cam_aiisp_send_rtos_cmd(TIS_EVT_CAMERA_STREAM_CONFIG, DATA_PTR_KERNEL_TYPE,
					       &config, req.size);
	}
#else
	AIISP_INFO(CAM_AIISP, "config request id %llu => frame id %llu",
		   config.params.frame_id, config.params.target_framid);
	result = cam_aiisp_send_rtos_cmd(TIS_EVT_CAMERA_STREAM_CONFIG, DATA_PTR_KERNEL_TYPE,
					 &config, req.size);
	return result;
#endif
}

//only handle command from user
static int cam_aiisp_handle_cmd(struct aiisp_device *aiisp_dev,
				struct cam_private_ioctl_arg *k_ioctl)
{
	struct cam_aiisp_send_cmd cmd;
	int result;

	if (k_ioctl->size != sizeof(struct cam_aiisp_send_cmd))
		return -EINVAL;

	if (!k_ioctl->ioctl_ptr)
		return -EINVAL;

	if (copy_from_user(&cmd, u64_to_user_ptr(k_ioctl->ioctl_ptr), k_ioctl->size))
		return -EFAULT;

	result = cam_aiisp_send_rtos_cmd(cmd.cmd, DATA_PTR_USER_TYPE, u64_to_user_ptr(cmd.ptr), cmd.size);

	if (!result) {
		if (copy_to_user(u64_to_user_ptr(k_ioctl->ioctl_ptr),
				 &cmd,
				 k_ioctl->size))
			return -EFAULT;
	} else {
		AIISP_ERR(CAM_AIISP, "RTOS cmd failure: %d", result);
	}

	return result;
}

static int cam_aiisp_sram_ops(uint32_t sram_addr, uint64_t length, uint64_t ptr,
			      uint32_t ops)
{
	int ret = 0;
	char *data = NULL;
	data = (char *)kzalloc(length, GFP_KERNEL);
	if (!data) {
		return -ENOMEM;
	}

	if (aiisp_dev->aiisp_connected) {
		if (ops == SRAM_WR) {
			/* Copy user data from user space */
			if (copy_from_user(data, u64_to_user_ptr(ptr), length)) {
				AIISP_ERR(CAM_AIISP, "copy_from_user failed.");
				ret = -EFAULT;
				goto end;
			}

			AIISP_DBG(CAM_AIISP, "SRAM WRITE addr = 0x%x length = %llu",
				  sram_addr, length);
			ret = write_data_for_kernel_thread(sram_addr, data, length, &send_to_tid);
			if (ret) {
				AIISP_ERR(CAM_AIISP, "write sram failed ret = %d", ret);
				goto end;
			}
		}

		if (ops == SRAM_RD) {
			AIISP_INFO(CAM_AIISP, "read sram");
		}
	}

end:
	kfree(data);
	return ret;
}

static int cam_aiisp_write_sram(struct aiisp_device *aiisp_dev,
				struct cam_private_ioctl_arg *k_ioctl)
{
	int result = 0;
	struct cam_aiisp_write_sram write_sram;

	if (k_ioctl->size != sizeof(struct cam_aiisp_write_sram))
		return -EINVAL;

	if (!k_ioctl->ioctl_ptr)
		return -EINVAL;

	if (copy_from_user(&write_sram, u64_to_user_ptr(k_ioctl->ioctl_ptr),
			   k_ioctl->size))
		return -EFAULT;

	result = cam_aiisp_sram_ops(write_sram.write_addr, write_sram.length,
				    write_sram.ptr, SRAM_WR);

	if (!result)
		if (copy_to_user(u64_to_user_ptr(k_ioctl->ioctl_ptr),
				 &write_sram,
				 k_ioctl->size))
			return -EFAULT;

	return result;
}

static long cam_aiisp_dev_ioctl(struct file *filep, void *fh,
				bool valid_prio, unsigned int cmd, void *arg)
{
	int32_t rc = 0;
	struct aiisp_device *aiisp_dev = video_drvdata(filep);
	struct cam_private_ioctl_arg k_ioctl;

	if (!aiisp_dev) {
		AIISP_ERR(CAM_AIISP, "aiisp_dev NULL");
		return -EINVAL;
	}

	if (!arg)
		return -EINVAL;

	if (cmd != CAM_PRIVATE_IOCTL_CMD)
		return -ENOIOCTLCMD;

	k_ioctl = *(struct cam_private_ioctl_arg *)arg;

	mutex_lock(&aiisp_dev->ioctl_lock);
	if (aiisp_dev->closing) {
		mutex_unlock(&aiisp_dev->ioctl_lock);
		AIISP_ERR(CAM_AIISP, "aiisp_dev is closing");
		return -EPERM;
	}
	atomic_inc(&aiisp_dev->ioctl_running_count);
	mutex_unlock(&aiisp_dev->ioctl_lock);

	switch (k_ioctl.id) {
	case CAM_AIISP_ADD_REQ:
		rc = cam_aiisp_add_req(aiisp_dev, &k_ioctl);
		break;
	case CAM_AIISP_SEND_CMD:
		rc = cam_aiisp_handle_cmd(aiisp_dev, &k_ioctl);
		break;
	case CAM_AIISP_WRITE_SRAM:
		rc = cam_aiisp_write_sram(aiisp_dev, &k_ioctl);
		break;
	case CAM_AIISP_CONFIG:
		rc = cam_aiisp_config(aiisp_dev, &k_ioctl);
		break;
	case CAM_AIISP_ACTION_MASK:
		rc = cam_aiisp_set_action_mask(aiisp_dev, &k_ioctl);
		break;
	case CAM_AIISP_GET_STREAM_CAP:
		rc = cam_aiisp_get_stream_capability(aiisp_dev, &k_ioctl);
		break;
	case CAM_AIISP_POWER_ON:
	case CAM_AIISP_POWER_OFF:
		break;
	case CAM_AIISP_GET_OPEN_TIMES:
		rc = cam_aiisp_get_open_times(aiisp_dev, &k_ioctl);
		break;
	default:
		AIISP_ERR(CAM_AIISP, "unknown ioctl %u", k_ioctl.id);
		rc = -ENOIOCTLCMD;
	}

	atomic_dec(&aiisp_dev->ioctl_running_count);
	return rc;
}

static unsigned int cam_aiisp_poll(struct file *f,
				   struct poll_table_struct *pll_table)
{
	int32_t rc = 0;
	struct v4l2_fh *eventq = f->private_data;

	if (!eventq)
		return -EINVAL;

	poll_wait(f, &eventq->wait, pll_table);

	if (v4l2_event_pending(eventq))
		rc = POLLPRI;

	return rc;
}

static int cam_aiisp_open(struct file *filep)
{
	int32_t rc = 0;
	struct aiisp_device *aiisp_dev = video_drvdata(filep);

	if (!aiisp_dev) {
		return -ENODEV;
	}
	if (aiisp_dev->error_hint) {
		atomic_inc(&aiisp_dev->open_times);
		aiisp_dev->error_hint = false;
	}
	mutex_lock(&aiisp_dev->aiisp_lock);
	rc = v4l2_fh_open(filep);
	if (!rc) {
		aiisp_dev->open_cnt++;
		spin_lock_bh(&aiisp_dev->cam_sync_eventq_lock);
		aiisp_dev->cam_sync_eventq = filep->private_data;
		spin_unlock_bh(&aiisp_dev->cam_sync_eventq_lock);
	} else {
		AIISP_ERR(CAM_AIISP, "v4l2_fh_open failed : %d", rc);
	}
	mutex_unlock(&aiisp_dev->aiisp_lock);
	return rc;
}

static int cam_aiisp_close(struct file *filep)
{
	int32_t rc = 0;
	struct aiisp_device *aiisp_dev = video_drvdata(filep);
	int check_time = CMD_COMPLITION_CHK_TIMES;

	AIISP_INFO(CAM_AIISP, "cam_aiisp_close");

	if (!aiisp_dev) {
		AIISP_ERR(CAM_AIISP, "Sync device NULL");
		rc = -ENODEV;
		return rc;
	}
	mutex_lock(&aiisp_dev->aiisp_lock);
	aiisp_dev->open_cnt--;
	if (!aiisp_dev->open_cnt) {
		/*
		 * Flush the work queue to wait for pending signal callbacks to
		 * finish
		 */
		flush_workqueue(aiisp_dev->work_queue);
		/*
		 * clean stream resource when close
		 */
		mutex_lock(&aiisp_dev->ioctl_lock);
		aiisp_dev->closing = 1;
		AIISP_INFO(CAM_AIISP, "aiisp_dev is closing and running count %d",
				atomic_read(&aiisp_dev->ioctl_running_count));
		mutex_unlock(&aiisp_dev->ioctl_lock);

		while (atomic_read(&aiisp_dev->ioctl_running_count) && (check_time > 0)) {
			check_time--;
			msleep(CMD_COMPLITION_TIMEOUT);
		}

		AIISP_INFO(CAM_AIISP, "aiisp_dev close running count %d",
				atomic_read(&aiisp_dev->ioctl_running_count));
		cam_aiisp_check_stream_and_reset();
		atomic_set(&aiisp_dev->ioctl_running_count, 0);
		aiisp_dev->closing = 0;
	}

	spin_lock_bh(&aiisp_dev->cam_sync_eventq_lock);
	aiisp_dev->cam_sync_eventq = NULL;
	spin_unlock_bh(&aiisp_dev->cam_sync_eventq_lock);
	v4l2_fh_release(filep);
	mutex_unlock(&aiisp_dev->aiisp_lock);
	return rc;
}

static struct v4l2_file_operations cam_aiisp_v4l2_fops = {
	.owner = THIS_MODULE,
	.open  = cam_aiisp_open,
	.release = cam_aiisp_close,
	.poll = cam_aiisp_poll,
	.unlocked_ioctl   = video_ioctl2,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = video_ioctl2,
#endif
};

static void cam_aiisp_event_queue_notify_error(const struct v4l2_event *old,
					       struct v4l2_event *xnew)
{
	if (aiisp_dev->version == CAM_AIISP_V4L_EVENT) {
		struct cam_aiisp_ev_header *ev_header;

		ev_header = CAM_AIISP_GET_HEADER_PTR((*old));
		AIISP_ERR(CAM_AIISP,
			  "Failed to notify event id %d statue %d reason %u %u %u %u",
			  old->id, ev_header->status,
			  ev_header->evt_param[0], ev_header->evt_param[1],
			  ev_header->evt_param[2], ev_header->evt_param[3]);

	}
}

static struct v4l2_subscribed_event_ops cam_aiisp_v4l2_ops = {
	.merge = cam_aiisp_event_queue_notify_error,
};

#define MAX_QUEUED_FSYNC 2
int cam_aiisp_subscribe_event(struct v4l2_fh *fh,
			      const struct v4l2_event_subscription *sub)
{
	if (sub->type == V4L2_EVENT_FRAME_SYNC) {
		/* todo: use id in multi sensor case */
		AIISP_INFO(CAM_AIISP, "userspace subscribes FSYNC0");
		return v4l2_event_subscribe(fh, sub, MAX_QUEUED_FSYNC, NULL);
	} else if (!(sub->type == CAM_AIISP_V4L_EVENT)) {
		AIISP_ERR(CAM_AIISP, "Non supported event type 0x%x", sub->type);
		return -EINVAL;
	}

	aiisp_dev->version = sub->type;
	AIISP_INFO(CAM_AIISP, "Sync event verion type 0x%x", aiisp_dev->version);
	return v4l2_event_subscribe(fh, sub, CAM_AIISP_MAX_V4L2_EVENTS, &cam_aiisp_v4l2_ops);
}

int cam_aiisp_unsubscribe_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{
	if (!(sub->type == CAM_AIISP_V4L_EVENT) && !(sub->type == V4L2_EVENT_FRAME_SYNC)) {
		AIISP_ERR(CAM_AIISP, "Non supported event type 0x%x", sub->type);
		return -EINVAL;
	}

	return v4l2_event_unsubscribe(fh, sub);
}

static const struct v4l2_ioctl_ops g_cam_aiisp_ioctl_ops = {
	.vidioc_subscribe_event = cam_aiisp_subscribe_event,
	.vidioc_unsubscribe_event = cam_aiisp_unsubscribe_event,
	.vidioc_default = cam_aiisp_dev_ioctl,
};

static int cam_aiisp_media_controller_init(struct aiisp_device *aiisp_dev,
					   struct platform_device *pdev)
{
	int rc;

	aiisp_dev->v4l2_dev.mdev = kzalloc(sizeof(struct media_device),
					   GFP_KERNEL);
	if (!aiisp_dev->v4l2_dev.mdev)
		return -ENOMEM;

	media_device_init(aiisp_dev->v4l2_dev.mdev);
	strlcpy(aiisp_dev->v4l2_dev.mdev->model, CAM_AIISP_DEVICE_NAME,
		sizeof(aiisp_dev->v4l2_dev.mdev->model));
	aiisp_dev->v4l2_dev.mdev->dev = &(pdev->dev);

	rc = media_device_register(aiisp_dev->v4l2_dev.mdev);
	if (rc < 0) {
		AIISP_ERR(CAM_AIISP, "media_device_register failed %d", rc);
		goto register_fail;
	}

	rc = media_entity_pads_init(&aiisp_dev->vdev->entity, 0, NULL);
	if (rc < 0) {
		AIISP_ERR(CAM_AIISP, "media_entity_pads_init failed %d", rc);
		goto entity_fail;
	}

	return 0;

entity_fail:
	media_device_unregister(aiisp_dev->v4l2_dev.mdev);
register_fail:
	media_device_cleanup(aiisp_dev->v4l2_dev.mdev);
	return rc;
}

static void cam_aiisp_media_controller_cleanup(struct aiisp_device *aiisp_dev)
{
	media_entity_cleanup(&aiisp_dev->vdev->entity);
	media_device_unregister(aiisp_dev->v4l2_dev.mdev);
	media_device_cleanup(aiisp_dev->v4l2_dev.mdev);
	kfree(aiisp_dev->v4l2_dev.mdev);
}

static void cam_aiisp_init_entity(struct aiisp_device *aiisp_dev)
{
	aiisp_dev->vdev->entity.function = CAM_AIISP_DEVICE_TYPE;
	aiisp_dev->vdev->entity.name = video_device_node_name(aiisp_dev->vdev);
}

static int aiisp_debug_set_send_async(void *data, u64 val)
{
	int rc = 0;
	struct aiisp_device *dev = data;
	dev->send_async = (int32_t)val;
	return rc;
}

static int aiisp_debug_get_send_async(void *data, u64 *val)
{
	int rc = 0;
	struct aiisp_device *dev = data;
	*val = dev->send_async;
	return rc;
}

DEFINE_SIMPLE_ATTRIBUTE(send_async, aiisp_debug_get_send_async, aiisp_debug_set_send_async, "%lld\n");

static int aiisp_debug_set_snv(void *data, u64 val)
{
	int rc = 0;
	struct aiisp_device *dev = data;
	dev->pipe_delay = (int32_t)val;
	return rc;
}

static int aiisp_debug_get_snv(void *data, u64 *val)
{
	int rc = 0;
	struct aiisp_device *dev = data;
	*val = dev->pipe_delay;
	return rc;
}

DEFINE_SIMPLE_ATTRIBUTE(pipe_delay, aiisp_debug_get_snv, aiisp_debug_set_snv, "%lld\n");

static int aiisp_debug_set_aiisp_connected(void *data, u64 val)
{
	int rc = 0;
	struct aiisp_device *dev = data;
	dev->aiisp_connected = (int32_t)val;
	return rc;
}

static int aiisp_debug_get_aiisp_connected(void *data, u64 *val)
{
	int rc = 0;
	struct aiisp_device *dev = data;
	*val = dev->aiisp_connected;
	return rc;
}

DEFINE_SIMPLE_ATTRIBUTE(aiisp_connected, aiisp_debug_get_aiisp_connected,
			aiisp_debug_set_aiisp_connected, "%lld\n");

static int cam_aiisp_debug_register(void)
{
	int rc = 0;
	struct dentry *dbgfileptr = NULL;

	dbgfileptr = debugfs_create_dir("cam_aiisp", NULL);
	if (!dbgfileptr) {
		AIISP_ERR(CAM_AIISP, "DebugFS could not create directory!");
		rc = -ENOENT;
		goto end;
	}
	/* Store parent inode for cleanup in caller */
	debugfs_root = dbgfileptr;

	dbgfileptr = debugfs_create_file("pipe_delay", 0644,
					 debugfs_root, aiisp_dev, &pipe_delay);

	if (IS_ERR(dbgfileptr)) {
		if (PTR_ERR(dbgfileptr) == -ENODEV)
			AIISP_ERR(CAM_AIISP, "DebugFS not enabled in kernel!");
		else
			rc = PTR_ERR(dbgfileptr);
	}

	dbgfileptr = debugfs_create_file("aiisp_connected", 0644,
					 debugfs_root, aiisp_dev, &aiisp_connected);

	if (IS_ERR(dbgfileptr)) {
		if (PTR_ERR(dbgfileptr) == -ENODEV)
			AIISP_ERR(CAM_AIISP, "DebugFS not enabled in kernel!");
		else
			rc = PTR_ERR(dbgfileptr);
	}

	dbgfileptr = debugfs_create_file("send_async", 0644,
					 debugfs_root, aiisp_dev, &send_async);

	if (IS_ERR(dbgfileptr)) {
		if (PTR_ERR(dbgfileptr) == -ENODEV)
			AIISP_ERR(CAM_AIISP, "DebugFS not enabled in kernel!");
		else
			rc = PTR_ERR(dbgfileptr);
	}
end:
	return rc;
}

static int cam_aiisp_debug_unregister(void)
{
	debugfs_remove_recursive(debugfs_root);
	return 0;
}

int cam_fysnc_hook_register(void *hook)
{
	if (!hook || !aiisp_dev)
		return -EINVAL;

	aiisp_dev->hook = hook;

	return 0;
}

void cam_fysnc_hook_unregister(void *hook)
{
	aiisp_dev->hook = NULL;
}

static int cam_ai_isp_dev_probe(struct platform_device *pdev)
{
	int rc;

	aiisp_dev = kzalloc(sizeof(struct aiisp_device), GFP_KERNEL);
	if (!aiisp_dev)
		return -ENOMEM;

	spin_lock_init(&aiisp_dev->cam_sync_eventq_lock);
	mutex_init(&aiisp_dev->aiisp_lock);
	mutex_init(&aiisp_dev->stream_lock);
	mutex_init(&aiisp_dev->list_lock);

	aiisp_dev->vdev = video_device_alloc();
	if (!aiisp_dev->vdev) {
		rc = -ENOMEM;
		goto vdev_fail;
	}

	rc = cam_aiisp_media_controller_init(aiisp_dev, pdev);
	if (rc < 0)
		goto mcinit_fail;

	aiisp_dev->vdev->v4l2_dev = &aiisp_dev->v4l2_dev;

	rc = v4l2_device_register(&(pdev->dev), aiisp_dev->vdev->v4l2_dev);
	if (rc < 0) {
		AIISP_ERR(CAM_AIISP, "v4l2_device_register failed %d", rc);
		goto register_fail;
	}

	strlcpy(aiisp_dev->vdev->name, CAM_AIISP_NAME,
		sizeof(aiisp_dev->vdev->name));
	aiisp_dev->vdev->release  = video_device_release_empty;
	aiisp_dev->vdev->fops     = &cam_aiisp_v4l2_fops;
	aiisp_dev->vdev->ioctl_ops = &g_cam_aiisp_ioctl_ops;
	aiisp_dev->vdev->minor     = -1;
	aiisp_dev->vdev->device_caps |= V4L2_CAP_VIDEO_CAPTURE;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	aiisp_dev->vdev->vfl_type  = VFL_TYPE_GRABBER;
	rc = video_register_device(aiisp_dev->vdev,
				   VFL_TYPE_GRABBER, -1);
#else
	aiisp_dev->vdev->vfl_type  = VFL_TYPE_VIDEO;
	rc = video_register_device(aiisp_dev->vdev,
				   VFL_TYPE_VIDEO, -1);
#endif
	if (rc < 0) {
		AIISP_ERR(CAM_AIISP, "video_register_device failed %d", rc);
		goto v4l2_fail;
	}

	cam_aiisp_init_entity(aiisp_dev);
	INIT_LIST_HEAD(&aiisp_dev->list);
	atomic_set(&aiisp_dev->seq_num, 0);
	video_set_drvdata(aiisp_dev->vdev, aiisp_dev);

	aiisp_dev->work_queue = alloc_workqueue(CAM_AIISP_WORKQUEUE_NAME,
						WQ_HIGHPRI | WQ_UNBOUND, 1);

	if (!aiisp_dev->work_queue) {
		rc = -ENOMEM;
		goto v4l2_fail;
	}

	register_process_msg_callback(&recv_tid, cam_aiisp_ipcm_callback_func);
	aiisp_dev->aiisp_connected = 1;
	aiisp_dev->send_async = 0;

	aiisp_dev->pdelay = 2;
	aiisp_dev->fsync_gpio_num = 0;

	atomic_set(&aiisp_dev->no_aiisp_rtos_session, NO_AIISP_RTOS_SESSION);
	atomic_set(&aiisp_dev->no_aiisp_rtos_stream, NO_AIISP_RTOS_STREAM);
	cam_aiisp_debug_register();
	aiisp_dev->dev = &pdev->dev;

	mutex_init(&aiisp_dev->ioctl_lock);
	atomic_set(&aiisp_dev->ioctl_running_count, 0);
	aiisp_dev->closing = 0;

	atomic_set(&aiisp_dev->open_times, 0);
	aiisp_dev->error_hint = false;
#ifdef QCOM_PLATFORM_CRM
	aiisp_dev->ops.get_dev_info = cam_aiisp_publish_dev_info;
	aiisp_dev->ops.link_setup = cam_aiisp_establish_link;
	aiisp_dev->ops.apply_req = cam_aiisp_apply_request;
	aiisp_dev->ops.notify_frame_skip = cam_aiisp_notify_frame_skip;
	aiisp_dev->ops.flush_req = cam_aiisp_flush_request;
	rc = cam_fsync_qcom_gpio_init(aiisp_dev);
	if (rc)
		goto v4l2_fail;
#else
	rc = cam_fsync_mtk_gpio_init(aiisp_dev);
#endif

	platform_set_drvdata(pdev, aiisp_dev);

    AIISP_INFO(CAM_AIISP, "ai_isp prob success!");
	return rc;

v4l2_fail:
	v4l2_device_unregister(aiisp_dev->vdev->v4l2_dev);
register_fail:
	cam_aiisp_media_controller_cleanup(aiisp_dev);
mcinit_fail:
	video_unregister_device(aiisp_dev->vdev);
	video_device_release(aiisp_dev->vdev);
vdev_fail:
	mutex_destroy(&aiisp_dev->aiisp_lock);
	mutex_destroy(&aiisp_dev->stream_lock);
	mutex_destroy(&aiisp_dev->list_lock);
	kfree(aiisp_dev);
	return rc;
}

static int cam_ai_isp_dev_remove(struct platform_device *pdev)
{
#ifdef QCOM_PLATFORM_CRM
	struct aiisp_device *aiisp_dev = NULL;
	int i = 0;
	aiisp_dev = platform_get_drvdata(pdev);
	for (i = 0; i < aiisp_dev->fsync_gpio_num; i++) {
		gpio_free(aiisp_dev->syncs_info[i].fsync_gpio);
	}
#endif

	cam_aiisp_debug_unregister();
	return 0;
}

struct platform_driver ai_isp_driver = {
	.probe = cam_ai_isp_dev_probe,
	.remove = cam_ai_isp_dev_remove,
	.driver = {
		.name = "cam_aiisp_dev",
		.of_match_table = cam_ai_isp_dt_match,
		.suppress_bind_attrs = true,
	},
};

int cam_ai_isp_dev_init(void)
{
	return platform_driver_register(&ai_isp_driver);
}

void cam_ai_isp_dev_exit(void)
{
	platform_driver_unregister(&ai_isp_driver);
}

module_init(cam_ai_isp_dev_init);
module_exit(cam_ai_isp_dev_exit);

MODULE_AUTHOR("guxiao@tetras.com");
MODULE_DESCRIPTION("Tetras AI ISP driver");
MODULE_LICENSE("GPL v2");
