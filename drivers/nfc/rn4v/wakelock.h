/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SAMSUNG NFC Controller
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Copyright (C) 2026 Transsion Inc.
 */

#ifndef _LINUX_WAKELOCK_NFC_H
#define _LINUX_WAKELOCK_NFC_H

#include <linux/ktime.h>
#include <linux/device.h>

/* A wake_lock prevents the system from entering suspend or other low power
 * states when active. If the type is set to WAKE_LOCK_SUSPEND, the wake_lock
 * prevents a full system suspend.
 */

enum {
	WAKE_LOCK_SUSPEND, /* Prevent suspend */
	WAKE_LOCK_TYPE_COUNT
};

struct wake_lock {
	struct wakeup_source *ws;
	struct device *dev;
};

static inline void wake_lock_init(struct wake_lock *lock, int type,
				  const char *name)
{
	lock->ws = wakeup_source_register(lock->dev, name);
}

static inline void wake_lock_destroy(struct wake_lock *lock)
{
	wakeup_source_unregister(lock->ws);
}

static inline void wake_lock(struct wake_lock *lock)
{
	__pm_stay_awake(lock->ws);
}

static inline void wake_lock_timeout(struct wake_lock *lock, long timeout)
{
	__pm_wakeup_event(lock->ws, jiffies_to_msecs(timeout));
}

static inline void wake_unlock(struct wake_lock *lock)
{
	__pm_relax(lock->ws);
}

static inline int wake_lock_active(struct wake_lock *lock)
{
	return lock->ws->active;
}

#endif
