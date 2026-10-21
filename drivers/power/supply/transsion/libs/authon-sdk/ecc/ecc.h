// SPDX-FileCopyrightText: 2024 Infineon Technologies AG
//
// SPDX-License-Identifier: MIT

/**
 * @file   ecc.h
 * @date   January, 2021
 * @brief  Implementation of ECC routine
 */
#ifndef __ECC_H__
#define __ECC_H__

#include "authon_api.h"

#define MAC_BYTE_LEN (10)

#define ODC_BYTE_LEN (50)
#define HASH_BYTE_LEN (32)
#define PUBLICKEY_BYTE_LEN (22)
#define ECC_CHALLENGE_LEN (21)
#define ECC_RESPONSE_LEN (44)

#define ECC_FIXED_WAIT (100)
#define ECC_RETRY_COUNT (100)
#define MAC_RETRY_COUNT (100)

#define INTERRUPT_START_DELAY (5)
#define MAC_RETRY_DELAY (2)

/** @brief Type of event handling */
enum event_handle { POLLING, INTERRUPT, FIXED_WAIT };

uint16_t authon_send_challenge_and_get_response(struct authon_context *ctx,
						uint8_t *gf2n_challenge,
						uint8_t *ecc_response,
						uint8_t ecc_key_set,
						enum event_handle event_handle);
uint16_t authon_get_ecc_publickey(struct authon_context *ctx,
				  uint8_t key_number, uint8_t *public_key);
uint16_t authon_get_odc(struct authon_context *ctx, uint8_t key_number,
			uint8_t *odc_value);

#endif /* __ECC_H__ */
