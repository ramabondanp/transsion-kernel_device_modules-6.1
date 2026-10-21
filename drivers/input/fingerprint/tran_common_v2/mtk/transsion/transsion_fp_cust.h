/*
 * Copyright (C) 2013-2016, Shenzhen Huiding Technology Co., Ltd.
 * All Rights Reserved.
 */
#ifndef __TRAN_SPI_TEE_H
#define __TRAN_SPI_TEE_H

#include <linux/types.h>
#include <linux/netlink.h>
#include <linux/cdev.h>
#include <linux/input.h>
#ifndef CONFIG_SPI_MT65XX_KERNEL510
#include "mtk_spi.h"
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#else
#include <linux/notifier.h>
#endif

#define TRAN_NETLINK_ROUTE 30
#define MAX_NL_MSG_LEN 16
/**********************IO Magic**********************/
#define TRAN_IOC_MAGIC	'g'

/* define for tran_spi_cfg_t->speed_hz */
#define TRAN_SPI_SPEED_LOW 1
#define TRAN_SPI_SPEED_MEDIUM 6
#define TRAN_SPI_SPEED_HIGH 9

#include "tran_fp_common_data_def.h"

/* define commands */
#define TRAN_IOC_INIT			_IOR(TRAN_IOC_MAGIC, 0, uint8_t)
#define TRAN_IOC_EXIT			_IO(TRAN_IOC_MAGIC, 1)
#define TRAN_IOC_RESET			_IO(TRAN_IOC_MAGIC, 2)

#define TRAN_IOC_ENABLE_IRQ		_IO(TRAN_IOC_MAGIC, 3)
#define TRAN_IOC_DISABLE_IRQ		_IO(TRAN_IOC_MAGIC, 4)

#define TRAN_IOC_ENABLE_SPI_CLK           _IOW(TRAN_IOC_MAGIC, 5, uint32_t)
#define TRAN_IOC_DISABLE_SPI_CLK		_IO(TRAN_IOC_MAGIC, 6)

#define TRAN_IOC_ENABLE_POWER		_IO(TRAN_IOC_MAGIC, 7)
#define TRAN_IOC_DISABLE_POWER		_IO(TRAN_IOC_MAGIC, 8)

#define TRAN_IOC_INPUT_KEY_EVENT		_IOW(TRAN_IOC_MAGIC, 9, struct tran_key_nav)

/* fp sensor has change to sleep mode while screen off */
#define TRAN_IOC_ENTER_SLEEP_MODE		_IO(TRAN_IOC_MAGIC, 10)
#define TRAN_IOC_GET_FW_INFO		_IOR(TRAN_IOC_MAGIC, 11, uint8_t)
#define TRAN_IOC_REMOVE		_IO(TRAN_IOC_MAGIC, 12)
#define TRAN_IOC_CHIP_INFO	_IOW(TRAN_IOC_MAGIC, 13, struct tran_ioc_chip_info)

#define TRAN_IOC_NAV_EVENT	_IOW(TRAN_IOC_MAGIC, 14, tran_key_nav_event_t)

/* for SPI REE transfer */
#define TRAN_IOC_TRANSFER_CMD		_IOWR(TRAN_IOC_MAGIC, 15, struct tran_ioc_transfer)
#define TRAN_IOC_TRANSFER_RAW_CMD	_IOWR(TRAN_IOC_MAGIC, 16, struct tran_ioc_transfer_raw)
#define TRAN_IOC_SPI_INIT_CFG_CMD	_IOW(TRAN_IOC_MAGIC, 17, tran_spi_cfg_t)

#define  TRAN_IOC_MAXNR    18  /* THIS MACRO IS NOT USED NOW... */
 
/**************************REE SPI******************************/

/**********************function defination**********************/
irqreturn_t tran_irq(int irq, void *handle);
int tran_fb_notifier_callback(struct notifier_block *self,unsigned long event, void *data);
int tran_key_nav_adjust_type_value(tran_key_nav_event_t *key_nav_p, unsigned int cmd);
#endif	/* __TRAN_SPI_TEE_H */
