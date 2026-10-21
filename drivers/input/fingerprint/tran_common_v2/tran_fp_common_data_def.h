#ifndef __TRAN_FP_COMMON_DATA_DEF_H__
#define __TRAN_FP_COMMON_DATA_DEF_H__
#ifdef CONFIG_PM_SLEEP
#include <linux/pm_wakeup.h>
#else
#include <linux/wakelock.h>
#endif

#define CONFIG_SPI_MT65XX_KERNEL510 y

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/cdev.h>
#include <linux/input.h>
#include <linux/timer.h>
#include "mtk_disp_notify.h"

#if !defined(CONFIG_SPI_MT65XX_KERNEL510)
#include <mt_spi.h>
#include <mt_spi_hal.h>
#endif

#define FP_NAME_SIZE 32
#define KEY_MAP_SIZE 16

enum work_mode {
	FINGER_MODE_NONE       = 1<<0,
	FINGER_INTERRUPT_MODE  = 1<<1,
	FINGER_KEY_MODE        = 1<<2,
	FINGER_FINGER_UP_MODE  = 1<<3,
	FINGER_READ_IMAGE_MODE = 1<<4,
	FINGER_MODE_MAX
};

enum power_suport {
	POWER_SUPORT_NONE = 0,
	POWER_SUPORT_REGULATOR,
	POWER_SUPORT_GPIO,
	POWER_SUPORT_REGULATOR_GPIO,
	POWER_SUPORT_MAX
};

struct tran_fp_interrupt_desc_t
{
	int gpio;
	int number;
	char *name;
	int int_count;
	struct timer_list timer;
	int finger_on;
	int detect_period;
	int detect_threshold;
	int drdy_irq_flag;
	int drdy_irq_abort;
};

struct tran_fp_data {
	/*************transsion start*******************************/
	dev_t devno;
	struct spi_device	*tran_fp_dev;
	struct platform_device	*tran_fp_plat_dev;
	struct cdev cdev;
	struct device *device;
	struct class *class;
	int device_count;
#ifndef CONFIG_SPI_MT65XX_KERNEL510
	struct mt_chip_conf spi_mcc;
#endif
#ifndef CONFIG_TRAN_FP_DRV_MODE_SPI
	struct clk * clk;
#endif
	struct regulator *vdd;
	int min_uV;
	int max_uV;
	const char *always_on;
	int hw_pwr_gpio;
	enum power_suport fp_regulator_support;
	u32 power_is_init;
	u32 power_is_on;
	spinlock_t spi_lock;
	struct list_head device_entry;
	char modalias[FP_NAME_SIZE];
	char ic_name[FP_NAME_SIZE];
	char vendor_name[FP_NAME_SIZE];
	struct input_dev *input;
	char fod_location_xy[FP_NAME_SIZE];
	u8 *tran_buffer;  /* only used for SPI transfer internal */
	struct mutex buf_lock;
	struct mutex release_lock;
	u8 buf_status;
	u8 tran_tee_afinity;
	u8 tran_openHal_flag;
	u8 tran_opt_fp_support;
	/* for netlink use */
	struct sock *nl_sk;
	struct sock *nl_sk_bio;

#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#else
	struct notifier_block notifier;
#endif

	u8 probe_finish;
	u8 irq_count;
	u8  need_update;
	/* bit24-bit32 of signal count */
	/* bit16-bit23 of event type, 1: key down; 2: key up; 3: fp data ready; 4: home key */
	/* bit0-bit15 of event type, buffer status register */
	u8 sig_count;
	u8 is_sleep_mode;
	u8 system_status;

	u32 event_type;
	int cs_gpio;
	int reset_gpio;
	int irq_gpio;
	u32 irq;
	int spi_buf_size;
	struct device_node *irq_node;
#ifdef CONFIG_OF
	struct pinctrl *tran_pinctrl;
	struct pinctrl_state *pins_irq;
	struct pinctrl_state *pins_reset_high, *pins_reset_low;
	struct pinctrl_state *pins_spi_default, *pins_spi_gpio;
#endif
	unsigned int users;
	u8 device_available;    /* changed during fingerprint chip sleep and wakeup phase */
	u8 irq_enabled;
	volatile unsigned int RcvIRQ;
#ifdef CONFIG_PM_SLEEP
	struct wakeup_source *tran_wakelock;
#else
	struct wake_lock tran_wakelock;
#endif
	wait_queue_head_t wq_irq_return;
	int cancel;
	u8 fb_status;
	int enable_report_blankon;
	int free_flag;
    bool history_support;

	struct miscdevice miscdev;
	int pwr_num;
	struct work_struct work_queue;
	struct workqueue_struct *wqueue;
	struct regulator *vdd_reg;
	int  (*resource_init) (struct tran_fp_data *tran_fp_dev);
	int  (*power_init) (struct tran_fp_data *tran_fp_dev, bool on);
	int  (*power_onoff) (struct tran_fp_data *tran_fp_dev, bool onoff);
	int  (*gpio_init) (struct tran_fp_data *tran_fp_dev);
	int  (*free_gpio) (struct tran_fp_data *tran_fp_dev);
	int  (*spi_clk_on)(struct tran_fp_data *tran_fp_datap);
	int  (*spi_clk_off)(struct tran_fp_data *tran_fp_datap);
	int  (*reset)(struct tran_fp_data *tran_fp_dev, u32 inter_delay, u32 post_delay);
	int  (*shutdown)(struct tran_fp_data *tran_fp_dev);
	void  (*enable_irq)(struct tran_fp_data *tran_fp_dev);
	void  (*disable_irq)(struct tran_fp_data *tran_fp_dev);
	int  (*irq_init)(void);
	int  (*proc_init)(struct tran_fp_data *tran_fp_dev);
	int  (*proc_deinit)(struct tran_fp_data *tran_fp_dev);
	int  (*input_init)(struct tran_fp_data *tran_fp_dev);
	int attribute;
	u32 inter_delay;
	u32 post_delay;
	struct proc_dir_entry * pde;
	atomic_t  spi_clk_onoff_cnt;
	/*************transsion end*******************************/
};

typedef enum tran_key_nav_event {
	TRAN_KEY_NAV_NONE = 0,
	TRAN_KEY_NAV_HOME,
	TRAN_KEY_NAV_POWER,
	TRAN_KEY_NAV_MENU,
	TRAN_KEY_NAV_BACK,
	TRAN_KEY_NAV_CAMERA,
	TRAN_KEY_NAV_FINGER_UP,
	TRAN_KEY_NAV_FINGER_DOWN,
	TRAN_KEY_NAV_UP,
	TRAN_KEY_NAV_DOWN,
	TRAN_KEY_NAV_LEFT,
	TRAN_KEY_NAV_RIGHT,
	TRAN_KEY_NAV_CLICK,
	TRAN_KEY_NAV_HEAVY,
	TRAN_KEY_NAV_LONG_PRESS,
	TRAN_KEY_NAV_DOUBLE_CLICK,
	TRAN_KEY_NAV_F28,
	TRAN_KEY_NAV_ENTER,
	TRAN_KEY_NAV_WAKEUP,
	TRAN_KEY_NAV_PAGEUP,
	TRAN_KEY_NAV_PAGEDOWN
} tran_key_nav_event_t;

typedef struct tran_key_nav {
	enum tran_key_nav_event key;
	uint32_t value;   /* key down = 1, key up = 0 */
} tran_key_nav_t;

  #if IS_ENABLED(CONFIG_TRAN_FOLD_DISPLAY)
  enum tran_netlink_cmd {
		TRAN_NETLINK_TEST = 0,
		TRAN_NETLINK_IRQ = 1,
		TRAN_NETLINK_SCREEN_OFF,
		TRAN_NETLINK_SCREEN_ON,
      TRAN_NETLINK_LCD1_SCREEN_ON,
      TRAN_NETLINK_LCD1_SCREEN_OFF,
      TRAN_NETLINK_FP_ID_PASS = 6,
      TRAN_NETLINK_FP_ID_FAILED = 7
  };
  #else
  enum tran_netlink_cmd {
		TRAN_NETLINK_TEST = 0,
		TRAN_NETLINK_IRQ = 1,
		TRAN_NETLINK_SCREEN_OFF,
		TRAN_NETLINK_SCREEN_ON,
      TRAN_NETLINK_FP_ID_PASS = 6,
      TRAN_NETLINK_FP_ID_FAILED = 7
  };
  #endif
typedef union pwr_val {
	struct {
		int vdd_val;
		int vio_val;
	} rgltor_val;

	int gpio_val;
} PWR_VAL_U;

#ifdef TRAN_FP_LOG_LVL_VERBOSE
#define TRAN_FP_DEBUG(fmt, args...) printk(KERN_INFO "[Tran_FP][Debug][%s][%d]"fmt"\n", __func__, __LINE__, ##args)
#define TRAN_FP_INFO(fmt, args...) printk(KERN_INFO "[Tran_FP][Info][%s][%d]"fmt"\n", __func__, __LINE__, ##args)
#elif defined(TRAN_FP_LOG_LVL_DEBUG)
#define TRAN_FP_DEBUG(fmt, args...) printk(KERN_INFO "[Tran_FP][Debug][%s][%d]"fmt"\n", __func__, __LINE__, ##args)
#define TRAN_FP_INFO(fmt, args...) printk(KERN_INFO "[Tran_FP][Info][%s][%d]"fmt"\n", __func__, __LINE__, ##args)
#elif defined(TRAN_FP_LOG_LVL_INFO)
#define TRAN_FP_DEBUG(fmt, args...)
#define TRAN_FP_INFO(fmt, args...) printk(KERN_INFO "[Tran_FP][Info][%s][%d]"fmt"\n", __func__, __LINE__, ##args)
#else
#define TRAN_FP_DEBUG(fmt, args...)
#define TRAN_FP_INFO(fmt, args...)
#endif
#define TRAN_FP_ERROR(fmt, args...) printk(KERN_ERR "[Tran_FP][Error][%s][%d]"fmt"\n", __func__, __LINE__, ##args)

#define TRAN_FUNC_ENTRY()  TRAN_FP_DEBUG("%s, %d, enter\n", __func__, __LINE__)
#define TRAN_FUNC_EXIT()  TRAN_FP_DEBUG("%s, %d, exit\n", __func__, __LINE__)
#endif
