##############################################################################
#
# (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
#
# Makefile for Tetras AI-ISP power control driver.
#
# Change Logs:
# Date           Author       Notes
# 2022-06-23     yanghua      Initialize.
#
##############################################################################
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/tetras_aiisp/linux-ext/include/tetras_kernel
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/tetras_aiisp/linux-ext/include/tetras_uapi
obj-m := pmctrl.o
pmctrl-objs := ai_isp_pmctrl.o