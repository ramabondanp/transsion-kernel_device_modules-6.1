// SPDX-FileCopyrightText: 2024 Infineon Technologies AG
//
// SPDX-License-Identifier: MIT

/**
 * @file   authon_api.h
 * @date   January, 2021
 * @brief  Implementation of Authenticate On API
 */
#ifndef __AUTHENTICATE_ON_API_H__
#define __AUTHENTICATE_ON_API_H__

#include "authon_config.h"
#include "interface/register.h"
#include "interface/swi_bus_command.h"

#define UID_BYTE_LENGTH (12)
#define UID_BIT_SIZE (UID_BYTE_LENGTH * 8)

#define authon_set_platform(on_ctx, pf_ctx) \
	swi_set_platform(&(on_ctx)->swi_ctx, pf_ctx)

enum uid_search_type {
	UID_BY_SEARCH,
	UID_BY_REGISTER_READ,
};

enum uid_order {
	MSB_FIRST,
	LSB_FIRST,
};

/** @brief Device communication interface */
enum interface_type {
	UNDEFINED_INTERFACE,
	SWI,
};

/** @brief SWI property */
struct swi_type {
	uint8_t device_address;
	uint8_t device_count;
	enum uid_search_type uid_by_search;
};

enum feature { UNSUPPORTED, SUPPORTED };

/** @brief Optional configurable features */
struct authon_attribute {
	enum feature host_authentication_support;
	enum feature kill_ecc_support;
	enum feature auto_kill_ecc;
	enum feature user_nvm_unlock;
};

/** @brief Device capabilities */
struct authon_capability {
	struct swi_type swi;

	//UID property
	uint8_t uid[CONFIG_SWI_MAX_COUNT][UID_BYTE_LEN];
	uint8_t uid_length;

	//NVM property
	uint16_t nvm_size;
	uint16_t nvm_page_count;

	//Crypto property
	uint16_t odc_bit_length;
	uint16_t ecc_bit_length;
	uint8_t num_lsc_counter;
	uint8_t num_ecc_keypair;

	struct authon_attribute device_attribute;
};

/** @brief support for multiple device on bus */
struct authon_enumeration {
	uint8_t uid[CONFIG_SWI_MAX_COUNT][UID_BYTE_LEN];
	uint16_t device_address[CONFIG_SWI_MAX_COUNT];
	uint16_t active_device;
	uint16_t device_found;
};

struct authon_context {
	uint8_t stack[UID_BIT_SIZE];
	size_t stack_pointer;

	struct authon_capability cap;
	struct authon_enumeration enumeration;
	struct swi_context swi_ctx;
};

/** SDK Configuration API */
uint16_t authon_init_sdk(struct authon_context *ctx);
uint16_t authon_get_active_interface(struct authon_context *ctx,
				     enum interface_type *active_interface);
uint16_t authon_deinit_sdk(struct authon_context *ctx);
uint16_t authon_get_sdk_device_found(struct authon_context *ctx,
				     uint8_t *device_found);
uint16_t authon_get_sdk_active_device(struct authon_context *ctx,
				      uint8_t *active_device);
uint16_t authon_set_sdk_active_device(struct authon_context *ctx,
				      uint8_t active_device);
uint16_t authon_set_sdk_active_device_uid(struct authon_context *ctx,
					  uint8_t *active_device_uid,
					  uint8_t invert_uid);

/** SWI Interface API */
uint16_t authon_get_swi_address(struct authon_context *ctx,
				uint16_t *device_address);
uint16_t authon_set_swi_address(struct authon_context *ctx,
				uint16_t device_address);
uint16_t authon_read_swi_sfr(struct authon_context *ctx, uint16_t sfr_address,
			     uint8_t *sfr_value, uint16_t length);
uint16_t authon_write_swi_sfr(struct authon_context *ctx, uint16_t sfr_address,
			      uint8_t *sfr_value, uint16_t length);

/** Device Characteristics API*/
uint16_t authon_exe_read_uid(struct authon_context *ctx, uint8_t *uid,
			     uint8_t *vid, uint8_t *pid);
uint16_t authon_exe_reset(struct authon_context *ctx);
uint16_t authon_exe_power_cycle(struct authon_context *ctx);
uint16_t authon_exe_power_down(struct authon_context *ctx);
uint16_t authon_exe_power_up(struct authon_context *ctx);

#if (ENABLE_SWI_SEARCH == 1)
uint16_t authon_exe_search_swi_uid(struct authon_context *ctx, uint8_t *uid,
				   uint8_t *device_count, enum uid_order order);
uint16_t authon_search_uid(struct authon_context *ctx, uint8_t *dev_cnt,
			   uint16_t *dev_addr);
#endif

void authon_select_by_address(struct authon_context *ctx,
			      uint16_t device_address);
uint16_t authon_select_by_uid(struct authon_context *ctx,
			      uint8_t *device_to_select);

uint16_t authon_get_error_code(struct authon_context *ctx, uint8_t *error_code);

#endif /* __AUTHENTICATE_ON_API_H__ */
