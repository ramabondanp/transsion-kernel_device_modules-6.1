#
# focaltech_ft3681.ko
#
# Kbuild: for kernel building external module
#
# Note:
# - Please use these predefined name for keeping code:
#   KO_MODULE_NAME KO_MODULE_PATH KO_MODULE_SRC
#

KO_MODULE_NAME := focaltech_noflash_common

$(warning =======================transsion tp focaltech_noflash===============================)
$(warning =======================transsion tp focaltech_noflash===============================)

#
# source
#
$(KO_MODULE_NAME)-y := focaltech_core.o \
		focaltech_ex_fun.o \
		focaltech_esdcheck.o \
		focaltech_point_report_check.o

$(KO_MODULE_NAME)-y += focaltech_flash_spi.o \
		focaltech_spi_v2.o

#
# Build Options
#

ccflags-y += -I$(DEVICE_MODULES_PATH)/include/linux
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/input/touchscreen/transsion/core/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/input/touchscreen/transsion/vendor/focaltech/focaltech_no_flash_common/include/

#
# Final Objects
#
obj-m := $(KO_MODULE_NAME).o

