#include "focaltech_core.h"
#include "focaltech_transsion.h"

extern int register_ts_controller(struct tran_ts_controller *controller);
extern int unregister_ts_controller(struct tran_ts_controller *controller);
extern int ft3681_init(void);
extern void ft3681_exit(void);

static int fts_ts_spi_init(void)
{
	int ret = 0;

	TRAN_FUNC_ENTER();
	ft3681_init();
	FTS_FUNC_EXIT();
	return ret;
}

static void suspend(struct device *h)
{
	fts_ts_suspend(NULL);
}

static void resume(struct device *h)
{
	fts_ts_resume(NULL);
}

static char *support_list[] = {
	"ft3683g",
	"ft3682g",
	NULL
};

static struct tran_controller_ops controller_ops = {
	.tran_ts_init = fts_ts_spi_init,
	.tran_ts_suspend = suspend,
	.tran_ts_resume = resume,
};

struct tran_ts_controller ft3683g_controller = {
	.cur_ic = NULL,
	.tran_ts_data = NULL,
	.vendor_name = "focaltech",
	.support_ic = support_list,
	.ops = &controller_ops,
};

static int __init fts_device_init(void)
{
	register_ts_controller(&ft3683g_controller);
	return 0;
}

static void __exit fts_device_exit(void)
{
	ft3681_exit();
	unregister_ts_controller(&ft3683g_controller);
}

module_init(fts_device_init);
module_exit(fts_device_exit);

MODULE_AUTHOR("FocalTech Driver Team");
MODULE_DESCRIPTION("FocalTech Touchscreen Driver");
MODULE_LICENSE("GPL v2");
