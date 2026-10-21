/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
/**
 * @brief   header of  ring buffer
 * @date    2022-2-28
 */

#ifndef __RING_BUFFER_H__
#define __RING_BUFFER_H__

#define RB_ONE_STEP 1   /* read and write the step len of ringbuffer */
#define WRITE_BYTES 4
struct ring_buffer_head_remote {
	u32 rb_addr_remote;
	uint16_t buffer_num; /* no change */
	uint16_t max_use_count; /* not use now in remote */
	uint16_t write_pos; /* write */
	uint16_t buffer_cur_count; /* not use now in remote */

	uint16_t read_pos; /* read */
	uint16_t recycle_pos; /* not use now in remote */
	uint16_t single_unit_size; /* no change */
	uint16_t reserve; /* no change */
};

struct ring_buffer_mng {
	u32 rb_head_remote;
	struct mutex rb_mutex;
};

/* Tables where the OS object information is stored */
int rb_remote_set(u32 rb_remote, struct ring_buffer_mng *p_rb_mng);
int ring_buffer_read_remote(struct ring_buffer_mng *p_rb_mng, void *p_buffer,
			    u32 size, u32 phy_type);
int ring_buffer_read_all_remote(struct ring_buffer_mng *p_rb_mng, void **p_buffer,
				u32 size, u32 phy_type, u32 *msg_cnt);
int ring_buffer_write_remote(struct ring_buffer_mng *p_rb_mng, void *p_buffer,
			     u32 size, u32 phy_type);

#endif /* __RING_BUFFER_H__ */
