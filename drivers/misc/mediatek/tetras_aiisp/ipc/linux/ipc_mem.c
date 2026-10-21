/*
 * (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * Change Logs:
 * Date           Author           Notes
 * 2022-2-24      Zhu Shiqiang     Initialize.
 */

/**
 * @brief   ipc mem api
 * @date    2021-12-24
 */

#include <linux/vmalloc.h>
#include "ipc_printk.h"
#include "ipc_mem.h"
#include "ipc_io.h"

#define IPC_WR_LEN	0x10
#define IPC_RD_LEN	0x10

void *ipc_memcpy(void *dst, void *src, uint32_t cnt, uint32_t mode)
{
	if (mode == READ_MODE) {
		ipc_write_nbytes((uintptr_t)dst, IPC_WR_LEN, (uint8_t *)src);
	} else if (mode == WRITE_MODE) {
		ipc_read_nbytes((uintptr_t)src, IPC_RD_LEN, (uint8_t *)dst);
	} else if (mode == NONE_MODE) {
		memcpy(dst, src, cnt);
	} else {
		ipc_err("ipc: mode:%d error\n", mode);
		return NULL;
	}

	return dst;
}

void ipc_memset(void *dst, int c, uint32_t cnt)
{
	memset(dst, c, cnt);
}

void *ipc_malloc(uint32_t size)
{
	return vmalloc(size);
}

void ipc_free(void *rmem)
{
	vfree(rmem);
}
