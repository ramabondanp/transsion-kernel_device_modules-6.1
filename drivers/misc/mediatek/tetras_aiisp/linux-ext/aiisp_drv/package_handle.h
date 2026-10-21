/*
 * (C) Copyright 2021-2022, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * Change Logs:
 * Date         Author          Notes
 * 2022-03-03   Yao Kun         Initialize.
 */

#ifndef __PACKAGE_HANDLE_H__
#define __PACKAGE_HANDLE_H__

#include <linux/types.h>
#include <linux/time.h>
#include <linux/version.h>

enum TUNING_TYPE {
    TUNING_REG = 0x1,
};

enum REG_UPD_TYPE {
    LSC_REG           = 0x1,
    COLOR_FUSION_REG  = 0x2,
    BLC_REG           = 0x3,
    SLIN0_REG         = 0x4,
    BPC_REG           = 0x5,
    PDAF_REG          = 0x6,
    WBC_REG           = 0x7,
    REMOSAIC_REG      = 0x8,
    RBIN0_REG         = 0x9,
    RBIN1_REG         = 0xA,
    AWBST0_REG        = 0xB,
    AEST0_REG         = 0xC,
    IMAGE_WR_REG      = 0xD,
    STATS_WR0_REG     = 0xE,
    RBIN2_REG         = 0xF,
    RBIN3_REG         = 0x10,
    AWBST1_REG        = 0x11,
    AEST1_REG         = 0x12,
    STATS_WR1_REG     = 0x13,
    SLIN1_REG         = 0x14,
    DPD_REG           = 0x15,
    STATS_WR2_REG     = 0x16,
    VBM0_REG          = 0x17,
    VBM1_REG          = 0x18,
    TOP_REG           = 0x19,
};

enum STR_TYPE {
    STR1           = 0x1,
    STR2           = 0x2,
    STR3           = 0x3,
};

enum MEM_TYPE {
    MEM           = 0x1,
};

enum EVENT_TYPE {
    CTRL          = 0x1,
    ERROR         = 0x2,
    INTR          = 0x3,
    SYNC          = 0x4,
};

#define SEND_EVENT_START 0x1000

struct aiisp_event_ctrl {
    uint32_t data;
};

struct aiisp_event_error {
    uint32_t data;
};

struct aiisp_event_intr {
    uint32_t data;
};

struct aiisp_event_sync {
    uint32_t data;
};

struct aiisp_event {
    uint32_t                type;
    union {
        struct aiisp_event_ctrl     ctrl;
        struct aiisp_event_error    error;
        struct aiisp_event_intr     intr;
        struct aiisp_event_sync     sync;
        uint8_t                     data[64];
    } u;
    uint32_t                pending;
    uint32_t                sequence;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
    struct timespec         timestamp;
#else
    struct timespec64       timestamp;
#endif
    uint32_t                id;
    uint32_t                reserved[8];
};

struct aiisp_pkg_hdr {
    uint32_t header;
    uint32_t version;
    uint32_t type;
    uint32_t size;
    uint32_t seq_num;
    uint32_t reserved[3];
};

struct aiisp_sub_pkg {
    uint32_t type;
    uint32_t size;
    uint32_t reserved[2];
};

struct pkg_info {
    uint32_t type;
    uint32_t sub_type;
    uint32_t size;
    uint32_t version;
    uint32_t seq_num;
    char *data;
};

#define PKG_HDR_SIZE         sizeof(struct aiisp_pkg_hdr)
#define SUB_PKG_HDR_SIZE     sizeof(struct aiisp_sub_pkg)
#define PKG_TAILER_SIZE      sizeof(uint32_t)
#define PKG_HDR_HEADER       0x1A1A1A1A
#define PKG_HDR_TAILER       0x5A5A5A5A
#define VERSION              0x0

#define ALIGN_UP(size, align)        (((size) + (align) - 1) & ~((align) - 1))

void *orgnize_send_data(struct pkg_info *info);
void *parse_recved_data(char *buf, uint64_t size, struct pkg_info *info);

#endif /* __PACKAGE_HANDLE_H__ */
