/*
 * (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * Change Logs:
 * Date           Author           Notes
 * 2022-02-24     Zhu Shiqiang     Initialize.
 */

/**
 * @brief   ipc mem header file
 * @date    2022-02-24
 */

#ifndef __IPC_MEM_H__
#define __IPC_MEM_H__

#include <linux/module.h>

enum ipc_mem_mode {
	NONE_MODE,
	WRITE_MODE,
	READ_MODE,
	MODE_MAX
};

void *ipc_memcpy(void *dst, void *src, uint32_t cnt, uint32_t mode);
void ipc_memset(void *dst, int c, uint32_t cnt);
void *ipc_malloc(uint32_t size);
void ipc_free(void *rmem);

#endif /* __IPC_MEM_H__*/
