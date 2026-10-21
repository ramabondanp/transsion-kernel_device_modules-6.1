# SPDX-License-Identifier: GPL-2.0
subdir-ccflags-$(CONFIG_POWER_SUPPLY_DEBUG) := -DDEBUG
subdir-ccflags-y += -Werror -I$(DEVICE_MODULES_PATH)/drivers/thermal/mediatek/
subdir-ccflags-y += -Werror -I$(DEVICE_MODULES_PATH)/drivers/power/supply/
subdir-ccflags-y += -Werror -I$(DEVICE_MODULES_PATH)/drivers/power/supply/transsion/wireless
subdir-ccflags-y += -Werror -I$(DEVICE_MODULES_PATH)/drivers/power/supply/transsion/leds
ccflags-$(CONFIG_DRM_MEDIATEK_V2) += -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/
subdir-ccflags-y += -Werror -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/typec/tcpc/inc
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/power/supply/transsion/libs/authon-sdk/

obj-$(CONFIG_TC_CHARGER) += tc_charger_framework.o tc_charger_class.o tc_adapter_class.o tc_algorithm_class.o tc_pd_adapter.o
tc_charger_framework-objs := tc_charger.o tc_charger_intf.o tc_charger_attr.o tc_basic_charger.o tc_voter.o

obj-$(CONFIG_TC_CHARGER_TYPE_DETECT)	+= tc_chg_type_det.o
obj-$(CONFIG_TC_TA_CLASS) += tc_ta_class.o 

obj-$(CONFIG_TC_PUMP_EXPRESS_50_SUPPORT) += tc_pep50.o
tc_pep50-y := tc_pe5_pps_intf.o tc_pe5_hal.o tc_pe5.o

obj-$(CONFIG_TCHARGER_RT9471) += tc_rt9471.o
obj-$(CONFIG_TCHARGER_HL7138) += tc_hl7138.o
obj-$(CONFIG_TCHARGER_HL7139A) += tc_hl7139a.o
obj-$(CONFIG_TCHARGER_SC8565) += tc_sc8565.o
obj-$(CONFIG_TCHARGER_SC8548) += tc_sc8548_charger.o

obj-$(CONFIG_TC_CLASS_SUPPORT) += tc_common_class.o
obj-$(CONFIG_TC_MISC_INTF) += tc_misc_intf.o
obj-$(CONFIG_TC_LCD_NOTIFY) += tc_lcd_notify.o
obj-$(CONFIG_TC_AMBIENT_DETECTION) += tc_ambient_temp_det.o
obj-$(CONFIG_TC_PID_CHARGER) += tc_pid_chg_algo.o
obj-$(CONFIG_TC_TEMP_FORECAST) += tc_temp_forecast.o
obj-$(CONFIG_TC_GAUGE) += tc_gauge.o
obj-$(CONFIG_TC_PMIC) += tc_pmic.o
obj-$(CONFIG_TC_AUDIO) += tc_audio.o
obj-$(CONFIG_TC_PD) += tc_pd_algo.o
tc_pd_algo-objs := tc_pd.o tc_pd_hal.o
obj-$(CONFIG_TC_PE20) += tc_pep20.o
tc_pep20-objs := tc_pe20.o tc_pe20_hal.o
obj-$(CONFIG_TC_USB_CONTROL) += tran_usb_ctrl.o
obj-$(CONFIG_TCHARGER_SC8950)	+= tc_sc8950.o
obj-$(CONFIG_TCHARGER_SGM41516D)	+= tc_sgm41516d.o
obj-$(CONFIG_TC_BQ27Z561)	+= tc_bq27z561.o
obj-$(CONFIG_TC_FUEL_GAUGE_NFG1000)	+= tc_fg_nfg1000.o
obj-$(CONFIG_TC_BATTERY) += tc_battery.o
obj-$(CONFIG_TC_CHARGE_TRANSFER) += tran_charge_transfer.o
obj-$(CONFIG_TC_OTG_CONTROL) += tc_otg_ctrl.o
obj-$(CONFIG_TC_WATER_DETECTION) += tc_water_detect.o

obj-$(CONFIG_TC_PORT_CORROSION) += tc_port_corrosion_prevention.o
obj-$(CONFIG_TC_PORT_BURNING) += tc_port_burning.o
obj-$(CONFIG_TC_PHY_INSERT_DET) += tc_phy_insert_det.o

obj-$(CONFIG_TC_AUTO_TEST)  += tc_auto_test.o
obj-$(CONFIG_TC_WIRELESS_CHARGER) += wireless/
obj-$(CONFIG_TC_CHARGER) += tc_tcpc.o
obj-$(CONFIG_TC_ADAPTER_CONTROL) += tc_adapter_control.o
obj-$(CONFIG_TC_SC760X) += tc_sc760x.o
obj-$(CONFIG_TRAN_BOOST_HL7603) += tc_boost_hl7603.o

obj-$(CONFIG_TC_LEDS_CLASS) += leds/
obj-$(CONFIG_TC_CHARGER_MT6375)	+= mt6375-charger.o

obj-$(CONFIG_BAT_AUTHON) += authon.o
obj-$(CONFIG_BAT_AUTHON_TEST) += kunit.o
authon-objs := authon_main.o
authon-objs += libs/authon-sdk/helper/helper.o
authon-objs += libs/authon-sdk/ecc/ecc.o
authon-objs += libs/authon-sdk/host_auth/host_auth.o
authon-objs += libs/authon-sdk/interface/interface.o
authon-objs += libs/authon-sdk/interface/swi.o
authon-objs += libs/authon-sdk/interface/swi_bus_command.o
authon-objs += libs/authon-sdk/nvm/nvm.o
authon-objs += libs/authon-sdk/authon_api.o
