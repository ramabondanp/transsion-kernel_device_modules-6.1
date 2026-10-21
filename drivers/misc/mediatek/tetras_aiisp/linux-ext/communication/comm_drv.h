/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */
/**
 * @brief   header of  comm_drv
 * @date    2022-2-28
 */

#ifndef __COMM_DRV_H__
#define __COMM_DRV_H__

#define PNO_MASK                       (0xFF)
#define PNO_RIGHT_SHIFT                (8)
#define USER_BUF_SIZE                  (0x1000)
#define READ_TIMEOUT                   (5000)
#define USER_MSG_RING_BUFFER_SIZE      (200)

#define LOCAL_MESSAGE                  (0)   /* local message */
#define CHIP_ANDROID_MESSAGE           (1)   /* message to remote android AP */

typedef void (*tml_asend_callback)(void *);

#define miscdev_to_commdev(d) container_of(d, struct comm_dev, misc_dev)

struct msg_usr {
	u8       magic;   /* 0xFE msg head magic */
	u8       msg_type;
	u16      event;
	struct target_id    recv_tid;
	struct target_id    send_tid;
	u32      send_serial;
	u8      *msg_data_user;  /* real data point */
	tml_asend_callback asend_callback;
	u32      msg_len;
	u32      crc;
};

struct comm_user_info {
	struct list_head user_list;
	wait_queue_head_t send_queue;  /* define wait queue object */
	wait_queue_head_t recv_queue;
	wait_queue_head_t recv_finish_queue;
	atomic_t recv_count;
	bool recv_finish_flag;
	struct msg_usr *s_msg_head;
	char *msg_data_buf;
	u32 msg_len;
	struct ring_buffer_mng *p_rb_mng;
	struct target_id tid;  /* used for bind process */
	u32 entity_id;
};

struct comm_proc_msg_list {
	struct list_head msg_list;
	struct target_id tid;
	void (*msg_proc_func_kernel)(u16, void *, u32);
};

struct comm_list_mng {
	struct list_head proc_msg_head;
	struct mutex proc_msg_lock;

	struct list_head user_head;
	struct mutex proc_user_lock;
};

struct comm_dev {
	struct miscdevice misc_dev;
	struct comm_list_mng list_mng;

	struct comm_adaptor adaptor;
};
#endif /* __COMM_DRV_H__ */
