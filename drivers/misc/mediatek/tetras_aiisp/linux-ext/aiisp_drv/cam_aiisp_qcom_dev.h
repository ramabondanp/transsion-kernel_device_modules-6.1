#ifndef __CAM_AI_ISP_QCOM_DEV_H__
#define __CAM_AI_ISP_QCOM_DEV_H__

#include "cam_aiisp_api.h"
#include "cam_req_mgr_interface.h"
#include "cam_ai_isp_hw.h"
#include "cam_ai_isp_hw_mgr_intf.h"

struct sensor_info {
	int32_t dev_hdl;
	cam_req_mgr_apply_req         apply_req;
	cam_req_mgr_flush_req         flush_req;
};

int cam_ai_isp_get_snv_switch(uint32_t *value);
int cam_aiisp_reg_sensor_ops(cam_req_mgr_apply_req apply_req, cam_req_mgr_flush_req flush_req);
int cam_aiisp_reg_sensor_req(struct sensor_req_param *param);
int cam_aiisp_send_cmd_from_qcom(void *hw_args);
#endif/* __CAM_AI_ISP_QCOM_DEV_H__ */
