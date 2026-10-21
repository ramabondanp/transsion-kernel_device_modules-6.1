/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note
 *
 * Change Logs:
 * Date           Author           Notes
 * 2022-02-24     Zhu Shiqiang     Initialize.
 */

/**
 * @brief   ipc io header file
 * @date    2022-02-24
 */

#ifndef __IPC_IO_H__
#define __IPC_IO_H__

enum {
    IPC_BRIDGE_INVAL = -1,
    IPC_BRIDGE_SPI = 0,
    IPC_BRIDGE_SDIO,
    IPC_BRIDGE_MAX
};

uint32_t ipc_read(uint32_t addr);
void ipc_write(uint32_t val, uint32_t addr);

int ipc_read_nbytes(uint32_t addr, uint32_t nbytes, uint8_t *readout);
int ipc_write_nbytes(uint32_t addr, uint32_t nbytes, uint8_t *writein);
#endif /* __IPC_IO_H__ */
