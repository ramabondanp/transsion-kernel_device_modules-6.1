// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#ifndef _HL7603_SW_H_

#define HL7603_CONFIG0					0x00

#define HL7603_DEV_ID_MASK        		(BIT(7) |BIT(6) |BIT(5) |BIT(4))
#define HL7603_DEV_ID_SHIFT       		4
#define HL7603_DEV_REV_MASK        		(BIT(3) |BIT(2) |BIT(1) |BIT(0))
#define HL7603_DEV_REV_SHIFT       		0

#define HL7603_CONFIG1					0x01

#define HL7603_RESET_MASK               		BIT(7)
#define HL7603_RESET_SHIFT              		7
#define HL7603_DEV_EN_MASK             	BIT(6)
#define HL7603_DEV_EN_SHIFT             	6
#define HL7603_MODE_CFG_MASK        	(BIT(5) |BIT(4))
#define HL7603_MODE_CFG_SHIFT       		4
#define HL7603_VOUT_DISCHG_MASK          BIT(3)
#define HL7603_VOUT_DISCHG_SHIFT         3
#define HL7603_EN_OOA_MASK             	BIT(2)
#define HL7603_EN_OOA_SHIFT             	2
#define HL7603_FPWM_CFG_MASK              BIT(1)
#define HL7603_FPWM_CFG_SHIFT             1

#define HL7603_VOUT_VSEL				0x02
#define HL7603_VOUT_REG_MASK               (BIT(5)|BIT(4)|(BIT(3)|BIT(2)|(BIT(1)|BIT(0))))
#define HL7603_VOUT_REG_SHIFT              		0

#define HL7603_ILIMSET1					0x03

#define HL7603_ILIN1_SET_MASK        		(BIT(7) |BIT(6))
#define HL7603_ILIN1_SET_SHIFT       		6
#define HL7603_ILIM_OFF_MASK    		BIT(5)
#define HL7603_ILIM_OFF_SHIFT   		5
#define HL7603_SOFT_START_MASK   		BIT(4)
#define HL7603_SOFT_START_SHIFT   		4
#define HL7603_ILIM_MASK        			(BIT(3) |BIT(2)|BIT(0))
#define HL7603_ILIM_SHIFT       			0

#define HL7603_ILIMSET2					0x04

#define HL7603_T_ILIM_H_MASK        		(BIT(1) |BIT(0))
#define HL7603_T_ILIM_H_SHIFT       		0

#define HL7603_STATUS					0x05

#define HL7603_TSD_MASK               		BIT(7)
#define HL7603_TSD_SHIFT              		7
#define HL7603_HOTDIE_MASK             	BIT(6)
#define HL7603_HOTDIE_SHIFT             	6
#define HL7603_DCDCMODE_MASK        	BIT(5)
#define HL7603_DCDCMODE_SHIFT       	5
#define HL7603_OPMODE_MASK        		BIT(4)
#define HL7603_OPMODE_SHIFT       		4
#define HL7603_VIN_OVP_MASK         	 	BIT(3)
#define HL7603_VIN_OVP_SHIFT         		3	
#define HL7603_VOUT_OVP_MASK             	BIT(2)
#define HL7603_VOUT_OVP_SHIFT             	2
#define HL7603_FAULT_MASK              		BIT(1)
#define HL7603_FAULT_SHIFT             		1
#define HL7603_PGOOD_MASK              	BIT(0)
#define HL7603_PGOOD_SHIFT             		0

#define HL7603_CHIP_ID         0xB3
#define HL7603A_CHIP_ID       0xC0 
#define HL7603A_BACK_CHIP_ID       0xB4 


struct hl7603_device_info {
	uint8_t id;
	char *name;
	//char *rm_name;
	uint8_t i2c_channel;
	uint8_t slave_addr;
	uint8_t buck_ctrl;
	uint8_t mode_shift;
	uint8_t en_shift;
	uint8_t chip_id;
	struct mutex hl7603_i2c_access;
	u32 voltage_value;
	struct i2c_client *client;
	struct device *dev;
	struct work_struct irq_work;
	int device_id;
	int rev_id;
	int dc_ibus_ucp_happened;
	u32 ic_role;
	int get_id_time;
	int get_rev_time;
	int init_finish_flag;
	int int_notify_enable_flag;
	int switching_frequency;
	int sense_r_actual;
	int sense_r_config;	
};

#endif //_HL7603_SW_H_