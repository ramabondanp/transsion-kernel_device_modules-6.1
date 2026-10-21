/*
 * (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * Change Logs:
 * Date           Author        Notes
 * 2022-01-21     Zhu Shiqiang  Initialize.
 */

/**
 * @brief   IPC SV
 * @date    2022-03-22
 */

#include <interrupt.h>
#include <irq.h>
#include "ipc_printk.h"
#include "ipc_io.h"
#include "ipc_dev.h"
#include "ipc_printk.h"

#define IPCDEV_NO_ACK		(0x0)
#define SV_MRIS_OFFSET		(0x800)
#define SV_IPCMRIS(X)		(IPC_BASE + SV_MRIS_OFFSET + 0x8 * (X) + 0x4)

static struct ipc_dev *sv_ipcdev;

void ipc_sv_work_mode_set(uint32_t ack_mode)
{
	sv_ipcdev->ack_mode = ack_mode;
}

static void ipc_sv_dev_send_ack(struct ipc_vdev *vdev)
{
	struct ipc_dev *ipcdev = vdev->dev;

	if (ipcdev->ack_mode == IPCDEV_NO_ACK)
		return;

	ipc_send_ack(&vdev->chan, ipcdev->ack_mode);

	rt_event_send(&ipcdev->event, BIT(0));
}

static void sv_ipc_irq_handler(int irq, void *dev)
{
	int ret, val, index;
	struct ipc_vdev *vdev = RT_NULL;
	struct ipc_dev *ipcdev = (struct ipc_dev *)dev;

	val = ipc_read(SV_IPCMRIS(IPC_INT_LINE1));
	ipc_debug("irq status 0x%x\n", val);
	while (val) {
		index = ffs(val) - 0x1;
		if (index >= IPC_VDEV_MAX) {
			ipc_warn("index(%d) is larger then max(%d)", index, IPC_VDEV_MAX);
			val &= (~(0x1 << index));
			continue;
		}

		vdev = ipcdev->vdev[index];
		if (!vdev) {
			ipc_warn("[error]: vedv is null, irq status: 0x%x", val);
			return;
		}

		ret = ipc_irq_clear(&vdev->chan, ipcdev->ack_mode);
		if (ret) {
			ipc_debug("auto-ack mode\n");
			return;
		}

		ret = ipc_data_store(&vdev->chan);

		if (ipcdev->ack_mode == IPCDEV_NO_ACK)
			ipc_release(&vdev->chan);

		vdev->ipc_event_flag = 1;
		wake_up(&vdev->ipc_event);

		ipc_sv_dev_send_ack(vdev);

		val &= (~(0x1 << index));
	}
}

void ipc_sv_init(struct ipc_dev *dev)
{
	sv_ipcdev = dev;

	rt_hw_interrupt_install(IPCM_INTR1, sv_ipc_irq_handler, dev, "ipc_int1");
}
