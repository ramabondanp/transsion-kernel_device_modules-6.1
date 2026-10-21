#
# adaptive-ts.ko
#
# Kbuild: for kernel building external module
#

KO_MODULE_NAME := adaptive-ts
$(warning =======================TRAN TP CORE===============================)
$(warning DEVICE_MODULES_PATH=$(DEVICE_MODULES_PATH))
$(warning CONFIG_DRM_MEDIATEK_V2=$(CONFIG_DRM_MEDIATEK_V2))
$(warning =======================TRAN TP CORE===============================)

$(KO_MODULE_NAME)-y := transsion_core.o
$(KO_MODULE_NAME)-y += platform/mediatek/transsion_mtk_platform.o

ccflags-$(CONFIG_DRM_MEDIATEK_V2) += -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/input/touchscreen/transsion/core/include/

obj-m += $(KO_MODULE_NAME).o
