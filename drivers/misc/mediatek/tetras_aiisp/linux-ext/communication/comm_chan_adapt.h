/*
 * (C) Copyright 2025, Imvision Co., Ltd
 * This file is classified as confidential level C4 within Imvision
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Change Logs:
 * Date         Author          Notes
 * 2022-2-28    zhoumiao        Initialize.
 */

#ifndef __COMM_CHAN_ADAPT_H__
#define __COMM_CHAN_ADAPT_H__

#include "comm_transfer.h"

#define IPC_MESSAGE_HEAD_MAGIC         (0xFE)
#define COMM_CACHE_SZ                  (256 * 1024)
#define COMM_CACHE_CNT                 (6)
#define SEND_MSG_WAITTIME_IN_256KB     (1000)

enum {
	SEND_MSG_SUCCESS      = 1,
	SEND_MSG_PAYLOAD_FAIL = 2,
	SEND_MSG_HEAD_FAIL    = 3,
	SEND_MSG_NOTIFY_FAIL  = 4,
};

struct message_head {
	u8 magic; /* 0xFE msgHead */
	u8 msg_type; /* use to diff user and kernel */
	u16 event;
	struct target_id recv_tid;
	struct target_id send_tid;
	u32 send_channel;
	u32 msg_data_rtos; /* real data point in rtos */
	u32 asend_callback; /* save rtos free function */
	u32 msg_len;
	u32 crc;
};

struct mailbox {
	/*
	 * 1-inderect msg; 2-direct msg; 3-malloc req; 4-malloc ack; 5-free req
	 * 6-rb req 7-rb ack
	 */
	u8 type;
	u8 channel;
	/* 1-msg_head size; 3-malloc size; 4-malloc size; 5-0 */
	u16 size;
	union {
		struct {
			uint32_t malloc_size;
			uint32_t malloc_seq;
		} malloc_req; /* 3 */
		struct {
			uint32_t malloc_ack_addr;
			uint32_t rtos_free_func;
			uint32_t malloc_seq;
		} malloc_ack; /* 4 */
		struct {
			uint32_t free_addr;
			uint32_t rtos_free_func;
		} free_req; /* 5 */
		struct {
			uint32_t wr_ringbuffer_addr;
			uint32_t rd_ringbuffer_addr;
			u16 ipc_ver;
			u16 api_ver;
		} rb_info; /* 6 */
	};
};

struct comm_chan_dynamic_info {
	int chan_status;
	struct ring_buffer_mng wr_rb_mng;
	struct ring_buffer_mng rd_rb_mng;
	struct comm_trans_dev_info dev_info;
	int (*dispatch_func)(struct message_head *, char *);
};

struct comm_thread_info {
	struct task_struct *recv_thread;
};

struct comm_chan_info {
	u8 suspend_acked;
	struct comm_chan_static_info *chan_static_info;
	struct comm_chan_dynamic_info *chan_dynamic_info;
	struct comm_thread_info chan_thread_info;
	struct wait_queue_head suspend_wait;
};

struct comm_cache_item {
	u8 *addr;
	u16 sz;
	u16 used;
};

struct comm_adaptor {
	u8 ignore_version_check;
	u8 handshake_ready;
	u8 power_state;
	struct wait_queue_head ready_wait;
	u8 *cache_addr;
	struct comm_cache_item cache[COMM_CACHE_CNT];
};

struct send_sync_ctrl {
	u32 send_done;
	wait_queue_head_t send_done_wait;
};

int comm_preset_cache(struct comm_adaptor *adaptor, u8 *cache);
int comm_get_cache(struct comm_adaptor *adaptor, u8 **cache, u32 *cache_sz);
int comm_put_cache(u8 *cache);

int comm_adapt_init(int (*reg_recv_proc)(struct message_head *, char *),
		    struct comm_adaptor *adaptor);
int send_msg_adapt(u16 event_id, char *buf, size_t length,
		   struct target_id *target, struct target_id *sender);
int read_msg_data(u16 phy_type,	struct message_head *p_msg_recv,
		  char *msg_data);
void comm_adapt_exit(struct comm_adaptor *adaptor);
int write_data_adapt(u32 write_addr, char *msg_data, u32 buf_size,
		     struct target_id *tid);
int read_data_adapt(u32 read_addr, char *msg_data, u32 buf_size,
		    struct target_id *tid);
int is_send_list_empty(u32 chip_id);
void show_send_list_info(u32 chip_id);
int sending_suspend_request(u32 chip_id);
int sending_suspend_done(u32 chip_id);
int waiting_suspend_ack(u32 chip_id);
#endif /* __COMM_CHAN_ADAPT_H__ */
