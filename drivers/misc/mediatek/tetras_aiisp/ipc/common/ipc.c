/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * Change Logs:
 * Date           Author        Notes
 * 2022-01-21     Zhu Shiqiang  Initialize.
 */

/**
 * @brief   IPC Interface
 * @date    2022-01-21
 */

#include "ipc_ring_buffer.h"
#include "ipc_config.h"
#include "ipcm.h"
#include "ipc.h"
#include "ipc_printk.h"
#include "ipc_errno.h"

#define UINIT_BUF_SIZE		16
#define IPC_BUF_NUM		100

int ipc_send(struct ipc_channel *chan, void *buf, uint32_t size)
{
	struct ipcm_config ipcm_cfg;
	struct chan_config *chan_cfg = chan->chan_cfg;

	if (chan->chan_cfg->srcid != chan->mst) {
		ipc_err("%s can't be send channel\n", chan->chan_cfg->name);
		return ERR_INVAL;
	}

	ipcm_cfg.sourceid = chan_cfg->srcid;
	ipcm_cfg.destid = chan_cfg->dstid;
	ipcm_cfg.mboxid = chan_cfg->mboxid;
	ipcm_cfg.mbox_buf = buf;
	ipcm_cfg.size = size;

	return ipcm_send(&ipcm_cfg);
}

/* read data from local ringbuffer */
int ipc_recv(struct ipc_channel *chan, void *buf, uint32_t size)
{
	if (chan->chan_cfg->dstid != chan->mst) {
		ipc_err("%s can't be recv channel\n", chan->chan_cfg->name);
		return ERR_INVAL;
	}

	return ring_buffer_read(&chan->rb_head, buf, size);
}

/* write data to local ringbuffer from mbox data register */
int ipc_data_store(struct ipc_channel *chan)
{
	struct chan_config *chan_cfg = chan->chan_cfg;

	return ring_buffer_write(&chan->rb_head, ipcm_dr_adr(chan_cfg->mboxid),
				 UINIT_BUF_SIZE);
}

void ipc_release(struct ipc_channel *chan)
{
	struct chan_config *chan_cfg = chan->chan_cfg;

	ipcm_release(chan_cfg->mboxid);
}

int ipc_irq_status(void)
{
	return ipcm_irq_status();
}

int ipc_irq_clear(struct ipc_channel *chan, uint32_t ack_mode)
{
	struct chan_config *chan_cfg = chan->chan_cfg;

	return ipcm_irq_clear(chan_cfg->mboxid, ack_mode);
}

int ipc_release_and_irq_clear(struct ipc_channel *chan, uint32_t ack_mode)
{
	struct chan_config *chan_cfg = chan->chan_cfg;

	return ipcm_release_and_clear_irq(chan_cfg->mboxid, ack_mode);
}

void ipc_reset(void)
{
	ipcm_reset();
}

void ipc_send_ack(struct ipc_channel *chan, uint32_t ack_mode)
{
	struct chan_config *chan_cfg = chan->chan_cfg;

	ipcm_send_ack(chan_cfg->mboxid, ack_mode);
}

uint32_t ipc_find_chan_by_name(const struct chan_config *chan_cfg, const char *name)
{
	int i = 0;

	for (i = 0; i < IPC_CHANID_MAX; i++) {
		if (!strcmp(chan_cfg[i].name, name)) {
			return i;
		}
	}

	return IPC_CHANID_MAX;
}

int ipc_chan_close(struct ipc_channel *chan)
{
	return ring_buffer_destory(&chan->rb_head);
}

int ipc_chan_open(struct ipc_channel *chan)
{
	return ring_buffer_init(&chan->rb_head, UINIT_BUF_SIZE, IPC_BUF_NUM);
}
