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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/msi.h>
#include <linux/bitfield.h>

#include "dw-hdma-drv.h"
#include "trs-pcie.h"

#define DEBUG

#define DW_PCIE_VSEC_DMA_ID			0x6
#define DW_PCIE_VSEC_DMA_BAR			GENMASK(10, 8)
#define DW_PCIE_VSEC_DMA_MAP			GENMASK(2, 0)
#define DW_PCIE_VSEC_DMA_WR_CH			GENMASK(9, 0)
#define DW_PCIE_VSEC_DMA_RD_CH			GENMASK(25, 16)

#define HDMA_REMOTE_PHYS_START			IATU_BAR3_TO_IB3_REGION_BASE

#define DW_BLOCK(a, b, c) \
	{ \
		.bar = a, \
		.off = b, \
		.sz = c, \
	},

struct dw_hdma_block {
	enum pci_barno			bar;
	off_t				off;
	size_t				sz;
};

struct dw_hdma_pcie_data {
	/* HDMA registers location */
	struct dw_hdma_block		rg;
	/* HDMA memory linked list location */
	struct dw_hdma_block		ll_wr[HDMA_MAX_WR_CH];
	struct dw_hdma_block		ll_rd[HDMA_MAX_RD_CH];
	/* HDMA memory data location */
	struct dw_hdma_block		dt_wr[HDMA_MAX_WR_CH];
	struct dw_hdma_block		dt_rd[HDMA_MAX_RD_CH];
	/* Other */
	enum dw_hdma_map_format		mf;
	u8				irqs;
	u16				wr_ch_cnt;
	u16				rd_ch_cnt;
};

static const struct dw_hdma_pcie_data dw_hdma_data = {
	/* HDMA registers location */
	.rg.bar				= (enum pci_barno)PCIE_CDM_BAR,
	.rg.off				= 0x00001000,	/*  4 Kbytes */
	.rg.sz				= 0x00002000,	/*  8 Kbytes */
	/* HDMA memory linked list location */
	.ll_wr = {
		/* Channel 0 - BAR 3, offset 0 Mbytes, size 2 Kbytes */
		DW_BLOCK((enum pci_barno)DDR_BAR, 0x00000000, 0x00000800)
		/* Channel 1 - BAR 3, offset 1 Mbytes, size 2 Kbytes */
		DW_BLOCK((enum pci_barno)DDR_BAR, 0x00100000, 0x00000800)
	},
	.ll_rd = {
		/* Channel 0 - BAR 3, offset 2 Mbytes, size 2 Kbytes */
		DW_BLOCK((enum pci_barno)DDR_BAR, 0x00200000, 0x00000800)
		/* Channel 1 - BAR 3, offset 3 Mbytes, size 2 Kbytes */
		DW_BLOCK((enum pci_barno)DDR_BAR, 0x00300000, 0x00000800)
	},
	/* HDMA memory data location */
	.dt_wr = {
		/* Channel 0 - BAR 3, offset 4 Mbytes, size 2 Mbytes */
		DW_BLOCK((enum pci_barno)DDR_BAR, 0x00400000, 0x00200000)
		/* Channel 1 - BAR 3, offset 6 Mbytes, size 2 Mbytes */
		DW_BLOCK((enum pci_barno)DDR_BAR, 0x00600000, 0x00200000)
	},
	.dt_rd = {
		/* Channel 0 - BAR 3, offset 8 Mbytes, size 2 Mbytes */
		DW_BLOCK((enum pci_barno)DDR_BAR, 0x00800000, 0x00200000)
		/* Channel 1 - BAR 3, offset 10 Mbytes, size 2 Mbytes */
		DW_BLOCK((enum pci_barno)DDR_BAR, 0x00a00000, 0x00200000)
	},
	/* Other */
	.mf				= MF_HDMA_NATIVE,
	.irqs				= 1,
	.wr_ch_cnt			= HDMA_MAX_WR_CH,
	.rd_ch_cnt			= HDMA_MAX_RD_CH,
};

static int dw_hdma_pcie_irq_vector(struct device *dev, unsigned int nr)
{
	return pci_irq_vector(to_pci_dev(dev), nr);
}

static const struct dw_hdma_core_ops dw_hdma_pcie_core_ops = {
	.irq_vector = dw_hdma_pcie_irq_vector,
};


u16 pci_find_vsec_capability(struct pci_dev *pdev, u16 vendor, int cap)
{
	u16 vsec = 0;
	u32 header;

	if (vendor != pdev->vendor)
		return 0;

	while ((vsec = pci_find_next_ext_capability(pdev, vsec,
						    PCI_EXT_CAP_ID_VNDR))) {
		if (pci_read_config_dword(pdev, vsec + PCI_VNDR_HEADER,
					  &header) == PCIBIOS_SUCCESSFUL &&
		    PCI_VNDR_HEADER_ID(header) == cap)
			return vsec;
	}

	return 0;
}

static void dw_hdma_pcie_get_vsec_dma_data(struct pci_dev *pdev,
					   struct dw_hdma_pcie_data *pdata)
{
	u32 val, map;
	u16 vsec;
	u64 off;

	vsec = pci_find_vsec_capability(pdev, PCI_VENDOR_ID_SYNOPSYS,
					DW_PCIE_VSEC_DMA_ID);
	if (!vsec)
		return;

	pci_read_config_dword(pdev, vsec + PCI_VNDR_HEADER, &val);
	if (PCI_VNDR_HEADER_REV(val) != 0x00 ||
	    PCI_VNDR_HEADER_LEN(val) != 0x18)
		return;

	pci_info(pdev, "Detected PCIe Vendor-Specific Extended Capability DMA\n");
	pci_read_config_dword(pdev, vsec + 0x8, &val);
	map = FIELD_GET(DW_PCIE_VSEC_DMA_MAP, val);
	if (map != MF_HDMA_COMPAT_PL &&
	    map != MF_HDMA_COMPAT &&
	    map != MF_HDMA_NATIVE_PL &&
	    map != MF_HDMA_NATIVE)
		return;

	pdata->mf = map;
	pdata->rg.bar = FIELD_GET(DW_PCIE_VSEC_DMA_BAR, val);

	pci_read_config_dword(pdev, vsec + 0xc, &val);
	pdata->wr_ch_cnt = min_t(u16, pdata->wr_ch_cnt,
				 FIELD_GET(DW_PCIE_VSEC_DMA_WR_CH, val));
	pdata->rd_ch_cnt = min_t(u16, pdata->rd_ch_cnt,
				 FIELD_GET(DW_PCIE_VSEC_DMA_RD_CH, val));

	pci_read_config_dword(pdev, vsec + 0x14, &val);
	off = val;
	pci_read_config_dword(pdev, vsec + 0x10, &val);
	off <<= 32;
	off |= val;
	pdata->rg.off = off;
}

int dw_hdma_pcie_probe(struct pci_dev *pdev)
{
	struct dw_hdma_pcie_data *pdata = (void *)&dw_hdma_data;
	struct dw_hdma_pcie_data vsec_data;
	struct device *dev = &pdev->dev;
	struct trs_pci_device *trs_dev = pci_get_drvdata(pdev);
	struct dw_hdma_chip *chip;
	struct dw_hdma *dw;
	int err, nr_irqs;
	int i;

	pci_info(pdev, "HDMA driver probing ...\n");

	memcpy(&vsec_data, pdata, sizeof(struct dw_hdma_pcie_data));

	/*
	 * Tries to find if exists a PCIe Vendor-Specific Extended Capability
	 * for the DMA, if one exists, then reconfigures it.
	 */
	dw_hdma_pcie_get_vsec_dma_data(pdev, &vsec_data);

	/* DMA configuration */
	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err) {
		pci_err(pdev, "DMA mask 64 set failed\n");
		return -1;
	}

	/* Data structure allocation */
	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		pci_err(pdev, "chip devm_kzalloc failed\n");
		return -1;
	}

	trs_dev->chip = chip;

	dw = devm_kzalloc(dev, sizeof(*dw), GFP_KERNEL);
	if (!dw) {
		pci_err(pdev, "dw devm_kzalloc failed\n");
		goto err_free_chip;
	}

	/* Other irqs are used by aiisp device driver */
	nr_irqs = min_t(u8, 1, vsec_data.irqs);

	/* Data structure initialization */
	chip->dw = dw;
	chip->dev = dev;
	chip->id = pdev->devfn;
	chip->irq = pdev->irq;

	dw->mf = vsec_data.mf;
	dw->nr_irqs = nr_irqs;
	dw->ops = &dw_hdma_pcie_core_ops;
	dw->wr_ch_cnt = vsec_data.wr_ch_cnt;
	dw->rd_ch_cnt = vsec_data.rd_ch_cnt;

	/* Currently for dmatest use */
	dw->data_bar_phys_base = pdev->resource[DDR_BAR].start;

	dw->rg_region.vaddr = pcim_iomap_table(pdev)[vsec_data.rg.bar];
	if (!dw->rg_region.vaddr) {
		pci_err(pdev, "register bar iomap failed[bar%d]\n", vsec_data.rg.bar);
		goto err_free_dw;
	}

	dw->rg_region.vaddr += vsec_data.rg.off;
	dw->rg_region.paddr = pdev->resource[vsec_data.rg.bar].start;
	dw->rg_region.paddr += vsec_data.rg.off;
	dw->rg_region.sz = vsec_data.rg.sz;

	for (i = 0; i < dw->wr_ch_cnt; i++) {
		struct dw_hdma_region *ll_region = &dw->ll_region_wr[i];
		struct dw_hdma_region *dt_region = &dw->dt_region_wr[i];
		struct dw_hdma_block *ll_block = &vsec_data.ll_wr[i];
		struct dw_hdma_block *dt_block = &vsec_data.dt_wr[i];

		ll_region->vaddr = pcim_iomap_table(pdev)[ll_block->bar];
		if (!ll_region->vaddr) {
			pci_err(pdev, "WRCH linked list iomap failed[bar%d, ch%d]\n",
				ll_block->bar, i);
			goto err_free_dw;
		}

		ll_region->vaddr += ll_block->off;
		/* AIISP remote hdma phys address */
		ll_region->paddr = HDMA_REMOTE_PHYS_START + ll_block->off;
		ll_region->sz = ll_block->sz;

		dt_region->vaddr = pcim_iomap_table(pdev)[dt_block->bar];
		if (!dt_region->vaddr) {
			pci_err(pdev, "WRCH data block iomap failed[bar%d, ch%d]\n",
				dt_block->bar, i);
			goto err_free_dw;
		}

		dt_region->vaddr += dt_block->off;
		/* AIISP remote hdma phys address */
		dt_region->paddr = HDMA_REMOTE_PHYS_START + dt_block->off;
		dt_region->sz = dt_block->sz;
	}

	for (i = 0; i < dw->rd_ch_cnt; i++) {
		struct dw_hdma_region *ll_region = &dw->ll_region_rd[i];
		struct dw_hdma_region *dt_region = &dw->dt_region_rd[i];
		struct dw_hdma_block *ll_block = &vsec_data.ll_rd[i];
		struct dw_hdma_block *dt_block = &vsec_data.dt_rd[i];

		ll_region->vaddr = pcim_iomap_table(pdev)[ll_block->bar];
		if (!ll_region->vaddr) {
			pci_err(pdev, "RDCH linked list iomap failed[bar%d, ch%d]\n",
				ll_block->bar, i);
			goto err_free_dw;
		}

		ll_region->vaddr += ll_block->off;
		ll_region->paddr = HDMA_REMOTE_PHYS_START + ll_block->off;
		ll_region->sz = ll_block->sz;

		dt_region->vaddr = pcim_iomap_table(pdev)[dt_block->bar];
		if (!dt_region->vaddr) {
			pci_err(pdev, "RDCH data block iomap failed[bar%d, ch%d]\n",
				dt_block->bar, i);
			goto err_free_dw;
		}

		dt_region->vaddr += dt_block->off;
		dt_region->paddr = HDMA_REMOTE_PHYS_START + dt_block->off;
		dt_region->sz = dt_block->sz;
	}

	/* Debug info */
	if (dw->mf == MF_HDMA_COMPAT_PL)
		pci_info(pdev, "Version: HDMA Compatible Port Logic (0x%x)\n", dw->mf);
	else if (dw->mf == MF_HDMA_COMPAT)
		pci_info(pdev, "Version: HDMA Compatible (0x%x)\n", dw->mf);
	else if (dw->mf == MF_HDMA_NATIVE_PL)
		pci_info(pdev, "Version: HDMA Native Port Logic (0x%x)\n", dw->mf);
	else if (dw->mf == MF_HDMA_NATIVE)
		pci_info(pdev, "Version: HDMA Native (0x%x)\n", dw->mf);
	else
		pci_info(pdev, "Version: Unknown (0x%x)\n", dw->mf);

	pci_info(pdev, "Registers: BAR=%u, off=0x%.8lx, sz=0x%zx bytes,"
		 "addr(v=%p, p=%pa)\n",
		 vsec_data.rg.bar, vsec_data.rg.off, vsec_data.rg.sz,
		 dw->rg_region.vaddr, &dw->rg_region.paddr);


	for (i = 0; i < dw->wr_ch_cnt; i++) {
		pci_info(pdev, "L. List: WRITE CH%.2u, BAR=%u,"
			 "off=0x%.8lx, sz=0x%zx bytes, addr(v=%p, p=%pa)\n",
			 i, vsec_data.ll_wr[i].bar,
			 vsec_data.ll_wr[i].off, dw->ll_region_wr[i].sz,
			 dw->ll_region_wr[i].vaddr, &dw->ll_region_wr[i].paddr);

		pci_info(pdev, "Data: WRITE CH%.2u, BAR=%u,"
			 "off=0x%.8lx, sz=0x%zx bytes, addr(v=%p, p=%pa)\n",
			 i, vsec_data.dt_wr[i].bar,
			 vsec_data.dt_wr[i].off, dw->dt_region_wr[i].sz,
			 dw->dt_region_wr[i].vaddr, &dw->dt_region_wr[i].paddr);
	}

	for (i = 0; i < dw->rd_ch_cnt; i++) {
		pci_info(pdev, "L. List: READ CH%.2u, BAR=%u,"
			 "off=0x%.8lx, sz=0x%zx bytes, addr(v=%p, p=%pa)\n",
			 i, vsec_data.ll_rd[i].bar,
			 vsec_data.ll_rd[i].off, dw->ll_region_rd[i].sz,
			 dw->ll_region_rd[i].vaddr, &dw->ll_region_rd[i].paddr);

		pci_info(pdev, "Data: READ CH%.2u, BAR=%u,"
			 "off=0x%.8lx, sz=0x%zx bytes, addr(v=%p, p=%pa)\n",
			 i, vsec_data.dt_rd[i].bar,
			 vsec_data.dt_rd[i].off, dw->dt_region_rd[i].sz,
			 dw->dt_region_rd[i].vaddr, &dw->dt_region_rd[i].paddr);
	}

	dw->irq = devm_kcalloc(dev, nr_irqs, sizeof(*dw->irq), GFP_KERNEL);
	if (!dw->irq)
		goto err_free_dw;

	/* Starting HDMA driver */
	err = dw_hdma_probe(chip);
	if (err) {
		pci_err(pdev, "HDMA probe failed\n");
		goto err_free_irq;
	}

	pci_info(pdev, "Completed hdma driver probe.\n");

	return 0;

err_free_irq:
	devm_kfree(dev, dw->irq);

err_free_dw:
	devm_kfree(dev, dw);

err_free_chip:
	devm_kfree(dev, chip);

	return -1;
}
EXPORT_SYMBOL_GPL(dw_hdma_pcie_probe);

void dw_hdma_pcie_remove(struct pci_dev *pdev)
{
	struct trs_pci_device *trs_dev = pci_get_drvdata(pdev);
	struct dw_hdma_chip *chip = trs_dev->chip;
	int err;

	/* Stopping HDMA driver */
	err = dw_hdma_remove(chip);
	if (err)
		pci_warn(pdev, "can't remove device properly: %d\n", err);

	devm_kfree(&pdev->dev, chip->dw->irq);
	devm_kfree(&pdev->dev, chip->dw);
	devm_kfree(&pdev->dev, chip);
}
EXPORT_SYMBOL_GPL(dw_hdma_pcie_remove);
