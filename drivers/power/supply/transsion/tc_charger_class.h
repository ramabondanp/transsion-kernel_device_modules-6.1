/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Transsion Inc.
 */

#ifndef LINUX_TC_CHARGER_CLASS_H
#define LINUX_TC_CHARGER_CLASS_H

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mutex.h>

extern struct kobject *chg_kobj;

enum dpdm_ctrl_status {
	DPDM_CTRL_HZ,
	DPDM_CTRL_0V,
	DPDM_CTRL_0_6V,
	DPDM_CTRL_3_3V,
	DPDM_CTRL_DP_HZ_DM_HZ,
	DPDM_CTRL_DP_0_6_DM_HZ,
	DPDM_CTRL_DP_3_3_DM_0_6,
};

static const char * const dpdm_ctrl_name[] = {
	[DPDM_CTRL_HZ]	 	  = "DPDM_CTRL_HZ",
	[DPDM_CTRL_0V]	 	  = "DPDM_CTRL_0V",
	[DPDM_CTRL_0_6V] 	  = "DPDM_CTRL_0_6V",
	[DPDM_CTRL_3_3V] 	  = "DPDM_CTRL_3_3V",
	[DPDM_CTRL_DP_HZ_DM_HZ]   = "DPDM_CTRL_DP_HZ_DM_HZ",
	[DPDM_CTRL_DP_0_6_DM_HZ]  = "DPDM_CTRL_DP_0_6_DM_HZ",
	[DPDM_CTRL_DP_3_3_DM_0_6] = "DPDM_CTRL_DP_3_3_DM_0_6",
};

enum adc_channel {
	ADC_CHANNEL_VBUS,
	ADC_CHANNEL_VSYS,
	ADC_CHANNEL_VBAT,
	ADC_CHANNEL_IBUS,
	ADC_CHANNEL_IBAT,
	ADC_CHANNEL_TEMP_JC,
	ADC_CHANNEL_VREFTS,
	ADC_CHANNEL_USBID,
	ADC_CHANNEL_TS,
	ADC_CHANNEL_TBAT,
	ADC_CHANNEL_TPCB,
	ADC_CHANNEL_TPA,
	ADC_CHANNEL_TSBUS,
	ADC_CHANNEL_VOUT,
};

enum tc_attach_type {
	TC_ATTACH_TYPE_NONE = 0,
	TC_ATTACH_TYPE_PWR_RDY,
	TC_ATTACH_TYPE_TYPEC,
	TC_ATTACH_TYPE_PD,
	TC_ATTACH_TYPE_PD_SDP,
	TC_ATTACH_TYPE_PD_DCP,
	TC_ATTACH_TYPE_PD_NONSTD,
};

static const char * const attach_type_name[] = {
	[TC_ATTACH_TYPE_NONE]      = "TC_ATTACH_TYPE_NONE",
	[TC_ATTACH_TYPE_PWR_RDY]   = "TC_ATTACH_TYPE_PWR_RDY",
	[TC_ATTACH_TYPE_TYPEC]     = "TC_ATTACH_TYPE_TYPEC",
	[TC_ATTACH_TYPE_PD]        = "TC_ATTACH_TYPE_PD",
	[TC_ATTACH_TYPE_PD_SDP]    = "TC_ATTACH_TYPE_PD_SDP",
	[TC_ATTACH_TYPE_PD_DCP]    = "TC_ATTACH_TYPE_PD_DCP",
	[TC_ATTACH_TYPE_PD_NONSTD] = "TC_ATTACH_TYPE_PD_NONSTD",
};

enum {
	TC_PORT_STAT_NOINFO = 0,
	TC_PORT_STAT_APPLE_10W,
	TC_PORT_STAT_SAMSUNG,
	TC_PORT_STAT_APPLE_5W,
	TC_PORT_STAT_APPLE_12W,
	TC_PORT_STAT_UNKNOWN_TA,
	TC_PORT_STAT_SDP,
	TC_PORT_STAT_CDP,
	TC_PORT_STAT_DCP,
};

static const char *const port_stat_name[] = {
	[TC_PORT_STAT_NOINFO] = "No Info",
	[TC_PORT_STAT_APPLE_10W] = "Apple 10W",
	[TC_PORT_STAT_SAMSUNG] = "Samsung",
	[TC_PORT_STAT_APPLE_5W] = "Apple 5W",
	[TC_PORT_STAT_APPLE_12W] = "Apple 12W",
	[TC_PORT_STAT_UNKNOWN_TA] = "Unknown TA",
	[TC_PORT_STAT_SDP] = "SDP",
	[TC_PORT_STAT_CDP] = "CDP",
	[TC_PORT_STAT_DCP] = "DCP",
};

enum hvchg_mode {
	HVCHG_NORMAL_MODE= 0,
	HVCHG_BOOST_MODE,
	HVCHG_BOOST_1_4_MODE,
	HVCHG_DIS_BOOST_MODE,
	HVCHG_CP_MODE_INIT,
	HVCHG_CP_WIRELESS_MODE_INIT,
	HVCHG_EN_CP_MODE,
	HVCHG_EN_BYPASS_MODE,
	HVCHG_EN_FWD_MODE,
	HVCHG_DIS_FWD_MODE,
	HVCHG_EN_REV_MODE,
	HVCHG_DIS_REV_MODE,
	HVCHG_SOFT_RESET,
	HVCHG_WIRELESS_LPM_MODE,

};

enum chg_usbsw {
	TC_USBSW_CHG = 0,
	TC_USBSW_USB,
};

struct charger_properties {
	const char *alias_name;
};

/* Data of notifier from charger device */
struct chgdev_notify {
	bool vbusov_stat;
};

/* charger_dev notify */
enum {
	CHARGER_DEV_NOTIFY_VBUS_OVP,
	CHARGER_DEV_NOTIFY_BAT_OVP,
	CHARGER_DEV_NOTIFY_EOC,
	CHARGER_DEV_NOTIFY_RECHG,
	CHARGER_DEV_NOTIFY_SAFETY_TIMEOUT,
	CHARGER_DEV_NOTIFY_VBATOVP_ALARM,
	CHARGER_DEV_NOTIFY_VBUSOVP_ALARM,
	CHARGER_DEV_NOTIFY_IBATOCP,
	CHARGER_DEV_NOTIFY_IBUSOCP,
	CHARGER_DEV_NOTIFY_IBUSUCP_FALL,
	CHARGER_DEV_NOTIFY_VOUTOVP,
	CHARGER_DEV_NOTIFY_VDROVP,
	CHARGER_DEV_NOTIFY_BATPRO_DONE,
	CHARGER_DEV_NOTIFY_OTG_FAULT,
	CHARGER_DEV_NOTIFY_PWR_LOSS,
	CHARGER_DEV_NOTIFY_PWR_VALID,
	CHARGER_DEV_NOTIFY_RFCVOL_INVALID,
	CHARGER_DEV_NOTIFY_PWR_CHANGED,
};

struct charger_device {
	struct charger_properties props;
	struct chgdev_notify noti;
	const struct charger_ops *ops;
	struct mutex ops_lock;
	struct device dev;
	struct srcu_notifier_head evt_nh;
	void	*driver_data;
	bool is_polling_mode;
};

enum charger_property {
	CHARGER_PROP_BLEED_DISCHARGE,
};

union charger_propval {
	int intval;
	const char *strval;
};

struct wireless_cap {
    int tx_pmax;
    int tx_vmax;
    int tx_imax;
    int ta_pmax;
    int ta_vmax;
    int ta_imax;
};

struct charger_ops {
	int (*suspend)(struct charger_device *dev, pm_message_t state);
	int (*resume)(struct charger_device *dev);

	/* cable plug in/out */
	int (*plug_in)(struct charger_device *dev);
	int (*plug_out)(struct charger_device *dev);

	/* enable/disable charger */
	int (*enable)(struct charger_device *dev, bool en);
	int (*is_enabled)(struct charger_device *dev, bool *en);

	/* enable/disable chip */
	int (*enable_chip)(struct charger_device *dev, bool en);
	int (*is_chip_enabled)(struct charger_device *dev, bool *en);

	/* get/set charging current*/
	int (*get_charging_current)(struct charger_device *dev, u32 *uA);
	int (*set_charging_current)(struct charger_device *dev, u32 uA);
	int (*get_min_charging_current)(struct charger_device *dev, u32 *uA);

	/* set cv */
	int (*set_constant_voltage)(struct charger_device *dev, u32 uV);
	int (*get_constant_voltage)(struct charger_device *dev, u32 *uV);

	/* set input_current */
	int (*get_input_current)(struct charger_device *dev, u32 *uA);
	int (*set_input_current)(struct charger_device *dev, u32 uA);
	int (*get_min_input_current)(struct charger_device *dev, u32 *uA);

	/* set termination current */
	int (*get_eoc_current)(struct charger_device *dev, u32 *uA);
	int (*set_eoc_current)(struct charger_device *dev, u32 uA);

	/* kick wdt */
	int (*kick_wdt)(struct charger_device *dev);

	int (*event)(struct charger_device *dev, u32 event, u32 args);

	/* 6-pin battery */
	int (*enable_6pin_battery_charging)(struct charger_device *dev,bool en);

	/* PE+/PE+2.0 */
	int (*send_ta_current_pattern)(struct charger_device *dev, bool is_inc);
	int (*send_ta20_current_pattern)(struct charger_device *dev, u32 uV);
	int (*reset_ta)(struct charger_device *dev);
	int (*enable_cable_drop_comp)(struct charger_device *dev, bool en);

	int (*set_mivr)(struct charger_device *dev, u32 uV);
	int (*get_mivr)(struct charger_device *dev, u32 *uV);
	int (*get_mivr_state)(struct charger_device *dev, bool *in_loop);

	/* enable/disable powerpath */
	int (*is_powerpath_enabled)(struct charger_device *dev, bool *en);
	int (*enable_powerpath)(struct charger_device *dev, bool en);

	/* enable/disable vbus ovp */
	int (*enable_vbus_ovp)(struct charger_device *dev, bool en);

	/* enable/disable charging safety timer */
	int (*is_safety_timer_enabled)(struct charger_device *dev, bool *en);
	int (*enable_safety_timer)(struct charger_device *dev, bool en);

	/* enable term */
	int (*enable_termination)(struct charger_device *dev, bool en);

	/* direct charging */
	int (*enable_direct_charging)(struct charger_device *dev, bool en);
	int (*kick_direct_charging_wdt)(struct charger_device *dev);
	int (*set_direct_charging_ibusoc)(struct charger_device *dev, u32 uA);
	int (*set_direct_charging_vbusov)(struct charger_device *dev, u32 uV);

	int (*set_ibusocp)(struct charger_device *dev, u32 uA);
	int (*set_vbusovp)(struct charger_device *dev, u32 uV);
	int (*set_ibatocp)(struct charger_device *dev, u32 uA);
	int (*set_vbatovp)(struct charger_device *dev, u32 uV);
	int (*set_vbatovp_alarm)(struct charger_device *dev, u32 uV);
	int (*reset_vbatovp_alarm)(struct charger_device *dev);
	int (*set_vbusovp_alarm)(struct charger_device *dev, u32 uV);
	int (*reset_vbusovp_alarm)(struct charger_device *dev);
	int (*is_vbuslowerr)(struct charger_device *dev, bool *err);
	int (*init_chip)(struct charger_device *dev);

	/* OTG */
	int (*enable_otg)(struct charger_device *dev, bool en);
	int (*disable_otg_step)(struct charger_device *dev, bool en);
	int (*enable_discharge)(struct charger_device *dev, bool en);
	int (*set_boost_current_limit)(struct charger_device *dev, u32 uA);
	int (*set_boost_voltage)(struct charger_device *dev, u32 uV);
	int (*get_boost_status)(struct charger_device *dev);

	/* charger type detection */
	int (*enable_chg_type_det)(struct charger_device *dev, bool en);
	int (*get_vbus_gd)(struct charger_device *dev, bool *vbus_gd);

	/* run AICL */
	int (*run_aicl)(struct charger_device *dev, u32 *uA);

	/* reset EOC state */
	int (*reset_eoc_state)(struct charger_device *dev);

	int (*safety_check)(struct charger_device *dev, u32 polling_ieoc);

	int (*is_charging_done)(struct charger_device *dev, bool *done);
	int (*set_pe20_efficiency_table)(struct charger_device *dev);
	int (*dump_registers)(struct charger_device *dev);

	int (*get_adc)(struct charger_device *dev, enum adc_channel chan,
		       int *min, int *max);
	int (*get_adc_accuracy)(struct charger_device *dev,
				enum adc_channel chan, int *min, int *max);
	int (*get_vbus_adc)(struct charger_device *dev, u32 *vbus);
	int (*get_ibus_adc)(struct charger_device *dev, u32 *ibus);
	int (*get_ibat_adc)(struct charger_device *dev, u32 *ibat);
	int (*get_tchg_adc)(struct charger_device *dev, int *tchg_min,int *tchg_max);
	int (*get_zcv)(struct charger_device *dev, u32 *uV);

	/* TypeC */
	int (*enable_usbid)(struct charger_device *dev, bool en);
	int (*set_usbid_rup)(struct charger_device *dev, u32 rup);
	int (*set_usbid_src_ton)(struct charger_device *dev, u32 src_ton);
	int (*enable_usbid_floating)(struct charger_device *dev, bool en);
	int (*enable_force_typec_otp)(struct charger_device *dev, bool en);
	int (*enable_hidden_mode)(struct charger_device *dev, bool en);
	int (*get_ctd_dischg_status)(struct charger_device *dev, u8 *status);
	int (*enable_hz)(struct charger_device *dev, bool en);
	int (*is_hz)(struct charger_device *dev, bool *is_hiz);
	int (*get_ilim_state)(struct charger_device *dev, bool *is_ilim);

	int (*set_property)(struct charger_device *dev,
			enum charger_property prop,
			union charger_propval *val);
	int (*get_property)(struct charger_device *dev,
			enum charger_property prop,
			union charger_propval *val);

	int (*soft_reset)(struct charger_device *dev);
	int (*set_dp_dm)(struct charger_device *dev, enum dpdm_ctrl_status dp_status, 
			enum dpdm_ctrl_status dm_status, bool en_rfc_detect);
	int (*get_dp_dm)(struct charger_device *dev, bool dp);
	int (*set_dp)(struct charger_device *, enum dpdm_ctrl_status status);
	int (*set_dm)(struct charger_device *, enum dpdm_ctrl_status status);
	int (*en_dpdm_ctrl)(struct charger_device *, bool en);
	int (*cp_rfc_detect)(struct charger_device *chg_dev, bool *is_rfc_ta);
	int (*set_dp_dm_coupler)(struct charger_device *, enum dpdm_ctrl_status status);
	int (*i2c_trans)(struct charger_device *dev, bool high);
	int (*init_adc)(struct charger_device *dev, bool en);
	int (*reset_ucp)(struct charger_device *dev);
	int (*set_pfm_mode)(struct charger_device *dev, bool en);
	int (*get_port_stat)(struct charger_device *dev, int *port_stat);
	int (*get_ts_adc)(struct charger_device *dev, u32 *uV);
	int (*get_vrefts_adc)(struct charger_device *dev, u32 *uV);
	int (*set_ircmp)(struct charger_device *dev, u32 ir);
	int (*set_ivcmp)(struct charger_device *dev, u32 iv);
	int (*set_ffc_sw_eoc)(struct charger_device *dev, bool chg_done);
	int (*set_tc30_oneshot)(struct charger_device *dev, int val);
	int (*get_aicr_state)(struct charger_device *dev, bool *state);
	int (*set_ibusucp_enable)(struct charger_device *chg_dev, bool enable);
	int (*get_vac1_status)(struct charger_device *chg_dev, bool *online);
	int (*set_run_spec)(struct charger_device *chg_dev, u32 run_sepc);
	int (*get_run_spec)(struct charger_device *chg_dev, u32 run_sepc);
	int (*enable_special_function)(struct charger_device *dev,  int function);

	/* wireless */
	int (*get_wireless_authenticate)(struct charger_device *dev, bool *en);
	int (*set_wireless_vbus)(struct charger_device *dev, int mV);
	int (*get_wireless_vbus)(struct charger_device *dev, int *mV);
	int (*get_wireless_ibus)(struct charger_device *dev, int *mA);
	int (*set_wireless_fast_mode)(struct charger_device *dev, bool en);
	int (*set_wireless_volt_sync)(struct charger_device *dev, u32 mv);
	int (*get_wireless_online)(struct charger_device *dev, bool *online);
	int (*get_wireless_pg_status)(struct charger_device *dev, bool *pg);
	int (*get_wireless_tx_power)(struct charger_device *dev, int *value);
	int (*get_wireless_capacity)(struct charger_device *chg_dev, void *info);
	int (*get_wireless_site)(struct charger_device *dev, bool *en);
	int (*set_wl_reverse_boost)(struct charger_device *dev, bool en);
	int (*get_wireless_reverse)(struct charger_device *dev, bool *state);
	int (*get_wireless_ce)(struct charger_device *dev);
	int (*get_tx_bridge_mode)(struct charger_device *dev);

	int (*is_water_detected)(struct charger_device *dev, bool *is_water);
	int (*enable_shipmode)(struct charger_device *dev, bool on);
	int (*get_shipmode_status)(struct charger_device *dev);
	int (*set_low_power_mode)(struct charger_device *dev, bool en);
	int (*get_bridge_mode)(struct charger_device *dev);
	int (*set_bridge_mode)(struct charger_device *dev);
	u8 (*get_wls_protocol)(struct charger_device *dev);
	u8 (*get_config_inductions)(struct charger_device *dev);
	bool (*pa_done)(struct charger_device *dev);
	int (*select_wireless_cur_limit)(struct charger_device *dev);
	void (*otg_mosfet_ctl)(struct charger_device *dev, bool en);
};

static inline void *charger_dev_get_drvdata(const struct charger_device *charger_dev)
{
	return charger_dev->driver_data;
}

static inline void charger_dev_set_drvdata(struct charger_device *charger_dev, void *data)
{
	charger_dev->driver_data = data;
}

extern struct charger_device *charger_device_register(const char *name,
	struct device *parent, void *devdata, const struct charger_ops *ops,
	const struct charger_properties *props);
extern void charger_device_unregister(struct charger_device *charger_dev);
extern struct charger_device *get_charger_by_name(const char *name);

#define to_charger_device(obj) container_of(obj, struct charger_device, dev)

static inline void *charger_get_data(struct charger_device *charger_dev)
{
	return dev_get_drvdata(&charger_dev->dev);
}

extern int charger_dev_enable(struct charger_device *dev, bool en);
extern int charger_dev_is_enabled(struct charger_device *dev, bool *en);
extern int charger_dev_plug_in(struct charger_device *dev);
extern int charger_dev_plug_out(struct charger_device *dev);
extern int charger_dev_set_charging_current(struct charger_device *dev, u32 uA);
extern int charger_dev_get_charging_current(struct charger_device *dev, u32 *uA);
extern int charger_dev_get_min_charging_current(struct charger_device *dev, u32 *uA);
extern int charger_dev_set_input_current(struct charger_device *dev, u32 uA);
extern int charger_dev_get_input_current(struct charger_device *dev, u32 *uA);
extern int charger_dev_get_min_input_current(struct charger_device *dev, u32 *uA);
extern int charger_dev_set_eoc_current(struct charger_device *dev, u32 uA);
extern int charger_dev_get_eoc_current(struct charger_device *dev, u32 *uA);
extern int charger_dev_kick_wdt(struct charger_device *dev);
extern int charger_dev_set_constant_voltage(struct charger_device *dev, u32 uV);
extern int charger_dev_get_constant_voltage(struct charger_device *dev, u32 *uV);
extern int charger_dev_dump_registers(struct charger_device *dev);
extern int charger_dev_enable_vbus_ovp(struct charger_device *dev, bool en);
extern int charger_dev_set_mivr(struct charger_device *dev, u32 uV);
extern int charger_dev_get_mivr(struct charger_device *dev, u32 *uV);
extern int charger_dev_get_mivr_state(struct charger_device *dev, bool *in_loop);
extern int charger_dev_do_event(struct charger_device *dev, u32 event, u32 args);
extern int charger_dev_enable_6pin_battery_charging(struct charger_device *dev, bool en);
extern int charger_dev_is_powerpath_enabled(struct charger_device *dev, bool *en);
extern int charger_dev_is_safety_timer_enabled(struct charger_device *dev, bool *en);
extern int charger_dev_enable_termination(struct charger_device *dev, bool en);
extern int charger_dev_is_charging_done(struct charger_device *dev, bool *done);
extern int charger_dev_enable_powerpath(struct charger_device *dev, bool en);
extern int charger_dev_enable_safety_timer(struct charger_device *dev, bool en);
extern int charger_dev_enable_chg_type_det(struct charger_device *dev, bool en);
extern int charger_dev_get_vbus_gd(struct charger_device *dev, bool *vbus_gd);
extern int charger_dev_enable_otg(struct charger_device *dev, bool en);
extern int charger_dev_disable_otg_step(struct charger_device *chg_dev, bool en);
extern int charger_dev_set_boost_voltage(struct charger_device *dev, u32 uV);
extern int charger_dev_get_boost_status(struct charger_device *dev);
extern int charger_dev_enable_discharge(struct charger_device *dev, bool en);
extern int charger_dev_set_boost_current_limit(struct charger_device *dev, u32 uA);
extern int charger_dev_get_zcv(struct charger_device *dev, u32 *uV);
extern int charger_dev_run_aicl(struct charger_device *dev, u32 *uA);
extern int charger_dev_reset_eoc_state(struct charger_device *dev);
extern int charger_dev_safety_check(struct charger_device *dev, u32 polling_ieoc);
extern int charger_dev_enable_hz(struct charger_device *dev, bool en);
extern int charger_dev_enable_shipmode(struct charger_device *chg_dev, bool on);
extern int charger_dev_get_shipmode_status(struct charger_device *chg_dev);
extern int charger_dev_set_low_power_mode(struct charger_device *chg_dev, bool en);
extern int charger_dev_is_water_detected(struct charger_device *chg_dev, bool *is_water);
extern int charger_dev_is_hz(struct charger_device *dev, bool *is_hiz);
extern int charger_dev_get_ilim_state(struct charger_device *dev, bool *is_ilim);

/* PE+/PE+2.0 */
extern int charger_dev_send_ta_current_pattern(struct charger_device *dev, bool is_increase);
extern int charger_dev_send_ta20_current_pattern(struct charger_device *dev, u32 uV);
extern int charger_dev_reset_ta(struct charger_device *dev);
extern int charger_dev_set_pe20_efficiency_table(struct charger_device *dev);
extern int charger_dev_enable_cable_drop_comp(struct charger_device *dev, bool en);

/* PE 3.0 */
extern int charger_dev_enable_chip(struct charger_device *dev, bool en);
extern int charger_dev_is_chip_enabled(struct charger_device *dev, bool *en);
extern int charger_dev_enable_direct_charging(struct charger_device *dev, bool en);
extern int charger_dev_kick_direct_charging_wdt(struct charger_device *dev);
extern int charger_dev_get_adc(struct charger_device *dev,
					enum adc_channel chan, int *min, int *max);
extern int charger_dev_get_adc_accuracy(struct charger_device *dev,
					enum adc_channel chan, int *min, int *max);
/* Prefer use charger_dev_get_adc api */
extern int charger_dev_get_vbus(struct charger_device *dev, u32 *vbus);
extern int charger_dev_get_ibus(struct charger_device *dev, u32 *ibus);
extern int charger_dev_get_ibat(struct charger_device *dev, u32 *ibat);
extern int charger_dev_get_temperature(struct charger_device *dev, int *tchg_min,int *tchg_max);
extern int charger_dev_set_direct_charging_ibusoc(struct charger_device *dev, u32 ua);
extern int charger_dev_set_direct_charging_vbusov(struct charger_device *dev, u32 uv);

extern int charger_dev_set_ibusocp(struct charger_device *dev, u32 uA);
extern int charger_dev_set_vbusovp(struct charger_device *dev, u32 uV);
extern int charger_dev_set_ibatocp(struct charger_device *dev, u32 uA);
extern int charger_dev_set_vbatovp(struct charger_device *dev, u32 uV);
extern int charger_dev_set_vbatovp_alarm(struct charger_device *dev,u32 uV);
extern int charger_dev_reset_vbatovp_alarm(struct charger_device *dev);
extern int charger_dev_set_vbusovp_alarm(struct charger_device *dev,u32 uV);
extern int charger_dev_reset_vbusovp_alarm(struct charger_device *dev);
extern int charger_dev_is_vbuslowerr(struct charger_device *dev, bool *err);
extern int charger_dev_init_chip(struct charger_device *dev);

/* TypeC */
extern int charger_dev_enable_usbid(struct charger_device *dev, bool en);
extern int charger_dev_set_usbid_rup(struct charger_device *dev, u32 rup);
extern int charger_dev_set_usbid_src_ton(struct charger_device *dev,u32 src_ton);
extern int charger_dev_enable_usbid_floating(struct charger_device *dev,bool en);
extern int charger_dev_enable_force_typec_otp(struct charger_device *dev,bool en);
extern int charger_dev_get_ctd_dischg_status(struct charger_device *dev,u8 *status);

extern int charger_dev_set_property(struct charger_device *dev,
					enum charger_property prop,
					union charger_propval *val);
extern int charger_dev_get_property(struct charger_device *dev,
					enum charger_property prop,
					union charger_propval *val);

/* For buck1 FPWM */
extern int charger_dev_enable_hidden_mode(struct charger_device *dev, bool en);

extern int register_charger_device_notifier(struct charger_device *dev, struct notifier_block *nb);
extern int unregister_charger_device_notifier(struct charger_device *dev, struct notifier_block *nb);
extern int charger_dev_notify(struct charger_device *dev, int event);
extern int charger_dev_set_dp(struct charger_device *chg_dev, enum dpdm_ctrl_status status);
extern int charger_dev_set_dm(struct charger_device *chg_dev, enum dpdm_ctrl_status status);
extern int charger_dev_en_dpdm_ctrl(struct charger_device *chg_dev, bool en);
extern int charger_dev_set_dp_dm_coupler(struct charger_device *chg_dev, enum dpdm_ctrl_status status);
extern int charger_dev_cp_rfc_detect(struct charger_device *charger_dev, bool *is_rfc_ta);
extern int charger_dev_set_dp_dm(struct charger_device *dev, enum dpdm_ctrl_status dp_status,
					enum dpdm_ctrl_status dm_status, bool en_rfc_detect);
extern int charger_dev_get_dp_dm(struct charger_device *dev, bool dp);
extern int charger_dev_i2c_trans(struct charger_device *dev, bool high);
extern int charger_dev_soft_reset(struct charger_device *dev);
extern int charger_dev_init_adc(struct charger_device *dev, bool en);
extern int charger_dev_reset_ucp(struct charger_device *dev);
extern int charger_dev_set_pfm_mode(struct charger_device *dev, bool en);

extern int charger_dev_get_port_stat(struct charger_device *chg_dev, int *port_stat);
extern int charger_dev_get_ts(struct charger_device *charger_dev, u32 *uV);
extern int charger_dev_get_vrefts(struct charger_device *charger_dev, u32 *uV);
extern int charger_dev_set_ircmp(
	struct charger_device *charger_dev, u32 ir);
extern int charger_dev_set_ivcmp(
	struct charger_device *charger_dev, u32 iv);
extern int charger_dev_set_ffc_sw_eoc(
	struct charger_device *charger_dev, bool chg_done);
extern int charger_dev_set_tc30_oneshot(struct charger_device *chg_dev, int val);
extern int charger_dev_get_aicr_state(struct charger_device *chg_dev, bool *state);
extern int charger_dev_set_ibusucp_enable(struct charger_device *chg_dev, bool enable);
extern int charger_dev_get_vac1_status(struct charger_device *chg_dev, bool *online);
extern int charger_dev_set_run_spec(struct charger_device *chg_dev, u32 run_sepc);
extern int charger_dev_get_run_spec(struct charger_device *chg_dev, u32 run_sepc);
extern int charger_dev_enable_special_function(struct charger_device *chg_dev, int function);

/* wireless */
extern int charger_dev_set_reverse_boost(struct charger_device *chg_dev, bool enable);
extern int wireless_ta_get_ce(struct charger_device *c_dev);
extern int wireless_ta_get_pg_state(struct charger_device *c_dev, bool *pg);
extern int wireless_get_tx_power(struct charger_device *c_dev, int *power);
extern int wireless_ta_authenticate(struct charger_device *c_dev, bool *auth);
extern int wireless_manager_reverse_state(struct charger_device *c_dev, bool *state);
extern int wireless_get_ta_tx_cap(struct charger_device *c_dev, void *info);
extern int wireless_set_tx_vol(struct charger_device *c_dev, int mv);
extern int wireless_ta_get_vbus(struct charger_device *c_dev, int *mv);
extern int wireless_ta_volt_sync(struct charger_device *c_dev, u32 mv);
extern int wireless_ta_get_ibus(struct charger_device *c_dev, int *ma);
extern int wireless_select_current_limit(struct charger_device *c_dev);

#endif /*LINUX_TC_CHARGER_CLASS_H*/
