#include "omnivision_tcm_core.h"
#include "omnivision_transsion.h"

extern struct ovt_tcm_hcd *g_tcm_hcd;
extern int ovt_tcm_suspend(struct device *dev);
extern int ovt_tcm_resume(struct device *dev);
extern int ovt_tcm_module_init(void);
extern void ovt_tcm_module_exit(void);

static int tran_ts_init(void)
{
	int ret = 0;

	TRAN_FUNC_ENTER();
	ovt_tcm_module_init();
	TRAN_FUNC_EXIT();
	return ret;
}

static void suspend(struct device *h)
{
	struct ovt_tcm_hcd *tcm_hcd = g_tcm_hcd;

	if (tcm_hcd)
		ovt_tcm_suspend(&tcm_hcd->pdev->dev);
}

static void resume(struct device *h)
{
	struct ovt_tcm_hcd *tcm_hcd = g_tcm_hcd;

	if (tcm_hcd)
		ovt_tcm_resume(&tcm_hcd->pdev->dev);
}

static char *support_list[] = {
	"td4160",
	"td4376",
	NULL
};

static struct tran_controller_ops controller_ops = {
	.tran_ts_init = tran_ts_init,
	.tran_ts_suspend = suspend,
	.tran_ts_resume = resume,
};

struct tran_ts_controller ovt_controller = {
	.cur_ic = NULL,
	.tran_ts_data = NULL,
	.vendor_name = "omnivision",
	.support_ic = support_list,
	.ops = &controller_ops,
};

static int __init controller_module_init(void)
{
	register_ts_controller(&ovt_controller);
	return 0;
}

static void __exit controller_module_exit(void)
{
	unregister_ts_controller(&ovt_controller);
	ovt_tcm_module_exit();
}

module_init(controller_module_init);
module_exit(controller_module_exit);

MODULE_AUTHOR("Transsion TP Driver Team");
MODULE_DESCRIPTION("Transsion Touchscreen Driver");
MODULE_LICENSE("GPL v2");
