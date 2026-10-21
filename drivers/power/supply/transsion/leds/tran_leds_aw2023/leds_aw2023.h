// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Transsion Inc.
 */

#ifndef __LINUX_AW2023_LED_H__
#define __LINUX_AW2023_LED_H__

/* The definition of each time described as shown in figure.
 *        /-----------\
 *       /      |      \
 *      /|      |      |\
 *     / |      |      | \-----------
 *       |hold_time_ms |      |
 *       |             |      |
 * rise_time_ms  fall_time_ms |
 *                       off_time_ms
 */


enum  led_color_value {
	COLOR_RED = 0,
	COLOR_GREEN,
	COLOR_BLUE,
	COLOR_YELLOW,	//huang r+g
	COLOR_PURPLE,	//zi r+b
	COLOR_CYAN,		//qing g+b
	COLOR_WHITE,	//r+g+b
	COLOR_ALL_MAX,
};

enum  led_work_value {
	AW2023_LED_OFF,
	AW2023_LED_NORMAL,
	AW2023_LED_BLINK,
	AW2023_LED_BREATHE,
	AW2023_LED_ALL_MAX,
};

#define AW2023_MAX_BRIGHTNESS 200
#define AW2023_IMAX   0x3
#define AW2023_MIN_BRIGHTNESS_VALUE 130
#define AW2023_REPEAT_MAX_TIMES 15
#endif
