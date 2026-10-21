// SPDX-FileCopyrightText: 2024 Infineon Technologies AG
//
// SPDX-License-Identifier: MIT

/**
 * @file   interface.h
 * @date   January, 2021
 * @brief  Implementation of chip communication interface
 */
#ifndef __INTERFACE_H__
#define __INTERFACE_H__

#include "interface/swi.h"

uint16_t intf_write_register(struct swi_context *ctx, uint16_t address,
			     uint8_t *data, uint8_t wr_len);
uint16_t intf_read_register(struct swi_context *ctx, uint16_t address,
			    uint8_t *data, uint8_t rd_len);

#endif /* __INTERFACE_H__ */
