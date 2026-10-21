ifneq ($(filter mt6877 mt6855 mt6879 mt6886, $(TARGET_BOARD_PLATFORM)),)
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := tetras_ipc.ko
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_OWNER := tetras
LOCAL_REQUIRED_MODULES := spibridge.ko sdiobridge.ko pmctrl.ko
include $(MTK_KERNEL_MODULE)
endif
