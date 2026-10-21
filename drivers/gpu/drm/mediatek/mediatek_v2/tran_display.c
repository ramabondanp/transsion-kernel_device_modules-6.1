// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) <2023> Transsion Inc.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_encoder.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/component.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_graph.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <video/mipi_display.h>
#include <video/videomode.h>
#ifndef DRM_CMDQ_DISABLE
#include <linux/soc/mediatek/mtk-cmdq-ext.h>
#else
#include "mtk-cmdq-ext.h"
#endif
#include <linux/ratelimit.h>
#include <soc/mediatek/smi.h>

#include "mtk_drm_ddp_comp.h"
#include "mtk_drm_crtc.h"
#include "mtk_drm_drv.h"
#include "mtk_drm_helper.h"
#include "mtk_mipi_tx.h"
#include "mtk_dump.h"
#include "mtk_log.h"
#include "mtk_drm_lowpower.h"
#include "mtk_drm_mmp.h"
#include "mtk_drm_arr.h"
#include "mtk_panel_ext.h"
#include "mtk_disp_notify.h"
#include "tran_display.h"

extern void mipi_dsi_dcs_write_gce2(struct mtk_dsi *dsi, struct cmdq_pkt *dummy,
					  const void *data, size_t len);
static struct mtk_dsi *g_tran_dsi[DDP_DSI_MAX] = {NULL};

void tran_dsi_init(struct mtk_dsi *dsi)
{
	if (dsi->ddp_comp.id == DDP_COMPONENT_DSI0) {
		g_tran_dsi[DDP_DSI0] = dsi;
	} else if (dsi->ddp_comp.id == DDP_COMPONENT_DSI1) {
		g_tran_dsi[DDP_DSI1] = dsi;
	} else {
		DDPINFO("%s, dsi error\n", __func__);
	}
}

struct mtk_dsi *tran_get_dsi(enum DDP_DSI_NUM index)
{
	if (index >= DDP_DSI_MAX) {
		DDPPR_ERR("%s index %d err!\n", __func__, index);
		return NULL;
	}

	if (g_tran_dsi[index] == NULL) {
		DDPPR_ERR("%s dsi[%d] is null!\n", __func__, index);
		return NULL;
	}

	return g_tran_dsi[index];
}

static ssize_t lcm_name_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%s", g_tran_dsi[DDP_DSI0]->panel->dev->driver->name);
}

static ssize_t lcm_sub_name_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%s", g_tran_dsi[DDP_DSI1]->panel->dev->driver->name);
}

static ssize_t tran_lcm_hbm_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	bool panel_hbm_state = false;

	if (g_tran_dsi[DDP_DSI0] && g_tran_dsi[DDP_DSI0]->ext && g_tran_dsi[DDP_DSI0]->ext->params &&
				g_tran_dsi[DDP_DSI0]->ext->params->tran_panel_params) {
		panel_hbm_state = g_tran_dsi[DDP_DSI0]->ext->params->tran_panel_params->panel_hbm_state;
	}

	return scnprintf(buf, PAGE_SIZE, "0xFA\n");
}
static ssize_t tran_lcm_hbm_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t size)
{
	unsigned long value = 0;
	ssize_t ret = 0;

	ret = kstrtoul(buf, 10, &value);
	if (ret)
		return ret;
	pr_info("%s value= %ld\n", __func__, value);

	if (value == 1) {
		tran_lcm_hbm_set(true);
	} else if (value == 0) {
		tran_lcm_hbm_set(false);
	}
	ret = size;
	return ret;
}

static ssize_t tran_lcm_lcm_dimming_state_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	unsigned int dimming_status = 0;

	if (g_tran_dsi[DDP_DSI0] && g_tran_dsi[DDP_DSI0]->ext && g_tran_dsi[DDP_DSI0]->ext->params &&
				g_tran_dsi[DDP_DSI0]->ext->params->tran_panel_params) {
		dimming_status = g_tran_dsi[DDP_DSI0]->ext->params->tran_panel_params->dimming_status;
	}
	return sprintf(buf, "dimming status:%d", dimming_status);
}
static ssize_t tran_lcm_lcm_dimming_state_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t size)
{
	unsigned int dimming_status = 0;
	unsigned long value = 0;
	ssize_t ret = 0;

	ret = kstrtoul(buf, 10, &value);
	if (ret)
		return ret;
	pr_err("%s value= %ld\n", __func__, value);

	switch (value) {
	case 1024:
	case 0:
		dimming_status = 0;
		break;
	case 1:
	case 2048:
		dimming_status = 1;
		break;
	default:
		ret = -EFAULT;
		goto end;
	}

	if (g_tran_dsi[DDP_DSI0] && g_tran_dsi[DDP_DSI0]->ext && g_tran_dsi[DDP_DSI0]->ext->params &&
		g_tran_dsi[DDP_DSI0]->ext->params->tran_panel_params) {
		int tran_dimming_data = dimming_status == 0 ? TRAN_DISP_BLANK_ENTER_DIMMING_OFF_NOTIFY : TRAN_DISP_BLANK_ENTER_DIMMING_ON_NOTIFY;

		g_tran_dsi[DDP_DSI0]->ext->params->tran_panel_params->dimming_status = dimming_status;
		mtk_disp_notifier_call_chain(MTK_DISP_EVENT_BLANK, &tran_dimming_data);
		pr_err("%s dimming_status = %d notify tran_dimming_data = %d\n", __func__, dimming_status, tran_dimming_data);
	} else {
		pr_err("%s panel dimming_status not init ! %ld\n", __func__, value);
	}

	ret = size;
end:
	return ret;
}

#if IS_ENABLED(CONFIG_TRANSSION_DOZE_BRIGHTNESS_SUPPORT) || IS_ENABLED(CONFIG_TRAN_ONE_DOZE_BRIGHTNESS_SUPPORT)
static ssize_t lcm_doze_backlight_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "lcm_doze_backlight:%d;%d;%d;%d;", g_tran_dsi[DDP_DSI0]->ext->params->tran_panel_params->lcm_doze_backlight->doze_backlight_num,
		g_tran_dsi[DDP_DSI0]->ext->params->tran_panel_params->lcm_doze_backlight->doze_backlight_level1,
		g_tran_dsi[DDP_DSI0]->ext->params->tran_panel_params->lcm_doze_backlight->doze_backlight_level2,
		g_tran_dsi[DDP_DSI0]->ext->params->tran_panel_params->lcm_doze_backlight->doze_backlight_level3);
}
#endif

static ssize_t tran_lcm_low_backlight_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	unsigned int lcm_low_backlight = 0;

	if (g_tran_dsi[DDP_DSI0] && g_tran_dsi[DDP_DSI0]->ext && g_tran_dsi[DDP_DSI0]->ext->params &&
		g_tran_dsi[DDP_DSI0]->ext->params->tran_panel_params) {
		lcm_low_backlight = g_tran_dsi[DDP_DSI0]->ext->params->tran_panel_params->lcm_low_backlight;
	}

	return sprintf(buf, "lcm_low_backlight:%d", lcm_low_backlight);
}

static ssize_t tran_lcm_low_backlight_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t size)
{
	unsigned int value = 0;
	int status = 0;

	if (sscanf(buf, "%d", &value) < 0) {
		status = -EFAULT;
	}
	pr_err("%s value= %d\n", __func__, value);

	if (g_tran_dsi[DDP_DSI0] && g_tran_dsi[DDP_DSI0]->ext && g_tran_dsi[DDP_DSI0]->ext->params &&
		g_tran_dsi[DDP_DSI0]->ext->params->tran_panel_params) {
		g_tran_dsi[DDP_DSI0]->ext->params->tran_panel_params->lcm_low_backlight = value;
	} else {
		pr_err("%s panel lcm_low_backlight not init ! %d\n", __func__, value);
	}

	return size;
}
#define tran_attr(_name, _mode, _show, _store) \
static struct kobj_attribute _name##_attr = {	\
	.attr	= {				\
		.name = __stringify(_name),	\
		.mode = _mode,			\
	},					\
	.show	= _show,			\
	.store	= _store,			\
}
tran_attr(lcm_name, 0444, lcm_name_show, NULL);
tran_attr(lcm_sub_name, 0444, lcm_sub_name_show, NULL);
tran_attr(lcm_hbm_state, 0664, tran_lcm_hbm_show, tran_lcm_hbm_store);
tran_attr(lcm_dimming_state, 0444, tran_lcm_lcm_dimming_state_show, tran_lcm_lcm_dimming_state_store);
#if IS_ENABLED(CONFIG_TRANSSION_DOZE_BRIGHTNESS_SUPPORT) || IS_ENABLED(CONFIG_TRAN_ONE_DOZE_BRIGHTNESS_SUPPORT)
tran_attr(lcm_doze_backlight, 0444, lcm_doze_backlight_show, NULL);
#endif
tran_attr(lcm_low_backlight, 0666, tran_lcm_low_backlight_show, tran_lcm_low_backlight_store);

static struct attribute *lcm[] = {
	&lcm_name_attr.attr,
	&lcm_hbm_state_attr.attr,
	&lcm_dimming_state_attr.attr,
	#if IS_ENABLED(CONFIG_TRANSSION_DOZE_BRIGHTNESS_SUPPORT) || IS_ENABLED(CONFIG_TRAN_ONE_DOZE_BRIGHTNESS_SUPPORT)
	&lcm_doze_backlight_attr.attr,
	#endif
	&lcm_low_backlight_attr.attr,
	NULL,
};

static struct attribute_group lcm_name_attr_group = {
	.attrs = lcm,
};
static struct attribute *lcm_sub[] = {
	&lcm_sub_name_attr.attr,
	NULL,
};

static struct attribute_group lcm_sub_name_attr_group = {
	.attrs = lcm_sub,
};
static struct kobject *mtkfb_sys_kobj;
int mtkfb_register_kernelfs(struct device *dev)
{
	int status = 0;

	if (mtkfb_sys_kobj == NULL) {
		mtkfb_sys_kobj = kobject_create_and_add("tran_display", kernel_kobj);
		if (!mtkfb_sys_kobj) {
			DDPINFO("create mtkfb sysfs fail\n");
			return -1;
		}
	}

	status = sysfs_create_group(mtkfb_sys_kobj, &lcm_name_attr_group);
	if (status) {
		DDPINFO("create mtkfb sysfs files fail\n");
		return -1;
	}

	return 0;
}
EXPORT_SYMBOL(mtkfb_register_kernelfs);

int mtkfb_register_sub_kernelfs(struct device *dev)
{
	int status = 0;

	if (mtkfb_sys_kobj == NULL) {
		mtkfb_sys_kobj = kobject_create_and_add("tran_display", kernel_kobj);
		if (!mtkfb_sys_kobj) {
			DDPINFO("create mtkfb sysfs fail\n");
			return -1;
		}
	}

	status = sysfs_create_group(mtkfb_sys_kobj, &lcm_sub_name_attr_group);
	if (status) {
		DDPINFO("create mtkfb sub sysfs files fail\n");
		return -1;
	}

	return 0;
}
EXPORT_SYMBOL(mtkfb_register_sub_kernelfs);
