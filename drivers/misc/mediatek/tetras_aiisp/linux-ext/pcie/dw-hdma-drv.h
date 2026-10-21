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

#ifndef _DW_HDMA_DRV_H
#define _DW_HDMA_DRV_H

#include <linux/msi.h>

#include <../drivers/dma/virt-dma.h>

#define HDMA_LL_SZ			24 /* 6 DWs */

#define HDMA_MAX_WR_CH			2  /* HW configured */
#define HDMA_MAX_RD_CH			2  /* HW configured */

#define HDMA_MAX_WR_LLQ_DEPTH		2  /* HW configured */
#define HDMA_MAX_RD_LLQ_DEPTH		2  /* HW configured */
#define HDMA_LLQ_PREFETCH_1		0  /* 1 desc prefetched */
#define HDMA_LLQ_PREFETCH_2		1  /* 2 desc prefetched */

enum dw_hdma_dir {
	HDMA_DIR_WRITE = 0,
	HDMA_DIR_READ
};

enum dw_hdma_map_format {
	MF_HDMA_LEGACY_PL		= 0x0,
	MF_HDMA_UNROLL			= 0x1,
	MF_HDMA_COMPAT_PL		= 0x4,
	MF_HDMA_COMPAT			= 0x5,
	MF_HDMA_NATIVE_PL		= 0x6,
	MF_HDMA_NATIVE			= 0x7
};

enum dw_hdma_request {
	HDMA_REQ_NONE = 0,
	HDMA_REQ_STOP,
	HDMA_REQ_PAUSE
};

enum dw_hdma_status {
	HDMA_ST_IDLE = 0,
	HDMA_ST_PAUSE,
	HDMA_ST_BUSY
};

enum dw_hdma_xfer_type {
	HDMA_XFER_SCATTER_GATHER = 0,
	HDMA_XFER_CYCLIC,
	HDMA_XFER_INTERLEAVED,
	HDMA_XFER_MEMCPY,
};

struct dw_hdma_chan;
struct dw_hdma_chunk;

struct dw_hdma_burst {
	struct list_head		list;
	u64				sar;
	u64				dar;
	u32				sz;
};

struct dw_hdma_region {
	phys_addr_t			paddr;
	void				__iomem *vaddr;
	size_t				sz;
};

struct dw_hdma_chunk {
	struct list_head		list;
	struct dw_hdma_chan		*chan;
	struct dw_hdma_burst		*burst;

	u32				bursts_alloc;

	u8				cb;
	struct dw_hdma_region		ll_region; /* Linked list */
};

struct dw_hdma_desc {
	struct virt_dma_desc		vd;
	struct dw_hdma_chan		*chan;
	struct dw_hdma_chunk		*chunk;

	u32				chunks_alloc;

	u32				alloc_sz;
	u32				xfer_sz;
};

struct dw_hdma_chip;

struct dw_hdma_chan {
	struct virt_dma_chan		vc;
	struct dw_hdma_chip		*chip;
	int				id;
	enum dw_hdma_dir		dir;

	u32				ll_max;

	struct msi_msg			msi;

	enum dw_hdma_request		request;
	enum dw_hdma_status		status;
	u8				configured;

	struct dma_slave_config		config;
};

struct dw_hdma_irq {
	struct msi_msg                  msi;
	u32				wr_mask;
	u32				rd_mask;
	struct dw_hdma			*dw;
};

struct dw_hdma_core_ops {
	int	(*irq_vector)(struct device *dev, unsigned int nr);
};

struct dw_hdma {
	char				name[20];

	struct dma_device		wr_hdma;
	u16				wr_ch_cnt;

	struct dma_device		rd_hdma;
	u16				rd_ch_cnt;

	struct dw_hdma_region		rg_region; /* Registers */
	struct dw_hdma_region		ll_region_wr[HDMA_MAX_WR_CH];
	struct dw_hdma_region		ll_region_rd[HDMA_MAX_RD_CH];
	struct dw_hdma_region		dt_region_wr[HDMA_MAX_WR_CH];
	struct dw_hdma_region		dt_region_rd[HDMA_MAX_RD_CH];

	struct dw_hdma_irq		*irq;
	int				nr_irqs;

	enum dw_hdma_map_format		mf;

	struct dw_hdma_chan		*chan;
	const struct dw_hdma_core_ops	*ops;

	phys_addr_t			data_bar_phys_base;

	raw_spinlock_t			lock; /* Only for legacy */
#ifdef CONFIG_DEBUG_FS
	struct dentry			*debugfs;
#endif /* CONFIG_DEBUG_FS */
};

struct dw_hdma_chip {
	struct device			*dev;
	int				id;
	int				irq;
	struct dw_hdma			*dw;
};

struct dw_hdma_sg {
	struct scatterlist		*sgl;
	unsigned int			len;
};

struct dw_hdma_cyclic {
	dma_addr_t			paddr;
	size_t				len;
	size_t				cnt;
};

struct dw_hdma_memcpy {
	dma_addr_t			dst;
	dma_addr_t			src;
	size_t				len;
};

struct dw_hdma_transfer {
	struct dma_chan			*dchan;
	union dw_hdma_xfer {
		struct dw_hdma_sg		sg;
		struct dw_hdma_cyclic		cyclic;
		struct dma_interleaved_template *il;
		struct dw_hdma_memcpy		memcpy;
	} xfer;
	enum dma_transfer_direction	direction;
	unsigned long			flags;
	enum dw_hdma_xfer_type		type;
};

static inline
struct dw_hdma_chan *vc2dw_hdma_chan(struct virt_dma_chan *vc)
{
	return container_of(vc, struct dw_hdma_chan, vc);
}

static inline
struct dw_hdma_chan *dchan2dw_hdma_chan(struct dma_chan *dchan)
{
	return vc2dw_hdma_chan(to_virt_chan(dchan));
}

int dw_hdma_probe(struct dw_hdma_chip *chip);
int dw_hdma_remove(struct dw_hdma_chip *chip);

#endif /* _DW_HDMA_DRV_H */
