/*
 * (C) Copyright 2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Change Logs:
 * Date           Author         Notes
 * 2024-03-06     Martin         Initialize.
 */

/**
 * @brief
 * @date    2024-03-06
 */
#ifndef __IPC_ERRNO_H__
#define __IPC_ERRNO_H__

typedef int                     ipc_err_t;
#define SUCCESS			((ipc_err_t)0)		/* All is OK */
#define ERR_PERM		((ipc_err_t)1)		/* Operation not permitted */
#define ERR_NOENT		((ipc_err_t)2)		/* No such file or directory */
#define ERR_INTR		((ipc_err_t)4)		/* Interrupted system call */
#define ERR_IO			((ipc_err_t)5)		/* I/O error */
#define ERR_2BIG		((ipc_err_t)7)		/* Argument list too long */
#define ERR_AGAIN		((ipc_err_t)11)		/* Try again */
#define ERR_NOMEM		((ipc_err_t)12)		/* Out of memory */
#define ERR_ACCES		((ipc_err_t)13)		/* Permission denied */
#define ERR_FAULT		((ipc_err_t)14)		/* Bad address */
#define ERR_BUSY		((ipc_err_t)16)		/* Device or resource busy */
#define ERR_EXIST		((ipc_err_t)17)		/* File exists */
#define ERR_NODEV		((ipc_err_t)19)		/* No such device */
#define ERR_INVAL		((ipc_err_t)22)		/* Invalid argument */
#define ERR_RANGE		((ipc_err_t)34)		/* Math result not representable */
#define ERR_DEADLK		((ipc_err_t)35)		/* Resource deadlock would occur */
#define ERR_NOLCK		((ipc_err_t)37)		/* No record locks available */
#define ERR_NOSYS		((ipc_err_t)38)		/* Invalid system call number */
#define ERR_NOMSG		((ipc_err_t)42)		/* No message of desired type */
#define ERR_NODATA		((ipc_err_t)61)		/* No data available */
#define ERR_TIME		((ipc_err_t)62)		/* Timer expired */
#define ERR_OVERFLOW		((ipc_err_t)75)		/* Value too large for defined data type */
#define ERR_TIMEDOUT		((ipc_err_t)110)	/* Connection timed out */
#define ERR_FAIL		((ipc_err_t)255)	/* Common failed or unkown error type */

#endif /* __IPC_ERRNO_H__ */
