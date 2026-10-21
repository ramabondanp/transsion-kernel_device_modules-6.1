// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#ifndef __TRAN_WIRELESS_CHARGER_H__
#define __TRAN_WIRELESS_CHARGER_H__

#define WIRELESS_INIT_CHARGER_CURRENT           1000000
#define WIRELESS_INIT_INPUT_CURRENT             500000
#define STANDARD_BPP_POWER 5

int __wireless_select_current_limit(struct wireless_manager *info);
int __wireless_charger_plug_out_reset(struct wireless_manager *info);

#endif
