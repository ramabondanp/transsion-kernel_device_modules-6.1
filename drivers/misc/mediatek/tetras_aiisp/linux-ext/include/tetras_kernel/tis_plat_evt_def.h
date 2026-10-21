/*
 * (C) Copyright 2023, Imvision Co., Ltd
 * This file is classified as confidential level C3 within Imvision
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * @brief   event definition
 * @date    2022-11-11
 */

#ifndef _TIS_PLAT_EVT_DEF_H__
#define _TIS_PLAT_EVT_DEF_H__

/* !!!NOTICE: event name must be TIS_EVT_xxx(module)_xxx(event) */

/* system events 1-200 */
#define TIS_EVT_SYS                             0
#define TIS_EVT_SYS_POWERON                     (TIS_EVT_SYS + 1)
#define TIS_EVT_SYS_POWEROFF                    (TIS_EVT_SYS + 2)
/* sending a proxy msg outside a tml task */
#define TIS_EVT_MSG_PROXY                       (TIS_EVT_SYS + 3)

#define TIS_EVT_SYS_FILE                        100
#define TIS_EVT_SYS_FILE_ADD                    (TIS_EVT_SYS_FILE + 1)
#define TIS_EVT_SYS_FILE_DEL                    (TIS_EVT_SYS_FILE + 2)
#define TIS_EVT_SYS_FILE_ACK                    (TIS_EVT_SYS_FILE + 3)
#define TIS_EVT_SYS_FILE_HANDSHAKE              (TIS_EVT_SYS_FILE + 4)
#define TIS_EVT_SYS_FILE_HANDSHAKE_ACK          (TIS_EVT_SYS_FILE + 5)
#define TIS_EVT_SYS_FILE_HANDSHAKE_TIMER        (TIS_EVT_SYS_FILE + 6)
#define TIS_EVT_SYS_FILE_MANAGER_START          (TIS_EVT_SYS_FILE + 7)
#define TIS_EVT_SYS_ADD_FILE_FAILED             (TIS_EVT_SYS_FILE + 8)
#define TIS_EVT_SYS_ADD_FILE_EXISTED            (TIS_EVT_SYS_FILE + 9)
#define TIS_EVT_SYS_DEL_ALL_MODEL_FILE          (TIS_EVT_SYS_FILE + 10)
#define TIS_EVT_SYS_DEL_ALL_FILE                (TIS_EVT_SYS_FILE + 11)
#define TIS_EVT_SYS_DEL_ALL_FILE_ACK            (TIS_EVT_SYS_FILE + 12)
#define TIS_EVT_SYS_LOAD_CONFIG_FILE            (TIS_EVT_SYS_FILE + 13)
#define TIS_EVT_SYS_LOAD_OTHER_FILE             (TIS_EVT_SYS_FILE + 14)

/* communication 201-300 */
#define TIS_EVT_COMM                            200
#define TIS_EVT_COMM_MALLOC_MEMORY              (TIS_EVT_COMM + 1)
#define TIS_EVT_COMM_MALLOC_ACK_MEMORY          (TIS_EVT_COMM + 2)
#define TIS_EVT_COMM_FREE_MEMORY                (TIS_EVT_COMM + 3)

/* vir_shell 301-350 */
#define TIS_EVT_VSHELL                          300
#define TIS_EVT_VSHELL_CONNECT                  (TIS_EVT_VSHELL + 1)
#define TIS_EVT_VSHELL_CMD                      (TIS_EVT_VSHELL + 2)
#define TIS_EVT_VSHELL_DISCONNECT               (TIS_EVT_VSHELL + 3)
#define TIS_EVT_VSHELL_OUTPUT                   (TIS_EVT_VSHELL + 4)
#define TIS_EVT_VSHELL_ENABLE                   (TIS_EVT_VSHELL + 5)
#define TIS_EVT_VSHELL_DISABLE                  (TIS_EVT_VSHELL + 6)
#define TIS_EVT_VSHELL_MANAGER_START            (TIS_EVT_VSHELL + 7)
#define TIS_EVT_VSHELL_MANAGER_HANDSKAE         (TIS_EVT_VSHELL + 8)
#define TIS_EVT_VSHELL_MANAGER_HANDSKAE_TIMER   (TIS_EVT_VSHELL + 9)
#define TIS_EVT_VSHELL_MANAGER_HANDSKAE_ACK     (TIS_EVT_VSHELL + 10)

/* chip_daemon 400-500 */
#define TIS_EVT_CHIP_DAEMON                     400

#define TIS_EVT_CHIP_DAEMON_START_CHIP          (TIS_EVT_CHIP_DAEMON + 1)
#define TIS_EVT_CHIP_DAEMON_GETINFO_CHIP        (TIS_EVT_CHIP_DAEMON + 2)
#define TIS_EVT_CHIP_DAEMON_CLOSE_CHIP          (TIS_EVT_CHIP_DAEMON + 3)
#define TIS_EVT_CHIP_DAEMON_RESET_CHIP          (TIS_EVT_CHIP_DAEMON + 4)
#define TIS_EVT_CHIP_DAEMON_INFAULT_CHIP        (TIS_EVT_CHIP_DAEMON + 5)

#define TIS_EVT_CHIP_DAEMON_INFO_REQ            (TIS_EVT_CHIP_DAEMON + 6)
#define TIS_EVT_CHIP_DAEMON_INFO_ACK            (TIS_EVT_CHIP_DAEMON + 7)

#define TIS_EVT_CHIP_DAEMON_SUSPEND_CHIP        (TIS_EVT_CHIP_DAEMON + 8)
#define TIS_EVT_CHIP_DAEMON_CAMHAL_START        (TIS_EVT_CHIP_DAEMON + 9)

/* framework timer 500-600 */
#define TIS_EVT_FRAMEWORK_TIMER_BASE            500
#define TIS_EVT_HEART_BEAT_TIMER                (TIS_EVT_FRAMEWORK_TIMER_BASE + 1)
#define TIS_EVT_DELAY_SLEEP_TIMER               (TIS_EVT_FRAMEWORK_TIMER_BASE + 2)
#define TIS_EVT_DISPLAY_ON                      (TIS_EVT_FRAMEWORK_TIMER_BASE + 3)
#define TIS_EVT_DISPLAY_OFF                     (TIS_EVT_FRAMEWORK_TIMER_BASE + 4)

/* 3A_service 601-700 */
#define TIS_EVT_3A_APP_BASE                     600
#define TIS_EVT_3A_SP                           (TIS_EVT_3A_APP_BASE + 1)
#define TIS_EVT_3A_AE                           (TIS_EVT_3A_APP_BASE + 2)
#define TIS_EVT_3A_AWB                          (TIS_EVT_3A_APP_BASE + 3)
#define TIS_EVT_3A_AF                           (TIS_EVT_3A_APP_BASE + 4)
#define TIS_EVT_3A_OP_AE                        (TIS_EVT_3A_APP_BASE + 5)
#define TIS_EVT_3A_OP_AWB                       (TIS_EVT_3A_APP_BASE + 6)
#define TIS_EVT_3A_OP_AF                        (TIS_EVT_3A_APP_BASE + 7)
#define TIS_EVT_3A_DATA                         (TIS_EVT_3A_APP_BASE + 8)
#define TIS_EVT_3A_CREATE                       (TIS_EVT_3A_APP_BASE + 9)
#define TIS_EVT_3A_DESTROY                      (TIS_EVT_3A_APP_BASE + 10)
#define TIS_EVT_3A_START                        (TIS_EVT_3A_APP_BASE + 11)
#define TIS_EVT_3A_STOP                         (TIS_EVT_3A_APP_BASE + 12)

/* EXEC_SHELL_CMD 701-800 */
#define TIS_EVT_EXEC_SHELL_CMD               700
#define TIS_EVT_EXEC_SHELL_CMD_REQ           (TIS_EVT_EXEC_SHELL_CMD + 1)
#define TIS_EVT_EXEC_SHELL_CMD_START         (TIS_EVT_EXEC_SHELL_CMD + 2)
#define TIS_EVT_EXEC_SHELL_CMD_STOP          (TIS_EVT_EXEC_SHELL_CMD + 3)
#define TIS_EVT_EXEC_SHELL_CMD_LOG           (TIS_EVT_EXEC_SHELL_CMD + 4)

/* camera 2000-2200 */
/* RTOS Camera Engine command 00-19 */
#define TIS_EVT_CAMERA                          2000
#define TIS_EVT_CAMERA_DEVICE_OPEN              (TIS_EVT_CAMERA + 1)
#define TIS_EVT_CAMERA_DEVICE_CLOSE             (TIS_EVT_CAMERA + 2)
#define TIS_EVT_CAMERA_DEVICE_CREATE_STREAM     (TIS_EVT_CAMERA + 3)
#define TIS_EVT_CAMERA_DEVICE_DESTROY_STREAM    (TIS_EVT_CAMERA + 4)
#define TIS_EVT_CAMERA_DEVICE_SET_FILE          (TIS_EVT_CAMERA + 5)
#define TIS_EVT_CAMERA_DEVICE_ALLOC_BUFF        (TIS_EVT_CAMERA + 6)
#define TIS_EVT_CAMERA_DEVICE_FREE_BUFF         (TIS_EVT_CAMERA + 7)
#define TIS_EVT_CAMERA_DEVICE_LOAD_EEPROM       (TIS_EVT_CAMERA + 8)
#define TIS_EVT_CAMERA_DEVICE_RESET             (TIS_EVT_CAMERA + 9)

/* RTOS Camera Engine command 20-39 */
#define TIS_EVT_CAMERA_STREAM_CONFIG            (TIS_EVT_CAMERA + 20)
#define TIS_EVT_CAMERA_STREAM_START             (TIS_EVT_CAMERA + 21)
#define TIS_EVT_CAMERA_STREAM_STOP              (TIS_EVT_CAMERA + 22)
#define TIS_EVT_CAMERA_STREAM_SWTRIG            (TIS_EVT_CAMERA + 23)
#define TIS_EVT_CAMERA_STREAM_RECOVERY          (TIS_EVT_CAMERA + 24)
#define TIS_EVT_CAMERA_STREAM_UPDATE_IMU        (TIS_EVT_CAMERA + 25)
#define TIS_EVT_CAMERA_STREAM_MAX               (TIS_EVT_CAMERA + 39)

/* Chip daemon command 40- */
#define TIS_EVT_CAMERA_DEVICE_QUERRY_STATUS     (TIS_EVT_CAMERA + 40)
#define TIS_EVT_CAMERA_DEVICE_DEV_ACTIVE        (TIS_EVT_CAMERA + 41)
#define TIS_EVT_CAMERA_DEVICE_DEV_DEACTIVE      (TIS_EVT_CAMERA + 42)

#define TIS_EVT_CAMERA_RESERVE                  (TIS_EVT_CAMERA + 150)
#define TIS_EVT_CAMERA_START                    (TIS_EVT_CAMERA + 151)
#define TIS_EVT_CAMERA_STOP                     (TIS_EVT_CAMERA + 152)
#define TIS_EVT_CAMERA_INVALID                  (TIS_EVT_CAMERA + 199)
/*
 * 1: if contains sensor name, load aiisp_tuning.bin and sensor name's model tlf
 * 2: if args is NULL, load aiisp_tuning.bin and all model tlf
 */
#define TIS_EVT_CAMERA_LOAD_FILE                (TIS_EVT_CAMERA + 200)
/* only load aiisp_config.bin */
#define TIS_EVT_CAMERA_LOAD_AIISP_CONFIG_FILE   (TIS_EVT_CAMERA + 201)
/* check model load status, true or false */
#define TIS_EVT_CAMERA_LOAD_MODEL_STATUS        (TIS_EVT_CAMERA + 202)

/*
 * camera cmd, the original command header info
 * todo : the cmd will be delete when the new cmd finish and test sucess
 */
#define TIS_EVT_CAMERA_CMD_MSG                  4096

#endif /* _TIS_PLAT_EVT_DEF_H__ */
