#
# Copyright (C) 2015 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

# tran LED class
obj-$(CONFIG_TC_LEDS_CLASS)		+= tc_led_class.o tc_led.o

subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/power/supply/transsion/leds/leds-aw22xxx
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/power/supply/transsion/leds/tran_leds_aw20144
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/power/supply/transsion/leds/tran_leds_aw2023
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/power/supply/transsion/leds/tran_leds_aw210xx
subdir-ccflags-y += -I$(CONFIG_TC_LEDS_HK320F0301)/drivers/power/supply/transsion/leds/tran_leds_hk32f0301
obj-$(CONFIG_TC_LEDS_AW22XXX) += leds-aw22xxx/
obj-$(CONFIG_TC_LEDS_AW20144) += tran_leds_aw20144/
obj-$(CONFIG_TC_LEDS_AW2023) += tran_leds_aw2023/
obj-$(CONFIG_TC_LEDS_AW210XX) += tran_leds_aw210xx/
obj-$(CONFIG_TC_LEDS_HK320F0301) += tran_leds_hk32f0301/
