LOCAL_PATH := $(call my-dir)

ifneq ($(filter lahaina taro, $(TARGET_BOARD_PLATFORM)),)
include $(LOCAL_PATH)/qcom.mk
endif

ifneq ($(filter lahaina, $(TARGET_BOARD_PLATFORM)),)
include $(LOCAL_PATH)/aiisp_drv/aiisp_drv_qcom.mk
endif

ifneq ($(filter mt6877, $(TARGET_BOARD_PLATFORM)),)
include $(LOCAL_PATH)/aiisp_drv/aiisp_drv_mtk.mk
include $(LOCAL_PATH)/../communication/comm_drv_mtk.mk
include $(LOCAL_PATH)/../tetras_ls_socbridge/spibridge_drv_mtk.mk
include $(LOCAL_PATH)/../sdio2axi/sdiobridge_drv_mtk.mk
include $(LOCAL_PATH)/../ai_isp_pmctrl/aiisp_pmctrl_drv_mtk.mk
endif

ifneq ($(filter mt6855 mt6879 mt6886, $(TARGET_BOARD_PLATFORM)),)
include $(LOCAL_PATH)/tetras_ls_socbridge/spibridge_drv_mtk.mk
include $(LOCAL_PATH)/../communication/comm_drv_mtk.mk
include $(LOCAL_PATH)/../aiisp_drv/aiisp_drv_mtk.mk
include $(LOCAL_PATH)/../sdio2axi/sdiobridge_drv_mtk.mk
include $(LOCAL_PATH)/../ai_isp_pmctrl/aiisp_pmctrl_drv_mtk.mk
endif
