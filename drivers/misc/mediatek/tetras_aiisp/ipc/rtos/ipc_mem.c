/*
 * (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * Change Logs:
 * Date           Author        Notes
 * 2022-02-16     Zhu Shiqiang  Initialize.
 */

/**
 * @brief   IPC Dev Mem
 * @date    2022-02-16
 */

#include "ipc_printk.h"

#define IPC_DR_SIZE	16

void *ipc_memcpy(void *dst, const void *src, uint32_t cnt, uint32_t mode)
{
	int i = 0;
	uint32_t *d;
	const uint32_t *s;

	d = (uint32_t *)dst;
	s = (uint32_t *)src;

	if (cnt != IPC_DR_SIZE) {
		ipc_err("ipc_memcpy paramters errors\n");
		return RT_NULL;
	}

	for (i = 0; i < cnt / 0x4; i++)
		*d++ = *s++;

	return d;
}
