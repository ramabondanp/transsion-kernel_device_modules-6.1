#ifndef _TRAN_HIBER_H
#define _TRAN_HIBER_H

#include <linux/freezer.h>
#include <linux/cgroup.h>
#include <linux/version.h>
#include <uapi/linux/android/binder.h>
#include <trace/hooks/binder.h>
#include <trace/hooks/signal.h>
#include <linux/sched/cputime.h>
#include <kernel/sched/sched.h>
#include <drivers/android/binder_internal.h>
#include <drivers/android/binder_alloc.h>

#define HIBER_NOERROR             (0)
#define HIBER_ERROR               (-1)
#define MIN_USERAPP_UID (10000)
#define HIBER_SYSTEM_UID (1000)
#define INTERFACETOKEN_BUFF_SIZE (140)
#define PARCEL_OFFSET (16)
#define TRANS_BINDER_DEBUG_ON (-10000)
#define TRANS_BINDER_DEBUG_OFF (-9000)

#define HIBER_FAMILY_VERSION  1
#define HIBER_FAMILY  "tran_hiber"
#define GENL_ID_GENERATE    0
#define NLA_DATA(na) ((char *)((char *)(na) + NLA_HDRLEN))
#define NLA_PAYLOAD(len) (len - NLA_HDRLEN)

#define TAG "HiberK"

enum {
	HIBER_ATTR_MSG_UNDEFINE = 0,
	HIBER_ATTR_MSG_GENL,
	__HIBER_ATTR_MSG_MAX
};
#define HIBER_ATTR_MSG_MAX (__HIBER_ATTR_MSG_MAX - 1)

enum {
	HIBER_CMD_UNDEFINE = 0,
	HIBER_CMD_GENL,
	__HIBER_CMD_MAX,
};
#define HIBER_CMD_MAX (__HIBER_CMD_MAX - 1)

struct hiber_message {
	int type;
	int port;
	int caller_uid;
	int caller_pid;
	int target_pid;
	int target_uid;
	int pkg_cmd;
	int code;
	char rpc_name[INTERFACETOKEN_BUFF_SIZE];
};

enum message_type {
	ASYNC_BINDER,
	SYNC_BINDER,
	FROZEN_TRANS,
	SIGNAL,
	PKG,
	LOOP_BACK,
	FROZEN_SYNC_TRANS,
	TYPE_MAX
};

enum pkg_cmd {
	ADD_ONE_UID,
	DEL_ONE_UID,
	DEL_ALL_UID,
	PKG_CMD_MAX
};

extern bool g_hiber_debug;

static inline bool is_frozen_tg(struct task_struct *task)
{
	return freezing(task->group_leader) ||
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
                (READ_ONCE(task->group_leader->__state) & TASK_FROZEN) || cgroup_task_frozen(task->group_leader);
#else
                frozen(task->group_leader) || cgroup_task_frozen(task->group_leader);
#endif
}

int hiber_report(enum message_type type, int caller_pid, int caller_uid,
		int target_pid, int target_uid, const char *rpc_name, int code);
void hiber_network_cmd_parse(uid_t uid, enum pkg_cmd cmd);
void hiber_check_frozen_transcation(uid_t uid, enum message_type type);
void hiber_check_frozen_sync_transcation(uid_t uid, enum message_type type);
int hiber_netfilter_init(void);
void hiber_netfilter_deinit(void);
void hiber_check_async_binder_buffer(bool is_async, int free_async_space,
				    int size, int binder_buffer_size, int alloc_buffer_size, int pid);
void hiber_check_signal(struct task_struct *p, int sig);

void binder_preset_handler(void *data, struct hlist_head *hhead,
			   struct mutex *lock);
void binder_trans_handler(void *data, struct binder_proc *target_proc,
			  struct binder_proc *proc, struct binder_thread *thread,
			  struct binder_transaction_data *tr);
void binder_reply_handler(void *data, struct binder_proc *target_proc,
			  struct binder_proc *proc, struct binder_thread *thread,
			  struct binder_transaction_data *tr);
void binder_alloc_handler(void *data, size_t size, size_t *free_async_space, int is_async);
void send_signal_handler(void *data, int sig, struct task_struct *killer,
			 struct task_struct *dst);

#endif  /*_TRAN_HIBER_H*/
