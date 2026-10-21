obj-$(CONFIG_MTK_VIDEO_LM3643)     += lm3643.o
obj-$(CONFIG_MTK_VIDEO_LM3644)     += lm3644.o
obj-$(CONFIG_MTK_VIDEO_AW36515)    += aw36515.o
obj-$(CONFIG_MTK_VIDEO_OCP81375)   += ocp81375.o
obj-$(CONFIG_MTK_VIDEO_OCP81378)   += ocp81378.o
obj-$(CONFIG_MTK_VIDEO_AWT36515)   += awt36515.o
obj-$(CONFIG_MTK_VIDEO_CW8722)     += cw8722.o
obj-$(CONFIG_MTK_VIDEO_SGM37864)   += sgm37864.o

MTK_CUSTOM_PATH := $(DEVICE_MODULES_PATH)/drivers/misc/mediatek
subdir-ccflags-y += \
	-I$(MTK_CUSTOM_PATH)/flashlight

subdir-ccflags-y += \
	-I$(DEVICE_MODULES_PATH)/drivers/thermal/mediatek
ifneq ($(wildcard $(MTK_CUSTOM_PATH)/flashlight/v4l2/k6983v1_64_alpha),)
obj-$(CONFIG_MTK_VIDEO_SY7806)     += k6983v1_64_alpha/sy7806.o
endif
