/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author         Notes
 * 2023-05-05     yangyuzun      Initialize.
 */

/**
 * @brief   hearer of bridge2axi common fuction
 * @date    2023-05-05
 */
#ifndef __LS_BRIDGE2AXI_COMMON_H__
#define __LS_BRIDGE2AXI_COMMON_H__

uint32_t error_detect(struct device *dev, u32 reg_glb_csr);
int polling_fifo_full(struct device *dev, u32 len);
int polling_xfer_finish(struct device *dev, u32 *glb_csr);
int ls_bridge2axi_glb_csr_setbits(struct device *dev, u32 bits_mask);
int check_scatter(struct device *dev, u32 scatter_num,
		  struct scatter_wr_unit *scatters, u32 *sz);
void scatter_buf_fill(u8 *buf, u32 scatter_num,
		      struct scatter_wr_unit *scatters);
void ls_bridge2axi_reset(struct device *dev);
int ls_bridge2axi_set_trans_mode(struct device *dev, u8 mode);
int ls_bridge2axi_get_version(struct device *dev, u32 *version);
int ls_bridge2axi_scatter_wr(struct device *dev, struct ls_bridge_scatter_msg *msg);
int ls_bridge2axi_ioc_csr(struct device *dev, u8 csr_addr, u32 *val, bool is_write);
int ls_bridge2axi_burst_len_op(struct device *dev, struct ls_bridge_busrt_op *op);
#endif /* __LS_BRIDGE2AXI_COMMON_H__ */
