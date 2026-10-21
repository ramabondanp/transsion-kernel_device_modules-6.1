/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Change Logs:
 * Date           Author         Notes
 * 2023-4-20     yanghua     Initialize.
 */

/**
 * @brief   AI_ISP_PMCTRL kernel api
 * @date    2023-06-02
 */

#ifndef _AI_ISP_PMCTRL_IO_H
#define _AI_ISP_PMCTRL_IO_H

/*
 * AI-ISP notifier events.
 *
 * RESET_PREPARE	Ai isp is going to reset.
 * RESET_DONE		Ai isp reset is done.
 * SUSPEND_PREPARE	Ai isp is going to suspend.
 */

#define AIISP_PM_EVENT_RESET_PREPARE		0x01
#define AIISP_PM_EVENT_RESET_DONE		0x02
#define AIISP_PM_EVENT_SUSPEND_PREPARE		0x04

int aiisp_pm_register_notifier(struct notifier_block *nb);
int aiisp_pm_unregister_notifier(struct notifier_block *nb);
void ai_isp_sw_wakeup_report(void);
bool is_chip_power_on(void);

#endif /* _AI_ISP_PMCTRL_IO_H */
