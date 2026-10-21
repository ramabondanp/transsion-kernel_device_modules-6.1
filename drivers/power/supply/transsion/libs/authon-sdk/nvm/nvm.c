// SPDX-FileCopyrightText: 2024 Infineon Technologies AG
//
// SPDX-License-Identifier: MIT

/**
 * @file   nvm.c
 * @date   January, 2021
 * @brief  Implementation of NVM functions
 */
#include "nvm/nvm.h"
#include "interface/interface.h"

/**
* @brief check if NVM is busy
* @param is_busy NVM busy status
*/
static uint16_t is_nvm_busy(struct authon_context *ctx, uint8_t *is_busy)
{
	uint8_t data;
	uint16_t ret;

	if (is_busy == NULL) {
		return APP_NVM_E_INPUT;
	}

	ret = intf_read_register(&ctx->swi_ctx, ON_SFR_BUSY_STS, &data, 1);
	if (SDK_INTERFACE_SUCCESS != ret) {
		return ret;
	}

	*is_busy = (data >> BIT_NVM_BUSY) & 0x01;

	return TRUE;
}

static uint16_t is_nvm_ready(struct authon_context *ctx)
{
	uint8_t is_busy = 1;
	uint16_t ret;
	// wait for NVM complete
	uint32_t nvm_timeout = NVM_RETRY_COUNT;

	do {
		ret = is_nvm_busy(ctx, &is_busy);
		if (ret != TRUE) {
			return ret;
		}

		/* check for timeout */
		if (nvm_timeout == 0u) {
			return APP_NVM_E_TIMEOUT;
		}

		nvm_timeout--;
	} while (is_busy == 1);

	return APP_NVM_SUCCESS;
}

static uint16_t authon_write_nvm_bytes(struct authon_context *ctx,
				       uint16_t address, uint8_t *data,
				       uint16_t bytes_to_program)
{
	uint16_t ret;
	//uint16_t page_count = bytes_to_program/PAGE_SIZE_BYTE;

	if (data == NULL) {
		return APP_NVM_E_INPUT;
	}

	//NVM address must be 4 page aligned. Otherwise, 16 bytes register write will fail.
	//Example: 0, 4, 8, 12 and ...
	if (bytes_to_program >= 16) {
		if ((address % 4) != 0) {
			return APP_NVM_E_INPUT;
		}
	}

	//User NVM space write default to 16 bytes writing and remaining bytes will be done in 4 bytes blocks
	if (address < ON_IFX_NVM_ADDR) {
		uint16_t remaining_bytes = bytes_to_program;
		uint8_t wr_len[1] = { 16 };
		uint16_t i = 0; //counter for 16 bytes write
		uint8_t j = 0; //counter for 4 bytes write

		// initialize the SWI write length to 16 bytes write

		ret = intf_write_register(&ctx->swi_ctx, ON_SFR_SWI_WD_LEN,
					  wr_len, 1);
		if (SDK_INTERFACE_SUCCESS != ret) {
			return ret;
		}

		// NVM write in blocks of 16 bytes
		while ((remaining_bytes / 16) > 0) {
			ret = intf_write_register(
				&ctx->swi_ctx, address + (i / 4), data + i, 16);

			if (SDK_INTERFACE_SUCCESS != ret) {
				return ret;
			}

			swi_delay_ms(&ctx->swi_ctx, NVM_PROGRAMMING_TIME);

			i += 16;
			remaining_bytes = remaining_bytes - 16;

			if (APP_NVM_SUCCESS != is_nvm_ready(ctx)) {
				return APP_NVM_E_TIMEOUT;
			}
		}

		// initialize the SWI write length to 4 bytes
		wr_len[0] = 4;
		ret = intf_write_register(&ctx->swi_ctx, ON_SFR_SWI_WD_LEN,
					  wr_len, 1);
		if (SDK_INTERFACE_SUCCESS != ret) {
			return ret;
		}

		// NVM write in blocks of 4 bytes
		while ((remaining_bytes / 4) > 0) {
			ret = intf_write_register(&ctx->swi_ctx,
						  address + ((i + j) / 4),
						  data + i + j, 4);
			if (SDK_INTERFACE_SUCCESS != ret) {
				return ret;
			}

			swi_delay_ms(&ctx->swi_ctx, NVM_PROGRAMMING_TIME);

			j += 4;
			remaining_bytes = remaining_bytes - 4;

			if (APP_NVM_SUCCESS != is_nvm_ready(ctx)) {
				return APP_NVM_E_TIMEOUT;
			}
		}
	} else { //Non-User NVM space write default to 4 bytes writing
		uint8_t wr_len[1] = { 4 };
		uint8_t k = 0; //counter for 4 bytes write
		uint16_t remain_bytes = bytes_to_program;

		// write the length to SWI WD length register
		ret = intf_write_register(&ctx->swi_ctx, ON_SFR_SWI_WD_LEN,
					  wr_len, 1);
		if (SDK_INTERFACE_SUCCESS != ret) {
			return ret;
		}

		// NVM write in blocks of 4 bytes
		while ((remain_bytes / 4) > 0) {
			ret = intf_write_register(&ctx->swi_ctx, address + k,
						  data + (k * 4), 4);
			if (SDK_INTERFACE_SUCCESS != ret) {
				return ret;
			}

			swi_delay_ms(&ctx->swi_ctx, NVM_PROGRAMMING_TIME);

			remain_bytes = remain_bytes - 4;
			k = k + 4;

			if (APP_NVM_SUCCESS != is_nvm_ready(ctx)) {
				return APP_NVM_E_TIMEOUT;
			}
		}
	}

	return APP_NVM_SUCCESS;
}

/**
 * @brief NVM can only be read by page. Device attribute will determine the available user NVM pages.
 * @param nvm_start_page NVM start page number
 * @param data buffer to be return
 * * @param page_count number of pages to be read
 * */
uint16_t authon_read_nvm(struct authon_context *ctx, uint16_t nvm_start_page,
			 uint8_t *data, uint8_t page_count)
{
	uint16_t ret;
	uint16_t read_len_byte = page_count * PAGE_SIZE_BYTE;
	uint16_t read_len = 0;
	uint16_t remain_read_len = read_len_byte;
	uint16_t i = 0;

	//NVM location out of range
	if (nvm_start_page > ON_LSC_2) {
		return APP_NVM_E_INPUT;
	}

	//for User NVM page access, check against device attribute
	if (nvm_start_page < ON_ODC1_ADDR) {
		//Checks for valid user NVM page corresponding to device attributes
		if ((nvm_start_page + page_count) > ctx->cap.nvm_page_count) {
			return APP_NVM_E_INPUT;
		}
	}

	//Read in blocks of max 256 bytes
	while (remain_read_len) {
		if (remain_read_len >= 256) {
			read_len = 256;
			ret = intf_read_register(&ctx->swi_ctx,
						 nvm_start_page + (i / 4),
						 data + i, 0x00);
		} else {
			read_len = remain_read_len;
			ret = intf_read_register(&ctx->swi_ctx,
						 nvm_start_page + (i / 4),
						 data + i, read_len);
		}

		if (SDK_INTERFACE_SUCCESS != ret) {
			return ret;
		}
		remain_read_len = remain_read_len - read_len;
		i = i + read_len;
	}
	return APP_NVM_SUCCESS;
}

/**
* @brief Write data into NVM page. The data to be written should be 4 bytes aligned. The NVM write programming delay is required.
* @param address Start NVM page.
* @param data pointer to buffer holding values to program into NVM.
* @param page_count number of page to write
*/
uint16_t authon_write_nvm(struct authon_context *ctx, uint16_t nvm_start_page,
			  uint8_t *data, uint8_t page_count)
{
	uint16_t ret;
	uint16_t i;

	// check for valid user NVM page, checks for valid data length
	if ((nvm_start_page + page_count > ctx->cap.nvm_page_count) &&
	    (nvm_start_page != ON_LSC_BUF_1) &&
	    (nvm_start_page != ON_LSC_BUF_2) && (nvm_start_page != ON_LSC_1) &&
	    (nvm_start_page != ON_LSC_2)) {
		return APP_NVM_E_INPUT;
	}

	//Check if any of the user pages is locked
	if ((nvm_start_page != ON_LSC_BUF_1) &&
	    (nvm_start_page != ON_LSC_BUF_2) && (nvm_start_page != ON_LSC_1) &&
	    (nvm_start_page != ON_LSC_2)) {
		for (i = 0; i < page_count; i++) {
			ret = authon_get_nvm_lock_status(ctx,
							 nvm_start_page + i);
			if (APP_NVM_PAGE_LOCKED == ret) {
				return ret;
			}
		}
	}

	ret = authon_write_nvm_bytes(ctx, nvm_start_page, data, page_count * 4);
	return ret;
}

/**
* @brief Get NVM lock page status
* @param page NVM page check
*/
uint16_t authon_get_nvm_lock_status(struct authon_context *ctx, uint8_t page)
{
	uint16_t reg_addr;
	uint16_t ret;
	uint8_t byte_idx;
	uint8_t bit_idx;
	uint8_t rd_data;

	if (page >= ON_USR_NVM_PAGE_SIZE) {
		return APP_NVM_E_INPUT;
	}

	byte_idx = page / 8;
	bit_idx = page % 8;

	reg_addr = ON_SFR_USER_NVM_LOCK_STS_1 + byte_idx;
	ret = intf_read_register(&ctx->swi_ctx, reg_addr, &rd_data, 1);
	if (SDK_INTERFACE_SUCCESS != ret) {
		return ret;
	}
	if (((rd_data >> bit_idx) & 0x01) == 0x01) {
		return APP_NVM_PAGE_LOCKED;
	} else {
		return APP_NVM_PAGE_NOT_LOCKED;
	}
}

/**
* @brief Lock NVM page. NVM write programming delay is required.
* @param page NVM page need to be locked
*/
uint16_t authon_set_nvm_page_lock(struct authon_context *ctx, uint8_t page)
{
	uint8_t data_wr;
	uint16_t ret;
	uint8_t lock_register = page / 8;
	uint8_t lock_register_bit = page % 8;

	if (page >= ON_USR_NVM_PAGE_SIZE) {
		return APP_NVM_E_INPUT;
	}

	data_wr = 1 << lock_register_bit;

	ret = intf_write_register(&ctx->swi_ctx,
				  (ON_SFR_USER_NVM_LOCK_1 + lock_register),
				  &data_wr, 1);
	if (SDK_INTERFACE_SUCCESS != ret) {
		return ret;
	}

	swi_delay_ms(&ctx->swi_ctx, NVM_PROGRAMMING_TIME);

	return APP_NVM_SUCCESS;
}

/**
* @brief Unlock all NVM pages that is locked
*/
uint16_t authon_unlock_nvm_locked(struct authon_context *ctx, uint8_t *mac_byte)
{
	uint16_t ret;
	uint8_t data[UID_BYTE_LEN];

	ret = intf_read_register(&ctx->swi_ctx, ON_SFR_LOCK_CONF_STS, data, 1);
	if (ON_SUCCESS != ret) {
		return APP_NVM_E_READ_LOCK_STS;
	}

	if (((data[0] >> BIT_IFX_USERNVM_RESET_CONFIG) & 0x01) == 0) {
		return APP_NVM_DISABLE_UNLOCK;
	}

	//pr_info("authon_unlock_nvm_locked: %x %x %x %x %x %x %x %x %x %x\n", mac_byte[0], mac_byte[1], mac_byte[2],
	//                                                                   mac_byte[3],mac_byte[4],mac_byte[5],
	//                                                                     mac_byte[6],mac_byte[7],mac_byte[8],
	//                                                                   mac_byte[9]);

	ret = swi_bus_command_maccr5(&ctx->swi_ctx, mac_byte);
	if (INF_SWI_SUCCESS != ret) {
		return APP_NVM_E_MACCR5;
	}

	return APP_NVM_SUCCESS;
}

/**
* @brief Read the decrease by value.
* @param lsc_dec_val pointer to uint16_t to store life span counter decrease value.
*/
uint16_t authon_get_lsc_decvalue(struct authon_context *ctx,
				 uint16_t *lsc_dec_val)
{
	uint16_t ret;
	uint8_t rd_data;

	if (lsc_dec_val == NULL) {
		return APP_NVM_E_INPUT;
	}

	ret = intf_read_register(&ctx->swi_ctx, ON_SFR_LSC_DEC_VAL_L, &rd_data,
				 1);
	if (SDK_INTERFACE_SUCCESS != ret) {
		return ret;
	}

	*lsc_dec_val = rd_data;

	ret = intf_read_register(&ctx->swi_ctx, ON_SFR_LSC_DEC_VAL_H, &rd_data,
				 1);
	if (SDK_INTERFACE_SUCCESS != ret) {
		return ret;
	}

	*lsc_dec_val |= rd_data << 8;

	return APP_LSC_SUCCESS;
}

/**
* @brief Decrement the LSC based on LSC_DEC_VAL_H/L reg. NVM write programming delay is required.
* @param lsc_select Lifespan counter.
*/
uint16_t authon_dec_lsc_value(struct authon_context *ctx, uint8_t lsc_select)
{
	uint8_t rd_timeout = 10;
	uint8_t wr_data;
	uint8_t lsc_busy_bit;
	uint8_t data;
	uint16_t ret;

	if (lsc_select == 0) {
		wr_data = 1 << BIT_DEC_LSC_1;
		lsc_busy_bit = BIT_LSC_DEC_1_STS;
	} else if (lsc_select == 1) {
		wr_data = 1 << BIT_DEC_LSC_2;
		lsc_busy_bit = BIT_LSC_DEC_2_STS;
	} else {
		return APP_NVM_E_INPUT;
	}

	// set DEC_LSC_1/2 in ON_SFR_LSC_CTRL
	ret = intf_write_register(&ctx->swi_ctx, ON_SFR_LSC_CTRL, &wr_data, 1);
	if (SDK_INTERFACE_SUCCESS != ret) {
		return ret;
	}

	do {
		ret = intf_read_register(&ctx->swi_ctx, ON_SFR_LSC_KILL_ACT_STS,
					 &data, 1);
		if (SDK_INTERFACE_SUCCESS != ret) {
			return ret;
		}

		if (rd_timeout == 0) {
			return APP_LSC_E_TIMEOUT;
		}

		rd_timeout--;
		swi_delay_ms(&ctx->swi_ctx, NVM_PROGRAMMING_TIME);
	} while (((data >> lsc_busy_bit) & 0x01) != 0x00);

	return APP_LSC_SUCCESS;
}

/**
* @brief Read life span counter value
* @param lsc_select select which life span counter to decrease
* @param lsc_value pointer to uint32_t to store current LifeSpanCounter into.
*/
uint16_t authon_get_lsc_value(struct authon_context *ctx, uint8_t lsc_select,
			      uint32_t *lsc_value)
{
	uint8_t data[4];
	uint16_t reg_addr;
	uint16_t ret = APP_NVM_INIT;

	if (lsc_select == 0) {
		reg_addr = ON_LSC_1;
	} else if (lsc_select == 1) {
		reg_addr = ON_LSC_2;
	} else {
		return APP_NVM_E_INPUT;
	}

	if (lsc_value == NULL) {
		return APP_NVM_E_INPUT;
	}

	ret = intf_read_register(&ctx->swi_ctx, reg_addr, data, 4);
	if (SDK_INTERFACE_SUCCESS != ret) {
		return ret;
	} else {
		*lsc_value = (data[3] << 24) + (data[2] << 16) +
			     (data[1] << 8) + (data[0]);
	}

	/* all done well */
	return APP_LSC_SUCCESS;
}

/**
* @brief Re-program life span counter with a new value. NVM write programming delay is required.
* @param lsc_select to select which life span counter to re-program
* @param lsc_value new life span counter value
*/
uint16_t authon_set_lsc_value(struct authon_context *ctx, uint8_t lsc_select,
			      uint32_t lsc_value)
{
	uint8_t data_rd;
	uint8_t bit_ptr;
	uint16_t reg_addr;
	uint8_t data_wr[4];
	uint16_t ret;

	//Check for NVM write endurance
	if (lsc_value > NVM_ENDURANCE) {
		return APP_NVM_E_LSC_ENDURANCE;
	}

	// check status of LSC_EN_STS
	// if it is set, cannot re-program life span counter
	if (lsc_select == 0) {
		bit_ptr = BIT_LSC_1_EN_STS;
	} else if (lsc_select == 1) {
		bit_ptr = BIT_LSC_2_EN_STS;
	} else {
		return APP_NVM_E_INPUT;
	}

	ret = intf_read_register(&ctx->swi_ctx, ON_SFR_LSC_FEAT_STS, &data_rd,
				 1);
	if (SDK_INTERFACE_SUCCESS != ret) {
		return ret;
	}
	if (((data_rd >> bit_ptr) & 0x01) == 0x01) {
		return APP_LSC_EN_STATUS;
	}

	// program life span counter in NVM
	data_wr[0] = lsc_value & 0xff;
	data_wr[1] = (lsc_value >> 8) & 0xff;
	data_wr[2] = (lsc_value >> 16) & 0xff;
	data_wr[3] = (lsc_value >> 24) & 0xff;

	if (lsc_select == 0) {
		reg_addr = ON_LSC_1;
	} else if (lsc_select == 1) {
		reg_addr = ON_LSC_2;
	} else {
		return APP_NVM_E_INPUT;
	}

	ret = authon_write_nvm(ctx, reg_addr, data_wr, 1);
	if (APP_NVM_SUCCESS != ret) {
		return ret;
	}

	// program life span counter buffer in NVM
	if (lsc_select == 0) {
		reg_addr = ON_LSC_BUF_1;
	} else if (lsc_select == 1) {
		reg_addr = ON_LSC_BUF_2;
	}

	ret = authon_write_nvm(ctx, reg_addr, data_wr, 1);
	if (APP_NVM_SUCCESS != ret) {
		return ret;
	}

	return APP_LSC_SUCCESS;
}

/**
* @brief enables life span counter protection, once enable, life span counter can't be re-programed. NVM write programming delay is required.
* @param lsc_number to select which life span counter to re-program
*/
uint16_t authon_set_lsc_protection(struct authon_context *ctx,
				   uint8_t lsc_number)
{
	uint8_t data_wr;
	uint16_t ret;

	if (lsc_number == 0) {
		data_wr = 1 << BIT_LSC_1_EN;
	} else if (lsc_number == 1) {
		data_wr = 1 << BIT_LSC_2_EN;
	} else {
		return APP_NVM_E_INPUT;
	}

	ret = intf_write_register(&ctx->swi_ctx, ON_SFR_LSC_CONF, &data_wr, 1);
	if (SDK_INTERFACE_SUCCESS != ret) {
		return ret;
	}

	swi_delay_ms(&ctx->swi_ctx, NVM_PROGRAMMING_TIME);

	return APP_LSC_SUCCESS;
}

/**
* @brief Get the LSC lock status. Note that once LSC_1_EN and LSC_2_EN are set(locked),
* the LSC 1 and 2 Buffer cannot be read/write.
* @param lsc_number to select which life span counter to reset
*/
uint16_t authon_get_lsc_lock_status(struct authon_context *ctx,
				    uint8_t lsc_number)
{
	uint16_t ret;
	uint8_t rd_data;
	uint8_t bit_lsc_en_sts;

	if (lsc_number == 0) {
		bit_lsc_en_sts = BIT_LSC_1_EN_STS;
	} else if (lsc_number == 1) {
		bit_lsc_en_sts = BIT_LSC_2_EN_STS;
	} else {
		return APP_LSC_E_INPUT;
	}

	ret = intf_read_register(&ctx->swi_ctx, ON_SFR_LSC_FEAT_STS, &rd_data,
				 1);
	if (SDK_INTERFACE_SUCCESS != ret) {
		return ret;
	} else {
		if (((rd_data >> bit_lsc_en_sts) & 0x01) == 0) {
			return APP_LSC_NOT_LOCKED;
		} else {
			return APP_LSC_LOCKED;
		}
	}
}

/**
* @brief Get the various IFX configuration.
* @param config_type IFX configuration type
*/
uint16_t authon_get_ifx_config(struct authon_context *ctx,
			       enum config_type config_type)
{
	uint16_t ret;
	uint8_t rd_data[1];

	ret = intf_read_register(&ctx->swi_ctx, ON_SFR_LOCK_CONF_STS, rd_data,
				 1);
	if (SDK_INTERFACE_SUCCESS != ret) {
		return ret;
	}

	switch (config_type) {
	case NVM_RESET_CONFIG: {
		if (((rd_data[0] >> BIT_IFX_USERNVM_RESET_CONFIG) & 0x01) ==
		    0) {
			return APP_NVM_DISABLE_RESET;
		} else {
			return APP_NVM_ENABLE_RESET;
		}
	}
	case CHIPLOCK_CONFIG: {
		if (((rd_data[0] >> BIT_CHIP_LOCK) & 0x01) == 0) {
			return APP_NVM_DISABLE_CHIPLOCK;
		} else {
			return APP_NVM_ENABLE_CHIPLOCK;
		}
	}
	case HOSTAUTH_CONFIG: {
		if (((rd_data[0] >> BIT_HOSTAUTH_CONF) & 0x01) == 0) {
			return APP_HA_DISABLE;
		} else {
			return APP_HA_ENABLE;
		}
	}
	case AUTOKILL_CONFIG: {
		if (((rd_data[0] >> BIT_IFX_AUTO_KILL_CONF) & 0x01) == 0) {
			return APP_ECC_DISABLE_AUTOKILL;
		} else {
			return APP_ECC_ENABLE_AUTOKILL;
		}
	}
	case KILL_CONFIG: {
		if (((rd_data[0] >> BIT_IFX_KILL_CONF) & 0x01) == 0) {
			return APP_ECC_DISABLE_KILL;
		} else {
			return APP_ECC_ENABLE_KILL;
		}
	}
	default:
		return SDK_E_CONFIG;
	}
}