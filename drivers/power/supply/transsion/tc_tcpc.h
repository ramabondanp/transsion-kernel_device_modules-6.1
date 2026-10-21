// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#ifndef LINUX_TC_TCPC_H
#define LINUX_TC_TCPC_H
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "tc_common_class.h"

struct tc_tcpc_vbus_state {
	int mv;
	int ma;
	uint8_t type;
};

struct tc_tcpc_noti {
	struct tc_tcpc_vbus_state vbus_state;
	int pd_type;
	int cc1_status;
	int cc2_status;
};

enum {
	TC_PD_CONNECT_NONE = 0,
	TC_PD_CONNECT_PE_READY_SNK,
	TC_PD_CONNECT_PE_READY_SNK_PD30,
	TC_PD_CONNECT_PE_READY_SRC_PD30,
	TC_PD_CONNECT_PE_READY_SNK_APDO,
	TC_PD_CONNECT_TYPEC_ONLY_SNK,
	TC_PD_CONNECT_TIMEOUT,
	TC_PD_CONNECT_MAX,
};

enum {
	/* tcpc */
	TC_PD_SRC_TO_SNK = 0,
	TC_PD_SNK_TO_SRC,
	TC_PD_TYPE,
	TC_PD_CONNECT_HARD_RESET,
	TC_TYPEC_WD_STATUS,
	TC_TYPEC_HRESET_STATUS,
	TC_TYPEC_USB_PLUG_IN,
	TC_TYPEC_USB_PLUG_OUT,
	TC_TYPEC_OTG_PLUG_IN,
	TC_TYPEC_OTG_PLUG_OUT,
	TC_TYPEC_AUDIO_PLUG_IN,
	TC_TYPEC_AUDIO_PLUG_OUT,
	TC_TYPEC_SNK_VBUS,
	TC_TYPEC_SRC_VBUS,
};

enum tc_typec_role_defination {
	TC_TYPEC_ROLE_UNKNOWN = 0,
	TC_TYPEC_ROLE_SNK,
	TC_TYPEC_ROLE_SRC,
	TC_TYPEC_ROLE_DRP,
	TC_TYPEC_ROLE_TRY_SRC,
	TC_TYPEC_ROLE_TRY_SNK,
	TC_TYPEC_ROLE_NR,
};

extern bool tc_tcpc_detect_dual_rp_cable(void);
extern bool tc_tcpc_detect_rp_ra_cable(void);
extern int tc_typec_change_role_postpone(
	struct tran_device *dev, uint8_t typec_role, bool postpone);
extern int register_tc_tcpc_notifier(struct notifier_block *nb);
extern int unregister_tc_tcpc_notifier(struct notifier_block *nb);
extern bool tc_maybe_dock(void);
extern const char *const tc_pd_type_tostring(int type);
#endif
