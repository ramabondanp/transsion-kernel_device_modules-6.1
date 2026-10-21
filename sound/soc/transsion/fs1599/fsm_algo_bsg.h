// SPDX-License-Identifier: GPL-2.0+
/**
 * Copyright (C) Shanghai FourSemi Ltd 2016-2025. All rights reserved.
 * 2025-03-27 File created.
 */

#ifndef __FSM_ALGO_BSG_H__
#define __FSM_ALGO_BSG_H__

#include "fsm-dev.h"

int fsm_algo_bsg_init(struct fsm_dev *fsm_dev);
int fsm_algo_bsg_monitor_switch(bool on);
void fsm_algo_bsg_deinit(struct fsm_dev *fsm_dev);

#endif // __FSM_ALGO_BSG_H__