// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#ifndef __MT5786_H__
#define __MT5786_H__

#include "mt5786_boot.h"
#include "tc_charger_class.h"
#include "wireless_class.h"
#include "wireless_manager.h"

#define MT5786_HWCHIP_ID 		0x5786

#define MTP_BLOCK_SIZE			256
#define SRAM_PAGE_SIZE			128

#define MT5786_KEY					0x57
#define MT5786_WDG_DISABLE		0x95
#define MT5786_WDG_ENABLE			0x59
#define MT5786_WDT_INTFALG			0x03

#define PGM_STATUS_READY			0x00
#define PGM_STATUS_WRITE			0x01 
#define PGM_STATUS_PROGOK			0x02 
#define PGM_STATUS_ERRCS			0x04 
#define PGM_STATUS_READ			0x08 
#define PGM_STATUS_WMTP			0x10 
#define PGM_STATUS_WNVR			0x10 
#define PGM_STATUS_ERRPGM			0x20
#define PGM_STATUS_DONE			0x80

#define MT5786_SYSMODE_MASK		0x0007
#define MT5786_SYSMODE_BRIDGE_MODE_MASK		0x0020

#define MT5786_MTP_CRC_HIGH 	0x09
#define MT5786_MTP_CRC_LOW 	0x08
#define MT5786_MTP_LEN_HIGH 	0x0d
#define MT5786_MTP_LEN_LOW 	0x0c

struct mt5786_dev;

enum mt5786_rx_register {
	MT5786_CHIP_ID 						= 0x00,
	MT5786_MAJOR_VER					= 0x08,
	MT5786_MINOR_VER					= 0x0c,
	MT5786_RX_CRC_VAL					= 0x10,
	MT5786_RX_CRC_LEN					= 0x12,
	MT5786_RX_CRC_CMD					= 0x14,
	MT5786_RX_CRC_RESULT				= 0x15,
	MT5786_RX_SYS_MODE					= 0x18,
	MT5786_RX_INTFLAG					= 0x20,
	MT5786_RX_INTCLR					= 0x24,
	MT5786_RX_CTRL 						= 0x28,
	MT5786_RX_CMD						= 0x2c,
	MT5786_RX_GET_IOUT					= 0x54,
	MT5786_RX_GET_VOUT					= 0x56,
	MT5786_RX_GET_VRECT					= 0x58,
	MT5786_RX_GET_TEMP					= 0x5a,
	MT5786_RX_GET_CEP					= 0x73,
	MT5786_PROTOCOL_TYPE 				= 0x85,
	MT5786_RX_LDO_OFF_CAUSE				= 0xa8,
	MT5786_RX_VRECT_CURVE_SET			= 0xae,
	MT5786_RX_ASK_CAP					= 0xbb,
	MT5786_RX_SET_MAX_POWER				= 0xbc,
	MT5786_RX_SET_VOUT					= 0xbe,
	MT5786_RX_ASK_BUFF					= 0xcb,
	MT5786_RX_FSK_BUFF					= 0xe1,
	MT5786_RX_MAX_POWER					= 0xee,
	MT5786_RX_NEG_POWER					= 0x16e,
	MT5786_RX_EPP_FOD_ISECTION0 		= 0x195,
	MT5786_RX_PGM_STATUS_ADDR			= 0x1000,
	MT5786_RX_PGM_ADDR_ADDR				= 0x1002,
	MT5786_RX_PGM_LENGTH_ADDR			= 0x1004,
	MT5786_RX_PGM_CHECKSUM_ADDR			= 0x1006,
	MT5786_RX_PGM_DATA_ADDR				= 0x1008,
	MT5786_RX_M0_CTRL_REG				= 0x5200,
	MT5786_RX_CODE_REMAP_REG			= 0x5208,
	MT5786_RX_SRAM_REMAP_REG			= 0x5218,
	MT5786_RX_SYS_KEY_REG				= 0x5244,
	MT5786_RX_PMU_WDGEN_REG				= 0x5808,
	MT5786_RX_PMU_FLAG_REG				= 0x5800,
};

enum mt5786_sysmode {
	MT5786_SYSMODE_UNKNOWN = 0,
	MT5786_SYSMODE_SBY,
	MT5786_SYSMODE_RX,
	MT5786_SYSMODE_TX,             
};

enum mt5786_int_mask {
	MT5786_INT_RX_SBY_READY			= BIT(0),
	MT5786_INT_RX_RX_READY			= BIT(1),
	MT5786_INT_RX_TX_READY			= BIT(2),
	MT5786_INT_RX_SS_READY			= BIT(4),
	MT5786_INT_RX_MLDO_ON			= BIT(5),
	MT5786_INT_RX_MLDO_OFF			= BIT(6),
	MT5786_INT_RX_PPP_ASK_SEND		= BIT(7),
	MT5786_INT_RX_PPP_FSK_RCV		= BIT(8),
	MT5786_INT_RX_PPP_FSK_TO			= BIT(9),
	MT5786_INT_RX_AC_MISSING			= BIT(10),
	MT5786_INT_RX_FC_OK				= BIT(11),
	MT5786_INT_RX_FC_FAILED			= BIT(12),
	MT5786_INT_RX_SV_OK				= BIT(13),
	MT5786_INT_RX_SV_FAILED			= BIT(14),
	MT5786_INT_RX_OPP_L0				= BIT(15),
	MT5786_INT_RX_OPP_L1				= BIT(16),
	MT5786_INT_RX_VRECT_OVP0			= BIT(17),
	MT5786_INT_RX_VRECT_OVP1			= BIT(18),
	MT5786_INT_RX_IOUT_OCP			= BIT(19),
	MT5786_INT_RX_VOUT_SCP			= BIT(20),
	MT5786_INT_RX_MLDO_OTP0			= BIT(21),
	MT5786_INT_RX_MLDO_OTP1			= BIT(22),
	MT5786_INT_RX_MLDO_VOUT_OVP		= BIT(23),
	MT5786_INT_RX_BCAK_FLOW			= BIT(24),
	MT5786_INT_RX_WPC_NEG_OK		= BIT(25),
	MT5786_INT_RX_TX_FSK_RCV			= BIT(26),
	//MT5786_INT_RX_WPC_EPP_CL_OK		= BIT(27),
	MT5786_INT_RX_WPC_NEG_EPP		= BIT(27),
	//MT5786_INT_RX_WPC_NEG_EPP		= BIT(28),

	/* declaration for tx */
	MT5786_INT_TX_DONE		= BIT(7),
	MT5786_INT_TX_POWER_TRANSFER	= BIT(14),
	MT5786_INT_TX_REMOVE_POWER		= BIT(15),
	MT5786_INT_TX_EPT 				= BIT(18),
	MT5786_INT_TX_VIN_OVP				= BIT(24),
	MT5786_INT_TX_IIN_OCP				= BIT(26),
	MT5786_INT_TX_PING_CLASH			= BIT(28),
	MT5786_INT_TX_WPC_MODE				= BIT(30),
};

enum mt5786_tx_register {
	MT5786_TX_CMD			= 0x2c,
	MT5786_TX_GET_CEP		= 0x71,
	MT5786_PROTOCOL_MODE		= 0x0081,
	MT5786_EPP_POWER 		= 0x0082,
	MT5786_WPC_STAT 			= 0x0083,
	MT5786_MAX_DUTY 		= 0x0298,
	MT5786_MAX_PERIOD 		= 0x029A,
	MT5786_MIN_DUTY 		= 0x029C,
	MT5786_MIN_PERIOD 		= 0x029E,
	MT5786_PING_DUTY 		= 0x02A0,
	MT5786_PING_FREQ 		= 0x02A2,
	MT5786_FULL_DUTY 		= 0x02A4,
	MT5786_FULL_FREQ 		= 0x02A6,
	MT5786_HALF_DUTY 		= 0x02A8,
	MT5786_HALF_FREQ 		= 0x02AA,
	MT5786_EPT_TYPE 		= 0x02F0,
};

enum mt5786_sys_status {
	MT5786_RX_PWRON		= BIT(4),
	MT5786_RX_LDOON		= BIT(5),
	MT5786_RX_EPP			= BIT(25), 
};

typedef enum {
	MT5786_INT_CLEAR			= BIT(8),
	MT5786_SEND_PPP			= BIT(9),
	MT5786_VOUT_CHANGE		= BIT(10),
	MT5786_SET_FOD_PARAM 	= BIT(15),
	MT5786_SET_RECT_FULL		= BIT(20),
	MT5786_SET_RECT_HALF		= BIT(21),
	MT5786_SET_MAX_POWER	= BIT(22),
} _rx_cmd;

typedef enum {
	MT5786_ENTER_TX		= BIT(1),
	MT5786_START_TX		= BIT(9),
	MT5786_STOP_TX		= BIT(10),
	MT5786_HALF_BR		= BIT(18),
	MT5786_FULL_BR		= BIT(19),
} _tx_cmd;

typedef struct Pgm_Type {
	u16 status;
	u16 addr;
	u16 length;
	u16 cs;
	u8 data[MTP_BLOCK_SIZE];
} Pgm_Type_t;

struct mt5786_func {
	int (*read)(struct mt5786_dev *dev, u16 reg, u8* val);
	int (*write)(struct mt5786_dev *dev, u16 reg, u8 val);
	int (*read_buf)(struct mt5786_dev *dev, u16 reg, u8* buf, u32 size);
	int (*write_buf)(struct mt5786_dev *dev, u16 reg, u8* buf, u32 size);
};

struct mt5786_dev {
	struct device* dev;
	struct i2c_client *client;
	struct wls_hw_info *hw_info;
	struct regmap *regmap;
	struct wireless_charger *wl_chg;
	struct mt5786_func bus;
	struct mutex notify_lock;
	struct mutex i2c_lock;

	int pg_irq;
	int dev_irq;
	int wired_irq;

	bool site;
	bool overload;
	bool pg;
	bool wired_state;
	bool ldo_on;
	bool bpp_rdy;
	u8 protocol;
	bool tx_mode;
	u8 wpc_mode;
	u32 min_freq;
	u32 max_freq;
	u32 project_power;
	u8 pt_fod_20v[31];
	u8 pt_fod_10v[31];

	struct gpio_desc *pg_gpio;
	struct gpio_desc *irq_gpio;
	struct gpio_desc *wired_gpio;
	struct gpio_desc *sleep_gpio;
	struct gpio_desc *wired_path_gpio;
	struct gpio_desc *protocol_gpio;
	struct gpio_desc *avdd_en;
	struct gpio_desc *test_gpio;
};

 const u16 mt5786_crc_ccitt_table[256] = {
    0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
    0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
    0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
    0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
    0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
    0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
    0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
    0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
    0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
    0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
    0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
    0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
    0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
    0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
    0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
    0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
    0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
    0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
    0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
    0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
    0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
    0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
    0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
    0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
    0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
    0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
    0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
    0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
    0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
    0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
    0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
    0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78
};

#endif//__MT5786_H__
