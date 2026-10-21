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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/printk.h>
#include <linux/mutex.h>
#include <linux/kthread.h>

#include "comm_drv_io.h"
#include "common.h"
#include "comm_ret_code.h"
#include "comm_log.h"
#include "comm_transfer.h"
#include "comm_phy.h"
#include "ring_buffer_remote.h"
#include "comm_route.h"
#include "comm_chan_adapt.h"
#include "tis_plat_ver_def.h"
#include "ipc_common.h"
#include "ai_isp_pmctrl.h"

struct send_list {
	struct list_head msg_list;
	u32 seq;
	struct message_head msg_head;
	char *msg_data_linux;
	u32 msg_len;
	struct send_sync_ctrl sync;
};

static struct comm_adaptor *g_adaptor;
static struct comm_chan_info *s_chan_info;
static int malloc_seq;
static struct mutex malloc_seq_mutex;

static LIST_HEAD(msg_list_head);
static DEFINE_RAW_SPINLOCK(msg_lock);

static void show_buffer(u8 *buffer, u32 len);
static void add_to_send_list(struct send_list *send_msg_node);
static void del_from_send_list(struct send_list *send_msg_node);
static struct send_list *find_msg_by_seq(u32 seq);
static int send_malloc_req(struct comm_chan_info *p_chan_info,
			   size_t length, u32 seq_no);
static void dispatch_recv_init(struct comm_chan_dynamic_info *chan_dyn_info);
static int send_no_data_msg(struct comm_chan_info *p_chan_info,
			    u16 event_id, size_t length, struct target_id *target,
			    struct target_id *sender);
static int get_chan_no_by_tid(struct target_id *tid, u16 *channel_no);

struct comm_adaptor *__get_adaptor(void)
{
	return g_adaptor;
}

int comm_preset_cache(struct comm_adaptor *adaptor, u8 *cache)
{
	u8 *p_cache;
	int i;
	int ret = 0;

	p_cache = cache;
	adaptor->cache_addr = cache;
	for (i = 0; i < COMM_CACHE_CNT; i++) {
		adaptor->cache[i].addr = p_cache;
		adaptor->cache[i].sz = (u16)COMM_CACHE_SZ;
		adaptor->cache[i].used  = 0;
		p_cache += COMM_CACHE_SZ;
		comm_debug("i %d addr 0x%p, sz %d, ptr 0x%p\n", i, adaptor->cache[i].addr,
			 adaptor->cache[i].sz, p_cache);
	}
	return ret;
}

static void comm_reset_cache(struct comm_adaptor *adaptor)
{
	int i;

	for (i = 0; i < COMM_CACHE_CNT; i++)
		adaptor->cache[i].used = 0;
}

static void comm_release_cache(struct comm_adaptor *adaptor)
{
	int i;

	if (adaptor->cache_addr == NULL) {
		comm_err("cache addr 0x%p invalid\n", adaptor->cache_addr);
		return;
	}
	for (i = 0; i < COMM_CACHE_CNT; i++) {
		if (adaptor->cache[i].addr) {
			adaptor->cache[i].addr = NULL;
			adaptor->cache[i].sz = 0;
			adaptor->cache[i].used  = -1;
		}
	}
	kfree(adaptor->cache_addr);
	adaptor->cache_addr = NULL;
}

int comm_get_cache(struct comm_adaptor *adaptor, u8 **cache, u32 *cache_sz)
{
	int i;

	mutex_lock(&malloc_seq_mutex);
	for (i = 0; i < COMM_CACHE_CNT; i++) {
		if (!adaptor->cache[i].used) {
			*cache = adaptor->cache[i].addr;
			*cache_sz = adaptor->cache[i].sz;
			adaptor->cache[i].used = 1;
			mutex_unlock(&malloc_seq_mutex);
			comm_debug("get cache[%d] = 0x%p\n", i, adaptor->cache[i].addr);
			return 0;
		}
	}
	mutex_unlock(&malloc_seq_mutex);
	comm_err("no more cache available\n");

	return -ENOMEM;
}

int comm_put_cache(u8 *cache)
{
	int i;
	struct comm_adaptor *adaptor = __get_adaptor();

	mutex_lock(&malloc_seq_mutex);
	for (i = 0; i < COMM_CACHE_CNT; i++) {
		if (adaptor->cache[i].addr == cache) {
			if (!adaptor->cache[i].used) {
				comm_err("cache 0x%p already freed\n", cache);
			}
			adaptor->cache[i].used = 0;
			mutex_unlock(&malloc_seq_mutex);
			return 0;
		}
	}
	mutex_unlock(&malloc_seq_mutex);

	comm_err("cache 0x%p not valid\n", cache);
	return -ENOENT;
}

int write_data_adapt(u32 write_addr, char *msg_data, u32 buf_size,
		     struct target_id *tid)
{
	int ret = RET_ERROR;
	u16 channel_no = 0;
	struct comm_chan_info *p_chan_info;
	u16 phy_type;

	ret = get_chan_no_by_tid(tid, &channel_no);
	if (ret != RET_OK) {
		comm_err("get chan info fail!\n");
		goto out;
	}
	p_chan_info = s_chan_info;
	phy_type = p_chan_info[channel_no].chan_static_info->phy_type;
	ret = comm_phy_write(write_addr, msg_data, buf_size, phy_type);

out:
	return ret;
}

int read_data_adapt(u32 read_addr, char *msg_data, u32 buf_size,
		    struct target_id *tid)
{
	int ret = RET_ERROR;
	u16 channel_no = 0;
	struct comm_chan_info *p_chan_info;
	u16 phy_type;

	ret = get_chan_no_by_tid(tid, &channel_no);
	if (ret != RET_OK) {
		comm_err("get chan info fail!\n");
		goto out;
	}
	p_chan_info = s_chan_info;
	phy_type = p_chan_info[channel_no].chan_static_info->phy_type;
	ret = comm_phy_read(read_addr, msg_data, buf_size, phy_type);

out:
	return ret;
}

int read_msg_data(u16 phy_type,
		  struct message_head *p_msg_recv, char *msg_data)
{
	int ret = RET_OK;

	if (NULL == p_msg_recv || NULL == msg_data) {
		comm_err("input is NULL!\n");
		return RET_ERROR;
	}

	ret = comm_phy_read(p_msg_recv->msg_data_rtos, (char *)msg_data,
			    p_msg_recv->msg_len, phy_type);
	comm_debug("len is %d\n", p_msg_recv->msg_len);

	/* show_buffer(msg_data, p_msg_recv->msg_len + BYTEALIGN_WIDTH); */
	return ret;
}

static int write_msg_data(u16 phy_type,
			  struct mailbox *mailbox_msg, struct send_list *p_send_list)
{
	int ret = RET_OK;


	if (NULL == mailbox_msg || NULL == p_send_list) {
		comm_err("input is NULL!\n");
		ret = RET_ERROR;
		goto out;
	}

	ret = comm_phy_write(mailbox_msg->malloc_ack.malloc_ack_addr,
			     (char *)p_send_list->msg_data_linux,
			     p_send_list->msg_len, phy_type);
	comm_debug("len is %d\n", p_send_list->msg_len);

out:
	return ret;
}

/* Will be extended to other communication type by tid. */
static int write_msg_head_to_rb(struct comm_chan_info *p_chan_info,
				struct message_head *msg_head)
{
	int ret = RET_OK;

	if (NULL == msg_head || (NULL == p_chan_info)) {
		comm_err("input NULL!\n");
		return RET_ERROR;
	}
	ret = ring_buffer_write_remote(&p_chan_info->chan_dynamic_info->wr_rb_mng,
				       (void *)msg_head, sizeof(struct message_head),
				       p_chan_info->chan_static_info->phy_type);
	return ret;
}

static int read_all_msg_head_from_rb(struct comm_chan_info *p_chan_info,
				     struct message_head **msg_head, u32 *msg_cnt)
{
	int ret = RET_OK;

	if (NULL == msg_head || (NULL == p_chan_info)) {
		return RET_ERROR;
	}

	ret = ring_buffer_read_all_remote(&p_chan_info->chan_dynamic_info->rd_rb_mng,
					  (void **)msg_head, sizeof(struct message_head),
					  p_chan_info->chan_static_info->phy_type,
					  msg_cnt);
	return ret;
}

static void set_comm_chan_status(struct comm_chan_info *p_chan_info,
				 int chan_status)
{
	p_chan_info->chan_dynamic_info->chan_status = chan_status;
}

static int get_comm_chan_status(struct comm_chan_info *p_chan_info)
{
	return p_chan_info->chan_dynamic_info->chan_status;
}

static inline void __msg_head_fill(struct message_head *msg_head, u16 event_id, size_t length,
				   struct target_id *target, struct target_id *sender)
{
	msg_head->magic = IPC_MESSAGE_HEAD_MAGIC;
	msg_head->event = event_id;
	memcpy(&msg_head->recv_tid, target, sizeof(*target));
	memcpy(&msg_head->send_tid, sender, sizeof(*sender));
	msg_head->msg_len = length;
	comm_debug("recv tid: pid %d chip_id %d pno %d name [%s]\n", target->pid,
		   target->chip_id, target->pno, target->name);
	comm_debug("sender tid: pid %d chip_id %d pno %d name [%s]\n", sender->pid,
		   sender->chip_id, sender->pno, sender->name);
}

static struct send_list *make_send_msg_node(struct comm_chan_info *p_chan_info,
					    u16 event_id,
					    char *buf,
					    size_t length,
					    struct target_id *target,
					    struct target_id *sender)
{
	struct send_list *send_msg_node;

	send_msg_node = kzalloc(sizeof(struct send_list), GFP_KERNEL);
	if (NULL == send_msg_node) {
		comm_err("alloc send list struct fail!\n");
		goto out;
	}

	INIT_LIST_HEAD(&send_msg_node->msg_list);
	init_waitqueue_head(&send_msg_node->sync.send_done_wait);
	send_msg_node->sync.send_done = 0;

	mutex_lock(&malloc_seq_mutex);
	send_msg_node->seq = malloc_seq++;
	if (malloc_seq > 0xffffff)
		malloc_seq = 0;
	mutex_unlock(&malloc_seq_mutex);

	send_msg_node->msg_data_linux = buf;
	send_msg_node->msg_len = length;

	__msg_head_fill(&send_msg_node->msg_head, event_id, length, target, sender);

	if (length) {
		send_msg_node->msg_head.crc = do_checksum(buf, length);
		comm_debug("send %zu bytes crc : 0x%x\n", length, send_msg_node->msg_head.crc);
	}

	comm_debug("send pid: %d\n", current->tgid);
	return send_msg_node;

out:
	return NULL;
}

int get_chan_no_by_tid(struct target_id *tid, u16 *channel_no)
{
	int i = 0;
	struct comm_chan_info *p_chan_info;

	if (NULL == tid || NULL == channel_no) {
		comm_err("input NULL\n");
		return RET_ERROR;
	}

	comm_debug("tid.chipid %d in route table\n", tid->chip_id);
	p_chan_info = s_chan_info;
	for (i = 0; i < COMM_CHANNEL_NUM; i++) {
		if (NULL == p_chan_info[i].chan_static_info)
			continue;
		if (tid->chip_id == p_chan_info[i].chan_static_info->dst_chip_id) {
			*channel_no = p_chan_info[i].chan_static_info->channel_no;
			break;
		}
	}
	if (i == COMM_CHANNEL_NUM) {
		comm_err("cann't find tid.chipid %d in route table\n", tid->chip_id);
		return RET_ERROR;
	}
	return RET_OK;
}

int send_msg_adapt(u16 event_id, char *buf, size_t length,
		   struct target_id *target, struct target_id *sender)
{
	int malloc_len = 0;
	int ret = RET_ERROR;
	struct send_list *send_msg_node = NULL;
	struct comm_chan_static_info *p_static_info;
	struct comm_chan_info *p_chan_info;
	u16 channel_no = 0;
	u32 timeout_ms = SEND_MSG_WAITTIME_IN_256KB;

	if (NULL == target || NULL == sender) {
		comm_err("input null!\n");
		ret = -EINVAL;
		goto free_out;
	}
	ret = get_chan_no_by_tid(target, &channel_no);
	if (ret != RET_OK) {
		comm_err("get chan info fail!\n");
		ret = -ENOENT;
		goto free_out;
	}

	p_chan_info = s_chan_info;
	p_static_info = p_chan_info[channel_no].chan_static_info;
	if (COMM_OK != p_chan_info[channel_no].chan_dynamic_info->chan_status) {
		comm_err("channel: %d is not comm ok!\n", channel_no);
		ret = -EPERM;
		goto free_out;
	}

	if (IPC_TRANS_TYPE == p_static_info->transfer_type) {
		if (0 == length) {
			ret = send_no_data_msg(p_chan_info, event_id, length, target, sender);
			return ret;
		}
		send_msg_node = make_send_msg_node(p_chan_info, event_id,
						   buf, length, target, sender);
		if (NULL == send_msg_node) {
			comm_err("make send node fail!\n");
			ret = -ENOMEM;
			goto free_out;
		}

		add_to_send_list(send_msg_node);
		comm_debug("send_msg_node->seq is %d, send_msg_node is 0x%p!\n",
			   send_msg_node->seq, send_msg_node);

		malloc_len = length % BYTEALIGN_WIDTH ?
			     (length + BYTEALIGN_WIDTH) : length;
		ret = send_malloc_req(p_chan_info, malloc_len,
				      send_msg_node->seq);
		if (0 != ret) {
			comm_err("Send malloc req fail!\n");
			goto out;
		}

		comm_debug("send msg wait %p until %d ms\n", &send_msg_node->sync, timeout_ms);
		ret = wait_event_timeout(send_msg_node->sync.send_done_wait,
					 (send_msg_node->sync.send_done != 0),
					 timeout_ms);
		if (ret == 0) {
			comm_err("send msg timeout!\n");
			ret = -ETIMEDOUT;
			goto out;
		}
		if (send_msg_node->sync.send_done != SEND_MSG_SUCCESS) {
			ret = -EAGAIN;
			goto out;
		}
	} else {
		/* TODO: other trans channel_no process */
	}
	ret = RET_OK;
out:
	/* comm cache put in del_from_send_list */
	del_from_send_list(send_msg_node);
	return ret;

free_out:
	if (NULL != buf)
		comm_put_cache(buf);
	return ret;
}

static void ring_info_process(struct comm_chan_info *p_chan_info,
			      struct mailbox *mailbox_msg, u32 is_comm_ok)
{
	struct mailbox mailbox_tmp;

	rb_remote_set(mailbox_msg->rb_info.rd_ringbuffer_addr,
		      &p_chan_info->chan_dynamic_info->wr_rb_mng);
	rb_remote_set(mailbox_msg->rb_info.wr_ringbuffer_addr,
		      &p_chan_info->chan_dynamic_info->rd_rb_mng);

	comm_debug("wr_rb_addr is 0x%x, read is 0x%x\n",
		   p_chan_info->chan_dynamic_info->wr_rb_mng.rb_head_remote,
		   p_chan_info->chan_dynamic_info->rd_rb_mng.rb_head_remote);


	memset((char *)&mailbox_tmp, 0, sizeof(struct mailbox));
	if (COMM_OK != is_comm_ok)
		mailbox_tmp.type = MAILBOX_TYPE_IPC_VER_MISMATCH;
	else {
		set_comm_chan_status(p_chan_info, COMM_OK);
		mailbox_tmp.type = MAILBOX_TYPE_RING_ACK;
	}

	mailbox_tmp.size = sizeof(struct mailbox);

	comm_transfer_dev_send(&p_chan_info->chan_dynamic_info->dev_info,
			       (void *)&mailbox_tmp, sizeof(struct mailbox));
}

static int send_no_data_msg(struct comm_chan_info *p_chan_info,
			    u16 event_id, size_t length, struct target_id *target,
			    struct target_id *sender)
{
	struct message_head *msg_head;
	struct mailbox mailbox_msg;
	struct comm_adaptor *adaptor = __get_adaptor();
	int ret = RET_OK;

	/* Fix spi2axi cannot write to memory on stack */
	msg_head = kzalloc(sizeof(struct message_head), GFP_KERNEL);
	if (NULL == msg_head) {
		comm_err("alloc is NULL!\n");
		return RET_ERROR;
	}

	__msg_head_fill(msg_head, event_id, length, target, sender);

	ret = write_msg_head_to_rb(p_chan_info, msg_head);
	if (RET_OK != ret)
		goto out;

	memset(&mailbox_msg, 0, sizeof(struct mailbox));
	mailbox_msg.type = MAILBOX_TYPE_ONLY_EVENT;
	comm_debug("only event message, channel %d!\n",
		   p_chan_info->chan_static_info->channel_no);

	if (adaptor->power_state == COMM_DEV_POWER_SUSPENDED) {
		comm_err("onlydata evt %d from[%d/%d/%d/] to [%d/%d/%d] send in suspended state\n",
			 event_id, sender->chip_id, sender->pid, sender->pno,
			 target->chip_id, target->pid, target->pno);
	}
	ret = comm_transfer_dev_send(&p_chan_info->chan_dynamic_info->dev_info,
				     (void *)&mailbox_msg, sizeof(struct mailbox));
	if (ret != RET_OK) {
		comm_err("send nodata mbox failed %d\n", ret);
	}

out:
	kfree(msg_head);
	return ret;
}

static int send_malloc_req(struct comm_chan_info *p_chan_info,
			   size_t length, u32 seq_no)
{
	int ret = RET_OK;
	struct mailbox mailbox_msg;
	struct comm_adaptor *adaptor = __get_adaptor();

	/* send malloc mailbox to rtos */
	memset(&mailbox_msg, 0, sizeof(struct mailbox));
	mailbox_msg.type = MAILBOX_TYPE_MALLOC_REQ;
	mailbox_msg.malloc_req.malloc_size = length;
	mailbox_msg.malloc_req.malloc_seq = seq_no;
	comm_debug("malloc req seq_no is %u, length is %zu\n", seq_no, length);

	if (adaptor->power_state == COMM_DEV_POWER_SUSPENDED) {
		comm_err("try send malloc_req in suspended state\n");
	}
	ret = comm_transfer_dev_send(&p_chan_info->chan_dynamic_info->dev_info,
				     (void *)&mailbox_msg, sizeof(struct mailbox));
	if (ret != RET_OK) {
		comm_err("send alloc_req length %zu, seq %u mbox failed %d\n", length, seq_no, ret);
	}

	return ret;
}

static int send_free_to_rtos(struct comm_chan_info *p_chan_info,
			     u32 free_addr_rtos, u32 callback)
{
	int ret = RET_OK;
	struct mailbox mailbox_msg;
	struct comm_adaptor *adaptor = __get_adaptor();

	/* send free mailbox msg to rtos */
	memset(&mailbox_msg, 0, sizeof(struct mailbox));
	mailbox_msg.type = MAILBOX_TYPE_FREE;
	mailbox_msg.free_req.free_addr = free_addr_rtos;
	mailbox_msg.free_req.rtos_free_func = callback;
	comm_debug("free_addr_rtos 0x%x callback 0x%x\n", free_addr_rtos, callback);
	if (adaptor->power_state == COMM_DEV_POWER_SUSPENDED) {
		comm_err("try send free in suspended state\n");
	}
	ret = comm_transfer_dev_send(&p_chan_info->chan_dynamic_info->dev_info,
				     (void *)&mailbox_msg, sizeof(struct mailbox));
	if (ret != RET_OK) {
		comm_err("send free mbox failed %d\n", ret);
	}

	return ret;
}

static inline void msg_send_done_notify(struct send_sync_ctrl *sync, u32 state)
{
	comm_debug("send complete %p\n", sync);
	sync->send_done = state;
	wake_up(&sync->send_done_wait);
}

static int malloc_ack_process(struct comm_chan_info *p_chan_info,
			      struct mailbox *mailbox_msg)
{
	struct send_list *p_send_list = NULL;
	struct mailbox mailbox_tmp;
	int ret = RET_ERROR;
	struct comm_adaptor *adaptor = __get_adaptor();
	u32 send_status = SEND_MSG_SUCCESS;
	int i = 0;

	p_send_list = find_msg_by_seq(mailbox_msg->malloc_ack.malloc_seq);
	comm_debug("first %x, sec %x,  seq is %d\n",
		   mailbox_msg->malloc_ack.malloc_ack_addr,
		   mailbox_msg->malloc_ack.rtos_free_func,
		   mailbox_msg->malloc_ack.malloc_seq);
	if (NULL == p_send_list) {
		/* msg response cost too much time, send_node already cleared by ap */
		comm_err("find fail, p_send_list is 0x%p, seq is %d\n",
			 p_send_list, mailbox_msg->malloc_ack.malloc_seq);
		send_free_to_rtos(p_chan_info,
				  mailbox_msg->malloc_ack.malloc_ack_addr,
				  mailbox_msg->malloc_ack.rtos_free_func);
		return -ENOENT;
	}

	ret = write_msg_data(p_chan_info->chan_static_info->phy_type,
			     mailbox_msg, p_send_list);
	if (0 != ret) {
		comm_err("write fail!\n");
		send_status = SEND_MSG_PAYLOAD_FAIL;
		goto send_free_out;
	}

	p_send_list->msg_head.msg_data_rtos = mailbox_msg->malloc_ack.malloc_ack_addr;
	p_send_list->msg_head.asend_callback = mailbox_msg->malloc_ack.rtos_free_func;

	/* write msg_head to share ring buffer 3 times */
	for (i = 0; i < WRITE_RB_TIMES; i++) {
		ret = write_msg_head_to_rb(p_chan_info, &p_send_list->msg_head);
		if (RET_OK == ret)
			break;
	}
	if (RET_OK != ret) {
		comm_err("write rtos buffer fail!\n");
		send_status = SEND_MSG_HEAD_FAIL;
		goto send_free_out;
	}
	/* build mailbox */
	memset((char *)&mailbox_tmp, 0, sizeof(struct mailbox));
	mailbox_tmp.type = MAILBOX_TYPE_INDIRECT_DATA;
	mailbox_tmp.size = sizeof(struct mailbox);

	if (adaptor->power_state == COMM_DEV_POWER_SUSPENDED) {
		comm_err("try send indirectdata in suspended state\n");
	}
	ret = comm_transfer_dev_send(&p_chan_info->chan_dynamic_info->dev_info,
				     (void *)&mailbox_tmp, sizeof(struct mailbox));
	if (RET_OK != ret) {
		comm_err("send malloc_ack mbox failed %d\n", ret);
		send_status = SEND_MSG_NOTIFY_FAIL;
		goto send_free_out;
	}

	msg_send_done_notify(&p_send_list->sync, send_status);

	return ret;

send_free_out:
	send_free_to_rtos(p_chan_info,
			  mailbox_msg->malloc_ack.malloc_ack_addr,
			  mailbox_msg->malloc_ack.rtos_free_func);

	return ret;
}

static inline struct comm_chan_info *__get_chan_by_chipid(u32 chip_id)
{
	return s_chan_info;
}

int sending_suspend_request(u32 target_chip)
{
	struct comm_chan_info *p_chan_info;
	struct mailbox mb = {0};
	int ret = RET_ERROR;

	comm_info("sending suspend request\n");
	p_chan_info = __get_chan_by_chipid(target_chip);
	p_chan_info->suspend_acked = 0;
	mb.type = MAILBOX_TYPE_SUSPEND_REQ;
	mb.size = sizeof(struct mailbox);

	ret = comm_transfer_dev_send(&p_chan_info->chan_dynamic_info->dev_info,
				     (void *)&mb, sizeof(struct mailbox));
	if (RET_OK != ret) {
		comm_err("send suspend req mbox fialed!\n");
	}
	return ret;
}

int sending_suspend_done(u32 target_chip)
{
	struct mailbox mb = {0};
	struct comm_chan_info *p_chan_info;
	int ret = RET_ERROR;

	comm_info("sending suspend done\n");

	p_chan_info = __get_chan_by_chipid(target_chip);
	mb.type = MAILBOX_TYPE_SUSPEND_DONE;
	mb.size = sizeof(struct mailbox);

	ret = comm_transfer_dev_send(&p_chan_info->chan_dynamic_info->dev_info,
				     (void *)&mb, sizeof(struct mailbox));
	if (RET_OK != ret) {
		comm_err("send suspend done mbox fialed!\n");
	}
	comm_info("suspended sent\n");
	return ret;
}

int waiting_suspend_ack(u32 target_chip)
{
	uint32_t timeout_ms = 1000;
	struct comm_chan_info *p_chan_info;
	int ret = RET_ERROR;

	p_chan_info = __get_chan_by_chipid(target_chip);

	ret = wait_event_timeout(p_chan_info->suspend_wait,
				 (p_chan_info->suspend_acked == 1), timeout_ms);
	if (ret == 0) {
		comm_debug("wait suspend acked timeout!\n");
		return -ETIME;
	}
	return 0;
}

#define ERR_PROCESS(p_msg_head, errcode)    do {                          \
    send_free_to_rtos(p_chan_info, (p_msg_head)->msg_data_rtos, (p_msg_head)->asend_callback);\
    (p_msg_head)++;                                       \
    ret |= (errcode);                                     \
} while (0)
static int ipc_msg_forward(struct comm_chan_info *p_chan_info,
			   struct mailbox *mailbox_msg)
{
	struct message_head *p_msg_recv = NULL;
	struct message_head *p_msg_cur;
	u32 msg_cnt = 0;
	int ret = RET_OK;
	void *msg_data_buf = NULL;
	uint32_t checksum = 0;
	int i;

	ret = read_all_msg_head_from_rb(p_chan_info, &p_msg_recv, &msg_cnt);
	if (RET_OK != ret) {
		comm_debug("can't read ring buffer, ret:%d!\n", ret);
		return ret;
	}

	if (msg_cnt > 1) {
		comm_debug("%d msgs in one readout, addr is %p\n", msg_cnt, p_msg_recv);
	}

	p_msg_cur = p_msg_recv;
	for (i = 0; i < msg_cnt; i++) {
		comm_debug("rtos msg:0x%x, event:%d, len:%d, from[%d/%d/%d] to [%d/%d/%d]\n",
			   p_msg_cur->msg_data_rtos, p_msg_cur->event, p_msg_cur->msg_len,
			   p_msg_cur->send_tid.chip_id, p_msg_cur->send_tid.pid,
			   p_msg_cur->send_tid.pno, p_msg_cur->recv_tid.chip_id,
			   p_msg_cur->recv_tid.pid, p_msg_cur->recv_tid.pno);

		if (NULL == p_chan_info->chan_dynamic_info->dispatch_func) {
			comm_err("dispatch is NULL!\n");
			ERR_PROCESS(p_msg_cur, RET_ERROR);
			continue;
		}

		if (p_msg_cur->msg_len != 0) {
			msg_data_buf = kzalloc(p_msg_cur->msg_len + BYTEALIGN_WIDTH,
					       GFP_KERNEL);
			if (NULL == msg_data_buf) {
				comm_err("alloc send list struct fail!\n");
				ERR_PROCESS(p_msg_cur, RET_ERROR);
				continue;
			}

			/* copy data from rtos */
			ret = read_msg_data(p_chan_info->chan_static_info->phy_type,
					    p_msg_cur, msg_data_buf);
			if (RET_OK != ret) {
				comm_err("read fail\n");
				kfree(msg_data_buf);
				ERR_PROCESS(p_msg_cur, RET_ERROR);
				continue;
			}
		} else {
			msg_data_buf = NULL;
		}

		if (p_msg_cur->msg_len) {
			checksum = do_checksum(msg_data_buf, p_msg_cur->msg_len);
			if (p_msg_cur->crc != checksum) {
				comm_err("recv  %d bytes crc : 0x%x, calced crc 0x%x crc err!\n",
					 p_msg_cur->msg_len, p_msg_cur->crc, checksum);
				comm_err("msg: evt %d, target: pid %d, pno %d\n",
					 p_msg_cur->event, p_msg_cur->recv_tid.pid,
					 p_msg_cur->recv_tid.pno);
				kfree(msg_data_buf);
				ERR_PROCESS(p_msg_cur, RET_ERROR);
				continue;
			}
		}

		send_free_to_rtos(p_chan_info, p_msg_cur->msg_data_rtos,
				  p_msg_cur->asend_callback);

		ret = p_chan_info->chan_dynamic_info->dispatch_func(p_msg_cur, msg_data_buf);
		if (RET_OK != ret) {
			comm_err("dispatch fail!\n");
			p_msg_cur++;
			ret |= RET_ERROR;
			continue;
		}
		p_msg_cur++;
	}

	kfree(p_msg_recv);
	return ret;
}

/**
 * @brief show_buffer() - display the memory buffer.
 *
 * @param buffer: buffer point.
 * @param len: buffer length.
 */
static void show_buffer(u8 *buffer, u32 len)
{
	int i = 0;
	/* payload don't show */
	if (len > sizeof(struct message_head))
		return;

	comm_debug("buffer is 0x%p\n", buffer);
	for (i = 0; i < len; i++) {
		comm_debug("%02x \n", buffer[i]);
	}
}

static void comm_soft_init(struct comm_adaptor *adaptor)
{
	adaptor->power_state = COMM_DEV_POWER_WORK;
	wake_up_interruptible(&adaptor->ready_wait);
	comm_reset_cache(adaptor);

	return;
}
/**
 * @brief recv_process() - recv process.
 *
 * @param p_in:  mailbox message.
 * @return process result.
 */
static int recv_process(struct comm_chan_info *p_chan_info)
{
	int ret = RET_OK;
	u32 is_comm_ok;
	struct mailbox *mailbox_msg = NULL;
	struct comm_adaptor *adaptor = __get_adaptor();

	if (NULL == p_chan_info) {
		comm_err("input is NULL!\n");
		return RET_ERROR;
	}

	mailbox_msg = kzalloc(sizeof(struct mailbox), GFP_KERNEL);
	if (NULL == mailbox_msg) {
		comm_err("alloc is NULL!\n");
		return RET_ERROR;
	}
	ret = comm_transfer_dev_recv(&p_chan_info->chan_dynamic_info->dev_info,
				     (void *)mailbox_msg, sizeof(struct mailbox));
	if (RET_OK != ret) {
		comm_err("recv msg fail!\n");
		goto free_out;
	}
	/* show_buffer((u8 *)mailbox_msg, 16); */
	comm_debug("type is 0x%x!\n", mailbox_msg->type);

	if (mailbox_msg->type == MAILBOX_TYPE_RING_INFO) {
		comm_info("recv task ring info!\n");
		is_comm_ok = COMM_OK;
		if (adaptor->ignore_version_check == 0) {
			if (mailbox_msg->rb_info.ipc_ver != RTOS_AP_IPC_VERSION) {
				comm_err("peer ipc_ver:%d, local ipc_ver: %d!\n",
					 mailbox_msg->rb_info.ipc_ver, RTOS_AP_IPC_VERSION);
				is_comm_ok = COMM_ERROR;
			}

			if (mailbox_msg->rb_info.api_ver != RTOS_AP_API_VERSION) {
				comm_err("peer api_ver:%d, local api_ver: %d!\n",
					 mailbox_msg->rb_info.api_ver, RTOS_AP_API_VERSION);
				is_comm_ok = COMM_ERROR;
			}
		}

		comm_info("ipc version check is %s\n", (is_comm_ok == COMM_OK) ? "OK" : "FAIL");

		ring_info_process(p_chan_info, mailbox_msg, is_comm_ok);
		ret = RET_OK;
		adaptor->handshake_ready = is_comm_ok;
		comm_soft_init(adaptor);

		goto free_out;
	}

	if (mailbox_msg->type == MAILBOX_TYPE_RTOS_WAKEUP) {
		adaptor->power_state = COMM_DEV_POWER_WORK;
		ai_isp_sw_wakeup_report();
		comm_info("pmctrl wakeup ready!\n");
		goto free_out;
	}

	if (mailbox_msg->type == MAILBOX_TYPE_SUSPEND_ACK) {
		comm_info("recved remote suspend ack wakeup %p!\n", &p_chan_info->suspend_wait);
		p_chan_info->suspend_acked = 1;
		wake_up(&p_chan_info->suspend_wait);
		goto free_out;
	}

	if (COMM_OK != get_comm_chan_status(p_chan_info)) {
		comm_err("comm not init ok!\n");
		goto free_out;
	}

	switch (mailbox_msg->type) {
	case MAILBOX_TYPE_INDIRECT_DATA:
		comm_debug("recv indirect data!\n");
		ipc_msg_forward(p_chan_info, mailbox_msg);
		break;

	case MAILBOX_TYPE_MALLOC_ACK:
		comm_debug("recv malloc ack!\n");
		malloc_ack_process(p_chan_info, mailbox_msg);
		break;

	case MAILBOX_TYPE_ONLY_EVENT:
		comm_debug("recv only event msg!\n");
		ipc_msg_forward(p_chan_info, mailbox_msg);
		break;

	default:
		comm_err("recv mail box type is %d\n", mailbox_msg->type);
		show_buffer((u8 *)mailbox_msg, 16);
		break;
	}
free_out:
	kfree(mailbox_msg);
	return ret;
}

/*********************************************************************
 *                   send list manage                                *
 *********************************************************************/
static void add_to_send_list(struct send_list *send_msg_node)
{
	unsigned long flags;

	if (NULL == send_msg_node) {
		comm_err("input NULL!\n");
		return;
	}

	raw_spin_lock_irqsave(&msg_lock, flags);
	list_add_tail(&send_msg_node->msg_list, &msg_list_head);
	raw_spin_unlock_irqrestore(&msg_lock, flags);
	comm_debug("add send_msg_node 0x%p, p_send_msg->seq %d,\n",
		   send_msg_node, send_msg_node->seq);
}

static void del_from_send_list(struct send_list *send_msg_node)
{
	unsigned long flags;

	if (NULL == send_msg_node) {
		comm_err("input NULL!\n");
		return;
	}

	if (NULL != send_msg_node->msg_data_linux)
		comm_put_cache(send_msg_node->msg_data_linux);
	raw_spin_lock_irqsave(&msg_lock, flags);
	list_del(&send_msg_node->msg_list);
	raw_spin_unlock_irqrestore(&msg_lock, flags);
	comm_debug("del send_msg_node is 0x%p\n", send_msg_node);
	kfree(send_msg_node);
}

static struct send_list *find_msg_by_seq(u32 seq)
{
	unsigned long flags;
	struct send_list *p_send_msg;

	raw_spin_lock_irqsave(&msg_lock, flags);

	if (list_empty(&msg_list_head)) {
		raw_spin_unlock_irqrestore(&msg_lock, flags);
		return NULL;
	}

	list_for_each_entry(p_send_msg, &msg_list_head, msg_list)
	if (p_send_msg->seq == seq)
		break;

	raw_spin_unlock_irqrestore(&msg_lock, flags);
	comm_debug("p_send_msg is 0x%p, p_send_msg->seq is %d, seq is %d\n",
		   p_send_msg, p_send_msg->seq, seq);
	if (p_send_msg->seq == seq)
		return p_send_msg;
	else
		return NULL;
}

int is_send_list_empty(u32 chip_id)
{
	int empty = 0;
	unsigned long flags;
	struct send_list *m;

	raw_spin_lock_irqsave(&msg_lock, flags);
	empty = list_empty(&msg_list_head);
	raw_spin_unlock_irqrestore(&msg_lock, flags);
	if (!empty) {
		list_for_each_entry(m, &msg_list_head, msg_list)
			comm_debug("msg evt %d inlist\n", m->msg_head.event);
	}

	return empty;
}

void show_send_list_info(u32 chip_id)
{
	struct send_list *m;
	unsigned long flags;

	raw_spin_lock_irqsave(&msg_lock, flags);
	if (list_empty(&msg_list_head)) {
		raw_spin_unlock_irqrestore(&msg_lock, flags);
		return;
	}
	list_for_each_entry(m, &msg_list_head, msg_list)
		comm_info("msg evt %d inlist\n", m->msg_head.event);

	raw_spin_unlock_irqrestore(&msg_lock, flags);
	return;
}

static void dispatch_recv_init(struct comm_chan_dynamic_info *chan_dyn_info)
{
	if (NULL == chan_dyn_info) {
		comm_err("input is NULL\n");
		return;
	}
	chan_dyn_info->dispatch_func = NULL;
}

/**
 * @brief dispatch_recv_reg - Register recv msg process by kernel app.
 * @param reg_func: recv msg process function.
 */
static void dispatch_recv_reg(struct comm_chan_info *p_chan_info,
			      int (*reg_func)(struct message_head *, char *))
{
	p_chan_info->chan_dynamic_info->dispatch_func = reg_func;
}

/*********************************************************************
 *                   create recv task by route table                 *
 *********************************************************************/
static int recv_thread(void *ptr)
{
	struct comm_chan_info *p_chan_info = ptr;

	comm_err("into demo_task!\n");
	while (1) {
		if (kthread_should_stop()) {
			comm_err("recv task exit!\n");
			break;
		}
		recv_process(p_chan_info);
	}

	return RET_OK;
}

static void start_recv_process(struct comm_chan_info *p_chan_info)
{
	int ret;
	char recv_task_name[NAME_LEN] = {0};

	snprintf(recv_task_name, NAME_LEN - 1, "recv_task_ch%d",
		 p_chan_info->chan_static_info->channel_no);
	p_chan_info->chan_thread_info.recv_thread = kthread_run(recv_thread,
								(void *)p_chan_info, recv_task_name);

	if (IS_ERR(p_chan_info->chan_thread_info.recv_thread)) {
		ret = PTR_ERR(p_chan_info->chan_thread_info.recv_thread);
		comm_err("create thread fail!\n");
	}
}

struct comm_chan_dynamic_info *comm_chan_dyn_init(u16 channel)
{
	struct comm_chan_dynamic_info *chan_dynamic_info;

	chan_dynamic_info = kzalloc(sizeof(struct comm_chan_dynamic_info),
				    GFP_KERNEL);
	if (NULL == chan_dynamic_info) {
		comm_err("init chan dyn: alloc fail!\n");
		return NULL;
	}
	memset(chan_dynamic_info, 0, sizeof(struct comm_chan_dynamic_info));
	dispatch_recv_init(chan_dynamic_info);
	return chan_dynamic_info;
}

int comm_adapt_init(int (*reg_recv_proc)(struct message_head *, char *),
		    struct comm_adaptor *adaptor)
{
	int i = 0;
	struct comm_chan_info *p_chan_info;
	int ret = 0;

	p_chan_info = kzalloc(sizeof(struct comm_chan_info) * COMM_CHANNEL_NUM,
			      GFP_KERNEL);
	if (NULL == p_chan_info) {
		comm_err("init comm fail, alloc fail!\n");
		return -ENOMEM;
	}

	g_adaptor = adaptor;
	memset(p_chan_info, 0, sizeof(struct comm_chan_info) * COMM_CHANNEL_NUM);
	for (i = 0; i < COMM_CHANNEL_NUM; i++) {
		init_waitqueue_head(&p_chan_info[i].suspend_wait);
		p_chan_info[i].chan_static_info = comm_route_table_init(i);
		if (p_chan_info[i].chan_static_info != NULL) {
			p_chan_info[i].chan_dynamic_info = comm_chan_dyn_init(i);
			dispatch_recv_reg(&p_chan_info[i], reg_recv_proc);
			p_chan_info[i].chan_dynamic_info->dev_info.trans_type =
				p_chan_info[i].chan_static_info->transfer_type;
			ret = comm_transfer_dev_init(&p_chan_info[i].chan_dynamic_info->dev_info);
			if (ret == RET_OK)
				start_recv_process(&p_chan_info[i]);
		}
	}
	s_chan_info = p_chan_info;
	mutex_init(&malloc_seq_mutex);
	malloc_seq = 0;

	return 0;
}

void comm_adapt_exit(struct comm_adaptor *adaptor)
{
	int i = 0;

	for (i = 0; i < COMM_CHANNEL_NUM; i++) {
		if (s_chan_info[i].chan_static_info != NULL) {
			kfree(s_chan_info[i].chan_static_info);
		}

		if (s_chan_info[i].chan_thread_info.recv_thread != NULL) {
			kthread_stop(s_chan_info[i].chan_thread_info.recv_thread);
		}

		if (s_chan_info[i].chan_dynamic_info != NULL) {
			comm_transfer_dev_exit(&s_chan_info[i].chan_dynamic_info->dev_info);
			kfree(s_chan_info[i].chan_dynamic_info);
		}

	}
	kfree(s_chan_info);

	comm_release_cache(adaptor);

	g_adaptor = NULL;
}
