# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for nfc devices

nfc_i2c-objs = common.o common_ese.o i2c_drv.o
obj-$(CONFIG_NXP_NFC_SNXXX_DEVICE) += nfc_i2c.o
ccflags-y += -DDYNAMIC_DEBUG_MODULE
