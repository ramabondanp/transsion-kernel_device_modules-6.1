// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#ifndef __PMIC_VOTER_H
#define __PMIC_VOTER_H

#include <linux/mutex.h>

#define DEBUG_FORCE_CLIENT	"DEBUG_FORCE_CLIENT"
#define CHG_THREAD_VOTER	"CHG_THREAD_VOTER"
#define PD_CHG_VOTER		"PD_CHG_VOTER"
#define TOTAL_SET_VOTER	        "TOTAL_SET_VOTER"
#define PE50_VOTER		"PE50_VOTER"
#define TC30_VOTER		"TC30_VOTER"
#define PE20_VOTER		"PE20_VOTER"
#define HVDCP20_VOTER		"HVDCP20_VOTER"
#define HVDCP30_VOTER		"HVDCP30_VOTER"
#define PDC_VOTER		"PDC_VOTER"
#define WIRELESS_VOTER          "WIRELESS_VOTER"
#define PID_VOTER               "PID_VOTER"
#define AICHG_VOTER          "AICHG_VOTER"
#define SMTCHG_VOTER          "SMTCHG_VOTER"
#define BYPASS_CHG_VOTER          "BYPASS_CHG_VOTER"



struct votable;

enum votable_type {
	VOTE_MIN,
	VOTE_MAX,
	VOTE_SET_ANY,
	NUM_VOTABLE_TYPES,
};

extern bool is_client_vote_enabled(struct votable *votable, const char *client_str);
extern bool is_client_vote_enabled_locked(struct votable *votable,
 							const char *client_str);
extern bool is_override_vote_enabled(struct votable *votable);
extern bool is_override_vote_enabled_locked(struct votable *votable);
extern int get_client_vote(struct votable *votable, const char *client_str);
extern int get_client_vote_locked(struct votable *votable, const char *client_str);
extern int get_effective_result_locked(struct votable *votable);
extern int get_effective_result(struct votable *votable);
extern const char *get_effective_client(struct votable *votable);
extern const char *get_effective_client_locked(struct votable *votable);
extern int vote(struct votable *votable, const char *client_str, bool state, int val);
extern int force_active_get(void *data, u64 *val);
extern int force_active_set(void *data, u64 val);
extern int vote_override(struct votable *votable, const char *override_client,
 		  bool state, int val);
extern int rerun_election(struct votable *votable);
extern struct votable *find_votable(const char *name);
extern struct votable *create_votable(const char *name,
				int votable_type,
				int (*callback)(struct votable *votable,
						void *data,
						int effective_result,
						const char *effective_client),
				void *data);
void destroy_votable(struct votable *votable);
void lock_votable(struct votable *votable);
void unlock_votable(struct votable *votable);

#endif /* __PMIC_VOTER_H */
