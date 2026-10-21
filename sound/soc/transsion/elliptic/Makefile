# SPDX-License-Identifier: GPL-2.0
ccflags-y := -I$(src) -Wall -Werror
IO_MODULE := mtk

# EXTRA_CFLAGS += -DDEBUG

ifeq ($(IO_MODULE), mtk)
ccflags-y += -I$(srctree)/../kernel_device_modules-6.1/drivers/misc/mediatek/scp/include
ccflags-y += -I$(srctree)/../kernel_device_modules-6.1/drivers/misc/mediatek/scp/rv
ccflags-y += -I$(srctree)/../kernel_device_modules-6.1/drivers/misc/mediatek/audio_ipi/include
ccflags-y += -I$(srctree)/../kernel_device_modules-6.1/drivers/misc/mediatek/sensors-1.0/hwmon/include
ccflags-y += -I$(srctree)/../kernel_device_modules-6.1/drivers/misc/mediatek/sensors-1.0/sensorHub/inc_v1
ccflags-y += -I$(srctree)/../kernel_device_modules-6.1/drivers/misc/mediatek/sensor/2.0/core
ccflags-y += -I$(srctree)/../kernel_device_modules-6.1/sound/soc/transsion/elliptic
endif

elliptic-ultrasound-objs := elliptic.o
elliptic-ultrasound-objs += elliptic_mixer_controls.o
elliptic-ultrasound-objs += io_modules/$(IO_MODULE)/elliptic_data_$(IO_MODULE)_io.o
elliptic-ultrasound-objs += elliptic_sysfs.o
obj-$(CONFIG_TRAN_ELLIPTIC_ULTRASOUND_PROXIMITY) += elliptic-ultrasound.o

# elliptic_driver-y := elliptic.o
# elliptic_driver-y += io_modules/$(IO_MODULE)/elliptic_data_io.o
# elliptic_driver-y += mixer_controls/$(MIXER_MODULE)/elliptic_mixer_controls.o
# elliptic_driver-y += elliptic_sysfs.o

# .PHONY: clean
# all:
# 	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) modules
# clean:
# 	rm *.o *.ko
