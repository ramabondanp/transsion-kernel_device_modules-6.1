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

/**
 * @brief AI ISP Driver Header
 * @date  2022-03-03
 */

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

#include "comm_drv_io.h"
#include "package_handle.h"
#include "tis_plat_evt_def.h"
#include "cam_aiisp.h"
#include "cam_aiisp_dbg.h"
#include "cam_aiisp_internal.h"
#include "cam_aiisp_api.h"
#include "cam_aiisp_dev.h"
#include "cam_aiisp_qcom_dev.h"

DECLARE_WAIT_QUEUE_HEAD(worker);

int cam_ai_isp_get_snv_switch(uint32_t *value)
{
	*value = get_aiisp_dev()->pipe_delay;
	return 0;
}
EXPORT_SYMBOL(cam_ai_isp_get_snv_switch);

static int cam_ai_isp_sensor_req_thd(void *data)
{
	struct cam_req_mgr_apply_request app_info;
	struct aiisp_device *aiisp_dev = get_aiisp_dev();
	uint64_t worker_reqid = 1;

	AIISP_INFO(CAM_AIISP, "%s", __func__);
	while (true) {
		wait_event(worker, (aiisp_dev->sns_req_mgr.to_apply_reqid > worker_reqid)
			   || aiisp_dev->sns_req_mgr.th_stop);

		if (aiisp_dev->sns_req_mgr.th_stop)
			break;

		worker_reqid = aiisp_dev->sns_req_mgr.to_apply_reqid;

		app_info.dev_hdl = aiisp_dev->sns_req_mgr.info.dev_hdl;
		app_info.request_id = worker_reqid;
		aiisp_dev->sns_req_mgr.info.apply_req(&app_info);
		AIISP_INFO(CAM_AIISP, "sensor applied %llu", worker_reqid);
	}

	AIISP_INFO(CAM_AIISP, "thread exit");
	do_exit(0);
}

int cam_aiisp_reg_sensor_ops(cam_req_mgr_apply_req apply_req, cam_req_mgr_flush_req flush_req)
{
	struct aiisp_device *aiisp_dev = get_aiisp_dev();
	aiisp_dev->sns_req_mgr.info.apply_req = apply_req;
	aiisp_dev->sns_req_mgr.info.flush_req = flush_req;
	return 0;
}
EXPORT_SYMBOL(cam_aiisp_reg_sensor_ops);

static int find_missed_slot(uint64_t req_id)
{
	int i;
	struct aiisp_device *aiisp_dev = get_aiisp_dev();
	for (i = 0; i < 4; i++) {
		if (req_id == aiisp_dev->sns_req_mgr.reqs[i].missed_reqid
		    && test_bit(i, &aiisp_dev->sns_req_mgr.missed_bitmap) == true)
			return i;
	}

	return -1;
}

static int add_to_vacant_slot(uint64_t req_id)
{
	int i;
	struct aiisp_device *aiisp_dev = get_aiisp_dev();
	for (i = 0; i < 4; i++) {
		if (test_bit(i, &aiisp_dev->sns_req_mgr.missed_bitmap) == false) {
			aiisp_dev->sns_req_mgr.reqs[i].missed_reqid = req_id;
			aiisp_dev->sns_req_mgr.reqs[i].missed_cnt = 1;
			__set_bit(i, &aiisp_dev->sns_req_mgr.missed_bitmap);

			return i;
		}
	}

	return -1;
}

int cam_aiisp_reg_sensor_req(struct sensor_req_param *param)
{
	int idx;
	struct aiisp_device *aiisp_dev = get_aiisp_dev();

	AIISP_INFO(CAM_AIISP, "sensor reg %llu", param->req_id);

	spin_lock(&aiisp_dev->sns_req_mgr.reqid_lock);

	if (aiisp_dev->sns_req_mgr.last_reg_reqid + 1 < param->req_id) {
		aiisp_dev->sns_req_mgr.last_reg_reqid = param->req_id;
		spin_unlock(&aiisp_dev->sns_req_mgr.reqid_lock);

		AIISP_ERR(CAM_AIISP, "sensor reqid jump, %llu -> %llu",
			  aiisp_dev->sns_req_mgr.last_reg_reqid, param->req_id);

		return -EINVAL;
	}

	aiisp_dev->sns_req_mgr.info.dev_hdl = param->dev_hdl;

	if (param->req_id > aiisp_dev->sns_req_mgr.last_reg_reqid)
		aiisp_dev->sns_req_mgr.last_reg_reqid = param->req_id;

	if (aiisp_dev->sns_req_mgr.missed_bitmap == 0) {
		spin_unlock(&aiisp_dev->sns_req_mgr.reqid_lock);
		return 0;
	}

	idx = find_missed_slot(param->req_id);
	if (idx < 0) {
		spin_unlock(&aiisp_dev->sns_req_mgr.reqid_lock);
		return 0;
	}

	if (--aiisp_dev->sns_req_mgr.reqs[idx].missed_cnt == 0)
		__clear_bit(idx, &aiisp_dev->sns_req_mgr.missed_bitmap);

	spin_unlock(&aiisp_dev->sns_req_mgr.reqid_lock);
	AIISP_INFO(CAM_AIISP, "sensor reg %llu, %u left",
		   param->req_id, aiisp_dev->sns_req_mgr.reqs[idx].missed_cnt);

	return -EAGAIN;
}
EXPORT_SYMBOL(cam_aiisp_reg_sensor_req);

/* qcom CRM command map to aiisp command */
static int cam_aiisp_qcom_command_map(int cmd, int *map_cmd)
{
	int ret = 0;
	struct aiisp_device *aiisp_dev = get_aiisp_dev();
	switch (cmd) {
	case HW_ACQUIRE: {
		*map_cmd = TIS_EVT_CAMERA_DEVICE_CREATE_STREAM;
		if (aiisp_dev->action_mask & STREAM_CREATE_MASK) {
			AIISP_INFO(CAM_AIISP, "Qcom TIS_EVT_CAMERA_DEVICE_CREATE_STREAM IGNORE.");
			ret = -EACCES;
		}
		break;
	}

	case HW_START: {
		*map_cmd = TIS_EVT_CAMERA_STREAM_START;
		if (aiisp_dev->action_mask & STREAM_ON_MASK) {
			AIISP_INFO(CAM_AIISP, "Qcom TIS_EVT_CAMERA_STREAM_START IGNORE.");
			ret = -EACCES;
		}

		if (aiisp_dev->pipe_delay) {
			aiisp_dev->sns_req_mgr.fcnt = 0;
			aiisp_dev->sns_req_mgr.reqid2fcnt = 1;
			aiisp_dev->sns_req_mgr.last_apply_reqid = 1;
			aiisp_dev->sns_req_mgr.last_reg_reqid = 1;
			aiisp_dev->sns_req_mgr.to_apply_reqid = 1;
			memset(aiisp_dev->sns_req_mgr.reqs, 0, sizeof(aiisp_dev->sns_req_mgr.reqs));
			aiisp_dev->sns_req_mgr.missed_bitmap = 0;
			aiisp_dev->sns_req_mgr.info.apply_req = NULL;
			aiisp_dev->sns_req_mgr.info.flush_req = NULL;
			spin_lock_init(&aiisp_dev->sns_req_mgr.reqid_lock);
			aiisp_dev->sns_req_mgr.th_stop = false;
			kthread_run(cam_ai_isp_sensor_req_thd, NULL, "ai_isp_sensor_req_thread", 0);
		}

		aiisp_dev->read_p = 0;
		aiisp_dev->write_p = 0;

		break;
	}

	case HW_STOP: {
		*map_cmd = TIS_EVT_CAMERA_STREAM_STOP;

		if (aiisp_dev->pipe_delay) {
			aiisp_dev->sns_req_mgr.th_stop = true;
			wake_up(&worker);
		}

		if (aiisp_dev->action_mask & STREAM_OFF_MASK) {
			AIISP_INFO(CAM_AIISP, "Qcom TIS_EVT_CAMERA_STREAM_STOP IGNORE.");
			ret = -EACCES;
		}
		break;
	}

	case HW_RELEASE: {
		*map_cmd = TIS_EVT_CAMERA_DEVICE_DESTROY_STREAM;
		if (aiisp_dev->action_mask & STREAM_REL_MASK) {
			AIISP_INFO(CAM_AIISP, "Qcom TIS_EVT_CAMERA_DEVICE_DESTROY_STREAM IGNORE.");
			ret = -EACCES;
		}
		break;
	}

	case HW_CONFIG: {
		*map_cmd = TIS_EVT_CAMERA_STREAM_CONFIG;
		if (aiisp_dev->action_mask & HW_CONFIG_MASK) {
			AIISP_INFO(CAM_AIISP, "Qcom TIS_EVT_CAMERA_STREAM_CONFIG IGNORE.");
			ret = -EACCES;
		}
		break;
	}

	default:
		AIISP_ERR(CAM_AIISP, "QCOM Unknown op code %u", cmd);
		return -EINVAL;
	}
	return ret;
}

int cam_aiisp_send_cmd_from_qcom(void *hw_args)
{
	int ret = 0;
	int ptr_type = DATA_PTR_USER_TYPE;
	struct cam_aiisp_hw_cmd_args *args = hw_args;
	struct cam_aiisp_hw_config *config_data = NULL;
	int command = 0;
	int slot = 0;
	struct aiisp_device *aiisp_dev = get_aiisp_dev();
	/* note: kernel ptr type */
	ptr_type = DATA_PTR_KERNEL_TYPE;
	ret = cam_aiisp_qcom_command_map(args->cmd, &command);
	if (ret == -EACCES) {
		ret = 0;
	} else {
		if (ret) {
			return ret;
		}
		if (AP2RTOS_CONFIG == command) {
			config_data  = (struct cam_aiisp_hw_config *)args->data;
			slot = config_data->hinfo.payload_len;
			if (aiisp_dev->streams_info[slot].stream_id !=
			    config_data->hinfo.stream_handle) {
				AIISP_ERR(CAM_AIISP,
					  "slot %d stream id %d not equal to current stream id %d",
					  slot, aiisp_dev->streams_info[slot].stream_id,
					  config_data->hinfo.stream_handle);
			}
			AIISP_INFO(CAM_AIISP, "crm config request id %d  => frame id %d ",
				   config_data->params.frame_id, config_data->params.target_framid);
			config_data->params.target_framid =
				aiisp_dev->streams_info[slot].curr_frame_id + aiisp_dev->pdelay;
			AIISP_INFO(CAM_AIISP, "config request id %d  => frame id %d ",
				   config_data->params.frame_id, config_data->params.target_framid);
		}
		ret = cam_aiisp_send_rtos_cmd(command, ptr_type, args->data, args->size);
	}
	return ret;
}


static irqreturn_t cam_fsync_qcom_irq_thread(int irq, void *dev)
{
	struct cam_aiisp_hw_config config;
	struct aiisp_device *aiisp_dev = get_aiisp_dev();
	int idx;

	aiisp_dev->sns_req_mgr.fcnt++;

	AIISP_INFO(CAM_AIISP, "irq %d", aiisp_dev->sns_req_mgr.fcnt);

	if (aiisp_dev->sns_req_mgr.fcnt == 1)
		return IRQ_HANDLED;

	if (aiisp_dev->read_p != aiisp_dev->write_p) {
		memcpy(&config, aiisp_dev->req_list + aiisp_dev->read_p++,
		       sizeof(struct cam_aiisp_hw_config));
		/* config.params.frame_id = aiisp_dev->sns_req_mgr.fcnt + 2; */
		config.params.target_framid =
			config.params.frame_id - aiisp_dev->sns_req_mgr.reqid2fcnt + 2;
		cam_aiisp_send_rtos_cmd(AP2RTOS_CONFIG,
					DATA_PTR_KERNEL_TYPE, &config,
					sizeof(struct cam_aiisp_hw_config));
		AIISP_INFO(CAM_AIISP, "config request id %d => frame id %d",
			   config.params.frame_id, config.params.target_framid);

		if (aiisp_dev->read_p == 8)
			aiisp_dev->read_p = 0;
	} else {
		AIISP_INFO(CAM_AIISP, "underflow");
	}

	spin_lock(&aiisp_dev->sns_req_mgr.reqid_lock);

	/* new reqid available */
	if (aiisp_dev->sns_req_mgr.last_apply_reqid <
	    aiisp_dev->sns_req_mgr.last_reg_reqid) {
		aiisp_dev->sns_req_mgr.to_apply_reqid =
			++aiisp_dev->sns_req_mgr.last_apply_reqid;
		spin_unlock(&aiisp_dev->sns_req_mgr.reqid_lock);

		AIISP_INFO(CAM_AIISP, "proceed with req %llu",
			   aiisp_dev->sns_req_mgr.to_apply_reqid);
		wake_up(&worker);
		return IRQ_HANDLED;
	}

	/* miss */
	idx = find_missed_slot(aiisp_dev->sns_req_mgr.last_apply_reqid + 1);
	/* already missed */
	if (idx >= 0) {
		aiisp_dev->sns_req_mgr.reqs[idx].missed_cnt++;
		spin_unlock(&aiisp_dev->sns_req_mgr.reqid_lock);

		AIISP_INFO(CAM_AIISP, "reqid %llu missed %u times",
			   aiisp_dev->sns_req_mgr.last_apply_reqid + 1,
			   aiisp_dev->sns_req_mgr.reqs[idx].missed_cnt);
		return IRQ_HANDLED;
	}

	/* new miss */
	if (add_to_vacant_slot(aiisp_dev->sns_req_mgr.last_apply_reqid + 1) >= 0) {
		spin_unlock(&aiisp_dev->sns_req_mgr.reqid_lock);
		AIISP_INFO(CAM_AIISP, "missed expected %llu",
			   aiisp_dev->sns_req_mgr.last_apply_reqid + 1);
		return IRQ_HANDLED;
	} else {
		spin_unlock(&aiisp_dev->sns_req_mgr.reqid_lock);
		AIISP_INFO(CAM_AIISP, "missed req overflow");
		{
			int j;
			for (j = 0; j < 4; j++)
				AIISP_INFO(CAM_AIISP, "miss %llu: %d",
					   aiisp_dev->sns_req_mgr.reqs[j].missed_reqid,
					   aiisp_dev->sns_req_mgr.reqs[j].missed_cnt);
		}
		return IRQ_HANDLED;
	}
}


static irqreturn_t cam_fsync_qcom_irq_handler(int irq, void *dev_id)
{
	struct aiisp_sync_sensor_info *sync_info;
	struct aiisp_device *aiisp_dev = get_aiisp_dev();
	int slot_idx = 0;
	struct aiisp_stream_info *stream_info = NULL;

	sync_info = (struct aiisp_sync_sensor_info *)dev_id;
	slot_idx = sync_info->slot_idx;
	stream_info = &aiisp_dev->streams_info[slot_idx];
	if (stream_info->stream_id != -2) {
		stream_info->curr_frame_id++;
		if (stream_info->curr_frame_id % 100 == 0) {
			AIISP_INFO(CAM_AIISP, "irq %d slot idx %d frame id %lld ",
				   irq, sync_info->slot_idx, stream_info->curr_frame_id);
		}
	} else {
		AIISP_ERR(CAM_AIISP, "should not recv irq in this state");
		aiisp_dev->streams_info[sync_info->slot_idx].curr_frame_id = 0;
	}

	return IRQ_WAKE_THREAD;
}

int cam_fsync_qcom_gpio_init(struct aiisp_device *aiisp_dev)
{
	int ret, irq_num;
	struct device *dev = aiisp_dev->dev;
	struct aiisp_sync_sensor_info *sync_info;
	sync_info = &(aiisp_dev->syncs_info[aiisp_dev->fsync_gpio_num]);
	sync_info->fsync_gpio = of_get_named_gpio_flags(dev->of_node, "fsync-intr0", 0, NULL);

	ret = gpio_request(sync_info->fsync_gpio, "fsync-intr0");
	if (ret) {
		AIISP_ERR(CAM_AIISP, "fsync unable to request gpio [%d]", sync_info->fsync_gpio);
		return ret;
	}

	ret = gpio_direction_input(sync_info->fsync_gpio);
	if (ret) {
		AIISP_ERR(CAM_AIISP, "fsync gpio set direction for gpio [%d] failed",
			  sync_info->fsync_gpio);
		goto err;
	}

	irq_num = gpio_to_irq(sync_info->fsync_gpio);
	sync_info->irq_num = irq_num;
	/* FIXME: should fetch from dts */
	sync_info->sensor_id = 4;
	sync_info->slot_idx = aiisp_dev->fsync_gpio_num;

	ret = devm_request_threaded_irq(dev, irq_num,
					cam_fsync_qcom_irq_handler,
					cam_fsync_qcom_irq_thread,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"fsync_irq", sync_info);
	if (ret) {
		AIISP_ERR(CAM_AIISP, "cam fsync failed irq=%d request ret = %d",
			  irq_num, ret);
		goto err;
	}
	aiisp_dev->fsync_gpio_num++;

	AIISP_INFO(CAM_AIISP, "fsync gpio init finished");

	return 0;

err:
	gpio_free(sync_info->fsync_gpio);
	AIISP_ERR(CAM_AIISP, "fsync gpio init error %d", ret);
	return ret;
}
