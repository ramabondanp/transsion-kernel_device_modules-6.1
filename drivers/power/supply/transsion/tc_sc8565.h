// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Transsion Inc.
 */

#ifndef __SC8565_H__
#define __SC8565_H__

/* Control */
// REG 0x00    DEVICE_VER
#define SC8565_REG_00                       0x00
#define REG00_DEVICE_VER_MASK               0xFF
#define REG00_DEVICE_VER_SHIFT              0


// REG 0x01    VBAT1_OVP
#define SC8565_REG_01                       0x01
#define REG01_VBAT_OVP_DIS_MASK             0x80
#define REG01_VBAT_OVP_DIS_SHIFT            7
#define REG01_VBAT_OVP_ENABLE               0
#define REG01_VBAT_OVP_DISABLE              1

#define REG01_VBAT_OVP_MASK_MASK            0x40
#define REG01_VBAT_OVP_MASK_SHIFT           6

#define REG01_VBAT_OVP_FLAG_MASK            0x20
#define REG01_VBAT_OVP_FLAG_SHIFT           5

#define REG01_VBAT_OVP_MASK                 0x1F
#define REG01_VBAT_OVP_SHIFT                0
#define REG01_VBAT_OVP_BASE                 4450
#define REG01_VBAT_OVP_LSB                  25
#define REG01_VBAT_OVP_MAX                  5225


// REG 0x02     IBAT_OCP
#define SC8565_REG_02                       0x02
#define REG02_IBAT_OCP_DIS_MASK             0x80
#define REG02_IBAT_OCP_DIS_SHIFT            7
#define REG02_IBAT_OCP_ENABLE               0
#define REG02_IBAT_OCP_DISABLE              1

#define REG02_IBAT_OCP_MASK_MASK            0x20
#define REG02_IBAT_OCP_MASK_SHIFT           5

#define REG02_IBAT_OCP_FLAG_MASK            0x10
#define REG02_IBAT_OCP_FLAG_SHIFT           4

#define REG02_IBAT_OCP_MASK                 0x0F
#define REG02_IBAT_OCP_SHIFT                0
#define REG02_IBAT_OCP_BASE                 8000
#define REG02_IBAT_OCP_LSB                  500
#define REG02_IBAT_OCP_MAX                  15500


// REG 0x03    VUSB_OVP
#define SC8565_REG_03                       0x03
#define REG03_OVPGATE_ON_DG_SET_MASK        0x80
#define REG03_OVPGATE_ON_DG_SET_SHIFT       7

#define REG03_VUSB_OVP_MASK_MASK            0x40
#define REG03_VUSB_OVP_MASK_SHIFT           6

#define REG03_VUSB_OVP_FLAG_MASK            0x20
#define REG03_VUSB_OVP_FLAG_SHIFT           5

#define REG03_VUSB_OVP_STAT_MASK            0x10
#define REG03_VUSB_OVP_STAT_SHIFT           4

#define REG03_VUSB_OVP_MASK                 0x0F
#define REG03_VUSB_OVP_SHIFT                0
#define REG02_VUSB_OVP_BASE                 11000
#define REG02_VUSB_OVP_LSB                  1000
#define REG02_VUSB_OVP_6PV5                 7

// REG 0x04    VWPC_OVP
#define SC8565_REG_04                       0x04
#define REG04_WPCGATE_ON_DG_SET_MASK        0x80
#define REG04_WPCGATE_ON_DG_SET_SHIFT       7

#define REG04_VWPC_OVP_MASK_MASK            0x40
#define REG04_VWPC_OVP_MASK_SHIFT           6

#define REG04_VWPC_OVP_FLAG_MASK            0x20
#define REG04_VWPC_OVP_FLAG_SHIFT           5

#define REG04_VWPC_OVP_STAT_MASK            0x10
#define REG04_VWPC_OVP_STAT_SHIFT           4

#define REG04_VWPC_OVP_MASK                 0x0F
#define REG04_VWPC_OVP_SHIFT                0
#define REG02_VWPC_OVP_BASE                 11000
#define REG02_VWPC_OVP_LSB                  1000
#define REG02_VWPC_OVP_6PV5                 7

// REG 0x05    VOUT_VBUS_OVP
#define SC8565_REG_05                       0x05
#define REG05_VBUS_OVP_MASK                 0xFC
#define REG05_VBUS_OVP_SHIFT                2
#define REG05_VBUS_OVP_41MODE_MAX           26600
#define REG05_VBUS_OVP_41MODE_BASE          14000
#define REG05_VBUS_OVP_41MODE_LSB           200
#define REG05_VBUS_OVP_21MODE_MAX           13300
#define REG05_VBUS_OVP_21MODE_BASE          7000
#define REG05_VBUS_OVP_21MODE_LSB           100
#define REG05_VBUS_OVP_11MODE_MAX           6650
#define REG05_VBUS_OVP_11MODE_BASE          3500
#define REG05_VBUS_OVP_11MODE_LSB           50

#define REG05_VOUT_OVP_MASK                 0x03
#define REG05_VOUT_OVP_SHIFT                0
#define REG05_VOUT_OVP_BASE                 4800
#define REG05_VOUT_OVP_LSB                  200


// REG 0x06    IBUS_OCP
#define SC8565_REG_06                       0x06
#define REG06_IBUS_OCP_DIS_MASK             0x80
#define REG06_IBUS_OCP_DIS_SHIFT            7
#define REG05_IBUS_OCP_ENABLE               0
#define REG05_IBUS_OCP_DISABLE              1

#define REG06_IBUS_OCP_MASK_MASK            0x40
#define REG06_IBUS_OCP_MASK_SHIFT           6

#define REG06_IBUS_OCP_FLAG_MASK            0x20
#define REG06_IBUS_OCP_FLAG_SHIFT           5

#define REG06_IBUS_OCP_MASK                 0x1F
#define REG06_IBUS_OCP_SHIFT                0
#define REG06_IBUS_OCP_BASE                 2500
#define REG06_IBUS_OCP_LSB                  125
#define REG06_IBUS_OCP_MAX                  6375

// REG 0x07    IBUS_UCP
#define SC8565_REG_07                       0x07
#define REG07_IBUS_UCP_DIS_MASK             0x80
#define REG07_IBUS_UCP_DIS_SHIFT            7
#define REG07_IBUS_UCP_DISABLE              1
#define REG07_IBUS_UCP_ENABLE               0

#define REG07_IBUS_UCP_FALL_DG_SET_MASK     0x30
#define REG07_IBUS_UCP_FALL_DG_SET_SHIFT    4
#define REG07_IBUS_UCP_FALL_DG_SET_8US      0
#define REG07_IBUS_UCP_FALL_DG_SET_5MS      1
#define REG07_IBUS_UCP_FALL_DG_SET_20MS     2
#define REG07_IBUS_UCP_FALL_DG_SET_50MS     3

#define REG07_IBUS_UCP_RISE_MASK_MASK       0x08
#define REG07_IBUS_UCP_RISE_MASK_SHIFT      3

#define REG07_IBUS_UCP_RISE_FLAG_MASK       0x04
#define REG07_IBUS_UCP_RISE_FLAG_SHIFT      2

#define REG07_IBUS_UCP_FALL_MASK_MASK       0x02
#define REG07_IBUS_UCP_FALL_MASK_SHIFT      1

#define REG07_IBUS_UCP_FALL_FLAG_MASK       0x01
#define REG07_IBUS_UCP_FALL_FLAG_SHIFT      0


// REG 0x08    PMID2OUT_OVP
#define SC8565_REG_08                       0x08
#define REG08_PMID2OUT_OVP_DIS_MASK         0x80
#define REG08_PMID2OUT_OVP_DIS_SHIFT        7
#define REG08_PMID2OUT_OVP_DISABLE          1
#define REG08_PMID2OUT_OVP_ENABLE           0

#define REG08_PMID2OUT_OVP_MASK_MASK        0x10
#define REG08_PMID2OUT_OVP_MASK_SHIFT       4

#define REG08_PMID2OUT_OVP_FLAG_MASK        0x08
#define REG08_PMID2OUT_OVP_FLAG_SHIFT       3

#define REG08_PMID2OUT_OVP_MASK             0x07
#define REG08_PMID2OUT_OVP_SHIFT            0
#define REG08_PMID2OUT_OVP_BASE             200
#define REG08_PMID2OUT_OVP_LSB              100


// REG 0x09     PMID2OUT_UVP
#define SC8565_REG_09                       0x09
#define REG09_PMID2OUT_UVP_DIS_MASK         0x80
#define REG09_PMID2OUT_UVP_DIS_SHIFT        7
#define REG09_PMID2OUT_UVP_DISABLE          1
#define REG09_PMID2OUT_UVP_ENABLE           0

#define REG09_PMID2OUT_UVP_MASK_MASK        0x10
#define REG09_PMID2OUT_UVP_MASK_SHIFT       4

#define REG09_PMID2OUT_UVP_FLAG_MASK        0x08
#define REG09_PMID2OUT_UVP_FLAG_SHIFT       3

#define REG09_PMID2OUT_UVP_MASK             0x07
#define REG09_PMID2OUT_UVP_SHIFT            0
#define REG09_PMID2OUT_UVP_BASE             100
#define REG09_PMID2OUT_UVP_LSB              50


// REG 0x0A    CONVERTER STATE
#define SC8565_REG_0A                       0x0A
#define REG0A_POR_FLAG_MASK                 0x80
#define REG0A_POR_FLAG_SHIFT                7

#define REG0A_ACRB_WPC_STAT_MASK            0x40
#define REG0A_ACRB_WPC_STAT_SHIFT           6

#define REG0A_ACRB_USB_STAT_MASK            0x20
#define REG0A_ACRB_USB_STAT_SHIFT           5

#define REG0A_VBUS_ERRORLO_STAT_MASK        0x10
#define REG0A_VBUS_ERRORLO_STAT_SHIFT       4

#define REG0A_VBUS_ERRORHI_STAT_MASK        0x08
#define REG0A_VBUS_ERRORHI_STAT_SHIFT       3

#define REG0A_QB_ON_STAT_MASK               0x04
#define REG0A_QB_ON_STAT_SHIFT              2

#define REG0A_CP_SWITCHING_STAT_MASK        0x02
#define REG0A_CP_SWITCHING_STAT_SHIFT       1

#define REG0A_PIN_DIAG_FAIL_FLAG_MASK       0x01
#define REG0A_PIN_DIAG_FAIL_FLAG_SHIFT      0


// REG 0x0B    CTRL1
#define SC8565_REG_0B                       0x0B
#define REG0B_CP_EN_MASK                    0x80
#define REG0B_CP_EN_SHIFT                   7
#define REG0B_CP_ENABLE                     1
#define REG0B_CP_DISABLE                    0

#define REG0B_QB_EN_MASK                    0x40
#define REG0B_QB_EN_SHIFT                   6
#define REG0B_QB_ENABLE                     1
#define REG0B_QB_DISABLE                    0

#define REG0B_ACDRV_MANUAL_EN_MASK          0x20
#define REG0B_ACDRV_MANUAL_EN_SHIFT         5
#define REG0B_ACDRV_MANUAL_MODE             1
#define REG0B_ACDRV_AUTO_MODE               0

#define REG0B_WPCGATE_EN_MASK               0x10
#define REG0B_WPCGATE_EN_SHIFT              4
#define REG0B_WPCGATE_ENABLE                1
#define REG0B_WPCGATE_DISABLE               0

#define REG0B_OVPGATE_EN_MASK               0x08
#define REG0B_OVPGATE_EN_SHIFT              3
#define REG0B_OVPGATE_ENABLE                1
#define REG0B_OVPGATE_DISABLE               0

#define REG0B_VBUS_PD_EN_MASK               0x04
#define REG0B_VBUS_PD_EN_SHIFT              2
#define REG0B_VBUS_PD_ENABLE                1
#define REG0B_VBUS_PD_DISABLE               0

#define REG0B_VWPC_PD_EN_MASK               0x02
#define REG0B_VWPC_PD_EN_SHIFT              1
#define REG0B_VWPC_PD_ENABLE                1
#define REG0B_VWPC_PD_DISABLE               0

#define REG0B_VUSB_PD_EN_MASK               0x01
#define REG0B_VUSB_PD_EN_SHIFT              0
#define REG0B_VUSB_PD_ENABLE                1
#define REG0B_VUSB_PD_DISABLE               0

// REG 0x0C    CTRL2
#define SC8565_REG_0C                       0x0C
#define REG0C_FSW_SET_MASK                  0xF8
#define REG0C_FSW_SET_SHIFT                 3
#define REG0C_FSW_SET_BASE                  300
#define REG0C_FSW_SET_LSB                   25

#define REG0C_FREQ_DITHER_MASK              0x02
#define REG0C_FREQ_DITHER_SHIFT             1
#define REG0C_FREQ_DITHER_ENABLE            1
#define REG0C_FREQ_DITHER_DISABLE           0

#define REG0C_ACDRV_HI_EN_MASK              0x01
#define REG0C_ACDRV_HI_EN_SHIFT             0


// REG 0x0D    CTRL3
#define SC8565_REG_0D                       0x0D
#define REG0D_VBUS_INRANGE_DET_DIS_MASK     0x80
#define REG0D_VBUS_INRANGE_DET_DIS_SHIFT    7

#define REG0D_SS_TIMEOUT_MASK               0x38
#define REG0D_SS_TIMEOUT_SHIFT              3
#define REG0D_SS_TIMEOUT_DISABLE            0
#define REG0D_SS_TIMEOUT_320MS              3
#define REG0D_SS_TIMEOUT_1280MS             4
#define REG0D_SS_TIMEOUT_5120MS             5
#define REG0D_SS_TIMEOUT_20480MS            6
#define REG0D_SS_TIMEOUT_81920MS            7

#define REG0D_WD_TIMEOUT_MASK               0x07
#define REG0D_WD_TIMEOUT_SHIFT              0
#define REG0D_WD_TIMEOUT_DISABLE            0
#define REG0D_WD_TIMEOUT_0P2S               1
#define REG0D_WD_TIMEOUT_0P5S               2
#define REG0D_WD_TIMEOUT_1S                 3
#define REG0D_WD_TIMEOUT_5S                 4
#define REG0D_WD_TIMEOUT_30S                5

// REG 0x0E    CTRL4
#define SC8565_REG_0E                       0x0E
#define REG0E_VBAT_OVP_DG_SET_MASK          0x20
#define REG0E_VBAT_OVP_DG_SET_SHIFT         5

#define REG0E_SET_IBAT_SNS_RES_MASK         0x10
#define REG0E_SET_IBAT_SNS_RES_SHIFT        4
#define REG0E_SET_IBAT_SNS_1MHM             0
#define REG0E_SET_IBAT_SNS_2MHM             1

#define REG0E_REG_RST_MASK                  0x08
#define REG0E_REG_RST_SHIFT                 3
#define REG0E_REG_RESET                     1

#define REG0E_MODE_MASK                     0x07
#define REG0E_MODE_SHIFT                    0
#define REG0E_FORWARD_4_1_CHARGER_MODE      0
#define REG0E_FORWARD_2_1_CHARGER_MODE      1
#define REG0E_FORWARD_1_1_CHARGER_MODE      2
#define REG0E_FORWARD_1_1_CHARGER_MODE1     3
#define REG0E_REVERSE_1_4_CONVERTER_MODE    4
#define REG0E_REVERSE_1_2_CONVERTER_MODE    5
#define REG0E_REVERSE_1_1_CONVERTER_MODE    6 
#define REG0E_REVERSE_1_1_CONVERTER_MODE1   7

// REG 0x0F    CTRL5
#define SC8565_REG_0F                       0x0F
#define REG0F_OVPGATE_STAT_MASK             0x80
#define REG0F_OVPGATE_STAT_SHIFT            7

#define REG0F_WPCGATE_STAT_MASK             0x40
#define REG0F_WPCGATE_STAT_SHIFT            6

#define REG0F_TSHUT_DIS_MASK                0x10
#define REG0F_TSHUT_DIS_SHIFT               4

#define REG0F_VWPC_OVP_DIS_MASK             0x08
#define REG0F_VWPC_OVP_DIS_SHIFT            3

#define REG0F_VUSB_OVP_DIS_MASK             0x04
#define REG0F_VUSB_OVP_DIS_SHIFT            2

#define REG0F_VBUS_OVP_DIS_MASK             0x02
#define REG0F_VBUS_OVP_DIS_SHIFT            1

#define REG0F_VOUT_OVP_DIS_MASK             0x01
#define REG0F_VOUT_OVP_DIS_SHIFT            0


// REG 0x10    INT_STAT
#define SC8565_REG_10                       0x10
#define REG10_VOUT_OK_SW_REGN_STAT_MASK     0x40
#define REG10_VOUT_OK_SW_REGN_STAT_SHIFT    6

#define REG10_VOUT_OK_CHG_STAT_MASK         0x10
#define REG10_VOUT_OK_CHG_STAT_SHIFT        4

#define REG10_VOUT_INSERT_STAT_MASK         0x08
#define REG10_VOUT_INSERT_STAT_SHIFT        3

#define REG10_VBUS_PRESENT_STAT_MASK        0x04
#define REG10_VBUS_PRESENT_STAT_SHIFT       2

#define REG10_VWPC_INSERT_STAT_MASK         0x02
#define REG10_VWPC_INSERT_STAT_SHIFT        1

#define REG10_VUSB_INSERT_STAT_MASK         0x01
#define REG10_VUSB_INSERT_STAT_SHIFT        0


// REG 0x11    INT_FLAG
#define SC8565_REG_11                       0x11
#define REG11_VOUT_OK_SW_REGN_FLAG_MASK     0x40
#define REG11_VOUT_OK_SW_REGN_FLAG_SHIFT    6

#define REG11_VOUT_OK_CHG_FLAG_MASK         0x10
#define REG11_VOUT_OK_CHG_FLAG_SHIFT        4

#define REG11_VOUT_INSERT_FLAG_MASK         0x08
#define REG11_VOUT_INSERT_FLAG_SHIFT        3

#define REG11_VBUS_PRESENT_FLAG_MASK        0x04
#define REG11_VBUS_PRESENT_FLAG_SHIFT       2

#define REG11_VWPC_INSERT_FLAG_MASK         0x02
#define REG11_VWPC_INSERT_FLAG_SHIFT        1

#define REG11_VUSB_INSERT_FLAG_MASK         0x01
#define REG11_VUSB_INSERT_FLAG_SHIFT        0


// REG 0x12    INT_MASK
#define SC8565_REG_12                       0x12
#define REG12_VOUT_OK_SW_REGN_MASK_MASK     0x40
#define REG12_VOUT_OK_SW_REGN_MASK_SHIFT    6

#define REG12_VOUT_OK_CHG_MASK_MASK         0x10
#define REG12_VOUT_OK_CHG_MASK_SHIFT        4

#define REG12_VOUT_INSERT_MASK_MASK         0x08
#define REG12_VOUT_INSERT_MASK_SHIFT        3

#define REG12_VBUS_PRESENT_MASK_MASK        0x04
#define REG12_VBUS_PRESENT_MASK_SHIFT       2

#define REG12_VWPC_INSERT_MASK_MASK         0x02
#define REG12_VWPC_INSERT_MASK_SHIFT        1

#define REG12_VUSB_INSERT_MASK_MASK         0x01
#define REG12_VUSB_INSERT_MASK_SHIFT        0


// REG 0x13    FLT_FLAG
#define SC8565_REG_13                       0x13
#define REG13_TSHUT_FLAG_MASK               0x40
#define REG13_TSHUT_FLAG_SHIFT              6

#define REG13_SS_TIMEOUT_FLAG_MASK          0x20
#define REG13_SS_TIMEOUT_FLAG_SHIFT         5

#define REG13_WD_TIMEOUT_FLAG_MASK          0x10
#define REG13_WD_TIMEOUT_FLAG_SHIFT         4

#define REG13_CONV_OCP_FLAG_MASK            0x08
#define REG13_CONV_OCP_FLAG_SHIFT           3

#define REG13_SS_FAIL_FLAG_MASK             0x04
#define REG13_SS_FAIL_FLAG_SHIFT            2

#define REG13_VBUS_OVP_FLAG_MASK            0x02
#define REG13_VBUS_OVP_FLAG_SHIFT           1

#define REG13_VOUT_OVP_FLAG_MASK            0x01
#define REG13_VOUT_OVP_FLAG_SHIFT           0


// REG 0x14    FLT_MASK
#define SC8565_REG_14                       0x14
#define REG14_TSHUT_MASK_MASK               0x40
#define REG14_TSHUT_MASK_SHIFT              6

#define REG14_SS_TIMEOUT_MASK_MASK          0x20
#define REG14_SS_TIMEOUT_MASK_SHIFT         5

#define REG14_WD_TIMEOUT_MASK_MASK          0x10
#define REG14_WD_TIMEOUT_MASK_SHIFT         4

#define REG14_CONV_OCP_MASK_MASK            0x08
#define REG14_CONV_OCP_MASK_SHIFT           3

#define REG14_SS_FAIL_MASK_MASK             0x04
#define REG14_SS_FAIL_MASK_SHIFT            2

#define REG14_VBUS_OVP_MASK_MASK            0x02
#define REG14_VBUS_OVP_MASK_SHIFT           1

#define REG14_VOUT_OVP_MASK_MASK            0x01
#define REG14_VOUT_OVP_MASK_SHIFT           0


/* ADC */
// REG 0x15    ADC_CTRL
#define SC8565_REG_15                       0x15
#define REG15_ADC_EN_MASK                   0x80
#define REG15_ADC_EN_SHIFT                  7
#define REG15_ADC_ENABLE                    1
#define REG15_ADC_DISABLE                   0

#define REG15_ADC_RATE_MASK                 0x40
#define REG15_ADC_RATE_SHIFT                6

#define REG15_ADC_DONE_STAT_MASK            0x20
#define REG15_ADC_DONE_STAT_SHIFT           5

#define REG15_ADC_DONE_FLAG_MASK            0x10
#define REG15_ADC_DONE_FLAG_SHIFT           4

#define REG15_ADC_DONE_MASK_MASK            0x08
#define REG15_ADC_DONE_MASK_SHIFT           3

#define REG15_VBAT2_ADC_EN_MASK             0x02
#define REG15_VBAT2_ADC_EN_SHIFT            1

#define REG15_IBUS_ADC_DIS_MASK             0x01
#define REG15_IBUS_ADC_DIS_SHIFT            0


// REG 0x16    ADC_FN_DISABLE
#define SC8565_REG_16                       0x16
#define REG16_VBUS_ADC_DIS_MASK             0x80
#define REG16_VBUS_ADC_DIS_SHIFT            7

#define REG16_VUSB_ADC_DIS_MASK             0x40
#define REG16_VUSB_ADC_DIS_SHIFT            6

#define REG16_VWPC_ADC_DIS_MASK             0x20
#define REG16_VWPC_ADC_DIS_SHIFT            5

#define REG16_VOUT_ADC_DIS_MASK             0x10
#define REG16_VOUT_ADC_DIS_SHIFT            4

#define REG16_VBAT_ADC_DIS_MASK             0x08
#define REG16_VBAT_ADC_DIS_SHIFT            3

#define REG16_IBAT_ADC_DIS_MASK             0x04
#define REG16_IBAT_ADC_DIS_SHIFT            2

#define REG16_TDIE_ADC_DIS_MASK             0x01
#define REG16_TDIE_ADC_DIS_SHIFT            0


// REG 0x17    IBUS_ADC1
#define SC8565_REG_17                       0x17
#define REG17_IBUS_ADC_MASK                 0x0F
#define REG17_IBUS_ADC_SHIFT                0


// REG 0x18    IBUS_ADC0
#define SC8565_REG_18                       0x18
#define REG18_IBUS_ADC_MASK                 0xFF
#define REG18_IBUS_ADC_SHIFT                0
#define REG18_IBUS_ADC_LSB                  15625 / 10000


// REG 0x19    VBUS_ADC1
#define SC8565_REG_19                       0x19
#define REG19_VBUS_ADC_MASK                 0x0F
#define REG19_VBUS_ADC_SHIFT                0


// REG 0x1A    VBUS_ADC0
#define SC8565_REG_1A                       0x1A
#define REG1A_VBUS_ADC_MASK                 0xFF
#define REG1A_VBUS_ADC_SHIFT                0
#define REG1A_VBUS_ADC_LSB                  625 / 100


// REG 0x1B    VUSB_ADC1
#define SC8565_REG_1B                       0x1B
#define REG1B_VUSB_ADC_MASK                 0x0F
#define REG1B_VUSB_ADC_SHIFT                0


// REG 0x1C    VUSB_ADC0
#define SC8565_REG_1C                       0x1C
#define REG1C_VUSB_ADC_MASK                 0xFF
#define REG1C_VUSB_ADC_SHIFT                0
#define REG1C_VUSB_ADC_LSB                  625 / 100


// REG 0x1D    VWPC_ADC1
#define SC8565_REG_1D                       0x1D
#define REG1D_VWPC_ADC_MASK                 0x0F
#define REG1D_VWPC_ADC_SHIFT                0


// REG 0x1E    VWPC_ADC0
#define SC8565_REG_1E                       0x1E
#define REG1E_VWPC_ADC_MASK                 0xFF
#define REG1E_VWPC_ADC_SHIFT                0
#define REG1E_VWPC_ADC_LSB                  625 / 100


// REG 0x1F    VOUT_ADC1
#define SC8565_REG_1F                       0x1F
#define REG1F_VOUT_ADC_MASK                 0x0F
#define REG1F_VOUT_ADC_SHIFT                0


// REG 0x20    VOUT_ADC0
#define SC8565_REG_20                       0x20
#define REG20_VOUT_ADC_MASK                 0xFF
#define REG20_VOUT_ADC_SHIFT                0
#define REG20_VOUT_ADC_LSB                  125 / 100


// REG 0x21    VBAT1_ADC1
#define SC8565_REG_21                       0x21
#define REG21_VBAT1_ADC_MASK                0x0F
#define REG21_VBAT1_ADC_SHIFT               0


// REG 0x22    VBAT1_ADC0
#define SC8565_REG_22                       0x22
#define REG22_VBAT_ADC_MASK                 0xFF
#define REG22_VBAT_ADC_SHIFT                0
#define REG22_VBAT_ADC_LSB                  125 / 100


// REG 0x23    IBAT_ADC1
#define SC8565_REG_23                       0x23
#define REG23_IBAT_ADC_MASK                 0x0F
#define REG23_IBAT_ADC_SHIFT                0


// REG 0x24    IBAT_ADC0
#define SC8565_REG_24                       0x24
#define REG24_IBAT_ADC_MASK                 0xFF
#define REG24_IBAT_ADC_SHIFT                0
#define REG24_IBAT_ADC_LSB                  375 / 100


// REG 0x25    RESERVED
#define SC8565_REG_25                       0x25


// REG 0x26    RESERVED
#define SC8565_REG_26                       0x26


// REG 0x27    TDIE_ADC1
#define SC8565_REG_27                       0x27
#define REG27_TDIE_ADC_MASK                 0x01
#define REG27_TDIE_ADC_SHIFT                0


// REG 0x28    TDIE_ADC0
#define SC8565_REG_28                       0x28
#define REG28_TDIE_ADC_MASK                 0xFF
#define REG28_TDIE_ADC_SHIFT                0
#define REG27_TDIE_ADC_LSB                  5 / 10


// REG 0x29    RESERVED
#define SC8565_REG_29                       0x29


/* DPDM_CTRL */
// REG 0x2A    DPDM_CTRL1
#define SC8565_REG_2A                       0x2A
#define REG2A_DM_500K_PD_EN_MASK            0x80
#define REG2A_DM_500K_PD_EN_SHIFT           7

#define REG2A_DP_500K_PD_EN_MASK            0x40
#define REG2A_DP_500K_PD_EN_SHIFT           6

#define REG2A_DM_20K_PD_EN_MASK             0x20
#define REG2A_DM_20K_PD_EN_SHIFT            5

#define REG2A_DP_20K_PD_EN_MASK             0x10
#define REG2A_DP_20K_PD_EN_SHIFT            4

#define REG2A_DM_SINK_EN_MASK               0x08
#define REG2A_DM_SINK_EN_SHIFT              3

#define REG2A_DP_SINK_EN_MASK               0x04
#define REG2A_DP_SINK_EN_SHIFT              2

#define REG2A_DP_SRC_10UA_MASK              0x02
#define REG2A_DP_SRC_10UA_SHIFT             

#define REG2A_DPDM_EN_MASK                  0x01
#define REG2A_DPDM_EN_SHIFT                 0


// REG 0x2B    DPDM_CTRL2
#define SC8565_REG_2B                       0x2B
#define REG2B_DM_3P3_EN_MASK                0x80
#define REG2B_DM_3P3_EN_SHIFT               7

#define REG2B_DPDM_OVP_DIS_MASK             0x40
#define REG2B_DPDM_OVP_DIS_SHIFT            6

#define REG2B_DM_BUF_MASK                   0x30
#define REG2B_DM_BUF_SHIFT                  4

#define REG2B_DP_BUF_MASK                   0x0C
#define REG2B_DP_BUF_SHIFT                  2

#define REG2B_DM_BUFF_EN_MASK               0x02
#define REG2B_DM_BUFF_EN_SHIFT              1

#define REG2B_DP_BUFF_EN_MASK               0x01
#define REG2B_DP_BUFF_EN_SHIFT              0


// REG 0x2C    DPDM_STAT
#define SC8565_REG_2C                       0x2C
#define REG2C_VDM_RD_MASK                   0x38
#define REG2C_VDM_RD_SHIFT                  3

#define REG2C_VDP_RD_MASK                   0x07
#define REG2C_VDP_RD_SHIFT                  0


// REG 0x2D    DPDM_FLAG_MASK
#define SC8565_REG_2D                       0x2D
#define REG2D_DM_LOW_MASK_MASK              0x40
#define REG2D_DM_LOW_MASK_SHIFT             6

#define REG2D_DM_LOW_FLAG_MASK              0x20
#define REG2D_DM_LOW_FLAG_SHIFT             5

#define REG2D_DP_LOW_MASK_MASK              0x10
#define REG2D_DP_LOW_MASK_SHIFT             4

#define REG2D_DP_LOW_FLAG_MASK              0x08
#define REG2D_DP_LOW_FLAG_SHIFT             3

#define REG2D_DPDM_OVP_MASK_MASK            0x04
#define REG2D_DPDM_OVP_MASK_SHIFT           2

#define REG2D_DPDM_OVP_FLAG_MASK            0x02
#define REG2D_DPDM_OVP_FLAG_SHIFT           1

#define REG2D_DPDM_OVP_STAT_MASK            0x01
#define REG2D_DPDM_OVP_STAT_SHIFT           0


// REG 0x2E    SCP_CTRL
#define SC8565_REG_2E                       0x2E
#define REG2E_SCP_EN_MASK                   0x80
#define REG2E_SCP_EN_SHIFT                  7

#define REG2E_SCP_SOFT_RST_MASK             0x40
#define REG2E_SCP_SOFT_RST_SHIFT            6

#define REG2E_TX_CRC_DIS_MASK               0x20
#define REG2E_TX_CRC_DIS_SHIFT              5

#define REG2E_CLR_TX_FIFO_MASK              0x08
#define REG2E_CLR_TX_FIFO_SHIFT             3

#define REG2E_CLR_RX_FIFO_MASK              0x04
#define REG2E_CLR_RX_FIFO_SHIFT             2

#define REG2E_SND_RST_TRANS_MASK            0x02
#define REG2E_SND_RST_TRANS_SHIFT           1

#define REG2E_SND_TRANS_MASK                0x01
#define REG2E_SND_TRANS_SHIFT               0


// REG 0x2F    SCP_WDATA
#define SC8565_REG_2F                       0x2F
#define REG2F_TX_DATA_MASK                  0xFF
#define REG2F_TX_DATA_SHIFT                 0


// REG 0x30    SCP_RDATA
#define SC8565_REG_30                       0x30
#define REG30_RX_DATA_MASK                  0xFF
#define REG30_RX_DATA_SHIFT                 0


// REG 0x31    SCP_FIFO_STAT
#define SC8565_REG_31                       0x31
#define REG31_TX_FIFO_CNT_STAT_MASK         0xF0
#define REG31_TX_FIFO_CNT_STAT_SHIFT        4

#define REG31_RX_FIFO_CNT_STAT_MASK         0x0F
#define REG31_RX_FIFO_CNT_STAT_SHIFT        0


// REG 0x32    SCP_STAT
#define SC8565_REG_32                       0x32
#define REG32_NO_FIRST_SLAVE_PING_STAT_MASK 0x80
#define REG32_NO_FIRST_SLAVE_PING_STAT_SHIFT 7

#define REG32_NO_MED_SLAVE_PING_STAT_MASK   0x40
#define REG32_NO_MED_SLAVE_PING_STAT_SHIFT  6

#define REG32_NO_LAST_SLAVE_PING_STAT_MASK  0x20
#define REG32_NO_LAST_SLAVE_PING_STAT_SHIFT 5

#define REG32_NO_RX_PKT_STAT_MASK           0x10
#define REG32_NO_RX_PKT_STAT_SHIFT          4

#define REG32_NO_TX_PKT_STAT_MASK           0x08
#define REG32_NO_TX_PKT_STAT_SHIFT          3

#define REG32_WDT_EXPIRED_STAT_MASK         0x04
#define REG32_WDT_EXPIRED_STAT_SHIFT        2

#define REG32_RX_CRC_ERR_STAT_MASK          0x02
#define REG32_RX_CRC_ERR_STAT_SHIFT         1

#define REG32_RX_PAR_ERR_STAT_MASK          0x01
#define REG32_RX_PAR_ERR_STAT_SHIFT         0


// REG 0x33    SCP_FLAG_MASK
#define SC8565_REG_33                       0x33
#define REG33_DM_BUSY_STAT_MASK             0x80
#define REG33_DM_BUSY_STAT_SHIFT            

#define REG33_TX_FIFO_EMPTY_MASK_MASK       0x20
#define REG33_TX_FIFO_EMPTY_MASK_SHIFT      5

#define REG33_TX_FIFO_EMPTY_FLAG_MASK       0x10
#define REG33_TX_FIFO_EMPTY_FLAG_SHIFT      4

#define REG33_RX_FIFO_FULL_MASK_MASK        0x08
#define REG33_RX_FIFO_FULL_MASK_SHIFT       3

#define REG33_RX_FIFO_FULL_FLAG_MASK        0x04
#define REG33_RX_FIFO_FULL_FLAG_SHIFT       2

#define REG33_TRANS_DONE_MASK_MASK          0x02
#define REG33_TRANS_DONE_MASK_SHIFT         1

#define REG33_TRANS_DONE_FLAG_MASK          0x01
#define REG33_TRANS_DONE_FLAG_SHIFT         0


// REG 0x40    CTRL6
#define SC8565_REG_40                       0x40
#define REG40_IBUS_RCP_DIS_MASK             0x80
#define REG40_IBUS_RCP_DIS_SHIFT            7

#define REG40_IBUS_LOW_RCP_BYPASS_EN_MASK   0x40
#define REG40_IBUS_LOW_RCP_BYPASS_EN_SHIFT  6

#define REG40_IBUS_RCP_MASK_MASK            0x20
#define REG40_IBUS_RCP_MASK_SHIFT           5

#define REG40_VWPC_REMOVE_MASK_MASK         0x10
#define REG40_VWPC_REMOVE_MASK_SHIFT        4

#define REG40_VUSB_REMOVE_MASK_MASK         0x08
#define REG40_VUSB_REMOVE_MASK_SHIFT        3

#define REG40_IBUS_RCP_FLAG_MASK            0x04
#define REG40_IBUS_RCP_FLAG_SHIFT           2

#define REG40_VWPC_REMOVE_FLAG_MASK         0x02
#define REG40_VWPC_REMOVE_FLAG_SHIFT        1

#define REG40_VUSB_REMOVE_FLAG_MASK         0x01
#define REG40_VUSB_REMOVE_FLAG_SHIFT        0


// REG 0x6E    DEVICE_ID
#define SC8565_REG_6E                       0x6E
#define REG6E_DEVICE_ID_MASK                0xFF
#define REG6E_DEVICE_ID_SHIFT               0
#define REG6E_DEVICE_ID                     0x81

// REG 0x7C
#define SC8565_REG_7C                       0x7C

// REG 0x7F    CTRL7
#define SC8565_REG_7F                       0x7F
#define REG7F_IBATSNS_HS_EN_MASK            0x04
#define REG7F_IBATSNS_HS_EN_SHIFT           2

#endif
