##############################################################################
#
# (C) Copyright 2021, Shenzhen Tetras.AI Technology Co., Ltd
#
# Makefile for Tetras pcie subsystem driver.
#
# Change Logs:
# Date           Author       Notes
# 2022-06-30     ShenJiming   Porting from kernel dmatest module.
#
##############################################################################
ifneq  ($(KERNELRELEASE),)
obj-m := dmatest.o
pcie-objs := dmatest.o
else
PWD ?= $(shell pwd)
CLANG_TRIPLE=aarch64-linux-gnu-
KERNELDIR ?= $(ANDROID_BUILD_TOP)/out/target/product/lahaina/obj/KERNEL_OBJ
CROSS_COMPILE=$(ANDROID_BUILD_TOP)/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-
LD=$(ANDROID_BUILD_TOP)/prebuilts/clang/host/linux-x86/clang-r383902b1/bin/ld.lld
MAKE=$(ANDROID_BUILD_TOP)/prebuilts/build-tools/linux-x86/bin/make
CC=$(ANDROID_BUILD_TOP)/prebuilts/clang/host/linux-x86/clang-r383902b1/bin/clang

.PHONY: modules clean

modules:
	$(MAKE) CC=$(CC) CLANG_TRIPLE=$(CLANG_TRIPLE) ARCH=arm64 LD=$(LD) CROSS_COMPILE=$(CROSS_COMPILE) \
	-C $(KERNELDIR) M=$(PWD) modules

BUILT_PRODUCT = $(shell find . -name '*.o' -o -name '*.mod' -o -name '*.symversions' -o -name '*.mod.c' -o -name '*.order' -o -name '*.symvers' -o -name '*.ko')
clean:
	@rm -f $(BUILT_PRODUCT)
endif
