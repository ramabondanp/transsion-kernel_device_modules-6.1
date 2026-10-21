# SPDX-License-Identifier: GPL-2.0
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/mt-plat

obj-$(CONFIG_DRM_PANEL_NOVATEK_NT35695B_VDO) += panel-nt35695b-auo-vdo.o
obj-$(CONFIG_DRM_PANEL_NOVATEK_NT36672A_VDO) += panel-nt36672a-rt4801-vdo.o
obj-$(CONFIG_DRM_PANEL_SAMSUNG_ANA6705_CMD) += panel-samsung-ana6705-cmd.o
obj-$(CONFIG_DRM_PANEL_SAMSUNG_ANA6705_CMD_FHDP) += panel-samsung-ana6705-cmd-fhdp.o
obj-$(CONFIG_DRM_PANEL_HX_NT37701_DPHY_CMD) += panel-hx-nt37701-dphy-cmd.o
obj-$(CONFIG_DRM_PANEL_HX_NT37701_DPHY_CMD_120HZ) += panel-hx-nt37701-dphy-cmd-120hz.o
obj-$(CONFIG_DRM_PANEL_TRULY_TD4330_VDO) += panel-truly-td4330-vdo.o
obj-$(CONFIG_DRM_PANEL_TRULY_TD4330_CMD) += panel-truly-td4330-cmd.o
obj-$(CONFIG_DRM_PANEL_SAMSUNG_OP_CMD) += panel-samsung-op-cmd.o
obj-$(CONFIG_DRM_PANEL_SAMSUNG_OP_CMD_MSYNC2) += panel-samsung-op-cmd-msync2.o
obj-$(CONFIG_DRM_PANEL_TRULY_FT8756_VDO) += panel-truly-ft8756-vdo.o
obj-$(CONFIG_DRM_PANEL_ALPHA_JDI_NT36672C_CPHY_VDO) += panel-alpha-jdi-nt36672c-cphy-vdo.o
obj-$(CONFIG_DRM_PANEL_SC_NT36672C_VDO_120HZ) += panel-sc-nt36672c-vdo-120hz.o
obj-$(CONFIG_DRM_PANEL_TIANMA_R66451_CMD_120HZ) += panel-tianma-r66451-cmd-120hz.o
obj-$(CONFIG_DRM_PANEL_TIANMA_R66451_CMD_120HZ_WA) += panel-tianma-r66451-cmd-120hz-wa.o
obj-$(CONFIG_DRM_PANEL_TIANMA_NT36672E_VDO_120HZ_HFP) += panel-tianma-nt36672e-vdo-120hz-hfp.o
obj-$(CONFIG_DRM_PANEL_ALPHA_JDI_NT36672E_CPHY_VDO) += panel-alpha-jdi-nt36672e-cphy-vdo.o
obj-$(CONFIG_DRM_PANEL_ALPHA_JDI_NT36672E_VDO_120HZ) += panel-alpha-jdi-nt36672e-vdo-120hz.o
obj-$(CONFIG_DRM_PANEL_ALPHA_JDI_NT36672E_VDO_120HZ_THRESHOLD) += panel-alpha-jdi-nt36672e-vdo-120hz-threshold.o
obj-$(CONFIG_DRM_PANEL_ALPHA_JDI_NT36672E_VDO_144HZ) += panel-alpha-jdi-nt36672e-vdo-144hz.o
obj-$(CONFIG_DRM_PANEL_ALPHA_JDI_NT36672E_VDO_60HZ) += panel-alpha-jdi-nt36672e-vdo-60hz.o
obj-$(CONFIG_DRM_PANEL_ALPHA_JDI_NT36672E_VDO_120HZ_HFP) += panel-alpha-jdi-nt36672e-vdo-120hz-hfp.o
obj-$(CONFIG_DRM_PANEL_ALPHA_JDI_NT36672E_VDO_144HZ_HFP) += panel-alpha-jdi-nt36672e-vdo-144hz-hfp.o
obj-$(CONFIG_DRM_PANEL_TRULY_NT35595_CMD) += panel-truly-nt35595-cmd.o
obj-$(CONFIG_DRM_PANEL_NT37801_MSYNC2_RTE) += panel-nt37801-msync2-rte.o
obj-$(CONFIG_DRM_PANEL_SAMSUNG_S68FC01_VDO_AOD) += panel-samsung-s68fc01-vdo-aod.o
obj-$(CONFIG_DRM_PANEL_ALPHA_SAMSUNG_S6E3HAE_CMD_120HZ) += panel-alpha-samsung-s6e3hae-fhd-dphy-cmd-120hz-r4831a.o
obj-$(CONFIG_DRM_PANEL_HIMAX_HX83121A_VDO) += panel-himax-hx83121a-vdo.o
obj-$(CONFIG_DRM_PANEL_NT37801_CMD_LTPO) += panel-nt37801-cmd-ltpo.o
obj-$(CONFIG_DRM_PANEL_NT37801_CMD_120HZ) += panel-nt37801-cmd-120hz.o
obj-$(CONFIG_DRM_PANEL_L12A_42_02_0A_DSC_CMD) += panel-l12a-42-02-0a-dsc-cmd.o
obj-$(CONFIG_DRM_PANEL_NT37701A_CMD) += panel-nt37701a-cmd.o
obj-$(CONFIG_DRM_PANEL_NT37801_CMD_FHD) += panel-nt37801-cmd-fhd.o
obj-$(CONFIG_DRM_PANEL_NT37801_CMD_SPR) += panel-nt37801-cmd-spr.o
obj-$(CONFIG_DRM_PANEL_NT37801_CMD_FHD_PLUS) += panel-nt37801-cmd-fhd-plus.o
obj-$(CONFIG_DRM_PANEL_ALPHA_NT37706_VDO_120HZ) += panel-alpha-nt37706-vdo-120hz.o
obj-$(CONFIG_DRM_PANEL_BOE_TS127QFMLL1DKP0) += panel-boe-ts127qfmll1dkp0.o
obj-$(CONFIG_DRM_PANEL_BOE_TS127QFMLL1DKP0_120HZ) += panel-boe-ts127qfmll1dkp0-120hz.o
obj-$(CONFIG_DRM_PANEL_NT37707_677_FHDP_DPHY_CMD) += panel-nt37707-667-fhdp-dphy-cmd.o
obj-$(CONFIG_DRM_PANEL_NT37705_1056_1068_DPHY_CMD) += panel-nt37705-1056-1068-dphy-cmd.o
obj-$(CONFIG_DRM_PANEL_VDTR6130_CMD_120HZ_CSOT) += panel-vdtr6130-fhdp-dsi-cmd-dphy-120hz-csot.o
ifneq ($(wildcard $(srctree)/$(src)/k6985v1_64),)
obj-$(CONFIG_DRM_TD2204_WQHD_AMB678ZY01_S6E3HC3_CMD) += k6985v1_64/td2204-wqhd-amb678zy01-s6e3hc3-cmd.o
endif
ifneq ($(wildcard $(srctree)/$(src)/k6985v1_64_alpha),)
obj-$(CONFIG_DRM_PANEL_NT37705_ALPHA_CMD) += k6985v1_64_alpha/panel-nt37705-alpha-cmd.o
endif
ifneq ($(wildcard $(srctree)/$(src)/k6989v1_64_alpha),)
obj-$(CONFIG_DRM_PANEL_ILI7838E_ALPHA_CMD) += k6989v1_64_alpha/panel-ili7838e-alpha-cmd.o
obj-$(CONFIG_DRM_PANEL_ILI7838E_DV2_ALPHA_CMD) += k6989v1_64_alpha/panel-ili7838e-dv2-alpha-cmd.o
obj-$(CONFIG_DRM_PANEL_ILI7838E_DV2_SPR_CMD) += k6989v1_64_alpha/panel-ili7838e-dv2-spr-cmd.o
obj-$(CONFIG_DRM_PANEL_RM692H5_ALPHA_CMD) += k6989v1_64_alpha/panel-rm692h5-alpha-cmd.o
endif
ifneq ($(wildcard $(srctree)/$(src)/k6989v1_64_alpha_vdo),)
obj-$(CONFIG_DRM_PANEL_ILI7838E_DV2_ALPHA_VDO) += k6989v1_64_alpha_vdo/panel-ili7838e-dv2-alpha-vdo.o
endif
ifneq ($(wildcard $(srctree)/$(src)/k6989v1_64_flip_alpha),)
obj-$(CONFIG_DRM_PANEL_BOE_BF130_DPHY_CMD_HD) += k6989v1_64_flip_alpha/panel-boe-bf130-dphy-cmd-hd.o
obj-$(CONFIG_DRM_PANEL_SAMSUNG_AMF680CM01_CMD_FHD) += k6989v1_64_flip_alpha/panel-samsung-amf680cm01-cmd-fhd.o
endif
ifneq ($(wildcard $(srctree)/$(src)/k6878v1_64_alpha),)
obj-$(CONFIG_DRM_PANEL_MOT_CSOT_NT37705_DPHY_CMD) += k6878v1_64_alpha/dsi-panel-mot-csot-nt37705-1056x1068-dphy-cmd-cli.o
obj-$(CONFIG_DRM_PANEL_MOT_CSOT_NT37707_FHDP_DPHY_CMD) += k6878v1_64_alpha/dsi-panel-mot-csot-nt37707-667-fhdp-dphy-cmd.o
endif
obj-$(CONFIG_NT37703A_FHDP_DSI_VDO_TM_TM_D8300) += nt37703a_fhdp_dsi_vdo_tm_tm_d8300.o
obj-$(CONFIG_NT37900_WQHD_DSI_CMD_CSOT_120HZ_D9300) += nt37900_wqhd_dsi_cmd_csot_120hz_d9300.o
#obj-$(CONFIG_ILI7838E_FHDP_DSI_CMD_TM_TM_120HZ_D9300) += ili7838e_fhdp_dsi_cmd_tm_tm_120hz_d9300.o
obj-$(CONFIG_NT37703A_FHDP_DSI_VDO_TM_TM_144HZ_X6856) += nt37703a_fhdp_dsi_vdo_tm_tm_144hz_x6856.o
obj-$(CONFIG_RM692J0_FHDP_DSI_CMD_DSC_BOE_BOE_144HZ_CM8) += rm692j0_fhdp_dsi_cmd_dsc_boe_boe_144hz_cm8.o
obj-$(CONFIG_NT37703A_FHDP_DSI_VDO_TM_TM_144HZ_CM7) += nt37703a_fhdp_dsi_vdo_tm_tm_144hz_cm7.o
obj-$(CONFIG_TRAN_PANEL_I2C_LCD_BIAS_SUPPORT) += tran_drm_i2c_drv/tran_drm_panel_i2c.o
obj-$(CONFIG_TD4160_HDP_DSI_VDO_TXD_INX_120HZ_X6857) += td4160_hdp_dsi_vdo_txd_inx_120hz_x6857.o
obj-$(CONFIG_NT37703A_FHDP_DSI_VDO_TM_TM_144HZ_X6870) += nt37703a_fhdp_dsi_vdo_tm_tm_144hz_x6870.o
obj-$(CONFIG_VTDR6126A_FHDP1224X2720_DSI_VDO_TM_144HZ_X6873) += vtdr6126a_fhdp1224x2720_dsi_vdo_tm_144hz_x6873.o
obj-$(CONFIG_NT37703A_FHDP_DSI_VDO_TM_TM_144HZ_LJ8K) += nt37703a_fhdp_dsi_vdo_tm_tm_144hz_lj8k.o
obj-$(CONFIG_FT8725_FHDP_DSI_VDO_TXD_BOE_144HZ_LJ7) += ft8725_fhdp_dsi_vdo_txd_boe_144hz_lj7.o
obj-$(CONFIG_RM692J0_FHDP_DSI_CMD_DSC_BOE_BOE_144HZ_LJ9) += rm692j0_fhdp_dsi_cmd_dsc_boe_boe_144hz_lj9.o
obj-$(CONFIG_RM692J0_FHDP_DSI_CMD_DSC_BOE2_BOE_144HZ_LJ9) += rm692j0_fhdp_dsi_cmd_dsc_boe2_boe_144hz_lj9.o
obj-$(CONFIG_VTDR6126A_FHDP1224X2720_DSI_VDO_TM_144HZ_LJ8) += vtdr6126a_fhdp1224x2720_dsi_vdo_tm_144hz_lj8.o
obj-$(CONFIG_VTDR6126A_FHDP1224X2720_DSI_VDO_TM_144HZ_X6876) += vtdr6126a_fhdp1224x2720_dsi_vdo_tm_144hz_x6876.o

ifneq ($(wildcard $(srctree)/$(src)/k6989v2_64_alpha),)
obj-$(CONFIG_DRM_PANEL_NT37706A_BOE_FHDPLUS_VDO) += k6989v2_64_alpha/panel-nt37706a-boe-fhdplus-vdo.o
endif

mediatek-drm-gateic-y := mtk_drm_panel_i2c.o \
		  mtk_drm_gateic_rt4801h.o \
		  mtk_drm_gateic_rt4831a.o \
		  mtk_drm_gateic.o

mediatek-drm-panel-drv-y :=  \
		  mtk_round_corner/mtk_drm_rc.o \
		  mtk_drm_panel_helper.o \
		  mtk_drm_panel_helper_dbi.o \
		  mtk_drm_panel_helper_dpi.o \
		  mtk_drm_panel_helper_dsi.o \
		  mtk_drm_panel_drv_dbi.o \
		  mtk_drm_panel_drv_dpi.o \
		  mtk_drm_panel_drv_dsi.o \
		  mtk_drm_panel_drv.o

mediatek-cust-panel-sample-y :=  \
		  cust_panel/mtk_cust_panel_sample/mtk_drm_panel_cust.o \
		  cust_panel/cust_lcm_hal.o


obj-$(CONFIG_DRM_MEDIATEK_PANEL) += mediatek-drm-gateic.o
obj-$(CONFIG_DRM_MEDIATEK_PANEL) += mediatek-drm-panel-drv.o
obj-$(CONFIG_DRM_MEDIATEK_PANEL) += mediatek-cust-panel-sample.o

ccflags-$(CONFIG_DRM_MEDIATEK_PANEL) += -I$(DEVICE_MODULES_PATH)/drivers/leds/
