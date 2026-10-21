/*
 * (C) Copyright 2022, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * Change Logs:
 * Date           Author           Notes
 * 2022-02-24     Zhu Shiqiang     Initialize.
 */

/**
 * @brief   ipc debug header file
 * @date    2022-02-24
 */

#ifndef __IPC_PRINTK_H__
#define __IPC_PRINTK_H__

#undef LOG_TAG
#define LOG_TAG		"IPC"
#include <ulog.h>

#define ipc_err(fmt, ...)					\
	LOG_E("[error]: %s(%d): "				\
		fmt, __func__, __LINE__, ##__VA_ARGS__)

#define ipc_warn(fmt, ...)					\
	LOG_W("[warning]: %s(%d): "				\
		fmt, __func__, __LINE__, ##__VA_ARGS__)

#define ipc_info(fmt, ...)					\
	LOG_I("[info]: %s(%d): "				\
		fmt, __func__, __LINE__, ##__VA_ARGS__)

/* #define IPC_DRV_DEBUG */

#ifdef IPC_DRV_DEBUG
#define ipc_debug(fmt, ...)					\
	LOG_D("[debug]: %s(%d): "				\
		fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define ipc_debug(fmt, ...)	{}
#endif
#endif /* __IPC_PRINTK_H__ */
