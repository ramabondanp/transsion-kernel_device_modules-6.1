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

#include <linux/module.h>

#define ipc_err(fmt, ...)					\
	pr_err("[error]: %s(%d): "				\
		fmt, __func__, __LINE__, ##__VA_ARGS__)

#define ipc_warn(fmt, ...)					\
	pr_warn("[warning]: %s(%d): "				\
		fmt, __func__, __LINE__, ##__VA_ARGS__)

#define ipc_info(fmt, ...)					\
	pr_info("[info]: %s(%d): "				\
		fmt, __func__, __LINE__, ##__VA_ARGS__)

#define ipc_debug(fmt, ...)					\
	pr_debug("[debug]: %s(%d): "				\
		fmt, __func__, __LINE__, ##__VA_ARGS__)
#endif /* __IPC_PRINTK_H__ */
