// SPDX-License-Identifier: GPL-2.0+
/**
 * Copyright (C) Shanghai FourSemi Ltd 2016-2025. All rights reserved.
 * 2025-03-27 File created.
 */

 #include "fsm_public.h"
 #include "fsm_algo_bsg.h"
 #include <linux/workqueue.h>
 #include <linux/of.h>
 #include <linux/mutex.h>
 #include <linux/power_supply.h>
 // FSADSP MODULE ID
 #define AFE_MODULE_ID_FSADSP_TX (0x10001110)
 #define AFE_MODULE_ID_FSADSP_RX (0x10001111)
 // FSADSP PARAM ID
 #define CAPI_V2_PARAM_FSADSP_BSGV2_CONFIG  0x10001FB6 //get
 #define CAPI_V2_PARAM_FSADSP_SYSTEM_INFO   0x10001FC5
 #define FSM_BSG_CHECK_INTERVAL	2
 #define FSM_BSG_CHECK_DSP_MAX	50
 #define FSM_BSG_PSPY_NAME	"battery"
 #define MNTR_1ST_DELAY_MS	10
 #define MNTR_PERIOD_MS		1000

 struct bsg_config_v3 {
	int bsg_mode;
	int bsg_enable;
	int bsg_interval;
	int vstep;
	int tc_mode;
	int tc_mode_enable;
	int tc_interval;
	int tstep;
 };

 struct bsg_param_v2 {
	int bsg_mode;
	int bsg_val;
	int tc_mode;
	int tc_val;
 };

 struct fsm_bsg_monitor {
	struct device *dev;
	struct delayed_work monitor_work;
	unsigned int mntr_1st_delay;
	unsigned int mntr_period;
	uint16_t mntr_scene;
	int bat_vol;
	int bat_cap;
	int bat_temp;
	bool dsp_ready;
	bool mntr_on;
 };

 static bool module_inited;
 static struct fsm_bsg_monitor bsg_monitor;
 static DEFINE_MUTEX(fsm_bsg_mutex);

 extern int mtk_spk_send_ipi_buf_to_dsp(
		void *data_buffer, uint32_t data_size);
 extern int mtk_spk_recv_ipi_buf_from_dsp(
		int8_t *buffer, int16_t size, uint32_t *buf_len);

static int fsm_bsg_get_ambient(struct fsm_bsg_monitor *mntr)
{
	union power_supply_propval prop;
	struct power_supply *psy;
	int ret;

	if (mntr == NULL)
		return -EINVAL;

	psy = power_supply_get_by_name(FSM_BSG_PSPY_NAME);
	if (psy == NULL) {
		dev_err(mntr->dev, "Failed to get power supply!\n");
		return -EINVAL;
	}

	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &prop);
	mntr->bat_vol = DIV_ROUND_CLOSEST(prop.intval, 1000);

	ret |= power_supply_get_property(psy,
			POWER_SUPPLY_PROP_CAPACITY, &prop);
	mntr->bat_cap = prop.intval;

	ret |= power_supply_get_property(psy,
			POWER_SUPPLY_PROP_TEMP, &prop);
	mntr->bat_temp = DIV_ROUND_CLOSEST(prop.intval, 10);

	power_supply_put(psy);
	dev_dbg(mntr->dev, "batv:%d cap:%d tempr:%d\n",
			mntr->bat_vol, mntr->bat_cap, mntr->bat_temp);

	return ret;
}

static int fsm_bsg_get_interval(struct fsm_bsg_monitor *mntr)
{
	struct bsg_config_v3 *cfg;
	int buf[32];
	int rcv_size;
	int ret;

	buf[0] = CAPI_V2_PARAM_FSADSP_BSGV2_CONFIG;
	ret = mtk_spk_send_ipi_buf_to_dsp((void *)buf, sizeof(buf[0]));
	if (ret)
		return ret;

	ret = mtk_spk_recv_ipi_buf_from_dsp((void *)buf,
		sizeof(buf), &rcv_size);
	if (ret)
		return ret;

	cfg = (struct bsg_config_v3 *)buf;
	if (!cfg->bsg_enable && !cfg->tc_mode_enable) {
		dev_info(mntr->dev, "BSG/CSG is disabled\n");
		return -ENOTSUPP;
	}

	mntr->dsp_ready = true;

	if (cfg->bsg_enable)
		mntr->mntr_period = cfg->bsg_interval;

	if (cfg->tc_mode_enable)
		mntr->mntr_period = cfg->tc_interval;

	return 0;
}

static int fsm_bsg_check_dsp_ready(struct fsm_bsg_monitor *mntr)
{
	int retry = 0;
	int ret;

	do {
		if (!mntr->mntr_on)
			break;
		ret = fsm_bsg_get_interval(mntr);
		if (!ret || ret == -ENOTSUPP) {
			return ret;
		} else {
			mdelay(FSM_BSG_CHECK_INTERVAL);
			continue;
		}
	} while (retry++ < FSM_BSG_CHECK_DSP_MAX);

	dev_err(mntr->dev, "Check dsp ready timeout!\n");

	return -EINVAL;
 }

static int fsm_bsg_send_buf_to_dsp(struct fsm_bsg_monitor *mntr,
	int *payload, int size)
{
	int ret;

	ret = fsm_bsg_check_dsp_ready(mntr);
	if (ret) {
		dev_err(mntr->dev, "DSP isn't ready or unsupport\n");
		return ret;
	}

	ret = mtk_spk_send_ipi_buf_to_dsp(payload, size);

	return ret;
}

static int fsm_bsg_send_batt_info(struct fsm_bsg_monitor *mntr)
{
	int payload[32];
	int count = 0;
	int ret;

	ret = fsm_bsg_get_ambient(mntr);
	if (ret) {
		dev_err(mntr->dev, "Failed to get ambient info\n");
		return ret;
	}

	/* send batt prot info */
	payload[count++] = CAPI_V2_PARAM_FSADSP_SYSTEM_INFO;
	payload[count++] = mntr->bat_vol * 1000;
	payload[count++] = mntr->bat_cap;
	payload[count++] = mntr->bat_temp * 10;

	ret = fsm_bsg_send_buf_to_dsp(mntr, payload, count * sizeof(payload[0]));

	return ret;
}

static void fsm_bsg_monitor_work(struct work_struct *work)
{
	struct fsm_bsg_monitor *mntr;
	int ret;

	mntr = container_of(work, struct fsm_bsg_monitor,
		monitor_work.work);

	ret = fsm_bsg_send_batt_info(mntr);
	if (ret) {
		dev_err(mntr->dev, "Failed to send batt info:%d\n", ret);
		return;
	}

	schedule_delayed_work(&mntr->monitor_work,
			msecs_to_jiffies(mntr->mntr_period));
}

int fsm_algo_bsg_init(struct fsm_dev *fsm_dev)
{
	struct fsm_bsg_monitor *mntr;
	struct device_node *np;
	int mntr_scene;
	int ret;

	if (fsm_dev == NULL || fsm_dev->i2c == NULL)
		return -EINVAL;

	mutex_lock(&fsm_bsg_mutex);
	if (module_inited) {
		mutex_unlock(&fsm_bsg_mutex);
		return 0;
	}

	mntr = &bsg_monitor;
	mntr->dev = &fsm_dev->i2c->dev;
	INIT_DELAYED_WORK(&mntr->monitor_work, fsm_bsg_monitor_work);
	mntr->mntr_1st_delay = MNTR_1ST_DELAY_MS;
	mntr->mntr_period = MNTR_PERIOD_MS;
	mntr->dsp_ready = false;

	mntr->mntr_scene = 0;
	np = mntr->dev->of_node;
	ret = of_property_read_u32(np, "fsm,mntr-scene", &mntr_scene);
	if (!ret)
		mntr->mntr_scene = (uint16_t)mntr_scene;

	dev_info(mntr->dev, "mntr-scene: 0x%04x\n", mntr->mntr_scene);
	module_inited = true;
	mutex_unlock(&fsm_bsg_mutex);

	return 0;
}

int fsm_algo_bsg_monitor_switch(bool on)
{
	struct fsm_bsg_monitor *mntr;
	fsm_config_t *cfg;
	bool enable;

	mutex_lock(&fsm_bsg_mutex);
	mntr = &bsg_monitor;
	cfg = fsm_get_config();

	enable = !!(cfg->next_scene & mntr->mntr_scene);
	if (!enable || (mntr->mntr_on ^ on) == 0) {
		mutex_unlock(&fsm_bsg_mutex);
		return 0;
    }

    dev_info(mntr->dev, "bsg monitor %s\n", on ? "On" : "Off");
    mntr->dsp_ready = false;
    mntr->mntr_on = on;
	mutex_unlock(&fsm_bsg_mutex);

	if (on) {
		schedule_delayed_work(&mntr->monitor_work,
				msecs_to_jiffies(mntr->mntr_1st_delay));
	} else {
		cancel_delayed_work_sync(&mntr->monitor_work);
	}

	return 0;
}

void fsm_algo_bsg_deinit(struct fsm_dev *fsm_dev)
{
	mutex_lock(&fsm_bsg_mutex);
	module_inited = false;
	mutex_unlock(&fsm_bsg_mutex);
}
