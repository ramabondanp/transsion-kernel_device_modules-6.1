/*
 * (C) Copyright 2021-2022, Shenzhen Tetras.AI Technology Co., Ltd
 *
 * Change Logs:
 * Date           Author         Notes
 * 2022-01-11     suntongce      Initialize.
 */

/**
 * @brief   Tetras I2C2APB/I3C2APB common uapi_io driver
 * @date    2022-01-11
 */

#ifndef __I2C2APB_IO_H__
#define __I2C2APB_IO_H__

#include <linux/types.h>
/**
 * @brief write an amount of 32-bit data via ixc2apb to isp soc.
 * @param apb		addr the apb addr to write.
 * @param data		write buffer start address
 * @data_len		the number of words to write
 * @return		success ?
 * @ retval 0		ok
 * @ retval others	err
 */
int ixc_bridge_write(u32 apb_addr, const u32 *data, u32 data_len);

/**
 * @brief read an amount of 32-bit data via ixc2apb from isp soc.
 * @param apb		addr the apb addr to read.
 * @param data		read buffer start address.
 * @data_len		the number of words to read
 * @read_flag		read method
 * @return		success ?
 * @ retval 0		ok
 * @ retval others	err
 */
int ixc_bridge_read(u32 apb_addr, u32 *data, u32 data_len, u32 read_flag);

#endif /* __I2C2APB_IO_H__ */
