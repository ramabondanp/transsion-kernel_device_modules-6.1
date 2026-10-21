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


subdir-ccflags-y += -Werror -I$(DEVICE_MODULES_PATH)/drivers/power/supply/
subdir-ccflags-y += -Werror -I$(DEVICE_MODULES_PATH)/drivers/power/supply/transsion
subdir-ccflags-y += -Werror -I$(DEVICE_MODULES_PATH)//drivers/misc/mediatek/hall_rlk/

obj-$(CONFIG_CHARGER_CPS4021) += tran_cps4021.o
tran_cps4021-objs := cps4021.o

obj-$(CONFIG_CHARGER_MT5728) += tc_mt5728.o
tc_mt5728-objs := tran_mt5728.o

obj-$(CONFIG_WIRELESS_MANAGER) += tran_wireless_manager.o
tran_wireless_manager-objs := wireless_manager.o wireless_class.o wireless_charger.o
