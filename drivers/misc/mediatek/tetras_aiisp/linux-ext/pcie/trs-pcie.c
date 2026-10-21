/*
 * (C) Copyright 2021-2022, Shenzhen Tetras.AI Technology Co., Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Change Logs:
 * Date           Author         Notes
 * 2022-06-30     ShenJiming     Initialize.
 */

/**
 * @brief   Tetras AIISP PCIe device driver
 * @date    2022-06-30
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/types.h>
#include "../drivers/pci/pci.h"

#include "trs-pcie.h"

#define DEBUG
#define DRV_MODULE_NAME			"aiisp-pcie"

extern int dw_hdma_pcie_probe(struct pci_dev *pdev);
extern void dw_hdma_pcie_remove(struct pci_dev *pdev);

/* tetras pcie device data exported */
struct trs_pci_device *trs_dev_data;

static irqreturn_t trs_dev_irqhandler(int irq, void *dev_id)
{
	struct trs_pci_device *trs_dev = dev_id;

	pci_dbg(trs_dev->pdev, "**** Tetras device MSI[%d] interupt happened! **** \n", irq);

	return IRQ_HANDLED;
}

static struct pci_device_id trs_pci_tbl[] = {
	{
		PCI_DEVICE(TETRAS_PCI_VENDOR_ID, TETRAS_PCI_DEVICE_ID),
	},
	{0},
};
MODULE_DEVICE_TABLE(pci, trs_pci_tbl);

static int trs_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct trs_pci_device *trs_dev;
	u32 num_irqs;
	int irq;
	int i, rc;

#ifdef TETRAS_SOC_DMA_TEST
	void *cpu_vaddr_1, *cpu_vaddr_2;
	phys_addr_t cpu_paddr_1, cpu_paddr_2;
	dma_addr_t bus_addr_1, bus_addr_2;
#endif

	pci_info(pdev, "Tetras PCIe probing ...\n");
	trs_dev = devm_kzalloc(dev, sizeof(*trs_dev), GFP_KERNEL);
	if (!trs_dev) {
		pci_err(pdev, "Out of memory\n");
		return ENOMEM;
	}

	/* Export device data for the hdma test */
	trs_dev_data = trs_dev;

	/* Save pci device data */
	trs_dev->pdev = pdev;

	/* Set private data into pci device */
	pci_set_drvdata(pdev, trs_dev);

	rc = pci_enable_device(pdev);
	if (rc) {
		pci_err(pdev, "Cannot enable PCI device\n");
		goto out_free;
	}

	/* Mapping PCI BAR0/3 regions for access */
	rc = pcim_iomap_regions(pdev, (1 << PCIE_CDM_BAR) | (1 << DDR_BAR),
				pci_name(pdev));
	if (rc) {
		pci_err(pdev, "HDMA BAR I/O remapping failed[0x%x]\n", rc);
		goto out_disable_pdev;
	}

	pci_set_master(pdev);

	num_irqs = pci_alloc_irq_vectors(pdev, TETRAS_MIN_VEC_NUM,
					 TETRAS_MAX_VEC_NUM, PCI_IRQ_MSI | PCI_IRQ_LEGACY);
	if ((num_irqs < TETRAS_MIN_VEC_NUM) || (num_irqs > TETRAS_MAX_VEC_NUM)) {
		pci_err(pdev, "Cannot obtain PCI interrupts\n");
		goto out_unmap_regions;
	}
	trs_dev->num_irqs = num_irqs;
	trs_dev->dma_irq = TETRAS_DMA_IRQ;

	/* Validating if PCI interrupts were enabled */
	if (!pci_dev_msi_enabled(pdev)) {
		pci_err(pdev, "enable interrupt failed\n");
		goto out_free_irq_vector;
	}

	for (i = TETRAS_MSI_IRQ_1; i < num_irqs; i++) {
		/* Install device-related interrupt handler */
		irq = pci_irq_vector(pdev, i);
		rc = request_irq(irq, trs_dev_irqhandler,
				 IRQF_SHARED, DRV_MODULE_NAME, trs_dev);
		if (rc) {
			pci_err(pdev, "Cannot register device irq\n");
			goto out_free_irq;
		}
	}

	/* Init pcie hdma controller */
	dw_hdma_pcie_probe(pdev);

#ifdef TETRAS_SOC_DMA_TEST
#define TETRAS_DMA_MEM_SZ	0x1000000 /* 16MB */

	cpu_vaddr_1 = dma_alloc_coherent(&pdev->dev, TETRAS_DMA_MEM_SZ, &bus_addr_1, GFP_KERNEL);
	if (cpu_vaddr_1) {
		cpu_paddr_1 = vmalloc_to_pfn(cpu_vaddr_1) << PAGE_SHIFT;
		pci_info(pdev, "DMA-Wr-Test-MEM allocated [virt:0x%p, bus:0x%p, phys:0x%p]\n",
			 cpu_vaddr_1, bus_addr_1, cpu_paddr_1);
	} else {
		pci_err(pdev, "DMA-WR-Test-MEM allocate failed\n");
	}

	cpu_vaddr_2 = dma_alloc_coherent(&pdev->dev, TETRAS_DMA_MEM_SZ, &bus_addr_2, GFP_KERNEL);
	if (cpu_vaddr_2) {
		cpu_paddr_2 = vmalloc_to_pfn(cpu_vaddr_2) << PAGE_SHIFT;
		pci_info(pdev, "DMA-Rd-Test-MEM allocated [virt:0x%p, bus:0x%p, phys:0x%p]\n",
			 cpu_vaddr_2, bus_addr_2, cpu_paddr_2);
	} else {
		pci_err(pdev, "DMA-Rd-Test-MEM allocate failed\n");
	}
#endif

	pci_info(pdev, "Completed Tetras pcie probe.\n");
	return 0;

out_free_irq:
	for (i = TETRAS_MSI_IRQ_1; i < num_irqs; i++)
		free_irq(pci_irq_vector(pdev, i), trs_dev);

out_free_irq_vector:
	pci_free_irq_vectors(pdev);
	pci_clear_master(pdev);

out_unmap_regions:
	pcim_iounmap_regions(pdev, (1 << PCIE_CDM_BAR) | (1 << DDR_BAR));

out_disable_pdev:
	pci_disable_device(pdev);

out_free:
	devm_kfree(dev, trs_dev);

	return rc;
}
EXPORT_SYMBOL_GPL(trs_dev_data);

static void trs_pci_remove(struct pci_dev *pdev)
{
	u32 i;
	struct trs_pci_device *trs_dev = pci_get_drvdata(pdev);

	dw_hdma_pcie_remove(pdev);

	pcim_iounmap_regions(pdev, (1 << PCIE_CDM_BAR) | (1 << DDR_BAR));

	for (i = 0; i < trs_dev->num_irqs; i++)
		free_irq(pci_irq_vector(pdev, i), trs_dev);

	pci_free_irq_vectors(pdev);

	pci_disable_device(pdev);

	devm_kfree(&pdev->dev, trs_dev);
}

static struct pci_driver trs_pci_driver = {
	.name		= DRV_MODULE_NAME,
	.id_table	= trs_pci_tbl,
	.probe		= trs_pci_probe,
	.remove		= trs_pci_remove,
};

static int __init trs_pci_init(void)
{
	pr_info(KBUILD_MODNAME ": loaded.\n");

	return pci_register_driver(&trs_pci_driver);

}

static void __exit trs_pci_exit(void)
{
	pr_info(KBUILD_MODNAME ": unloaded.\n");

	return pci_unregister_driver(&trs_pci_driver);
}

module_init(trs_pci_init);
module_exit(trs_pci_exit);

MODULE_AUTHOR("shenjiming@tetras.ai");
MODULE_DESCRIPTION("Tetras AIISP PCIe Device Driver");
MODULE_LICENSE("GPL v2");
