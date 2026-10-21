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
#Target platform compile setup
ifeq ($(TARGET_PRODUCT), full_k6877v1_64_pre)
CLANG_TRIPLE=aarch64-linux-gnu-
MAKE=$(ANDROID_BUILD_TOP)/prebuilts/build-tools/linux-x86/bin/make
CROSS_COMPILE=$(ANDROID_BUILD_TOP)/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-
KERNELDIR ?= $(ANDROID_BUILD_TOP)/out/target/product/k6877v1_64_pre/obj/KERNEL_OBJ/
LD=$(ANDROID_BUILD_TOP)/prebuilts/clang/host/linux-x86/clang-r416183b/bin/ld.lld
CC=$(ANDROID_BUILD_TOP)/prebuilts/clang/host/linux-x86/clang-r416183b/bin/clang
EXTRA_PARAM=CC=$(CC) CLANG_TRIPLE=$(CLANG_TRIPLE) ARCH=arm64 LD=$(LD) CROSS_COMPILE=$(CROSS_COMPILE)
endif

ifeq ($(TARGET_PRODUCT), lahaina)
CLANG_TRIPLE=aarch64-linux-gnu-
MAKE=$(ANDROID_BUILD_TOP)/prebuilts/build-tools/linux-x86/bin/make
CROSS_COMPILE=$(ANDROID_BUILD_TOP)/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-
KERNELDIR ?= $(ANDROID_BUILD_TOP)/out/target/product/lahaina/obj/KERNEL_OBJ
LD=$(ANDROID_BUILD_TOP)/prebuilts/clang/host/linux-x86/clang-r383902b1/bin/ld.lld
CC=$(ANDROID_BUILD_TOP)/prebuilts/clang/host/linux-x86/clang-r383902b1/bin/clang
EXTRA_PARAM=CC=$(CC) CLANG_TRIPLE=$(CLANG_TRIPLE) ARCH=arm64 LD=$(LD) CROSS_COMPILE=$(CROSS_COMPILE)
endif

ifeq ($(TARGET_PRODUCT), ya157c)
KERNELDIR ?= $(SDKTARGETSYSROOT)/../../../kernel/build/
MAKE=make
EXTRA_PARAM=
endif

ifeq ($(TARGET_PRODUCT), sony_ne2)
KERNELDIR ?= $(NE2_SDK_ROOT)/kernel/build/
MAKE=make
EXTRA_PARAM=ARCH=arm64
endif

ifeq ($(TARGET_PRODUCT), imx7d_96b)
CROSS_COMPILE=arm-linux-gnueabihf-
KERNELDIR ?= $(IMX7D_96B_SDK_ROOT)/linux/linux-5.10/
MAKE=make
EXTRA_PARAM=
endif
