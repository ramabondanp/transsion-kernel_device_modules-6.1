/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * Change Logs:
 * Date           Author        Notes
 * 2022-01-21     Zhu Shiqiang  Initialize.
 */

/**
 * @brief   IPC IO Interface
 * @date    2022-01-21
 */

#ifndef __IPC_IO_H__
#define __IPC_IO_H__

#include <io.h>

#define ipc_read(x)     read32(x)
#define ipc_write(val, addr)    write32((val), (addr))

static inline int ipc_read_nbytes(uint32_t addr, uint32_t nbytes, uint8_t *readout)
{
	uint32_t i;
	uint32_t n_align = (nbytes / sizeof(uint32_t)) * sizeof(uint32_t);
	uint32_t n_remain = nbytes % sizeof(uint32_t);

	for (i = 0; i < n_align; i += sizeof(uint32_t)) {
		*(uint32_t *)(readout + i) = ipc_read(addr + i);
	}

	if (!n_remain)
		return nbytes;

	for (i = n_align; i < nbytes; i++) {
		*(readout + i) = read8(addr + i);
	}

	return nbytes;
}

static inline int ipc_write_nbytes(uint32_t addr, uint32_t nbytes, uint8_t *writein)
{
	uint32_t i;
	uint32_t n_align = (nbytes / sizeof(uint32_t)) * sizeof(uint32_t);
	uint32_t n_remain = nbytes % sizeof(uint32_t);

	for (i = 0; i < n_align; i += sizeof(uint32_t)) {
		ipc_write(*(uint32_t *)(writein + i), addr + i);
	}

	if (!n_remain)
		return nbytes;

	for (i = n_align; i < nbytes; i++) {
		write8(*(writein + i), addr + i);
	}

	return nbytes;
}
#endif /* __IPC_IO_H__ */
