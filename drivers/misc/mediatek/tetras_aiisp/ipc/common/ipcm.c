/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * Change Logs:
 * Date           Author        Notes
 * 2022-01-21     Zhu Shiqiang  Initialize.
 */

/**
 * @brief   IPCM Interface
 * @date    2022-01-21
 */

#include "ipc_config.h"
#include "ipc_io.h"
#include "ipc_printk.h"
#include "ipc_mem.h"
#include "ipc_misc.h"
#include "ipcm.h"

static int make_ipcm_resource_acquire_reg_sequence(struct ipcm_config *ipcm_cfg,
						   struct ipcm_ctrl_registers *ctrl_regs)
{
	uint32_t try = 0;
	uint32_t ret;

	/* wait until source id is released */
	while (try < IPCM_TRY_TIMES) {
		ret = ipc_read_nbytes(IPCM0SOURCE(ipcm_cfg->mboxid), sizeof(*ctrl_regs),
				      (uint8_t *)ctrl_regs);
		if (ret != sizeof(*ctrl_regs)) {
			ipc_err("ipc read: 0x%08x falied, ret %d\n",
				IPCM0SOURCE(ipcm_cfg->mboxid), ret);
			return -1;
		}

		if (ctrl_regs->source == 0)
			break;

		ipc_mdelay(TIME_DELAY);
		try++;
	}

	if (try >= IPCM_TRY_TIMES) {
		ipc_err("ipcm mailbox[%d] is busy! "
			"source adr: 0x%x val: 0x%x\n",
			ipcm_cfg->mboxid, IPCM0SOURCE(ipcm_cfg->mboxid),
			ctrl_regs->source);
		return -1;
	}

	ctrl_regs->source = ipcm_cfg->sourceid;
	if (ipcm_cfg->ipcm_mode == IPCM_AUTO_ACK)
		ctrl_regs->mode = EN_AUTO_ACK;
	ctrl_regs->dset = ipcm_cfg->destid;
	ctrl_regs->mset = ipcm_cfg->sourceid | ipcm_cfg->destid;

	return 0;
}

void ipcm_send_ack(uint32_t mboxid, uint32_t ack_mode)
{
	switch (ack_mode) {
	case IPCM_NO_ACK:
		break;
	case IPCM_MANUAL_ACK:
		ipc_write(IPCM_ACK_PATTERN, IPCM0DR0(mboxid));
		ipc_write(IRQ_TO_SRC, IPCM0SEND(mboxid));
		break;
	case IPCM_AUTO_ACK:
		ipc_write(IPCM_ACK_PATTERN, IPCM0DR0(mboxid));
		ipc_write(IRQ_TO_SRC, IPCM0DCLEAR(mboxid));
		break;
	default:
		break;
	}
}

void ipcm_release(uint32_t mboxid)
{
	ipc_write(0x0, IPCM0SOURCE(mboxid));
}

void ipcm_reset(void)
{
	/* assert */
	ipc_write(IPCM_RST, AON_RST_EN1);

	/* disassert */
	ipc_write(IPCM_RST, AON_RST_DIS1);
}

int ipcm_send(struct ipcm_config *ipcm_cfg)
{
	int ret = 0;
	struct ipcm_ctrl_registers ctrl_regs;

	if (ipcm_cfg->size > MBOX_DR_SIZE) {
		ipc_err("ipcm_cfg size[%d] error!\n", ipcm_cfg->size);
		ret = -1;
		goto out;
	}

	ret = make_ipcm_resource_acquire_reg_sequence(ipcm_cfg, &ctrl_regs);
	if (ret) {
		ipc_err("mbox alloc failed!\n");
		goto out;
	}

	ipc_memcpy(&ctrl_regs.dr[0], ipcm_cfg->mbox_buf, ipcm_cfg->size, NONE_MODE);
	ret = ipc_write_nbytes(IPCM0SOURCE(ipcm_cfg->mboxid), sizeof(ctrl_regs),
			       (uint8_t *)&ctrl_regs);
	if (ret != sizeof(ctrl_regs)) {
		ipc_err("ipc write 0x%x with %zu bytes failed, ret %d!\n",
			IPCM0SOURCE(ipcm_cfg->mboxid), sizeof(ctrl_regs), ret);
		goto out;
	}


	if (ipc_read(IPCM0SOURCE(ipcm_cfg->mboxid)) != ipcm_cfg->sourceid) {
		ipc_err("ipcm sourece[0x%x] acquire failed, set 0x%x, get 0x%x\n",
			ipcm_cfg->sourceid, ipcm_cfg->sourceid,
			ipc_read(IPCM0SOURCE(ipcm_cfg->mboxid)));
		return -1;
	}

	ipc_write(IRQ_TO_DST, IPCM0SEND(ipcm_cfg->mboxid));

	return 0;

out:
	return ret;
}

void *ipcm_dr_adr(uint32_t mboxid)
{
	return (uint32_t *)(long)IPCM0DR0(mboxid);
}

int ipcm_irq_status(void)
{
	return ipc_read(IPCMRIS(IPC_INT_LINE));
}

/*
 * return:
 * 0: no-ack mode
 * 1: manual-ack mode or auto-ack mode
 */
int ipcm_irq_clear(uint32_t mboxid, uint32_t ack_mode)
{
	/* clear no-ack mode pending bit */
	if (ack_mode == IPCM_NO_ACK) {
		ipc_write(0x0, IPCM0SEND(mboxid));
		return 0;
	}

	/* clear manual-ack or auto-ack  mode pending bit */
	if ((ack_mode == IPCM_MANUAL_ACK || ack_mode == IPCM_AUTO_ACK)
	    && ipc_read(IPCM0DR0(mboxid)) != IPCM_ACK_PATTERN) {
		ipc_write(0x0, IPCM0SEND(mboxid));
		return 0;
	}

	if (ipc_read(IPCM0DR0(mboxid)) == IPCM_ACK_PATTERN)
		ipc_write(0x0, IPCM0SEND(mboxid));

	return ACK_HANDLED;
}

/*
 * return:
 * 0: no-ack mode
 * 1: manual-ack mode or auto-ack mode
 */
int ipcm_release_and_clear_irq(uint32_t mboxid, uint32_t ack_mode)
{
	uint8_t reg_cache[IPCM_REG_CTRL_CACHE_SZ] = {0};
	uint32_t ack_pattern;
	int ret = 0;
	/* clear no-ack mode pending bit */
	if (ack_mode == IPCM_NO_ACK) {
		reg_cache[SEND_OFFSET] = 0;
		goto exit;
	}
	ack_pattern = ipc_read(IPCM0DR0(mboxid));
	/* clear manual-ack or auto-ack  mode pending bit */
	if ((ack_mode == IPCM_MANUAL_ACK || ack_mode == IPCM_AUTO_ACK)
	    && ack_pattern != IPCM_ACK_PATTERN) {
		reg_cache[SEND_OFFSET] = 0;
		goto exit;
	}

	if (ack_pattern  == IPCM_ACK_PATTERN)
		reg_cache[SEND_OFFSET] = 0;

	ret = ACK_HANDLED;

exit:
	ipc_write_nbytes(IPCM0SOURCE(mboxid), IPCM_REG_CTRL_CACHE_SZ, reg_cache);

	return ret;
}
