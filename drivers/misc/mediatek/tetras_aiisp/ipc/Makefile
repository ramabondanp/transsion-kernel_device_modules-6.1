##############################################################################
#
# (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
#
# Makefile for Tetras spi2ahb driver.
#
# Change Logs:
# Date		Author       Notes
# 2022-1-27	Zhoumiao     Initialize.
#
##############################################################################
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/tetras_aiisp/ipc/linux
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/tetras_aiisp/ipc/common

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/tetras_aiisp/linux-ext/include/tetras_kernel
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/tetras_aiisp/linux-ext/include/tetras_uapi

obj-m := tetras_ipc.o
tetras_ipc-objs := common/ipc.o linux/ipc_mem.o linux/ipc_misc.o linux/ipc_io.o\
	linux/ipc_dev.o common/ipcm.o common/ipc_ring_buffer.o

