#ifndef __TRANSSON_CORE_H__
#define __TRANSSON_CORE_H__

#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/irq.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/pm_wakeup.h>

#define TP_DRV_MAX_COUNT	10
#define MAX_LCM_NAME_LEN	64

#define TPD_DEVICE		"tran-tpd"

#define ENUM_TO_STRING(enumVar) #enumVar

#define TRAN_DEBUG(fmt, args...) do { \
	printk("[tran_touch]%s %d:"fmt"\n", __func__, __LINE__, ##args); \
} while (0)

#define TRAN_INFO(fmt, args...) do { \
	printk("[tran_touch-INFO]%s %d:"fmt"\n", __func__, __LINE__, ##args); \
} while (0)

#define TRAN_ERROR(fmt, args...) do { \
	printk(KERN_ERR "[tran_touch-ERROR]%s %d:"fmt"\n", __func__, __LINE__, ##args); \
} while (0)

#define TRAN_FUNC_ENTER() do { \
	printk("[tran_touch]++++++++++%s: In(%d)++++++++++\n", __func__, __LINE__); \
} while (0)

#define TRAN_FUNC_EXIT() do { \
	printk("[tran_touch]----------%s: Exit(%d)----------\n", __func__, __LINE__); \
} while (0)

struct tran_ts_core;
struct tran_ts_controller;
struct tran_core_ops;

extern struct tran_ts_core g_tran_ts;
extern unsigned char g_tran_tp_provide;

void init_tran_platform_info(void);
int register_ts_controller(struct tran_ts_controller *controller);
int unregister_ts_controller(struct tran_ts_controller *controller);
int get_tpd_ops(struct tran_ts_controller *controller, struct tran_core_ops **ops);

enum TS_CORE_MODE {
	TRAN_ACTIVE_MODE = 10,
	TRAN_SUSPEND_MODE = 11,
	TRAN_MAX_MODE
};

enum TRAN_TRIGER_EVENT {
	RESUME_TRIGER = 0,
	SUSPEND_TRIGER = 1,
};

struct tran_controller_ops {
	int (*tran_ts_init)(void);
	void (*tran_ts_suspend)(struct device *h);
	void (*tran_ts_resume)(struct device *h);
};

struct tran_ts_controller {
	char *cur_ic;
	char *vendor_name;
	char **support_ic;
	struct input_dev *input_dev;
	struct tran_controller_ops *ops;
	struct tran_ts_core *tran_ts_data;
};

struct tran_platform_ops {
	int (*plat_init)(struct tran_ts_core *pdata);
	int (*plat_deinit)(struct tran_ts_core *pdata);
	int (*get_lcm_name)(char *p_name);
};

struct tran_ts_platform {
	struct tran_platform_ops *ops;
	struct tran_ts_core *tran_ts_data;
};

struct supplier_list {
	int supplier_num;
	u32 resolution_multiples;
	char *supplier_info;
	char *supplier_fw;
	char *supplier_testfw;
	char *supplier_vendor;
	struct list_head next;
};

struct controller_list {
	struct tran_ts_controller *controller;
	struct list_head next;
};

struct tran_core_ops {
	void (*controller_init_completion)(void);
	void (*controller_uninit_completion)(void);
};

struct tran_ts_core {
	bool controller_init_state;
	u32 ts_core_mode;

	struct platform_device *pdev;

	struct mutex dsp_event_mutex;
	struct mutex controller_mutex;
	struct completion comp;
	struct workqueue_struct *tpd_init_workqueue;
	struct workqueue_struct *tpd_workqueue;
	struct work_struct tpd_dsp_work;
	struct work_struct tpd_match_work;
	struct notifier_block dsp_event_block;

	struct list_head supplier_head;
	struct list_head controller_head;

	struct tran_core_ops *tpd_ops;
	struct supplier_list *supplier;
	struct tran_ts_controller *controller;
	struct tran_ts_platform *platform_data;
};

#endif
