// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Transsion Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/time.h>
#if IS_ENABLED(CONFIG_TC_CHARGER)
#include "tc_common_class.h"
#include "tc_misc_intf.h"
#endif
#include "tcpm.h"
#include "tcpci_core.h"
#include <linux/of_gpio.h>

struct splitswitch_gpio_priv {
	int mic_gnd_switch_dio1520;
	int audio_usb_switch_dio32020;
};

struct splitswitch_gpio_priv g_splitswitch_gpio_data = {
	.mic_gnd_switch_dio1520 = -1,
	.audio_usb_switch_dio32020 = -1,
};

enum g_splitswitch_gpio_status {
	ANA_TYPEC_PLUG_OUT,
	ANA_TYPEC_PLUG_IN,
	ANA_TYPEC_ACCDET_CHECK_0,
	ANA_TYPEC_ACCDET_CHECK_1,
	ANA_TYPEC_NO_MIC,
};

void tran_gpio_set_for_dio1520(int cmd)
{
	switch (cmd) {
	case ANA_TYPEC_PLUG_IN:
		gpio_set_value(g_splitswitch_gpio_data.audio_usb_switch_dio32020, 1);
		break;
	case ANA_TYPEC_PLUG_OUT:
		gpio_set_value(g_splitswitch_gpio_data.mic_gnd_switch_dio1520, 0);
		gpio_set_value(g_splitswitch_gpio_data.audio_usb_switch_dio32020, 0);
		break;
	case ANA_TYPEC_ACCDET_CHECK_0:
		gpio_set_value(g_splitswitch_gpio_data.mic_gnd_switch_dio1520, 0);
		break;
	case ANA_TYPEC_ACCDET_CHECK_1:
		gpio_set_value(g_splitswitch_gpio_data.mic_gnd_switch_dio1520, 1);
		break;
	case ANA_TYPEC_NO_MIC:
		gpio_set_value(g_splitswitch_gpio_data.mic_gnd_switch_dio1520, 0);
		break;
	default:
		break;
	}
	pr_info("%s() cmd=%d, audio_usb_switch_dio32020=%d, mic_gnd_switch_dio1520=%d", __func__, cmd, gpio_get_value(g_splitswitch_gpio_data.audio_usb_switch_dio32020), gpio_get_value(g_splitswitch_gpio_data.mic_gnd_switch_dio1520));
}

void tran_splitswitch_set_gpio(int cmd)
{
#if IS_ENABLED(CONFIG_USB_SWITCH_DIO1520)
	tran_gpio_set_for_dio1520(cmd);
#endif
}
EXPORT_SYMBOL(tran_splitswitch_set_gpio);

static int sliptswitch_gpio_probe(struct platform_device *pdev)
{
	pr_info("%s() enter", __func__);
	struct device_node *np = of_find_compatible_node(NULL, NULL, "transsion,sliptswitch-gpio-audioswitch");
	if (np) {
#if IS_ENABLED(CONFIG_USB_SWITCH_DIO1520)
		g_splitswitch_gpio_data.audio_usb_switch_dio32020 = of_get_named_gpio(np, "audio_usb_switch_dio32020", 0);
		g_splitswitch_gpio_data.mic_gnd_switch_dio1520 = of_get_named_gpio(np, "mic_gnd_switch_dio1520", 0);
		pr_info("%s() audio_usb_switch_dio32020=%d, mic_gnd_switch_dio1520=%d", __func__, g_splitswitch_gpio_data.audio_usb_switch_dio32020, g_splitswitch_gpio_data.mic_gnd_switch_dio1520);
#endif
	}

	return 0;
}

static int sliptswitch_gpio_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id sliptswitch_gpio_match[] = {
	{ .compatible = "transsion,sliptswitch-gpio-audioswitch", },
	{ },
};

static struct platform_driver sliptswitch_gpio_driver = {
	.probe = sliptswitch_gpio_probe,
	.remove = sliptswitch_gpio_remove,
	.driver = {
		.name = "sliptswitch_gpio",
		.owner = THIS_MODULE,
		.of_match_table = sliptswitch_gpio_match,
	},
};

static int __init sliptswitch_gpio_init(void)
{
	int ret = 0;
	ret = platform_driver_register(&sliptswitch_gpio_driver);
	if (ret)
		pr_info("sliptswitch_gpio_driver driver register error %d", ret);
	return ret;
}

static void __exit sliptswitch_gpio_exit(void)
{
	platform_driver_unregister(&sliptswitch_gpio_driver);
}

module_init(sliptswitch_gpio_init);
module_exit(sliptswitch_gpio_exit);

MODULE_DESCRIPTION("SLIPTSWITCH GPIO Driver");
MODULE_AUTHOR("transsion.com");
MODULE_LICENSE("GPL v2");
