// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#include "aw_lamp_interface.h"

void aw_get_colorful_rgb_data(unsigned char *p_led_color,
							  AW_COLORFUL_INTERFACE_STRUCT *p_colorful_interface)
{
	if ( p_colorful_interface != NULL
		&& p_colorful_interface->getBrightnessfunc != NULL
		&& p_colorful_interface->p_algo_data != NULL) {
		p_colorful_interface->p_algo_data->total_frames = p_colorful_interface->total_frames;
		p_colorful_interface->p_algo_data->cur_frame = p_colorful_interface->cur_frame;
		/* get led color of red in RGB lamp */
		if (p_colorful_interface->p_color_1 != p_colorful_interface->p_color_2) {
			p_colorful_interface->p_algo_data->data_start = p_colorful_interface->p_color_1;
			p_colorful_interface->p_algo_data->data_end = p_colorful_interface->p_color_2;
			*p_led_color = p_colorful_interface->getBrightnessfunc(p_colorful_interface->p_algo_data);
		} else {
			*p_led_color  = p_colorful_interface->p_color_1;
		}
	}

	
}

void aw_set_rgb_color(unsigned  char rgb_idx, unsigned  char *dim_reg, unsigned char p_rgb_dim)
{

		dim_reg[rgb_idx] = p_rgb_dim;
	

}
void aw_set_colorful_rgb_data(unsigned  char rgb_idx, unsigned  char *dim_reg,
				AW_COLORFUL_INTERFACE_STRUCT *p_colorful_interface)
{
	unsigned  char rgb_color = 0;

	if (p_colorful_interface != NULL) {
		aw_get_colorful_rgb_data(&rgb_color, p_colorful_interface);
		aw_set_rgb_color(rgb_idx, dim_reg, rgb_color);
	}
}
#if 0
unsigned char aw_get_real_dim(unsigned char led_dim)
{
	unsigned char real_dim = 0;

	/* dim[7:6] register is used for pattern, dim[5:0] is used for dim */
	real_dim = led_dim >> 2;
	if (real_dim == 0 && led_dim > 0) {
		real_dim = 1;
	}
	return real_dim;
}
#endif

void aw_set_rgb_brightness(unsigned char rgb_idx,
						   unsigned char *fade_reg,
						   unsigned char brightness)
{
	if (fade_reg != NULL) {
		fade_reg[rgb_idx] = brightness;
	}
}

