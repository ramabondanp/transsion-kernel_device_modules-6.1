/*
 * (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * Change Logs:
 * Date           Author        Notes
 * 2022-01-21     Zhou Miao     Initialize.
 */

/**
 * @brief   IPCM Ring Buffer Interface
 * @date    2022-01-21
 */

#include "ipc_printk.h"
#include "ipc_mem.h"
#include "ipc_ring_buffer.h"

/* init ring buffer */
int ring_buffer_init(ring_buffer_head_t *p_ring_buffer, uint32_t single_unit_size,
			uint32_t buffer_num)
{
	if (!p_ring_buffer) {
		ipc_err("ring_buffer_init p_ring_buffer is NULL!\n");
		return -1;
	}

	p_ring_buffer->ring_buffer_addr = ipc_malloc(single_unit_size * buffer_num);
	if (!(p_ring_buffer->ring_buffer_addr)) {
		ipc_err("ring_buffer_init malloc failed!\n");
		return -1;
	}

	p_ring_buffer->write_pos = 0;
	p_ring_buffer->read_pos = 0;
	p_ring_buffer->recycle_pos = 0;
	p_ring_buffer->buffer_cur_count = 0;
	p_ring_buffer->single_unit_size = single_unit_size;
	p_ring_buffer->buffer_num = buffer_num;

	ipc_debug("init rb_head %p, single_unit_size %d, buffer_num: %d!\n",
		  p_ring_buffer, single_unit_size, buffer_num);

	return 0;
}

int ring_buffer_destory(ring_buffer_head_t *p_ring_buffer)
{
	ipc_free(p_ring_buffer->ring_buffer_addr);
	ipc_memset(p_ring_buffer, 0, sizeof(ring_buffer_head_t));

	return 0;
}

/*
 * brief read a single ring buffer unit by read_pos
 * param p_ring_buffer : ring_buffer handler
 * param read_pos: index of ring buffer to read.
 */
int ring_buffer_read(ring_buffer_head_t *p_ring_buffer, void *p_buffer, uint32_t size)
{

	if (!p_ring_buffer || !(p_ring_buffer->ring_buffer_addr)) {
		ipc_err("ring_buffer_read p_ring_buffer is NULL!\n");
		goto out;
	}

	if (!p_buffer) {
		ipc_err("ring_buffer_read p_buffer is NULL!\n");
		goto out;
	}

	if (size != p_ring_buffer->single_unit_size) {
		ipc_err("input size %d is not euqal single_unit_size %d\n",
			size, p_ring_buffer->single_unit_size);
		goto out;
	}

	if (p_ring_buffer->read_pos == p_ring_buffer->write_pos) {
		ipc_debug("read head 0x%p, read %d, write %d,"
			  "rb_addr is 0x%p\n", p_ring_buffer,
			  p_ring_buffer->read_pos, p_ring_buffer->write_pos,
			  p_ring_buffer->ring_buffer_addr);
		goto out;
	}

	ipc_memcpy(p_buffer, p_ring_buffer->ring_buffer_addr +
		  p_ring_buffer->read_pos * p_ring_buffer->single_unit_size,
		  p_ring_buffer->single_unit_size, NONE_MODE);

	p_ring_buffer->read_pos++;

	if (p_ring_buffer->read_pos > p_ring_buffer->buffer_num - 1)
		p_ring_buffer->read_pos = 0;

	return 0;

out:
	return -1;
}

/*
 * brief write a single ring buffer unit to ring buffer.
 * param p_ring_buffer : ring_buffer handler
 * param p_buffer: The buffer is used to be write into ring buffer.
 */
int ring_buffer_write(ring_buffer_head_t *p_ring_buffer, void *p_buffer, uint32_t size)
{
	if (!p_ring_buffer || !(p_ring_buffer->ring_buffer_addr)) {
		ipc_err("p_ring_buffer is NULL!\n");
		goto out;
	}

	if (!p_buffer) {
		ipc_err("p_buffer is NULL!\n");
		goto out;
	}

	if (size != p_ring_buffer->single_unit_size) {
		ipc_err("ring_buffer_write input size %d is not euqal single_unit_size %d!\n",
			size, p_ring_buffer->single_unit_size);
		goto out;
	}

	if ((p_ring_buffer->write_pos + 1 == p_ring_buffer->read_pos) ||
	    (p_ring_buffer->write_pos + 1 - p_ring_buffer->buffer_num) ==
	     p_ring_buffer->read_pos) {
		ipc_err("write head 0x%p, read %d, write %d, rb_addr is 0x%p\n", p_ring_buffer,
			p_ring_buffer->read_pos, p_ring_buffer->write_pos,
			p_ring_buffer->ring_buffer_addr);
		goto out;
	}

	ipc_memcpy(p_ring_buffer->ring_buffer_addr + p_ring_buffer->write_pos
		   * p_ring_buffer->single_unit_size, p_buffer,
		   p_ring_buffer->single_unit_size, WRITE_MODE);

	p_ring_buffer->write_pos++;

	if (p_ring_buffer->write_pos > p_ring_buffer->buffer_num - 1)
		p_ring_buffer->write_pos = 0;

	ipc_debug("after write rb_head %p, read %d, write %d, rb_addr is 0x%p\n", p_ring_buffer,
		  p_ring_buffer->read_pos, p_ring_buffer->write_pos,
		  p_ring_buffer->ring_buffer_addr);

	return 0;

out:
	return -1;
}
