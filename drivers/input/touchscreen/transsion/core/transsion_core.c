#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "transsion_core.h"

extern struct tran_ts_core g_tran_ts;
extern struct tran_core_ops g_tpd_ops;

int get_tpd_ops(struct tran_ts_controller *controller, struct tran_core_ops **ops)
{
	if (!controller || !ops)
		return 0;

	if (controller->tran_ts_data && controller->tran_ts_data->tpd_ops) {
		*ops = controller->tran_ts_data->tpd_ops;
		return 1;
	}

	TRAN_INFO("get ops fail !");
	return 0;
}
EXPORT_SYMBOL_GPL(get_tpd_ops);

static void dsp_event_callback(struct work_struct *work)
{
	struct tran_ts_controller *pController = g_tran_ts.controller;

	if (g_tran_ts.controller_init_state && pController && pController->ops &&
	    pController->ops->tran_ts_resume) {
		pController->ops->tran_ts_resume(&g_tran_ts.pdev->dev);
	} else {
		TRAN_INFO("vendor_probe_result is false!");
		mutex_unlock(&g_tran_ts.dsp_event_mutex);
		return;
	}

	g_tran_ts.ts_core_mode = TRAN_ACTIVE_MODE;
	TRAN_INFO("core mode change to TRAN_ACTIVE_MODE");
	mutex_unlock(&g_tran_ts.dsp_event_mutex);
	TRAN_INFO("drm notify tp resume end");
}

static int check_support_ic(struct tran_ts_core *ts_core,
			    struct tran_ts_controller *controller, char *supInfo)
{
	int i;
	char **support_ic = NULL;

	if (controller) {
		support_ic = controller->support_ic;
		for (i = 0; support_ic[i] != NULL; i++) {
			TRAN_INFO("Check whether the supplier supports %s", support_ic[i]);
			if (strstr(supInfo, support_ic[i])) {
				controller->cur_ic = support_ic[i];
				controller->tran_ts_data = ts_core;
				ts_core->controller = controller;
				return 1;
			}
		}
	}

	ts_core->controller = NULL;
	return 0;
}

static int match_tp_controller(struct tran_ts_core *ts_core)
{
	char lcm_name[64] = {0};
	struct supplier_list *supList = NULL;
	struct controller_list *controllerList = NULL;
	struct tran_ts_platform *platData = ts_core->platform_data;

	if (platData && platData->ops && platData->ops->get_lcm_name) {
		platData->ops->get_lcm_name(lcm_name);
		TRAN_INFO("get_lcm_name %s", lcm_name);
	}

	list_for_each_entry(supList, &ts_core->supplier_head, next) {
		TRAN_INFO("check supplier-%d %s", supList->supplier_num, supList->supplier_info);
		if (strstr(supList->supplier_info, lcm_name)) {
			TRAN_INFO("apply supplier-%d %s", supList->supplier_num, supList->supplier_info);
			ts_core->supplier = supList;
			break;
		} else if (strstr(supList->supplier_info, "any")) {
			TRAN_INFO("apply supplier-%d %s", supList->supplier_num, supList->supplier_info);
			ts_core->supplier = supList;
			break;
		} else {
			ts_core->supplier = NULL;
		}
	}

	if (!ts_core->supplier) {
		TRAN_ERROR("no supplier match!!");
		return 0;
	}

	mutex_lock(&ts_core->controller_mutex);
	list_for_each_entry(controllerList, &ts_core->controller_head, next) {
		if (!controllerList->controller->vendor_name) {
			mutex_unlock(&ts_core->controller_mutex);
			return 0;
		}
		TRAN_INFO("try to find %s controller", controllerList->controller->vendor_name);
		if (strstr(supList->supplier_info, controllerList->controller->vendor_name))
			check_support_ic(ts_core, controllerList->controller, supList->supplier_info);
	}
	mutex_unlock(&ts_core->controller_mutex);

	if (ts_core->controller) {
		TRAN_INFO("start load controller vendor:%s ic:%s",
			  ts_core->controller->vendor_name, ts_core->controller->cur_ic);
		ts_core->controller->ops->tran_ts_init();
		return 1;
	}

	TRAN_ERROR("no controller match exit 0");
	return 0;
}

int register_ts_controller(struct tran_ts_controller *controller)
{
	struct controller_list *controllerList = NULL;

	if (!controller) {
		TRAN_INFO("Don't register reason controller is null");
		return -1;
	}

	controllerList = vmalloc(sizeof(*controllerList));
	if (!controllerList) {
		TRAN_ERROR("request controllerList mem failed");
		return -1;
	}

	memset(controllerList, 0, sizeof(*controllerList));
	controllerList->controller = controller;

	mutex_lock(&g_tran_ts.controller_mutex);
	list_add_tail(&controllerList->next, &g_tran_ts.controller_head);
	mutex_unlock(&g_tran_ts.controller_mutex);

	if (!queue_work(g_tran_ts.tpd_init_workqueue, &g_tran_ts.tpd_match_work))
		TRAN_ERROR("touch device queue_work failed");
	return 0;
}
EXPORT_SYMBOL_GPL(register_ts_controller);

int unregister_ts_controller(struct tran_ts_controller *controller)
{
	return 0;
}
EXPORT_SYMBOL_GPL(unregister_ts_controller);

static int tran_parse_dt(struct device *dev, struct tran_ts_core *core_data)
{
	int i, len, ret;
	int supplier_num = 0;
	u32 resolution_multiples = 1;
	const char *supplier_info = NULL;
	const char *supplier_fw = NULL;
	const char *supplier_testfw = NULL;
	const char *supplier_vendor = NULL;
	struct supplier_list *tmpList = NULL;
	char supplierx[32] = {0};
	struct device_node *np = dev->of_node;

	TRAN_FUNC_ENTER();

	ret = of_property_read_u32(np, "supplier-num", &supplier_num);
	if (ret < 0) {
		TRAN_ERROR("supplier-num: can not find!");
		return ret;
	}
	TRAN_INFO("supplier-num: %d", supplier_num);

	for (i = 0; i < supplier_num; i++) {
		tmpList = NULL;
		supplier_info = NULL;
		supplier_vendor = NULL;
		memset(supplierx, 0, sizeof(supplierx));

		snprintf(supplierx, sizeof(supplierx), "supplier-%d", i);
		supplier_info = of_get_property(np, supplierx, &len);
		if (!supplier_info) {
			TRAN_ERROR("supplier-%d: can not find!", i);
			return -EINVAL;
		}
		TRAN_INFO("supplier-%d: %s", i, supplier_info);

		snprintf(supplierx, sizeof(supplierx), "supplier-vendor-fwname-%d", i);
		supplier_fw = of_get_property(np, supplierx, &len);
		if (!supplier_fw)
			TRAN_ERROR("supplier-vendor-fwname-%d: can not find!", i);
		else
			TRAN_INFO("supplier-vendor-fwname-%d: %s", i, supplier_fw);

		snprintf(supplierx, sizeof(supplierx), "supplier-vendor-testname-%d", i);
		supplier_testfw = of_get_property(np, supplierx, &len);
		if (!supplier_testfw)
			TRAN_ERROR("supplier-vendor-testname-%d: can not find!", i);
		else
			TRAN_INFO("supplier-vendor-testname-%d: %s", i, supplier_testfw);

		snprintf(supplierx, sizeof(supplierx), "supplier-vendor-%d", i);
		supplier_vendor = of_get_property(np, supplierx, &len);
		if (!supplier_vendor)
			TRAN_INFO("supplier-vendor-%d: can not find!", i);
		else
			TRAN_INFO("supplier-vendor-%d: %s", i, supplier_vendor);

		snprintf(supplierx, sizeof(supplierx), "supplier-resolution-%d", i);
		ret = of_property_read_u32(np, supplierx, &resolution_multiples);
		if (ret < 0) {
			TRAN_ERROR("supplier-resolution-%d: can not find!", i);
			resolution_multiples = 1;
		}
		TRAN_INFO("supplier-resolution-%d: %d", i, resolution_multiples);

		tmpList = vmalloc(sizeof(*tmpList));
		if (!tmpList) {
			TRAN_ERROR("request supplier_list mem failed!");
			return -ENOMEM;
		}

		tmpList->supplier_num = i;
		tmpList->supplier_info = (char *)supplier_info;
		tmpList->supplier_fw = (char *)supplier_fw;
		tmpList->supplier_testfw = (char *)supplier_testfw;
		tmpList->supplier_vendor = (char *)supplier_vendor;
		tmpList->resolution_multiples = resolution_multiples;
		list_add_tail(&tmpList->next, &core_data->supplier_head);
	}

	TRAN_FUNC_EXIT();
	return 0;
}

static void controller_init_finish(void)
{
	g_tran_ts.controller_init_state = true;
	complete(&g_tran_ts.comp);
	TRAN_INFO("controller load finish");
}

static void controller_uninit_finish(void)
{
	g_tran_ts.controller_init_state = false;
	TRAN_INFO("controller remove finish");
}

static void controller_match_work(struct work_struct *work)
{
	int ret;

	if (g_tran_ts.controller) {
		TRAN_INFO("Not allow to match controller");
		return;
	}

	if (match_tp_controller(&g_tran_ts)) {
		ret = wait_for_completion_timeout(&g_tran_ts.comp, msecs_to_jiffies(8000));
		if (ret == 0) {
			TRAN_ERROR("init controller timeout!");
			g_tran_ts.controller = NULL;
			return;
		}
		TRAN_INFO("load controller success!");
		if (g_tran_ts.platform_data && g_tran_ts.platform_data->ops &&
		    g_tran_ts.platform_data->ops->plat_init)
			g_tran_ts.platform_data->ops->plat_init(&g_tran_ts);
	}
}

static int tpd_probe(struct platform_device *pdev)
{
	struct tran_ts_platform *tran_pdata = g_tran_ts.platform_data;

	TRAN_FUNC_ENTER();

	if (!tran_pdata) {
		TRAN_ERROR("tran platform is NULL!");
		return 0;
	}

	INIT_WORK(&g_tran_ts.tpd_match_work, controller_match_work);
	INIT_WORK(&g_tran_ts.tpd_dsp_work, dsp_event_callback);
	init_completion(&g_tran_ts.comp);

	tran_parse_dt(&pdev->dev, &g_tran_ts);

	g_tran_ts.pdev = pdev;
	g_tran_ts.ts_core_mode = TRAN_ACTIVE_MODE;
	g_tran_ts.tpd_workqueue = create_singlethread_workqueue("tpd_work_queue");

	TRAN_FUNC_EXIT();
	return 0;
}

static int tpd_remove(struct platform_device *pdev)
{
	struct tran_ts_platform *pdata = g_tran_ts.platform_data;

	if (pdata && pdata->ops && pdata->ops->plat_deinit)
		pdata->ops->plat_deinit(&g_tran_ts);
	return 0;
}

static const struct dev_pm_ops tpd_pm_ops = {
	.suspend = NULL,
	.resume = NULL,
};

static const struct of_device_id touch_of_match[] = {
	{ .compatible = "mediatek,touch", },
	{ .compatible = "transsion,touch", },
	{},
};

static void tran_shutdown(struct platform_device *pdev)
{
	struct tran_ts_platform *pdata = g_tran_ts.platform_data;

	TRAN_FUNC_ENTER();
	if (pdata && pdata->ops && pdata->ops->plat_deinit)
		pdata->ops->plat_deinit(&g_tran_ts);
}

static struct platform_driver tpd_driver = {
	.remove = tpd_remove,
	.shutdown = tran_shutdown,
	.probe = tpd_probe,
	.driver = {
		.name = TPD_DEVICE,
		.pm = &tpd_pm_ops,
		.owner = THIS_MODULE,
		.of_match_table = touch_of_match,
	},
};

struct tran_core_ops g_tpd_ops = {
	.controller_init_completion = controller_init_finish,
	.controller_uninit_completion = controller_uninit_finish,
};

struct tran_ts_core g_tran_ts = {
	.supplier_head = LIST_HEAD_INIT(g_tran_ts.supplier_head),
	.controller_head = LIST_HEAD_INIT(g_tran_ts.controller_head),
	.controller_mutex = __MUTEX_INITIALIZER(g_tran_ts.controller_mutex),
	.dsp_event_mutex = __MUTEX_INITIALIZER(g_tran_ts.dsp_event_mutex),
	.tpd_ops = &g_tpd_ops,
};
EXPORT_SYMBOL_GPL(g_tran_ts);

unsigned char g_tran_tp_provide;
EXPORT_SYMBOL(g_tran_tp_provide);

static int __init tpd_device_init(void)
{
	TRAN_FUNC_ENTER();
	init_tran_platform_info();

	g_tran_ts.tpd_init_workqueue = create_singlethread_workqueue("tpd_init_queue");

	if (platform_driver_register(&tpd_driver) != 0)
		TRAN_ERROR("unable to register touch panel driver.");

	TRAN_FUNC_EXIT();
	return 0;
}

static void __exit tpd_device_exit(void)
{
	TRAN_FUNC_ENTER();
	platform_driver_unregister(&tpd_driver);
	TRAN_FUNC_EXIT();
}

module_init(tpd_device_init);
module_exit(tpd_device_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Transsion touch panel driver");
MODULE_AUTHOR("Transsion");
