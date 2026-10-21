#include <linux/of.h>
#include <linux/of_device.h>
#include "mtk_disp_notify.h"
#include "transsion_core.h"

static int nvt_disp_notifier_callback(struct notifier_block *nb,
				      unsigned long value, void *v)
{
#ifdef CONFIG_DRM_MEDIATEK_V2
	int *data = (int *)v;

	if (!v) {
		TRAN_ERROR("wrong value v check notifier !!");
		return -1;
	}

	if (value == MTK_DISP_EVENT_BLANK) {
		if (*data == MTK_DISP_BLANK_UNBLANK ||
		    *data == TRAN_DISP_BLANK_AOD_NOTIFY) {
			TRAN_INFO("drm notify tp resume start");
			mutex_lock(&g_tran_ts.dsp_event_mutex);
			queue_work(g_tran_ts.tpd_workqueue, &g_tran_ts.tpd_dsp_work);
		}
	} else if (value == MTK_DISP_EARLY_EVENT_BLANK) {
		if (*data == MTK_DISP_BLANK_POWERDOWN) {
			TRAN_INFO("drm notify tp suspends start");
			mutex_lock(&g_tran_ts.dsp_event_mutex);
			cancel_work_sync(&g_tran_ts.tpd_dsp_work);
			if (g_tran_ts.controller_init_state &&
			    g_tran_ts.controller && g_tran_ts.controller->ops &&
			    g_tran_ts.controller->ops->tran_ts_suspend) {
				g_tran_ts.ts_core_mode = TRAN_SUSPEND_MODE;
				g_tran_ts.controller->ops->tran_ts_suspend(NULL);
			} else {
				TRAN_INFO("vendor_probe_result is false!");
			}
			mutex_unlock(&g_tran_ts.dsp_event_mutex);
			TRAN_INFO("drm notify tp suspends end");
		}
	}
#endif
	return 0;
}

static int mtk_register_dsp_notify(struct tran_ts_core *core_data)
{
#ifdef CONFIG_DRM_MEDIATEK_V2
	int ret;

	core_data->dsp_event_block.notifier_call = nvt_disp_notifier_callback;
	ret = mtk_disp_notifier_register("Tran Touch", &core_data->dsp_event_block);
	if (ret)
		TRAN_ERROR("Failed to register disp notifier client:%d", ret);
#endif
	return 0;
}

static int mtk_unregister_dsp_notify(struct tran_ts_core *core_data)
{
#ifdef CONFIG_DRM_MEDIATEK_V2
	if (mtk_disp_notifier_unregister(&core_data->dsp_event_block))
		TRAN_ERROR("Error occurred while unregistering disp_notifier.\n");
#endif
	return 0;
}

static int get_bootargs(char *current_mode, char *boot_param)
{
	struct device_node *np;
	const char *cmd_line;
	char *s = NULL;
	int ret = 0;
	int i = 0;

	if (!current_mode)
		return -1;

	np = of_find_node_by_path("/chosen");
	if (!np) {
		TRAN_INFO("Can't get the /chosen");
		return -EIO;
	}

	if (!strcmp(boot_param, "lcm_name=")) {
		ret = of_property_read_string(np, "touch,lcm_name", &cmd_line);
		if (ret < 0) {
			TRAN_INFO("Can't get the touch,ret=%d", ret);
		} else {
			strcpy(current_mode, cmd_line);
			TRAN_INFO("get lcm_name: %s  len:%lu", cmd_line, strlen(cmd_line));
			return 0;
		}
	}

	ret = of_property_read_string(np, "bootargs", &cmd_line);
	if (ret < 0) {
		TRAN_INFO("Can't get the bootargs");
		return ret;
	}

	s = strstr(cmd_line, boot_param);
	if (s) {
		s += strlen(boot_param);
		while (*s != ' ' && i < 64) {
			*current_mode++ = *s++;
			i++;
		}
		*current_mode = '\0';
	} else {
		return -1;
	}

	return 0;
}

static int mtk_find_lcm_name(char *p_name)
{
	if (p_name)
		get_bootargs(p_name, "lcm_name=");
	return 1;
}

static int mtk_plat_init(struct tran_ts_core *core_data)
{
	TRAN_FUNC_ENTER();
#ifdef CONFIG_DRM_MEDIATEK_V2
	mtk_register_dsp_notify(core_data);
#endif
	TRAN_FUNC_EXIT();
	return 0;
}

static int mtk_plat_deinit(struct tran_ts_core *core_data)
{
	TRAN_FUNC_ENTER();
#ifdef CONFIG_DRM_MEDIATEK_V2
	mtk_unregister_dsp_notify(core_data);
#endif
	TRAN_FUNC_EXIT();
	return 0;
}

static struct tran_platform_ops mtk_plat_ops = {
	.plat_init = mtk_plat_init,
	.plat_deinit = mtk_plat_deinit,
	.get_lcm_name = mtk_find_lcm_name,
};

static struct tran_ts_platform mtk_plat = {
	.ops = &mtk_plat_ops,
};

void init_tran_platform_info(void)
{
	g_tran_ts.platform_data = &mtk_plat;
}
