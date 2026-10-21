/*
 * (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * Change Logs:
 * Date           Author           Notes
 * 2022-2-24      Zhu Shiqiang     Initialize.
 */

/**
 * @brief   ipc misc
 * @date    2021-12-24
 */

#include <linux/delay.h>

void ipc_mdelay(uint32_t time)
{
	msleep(time);
}
