/*
 * (C) Copyright 2021-2022, Shenzhen Tetras.AI Technology Co., Ltd
 */
/**
 * @brief   header of  ring buffer
 * @date    2022-2-28
 */

#ifndef __COMM_SPI_H__
#define __COMM_SPI_H__

int comm_spi_read(u32 ahb_addr, char *buffer, u32 buf_size);
int comm_spi_write(u32 ahb_addr, char *buffer, u32 buf_size);

#endif /* __COMM_SPI_H__ */
