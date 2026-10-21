/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) <2023> Transsion Inc.
 */

#ifndef __TRAN_DISPLAY_H__
#define __TRAN_DISPLAY_H__
#include "mtk_dsi.h"

#define DDP_DSI_COUNT 2

enum DDP_DSI_NUM {
	DDP_DSI0 = 0,
	DDP_DSI1,
	DDP_DSI_MAX
};

void tran_dsi_init(struct mtk_dsi *dsi);
struct mtk_dsi *tran_get_dsi(enum DDP_DSI_NUM index);
int mtkfb_register_kernelfs(struct device *dev);
int mtkfb_register_sub_kernelfs(struct device *dev);
#endif
