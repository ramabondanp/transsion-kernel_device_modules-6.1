# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/typec/tcpc/inc
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/power/supply/

subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/power/supply/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/power/supply/tran_chg
 subdir-ccflags-y += -I$(srctree)/drivers/extcon/
obj-$(CONFIG_EXTCON_MTK_USB) += extcon-mtk-usb.o
