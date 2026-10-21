/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author         Notes
 * 2023-08-13     suntongce      Initialize.
 */

/**
 * @brief   Tetras communication device driver uapi
 * @date    2023-08-13
 */

#ifndef __UAPI_COMM_DEV_IO_H__
#define __UAPI_COMM_DEV_IO_H__

#include <linux/types.h>
#include <linux/ioctl.h>

#define COMM_DRV_MAGIC			0x43			/* "C" */
#define COMM_DRV_WAIT_READY_TIMEOUT	_IOW(COMM_DRV_MAGIC,  0x01, uint32_t)
#define COMM_DRV_REQ_SUSPEND		_IOW(COMM_DRV_MAGIC,  0x02, uint32_t)
#define COMM_DRV_GET_SUSPEND_STATE	_IOR(COMM_DRV_MAGIC,  0x03, uint32_t)
#define COMM_DRV_SET_ENTITY_ID		_IOW(COMM_DRV_MAGIC,  0x04, uint32_t)

#endif /* __UAPI_COMM_DEV_IO_H__ */
