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

obj-m := spibridge.o
spibridge-objs := spi2axi.o spi2ahb.o i2c2axi.o spi_bridge_drv.o i2c_bridge_drv.o ls_bridge2axi.o tetras_ls_socbridge.o

