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

#ifndef _DW_HDMA_DEBUG_FS_H
#define _DW_HDMA_DEBUG_FS_H

#include "dw-hdma-drv.h"

#ifdef CONFIG_DEBUG_FS
void dw_hdma_debugfs_on(struct dw_hdma_chip *chip);
void dw_hdma_debugfs_off(struct dw_hdma_chip *chip);
#else
static inline void dw_hdma_debugfs_on(struct dw_hdma_chip *chip)
{
}

static inline void dw_hdma_debugfs_off(struct dw_hdma_chip *chip)
{
}
#endif /* CONFIG_DEBUG_FS */

#endif /* _DW_HDMA_DEBUG_FS_H */
