# SPDX-License-Identifier: GPL-2.0

ifneq ($(CONFIG_MTK_TINYSYS_SCP_SUPPORT),)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/scp/include
endif

ifneq ($(CONFIG_DEVICE_MODULES_SND_SOC_MEDIATEK),)
ccflags-y += -I$(DEVICE_MODULES_PATH)/sound/soc/mediatek/common
endif

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/typec/tcpc/inc
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/power/supply/transsion
obj-m += splitswitch/

obj-$(CONFIG_SND_SMARTPA_AW883XX) += aw883xx/
obj-$(CONFIG_SND_SOC_AW87XXX) += aw87xxx/
obj-$(CONFIG_USB_SWITCH_WAS4783C) += was4783c.o
ifeq ($(CONFIG_SND_SOC_FS1589), m)
obj-m += fs1599/
endif
ifeq ($(CONFIG_SND_SOC_FS1599), m)
obj-m += fs1599/
endif
obj-$(CONFIG_TRAN_AUDIO_CONTROL)  += tran_audio_control/

obj-$(CONFIG_SND_SOC_OCA72XXX) += oca72xxx/

obj-$(CONFIG_USB_SWITCH_HL1280) += hl1280.o

obj-$(CONFIG_TRAN_ELLIPTIC_ULTRASOUND_PROXIMITY) += elliptic/
