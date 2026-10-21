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

#include <linux/bitfield.h>

#include "dw-hdma-drv.h"
#include "dw-hdma-core.h"
#include "dw-hdma-regs.h"
#include "dw-hdma-debugfs.h"

enum dw_hdma_control {
	DW_HDMA_V0_CB			= BIT(0),
	DW_HDMA_V0_TCB			= BIT(1),
	DW_HDMA_V0_LLP			= BIT(2),
	DW_HDMA_V0_LWIE			= BIT(3),
	DW_HDMA_V0_RWIE			= BIT(4),
	DW_HDMA_V0_ELEMENT_PREFETCH	= GENMASK(21, 16),
};

static inline struct dw_hdma_regs __iomem *__dw_regs(struct dw_hdma *dw)
{
	return dw->rg_region.vaddr;
}

static inline struct dw_hdma_ch_regs __iomem *
__dw_ch_regs(struct dw_hdma *dw, enum dw_hdma_dir dir, u16 ch)
{
	if (dir == HDMA_DIR_WRITE)
		return &__dw_regs(dw)->ch[ch].wr;

	return &__dw_regs(dw)->ch[ch].rd;
}

#define SET_CH(dw, dir, ch, name, value) \
	writel(value, &(__dw_ch_regs(dw, dir, ch)->name))

#define GET_CH(dw, dir, ch, name) \
	readl(&(__dw_ch_regs(dw, dir, ch)->name))

#define SET_LL(ll, value) \
	writel(value, ll)

enum dma_status dw_hdma_core_ch_status(struct dw_hdma_chan *chan)
{
	struct dw_hdma *dw = chan->chip->dw;
	u32 status;

	status = FIELD_GET(HDMA_CH_STATUS_MASK,
			   GET_CH(dw, chan->dir, chan->id, ch_status));

	if (status == HDMA_CH_STATUS_RUNNING)
		return DMA_IN_PROGRESS;
	else if (status == HDMA_CH_STATUS_STOPPED)
		return DMA_COMPLETE;
	else
		return DMA_ERROR;
}

void dw_hdma_core_clear_done_int(struct dw_hdma_chan *chan)
{
	struct dw_hdma *dw = chan->chip->dw;

	SET_CH(dw, chan->dir, chan->id, int_clear, HDMA_CH_INT_STOP);
}

void dw_hdma_core_clear_abort_int(struct dw_hdma_chan *chan)
{
	struct dw_hdma *dw = chan->chip->dw;

	SET_CH(dw, chan->dir, chan->id, int_clear, HDMA_CH_INT_ABORT);
}

void dw_hdma_core_clear_watermark_int(struct dw_hdma_chan *chan)
{
	struct dw_hdma *dw = chan->chip->dw;

	SET_CH(dw, chan->dir, chan->id, int_clear, HDMA_CH_INT_WATERMARK);
}

u32 dw_hdma_core_status_done_int(struct dw_hdma_chan *chan)
{
	struct dw_hdma *dw = chan->chip->dw;

	return FIELD_GET(HDMA_CH_INT_STOP,
			 GET_CH(dw, chan->dir, chan->id, int_status));
}

u32 dw_hdma_core_status_abort_int(struct dw_hdma_chan *chan)
{
	struct dw_hdma *dw = chan->chip->dw;

	return FIELD_GET(HDMA_CH_INT_ABORT,
			 GET_CH(dw, chan->dir, chan->id, int_status));
}

u32 dw_hdma_core_status_abort_error(struct dw_hdma_chan *chan)
{
	struct dw_hdma *dw = chan->chip->dw;

	return FIELD_GET(HDMA_CH_INT_ERROR_MASK,
			 GET_CH(dw, chan->dir, chan->id, int_status));
}

u32 dw_hdma_core_status_watermark_int(struct dw_hdma_chan *chan)
{
	struct dw_hdma *dw = chan->chip->dw;

	return FIELD_GET(HDMA_CH_INT_WATERMARK,
			 GET_CH(dw, chan->dir, chan->id, int_status));
}

static void dw_hdma_core_write_chunk(struct dw_hdma_chunk *chunk)
{
	struct dw_hdma_burst *child;
	struct dw_hdma_lli __iomem *lli;
	struct dw_hdma_llp __iomem *llp;
	u32 control = 0, i = 0;
	int j;

	lli = chunk->ll_region.vaddr;

	if (chunk->cb)
		control = DW_HDMA_V0_CB;

	j = chunk->bursts_alloc;
	list_for_each_entry(child, &chunk->burst->list, list) {
		j--;
		if (!j)
			control |= (DW_HDMA_V0_LWIE | DW_HDMA_V0_RWIE);

		/* Channel control */
		SET_LL(&lli[i].control, control);
		/* Transfer size */
		SET_LL(&lli[i].transfer_size, child->sz);
		/* SAR - low, high */
		SET_LL(&lli[i].sar_low, lower_32_bits(child->sar));
		SET_LL(&lli[i].sar_high, upper_32_bits(child->sar));
		/* DAR - low, high */
		SET_LL(&lli[i].dar_low, lower_32_bits(child->dar));
		SET_LL(&lli[i].dar_high, upper_32_bits(child->dar));
		i++;
	}

	llp = (void __iomem *)&lli[i];
	control = DW_HDMA_V0_LLP | DW_HDMA_V0_TCB;

	/* 1 DMA descriptor is prefetched */
	control &= ~DW_HDMA_V0_ELEMENT_PREFETCH;
	control |= HDMA_LLQ_PREFETCH_1;

	if (!chunk->cb)
		control |= DW_HDMA_V0_CB;

	/* Channel control */
	SET_LL(&llp->control, control);

	/* Linked list  - low, high */
	SET_LL(&llp->llp_low, lower_32_bits(chunk->ll_region.paddr));
	SET_LL(&llp->llp_high, upper_32_bits(chunk->ll_region.paddr));
}

void dw_hdma_core_start(struct dw_hdma_chunk *chunk, bool first)
{
	struct dw_hdma_chan *chan = chunk->chan;
	struct dw_hdma *dw = chan->chip->dw;
	u32 tmp;

	dw_hdma_core_write_chunk(chunk);

	if (first) {
		/* Enable channel */
		SET_CH(dw, chan->dir, chan->id, ch_enable, BIT(0));

		/* Interrupt unmask - done, watermark, abort */
		tmp = GET_CH(dw, chan->dir, chan->id, int_setup);
		tmp &= ~(HDMA_CH_LCOAL_INT_STOP_MASK
			 // | HDMA_CH_LOCAL_INT_WATERMARK_MASK
			 | HDMA_CH_LOCAL_INT_ABORT_MASK);

		/* Interrupt enable - both local and remote done, abort */
		tmp |= (HDMA_CH_REMOTE_STOP_INT_EN
			| HDMA_CH_LOCAL_STOP_INT_EN
			| HDMA_CH_REMOTE_ABORT_INT_EN
			| HDMA_CH_LOCAL_ABORT_INT_EN);

		SET_CH(dw, chan->dir, chan->id, int_setup, tmp);

		/* Interrupt enable - both local and remote watermark */
		SET_CH(dw, chan->dir, chan->id, ll_watermark_en,
		       HDMA_CH_WATERMARK_LWIE | HDMA_CH_WATERMARK_RWIE);

		/* Cycle state enable */
		SET_CH(dw, chan->dir, chan->id, ch_cycle, HDMA_CH_CYCLE_STATE);

		/* Channel control */
		SET_CH(dw, chan->dir, chan->id, ch_ctrl1, HDMA_CH_CONTROL1_LLE);

		/* Linked list - low, high */
		SET_CH(dw, chan->dir, chan->id, llp_low,
		       lower_32_bits(chunk->ll_region.paddr));
		SET_CH(dw, chan->dir, chan->id, llp_high,
		       upper_32_bits(chunk->ll_region.paddr));
	}
	/* Doorbell */
	SET_CH(dw, chan->dir, chan->id, ch_doorbell, HDMA_CH_DB_START);
}

int dw_hdma_core_device_config(struct dw_hdma_chan *chan)
{
	struct dw_hdma *dw = chan->chip->dw;

	/* MSI done addr - low, high */
	SET_CH(dw, chan->dir, chan->id, msi_stop_low, chan->msi.address_lo);
	SET_CH(dw, chan->dir, chan->id, msi_stop_high, chan->msi.address_hi);

	/* MSI watermark addr - low, high */
	SET_CH(dw, chan->dir, chan->id, msi_watermark_low, chan->msi.address_lo);
	SET_CH(dw, chan->dir, chan->id, msi_watermark_high, chan->msi.address_hi);

	/* MSI abort addr - low, high */
	SET_CH(dw, chan->dir, chan->id, msi_abort_low, chan->msi.address_lo);
	SET_CH(dw, chan->dir, chan->id, msi_abort_high, chan->msi.address_hi);

	/* MSI data - low, high */
	SET_CH(dw, chan->dir, chan->id, msi_msgd, chan->msi.data);

	return 0;
}

/* HDMA debugfs callbacks */
void dw_hdma_core_debugfs_on(struct dw_hdma_chip *chip)
{
	dw_hdma_debugfs_on(chip);
}

void dw_hdma_core_debugfs_off(struct dw_hdma_chip *chip)
{
	dw_hdma_debugfs_off(chip);
}
