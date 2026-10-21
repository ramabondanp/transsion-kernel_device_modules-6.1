##############################################################################
#
# (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
#
# Makefile for Tetras spi2ahb driver.
#
# Change Logs:
# Date           Author       Notes
# 2021-12-30     yanghua      Initialize.
#
##############################################################################
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/tetras_aiisp/linux-ext/include/tetras_kernel
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/tetras_aiisp/linux-ext/include/tetras_uapi
ccflags-y += -I$(srctree)/drivers/mmc/core


obj-m := sdiobridge.o
sdiobridge-objs := sdio2axi_pwrseq.o tetras_sdiobridge.o

