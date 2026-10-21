// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#ifndef __TC_GAUGE_H
#define __TC_GAUGE_H
#define DEFAULT_BATT_VOLTAGE	3999
#define DEFAULT_BATT_TEMP	25
#define DEFAULT_UISOC	50
#define DEFAULT_Q_MAX 4000

enum {
	GAUGE_NON_BATT_MODE = 0,
	GAUGE_SINGLE_MASTER_BATT_MODE,
	GAUGE_SINGLE_SLAVE_BATT_MODE,
	GAUGE_DUAL_BATT_MODE,
};

enum {
	GAUGE_DUAL_BATT_ONLINE = 0,
	GAUGE_SINGLE_MASTER_BATT_ONLINE,
	GAUGE_SINGLE_SLAVE_BATT_ONLINE,
	GAUGE_NONE_BATT_ONLINE,
};

static const char * const BATTERY_ONLINE_STATUS[] = {
	[GAUGE_DUAL_BATT_ONLINE]            = "dual_battery",
	[GAUGE_SINGLE_MASTER_BATT_ONLINE]   = "single_master",
	[GAUGE_SINGLE_SLAVE_BATT_ONLINE]    = "single_slave",
	[GAUGE_NONE_BATT_ONLINE]            = "none_batt",

};
#endif
