/*
 * (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * Change Logs:
 * Date           Author        Notes
 * 2022-01-21     Zhu Shiqiang  Initialize.
 */

/**
 * @brief   IPC MEM Interface
 * @date    2022-01-21
 */

#ifndef __IPC_MEM_H__
#define __IPC_MEM_H__

#define ipc_memset(a, b, c)			rt_memset(a, b, c)
#define ipc_malloc(a)				rt_malloc(a)
#define ipc_free(a)				rt_free(a)

enum ipc_mem_mode {
	NONE_MODE,
	WRITE_MODE,
	READ_MODE,
	MODE_MAX
};

void *ipc_memcpy(void *dst, const void *src, uint32_t cnt, uint32_t mode);

#endif /* __IPC_MEM_H__ */
