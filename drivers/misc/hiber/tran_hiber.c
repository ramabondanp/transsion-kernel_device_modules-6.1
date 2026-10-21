#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/genetlink.h>
#include "tran_hiber.h"

#define NETLINK_PORT        (0x15356)

#define NETLINK_TRAN_FREEZE       28

static struct sock *sock_handle = NULL;
static atomic_t hiber_deamon_port;

bool g_hiber_debug = false;

static void dump_message(char* dumpTag, struct hiber_message *msg)
{
    pr_err("%s [%s] type=%d, port=%d, caller_uid=%d, caller_pid=%d, target_pid=%d" \
            ", target_uid=%d, pkg_cmd=%d, code=%d, rpc_name=%s.\n",
            TAG, dumpTag, msg->type, msg->port, msg->caller_uid, msg->caller_pid,
            msg->target_pid, msg->target_uid, msg->pkg_cmd, msg->code, msg->rpc_name);
}

int hiber_report(enum message_type type, int caller_pid, int caller_uid,
        int target_pid, int target_uid, const char *rpc_name, int code)
{
    int len = 0;
    int ret = 0;
    struct hiber_message *data = NULL;
    struct sk_buff *skb = NULL;
    struct nlmsghdr *nlh = NULL;

    if (atomic_read(&hiber_deamon_port) == -1) {
        pr_err("%s %s: hiber_deamon_port invalid!\n", TAG, __func__);
        return HIBER_ERROR;
    }

    if (sock_handle == NULL) {
        pr_err("%s: sock_handle invalid!\n", __func__);
        return HIBER_ERROR;
    }

    if (type >= TYPE_MAX) {
        pr_err("%s: type = %d invalid!\n", __func__, type);
        return HIBER_ERROR;
    }

    len = sizeof(struct hiber_message);
    skb = nlmsg_new(len, GFP_ATOMIC);
    if (skb == NULL) {
        pr_err("%s: type =%d, nlmsg_new failed!\n", __func__, type);
        return HIBER_ERROR;
    }

    nlh = nlmsg_put(skb, 0, 0, 0, len, 0);
    if (nlh == NULL) {
        pr_err("%s: type =%d, nlmsg_put failed!\n", __func__, type);
        kfree_skb(skb);
        return HIBER_ERROR;
    }

    data = nlmsg_data(nlh);
    if(data == NULL) {
        pr_err("%s: type =%d, nlmsg_data failed!\n", __func__, type);
        return HIBER_ERROR;
    }
    data->type = type;
    data->port = NETLINK_PORT;
    data->caller_pid = caller_pid;
    data->caller_uid = caller_uid;
    data->target_pid = target_pid;
    data->target_uid = target_uid;
    data->pkg_cmd = -1;
    data->code = code;
    strlcpy(data->rpc_name, rpc_name, INTERFACETOKEN_BUFF_SIZE);
    nlmsg_end(skb, nlh);

    if (g_hiber_debug) {
        dump_message("hiber_report", data);
    }

    if ((ret = nlmsg_unicast(sock_handle, skb, (u32)atomic_read(&hiber_deamon_port))) < 0) {
        pr_err("%s %s: nlmsg_unicast failed! err = %d\n", TAG, __func__ , ret);
        return HIBER_ERROR;
    }

    return HIBER_NOERROR;
}

static void hiber_handler(struct sk_buff *skb)
{
    struct hiber_message *data = NULL;
    struct nlmsghdr *nlh = NULL;
    unsigned int len  = 0;
    int uid = -1;

    if (!skb) {
        pr_err("%s %s: recv skb NULL!\n", TAG, __func__);
        return;
    }

    uid = (*NETLINK_CREDS(skb)).uid.val;

    if (skb->len >= NLMSG_SPACE(0)) {
        nlh = nlmsg_hdr(skb);
        len = NLMSG_PAYLOAD(nlh, 0);
        data = (struct hiber_message *)NLMSG_DATA(nlh);

        if (g_hiber_debug) {
            dump_message("hiber_handler", data);
        }

        if (len < sizeof(struct hiber_message)) {
            pr_err("%s %s: hiber_message len check faied! len = %d	min_expected_len = %lu!\n",
                    TAG, __func__, len, sizeof(struct hiber_message));
            return;
        }

        if (data->port < 0) {
            pr_err("%s %s: portid = %d invalid!\n", TAG, __func__, data->port);
            return;
        }

        if (data->type >= TYPE_MAX) {
            pr_err("%s %s: type = %d invalid!\n", TAG, __func__, data->type);
            return;
        }

        if (atomic_read(&hiber_deamon_port) == -1 && data->type != LOOP_BACK) {
            pr_err("%s %s: handshake not setup, type = %d!\n", TAG, __func__, data->type);
            return;
        }

        switch (data->type) {
        case LOOP_BACK:
            atomic_set(&hiber_deamon_port, data->port);
            hiber_report(LOOP_BACK, -1, -1, -1, -1, "loop back", -1);
            printk(KERN_ERR "%s %s: --> LOOP_BACK, port = %d\n", TAG, __func__, data->port);
            break;

        case PKG:
            printk(KERN_ERR "%s %s: --> PKG, uid = %d, pkg_cmd = %d\n", TAG, __func__,
                    data->target_uid, data->pkg_cmd);
            hiber_network_cmd_parse(data->target_uid, data->pkg_cmd);
            break;

        case FROZEN_TRANS:
            printk(KERN_ERR "%s %s: --> FROZEN_TRANS, uid = %d\n", TAG, __func__, data->target_uid);
            hiber_check_frozen_transcation(data->target_uid, data->type);
            break;

       case FROZEN_SYNC_TRANS:
            printk(KERN_ERR "%s %s: --> FROZEN_SYNC_TRANS, uid = %d\n", TAG, __func__, data->target_uid);
            hiber_check_frozen_sync_transcation(data->target_uid, data->type);
            break;

        default:
            pr_err("%s %s: hiber_messag type invalid (%d)\n", TAG, __func__, data->type);
            break;
        }
    }
}

int register_hiber_vendor_hooks(void)
{
    int rc = 0;

    rc = register_trace_android_vh_binder_preset(binder_preset_handler, NULL);
    if (rc != 0) {
        pr_err("%s register_trace_android_vh_binder_preset failed, rc=%d\n", TAG, rc);
        return rc;
    }

    rc = register_trace_android_vh_binder_trans(binder_trans_handler, NULL);
    if (rc != 0) {
        pr_err("%s register_trace_android_vh_binder_trans failed, rc=%d\n", TAG, rc);
        return rc;
    }

    rc = register_trace_android_vh_binder_reply(binder_reply_handler, NULL);
    if (rc != 0) {
        pr_err("%s register_trace_android_vh_binder_reply failed, rc=%d\n", TAG, rc);
        return rc;
    }

    rc = register_trace_android_vh_binder_alloc_new_buf_locked(binder_alloc_handler, NULL);
	if (rc != 0) {
		pr_err("register_trace_android_vh_binder_alloc_new_buf_locked failed, rc=%d\n",
		       rc);
		return rc;
	}

    rc = register_trace_android_vh_do_send_sig_info(send_signal_handler, NULL);
    if (rc != 0) {
        pr_err("%s register_trace_android_vh_do_send_sig_info failed, rc=%d\n", TAG, rc);
        return rc;
    }

    return rc;
}

void unregister_hiber_vendor_hooks(void)
{
    unregister_trace_android_vh_binder_preset(binder_preset_handler, NULL);
    unregister_trace_android_vh_binder_trans(binder_trans_handler, NULL);
    unregister_trace_android_vh_binder_reply(binder_reply_handler, NULL);
    unregister_trace_android_vh_binder_alloc_new_buf_locked(binder_alloc_handler, NULL);
    unregister_trace_android_vh_do_send_sig_info(send_signal_handler, NULL);
}

static int __init hiber_core_init(void)
{
    struct netlink_kernel_cfg cfg = {
        .input = hiber_handler,
    };

    atomic_set(&hiber_deamon_port, -1);

    sock_handle = netlink_kernel_create(&init_net, NETLINK_TRAN_FREEZE, &cfg);
    if (sock_handle == NULL) {
        pr_err("%s: create netlink socket failed!\n", __func__);
        return HIBER_ERROR;
    }

    if (register_hiber_vendor_hooks() != 0) {
        pr_err("%s %s: vendor hook register failed!\n", TAG, __func__);
        netlink_kernel_release(sock_handle);
        return HIBER_ERROR;
    }

    atomic_set(&hiber_deamon_port, -1);

    if (hiber_netfilter_init() == HIBER_ERROR) {
        pr_err("%s %s: netfilter init failed!\n", TAG, __func__);
        netlink_kernel_release(sock_handle);
        return HIBER_ERROR;
    }

    printk(KERN_INFO "%s: init hiber on port (%d)-\n", TAG, NETLINK_TRAN_FREEZE);
    return HIBER_NOERROR;
}

static void __exit hiber_core_exit(void)
{
    if (sock_handle)
        netlink_kernel_release(sock_handle);

    unregister_hiber_vendor_hooks();
    hiber_netfilter_deinit();
    printk(KERN_INFO "%s: exit hiber on port (%d)-\n", TAG, NETLINK_TRAN_FREEZE);
}

module_init(hiber_core_init);
module_exit(hiber_core_exit);
MODULE_LICENSE("GPL");

