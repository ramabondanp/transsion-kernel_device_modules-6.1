// SPDX-FileCopyrightText: 2024 Infineon Technologies AG
//
// SPDX-License-Identifier: MIT

/**
 * @file   host_auth.c
 * @date   January, 2021
 * @brief  Implementation of host authentication routine
 */

#include "host_auth/host_auth.h"

/**
* @brief Send HRREQ and HRRES to device to get random number
* @param nonce_a Nounce A read from device
*/
uint16_t get_random_number(struct authon_context *ctx, uint8_t *nonce_a)
{
	uint16_t ret = APP_HA_INIT;
	struct swi_context *swi_ctx = &ctx->swi_ctx;

	if (nonce_a == NULL) {
		return APP_HA_E_INPUT;
	}

	//swi_irq_disable(swi_ctx);

	// Request for random number from Authenticate On
	ret = swi_bus_command_hrreq(swi_ctx);
	//swi_irq_restore(swi_ctx);
	if (ret != INF_SWI_SUCCESS) {
	//	swi_irq_restore(swi_ctx);
		return APP_HA_E_HRREQ;
	}

	swi_delay_ms(swi_ctx, 10);

	// Get random number
	swi_irq_disable(swi_ctx);
	ret = swi_bus_command_hrres(swi_ctx, nonce_a);
	swi_irq_restore(swi_ctx);
	if (ret != INF_SWI_SUCCESS) {
		//swi_irq_restore(swi_ctx);
		return APP_HA_E_HRRES;
	}


	swi_delay_ms(swi_ctx, 5);

	return ret;
}