#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include "tran_hiber.h"

struct hlist_head *binder_procs = NULL;
struct mutex *binder_procs_lock = NULL;

#define binder_inner_proc_lock(proc) _binder_inner_proc_lock(proc, __LINE__)
static void
_binder_inner_proc_lock(struct binder_proc *proc, int line)
__acquires(&proc->inner_lock)
{
	spin_lock(&proc->inner_lock);
}

#define binder_inner_proc_unlock(proc) _binder_inner_proc_unlock(proc, __LINE__)
static void
_binder_inner_proc_unlock(struct binder_proc *proc, int line)
__releases(&proc->inner_lock)
{
	spin_unlock(&proc->inner_lock);
}

static bool binder_worklist_empty_ilocked(struct list_head *list)
{
	return list_empty(list);
}

void binder_preset_handler(void *data, struct hlist_head *hhead,
			   struct mutex *lock)
{
	if (binder_procs == NULL)
		binder_procs = hhead;

	if (binder_procs_lock == NULL)
		binder_procs_lock = lock;
}

void binder_trans_handler(void *data, struct binder_proc *target_proc,
			  struct binder_proc *proc,
			  struct binder_thread *thread,
			  struct binder_transaction_data *tr)
{
	char buf_data[INTERFACETOKEN_BUFF_SIZE];
	size_t buf_data_size;
	char buf[INTERFACETOKEN_BUFF_SIZE] = {0};
	int i = 0;
	int j = 0;

	if (!(tr->flags & TF_ONE_WAY)
			&& target_proc
			&& (task_uid(target_proc->tsk).val > MIN_USERAPP_UID || task_uid(target_proc->tsk).val == HIBER_SYSTEM_UID)
			&& (proc->pid != target_proc->pid)
			&& is_frozen_tg(target_proc->tsk)) {
		buf_data_size = tr->data_size > INTERFACETOKEN_BUFF_SIZE ?
				INTERFACETOKEN_BUFF_SIZE : tr->data_size;

		if (g_hiber_debug) {
			if (access_ok((char *)tr->data.ptr.buffer, buf_data_size) && !__copy_from_user_inatomic(buf_data, (char *)tr->data.ptr.buffer, buf_data_size)) {
				if (buf_data_size > PARCEL_OFFSET) {
					char *p = (char *)(buf_data) + PARCEL_OFFSET;
					j = PARCEL_OFFSET + 1;

					while (i < INTERFACETOKEN_BUFF_SIZE && j < buf_data_size && *p != '\0') {
						buf[i++] = *p;
						j += 2;
						p += 2;
					}

					if (i == INTERFACETOKEN_BUFF_SIZE) buf[i - 1] = '\0';
				}
			}

			printk(KERN_ERR
					"HiberK: report sync-binder, caller_pid = %d, caller_uid = %d, buf=%s, code=%d.\n",
					task_tgid_nr(proc->tsk), task_uid(proc->tsk).val, buf, tr->code);
		}

		hiber_report(SYNC_BINDER, task_tgid_nr(proc->tsk), task_uid(proc->tsk).val,
				task_tgid_nr(target_proc->tsk), task_uid(target_proc->tsk).val, "SYNC_BINDER", -1);
	}

	if ((tr->flags & TF_ONE_WAY)
			&& target_proc
			&& (task_uid(target_proc->tsk).val > MIN_USERAPP_UID || task_uid(target_proc->tsk).val == HIBER_SYSTEM_UID)
			&& (proc->pid != target_proc->pid)
			&& is_frozen_tg(target_proc->tsk)) {
		buf_data_size = tr->data_size > INTERFACETOKEN_BUFF_SIZE ?
				INTERFACETOKEN_BUFF_SIZE : tr->data_size;

		if (access_ok((char *)tr->data.ptr.buffer, buf_data_size) && !__copy_from_user_inatomic(buf_data, (char *)tr->data.ptr.buffer, buf_data_size)) {
			if (buf_data_size > PARCEL_OFFSET) {
				char *p = (char *)(buf_data) + PARCEL_OFFSET;
				j = PARCEL_OFFSET + 1;

				while (i < INTERFACETOKEN_BUFF_SIZE && j < buf_data_size && *p != '\0') {
					buf[i++] = *p;
					j += 2;
					p += 2;
				}

				if (i == INTERFACETOKEN_BUFF_SIZE) buf[i - 1] = '\0';
			}

			hiber_report(ASYNC_BINDER, task_tgid_nr(proc->tsk), task_uid(proc->tsk).val,
				    task_tgid_nr(target_proc->tsk), task_uid(target_proc->tsk).val, buf, tr->code);
		}
	}

}

void binder_reply_handler(void *data, struct binder_proc *target_proc,
			  struct binder_proc *proc,
			  struct binder_thread *thread,
			  struct binder_transaction_data *tr)
{
	if (target_proc
			&& (task_uid(target_proc->tsk).val > MIN_USERAPP_UID || task_uid(target_proc->tsk).val == HIBER_SYSTEM_UID)
			&& (proc->pid != target_proc->pid)
			&& is_frozen_tg(target_proc->tsk)) {

		if (g_hiber_debug) {
			char buf_data[INTERFACETOKEN_BUFF_SIZE];
			size_t buf_data_size;
			char buf[INTERFACETOKEN_BUFF_SIZE] = {0};
			int i = 0;
			int j = 0;

			buf_data_size = tr->data_size > INTERFACETOKEN_BUFF_SIZE ?
					INTERFACETOKEN_BUFF_SIZE : tr->data_size;

			if (access_ok((char *)tr->data.ptr.buffer, buf_data_size) && !__copy_from_user_inatomic(buf_data, (char *)tr->data.ptr.buffer, buf_data_size)) {
				if (buf_data_size > PARCEL_OFFSET) {
					char *p = (char *)(buf_data) + PARCEL_OFFSET;
					j = PARCEL_OFFSET + 1;

					while (i < INTERFACETOKEN_BUFF_SIZE && j < buf_data_size && *p != '\0') {
						buf[i++] = *p;
						j += 2;
						p += 2;
					}

					if (i == INTERFACETOKEN_BUFF_SIZE) buf[i - 1] = '\0';
				}
			}

			printk(KERN_ERR
					"HiberK: reply-report sync-binder, caller_pid = %d, caller_uid = %d, buf=%s, code=%d.\n",
					task_tgid_nr(proc->tsk), task_uid(proc->tsk).val, buf, tr->code);
		}

		hiber_report(SYNC_BINDER, task_tgid_nr(proc->tsk), task_uid(proc->tsk).val,
			task_tgid_nr(target_proc->tsk), task_uid(target_proc->tsk).val, "SYNC_BINDER", -1);
	}
}

void binder_alloc_handler(void *data, size_t size, size_t *free_async_space, int is_async)
{
	struct task_struct *p = NULL;
	struct binder_alloc *alloc = NULL;

	alloc = container_of(free_async_space, struct binder_alloc, free_async_space);
	if (alloc == NULL) {
		return;
	}

	if (is_async
		&& (alloc->free_async_space < 3 * (size + sizeof(struct binder_buffer))
		|| (alloc->free_async_space < 100*1024))) {
		rcu_read_lock();
		p = find_task_by_vpid(alloc->pid);
		rcu_read_unlock();

		if (p != NULL && is_frozen_tg(p)) {
			hiber_report(ASYNC_BINDER, task_tgid_nr(current), task_uid(current).val,
					task_tgid_nr(p), task_uid(p).val, "free_buffer_full", -1);
		}
	}
}

void send_signal_handler(void *data, int sig, struct task_struct *killer,
			 struct task_struct *dst)
{
	if (is_frozen_tg(dst)
			&& (sig == SIGKILL || sig == SIGTERM || sig == SIGABRT || sig == SIGQUIT)) {
		if (hiber_report(SIGNAL, task_tgid_nr(killer), task_uid(killer).val,
				task_tgid_nr(dst), task_uid(dst).val, "signal", -1) == HIBER_ERROR)
			printk(KERN_ERR
			       "HiberK: report signal-freeze failed, sig = %d, caller = %d, target_uid = %d\n",
			       sig, task_tgid_nr(killer), task_uid(dst).val);
	}
}

static void get_uid_pid(int *from_pid, int *from_uid, int *to_pid, int *to_uid,
			struct binder_transaction *tr)
{
	if (tr->from != NULL && tr->from->proc != NULL
			&& tr->from->proc->tsk != NULL)
		*from_pid = task_tgid_nr(tr->from->proc->tsk);

	else
		*from_pid = -1;

	*from_uid = tr->sender_euid.val;
	*to_pid = tr->to_thread ?  tr->to_thread->proc->pid : -1;
	*to_uid = tr->to_thread ? task_uid(tr->to_thread->task).val : -1;
}

void hiber_check_uid_proc_status_debug(struct binder_proc *proc,
				      enum message_type type)
{
	struct rb_node *n = NULL;
	struct binder_thread *thread = NULL;
	int from_uid = -1;
	int from_pid = -1;
	int to_uid = -1;
	int to_pid = -1;
	struct binder_transaction *btrans = NULL;
	bool empty = true;
	int need_reply = -1;
	struct binder_work *w = NULL;

	binder_inner_proc_lock(proc);

	for (n = rb_first(&proc->threads); n != NULL; n = rb_next(n)) {
		thread = rb_entry(n, struct binder_thread, rb_node);
		empty = binder_worklist_empty_ilocked(&thread->todo);

		if (thread->task != NULL) {
			to_uid = task_uid(thread->task).val;

			if (!empty) {
				list_for_each_entry(w, &thread->todo, entry) {
					if (w != NULL && w->type == BINDER_WORK_TRANSACTION) {
						btrans = container_of(w, struct binder_transaction, work);
						spin_lock(&btrans->lock);

						if (btrans != NULL && btrans->to_thread == thread) {
							need_reply = (int)(btrans->need_reply == 1);
							get_uid_pid(&from_pid, &from_uid, &to_pid, &to_uid, btrans);
							spin_unlock(&btrans->lock);
							hiber_report(type, from_pid, from_uid, to_pid, to_uid, "FROZEN_TRANS_THREAD",
								    need_reply);

						} else
							spin_unlock(&btrans->lock);
					}
				}
			}

			btrans = thread->transaction_stack;

			if (btrans) {
				spin_lock(&btrans->lock);

				if (btrans->to_thread == thread) {
					need_reply = (int)(btrans->need_reply == 1);
					get_uid_pid(&from_pid, &from_uid, &to_pid, &to_uid, btrans);
					spin_unlock(&btrans->lock);
					hiber_report(type, from_pid, from_uid, to_pid, to_uid, "FROZEN_TRANS_STACK",
						    need_reply);

				} else
					spin_unlock(&btrans->lock);
			}
		}
	}

	empty = binder_worklist_empty_ilocked(&proc->todo);

	if (proc->tsk != NULL && !empty) {
		to_uid = task_uid(proc->tsk).val;
		list_for_each_entry(w, &proc->todo, entry) {
			if (w != NULL && w->type == BINDER_WORK_TRANSACTION) {
				btrans = container_of(w, struct binder_transaction, work);
				spin_lock(&btrans->lock);

				if (btrans != NULL && btrans->to_thread == thread) {
					need_reply = (int)(btrans->need_reply == 1);
					get_uid_pid(&from_pid, &from_uid, &to_pid, &to_uid, btrans);
					spin_unlock(&btrans->lock);
					hiber_report(type, from_pid, from_uid, to_pid, to_uid, "FROZEN_TRANS_PROC",
						    need_reply);

				} else
					spin_unlock(&btrans->lock);
			}
		}
	}

	binder_inner_proc_unlock(proc);
}

static void hiber_check_uid_proc_status(struct binder_proc *proc,
				       enum message_type type)
{
	struct rb_node *n = NULL;
	struct binder_thread *thread = NULL;
	int uid = -1;
	struct binder_transaction *btrans = NULL;
	bool empty = true;

	binder_inner_proc_lock(proc);

	for (n = rb_first(&proc->threads); n != NULL; n = rb_next(n)) {
		thread = rb_entry(n, struct binder_thread, rb_node);
		empty = binder_worklist_empty_ilocked(&thread->todo);

		if (thread->task != NULL) {
			uid = task_uid(thread->task).val;

			if (!empty) {
				binder_inner_proc_unlock(proc);
				hiber_report(type, -1, -1, -1, uid, "FROZEN_TRANS_THREAD", 1);
				return;
			}

			btrans = thread->transaction_stack;

			if (btrans) {
				spin_lock(&btrans->lock);

				if (btrans->to_thread == thread) {
					spin_unlock(&btrans->lock);
					binder_inner_proc_unlock(proc);
					hiber_report(type, -1, -1, -1, uid, "FROZEN_TRANS_STACK", 1);
					return;
				}

				spin_unlock(&btrans->lock);
			}
		}
	}

	empty = binder_worklist_empty_ilocked(&proc->todo);

	if (proc->tsk != NULL && !empty) {
		uid = task_uid(proc->tsk).val;
		binder_inner_proc_unlock(proc);
		hiber_report(type, -1, -1, -1, uid, "FROZEN_TRANS_PROC", 1);
		return;
	}

	binder_inner_proc_unlock(proc);
}

void hiber_check_frozen_transcation(uid_t uid, enum message_type type)
{
	struct binder_proc *proc;

	if (uid == TRANS_BINDER_DEBUG_ON)
		g_hiber_debug = true;

	else if (uid == TRANS_BINDER_DEBUG_OFF)
		g_hiber_debug = false;

	mutex_lock(binder_procs_lock);
	hlist_for_each_entry(proc, binder_procs, proc_node) {
		if (proc != NULL && (task_uid(proc->tsk).val == uid)) {
			if (g_hiber_debug == true)
				hiber_check_uid_proc_status_debug(proc, type);

			else
				hiber_check_uid_proc_status(proc, type);
		}
	}
	mutex_unlock(binder_procs_lock);
}

void hiber_check_uid_proc_status_sync_debug(struct binder_proc *proc,
                                       enum message_type type)
{
    struct rb_node *n = NULL;
    struct binder_thread *thread = NULL;
    int from_uid = -1, from_pid = -1;
    int to_uid = -1, to_pid = -1;
    struct binder_transaction *btrans = NULL;
    int need_reply = -1;
    binder_inner_proc_lock(proc);
    for (n = rb_first(&proc->threads); n != NULL; n = rb_next(n)) {
        thread = rb_entry(n, struct binder_thread, rb_node);
        if (thread->task != NULL) {
            to_uid = task_uid(thread->task).val;
            btrans = thread->transaction_stack;
            if (btrans) {
                spin_lock(&btrans->lock);
                if (btrans->to_thread == thread) {
                    need_reply = (int)(btrans->need_reply == 1);
                    get_uid_pid(&from_pid, &from_uid, &to_pid, &to_uid, btrans);
                    spin_unlock(&btrans->lock);
                    hiber_report(type, from_pid, from_uid, to_pid, to_uid,
                                 "FROZEN_TRANS_STACK", need_reply);
                } else {
                    spin_unlock(&btrans->lock);
                }
            }
        }
    }
    binder_inner_proc_unlock(proc);
}



static void hiber_check_uid_proc_status_sync(struct binder_proc *proc,
                                        enum message_type type)
{
    struct rb_node *n = NULL;
    struct binder_thread *thread = NULL;
    int uid = -1;
    struct binder_transaction *btrans = NULL;

    binder_inner_proc_lock(proc);

    for (n = rb_first(&proc->threads); n != NULL; n = rb_next(n)) {
        thread = rb_entry(n, struct binder_thread, rb_node);

        if (thread->task != NULL) {
            uid = task_uid(thread->task).val;

            btrans = thread->transaction_stack;
            if (btrans) {
                spin_lock(&btrans->lock);

                if (btrans->to_thread == thread) {
                    spin_unlock(&btrans->lock);
                    binder_inner_proc_unlock(proc);
                    hiber_report(type, -1, -1, -1, uid, "FROZEN_TRANS_STACK", 1);
                    return;
                }

                spin_unlock(&btrans->lock);
            }
        }
    }

    binder_inner_proc_unlock(proc);
}

void hiber_check_frozen_sync_transcation(uid_t uid, enum message_type type)
{
    struct binder_proc *proc;

    if (uid == TRANS_BINDER_DEBUG_ON)
        g_hiber_debug = true;

    else if (uid == TRANS_BINDER_DEBUG_OFF)
        g_hiber_debug = false;

    mutex_lock(binder_procs_lock);
    hlist_for_each_entry(proc, binder_procs, proc_node) {
        if (proc != NULL && (task_uid(proc->tsk).val == uid)) {
            if (g_hiber_debug == true)
                hiber_check_uid_proc_status_sync_debug(proc, type);

            else
                hiber_check_uid_proc_status_sync(proc, type);
        }
    }
    mutex_unlock(binder_procs_lock);
}
