// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#ifndef __CPS4021_WIRELESS__H__
#define __CPS4021_WIRELESS__H__
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/power_supply.h>
#include "tc_charger_class.h"
#include "wireless_class.h"
#include "wireless_manager.h"

#define CPS_WLS_FAIL		-1
#define CPS_WLS_SUCCESS		0
#define PGM_BUFFER0_1		0x50
#define PGM_ERASER_0		0x60
#define PGM_ERASER_1		0x70
#define PGM_WR_FLAG			0x80
#define SYS_RESET			0xE0
#define PGM_BUFFER0			0x10
#define PGM_BUFFER1			0x20
#define RUNNING				0x66
#define PASS				0x55
#define FAIL				0xAA
#define ILLEGAL				0x40
#define CACL_CRC_APP		0x90
#define CACL_CRC_TEST		0xB0
/*****************************************************************************
 *  CMD REG
 ****************************************************************************/
#define ADDR_BUFFER0        0x0F00 //0x20000f00
#define ADDR_BUFFER1        0x1300 //0x20001300
#define ADDR_CMD            0x17F0 //0x200017f0
#define ADDR_FLAG           0x17F4 //0x200017f4
#define ADDR_BUF_SIZE       0x17F8 //0x200017f8
/*------------------------ REGISTER -------------------------*/
#define REG_CHIPID					0x0000
#define REG_MINORVER				0x0008
#define REG_MAJORVER				0x0009
#define REG_SYSMODE					0x000A
#define REG_CRC_VAL					0x000c
#define REG_INTEN					0x0020
#define REG_INTFLAG					0x0024
#define REG_INTCLR					0x0028
#define REG_CMD						0x002C
#define REG_FUNC_EN					0x0030
//RX CPS4021
#define REG_RX_PPP					0x0050
#define REG_RX_BC					0x0070
#define REG_RX_VOUT_SET				0x0090
#define REG_RX_FC_VPA_VOLTAGE		0x0092
#define REG_RX_FC_MLDO_VOLTAGE		0x0094
#define REG_RX_FC_BOOST_MODE_EN		0x0096
#define REG_RX_FC_DELTA				0x0098
#define REG_RX_WATCHDOG				0x009A
#define REG_RX_DROP_VOL_MIN			0x009C
#define REG_RX_DROP_VOL_MAX			0x009E
#define REG_RX_DROP_CUR_MIN			0x00A0
#define REG_RX_DROP_CUR_MAX			0x00A2
#define REG_RX_DUMMY_LOAD_OP_MOD_VAL		0x00A8
#define REG_RX_DUMMY_LOAD_MOD_VAL			0x00A9
#define REG_RX_SW_OCP_TH					0x00B2
#define REG_RX_EPT_VAL						0x00BE
#define REG_RX_MAX_POWER					0x00BF
#define REG_RX_RP_CURRENT_0					0x00C0
#define REG_RX_RP_CURRENT_1					0x00C1
#define REG_RX_RP_CURRENT_2					0x00C2
#define REG_RX_RP_CURRENT_3					0x00C3
#define REG_RX_RP_CURRENT_4					0x00C4
#define REG_RX_RP_CURRENT_5					0x00C5
#define REG_RX_RP_CURRENT_6					0x00C6
#define REG_RX_FOD_C0_GAIN					0x00C7
#define REG_RX_FOD_C0_OFFSET				0x00C8
#define REG_RX_FOD_C1_GAIN					0x00C9
#define REG_RX_FOD_C1_OFFSET				0x00CA
#define REG_RX_FOD_C2_GAIN					0x00CB
#define REG_RX_FOD_C2_OFFSET				0x00CC
#define REG_RX_FOD_C3_GAIN					0x00CD
#define REG_RX_FOD_C3_OFFSET				0x00CE
#define REG_RX_FOD_C4_GAIN					0x00CF
#define REG_RX_FOD_C4_OFFSET				0x00D0
#define REG_RX_FOD_C5_GAIN					0x00D1
#define REG_RX_FOD_C5_OFFSET				0x00D2
#define REG_RX_FOD_C6_GAIN					0x00D3
#define REG_RX_FOD_C6_OFFSET				0x00D4
#define REG_RX_FOD_C7_GAIN					0x00D5
#define REG_RX_FOD_C7_OFFSET				0x00D6
#define REG_RX_ADC_VRECT					0x0104
#define REG_RX_ADC_MLDO_DROP				0x0106
#define REG_RX_ADC_IRECT					0x0108
#define REG_RX_ADC_VOUT						0x010A
#define REG_RX_ADC_TMP_DIE					0x010C
#define REG_RX_FOP_VAL						0x010E
#define REG_RX_SS_VAL						0x0114
#define REG_RX_CE_VAL						0x0116
#define REG_RX_RP_VAL						0x0118
#define REG_RX_EPT_CODE						0x011A
#define REG_RX_STATUS						0x011C
#define REG_RX_NEGO_POWER					0x011D
#define REG_RX_POWER_MODE					0x011E
//TX CPS4021
#define REG_TX_BC					0x0050
#define REG_TX_PPP					0x0070
#define REG_TX_OCP_TH				0x0090
#define REG_TX_OVP_TH				0x0094
#define REG_TX_PING_OCP_TH			0x0096
#define REG_TX_CC_TH				0x0098
#define REG_TX_DUTY_MIN				0x009C
#define REG_TX_DUTY_MAX				0x009D
#define REG_TX_FOP_MIN				0x009E
#define REG_TX_FOP_MAX				0x009F
#define REG_TX_PING_FREQ			0x00A0
#define REG_TX_PING_DUTY			0x00A1
#define REG_TX_PING_TIME			0x00A2
#define REG_TX_PING_INTERVAL		0x00A4
#define REG_TX_MAX_POWER			0x00A6
#define REG_TX_FOD_CNT_TH			0x00A7
#define REG_TX_FOD_TH				0x00A8
#define REG_TX_REF_Q_FACTOR			0x00AA
#define REG_TX_H_F_B_FREQ			0x00AC
#define REG_TX_F_H_B_FREQ			0x00AD
#define REG_TX_H_F_B_DUTY			0x00AE
#define REG_TX_F_H_B_DUTY			0x00AF
#define REG_TX_QF_CHG_VOL			0x00B0
#define REG_TX_ADC_VIN				0x0100
#define REG_TX_ADC_VPA				0x0102
#define REG_TX_ADC_IPA				0x0104
#define REG_TX_ADC_TEMP_DIE			0x0106
#define REG_TX_FOP_VAL				0x0108
#define REG_TX_DUTY_VAL				0x010A
#define REG_TX_EPT_VAL				0x010B
#define REG_TX_SSP_VAL				0x010C
#define REG_TX_CE_VAL				0x010D
#define REG_TX_RP_VAL				0x010E
#define REG_TX_EPT_RST				0x0110
#define REG_TX_Q_FACTOR_VAL			0x0114
#define REG_TX_RX_POWER				0x0116
#define REG_TX_TX_POWER				0x0118
#define REG_TX_PLOSS				0x011A
#define REG_TX_PLOSS_TH				0x011C
#define REG_TX_FOD_TRIG_CNT			0x011E

struct cps4021_dev;
// system mode
typedef enum {
	UNKNOWN_MODE = 0,
	BCK_PWR_MODE,
	TX_MODE,
	RX_MODE,
} _CUST_CMD_E;
//cmd data
typedef enum {
	TXCMD_CRCCHECK				= BIT(0),
	TXCMD_SEND_FSK_DATA			= BIT(2),
	TXCMD_TO_TX_MODE			= BIT(3),
	TXCMD_TO_BP_MODE			= BIT(4),
	TXCMD_TO_FULL_BR			= BIT(5),
	TXCMD_TO_HALF_BR			= BIT(6),
} _TX_CUST_CMD_E;

typedef enum {
	RXCMD_SEND_DATA				= BIT(0),
	RXCMD_FASTCHARGER			= BIT(1),
	RXCMD_SEND_EPT				= BIT(2),
	RXCMD_BOOSTER_EN			= BIT(4),
	RXCMD_BOOSTER_DIS			= BIT(5),
	RXCMD_LDOON_INIT			= BIT(6),
	RXCMD_EPPCAL_INIT			= BIT(7),
} _RX_CUST_CMD_E;
//TX func data
typedef enum {
	TXFUNC_PING_EN				= BIT(0),
	TXFUNC_FOD_EN				= BIT(1),
	TXFUNC_24BIT_RP_EN			= BIT(2),
	TXFUNC_Q_VALUE_EN			= BIT(3),
	TXFUNC_LP_EN				= BIT(4),
	TXFUNC_BR_CRTL_EN			= BIT(5),
	TXFUNC_QI_PRO_SEL 			= BIT(6),
} _TX_CUST_FUNC_E;

enum SYS_STATUS {
	RX_PWRON					= BIT(0),
	RX_LDOON					= BIT(1),
	RX_EPP						= BIT(2),
};

/* -------------- INTFLAG ------------*/
typedef enum {
	INT_RX_POWER_ON			= BIT(0),
	INT_RX_READY			= BIT(1),
	INT_RX_LDO_OFF			= BIT(2),
	INT_RX_LDO_ON			= BIT(3),
	INT_RX_HEAVY_LOAD		= BIT(5),
	INT_RX_LIGHT_LOAD		= BIT(6),
	INT_RX_FSK_TO			= BIT(7),
	INT_RX_FSK_PKT			= BIT(8),
	INT_RX_VRECT_OVP		= BIT(9),
	INT_RX_VRECT_OVP_TO		= BIT(10),
	INT_RX_VRECT_BVP		= BIT(11),
	INT_RX_OTP				= BIT(12),
	INT_RX_HTP				= BIT(13),
	INT_RX_LDOOCP			= BIT(14),
	INT_RX_LDOHOCP			= BIT(15),
	INT_RX_LDOOPP			= BIT(17),
	INT_RX_LDOUVP			= BIT(18),
	INT_RX_LDOOVP			= BIT(19),
	INT_RX_AC_LOSS			= BIT(20),
	INT_RX_SR_SW_F			= BIT(22),
	INT_RX_SR_SW_S			= BIT(23),
	INT_RX_START_OV			= BIT(26),
	INT_RX_EPP_CALI			= BIT(27),
	INT_RX_SR_BR_H2F		= BIT(28),
	/* declaration for tx */
	INT_TX_INIT_DONE		= BIT(0),
	INT_TX_PING				= BIT(1),
	INT_TX_SSP				= BIT(2),
	INT_TX_IDP				= BIT(3),
	INT_TX_CFGP				= BIT(4),
	INT_TX_PVT_ASK			= BIT(5),
	INT_TX_EPT				= BIT(6),
	INT_TX_AC_DET			= BIT(7),
	INT_TX_HTP				= BIT(8),
	INT_TX_BR_H_T_F			= BIT(9),
	INT_TX_BR_F_T_H			= BIT(10),
	INT_TX_LP_END			= BIT(11),
	PT_INT					= BIT(12),
} _INT_TYPE_E;

struct parameter {
	u32 drop_vol;
	u16 q_factor;
	u16 tx_ping_ocp_th;
	u8 tx_fop_min;
	u8 tx_fop_max;
	u16 tx_fod_th;
};

struct irq_map_desc {
	const char *name;
	int (*hdlr)(struct cps4021_dev *chip);
	u32 stat_mask;
};

struct cps4021_func {
	int (*read)(struct cps4021_dev *dev, u32 reg, u8 *val);
	int (*write)(struct cps4021_dev *dev, u32 reg, u8 val);
	int (*read_buf)(struct cps4021_dev *dev, u32 reg, u8 *buf, u32 size);
	int (*write_buf)(struct cps4021_dev *dev, u32 reg, u8 *buf, u32 size);
};

struct cps4021_dev {
	struct wireless_charger *wl_chg;
	struct wls_hw_info *hw_info;
	struct i2c_client *client;
	struct regmap *regmap;
	struct mutex notify_lock;
	struct device *dev;
	struct cps4021_func bus;
	struct mutex i2c_lock;
	int pg_irq;
	int dev_irq;
	int wired_irq;
	bool site;
	bool pg;
	bool ldo_on;
	bool tx_mode_en;
	struct gpio_desc *pg_gpio;
	struct gpio_desc *irq_gpio;
	struct gpio_desc *wired_gpio;
	struct gpio_desc *dv_vctrl_gpio;
	struct gpio_desc *sleep_gpio;
	struct gpio_desc *ovp_ctrl_gpio;
	struct gpio_desc *protocol_gpio;
	struct parameter par;
	u8 protocol;
};

#endif
