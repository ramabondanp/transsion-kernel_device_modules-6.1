/*
 * (C) Copyright 2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note
 *
 * Change Logs:
 * Date           Author         Notes
 * 2023-11-16     Yongming Sha   Initialize.
 */

/**
 * @brief   ai_isp pmctrl ioctl define header file
 * @date    2023-11-16
 */

#ifndef _AI_ISP_PMCTRL_DEF_H
#define _AI_ISP_PMCTRL_DEF_H

/**
 * PMCTRL_CMD_DEVINFO
 * read info data from pmctrl drv
 */
#define PMCTRL_CMD_DEVINFO           _IOR('t', 0, uint32_t)

/**
 * PMCTRL_CMD_RST_REQ
 * read ai_isp reset request from pmctrl drv
 */
#define PMCTRL_CMD_RST_REQ           _IOR('t', 1, uint32_t)

/**
 * PMCTRL_CMD_TCXO_STAT
 * read ai_isp tcxo state from pmctrl drv.
 * 0: low level; 1: high level.
 */
#define PMCTRL_CMD_TCXO_STAT		_IOR('t', 2, uint32_t)
#define TCXO_STAT_DIS			0x0
#define TCXO_STAT_EN			0x1

/**
 * PMCTRL_CMD_RTOS_WK_STAT
 * read ai_isp rtos wakeup state from pmctrl drv.
 * 0: not wakeup from sleep; 1: already wakeup from sleep.
 */
#define PMCTRL_CMD_RTOS_WK_STAT		_IOR('t', 3, uint32_t)
#define RTOS_WAKEUP_STAT_NONE		0x0
#define RTOS_WAKEUP_STAT_ACTIVE		0x1


#define CRASH_RESET_FLAG            (0x1)

typedef void *(*thread_pm_t)(void *par);

/* ai_isp reset request flag */
enum rst_req_state {
	AI_ISP_RST_DEACTIVE = 0,
	AI_ISP_RST_ACTIVE,
};

#endif /* _AI_ISP_PMCTRL_DEF_H */
