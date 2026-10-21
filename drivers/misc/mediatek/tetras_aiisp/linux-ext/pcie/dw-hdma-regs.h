/*
 * (C) Copyright 2021-2022, Shenzhen Tetras.AI Technology Co., Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Refer to linux dw-edma driver - "drivers\dma\dw-edma"
 *
 * Change Logs:
 * Date           Author         Notes
 * 2022-06-30     ShenJiming     Initialize.
 */

/**
 * @brief   Tetras PCIe HDMA device driver
 * @date    2022-06-30
 */

#ifndef _DW_HDMA_REGS_H
#define _DW_HDMA_REGS_H

#include <linux/dmaengine.h>

#define HDMA_MAX_NR_CH					4

/* HDMA_ENABLE_OFF_WRCH */
#define HDMA_CH_EN					BIT(0)

/* HDMA_STATUS_OFF_WRCH */
#define HDMA_CH_STATUS_MASK				GENMASK(2, 0)
#define  HDMA_CH_STATUS_RUNNING				1
#define  HDMA_CH_STATUS_ABORTED				2
#define  HDMA_CH_STATUS_STOPPED				3

/* HDMA_INT_STATUS_OFF_WRCH */
#define HDMA_CH_INT_ERROR_MASK				GENMASK(6, 3)
#define  HDMA_CH_INT_ERR_LL_CPL_UR			1
#define  HDMA_CH_INT_EER_LL_CPL_CA			2
#define  HDMA_CH_INT_EER_LL_CPL_EP			3
#define  HDMA_CH_INT_EER_DATA_CPL_TIMEOUT		8
#define  HDMA_CH_INT_EER_DATA_CPL_UR			9
#define  HDMA_CH_INT_EER_DATA_CPL_CA			0xa
#define  HDMA_CH_INT_EER_DATA_CPL_EP			0xb
#define  HDMA_CH_INT_EER_DATA_MWR			0xc
#define HDMA_CH_INT_ABORT				BIT(2)
#define HDMA_CH_INT_WATERMARK				BIT(1)
#define HDMA_CH_INT_STOP				BIT(0)

/* HDMA_INT_SETUP_OFF_WRCH */
#define HDMA_CH_INT_EN_MASK				GENMASK(6, 3)
#define  HDMA_CH_LOCAL_ABORT_INT_EN			BIT(6)
#define  HDMA_CH_REMOTE_ABORT_INT_EN			BIT(5)
#define  HDMA_CH_LOCAL_STOP_INT_EN			BIT(4)
#define  HDMA_CH_REMOTE_STOP_INT_EN			BIT(3)
#define HDMA_CH_LOCAL_INT_MASK				GENMASK(2, 0)
#define  HDMA_CH_LOCAL_INT_ABORT_MASK			BIT(2)
#define  HDMA_CH_LOCAL_INT_WATERMARK_MASK		BIT(1)
#define  HDMA_CH_LCOAL_INT_STOP_MASK			BIT(0)

/* HDMA_DOORBELL_OFF_WRCH */
#define HDMA_CH_DB_STOP					BIT(1)
#define HDMA_CH_DB_START				BIT(0)

/* HDMA_CYCLE_OFF_WRCH */
#define HDMA_CH_CYCLE_STATE				BIT(1)
#define HDMA_CH_CYCLE_BIT				BIT(0)

/* HDMA_WATERMARK_EN_OFF_WRCH */
#define HDMA_CH_WATERMARK_LWIE				BIT(1)
#define HDMA_CH_WATERMARK_RWIE				BIT(0)

/* HDMA_CONTROL1_OFF_WRCH */
#define HDMA_CH_CONTROL1_LLE				BIT(0)

/* HDMA channel context grouping */
struct dw_hdma_ch_regs {
	u32 ch_enable;					/* 0x000 */
	u32 ch_doorbell;				/* 0x004 */
	u32 ch_prefetch;				/* 0x008 */
	u32 ch_handshake;				/* 0x00c */
	u32 llp_low;					/* 0x010 */
	u32 llp_high;					/* 0x014 */
	u32 ch_cycle;					/* 0x018 */
	u32 ch_xfer_size;				/* 0x01c */
	u32 sar_low;					/* 0x020 */
	u32 sar_high;					/* 0x024 */
	u32 dar_low;					/* 0x028 */
	u32 dar_high;					/* 0x02c */
	u32 ll_watermark_en;				/* 0x030 */
	u32 ch_ctrl1;					/* 0x034 */
	u32 ch_func_num;				/* 0x038 */
	u32 ch_qos;					/* 0x03c */
	u32 padding_1[16];				/* [0x40..0x7c] */
	u32 ch_status;					/* 0x080 */
	u32 int_status;					/* 0x084 */
	u32 int_setup;					/* 0x088 */
	u32 int_clear;					/* 0x08c */
	u32 msi_stop_low;				/* 0x090 */
	u32 msi_stop_high;				/* 0x094 */
	u32 msi_watermark_low;				/* 0x098 */
	u32 msi_watermark_high;				/* 0x09c */
	u32 msi_abort_low;				/* 0x0a0 */
	u32 msi_abort_high;				/* 0x0a4 */
	u32 msi_msgd;					/* 0x0a8 */
	u32 padding_2[21];				/* [0xac..0xfc] */
};

struct dw_hdma_ch {
	struct dw_hdma_ch_regs wr;			/* [0x000..0x0fc] */
	struct dw_hdma_ch_regs rd;			/* [0x100..0x1fc] */
};

struct dw_hdma_regs {
	struct dw_hdma_ch ch[HDMA_MAX_NR_CH];	/* [0x000..0x7fc] */
};

struct dw_hdma_lli {
	u32 control;
	u32 transfer_size;
	u32 sar_low;
	u32 sar_high;
	u32 dar_low;
	u32 dar_high;
};

struct dw_hdma_llp {
	u32 control;
	u32 reserved;
	u32 llp_low;
	u32 llp_high;
};

#endif /* _DW_HDMA_REGS_H */
