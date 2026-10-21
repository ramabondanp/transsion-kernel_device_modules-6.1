/*
 * (C) Copyright 2021-2022, Shenzhen Tetras.AI Technology Co., Ltd
 *
 * Change Logs:
 * Date         Author          Notes
 * 2022-03-03   Yao Kun         Initialize.
 */

/**
 * @brief AI ISP Driver Header
 * @date  2022-03-03
 */

#ifndef __CAM_AI_ISP_API_H__
#define __CAM_AI_ISP_API_H__

#include <linux/types.h>

#include <camera/media/cam_defs.h>
#include <camera/media/cam_req_mgr.h>
#include <cam_req_mgr_core.h>
#include "cam_ai_isp_hw.h"
#include "cam_aiisp.h"

enum HW_CMD_LIST {
	GET_CAP     = 0x1,
	HW_ACQUIRE  = 0x2,
	HW_START    = 0x3,
	HW_STOP     = 0x4,
	HW_READ     = 0x5,
	HW_WRITE    = 0x6,
	HW_RELEASE  = 0x7,
	HW_PRE_UPD  = 0x8,
	HW_CONFIG   = 0x9,
	HW_RESET    = 0xA,
	HW_CMD      = 0xB,
	HW_CMD_BUTT = 0xC
};

struct cam_aiisp_hw_cmd_args {
	uint32_t cmd;
	char* data;
	uint64_t size;
};

struct sensor_req_param {
	int32_t sensor_id;
	int32_t link_hdl;
	int32_t dev_hdl;
	uint64_t req_id;
};

typedef int (*cam_aiisp_event_cb_func)(uint32_t evt_id,
	struct cam_ai_isp_hw_event_info *evt_info);

int32_t cam_aiisp_insert_link(struct cam_req_mgr_ver_info *link_info,
			      int32_t dev_handle);
void cam_aiisp_unlink(struct cam_req_mgr_core_link *link);
int32_t cam_aiisp_get_param(struct cam_req_mgr_kmd_ops **ops, uint64_t *dev_id);
int32_t cam_aiisp_hw_mgr(struct cam_aiisp_hw_cmd_args *args);
int32_t cam_aiisp_hw_mgr_evt(cam_aiisp_event_cb_func event_cb);

int32_t cam_aiisp_publish_dev_info(struct cam_req_mgr_device_info *info);
int32_t cam_aiisp_establish_link(struct cam_req_mgr_core_dev_link_setup *link);
int32_t cam_aiisp_apply_request(struct cam_req_mgr_apply_request *apply);
int32_t cam_aiisp_notify_frame_skip(struct cam_req_mgr_apply_request *apply);
int32_t cam_aiisp_flush_request(struct cam_req_mgr_flush_request *flush_req);

int cam_ai_isp_get_snv_switch(uint32_t *value);
int cam_aiisp_reg_sensor_req(struct sensor_req_param *param);
int cam_aiisp_reg_sensor_ops(cam_req_mgr_apply_req apply_req, cam_req_mgr_flush_req flush_req);

#endif /* __CAM_AI_ISP_API_H__ */
