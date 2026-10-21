// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Transsion Inc.
 */

#include <linux/err.h>
#include <linux/dev_printk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <sound/soc.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include "tran_audio_control.h"
int g_audio_pa_type;
int g_tran_pa_mode;
int g_tran_pa_open;
enum tran_pa_type pa_type;
pa_control_struct pa_register[TRAN_PA_TYPE_MAX];

int register_to_pa_manager(pa_control_struct *pa)
{
	int register_id = pa->id;
	pr_info("register_id %d ", pa->id);
	if (register_id >= TRAN_PA_TYPE_MAX || register_id < TRAN_PA_TYPE_MIN) {
		pr_err("id %d is wrong", pa->id);
		return -EINVAL;
	}

	if (pa_register[register_id].has_register) {
		pr_err("id %d, name %s pa had registered, can not register again",
			pa->id, pa->name);
		return -EEXIST;
	}

	pa_register[register_id].id = register_id;
	memcpy(pa_register[register_id].name, pa->name, PA_NAME_SIZE);
	pa_register[register_id].open_pa = pa->open_pa;
	pa_register[register_id].close_pa = pa->close_pa;
	pa_register[register_id].set_pa = pa->set_pa;
	pa_register[register_id].has_register = true;
	return 0;
}
EXPORT_SYMBOL(register_to_pa_manager);

int unregister_to_pa_manager(enum tran_pa_type id)
{
	if (id >= TRAN_PA_TYPE_MAX || id < TRAN_PA_TYPE_MIN) {
		pr_err("id %d is wrong", id);
		return -EINVAL;
	}

	if (!pa_register[id].has_register) {
		pr_err("id %d pa had not registered, can not unregister", id);
		return -EEXIST;
	}

	pa_register[id].id = -1;
	memset(pa_register[id].name, 0, PA_NAME_SIZE);
	pa_register[id].open_pa = NULL;
	pa_register[id].close_pa = NULL;
	pa_register[id].set_pa = NULL;
	pa_register[id].has_register = false;
	return 0;
}
EXPORT_SYMBOL(unregister_to_pa_manager);

/*********************************************
 **  fs1599n_pa
 ********************************************/
#if IS_ENABLED(CONFIG_SND_SOC_FS1599) || IS_ENABLED(CONFIG_SND_SOC_FS1589)
extern void fsm_speaker_onn(void);
extern void fsm_speaker_off(void);
extern void fsm_set_scene(int scene);
extern void fsm_add_card_controls(struct snd_soc_card *card);

int open_fs1599_pa(int mode)
{
	pr_info("mode: %d", mode);
	fsm_speaker_onn();
	return 0;
}

int close_fs1599_pa(void)
{
	fsm_speaker_off();
	return 0;
}

int set_fs1599_pa(int mode)
{
	pr_debug(" set_fs1599_pa mode: %d", mode);
	fsm_set_scene(mode);
	return 0;
}

void fs1599_pa_has_register(void)
{
	pa_control_struct fs1599_pa;
	fs1599_pa.id = TRAN_PA_TYPE_FS1599N;
	strncpy(fs1599_pa.name, "FS1599N PA", PA_NAME_SIZE - 1);
	fs1599_pa.open_pa = open_fs1599_pa;
	fs1599_pa.close_pa = close_fs1599_pa;
	fs1599_pa.set_pa = set_fs1599_pa;
	fs1599_pa.has_register = true;

	register_to_pa_manager(&fs1599_pa);
}

#endif

/*********************************************
 **  oca72xxx_pa
 ********************************************/
#if IS_ENABLED(CONFIG_SND_SOC_OCA72XXX)
int g_oca_pa_mode_0;
int g_oca_pa_mode_1;
extern int oca72xxx_add_codec_controls(void *codec);
extern int oca72xxx_set_profile(int dev_index, char *profile);
static char *oca_profile[] = {"Music", "Voice", "Voip", "Ring", "Low_pwr",
	"MMI_Bypass", "MMI_Bypass", "Top_Left", "Top_Left_Bypass", "Top_Right",
	"Top_Right_Bypass", "Bot_Left", "Bot_Left_Bypass", "Bot_Right", "Bot_Right_Bypass",
	"Receiver", "Off"};

int open_oca72xxx_pa(int mode)
{
	pr_info("open_oca72xxx_pa mode_0:%d, mode_1:%d", g_oca_pa_mode_0, g_oca_pa_mode_1);
	oca72xxx_set_profile(OCA_DEV_0, oca_profile[g_oca_pa_mode_0]);
	oca72xxx_set_profile(OCA_DEV_1, oca_profile[g_oca_pa_mode_1]);
	return 0;
}

int close_oca72xxx_pa(void)
{
	pr_info("close_oca72xxx_pa");
	oca72xxx_set_profile(OCA_DEV_0, oca_profile[16]);
	oca72xxx_set_profile(OCA_DEV_1, oca_profile[16]);
	return 0;
}
int set_oca72xxx_pa(int mode)
{
	pr_info("set_oca72xxx_pa mode: %d  oca_profile: %s ", mode, oca_profile[mode]);
	switch (mode) {
	case TRAN_MODE_MUSIC:
	case TRAN_MODE_VOICE:
	case TRAN_MODE_VOIP:
	case TRAN_MODE_RING:
	case TRAN_MODE_LOW_PWR:
	case TRAN_MODE_MMI_ALL:
	case TRAN_MODE_MMI_ALL_BYPASS:
	case TRAN_MODE_MAX:
		g_oca_pa_mode_0 = mode;
		g_oca_pa_mode_1 = mode;
		//oca72xxx_set_profile(OCA_DEV_0, oca_profile[mode]);
		//oca72xxx_set_profile(OCA_DEV_1, oca_profile[mode]);
		break;
	case TRAN_MODE_RECEIVER:
	case TRAN_MODE_TOP_LEFT:
	case TRAN_MODE_TOP_LEFT_BYPASS:
	case TRAN_MODE_TOP_RIGHT:
	case TRAN_MODE_TOP_RIGHT_BYPASS:
		g_oca_pa_mode_0 = mode;
		g_oca_pa_mode_1 = 16;
		oca72xxx_set_profile(OCA_DEV_0, oca_profile[mode]);
		oca72xxx_set_profile(OCA_DEV_1, oca_profile[16]);
		break;
	case TRAN_MODE_BOT_RIGHT:
	case TRAN_MODE_BOT_RIGHT_BYPASS:
		g_oca_pa_mode_0 = 16;
		g_oca_pa_mode_1 = mode;
		oca72xxx_set_profile(OCA_DEV_0, oca_profile[16]);
		oca72xxx_set_profile(OCA_DEV_1, oca_profile[mode]);
		break;
	default:
		break;
	}

	return 0;
}

void oca72xxx_pa_has_register(void)
{
	pa_control_struct  oca72xxx_pa;
	oca72xxx_pa.id = TRAN_PA_TYPE_OCA72XXX;
	strncpy(oca72xxx_pa.name, "OCA72XXX PA", PA_NAME_SIZE - 1);
	oca72xxx_pa.open_pa = open_oca72xxx_pa;
	oca72xxx_pa.close_pa = close_oca72xxx_pa;
	oca72xxx_pa.set_pa = set_oca72xxx_pa;
	oca72xxx_pa.has_register = true;

	register_to_pa_manager(&oca72xxx_pa);
}
#endif


#if IS_ENABLED(CONFIG_SND_SOC_AW87XXX)
/*********************************************
 **  aw87390_pa
 ********************************************/
extern int aw87xxx_set_profile(int dev_index, char *profile);
static char *aw_profile[] = {"Music", "Voice", "Voip", "Ring", "Low_pwr",
	"MMI_Bypass", "MMI_Bypass", "Top_Left", "Top_Left_Bypass", "Top_Right",
	"Top_Right_Bypass", "Bot_Left", "Bot_Left_Bypass", "Bot_Right", "Bot_Right_Bypass",
	"Receiver", "Off"};
int open_aw87390_pa(int mode)
{
	pr_debug("open_aw87390_pa mode: %d", mode);
	if (g_tran_pa_open == 0) {
		aw87xxx_set_profile(AW_DEV_0, aw_profile[0]);
		aw87xxx_set_profile(AW_DEV_1, aw_profile[0]);
		g_tran_pa_open = 1;
	} else {
		pr_debug("open_aw87390_pa already open, do nothing ");
	}
	return 0;
}

int close_aw87390_pa(void)
{
	g_tran_pa_open = 0;
	g_tran_pa_mode = 16;
	aw87xxx_set_profile(AW_DEV_0, aw_profile[16]);
	aw87xxx_set_profile(AW_DEV_1, aw_profile[16]);
	return 0;
}

int set_aw87390_pa(int mode)
{
	pr_info("set_aw87390_pa mode: %d  aw_profile: %s ", mode, aw_profile[mode]);
	g_tran_pa_open = 1;
	switch (mode) {
	case TRAN_MODE_MUSIC:
	case TRAN_MODE_VOICE:
	case TRAN_MODE_VOIP:
	case TRAN_MODE_RING:
	case TRAN_MODE_LOW_PWR:
	case TRAN_MODE_MMI_ALL:
	case TRAN_MODE_MMI_ALL_BYPASS:
	case TRAN_MODE_MAX:
		aw87xxx_set_profile(AW_DEV_0, aw_profile[mode]);
		aw87xxx_set_profile(AW_DEV_1, aw_profile[mode]);
		break;
	case TRAN_MODE_RECEIVER:
	case TRAN_MODE_TOP_LEFT:
	case TRAN_MODE_TOP_LEFT_BYPASS:
	case TRAN_MODE_TOP_RIGHT:
	case TRAN_MODE_TOP_RIGHT_BYPASS:
		aw87xxx_set_profile(AW_DEV_0, aw_profile[mode]);
		aw87xxx_set_profile(AW_DEV_1, aw_profile[16]);
		break;
	case TRAN_MODE_BOT_RIGHT:
	case TRAN_MODE_BOT_RIGHT_BYPASS:
		aw87xxx_set_profile(AW_DEV_0, aw_profile[16]);
		aw87xxx_set_profile(AW_DEV_1, aw_profile[mode]);
		break;
	default:
		break;
	}

	return 0;
}

void aw87390_pa_has_register(void)
{
	pa_control_struct  aw87390_pa;
	aw87390_pa.id = TRAN_PA_TYPE_AW87390;
	strncpy(aw87390_pa.name, "AW87390 PA", PA_NAME_SIZE - 1);
	aw87390_pa.open_pa = open_aw87390_pa;
	aw87390_pa.close_pa = close_aw87390_pa;
	aw87390_pa.set_pa = set_aw87390_pa;
	aw87390_pa.has_register = true;

	register_to_pa_manager(&aw87390_pa);
}
#endif

#if IS_ENABLED(CONFIG_SND_SMARTPA_AW883XX)
/*********************************************
 **  aw883xx_pa
 ********************************************/
extern int aw883xx_set_profile(int mode_index);
static char *aw883xx_profile[] = {"Music", "Voice", "Voip", "Ring", "Low_pwr",
	"MMI_Bypass", "MMI_Bypass", "Top_Left", "Top_Left_Bypass", "Top_Right",
	"Top_Right_Bypass", "Bot_Left", "Bot_Left_Bypass", "Bot_Right", "Bot_Right_Bypass",
	"Receiver", "Off"};
int open_aw883xx_pa(int mode)
{
	pr_info("open_aw883xx_pa mode: %d", mode);
	aw883xx_set_profile(mode);
	return 0;
}

int close_aw883xx_pa(void)
{
	pr_info("close close_aw883xx_pa:");
	aw883xx_set_profile(16);
	return 0;
}

int set_aw883xx_pa(int mode)
{
	pr_info(" set_aw883xx_pa mode: %d  aw883xx_profile: %s ", mode, aw883xx_profile[mode]);
	aw883xx_set_profile(mode);
	return 0;
}

void aw883xx_pa_has_register(void)
{
	pa_control_struct  aw883xx_pa;
	aw883xx_pa.id = TRAN_PA_TYPE_SMARTPA_AW883XX;
	strncpy(aw883xx_pa.name, "AW883XX PA", PA_NAME_SIZE - 1);
	aw883xx_pa.open_pa = open_aw883xx_pa;
	aw883xx_pa.close_pa = close_aw883xx_pa;
	aw883xx_pa.set_pa = set_aw883xx_pa;
	aw883xx_pa.has_register = true;

	register_to_pa_manager(&aw883xx_pa);
}
#endif

/*********************************************
 **  typec_function start
 ********************************************/
#if IS_ENABLED(CONFIG_USB_SWITCH_WAS4783C) || IS_ENABLED(CONFIG_USB_SWITCH_HL1280) || IS_ENABLED(CONFIG_USB_SWITCH_SPLIT)
struct ext_typec_data {
	int typec_usb_change_gpio;
};
struct ext_typec_data g_typec_data = {
	.typec_usb_change_gpio = -1,
};

static void tran_parse_dts_node_for_typec(void)
{
	struct device_node *np = of_find_compatible_node(NULL, NULL, "tran_audio,audio");
	g_typec_data.typec_usb_change_gpio = -1;
	if (np) {
		g_typec_data.typec_usb_change_gpio = of_get_named_gpio(np, "typec_usb_change_gpio", 0);
		pr_info("%s() typec_usb_change_gpio = %d\n", __func__, g_typec_data.typec_usb_change_gpio);
	}
}

static int Tran_TypeC_USB_Change_GPIO_Get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s() enter\n", __func__);
	return 0;
}
static int Tran_TypeC_USB_Change_GPIO_Set(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	pr_info("%s() enter\n", __func__);
	if (ucontrol->value.integer.value[0]) {
		gpio_set_value(g_typec_data.typec_usb_change_gpio, 1);
	} else {
		pr_info("%s() set gpio low\n", __func__);
		gpio_set_value(g_typec_data.typec_usb_change_gpio, 0);
	}
	return 0;
}

static const char *const typec_function[] = { "Off", "On" };

static const struct soc_enum Audio_TYPEC_Enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(typec_function), typec_function),
};
#endif
/*********************************************
 **  typec_function end
 ********************************************/

int tran_audio_pa_type_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_audio_pa_type;
	pr_info("tran_audio_pa_type_get g_audio_pa_type(%d)", g_audio_pa_type);

	return 0;
}

int tran_audio_pa_type_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{

	return 0;
}

int tran_pa_open_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_tran_pa_open;
	pr_info("tran_pa_open_get g_tran_pa_open(%d)", g_tran_pa_open);

	return 0;
}

int tran_pa_open_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{

	g_tran_pa_open = ucontrol->value.integer.value[0];

	pr_info("g_tran_pa_open: %d", g_tran_pa_open);
	pr_info("g_audio_pa_type: %d", g_audio_pa_type);
	pa_type = (enum tran_pa_type) g_audio_pa_type;

	if (pa_register[pa_type].has_register) {
		if (ucontrol->value.integer.value[0]) {
			pa_register[pa_type].open_pa(0);
		} else {
			pa_register[pa_type].close_pa();
		}
	} else {
		pr_err("PA type %d has not registered", pa_type);
	}

	return 0;
}

int tran_pa_mode_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_tran_pa_mode;
	pr_info("tran_pa_mode_get g_tran_pa_mode(%d)", g_tran_pa_mode);

	return 0;
}

int tran_pa_mode_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	int new_mode = ucontrol->value.integer.value[0];

	if (new_mode < TRAN_MODE_NONE || new_mode >= TRAN_MODE_MAX) {
		pr_err("Invalid tran_pa_mode value: %d", new_mode);
		return -EINVAL;
	}

	g_tran_pa_mode = (enum tran_pa_mode_t)new_mode;

	pr_info("g_tran_pa_mode: %d", g_tran_pa_mode);

	pa_type = (enum tran_pa_type) g_audio_pa_type;

	if (pa_register[pa_type].has_register) {
		pa_register[pa_type].set_pa(g_tran_pa_mode);
	} else {
		pr_err("PA type %d has not registered", pa_type);
	}

	return 0;
}

static const struct snd_kcontrol_new tran_audio_snd_controls[] = {
	SOC_SINGLE_EXT("Tran_Audio_PA_TYPE", SND_SOC_NOPM, 0, 10, 0,
			tran_audio_pa_type_get, tran_audio_pa_type_put),
	SOC_SINGLE_EXT("Tran_PA_OPEN", SND_SOC_NOPM, 0, 1, 0,
			tran_pa_open_get, tran_pa_open_put),
	SOC_SINGLE_EXT("Tran_Pa_Scene", SND_SOC_NOPM, 0, 17, 0,
			tran_pa_mode_get, tran_pa_mode_put),
#if IS_ENABLED(CONFIG_USB_SWITCH_WAS4783C) || IS_ENABLED(CONFIG_USB_SWITCH_HL1280) || IS_ENABLED(CONFIG_USB_SWITCH_SPLIT)
	SOC_ENUM_EXT("Tran_TypeC_USB_Change_Switch", Audio_TYPEC_Enum[0],
			Tran_TypeC_USB_Change_GPIO_Get, Tran_TypeC_USB_Change_GPIO_Set),
#endif
};

void tran_audio_pa_switch(bool pa_open_value)
{
	pr_info("tran_audio_pa_switch g_audio_pa_type(%d), pa_open_value(%d)", g_audio_pa_type, pa_open_value);
	if (pa_register[g_audio_pa_type].has_register) {
		if (pa_open_value) {
			pa_register[g_audio_pa_type].open_pa(0);
		} else {
			pa_register[g_audio_pa_type].close_pa();
		}
		return;
	}
}
EXPORT_SYMBOL(tran_audio_pa_switch);

void tran_audio_add_card_controls(struct snd_soc_card *card)
{
#if IS_ENABLED(CONFIG_SND_SOC_FS1599) || IS_ENABLED(CONFIG_SND_SOC_FS1589)
	fsm_add_card_controls(card);
#endif

	snd_soc_add_card_controls(card, tran_audio_snd_controls,
			ARRAY_SIZE(tran_audio_snd_controls));
}
EXPORT_SYMBOL(tran_audio_add_card_controls);

/*********************************************
 **  dts parse
 ********************************************/

static int tran_parse_dts_node(struct platform_device *pdev)
{
	pr_info("%s enter in!\n", __func__);
	if (of_property_read_u32(pdev->dev.of_node, "gpio_no", &g_spk_amp_data.gpio_no)) {
		pr_err("[tran_pa_controller] Cannot find gpio_no!\n");
	}

	if (of_property_read_u32(pdev->dev.of_node, "dualspeaker_gpio_no",
		&g_spk_amp_data.dualspeaker_gpio_no)) {
		pr_err("[tran_pa_controller] Cannot find dualspeaker_gpio_no!\n");
	}

	if (of_property_read_u32(pdev->dev.of_node, "normal_mode", &g_spk_amp_data.normal_mode)) {
		pr_err("[tran_pa_controller] Cannot find normal_mode!\n");
	}

	if (of_property_read_u32(pdev->dev.of_node, "top_speech_mode", &g_spk_amp_data.top_speech_mode)) {
		pr_err("[tran_pa_controller] Cannot find top_speech_mode!\n");
	}

	if (of_property_read_u32(pdev->dev.of_node, "bottom_speech_mode",
		&g_spk_amp_data.bottom_speech_mode)) {
		g_spk_amp_data.bottom_speech_mode = g_spk_amp_data.top_speech_mode;
		pr_err("[tran_pa_controller] Cannot find bottom_speech_mode!\n");
	}

	if (of_property_read_u32(pdev->dev.of_node, "receiver_mode", &g_spk_amp_data.receiver_mode)) {
		pr_err("[tran_pa_controller] Cannot find receiver_mode!\n");
	}

	if (of_property_read_u32(pdev->dev.of_node, "top_fm_mode", &g_spk_amp_data.top_fm_mode)) {
		pr_err("[tran_pa_controller] Cannot find top_fm_mode!\n");
	}

	if (of_property_read_u32(pdev->dev.of_node, "bottom_fm_mode", &g_spk_amp_data.bottom_fm_mode)) {
		pr_err("[tran_pa_controller] Cannot find bottom_fm_mode!\n");
	}

	if (of_property_read_u32(pdev->dev.of_node, "tran_pa_type", &g_audio_pa_type)) {
		pr_err("[tran_pa_type] Cannot find tran_pa_type!\n");
	}

	pr_info("[tran_pa_controller] gpio_no = %d\n", g_spk_amp_data.gpio_no);
	pr_info("[tran_pa_controller] dualspeaker_gpio_no = %d\n", g_spk_amp_data.dualspeaker_gpio_no);
	pr_info("[tran_pa_controller] normal_mode = %d\n", g_spk_amp_data.normal_mode);
	pr_info("[tran_pa_controller] top_speech_mode = %d\n", g_spk_amp_data.top_speech_mode);
	pr_info("[tran_pa_controller] bottom_speech_mode = %d\n", g_spk_amp_data.bottom_speech_mode);
	pr_info("[tran_pa_controller] receiver_mode = %d\n", g_spk_amp_data.receiver_mode);
	pr_info("[tran_pa_controller] top_fm_mode = %d\n", g_spk_amp_data.top_fm_mode);
	pr_info("[tran_pa_type] g_audio_pa_type = %d\n", g_audio_pa_type);

	return 0;
}

static int tran_audio_controller_dev_probe(struct platform_device *pdev)
{
	int ret = 0;
	pr_info("%s enter in!\n", __func__);
	tran_parse_dts_node(pdev);
#if IS_ENABLED(CONFIG_USB_SWITCH_WAS4783C) || IS_ENABLED(CONFIG_USB_SWITCH_HL1280) || IS_ENABLED(CONFIG_USB_SWITCH_SPLIT)
	tran_parse_dts_node_for_typec();
#endif
#if IS_ENABLED(CONFIG_SND_SOC_AW87XXX)
	aw87390_pa_has_register();
#endif
#if IS_ENABLED(CONFIG_SND_SOC_FS1599) || IS_ENABLED(CONFIG_SND_SOC_FS1589)
	fs1599_pa_has_register();
#endif
#if IS_ENABLED(CONFIG_SND_SMARTPA_AW883XX)
	aw883xx_pa_has_register();
#endif
#if IS_ENABLED(CONFIG_SND_SOC_OCA72XXX)
	oca72xxx_pa_has_register();
#endif
	pr_info("%s done!\n", __func__);
	return ret;
}

//
void tran_audio_add_codec_controls(struct snd_soc_component *cmpnt)
{
#if IS_ENABLED(CONFIG_SND_SOC_OCA72XXX)
	oca72xxx_add_codec_controls((void *)cmpnt);
#endif
}
EXPORT_SYMBOL(tran_audio_add_codec_controls);

static int tran_audio_controller_dev_remove(struct platform_device *pdev)
{

	return 0;
}

static const struct of_device_id tran_audio_controller_match[] = {
	{ .compatible = "tran-audio-controller", },
	{ },
};

static struct platform_driver tran_audio_controller_driver = {
	.probe = tran_audio_controller_dev_probe,
	.remove = tran_audio_controller_dev_remove,
	.driver = {
		.name = "tran_audio_controller",
		.owner = THIS_MODULE,
		.of_match_table = tran_audio_controller_match,
	},
};

static int __init tran_audio_controller_init(void)
{
	int ret = 0;
	pr_info("tran_audio_controller_driver  driver register error %d", ret);
	ret = platform_driver_register(&tran_audio_controller_driver);
	if (ret)
		pr_info("tran_audio_controller_driver driver register error %d", ret);
	return ret;
}

static void __exit tran_audio_controller_exit(void)
{
	platform_driver_unregister(&tran_audio_controller_driver);
}

module_init(tran_audio_controller_init);
module_exit(tran_audio_controller_exit);

MODULE_DESCRIPTION("tran audio control");
MODULE_AUTHOR("transsion.com");
MODULE_LICENSE("GPL v2");
