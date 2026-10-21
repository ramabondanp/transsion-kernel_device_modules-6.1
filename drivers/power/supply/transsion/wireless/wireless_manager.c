// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Transsion Inc.
 */

#define pr_fmt(fmt) "wireless_manager: " fmt
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <uapi/linux/sched/types.h>
#include <linux/sched.h>
#include "wireless_class.h"
#include "wireless_manager.h"
#include "wireless_charger.h"
#include "tc_charger.h"
#include "tc_misc_intf.h"

static void wireless_reverse_charge(struct wireless_manager *m, bool enable);
static void open_boost_for_update_fw(struct wireless_manager *m, bool enable);
static int wireless_charger_fw_update(struct wireless_manager *m, bool wired);
static char fw_update[10] = {0};

static const struct config_desc wls_desc = {
        .low_inductions = false,
        .sw_vboost = 5300000,
        .sw_iboost = 2000000,
        .done_interval = 120,
	.wireless_init_input_current = WIRELESS_INIT_INPUT_CURRENT,
	.wireless_init_charger_current = WIRELESS_INIT_CHARGER_CURRENT,
	.boost_pd = 0,
	.boost_mode = 0,
	/* 0 tx bpp, 1 tx epp */
	.tx_protocol = 0,
	.bank_soc_support = 1,
	.tx_power = 5,
};

static int notify_uevent(struct wireless_manager *m)
{
	int ret = 0;
	char *start = "ITranCharger,POWER_BANK_SOC";
	char *stop = "ITranCharger,POWER_BANK_SOC_STOP";
	char *env[2] = {NULL, NULL};

	if (m->desc.bank_soc_support == 0)
	{
		return -EINVAL;
	}

	if (m->power_bank) {
		env[0] = start;
	} else {
		env[0] = stop;
	}

	if (IS_ERR(m))
		return ret;

	ret = kobject_uevent_env(&m->pdev->dev.kobj, KOBJ_CHANGE, env);
	if (ret)
		pr_info("%s: kobject_uevent_fail, ret=%d", __func__, ret);

	return ret;
}

static int wireless_get_uisoc(struct wireless_manager *m)
{
	return tc_get_uisoc();
}

static bool check_is_usb_rdy(struct device *dev)
{
	struct device_node *node;
	bool ready = false;

	node = of_parse_phandle(dev->of_node, "usb", 0);
	if (node) {
		ready = !of_property_read_bool(node, "cdp-block");
	} else {
		dev_info(dev, "usb node missing or invalid\n");
	}

	return ready;
}

static int wireless_check_usb_rdy(struct wireless_manager *m)
{
	int i;
	int ret = 0;
	int max_wait_cnt = 200;

	for (i = 0; i < max_wait_cnt; i++) {
		if (check_is_usb_rdy(&m->pdev->dev)) {
			break;
		}
		msleep(100);
	}

	if (i == max_wait_cnt) {
		pr_err("%s:CDP timeout\n", __func__);
		ret = -EINVAL;
	}

	pr_info("%s:CDP free\n", __func__);
	return ret;
}

static int wireless_power_supply_changed(bool online)
{
	int ret = 0;
	struct power_supply *psy = NULL;
	union power_supply_propval propval = {0};

	psy = power_supply_get_by_name("wireless");
	if (!psy) {
		pr_info("%s: get power supply failed\n", __func__);
		return -EINVAL;
	}

	propval.intval = online;
	ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_ONLINE, &propval);
	if (ret < 0) {
		pr_err("%s: online set failed, ret = %d\n", __func__, ret);
	} else {
		pr_info("%s: online = %d\n", __func__, propval.intval);
	}

	propval.intval = online ? POWER_SUPPLY_TYPE_WIRELESS : POWER_SUPPLY_TYPE_UNKNOWN;

	ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_TYPE, &propval);
	if (ret < 0) {
		pr_err("%s: psy type failed, ret = %d\n", __func__, ret);
	} else {
		pr_info("%s: chg_type = %d\n", __func__, propval.intval);
	}

	propval.intval = online ? POWER_SUPPLY_USB_TYPE_DCP : POWER_SUPPLY_USB_TYPE_UNKNOWN;

	ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_USB_TYPE, &propval);
	if (ret < 0) {
		pr_err("%s: psy type failed, ret = %d\n", __func__, ret);
	} else {
		pr_info("%s: usb_type = %d\n", __func__, propval.intval);
	}

	//power_supply_changed(psy);
	return ret;
}

static void wls_chg_start_timer(struct wireless_manager *m, u32 time_sec)
{
	ktime_t ktime;
	int ret = 0;

	/* If the timer was already set, cancel it */
	ret = alarm_try_to_cancel(&m->alarm_uevent);
	if (ret < 0) {
		pr_err("Callbackback was running, skip timer\n");
		return;
	}

	ktime = ktime_set(time_sec, 0);
	alarm_start_relative(&m->alarm_uevent, ktime);
}

static void reverse_charger_for_adb(struct wireless_manager *m, int opener)
{
	struct tx_config txc = {0, 10};
	pr_info("%s opener:%d\n", __func__, opener);

	if (opener == ADB) {
		set_tx_mode_prepare(m, true);
		__wireless_charger_set_tx_mode(m, true, &txc);
		m->reverse_charger = true;
	} else if (opener == RC_CLOSE) {
		set_tx_mode_prepare(m, false);
		__wireless_charger_set_tx_mode(m, false, &txc);
		m->reverse_charger = false;
	} else {
		pr_info("%s error cmd!\n", __func__);
	}
}

static void reverse_charger_for_setting(struct wireless_manager *m, int opener)
{
	pr_info("%s opener:%d\n", __func__, opener);

	if (opener == SETTING) {
		wls_chg_start_timer(m, m->desc.done_interval);

		/* 
		 * for some ic rom not enough, tx fw update to ram
		 * system app task not allow access vendor file
		 */
		set_bit(WIRELESS_TX_MODE_RESTART, &m->work_state);
		m->wakeup_thread = true;
		wake_up_interruptible(&m->wait_que);
	} else if (opener == RC_CLOSE) {
		alarm_cancel(&m->alarm_uevent);
		wireless_reverse_charge(m, false);
	} else {
		pr_info("%s error cmd!\n", __func__);
	}
}

static int wireless_get_charger_type(void)
{
	return tc_get_charger_type();
}

static ssize_t reverse_charger_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	struct wireless_manager *m = dev_get_drvdata(dev);
	int val = 0;
	int chg_type = 0;
	int ret; 
	chg_type = wireless_get_charger_type();

	if (buf != NULL && len != 0) {
		ret = kstrtouint(buf, 10, &val);
		if(ret < 0)
			pr_info("%s:kstrtouint fail\n",__func__);

		switch (chg_type) {
		case POWER_SUPPLY_TYPE_UNKNOWN:
			reverse_charger_for_setting(m, val);
			break;
		case POWER_SUPPLY_TYPE_USB:
			reverse_charger_for_adb(m, val);
			break;
		default:
			break;
		}
	}

	return len;
}

static ssize_t reverse_charger_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct wireless_manager *m = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", m->reverse_setup);
}

static ssize_t rxfwver_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int ret = 0;
	u32 fw_version = 0;
	bool wired = false;
	struct wireless_manager *m = dev_get_drvdata(dev);

	__wireless_charger_get_wired_state(m, &wired);
	if (!wired) {
		if (m->boost) {
			pr_info("wired not plug in, boost error\n");
			return snprintf(buf, PAGE_SIZE, "get fw version fail, boost:%d\n", m->boost);
		}
		pr_info("wired not plug in, open boost\n");
		open_boost_for_update_fw(m, true);
	}

	mutex_lock(&m->fw_lock);
	ret = __wireless_charger_get_fw_version(m, &fw_version);
	mutex_unlock(&m->fw_lock);

	if (m->boost) {
		open_boost_for_update_fw(m, false);
	}

	if (ret) {
		return snprintf(buf, PAGE_SIZE, "get fw version fail, ret:%d\n", ret);
	} else {
		return snprintf(buf, PAGE_SIZE, "%04x\n", fw_version);
	}
}

static ssize_t fw_update_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int ret = 0;
	char fw_name[64] = {0};
	int count = 0;
	const char *suffix_name = "fw.bin";
	const struct firmware *fw = NULL;
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;
	struct wireless_manager *m = dev_get_drvdata(dev);

	if (strncmp(fw_update, "tran_fw", strlen("tran_fw"))) {
		return snprintf(buf, PAGE_SIZE, "input error cmd!\n");
	}

	mutex_lock(&m->fw_lock);
	atomic_set(&m->fw_update, 1);
	list_for_each_entry(wcd, &m->head, list) {
		ops = (struct wls_ops *)wcd->ops;
		if (ops && ops->set_wireless_fw_update) {
			snprintf(fw_name, sizeof(fw_name), "%s_%s", wcd->name, suffix_name);
			count = snprintf(buf, PAGE_SIZE, "start update :%s\n", fw_name);
			ret = request_firmware(&fw, fw_name, &m->pdev->dev);
			if (ret) {
				count += snprintf(buf + count, PAGE_SIZE,
					"request %s fw fail, ret:%d\n", fw_name, ret);
				continue;
			}
			ret = ops->set_wireless_fw_update(wcd, fw, true);
			if (ret) {
				count += snprintf(buf + count, PAGE_SIZE,
					"update %s fw fail, ret:%d\n", fw_name, ret);
			}
			count += snprintf(buf + count, PAGE_SIZE, "update :%s succeed\n", fw_name);
			release_firmware(fw);
		}
	}
	atomic_set(&m->fw_update, 0);
	mutex_unlock(&m->fw_lock);
	count += snprintf(buf + count, PAGE_SIZE, "wireless update fw finish!\n");
	return count;
}

static ssize_t fw_update_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t cmd_len)
{
	int len = min(sizeof(fw_update), cmd_len);

	memcpy(fw_update, buf, len);
	return len;
}

static ssize_t bank_soc_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct wireless_manager *m = dev_get_drvdata(dev);

	if (m->desc.bank_soc_support == 0)
	{
		return -EINVAL;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", m->power_bank_soc);
}

static ssize_t tx_magn_fan_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct wireless_manager *m = dev_get_drvdata(dev);

	pr_info("%s:tx_magnetism_fan:%d\n", __func__, m->tx_magnetism_fan);
	return snprintf(buf, PAGE_SIZE, "%d\n", m->tx_magnetism_fan);
}

static char *decode_string(char *source, char *tar, int offset)
{
	int index0,index1;
	char *buf0 = NULL;
	char *buf1 = NULL;
	static char buf[32];

	buf0 = strstr(source, tar);
	if (buf0) {
		index0 = strlen(buf0);
		buf1 = strstr(buf0, ",");
		if (buf1) {
			index1 = strlen(buf1);
			strncpy(buf, buf0 + offset, index0 - index1 - offset);
			return buf;
		}
	}

	return NULL;
}

static ssize_t rx_reg_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t cmd_len)
{
	int reg = 0, data = 0,ret = 0;
	char dbuf[32] = {0};
	char *str = NULL;
	struct wireless_manager *m = dev_get_drvdata(dev);
	int len = min(ARRAY_SIZE(dbuf), cmd_len);

	memcpy(dbuf, buf, len);

	str = decode_string(dbuf, "rd_addr:0x", strlen("rd_addr:0x"));
	if (str) {
		ret = kstrtouint(str, 16, &m->record);
		return len;
	}

	str = decode_string(dbuf, "addr:0x", strlen("addr:0x"));
	if (str == NULL)
		return len;
	ret = kstrtouint(str, 16, &reg);

	str = decode_string(dbuf, "data:0x", strlen("data:0x"));
	if (str == NULL)
		return len;
	ret = kstrtouint(str, 16, &data);

	pr_info("%s, dbuf:%s, reg:%x, data:%x\n", __func__, dbuf, reg, data);
	wireless_wirte_data(m, reg, data);

	return len;
}

static ssize_t rx_reg_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	u8 data = 0;
	struct wireless_manager *m = dev_get_drvdata(dev);

	wireless_read_data(m, m->record, &data);

	return snprintf(buf, PAGE_SIZE, "addr[%x]:%x\n", m->record, data);
}

static ssize_t protocol_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int power = 0;
	struct wls_hw_info hw_info = {0};
	struct wireless_manager *m = dev_get_drvdata(dev);
	int protocol = __wireless_charger_get_wls_protocol(m);

	__wireless_charger_get_power(m, &power);
	if (m->private_protocol == WIRELESS_AUTH_SUCCESS) {
		protocol = 7;
		__wireless_charger_get_capacity(m, &hw_info);
		power = hw_info.tx_pmax / 10;
	}

	return snprintf(buf, PAGE_SIZE, "%s:%d\n",
		wls_protocol_mode_name(protocol), power);
}

#if IS_ENABLED(CONFIG_TRAN_AGING_KOM)
static int fake_rxstat_change(struct wireless_manager *m)
{
	int ret = 0;
	char *env[2] = {"FAKERXSTAT=1", NULL };

	pr_info("%s\n", __func__);

	ret = kobject_uevent_env(&m->pdev->dev.kobj, KOBJ_CHANGE, env);
	if (ret)
		pr_info("%s: kobject_uevent_fail, ret=%d", __func__, ret);

	return ret;
}

static void wireless_fake_rx_detect(struct wireless_manager *m)
{
	int chg_type = 0;
	int ret = 0;

	chg_type = wireless_get_charger_type();

	if ((chg_type != POWER_SUPPLY_TYPE_UNKNOWN)
		&& (chg_type != POWER_SUPPLY_TYPE_WIRELESS)) {
		ret =  __wireless_charger_get_pg(m, &m->pg_status);
		if (!ret) {
			fake_rxstat_change(m);
		}
	}
}

static ssize_t fake_rx_detect_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct wireless_manager *m = dev_get_drvdata(dev);

	pr_info("fake_rxdetect = %d\n", m->pg_status);
	return sprintf(buf, "%u\n", m->pg_status);
}
#endif

static DEVICE_ATTR_RW(reverse_charger);
static DEVICE_ATTR_RW(fw_update);
static DEVICE_ATTR_RO(rxfwver);
static DEVICE_ATTR_RO(bank_soc);
static DEVICE_ATTR_RO(tx_magn_fan);
static DEVICE_ATTR_RW(rx_reg);
static DEVICE_ATTR_RO(protocol);
#if IS_ENABLED(CONFIG_TRAN_AGING_KOM)
static DEVICE_ATTR_RO(fake_rx_detect);
#endif

static struct attribute *wireless_sysfs_attrs[] = {
	&dev_attr_reverse_charger.attr,
	&dev_attr_rxfwver.attr,
	&dev_attr_fw_update.attr,
	&dev_attr_bank_soc.attr,
	&dev_attr_tx_magn_fan.attr,
	&dev_attr_rx_reg.attr,
	&dev_attr_protocol.attr,
#if IS_ENABLED(CONFIG_TRAN_AGING_KOM)
	&dev_attr_fake_rx_detect.attr,
#endif
	NULL,
};

static const struct attribute_group wireless_sysfs_group = {
	.name  = "Rx",
	.attrs = wireless_sysfs_attrs,
};

static void wireless_tran_dev_set_pg_state(struct wireless_manager *m,bool enable)
{
	union com_propval com_val = {0, };

	if (IS_ERR_OR_NULL(m->pid_chg_dev)) {
		m->pid_chg_dev = tran_get_by_name("pid_chg_algo");
		if (IS_ERR_OR_NULL(m->pid_chg_dev)) {
			pr_err("Couldn't get pid_chg_dev\n");
			return;
		}
	}
	com_val.intval = enable;
	if(enable)
		tran_dev_set_prop(m->pid_chg_dev, TRAN_PROP_POWER_PG_ON, &com_val);
	else	
		tran_dev_set_prop(m->pid_chg_dev, TRAN_PROP_POWER_PG_OFF, &com_val);
}

void wireless_do_plug_in(struct wireless_manager *m)
{
	pr_info("%s: plug_in:%d\n", __func__, m->plug_in);
	if (m->plug_in)
		return;

	m->plug_in = true;
	m->plug_out = false;
	m->pa_auth_done = false;
	tc_chg_switch_vbus_ovp(CHARGER_VOLTAGE_WIRELESS_EPP, WIRELESS_VOTER, true);
	wireless_power_supply_changed(true);
	wireless_tran_dev_set_pg_state(m, true);

}

static void wireless_do_plug_out(struct wireless_manager *m, bool schedule_by_work)
{
	pr_info("%s: plug_out:%d\n", __func__, m->plug_out);
	if (m->plug_out)
		return;

	if (m->power_bank) {
		m->power_bank = false;
		notify_uevent(m);
	}
 
	m->chg_done = false;
	m->power_bank_soc = 0xff;
	m->plug_out = true;
	m->plug_in = false;
	m->epp_status = false;
	m->tx_magnetism_fan = false;
	m->max_soc_plugin = false;
	tc_chg_switch_vbus_ovp(CHARGER_VOLTAGE_WIRELESS_EPP, WIRELESS_VOTER, false);
	cancel_delayed_work(&m->power_bank_work);
	flush_delayed_work(&m->power_bank_work);
	if (!schedule_by_work) {
		cancel_delayed_work(&m->ldo_on);
		flush_delayed_work(&m->ldo_on);
	}
	wireless_power_supply_changed(false);
	__wireless_charger_set_plug_out(m);
	m->private_protocol = WIRELESS_AUTH_UNKNOWN;
	wireless_tran_dev_set_pg_state(m, false);
}

static void reverse_charger_to_wired(struct wireless_manager *m, bool wired)
{
	if ((m->reverse_setup || m->reverse_charger == true) && wired) {
		pr_info("%s\n", __func__);
		m->reverse_charger = false;
		schedule_work(&m->uevent_work);
	}
}

static void wireless_charger_to_wired(struct wireless_manager *m, bool wired)
{
	if (m->plug_in && wired) {
		pr_info("%s\n", __func__);
		wireless_do_plug_out(m, false);
	}
}

#define UPDATE_GET_FW_TIMES 10
static int wireless_charger_fw_update(struct wireless_manager *m, bool wired)
{
	int ret = 0, retry = 0;
	char fw_name[64] = {0};
#if IS_ENABLED(CONFIG_TRAN_AGING_KOM)
	const char *suffix_name = "kom_fw.bin";
#else
	const char *suffix_name = "fw.bin";
#endif
	const struct firmware *fw;
	struct wireless_charger *wcd = NULL;
	struct wls_ops *ops = NULL;

	if (!wired && tc_get_vbus() < 4000) {
		pr_info("wired not plug in, open boost,vbus = %d\n", tc_get_vbus());
		open_boost_for_update_fw(m, true);
	}

	mutex_lock(&m->fw_lock);
	atomic_set(&m->fw_update, 1);

	list_for_each_entry(wcd, &m->head, list) {
		ops = (struct wls_ops *)wcd->ops;
		if (ops && ops->set_wireless_fw_update) {
			snprintf(fw_name, sizeof(fw_name), "%s_%s", wcd->name, suffix_name);
			for(retry = 0 ;retry < UPDATE_GET_FW_TIMES; retry ++){
				ret = request_firmware(&fw, fw_name, &m->pdev->dev);
				if (ret) {
					pr_err("request %s fw fail, ret:%d\n", fw_name, ret);
					msleep(500);
					continue;
				}else
					goto update;
			}
			goto update_get_fw_error;

		update:
			ret = ops->set_wireless_fw_update(wcd, fw, false);
			if (ret) {
				pr_err("update %s fw fail, ret:%d\n", fw_name, ret);
			}
			release_firmware(fw);
		}
	}
update_get_fw_error:
	atomic_set(&m->fw_update, 0);
	mutex_unlock(&m->fw_lock);

	if (m->boost) {
		open_boost_for_update_fw(m, false);
	}

	return ret;
}

static int get_power_bank_info(struct wireless_manager *m)
{
	int ret = 0;
	struct power_bank pb = {0};
	struct wls_hw_info hw_info = {0};

	if (!m->power_bank)
		return -ENOTSUPP;

	__wireless_charger_get_capacity(m, &hw_info);
	if (!hw_info.power_bank)
		return -EINVAL;

	ret = __wireless_charger_power_bank_info(m, &pb);
	if (ret < 0) {
		pr_err("%s charger power bank fail, ret:%d\n", __func__, ret);
		return ret;
	}

	m->power_bank = true;
	m->power_bank_soc = pb.soc;

	/* notity power bank soc to hal service */
	pr_info("notify bank soc :%d\n", pb.soc);
	notify_uevent(m);
	return 0;
}

static int wireless_monitor_thread(void *arg)
{
	struct wireless_manager *m = arg;
	volatile unsigned long *state;
	int ret = 0;
	bool wired = false;
	int i = 0;
	struct sched_param param = {.sched_priority = MAX_RT_PRIO / 2 + 1};

	sched_setscheduler(current, SCHED_FIFO, &param);
	while (!kthread_should_stop()) {
		ret = wait_event_interruptible(m->wait_que, m->wakeup_thread);
		if (ret < 0) {
			pr_err("wait event been interrupted(%d)\n", ret);
			continue;
		}

		pm_stay_awake(&m->pdev->dev);
		mutex_lock(&m->thread_lock);
		m->wakeup_thread = false;

next_task:
		state = &m->work_state;

		for(i = 0; (i < WIRELESS_MAX) && *state; i++) {
			/* determine whether the status needs to be started */
			if (!test_and_clear_bit(i, state))
				continue;

			switch (i) {
			case WIRELESS_POWE_BANK:
				ret = get_power_bank_info(m);
				if (!ret) {
					schedule_delayed_work(&m->power_bank_work, HZ * 10);
				}
				break;
			case WIRELESS_RX_EPP_READY:
				/* Keep EPP/BPP only; skip private ASK/auth (no wireless PE50). */
				m->pa_auth_done = true;
				break;
			case WIRELESS_PG_CHANGE:
			#if IS_ENABLED(CONFIG_TRAN_AGING_KOM)
				wireless_fake_rx_detect(m);
			#else
				wireless_check_usb_rdy(m);
				ret = __wireless_charger_get_pg(m, &m->pg_status);
				if (ret)
					break;

				if (!m->pg_status) {
					wireless_do_plug_out(m, false);
				} else {
					wireless_do_plug_in(m);
				}
			#endif
				break;
			case WIRELESS_TX_AC_VALID:
				schedule_work(&m->uevent_work);
				break;
			case WIRELESS_PROBE_END:
				__wireless_charger_enter_sleep(m, false);
				__wireless_charger_get_pg(m, &m->pg_status);
				__wireless_charger_get_wired_state(m, &wired);
				if (m->pg_status && !wired) {
					set_bit(WIRELESS_PG_CHANGE, state);
					break;
				}
				wireless_check_usb_rdy(m);
				__wireless_charger_get_wired_state(m, &wired);
				wireless_charger_fw_update(m, wired);
				m->probe_done = true;
				break;
			case WIRELESS_RX_SR_BR_H2F:
				if (m->rx_setup_mode == HALF_BRIDGE) {
					m->rx_setup_mode = FULL_BRIDGE;
					__wireless_charger_set_voltage(m, 7500, false);
				}
				break;
			case WIRELESS_TX_INIT_DONE:
				if(m->desc.boost_pd == CHARGER_PUMP_4P1){
				charger_dev_enable_hz(m->primary_charger, true);
				msleep(10);
				charger_dev_enable_powerpath(m->primary_charger, true);
				}
				/* switch wireless to tx mode */
				__wireless_charger_get_wired_state(m, &wired);
				if (m->reverse_charger ||wired) {
					break;
				}

				if (__wireless_charger_set_tx_mode(m, true, (struct tx_config *)m->txc) < 0) {
					pr_err("set wireless to tx mode failed\n");
				}
				m->reverse_charger = true;
				break;
			case WIRELESS_TX_MODE_RESTART:
				m->reverse_charger = false;
				wireless_reverse_charge(m, true);
				break;
			default:
				break;
			}
		}

		/* 
		 *	Determine whether the thread is reactivated
		 *	in another state during execution
		 */
		if (*state) {
			pr_debug("next work status : %lx\n", *state);
			goto next_task;
		}

		mutex_unlock(&m->thread_lock);
		pm_relax(&m->pdev->dev);
	}
	return 0;
}

static void wireless_chg_parse_dt(struct wireless_manager *m)
{
	struct device_node *np = m->pdev->dev.of_node;
	static struct tx_config txc = {0};

	memcpy(&m->desc, &wls_desc, sizeof(wls_desc));

	of_property_read_u32(np, "done_interval", &m->desc.done_interval);
	of_property_read_u32(np, "sw_vboost", &m->desc.sw_vboost);
	of_property_read_u32(np, "sw_iboost", &m->desc.sw_iboost);
	of_property_read_u32(np, "boost_pd", &m->desc.boost_pd);
	of_property_read_u8(np, "boost_mode", &m->desc.boost_mode);
	of_property_read_u8(np, "tx_power", &m->desc.tx_power);
	of_property_read_u8(np, "tx_protocol", &m->desc.tx_protocol);
	of_property_read_u32(np, "bank_soc_support", &m->desc.bank_soc_support);
	
	of_property_read_u32(np, "done_interval", &m->desc.done_interval);
	of_property_read_u32(np, "wireless_init_input_current", &m->desc.wireless_init_input_current);
	of_property_read_u32(np, "wireless_init_charger_current", &m->desc.wireless_init_charger_current);

	pr_info("Parse done_interval[%d], sw_vboost:%d, sw_iboost:%d, boost_pd:%d, boost_mode:%d"
		"wireless_init_input_current:%d, wireless_init_charger_current:%d,tx_power=%d,tx_protocol=%d\n",
		m->desc.done_interval, m->desc.sw_vboost, m->desc.sw_iboost, m->desc.boost_pd, m->desc.boost_mode,
		m->desc.wireless_init_input_current, m->desc.wireless_init_charger_current,m->desc.tx_power,m->desc.tx_protocol);
	m->desc.low_inductions = of_property_read_bool(np, "low_inductions");
	m->desc.support_magnetism_fan_check = of_property_read_bool(np, "support_magnetism_fan_check");
	m->desc.support_lpm_down = of_property_read_bool(np, "support_lpm_down");
	txc.power = m->desc.tx_power * 2;
	txc.protocol = m->desc.tx_protocol;
	m->txc = &txc;
}

static char *event_string[WIRELESS_MAX] = {
	[WIRELESS_PROBE_END] 		= "WIRELESS_PROBE_END",
	[WIRELESS_PG_CHANGE] 		= "WIRELESS_PG_CHANGE",
	[WIRELESS_LDO_ON] 			= "WIRELESS_LDO_ON",
	[WIRELESS_WIRED_CHANGE] 	= "WIRELESS_WIRED_CHANGE",
	[WIRELESS_TX_AC_VALID] 		= "WIRELESS_TX_AC_VALID",
	[WIRELESS_RX_EPP_READY] 	= "WIRELESS_RX_EPP_READY",
	[WIRELESS_RX_HW_ERR] 		= "WIRELESS_RX_HW_ERR",
	[WIRELESS_TX_DET_RX] 		= "WIRELESS_TX_DET_RX",
	[WIRELESS_TX_RMV_RX] 		= "WIRELESS_TX_RMV_RX",
	[WIRELESS_TX_MODE_CLOSE] 	= "WIRELESS_TX_MODE_CLOSE",
	[WIRELESS_TX_MODE_RESTART] 	= "WIRELESS_TX_MODE_RESTART",
	[WIRELESS_RX_SR_BR_H2F] 	= "WIRELESS_RX_SR_BR_H2F",
	[WIRELESS_TX_INIT_DONE] 	= "WIRELESS_TX_INIT_DONE",
	[WIRELESS_RX_MPP] 			= "WIRELESS_RX_MPP",
};

static void wireless_charger_notify_state(void *data, int event)
{
	struct wireless_manager *m = data;
	bool wired = false;

	pr_info(":%s, event:%s!\n", __func__, event_string[event]);
	pm_wakeup_event(&m->pdev->dev, 500);

	if (atomic_read(&m->fw_update))
		return;

	mutex_lock(&m->notify_lock);

	switch (event) {
	case WIRELESS_RX_EPP_READY:
		m->epp_status = true;
		set_bit(event, &m->work_state);
		schedule_delayed_work(&m->wake_work, 0);
		flush_delayed_work(&m->wake_work);
        break;
	case WIRELESS_RX_MPP:
		tc_chg_switch_vbus_ovp(CHARGER_VOLTAGE_WIRELESS_EPP, WIRELESS_VOTER, true);
        break;
	case WIRELESS_TX_DET_RX:
		if (!m->tx_det_rx) {
			alarm_cancel(&m->alarm_uevent);
			m->tx_det_rx = true;
		}
        break;
	case WIRELESS_TX_RMV_RX:
		if (m->tx_det_rx) {
			wls_chg_start_timer(m, m->desc.done_interval);
			m->tx_det_rx = false;
		}
		break;
	case WIRELESS_TX_MODE_CLOSE:
		wls_chg_start_timer(m, 0);
		m->tx_det_rx = false;
		break;
	case WIRELESS_PG_CHANGE:
		set_bit(event, &m->work_state);
		cancel_delayed_work(&m->wake_work);
		flush_delayed_work(&m->wake_work);
		__wireless_charger_get_pg(m, &m->pg_status);
		if (m->pg_status){
			schedule_delayed_work(&m->wake_work, HZ / 3);
#if !IS_ENABLED(CONFIG_TRAN_AGING_KOM)
			cancel_delayed_work(&m->ldo_on);
			schedule_delayed_work(&m->ldo_on, HZ * 15);
#endif
		}else{
			memset(&m->wls, 0, sizeof(m->wls));
			schedule_delayed_work(&m->wake_work, HZ * 2);
		}
		break;
	case WIRELESS_LDO_ON:
		cancel_delayed_work(&m->ldo_on);
		flush_delayed_work(&m->ldo_on);
		cancel_delayed_work(&m->power_bank_work);
		flush_delayed_work(&m->power_bank_work);
		break;
	case WIRELESS_WIRED_CHANGE:
		if (__wireless_charger_get_wired_state(m, &wired))
			break;
		m->wired_plugin = wired;
		wireless_charger_to_wired(m, wired);
		reverse_charger_to_wired(m, wired);
		__wireless_charger_enter_sleep(m, wired);
		if (wired)
			schedule_delayed_work(&m->lpm_work, HZ / 10);
		else {
			cancel_delayed_work(&m->lpm_work);
			flush_delayed_work(&m->lpm_work);
		}
		break;
	case WIRELESS_TX_INIT_DONE:
		set_bit(event, &m->work_state);
		schedule_delayed_work(&m->wake_work, HZ / 10);
		break;
	case WIRELESS_TX_MODE_RESTART:
	case WIRELESS_TX_AC_VALID:
	case WIRELESS_RX_SR_BR_H2F:
	case WIRELESS_PROBE_END:
		set_bit(event, &m->work_state);
		schedule_delayed_work(&m->wake_work, 0);
		break;
	case WIRELESS_RX_HW_ERR:
		__wireless_charger_set_tx_reset(m);
		break;
	default:
        break;
    }

	mutex_unlock(&m->notify_lock);
    return;
}

static int wireless_tx_uevent(struct wireless_manager *m)
{
	int ret = 0;
	char *env[2] = {"TXSTAT=1", NULL};

	if (IS_ERR_OR_NULL(m))
		return ret;

	ret = kobject_uevent_env(&m->pdev->dev.kobj, KOBJ_CHANGE, env);
	if (ret)
		pr_err("%s: kobject_uevent_fail, ret=%d", __func__, ret);

	return ret;
}

static enum alarmtimer_restart wl_chg_algo_timer(struct alarm *alarm, ktime_t now)
{
	struct wireless_manager *m =
		container_of(alarm, struct wireless_manager, alarm_uevent);

	schedule_work(&m->uevent_work);
	return ALARMTIMER_NORESTART;
}

static int wireless_charger_get_auth(struct charger_device *chg_dev, bool *auth)
{
	struct wireless_manager *m = charger_get_data(chg_dev);

	pr_info("%s\n", __func__);
	if (!m->epp_status) {
		*auth = false;
		return 0;
	}

	if (m->private_protocol == WIRELESS_AUTH_SUCCESS || (m->max_soc_plugin && wireless_get_uisoc(m) <= END_MAX_SOC))
		*auth = true;
	else
		*auth = false;

	return 0;
}

static int wireless_charger_get_capacity(struct charger_device *chg_dev, void *hw_info)
{
	struct wireless_manager *m = charger_get_data(chg_dev);

	return __wireless_charger_get_capacity(m, hw_info);
}

static int wireless_charger_set_vbus(struct charger_device *chg_dev, int volt)
{
	struct wireless_manager *m = charger_get_data(chg_dev);

	return __wireless_charger_set_voltage(m, volt, false);
}

static int wireless_charger_get_vbus(struct charger_device *chg_dev, int *volt)
{
	struct wireless_manager *m = charger_get_data(chg_dev);

	return __wireless_charger_get_vbus(m, volt);
}

static int wireless_charger_get_ibus(struct charger_device *chg_dev, int *ibus)
{
	struct wireless_manager *m = charger_get_data(chg_dev);

	return __wireless_charger_get_ibus(m, ibus);
}

static int wireless_charger_set_volt_sync(struct charger_device *chg_dev, u32 mv)
{
	struct wireless_manager *m = charger_get_data(chg_dev);

	return __wireless_charger_set_volt_sync(m, mv);
}

static int wireless_charger_get_pg_status(struct charger_device *chg_dev, bool *pg)
{
	struct wireless_manager *m = charger_get_data(chg_dev);

	*pg = m->pg_status;
	return 0;
}

static int wireless_charger_event(struct charger_device *chg_dev, u32 event, u32 args)
{
	int ret = 0;
	struct wireless_manager *m = charger_get_data(chg_dev);

	switch (event) {
	case EVENT_FULL:
		m->chg_done = true;
		ret = __wireless_charger_set_drop_voltage(m);
		break;
	case EVENT_DISCHARGE:
		m->chg_done = false;
		ret = __wireless_charger_set_drop_voltage(m);
		break;
	case EVENT_RECHARGE:
		m->chg_done = false;
		memset(&m->wls, 0, sizeof(m->wls));
		break;
	default:
		break;
	}

	return ret;
}

static int wireless_charger_get_tx_power(struct charger_device *chg_dev, int *epp_power)
{
	struct wireless_manager *m = charger_get_data(chg_dev);

	return __wireless_charger_get_power(m, epp_power);
}

static int wireless_charger_get_reverse(struct charger_device *chg_dev, bool *state)
{
	struct wireless_manager *m = charger_get_data(chg_dev);

	*state = (m->reverse_setup == true) | m->boost;
	pr_info("%s, state:%d\n", __func__, *state);
	return 0;
}

static int wireless_charger_get_adc(struct charger_device *chg_dev,
						enum adc_channel chan, int *min, int *max)
{
	struct wireless_manager *m = charger_get_data(chg_dev);

	return __wireless_charger_get_adc(m, chan);
}

static int wireless_charger_get_ce(struct charger_device *chg_dev)
{
	struct wireless_manager *m = charger_get_data(chg_dev);
	return __wireless_charger_get_tx_ce_value(m);
}

static int wireless_charger_get_bridge_mode(struct charger_device *chg_dev)
{
	struct wireless_manager *m = charger_get_data(chg_dev);

	m->rx_setup_mode = __wireless_charger_bridge_mode(m);
	return m->rx_setup_mode;
}

static int wireless_charger_set_bridge_mode(struct charger_device *chg_dev)
{
	struct wireless_manager *m = charger_get_data(chg_dev);

	__wireless_charger_set_bridge_mode(m);
	
	return 0;
}

static u8 wireless_charger_get_protocol(struct charger_device *chg_dev)
{
	struct wireless_manager *m = charger_get_data(chg_dev);

	return __wireless_charger_get_wls_protocol(m);
}

static u8 wireless_charger_config_inductions(struct charger_device *chg_dev)
{
	struct wireless_manager *m = charger_get_data(chg_dev);

	return m->desc.low_inductions;
}

static int wireless_charger_select_current_limit(struct charger_device *chg_dev)
{
	struct wireless_manager *m = charger_get_data(chg_dev);

	if (m->chg_done)
		return 0;

	return __wireless_select_current_limit(m);
}

static int wireless_charger_plug_out_reset(struct charger_device *chg_dev)
{
	struct wireless_manager *m = charger_get_data(chg_dev);

	return __wireless_charger_plug_out_reset(m);	
}

static bool wireless_charger_pa_done(struct charger_device *chg_dev)
{
	struct wireless_manager *m = charger_get_data(chg_dev);

	pr_info("%s, pa_auth_done:%d\n", __func__, m->pa_auth_done);
	return m->pa_auth_done;
}

static void wireless_otg_mosfet_ctl(struct charger_device *chg_dev, bool en)
{
	struct wireless_manager *m = charger_get_data(chg_dev);

	pr_info("%s, en:%d\n", __func__, en);

	__set_wired_path_setup(m, en);
}

static struct charger_ops wireless_manager_ops = {
	.get_wireless_authenticate	= wireless_charger_get_auth,
	.get_wireless_capacity		= wireless_charger_get_capacity,
	.set_wireless_vbus		= wireless_charger_set_vbus,
	.get_wireless_vbus		= wireless_charger_get_vbus,
	.get_wireless_ibus		= wireless_charger_get_ibus,
	.set_wireless_volt_sync		= wireless_charger_set_volt_sync,
	.get_wireless_pg_status		= wireless_charger_get_pg_status,
	.get_wireless_tx_power		= wireless_charger_get_tx_power,
	.get_wireless_reverse	 	= wireless_charger_get_reverse,
	.get_adc 			= wireless_charger_get_adc,
	.get_wireless_ce		= wireless_charger_get_ce,
	.get_bridge_mode 		= wireless_charger_get_bridge_mode,
	.set_bridge_mode 		= wireless_charger_set_bridge_mode,
	.get_wls_protocol 		= wireless_charger_get_protocol,
	.event 				= wireless_charger_event,
	.get_config_inductions 		= wireless_charger_config_inductions,
	.select_wireless_cur_limit      = wireless_charger_select_current_limit,
	.plug_out                       = wireless_charger_plug_out_reset,
	.pa_done 					= wireless_charger_pa_done,
	.otg_mosfet_ctl 					= wireless_otg_mosfet_ctl,
};

static int wireless_pd_tcp_notifier_call(struct notifier_block *pnb,
				unsigned long event, void *data)
{
	/* struct tc_tcpc_noti *noti = data; */
	struct wireless_manager *m = container_of(pnb,
		struct wireless_manager, pd_nb);
	struct tc_tcpc_noti *noti = data;
	int pd_type;

	pr_info("%s: event = %lu\n", __func__, event);
	switch (event) {
	case TC_TYPEC_OTG_PLUG_IN:
		m->wired_otg_en = true;
		__wireless_charger_enter_sleep(m, true);
		__wireless_charger_enter_lpm_mode(m,true);
		break;
	case TC_TYPEC_OTG_PLUG_OUT:
		m->wired_otg_en = false;
		__wireless_charger_enter_lpm_mode(m,false);
		__wireless_charger_enter_sleep(m, false);
		break;
	case TC_PD_TYPE:
		pd_type = noti->pd_type;
		switch(pd_type){
			case TC_PD_CONNECT_NONE:
				m->pd_connect_hardreset = false;
				m->pd_connect = false;
				break;
			case TC_PD_CONNECT_PE_READY_SNK_PD30:
			case TC_PD_CONNECT_PE_READY_SNK_APDO:
			case TC_PD_CONNECT_PE_READY_SRC_PD30:
				m->pd_connect_hardreset = false;
				m->pd_connect = true;
				break;
			case TC_PD_CONNECT_TYPEC_ONLY_SNK:
				m->pd_connect_hardreset = false;
				break;
			default:
				break;
		}
		break;
	case TC_PD_CONNECT_HARD_RESET:
		m->pd_connect_hardreset = true;
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int sw_charger_boost_en(struct wireless_manager *m, bool en)
{
	int ret = 0;
	
	if (IS_ERR_OR_NULL(m->primary_charger)) {
		m->primary_charger = get_charger_by_name("primary_chg");
		if(IS_ERR_OR_NULL(m->primary_charger)){
			pr_err("get primary chagrer fail, ret = %d\n", ret);
			return -1;
		}
	}

	/* Configure switch otg output to the maximum */
	if (en) {
	        ret = charger_dev_set_boost_voltage(m->primary_charger, m->desc.sw_vboost);
	        if (ret < 0) {
	                pr_err("set boost voltage failed, ret = %d\n", ret);
	        }
	
	        ret = charger_dev_set_boost_current_limit(m->primary_charger, m->desc.sw_iboost);
	        if (ret < 0) {
	                pr_err("set boost current failed, ret = %d\n", ret);
	        }
	} else {
	        ret = charger_dev_set_boost_current_limit(m->primary_charger, 1000000);
	        if (ret < 0) {
	                pr_err("set boost current failed, ret = %d\n", ret);
	        }
	}

	ret = charger_dev_enable_otg(m->primary_charger, en);
	if (ret < 0) {
	        pr_err("enable otg failed, ret = %d\n", ret);
	        return ret;
	}

	return ret;
}

static int charger_pump_config(struct wireless_manager *m, bool state)
{
	int ret = 0;
	struct charger_device *chg_pump = NULL;

	chg_pump = get_charger_by_name("primary_hv_divider_chg");
	if (!IS_ERR_OR_NULL(chg_pump)) {
		if (state) {
			ret = charger_dev_enable_special_function(chg_pump, HVCHG_DIS_FWD_MODE);
			if (ret) {
				pr_err("%s, charger pump dis fwd mode fail(%d)\n", __func__, ret);
				return ret;
			}

			ret = charger_dev_enable_special_function(chg_pump, HVCHG_EN_CP_MODE);
			if (ret) {
				pr_err("%s, charger pump en cp mode fail(%d)\n", __func__, ret);
				return ret;
			}

			ret = charger_dev_enable_special_function(chg_pump, HVCHG_EN_REV_MODE);
			if (ret) {
				pr_err("%s, charger pump en rev mode fail(%d)\n", __func__, ret);
				return ret;
			}
		} else {
			charger_dev_enable_special_function(chg_pump, HVCHG_SOFT_RESET);
		}
	}

	return 0;
}


static void dvchg_shutdown_config(struct wireless_manager *m)
{
	if(IS_ERR_OR_NULL(m->dvchg1_dev)) {
		m->dvchg1_dev = get_charger_by_name("primary_dvchg");
		if(IS_ERR_OR_NULL(m->dvchg1_dev)){
			pr_err("%s, dvchg1_dev get fail\n", __func__);
			return;
		}
	}
	charger_dev_enable_special_function(m->dvchg1_dev, HVCHG_WIRELESS_LPM_MODE);

	return;
}

static int __wireless_set_reverse_boost(struct wireless_manager *m, bool en)
{
	int ret = 0;
	u32 boost_pd = m->desc.boost_pd;
	u8 boost_mode = m->desc.boost_mode;
	struct charger_device *rb = get_charger_by_name("reverse_boost");

	switch (boost_pd) {
	case SWITCH_CHARGER:
		ret = sw_charger_boost_en(m, en);
		break;
	case CHARGER_PUMP_2P1:
		ret = sw_charger_boost_en(m, en);
		if (ret) {
			pr_err("%s, open charger boost fail(%d)\n", __func__, ret);
			break;
		}
		ret = charger_pump_config(m, en);
		break;
	case CHARGER_PUMP_4P1:
		if(IS_ERR_OR_NULL(m->dvchg1_dev)) {
			m->dvchg1_dev = get_charger_by_name("primary_dvchg");
			if(IS_ERR_OR_NULL(m->dvchg1_dev)){
				pr_err("%s, dvchg1_dev get fail\n", __func__);
				break;
			}
		}

		if (IS_ERR_OR_NULL(m->primary_charger)) {
		m->primary_charger = get_charger_by_name("primary_chg");
		if(IS_ERR_OR_NULL(m->primary_charger)){
			pr_err("get primary chagrer fail, ret = %d\n", ret);
			break;
			}
		}

		if(en) {
			charger_dev_enable_powerpath(m->primary_charger, false);
			charger_dev_enable_special_function(m->dvchg1_dev, boost_mode);
			charger_dev_enable_special_function(m->dvchg1_dev, HVCHG_EN_REV_MODE);
		} else {
			charger_dev_enable_hz(m->primary_charger, false);
			charger_dev_enable_special_function(m->dvchg1_dev, HVCHG_NORMAL_MODE);
		}
		break;
	case EXTERNAL_BOOST:
		ret = charger_dev_enable_powerpath(m->primary_charger, true);
		if (ret) {
			pr_err("%s, enable hz fail(%d)\n", __func__, ret);
			break;
		}

		ret = charger_dev_set_reverse_boost(rb, en);
		break;
	default:
		return -EINVAL;
	}
	
	return ret;
}

static void open_boost_for_update_fw(struct wireless_manager *m, bool enable)
{
	int ret = 0;

	pr_info("%s, enable:%d\n", __func__, enable);

	if (enable) {
		m->boost = true;
		/* Close wired path setup */
		ret = __set_wired_path_setup(m, true);
		if (ret < 0) {
			pr_err("set wired access failed, ret = %d\n", ret);
			goto out;
		}

		ret = sw_charger_boost_en(m, true);
		if (ret) {
			pr_err("set reverse boost fail\n");
			goto boost_exit;
		}

		if (m->desc.boost_pd == EXTERNAL_BOOST) {
			__wireless_charger_set_ovp_ctrl(m, false);
		}

		msleep(300);
		return;
	}

	/* disable wireless reverse flow */
	if(!m->wired_otg_en){
	ret = sw_charger_boost_en(m, false);
	if (ret) {
		pr_err("set reverse boost fail\n");
		}
	}

	msleep(50);

	if (m->desc.boost_pd == EXTERNAL_BOOST) {
		__wireless_charger_set_ovp_ctrl(m, true);
	}

	msleep(200);

boost_exit:
	/* Switch cp to auto mode */
	ret = __set_wired_path_setup(m, false);
	if (ret < 0) {
		pr_err("set wired access failed, ret = %d\n", ret);
	}
out:
	m->boost = false;
}

static void wireless_reverse_charge(struct wireless_manager *m, bool enable)
{
	int ret = 0;

	if(m->reverse_setup == enable) {
		pr_err("repeat entry\n");
		return;
	}

	m->tx_det_rx = false;
	m->reverse_charger = false;

	mutex_lock(&m->reverse_lock);

	if (enable) {
		/* Close wired path setup */
		ret = __set_wired_path_setup(m, true);
		if (ret < 0) {
			pr_err("set wired access failed, ret = %d\n", ret);
			goto wired_path_exit;
		}
		m->reverse_setup = true;
		ret = __wireless_set_reverse_boost(m, true);
		if (ret) {
			pr_err("set reverse boost fail\n");
			goto boost_exit;
		}
		//m->reverse_charger = true;
		if (m->desc.boost_pd == EXTERNAL_BOOST) {
			__wireless_charger_set_ovp_ctrl(m, false);
		}

		set_tx_mode_prepare(m, true);
		mutex_unlock(&m->reverse_lock);
		return;
	}
	set_tx_mode_prepare(m, false);

	/* switch wireless to rx mode */
	ret = __wireless_charger_set_tx_mode(m, false, (struct tx_config *)m->txc);
	if (ret < 0) {
		pr_err("wireless to rx mode failed, ret = %d\n", ret);
	}

	/* disable wireless reverse flow */
	ret = __wireless_set_reverse_boost(m, false);
	if (ret) {
		pr_err("set reverse boost fail\n");
	}
	msleep(50);

	if (m->desc.boost_pd == EXTERNAL_BOOST) {
		__wireless_charger_set_ovp_ctrl(m, true);
	}

boost_exit:
	/* Switch cp to auto mode */
	ret = __set_wired_path_setup(m, false);
	if (ret < 0) {
		pr_err("set wired access failed, ret = %d\n", ret);
	}

wired_path_exit:
	m->reverse_setup = false;
	mutex_unlock(&m->reverse_lock);
}

static void wireless_charger_uevent_work(struct work_struct *data)
{
	struct wireless_manager *m = NULL;
	m = container_of(data, struct wireless_manager, uevent_work);

	pr_info("wireless reverse charger count done, notify close\n");
	wireless_tx_uevent(m);
}

static void wakeup_state_machine(struct work_struct *data)
{
	struct wireless_manager *m = container_of(data, 
					struct wireless_manager,
					wake_work.work);
	m->wakeup_thread = true;
	wake_up_interruptible(&m->wait_que);
}

static void power_bank_func(struct work_struct *data)
{
	struct wireless_manager *m = container_of(data, 
					struct wireless_manager,
					power_bank_work.work);
	
	pr_info("%s\n", __func__);
	if (m->desc.bank_soc_support == 0)
	{
		return;
	}
	set_bit(WIRELESS_POWE_BANK, &m->work_state);
	m->wakeup_thread = true;
	wake_up_interruptible(&m->wait_que);
}

static void ldo_on_check(struct work_struct *data)
{
	struct wireless_manager *m = container_of(data, 
					struct wireless_manager,
					ldo_on.work);

	pr_info("%s\n", __func__);
	wireless_do_plug_out(m, true);
}

/*Add for PD test, TD 4.10.3 usb suspend current*/
static void lpm_setting_work(struct work_struct *data)
{
	struct wireless_manager *m = container_of(data,
					struct wireless_manager,
					lpm_work.work);

	if (m->probe_done && !m->shutdown) 
		__wireless_charger_enter_lpm_mode(m, m->wired_plugin);
	else
		schedule_delayed_work(&m->lpm_work, HZ * 5);
	pr_info("%s probe_done(%d)\n", __func__, m->probe_done);
}

static int tc_wireless_get_property(struct tran_device *dev,
				enum tran_common_prop prop,
				union com_propval *val)
{
	int ret = 0;
	struct wireless_manager *m = tran_get_data(dev);
	struct wls_hw_info hw_info = {0};

	switch (prop) {
	case TRAN_PROP_POWER_NOW:
		__wireless_charger_get_capacity(m, &hw_info);
		val->intval = min(hw_info.tx_pmax, hw_info.ta_pmax) / 10;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int tc_wireless_set_property(struct tran_device *dev,
					   enum tran_common_prop prop,
					   const union com_propval *val)
{
	int ret = 0;
	switch (prop) {
	default:
		ret = -EINVAL;
	}

	return ret;
}

static struct tran_ops tc_wireless_ops = {
	.get_prop = tc_wireless_get_property,
	.set_prop = tc_wireless_set_property,
};

static int tc_wireless_prop_init(struct wireless_manager *m)
{

        m->tc_wireless_props.alias_name = "tc_wireless";
	m->tc_wireless_dev = tran_device_register("tc_wireless",
						m->dev, m,
						&tc_wireless_ops,
						&m->tc_wireless_props);
	if (IS_ERR_OR_NULL(m->tc_wireless_dev))
		return -ENODEV;

	return 0;
}

static int wireless_manager_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct wireless_manager *m = NULL;

	pr_info("%s start!\n", __func__);

	m = devm_kzalloc(&pdev->dev, sizeof(*m), GFP_KERNEL);
	if (!m)
		return -ENOMEM;

	m->primary_charger = get_charger_by_name("primary_chg");
	if (IS_ERR_OR_NULL(m->primary_charger)) {
		pr_err("%s: get primary charger device failed\n", __func__);
		return -EPROBE_DEFER;
	}

	m->tc_chg_dev = tran_get_by_name("tc_charger");
	if (IS_ERR_OR_NULL(m->tc_chg_dev)) {
		pr_err("%s: get tc charger device failed\n", __func__);
		return -EPROBE_DEFER;
	}

	m->pid_chg_dev = tran_get_by_name("pid_chg_algo");
	if (IS_ERR_OR_NULL(m->pid_chg_dev)) {
		pr_err("%s: get pid charger device failed\n", __func__);
	}

	m->pdev = pdev;
	m->dev = &pdev->dev;

	platform_set_drvdata(pdev, m);

	INIT_LIST_HEAD(&m->head);
	m->state_call_back = wireless_charger_notify_state;

	mutex_init(&m->thread_lock);
	mutex_init(&m->reverse_lock);
	mutex_init(&m->fw_lock);
	mutex_init(&m->notify_lock);
	atomic_set(&m->fw_update, 0);
	spin_lock_init(&m->lock_register);

	init_waitqueue_head(&m->wait_que);

	m->reverse_charger = -1;
	m->plug_out = true;

	device_init_wakeup(&pdev->dev, true);
	INIT_WORK(&m->uevent_work, wireless_charger_uevent_work);
	INIT_DELAYED_WORK(&m->wake_work, wakeup_state_machine);
	INIT_DELAYED_WORK(&m->power_bank_work, power_bank_func);
	INIT_DELAYED_WORK(&m->ldo_on, ldo_on_check);
	INIT_DELAYED_WORK(&m->lpm_work, lpm_setting_work);

	wireless_chg_parse_dt(m);

	m->chg_props.alias_name = "wireless_manager";
	m->wireless_chg = charger_device_register("wireless_manager",
			&pdev->dev, m, &wireless_manager_ops, &m->chg_props);
	if (IS_ERR_OR_NULL(m->wireless_chg)) {
		pr_err("register wireless charger device failed\n");
		goto err_register_chg_dev;
	}

	ret = tc_wireless_prop_init(m);
	if (ret < 0) {
		pr_err("%s: register tran dev fail(%d)\n",  __func__, ret);
		goto err_register_chg_dev;
	}

	m->pd_nb.notifier_call = wireless_pd_tcp_notifier_call;
	ret = register_tc_tcpc_notifier(&m->pd_nb);
	if (ret < 0) {
		pr_err("%s: register tcpc notifier fail(%d)\n",  __func__, ret);
		goto err_register_tcpc_notify;
	}

	m->task = kthread_run(wireless_monitor_thread, m, "wireless_thread");
	if (IS_ERR_OR_NULL(m->task)) {
		pr_err("%s: creat kthread fail\n",  __func__);
		goto err_kthread_run;
	}

	/* create sysfs node */
	ret = sysfs_create_group(&pdev->dev.kobj, &wireless_sysfs_group);

    m->kobj = kobject_create_and_add("wireless", NULL);
	if (!m->kobj) {
		pr_err("%s: sysfs_create_group fail\n", __func__);
		goto err_kobject_create;
	}

	ret = sysfs_create_link(m->kobj, &pdev->dev.kobj, "wireless");
	if (ret < 0) {
		pr_err("%s: sysfs_create_link failed\n", __func__);
		goto err_create_sysfs_link;
	}

	alarm_init(&m->alarm_uevent, ALARM_REALTIME, wl_chg_algo_timer);
	pr_err("%s: successful\n",  __func__);
	return 0;

err_create_sysfs_link:
	sysfs_remove_group(&pdev->dev.kobj, &wireless_sysfs_group);
err_kobject_create:
	kthread_stop(m->task);
err_kthread_run:
	unregister_tc_tcpc_notifier(&m->pd_nb);
err_register_tcpc_notify:
err_register_chg_dev:
	mutex_destroy(&m->thread_lock);
	mutex_destroy(&m->reverse_lock);
	mutex_destroy(&m->fw_lock);
	mutex_destroy(&m->notify_lock);
	return -EINVAL;
}

static int wireless_manager_remove(struct platform_device *dev)
{
	struct wireless_manager *m = platform_get_drvdata(dev);

	pr_info("%s\n", __func__);
	sysfs_remove_link(m->kobj, "wireless");
	kobject_put(m->kobj);
	sysfs_remove_group(&m->pdev->dev.kobj, &wireless_sysfs_group);

	mutex_destroy(&m->thread_lock);
	mutex_destroy(&m->reverse_lock);
	mutex_destroy(&m->fw_lock);

	unregister_tc_tcpc_notifier(&m->pd_nb);
	charger_device_unregister(m->wireless_chg);
	kfree(m->wireless_chg);
	devm_kfree(&m->pdev->dev, m);
	return 0;
}

static int wireless_manager_resume(struct device *dev)
{
	struct wireless_manager *m = dev_get_drvdata(dev);

	if (m->power_bank)
		schedule_delayed_work(&m->power_bank_work, HZ);

	return 0;
}

static int wireless_manager_suspend(struct device *dev)
{
	struct wireless_manager *m = dev_get_drvdata(dev);

	if (m->power_bank) {
		cancel_delayed_work(&m->power_bank_work);
		flush_delayed_work(&m->power_bank_work);
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(wireless_manager_pm_ops,
	wireless_manager_suspend, wireless_manager_resume);

static void wireless_manager_shutdown(struct platform_device *dev)
{
	struct wireless_manager *m = platform_get_drvdata(dev);
	bool wired = false;
	int uisoc = 0;
	if(m->desc.support_lpm_down){
		__wireless_charger_get_wired_state(m, &wired);
		uisoc = wireless_get_uisoc(m);
		if(wired && (uisoc > SHUTDOWN_LPM_SOC)) {
			m->shutdown = true;
			dvchg_shutdown_config(m);
			pr_info("%s soc =%d\n", __func__,uisoc);
		}
	}
	__wireless_charger_set_voltage(m, 5500, true);
}

static const struct of_device_id wireless_manager_of_match[] = {
	{ .compatible = "tran,wireless_manager", },
	{},
};

MODULE_DEVICE_TABLE(of, wireless_manager_of_match);

static struct platform_driver wireless_manager = {
	.probe		= wireless_manager_probe,
	.remove		= wireless_manager_remove,
	.shutdown	= wireless_manager_shutdown,
	.driver		= {
		.name = "wireless_manager",
		.owner = THIS_MODULE,
		.of_match_table = wireless_manager_of_match,
		.pm = &wireless_manager_pm_ops,
	},
};

module_platform_driver(wireless_manager);

MODULE_AUTHOR("Transsion Inc.");
MODULE_DESCRIPTION("transsion wirless charger Driver");
MODULE_LICENSE("GPL");
