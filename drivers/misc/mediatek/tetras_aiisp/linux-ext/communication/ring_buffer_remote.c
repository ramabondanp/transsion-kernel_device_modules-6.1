/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Change Logs:
 * Date         Author          Notes
 * 2022-2-28    zhoumiao        Initialize.
 */
/**
 * @brief   ring buffer
 * @date    2022-2-28
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/printk.h>
#include <linux/mutex.h>

#include "comm_ret_code.h"
#include "comm_log.h"
#include "comm_phy.h"
#include "ring_buffer_remote.h"

/**
 * @brief ring buffer remote init
 * @param rb_remote : ring_buffer handler remote
 * @param p_rb_mng: ring_buffer mng
*/
int rb_remote_set(u32 rb_remote, struct ring_buffer_mng *p_rb_mng)
{
	int ret = RET_OK;
	comm_info("rb init!\n");

	if (NULL == p_rb_mng) {
		comm_err("input rb mng!\n");
		ret = RET_ERROR;
		goto out;
	}

	p_rb_mng->rb_head_remote = rb_remote;
	mutex_init(&p_rb_mng->rb_mutex);

out:
	return ret;
}

/**
 * @brief read a single ring buffer unit by read_pos remote
 * @param p_rb_mng : ring_buffer mng
 * @param p_buffer: The addr where the read data is stored
 * @param size: size of read data
*/
int ring_buffer_read_remote(struct ring_buffer_mng *p_rb_mng, void *p_buffer,
			    u32 size, u32 phy_type)
{
	struct ring_buffer_head_remote *p_ring_buffer = NULL;
	int ret = RET_OK;
	u32 ring_buffer_offset;

	if (!p_buffer || !p_rb_mng) {
		comm_err("input parameter is NULL!\n");
		goto out;
	}

	p_ring_buffer =
		kzalloc(sizeof(struct ring_buffer_head_remote), GFP_KERNEL);

	if (!p_ring_buffer) {
		comm_err("ring_buffer_read p_ring_buffer is NULL!\n");
		goto out;
	}
	mutex_lock(&p_rb_mng->rb_mutex);

	ret = comm_phy_read(p_rb_mng->rb_head_remote, (char *)p_ring_buffer,
			    sizeof(struct ring_buffer_head_remote), phy_type);
	if (0 != ret) {
		comm_err("spi read head fail!\n");
		goto free_out;
	}

	if (size != p_ring_buffer->single_unit_size) {
		comm_err("input %d is not euqal single_unit_size %d\n", size,
			 p_ring_buffer->single_unit_size);
		goto free_out;
	}

	if (p_ring_buffer->read_pos == p_ring_buffer->write_pos) {
		comm_debug("ring buffer has no unit!\n");
		goto free_out;
	}
	ring_buffer_offset =
		p_ring_buffer->rb_addr_remote +
		p_ring_buffer->read_pos * p_ring_buffer->single_unit_size;

	ret = comm_phy_read(ring_buffer_offset, (char *)p_buffer,
			    p_ring_buffer->single_unit_size, phy_type);

	if (0 != ret) {
		comm_err("spi read msg fail!\n");
		goto free_out;
	}
	p_ring_buffer->read_pos++;

	if (p_ring_buffer->read_pos > p_ring_buffer->buffer_num - 1)
		p_ring_buffer->read_pos = 0;

	ring_buffer_offset =
		offsetof(struct ring_buffer_head_remote, read_pos) +
		p_rb_mng->rb_head_remote;

	comm_debug("read: ring_buffer_offset is 0x%x, rb_head_remote is 0x%x,"
		   " read_pos is %d, rb_addr_remote: 0x%x\n",
		   ring_buffer_offset, p_rb_mng->rb_head_remote,
		   p_ring_buffer->read_pos, p_ring_buffer->rb_addr_remote);

	ret = comm_phy_write(ring_buffer_offset, (char *)(&p_ring_buffer->read_pos),
			     WRITE_BYTES, phy_type);

	if (0 != ret) {
		comm_err("spi read msg fail!\n");
		goto free_out;
	}
	mutex_unlock(&p_rb_mng->rb_mutex);
	kfree(p_ring_buffer);
	return RET_OK;

free_out:
	mutex_unlock(&p_rb_mng->rb_mutex);
	kfree(p_ring_buffer);
out:
	return RET_ERROR;
}

/* read all remote ringbuffer items in at most 2 read ops */
int ring_buffer_read_all_remote(struct ring_buffer_mng *p_rb_mng, void **p_buffer,
				u32 size, u32 phy_type, u32 *msg_cnt)
{
	struct ring_buffer_head_remote *p_ring_buffer = NULL;
	int ret = RET_OK;
	u32 ring_buffer_offset;
	u32 msg_head_cnt = 0;
	void *msg_head_buf = NULL;
	u32 tail_sz;
	u32 msg_sz;

	if (!p_buffer || !p_rb_mng || !size || !msg_cnt) {
		comm_err("input parameter invalid %p, %p %d %p!\n", p_rb_mng, p_buffer, size,
			 msg_cnt);
		return RET_ERROR;
	}

	p_ring_buffer = kzalloc(sizeof(struct ring_buffer_head_remote), GFP_KERNEL);

	if (!p_ring_buffer) {
		comm_err("ring_buffer_read p_ring_buffer is NULL!\n");
		return RET_ERROR;
	}
	mutex_lock(&p_rb_mng->rb_mutex);

	ret = comm_phy_read(p_rb_mng->rb_head_remote, (char *)p_ring_buffer,
			    sizeof(struct ring_buffer_head_remote), phy_type);
	if (0 != ret) {
		comm_err("spi read head fail!\n");
		goto free_out;
	}

	if (size != p_ring_buffer->single_unit_size) {
		comm_err("input %d is not euqal single_unit_size %d\n", size,
			 p_ring_buffer->single_unit_size);
		goto free_out;
	}

	if (p_ring_buffer->read_pos == p_ring_buffer->write_pos) {
		comm_debug("ring buffer has no unit!\n");
		goto free_out;
	}

	if (p_ring_buffer->write_pos > p_ring_buffer->read_pos) {
		msg_head_cnt = p_ring_buffer->write_pos - p_ring_buffer->read_pos;
	} else {
		msg_head_cnt = p_ring_buffer->write_pos + p_ring_buffer->buffer_num -
			       p_ring_buffer->read_pos;
	}

	msg_sz = msg_head_cnt * p_ring_buffer->single_unit_size;
	msg_head_buf = kzalloc(msg_sz, GFP_KERNEL);
	if (!msg_head_buf) {
		comm_err("malloc rb head space failed\n");
		goto free_out;
	}

	ring_buffer_offset =
		p_ring_buffer->rb_addr_remote +
		p_ring_buffer->read_pos * p_ring_buffer->single_unit_size;

	if (p_ring_buffer->write_pos > p_ring_buffer->read_pos) {
		ret = comm_phy_read(ring_buffer_offset, (char *)msg_head_buf, msg_sz, phy_type);
		if (0 != ret) {
			comm_err("spi read msg fail!\n");
			goto free_msg_head;
		}
	} else {
		tail_sz = p_ring_buffer->single_unit_size *
			  (p_ring_buffer->buffer_num - p_ring_buffer->read_pos);
		ret = comm_phy_read(ring_buffer_offset, (char *)msg_head_buf, tail_sz, phy_type);
		if (0 != ret) {
			comm_err("spi read msg fail!\n");
			goto free_msg_head;
		}
		if (tail_sz < msg_sz) {
			ret = comm_phy_read(p_ring_buffer->rb_addr_remote,
					    (char *)msg_head_buf + tail_sz,
					    msg_sz - tail_sz,
					    phy_type);
			if (0 != ret) {
				comm_err("spi read msg fail!\n");
				goto free_msg_head;
			}
		}
	}
	p_ring_buffer->read_pos += msg_head_cnt;
	p_ring_buffer->read_pos %= p_ring_buffer->buffer_num;

	ring_buffer_offset =
		offsetof(struct ring_buffer_head_remote, read_pos) +
		p_rb_mng->rb_head_remote;

	comm_debug("read: ring_buffer_offset is 0x%x, rb_head_remote is 0x%x,"
		   " read_pos is %d, rb_addr_remote: 0x%x\n",
		   ring_buffer_offset, p_rb_mng->rb_head_remote,
		   p_ring_buffer->read_pos, p_ring_buffer->rb_addr_remote);

	ret = comm_phy_write(ring_buffer_offset, (char *)(&p_ring_buffer->read_pos),
			     WRITE_BYTES, phy_type);

	if (0 != ret) {
		comm_err("spi read msg fail!\n");
		goto free_msg_head;
	}

	mutex_unlock(&p_rb_mng->rb_mutex);
	kfree(p_ring_buffer);
	*p_buffer = msg_head_buf;
	*msg_cnt = msg_head_cnt;

	return RET_OK;
free_msg_head:
	kfree(msg_head_buf);
free_out:
	mutex_unlock(&p_rb_mng->rb_mutex);
	kfree(p_ring_buffer);
	return RET_ERROR;
}
/**
 * @brief Write a single ring buffer unit to ring buffer remote.
 * @param ring_buffer_remote : rtos ring buffer addr
 * @param p_buffer: The buffer is used to be write into ring buffer.
 * @param size: The length of write bytes.
 */
int ring_buffer_write_remote(struct ring_buffer_mng *p_rb_mng, void *p_buffer,
			     u32 size, u32 phy_type)
{
	struct ring_buffer_head_remote *p_ring_buffer = NULL;
	int ret = RET_OK;
	u32 ring_buffer_offset;

	if (!p_buffer || !p_rb_mng) {
		comm_err("ring_buffer_read p_buffer is NULL!\n");
		goto out;
	}
	p_ring_buffer =
		kzalloc(sizeof(struct ring_buffer_head_remote), GFP_KERNEL);

	if (!p_ring_buffer) {
		comm_err("ring_buffer_read p_ring_buffer is NULL!\n");
		goto out;
	}
	mutex_lock(&p_rb_mng->rb_mutex);

	ret = comm_phy_read(p_rb_mng->rb_head_remote, (char *)p_ring_buffer,
			    sizeof(struct ring_buffer_head_remote), phy_type);

	if (0 != ret) {
		comm_err("spi read head fail!\n");
		goto free_out;
	}

	if (size != p_ring_buffer->single_unit_size) {
		comm_err("ring_buffer_write input size %d is" \
			 " not euqal single_unit_size %d!\n",
			 size, p_ring_buffer->single_unit_size);
		goto free_out;
	}

	if (p_ring_buffer->write_pos + RB_ONE_STEP == p_ring_buffer->read_pos ||
	    p_ring_buffer->write_pos + RB_ONE_STEP - p_ring_buffer->buffer_num ==
	    p_ring_buffer->read_pos) {
		comm_err("ring_buffer is full!\n");
		goto free_out;
	}
	ret = comm_phy_write(p_ring_buffer->rb_addr_remote +
			     p_ring_buffer->write_pos *
			     p_ring_buffer->single_unit_size,
			     p_buffer,
			     size, phy_type);

	if (0 != ret) {
		comm_err("spi write msg fail!\n");
		goto free_out;
	}

	p_ring_buffer->write_pos++;

	if (p_ring_buffer->write_pos >= p_ring_buffer->buffer_num)
		p_ring_buffer->write_pos = 0;

	ring_buffer_offset =
		offsetof(struct ring_buffer_head_remote, write_pos) +
		p_rb_mng->rb_head_remote;

	comm_debug("write: ring_buffer_offset is 0x%x, "
		   "rb_head_remote is 0x%x, write_pos is %d, rb_addr: 0x%x\n",
		   ring_buffer_offset, p_rb_mng->rb_head_remote,
		   p_ring_buffer->write_pos, p_ring_buffer->rb_addr_remote);

	ret = comm_phy_write(ring_buffer_offset,
			     (char *)(&p_ring_buffer->write_pos),
			     WRITE_BYTES, phy_type);
	if (0 != ret) {
		comm_err("spi write msg fail!\n");
		goto free_out;
	}
	comm_debug("write: done!\n");
	mutex_unlock(&p_rb_mng->rb_mutex);
	kfree(p_ring_buffer);
	return RET_OK;

free_out:
	mutex_unlock(&p_rb_mng->rb_mutex);
	kfree(p_ring_buffer);
out:
	return RET_ERROR;
}
