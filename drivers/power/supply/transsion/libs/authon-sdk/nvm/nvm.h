// SPDX-FileCopyrightText: 2024 Infineon Technologies AG
//
// SPDX-License-Identifier: MIT

/**
 * @file   nvm.h
 * @date   January, 2021
 * @brief  Implementation of NVM definition
 */
#ifndef __NVM_H__
#define __NVM_H__

#include "authon_api.h"

#define NVM_ENDURANCE 500000 /*!  NVM Endurance write cycle */
#define NVM_PROGRAMMING_TIME (7) /*!Max. NVM programming time */
#define MACCR_PROGRAMMING_TIME (43) /*!Max. MACCR programming time */
#define MACK_PROGRAMMING_TIME (20) /*!Max. MACK programming time */

#define PAGE_SIZE_BYTE (4u) /*!Each page is 4 bytes */
#define ON_USR_NVM_PAGE_SIZE (64u) /*!  64 pages of User NVM for 2Kbits NVM */

#define NVM_RETRY_COUNT 10000 /*!< NVM retry loop > 5100us */

#if (ON_USR_NVM_PAGE_SIZE != 64)
#pragma message("Error: Invalid NVM size")
#endif

enum config_type {
	NVM_RESET_CONFIG,
	CHIPLOCK_CONFIG,
	HOSTAUTH_CONFIG,
	AUTOKILL_CONFIG,
	KILL_CONFIG
};

/** NVM API */
uint16_t authon_read_nvm(struct authon_context *ctx, uint16_t nvm_start_page,
			 uint8_t *data, uint8_t page_count);
uint16_t authon_write_nvm(struct authon_context *ctx, uint16_t nvm_start_page,
			  uint8_t *data, uint8_t page_count);
uint16_t authon_set_nvm_page_lock(struct authon_context *ctx, uint8_t page);
uint16_t authon_get_nvm_lock_status(struct authon_context *ctx, uint8_t page);
uint16_t authon_get_ifx_config(struct authon_context *ctx,
			       enum config_type config_type);
uint16_t authon_unlock_nvm_locked(struct authon_context *ctx,
				  uint8_t *mac_byte);

/** LSC API */
uint16_t authon_set_lsc_value(struct authon_context *ctx, uint8_t lsc_select,
			      uint32_t lsc_value);
uint16_t authon_get_lsc_value(struct authon_context *ctx, uint8_t lsc_select,
			      uint32_t *lsc_value);
uint16_t authon_get_lsc_decvalue(struct authon_context *ctx,
				 uint16_t *lsc_dec_val);
uint16_t authon_dec_lsc_value(struct authon_context *ctx, uint8_t lsc_select);
uint16_t authon_set_lsc_protection(struct authon_context *ctx,
				   uint8_t lsc_number);
uint16_t authon_get_lsc_lock_status(struct authon_context *ctx,
				    uint8_t lsc_number);

#endif /* __NVM_H__ */
