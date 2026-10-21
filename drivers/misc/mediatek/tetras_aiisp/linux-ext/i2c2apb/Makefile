##############################################################################
#
# (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
#
# Makefile for Tetras driver.
#
# Change Logs:
# Date           Author       Notes
# 2022-02-15     suntongce    Initialize.
#
##############################################################################

#
# Makefile for tetras i3c2apb drivers
#

ifneq  ($(KERNELRELEASE),)
ccflags-y := -I$(PWD)/../include/tetras_kernel -I$(PWD)/../include/tetras_uapi
obj-m:=i2c2apb.o
i2c2apb-objs:= i2c2apb_drv.o
else
PWD ?= $(shell pwd)
CLANG_TRIPLE=aarch64-linux-gnu-
MAKE=$(ANDROID_BUILD_TOP)/prebuilts/build-tools/linux-x86/bin/make
CROSS_COMPILE=$(ANDROID_BUILD_TOP)/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-

# NOTE: qcom and mtk use different path and clang veriosn
ifeq ($(TARGET_PRODUCT), full_k6877v1_64_pre)
KERNELDIR ?= $(ANDROID_BUILD_TOP)/out/target/product/k6877v1_64_pre/obj/KERNEL_OBJ/
LD=$(ANDROID_BUILD_TOP)/prebuilts/clang/host/linux-x86/clang-r416183b/bin/ld.lld
CC=$(ANDROID_BUILD_TOP)/prebuilts/clang/host/linux-x86/clang-r416183b/bin/clang
else
KERNELDIR ?= $(ANDROID_BUILD_TOP)/out/target/product/lahaina/obj/KERNEL_OBJ
LD=$(ANDROID_BUILD_TOP)/prebuilts/clang/host/linux-x86/clang-r383902b1/bin/ld.lld
CC=$(ANDROID_BUILD_TOP)/prebuilts/clang/host/linux-x86/clang-r383902b1/bin/clang
endif

.PHONY: modules clean

modules:
	$(MAKE) CC=$(CC) CLANG_TRIPLE=$(CLANG_TRIPLE) ARCH=arm64 LD=$(LD) CROSS_COMPILE=$(CROSS_COMPILE) \
	-C $(KERNELDIR) M=$(PWD) modules

BUILT_PRODUCT = $(shell find . -name '*.o' -o -name '*.mod' -o -name '*.symversions' -o -name '*.mod.c' -o -name '*.order' -o -name '*.symvers' -o -name '*.ko')
clean:
	@rm -f $(BUILT_PRODUCT)
endif
