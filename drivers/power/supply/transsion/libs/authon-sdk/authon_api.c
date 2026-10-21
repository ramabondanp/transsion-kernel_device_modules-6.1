// SPDX-FileCopyrightText: 2024 Infineon Technologies AG
//
// SPDX-License-Identifier: MIT

/**
 * @file   authon_api.c
 * @date   January, 2021
 * @brief  Implementation of Authenticate On SDK API
 */
#include "authon_api.h"
#include "interface/interface.h"
#include "nvm/nvm.h"
#include <linux/printk.h>

#define DEFAULT_ECC_KEY_COUNT (2) //Every Authenticate On has 2 ECC key pairs
#define UID_SEARCH_RETRY (3) //Number of UID search retries

/**
* @brief Update SDK active device capabilities
*/
static uint16_t update_active_device_capabilities(struct authon_context *ctx)
{
	uint16_t ret;

	ret = authon_get_ifx_config(ctx, HOSTAUTH_CONFIG);
	if ((ret != APP_HA_ENABLE) && (ret != APP_HA_DISABLE)) {
		return ret;
	} else if (ret == APP_HA_ENABLE) {
		ctx->cap.device_attribute.host_authentication_support =
			SUPPORTED;
	} else {
		ctx->cap.device_attribute.host_authentication_support =
			UNSUPPORTED;
	}

	if (authon_get_ifx_config(ctx, KILL_CONFIG) == APP_ECC_ENABLE_KILL) {
		ctx->cap.device_attribute.kill_ecc_support = SUPPORTED;
	} else {
		ctx->cap.device_attribute.kill_ecc_support = UNSUPPORTED;
	}

	if (authon_get_ifx_config(ctx, AUTOKILL_CONFIG) ==
	    APP_ECC_ENABLE_AUTOKILL) {
		ctx->cap.device_attribute.auto_kill_ecc = SUPPORTED;
	} else {
		ctx->cap.device_attribute.auto_kill_ecc = UNSUPPORTED;
	}

	if (authon_get_ifx_config(ctx, NVM_RESET_CONFIG) ==
	    APP_NVM_ENABLE_RESET) {
		ctx->cap.device_attribute.user_nvm_unlock = SUPPORTED;
	} else {
		ctx->cap.device_attribute.user_nvm_unlock = UNSUPPORTED;
	}

	return SDK_SUCCESS;
}

/**
* @brief Initialize the SDK
* @param cap_config SDK configuration input
*/
uint16_t authon_init_sdk(struct authon_context *ctx)
{
	uint16_t ret = SDK_INIT;
	uint8_t uid[12];
	uint8_t tid[4];
	uint8_t pid[4];

	ctx->cap.uid_length = UID_BYTE_LEN;
	ctx->cap.nvm_size = PAGE_SIZE_BYTE;
	ctx->cap.nvm_page_count = ON_USR_NVM_PAGE_SIZE;
	ctx->cap.num_ecc_keypair = DEFAULT_ECC_KEY_COUNT;
	ctx->cap.swi.device_count = 0;

	/* Power cycle routine */
	ret = authon_exe_power_cycle(ctx);
	if (ret != EXE_SUCCESS) {
		return ret;
	}

#if (ENABLE_SWI_SEARCH == 1)
	uint8_t device_count = 0;
	uint8_t retry = UID_SEARCH_RETRY;

	while (retry) {
		ret = authon_search_uid(ctx, (uint8_t *)ctx->enumeration.uid[0],
					&(ctx->enumeration.device_found));
		if (ret == APP_SID_SUCCESS) {
			break;
		} else if (APP_SID_SUCCESS != ret || device_count == 0) {
			authon_exe_reset(ctx);
		}

		retry--;
		if (retry == 0) {
			ctx->cap.swi.device_count = 0;
			return APP_SID_E_NO_DEV;
		}
	}

	/* UID reverse in necessary for ODC verification */
	for (uint8_t i = 0; i < ctx->enumeration.device_found; i++) {
		for (uint8_t j = 0; j < 12; j++) {
			ctx->cap.uid[i][11 - j] = ctx->enumeration.uid[i][j];
		}
	}

	/* Only make the first device active. Device can only be selected from MSB order. So reverse the order again */
	uint8_t first_active_uid[12] = { 0 };
	for (uint8_t j = 0; j < 12; j++) {
		first_active_uid[11 - j] = ctx->cap.uid[0][j];
	}

	ret = authon_select_by_uid(ctx, (uint8_t *)first_active_uid);
	if (ret != APP_SID_SUCCESS) {
		return APP_SID_E_SELECT;
	}

	/* Get the address of the active device */
	ret = authon_get_swi_address(ctx, ctx->enumeration.device_address);
	if (ret != SDK_SUCCESS) {
		return ret;
	}

	/* Report the number of device found */
	ctx->cap.swi.device_count = ctx->enumeration.device_found;
	ctx->cap.swi.uid_by_search = UID_BY_SEARCH;
#else
	/* Select using default address and expects only a single device on the bus */
	swi_bus_command_eda(&ctx->swi_ctx, 0);
	swi_bus_command_sda(&ctx->swi_ctx, ON_DEFAULT_ADDRESS);
	ctx->cap.swi.uid_by_search = UID_BY_REGISTER_READ;

	ret = authon_exe_read_uid(ctx, uid, tid, pid);
	if (ret != EXE_SUCCESS) {
		return ret;
	}

	memcpy(ctx->enumeration.uid, uid, 12);

	ctx->enumeration.device_found = 1;
	ctx->cap.swi.device_count = ctx->enumeration.device_found;
#endif

	return EXE_SUCCESS;
}

/**
* @brief Return the number of device found on the bus
* @param device_found number of device found
*/
uint16_t authon_get_sdk_device_found(struct authon_context *ctx,
				     uint8_t *device_found)
{
	if (device_found == NULL) {
		return SDK_E_INPUT;
	}

	*device_found = ctx->enumeration.device_found;

	return SDK_SUCCESS;
}

/**
* @brief Return the current active device
* @param active_device active device
*/
uint16_t authon_get_sdk_active_device(struct authon_context *ctx,
				      uint8_t *active_device)
{
	if (active_device == NULL) {
		return SDK_E_INPUT;
	}

	*active_device = ctx->enumeration.active_device;

	return SDK_SUCCESS;
}

/**
* @brief Set and select the current active device.
* @param active_device active device
*/
uint16_t authon_set_sdk_active_device(struct authon_context *ctx,
				      uint8_t active_device)
{
	uint16_t ret;

	if (active_device > ctx->enumeration.device_found) {
		return SDK_E_INPUT;
	}

	ctx->enumeration.active_device = active_device;

	ret = authon_select_by_uid(
		ctx, (uint8_t *)ctx->enumeration.uid[active_device]);
	if (ret != APP_SID_SUCCESS) {
		return APP_SID_E_SELECT;
	}

	// populate the SDK with the active device
	ret = update_active_device_capabilities(ctx);
	if (ret != SDK_SUCCESS) {
		return ret;
	}

	return SDK_SUCCESS;
}

/**
* @brief Set and select the current active device by UID.
* @param active_device_uid active device uid
* @param invert the UID is stored in reverse and need to be flipped
*/
uint16_t authon_set_sdk_active_device_uid(struct authon_context *ctx,
					  uint8_t *active_device_uid,
					  uint8_t invert)
{
	uint16_t ret;
	uint8_t compare_uid[12];
	uint8_t device_found = ctx->enumeration.device_found;
	uint8_t mismatch = 1;
	uint8_t device_found_index = 0;
	uint8_t i, j;

	if (invert == 1) { // flip the UID
		for (j = 0; j < 12; j++) {
			compare_uid[11 - j] = active_device_uid[j];
		}
	} else {
		for (j = 0; j < 12; j++) {
			compare_uid[j] = active_device_uid[j];
		}
	}

	while (device_found) {
		mismatch = 0;
		for (i = 0; i < 12; i++) {
			if (compare_uid[i] !=
			    ctx->enumeration.uid[device_found - 1][i]) {
				mismatch = 1;
				break;
			}
		}

		if (mismatch == 0) {
			break;
		}
		device_found--;
	}

	if (mismatch == 0) {
		device_found_index = device_found - 1;
		ret = authon_set_sdk_active_device(ctx, device_found_index);
		if (ret != SDK_SUCCESS) {
			return ret;
		}
		return SDK_SUCCESS;
	} else {
		return SDK_E_UID_NOT_ENUM;
	}
}

/**
* @brief Get device address of the current active device
* @param device_address device_address
*/
uint16_t authon_get_swi_address(struct authon_context *ctx,
				uint16_t *device_address)
{
	uint8_t data;
	uint16_t ret = APP_NVM_INIT;
	uint16_t nw_rd_dev_addr = 0;

	ret = intf_read_register(&ctx->swi_ctx, ON_SFR_SWI_DADR0_H, &data, 1);
	if (ret != SDK_INTERFACE_SUCCESS) {
		return ret;
	}

	nw_rd_dev_addr = data << 8;
	ret = intf_read_register(&ctx->swi_ctx, ON_SFR_SWI_DADR0_L, &data, 1);
	if (ret != SDK_INTERFACE_SUCCESS) {
		return ret;
	}

	nw_rd_dev_addr |= data;

	if (ret == SDK_INTERFACE_SUCCESS) {
		*device_address = nw_rd_dev_addr;
		return SDK_SUCCESS;
	} else {
		return APP_NVM_E_READ_DEVICEADDR;
	}
}

/**
* @brief Set device address of the current active device
* @param device_address device_address
*/
uint16_t authon_set_swi_address(struct authon_context *ctx,
				uint16_t device_address)
{
	uint16_t ret;

	if (ctx->cap.swi.device_address == device_address) {
		return INF_SWI_SUCCESS;
	}

	ret = swi_bus_command_wda(&ctx->swi_ctx, device_address);
	if (INF_SWI_SUCCESS != ret) {
		return INF_SWI_E_SET_ADDRESS;
	}

	ctx->cap.swi.device_address = device_address;

	return SDK_SUCCESS;
}

/**
* @brief Read SWI SFR directly
* @param sfr_address SFR address to be read
* @param sfr_value SFR value to be read
* @param length Byte length to be read
*/
uint16_t authon_read_swi_sfr(struct authon_context *ctx, uint16_t sfr_address,
			     uint8_t *sfr_value, uint16_t length)
{
	uint16_t ret;

	swi_irq_disable(&ctx->swi_ctx);
	/* send address to read from */
	swi_bus_command_era(&ctx->swi_ctx, BYTE_HIGH(sfr_address));
	ret = swi_bus_command_rra(&ctx->swi_ctx, BYTE_LOW(sfr_address), length,
				  sfr_value);
	swi_irq_restore(&ctx->swi_ctx);

	return ret;
}

/**
* @brief Write SWI SFR directly
* @param sfr_address SFR address to be written
* @param sfr_value SFR value to be written
* @param length Byte length to be written
*/
uint16_t authon_write_swi_sfr(struct authon_context *ctx, uint16_t sfr_address,
			      uint8_t *sfr_value, uint16_t length)
{
	/* send address to read from */
	swi_bus_command_era(&ctx->swi_ctx, BYTE_HIGH(sfr_address));
	swi_bus_command_wra(&ctx->swi_ctx, BYTE_LOW(sfr_address));
	swi_bus_command_wd(&ctx->swi_ctx, length, sfr_value);

	return INF_SWI_SUCCESS;
}

/**
* @brief Returns the active device UID
* @param uid UID value
* @param tid TID value
* @param pid PID value
*/
uint16_t authon_exe_read_uid(struct authon_context *ctx, uint8_t *uid,
			     uint8_t *tid, uint8_t *pid)
{
	uint16_t res;
	uint16_t uid_address_offset = ON_UID_ADDR + 2;

	if ((uid == NULL) || (tid == NULL) || (pid == NULL)) {
		return EXE_E_INPUT;
	}

	memset(ctx->cap.uid, 0, ctx->cap.uid_length);

	swi_irq_disable(&ctx->swi_ctx);

	swi_bus_command_rbl(&ctx->swi_ctx, 3); /*Read 2^3 bytes*/

	/* send address to read from */
	swi_bus_command_era(&ctx->swi_ctx, BYTE_HIGH(ON_UID_ADDR));
	res = swi_bus_command_rra(&ctx->swi_ctx, BYTE_LOW(ON_UID_ADDR), 8, uid);
	if (res != INF_SWI_SUCCESS) {
		swi_irq_restore(&ctx->swi_ctx);
		return EXE_READUIDFAILED;
	}

	swi_bus_command_rbl(&ctx->swi_ctx, 2); /*Read 2^2 bytes*/

	/* send address to read from */
	swi_bus_command_era(&ctx->swi_ctx, BYTE_HIGH(uid_address_offset));
	res = swi_bus_command_rra(&ctx->swi_ctx, BYTE_LOW(uid_address_offset),
				  4, uid + 8);
	if (res != INF_SWI_SUCCESS) {
		swi_irq_restore(&ctx->swi_ctx);
		return EXE_READUIDFAILED;
	}

	swi_irq_restore(&ctx->swi_ctx);

	memcpy(ctx->cap.uid, uid, ctx->cap.uid_length);
	memcpy(tid, uid + 10, 2);
	memcpy(pid, uid + 8, 2);

	return EXE_SUCCESS;
}

/**
* @brief Soft reset of the active device
*/
uint16_t authon_exe_reset(struct authon_context *ctx)
{
	swi_abort_irq(&ctx->swi_ctx);
	swi_bus_command_bres(&ctx->swi_ctx);
	swi_delay_us(&ctx->swi_ctx, ctx->swi_ctx.reset_delay_time);
	return EXE_SUCCESS;
}

/**
* @brief Power cycle the active device
*/
uint16_t authon_exe_power_cycle(struct authon_context *ctx)
{
	authon_exe_power_down(ctx);
	authon_exe_power_up(ctx);
	return EXE_SUCCESS;
}

/**
* @brief Power down the active device
*/
uint16_t authon_exe_power_down(struct authon_context *ctx)
{
	swi_power_down(&ctx->swi_ctx);
	swi_delay_us(&ctx->swi_ctx, ctx->swi_ctx.power_down_delay_time);
	return EXE_SUCCESS;
}

/**
* @brief Power up the active device
*/
uint16_t authon_exe_power_up(struct authon_context *ctx)
{
	swi_power_up(&ctx->swi_ctx);
	swi_delay_us(&ctx->swi_ctx, ctx->swi_ctx.power_up_delay_time);
	return EXE_SUCCESS;
}

/**
* @brief Frees the SDK resource used by the active device
*/
uint16_t authon_deinit_sdk(struct authon_context *ctx)
{
	uint16_t ret;

	// Power down device first
	ret = authon_exe_power_down(ctx);
	if (ret != EXE_SUCCESS) {
		return ret;
	}

	ctx->cap.swi.device_address = 0;
	memset(ctx->cap.uid, 0, ctx->cap.uid_length);

	return SDK_SUCCESS;
}

#if (ENABLE_SWI_SEARCH == 1)
/**
* @brief Get the probe status of the given UID bit position.
* @param bit_info
*             bit 0: 0: DIP needed  ; 1: DIP not needed
*             bit 1: 0: DIE1 not needed;  1: DIE1 needed
*             bit 2: 0: DIE0 not needed;  1: DIE0 needed
*/
static uint8_t uid_search_get_dip_done_bit(uint8_t bit_info)
{
	if (bit_info & 0x01) {
		return TRUE;
	} else {
		return FALSE;
	}
}

/**
* @brief Update the probe status(Probe done or not) of the given UID bit position.
* @param bit_info
*             bit 0: 0: DIP needed  ; 1: DIP not needed
*             bit 1: 0: DIE1 not needed;  1: DIE1 needed
*             bit 2: 0: DIE0 not needed;  1: DIE0 needed
*/
static void uid_search_set_dip_done_bit(uint8_t *bit_info, uint16_t Bit)
{
	if (Bit) {
		*bit_info = (*bit_info) | 0x01; /*!< set */
	} else {
		*bit_info = (*bit_info) & 0xfe; /*!< clear */
	}
}

/**
* @brief Get the probe status(DIE0 or not) of the given UID bit position.
*        <BR>Return TRUE if DIE0(bit2) is set
* @param bit_info
*             bit 0: 0: DIP needed  ; 1: DIP not needed
*             bit 1: 0: DIE1 not needed;  1: DIE1 needed
*             bit 2: 0: DIE0 not needed;  1: DIE0 needed
*/
static uint8_t uid_search_get_die0_info(uint8_t bit_info)
{
	if (bit_info & 0x04) {
		return TRUE;
	} else {
		return FALSE;
	}
}

/**
* @brief Update the probe status(DIE0 or not) of the given UID bit position.
* @param bit_info
*             bit 0: 0: DIP needed  ; 1: DIP not needed
*             bit 1: 0: DIE1 not needed;  1: DIE1 needed
*             bit 2: 0: DIE0 not needed;  1: DIE0 needed
*/
static void uid_search_set_die0_info(uint8_t *bit_info, uint16_t Data)
{
	if (Data) {
		*bit_info = (*bit_info) | 0x04; /*!< set */
	} else {
		*bit_info = (*bit_info) & 0xfb; /*!< clear */
	}
}

/**
* @brief Get the probe status(DIE1 or not) of the given UID bit position.
*        <BR>Return TRUE if DIE1(bit1) is set
* @param bit_info
*             bit 0: 0: DIP needed  ; 1: DIP not needed
*             bit 1: 0: DIE1 not needed;  1: DIE1 needed
*             bit 2: 0: DIE0 not needed;  1: DIE0 needed
*/
static uint8_t uid_search_get_die1_info(uint8_t bit_info)
{
	if (bit_info & 0x02) {
		return TRUE;
	} else {
		return FALSE;
	}
}

/**
* @brief Update the probe status(DIE1 or not) of the given UID bit position.
* @param bit_info
*             bit 0: 0: DIP needed  ; 1: DIP not needed
*             bit 1: 0: DIE1 not needed;  1: DIE1 needed
*             bit 2: 0: DIE0 not needed;  1: DIE0 needed
*/
static void uid_search_set_die1_info(uint8_t *bit_info)
{
	*bit_info = (*bit_info) | 0x02;
}

/**
* @brief Pops the stack to retrieve the last branch location.
* @param data: read out last data in ctx->stack.
*/
static uint16_t pop(struct authon_context *ctx, uint8_t *data)
{
	if (ctx->stack_pointer == 0) {
		return APP_SID_E_STACK_EMPTY;
	}

	*data = ctx->stack[--ctx->stack_pointer];

	return APP_SID_SUCCESS;
}

/**
* @brief Push the branch location data to the stack.
* @param data: stores the branch location to the stack.
*/
static uint16_t push(struct authon_context *ctx, uint8_t data)
{
	if (ctx->stack_pointer == UID_BIT_SIZE) {
		return APP_SID_E_STACK_FULL;
	}

	ctx->stack[ctx->stack_pointer++] = data;

	return APP_SID_SUCCESS;
}

/**
* @brief Find UID of the device connected on the SWI. At the end of the search, the device is automatically selected.
*        <BR>NOTE: this function only supports one attached device.
* @param device_uid Return device UID.
*/
static uint16_t swi_bit_search_uid(struct authon_context *ctx,
				   uint8_t *device_uid)
{
	uint8_t found_0 = 0u;
	uint8_t found_1 = 0u;
	uint8_t bit_index = 0u;
	uint8_t uid_byte_index = 0u;
	uint8_t bits_to_search = UID_BIT_SIZE;
	uint8_t *uid = (uint8_t *)device_uid;

	swi_bus_command_diss(&ctx->swi_ctx);

	for (; bits_to_search != 0u; bits_to_search--) {
		uid_byte_index = bit_index >> 3u;
		uid[uid_byte_index] = (uint8_t)(uid[uid_byte_index] << 1u);

		swi_bus_command_dip0(&ctx->swi_ctx, &found_0);
		swi_bus_command_dip1(&ctx->swi_ctx, &found_1);

		if ((found_1 == FALSE) &&
		    (found_0 == TRUE)) { /*!< current bit is 0 */
			uid[uid_byte_index] &= 0xFEu;
			swi_bus_command_die0(&ctx->swi_ctx);
		} else if ((found_1 == TRUE) &&
			   (found_0 == FALSE)) { /*!< current bit is 1 */
			uid[uid_byte_index] |= 0x01u;
			swi_bus_command_die1(&ctx->swi_ctx);
		} else { /*!< current bit is neither 0 nor 1 */
			swi_abort_irq(&ctx->swi_ctx);
			return APP_SID_E_NO_DEV;
		}
		bit_index++;
	}

	return APP_SID_SUCCESS;
}
/**
* @brief Search for UIDs on the SWI where the UID will be returned in increasing order.
*    <BR>The device will be assigned with a temporary device address sequentially from 1 to N. 1 is the smallest UID found.
*    <BR>Note: The device address is loaded into the register and not written into the NVM.
*    <BR>Therefore, upon power cycle, the device will get the value from the NVM device address again.
* @param pst_detected_puid Pointer to the UID table structure.
* @param dev_count Pointer to the number of devices found. If NULL, expects only 1 device on the SWI bus.
*/
uint16_t authon_search_uid(struct authon_context *ctx,
			   uint8_t *pst_detected_puid, uint16_t *dev_count)
{
	uint8_t uid[UID_BYTE_LENGTH];
	uint8_t bit_info[UID_BIT_SIZE];
	uint8_t current_id_ptr, last_id_ptr = 0;
	uint16_t slave_count = 0;
	uint8_t found_0;
	uint8_t found_1;
	uint8_t bit_count = UID_BIT_SIZE;
	uint8_t byte_index;
	uint8_t search_done = FALSE;
	uint8_t i;
	uint8_t bytes[UID_BYTE_LENGTH];
	uint8_t *ptr = (uint8_t *)pst_detected_puid;

	//uint32_t EccTimeOut = Get_EccTimeout();

	if (dev_count == NULL) { /*!< Expects only 1 device on SWI bus */
		return swi_bit_search_uid(ctx, pst_detected_puid);
	}

	ctx->stack_pointer = 0; /*!< clears the stack pointer */
	memset(bit_info, 0, bit_count);

	do {
		if (slave_count > 0) {
			swi_bus_command_die0(&ctx->swi_ctx);
		}

		swi_bus_command_diss(&ctx->swi_ctx);
		current_id_ptr = UID_BIT_SIZE;
		bit_count = UID_BIT_SIZE;

		memset(bytes, 0, sizeof(bytes));

		for (; bit_count > 0; bit_count--) {
			current_id_ptr--;
			byte_index = (uint8_t)(11 - (current_id_ptr >> 3u));
			bytes[byte_index] = (uint8_t)(
				bytes[byte_index] << 1u); /*!< init LSB to 1 */

			/*!< Check UID bit position probe status, if false -> proceed to probe */
			if (uid_search_get_dip_done_bit(
				    bit_info[current_id_ptr]) ==
			    FALSE) { /*!< need to do DIP */
				swi_bus_command_dip0(&ctx->swi_ctx, &found_0);
				swi_bus_command_dip1(&ctx->swi_ctx, &found_1);

				/*!< current bit = 1 and current bit = 0 */
				if ((found_0 == TRUE) && (found_1 == TRUE)) {
					last_id_ptr = current_id_ptr;

					/*!< Stack stores the bit location of both DIP0_responded and DIP1_Responded */
					if (push(ctx, last_id_ptr) ==
					    APP_SID_E_STACK_FULL) {
						return APP_SID_E_STACK_FULL;
					}

					/*!< Transverse '0' Branch using DIE0 */
					bytes[byte_index] &=
						0xFEu; /*!< Enter '0' branch first. */
					swi_bus_command_die0(&ctx->swi_ctx);
					uid_search_set_die0_info(
						&bit_info[last_id_ptr],
						TRUE); /*!< Note that DIE0 is needed  */
					uid_search_set_die1_info(
						&bit_info[current_id_ptr]); /*!< Note that DIE1 is needed */
				} else if ((found_0 == TRUE) &&
					   (found_1 ==
					    FALSE)) { /*!< current bit = 1 only*/
					/*!< Transverse '0' Branch using DIE0 */
					bytes[byte_index] &=
						0xFEu; /*!< '0' is found, update UID value. Enter '0' branch. */
					swi_bus_command_die0(&ctx->swi_ctx);
					uid_search_set_die0_info(
						&bit_info[current_id_ptr],
						TRUE); /*!< Note that DIE0 is needed */
				} else if ((found_0 == FALSE) &&
					   (found_1 == TRUE)) { /*!< DIE1 */
					/*!< Transverse '1' Branch using DIE1 */
					bytes[byte_index] |=
						0x01u; /*!< '1' is found, update UID value. Enter '1' branch. */
					swi_bus_command_die1(&ctx->swi_ctx);
					uid_search_set_die0_info(
						&bit_info[current_id_ptr],
						FALSE); /*!< Note that DIE0 is not needed. */
					uid_search_set_die1_info(
						&bit_info[current_id_ptr]); /*!< Note that DIE1 is needed. */
				} else { /*!< No Response  */
					return APP_SID_E_NO_DEV;
				}

				uid_search_set_dip_done_bit(
					&bit_info[current_id_ptr],
					TRUE); //Note that current bit position is done.
				/*!< // DIPDone == TRUE //Case of DIP is done but branch occurred. */
			} else {
				if (current_id_ptr == last_id_ptr) {
					pop(ctx,
					    &current_id_ptr); /*!< Get pointer position from stack */
				}

				/*!< If DIE0 is required? -> DIE0 */
				if (uid_search_get_die0_info(
					    bit_info[current_id_ptr]) == TRUE) {
					bytes[byte_index] &= 0xFEu;
					//swi_send_raw_word_no_irq(SWI_BC, SWI_DIE0);
					swi_bus_command_die0(&ctx->swi_ctx);
					/*!< If DIE1 is required? -> DIE1 */
				} else if (uid_search_get_die1_info(
						   bit_info[current_id_ptr]) ==
					   TRUE) {
					bytes[byte_index] |= 0x01u;
					//swi_send_raw_word_no_irq(SWI_BC, SWI_DIE1);
					swi_bus_command_die1(&ctx->swi_ctx);
				}
			}

			/*!< clear dip done from last bit index that has two positive response */
			if (current_id_ptr == 0) {
				/*!< No collision, so that's the end */
				if (ctx->stack_pointer == 0) {
					search_done = TRUE;
				} else { /*!< retrieve the location of last collision bit */
					pop(ctx,
					    &last_id_ptr); /*!< refresh last_id_ptr  */
					push(ctx, last_id_ptr);

					/*!< clears all the done bits from LSB to collision location  */
					/*!< clears DIP done bit since last id pointer  */
					for (i = 0; i < last_id_ptr; i++) {
						uid_search_set_dip_done_bit(
							&bit_info[i],
							FALSE); //Note all values unti last pointer position is NOT done
					}

					uid_search_set_die0_info(
						&bit_info[last_id_ptr],
						FALSE); /*!< clear  */
				}
			} // if (current_id_ptr == 0)
		} //for ( ; bit_count > 0; bit_count --)

		/*!< Copy UID to buffer  */
		memcpy(ptr, bytes, UID_BYTE_LENGTH);

		slave_count++;
		ptr += sizeof(uid);
	} while (search_done == FALSE);

	*dev_count = slave_count;
	return APP_SID_SUCCESS;
}

/**
* @brief Search for all swi on the bus for UID
* @param uid address on the bus
* @param device_count number of devices found on the bus
* @param order either msb order or lsb order
*/
uint16_t authon_exe_search_swi_uid(struct authon_context *ctx, uint8_t *uid,
				   uint8_t *device_count, enum uid_order order)
{
	uint16_t ret;

	if ((uid == NULL) || (device_count == NULL)) {
		return EXE_E_INPUT;
	}
//Expects multiple device on the SWI bus
#if (CONFIG_SWI_MAX_COUNT > 1)
	uint16_t devices_found = 0;
	ret = authon_search_uid(ctx, uid, &devices_found);
	if (APP_SID_SUCCESS != ret) {
		return ret;
	}
#else
	ret = authon_search_uid(
		ctx, uid,
		NULL); //use a simpler search algorith if we only expect single device
	if (APP_SID_SUCCESS != ret) {
		return ret;
	} else {
		device_found = 1;
	}
	if (order == LSB_FIRST) {
		uint8_t uid96_reverse[UID_BYTE_LEN];
		for (uint8_t i = 0; i < UID_BYTE_LEN; i++) { /*!< reverse uid */
			uid96_reverse[UID_BYTE_LEN - i - 1] = uid[i];
		}
		memcpy(uid, uid96_reverse, UID_BYTE_LEN);
	}
#endif
	*device_count = devices_found;
	return EXE_SUCCESS;
}
#endif

/**
* @brief Select device by applying a given device UID.
*        <BR>After UID selection a device address can be set for easier
* @param device_to_select Pointer to UID structure to hold the UID to use.
*/
uint16_t authon_select_by_uid(struct authon_context *ctx,
			      uint8_t *device_to_select)
{
	uint8_t byte_index;
	uint8_t bit_index = 0;
	uint8_t bits_to_execute = UID_BIT_SIZE;

	uint8_t ref_bit = 0;
	uint8_t *ref_bytes = (uint8_t *)device_to_select;

	if (bits_to_execute > UID_BIT_SIZE) {
		return APP_SID_E_SELECT;
	}

	swi_bus_command_die0(&ctx->swi_ctx);
	swi_bus_command_diss(&ctx->swi_ctx);

	for (; bits_to_execute != 0u; bits_to_execute--) {
		byte_index = bit_index >> 3u;
		ref_bit = (uint8_t)((ref_bytes[byte_index] &
				     (1u << (7u - (bit_index & 0x07u)))));

		if (ref_bit == 0u) {
			swi_bus_command_die0(&ctx->swi_ctx);
		} else {
			swi_bus_command_die1(&ctx->swi_ctx);
		}

		bit_index++;
	}

	return APP_SID_SUCCESS;
}

/**
* @brief activate device by device address
* @param device_address SWI device address
*/
void authon_select_by_address(struct authon_context *ctx,
			      uint16_t device_address)
{
	swi_bus_command_eda(&ctx->swi_ctx, BYTE_HIGH(device_address));
	swi_bus_command_sda(&ctx->swi_ctx, BYTE_LOW(device_address));
}

uint16_t authon_get_error_code(struct authon_context *ctx, uint8_t *error_code)
{
	uint16_t ret;

	ret = intf_read_register(&ctx->swi_ctx, ON_SFR_SWI_ERR_CODE_STS,
				 error_code, 1);
	if (SDK_INTERFACE_SUCCESS != ret) {
		swi_print(&ctx->swi_ctx,
			  "Read Register 0x%.4X failed. ret=%x\r\n",
			  ON_SFR_SWI_ERR_CODE_STS, ret);
		return ret;
	}

	return INF_SWI_SUCCESS;
}
