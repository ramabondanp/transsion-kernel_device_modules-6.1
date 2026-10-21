/*
 * (C) Copyright 2021-2022, Shenzhen Tetras.AI Technology Co., Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Refer to linux dw-edma driver - "drivers\dma\dw-edma\"
 *
 * Change Logs:
 * Date           Author         Notes
 * 2022-06-30     ShenJiming     Initialize.
 */

/**
 * @brief   Tetras PCIe HDMA device driver
 * @date    2022-06-30
 */

#ifndef _DW_HDMA_CORE_H
#define _DW_HDMA_CORE_H

/* HDMA management callbacks */
void dw_hdma_core_off(struct dw_hdma *chan);
enum dma_status dw_hdma_core_ch_status(struct dw_hdma_chan *chan);
void dw_hdma_core_clear_done_int(struct dw_hdma_chan *chan);
void dw_hdma_core_clear_abort_int(struct dw_hdma_chan *chan);
void dw_hdma_core_clear_watermark_int(struct dw_hdma_chan *chan);
u32 dw_hdma_core_status_done_int(struct dw_hdma_chan *chan);
u32 dw_hdma_core_status_abort_int(struct dw_hdma_chan *chan);
u32 dw_hdma_core_status_abort_error(struct dw_hdma_chan *chan);
u32 dw_hdma_core_status_watermark_int(struct dw_hdma_chan *chan);
void dw_hdma_core_start(struct dw_hdma_chunk *chunk, bool first);
int dw_hdma_core_device_config(struct dw_hdma_chan *chan);
/* HDMA debug fs callbacks */
void dw_hdma_core_debugfs_on(struct dw_hdma_chip *chip);
void dw_hdma_core_debugfs_off(struct dw_hdma_chip *chip);

#endif /* _DW_HDMA_CORE_H */
