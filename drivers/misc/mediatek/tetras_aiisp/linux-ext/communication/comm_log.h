/*
 * (C) Copyright 2021-2022, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */
/**
 * @brief   header of  ring buffer
 * @date    2022-2-28
 */

#ifndef __COMM_LOG_H__
#define __COMM_LOG_H__

#define comm_err(fmt, ...)				\
	pr_info("comm error %s(%d): "				\
		fmt, __func__, __LINE__, ##__VA_ARGS__)
#define comm_info(fmt, ...)				\
	pr_info("comm info %s(%d): "				\
		fmt, __func__, __LINE__, ##__VA_ARGS__)
#define comm_debug(fmt, ...)				\
	pr_debug("comm dbg %s(%d): "				\
		fmt, __func__, __LINE__, ##__VA_ARGS__)

#endif /* __COMM_LOG_H__ */
