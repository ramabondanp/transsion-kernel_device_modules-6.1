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

#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/printk.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/atomic.h>
#include <linux/kernel.h>
#include <linux/errno.h>

#include "ipc_common.h"
#include "comm_drv_io.h"
#include "comm_dev_io.h"
#include "comm_ret_code.h"
#include "common.h"
#include "comm_log.h"
#include "ring_buffer_local.h"
#include "comm_route.h"
#include "comm_chan_adapt.h"
#include "comm_drv.h"

#define MSG_LENGTH_WITH_USR_HEAD(p)   (p->msg_len + sizeof(struct msg_usr))

static struct comm_proc_msg_list *find_proc_node_by_tid(struct target_id *tid);
static void del_from_proc_list(struct comm_proc_msg_list *proc_msg_node);

static void user_list_uninstall(struct comm_dev *comm);
static struct comm_user_info *find_user_node_by_pid(struct list_head *head, u32 pid);
static struct comm_user_info *find_user_node_by_entity_id(struct list_head *head, u32 ent);
static void del_from_user_list(struct comm_user_info *user_node);
static void comm_free_ringbuffer_data(struct ring_buffer_mng *rb_mng);
static int send_msg(u16 event_id, char *buf, size_t length,
		    struct target_id *target, struct target_id *sender);

static struct comm_dev *g_comm_dev;
struct comm_dev *comm_get_dev(void)
{
	return g_comm_dev;
}

/*********************************************************************
 *             Interface with userspace (file operations)            *
 *********************************************************************/
static int comm_open(struct inode *inode, struct file *file)
{
	struct comm_user_info *user_node;
	struct comm_dev *comm = miscdev_to_commdev(file->private_data);

	user_node = kzalloc(sizeof(struct comm_user_info), GFP_KERNEL);
	if (NULL == user_node) {
		comm_err("user info malloc fail!\n");
		return RET_ERROR;
	}
	init_waitqueue_head(&user_node->recv_queue);
	init_waitqueue_head(&user_node->send_queue);
	user_node->tid.pid = current->tgid;
	atomic_set(&(user_node->recv_count), 0);

	user_node->p_rb_mng = ring_buffer_init(sizeof(struct msg_usr *),
					       USER_MSG_RING_BUFFER_SIZE);
	if (NULL == user_node->p_rb_mng) {
		comm_err("user info ringbuffer init fail!\n");
		return RET_ERROR;
	}
	comm_info("comm_open user pid: %d!, tgid is %d\n",
		  user_node->tid.pid, current->tgid);
	mutex_lock(&comm->list_mng.proc_user_lock);
	list_add_tail(&user_node->user_list, &comm->list_mng.user_head);
	mutex_unlock(&comm->list_mng.proc_user_lock);

	return RET_OK;
}

static int comm_close(struct inode *inode, struct file *file)
{
	struct comm_dev *comm = miscdev_to_commdev(file->private_data);
	struct comm_user_info *user_node;
	struct ring_buffer_mng *rb_mng;
	u32 pid;

	pid = current->tgid;

	mutex_lock(&comm->list_mng.proc_user_lock);
	user_node = find_user_node_by_pid(&comm->list_mng.user_head, pid);
	if (NULL == user_node) {
		comm_err("find fail, user_node is 0x%p, pid is %d\n",
			 user_node, pid);
		mutex_unlock(&comm->list_mng.proc_user_lock);
		return RET_ERROR;
	}
	comm_debug("del node: 0x%p, pid: %d\n", user_node, pid);
	rb_mng = user_node->p_rb_mng;
	del_from_user_list(user_node);
	mutex_unlock(&comm->list_mng.proc_user_lock);

	comm_free_ringbuffer_data(rb_mng);

	return RET_OK;
}

static ssize_t comm_read(struct file *file, char __user *buf, size_t count,
			 loff_t *ppos)
{
	struct comm_dev *comm = miscdev_to_commdev(file->private_data);
	int ret = RET_ERROR;
	u32 pid;
	struct comm_user_info *p_user_info;
	struct msg_usr *msg_recv_rb;

	pid = current->tgid;
	mutex_lock(&comm->list_mng.proc_user_lock);
	p_user_info = find_user_node_by_pid(&comm->list_mng.user_head, pid);
	mutex_unlock(&comm->list_mng.proc_user_lock);
	if (NULL == p_user_info) {
		comm_err("find fail, p_user_info is 0x%p, pid is %d\n",
			 p_user_info, pid);
		return RET_ERROR;
	}

	ret = wait_event_interruptible_timeout(p_user_info->recv_queue,
					       (atomic_read(&(p_user_info->recv_count)) > 0),
					       msecs_to_jiffies(READ_TIMEOUT));
	if (ret == 0) {
		comm_debug("comm_read timeout!\n");
		ret = -ETIME;
		goto out;
	} else if (ret == -ERESTARTSYS) {
		comm_err("comm_read interrupted by a signal!\n");
		goto out;
	}
	atomic_dec(&(p_user_info->recv_count));

	ret = ring_buffer_read(p_user_info->p_rb_mng, (void *)&msg_recv_rb,
			       sizeof(struct msg_usr *));
	if (RET_OK != ret) {
		comm_debug("read ringbuffer empty!\n");
		return RET_ERROR;
	}
	if (MSG_LENGTH_WITH_USR_HEAD(msg_recv_rb) > count) {
		comm_err("read msg is larger than %zu!", count);
		ret = RET_ERROR;
		goto free_out;
	}

	if (copy_to_user(buf, msg_recv_rb, sizeof(struct msg_usr))) {
		comm_err("copy to user failed!\n");
		ret = RET_ERROR;
		goto free_out;
	}

	if (0 != msg_recv_rb->msg_len && NULL != msg_recv_rb->msg_data_user) {
		if (copy_to_user(buf + sizeof(struct msg_usr),
				 msg_recv_rb->msg_data_user, msg_recv_rb->msg_len)) {
			comm_err("copy to user failed!\n");
			ret = RET_ERROR;
			goto free_out;
		}
		comm_debug("addr:%p, msg in kernel:%s", buf + sizeof(struct msg_usr),
			   msg_recv_rb->msg_data_user);
	}
	ret = MSG_LENGTH_WITH_USR_HEAD(msg_recv_rb);

free_out:
	if (NULL != msg_recv_rb->msg_data_user)
		kfree(msg_recv_rb->msg_data_user);
	kfree(msg_recv_rb);
out:
	return ret;
}

static ssize_t comm_write(struct file *file, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	struct comm_dev *comm = miscdev_to_commdev(file->private_data);
	u8 *p_send_data = NULL;
	ssize_t ret = RET_OK;
	struct msg_usr *msg_head_send = NULL;
	struct comm_user_info *user_node;
	u32 cache_sz;

	mutex_lock(&comm->list_mng.proc_user_lock);
	user_node = find_user_node_by_pid(&comm->list_mng.user_head, current->tgid);
	if (NULL == user_node) {
		comm_err("find fail, user_node is 0x%p, pid is %d\n",
			 user_node, current->tgid);
		mutex_unlock(&comm->list_mng.proc_user_lock);
		return RET_ERROR;
	}
	mutex_unlock(&comm->list_mng.proc_user_lock);


	if (comm->adaptor.power_state != COMM_DEV_POWER_WORK) {
		comm_err("pid %d buf %p comm state is %d, don't use communication when device not work!\n",
			 current->tgid, buf, comm->adaptor.power_state);
		return -ENOTSUPP;
	}

	if (count != sizeof(struct msg_usr)) {
		comm_err("send msg head error!\n");
		return RET_ERROR;
	}

	msg_head_send = kzalloc(count, GFP_KERNEL);
	if (NULL == msg_head_send) {
		comm_err("comm write malloc fail!\n");
		return RET_ERROR;
	}

	if (copy_from_user((char *)msg_head_send, (char *)buf, count)) {
		comm_err("copy from user failed!\n");
		return RET_ERROR;
	}
	if (0 != msg_head_send->msg_len) {
		if (msg_head_send->msg_len > COMM_CACHE_SZ) {
			kfree(msg_head_send);
			comm_err("payload sz %d too big!\n", msg_head_send->msg_len);
			return -ENOMEM;
		}

		ret = comm_get_cache(&comm->adaptor, &p_send_data, &cache_sz);
		if (ret) {
			comm_err("no cache!\n");
			kfree(msg_head_send);
			return RET_ERROR;
		}
		if (copy_from_user(p_send_data, msg_head_send->msg_data_user,
				   msg_head_send->msg_len)) {
			comm_err("copy from user failed!\n");
			kfree(msg_head_send);
			return RET_ERROR;
		}
	}

	if (user_node->entity_id == 0) {
		kfree(msg_head_send);
		comm_err("need specify sender's entity id\n");
		return RET_ERROR;
	}
	msg_head_send->send_tid.pid = user_node->entity_id;

	ret = send_msg(msg_head_send->event, p_send_data,
		       msg_head_send->msg_len, &msg_head_send->recv_tid,
		       &msg_head_send->send_tid);

	kfree(msg_head_send);
	return ret;
}

/**
 * @brief send_msg_for_kernel_thread() - send msg to rtos.
 * @param event_id: send event id.
 * @param buf: send msg data address.
 * @param length: send msg length.
 * @param tid: the destination task id.
 * @return process result.
 */
int send_msg_for_kernel_thread(u16 event_id, char *buf, size_t length,
			       struct target_id *sender, struct target_id *recver)
{
	u8 *p_send_data = NULL;
	struct target_id send_tid = {0};
	struct comm_dev *comm = comm_get_dev();
	u32 cache_sz;
	int ret;

	if (comm->adaptor.power_state != COMM_DEV_POWER_WORK) {
		comm_err("comm state is %d, kern don't use communication when device not work!\n",
			 comm->adaptor.power_state);
		return -ENOTSUPP;
	}

	if (0 != length) {
		if (length > COMM_CACHE_SZ) {
			comm_err("payload too big!\n");
			return RET_ERROR;
		}

		ret = comm_get_cache(&comm->adaptor, &p_send_data, &cache_sz);
		if (ret) {
			comm_err("no cache!\n");
			return RET_ERROR;
		}
		memcpy(p_send_data, buf, length);
	}
	send_tid.pid = current->tgid;
	send_tid.pno = sender->pno;
	ret = send_msg(event_id, p_send_data, length, recver, &send_tid);
	if (ret != RET_OK) {
		comm_err("send msg fail!\n");
		return ret;
	}
	return RET_OK;
}
EXPORT_SYMBOL(send_msg_for_kernel_thread);

/**
 * @brief write_data_for_kernel_thread() - write data to rtos mem addr.
 * @param write_addr: addr in rtos.
 * @param buf: write data address.
 * @param length: write data length.
 * @param tid: the destination task id.
 * @return process result.
 */
int write_data_for_kernel_thread(u32 write_addr, char *buf, size_t length,
				 struct target_id *tid)
{
	return write_data_adapt(write_addr, buf, length, tid);
}
EXPORT_SYMBOL(write_data_for_kernel_thread);

/**
 * @brief read_data_for_kernel_thread() - read data from rtos mem addr.
 * @param read_addr: addr in rtos.
 * @param buf: read data address.
 * @param length: read data length.
 * @param tid: the destination task id.
 * @return process result.
 */
int read_data_for_kernel_thread(u32 read_addr, char *buf, size_t length,
				struct target_id *tid)
{
	return read_data_adapt(read_addr, buf, length, tid);
}
EXPORT_SYMBOL(read_data_for_kernel_thread);

/*********************************************************************
 *                   send msg process in kernel                      *
 *********************************************************************/
/**
 * @brief send_msg() - send msg to rtos.
 * @param event_id: send event id.
 * @param buf: send msg data address.
 * @param length: send msg length.
 * @param target: the destination task id.
 * @param sender: the source task id.
 * @return process result.
 */
static int send_msg(u16 event_id, char *buf, size_t length,
		    struct target_id *target, struct target_id *sender)
{
	int ret = RET_OK;

	if (NULL == target || NULL == sender) {
		comm_err("input param is NULL!\n");
		return RET_ERROR;
	}

	comm_debug("send msg: evt %d, len %zu. from[%d/%d/%d] to [%d/%d/%d]!\n",
		   event_id, length, sender->chip_id, sender->pid, sender->pno,
		   target->chip_id, target->pid, target->pno);

	sender->chip_id = CHIP_ANDROID_MESSAGE;
	ret = send_msg_adapt(event_id, buf, length, target, sender);
	if (ret != RET_OK) {
		comm_err("send msg fail: evt %d, len %zu. from[%d/%d/%d] to [%d/%d/%d]!\n",
			 event_id, length, sender->chip_id, sender->pid, sender->pno,
			 target->chip_id, target->pid, target->pno);
		return ret;
	}

	return ret;
}

/*********************************************************************
 *                   recv msg process in kernel                      *
 *********************************************************************/
int dispatch_recv(struct message_head *p_msg_recv, char *msg_data)
{
	int ret = RET_OK;
	struct comm_proc_msg_list *proc_msg_node;
	struct comm_user_info *p_user_info;
	struct msg_usr *msg_recv_rb;
	struct comm_dev *comm = comm_get_dev();

	if (NULL == p_msg_recv) {
		comm_err("input is NULL\n");
		return RET_ERROR;
	}

	comm_debug("dispatch msg: evt %d, len %d. from[%d/%d/%d] to [%d/%d/%d]!\n",
		   p_msg_recv->event, p_msg_recv->msg_len, p_msg_recv->send_tid.chip_id,
		   p_msg_recv->send_tid.pid, p_msg_recv->send_tid.pno,
		   p_msg_recv->recv_tid.chip_id, p_msg_recv->recv_tid.pid,
		   p_msg_recv->recv_tid.pno);

	comm_debug("into dispatch_recv, pno: %d\n", p_msg_recv->recv_tid.pno);
	/* determine whether msg to kernel or user by pno 0x0-0xff: kernel */
	if ((p_msg_recv->recv_tid.pno | PNO_MASK) == PNO_MASK) {
		mutex_lock(&comm->list_mng.proc_msg_lock);
		proc_msg_node = find_proc_node_by_tid(&p_msg_recv->recv_tid);
		mutex_unlock(&comm->list_mng.proc_msg_lock);
		if (NULL == proc_msg_node) {
			comm_err("find fail, proc_msg_node is 0x%p, pno is %d\n",
				 proc_msg_node, p_msg_recv->recv_tid.pno);
			goto err_out;
		}
		if (NULL != proc_msg_node->msg_proc_func_kernel) {
			/* call v4l2 kernel function */
			proc_msg_node->msg_proc_func_kernel(p_msg_recv->event,
							    msg_data, p_msg_recv->msg_len);
		}
		if (msg_data != NULL)
			kfree(msg_data);
	} else {
		p_msg_recv->recv_tid.pno = p_msg_recv->recv_tid.pno >> PNO_RIGHT_SHIFT;
		mutex_lock(&comm->list_mng.proc_user_lock);
		p_user_info = find_user_node_by_entity_id(&comm->list_mng.user_head,
							  p_msg_recv->recv_tid.pid);
		mutex_unlock(&comm->list_mng.proc_user_lock);
		if (NULL == p_user_info) {
			comm_err("find fail, p_user_info is 0x%p, pid is %d\n",
				 p_user_info, p_msg_recv->recv_tid.pid);
			goto err_out;
		}

		msg_recv_rb = kzalloc(sizeof(struct msg_usr), GFP_KERNEL);
		if (NULL == msg_recv_rb) {
			comm_err("can't malloc mem!\n");
			goto err_out;
		}
		comm_debug("find success, p_user_info is 0x%p, pid is %d\n",
			   p_user_info, p_msg_recv->recv_tid.pid);

		memcpy(msg_recv_rb, p_msg_recv, sizeof(struct message_head));
		msg_recv_rb->msg_len = p_msg_recv->msg_len;
		msg_recv_rb->msg_data_user = msg_data;

		ret = ring_buffer_write(p_user_info->p_rb_mng, (void *)&msg_recv_rb,
					sizeof(struct msg_usr *));
		if (RET_ERROR == ret) {
			comm_err("can't write ringbuffer!\n");
			kfree(msg_recv_rb);
			goto err_out;
		}
		atomic_inc(&(p_user_info->recv_count));
		wake_up_interruptible(&p_user_info->recv_queue);
	}

	return ret;
err_out:
	if (msg_data != NULL)
		kfree(msg_data);
	return RET_ERROR;
}

static struct comm_proc_msg_list *make_proc_msg_node(struct target_id *tid,
						     process_msg_t cb)
{
	struct comm_proc_msg_list *proc_msg_node;

	proc_msg_node = kzalloc(sizeof(struct comm_proc_msg_list), GFP_KERNEL);
	if (NULL == proc_msg_node) {
		comm_err("kzalloc proc msg list struct fail!\n");
		return NULL;
	}
	memcpy(&proc_msg_node->tid, tid, sizeof(struct target_id));
	proc_msg_node->msg_proc_func_kernel = cb;

	return proc_msg_node;
}

/**
 * @brief msg_proc_kernel_reg - Register recv msg process by kernel app.
 * @param tid: the task information of process msg.
 * @param reg_func: msg process function.
 */
int register_process_msg_callback(struct target_id *tid, process_msg_t cb)
{
	struct comm_proc_msg_list *proc_msg_node;
	struct comm_dev *comm = comm_get_dev();

	proc_msg_node = make_proc_msg_node(tid, cb);
	if (NULL == proc_msg_node) {
		comm_err("make send node fail!\n");
		return RET_ERROR;
	}
	mutex_lock(&comm->list_mng.proc_msg_lock);
	list_add_tail(&proc_msg_node->msg_list, &comm->list_mng.proc_msg_head);
	mutex_unlock(&comm->list_mng.proc_msg_lock);
	comm_info("add proc_msg_node 0x%p, p_send_msg->pno %d,\n",
		  proc_msg_node, proc_msg_node->tid.pno);
	return RET_OK;
}
EXPORT_SYMBOL(register_process_msg_callback);

/**
 * @brief unregister_process_msg_callback - unregister recv msg proc by kernel.
 * @param tid: the task information of process msg.
 */
void unregister_process_msg_callback(struct target_id *tid)
{
	struct comm_proc_msg_list *proc_msg_node;
	struct comm_dev *comm = comm_get_dev();

	mutex_lock(&comm->list_mng.proc_msg_lock);
	proc_msg_node = find_proc_node_by_tid(tid);
	if (NULL == proc_msg_node) {
		comm_err("find msg node fail!\n");
		mutex_unlock(&comm->list_mng.proc_msg_lock);
		return;
	}

	del_from_proc_list(proc_msg_node);
	mutex_unlock(&comm->list_mng.proc_msg_lock);
}
EXPORT_SYMBOL(unregister_process_msg_callback);

static void comm_free_ringbuffer_data(struct ring_buffer_mng *rb_mng)
{
	struct msg_usr *msg_recv_rb;
	int ret = RET_ERROR;

	if (NULL == rb_mng) {
		comm_err("rb_mng is null\n");
		return;
	}

	while (1) {
		ret = ring_buffer_read(rb_mng, (void *)&msg_recv_rb,
				       sizeof(struct msg_usr *));
		if (RET_OK != ret)
			break;
		if (NULL != msg_recv_rb->msg_data_user)
			kfree(msg_recv_rb->msg_data_user);
		kfree(msg_recv_rb);
	}
	ring_buffer_destory(rb_mng);
}

static void proc_msg_list_uninstall(struct comm_dev *comm)
{
	struct comm_proc_msg_list *proc_msg_node;
	struct comm_proc_msg_list *next_node;

	mutex_lock(&comm->list_mng.proc_msg_lock);

	list_for_each_entry_safe(proc_msg_node, next_node, &comm->list_mng.proc_msg_head, msg_list)
	del_from_proc_list(proc_msg_node);

	mutex_unlock(&comm->list_mng.proc_msg_lock);
}

static void del_from_proc_list(struct comm_proc_msg_list *proc_msg_node)
{
	comm_info("del proc_msg_node is 0x%p\n", proc_msg_node);
	list_del(&proc_msg_node->msg_list);
	kfree(proc_msg_node);
}

static struct comm_proc_msg_list *find_proc_node_by_tid(struct target_id *tid)
{
	struct comm_proc_msg_list *proc_msg_node;
	struct comm_dev *comm = comm_get_dev();

	list_for_each_entry(proc_msg_node, &comm->list_mng.proc_msg_head, msg_list)
	if (proc_msg_node->tid.pno == tid->pno)
		break;

	if (proc_msg_node->tid.pno == tid->pno)
		return proc_msg_node;
	else
		return NULL;
}

static void user_list_uninstall(struct comm_dev *comm)
{
	struct comm_user_info *user_node;
	struct comm_user_info *next_node;

	mutex_lock(&comm->list_mng.proc_user_lock);

	list_for_each_entry_safe(user_node, next_node, &comm->list_mng.user_head, user_list)
	del_from_user_list(user_node);

	mutex_unlock(&comm->list_mng.proc_user_lock);
}

static void del_from_user_list(struct comm_user_info *user_node)
{
	comm_info("del user_node is 0x%p\n", user_node);
	list_del(&user_node->user_list);
	kfree(user_node);
}

static struct comm_user_info *find_user_node_by_pid(struct list_head *head, u32 pid)
{
	struct comm_user_info *user_node = NULL;

	list_for_each_entry(user_node, head, user_list) {
		if (user_node->tid.pid == pid)
			break;
	}
	if (user_node == NULL) {
		comm_err("cann't find pid %d\n", pid);
		return NULL;
	}

	if (&user_node->user_list == head) {
		comm_err("usernode head wrong, search head %p, userhead %p\n",
			 head, &user_node->user_list);
		return NULL;
	}

	return user_node;
}

static struct comm_user_info *find_user_node_by_entity_id(struct list_head *head, u32 ent)
{
	struct comm_user_info *user_node = NULL;

	list_for_each_entry(user_node, head, user_list) {
		if (user_node->entity_id == ent)
			break;
	}
	if (user_node == NULL) {
		comm_err("entity %d not receiving\n", ent);
		return NULL;
	}

	if (&user_node->user_list == head) {
		comm_err("usernode head wrong, search head %p, userhead %p\n",
			 head, &user_node->user_list);
		return NULL;
	}

	return user_node;
}

int comm_set_sender_entity_id(u32 entity_id)
{
	struct comm_dev *comm = comm_get_dev();
	struct comm_user_info *user_node;

	mutex_lock(&comm->list_mng.proc_user_lock);
	user_node = find_user_node_by_pid(&comm->list_mng.user_head, current->tgid);
	if (user_node == NULL) {
		comm_err("current pid not register\n");
		mutex_unlock(&comm->list_mng.proc_user_lock);
		return -ENOENT;
	}

	user_node->entity_id = entity_id;
	mutex_unlock(&comm->list_mng.proc_user_lock);
	comm_info("set ent %d, pid %d\n", entity_id, current->tgid);
	return 0;
}

/**
 * comm_ioctl- communication device private ioctrl
 *
 * Returns negative errno, 0 for success
 */
long comm_ioctl(struct file *pfile, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	void __user *argp = (void __user *)arg;
	uint32_t timeout_ms;
	uint32_t suspend;
	struct comm_dev *comm = miscdev_to_commdev(pfile->private_data);
	int32_t wait_times = 100;
	uint32_t ret_stat = 0xFF;
	uint32_t entity_id;

	switch (cmd) {
	case COMM_DRV_WAIT_READY_TIMEOUT:

		if (copy_from_user(&timeout_ms, (void __user *)argp, sizeof(uint32_t))) {
			pr_warn(" from userspace err\n");
			return -EFAULT;
		}

		ret = wait_event_interruptible_timeout(comm->adaptor.ready_wait,
						       (comm->adaptor.handshake_ready == COMM_OK),
						       timeout_ms);
		if (ret == 0) {
			pr_err("wait comm_dev ready timeout\n");
			return -ETIMEDOUT;
		} else if (ret < 0) {
			pr_err("wait comm_dev interrupted\n");
			return ret;
		}

		ret = 0;
		comm->adaptor.handshake_ready = COMM_ERROR;

		break;

	case COMM_DRV_REQ_SUSPEND:

		if (copy_from_user(&suspend, (void __user *)argp, sizeof(uint32_t))) {
			pr_warn(" from userspace err\n");
			return -EFAULT;
		}

		if (suspend) {
			comm_info("user requesting suspend\n");
			comm->adaptor.power_state = COMM_DEV_POWER_SUSPENDING;
		} else {
			comm->adaptor.power_state = COMM_DEV_POWER_WORK;
			comm_info("user request work\n");
			return 0;
		}

		while (!is_send_list_empty(CHIP_RTOS) && (wait_times--)) {
			msleep(10);
		}
		if (wait_times < 0) {
			comm_err("still msg sending after 1 Second!\n");
			show_send_list_info(CHIP_RTOS);
			return -EAGAIN;
		}

		ret =  sending_suspend_request(CHIP_RTOS);
		if (ret != RET_OK) {
			return -EAGAIN;
		}

		ret = waiting_suspend_ack(CHIP_RTOS);
		if (ret != RET_OK) {
			return -EAGAIN;
		}

		ret = sending_suspend_done(CHIP_RTOS);
		if (ret != RET_OK) {
			return -ETIMEDOUT;
		}

		comm->adaptor.power_state = COMM_DEV_POWER_SUSPENDED;
		ret = 0;

		break;

	case COMM_DRV_GET_SUSPEND_STATE:

		ret_stat = (comm->adaptor.power_state == COMM_DEV_POWER_SUSPENDED) ? 1 : 0;
		if (copy_to_user(argp, &ret_stat, sizeof(uint32_t))) {
			pr_warn("copy to userspace err\n");
			return -EFAULT;
		}
		break;

	case COMM_DRV_SET_ENTITY_ID:
		if (copy_from_user(&entity_id, (void __user *)argp, sizeof(uint32_t))) {
			comm_err("copy from userspace err\n");
			return -EFAULT;
		}

		if (entity_id == 0) {
			comm_err("entity number cannot be zero\n");
			return -EINVAL;
		}

		ret = comm_set_sender_entity_id(entity_id);

		break;
	default:
		pr_err(" bad cmd %lu\n", arg);
		ret = -ENOIOCTLCMD;
		break;
	}
	return ret;
}

static ssize_t ipc_version_check_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct comm_dev *comm = comm_get_dev();
	ssize_t len = 0;

	len = snprintf(buf, 64, "communication %s support version check!\n",
		       comm->adaptor.ignore_version_check ? "not" : "");

	return len;
}

static ssize_t ipc_version_check_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t n)
{
	struct comm_dev *comm = comm_get_dev();
	int rc = 0;
	uint32_t val = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc) {
		comm_err("input str invalid %s\n", buf);
		return n;
	}

	comm->adaptor.ignore_version_check = val;
	comm_info("ignore version check new value %d\n", comm->adaptor.ignore_version_check);

	return n;
}

static DEVICE_ATTR_RW(ipc_version_check);

/* define init device opera object */
static const struct file_operations comm_fops = {
	.owner = THIS_MODULE,
	.read = comm_read,
	.write = comm_write,
	.open = comm_open,
	.release = comm_close,
	.unlocked_ioctl = comm_ioctl,
};

static int comm_init(void)
{
	struct comm_dev *pdev;
	uint32_t ret = RET_OK;
	u8 *cache;

	pdev = kzalloc(sizeof(struct comm_dev), GFP_KERNEL);
	if (!pdev) {
		comm_err("%s malloc failed\n", __func__);
		return -ENOMEM;
	}

	cache = kzalloc(COMM_CACHE_SZ * COMM_CACHE_CNT, GFP_KERNEL);
	if (!cache) {
		comm_err("%s malloc failed\n", __func__);
		kfree(pdev);
		return -ENOMEM;
	}

	pdev->misc_dev.minor = MISC_DYNAMIC_MINOR;
	pdev->misc_dev.name = "commdev";
	pdev->misc_dev.fops = &comm_fops;
	ret = misc_register(&pdev->misc_dev);
	if (ret) {
		comm_err("misc dev register failed\n");
		goto pdev_free;
	}

	INIT_LIST_HEAD(&(pdev->list_mng).proc_msg_head);
	INIT_LIST_HEAD(&(pdev->list_mng).user_head);

	mutex_init(&(pdev->list_mng).proc_msg_lock);
	mutex_init(&(pdev->list_mng).proc_user_lock);
	ret = comm_adapt_init(dispatch_recv, &pdev->adaptor);
	if (ret) {
		goto list_mng_destroy;
	}
	comm_preset_cache(&pdev->adaptor, cache);

	pdev->adaptor.handshake_ready = COMM_ERROR;
	init_waitqueue_head(&pdev->adaptor.ready_wait);
	pdev->adaptor.power_state = COMM_DEV_POWER_WORK;
	init_waitqueue_head(&pdev->adaptor.ready_wait);

	ret = device_create_file(pdev->misc_dev.this_device, &dev_attr_ipc_version_check);
	if (ret) {
		dev_err(pdev->misc_dev.this_device, "create attr file failed\n");
		goto adaptor_deinit;
	}

	g_comm_dev = pdev;

	return ret;

adaptor_deinit:
	comm_adapt_exit(&pdev->adaptor);

list_mng_destroy:
	mutex_destroy(&(pdev->list_mng).proc_msg_lock);
	mutex_destroy(&(pdev->list_mng).proc_user_lock);
	misc_deregister(&pdev->misc_dev);

pdev_free:
	kfree(pdev);
	kfree(cache);

	return ret;
}

static void comm_exit(void)
{
	misc_deregister(&g_comm_dev->misc_dev);

	comm_adapt_exit(&g_comm_dev->adaptor);
	proc_msg_list_uninstall(g_comm_dev);
	user_list_uninstall(g_comm_dev);

	kfree(g_comm_dev);
	g_comm_dev = NULL;
}

module_init(comm_init);
module_exit(comm_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("tetras.ai");
MODULE_DESCRIPTION("process module for other chips' data in");
