// SPDX-FileCopyrightText: 2024 Infineon Technologies AG
//
// SPDX-License-Identifier: MIT

/**
 * @file   swi_bus_command.h
 * @date   January, 2021
 * @brief  Implementation of SWI bus command
 *
 */
#ifndef __SWI_COMMAND_H__
#define __SWI_COMMAND_H__

#include "interface/swi.h"

uint16_t swi_bus_command_eint(struct swi_context *ctx,
			      uint8_t *is_irq_received);
uint16_t swi_bus_command_rbl(struct swi_context *ctx, uint8_t rbl_numb);
uint16_t swi_bus_command_diss(struct swi_context *ctx);
uint16_t swi_bus_command_dip0(struct swi_context *ctx,
			      uint8_t *is_irq_received);
uint16_t swi_bus_command_dip1(struct swi_context *ctx,
			      uint8_t *is_irq_received);
uint16_t swi_bus_command_die0(struct swi_context *ctx);
uint16_t swi_bus_command_die1(struct swi_context *ctx);
uint16_t swi_bus_command_di00(struct swi_context *ctx,
			      uint8_t *is_irq_received);
uint16_t swi_bus_command_di01(struct swi_context *ctx,
			      uint8_t *is_irq_received);
uint16_t swi_bus_command_di10(struct swi_context *ctx,
			      uint8_t *is_irq_received);
uint16_t swi_bus_command_di11(struct swi_context *ctx,
			      uint8_t *is_irq_received);
uint16_t swi_bus_command_eda(struct swi_context *ctx, uint8_t addr);
uint16_t swi_bus_command_sda(struct swi_context *ctx, uint8_t addr);
uint16_t swi_bus_command_era(struct swi_context *ctx, uint8_t addr);
uint16_t swi_bus_command_wra(struct swi_context *ctx, uint8_t addr);
uint16_t swi_bus_command_rra(struct swi_context *ctx, uint8_t addr,
			     uint16_t rd_len, uint8_t *data);
uint16_t swi_bus_command_wd(struct swi_context *ctx, uint16_t data_len,
			    uint8_t *data);
uint16_t swi_bus_command_bres(struct swi_context *ctx);
uint16_t swi_bus_command_pdwn(struct swi_context *ctx);
uint16_t swi_bus_command_hrreq(struct swi_context *ctx);
uint16_t swi_bus_command_hrres(struct swi_context *ctx, uint8_t *data);
uint16_t swi_bus_command_wda(struct swi_context *ctx, uint16_t addr);
uint16_t swi_bus_command_macs(struct swi_context *ctx, uint16_t mac_addr);
uint16_t swi_bus_command_macr(struct swi_context *ctx, uint8_t *data);
uint16_t swi_bus_command_mack(struct swi_context *ctx, uint8_t *data);
uint16_t swi_bus_command_maccr5(struct swi_context *ctx, uint8_t *data);
uint16_t swi_bus_command_eccs1(struct swi_context *ctx);
uint16_t swi_bus_command_eccs2(struct swi_context *ctx);
uint16_t swi_bus_command_eccc(struct swi_context *ctx, uint8_t *data);
uint16_t swi_bus_command_eccr(struct swi_context *ctx, uint8_t *data);
uint16_t swi_bus_command_drres(struct swi_context *ctx, uint8_t *data);
uint16_t swi_bus_command_hreq1(struct swi_context *ctx, uint8_t *data);
uint16_t swi_bus_command_dres1(struct swi_context *ctx, uint8_t *data);

#endif /* __SWI_COMMAND_H__ */
