#ifndef __TC_PDLC_H__
#define __TC_PDLC_H__

/*******************************************************************************
 *
 * struct
 *
 ******************************************************************************/
#define TRAN_PDLC_POWER_ON 1
#define TRAN_PDLC_POWER_OFF 0
#define TRAN_PDLC_LOG_ENABLE

#if defined(TRAN_PDLC_LOG_ENABLE)
#define TRAN_PDLC_LOG(fmt, args...) pr_info("[Tran_pdlc]%s %d: " fmt, \
		__func__, __LINE__, ##arg)
#else
#define TRAN_PDLC_LOG(fmt, args...)
#endif

#define TRAN_PDLC_ERR(fmt, args...)	pr_err("[Tran_pdlc]%s %d: " fmt, \
		__func__, __LINE__, ##args)

struct tc_pdlc_pinctrl {
	struct pinctrl_state *gpio_boost_on_state;
	struct pinctrl_state *gpio_boost_off_state;
};

struct pdlc_data {
	struct device *pdlc_dev;
	struct work_struct pdlc_work;
	struct delayed_work pdlc_boost_off_work;
	struct platform_device *pdev;
	struct tc_led_device *led_dev;
	struct notifier_block pdlc_nb;
	struct led_classdev pdlc_led_dev;
	struct wakeup_source wakelock;
	struct pinctrl *pinctrl;
	//struct tc_pdlc_pinctrl pinctrl_data;
	int pdlc_current_status;
	int pdlc_breath_time;
	int latest_cmd;
	int detect_gpio;
	bool run_flag;
	bool support_pdlc_hw_detect;
	bool boost_off_onging;
	atomic_t pdlc_eanble;
	struct pinctrl_state *gpio_boost_on_state;
	struct pinctrl_state *gpio_boost_off_state;
};
#endif
