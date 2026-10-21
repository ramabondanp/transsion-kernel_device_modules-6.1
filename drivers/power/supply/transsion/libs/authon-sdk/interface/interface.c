// SPDX-FileCopyrightText: 2024 Infineon Technologies AG
//
// SPDX-License-Identifier: MIT

/**
 * @file   interface.c
 * @date   January, 2021
 * @brief  Implementation of communication interface
 */
#include "interface/interface.h"
#include "interface/register.h"
#include "interface/swi_bus_command.h"

/**
* @brief Write data to register. Some registers can only be accessed after host authentication.
* Certain register write will requires NVM write and will requires NVM programming delay.
* @param address address of the device
* @param data Pointer to data buffer
* @param wr_len Length of data to be write into the register
*/
uint16_t intf_write_register(struct swi_context *ctx, uint16_t address,
			     uint8_t *data, uint8_t wr_len)
{
	if (data == NULL || wr_len == 0) {
		return SDK_E_INPUT;
	}

	swi_irq_disable(ctx);
	swi_bus_command_era(ctx, BYTE_HIGH(address));
	swi_bus_command_wra(ctx, BYTE_LOW(address));
	swi_bus_command_wd(ctx, wr_len, data);
	swi_irq_restore(ctx);

	return SDK_INTERFACE_SUCCESS;
}

/**
* @brief Read_Register. Some registers can only be accessed after host authentication.
* @param address Address to be read
* @param data Pointer to return data buffer
* @param rd_len Length of data to be read from the register
*/
uint16_t intf_read_register(struct swi_context *ctx, uint16_t address,
			    uint8_t *data, uint8_t rd_len)
{
	uint16_t len;
	uint16_t ret = SDK_INIT;

	len = rd_len;
	if (data == NULL) {
		return SDK_E_INPUT;
	}

	// for NVM READ
	if (address < 0x4000) {
		if (len == 0x00) {
			ret = intf_write_register(ctx, ON_SFR_SWI_RD_LEN,
						  (uint8_t *)&len, 1);
			len = 256;
		} else {
			//set length to read
			ret = intf_write_register(ctx, ON_SFR_SWI_RD_LEN,
						  (uint8_t *)&len, 1);
		}
		if (SDK_INTERFACE_SUCCESS != ret) {
			return ret;
		}
	} else {
		len = 1;
	}

	swi_irq_disable(ctx);

	/* send address to read from */
	swi_bus_command_era(ctx, BYTE_HIGH(address));
	ret = swi_bus_command_rra(ctx, BYTE_LOW(address), len, data);
	if (INF_SWI_SUCCESS != ret) {
		swi_irq_restore(ctx);
		return ret;
	}

	swi_irq_restore(ctx);

	return SDK_INTERFACE_SUCCESS;
}