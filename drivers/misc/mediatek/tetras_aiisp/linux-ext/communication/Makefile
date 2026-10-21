##############################################################################
#
# (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
#
# Makefile for Tetras spi2ahb driver.
#
# Change Logs:
# Date           Author       Notes
# 2022-1-27     zhoumiao     Initialize.
#
##############################################################################
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/tetras_aiisp/linux-ext/include/tetras_kernel
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/tetras_aiisp/linux-ext/include/tetras_uapi
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/tetras_aiisp/ipc/linux
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/tetras_aiisp/ipc/common

obj-m := communication.o
communication-objs := comm_drv.o comm_chan_adapt.o ring_buffer_remote.o ring_buffer_local.o comm_phy.o comm_transfer.o comm_route.o channel/comm_spi.o channel/comm_ipc.o