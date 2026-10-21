/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C3 within Tetras.AI
 *
 * Change Logs:
 * Date           Author         Notes
 * 2023-7-25      Suntonce       inited
 */

/**
 * @brief   Inter-Process-Communication common header
 * @date    2023-7-25
 */
#ifndef __IPC_COMMON_H__
#define __IPC_COMMON_H__

enum {
    COMM_DEV_POWER_WORK = 0,
    COMM_DEV_POWER_SUSPENDING = 1,
    COMM_DEV_POWER_SUSPENDED,
    COMM_DEV_POWER_MAX,
};

enum mailbox_type {
    MAILBOX_TYPE_INDIRECT_DATA = 1, /* Data is transmit in shared memory */
    MAILBOX_TYPE_DIRECT_DATA = 2, /* Data is transmit directly */
    MAILBOX_TYPE_MALLOC_REQ = 3,  /* linux malloc req */
    MAILBOX_TYPE_MALLOC_ACK = 4,  /* rtos malloc ack msg */
    MAILBOX_TYPE_FREE = 5,        /* linux free msg */
    MAILBOX_TYPE_RING_INFO = 6,   /* rtos send ring info msg */
    MAILBOX_TYPE_RING_ACK = 7,    /* linux  send ring info ack */
    MAILBOX_TYPE_ONLY_EVENT = 8,  /* only event no data msg */
    MAILBOX_TYPE_IPC_VER_MISMATCH = 9,  /* ipc_ver on both side don't match */
    MAILBOX_TYPE_TML_VER_MISMATCH = 10,  /* tml_ver on both side don't match */
    MAILBOX_TYPE_RTOS_WAKEUP = 11,  /* notify Ap wakeup finish */
    MAILBOX_TYPE_SUSPEND_REQ = 12,  /* rtos wakeup finish */
    MAILBOX_TYPE_SUSPEND_ACK = 13,  /* rtos wakeup finish */
    MAILBOX_TYPE_SUSPEND_DONE = 14,  /* rtos wakeup finish */
    MAILBOX_TYPE_LOG_OUTPUT = 15,  /* rtos log output */
};

/* checksum code borrowed from kernel/lib/checksum.c start */
static inline unsigned short from32to16(unsigned int x)
{
    /* add up 16-bit and 16-bit for 16+c bit */
    x = (x & 0xffff) + (x >> 16);
    /* add up carry.. */
    x = (x & 0xffff) + (x >> 16);
    return x;
}

#define MACHINE_IS_LITTLE_ENDIAN 1
static inline unsigned int do_checksum(const unsigned char *buff, int len)
{
    int odd;
    unsigned int result = 0;

    if (len <= 0)
        goto out;
    odd = 1 & (unsigned long) buff;
    if (odd) {
#ifdef MACHINE_IS_LITTLE_ENDIAN
        result += (*buff << 8);
#else
        result = *buff;
#endif
        len--;
        buff++;
    }
    if (len >= 2) {
        if (2 & (unsigned long) buff) {
            result += *(unsigned short *) buff;
            len -= 2;
            buff += 2;
        }
        if (len >= 4) {
            const unsigned char *end = buff + ((unsigned)len & ~3);
            unsigned int carry = 0;
            do {
                unsigned int w = *(unsigned int *) buff;
                buff += 4;
                result += carry;
                result += w;
                carry = (w > result);
            } while (buff < end);
            result += carry;
            result = (result & 0xffff) + (result >> 16);
        }
        if (len & 2) {
            result += *(unsigned short *) buff;
            buff += 2;
        }
    }
    if (len & 1)
#ifdef MACHINE_IS_LITTLE_ENDIAN
        result += *buff;
#else
    result += (*buff << 8);
#endif
    result = from32to16(result);
    if (odd)
        result = ((result >> 8) & 0xff) | ((result & 0xff) << 8);
out:
    return result;
}
/* checksum code borrowed from kernel/lib/checksum.c end */

#endif /*  __IPC_COMMON_H__ */
