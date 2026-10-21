LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
TETRAS_DRV_DIR := $(LOCAL_PATH)/../
LOCAL_MODULE := communication.ko
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_OWNER := tetras
LOCAL_REQUIRED_MODULES := spibridge.ko
LOCAL_REQUIRED_MODULES += tetras_ipc.ko
LOCAL_REQUIRED_MODULES += pmctrl.ko
include $(MTK_KERNEL_MODULE)
