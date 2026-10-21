/*
 * (C) Copyright 2022, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * Change Logs:
 * Date           Author           Notes
 * 2022-02-24     Zhu Shiqiang     Initialize.
 */

/**
 * @brief   ipc config header file
 * @date    2022-02-24
 */

#ifndef __IPC_CONFIG_H__
#define __IPC_CONFIG_H__

#include "ipc_basic.h"

#define IPC_BASE		0x46020000
#ifndef CRG_SC_BASE
#define CRG_SC_BASE		0x46011000
#endif
#define AON_RST_EN1		(CRG_SC_BASE + 0x618)
#define AON_RST_DIS1		(CRG_SC_BASE + 0x61C)

/* commong part */
#define IPC_MBOX0		0x0
#define IPC_MBOX1		0x1
#define IPC_MBOX2		0x2
#define IPC_MBOX3		0x3

/*
 * ipcm interrupt line 0/1 connected to cortex-M7
 * ipcm interrupt line 2 connected to Qcom gpio
 */
#define IPC_INT_LINE0		0x0
#define IPC_INT_LINE1		0x1
#define IPC_INT_LINE2		0x2

#ifdef RTT_IPC
#define IPC_INT_LINE		IPC_INT_LINE0
#else
#define IPC_INT_LINE		IPC_INT_LINE2
#endif

/* source id and destion id defination */
enum master_id {
	IPC_MASTER_AP = (0x1 << IPC_INT_LINE2),
	IPC_MASTER_M7_0 = (0x1 << IPC_INT_LINE0),
	IPC_MASTER_M7_1 = (0x1 << IPC_INT_LINE1),
};

/*
 * ipc configuration
 * note: this configuration is for project, should be located in project dir or
 * registered from user space and maintained by host
 */
#define IPC_CHAN_CONFIGS {			\
	[0] = {					\
		.name = "m7_2_qcom",		\
		.mboxid = IPC_MBOX2,		\
		.srcid = IPC_MASTER_M7_0,	\
		.dstid = IPC_MASTER_AP,		\
	},					\
	[1] = {					\
		.name = "m7_2_ap",		\
		.mboxid = IPC_MBOX1,		\
		.srcid = IPC_MASTER_M7_0,	\
		.dstid = IPC_MASTER_AP,		\
	},					\
	[2] = {					\
		.name = "qcom_2_m7",		\
		.mboxid = IPC_MBOX0,		\
		.srcid = IPC_MASTER_AP,		\
		.dstid = IPC_MASTER_M7_0,	\
	},					\
	[3] = {					\
		.name = "ap_2_m7",		\
		.mboxid = IPC_MBOX3,		\
		.srcid = IPC_MASTER_AP,		\
		.dstid = IPC_MASTER_M7_0,	\
	}					\
}
#endif /* __IPC_CONFIG_H__ */
