// SPDX-FileCopyrightText: 2024 Infineon Technologies AG
//
// SPDX-License-Identifier: MIT

/**
 * @file   helper.h
 * @date   January, 2021
 * @brief  Definition of platform functions
 */
#ifndef __HELPER_H__
#define __HELPER_H__

#include "authon_api.h"

/* CRC functions */
uint16_t crc16_gen(uint8_t *data, uint16_t len);

#endif /* __HELPER_H__ */
