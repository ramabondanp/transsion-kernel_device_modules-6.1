/*
 * (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * Change Logs:
 * Date           Author        Notes
 * 2022-01-21     Zhu Shiqiang  Initialize.
 */

/**
 * @brief   IPCM Ring Buffer Interface
 * @date    202-01-21
 */

#ifndef __IPC_RING_BUFFER_H__
#define __IPC_RING_BUFFER_H__

#include "ipc_basic.h"

typedef struct ring_buf_header {
	void  *ring_buffer_addr;
	uint16_t  write_pos;
	uint16_t  read_pos;
	uint16_t  recycle_pos;
	uint16_t  buffer_cur_count;
	uint16_t  single_unit_size;
	uint16_t  buffer_num;
	uint16_t  max_use_count;
} ring_buffer_head_t;

int ring_buffer_init(ring_buffer_head_t *p_ring_buffer,
			uint32_t single_unit_size,
			uint32_t buffer_length);
int ring_buffer_destory(ring_buffer_head_t *p_ring_buffer);
int ring_buffer_read(ring_buffer_head_t *p_ring_buffer, void *p_buffer, uint32_t size);
int ring_buffer_write(ring_buffer_head_t *p_ring_buffer, void *p_buffer,  uint32_t size);
int ring_buffer_get_free_units(ring_buffer_head_t *p_ring_buffer);

#endif /* __IPC_RING_BUFFER_H__ */
