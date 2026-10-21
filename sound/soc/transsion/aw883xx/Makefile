# MTK include path
#subdir-ccflags-y += -I$(srctree)/sound/soc/mediatek/audio_dsp
#subdir-ccflags-y += -I$(srctree)/sound/soc/mediatek/common
#subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/audio_ipi/include
#subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
#subdir-ccflags-y += -I$(srctree)/drivers/base
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/audio_ipi/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/sound/soc/mediatek/audio_dsp
ccflags-y += -I$(DEVICE_MODULES_PATH)/sound/soc/mediatek/common
# debugging support (also enables trace_printk)
#ccflags-y += -Werror
#ccflags-y += $(call cc-option,-Wno-date-time,)

snd-soc-aw883xx-objs:= aw883xx.o aw883xx_monitor.o aw883xx_bin_parse.o aw883xx_device.o aw883xx_init.o aw883xx_calib.o aw883xx_spin.o aw883xx_pid_2049_init.o aw883xx_pid_2066_init.o aw883xx_pid_2183_init.o

obj-$(CONFIG_SND_SMARTPA_AW883XX) += snd-soc-aw883xx.o
