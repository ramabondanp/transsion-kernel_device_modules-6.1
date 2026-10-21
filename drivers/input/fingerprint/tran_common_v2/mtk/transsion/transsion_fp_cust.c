#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/fb.h>
#include <linux/interrupt.h>
//#include <teei_fp.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#endif

#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif

#ifdef CONFIG_MTK_CLKMGR
#include "mach/mt_clkmgr.h"
#else
#include <linux/clk.h>
#endif

#include <net/sock.h>
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>

#define CONFIG_SPI_MT65XX_KERNEL510 y

/* MTK header */
#ifndef CONFIG_SPI_MT65XX_KERNEL510
#include "mt_spi.h"
#include "mt_spi_hal.h"
#else
#include "tran_spi.h"
#endif

/* there is no this file on standardized GPIO platform */
#ifdef CONFIG_MTK_GPIO
#include "mtk_gpio.h"
#include "mach/gpio_const.h"
#endif

//#include <mt-plat/sync_write.h>
#include <linux/of_address.h>

#ifdef CONFIG_TRAN_FP_DRV_MODE_SPI
#ifdef CONFIG_SPI_MT65XX_KERNEL510
#include <linux/platform_data/spi-mt65xx.h>
#endif
#endif

#include  <linux/regulator/consumer.h>


#include "transsion_fp_cust.h"
#include "mtk_disp_notify.h"


extern struct tran_fp_data * g_tran_fp_datap;

extern void tran_netlink_send(struct tran_fp_data *tran_fp_datap, const int cmd);
extern void tran_netlink_bio_send(struct tran_fp_data *tran_fp_datap, const int cmd);
extern void mt_spi_enable_master_clk(struct spi_device *spidev);







irqreturn_t tran_irq(int irq, void *handle)
{
	struct tran_fp_data *tran_dev = (struct tran_fp_data *)handle;
#ifdef FINGERPRINT_INTERRUPT_LOG
	TRAN_FUNC_ENTRY();
#endif

#ifdef CONFIG_PM_SLEEP
    __pm_wakeup_event(g_tran_fp_datap->tran_wakelock, msecs_to_jiffies(1500));
#else
    wake_lock_timeout(&g_tran_fp_datap->tran_wakelock, msecs_to_jiffies(1500));
#endif

	tran_netlink_send(tran_dev, TRAN_NETLINK_IRQ);
	tran_dev->sig_count++;

#ifdef FINGERPRINT_INTERRUPT_LOG
	TRAN_FUNC_EXIT();
#endif
	return IRQ_HANDLED;
}
#if IS_ENABLED(CONFIG_TRAN_HBM_NOTIFY_FINGERPRINT) || IS_ENABLED(CONFIG_TRAN_LHBM_NOTIFY_FINGERPRINT)
static void tran_fasync_for_lcm_full_HBM(void)
{
	struct tran_fp_data *tran_fp_datap = g_tran_fp_datap;
	char* env_ext[2] = {"TRAN_FULL_HBM_EVENT=FULL_HBM_SET", NULL};
	if(tran_fp_datap->tran_fp_dev!=NULL){
		TRAN_FP_INFO("lcm start setting HBM and prepare to notify fp HAL by uevent");
		kobject_uevent_env(&tran_fp_datap->tran_fp_dev->dev.kobj, KOBJ_CHANGE, env_ext);
	}else{
		TRAN_FP_ERROR("tran_fp_datap->tran_fp_dev is null");
	}
	//kill_fasync(&tran_fp_datap->async_queue, sig, band);
}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
 void tran_early_suspend(struct early_suspend *handler)
{
	struct tran_fp_data *tran_dev = NULL;

	tran_dev = container_of(handler, struct tran_fp_data, early_suspend);
	TRAN_FP_DEBUG("enter\n");

	tran_netlink_send(tran_dev, TRAN_NETLINK_SCREEN_OFF);
}

 void tran_late_resume(struct early_suspend *handler)
{
	struct tran_fp_data *tran_dev = NULL;

	tran_dev = container_of(handler, struct tran_fp_data, early_suspend);
	TRAN_FP_DEBUG("enter\n");

	tran_netlink_send(tran_dev, TRAN_NETLINK_SCREEN_ON);
    if (g_tran_fp_datap->history_support) {
	    tran_netlink_bio_send(tran_dev, TRAN_NETLINK_SCREEN_ON);
    }
}
#else
int tran_fb_notifier_callback(struct notifier_block *self,unsigned long event, void *data)
{
	struct tran_fp_data *tran_dev = NULL;
	//struct fb_event *evdata = data;
	//unsigned int blank;
	int retval = 0;
	int *blank=(int *)data;
	TRAN_FUNC_ENTRY();

	/* If we aren't interested in this event, skip it immediately ... */
	if (event != MTK_DISP_EVENT_BLANK /* FB_EARLY_EVENT_BLANK */)
		return 0;

	tran_dev = container_of(self, struct tran_fp_data, notifier);
	//blank = *(int *)evdata->data;

	TRAN_FP_DEBUG(" enter, blank=0x%x\n", *blank);
	switch (*blank) {
#if IS_ENABLED(CONFIG_TRAN_HBM_NOTIFY_FINGERPRINT) || IS_ENABLED(CONFIG_TRAN_LHBM_NOTIFY_FINGERPRINT)
		case TRAN_DISP_BLANK_NOTIFY_FINGERPRINT:
		TRAN_FP_INFO(" TRAN_DISP_BLANK_NOTIFY_FINGERPRINT\n");
		tran_fasync_for_lcm_full_HBM();
		break;
#endif
	case MTK_DISP_BLANK_UNBLANK:
		TRAN_FP_INFO(" lcd on notify\n");
		tran_netlink_send(tran_dev, TRAN_NETLINK_SCREEN_ON);
        if (g_tran_fp_datap->history_support) {
            tran_netlink_bio_send(tran_dev, TRAN_NETLINK_SCREEN_ON);
        }
		break;

	case MTK_DISP_BLANK_POWERDOWN:
		TRAN_FP_INFO(" lcd off notify\n");
		tran_netlink_send(tran_dev, TRAN_NETLINK_SCREEN_OFF);
		break;
#if IS_ENABLED(CONFIG_TRAN_FOLD_DISPLAY)
	case TRAN_DISP_BLANK_UNBLANK_DSI1:
		TRAN_FP_INFO("lcd_1 on notify\n");
		tran_netlink_send(tran_dev, TRAN_NETLINK_LCD1_SCREEN_ON);
        if (g_tran_fp_datap->history_support) {
            tran_netlink_bio_send(tran_dev, TRAN_NETLINK_SCREEN_ON);
        }
		break;
	case TRAN_DISP_BLANK_POWERDOWN_DSI1:
		TRAN_FP_INFO("lcd_1 off notify\n");
		tran_netlink_send(tran_dev, TRAN_NETLINK_LCD1_SCREEN_OFF);
		break;
#endif

	default:
		TRAN_FP_INFO(" other notifier, ignore\n");
		break;
	}
	TRAN_FUNC_EXIT();
	return retval;
}
#endif //CONFIG_HAS_EARLYSUSPEND





int tran_key_nav_adjust_type_value(tran_key_nav_event_t *key_nav_p, unsigned int cmd)
{
	switch(cmd)
	{
	    case TRAN_IOC_NAV_EVENT:
		switch(*key_nav_p)
		{
		case TRAN_KEY_NAV_HOME:
			*key_nav_p = TRAN_KEY_NAV_FINGER_UP;
			break;
		case TRAN_KEY_NAV_POWER:
			*key_nav_p = TRAN_KEY_NAV_FINGER_DOWN;
			break;
		case TRAN_KEY_NAV_MENU:
			*key_nav_p = TRAN_KEY_NAV_UP;
			break;
		case TRAN_KEY_NAV_BACK:
			*key_nav_p = TRAN_KEY_NAV_DOWN;
			break;
		case TRAN_KEY_NAV_CAMERA:
			*key_nav_p = TRAN_KEY_NAV_LEFT;
			break;
		case TRAN_KEY_NAV_FINGER_UP:
			*key_nav_p = TRAN_KEY_NAV_RIGHT;
			break;
		case TRAN_KEY_NAV_FINGER_DOWN:
			*key_nav_p = TRAN_KEY_NAV_CLICK;
			break;
		case TRAN_KEY_NAV_UP:
			*key_nav_p = TRAN_KEY_NAV_HEAVY;
			break;
		case TRAN_KEY_NAV_DOWN:
			*key_nav_p = TRAN_KEY_NAV_LONG_PRESS;
			break;
		case TRAN_KEY_NAV_LEFT:
			*key_nav_p = TRAN_KEY_NAV_DOUBLE_CLICK;
			break;
		default:
			break;
		}
		break;
	    case TRAN_IOC_INPUT_KEY_EVENT:
		switch(*key_nav_p)
		{
		case TRAN_KEY_NAV_HOME:
			*key_nav_p = TRAN_KEY_NAV_F28;
			break;
		default:
			break;
		}
		break;
	    default:
		break;
	}
	return 0;
}
