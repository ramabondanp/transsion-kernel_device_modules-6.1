/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef __MTK_IRTX_PWM_H__
#define __MTK_IRTX_PWM_H__

/* PMIC LDO helpers provided by the lcm/camera module */
#if defined(CONFIG_IR_LED_EXTERNAL_LDO)
int lcm_cam_single_ldo_control(int pinidx, unsigned int voltage);
int lcm_cam_single_ldo_disable(int pinidx);
#endif

#endif /* __MTK_IRTX_PWM_H__ */
