/*
 * (C) Copyright 2025, Imvision Co., Ltd
 * This file is classified as confidential level C4 within Imvision
 *
 * Change Logs:
 * Date           Author        Notes
 * 2022-01-21     Zhu Shiqiang  Initialize.
 */

/**
 * @brief   IPCM Common Configuration
 * @date    2022-01-21
 */

#ifndef __IPCM_H__
#define __IPCM_H__

#include "ipc_basic.h"

/* IPCM0 Module */
#define SOURCE_OFFSET		(0x0)
#define DSET_OFFSET		(0x4)
#define DCLEAR_OFFSET		(0x8)
#define DSTATUS_OFFSET		(0xC)
#define MODE_OFFSET		(0x10)
#define MSET_OFFSET		(0x14)
#define MCLEAR_OFFSET		(0x18)
#define MSTATUS_OFFSET		(0x1C)
#define SEND_OFFSET		(0x20)
#define DR0_OFFSET		(0x24)
#define DR1_OFFSET		(0x28)
#define DR2_OFFSET		(0x2C)
#define DR3_OFFSET		(0x30)
#define DR4_OFFSET		(0x34)
#define DR5_OFFSET		(0x38)
#define DR6_OFFSET		(0x3C)

#define IPCM0SOURCE(X)		(IPC_BASE + (X) * 0x40 + SOURCE_OFFSET)
#define IPCM0DSET(X)		(IPC_BASE + (X) * 0x40 + DSET_OFFSET)
#define IPCM0DCLEAR(X)		(IPC_BASE + (X) * 0x40 + DCLEAR_OFFSET)
#define IPCM0DSTATUS(X)		(IPC_BASE + (X) * 0x40 + DSTATUS_OFFSET)
#define IPCM0MODE(X)		(IPC_BASE + (X) * 0x40 + MODE_OFFSET)
#define IPCM0MSET(X)		(IPC_BASE + (X) * 0x40 + MSET_OFFSET)
#define IPCM0MCLEAR(X)		(IPC_BASE + (X) * 0x40 + MCLEAR_OFFSET)
#define IPCM0MSTATUS(X)		(IPC_BASE + (X) * 0x40 + MSTATUS_OFFSET)
#define IPCM0SEND(X)		(IPC_BASE + (X) * 0x40 + SEND_OFFSET)
#define IPCM0DR0(X)		(IPC_BASE + (X) * 0x40 + DR0_OFFSET)
#define IPCM0DR1(X)		(IPC_BASE + (X) * 0x40 + DR1_OFFSET)
#define IPCM0DR2(X)		(IPC_BASE + (X) * 0x40 + DR2_OFFSET)
#define IPCM0DR3(X)		(IPC_BASE + (X) * 0x40 + DR3_OFFSET)

/* irq status regs */
#define MMIS_OFFSET		(0x800)
#define IPCMMIS(X)		(IPC_BASE + MMIS_OFFSET + 0x8 * (X))

#define MRIS_OFFSET		(0x800)
#define IPCMRIS(X)		(IPC_BASE + MRIS_OFFSET + 0x8 * (X) + 0x4)

#define MBOX_DR_SIZE		16

#define IPCM_NO_ACK		0
#define IPCM_MANUAL_ACK		1
#define IPCM_AUTO_ACK		2

#define ACK_HANDLED		1
#define EN_AUTO_ACK		1
#define IPCM_ACK_PATTERN	0xfefefefe

#define TIME_DELAY		25
#define IPCM_TRY_TIMES		200

#define IRQ_TO_DST		(0x1 << 0x0)
#define IRQ_TO_SRC		(0x1 << 0x1)

#define IPCM_RST		(0x1 << 31)

#define IPCM_REG_CTRL_CACHE_SZ	(DR0_OFFSET)

struct ipcm_config {
	uint32_t mboxid;
	uint32_t sourceid;
	uint32_t destid;
	uint32_t size;
	uint32_t ipcm_mode;
	void *mbox_buf;
};

struct ipcm_ctrl_registers {
	uint32_t source;
	uint32_t dset;
	uint32_t dclear;
	uint32_t dstatus;
	uint32_t mode;
	uint32_t mset;
	uint32_t mclear;
	uint32_t mstatus;
	uint32_t send;
	uint32_t dr[4];
};

int ipcm_send(struct ipcm_config *data);
int ipcm_irq_status(void);
int ipcm_irq_clear(uint32_t mboxid, uint32_t ack_mode);
int ipcm_release_and_clear_irq(uint32_t mboxid, uint32_t ack_mode);
void ipcm_send_ack(uint32_t mboxid, uint32_t ack_mode);
void ipcm_release(uint32_t mboxid);
void *ipcm_dr_adr(uint32_t mboxid);
void ipcm_reset(void);

#endif /* __IPCM_H__ */
