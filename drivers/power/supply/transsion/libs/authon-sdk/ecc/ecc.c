// SPDX-FileCopyrightText: 2024 Infineon Technologies AG
//
// SPDX-License-Identifier: MIT

/**
 * @file   ecc.c
 * @date   January, 2021
 * @brief  ECC implementation of ECC routine
 */
#include "ecc/ecc.h"
#include "nvm/nvm.h"
#include "interface/interface.h"

/**
* @brief Get ECC public key value including random padding
* @param key_number ECC key selection
* @param public_key ECC public key
*/
#define PK_PAGE_COUNT \
	((PUBLICKEY_BYTE_LEN + PAGE_SIZE_BYTE - 1) / PAGE_SIZE_BYTE)
uint16_t authon_get_ecc_publickey(struct authon_context *ctx,
				  uint8_t key_number, uint8_t *public_key)
{
	uint16_t nvm_addr;
	uint16_t ret = APP_ECC_INIT;
	uint8_t pubk[PK_PAGE_COUNT * PAGE_SIZE_BYTE];

	if ((key_number > 1) || (public_key == NULL)) {
		return APP_ECC_E_INPUT;
	}
	// read Public key
	if (key_number == 0) {
		nvm_addr = ON_AUTH_PK1_ADDR;
	} else {
		nvm_addr = ON_AUTH_PK2_ADDR;
	}

	ret = authon_read_nvm(ctx, nvm_addr, pubk, PK_PAGE_COUNT);
	if (APP_NVM_SUCCESS != ret) {
		return APP_ECC_E_READ_PK;
	}

	//copy to output public key
	memcpy(public_key, (pubk + 2), PUBLICKEY_BYTE_LEN);
	return APP_ECC_SUCCESS;
}

/**
* @brief Get ODC value including random padding
* @param key_number ECC key selection
* @param gf2n_ODC ODC to be returned
*/
#define ODC_PAGE_COUNT ((ODC_BYTE_LEN + PAGE_SIZE_BYTE - 1) / PAGE_SIZE_BYTE)
uint16_t authon_get_odc(struct authon_context *ctx, uint8_t key_number,
			uint8_t *gf2n_ODC)
{
	uint16_t nvm_addr;
	uint16_t ret = APP_ECC_INIT;

	if ((key_number > 1) || (gf2n_ODC == NULL)) {
		return APP_ECC_E_INPUT;
	}

	// read ODC
	if (key_number == 0) {
		nvm_addr = ON_ODC1_ADDR;
	} else {
		nvm_addr = ON_ODC2_ADDR;
	}

	ret = authon_read_nvm(ctx, nvm_addr, gf2n_ODC, ODC_PAGE_COUNT);
	if (APP_NVM_SUCCESS != ret) {
		return APP_ECC_E_READ_ODC;
	}

	return APP_ECC_SUCCESS;
}

/**
* @brief Send Challenge and get response on SWI bus
* @param gf2n_challenge Challenge value
* @param ecc_response Calculated ECC Response
* @param ecc_keySet selected ecc key
* @param evt event handling using polling or interrupt
*/
uint16_t authon_send_challenge_and_get_response(struct authon_context *ctx,
						uint8_t *gf2n_challenge,
						uint8_t *ecc_response,
						uint8_t ecc_key_set,
						enum event_handle event_handle)
{
	uint8_t data;
	uint8_t timeout_cnt;
	uint8_t irq_detected = NO_INTERRUPT;
	uint16_t ret;

	if (gf2n_challenge == NULL) {
		return APP_ECC_E_INPUT;
	}

	if (ecc_key_set > 1) {
		return APP_ECC_E_INPUT;
	}

	// if needed for interrupt mode
	// enable interrupt first
	if (event_handle == INTERRUPT) {
		// enable Device interrupt enable
		data = 1 << BIT_DEV_INT_EN;
		ret = intf_write_register(&ctx->swi_ctx, ON_SFR_SWI_CONF_1,
					  &data, 1);
		if (SDK_INTERFACE_SUCCESS != ret) {
			return ret;
		}

		// enable ECC_DONE_INT_EN
		data = 1 << BIT_ECC_DONE_INT_EN;
		ret = intf_write_register(&ctx->swi_ctx, ON_SFR_SWI_INT_EN,
					  &data, 1);
		if (SDK_INTERFACE_SUCCESS != ret) {
			return ret;
		}

		// check ecc interrupt done bit is set and clear it
		// incase if interrupt bit status was set because MAC/ECC run before
		ret = intf_read_register(&ctx->swi_ctx, ON_SFR_SWI_INT_STS,
					 &data, 1);
		if (SDK_INTERFACE_SUCCESS != ret) {
			return ret;
		}
		if (data != 0) {
			// clear it
			ret = intf_write_register(&ctx->swi_ctx,
						  ON_SFR_SWI_INT_STS, &data, 1);
			if (SDK_INTERFACE_SUCCESS != ret) {
				return ret;
			}
		}
	}

	// send ECCC command
	ret = swi_bus_command_eccc(&ctx->swi_ctx, gf2n_challenge);
	if (INF_SWI_SUCCESS != ret) {
		return ret;
	}

	// start ECC calculation
	if (ecc_key_set == 0) {
		ret = swi_bus_command_eccs1(&ctx->swi_ctx);
		if (INF_SWI_SUCCESS != ret) {
			return ret;
		}
	} else {
		ret = swi_bus_command_eccs2(&ctx->swi_ctx);
		if (INF_SWI_SUCCESS != ret) {
			return ret;
		}
	}

	switch (event_handle) {
	case FIXED_WAIT:
	case POLLING:
		if (event_handle == FIXED_WAIT) {
			swi_delay_ms(&ctx->swi_ctx, ECC_FIXED_WAIT);
		}

		timeout_cnt = ECC_RETRY_COUNT;
		data = 0xff;

		do {
			ret = intf_read_register(&ctx->swi_ctx, ON_SFR_BUSY_STS,
						 &data, 1);
			if (SDK_INTERFACE_SUCCESS != ret) {
				return ret;
			}

			if (timeout_cnt == 0) {
				return APP_ECC_E_AUTHMAC_BUSY;
			}
			timeout_cnt--;

		} while (((data >> BIT_AUTH_MAC_BUSY) & 0x01) != 0);

		break;

	case INTERRUPT:
		swi_delay_ms(&ctx->swi_ctx, INTERRUPT_START_DELAY);

		//Enable interrupt
		swi_bus_command_eint(&ctx->swi_ctx, &irq_detected);

		if (irq_detected == NO_INTERRUPT) {
			// check ecc interrupt done bit is set and clear it
			ret = intf_read_register(&ctx->swi_ctx,
						 ON_SFR_SWI_INT_STS, &data, 1);
			if (SDK_INTERFACE_SUCCESS != ret) {
				return ret;
			}
			return APP_ECC_AUTHMAC_EINT_NOINT;
		}

		// check ecc interrupt done bit is set and clear it
		ret = intf_read_register(&ctx->swi_ctx, ON_SFR_SWI_INT_STS,
					 &data, 1);
		if (SDK_INTERFACE_SUCCESS != ret) {
			return ret;
		}
		if (((data >> BIT_ECC_DONE_INT_STS) & 0x01) == 0x01) {
			// clear it
			ret = intf_write_register(&ctx->swi_ctx,
						  ON_SFR_SWI_INT_STS, &data, 1);
			if (SDK_INTERFACE_SUCCESS != ret) {
				return ret;
			}
		}
		break;
	}

	swi_irq_disable(&ctx->swi_ctx);

	//Get Response
	ret = swi_bus_command_eccr(&ctx->swi_ctx, ecc_response);
	if (INF_SWI_SUCCESS != ret) {
		swi_irq_restore(&ctx->swi_ctx);
		return ret;
	}

	swi_irq_restore(&ctx->swi_ctx);

	return APP_ECC_SUCCESS;
}