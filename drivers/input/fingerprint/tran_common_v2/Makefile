# Copyright (C) 2015 Transsion Inc
# This file is used to setup the build rules

ccflags-y    += -I$(DEVICE_MODULES_PATH)/drivers/input/fingerprint/tran_common_v2 -I$(DEVICE_MODULES_PATH)/drivers/input/fingerprint/tran_common_v2/mtk/transsion \
-I$(DEVICE_MODULES_PATH)/drivers/spi -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/mt-plat -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/teei/300/tz_driver/include -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/teei/300/common/include -I$(srctree)/drivers/regulator -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/ -I$(DEVICE_MODULES_PATH)/drivers/tee/gud/510/MobiCoreDriver/public

ccflags-y    += -D__TRAN_BUILD_DATE=\"$(shell date +%Y%m%d)\"


ifeq ($(findstring android_u,$(CONFIG_TRAN_FP_ANDROID_VERSION)),android_u)
ccflags-y    += -DTRAN_FP_ANDROID_U
endif


ifeq ($(strip $(CONFIG_TRAN_FP_LOG_LVL)),VERBOSE)
ccflags-y    += -DTRAN_FP_LOG_LVL_VERBOSE
else ifeq ($(strip $(CONFIG_TRAN_FP_LOG_LVL)),DEBUG)
ccflags-y    += -DTRAN_FP_LOG_LVL_DEBUG
else ifeq ($(strip $(CONFIG_TRAN_FP_LOG_LVL)),INFO)
ccflags-y    += -DTRAN_FP_LOG_LVL_INFO
else ifeq ($(strip $(CONFIG_TRAN_FP_LOG_LVL)),WARNING)
ccflags-y    += -DTRAN_FP_LOG_LVL_WARNING
else ifeq ($(strip $(CONFIG_TRAN_FP_LOG_LVL)),ERROR)
ccflags-y    += -DTRAN_FP_LOG_LVL_ERROR
endif

$(info CONFIG_TRAN_FP_LOG_LVL=$(CONFIG_TRAN_FP_LOG_LVL) ...)
obj-$(CONFIG_TRAN_FINGERPRINT) += tran_fp_v2.o
tran_fp_v2-y := tran_fp_main.o tran_fp_ext.o
tran_fp_v2-y += mtk/transsion/transsion_fp_cust.o
