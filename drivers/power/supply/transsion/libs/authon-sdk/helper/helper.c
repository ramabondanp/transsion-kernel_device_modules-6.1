// SPDX-FileCopyrightText: 2024 Infineon Technologies AG
//
// SPDX-License-Identifier: MIT

/**
 * @file   helper.c
 * @date   January, 2021
 * @brief  Implementation of common related platform functions
 */
#include "helper.h"
#include "interface/swi.h"

#define CRC16_POLYNOMIAL 0x8005

static uint8_t reflect_8(uint8_t in)
{
	uint8_t resByte = 0;
	int i = 0;

	for (i = 0; i < 8; i++) {
		if ((in & (1 << i)) != 0) {
			resByte |= (uint8_t)(1 << (7 - i));
		}
	}

	return resByte;
}

static uint16_t reflect_16(uint16_t val)
{
	uint16_t resVal = 0;
	int i = 0;

	for (i = 0; i < 16; i++) {
		if ((val & (1 << i)) != 0) {
			resVal |= (uint16_t)(1 << (15 - i));
		}
	}

	return resVal;
}

/**
* @brief Generates CRC required for SWI
* @param data data input to be calculated
* @param len number of bytes 
*/
uint16_t crc16_gen(uint8_t *data, uint16_t len)
{
	uint16_t i, j;
	uint16_t crc = 0xffff;
	uint8_t data_reflet;

	if (len == 0)
		return (~crc);

	for (i = 0; i < len; i++) {
		data_reflet = reflect_8(data[i]);
		crc ^= data_reflet << 8; /* Move byte into MSB of 16-bit CRC */

		for (j = 0; j < 8; j++) {
			if ((crc & 0x8000) != 0) { /* Test for MSB = bit 15 */
				crc = ((crc << 1) ^ CRC16_POLYNOMIAL);
			} else {
				crc <<= 1;
			}
		}
	}
	crc = reflect_16(crc);
	crc = crc ^ 0xffff;

	return (crc);
}
