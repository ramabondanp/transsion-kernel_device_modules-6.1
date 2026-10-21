#
# focaltech_ft3681.ko
#
# Kbuild: for kernel building external module
#
# Note:
# - Please use these predefined name for keeping code:
#   KO_MODULE_NAME KO_MODULE_PATH KO_MODULE_SRC
#

KO_MODULE_NAME := gt9916_common
TRAN_TP_CORE_CODE_PATH := $(if $(filter /%,$(src)),,$(srctree)/)$(src)
$(warning =======================TRAN TP VENDOR===============================)
$(warning TRAN_TP_CORE_CODE_PATH=$(TRAN_TP_CORE_CODE_PATH))
$(warning DRM_MEDIATEK=$(CURDIR))
$(warning =======================TRAN TP VENDOR===============================)


#
# source
#
$(KO_MODULE_NAME)-y := \
		goodix_brl_i2c.o \
		goodix_brl_spi.o \
		goodix_ts_core.o \
		goodix_brl_hw.o \
		goodix_cfg_bin.o \
		goodix_ts_utils.o \
		goodix_brl_fwupdate.o
#
# Build Options
#

ccflags-y += -I$(DEVICE_MODULES_PATH)/include/linux
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/input/touchscreen/transsion/core/include/

#
# Final Objects
#
obj-m := $(KO_MODULE_NAME).o
