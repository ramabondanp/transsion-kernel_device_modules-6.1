LOCAL_PATH := $(call my-dir)
DLKM_DIR   := $(TOP)/device/qcom/common/dlkm
KBUILD_OPTIONS := KERNEL_DIR=$(shell pwd)/kernel/msm-5.4
KBUILD_OPTIONS += TETRAS_DRV_DIR=$(shell pwd)/vendor/tetras/common/drivers/linux-ext/
include $(CLEAR_VARS)
# For incremental compilation
LOCAL_SRC_FILES           := $(wildcard $(LOCAL_PATH)/*)
LOCAL_MODULE              := cam_ai_isp.ko
LOCAL_MODULE_KBUILD_NAME  := cam_ai_isp.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
# Include kp_module.ko in the /vendor/lib/modules (vendor.img)
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/cam_ai_isp.ko
include $(DLKM_DIR)/AndroidKernelModule.mk
