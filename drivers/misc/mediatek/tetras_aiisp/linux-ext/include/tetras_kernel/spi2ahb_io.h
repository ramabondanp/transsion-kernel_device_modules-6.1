/*
 * (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 * Change Logs:
 * Date           Author         Notes
 * 2022-01-20     yanghua     Initialize.
 */

/**
 * @brief   Spi2ahb kernel header file
 * @date    2021-01-20
 */

#ifndef _SPI2AHB_IO_H
#define _SPI2AHB_IO_H

#include <linux/types.h>
/**
 * @brief write an amount of 32-bit data via spi2ahb to isp soc.
 * @param ahb		addr the ahb addr to write.
 * @param data		write buffer start address
 * @data_len		the number of words to write
 * @return		success ?
 * @ retval 0		ok
 * @ retval others	err
 */
int spi_bridge_write(u32 ahb_addr, const u8 *data, u32 data_len);

/**
 * @brief read an amount of 32-bit data via spi2ahb from isp soc.
 * @param ahb		addr the ahb addr to read.
 * @param data		read buffer start address.
 * @data_len		the number of words to read
 * @read_flag		read method
 * @return		success ?
 * @ retval 0		ok
 * @ retval others	err
 */
int spi_bridge_read(u32 ahb_addr, u8 *data, u32 data_len);
#endif
