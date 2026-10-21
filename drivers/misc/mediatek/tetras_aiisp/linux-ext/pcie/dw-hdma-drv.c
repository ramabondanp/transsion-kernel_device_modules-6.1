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

#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/pm_runtime.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/dma-mapping.h>

#include "dw-hdma-drv.h"
#include "dw-hdma-core.h"
#include "dw-hdma-regs.h"

#include <../drivers/dma/dmaengine.h>
#include <../drivers/dma/virt-dma.h>

#define	DEBUG
#define	VERBOSE_DEBUG

static const char *const dw_hdma_chan_error_str[] = {
	[0] = "SUCCESS",
	[HDMA_CH_INT_ERR_LL_CPL_UR] = "HDMA_CH_INT_ERR_LL_CPL_UR",
	[HDMA_CH_INT_EER_LL_CPL_CA] = "HDMA_CH_INT_EER_LL_CPL_CA",
	[HDMA_CH_INT_EER_LL_CPL_EP] = "HDMA_CH_INT_EER_LL_CPL_EP",
	[HDMA_CH_INT_EER_DATA_CPL_TIMEOUT] = "HDMA_CH_INT_EER_DATA_CPL_TIMEOUT",
	[HDMA_CH_INT_EER_DATA_CPL_UR] = "HDMA_CH_INT_EER_DATA_CPL_UR",
	[HDMA_CH_INT_EER_DATA_CPL_CA] = "HDMA_CH_INT_EER_DATA_CPL_CA",
	[HDMA_CH_INT_EER_DATA_CPL_EP] = "HDMA_CH_INT_EER_DATA_CPL_EP",
	[HDMA_CH_INT_EER_DATA_MWR] = "HDMA_CH_INT_EER_DATA_MWR",
};

#define HDMA_ERR_STR(state) ((state) >= ARRAY_SIZE(dw_hdma_chan_error_str) ? \
			      "HDMA_ERR_INVALID" : dw_hdma_chan_error_str[state])

static inline
struct device *dchan2dev(struct dma_chan *dchan)
{
	return &dchan->dev->device;
}

static inline
struct device *chan2dev(struct dw_hdma_chan *chan)
{
	return &chan->vc.chan.dev->device;
}

static inline
struct dw_hdma_desc *vd2dw_hdma_desc(struct virt_dma_desc *vd)
{
	return container_of(vd, struct dw_hdma_desc, vd);
}

static struct dw_hdma_burst *dw_hdma_alloc_burst(struct dw_hdma_chunk *chunk)
{
	struct dw_hdma_burst *burst;

	burst = kzalloc(sizeof(*burst), GFP_NOWAIT);
	if (unlikely(!burst))
		return NULL;

	INIT_LIST_HEAD(&burst->list);
	if (chunk->burst) {
		/* Create and add new element into the linked list */
		chunk->bursts_alloc++;
		list_add_tail(&burst->list, &chunk->burst->list);
	} else {
		/* List head */
		chunk->bursts_alloc = 0;
		chunk->burst = burst;
	}

	return burst;
}

static struct dw_hdma_chunk *dw_hdma_alloc_chunk(struct dw_hdma_desc *desc)
{
	struct dw_hdma_chan *chan = desc->chan;
	struct dw_hdma *dw = chan->chip->dw;
	struct dw_hdma_chunk *chunk;

	chunk = kzalloc(sizeof(*chunk), GFP_NOWAIT);
	if (unlikely(!chunk))
		return NULL;

	INIT_LIST_HEAD(&chunk->list);
	chunk->chan = chan;
	/* Toggling change bit (CB) in each chunk, this is a mechanism to
	 * inform the HDMA HW block that this is a new linked list ready
	 * to be consumed.
	 *  - Odd chunks originate CB equal to 0
	 *  - Even chunks originate CB equal to 1
	 */
	chunk->cb = !(desc->chunks_alloc % 2);
	if (chan->dir == HDMA_DIR_WRITE) {
		chunk->ll_region.paddr = dw->ll_region_wr[chan->id].paddr;
		chunk->ll_region.vaddr = dw->ll_region_wr[chan->id].vaddr;
	} else {
		chunk->ll_region.paddr = dw->ll_region_rd[chan->id].paddr;
		chunk->ll_region.vaddr = dw->ll_region_rd[chan->id].vaddr;
	}

	if (desc->chunk) {
		/* Create and add new element into the linked list */
		if (!dw_hdma_alloc_burst(chunk)) {
			kfree(chunk);
			return NULL;
		}
		desc->chunks_alloc++;
		list_add_tail(&chunk->list, &desc->chunk->list);
	} else {
		/* List head */
		chunk->burst = NULL;
		desc->chunks_alloc = 0;
		desc->chunk = chunk;
	}

	return chunk;
}

static struct dw_hdma_desc *dw_hdma_alloc_desc(struct dw_hdma_chan *chan)
{
	struct dw_hdma_desc *desc;

	desc = kzalloc(sizeof(*desc), GFP_NOWAIT);
	if (unlikely(!desc))
		return NULL;

	desc->chan = chan;
	if (!dw_hdma_alloc_chunk(desc)) {
		kfree(desc);
		return NULL;
	}

	return desc;
}

static void dw_hdma_free_burst(struct dw_hdma_chunk *chunk)
{
	struct dw_hdma_burst *child, *_next;

	/* Remove all the list elements */
	list_for_each_entry_safe(child, _next, &chunk->burst->list, list) {
		list_del(&child->list);
		kfree(child);
		chunk->bursts_alloc--;
	}

	/* Remove the list head */
	kfree(child);
	chunk->burst = NULL;
}

static void dw_hdma_free_chunk(struct dw_hdma_desc *desc)
{
	struct dw_hdma_chunk *child, *_next;

	if (!desc->chunk)
		return;

	/* Remove all the list elements */
	list_for_each_entry_safe(child, _next, &desc->chunk->list, list) {
		dw_hdma_free_burst(child);
		list_del(&child->list);
		kfree(child);
		desc->chunks_alloc--;
	}

	/* Remove the list head */
	kfree(child);
	desc->chunk = NULL;
}

static void dw_hdma_free_desc(struct dw_hdma_desc *desc)
{
	dw_hdma_free_chunk(desc);
	kfree(desc);
}

static void vchan_free_desc(struct virt_dma_desc *vdesc)
{
	dw_hdma_free_desc(vd2dw_hdma_desc(vdesc));
}

static void dw_hdma_start_transfer(struct dw_hdma_chan *chan)
{
	struct dw_hdma_chunk *child;
	struct dw_hdma_desc *desc;
	struct virt_dma_desc *vd;

	vd = vchan_next_desc(&chan->vc);
	if (!vd)
		return;

	desc = vd2dw_hdma_desc(vd);
	if (!desc)
		return;

	child = list_first_entry_or_null(&desc->chunk->list,
					 struct dw_hdma_chunk, list);
	if (!child)
		return;

	dw_hdma_core_start(child, !desc->xfer_sz);
	desc->xfer_sz += child->ll_region.sz;
	dw_hdma_free_burst(child);
	list_del(&child->list);
	kfree(child);
	desc->chunks_alloc--;
}

static int dw_hdma_device_config(struct dma_chan *dchan,
				 struct dma_slave_config *config)
{
	struct dw_hdma_chan *chan = dchan2dw_hdma_chan(dchan);

	memcpy(&chan->config, config, sizeof(*config));
	chan->configured = true;

	return 0;
}

static int dw_hdma_device_pause(struct dma_chan *dchan)
{
	struct dw_hdma_chan *chan = dchan2dw_hdma_chan(dchan);
	int err = 0;

	if (!chan->configured)
		err = -EPERM;
	else if (chan->status != HDMA_ST_BUSY)
		err = -EPERM;
	else if (chan->request != HDMA_REQ_NONE)
		err = -EPERM;
	else
		chan->request = HDMA_REQ_PAUSE;

	return err;
}

static int dw_hdma_device_resume(struct dma_chan *dchan)
{
	struct dw_hdma_chan *chan = dchan2dw_hdma_chan(dchan);
	int err = 0;

	if (!chan->configured) {
		err = -EPERM;
	} else if (chan->status != HDMA_ST_PAUSE) {
		err = -EPERM;
	} else if (chan->request != HDMA_REQ_NONE) {
		err = -EPERM;
	} else {
		chan->status = HDMA_ST_BUSY;
		dw_hdma_start_transfer(chan);
	}

	return err;
}

static int dw_hdma_device_terminate_all(struct dma_chan *dchan)
{
	struct dw_hdma_chan *chan = dchan2dw_hdma_chan(dchan);
	int err = 0;

	if (!chan->configured) {
		/* Do nothing */
	} else if (chan->status == HDMA_ST_PAUSE) {
		chan->status = HDMA_ST_IDLE;
		chan->configured = false;
	} else if (chan->status == HDMA_ST_IDLE) {
		chan->configured = false;
	} else if (dw_hdma_core_ch_status(chan) == DMA_COMPLETE) {
		/*
		 * The channel is in a false BUSY state, probably didn't
		 * receive or lost an interrupt
		 */
		chan->status = HDMA_ST_IDLE;
		chan->configured = false;
	} else if (chan->request > HDMA_REQ_PAUSE) {
		err = -EPERM;
	} else {
		chan->request = HDMA_REQ_STOP;
	}

	return err;
}

static void dw_hdma_device_issue_pending(struct dma_chan *dchan)
{
	struct dw_hdma_chan *chan = dchan2dw_hdma_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&chan->vc.lock, flags);
	if (chan->configured && chan->request == HDMA_REQ_NONE &&
	    chan->status == HDMA_ST_IDLE && vchan_issue_pending(&chan->vc)) {
		chan->status = HDMA_ST_BUSY;
		dw_hdma_start_transfer(chan);
	}
	spin_unlock_irqrestore(&chan->vc.lock, flags);
}

static enum dma_status
dw_hdma_device_tx_status(struct dma_chan *dchan, dma_cookie_t cookie,
			 struct dma_tx_state *txstate) {
	struct dw_hdma_chan *chan = dchan2dw_hdma_chan(dchan);
	struct dw_hdma_desc *desc;
	struct virt_dma_desc *vd;
	unsigned long flags;
	enum dma_status ret;
	u32 residue = 0;

	ret = dma_cookie_status(dchan, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	if (ret == DMA_IN_PROGRESS && chan->status == HDMA_ST_PAUSE)
		ret = DMA_PAUSED;

	if (!txstate)
		goto ret_residue;

	spin_lock_irqsave(&chan->vc.lock, flags);
	vd = vchan_find_desc(&chan->vc, cookie);
	if (vd)
	{
		desc = vd2dw_hdma_desc(vd);
		if (desc)
			residue = desc->alloc_sz - desc->xfer_sz;
	}
	spin_unlock_irqrestore(&chan->vc.lock, flags);

ret_residue:
	dma_set_residue(txstate, residue);

	return ret;
}

static struct dma_async_tx_descriptor *
dw_hdma_device_transfer(struct dw_hdma_transfer *xfer)
{
	struct dw_hdma_chan *chan = dchan2dw_hdma_chan(xfer->dchan);
	enum dma_transfer_direction dir = xfer->direction;
	phys_addr_t src_addr, dst_addr;
	struct scatterlist *sg = NULL;
	struct dw_hdma_chunk *chunk;
	struct dw_hdma_burst *burst;
	struct dw_hdma_desc *desc;
	u32 cnt = 0;
	int i;

	if (!chan->configured)
		return NULL;

	switch (chan->config.direction) {
	case DMA_DEV_TO_MEM: /* local DMA */
		if (dir == DMA_DEV_TO_MEM && chan->dir == HDMA_DIR_READ)
			break;
		return NULL;
	case DMA_MEM_TO_DEV: /* local DMA */
		if (dir == DMA_MEM_TO_DEV && chan->dir == HDMA_DIR_WRITE)
			break;
		return NULL;
	default: /* remote DMA */
		break;
#if 0
		if (dir == DMA_MEM_TO_DEV && chan->dir == HDMA_DIR_READ)
			break;
		if (dir == DMA_DEV_TO_MEM && chan->dir == HDMA_DIR_WRITE)
			break;
		return NULL;
#endif
	}

	if (xfer->type == HDMA_XFER_CYCLIC) {
		if (!xfer->xfer.cyclic.len || !xfer->xfer.cyclic.cnt)
			return NULL;
	} else if (xfer->type == HDMA_XFER_SCATTER_GATHER) {
		if (xfer->xfer.sg.len < 1)
			return NULL;
	} else if (xfer->type == HDMA_XFER_INTERLEAVED) {
		if (!xfer->xfer.il->numf)
			return NULL;
		if (xfer->xfer.il->numf > 0 && xfer->xfer.il->frame_size > 0)
			return NULL;
	} else if (xfer->type == HDMA_XFER_MEMCPY) {
		if (!xfer->xfer.memcpy.dst || !xfer->xfer.memcpy.src
		    || !xfer->xfer.memcpy.len)
			return NULL;
	} else {
		return NULL;
	}

	desc = dw_hdma_alloc_desc(chan);
	if (unlikely(!desc))
		goto err_alloc;

	chunk = dw_hdma_alloc_chunk(desc);
	if (unlikely(!chunk))
		goto err_alloc;

	if (xfer->type == HDMA_XFER_INTERLEAVED) {
		src_addr = xfer->xfer.il->src_start;
		dst_addr = xfer->xfer.il->dst_start;
	} else if (xfer->type == HDMA_XFER_MEMCPY) {
		src_addr = xfer->xfer.memcpy.src;
		dst_addr = xfer->xfer.memcpy.dst;
	} else {
		src_addr = chan->config.src_addr;
		dst_addr = chan->config.dst_addr;
	}

	if (xfer->type == HDMA_XFER_CYCLIC) {
		cnt = xfer->xfer.cyclic.cnt;
	} else if (xfer->type == HDMA_XFER_SCATTER_GATHER) {
		cnt = xfer->xfer.sg.len;
		sg = xfer->xfer.sg.sgl;
	} else if (xfer->type == HDMA_XFER_INTERLEAVED) {
		if (xfer->xfer.il->numf > 0)
			cnt = xfer->xfer.il->numf;
		else
			cnt = xfer->xfer.il->frame_size;
	} else if (xfer->type == HDMA_XFER_MEMCPY) {
		cnt = 1;
	}

	for (i = 0; i < cnt; i++) {
		if (xfer->type == HDMA_XFER_SCATTER_GATHER && !sg)
			break;

		if (chunk->bursts_alloc == chan->ll_max) {
			chunk = dw_hdma_alloc_chunk(desc);
			if (unlikely(!chunk))
				goto err_alloc;
		}

		burst = dw_hdma_alloc_burst(chunk);
		if (unlikely(!burst))
			goto err_alloc;

		if (xfer->type == HDMA_XFER_CYCLIC)
			burst->sz = xfer->xfer.cyclic.len;
		else if (xfer->type == HDMA_XFER_SCATTER_GATHER)
			burst->sz = sg_dma_len(sg);
		else if (xfer->type == HDMA_XFER_INTERLEAVED)
			burst->sz = xfer->xfer.il->sgl[i].size;
		else if (xfer->type == HDMA_XFER_MEMCPY)
			burst->sz = xfer->xfer.memcpy.len;

		chunk->ll_region.sz += burst->sz;
		desc->alloc_sz += burst->sz;

		if (chan->dir == HDMA_DIR_WRITE) {
			burst->sar = src_addr;
			if (xfer->type == HDMA_XFER_CYCLIC) {
				burst->dar = xfer->xfer.cyclic.paddr;
			} else if (xfer->type == HDMA_XFER_SCATTER_GATHER) {
				src_addr += sg_dma_len(sg);
				burst->dar = sg_dma_address(sg);
				/* Unlike the typical assumption by other
				 * drivers/IPs the peripheral memory isn't
				 * a FIFO memory, in this case, it's a
				 * linear memory and that why the source
				 * and destination addresses are increased
				 * by the same portion (data length)
				 */
			} else if (xfer->type == HDMA_XFER_MEMCPY) {
				burst->dar = dst_addr;
			}
		} else {
			burst->dar = dst_addr;
			if (xfer->type == HDMA_XFER_CYCLIC) {
				burst->sar = xfer->xfer.cyclic.paddr;
			} else if (xfer->type == HDMA_XFER_SCATTER_GATHER) {
				dst_addr += sg_dma_len(sg);
				burst->sar = sg_dma_address(sg);
				/* Unlike the typical assumption by other
				 * drivers/IPs the peripheral memory isn't
				 * a FIFO memory, in this case, it's a
				 * linear memory and that why the source
				 * and destination addresses are increased
				 * by the same portion (data length)
				 */
			} else if (xfer->type == HDMA_XFER_MEMCPY) {
				burst->sar = src_addr;
			}
		}

		if (xfer->type == HDMA_XFER_SCATTER_GATHER) {
			sg = sg_next(sg);
		} else if (xfer->type == HDMA_XFER_INTERLEAVED &&
			   xfer->xfer.il->frame_size > 0) {
			struct dma_interleaved_template *il = xfer->xfer.il;
			struct data_chunk *dc = &il->sgl[i];

			if (il->src_sgl) {
				src_addr += burst->sz;
				src_addr += dmaengine_get_src_icg(il, dc);
			}

			if (il->dst_sgl) {
				dst_addr += burst->sz;
				dst_addr += dmaengine_get_dst_icg(il, dc);
			}
		}
	}

	return vchan_tx_prep(&chan->vc, &desc->vd, xfer->flags);

err_alloc:
	if (desc)
		dw_hdma_free_desc(desc);

	return NULL;
}

static struct dma_async_tx_descriptor *
dw_hdma_device_prep_slave_sg(struct dma_chan *dchan, struct scatterlist *sgl,
			     unsigned int len,
			     enum dma_transfer_direction direction,
			     unsigned long flags, void *context)
{
	struct dw_hdma_transfer xfer;

	xfer.dchan = dchan;
	xfer.direction = direction;
	xfer.xfer.sg.sgl = sgl;
	xfer.xfer.sg.len = len;
	xfer.flags = flags;
	xfer.type = HDMA_XFER_SCATTER_GATHER;

	return dw_hdma_device_transfer(&xfer);
}

static struct dma_async_tx_descriptor *
dw_hdma_device_prep_dma_cyclic(struct dma_chan *dchan, dma_addr_t paddr,
			       size_t len, size_t count,
			       enum dma_transfer_direction direction,
			       unsigned long flags)
{
	struct dw_hdma_transfer xfer;

	xfer.dchan = dchan;
	xfer.direction = direction;
	xfer.xfer.cyclic.paddr = paddr;
	xfer.xfer.cyclic.len = len;
	xfer.xfer.cyclic.cnt = count;
	xfer.flags = flags;
	xfer.type = HDMA_XFER_CYCLIC;

	return dw_hdma_device_transfer(&xfer);
}

static struct dma_async_tx_descriptor *
dw_hdma_device_prep_interleaved_dma(struct dma_chan *dchan,
				    struct dma_interleaved_template *ilt,
				    unsigned long flags)
{
	struct dw_hdma_transfer xfer;

	xfer.dchan = dchan;
	xfer.direction = ilt->dir;
	xfer.xfer.il = ilt;
	xfer.flags = flags;
	xfer.type = HDMA_XFER_INTERLEAVED;

	return dw_hdma_device_transfer(&xfer);
}

static struct dma_async_tx_descriptor *
dw_hdma_device_prep_dma_memcpy(struct dma_chan *dchan,
			       dma_addr_t dest, dma_addr_t src, size_t len,
			       unsigned long flags)
{
	struct dw_hdma_transfer xfer;

	xfer.dchan = dchan;
	xfer.xfer.memcpy.dst = dest;
	xfer.xfer.memcpy.src = src;
	xfer.xfer.memcpy.len = len;

	xfer.flags = flags;
	xfer.type = HDMA_XFER_MEMCPY;

	return dw_hdma_device_transfer(&xfer);
}

static void dw_hdma_done_interrupt(struct dw_hdma_chan *chan)
{
	struct dw_hdma_desc *desc;
	struct virt_dma_desc *vd;
	unsigned long flags;

	dw_hdma_core_clear_done_int(chan);

	spin_lock_irqsave(&chan->vc.lock, flags);
	vd = vchan_next_desc(&chan->vc);
	if (vd) {
		switch (chan->request) {
		case HDMA_REQ_NONE:
			desc = vd2dw_hdma_desc(vd);
			if (desc->chunks_alloc) {
				chan->status = HDMA_ST_BUSY;
				dw_hdma_start_transfer(chan);
			} else {
				list_del(&vd->node);
				vchan_cookie_complete(vd);
				chan->status = HDMA_ST_IDLE;
			}
			break;

		case HDMA_REQ_STOP:
			list_del(&vd->node);
			vchan_cookie_complete(vd);
			chan->request = HDMA_REQ_NONE;
			chan->status = HDMA_ST_IDLE;
			break;

		case HDMA_REQ_PAUSE:
			chan->request = HDMA_REQ_NONE;
			chan->status = HDMA_ST_PAUSE;
			break;

		default:
			break;
		}
	}
	spin_unlock_irqrestore(&chan->vc.lock, flags);
}

static void dw_hdma_abort_interrupt(struct dw_hdma_chan *chan)
{
	struct virt_dma_desc *vd;
	unsigned long flags;
	unsigned long err_sts;

	dw_hdma_core_clear_abort_int(chan);

	err_sts = dw_hdma_core_status_abort_error(chan);
	if (err_sts)
		dev_err(NULL, "[%s] hdma xfer abort [%d(%s)] \n",
			chan->chip->dw->name, err_sts, HDMA_ERR_STR(err_sts));

	spin_lock_irqsave(&chan->vc.lock, flags);
	vd = vchan_next_desc(&chan->vc);
	if (vd) {
		list_del(&vd->node);
		vchan_cookie_complete(vd);
	}
	spin_unlock_irqrestore(&chan->vc.lock, flags);

	chan->request = HDMA_REQ_NONE;
	chan->status = HDMA_ST_IDLE;
}

static void dw_hdma_watermark_interrupt(struct dw_hdma_chan *chan)
{

}

static irqreturn_t dw_hdma_interrupt(int irq, void *data, bool write)
{
	struct dw_hdma_irq *dw_irq = data;
	struct dw_hdma *dw = dw_irq->dw;
	unsigned long total, pos, mask;
	unsigned long off;

	if (write) {
		total = dw->wr_ch_cnt;
		off = 0;
		mask = dw_irq->wr_mask;
	} else {
		total = dw->rd_ch_cnt;
		off = dw->wr_ch_cnt;
		mask = dw_irq->rd_mask;
	}

	for_each_set_bit(pos, &mask, total) {
		struct dw_hdma_chan *chan = &dw->chan[pos + off];

		if (dw_hdma_core_status_done_int(chan))
			dw_hdma_done_interrupt(chan);

		if (dw_hdma_core_status_watermark_int(chan))
			dw_hdma_watermark_interrupt(chan);

		if (dw_hdma_core_status_abort_int(chan))
			dw_hdma_abort_interrupt(chan);
	}

	return IRQ_HANDLED;
}

static inline irqreturn_t dw_hdma_interrupt_write(int irq, void *data)
{
	return dw_hdma_interrupt(irq, data, true);
}

static inline irqreturn_t dw_hdma_interrupt_read(int irq, void *data)
{
	return dw_hdma_interrupt(irq, data, false);
}

static irqreturn_t dw_hdma_interrupt_common(int irq, void *data)
{
	dw_hdma_interrupt(irq, data, true);
	dw_hdma_interrupt(irq, data, false);

	return IRQ_HANDLED;
}

static int dw_hdma_alloc_chan_resources(struct dma_chan *dchan)
{
	struct dw_hdma_chan *chan = dchan2dw_hdma_chan(dchan);

	if (chan->status != HDMA_ST_IDLE)
		return -EBUSY;

	pm_runtime_get(chan->chip->dev);

	return 0;
}

static void dw_hdma_free_chan_resources(struct dma_chan *dchan)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(5000);
	struct dw_hdma_chan *chan = dchan2dw_hdma_chan(dchan);
	int ret;

	while (time_before(jiffies, timeout)) {
		ret = dw_hdma_device_terminate_all(dchan);
		if (!ret)
			break;

		if (time_after_eq(jiffies, timeout))
			return;

		cpu_relax();
	}

	pm_runtime_put(chan->chip->dev);
}

static int dw_hdma_channel_setup(struct dw_hdma_chip *chip, bool write,
				 u32 wr_alloc, u32 rd_alloc)
{
	struct dw_hdma_region *dt_region;
	struct device *dev = chip->dev;
	struct dw_hdma *dw = chip->dw;
	struct dw_hdma_chan *chan;
	struct dw_hdma_irq *irq;
	struct dma_device *dma;
	u32 alloc, off_alloc;
	u32 i, j, cnt;
	int err = 0;
	u32 pos;

	if (write) {
		i = 0;
		cnt = dw->wr_ch_cnt;
		dma = &dw->wr_hdma;
		alloc = wr_alloc;
		off_alloc = 0;
	} else {
		i = dw->wr_ch_cnt;
		cnt = dw->rd_ch_cnt;
		dma = &dw->rd_hdma;
		alloc = rd_alloc;
		off_alloc = wr_alloc;
	}

	INIT_LIST_HEAD(&dma->channels);
	for (j = 0; (alloc || dw->nr_irqs == 1) && j < cnt; j++, i++) {
		chan = &dw->chan[i];

		dt_region = devm_kzalloc(dev, sizeof(*dt_region), GFP_KERNEL);
		if (!dt_region)
			return -ENOMEM;

		chan->vc.chan.private = dt_region;

		chan->chip = chip;
		chan->id = j;
		chan->dir = write ? HDMA_DIR_WRITE : HDMA_DIR_READ;
		chan->configured = false;
		chan->request = HDMA_REQ_NONE;
		chan->status = HDMA_ST_IDLE;

		if (write)
			chan->ll_max = (dw->ll_region_wr[j].sz / HDMA_LL_SZ);
		else
			chan->ll_max = (dw->ll_region_rd[j].sz / HDMA_LL_SZ);
		chan->ll_max -= 1;

		dev_info(dev, "L. List: Channel %s[%u] max_cnt=%u\n",
			 write ? "write" : "read", j, chan->ll_max);

		if (dw->nr_irqs == 1)
			pos = 0;
		else
			pos = off_alloc + (j % alloc);

		irq = &dw->irq[pos];

		if (write)
			irq->wr_mask |= BIT(j);
		else
			irq->rd_mask |= BIT(j);

		irq->dw = dw;
		memcpy(&chan->msi, &irq->msi, sizeof(chan->msi));

		dev_info(dev, "MSI: Channel %s[%u] addr=0x%.8x%.8x, data=0x%.8x\n",
			 write ? "write" : "read", j,
			 chan->msi.address_hi, chan->msi.address_lo,
			 chan->msi.data);

		chan->vc.desc_free = vchan_free_desc;
		vchan_init(&chan->vc, dma);

		if (write) {
			dt_region->paddr = dw->dt_region_wr[j].paddr;
			dt_region->vaddr = dw->dt_region_wr[j].vaddr;
			dt_region->sz = dw->dt_region_wr[j].sz;
		} else {
			dt_region->paddr = dw->dt_region_rd[j].paddr;
			dt_region->vaddr = dw->dt_region_rd[j].vaddr;
			dt_region->sz = dw->dt_region_rd[j].sz;
		}

		dw_hdma_core_device_config(chan);
	}

	/* Set DMA channel capabilities */
	dma_cap_zero(dma->cap_mask);
	dma_cap_set(DMA_SLAVE, dma->cap_mask);
	dma_cap_set(DMA_CYCLIC, dma->cap_mask);
	dma_cap_set(DMA_PRIVATE, dma->cap_mask);
	dma_cap_set(DMA_INTERLEAVE, dma->cap_mask);
	dma_cap_set(DMA_MEMCPY, dma->cap_mask); /* For dmatest */
	dma->directions = BIT(write ? DMA_DEV_TO_MEM : DMA_MEM_TO_DEV);
	dma->src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	dma->dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	dma->residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;
	dma->chancnt = cnt;

	/* Set DMA channel callbacks */
	dma->dev = chip->dev;
	dma->device_alloc_chan_resources = dw_hdma_alloc_chan_resources;
	dma->device_free_chan_resources = dw_hdma_free_chan_resources;
	dma->device_config = dw_hdma_device_config;
	dma->device_pause = dw_hdma_device_pause;
	dma->device_resume = dw_hdma_device_resume;
	dma->device_terminate_all = dw_hdma_device_terminate_all;
	dma->device_issue_pending = dw_hdma_device_issue_pending;
	dma->device_tx_status = dw_hdma_device_tx_status;
	dma->device_prep_slave_sg = dw_hdma_device_prep_slave_sg;
	dma->device_prep_dma_cyclic = dw_hdma_device_prep_dma_cyclic;
	dma->device_prep_interleaved_dma = dw_hdma_device_prep_interleaved_dma;
	/* Memcpy for dmatest */
	dma->device_prep_dma_memcpy = dw_hdma_device_prep_dma_memcpy;

	dma_set_max_seg_size(dma->dev, U32_MAX);

	/* Register DMA device */
	err = dma_async_device_register(dma);

	return err;
}

static inline void dw_hdma_dec_irq_alloc(int *nr_irqs, u32 *alloc, u16 cnt)
{
	if (*nr_irqs && *alloc < cnt) {
		(*alloc)++;
		(*nr_irqs)--;
	}
}

static inline void dw_hdma_add_irq_mask(u32 *mask, u32 alloc, u16 cnt)
{
	while (*mask * alloc < cnt)
		(*mask)++;
}

static int dw_hdma_irq_request(struct dw_hdma_chip *chip,
			       u32 *wr_alloc, u32 *rd_alloc)
{
	struct device *dev = chip->dev;
	struct dw_hdma *dw = chip->dw;
	u32 wr_mask = 1;
	u32 rd_mask = 1;
	int i, err = 0;
	u32 ch_cnt;
	int irq;

	ch_cnt = dw->wr_ch_cnt + dw->rd_ch_cnt;

	if (dw->nr_irqs < 1)
		return -EINVAL;

	if (dw->nr_irqs == 1) {
		/* Common IRQ shared among all channels */
		irq = dw->ops->irq_vector(dev, 0); /* TETRAS_DMA_IRQ */
		err = request_irq(irq, dw_hdma_interrupt_common,
				  IRQF_SHARED, dw->name, &dw->irq[0]);
		if (err) {
			dw->nr_irqs = 0;
			return err;
		}

		if (irq_get_msi_desc(irq))
			get_cached_msi_msg(irq, &dw->irq[0].msi);
	} else {
		/* Distribute IRQs equally among all channels */
		int tmp = dw->nr_irqs;

		while (tmp && (*wr_alloc + *rd_alloc) < ch_cnt) {
			dw_hdma_dec_irq_alloc(&tmp, wr_alloc, dw->wr_ch_cnt);
			dw_hdma_dec_irq_alloc(&tmp, rd_alloc, dw->rd_ch_cnt);
		}

		dw_hdma_add_irq_mask(&wr_mask, *wr_alloc, dw->wr_ch_cnt);
		dw_hdma_add_irq_mask(&rd_mask, *rd_alloc, dw->rd_ch_cnt);

		for (i = 0; i < (*wr_alloc + *rd_alloc); i++) {
			irq = dw->ops->irq_vector(dev, i);
			err = request_irq(irq,
					  i < *wr_alloc ?
					  dw_hdma_interrupt_write :
					  dw_hdma_interrupt_read,
					  IRQF_SHARED, dw->name,
					  &dw->irq[i]);
			if (err) {
				dw->nr_irqs = i;
				return err;
			}

			if (irq_get_msi_desc(irq))
				get_cached_msi_msg(irq, &dw->irq[i].msi);
		}

		dw->nr_irqs = i;
	}

	return err;
}

int dw_hdma_probe(struct dw_hdma_chip *chip)
{
	struct device *dev;
	struct dw_hdma *dw;
	u32 wr_alloc = 0;
	u32 rd_alloc = 0;
	int i, err;

	if (!chip)
		return -EINVAL;

	dev = chip->dev;
	if (!dev)
		return -EINVAL;

	dw = chip->dw;
	if (!dw || !dw->irq || !dw->ops || !dw->ops->irq_vector)
		return -EINVAL;

	raw_spin_lock_init(&dw->lock);

	dev_info(dev, "Channels: write=%d, read=%d\n",
		 dw->wr_ch_cnt, dw->rd_ch_cnt);

	/* Allocate channels */
	dw->chan = devm_kcalloc(dev, dw->wr_ch_cnt + dw->rd_ch_cnt,
				sizeof(*dw->chan), GFP_KERNEL);
	if (!dw->chan)
		return -ENOMEM;

	snprintf(dw->name, sizeof(dw->name), "dw-hdma-core:%d", chip->id);

	/* Request IRQs */
	err = dw_hdma_irq_request(chip, &wr_alloc, &rd_alloc);
	if (err)
		return err;

	/* Setup write channels */
	err = dw_hdma_channel_setup(chip, true, wr_alloc, rd_alloc);
	if (err)
		goto err_irq_free;

	/* Setup read channels */
	err = dw_hdma_channel_setup(chip, false, wr_alloc, rd_alloc);
	if (err)
		goto err_irq_free;

	/* Power management */
	pm_runtime_enable(dev);

	/* Turn debugfs on */
	dw_hdma_core_debugfs_on(chip);

	return 0;

err_irq_free:
	for (i = (dw->nr_irqs - 1); i >= 0; i--)
		free_irq(dw->ops->irq_vector(dev, i), &dw->irq[i]);

	dw->nr_irqs = 0;

	return err;
}
EXPORT_SYMBOL_GPL(dw_hdma_probe);

int dw_hdma_remove(struct dw_hdma_chip *chip)
{
	struct dw_hdma_chan *chan, *_chan;
	struct device *dev = chip->dev;
	struct dw_hdma *dw = chip->dw;
	int i;

	/* Free irqs */
	for (i = (dw->nr_irqs - 1); i >= 0; i--)
		free_irq(dw->ops->irq_vector(dev, i), &dw->irq[i]);

	/* Power management */
	pm_runtime_disable(dev);

	/* Deregister HDMA device */
	dma_async_device_unregister(&dw->wr_hdma);
	list_for_each_entry_safe(chan, _chan, &dw->wr_hdma.channels,
				 vc.chan.device_node) {
		tasklet_kill(&chan->vc.task);
		list_del(&chan->vc.chan.device_node);
	}

	dma_async_device_unregister(&dw->rd_hdma);
	list_for_each_entry_safe(chan, _chan, &dw->rd_hdma.channels,
				 vc.chan.device_node) {
		tasklet_kill(&chan->vc.task);
		list_del(&chan->vc.chan.device_node);
	}

	/* Turn debugfs off */
	dw_hdma_core_debugfs_off(chip);

	return 0;
}
EXPORT_SYMBOL_GPL(dw_hdma_remove);
