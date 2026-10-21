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

#include <linux/debugfs.h>
#include <linux/bitfield.h>

#include "dw-hdma-debugfs.h"
#include "dw-hdma-regs.h"
#include "dw-hdma-drv.h"

#ifdef CONFIG_DEBUG_FS

#define REGS_ADDR(name) \
	((void __force *)&regs->name)
#define REGISTER(name) \
	{ #name, REGS_ADDR(name) }

#define WRITE_STR		"write"
#define READ_STR		"read"
#define CHANNEL_STR		"channel"
#define REGISTERS_STR		"registers"

static struct dentry		*base_dir;
static struct dw_hdma		*dw;
static struct dw_hdma_regs	__iomem *regs;

static struct {
	void			__iomem *start;
	void			__iomem *end;
} lim[2][HDMA_MAX_NR_CH];

struct debugfs_entries {
	const char		*name;
	dma_addr_t		*reg;
};

static int dw_hdma_debugfs_u32_get(void *data, u64 *val)
{
	void __iomem *reg = (void __force __iomem *)data;

	*val = readl(reg);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_x32, dw_hdma_debugfs_u32_get, NULL, "0x%08llx\n");

static void dw_hdma_debugfs_create_x32(const struct debugfs_entries entries[],
				       int nr_entries, struct dentry *dir)
{
	int i;

	for (i = 0; i < nr_entries; i++) {
		if (!debugfs_create_file_unsafe(entries[i].name, 0444, dir,
						entries[i].reg,	&fops_x32))
			break;
	}
}

static void dw_hdma_debugfs_regs_ch(struct dw_hdma_ch_regs __iomem *regs,
				    struct dentry *dir)
{
	int nr_entries;
	const struct debugfs_entries debugfs_regs[] = {
		/* Control */
		REGISTER(ch_enable),
		REGISTER(ch_doorbell),
		REGISTER(ch_prefetch),
		REGISTER(ch_handshake),
		REGISTER(llp_low),
		REGISTER(llp_high),
		REGISTER(ch_cycle),
		REGISTER(ch_xfer_size),
		REGISTER(sar_low),
		REGISTER(sar_high),
		REGISTER(dar_low),
		REGISTER(dar_high),
		REGISTER(ll_watermark_en),
		REGISTER(ch_ctrl1),
		REGISTER(ch_func_num),
		REGISTER(ch_qos),
		/* Status */
		REGISTER(ch_status),
		REGISTER(int_status),
		REGISTER(int_setup),
		REGISTER(int_clear),
		REGISTER(msi_stop_low),
		REGISTER(msi_stop_high),
		REGISTER(msi_watermark_low),
		REGISTER(msi_watermark_high),
		REGISTER(msi_abort_low),
		REGISTER(msi_abort_high),
		REGISTER(msi_msgd),
	};

	nr_entries = ARRAY_SIZE(debugfs_regs);
	dw_hdma_debugfs_create_x32(debugfs_regs, nr_entries, dir);
}

static void dw_hdma_debugfs_regs_wr(struct dentry *dir)
{
	struct dentry *regs_dir, *ch_dir;
	int i;
	char name[16];

	regs_dir = debugfs_create_dir(WRITE_STR, dir);
	if (!regs_dir)
		return;

	for (i = 0; i < dw->wr_ch_cnt; i++) {
		snprintf(name, sizeof(name), "%s:%d", CHANNEL_STR, i);

		ch_dir = debugfs_create_dir(name, regs_dir);
		if (!ch_dir)
			return;

		dw_hdma_debugfs_regs_ch(&regs->ch[i].wr, ch_dir);

		lim[0][i].start = &regs->ch[i].wr;
		lim[0][i].end = &regs->ch[i].wr.padding_2[0];
	}
}

static void dw_hdma_debugfs_regs_rd(struct dentry *dir)
{
	struct dentry *regs_dir, *ch_dir;
	int i;
	char name[16];

	regs_dir = debugfs_create_dir(READ_STR, dir);
	if (!regs_dir)
		return;

	for (i = 0; i < dw->rd_ch_cnt; i++) {
		snprintf(name, sizeof(name), "%s:%d", CHANNEL_STR, i);

		ch_dir = debugfs_create_dir(name, regs_dir);
		if (!ch_dir)
			return;

		dw_hdma_debugfs_regs_ch(&regs->ch[i].rd, ch_dir);

		lim[1][i].start = &regs->ch[i].rd;
		lim[1][i].end = &regs->ch[i].rd.padding_2[0];
	}
}

static void dw_hdma_debugfs_regs(void)
{
	struct dentry *regs_dir;

	regs_dir = debugfs_create_dir(REGISTERS_STR, base_dir);
	if (!regs_dir)
		return;

	dw_hdma_debugfs_regs_wr(regs_dir);
	dw_hdma_debugfs_regs_rd(regs_dir);
}

void dw_hdma_debugfs_on(struct dw_hdma_chip *chip)
{
	dw = chip->dw;
	if (!dw)
		return;

	regs = dw->rg_region.vaddr;
	if (!regs)
		return;

	base_dir = debugfs_create_dir(dw->name, 0);
	if (!base_dir)
		return;

	debugfs_create_u32("mf", 0444, dw->debugfs, &dw->mf);
	debugfs_create_u16("wr_ch_cnt", 0444, dw->debugfs, &dw->wr_ch_cnt);
	debugfs_create_u16("rd_ch_cnt", 0444, dw->debugfs, &dw->rd_ch_cnt);

	dw_hdma_debugfs_regs();
}

void dw_hdma_debugfs_off(struct dw_hdma_chip *chip)
{
	debugfs_remove_recursive(base_dir);
}

#endif /* CONFIG_DEBUG_FS */
