/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Transsion Inc.
 */

#ifndef LINUX_POWER_ADAPTER_CLASS_H
#define LINUX_POWER_ADAPTER_CLASS_H

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mutex.h>


#define ADAPTER_CAP_MAX_NR 10

struct tadapter_power_cap {
	uint8_t selected_cap_idx;
	uint8_t nr;
	uint8_t pdp;
	uint8_t pwr_limit[ADAPTER_CAP_MAX_NR];
	int max_mv[ADAPTER_CAP_MAX_NR];
	int min_mv[ADAPTER_CAP_MAX_NR];
	int ma[ADAPTER_CAP_MAX_NR];
	int maxwatt[ADAPTER_CAP_MAX_NR];
	int minwatt[ADAPTER_CAP_MAX_NR];
	uint8_t type[ADAPTER_CAP_MAX_NR];
	int info[ADAPTER_CAP_MAX_NR];
};

struct tadapter_auth_data {
	int vcap_min;
	int vcap_max;
	int icap_min;
	int vta_min;
	int vta_max;
	int ita_max;
	int ita_min;
	bool pwr_lmt;
	u8 pdp;
	bool support_meas_cap;
	bool support_status;
	bool support_cc;
	u32 vta_step;
	u32 ita_step;
	u32 ita_gap_per_vstep;
	u32 ita_gap_cv_mode;
	u32 full_power_run_time;
};

enum tadapter_type {
	TC_PD_ADAPTER,
};

// enum tadapter_event {
//         TC_PD_CONNECT_NONE,
//         TC_PD_CONNECT_HARD_RESET,
//         TC_PD_CONNECT_PE_READY_SNK,
//         TC_PD_CONNECT_PE_READY_SNK_PD30,
//         TC_PD_CONNECT_PE_READY_SNK_APDO,
//         TC_PD_CONNECT_TYPEC_ONLY_SNK,
//         TC_TYPEC_WD_STATUS,
//         TC_TYPEC_HRESET_STATUS,
//         TC_PD_CONNECT_TIMEOUT,
// };

enum tadapter_property {
	TYPEC_RP_LEVEL,
	PD_TYPE,
	PD_TYPE_TIMEOUT,
	PD_ATTACHED_TIME,
	PD_SRC_PDO_SUPPORT_USB_SUSPEND,
};

enum tadapter_cap_type {
	TC_PD_APDO_START,
	TC_PD_APDO_END,
	TC_PD,
	TC_PD_APDO,
	TC_CAP_TYPE_UNKNOWN,
};

enum tadapter_return_value {
	TC_ADAPTER_OK = 0,
	TC_ADAPTER_NOT_SUPPORT,
	TC_ADAPTER_TIMEOUT,
	TC_ADAPTER_REJECT,
	TC_ADAPTER_ERROR,
	TC_ADAPTER_ADJUST,
};


struct tadapter_status {
	int temperature;
	bool ocp;
	bool otp;
	bool ovp;
	bool cc_mode;
	bool wls_site;
	u8 cable_capability;
};

struct tadapter_properties {
	const char *alias_name;
};

struct tadapter_device {
	struct tadapter_properties props;
	const struct tadapter_ops *ops;
	struct mutex ops_lock;
	struct device dev;
	struct srcu_notifier_head evt_nh;
	void	*driver_data;

};

struct tadapter_ops {
	int (*suspend)(struct tadapter_device *dev, pm_message_t state);
	int (*resume)(struct tadapter_device *dev);
	int (*get_property)(struct tadapter_device *dev,
		enum tadapter_property pro);
	int (*get_status)(struct tadapter_device *dev,
		struct tadapter_status *sta);
	int (*set_cap)(struct tadapter_device *dev, enum tadapter_cap_type type,
		int mV, int mA);
	int (*get_cap)(struct tadapter_device *dev, enum tadapter_cap_type type,
		struct tadapter_power_cap *cap);
	int (*get_output)(struct tadapter_device *dev, int *mV, int *mA);
	int (*authentication)(struct tadapter_device *dev,
			      struct tadapter_auth_data *data);
	int (*is_cc)(struct tadapter_device *dev, bool *cc);
	int (*set_wdt)(struct tadapter_device *dev, u32 ms);
	int (*enable_wdt)(struct tadapter_device *dev, bool en);
	int (*sync_volt)(struct tadapter_device *dev, u32 mV);
	int (*send_hardreset)(struct tadapter_device *dev);
	int (*get_ta_fw)(struct tadapter_device *dev,u8 *code,u32 length);
	int (*plug_out_reset)(struct tadapter_device *dev);
	int (*get_det_done)(struct tadapter_device *dev, bool *det_done);
	int (*exit_protocol)(struct tadapter_device *dev);
	int (*get_epp)(struct tadapter_device *dev);
	int (*get_ce)(struct tadapter_device *dev);
};

static inline void *tadapter_dev_get_drvdata(
	const struct tadapter_device *tadapter_dev)
{
	return tadapter_dev->driver_data;
}

static inline void tadapter_dev_set_drvdata(
	struct tadapter_device *tadapter_dev, void *data)
{
	tadapter_dev->driver_data = data;
}

extern struct tadapter_device *tadapter_device_register(
	const char *name,
	struct device *parent, void *devdata, const struct tadapter_ops *ops,
	const struct tadapter_properties *props);
extern void tadapter_device_unregister(
	struct tadapter_device *tadapter_dev);
extern int register_tadapter_device_notifier(struct tadapter_device *tadapter_dev,
				struct notifier_block *nb);
extern int unregister_tadapter_device_notifier(
				struct tadapter_device *tadapter_dev,
				struct notifier_block *nb);
extern struct tadapter_device *get_tadapter_by_name(
	const char *name);

#define to_tadapter_device(obj) container_of(obj, struct tadapter_device, dev)

extern int tadapter_dev_get_property(struct tadapter_device *tadapter_dev,
	enum tadapter_property sta);
extern int tadapter_dev_get_status(struct tadapter_device *tadapter_dev,
	struct tadapter_status *sta);
extern int tadapter_dev_get_output(struct tadapter_device *tadapter_dev,
	int *mV, int *mA);
extern int tadapter_dev_set_cap(struct tadapter_device *tadapter_dev,
	enum tadapter_cap_type type,
	int mV, int mA);
extern int tadapter_dev_get_cap(struct tadapter_device *tadapter_dev,
	enum tadapter_cap_type type,
	struct tadapter_power_cap *cap);
extern int tadapter_dev_authentication(struct tadapter_device *tadapter_dev,
				      struct tadapter_auth_data *data);
extern int tadapter_dev_is_cc(struct tadapter_device *tadapter_dev, bool *cc);
extern int tadapter_dev_set_wdt(struct tadapter_device *tadapter_dev, u32 ms);
extern int tadapter_dev_enable_wdt(struct tadapter_device *tadapter_dev, bool en);
extern int tadapter_dev_sync_volt(struct tadapter_device *tadapter_dev, u32 mV);
extern int tadapter_dev_send_hardreset(struct tadapter_device *tadapter_dev);
extern int tadapter_dev_get_ta_fw(struct tadapter_device *tadapter_dev,
                                  u8 *code,u32 length);
extern int tadapter_dev_plug_out_reset(struct tadapter_device *tadapter_dev);
extern int tadapter_dev_get_det_done(struct tadapter_device *tadapter_dev, bool *det_done);
extern int tadapter_dev_exit_protocol(struct tadapter_device *tadapter_dev);
extern int tadapter_dev_get_epp(struct tadapter_device *tadapter_dev);
extern int tadapter_dev_get_ce(struct tadapter_device *tadapter_dev);
#endif /*LINUX_POWER_ADAPTER_CLASS_H*/
