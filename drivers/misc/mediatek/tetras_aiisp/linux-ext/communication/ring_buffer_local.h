/*
 * (C) Copyright 2021-2022, Shenzhen Tetras.AI Technology Co., Ltd
 */
/**
 * @brief   header of  ring buffer
 * @date    2022-1-13
 */

#ifndef __RING_BUFFER_H__
#define __RING_BUFFER_H__

/* please don't change the struct order, because of the spi2ahb read or write 4 bytes */
struct ring_buffer_head {
	void *ring_buffer_addr;
	uint16_t buffer_num; /* no change */
	uint16_t max_use_count; /* write */
	uint16_t write_pos; /* write */
	uint16_t buffer_cur_count; /* write */

	uint16_t read_pos; /* read */
	uint16_t recycle_pos; /* not use now */
	uint16_t single_unit_size; /* no change */
};

struct ring_buffer_mng {
	struct ring_buffer_head rb_head;
	struct mutex rb_mutex;
};

/* Tables where the OS object information is stored */
struct ring_buffer_mng *ring_buffer_init(u32 single_unit_size, u32 buffer_num);
int ring_buffer_read(struct ring_buffer_mng *p_ring_buffer,
		     void *p_buffer, u32 size);
int ring_buffer_write(struct ring_buffer_mng *p_ring_buffer,
		      void *p_buffer, u32 size);
int ring_buffer_destory(struct ring_buffer_mng *p_ring_buffer);
#endif /* __RING_BUFFER_H__ */
