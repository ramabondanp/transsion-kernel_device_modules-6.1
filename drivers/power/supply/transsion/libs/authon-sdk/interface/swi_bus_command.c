// SPDX-FileCopyrightText: 2024 Infineon Technologies AG
//
// SPDX-License-Identifier: MIT

/**
 * @file   swi_bus_command.c
 * @date   January, 2021
 * @brief  Implementation of SWI bus command
 */
#include "interface/swi_bus_command.h"
#include "authon_status.h"
#include "helper/helper.h" /* For crc16_gen() */
#include "interface/swi.h" /* For SWI_... */
#include "ecc/ecc.h" /* For MAC_BYTE_LEN */
#include "nvm/nvm.h" /* For ..._PROGRAMMING_TIME */

/**
* @brief Generates SWI bus command EINT
* @param is_irq_received interrupt status received.
*/
uint16_t swi_bus_command_eint(struct swi_context *ctx, uint8_t *is_irq_received)
{
	uint32_t swi_cmd;
	uint8_t cmd_array[3];
	uint16_t crc_16;

	swi_cmd = (SWI_BC << 8) + SWI_EINT;
	// send command
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, FALSE, FALSE, NULL);

	// compute CRC
	cmd_array[0] = swi_cmd & 0xff;
	cmd_array[1] = (swi_cmd >> 8) & 0xff;
	cmd_array[2] = (swi_cmd >> 16) & 0x1;
	crc_16 = crc16_gen(cmd_array, 3);

	// send CRC0
	swi_cmd = (SWI_WD << 8) + (crc_16 & 0xff);
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, FALSE, FALSE, NULL);

	// send CRC1
	swi_cmd = (SWI_WD << 8) + ((crc_16 >> 8) & 0xff);
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, TRUE, FALSE, is_irq_received);

	return INF_SWI_SUCCESS;
}

/**
* @brief Generates SWI bus command RBL
* @param rbl_numb read burst length
*/
uint16_t swi_bus_command_rbl(struct swi_context *ctx, uint8_t rbl_numb)
{
	switch (rbl_numb) {
	case 0:
		swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_RBL0);
		break;
	case 1:
		swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_RBL1);
		break;
	case 2:
		swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_RBL2);
		break;
	case 3:
		swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_RBL3);
		break;
	default:
		break;
	}

	return INF_SWI_SUCCESS;
}

/**
* @brief Generates SWI bus command DISS
*/
uint16_t swi_bus_command_diss(struct swi_context *ctx)
{
	swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_DISS);
	return INF_SWI_SUCCESS;
}

/**
* @brief Generates SWI bus command DIP0 and and check if having interrupt response
* @param is_irq_received Interrupt received status
*/
uint16_t swi_bus_command_dip0(struct swi_context *ctx, uint8_t *is_irq_received)
{
	uint32_t swi_cmd;
	uint8_t cmd_array[3];
	uint16_t crc_16;

	swi_cmd = (SWI_BC << 8) + SWI_DIP0;
	// send command
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, FALSE, FALSE, NULL);

	// compute CRC
	cmd_array[0] = swi_cmd & 0xff;
	cmd_array[1] = (swi_cmd >> 8) & 0xff;
	cmd_array[2] = (swi_cmd >> 16) & 0x1;
	crc_16 = crc16_gen(cmd_array, 3);

	// send CRC0
	swi_cmd = (SWI_WD << 8) + (crc_16 & 0xff);
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, FALSE, FALSE, NULL);

	// send CRC1
	swi_cmd = (SWI_WD << 8) + ((crc_16 >> 8) & 0xff);
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, TRUE, TRUE, is_irq_received);

	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command DIP1 and check if having interrupt response
* @param is_irq_received Interrupt received status
*/
uint16_t swi_bus_command_dip1(struct swi_context *ctx, uint8_t *is_irq_received)
{
	uint32_t swi_cmd;
	uint8_t cmd_array[3];
	uint16_t crc_16;

	swi_cmd = (SWI_BC << 8) + SWI_DIP1;
	// send command
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, FALSE, FALSE, NULL);

	// compute CRC
	cmd_array[0] = swi_cmd & 0xff;
	cmd_array[1] = (swi_cmd >> 8) & 0xff;
	cmd_array[2] = (swi_cmd >> 16) & 0x1;
	crc_16 = crc16_gen(cmd_array, 3);

	// send CRC0
	swi_cmd = (SWI_WD << 8) + (crc_16 & 0xff);
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, FALSE, FALSE, NULL);

	// send CRC1
	swi_cmd = (SWI_WD << 8) + ((crc_16 >> 8) & 0xff);
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, TRUE, TRUE, is_irq_received);

	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command DIE0
*/
uint16_t swi_bus_command_die0(struct swi_context *ctx)
{
	swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_DIE0);
	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command DIE1
*/
uint16_t swi_bus_command_die1(struct swi_context *ctx)
{
	swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_DIE1);
	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command DI00 and check if having interrupt response
* @param is_irq_received Interrupt received status
*/
uint16_t swi_bus_command_di00(struct swi_context *ctx, uint8_t *is_irq_received)
{
	uint32_t swi_cmd;
	uint8_t cmd_array[3];
	uint16_t crc_16;

	swi_cmd = (SWI_BC << 8) + SWI_DI00;
	// send command
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, FALSE, FALSE, NULL);

	// compute CRC
	cmd_array[0] = swi_cmd & 0xff;
	cmd_array[1] = (swi_cmd >> 8) & 0xff;
	cmd_array[2] = (swi_cmd >> 16) & 0x1;
	crc_16 = crc16_gen(cmd_array, 3);

	// send CRC0
	swi_cmd = (SWI_WD << 8) + (crc_16 & 0xff);
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, FALSE, FALSE, NULL);

	// send CRC1
	swi_cmd = (SWI_WD << 8) + ((crc_16 >> 8) & 0xff);
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, TRUE, TRUE, is_irq_received);

	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command DI01 and check if having interrupt response
* @param is_irq_received Interrupt received status
*/
uint16_t swi_bus_command_di01(struct swi_context *ctx, uint8_t *is_irq_received)
{
	uint32_t swi_cmd;
	uint8_t cmd_array[3];
	uint16_t crc_16;

	swi_cmd = (SWI_BC << 8) + SWI_DI01;
	// send command
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, FALSE, FALSE, NULL);

	// compute CRC
	cmd_array[0] = swi_cmd & 0xff;
	cmd_array[1] = (swi_cmd >> 8) & 0xff;
	cmd_array[2] = (swi_cmd >> 16) & 0x1;
	crc_16 = crc16_gen(cmd_array, 3);

	// send CRC0
	swi_cmd = (SWI_WD << 8) + (crc_16 & 0xff);
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, FALSE, FALSE, NULL);

	// send CRC1
	swi_cmd = (SWI_WD << 8) + ((crc_16 >> 8) & 0xff);
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, TRUE, TRUE, is_irq_received);

	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command DI10 and check if having interrupt response
* @param is_irq_received Interrupt received status
*/
uint16_t swi_bus_command_di10(struct swi_context *ctx, uint8_t *is_irq_received)
{
	uint32_t swi_cmd;
	uint8_t cmd_array[3];
	uint16_t crc_16;

	swi_cmd = (SWI_BC << 8) + SWI_DI10;
	// send command
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, FALSE, FALSE, NULL);

	// compute CRC
	cmd_array[0] = swi_cmd & 0xff;
	cmd_array[1] = (swi_cmd >> 8) & 0xff;
	cmd_array[2] = (swi_cmd >> 16) & 0x1;
	crc_16 = crc16_gen(cmd_array, 3);

	// send CRC0
	swi_cmd = (SWI_WD << 8) + (crc_16 & 0xff);
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, FALSE, FALSE, NULL);

	// send CRC1
	swi_cmd = (SWI_WD << 8) + ((crc_16 >> 8) & 0xff);
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, TRUE, TRUE, is_irq_received);

	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command DI11 and check if having interrupt response
* @param is_irq_received Interrupt received status
*/
uint16_t swi_bus_command_di11(struct swi_context *ctx, uint8_t *is_irq_received)
{
	uint32_t swi_cmd;
	uint8_t cmd_array[3];
	uint16_t crc_16;

	swi_cmd = (SWI_BC << 8) + SWI_DI11;
	// send command
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, FALSE, FALSE, NULL);

	// compute CRC
	cmd_array[0] = swi_cmd & 0xff;
	cmd_array[1] = (swi_cmd >> 8) & 0xff;
	cmd_array[2] = (swi_cmd >> 16) & 0x1;
	crc_16 = crc16_gen(cmd_array, 3);

	// send CRC0
	swi_cmd = (SWI_WD << 8) + (crc_16 & 0xff);
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, FALSE, FALSE, NULL);

	// send CRC1
	swi_cmd = (SWI_WD << 8) + ((crc_16 >> 8) & 0xff);
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, TRUE, TRUE, is_irq_received);

	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command ERA
* @param addr higher byte of device address
*/
uint16_t swi_bus_command_eda(struct swi_context *ctx, uint8_t addr)
{
	swi_send_raw_word_no_irq(ctx, SWI_EDA, addr);
	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command SDA
* @param addr Lower byte of device address
*/
uint16_t swi_bus_command_sda(struct swi_context *ctx, uint8_t addr)
{
	swi_send_raw_word_no_irq(ctx, SWI_SDA, addr);
	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command ERA
* @param addr Higher byte of device address
*/
uint16_t swi_bus_command_era(struct swi_context *ctx, uint8_t addr)
{
	swi_send_raw_word_no_irq(ctx, SWI_ERA, addr);
	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command WRA
* @param addr Lower byte of device address
*/
uint16_t swi_bus_command_wra(struct swi_context *ctx, uint8_t addr)
{
	swi_send_raw_word_no_irq(ctx, SWI_WRA, addr);
	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command RRA
* @param addr Lower byte of register address
* @param rd_len read byte length
* @param data data to be store after read
*/
uint16_t swi_bus_command_rra(struct swi_context *ctx, uint8_t addr,
			     uint16_t rd_len, uint8_t *data)
{
	uint16_t ret = INF_SWI_INIT;
	uint32_t baud_stop = ctx->baud_stop;

	swi_send_raw_word_no_irq(ctx, SWI_RRA, addr);
	ret = swi_receive_data(ctx, data, rd_len);

	swi_delay_us(ctx, baud_stop);

	return ret;
}

/**
* @brief Send SWI bus command WD
* @param data_len write byte length
* @param data data to be written
*/
uint16_t swi_bus_command_wd(struct swi_context *ctx, uint16_t data_len,
			    uint8_t *data)
{
	uint16_t i, j;
	uint32_t swi_cmd;
	uint8_t cmd_array[3 * 50]; // max wd length was ECCC, 21 bytes
	uint16_t crc_16;

	j = 0;
	for (i = 0; i < data_len; i++) {
		swi_cmd = (SWI_WD << 8) + data[i];
		// send command
		swi_cmd = swi_compute_parity(swi_cmd);
		swi_cmd = swi_add_invert_flag(swi_cmd);
		swi_send_raw_word(ctx, swi_cmd, FALSE, FALSE, NULL);

		// compute CRC
		cmd_array[j++] = swi_cmd & 0xff;
		cmd_array[j++] = (swi_cmd >> 8) & 0xff;
		cmd_array[j++] = (swi_cmd >> 16) & 0x1;
	}
	crc_16 = crc16_gen(cmd_array, j);

	// send CRC0
	swi_cmd = (SWI_WD << 8) + (crc_16 & 0xff);
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, FALSE, FALSE, NULL);

	// send CRC1
	swi_cmd = (SWI_WD << 8) + ((crc_16 >> 8) & 0xff);
	swi_cmd = swi_compute_parity(swi_cmd);
	swi_cmd = swi_add_invert_flag(swi_cmd);
	swi_send_raw_word(ctx, swi_cmd, FALSE, FALSE, NULL);

	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command BRES
*/
uint16_t swi_bus_command_bres(struct swi_context *ctx)
{
	swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_BRES);
	swi_delay_us(ctx, ctx->power_down_delay_time);

	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command PDWN
*/
uint16_t swi_bus_command_pdwn(struct swi_context *ctx)
{
	swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_PDWN);
	swi_delay_us(ctx, ctx->power_down_delay_time);

	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command HRREQ
*/
uint16_t swi_bus_command_hrreq(struct swi_context *ctx)
{
	swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_HRREQ);
	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command HRRES
*/
uint16_t swi_bus_command_hrres(struct swi_context *ctx, uint8_t *data)
{
	swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_HRRES);
	swi_bus_command_era(ctx, SWI_ERA_DUMMYADDR);

	return swi_bus_command_rra(ctx, SWI_WRARRA_DUMMYADDR, MAC_BYTE_LEN,
				   data);
}

/**
* @brief Send SWI bus command WDA
* @param addr New device address
*/
uint16_t swi_bus_command_wda(struct swi_context *ctx, uint16_t addr)
{
	uint8_t data_wr[2];

	swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_WDA);
	swi_bus_command_era(ctx, SWI_ERA_DUMMYADDR);
	swi_bus_command_wra(ctx, SWI_WRARRA_DUMMYADDR);
	data_wr[0] = BYTE_HIGH(addr);
	data_wr[1] = BYTE_LOW(addr);
	swi_bus_command_wd(ctx, 2, data_wr);

	swi_delay_ms(ctx, NVM_PROGRAMMING_TIME);

	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command MACS
* @param mac_addr NVM address to be MAC
*/
uint16_t swi_bus_command_macs(struct swi_context *ctx, uint16_t mac_addr)
{
	swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_MACS);
	swi_bus_command_era(ctx, BYTE_HIGH(mac_addr));
	swi_bus_command_wra(ctx, BYTE_LOW(mac_addr));

	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command MACR
* @param data read response data
*/
uint16_t swi_bus_command_macr(struct swi_context *ctx, uint8_t *data)
{
	swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_MACR);
	swi_bus_command_era(ctx, SWI_ERA_DUMMYADDR);

	return swi_bus_command_rra(ctx, SWI_WRARRA_DUMMYADDR, MAC_BYTE_LEN,
				   data);
}

/**
* @brief Send SWI bus command MACK
* @param data pointer for mac result of kill password
*/
uint16_t swi_bus_command_mack(struct swi_context *ctx, uint8_t *data)
{
	swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_MACK);
	swi_bus_command_era(ctx, SWI_ERA_DUMMYADDR);
	swi_bus_command_wra(ctx, SWI_WRARRA_DUMMYADDR);
	swi_bus_command_wd(ctx, MAC_BYTE_LEN, data);

	swi_delay_ms(ctx, MACK_PROGRAMMING_TIME);

	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command MACCR5
* @param data pointer for mac result of reset password
*/
uint16_t swi_bus_command_maccr5(struct swi_context *ctx, uint8_t *data)
{
	swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_MACCR5);
	swi_bus_command_era(ctx, SWI_ERA_DUMMYADDR);
	swi_bus_command_wra(ctx, SWI_WRARRA_DUMMYADDR);

	swi_delay_ms(ctx, MACCR_PROGRAMMING_TIME);

	return swi_bus_command_wd(ctx, MAC_BYTE_LEN, data);
}

/**
* @brief Send SWI bus command ECCS1
*/
uint16_t swi_bus_command_eccs1(struct swi_context *ctx)
{
	swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_ECCS1);

	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command ECCS2
*/
uint16_t swi_bus_command_eccs2(struct swi_context *ctx)
{
	swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_ECCS2);

	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command ECCC
* @param data pointer to challenge data
*/
uint16_t swi_bus_command_eccc(struct swi_context *ctx, uint8_t *data)
{
	swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_ECCC);
	swi_bus_command_era(ctx, SWI_ERA_DUMMYADDR);
	swi_bus_command_wra(ctx, SWI_WRARRA_DUMMYADDR);

	return swi_bus_command_wd(ctx, ECC_CHALLENGE_LEN, data);
}

/**
* @brief Send SWI bus command ECCR
* @param data pointer to response data
*/
uint16_t swi_bus_command_eccr(struct swi_context *ctx, uint8_t *data)
{
	swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_ECCR);
	swi_bus_command_era(ctx, SWI_ERA_DUMMYADDR);

	return swi_bus_command_rra(ctx, SWI_WRARRA_DUMMYADDR, ECC_RESPONSE_LEN,
				   data);
}

/**
* @brief Send SWI bus command DRRES
* @param data pointer to nouce B
*/
uint16_t swi_bus_command_drres(struct swi_context *ctx, uint8_t *data)
{
	swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_DRRES);
	swi_bus_command_era(ctx, SWI_ERA_DUMMYADDR);
	swi_bus_command_wra(ctx, SWI_WRARRA_DUMMYADDR);
	swi_bus_command_wd(ctx, MAC_BYTE_LEN, data);

	return INF_SWI_SUCCESS;
}

/**
* @brief Send SWI bus command HREQ1
* @param data pointer to Tag A
*/
uint16_t swi_bus_command_hreq1(struct swi_context *ctx, uint8_t *data)
{
	swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_HREQ1);
	swi_bus_command_era(ctx, SWI_ERA_DUMMYADDR);
	swi_bus_command_wra(ctx, SWI_WRARRA_DUMMYADDR);

	return swi_bus_command_wd(ctx, MAC_BYTE_LEN, data);
}

/**
* @brief Send SWI bus command DRES1
* @param data pointer to Tag B
*/
uint16_t swi_bus_command_dres1(struct swi_context *ctx, uint8_t *data)
{
	swi_send_raw_word_no_irq(ctx, SWI_BC, SWI_DRES1);
	swi_bus_command_era(ctx, SWI_ERA_DUMMYADDR);

	return swi_bus_command_rra(ctx, SWI_WRARRA_DUMMYADDR, MAC_BYTE_LEN,
				   data);
}
