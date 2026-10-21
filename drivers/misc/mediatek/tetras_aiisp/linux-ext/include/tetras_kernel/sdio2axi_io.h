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
 * @brief   SDIO2AXI kernel api
 * @date    2023-04-24
 */

#ifndef _SDIO2AXI_IO_H
#define _SDIO2AXI_IO_H

#include <linux/types.h>
int sdio_bridge_write(u32 addr, const u8 *data, u32 nbytes);
int sdio_bridge_read(u32 addr, u8 *data, u32 nbytes);

#endif /* _SDIO2AXI_IO_H */
