/*
 * (C) Copyright 2021-2022, Shenzhen Tetras.AI Technology Co., Ltd
 */
/**
 * @brief   ring buffer
 * @date    2022-1-13
 */

#include <linux/slab.h>
#include <linux/printk.h>
#include <linux/mutex.h>

#include "comm_ret_code.h"
#include "comm_log.h"
#include "ring_buffer_local.h"

/**
 * @brief init ring buffer
 * @param single_unit_size : single unit size
 * @param buffer_num: The counts of buffer units.
 * @return ring buffer handler or NULL;
 */
struct ring_buffer_mng *ring_buffer_init(u32 single_unit_size, u32 buffer_num)
{
	struct ring_buffer_mng *p_ring_buffer = NULL;

	p_ring_buffer = kzalloc(sizeof(struct ring_buffer_mng), GFP_KERNEL);
	if (!p_ring_buffer) {
		comm_err("p_ring_buffer kzalloc is NULL!\n");
		goto out;
	}

	p_ring_buffer->rb_head.ring_buffer_addr =
		kzalloc(single_unit_size * buffer_num, GFP_KERNEL);
	if (!(p_ring_buffer->rb_head.ring_buffer_addr)) {
		comm_err("malloc failed!\n");
		goto malloc_rb_addr_fail;
	}
	p_ring_buffer->rb_head.write_pos = 0;
	p_ring_buffer->rb_head.read_pos = 0;
	p_ring_buffer->rb_head.recycle_pos = 0;
	p_ring_buffer->rb_head.single_unit_size = single_unit_size;
	p_ring_buffer->rb_head.buffer_num = buffer_num;
	mutex_init(&p_ring_buffer->rb_mutex);

	return (void *)p_ring_buffer;

malloc_rb_addr_fail:
	kfree(p_ring_buffer);
out:
	return NULL;
}

/**
 * @brief destory a ring buffer
 *
 * @param p_in : ring_buffer handler
 * @return result
 */
int ring_buffer_destory(struct ring_buffer_mng *p_ring_buffer)
{
	int ret = RET_OK;

	if (!p_ring_buffer || !(p_ring_buffer->rb_head.ring_buffer_addr)) {
		comm_err("ring_buffer_destory p_ring_buffer is NULL!\n");
		ret = RET_ERROR;
		goto out;
	}
	kfree(p_ring_buffer->rb_head.ring_buffer_addr);
	kfree(p_ring_buffer);

out:
	return ret;
}

/**
 * @brief read a single ring buffer unit by read_pos
 * @param p_ring_buffer : ring_buffer handler
 * @param p_buffer: The buffer is used to be read from ring buffer.
 * @return result
 */
int ring_buffer_read(struct ring_buffer_mng *p_ring_buffer,
		     void *p_buffer, u32 size)
{
	mutex_lock(&p_ring_buffer->rb_mutex);
	if (!p_ring_buffer || !p_buffer) {
		comm_err("ring_buffer_read input is NULL!\n");
		goto out;
	}

	if (!(p_ring_buffer->rb_head.ring_buffer_addr)) {
		comm_err("ring_buffer_read ring_buffer_addr is NULL!\n");
		goto out;
	}

	if (size != p_ring_buffer->rb_head.single_unit_size) {
		comm_err("size %d is not euqal single_unit_size %d\n",
			 size, p_ring_buffer->rb_head.single_unit_size);
		goto out;
	}

	if (p_ring_buffer->rb_head.read_pos == p_ring_buffer->rb_head.write_pos) {
		comm_info("err: read head %p, read %d, write %d, "
			  "rb_addr is %p\n", &p_ring_buffer->rb_head,
			  p_ring_buffer->rb_head.read_pos,
			  p_ring_buffer->rb_head.write_pos,
			  p_ring_buffer->rb_head.ring_buffer_addr);
		goto out;
	}
	comm_debug("dbg: read head %p, read %d, write %d, "
		   "rb_addr is %p\n", &p_ring_buffer->rb_head,
		   p_ring_buffer->rb_head.read_pos,
		   p_ring_buffer->rb_head.write_pos,
		   p_ring_buffer->rb_head.ring_buffer_addr);

	memcpy(p_buffer, p_ring_buffer->rb_head.ring_buffer_addr +
	       p_ring_buffer->rb_head.read_pos *
	       p_ring_buffer->rb_head.single_unit_size,
	       p_ring_buffer->rb_head.single_unit_size);

	p_ring_buffer->rb_head.read_pos = (p_ring_buffer->rb_head.read_pos !=
					   p_ring_buffer->rb_head.buffer_num - 1) ?
					  (p_ring_buffer->rb_head.read_pos + 1) : 0;

	mutex_unlock(&p_ring_buffer->rb_mutex);
	return RET_OK;

out:
	mutex_unlock(&p_ring_buffer->rb_mutex);
	return RET_ERROR;
}

/**
 * @brief write a single ring buffer unit to ring buffer.
 * @param p_ring_buffer : ring_buffer handler
 * @param p_buffer: The buffer is used to be write into ring buffer.
 * @return result
 */
int ring_buffer_write(struct ring_buffer_mng *p_ring_buffer,
		      void *p_buffer,
		      u32 size)
{
	mutex_lock(&p_ring_buffer->rb_mutex);
	if (!p_ring_buffer || !p_buffer) {
		comm_err("write: input is NULL!\n");
		goto out;
	}

	if (!(p_ring_buffer->rb_head.ring_buffer_addr)) {
		comm_err("write: ring_buffer_addr is NULL!\n");
		goto out;
	}

	if (!(p_ring_buffer->rb_head.ring_buffer_addr)) {
		comm_err("write: ring_buffer_addr is NULL!\n");
		goto out;
	}

	if (!p_buffer) {
		comm_err("p_buffer is NULL!\n");
		goto out;
	}

	if (size != p_ring_buffer->rb_head.single_unit_size) {
		comm_err("ring_buffer_write input size %d is "
			 "not euqal single_unit_size %d!\n",
			 size, p_ring_buffer->rb_head.single_unit_size);
		goto out;
	}
	if (p_ring_buffer->rb_head.write_pos + 1 ==
	    p_ring_buffer->rb_head.read_pos ||
	    p_ring_buffer->rb_head.write_pos + 1 -
	    p_ring_buffer->rb_head.buffer_num ==
	    p_ring_buffer->rb_head.read_pos) {
		comm_err("err: write head %p, read %d, write %d, "
			 "rb_addr is 0x%p\n", &p_ring_buffer->rb_head,
			 p_ring_buffer->rb_head.read_pos,
			 p_ring_buffer->rb_head.write_pos,
			 p_ring_buffer->rb_head.ring_buffer_addr);
		goto out;
	}
	comm_debug("dbg: write head %p, read %d, write %d, "
		   "rb_addr is %p\n", &p_ring_buffer->rb_head,
		   p_ring_buffer->rb_head.read_pos,
		   p_ring_buffer->rb_head.write_pos,
		   p_ring_buffer->rb_head.ring_buffer_addr);

	memcpy(p_ring_buffer->rb_head.ring_buffer_addr +
	       p_ring_buffer->rb_head.write_pos *
	       p_ring_buffer->rb_head.single_unit_size,
	       p_buffer, p_ring_buffer->rb_head.single_unit_size);

	p_ring_buffer->rb_head.write_pos = (p_ring_buffer->rb_head.write_pos !=
					    p_ring_buffer->rb_head.buffer_num - 1) ?
					   (p_ring_buffer->rb_head.write_pos + 1) : 0;

	comm_debug("write_pos is %d, read is %d\n",
		   p_ring_buffer->rb_head.write_pos,
		   p_ring_buffer->rb_head.read_pos);
	mutex_unlock(&p_ring_buffer->rb_mutex);
	return RET_OK;

out:
	mutex_unlock(&p_ring_buffer->rb_mutex);
	return RET_ERROR;
}
