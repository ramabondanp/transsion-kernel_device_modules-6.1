// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#include "wireless_class.h"

void wireless_ic_set_state(struct wireless_charger *wl_chg,
	enum wireless_state state)
{
	struct wireless_manager *wm = dev_get_drvdata(&wl_chg->wm_chg->dev);

	if (!wm || !wm->state_call_back)
		return;

	wm->state_call_back(wm, state);
}
EXPORT_SYMBOL(wireless_ic_set_state);

int __wireless_charger_set_drop_voltage(struct wireless_manager *m)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->set_drop_voltage) {
			return ops->set_drop_voltage(wcd);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_set_drop_voltage);

int __wireless_charger_tx_bridge_voltage(struct wireless_manager *m)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->tx_bridge_voltage) {
			return ops->tx_bridge_voltage(wcd);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_tx_bridge_voltage);

int __wireless_charger_get_tx_ce_value(struct wireless_manager *m)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->get_tx_ce_value) {
			return ops->get_tx_ce_value(wcd);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_get_tx_ce_value);

int __wireless_charger_bridge_mode(struct wireless_manager *m)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->get_bridge_mode) {
			return ops->get_bridge_mode(wcd);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_bridge_mode);

int __wireless_charger_set_bridge_mode(struct wireless_manager *m)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->set_bridge_mode) {
			return ops->set_bridge_mode(wcd);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_set_bridge_mode);

int __wireless_charger_set_bridge_logic(struct wireless_manager *m)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops->set_bridge_logic) {
			return ops->set_bridge_logic(wcd);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_set_bridge_logic);

int __wireless_charger_set_ovp_ctrl(struct wireless_manager *m, int en)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->set_ovp_ctrl) {
			return ops->set_ovp_ctrl(wcd, en);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_set_ovp_ctrl);

int __wireless_charger_enter_sleep(struct wireless_manager *m, bool en)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->set_wireless_sleep) {
			return ops->set_wireless_sleep(wcd, en);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_enter_sleep);

int __wireless_charger_enter_lpm_mode(struct wireless_manager *m, bool en)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->set_wireless_lpm_mode) {
			return ops->set_wireless_lpm_mode(wcd, en);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_enter_lpm_mode);

int __wireless_charger_get_fw_version(struct wireless_manager *m, u32 *ver)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m || !ver)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->get_wireless_fw_version) {
			return ops->get_wireless_fw_version(wcd, ver);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_get_fw_version);

int __wireless_charger_get_wired_state(struct wireless_manager *m, bool *state)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m || !state)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->get_wired_state) {
			return ops->get_wired_state(wcd, state);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_get_wired_state);

int __wireless_charger_get_pg(struct wireless_manager *m, bool *pg_status)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m || !pg_status)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->get_wireless_pg) {
			return ops->get_wireless_pg(wcd, pg_status);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_get_pg);

int __wireless_charger_get_vbus(struct wireless_manager *m, int *vbus)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m || !vbus)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->get_wireless_vbus) {
			return ops->get_wireless_vbus(wcd, vbus);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_get_vbus);

int __wireless_charger_get_ibus(struct wireless_manager *m, int *ibus)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m || !ibus)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->get_wireless_ibus) {
			return ops->get_wireless_ibus(wcd, ibus);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_get_ibus);

int __wireless_charger_set_plug_in(struct wireless_manager *m)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->set_wireless_plug_in) {
			return ops->set_wireless_plug_in(wcd);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_set_plug_in);

int __wireless_charger_set_plug_out(struct wireless_manager *m)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->set_wireless_plug_out) {
			return ops->set_wireless_plug_out(wcd);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_set_plug_out);

int __wireless_charger_get_capacity(struct wireless_manager *m,
		struct wls_hw_info *hw_info)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m || !hw_info)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->get_wireless_hw_info) {
			return ops->get_wireless_hw_info(wcd, hw_info);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_get_capacity);

int wireless_wirte_data(struct wireless_manager *m, int reg, u8 data)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->wirte_reg_data) {
			return ops->wirte_reg_data(wcd, reg, data);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(wireless_wirte_data);

int wireless_read_data(struct wireless_manager *m, int reg, u8 *data)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->read_reg_data) {
			return ops->read_reg_data(wcd, reg, data);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(wireless_read_data);

int set_tx_mode_prepare(struct wireless_manager *m, bool en)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->tx_mode_prepare) {
			return ops->tx_mode_prepare(wcd, en);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(set_tx_mode_prepare);

int __wireless_charger_authenticate(struct wireless_manager *m, bool *en)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m || !en)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->get_wireless_authenticate) {
			return ops->get_wireless_authenticate(wcd, en);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_authenticate);

int __wireless_charger_set_voltage(struct wireless_manager *m, int mv, bool shutdown)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->set_wireless_voltage) {
			return ops->set_wireless_voltage(wcd, mv, shutdown);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_set_voltage);

int __wireless_charger_set_volt_sync(struct wireless_manager *m, u32 mv)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->set_wireless_volt_sync) {
			return ops->set_wireless_volt_sync(wcd, mv);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_set_volt_sync);

int __wireless_charger_dump_status(struct wireless_manager *m)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->dump_wireless_status) {
			return ops->dump_wireless_status(wcd);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_dump_status);

int __wireless_charger_get_online(struct wireless_manager *m, bool *online)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m || !online)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->get_wireless_online) {
			return ops->get_wireless_online(wcd, online);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_get_online);

int __wireless_charger_get_epp_status(struct wireless_manager *m, bool *epp)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m || !epp)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->get_wireless_epp_status) {
			return ops->get_wireless_epp_status(wcd, epp);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_get_epp_status);

int __set_wired_path_setup(struct wireless_manager *m, bool en)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->wired_path_setup) {
			return ops->wired_path_setup(wcd, en);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__set_wired_path_setup);

int __wireless_charger_set_tx_mode(struct wireless_manager *m, bool en, struct tx_config *txc)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops->set_wireless_tx_mode) {
			return ops->set_wireless_tx_mode(wcd, en, txc);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_set_tx_mode);

int __wireless_charger_get_power(struct wireless_manager *m, int *value)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m || !value)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->get_wireless_power) {
			return ops->get_wireless_power(wcd, value);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_get_power);

int __wireless_charger_get_wls_protocol(struct wireless_manager *m)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->get_wls_protocol) {
			return ops->get_wls_protocol(wcd);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_get_wls_protocol);

int __wirless_charger_magnetism_fan_check(struct wireless_manager *m, struct wls_hw_info *hw_info)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->magnetism_fan_check) {
			return ops->magnetism_fan_check(wcd, hw_info);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wirless_charger_magnetism_fan_check);


int __wireless_charger_negotiate_power(struct wireless_manager *m)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->wireless_negotiate_power) {
			return ops->wireless_negotiate_power(wcd);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_negotiate_power);

int __wireless_charger_product_info(struct wireless_manager *m)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->wireless_product_info) {
			return ops->wireless_product_info(wcd);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_product_info);


int __wireless_charger_power_bank_info(struct wireless_manager *m,
	struct power_bank *pb)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->wireless_power_bank) {
			return ops->wireless_power_bank(wcd, pb);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_power_bank_info);

int __wireless_charger_pb_product_info(struct wireless_manager *m)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->wireless_pb_product_info) {
			return ops->wireless_pb_product_info(wcd);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_pb_product_info);

int __wireless_charger_set_tx_reset(struct wireless_manager *m)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->set_wireless_tx_reset) {
			return ops->set_wireless_tx_reset(wcd);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_set_tx_reset);

int __wireless_charger_set_soft_reset(struct wireless_manager *m)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->set_wireless_soft_reset) {
			return ops->set_wireless_soft_reset(wcd);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_set_soft_reset);

int __wireless_charger_get_adc(struct wireless_manager *m, enum adc_channel chan)
{
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!m)
		return -ENOTSUPP;

	list_for_each_entry(wcd, &m->head, list) {
		ops = wcd->ops;
		if (ops && ops->get_wireless_adc) {
			return ops->get_wireless_adc(wcd, chan);
		}
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(__wireless_charger_get_adc);

void unregister_wireless_charger_device(struct wireless_charger *wcd)
{
	list_del(&wcd->list);
}
EXPORT_SYMBOL(unregister_wireless_charger_device);

int register_wireless_charger_device(struct wireless_charger *wcd, void *data)
{
	struct charger_device *cdev = wcd->wm_chg;
	struct wireless_manager *m = NULL;

	if (data && cdev) {
		m = dev_get_drvdata(&cdev->dev);
		spin_lock(&m->lock_register);
		wcd->private_d = data;
		list_add_tail(&wcd->list, &m->head);
		spin_unlock(&m->lock_register);
		return 0;
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(register_wireless_charger_device);

inline u32 SizeofPkt(u8 hdr)
{
	if (hdr < 0x20)
		return 1;

	if (hdr < 0x80)
		return (2 + ((hdr - 0x20) >> 4));

	if (hdr < 0xe0)
		return (8 + ((hdr - 0x80) >> 3));

	return (20 + ((hdr - 0xe0) >> 2));
}
EXPORT_SYMBOL(SizeofPkt);

inline void decode_tx_vmax(int product, struct wls_hw_info *hw_info)
{
	u8 tx_vmax = (product >> TX_VMAX_SHIFT) & TX_VMAX_MASK;

	switch (tx_vmax) {
	case 0:
		hw_info->tx_vmax = 15000;
		break;
	case 1:
		hw_info->tx_vmax = 22000;
		break;
	case 2:
		hw_info->tx_vmax = 33000;
		break;
	default:
		pr_err("unknow tx vmax\n");
		hw_info->tx_vmax = 15000;
	}
}
EXPORT_SYMBOL(decode_tx_vmax);

inline void decode_tx_imax(int product, struct wls_hw_info *hw_info)
{
	u8 tx_imax = (product >> TX_IMAX_SHIFT) & TX_IMAX_MASK;

	switch (tx_imax) {
	case 0:
		hw_info->tx_imax = 3000;
		break;
	case 1:
		hw_info->tx_imax = 6000;
		break;
	case 2:
		hw_info->tx_imax = 2000;
		break;
	case 3:
		hw_info->tx_imax = 1500;
		break;
	default:
		pr_err("unknow tx imax\n");
		hw_info->tx_imax = 2000;
	}
}
EXPORT_SYMBOL(decode_tx_imax);

inline void decode_tx_magnetism(int product, struct wls_hw_info *hw_info)
{
	u8 magnetism = (product >> TX_MAGNETISM_SHIFT) & TX_MAGNETISM_MASK;

	switch (magnetism) {
	case 0:
		hw_info->tx_magnetism = false;
		break;
	case 1:
	case 2:
		hw_info->tx_magnetism = true;
		break;
	default:
		pr_err("unknow tx tx_magnetism\n");
	}
}
EXPORT_SYMBOL(decode_tx_magnetism);

inline void decode_tx_type(int product, struct wls_hw_info *hw_info)
{
	hw_info->power_bank = (product >> TX_TYPE_SHIFT) & TX_TYPE_MASK;
}
EXPORT_SYMBOL(decode_tx_type);

inline void decode_tx_magnetism_fan(int product, struct wls_hw_info *hw_info)
{
	hw_info->tx_magnetism_fan = (product >> TX_MAGNETISM_FAN_SHIFT) & TX_MAGNETISM_FAN_MASK;
}
EXPORT_SYMBOL(decode_tx_magnetism_fan);


inline void decode_tx_bridge_voltage(int product, struct wls_hw_info *hw_info)
{
	u8 tx_bridge_vol = (product >> TX_BRIDGE_VOLTAGE_SHIFT)
			& TX_BRIDGE_VOLTAGE_MASK;

	switch (tx_bridge_vol) {
	case 0:
		hw_info->tx_bridge_vol_max = 15000;
		break;
	case 1:
		hw_info->tx_bridge_vol_max = 22000;
		break;
	case 2:
		hw_info->tx_bridge_vol_max = 33000;
		break;
	default:
		pr_err("unknow tx tx_bridge_vol_max\n");
	}
}
EXPORT_SYMBOL(decode_tx_bridge_voltage);

inline char *wls_protocol_mode_name(int num)
{
	switch (num) {
	case 0:
		return "ac_missing";
	case 1:
		return "bpp";
	case 2:
		return "mpp_restricted";
	case 3:
		return "mpp_full";
	case 4:
		return "mpp_cloak";
	case 5:
		return "mpp_cloak_force";
	case 6:
		return "epp";
	case 7:
		return "transsion";
	}

	return "unknow";
}
EXPORT_SYMBOL(wls_protocol_mode_name);
