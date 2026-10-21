/*
 * (C) Copyright 2025, Imvision Co., Ltd
 * This file is classified as confidential level C4 within Imvision
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author         Notes
 * 2023-04-28     yangyuzun      Initialize.
 */

/**
 * @brief   header for spi_dridge_drv
 * @date    2023-04-28
 */

#ifndef __SPI_BRIDGE_DRV_H__
#define __SPI_BRIDGE_DRV_H__

struct spi2axi_ctx;

struct spibridge_priv {
	struct spibridge *cur_bridge;
	struct spibridge *bridges[SPI_BRIDGE_NUM];
	int xfer_timedout;
	bool switch_enable;
	u32 switch_reg_addr;
};

struct spibridge {
	const char *name;
	u32 hw_sel;
	struct ls_bridge_ops *ops;
	void *cfg_data;
};

int spi_bridge_write(u32 ahb_addr, const u8 *data, u32 data_len);
int spi_bridge_read(u32 ahb_addr, u8 *data, u32 data_len);
int sel_cur_spibridge(struct spi_device *spi);
int switch_bridge(struct spi_device *spi, u32 bridge_id);
u32 get_cur_bridge_id(struct spibridge_priv *priv);
int spibridge_speed_op(struct spi_device *spi, struct ls_bridge_speed_msg *msg);
int spibridge_set_mode(struct spi_device *spi, u32 mode);
int spi2axi_tunning(struct device *dev);
int spi2axi_bus_tuning(struct spi_device *spi, struct spi2axi_ctx *data);
int spi2axi_speed_op(struct device *dev, struct ls_bridge_speed_msg *msg);
int spi2axi_set_mode(struct device *dev, u32 mode);
#endif /* __SPI_BRIDGE_DRV_H__ */
