#
# Makefile for misc devices that really don't fit anywhere else.
#

obj-$(CONFIG_MTK_V4L2_LENS)   += v4l2/
obj-$(CONFIG_MTK_LENS)   += proprietary/

ifneq ($(CONFIG_CUSTOM_TRAN_PROJECT),)
TRAN_KERNEL_FLAGS := $(addprefix -D, $(shell echo \
     $(CONFIG_CUSTOM_TRAN_PROJECT) |  tr a-z A-Z))
     subdir-ccflags-y += $(TRAN_KERNEL_FLAGS)
endif
