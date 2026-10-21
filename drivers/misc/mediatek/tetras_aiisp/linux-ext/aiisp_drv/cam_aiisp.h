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

#ifndef __UAPI_CAM_AI_ISP_H__
#define __UAPI_CAM_AI_ISP_H__

#include <linux/types.h>
#include "tuning_param.h"
#include "parameter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AIISP_EXPOSURE_MAX 3

enum AIISP_RTOS_COMMAND {
	AP2RTOS_DOWNLOAD_TUNING_FILE_DONE,
	AP2RTOS_INIT, /* init with camera id */
	AP2RTOS_FLUSH,
	AP2RTOS_CONFIG_STREAM, /* config stream */
	AP2RTOS_CONFIG, /* Add Request */
	AP2RTOS_STREAM_ON, /* stream on */
	AP2RTOS_STREAM_OFF, /* stream off */
	AP2RTOS_STREAM_SET_PIPEDELAY, /* set pipedelay number */
	AP2RTOS_STREAM_RELEASE, /* relese stream resource */
	AP2RTOS_RELEASE, /* release with camera id */
	AP2RTOS_TRIGGER_CONFIG_HW, /* trigger hw config */
	AP2RTOS_BUTT
};

enum AIISP_CONFIG {
	AIISP_SET_PIPEDELAY,
};

enum AIISP_ACTION_MASK {
	STREAM_CREATE_MASK = 1 << 0,
	STREAM_ON_MASK = 1 << 1,
	HW_CONFIG_MASK = 1 << 2,
	STREAM_OFF_MASK = 1 << 3,
	STREAM_REL_MASK = 1 << 4,
};

struct cam_aiisp_send_cmd {
	uint32_t cmd;
	uint64_t size;
	uint64_t ptr;
	int32_t  ret;
};

struct cam_aiisp_write_sram {
	uint32_t write_addr;
	uint64_t length;
	uint64_t ptr;
	int32_t  ret;
};

struct cam_aiisp_read_sram {
	uint32_t cmd;
	uint64_t size;
	uint64_t ptr;
	int32_t  ret;
};

struct cam_aiisp_tuning_sram {
	uint64_t data_config;
	uint64_t ptr;
	int32_t  ret;
};

struct cam_aiisp_add_req {
	uint64_t size;
	uint64_t ptr;
};

struct cam_aiisp_add_request {
	int32_t  link_hdl;
	int32_t  dev_hdl;
	uint64_t req_id;
	uint32_t skip_at_sof;
	uint32_t skip_at_eof;
	bool trigger_eof;
};

struct cam_aiisp_action_mask {
	int mask;
};

struct cam_aiisp_poweron {
	uint32_t reserved;
};

struct cam_aiisp_poweroff {
	uint32_t reserved;
};

struct cam_aiisp_config_item {
	uint32_t cmd;
	uint32_t value;
	int32_t  ret;
};

// hal init command
struct cam_aiisp_ins_init {
	struct cam_cmd_header hinfo;
	//init data
	char init_data[0];
};

struct cam_aiisp_stream_flush {
	struct cam_cmd_header hinfo;
	int32_t  req_id;
	uint32_t reserved;
};

struct cam_aiisp_stream_config {
	struct cam_cmd_header hinfo;
	struct cam_cmd_stream_config config;
};

struct cam_aiisp_stream_SNV_switch {
	struct cam_cmd_header hinfo;
	int SNV_switch;
};

struct cam_aiisp_stream_cap {
	struct cam_cmd_header hinfo;
	cam_stream_capability_t caps;
};

struct cam_aiisp_hw_config {
	struct cam_cmd_header hinfo;
	struct cam_3a_info params;
};

struct cam_aiisp_tuning_data_config {
	struct cam_cmd_header hinfo;
	uint32_t data_size;
};

struct cam_aiisp_tuning_addr_config {
	struct cam_cmd_header hinfo;
	uint32_t data_addr;
};

struct cam_aiisp_hw_trigger_info {
	struct cam_cmd_header hinfo;
	uint64_t   request_id;
	uint64_t   frame_id;
};

struct cam_aiisp_open_times {
	uint32_t open_times;
	uint32_t threshold;
};

#ifdef __cplusplus
}
#endif
#endif /* __UAPI_CAM_AI_ISP_H__ */
