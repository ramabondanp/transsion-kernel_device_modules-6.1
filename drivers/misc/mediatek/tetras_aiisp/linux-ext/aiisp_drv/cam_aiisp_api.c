/*
 * (C) Copyright 2021-2022, Shenzhen Tetras.AI Technology Co., Ltd
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

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <media/v4l2-event.h>

#include "cam_req_mgr_util.h"
#include "cam_req_mgr_interface.h"
#include "cam_aiisp_dbg.h"
#include "cam_aiisp_internal.h"
#include "cam_aiisp_api.h"
#include "cam_aiisp_dev.h"

int32_t cam_aiisp_publish_dev_info(struct cam_req_mgr_device_info *info)
{
	int rc = 0;

	if (!info)
		return -EINVAL;

	info->dev_id = CAM_REQ_MGR_DEVICE_AI_ISP_HW;
	strlcpy(info->name, CAM_AIISP_NAME, sizeof(info->name));
	info->p_delay = 2;
	info->trigger = CAM_TRIGGER_POINT_SOF;
	AIISP_ERR(CAM_AIISP, "cam_aiisp_publish_dev_info.");

	return rc;
}

int32_t cam_aiisp_establish_link(struct cam_req_mgr_core_dev_link_setup *link)
{
	struct aiisp_device *aiisp_dev = get_aiisp_dev();
	if (!link)
		return -EINVAL;
	aiisp_dev->crm_cb = link->crm_cb;
	AIISP_ERR(CAM_AIISP, "cam_aiisp_establish_link.");
	return 0;
}

int32_t cam_aiisp_apply_request(struct cam_req_mgr_apply_request *apply)
{
	AIISP_ERR(CAM_AIISP, "cam_aiisp_apply_request.");
	return 0;
}

int32_t cam_aiisp_notify_frame_skip(struct cam_req_mgr_apply_request *apply)
{
	AIISP_ERR(CAM_AIISP, "cam_aiisp_notify_frame_skip.");
	return 0;
}

int32_t cam_aiisp_flush_request(struct cam_req_mgr_flush_request *flush_req)
{
	AIISP_ERR(CAM_AIISP, "cam_aiisp_flush_request.");
	return 0;
}

int32_t cam_aiisp_hw_mgr(struct cam_aiisp_hw_cmd_args *args)
{
	int32_t ret = 0;

	AIISP_INFO(CAM_AIISP, "cam_aiisp_hw_mgr ");
	ret = cam_aiisp_send_cmd_from_qcom(args);
	if (ret) {
		AIISP_ERR(CAM_AIISP, "cam_aiisp_hw_mgr err ret = %d.", ret);
	}

	return 0;
}
EXPORT_SYMBOL(cam_aiisp_hw_mgr);

int32_t cam_aiisp_hw_mgr_evt(cam_aiisp_event_cb_func event_cb)
{
	struct aiisp_device *aiisp_dev = get_aiisp_dev();
	AIISP_ERR(CAM_AIISP, "cam_aiisp_hw_mgr_evt.");
	aiisp_dev->event_cb = event_cb;

	return 0;
}
EXPORT_SYMBOL(cam_aiisp_hw_mgr_evt);

int32_t cam_aiisp_get_param(struct cam_req_mgr_kmd_ops **ops,
			    uint64_t *dev_id)
{
	struct aiisp_device *aiisp_dev = get_aiisp_dev();
	AIISP_ERR(CAM_AIISP, "cam_aiisp_get_param.");
	*ops = &(aiisp_dev->ops);
	*dev_id = CAM_AIISP;

	return 0;
}
EXPORT_SYMBOL(cam_aiisp_get_param);
