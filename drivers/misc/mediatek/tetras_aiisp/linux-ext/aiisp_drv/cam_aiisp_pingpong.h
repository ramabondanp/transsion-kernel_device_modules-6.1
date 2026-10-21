/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Change Logs:
 * Date         Author          Notes
 * 2022-12-08   Gu Xiao         Initialize.
 *
 * AI ISP Driver header for MTK platform
 */

#ifndef __CAM_AI_ISP_PINGPONG_H__
#define __CAM_AI_ISP_PINGPONG_H__

struct pingpong_handle {
	uint32_t count;
	uint32_t buf_base_addr;
	uint32_t single_buf_size;
	uint32_t single_buf_num;
	struct target_id tid;
};

struct pingpong_handle *
pingpong_buf_init(uint32_t buf_addr, uint32_t single_buf_size, uint32_t single_buf_num);
int pingpong_buf_write(struct pingpong_handle *handle, char *buf, size_t length);
int pingpong_buf_read(struct pingpong_handle *handle, char *buf, size_t length);
void pingpong_buf_deinit(struct pingpong_handle *handle);
int pingpong_buf_start(struct pingpong_handle *handle);

#endif/* __CAM_AI_ISP_PINGPONG_H__ */
