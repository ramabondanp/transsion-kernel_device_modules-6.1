/*
 * (C) Copyright 2024, Imvision Co., Ltd
 * This file is classified as confidential level C4 within Imvision
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Change Logs:
 * Date           Author         Notes
 * 2022-2-24      yanghua     Initialize.
 */

/**
 * @brief   ISP power control driver
 * @date    2022-06-29
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/uaccess.h>
#include <linux/regulator/consumer.h>
#include "ai_isp_pmctrl.h"
#include "ai_isp_pmctrl_def.h"

struct isp_pm_state {
	bool is_awake;
	bool is_reset;
	bool is_power_en;
};

enum ai_isp_gpios {
	AP_WAKE_UP,
	ISP_RST_REQ,
	ISP_HD_RST_N,
	ISP_PWR_EN,
	ISP_PWR_EN_2,
	ISP_TCXO_EN,
	ISP_GPIO_NUM,
};

#define GPIO_OUTPUT		0x1
#define GPIO_INPUT		0x0
#define GPIO_OUT_HIGH		0x1
#define GPIO_OUT_LOW		0x0

struct isp_gpio_desc {
	const char *name;
	uint8_t dir;
	uint8_t init_val;
};

static struct isp_gpio_desc gpios[ISP_GPIO_NUM] = {
	[AP_WAKE_UP]	= {
		.name		= "gpio-wake-up",
		.dir		= GPIO_OUTPUT,
		.init_val	= GPIO_OUT_HIGH,
	},
	[ISP_RST_REQ]	= {
		.name		= "gpio-rst-req",
		.dir		= GPIO_INPUT,
	},
	[ISP_HD_RST_N]	= {
		.name		= "gpio-hd-rst",
		.dir		= GPIO_OUTPUT,
		.init_val	= GPIO_OUT_HIGH,
	},
	[ISP_PWR_EN]	= {
		.name		= "gpio-pwr-en",
		.dir		= GPIO_OUTPUT,
		.init_val	= GPIO_OUT_LOW,
	},
	[ISP_PWR_EN_2]	= {
		.name		= "gpio-pwr-en-2",
		.dir		= GPIO_OUTPUT,
		.init_val	= GPIO_OUT_LOW,
	},
	[ISP_TCXO_EN]	= {
		.name		= "gpio-tcxo-en",
		.dir		= GPIO_INPUT,
	},
};

struct isp_v1_pwr_gpio_desc {
	const char *name;
};

enum ai_isp_v1_pwr_seq_gpios {
	V1_VDD1_0V75,
	V1_VDD2_0V75,
	V1_VDD3_1V8,
	V1_VDD4_1V1,
	V1_VDD5_0V6,
	V1_VDD1_0V75_LSW,
	V1_VDD8_1V8,
	V1_PWR_GPIO_NUM,
};

struct pwr_seq_desc {
	uint8_t id;
	uint8_t delay_ms;
};

static struct pwr_seq_desc v1_pwr_on_seq[V1_PWR_GPIO_NUM] = {
	{.id = V1_VDD1_0V75,		.delay_ms = 1},
	{.id = V1_VDD2_0V75,		.delay_ms = 2},
	{.id = V1_VDD3_1V8,		.delay_ms = 1},
	{.id = V1_VDD4_1V1,		.delay_ms = 1},
	{.id = V1_VDD5_0V6,		.delay_ms = 0},
	{.id = V1_VDD1_0V75_LSW,	.delay_ms = 1},
	{.id = V1_VDD8_1V8,		.delay_ms = 1},
};

static struct pwr_seq_desc v1_pwr_off_seq[V1_PWR_GPIO_NUM] = {
	{.id = V1_VDD1_0V75_LSW,	.delay_ms = 0},
	{.id = V1_VDD8_1V8,		.delay_ms = 6},
	{.id = V1_VDD2_0V75,		.delay_ms = 5},
	{.id = V1_VDD1_0V75,		.delay_ms = 1},
	{.id = V1_VDD5_0V6,		.delay_ms = 1},
	{.id = V1_VDD4_1V1,		.delay_ms = 2},
	{.id = V1_VDD3_1V8,		.delay_ms = 1},
};

#define V1_LP_GPIO_NUM 3
static struct pwr_seq_desc v1_wakeup_seq[V1_LP_GPIO_NUM] = {
	{.id = V1_VDD2_0V75,		.delay_ms = 1},
	{.id = V1_VDD1_0V75_LSW,	.delay_ms = 1},
	{.id = V1_VDD8_1V8,		.delay_ms = 1},
};

static struct pwr_seq_desc v1_suspend_seq[V1_LP_GPIO_NUM] = {
	{.id = V1_VDD8_1V8,		.delay_ms = 1},
	{.id = V1_VDD1_0V75_LSW,	.delay_ms = 1},
	{.id = V1_VDD2_0V75,		.delay_ms = 1},
};

static struct isp_v1_pwr_gpio_desc v1_pwr_seq_gpios[V1_PWR_GPIO_NUM] = {
	[V1_VDD1_0V75]		= {.name = "v1-vdd1-0v75"},
	[V1_VDD2_0V75]		= {.name = "v1-vdd2-0v75"},
	[V1_VDD3_1V8]		= {.name = "v1-vdd3-1v8"},
	[V1_VDD4_1V1]		= {.name = "v1-vdd4-1v1"},
	[V1_VDD5_0V6]		= {.name = "v1-vdd5-0v6"},
	[V1_VDD1_0V75_LSW]	= {.name = "v1-vdd1-0v75-lsw"},
	[V1_VDD8_1V8]		= {.name = "v1-vdd8-1v8"},
};

struct isp_pm_dev {
	int isp_gpios[ISP_GPIO_NUM];
	int isp_v1_pwr_seq_gpios[V1_PWR_GPIO_NUM];
	bool seperate_pwr_ok;
	struct regulator *vbuck;
	struct isp_pm_state pm_state;
	struct mutex state_lock;
	struct device *dev;
	struct blocking_notifier_head pm_notifier;

	/* pmctrl_misc parameter */
	wait_queue_head_t rst_req_wait;
	int rst_req_flag;
	int rtos_wakeup_flag;
};

static struct isp_pm_dev *ai_isp_pm_dev;
static int v1_isp_pwr_seq(struct isp_pm_dev *pm_dev, bool is_pwr_on)
{
	struct pwr_seq_desc *seq_tab;
	uint8_t ctrl_val;
	int i, ret;
	int err_cnt = 0;
	uint8_t id;
	struct device *dev = pm_dev->dev;

	seq_tab = is_pwr_on ? v1_pwr_on_seq : v1_pwr_off_seq;
	ctrl_val = is_pwr_on ? GPIO_OUT_HIGH : GPIO_OUT_LOW;

	for (i = 0; i < V1_PWR_GPIO_NUM; i++) {
		id = seq_tab[i].id;
		if (pm_dev->isp_v1_pwr_seq_gpios[id] >= 0) {
			ret = gpio_direction_output(pm_dev->isp_v1_pwr_seq_gpios[id], ctrl_val);
			if (ret) {
				dev_err(dev, "pwr seq io %d failed\n", i);
				err_cnt++;
			}
		}
		mdelay(seq_tab[i].delay_ms);
	}

	return (err_cnt ? -1 : 0);
}

static int v1_isp_lp_seq(struct isp_pm_dev *pm_dev, bool is_suspend)
{
	struct pwr_seq_desc *seq_tab;
	uint8_t ctrl_val;
	int i, ret;
	int err_cnt = 0;
	uint8_t id;
	struct device *dev = pm_dev->dev;

	seq_tab = is_suspend ? v1_suspend_seq : v1_wakeup_seq;
	ctrl_val = is_suspend ? GPIO_OUT_LOW : GPIO_OUT_HIGH;

	for (i = 0; i < V1_LP_GPIO_NUM; i++) {
		id = seq_tab[i].id;
		if (pm_dev->isp_v1_pwr_seq_gpios[id] >= 0) {
			ret = gpio_direction_output(pm_dev->isp_v1_pwr_seq_gpios[id], ctrl_val);
			if (ret) {
				dev_err(dev, "pwr seq io %d failed\n", i);
				err_cnt++;
			}
		}
		mdelay(seq_tab[i].delay_ms);
	}

	return (err_cnt ? -1 : 0);
}

enum pm_ctrl_code_def {
	SUSPEND,
	WAKEUP,
	RESET,
	RELEASE_RESET,
	POWER_EN,
	POWER_DIS,
	PM_CODE_NUM,
};

static int aiisp_pm_notifier_call_chain(struct isp_pm_dev *pm_dev,
					unsigned long event, void *data)
{
	return blocking_notifier_call_chain(&pm_dev->pm_notifier, event, data);
}

int aiisp_pm_register_notifier(struct notifier_block *nb)
{
	if (!ai_isp_pm_dev)
		return -ENODEV;

	return blocking_notifier_chain_register(&ai_isp_pm_dev->pm_notifier, nb);
}
EXPORT_SYMBOL_GPL(aiisp_pm_register_notifier);

int aiisp_pm_unregister_notifier(struct notifier_block *nb)
{
	if (!ai_isp_pm_dev)
		return -ENODEV;

	return blocking_notifier_chain_unregister(&ai_isp_pm_dev->pm_notifier, nb);
}
EXPORT_SYMBOL_GPL(aiisp_pm_unregister_notifier);

static const char *pm_ctrl_code[PM_CODE_NUM + 1] = {
	[SUSPEND] = "sleep",
	[WAKEUP] = "wakeup",
	[RESET] = "reset",
	[RELEASE_RESET] = "de-reset",
	[POWER_EN] = "power-en",
	[POWER_DIS] = "power-dis",
	[PM_CODE_NUM] = "invalid",
};

static ssize_t pm_ctrl_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct isp_pm_dev *pm_dev = dev_get_drvdata(dev);
	ssize_t len = 0;
	int i;

	for (i = 0; i < ISP_GPIO_NUM; i++) {
		if (pm_dev->isp_gpios[i] >= 0)
			len += sprintf(buf + len, "%-15s dir:%-6s state:%d \n",
					gpios[i].name,
					(gpios[i].dir == GPIO_INPUT ? "INPUT" : "OUTPUT"),
					gpio_get_value(pm_dev->isp_gpios[i]));
		else
			len += sprintf(buf + len, "%-15s not supported\n", gpios[i].name);
	}

	for (i = 0; i < V1_PWR_GPIO_NUM; i++) {
		if (pm_dev->isp_v1_pwr_seq_gpios[i] >= 0)
			len += sprintf(buf + len, "%-15s dir:%-6s state:%d \n",
					v1_pwr_seq_gpios[i].name, "OUTPUT",
					gpio_get_value(pm_dev->isp_v1_pwr_seq_gpios[i]));
		else
			len += sprintf(buf + len, "%-15s not supported\n",
				       v1_pwr_seq_gpios[i].name);
	}

	return len;
}

static ssize_t pm_ctrl_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t n)
{
	struct isp_pm_dev *pm_dev = NULL;
	size_t len;
	char *p;
	int state;
	int ret = 0;
	struct isp_pm_state pm_state;

	p = memchr(buf, '\n', n);
	len = p ? p - buf : n;

	pm_dev = dev_get_drvdata(dev);

	mutex_lock(&pm_dev->state_lock);

	for (state = 0; state < PM_CODE_NUM; state++) {
		if (len == strlen(pm_ctrl_code[state]) &&
		    !strncmp(buf, pm_ctrl_code[state], len)) {
			break;
		}
	}

	dev_info(dev, "isp power control %s start\n", pm_ctrl_code[state]);

	memcpy(&pm_state, &pm_dev->pm_state, sizeof(struct isp_pm_state));

	switch (state) {
	case SUSPEND:
		aiisp_pm_notifier_call_chain(pm_dev, AIISP_PM_EVENT_SUSPEND_PREPARE, NULL);
		if (pm_dev->isp_gpios[AP_WAKE_UP] >= 0)
			ret = gpio_direction_output(pm_dev->isp_gpios[AP_WAKE_UP], GPIO_OUT_LOW);
		if (!ret) {
			/* Clear rtos_wakeup_flag to 0 after ap_wakeup pin set to low level */
			ai_isp_pm_dev->rtos_wakeup_flag = RTOS_WAKEUP_STAT_NONE;
			pm_state.is_awake = false;
		}
		break;
	case WAKEUP:
		if (pm_dev->isp_gpios[AP_WAKE_UP] >= 0)
			ret = gpio_direction_output(pm_dev->isp_gpios[AP_WAKE_UP], GPIO_OUT_HIGH);
		if (!ret)
			pm_state.is_awake = true;
		break;
	case RESET:
		aiisp_pm_notifier_call_chain(pm_dev, AIISP_PM_EVENT_RESET_PREPARE, NULL);
		if (pm_dev->isp_gpios[ISP_HD_RST_N] >= 0)
			ret = gpio_direction_output(pm_dev->isp_gpios[ISP_HD_RST_N], GPIO_OUT_LOW);
		if (!ret) {
			/* Clear rtos_wakeup_flag to 0 after reset */
			ai_isp_pm_dev->rtos_wakeup_flag = RTOS_WAKEUP_STAT_NONE;
			pm_state.is_reset = true;
		}
		break;
	case RELEASE_RESET:
		if (pm_dev->isp_gpios[ISP_HD_RST_N] >= 0) {
			ret = gpio_direction_output(pm_dev->isp_gpios[ISP_HD_RST_N], GPIO_OUT_HIGH);
			mdelay(10);
		}
		if (!ret)
			pm_state.is_reset = false;
		aiisp_pm_notifier_call_chain(pm_dev, AIISP_PM_EVENT_RESET_DONE, NULL);
		break;
	case POWER_EN:
		if (pm_dev->isp_gpios[ISP_PWR_EN] >= 0) {
			ret = gpio_direction_output(pm_dev->isp_gpios[ISP_PWR_EN], GPIO_OUT_HIGH);
			if (!ret && pm_dev->vbuck) {
				mdelay(1);
				ret = regulator_set_voltage(pm_dev->vbuck, 750000, 750000);
				if (ret) {
					dev_err(dev, "vbuck set voltage failed\n");
					break;
				}
				if (!regulator_is_enabled(pm_dev->vbuck)) {
					ret = regulator_enable(pm_dev->vbuck);
					if (ret) {
						dev_err(dev, "vbuck enable failed\n");
						break;
					}
				}
			}
			if (!ret && (pm_dev->isp_gpios[ISP_PWR_EN_2] >= 0)) {
				mdelay(1);
				ret = gpio_direction_output(pm_dev->isp_gpios[ISP_PWR_EN_2],
							    GPIO_OUT_HIGH);
			}
		} else if (pm_dev->seperate_pwr_ok) {
			ret = v1_isp_pwr_seq(pm_dev, true);
		}
		if (!ret)
			pm_state.is_power_en = true;
		break;
	case POWER_DIS:
		if (pm_dev->isp_gpios[ISP_PWR_EN] >= 0) {
			if (pm_dev->isp_gpios[ISP_PWR_EN_2] >= 0)
				gpio_direction_output(pm_dev->isp_gpios[ISP_PWR_EN_2],
						      GPIO_OUT_LOW);
			if (pm_dev->vbuck)
				regulator_disable(pm_dev->vbuck);
			ret = gpio_direction_output(pm_dev->isp_gpios[ISP_PWR_EN], GPIO_OUT_LOW);
		} else if (pm_dev->seperate_pwr_ok) {
			ret = v1_isp_pwr_seq(pm_dev, false);
		}
		if (!ret)
			pm_state.is_power_en = false;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret) {
		dev_info(dev, "isp power control %s failed\n", pm_ctrl_code[state]);
		mutex_unlock(&pm_dev->state_lock);
		return ret;
	}

	memcpy(&pm_dev->pm_state, &pm_state, sizeof(struct isp_pm_state));
	mutex_unlock(&pm_dev->state_lock);
	dev_info(dev, "isp power control %s done\n", pm_ctrl_code[state]);

	return n;
}

static DEVICE_ATTR_RW(pm_ctrl);

static int isp_gpio_init(int gpio, struct isp_gpio_desc *desc)
{
	if (desc->dir == GPIO_OUTPUT)
		return gpio_direction_output(gpio, desc->init_val);
	else
		return gpio_direction_input(gpio);
}

static void isp_gpio_free(struct isp_pm_dev *pm_dev)
{
	int gpio;

	for (gpio = 0; gpio < ISP_GPIO_NUM; gpio++) {
		if (pm_dev->isp_gpios[gpio] >= 0)
			gpio_free(pm_dev->isp_gpios[gpio]);
	}

	for (gpio = 0; gpio < V1_PWR_GPIO_NUM; gpio++) {
		if (pm_dev->isp_v1_pwr_seq_gpios[gpio] >= 0)
			gpio_free(pm_dev->isp_v1_pwr_seq_gpios[gpio]);
	}
}

/**
 * When ai_isp software wakeup from deep sleep,
 * software will report ready status via IPC, IPC software
 * will call this API to report SW wakeup status to userspace.
 */
void ai_isp_sw_wakeup_report(void)
{
	if (!ai_isp_pm_dev)
		return;

	dev_info(ai_isp_pm_dev->dev, "AI-ISP wakeup_ready report!\n");
	ai_isp_pm_dev->rtos_wakeup_flag = RTOS_WAKEUP_STAT_ACTIVE;
}
EXPORT_SYMBOL(ai_isp_sw_wakeup_report);

bool is_chip_power_on(void)
{
	bool is_power_en;

	if (!ai_isp_pm_dev)
		return false;
	mutex_lock(&ai_isp_pm_dev->state_lock);
	is_power_en = ai_isp_pm_dev->pm_state.is_power_en;
	mutex_unlock(&ai_isp_pm_dev->state_lock);

	return is_power_en;
}
EXPORT_SYMBOL(is_chip_power_on);

static irqreturn_t pm_dev_rst_req_thread_handler(int irq, void *dev_id)
{
	struct isp_pm_dev *pm_dev = (struct isp_pm_dev *)dev_id;

	mutex_lock(&pm_dev->state_lock);

	if (pm_dev->pm_state.is_power_en) {
		dev_info(pm_dev->dev, "AI-ISP request for reset!\n");
		/* wakeup reset request wait queue */
		pm_dev->rst_req_flag = AI_ISP_RST_ACTIVE;
		wake_up(&pm_dev->rst_req_wait);
	}
	mutex_unlock(&pm_dev->state_lock);

	return IRQ_HANDLED;
}

static irqreturn_t pm_dev_tcxo_thread_handler(int irq, void *dev_id)
{
	int ret;
	struct isp_pm_dev *pm_dev = (struct isp_pm_dev *)dev_id;

	mutex_lock(&pm_dev->state_lock);

	if (pm_dev->pm_state.is_power_en) {
		if (gpio_get_value(pm_dev->isp_gpios[ISP_TCXO_EN])) {
			dev_info(pm_dev->dev, "AI-ISP tcxo en report!\n");
			if (pm_dev->seperate_pwr_ok)
				v1_isp_lp_seq(pm_dev, false);
			if (pm_dev->vbuck) {
				if (!regulator_is_enabled(pm_dev->vbuck)) {
					ret = regulator_enable(pm_dev->vbuck);
					if (ret)
						dev_err(pm_dev->dev, "Regulator enable failed!\n");
				}
			}
		} else {
			dev_info(pm_dev->dev, "AI-ISP tcxo dis report!\n");
			if (pm_dev->seperate_pwr_ok)
				v1_isp_lp_seq(pm_dev, true);
			if (pm_dev->vbuck) {
				ret = regulator_disable(pm_dev->vbuck);
				if (ret)
					dev_err(pm_dev->dev, "Regulator disable failed!\n");
				if (!regulator_is_enabled(pm_dev->vbuck))
					dev_info(pm_dev->dev, "VBuck disable success!\n");
				else
					dev_info(pm_dev->dev, "VBuck still enable!\n");
			}
		}
	}

	mutex_unlock(&pm_dev->state_lock);

	return IRQ_HANDLED;
}

static int isp_pwr_seq_gpio_init(struct isp_pm_dev *pm_dev)
{
	int id;
	struct device *dev = pm_dev->dev;
	int err_cnt = 0;

	for (id = 0; id < V1_PWR_GPIO_NUM; id++) {
		struct isp_v1_pwr_gpio_desc *desc = &v1_pwr_seq_gpios[id];

		pm_dev->isp_v1_pwr_seq_gpios[id] =
			of_get_named_gpio_flags(dev->of_node, desc->name, 0, NULL);

		if (pm_dev->isp_v1_pwr_seq_gpios[id] < 0) {
			dev_warn(dev, "get %s failed!\n", desc->name);
			err_cnt++;
			continue;
		}

		if (gpio_request(pm_dev->isp_v1_pwr_seq_gpios[id], desc->name)) {
			dev_warn(dev, "request gpio [%s] failed\n", desc->name);
			pm_dev->isp_v1_pwr_seq_gpios[id] = -1;
			err_cnt++;
			continue;
		}
	}

	return err_cnt ? -1 : 0;
}

/* pmctrl_misc setting */
static long pmctrl_ioctl(struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	void __user *ubuf = (void __user *)arg;
	struct isp_pm_dev *pm_dev = (struct isp_pm_dev *)(file->private_data);
	struct device *dev = pm_dev->dev;
	uint32_t pmctrl_devinfo = 1;
	uint32_t tcxo_stat;

	switch (cmd) {
	case PMCTRL_CMD_DEVINFO:
		if (copy_to_user(ubuf, &pmctrl_devinfo, sizeof(uint32_t)))
			return -EINVAL;
		break;
	case PMCTRL_CMD_RST_REQ:
		/* clear reset request flag */
		pm_dev->rst_req_flag = AI_ISP_RST_DEACTIVE;
		dev_info(dev, "pmctrl start to wait!\n");
		wait_event_interruptible(pm_dev->rst_req_wait, pm_dev->rst_req_flag);
		dev_info(dev, "pmctrl wait over!\n");
		if (copy_to_user(ubuf, &pm_dev->rst_req_flag, sizeof(uint32_t)))
			return -EINVAL;
		break;
	case PMCTRL_CMD_TCXO_STAT:
		if (gpio_get_value(pm_dev->isp_gpios[ISP_TCXO_EN]))
			tcxo_stat = TCXO_STAT_EN;
		else
			tcxo_stat = TCXO_STAT_DIS;
		if (copy_to_user(ubuf, &tcxo_stat, sizeof(uint32_t)))
			return -EINVAL;
		break;
	case PMCTRL_CMD_RTOS_WK_STAT:
		if (copy_to_user(ubuf, &pm_dev->rtos_wakeup_flag, sizeof(uint32_t)))
			return -EINVAL;
		break;
	default:
		dev_err(dev, "pmctrl invalid cmd!\n");
		return -ENOIOCTLCMD;
	}

	return ret;
}

static int pmctrl_open(struct inode *inode, struct file *file)
{
	if (!ai_isp_pm_dev) {
		pr_err("aiisp pmctrl: no device found!\n");
		return -EINVAL;
	}

	file->private_data = ai_isp_pm_dev;

	return 0;
}

static int pmctrl_close(struct inode *inode, struct file *file)
{
	file->private_data = NULL;

	return 0;
}

static ssize_t pmctrl_read(struct file *file, char __user *buf, size_t len, loff_t *offp)
{
	/* TODO:Need to be supplemented */

	return -EFAULT;
}

static const struct file_operations pmctrl_misc_fops = {
	.owner		= THIS_MODULE,
	.read		= pmctrl_read,
	.unlocked_ioctl	= pmctrl_ioctl,
	.open		= pmctrl_open,
	.release	= pmctrl_close,
};

static struct miscdevice pmctrl_misc = {
	MISC_DYNAMIC_MINOR,
	"tetras_pmctrl_report",
	&pmctrl_misc_fops,
};

static int pmctrl_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct isp_pm_dev *pm_dev = NULL;
	struct device *dev = &pdev->dev;
	int id;
	int irq_num = 0;

	pm_dev = devm_kzalloc(dev, sizeof(struct isp_pm_dev), GFP_KERNEL);
	if (!pm_dev) {
		dev_err(dev, "failed to allocate memory\n");
		return -ENOMEM;
	}
	pm_dev->dev = dev;

	for (id = 0; id < ISP_GPIO_NUM; id++) {
		struct isp_gpio_desc *desc = &gpios[id];

		pm_dev->isp_gpios[id] =
			of_get_named_gpio_flags(dev->of_node, desc->name, 0, NULL);

		if (pm_dev->isp_gpios[id] < 0) {
			dev_warn(dev, "get %s failed!\n", desc->name);
			continue;
		}

		if (gpio_request(pm_dev->isp_gpios[id], desc->name)) {
			dev_warn(dev, "request gpio [%s] failed\n", desc->name);
			pm_dev->isp_gpios[id] = -1;
			continue;
		}

		if (isp_gpio_init(pm_dev->isp_gpios[id], desc))
			dev_warn(dev, "gpio [%s] init failed\n", desc->name);
	}

	pm_dev->vbuck = devm_regulator_get_exclusive(dev, "vbuck");
	if (IS_ERR(pm_dev->vbuck)) {
		dev_err(dev, "Cannot get vbuck\n");
		pm_dev->vbuck = NULL;
	}

	/* If use seperate power-on gpios, pwr-en io is not used */
	if (pm_dev->isp_gpios[ISP_PWR_EN] < 0) {
		pm_dev->seperate_pwr_ok = true;
		if (isp_pwr_seq_gpio_init(pm_dev)) {
			dev_warn(dev, "seperate pwr-io get failed\n");
			pm_dev->seperate_pwr_ok = false;
		}
	}

	if (pm_dev->isp_gpios[ISP_RST_REQ] >= 0) {
		irq_num = gpio_to_irq(pm_dev->isp_gpios[ISP_RST_REQ]);
		if (irq_num < 0) {
			dev_err(pm_dev->dev, "irq_num invalid %d, gpio %d\n",
				irq_num, pm_dev->isp_gpios[ISP_RST_REQ]);
		} else {
			ret = devm_request_threaded_irq(pm_dev->dev, irq_num, NULL,
							pm_dev_rst_req_thread_handler,
							IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
							"isp_rst_irq", pm_dev);
			if (ret)
				dev_err(pm_dev->dev, "request rst isr failed, ret = %d, irq_num %d\n",
					ret, irq_num);
		}
	}

	if (pm_dev->isp_gpios[ISP_TCXO_EN] >= 0) {
		irq_num = gpio_to_irq(pm_dev->isp_gpios[ISP_TCXO_EN]);
		if (irq_num < 0) {
			dev_err(pm_dev->dev, "irq_num invalid %d, gpio %d\n",
				irq_num, pm_dev->isp_gpios[ISP_TCXO_EN]);
		} else {
			ret = devm_request_threaded_irq(pm_dev->dev, irq_num, NULL,
							pm_dev_tcxo_thread_handler,
							IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING
							| IRQF_ONESHOT,
							"isp_tcxo_irq", pm_dev);
			if (ret)
				dev_err(pm_dev->dev, "request tcxo isr failed, ret = %d, irq_num %d\n",
					ret, irq_num);
		}
	}

	pm_dev->pm_state.is_reset = false;
	pm_dev->pm_state.is_awake = true;
	pm_dev->pm_state.is_power_en = false;

	mutex_init(&pm_dev->state_lock);

	BLOCKING_INIT_NOTIFIER_HEAD(&pm_dev->pm_notifier);
	dev_set_drvdata(dev, pm_dev);
	platform_set_drvdata(pdev, pm_dev);

	ai_isp_pm_dev = pm_dev;

	ret = device_create_file(dev, &dev_attr_pm_ctrl);
	if (ret)
		dev_err(dev, "create file failed\n");

	ret = misc_register(&pmctrl_misc);
	if (ret) {
		dev_err(dev, "misc_register failed\n");
	} else {
		init_waitqueue_head(&pm_dev->rst_req_wait);
		dev_info(dev, "reset request wait_queue init success!\n");
	}

	return ret;
}

static int pmctrl_remove(struct platform_device *pdev)
{
	struct isp_pm_dev *pm_dev = NULL;

	pm_dev = platform_get_drvdata(pdev);

	isp_gpio_free(pm_dev);
	mutex_destroy(&pm_dev->state_lock);
	device_remove_file(&pdev->dev, &dev_attr_pm_ctrl);
	devm_kfree(&pdev->dev, pm_dev);

	dev_info(&pdev->dev, "tetras ai isp pmctrl driver remove done\n");

	return 0;
}

static int ai_isp_pm_suspend_prepare(struct device *dev)
{
	struct isp_pm_dev *pm_dev = NULL;

	pm_dev = dev_get_drvdata(dev);
	if (!pm_dev)
		return 0;

	if (pm_dev->isp_gpios[ISP_TCXO_EN] >= 0) {
		/* If TCXO_EN PIN not LOW, AI-ISP not in sleep state */
		if (gpio_get_value(pm_dev->isp_gpios[ISP_TCXO_EN]) != GPIO_OUT_LOW) {
			dev_err(dev, "ai_isp not in sleep state!\n");
			return -EBUSY;
		}
	}

	return 0;
}

static const struct dev_pm_ops aiisp_pm = {
	.prepare = ai_isp_pm_suspend_prepare,
};
static const struct of_device_id pmctrl_of_match[] = {
	{.compatible = "tetras,ai-isp-pm_ctrl", },
	{},
};
MODULE_DEVICE_TABLE(of, pmctrl_of_match);

static struct platform_driver pmctrl_driver = {
	.probe     = pmctrl_probe,
	.remove    = pmctrl_remove,
	.driver    = {
			.name = "tetras-pmctrl",
			.owner = THIS_MODULE,
			.pm = &aiisp_pm,
			.of_match_table = of_match_ptr(pmctrl_of_match),
		     },
};

static int __init pmctrl_init(void)
{
	int ret;

	ret = platform_driver_register(&pmctrl_driver);
	if (ret)
		pr_warn("aiisp pmctrl driver not registered\n");

	return ret;
}

static void __exit pmctrl_exit(void)
{
	misc_deregister(&pmctrl_misc);
	platform_driver_unregister(&pmctrl_driver);
}

module_init(pmctrl_init);
module_exit(pmctrl_exit);

MODULE_AUTHOR("yanghua@tetras.ai");
MODULE_DESCRIPTION("AI-ISP Power Control Driver 20220628");
MODULE_LICENSE("GPL v2");
