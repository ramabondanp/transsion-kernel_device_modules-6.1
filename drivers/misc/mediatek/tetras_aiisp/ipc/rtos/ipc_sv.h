/*
 * (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * Change Logs:
 * Date           Author        Notes
 * 2022-03-23     Zhu Shiqiang  Initialize.
 */

/**
 * @brief   IPC SV Header
 * @date    2022-01-21
 */

#ifndef __IPC_SV_H__
#define __IPC_SV_H__

void ipc_sv_init(struct ipc_dev *dev);
void ipc_sv_work_mode_set(uint32_t ack_mode);

#endif /* __IPC_SV_H__ */
