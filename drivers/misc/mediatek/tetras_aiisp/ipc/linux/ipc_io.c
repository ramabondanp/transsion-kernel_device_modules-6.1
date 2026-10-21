/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Change Logs:
 * Date           Author           Notes
 * 2022-2-24      Zhu Shiqiang     Initialize.
 */

/**
 * @brief   ipc io api
 * @date    2021-12-24
 */

#include "spi2ahb_io.h"
#include "sdio2axi_io.h"
#include "ipc_printk.h"
#include "ipc_io.h"

#define READ_ERR_DATA			0xffffffff
#define SPIBRIDGE_LENGTH_ALIGN(len)	(((len) / sizeof(uint32_t)) * sizeof(uint32_t))

extern uint32_t bridge_mode;
struct bridge_ops {
	int (*read)(u32 addr, u8 *data, u32 nbytes);
	int (*write)(u32 addr, const u8 *data, u32 nbytes);
};

static const struct bridge_ops g_bridge_ops[IPC_BRIDGE_MAX] = {
	{
		.read = spi_bridge_read,
		.write = spi_bridge_write,
	},
	{
		.read = sdio_bridge_read,
		.write = sdio_bridge_write,
	},
};

uint32_t ipc_read(uint32_t addr)
{
	int ret;

	uint32_t data = 0;

	/* read 4 bytes once */
	ret = g_bridge_ops[bridge_mode].read(addr, (uint8_t *)&data, sizeof(data));
	if (ret) {
		ipc_err("ipc read: 0x%08x falied\n", addr);
		return READ_ERR_DATA;
	}

	return data;
}

void ipc_write(uint32_t val, uint32_t addr)
{
	uint32_t data = val;

	/* write 4 bytes once */
	g_bridge_ops[bridge_mode].write(addr, (uint8_t *)&data, sizeof(data));
}

int ipc_read_nbytes(uint32_t addr, uint32_t nbytes, uint8_t *readout)
{
	int ret = 0;

	ret = g_bridge_ops[bridge_mode].read(addr, readout, SPIBRIDGE_LENGTH_ALIGN(nbytes));
	if (ret != 0) {
		ipc_err("ipc read: 0x%08x falied\n", addr);
		return 0;
	}

	return SPIBRIDGE_LENGTH_ALIGN(nbytes);
}
EXPORT_SYMBOL(ipc_read_nbytes);

int ipc_write_nbytes(uint32_t addr, uint32_t nbytes, uint8_t *writein)
{
	int ret = 0;

	ret = g_bridge_ops[bridge_mode].write(addr, (uint8_t *)writein,
					      SPIBRIDGE_LENGTH_ALIGN(nbytes));
	if (ret != 0) {
		ipc_err("ipc read: 0x%08x falied\n", addr);
		return 0;
	}

	return SPIBRIDGE_LENGTH_ALIGN(nbytes);
}
EXPORT_SYMBOL(ipc_write_nbytes);
