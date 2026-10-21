// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#ifndef __AW210XX_REG_CFG_H__
#define __AW210XX_REG_CFG_H__
#include "aw_lamp_interface.h"






/*********************************************************
 *
 * chip info
 *
 ********************************************************/


#if 1

static const int aw210xx_reg_map[] = {

	0x48, 0x23, //1
	0x49, 0x24, //2
	0x4a, 0x25, //3
	0x4b, 0x26, //4
	0x4c, 0x27, //5
	0x4d, 0x28, //6
	0x46, 0x21, //7
	0x47, 0x22, //8
};
#else
static const int aw210xx_reg_map[] = {
	0x4b, 0x26, //6
	0x4c, 0x27, //7
	0x4d, 0x28, //8
	0x49, 0x24, //4
	0x48, 0x23, //3
	0x46, 0x21, //1
	0x47, 0x22, //2
	0x4a, 0x25, //5
};
#endif

/*********************************************************
 *
 * effect data
 *
 ********************************************************/
/* breath */
#if 0
static  AW_COLOR_STRUCT rgb_color_list[] = {
	{  0,   0, 255},
	{255,   0,   0},
	{  0, 255,   0},
};
static  AW_MULTI_BREATH_DATA_STRUCT aw210xx_rgb_data[] = {
	{{0, 500, 50, 500,0, 50}, 12, 255, 0, sizeof(rgb_color_list)/sizeof(AW_COLOR_STRUCT), rgb_color_list},
	{{0, 500, 50, 500,0, 50}, 12, 255, 0, sizeof(rgb_color_list)/sizeof(AW_COLOR_STRUCT), rgb_color_list},
	{{0, 500, 50, 500,0, 50}, 12, 255, 0, sizeof(rgb_color_list)/sizeof(AW_COLOR_STRUCT), rgb_color_list},
	{{0, 500, 50, 500,0, 50}, 12, 255, 0, sizeof(rgb_color_list)/sizeof(AW_COLOR_STRUCT), rgb_color_list},
	{{0, 500, 50, 500,0, 50}, 12, 255, 0, sizeof(rgb_color_list)/sizeof(AW_COLOR_STRUCT), rgb_color_list},
	{{0, 500, 50, 500,0, 50}, 12, 255, 0, sizeof(rgb_color_list)/sizeof(AW_COLOR_STRUCT), rgb_color_list},
};




/* horse race lamp */

static  AW_COLOR_STRUCT rgb_color_list2[] = {
	{  255,  255, 255},
};
static  AW_MULTI_BREATH_DATA_STRUCT aw210xx_rgb_data2[] = {
	{{  0, 120, 360, 120, 120,300}, 10, 120, 0, sizeof(rgb_color_list2)/sizeof(AW_COLOR_STRUCT), rgb_color_list2},
	{{ 60, 120, 360, 120, 120,240}, 10, 120, 0, sizeof(rgb_color_list2)/sizeof(AW_COLOR_STRUCT), rgb_color_list2},
	{{120, 120, 360, 120, 120,180}, 10, 120, 0, sizeof(rgb_color_list2)/sizeof(AW_COLOR_STRUCT), rgb_color_list2},
	{{180, 120, 360, 120, 120,120}, 10, 120, 0, sizeof(rgb_color_list2)/sizeof(AW_COLOR_STRUCT), rgb_color_list2},
	{{240, 120, 360, 120, 120, 60}, 10, 120, 0, sizeof(rgb_color_list2)/sizeof(AW_COLOR_STRUCT), rgb_color_list2},
	{{300, 120, 360, 120, 120, 0 }, 10, 120, 0, sizeof(rgb_color_list2)/sizeof(AW_COLOR_STRUCT), rgb_color_list2},
};		


static  AW_COLOR_STRUCT rgb_color_list3[] = {
	{  255,  0, 0},
	{  255,  255, 0},
	{  0,  255, 0},
	{  0,  255, 255},
	{  0,  0, 255},
	{  255,  0, 255},
	{  255,  255, 255},
	{  255,  128, 128},
};
static  AW_MULTI_BREATH_DATA_STRUCT aw210xx_rgb_data3[] = {
	{{0, 0, 500, 0, 0,50}, 16, 120, 0, sizeof(rgb_color_list3)/sizeof(AW_COLOR_STRUCT), rgb_color_list3},
	{{0, 0, 500, 0, 0,50}, 16, 120, 0, sizeof(rgb_color_list3)/sizeof(AW_COLOR_STRUCT), rgb_color_list3},
	{{0, 0, 500, 0, 0,50}, 16, 120, 0, sizeof(rgb_color_list3)/sizeof(AW_COLOR_STRUCT), rgb_color_list3},
	{{0, 0, 500, 0, 0,50}, 16, 120, 0, sizeof(rgb_color_list3)/sizeof(AW_COLOR_STRUCT), rgb_color_list3},
	{{0, 0, 500, 0, 0,50}, 16, 120, 0, sizeof(rgb_color_list3)/sizeof(AW_COLOR_STRUCT), rgb_color_list3},
	{{0, 0, 500, 0, 0,50}, 16, 120, 0, sizeof(rgb_color_list3)/sizeof(AW_COLOR_STRUCT), rgb_color_list3},
};
#endif
#if 1
static  unsigned char rgb_color_list[] = {
	255
};

/* horse race lamp */

static  AW_MULTI_BREATH_DATA_STRUCT transsion_gamesinglekill_data[] = {
	{{400,  0, 320, 0,  0, 400}, 1, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{780,  0, 320, 0,  0, 20},  1, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{780,  0, 320, 0,  0, 20},  1, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{20,   0, 320, 0,  0, 780}, 1, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{400,  0, 320, 0,  0, 400}, 1, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{780,  0, 320, 0,  0, 20},  1, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{780,  0, 320, 0,  0, 20},  1, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{20,   0, 320, 0,  0, 780}, 1, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_gamedoublekill_data[] = {
	{{20,  0, 0, 0,0, 20}, 5, 255, 0,8, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{320, 0, 0, 0,0, 20}, 5, 255, 0,8, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{20,  0, 0, 0,0, 20}, 5, 255, 0,8, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{320, 0, 0, 0,0, 20}, 5, 255, 0,8, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{20,  0, 0, 0,0, 20}, 5, 255, 0,8, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{320, 0, 0, 0,0, 20}, 5, 255, 0,8, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{20,  0, 0, 0,0, 20}, 5, 255, 0,8, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{320, 0, 0, 0,0, 20}, 5, 255, 0,8, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_photo_3s_data[] = {
	{{0,   220,   0, 220,0, 0}, 4, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   220,   0, 220,0, 0}, 4, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   220,   0, 220,0, 0}, 4, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   220,   0, 220,0, 0}, 4, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   220,   0, 220,0, 0}, 4, 255, 0,0,sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   220,   0, 220,0, 0}, 4, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   220,   0, 220,0, 0}, 4, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   220,   0, 220,0, 0}, 4, 255, 0,0,sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},

	{{0,   80,   0, 80,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   80,   0, 80,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   80,   0, 80,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   80,   0, 80,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   80,   0, 80,0, 0}, 5, 255, 0,0,sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   80,   0, 80,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   80,   0, 80,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   80,   0, 80,0, 0}, 5, 255, 0,0,sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_photo_5s_data[] = {
	{{0,   420,   0, 420,0, 0}, 2, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   420,   0, 420,0, 0}, 2, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   420,   0, 420,0, 0}, 2, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   420,   0, 420,0, 0}, 2, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   420,   0, 420,0, 0}, 2, 255, 0,0,sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   420,   0, 420,0, 0}, 2, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   420,   0, 420,0, 0}, 2, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   420,   0, 420,0, 0}, 2, 255, 0,0,sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	
	{{0,   200,   0, 180,0, 0}, 4, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   200,   0, 180,0, 0}, 4, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   200,   0, 180,0, 0}, 4, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   200,   0, 180,0, 0}, 4, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   200,   0, 180,0, 0}, 4, 255, 0,0,sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   200,   0, 180,0, 0}, 4, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   200,   0, 180,0, 0}, 4, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   200,   0, 180,0, 0}, 4, 255, 0,0,sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	
	{{0,   60,   0, 60,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   60,   0, 60,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   60,   0, 60,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   60,   0, 60,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   60,   0, 60,0, 0}, 5, 255, 0,0,sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   60,   0, 60,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   60,   0, 60,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   60,   0, 60,0, 0}, 5, 255, 0,0,sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};


 static  AW_MULTI_BREATH_DATA_STRUCT transsion_photo_10s_data[] = {
	{{0,   440,   0, 420,0, 0}, 7, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   440,   0, 420,0, 0}, 7, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   440,   0, 420,0, 0}, 7, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   440,   0, 420,0, 0}, 7, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   440,   0, 420,0, 0}, 7, 255, 0,0,sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   440,   0, 420,0, 0}, 7, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   440,   0, 420,0, 0}, 7, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   440,   0, 420,0, 0}, 7, 255, 0,0,sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	
	{{0,   200,   0, 200,0, 0}, 4, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   200,   0, 200,0, 0}, 4, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   200,   0, 200,0, 0}, 4, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   200,   0, 200,0, 0}, 4, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   200,   0, 200,0, 0}, 4, 255, 0,0,sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   200,   0, 200,0, 0}, 4, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   200,   0, 200,0, 0}, 4, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   200,   0, 200,0, 0}, 4, 255, 0,0,sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	
	{{0,   60,   0, 60,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   60,   0, 60,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   60,   0, 60,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   60,   0, 60,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   60,   0, 60,0, 0}, 5, 255, 0,0,sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   60,   0, 60,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   60,   0, 60,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,   60,   0, 60,0, 0}, 5, 255, 0,0,sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};


 static  AW_MULTI_BREATH_DATA_STRUCT transsion_photo_gamefirstblood_data[] = {

	{{60,    140,   300, 140,0, 80}, 1, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,     140,   300, 140,0, 20},  1, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,     140,   300, 140,0, 20}, 1, 255, 0, 0,sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{60,    140,   300, 140,0, 80},  1, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{120,   140,   300, 140,0, 140}, 1, 255, 0, 0,sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{180,   140,   300, 140,0, 200},  1, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{180,   140,   300, 140,0, 200}, 1, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{120,   140,   300, 140,0, 140},  1, 255, 0, 0,sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},

 };

static  AW_MULTI_BREATH_DATA_STRUCT transsion_charge1_data[] = {
	{{20,    140,   860, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{120,  140,   760, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{220,  140,   660, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{320,  140,   560, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{420,  140,   460, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{520,  140,   360, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{620,  140,   260, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{720,  140,   160, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_charge2_data[] = {
	{{20,    140,   860, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{120,  140,   760, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{220,  140,   660, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{320,  140,   560, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{420,  140,   460, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{520,  140,   360, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{620,  140,   260, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{720,  140,   160, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_charge3_data[] = {
	{{20,    140,   860, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{120,  140,   760, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{220,  140,   660, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{320,  140,   560, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{420,  140,   460, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{520,  140,   360, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{620,  140,   260, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{720,  140,   160, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_charge4_data[] = {
	{{20,    140,   860, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{120,  140,   760, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{220,  140,   660, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{320,  140,   560, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{420,  140,   460, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{520,  140,   360, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{620,  140,   260, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{720,  140,   160, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_charge5_data[] = {
	{{20,    140,   860, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{120,  140,   760, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{220,  140,   660, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{320,  140,   560, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{420,  140,   460, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{520,  140,   360, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{620,  140,   260, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{720,  140,   160, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_charge6_data[] = {
	{{20,    140,   860, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{120,  140,   760, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{220,  140,   660, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{320,  140,   560, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{420,  140,   460, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{520,  140,   360, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{620,  140,   260, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{720,  140,   160, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_charge7_data[] = {
	{{20,    140,   860, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{120,  140,   760, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{220,  140,   660, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{320,  140,   560, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{420,  140,   460, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{520,  140,   360, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{620,  140,   260, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{720,  140,   160, 500,0, 0}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_charge8_data[] = {
	{{20,    140,   860, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{120,  140,   760, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{220,  140,   660, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 	{{320,  140,   560, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{420,  140,   460, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{520,  140,   360, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{620,  140,   260, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{720,  140,   160, 500,0, 260}, 3, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_charge_full_data[] = {
	{{0,    140,   860, 500,0, 260}, 1, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{100,  140,   760, 500,0, 260}, 1, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{200,  140,   660, 500,0, 260}, 1, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{300,  140,   560, 500,0, 260}, 1, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{400,  140,   460, 500,0, 260}, 1, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{500,  140,   360, 500,0, 260}, 1, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{600,  140,   260, 500,0, 260}, 1, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{700,  140,   160, 500,0, 260}, 1, 255, 0,1, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_charge_setting_data[] = {
	{{500,   100,   200, 100,400, 20}, 6, 255, 20,12, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{600,   100,   200, 100,400, 20}, 6, 255, 20,12, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{700,   100,   200, 100,400, 20}, 6, 255, 20,12, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{800,   100,   200, 100,400, 20}, 6, 255, 20,12, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{900,   100,   200, 100,400, 20}, 6, 255, 20,12, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{1000,  100,   200, 100,400, 20}, 6, 255, 20,12, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{1100,  100,   200, 100,400, 20}, 6, 255, 20,12, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{1200,  100,   200, 100,400, 20}, 6, 255, 20,12, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
 };

static  AW_MULTI_BREATH_DATA_STRUCT transsion_call_data[] = {
	{{20,   160,  160, 160,160, 20}, 0, 255, 0,5, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{100,  160,  160, 160,160, 20}, 0, 255, 0,5, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{180,  160,  160, 160,160, 20}, 0, 255, 0,5, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{260,  160,  160, 160,160, 20}, 0, 255, 0,5, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{340,  160,  160, 160,160, 20}, 0, 255, 0,5, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{420,  160,  160, 160,160, 20}, 0, 255, 0,5, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{500,  160,  160, 160,160, 20}, 0, 255, 0,5, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{580,  160,  160, 160,160, 20}, 0, 255, 0,5, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_call_preview_data[] = {
	{{20,   160,  160, 160,160, 20}, 7, 255, 0,5, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{100,  160,  160, 160,160, 20}, 7, 255, 0,5, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{180,  160,  160, 160,160, 20}, 7, 255, 0,5, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{260,  160,  160, 160,160, 20}, 7, 255, 0,5, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{340,  160,  160, 160,160, 20}, 7, 255, 0,5, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{420,  160,  160, 160,160, 20}, 7, 255, 0,5, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{500,  160,  160, 160,160, 20}, 7, 255, 0,5, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{580,  160,  160, 160,160, 20}, 7, 255, 0,5, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_record_data[] = {
	{{20,   120,  240, 120,480, 20}, 0, 255, 20,9, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{140,  120,  240, 120,480, 20}, 0, 255, 20,9, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{260,  120,  240, 120,480, 20}, 0, 255, 20,9, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{380,  120,  240, 120,480, 20}, 0, 255, 20,9, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{500,  120,  240, 120,480, 20}, 0, 255, 20,9, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{620,  120,  240, 120,480, 20}, 0, 255, 20,9, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{740,  120,  240, 120,480, 20}, 0, 255, 20,9, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{860,  120,  240, 120,480, 20}, 0, 255, 20,9, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_notice_data[] = {
	{{0,  200,  0, 160,340, 400}, 1, 255, 0,2, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  200,  0, 160,340, 400}, 1, 255, 0,2, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  200,  0, 160,340, 400}, 1, 255, 0,2, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  200,  0, 160,340, 400}, 1, 255, 0,2, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  200,  0, 160,340, 400}, 1, 255, 0,2, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  200,  0, 160,340, 400}, 1, 255, 0,2, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  200,  0, 160,340, 400}, 1, 255, 0,2, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  200,  0, 160,340, 400}, 1, 255, 0,2, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_awake_data[] = {
	{{240,  160,   0, 160,0, 480}, 240, 255, 0,3, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{160,  160,   0, 160,80, 480},240, 255, 0,3, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{80,   160,   0, 160,160, 480},240, 255, 0,3, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,    160,   0, 160,240, 480},240, 255, 0,3, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,    160,   0, 160,240, 480},240, 255, 0,3, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{80,   160,   0, 160,160, 480}, 240, 255, 0,3, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{160,  160,   0, 160,80, 480}, 240, 255, 0,3, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{240,  160,   0, 160,0, 480}, 240, 255, 0,3, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_analysys_data[] = {
	{{20,   120,   240, 120,480, 20}, 0, 255, 20,11, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{140,  120,   240, 120,480, 20}, 0, 255, 20,11, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{260,  120,   240, 120,480, 20}, 0, 255, 20,11, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{380,  120,   240, 120,480, 20}, 0, 255, 20,11, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{500,  120,   240, 120,480, 20}, 0, 255, 20,11, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{620,  120,   240, 120,480, 20}, 0, 255, 20,11, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{740,  120,   240, 120,480, 20}, 0, 255, 20,11, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{860,  120,   240, 120,480, 20}, 0, 255, 20,11, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_answer_data[] = {
	{{0,  760,  0, 760,0, 20}, 0, 255, 20,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  760,  0, 760,0, 20}, 0, 255, 20,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  760,  0, 760,0, 20}, 0, 255, 20,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  760,  0, 760,0, 20}, 0, 255, 20,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  760,  0, 760,0, 20}, 0, 255, 20,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  760,  0, 760,0, 20}, 0, 255, 20,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  760,  0, 760,0, 20}, 0, 255, 20,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  760,  0, 760,0, 20}, 0, 255, 20,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};


static  AW_MULTI_BREATH_DATA_STRUCT transsion_voice_data[] = {
	{{240,  160,   0, 160,0, 480}, 4, 255, 0,6, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{160,  160,   0, 160,80, 480}, 4, 255, 0,6, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{80,   160,   0, 160,160, 480}, 4, 255, 0,6, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,    160,   0, 160,240, 480}, 4, 255, 0,6, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,    160,   0, 160,240, 480}, 4, 255, 0,6, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{80,   160,   0, 160,160, 480}, 4, 255, 0,6, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{160,  160,   0, 160,80, 480}, 4, 255, 0,6, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{240,  160,   0, 160,0, 480}, 4, 255, 0,6, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{20,   100,   200, 100,400, 20}, 6, 255, 20,7, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{120,  100,   200, 100,400, 20}, 6, 255, 20,7, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{220,  100,   200, 100,400, 20}, 6, 255, 20,7, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{320,  100,   200, 100,400, 20}, 6, 255, 20,7, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{420,  100,   200, 100,400, 20}, 6, 255, 20,7, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{520,  100,   200, 100,400, 20}, 6, 255, 20,7, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{620,  100,   200, 100,400, 20}, 6, 255, 20,7, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{720,  100,   200, 100,400, 20}, 6, 255, 20,7, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  640,   0, 640,0, 0}, 3, 255, 20,10, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  640,   0, 640,0, 0}, 3, 255, 20,10, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  640,   0, 640,0, 0}, 3, 255, 20,10, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  640,   0, 640,0, 0}, 3, 255, 20,10, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  640,   0, 640,0, 0}, 3, 255, 20,10, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  640,   0, 640,0, 0}, 3, 255, 20,10, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  640,   0, 640,0, 0}, 3, 255, 20,10, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0,  640,   0, 640,0, 0}, 3, 255, 20,10, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};



static  AW_MULTI_BREATH_DATA_STRUCT transsion_gamestart_data[] = {
	{{460,  0,   0, 0,0, 0}, 5, 255, 0,4, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{960,  0,   0, 0,0, 0}, 5, 255, 0,4, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{960,  0,   0, 0,0, 0}, 5, 255, 0,4, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{20,  0,   0, 0,0, 0}, 5, 255, 0,4, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{460,  0,   0, 0,0, 0}, 5, 255, 0,4, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{960,  0,   0, 0,0, 0}, 5, 255, 0,4, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{960,  0,   0, 0,0, 0}, 5, 255, 0,4, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{20,  0,   0, 0,0, 0}, 5, 255, 0,4, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_all_on_data[] = {
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_all_off_data[] = {
	{{0, 0, 0, 0,0, 100}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 100}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 100}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 100}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 100}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 100}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 100}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 100}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_on_1_data[] = {
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_on_2_data[] = {
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_on_3_data[] = {
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_on_4_data[] = {
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_on_5_data[] = {
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_on_6_data[] = {
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_on_7_data[] = {
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 0, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static  AW_MULTI_BREATH_DATA_STRUCT transsion_on_8_data[] = {
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
	{{0, 0, 100, 0,0, 0}, 5, 255, 0,0, sizeof(rgb_color_list)/sizeof(unsigned char), rgb_color_list},
};

static unsigned int  time8[] ={
	0,400,0,820,
	0,0,0,820,
	0,0,0,820,
	0,0,0,880,
	460,0,600,0,0,
};



static unsigned int  time1[] ={
	
	0,460,0,720,
	0,0,0,720,
	0,0,0,720,
	0,0,0,680,
	460,0,600,0,0,
};

static unsigned int  time2[] ={
	
	0,120,0,400,
	0,120,0,260,
	0,100,0,260,
	0,120,0,420,
	480,980,600,0,0,
};
static unsigned int  time3[] ={

	0,120,0,100,
	0,120,0,700,
	0,120,0,400,
	0,120,0,620,
	480,480,600,0,0,
	};

static unsigned int  time4[] ={

	0,400,0,820,
	0,0,0,820,
	0,0,0,820,
	0,0,0,880,
	460,0,600,0,0,
};

static unsigned int  time5[] ={

	0,460,0,720,
	0,0,0,720,
	0,0,0,720,
	0,0,0,680,
	460,0,600,0,0,
};

static unsigned int  time6[] ={
	
	0,120,0,260,
	0,100,0,260,
	0,120,0,260,
	0,100,0,580,
	480,980,600,0,0,
};

static unsigned int  time7[] ={
	
	0,260,0,420,
	0,100,0,720,
	0,100,0,780,
	480,400,0,0,
	0,0,600,0,0,
};




static uint32_t game_double_time1[] ={
	0,260,0,260,
	0,260,0,260,
	0,260,0,260,
	0,260,0,160,
	0,0,0,160,0,

};

static uint32_t game_double_time2[] ={
	0,260,0,260,
	0,260,0,260,
	0,260,0,260,
	0,140,0,0,
	0,0,140,0,0,
};

static uint32_t game_double_time3[] ={
	0,260,0,260,
	0,260,0,260,
	0,260,0,260,
	0,260,0,160,
	0,0,0,160,0,
};

static uint32_t game_double_time4[] ={
	0,260,0,260,
	0,260,0,260,
	0,260,0,260,
	0,140,0,0,
	0,0,140,0,0,
};

static uint32_t game_double_time5[] ={
	0,260,0,260,
	0,260,0,260,
	0,260,0,260,
	0,260,0,160,
	0,0,0,160,0,
};

static uint32_t game_double_time6[] ={
	0,260,0,260,
	0,260,0,260,
	0,260,0,260,
	0,140,0,0,
	0,0,140,0,0,
};

static uint32_t game_double_time7[] ={
	0,260,0,260,
	0,260,0,260,
	0,260,0,260,
	0,260,0,160,
	0,0,0,160,0,
};

static uint32_t game_double_time8[] ={
	0,260,0,260,
	0,260,0,260,
	0,260,0,260,
	0,140,0,0,
	0,0,140,0,0,
};

#endif
#endif
