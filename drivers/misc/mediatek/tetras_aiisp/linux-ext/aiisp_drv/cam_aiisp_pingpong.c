/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Change Logs:
 * Date         Author          Notes
 * 2023-03-17   zhoumiao         Initialize.
 *
 * AI ISP Driver for MTK platform
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/slab.h>

#include "comm_drv_io.h"
#include "cam_aiisp_pingpong.h"
#include "cam_aiisp_dbg.h"

/* #define DEBUG_MTK_VSYNC_SIMU */
/**
 * @brief init_pingping_buf() - read data from rtos mem addr.
 * @param buf_addr: ping buffer addr.
 * @param pong_buf_offset: pong buffer offset.
 * @param comm_type: phy type.
 * @return pingpong handle.
 */
struct pingpong_handle *
pingpong_buf_init(uint32_t buf_addr, uint32_t single_buf_num, uint32_t single_buf_size)
{
	struct pingpong_handle *handle = NULL;

	handle = kzalloc(sizeof(struct pingpong_handle), GFP_KERNEL);
	if (NULL == handle) {
		AIISP_ERR(CAM_AIISP, "pingpong_buf malloc handle fail!");
		return NULL;
	}

	handle->count = 0;
	handle->buf_base_addr = buf_addr;
	handle->single_buf_size = single_buf_size;
	handle->single_buf_num = single_buf_num;

	AIISP_INFO(CAM_AIISP, "pingpong_buf handle init success!");
	AIISP_INFO(CAM_AIISP, "pingpong_buf remote buf_addr = 0x%x", buf_addr);
	AIISP_INFO(CAM_AIISP, "pingpong_buf buf_size = 0x%x", single_buf_size);
	AIISP_INFO(CAM_AIISP, "pingpong_buf buf_num = 0x%x", single_buf_num);

	return handle;
}

int pingpong_buf_read(struct pingpong_handle *handle, char *buf, size_t length)
{
	return 0;
}

int pingpong_buf_start(struct pingpong_handle *handle)
{
	if (NULL == handle) {
		AIISP_ERR(CAM_AIISP, "write_pingpong_buf handle is NULL!");
		return -1;
	}

	handle->count = 0;

	return 0;
}
/* This hardcode, should fix by communication */
static struct target_id recv_tid = {
	.pno = 20,
	.chip_id = 1,
};

int pingpong_buf_write(struct pingpong_handle *handle, char *buf, size_t length)
{
	uint32_t write_buf_addr = 0;
	char *tmp_buf = NULL;

	if (NULL == handle) {
		AIISP_ERR(CAM_AIISP, "write_pingpong_buf handle is NULL!");
		return -1;
	}

	tmp_buf = kzalloc(handle->single_buf_size, GFP_KERNEL);
	if (NULL == tmp_buf) {
		AIISP_ERR(CAM_AIISP, "alloc tmp_buf fail!");
		return -1;
	}

	write_buf_addr = handle->buf_base_addr + (handle->count % handle->single_buf_num) *
			 handle->single_buf_size;

	if (length > handle->single_buf_size - sizeof(uint32_t)) {
		AIISP_ERR(CAM_AIISP, "send length: %zu large than %zu",
			  length, handle->single_buf_size - sizeof(uint32_t));
		kfree(tmp_buf);

		return -1;
	}

	handle->count++;
	memcpy(tmp_buf, buf, length);
	memcpy(tmp_buf + handle->single_buf_size - sizeof(uint32_t), (char *)&handle->count,
	       sizeof(uint32_t));

	/* This hardcode, should fix by communication */
	write_data_for_kernel_thread(write_buf_addr, tmp_buf, handle->single_buf_size,
				     &recv_tid);

	kfree(tmp_buf);

	return 0;
}

void pingpong_buf_deinit(struct pingpong_handle *handle)
{
	if (NULL == handle) {
		AIISP_ERR(CAM_AIISP, "deinit pingpong_buf input null!");
		return;
	}

	kfree(handle);
	handle = NULL;

	AIISP_INFO(CAM_AIISP, "pingpong_buf handle deinit success!");
}
