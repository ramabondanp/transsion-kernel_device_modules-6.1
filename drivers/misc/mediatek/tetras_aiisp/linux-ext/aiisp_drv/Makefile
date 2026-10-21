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
ccflags-y += -DLINUX_KERNEL
#ccflags-y += -DUSE_MTK_EXTISP
obj-m:= cam_ai_isp.o

cam_ai_isp-objs :=     \
	./cam_aiisp_dev.o \
	./package_handle.o \
	./cam_aiisp_pingpong.o \
	./cam_aiisp_mtk_dev.o

KBUILD_EXTRA_SYMBOLS=$(PWD)/../../common/drivers/linux-ext/communication/Module.symvers

clean:
	@rm -rf *.o *.order *.symvers *.mod *.mod.* .cam_aiisp_dev.mod.cmd .*.o.cmd .*.mod.o.cmd .*.ko.cmd .tmp_versions *.ko *.symversions

.PHONY: clean




