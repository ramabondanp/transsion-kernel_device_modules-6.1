LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
TETRAS_DRV_DIR := $(LOCAL_PATH)/../
LOCAL_MODULE := spibridge.ko
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_OWNER := tetras
include $(MTK_KERNEL_MODULE)
