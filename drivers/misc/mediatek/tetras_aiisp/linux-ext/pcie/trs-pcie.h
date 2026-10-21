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

#ifndef _TRS_PCIE_H
#define _TRS_PCIE_H

#include <linux/pci.h>
#include <linux/types.h>
#include <linux/pci-epf.h>
#include "dw-hdma-drv.h"

/* TBD, currently is default value */
#define TETRAS_PCI_VENDOR_ID		0x16c3
#define TETRAS_PCI_DEVICE_ID		0xabcd

#define TETRAS_MIN_VEC_NUM		8
#define TETRAS_MAX_VEC_NUM		8

#define TETRAS_DMA_IRQ			0 /* pcie hdma interrupt */
#define TETRAS_MSI_IRQ_1		1 /* reserved */
#define TETRAS_MSI_IRQ_2		2 /* reserved */
#define TETRAS_MSI_IRQ_3		3 /* reserved */
#define TETRAS_MSI_IRQ_4		4 /* reserved */
#define TETRAS_MSI_IRQ_5		5 /* reserved */
#define TETRAS_MSI_IRQ_6		6 /* reserved */
#define TETRAS_MSI_IRQ_7		7 /* reserved */

/* For AIISP SoC DMA Wr/Rd test */
#define TETRAS_SOC_DMA_TEST

/*
 **************************************************************
 * This part is tetras specific memory & registers definition,
 * such as memory map, PCIe subysystem etc.
 * Should sync with Endpoint definitions!
 **************************************************************
 */

/* Inbound: DBI PCIe CDM space */
#define IATU_BAR0_TO_IB0_REGION_BASE	0x56000000
#define IATU_BAR0_TO_IB0_REGION_SIZE	SZ_4M

/* Inbound: Internal RAM space */
#define IATU_BAR1_TO_IB1_REGION_BASE	0x20000000
#define IATU_BAR1_TO_IB1_REGION_SIZE	SZ_2M

/* Inbound: Internal registers space */
#define IATU_BAR2_TO_IB2_REGION_BASE	0x40000000
#define IATU_BAR2_TO_IB2_REGION_SIZE	SZ_128M

/* Inbound: Non-prefetch DDR MEM32 space */
#define IATU_BAR3_TO_IB3_REGION_BASE	0x60000000
#define IATU_BAR3_TO_IB3_REGION_SIZE	SZ_256M

/* Inbound: Prefetch DDR MEM64 space */
#define IATU_BAR4_TO_IB4_REGION_BASE	(0x60000000 + SZ_128M)
#define IATU_BAR4_TO_IB4_REGION_SIZE	SZ_16M

/* Outbound: PCIe external slave space */
#define IATU_OB0_REGION_BASE		0x90000000
#define IATU_OB0_REGION_SIZE		SZ_128M

/* Tetras PCIe DBI base address */
#define TETRAS_PCIE_DBI_BASE		0x56000000
#define TETRAS_PCIE_DBI_SIZE		SZ_4K

/* Tetras PCIe DBI2 base address */
#define TETRAS_PCIE_DBI2_BASE		0x56100000
#define TETRAS_PCIE_DBI2_SIZE		SZ_4K

/* Tetras PCIe Sub System Bus base address */
#define TETRAS_PCIE_SS_BASE		0x58000000
#define TETRAS_PCIE_SS_SIZE		SZ_512K

/* PCIE_SS_CFG registers offset */
#define PCIE_SII_CTRL_HDR		0x40000

#define PE0_GEN_CTRL_3			0x40018
#define  PE0_LTSSM_EN			BIT(0)

#define PE0_GEN_CTRL_4			0x4001c
#define  LTSSM_EN_CLR_MASK		BIT(30)

#define PE0_PM_CTRL			0x40030
#define  PM_PME_EN			BIT(31)
#define  READY_ENTER_L23		BIT(19)
#define  EXIT_ASPM_L1			BIT(18)
#define  ENTER_ASPM_L1			BIT(17)

#define PE0_PM_STS			0x40034
#define  PM_LINKST_L2_EXIT		BIT(15)
#define  PM_LINKST_IN_L2		BIT(14)
#define  PM_LINKST_IN_L1SUB		BIT(13)
#define  PM_LINKST_IN_L1		BIT(12)
#define  PM_LINKST_IN_L0S		BIT(11)
#define  PM_CURNT_STATE_MASK		0x0700
#define  PM_CURNT_STATE_SHIFT		8
#define  PM_DSTATE_MASK			0x0007
#define  PM_DSTATE_SHIFT		0

#define PE0_MSI_GEN_CTRL		0x40058

#define PE0_INT_STS			0x4005c
#define  INTERNAL_INTD_STS		BIT(11)
#define  INTERNAL_INTB_STS		BIT(10)
#define  INTERNAL_INTC_STS		BIT(9)
#define  INTERNAL_INTA_STS		BIT(8)
#define  INTERNAL_INTX_SHIFT		8

#define PE0_RX_MSG_INT_STS		0x40080
#define PE0_ERR_INT_STS			0x40084
#define PE0_MISC_INT_STS		0x40088

#define PE0_RX_MSG_INT_CTRL		0x40090
#define PE0_ERR_INT_CTRL		0x40094
#define PE0_MISC_INT_CTRL		0x40098

#define PE0_LTR_MSG_REQ			0x401cc

#define PE0_LINK_DBG_2			0x40304
#define  CDM_IN_RESET			BIT(24)

#define APB_CLKFREQ_TIMEOUT		0x46014

#define SS_RST_CTRL_1			0x46048
#define  PE0_HOLD_PHY_RST		BIT(4)
#define  PE0_PERST_PHY_EN		BIT(3)
#define  PE0_SOFT_PHY_RESET		BIT(2)
#define  PE0_SOFT_WARM_RESET		BIT(1)
#define  PE0_SOFT_COLD_RESET		BIT(0)

#define PCIE_PHY_GEN_CTRL_2		0x47014
#define  PHY0_REF_USE_PAD		BIT(1)

#define PCIE_PHY_GEN_CTRL_3		0x47018
#define  PHY0_SRAM_EXT_LD_DONE		BIT(1)
#define  PHY0_SRAM_INIT_DONE		BIT(7)

#define PCIE_PHY_COMMON_CTRL		0x4702c
#define  PHY_EXT_CTRL_SEL		BIT(16)

#define PTM_INFO_CFG			0x7002c
#define  PTM_MANUAL_UPDATE		BIT(1)
#define  PTM_AUTO_UPDATE		BIT(0)

#define PTM_INFO_RPT0			0x70030
#define  PTM_REQ_REPLAY_TX		BIT(7)
#define  PTM_REQ_DUP_RX			BIT(6)
#define  PTM_REQ_RSP_TIMEOUT		BIT(5)
#define  PTM_CLK_UPDATED		BIT(4)
#define  PTM_CLK_UPDATING		BIT(3)
#define  PTM_TRIGGER_ALLOWED		BIT(2)
#define  PTM_RSP_RDY_VALIDATE		BIT(1)
#define  PTM_CONTEXT_VALID		BIT(0)

#define PTM_INFO_RPT1_LO		0x70034
#define PTM_INFO_RPT1_HI		0x70038

#define PTM_INFO_RPT2_LO		0x7003c
#define PTM_INFO_RPT2_HI		0x70040

#define LEGACY_INT_CFG			0x70044
#define  SYS_INT_SOFT			BIT(0)

#define MSI_INT_CFG			0x70048
#define  MSI_INT_SOFT			BIT(0)

#define EDMA_INT_STS			0x70074

/* BAR[0:3] is non-prefetch MEM32 and BAR[4:5] is prefetch MEM64 */
enum trs_pcie_bar {
	PCIE_CDM_BAR		= BAR_0,
	TCM_SRAM_BAR		= BAR_1,
	PERI_CFG_BAR		= BAR_2,
	DDR_BAR			= BAR_3,
	DDR_PREF_BAR		= BAR_4,
};

struct trs_pci_device {
	struct pci_dev		*pdev;
	struct dw_hdma_chip	*chip;
	void __iomem		*bar_vaddr[BAR_5];
	void __iomem		*pcie_ss_vaddr;

	u8			num_irqs;
	u8			event_irq;
	u8			dma_irq;
};

#endif /* _TRS_PCIE_H */
