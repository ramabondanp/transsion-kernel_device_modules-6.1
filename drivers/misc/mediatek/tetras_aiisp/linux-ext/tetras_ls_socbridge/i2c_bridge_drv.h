/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author         Notes
 * 2023-04-28     yangyuzun      Initialize.
 */

/**
 * @brief
 * @date    2023-04-28
 */

#ifndef __I2C_BRIDGE_DRV_H__
#define __I2C_BRIDGE_DRV_H__

struct i2cbridge_priv {
	struct ls_bridge_ops *ops;
	/* sturct i2c_msg buf len limit */
	u32 max_buf_len;
};

int i2c_bridge_read(u32 axi_addr, u8 *data, u32 data_len);
int i2c_bridge_write(u32 axi_addr, u8 *data, u32 data_len);
#endif /* __I2C_BRIDGE_DRV_H__ */
