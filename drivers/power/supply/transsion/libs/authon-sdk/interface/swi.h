// SPDX-FileCopyrightText: 2024 Infineon Technologies AG
//
// SPDX-License-Identifier: MIT

/**
 * @file   swi.h
 * @date   January, 2021
 * @brief  Implementation of SWI bus protocol
 */
#ifndef __SWI_H__
#define __SWI_H__

#include "authon_status.h"
#include "platform/authon_platform.h"

#define SWI_FRAME_BIT_SIZE \
	17 /*!< SWI consists of 2 training + 2 CMD + 8 DATA + 1 INV bits + 4 Parity bits*/

/* Transaction Elements */
/* BroadCast */
#define SWI_BC (0x08u) /* Bus Command */
#define SWI_EDA (0x09u) /* Extended Device Address */
#define SWI_SDA (0x0Au) /* Slave Device Address */

/* MultiCast */
#define SWI_WD (0x04u) /* Write Data */
#define SWI_ERA (0x05u) /* Extended Register Address */
#define SWI_WRA (0x06u) /* Write Register Address */
#define SWI_RRA (0x07u) /* Read Register Address */

/* Unicast */
#define SWI_RD_ACK (0x06u) /* ACK and not End of transmission */
#define SWI_RD_NACK (0x04u) /* NACK and not End of transmission */
#define SWI_RD_ACK_EOT (0x07u) /* ACK and End of transmission */
#define SWI_RD_NACK_EOT (0x05u) /* NACK and End of transmission */

/* Bus Command */
#define SWI_BRES (0x00u) /* Bus Reset */
#define SWI_PDWN (0x02u) /* Power Down */
#define SWI_EINT (0x10u) /* Enable Interrupt */
#define SWI_WDA (0x18u) /* Write Device Address */
#define SWI_RBL0 (0x20u) /* RBLn Set Read Burst Length 2^n */
#define SWI_RBL1 (0x21u) /* RBLn Set Read Burst Length 2^n */
#define SWI_RBL2 (0x22u) /* RBLn Set Read Burst Length 2^n */
#define SWI_RBL3 (0x23u) /* RBLn Set Read Burst Length 2^n */
#define SWI_DISS (0x30u) /* Device ID Search Start */
#define SWI_DIMM (0x32u) /* Device ID Search Memory */
#define SWI_DIRC (0x33u) /* Device ID Search Recall */
#define SWI_DIE0 (0x34u) /* Device ID Search Enter 0 */
#define SWI_DIE1 (0x35u) /* Device ID Search Enter 1 */
#define SWI_DIP0 (0x36u) /* Device ID Search Probe 0 */
#define SWI_DIP1 (0x37u) /* Device ID Search Probe 1 */
#define SWI_DI00 (0x38u) /* DIS Enter 0 Probe 0 (DIE0 + DIP0) */
#define SWI_DI01 (0x39u) /* DIS Enter 0 Probe 1 (DIE0 + DIP1) */
#define SWI_DI10 (0x3Au) /* DIS Enter 1 Probe 0 (DIE1 + DIP0) */
#define SWI_DI11 (0x3Bu) /* DIS Enter 1 Probe 1 (DIE1 + DIP1) */
#define SWI_DASM (0x40u) /* Device Activation Stick Mode */
#define SWI_DACL (0x41u) /* Device Activation Clear */
#define SWI_CURD (0x70u) /* Call for Un-registered Devices */

#define SWI_ECCS1 (0xC1) /* Start ECC1 */
#define SWI_ECCS2 (0xC2) /* Start ECC2 */
#define SWI_ECCC (0xC3) /* Send ECC challenge */
#define SWI_ECCR (0xC4) /* ECC response */

#define SWI_MACS (0xD2) /* Start MAC  Process*/
#define SWI_MACR (0xD4) /* MAC response */
#define SWI_MACK (0xD8) /* MAC kill */
#define SWI_MACCR5 (0xdd)

#define SWI_HRREQ (0xE0) /* Host Request Random Number */
#define SWI_HRRES (0xE1) /* Host Receive Random Number */
#define SWI_DRRES (0xE2)
#define SWI_HREQ1 (0xE3)
#define SWI_DRES1 (0xE4)

#define SWI_ERA_DUMMYADDR (0x50)
#define SWI_WRARRA_DUMMYADDR (0x80)

#define swi_set_platform(swi_ctx, pf_ctx)                              \
	do {                                                           \
		(swi_ctx)->pf_ctx = (authon_platform_context *)pf_ctx; \
	} while (0)

#define swi_gpio_conf(ctx, a, b) authon_pf_gpio_conf((ctx)->pf_ctx, a, b)
#define swi_gpio_write(ctx, a) authon_pf_gpio_write((ctx)->pf_ctx, a)
#define swi_gpio_read(ctx) authon_pf_gpio_read((ctx)->pf_ctx)
#define swi_irq_disable(ctx) authon_pf_irq_disable((ctx)->pf_ctx)
#define swi_irq_restore(ctx) authon_pf_irq_restore((ctx)->pf_ctx)
#define swi_delay_us(ctx, a) authon_pf_delay_us((ctx)->pf_ctx, a)
#define swi_delay_ms(ctx, a) authon_pf_delay_ms((ctx)->pf_ctx, a)
#define swi_tick_ns(ctx) authon_pf_tick_ns((ctx)->pf_ctx)
#define swi_alt_rng(ctx, a, b) authon_pf_alt_rng((ctx)->pf_ctx, a, b)
#define swi_print(ctx, a, ...) authon_pf_print((ctx)->pf_ctx, a, ##__VA_ARGS__)
#define swi_print_cont(ctx, a, ...) \
	authon_pf_print_cont((ctx)->pf_ctx, a, ##__VA_ARGS__)

/**
 * @brief Defines the speed of SWI baud rate
 */
enum swi_speed {
	HIGH_SPEED = 0, /*!< High speed SWI baud rate */
	LOW_SPEED /*!< Low speed SWI baud rate */
};

struct swi_context {
	uint32_t baud_low; /* 1 Tau */
	uint32_t baud_high; /* 3 Tau */
	uint32_t baud_stop; /* 5 Tau */
	uint32_t response_timeout; /* 10 Tau */
	uint32_t response_long_retry; /* >100ms for ECC max time */
	uint32_t power_down_delay_time; /* 2000us for direct power mode; >2500 for indirect power mode */
	uint32_t power_up_delay_time; /* 8000us for direct power mode; 10000 for indirect power mode */
	uint32_t wake_up_delay_time; /* tWK,DELAY >7000us */
	uint32_t wake_up_low_time; /* tWK,LOW 10us */
	/*uint32_t            wake_up_filter_time;     tWK 28us */
	uint32_t reset_delay_time; /* tSRD >1000us */

	authon_platform_context *pf_ctx;
};

uint16_t swi_compute_parity(uint16_t cmd);
uint32_t swi_add_invert_flag(uint16_t cmd_parity);

void swi_send_raw_word(struct swi_context *ctx, uint32_t cmd,
		       uint8_t wait_for_irq, uint8_t immediate_irq,
		       uint8_t *irq_detected);
void swi_send_raw_word_no_irq(struct swi_context *ctx, uint8_t code,
			      uint8_t data);
void swi_abort_irq(struct swi_context *ctx);
void swi_wait_for_irq(struct swi_context *ctx, enum int_status *irq_detected,
		      uint8_t immediate);
void swi_power_up(struct swi_context *ctx);
void swi_power_down(struct swi_context *ctx);
void swi_power_down_hard_reset(struct swi_context *ctx);
void swi_power_up_hard_reset(struct swi_context *ctx);
uint16_t swi_receive_data(struct swi_context *ctx, uint8_t *byte,
			  uint16_t rd_len);
uint16_t swi_receive_raw_word(struct swi_context *ctx, uint32_t *data);

#endif /* __SWI_H__ */
