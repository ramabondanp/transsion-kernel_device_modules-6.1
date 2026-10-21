// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#ifndef __AW_BREATH_ALGORITHM_H__
#define __AW_BREATH_ALGORITHM_H__


#include <linux/types.h>
#include <linux/string.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
typedef enum{
	BREATH_ALGO_NONE,
	BREATH_ALGO_GAMMA_CORRECTION,
	BREATH_ALGO_LINEAR_CORRECTION,
	BREATH_ALGO_MAX,
} BREATH_ALGO_ID;

typedef struct{
	unsigned short total_frames;
	unsigned short cur_frame;
	unsigned short data_start;
	unsigned short data_end;
} ALGO_DATA_STRUCT;

typedef unsigned char (*GetBrightnessFuncPtr)(ALGO_DATA_STRUCT *);
extern
GetBrightnessFuncPtr aw_get_breath_brightness_algo_func(BREATH_ALGO_ID algo_id);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* __AW_BREATH_ALGORITHM_H__ */
