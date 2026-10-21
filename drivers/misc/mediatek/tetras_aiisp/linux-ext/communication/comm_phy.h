/*
 * (C) Copyright 2021-2022, Shenzhen Tetras.AI Technology Co., Ltd
 */
/**
 * @brief   header of  ring buffer
 * @date    2022-2-28
 */

#ifndef __COMM_PHY_H__
#define __COMM_PHY_H__

int comm_phy_read(u32 ahb_addr, char *buffer, u32 buf_size, u32 phy_type);
int comm_phy_write(u32 ahb_addr, char *buffer, u32 buf_size, u32 phy_type);

#endif /* __COMM_PHY_H__ */
