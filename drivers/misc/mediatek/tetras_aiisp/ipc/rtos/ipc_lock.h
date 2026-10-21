/*
 * (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * Change Logs:
 * Date           Author        Notes
 * 2022-01-21     Zhu Shiqiang  Initialize.
 */

/**
 * @brief   IPC Mutex Interface
 * @date    2022-01-21
 */

#ifndef __IPC_MUTEX_H__
#define __IPC_MUTEX_H__

#define ipc_mutex_init(lock)		mutex_init(lock)
#define ipc_mutex_lock(lock)		mutex_lock(lock)
#define ipc_mutex_trylock(lock)		mutex_trylock(lock)
#define ipc_mutex_unlock(lock)		mutex_unlock(lock)
#define ipc_mutex_is_locked(lock)	mutex_is_locked(lock)

#define ipc_sem_release(sem)		rt_sem_release(sem)
#define ipc_sem_take(sem)		rt_sem_take(sem)
#define ipc_sem_int(sem)		rt_sem_int(sem)
#define ipc_sem_trytake(sem)		rt_sem_trytake(sem)
#define ipc_sem_create(sem)		rt_sem_create(sem)
#define ipc_sem_control(sem)		rt_sem_control(sem)
#define ipc_sem_delete(sem)		rt_sem_delete(sem)

#endif /* __IPC_MUTEX_H__ */
