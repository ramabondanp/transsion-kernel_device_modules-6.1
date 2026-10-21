// SPDX-License-Identifier: GPL-2.0-only
/*
 * (C) Copyright 2021-2022, Shenzhen Tetras.AI Technology Co., Ltd
 *
 * Change Logs:
 * Date         Author          Notes
 * 2022-03-03   Yao Kun         Initialize.
 */

/**
 * @brief AI ISP Driver Header
 * @date  2022-03-03
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "package_handle.h"

void *orgnize_send_data(struct pkg_info *info)
{
    char *data = NULL, *temp = NULL;
    struct aiisp_pkg_hdr *pkg_hdr;
    struct aiisp_sub_pkg *sub_pkg_hdr;
    uint64_t total_size = info->size +
                          PKG_HDR_SIZE + SUB_PKG_HDR_SIZE + PKG_TAILER_SIZE;

    if (!info) {
        return NULL;
    }

    // info->size = ALIGN_UP(info->size, 4);
    total_size = info->size +
                 PKG_HDR_SIZE + SUB_PKG_HDR_SIZE + PKG_TAILER_SIZE;
    data = (char *)kzalloc(total_size, GFP_KERNEL);
    if (!data) {
        return NULL;
    }

    temp = data;
    pkg_hdr = (struct aiisp_pkg_hdr *)temp;
    pkg_hdr->header = PKG_HDR_HEADER;
    pkg_hdr->type = info->type;
    pkg_hdr->size = info->size + SUB_PKG_HDR_SIZE;
    pkg_hdr->seq_num = info->seq_num;
    pkg_hdr->version = info->version;
    temp += PKG_HDR_SIZE;

    sub_pkg_hdr = (struct aiisp_sub_pkg *)temp;
    sub_pkg_hdr->type = info->sub_type;
    sub_pkg_hdr->size = info->size;
    temp += SUB_PKG_HDR_SIZE;

    // memcpy(temp, buf, info->size);
    info->data = temp;
    temp += info->size;

    *((uint32_t *)temp) = PKG_HDR_TAILER;
    info->size = total_size;

    return data;
}

void *parse_recved_data(char *buf, uint64_t size, struct pkg_info *info)
{
    char *temp = NULL;
    struct aiisp_pkg_hdr *pkg_hdr;
    struct aiisp_sub_pkg *sub_pkg_hdr;

    /* Pointer check */
    if ((!buf) || (!size) || (!info)) {
        return NULL;
    }

    if (size <= PKG_HDR_SIZE + SUB_PKG_HDR_SIZE + PKG_TAILER_SIZE) {
        return NULL;
    }

    temp = buf;
    pkg_hdr = (struct aiisp_pkg_hdr *)temp;
    if (pkg_hdr->header != PKG_HDR_HEADER) {
        return NULL;
    }

    if (pkg_hdr->version != VERSION){
        return NULL;
    }

    info->type = pkg_hdr->type;
    if (size < pkg_hdr->size) {
        return NULL;
    }

    info->seq_num = pkg_hdr->seq_num;

    temp += PKG_HDR_SIZE;
    sub_pkg_hdr = (struct aiisp_sub_pkg *)temp;
    info->sub_type = sub_pkg_hdr->type;
    if (size < sub_pkg_hdr->size) {
        return NULL;
    }
    info->size = sub_pkg_hdr->size;

    temp += SUB_PKG_HDR_SIZE + sub_pkg_hdr->size;
    if (*((uint32_t *)temp) != PKG_HDR_TAILER) {
        return NULL;
    }

    info->data = buf + PKG_HDR_SIZE + SUB_PKG_HDR_SIZE ;

    return (buf + PKG_HDR_SIZE + SUB_PKG_HDR_SIZE);
}
