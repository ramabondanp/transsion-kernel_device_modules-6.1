/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Change Logs:
 * Date           Author         Notes
 * 2023-10-31     guxiao	Add copyright info.
 */

#ifndef __COMM_DRV_IO_H__
#define __COMM_DRV_IO_H__

#ifndef NAME_LEN
#define NAME_LEN 28
#endif

struct target_id {
	u16 pno; /* task registration number in task_reg_table, */
	u16 chip_id; /* 0 self; 1, 2, 3... other */
	u32 pid;     /* identifies a user-mode thread */
	u8  name[NAME_LEN];
};

enum CHIP_ID {
	CHIP_LOCAL = 0,
	CHIP_RTOS,
	CHIP_AP,
};

typedef void (*free_func_t)(const void *);
typedef void (*process_msg_t)(u16 event_id, void *data_buf, u32 data_len);

int send_msg_for_kernel_thread(u16 event_id, char *buf, size_t length,
			       struct target_id *sender, struct target_id *recver);
int register_process_msg_callback(struct target_id *tid, process_msg_t cb);
void unregister_process_msg_callback(struct target_id *tid);
int write_data_for_kernel_thread(u32 write_addr, char *buf, size_t length,
				 struct target_id *tid);
int read_data_for_kernel_thread(u32 write_addr, char *buf, size_t length,
				struct target_id *tid);
#endif /* __COMM_DRV_IO_H__ */
