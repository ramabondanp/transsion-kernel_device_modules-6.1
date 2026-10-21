// SPDX-FileCopyrightText: 2024 Infineon Technologies AG
//
// SPDX-License-Identifier: MIT

/**
 * @file   swi.c
 * @date   January, 2021
 * @brief  Implementation of SWI bus protocol
 */
#include "interface/swi.h"
#include "helper/helper.h"
#include <linux/printk.h>

#define CHECK_TRAINING_BITS_TIME \
	(1) /*Set '1' to check the data frame value against the Tau value calculated from Training bits */

/**
* @brief count number of 1s from the input data
* @param data SWI command code + data
* @param cnt_bits total bits need to be counted
*/
static uint8_t count_ones(uint16_t data, uint8_t cnt_bits)
{
	uint8_t counter;
	uint8_t i;

	counter = 0;
	for (i = 0; i < cnt_bits; i++) {
		if (((data >> i) & 0x0001) == 0x01)
			counter++;
	}

	return counter;
}

/**
* @brief Perform time distance measurement using ktime (Linux only).
* @param tau Tau derived from training bits
* @param distance measured timing distance.
* @param value decoded binary value on when the returned value is TRUE, else ignore.
*/
static uint8_t time_distance_code(uint64_t tau, uint64_t distance,
				  uint8_t *value)
{
	uint64_t half_tau = (tau >> 1);
	//pr_info("tau=%llu distance=%llu\n", tau, distance);

	if ((distance >= half_tau) && (distance <= (tau + half_tau))) {
		*value = 0;
	} else if ((distance >= ((2 * tau) + half_tau)) &&
		   (distance <= ((3 * tau) + half_tau))) {
		*value = 1;
	} else {
		return FALSE;
	}

	return TRUE;
}

/**
* @brief Send Output to SWI.
* @param code Absolute Register address.
* @param data byte data.
* @param wait_for_irq wait_for_irq .
* @param immediate_irq immediate_irq TRUE/FASE.
* @param irq_detected Pointer to IRQ status.
*/
void swi_send_raw_word(struct swi_context *ctx, uint32_t cmd,
		       uint8_t wait_for_irq, uint8_t immediate_irq,
		       uint8_t *irq_detected)
{
	enum int_status irq_occured = NO_INTERRUPT;
	uint32_t baud_low = ctx->baud_low;
	uint32_t baud_high = ctx->baud_high;
	uint32_t baud_stop = ctx->baud_stop;

	swi_irq_disable(ctx);

	swi_gpio_write(
		ctx,
		SWI_GPIO_LEVEL_HIGH); /*!< Send a STOP signal first to have time to receive either IRQ or data! */
	swi_gpio_conf(ctx, SWI_GPIO_DIRECTION_OUT, 1);
	swi_delay_us(ctx, baud_stop); /*!< send a STOP command first */

	swi_gpio_write(ctx, SWI_GPIO_LEVEL_LOW); /*!< send BCF */
	(cmd & 0x10000) ? swi_delay_us(ctx, baud_high) :
				swi_delay_us(ctx, baud_low);

	swi_gpio_write(ctx, SWI_GPIO_LEVEL_HIGH); /*!< send _BCF */
	(cmd & 0x08000) ? swi_delay_us(ctx, baud_high) :
				swi_delay_us(ctx, baud_low);

	swi_gpio_write(ctx, SWI_GPIO_LEVEL_LOW); /*!< send bit9 */
	(cmd & 0x04000) ? swi_delay_us(ctx, baud_high) :
				swi_delay_us(ctx, baud_low);

	swi_gpio_write(ctx, SWI_GPIO_LEVEL_HIGH); /*!< send bit8 */
	(cmd & 0x02000) ? swi_delay_us(ctx, baud_high) :
				swi_delay_us(ctx, baud_low);

	swi_gpio_write(ctx, SWI_GPIO_LEVEL_LOW); /*!< send bit7 */
	(cmd & 0x01000) ? swi_delay_us(ctx, baud_high) :
				swi_delay_us(ctx, baud_low);

	swi_gpio_write(ctx, SWI_GPIO_LEVEL_HIGH); /*!< send bit6 */
	(cmd & 0x00800) ? swi_delay_us(ctx, baud_high) :
				swi_delay_us(ctx, baud_low);

	swi_gpio_write(ctx, SWI_GPIO_LEVEL_LOW); /*!< send bit5 */
	(cmd & 0x00400) ? swi_delay_us(ctx, baud_high) :
				swi_delay_us(ctx, baud_low);

	swi_gpio_write(ctx, SWI_GPIO_LEVEL_HIGH); /*!< send bit4 */
	(cmd & 0x00200) ? swi_delay_us(ctx, baud_high) :
				swi_delay_us(ctx, baud_low);

	swi_gpio_write(ctx, SWI_GPIO_LEVEL_LOW); /*!< send Parity bit3 */
	(cmd & 0x00100) ? swi_delay_us(ctx, baud_high) :
				swi_delay_us(ctx, baud_low);

	swi_gpio_write(ctx, SWI_GPIO_LEVEL_HIGH); /*!< send bit3 */
	(cmd & 0x00080) ? swi_delay_us(ctx, baud_high) :
				swi_delay_us(ctx, baud_low);

	swi_gpio_write(ctx, SWI_GPIO_LEVEL_LOW); /*!< send bit2 */
	(cmd & 0x00040) ? swi_delay_us(ctx, baud_high) :
				swi_delay_us(ctx, baud_low);

	swi_gpio_write(ctx, SWI_GPIO_LEVEL_HIGH); /*!< send bit1 */
	(cmd & 0x00020) ? swi_delay_us(ctx, baud_high) :
				swi_delay_us(ctx, baud_low);

	swi_gpio_write(ctx, SWI_GPIO_LEVEL_LOW); /*!< send Parity bit2 */
	(cmd & 0x00010) ? swi_delay_us(ctx, baud_high) :
				swi_delay_us(ctx, baud_low);

	swi_gpio_write(ctx, SWI_GPIO_LEVEL_HIGH); /*!< send bit0 */
	(cmd & 0x00008) ? swi_delay_us(ctx, baud_high) :
				swi_delay_us(ctx, baud_low);

	swi_gpio_write(ctx, SWI_GPIO_LEVEL_LOW); /*!< send Parity bit1 */
	(cmd & 0x00004) ? swi_delay_us(ctx, baud_high) :
				swi_delay_us(ctx, baud_low);

	swi_gpio_write(ctx, SWI_GPIO_LEVEL_HIGH); /*!< send Parity bit0 */
	(cmd & 0x00002) ? swi_delay_us(ctx, baud_high) :
				swi_delay_us(ctx, baud_low);

	swi_gpio_write(ctx, SWI_GPIO_LEVEL_LOW); /*!< send inversion bit */
	(cmd & 0x00001) ? swi_delay_us(ctx, baud_high) :
				swi_delay_us(ctx, baud_low);

	swi_gpio_write(ctx, SWI_GPIO_LEVEL_HIGH); /*!< send STOP */
	swi_delay_us(ctx, baud_stop);

	swi_gpio_conf(ctx, SWI_GPIO_DIRECTION_IN,
		      1); /*!< change GPIO as input */

	/* wait for interrupt */
	if (wait_for_irq == TRUE) {
		swi_wait_for_irq(ctx, &irq_occured, immediate_irq);
	}

	/* Update IRQ status */
	if ((wait_for_irq == TRUE) || (immediate_irq == TRUE)) {
		*irq_detected = irq_occured;
	}

	swi_irq_restore(ctx);
}

/**
* @brief Send SWI command without interrupt response
* @param code command to be send
* @param data data to be send
*/
void swi_send_raw_word_no_irq(struct swi_context *ctx, uint8_t code,
			      uint8_t data)
{
	uint32_t swi_cmd;
	uint8_t cmd_array[3];
	uint16_t crc_16;

	swi_cmd = (code << 8) + data;
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
	swi_send_raw_word(ctx, swi_cmd, FALSE, FALSE, NULL);
}

/**
* @brief Receive data from SWI
* @param data SWI command code + data
* @param rd_len total bits need to be counted
*/
uint16_t swi_receive_data(struct swi_context *ctx, uint8_t *data,
			  uint16_t rd_len)
{
	uint16_t ret;
	uint16_t i;
	uint16_t cmd_parity;
	uint16_t P0_MASK = 0x5555;
	uint16_t P1_MASK = 0x6666;
	uint16_t P2_MASK = 0x7878;
	uint16_t P3_MASK = 0x7f80;
	uint32_t rec_data;
	uint8_t crc_byte[256 * 3];
	uint16_t ptr;
	uint16_t crc_16;

	/* read out data */
	ptr = 0;
	for (i = 0; i < (rd_len); i++) {
		ret = swi_receive_raw_word(ctx, &rec_data);
		if (ret != INF_SWI_SUCCESS) {
			return ret;
		}

		// record in crc array
		crc_byte[ptr++] = rec_data & 0xff;
		crc_byte[ptr++] = (rec_data >> 8) & 0xff;
		crc_byte[ptr++] = (rec_data >> 16) & 0xff;

		// check on inversion bit
		if ((rec_data & 0x01) == 0x01) {
			rec_data ^= 0x7fff;
		}
		rec_data = rec_data >> 1;

		// -----------------------
		// check Parity
		// -----------------------
		cmd_parity = 0;
		// P0
		cmd_parity = count_ones(rec_data & P0_MASK, 16) & 0x01;
		// P1
		cmd_parity |= (count_ones(rec_data & P1_MASK, 16) & 0x01) << 1;
		// P2
		cmd_parity |= (count_ones(rec_data & P2_MASK, 16) & 0x01) << 2;
		// P3
		cmd_parity |= (count_ones(rec_data & P3_MASK, 16) & 0x01) << 3;

		if (cmd_parity != 0) {
			return INF_SWI_E_RD_PARITY;
		}

		rec_data = ((rec_data >> 2) & 0x01) | ((rec_data >> 3) & 0x0e) |
			   ((rec_data >> 4) & 0xfff0);

		// check on ACK
		if ((rec_data & 0x0200) != 0x0200) {
			*data = rec_data & 0xff;
			return INF_SWI_E_RD_ACK;
		}

		*(data + i) = rec_data & 0xff;
	}

	// receive CRC
	crc_16 = 0;
	for (i = 0; i < 2; i++) {
		ret = swi_receive_raw_word(ctx, &rec_data);
		if (ret != INF_SWI_SUCCESS) {
			return ret;
		}

		// check on inversion bit
		if ((rec_data & 0x01) == 0x01) {
			rec_data ^= 0x7fff;
		}
		rec_data = rec_data >> 1;

		// -----------------------
		// check Parity
		// -----------------------
		cmd_parity = 0;
		// P0
		cmd_parity = count_ones(rec_data & P0_MASK, 16) & 0x01;
		// P1
		cmd_parity |= (count_ones(rec_data & P1_MASK, 16) & 0x01) << 1;
		// P2
		cmd_parity |= (count_ones(rec_data & P2_MASK, 16) & 0x01) << 2;
		// P3
		cmd_parity |= (count_ones(rec_data & P3_MASK, 16) & 0x01) << 3;

		if (cmd_parity != 0) {
			return INF_SWI_E_RD_PARITY;
		}

		rec_data = ((rec_data >> 2) & 0x01) | ((rec_data >> 3) & 0x0e) |
			   ((rec_data >> 4) & 0xfff0);
		crc_16 |= (rec_data & 0xff) << (i * 8);
	}

	if (crc_16 != crc16_gen(crc_byte, ptr)) {
		return INF_SWI_E_RD_CRC;
	}

	return INF_SWI_SUCCESS;
}

/**
* @brief Perform GPIO sampling to form SWI raw data from device.
* @param byte Data sampled from GPIO.
*/
uint16_t swi_receive_raw_word(struct swi_context *ctx, uint32_t *data)
{
	uint16_t previous_swi_state;
	uint8_t index = SWI_FRAME_BIT_SIZE - 1;
	uint8_t bits_to_capture;

	uint32_t baud_low = ctx->baud_low;
	uint32_t timeout = ctx->response_timeout;
	uint32_t times[SWI_FRAME_BIT_SIZE];
	uint32_t max_time = 0u;
	uint32_t min_time = ~max_time;
	uint64_t count;
	uint64_t threshold;
	uint8_t i;

	uint32_t ktime_time[SWI_FRAME_BIT_SIZE];
	uint64_t ktime_firstcount;
	uint64_t ktime_count;
	uint32_t ktime_decode_data;
	uint64_t ktime_tau;
	uint8_t logic_value[1];

#if (CHECK_TRAINING_BITS_TIME == 1)
	uint64_t training_0 = 0;
	uint64_t training_1 = 0;

	uint64_t min_tau_1 = (6 * baud_low) / 10; //0.6*Tau
	uint64_t max_tau_1 = (14 * baud_low) / 10; //1.4*Tau
	uint64_t min_tau_3 = (26 * baud_low) / 10; //2.6*Tau
	uint64_t max_tau_3 = (34 * baud_low) / 10; //3.4*Tau

	uint32_t training_0us;
	uint32_t training_1us;
#endif

	swi_irq_disable(ctx);

	while (swi_gpio_read(ctx) && timeout) {
		swi_delay_us(ctx, 1);
		timeout--;
	}

	if (timeout == 0u) {
		swi_irq_restore(ctx);
		return INF_SWI_E_TIMEOUT;
	}

	//get port state
	previous_swi_state = swi_gpio_read(ctx); /*!< get port state */
	ktime_firstcount = swi_tick_ns(ctx); // First falling edge

	/*!< measure time using CPU tick and ktime*/
	for (bits_to_capture = SWI_FRAME_BIT_SIZE; bits_to_capture != 0u;
	     bits_to_capture--) {
		count = 0u;
		timeout = ctx->response_timeout;

		while ((swi_gpio_read(ctx) == previous_swi_state) && timeout) {
			swi_delay_us(ctx, 1);
			count++;
			timeout--;
		}

		times[index] = count;
		ktime_time[index] = swi_tick_ns(ctx); //next falling edge
		index--;

		previous_swi_state = swi_gpio_read(ctx);
	}

	swi_irq_restore(ctx);

	// evaluate results of ktime
	for (index = 0; index <= 16; index++) {
		if (ktime_time[16 - index] > ktime_firstcount) {
			ktime_firstcount =
				ktime_time[16 - index] - ktime_firstcount;
		} else { //this part is unlikely to happen since it take very long time to overflow 64-bit nanosec counter
			ktime_firstcount =
				ktime_time[16 - index] +
				(0xFFFFFFFFFFFFFFFF - ktime_firstcount);
		}

		ktime_count = ktime_firstcount;
		ktime_firstcount = ktime_time[16 - index];
		ktime_time[16 - index] = ktime_count;
	}

	/*!< evaluate detected results of CPU tick */
	for (index = (SWI_FRAME_BIT_SIZE - 1); index != 0u; index--) {
		count = times[index];
		if (count < min_time) {
			min_time = count;
		} else if (count > max_time) {
			max_time = count;
		} else {
			/*  no change required */
		}
	}

	/* calculate threshold using CPU tick */
	threshold = ((max_time - min_time) >> 1u);
	threshold += min_time;

	*data = 0;
	for (i = 0; i < SWI_FRAME_BIT_SIZE; i++) {
		*data |= ((times[i] > threshold) ? 1u : 0u) << i;
	}
	//pr_info("CPU tick data = 0x%x\n", *data);

	/* calculate Tau using ktime*/
	ktime_tau = ((ktime_time[16] + ktime_time[15]) >>
		     2u); //calculate Tau from training bits

#if (CHECK_TRAINING_BITS_TIME == 1)
	training_0 = ktime_time[16];
	training_1 = ktime_time[15];
	//pr_info("ktime training_0 = %llu training_1 = %llu\n", training_0, training_1);

	//Check training_0 bit to fall between min and max of receiver Tau rate.
	//0.6*Tau(min 1 Tau)
	//1.4*Tau(max 1 Tau)
	//2.6*Tau(min 3 Tau)
	//3.4*Tau(max 3 Tau)
	if (training_0 > training_1) {
		training_0us = training_0 >> 10;
		if ((training_0us < min_tau_3) || (training_0us > max_tau_3)) {
			//printk("Invalid training0= %d(%llu) min_tau_3=%llu max_tau_3=%llu\n", training_0us,training_0, min_tau_3, max_tau_3);
			return INF_SWI_E_TRAINING; //Invalid training bit, let fail it
		}
	} else {
		training_0us = training_0 >> 10;
		if ((training_0us < min_tau_1) || (training_0us > max_tau_1)) {
			//printk("Invalid training0= %d(%llu) min_tau_1=%llu max_tau_1=%llu\n", training_0us,training_0, min_tau_1, max_tau_1);
			return INF_SWI_E_TRAINING; //Invalid training bit, let fail it
		}
	}

	//Check training_1 bit to fall between min and max of receiver Tau rate.
	if (training_1 > training_0) {
		training_1us = training_1 >> 10;
		if ((training_1us < min_tau_3) || (training_1us > max_tau_3)) {
			//printk("Invalid training1= %d(%llu) min_tau_3=%llu max_tau_3=%llu\n", training_1us, training_1, min_tau_3, max_tau_3);
			return INF_SWI_E_TRAINING; //Invalid training bit, let fail it
		}
	} else {
		training_1us = training_1 >> 10;
		if ((training_1us < min_tau_1) || (training_1us > max_tau_1)) {
			//printk("Invalid training1= %d(%llu) min_tau_1=%llu max_tau_1=%llu\n", training_1us, training_1, min_tau_1, max_tau_1);
			return INF_SWI_E_TRAINING; //Invalid training bit, let fail it
		}
	}
#endif

	ktime_decode_data = 0x00;

	for (i = 0; i < 17; i++) {
		if (time_distance_code(ktime_tau, ktime_time[i], logic_value) ==
		    TRUE) {
			ktime_decode_data |= (logic_value[0] << (i));
		} else {
			return INF_SWI_E_TRAINING; //Invalid training bit, let fail it
		}
	}

	if (ktime_decode_data != (*data)) {
		///pr_err("mismatched CPU tick:0x%x Ktime:0x%x\n", *data, ktime_decode_data);
		*data = ktime_decode_data; //if the data is mismatched use the ktime derieved data
	}

	return INF_SWI_SUCCESS;
}

/**
* @brief Compute parity bits for SWI command, return command with parity
* @param code SWI command code
* @param data SWI command data
*/
uint16_t swi_compute_parity(uint16_t cmd)
{
	uint16_t cmd_parity;
	uint16_t P0_MASK = 0x055b;
	uint16_t P1_MASK = 0x066d;
	uint16_t P2_MASK = 0x078e;
	uint16_t P3_MASK = 0x07f0;

	cmd_parity = 0;
	// P0
	cmd_parity = count_ones(cmd & P0_MASK, 16) & 0x01;
	// P1
	cmd_parity |= (count_ones(cmd & P1_MASK, 16) & 0x01) << 1;
	// P2
	cmd_parity |= (count_ones(cmd & P2_MASK, 16) & 0x01) << 3;
	// P3
	cmd_parity |= (count_ones(cmd & P3_MASK, 16) & 0x01) << 7;

	// add in command
	cmd_parity |= ((cmd & 0xfff0) << 4) | ((cmd & 0x0e) << 3) |
		      ((cmd & 0x01) << 2);

	return cmd_parity;
}

/**
* @brief Compute invert flag and add into SWI command
* @param cmd_parity SWI command with parity bits added
*/
uint32_t swi_add_invert_flag(uint16_t cmd_parity)
{
	uint32_t cmd_17b;

	cmd_17b = cmd_parity << 1;

	if (count_ones(cmd_parity, 14) >= 8) {
		cmd_17b |= 1;
		cmd_17b ^= 0x7ffe;
	}

	return cmd_17b;
}

/**
* @brief host Send SWI interrupt pulse to device if no interrupt response from device for the command that has interrupt response
*/
void swi_abort_irq(struct swi_context *ctx)
{
	uint32_t baud_low = ctx->baud_low;

	swi_gpio_conf(ctx, SWI_GPIO_DIRECTION_OUT, 0);
	swi_delay_us(ctx, baud_low); /*!< delay for 1 tau */
	swi_gpio_write(ctx, SWI_GPIO_LEVEL_HIGH);
	swi_gpio_conf(ctx, SWI_GPIO_DIRECTION_IN, 1);
}

/**
* @brief Wait for SWI interrupt from device
* @param irq_detected Pointer to IRQ status, Expects IRQ immediated? :TRUE/FASE.
* @param immediate IRQ Status.
*/
void swi_wait_for_irq(struct swi_context *ctx, enum int_status *irq_detected,
		      uint8_t immediate)
{
	enum int_status result = NO_INTERRUPT;
	volatile uint32_t timeout;
	uint32_t baud_low = ctx->baud_low;
	uint32_t baud_stop = ctx->baud_stop;
	uint32_t response_timeout = ctx->response_timeout;
	uint32_t response_long_retry = ctx->response_long_retry;

	if (immediate) {
		timeout = response_timeout;
	} else {
		timeout = response_long_retry;
	}

	*irq_detected = NO_INTERRUPT;

	while (timeout) {
		if (!swi_gpio_read(ctx)) {
			result = GOT_INTERRUPT;
			*irq_detected = GOT_INTERRUPT;
			break;
		}
		timeout--;
	}

	swi_delay_us(ctx, baud_low);
	swi_delay_us(ctx, baud_stop);

	if (result == NO_INTERRUPT) {
		swi_abort_irq(ctx);
	}

	return;
}

/**
* @brief power up device by one SWI pulse after PDWN command
*/
void swi_power_up(struct swi_context *ctx)
{
	uint32_t baud_power_up_time = ctx->power_up_delay_time;

	swi_gpio_conf(ctx, SWI_GPIO_DIRECTION_OUT, 1);
	swi_delay_us(ctx, baud_power_up_time);
}

/**
* @brief power down device by pull SWI line low, after SWI pull low power down,
* need to call swi_power_up_hard_reset() to power up device.
*/
void swi_power_down_hard_reset(struct swi_context *ctx)
{
	uint32_t baud_power_up_time = ctx->power_up_delay_time;
	uint32_t wake_up_delay = ctx->wake_up_delay_time;
	uint32_t wake_up_low = ctx->wake_up_low_time;

	swi_gpio_conf(ctx, SWI_GPIO_DIRECTION_OUT, 0);
	swi_delay_us(ctx, wake_up_delay + wake_up_low);
	swi_gpio_write(ctx, SWI_GPIO_LEVEL_HIGH);
	swi_delay_us(ctx, baud_power_up_time);
}

/**
* @brief power down device by pull SWI line low
*/
void swi_power_down(struct swi_context *ctx)
{
	swi_gpio_conf(ctx, SWI_GPIO_DIRECTION_OUT, 0);
	swi_gpio_write(ctx, SWI_GPIO_LEVEL_LOW);
}

/**
* @brief power up device by pull SWI line high
*/
void swi_power_up_hard_reset(struct swi_context *ctx)
{
	uint32_t power_down_time = ctx->power_down_delay_time;
	uint32_t power_up_time = ctx->power_up_delay_time;

	swi_gpio_conf(ctx, SWI_GPIO_DIRECTION_OUT, 0);
	swi_delay_us(ctx, power_down_time);
	swi_gpio_write(ctx, SWI_GPIO_LEVEL_HIGH);
	swi_delay_us(ctx, power_up_time);
}
