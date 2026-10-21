TETRAS_DRV_DIR := $(shell pwd)/vendor/tetras/common/drivers/linux-ext/

# Build msm-mmrm.ko
###########################################################
# This is set once per LOCAL_PATH, not per (kernel) module
KBUILD_OPTIONS := TETRAS_DRV_ROOT=$(TETRAS_DRV_DIR)
KBUILD_OPTIONS += BOARD_PLATFORM=$(TARGET_BOARD_PLATFORM)
###########################################################

DLKM_DIR   := $(TOP)/device/qcom/common/dlkm

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
# For incremental compilation
LOCAL_SRC_FILES           := $(wildcard $(LOCAL_PATH)/**/*) $(wildcard $(LOCAL_PATH)/*)
LOCAL_MODULE              := spibridge.ko
LOCAL_MODULE_KBUILD_NAME  := tetras_ls_socbridge/spibridge.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
# Include kp_module.ko in the /vendor/lib/modules (vendor.img)
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/spibridge.ko
ifeq ($(TARGET_BOARD_PLATFORM), lahaina)
include $(DLKM_DIR)/AndroidKernelModule.mk
else
include $(DLKM_DIR)/Build_external_kernelmodule.mk
endif

include $(CLEAR_VARS)
# For incremental compilation
LOCAL_SRC_FILES           := $(wildcard $(LOCAL_PATH)/**/*) $(wildcard $(LOCAL_PATH)/*)
LOCAL_MODULE              := pmctrl.ko
LOCAL_MODULE_KBUILD_NAME  := ai_isp_pmctrl/pmctrl.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
# Include kp_module.ko in the /vendor/lib/modules (vendor.img)
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/pmctrl.ko
ifeq ($(TARGET_BOARD_PLATFORM), lahaina)
include $(DLKM_DIR)/AndroidKernelModule.mk
else
include $(DLKM_DIR)/Build_external_kernelmodule.mk
endif

include $(CLEAR_VARS)
# For incremental compilation
LOCAL_SRC_FILES           := $(wildcard $(LOCAL_PATH)/**/*) $(wildcard $(LOCAL_PATH)/*)
LOCAL_MODULE              := tetras_ipc.ko
LOCAL_MODULE_KBUILD_NAME  := ../ipc/tetras_ipc.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
# Include kp_module.ko in the /vendor/lib/modules (vendor.img)
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/tetras_ipc.ko
ifeq ($(TARGET_BOARD_PLATFORM), lahaina)
include $(DLKM_DIR)/AndroidKernelModule.mk
else
include $(DLKM_DIR)/Build_external_kernelmodule.mk
endif

include $(CLEAR_VARS)
# For incremental compilation
LOCAL_SRC_FILES           := $(wildcard $(LOCAL_PATH)/**/*) $(wildcard $(LOCAL_PATH)/*)
LOCAL_MODULE              := i2c2apb.ko
LOCAL_MODULE_KBUILD_NAME  := i2c2apb/i2c2apb.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
# Include kp_module.ko in the /vendor/lib/modules (vendor.img)
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/i2c2apb.ko
ifeq ($(TARGET_BOARD_PLATFORM), lahaina)
include $(DLKM_DIR)/AndroidKernelModule.mk
else
include $(DLKM_DIR)/Build_external_kernelmodule.mk
endif

include $(CLEAR_VARS)
# For incremental compilation
LOCAL_SRC_FILES           := $(wildcard $(LOCAL_PATH)/**/*) $(wildcard $(LOCAL_PATH)/*)
LOCAL_MODULE              := communication.ko
LOCAL_MODULE_KBUILD_NAME  := communication/communication.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/communication.ko
ifeq ($(TARGET_BOARD_PLATFORM), lahaina)
include $(DLKM_DIR)/AndroidKernelModule.mk
else
include $(DLKM_DIR)/Build_external_kernelmodule.mk
endif

include $(CLEAR_VARS)
# For incremental compilation
LOCAL_SRC_FILES           := $(wildcard $(LOCAL_PATH)/**/*) $(wildcard $(LOCAL_PATH)/*)
LOCAL_MODULE              := sdiobridge.ko
LOCAL_MODULE_KBUILD_NAME  := sdio2axi/sdiobridge.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/sdiobridge.ko
ifeq ($(TARGET_BOARD_PLATFORM), lahaina)
include $(DLKM_DIR)/AndroidKernelModule.mk
else
include $(DLKM_DIR)/Build_external_kernelmodule.mk
endif
