/*
 * (C) Copyright 2021-2022, Shenzhen Tetras.AI Technology Co., Ltd
 */
/**
 * @brief   header of  ring buffer
 * @date    2022-2-28
 */

#ifndef __COMM_TRANSFER_H__
#define __COMM_TRANSFER_H__

struct comm_trans_dev_info {
	u32 trans_type;
	send_dev_t trans_send_dev;
	recv_dev_t trans_recv_dev;
};

int comm_transfer_dev_init(struct comm_trans_dev_info *p_dev_info);
int comm_transfer_dev_send(struct comm_trans_dev_info *p_dev_info, void *buffer,
			   u32 buffer_size);
int comm_transfer_dev_recv(struct comm_trans_dev_info *p_dev_info, void *buffer,
			   u32 buffer_size);
void comm_transfer_dev_exit(struct comm_trans_dev_info *p_dev_info);

#endif /* __COMM_TRANSFER_H__ */
