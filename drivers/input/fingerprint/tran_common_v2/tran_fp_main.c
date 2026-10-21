/**
 * Copyright (C) 2015 Transsion Inc
 * This file is used to implement the main or fundamental functions for fingerprint device driver
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/compat.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <linux/of_irq.h>
#include <linux/kobject.h>
#include <linux/debugfs.h>
#include <../kernel/power/power.h>
#include <linux/jiffies.h>
#ifdef CONFIG_PM_SLEEP
#include <linux/pm_wakeup.h>
#else
#include <linux/wakelock.h>
#endif
#include <asm/uaccess.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/cdev.h>
#include <linux/clk.h>
#include <linux/cpumask.h>
#include <linux/kernel_stat.h>
#include <linux/bug.h>
#include <linux/param.h>
#include <linux/ioport.h>
#include <linux/irqreturn.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/cpu.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

#define CONFIG_SPI_MT65XX_KERNEL510 y

#if !defined(CONFIG_SPI_MT65XX_KERNEL510)
#include <mt_spi.h>
#include <mt_spi_hal.h>
#endif

#include "tran_fp_main.h"
#include "mc_linux_api.h"

#include <linux/timex.h>
#include <linux/timer.h>
#include <linux/fb.h>
#include "transsion_fp_cust.h"


#define N_SPI_MINORS 32
#define TRAN_FP_STATIC_MINOR 228
#define PROC_VND_ID_LEN 32
char android_version = 0;
struct tran_fp_data	* g_tran_fp_datap;
int g_tran_fp_req_irq_times = 0;


extern struct attribute_group tran_fp_attribute_group;
u8 g_fp_vendor_id;

//extern void app_get_fingerprint_name(char *name);
extern int tran_proc_init(struct tran_fp_data *tran_fp_datap);
extern int tran_proc_deinit(struct tran_fp_data *tran_fp_dev);

extern void tran_netlink_send(struct tran_fp_data *tran_fp_datap, const int cmd);
extern char vendor_name[PROC_VND_ID_LEN];

//extern void set_tee_worker_threads_on_big_core(bool big_core , int afinity_mask);
//extern cpumask_t tee_set_affinity(void);
static unsigned int bufsiz = 64; //(25 * 1024);


static DECLARE_BITMAP(minors, N_SPI_MINORS);
static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);
static DECLARE_WAIT_QUEUE_HEAD(tran_fp_poll_waitq);

static struct kobject *fp_sys_kobj;
module_param(bufsiz, uint, S_IRUGO);
MODULE_PARM_DESC(bufsiz, "data bytes in biggest supported SPI message");

static long tran_fp_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int	err = 0;
	int	retval = 0;
	struct tran_fp_data	*tran_fp_datap = NULL;
	struct spi_device *tran_fp_dev = NULL;
	PWR_VAL_U power_val;
	struct tran_ioc_chip_info info;
	tran_key_nav_event_t key_nav_event = TRAN_KEY_NAV_NONE;
	tran_key_nav_t tran_key_nav = {TRAN_KEY_NAV_NONE, 0};
	u8 netlink_route = TRAN_NETLINK_ROUTE;
	u8 need_update = 0;
	unsigned int val = 0;

	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
	if (err == 0 && _IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
	if (err){
		TRAN_FP_ERROR(" cannot access read! ======\n");
		return -EFAULT;
	}
	tran_fp_datap = filp->private_data;
	if (tran_fp_datap == NULL) {
		TRAN_FP_ERROR(" tran_fp_datap is NULL ! ======\n");
		return -EINVAL;
	}
	tran_fp_datap->cancel = 0;
	spin_lock_irq(&tran_fp_datap->spi_lock);
#ifdef CONFIG_TRAN_FP_DRV_MODE_SPI
	tran_fp_dev = spi_dev_get(tran_fp_datap->tran_fp_dev);
#else
	tran_fp_dev = (tran_fp_datap->tran_fp_dev && get_device(&tran_fp_datap->tran_fp_dev->dev)) ? tran_fp_datap->tran_fp_dev : NULL;
#endif
	spin_unlock_irq(&tran_fp_datap->spi_lock);
	if (tran_fp_dev == NULL) {
		TRAN_FP_ERROR(" no tran_fp_dev! ======\n");
		return -ESHUTDOWN;
	}
	mutex_lock(&tran_fp_datap->buf_lock);
	switch (cmd) {
		case TRAN_IOC_INIT:
			TRAN_FP_INFO(" TRAN_IOC_INIT tran init======\n");

			if (copy_to_user((void __user *)arg, (void *)&netlink_route, sizeof(u8))) {
				retval = -EFAULT;
				break;
			}

			if (tran_fp_datap->system_status) {
				TRAN_FP_INFO(" system re-started======\n");
				break;
			}
			retval = request_threaded_irq(tran_fp_datap->irq, NULL, tran_irq,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT, "goodix_fp_irq", tran_fp_datap);
			if (!retval) {
				TRAN_FP_INFO(" irq thread request success!\n");
				g_tran_fp_req_irq_times = 1;
			}
			else {
				TRAN_FP_ERROR(" irq thread request failed, retval=%d\n", retval);
				break;
			}
			retval = enable_irq_wake(tran_fp_datap->irq);
			if (retval < 0 ) {
				TRAN_FP_ERROR(" enable_irq_wake failed , retval = %d \n", retval);
				free_irq(tran_fp_datap->irq, tran_fp_datap);
				break;
			} else {
				TRAN_FP_INFO(" enable_irq_wake successful======\n");
				tran_fp_datap->irq_count = 1;
			}
			tran_fp_datap->disable_irq(tran_fp_datap);
			tran_fp_datap->sig_count = 0;
			tran_fp_datap->system_status = 1;
			TRAN_FP_INFO(" tran init finished======\n");
			break;

		case TRAN_IOC_CHIP_INFO:
			TRAN_FP_INFO(" TRAN_IOC_CHIP_INFO ======\n");
			if (copy_from_user(&info, (struct tran_ioc_chip_info *)arg, sizeof(struct tran_ioc_chip_info))) {
				retval = -EFAULT;
				break;
			}
			g_fp_vendor_id = info.vendor_id;
			strcpy(g_tran_fp_datap->ic_name,info.icname);
			setVendorName();
			TRAN_FP_INFO(" vendor_id 0x%x\n", g_fp_vendor_id);
			TRAN_FP_INFO(" mode 0x%x\n", info.mode);
			TRAN_FP_INFO(" operation 0x%x\n", info.operation);
			TRAN_FP_INFO(" icname 0x%s\n", info.icname);
			break;

		case TRAN_IOC_EXIT:
			TRAN_FP_INFO(" TRAN_IOC_EXIT ======\n");
			tran_fp_datap->disable_irq(tran_fp_datap);
			if (tran_fp_datap->irq && g_tran_fp_req_irq_times == 1) {
				free_irq(tran_fp_datap->irq, tran_fp_datap);
				g_tran_fp_req_irq_times = 0;
				tran_fp_datap->irq_count = 0;
			}
			tran_fp_datap->system_status = 0;
			TRAN_FP_INFO(" tran exit finished ======\n");
			break;

		case TRAN_IOC_RESET:
			TRAN_FP_INFO(" tran chip reset command\n");
			tran_fp_datap->reset(tran_fp_datap, tran_fp_datap->inter_delay, tran_fp_datap->post_delay);
			break;

		case TRAN_IOC_ENABLE_IRQ:
			TRAN_FP_INFO(" TRAN_IOC_ENABLE_IRQ ======\n");
			tran_fp_datap->enable_irq(tran_fp_datap);
			break;

		case TRAN_IOC_DISABLE_IRQ:
			TRAN_FP_INFO(" TRAN_IOC_DISABLE_IRQ ======\n");
			tran_fp_datap->disable_irq(tran_fp_datap);
			break;

		case TRAN_IOC_ENABLE_SPI_CLK:
			TRAN_FP_INFO(" TRAN_IOC_ENABLE_SPI_CLK ======\n");
			tran_fp_datap->spi_clk_on(tran_fp_datap);
			break;

		case TRAN_IOC_DISABLE_SPI_CLK:
			TRAN_FP_INFO(" TRAN_IOC_DISABLE_SPI_CLK ======\n");
			tran_fp_datap->spi_clk_off(tran_fp_datap);
			break;

		case TRAN_IOC_ENABLE_POWER:
			TRAN_FP_INFO(" TRAN_IOC_ENABLE_POWER ======\n");
			tran_fp_datap->power_init(tran_fp_datap, true);
			tran_fp_datap->power_onoff(tran_fp_datap, true);
			tran_fp_get_power_val(tran_fp_datap, &power_val);
			tran_fp_datap->reset(tran_fp_datap, tran_fp_datap->inter_delay, tran_fp_datap->post_delay);
			break;

		case TRAN_IOC_DISABLE_POWER:
			TRAN_FP_INFO(" TRAN_IOC_DISABLE_POWER ======\n");
			tran_fp_datap->power_onoff(tran_fp_datap, false);
			tran_fp_get_power_val(tran_fp_datap, &power_val);
			tran_fp_datap->power_init(tran_fp_datap, false);
			break;

		case TRAN_IOC_INPUT_KEY_EVENT:
			TRAN_FP_INFO(" TRAN_IOC_INPUT_KEY_EVENT ======\n");
			if (copy_from_user(&tran_key_nav, (tran_key_nav_t *)arg, sizeof(tran_key_nav_t))) {
				TRAN_FP_ERROR("Failed to copy input key event from user to kernel\n");
				retval = -EFAULT;
				break;
			}
			if ((TRAN_KEY_NAV_POWER == tran_key_nav.key || TRAN_KEY_NAV_CAMERA == tran_key_nav.key) && (tran_key_nav.value == 1)) {
				tran_fp_report_key_nav_event(tran_fp_datap->input, &tran_key_nav);
				tran_key_nav.value = 0;
				tran_fp_report_key_nav_event(tran_fp_datap->input, &tran_key_nav);
				tran_key_nav.value = 1;
			}
			if (TRAN_KEY_NAV_HOME == tran_key_nav.key) {
				tran_key_nav_adjust_type_value(&(tran_key_nav.key), TRAN_IOC_INPUT_KEY_EVENT);
				tran_fp_report_key_nav_event(tran_fp_datap->input, &tran_key_nav);
			}
			break;

		case TRAN_IOC_NAV_EVENT:
			TRAN_FP_INFO(" TRAN_IOC_NAV_EVENT ======\n");
			if (copy_from_user(&key_nav_event, (tran_key_nav_event_t *)arg, sizeof(tran_key_nav_event_t))) {
				TRAN_FP_ERROR("Failed to copy nav event from user to kernel\n");
				retval = -EFAULT;
				break;
			}
			tran_key_nav_adjust_type_value(&key_nav_event, TRAN_IOC_NAV_EVENT);
			tran_key_nav.key = key_nav_event;
			tran_key_nav.value = 1;
			tran_fp_report_key_nav_event(tran_fp_datap->input, &tran_key_nav);
			tran_key_nav.value = 0;
			tran_fp_report_key_nav_event(tran_fp_datap->input, &tran_key_nav);
			break;

		case TRAN_IOC_ENTER_SLEEP_MODE:
			TRAN_FP_INFO(" TRAN_IOC_ENTER_SLEEP_MODE ======\n");
			break;

		case TRAN_IOC_GET_FW_INFO:
			TRAN_FP_INFO(" TRAN_IOC_GET_FW_INFO ======\n");
			need_update = tran_fp_datap->need_update;
			TRAN_FP_DEBUG(" firmware info  0x%x\n", need_update);
			if (copy_to_user((void __user *)arg, (void *)&need_update, sizeof(u8))) {
				TRAN_FP_ERROR("Failed to copy data to user\n");
				retval = -EFAULT;
			}
			break;
		case TRAN_IOC_REMOVE:
			TRAN_FP_INFO(" TRAN_IOC_REMOVE ======\n");
			tran_netlink_destroy(tran_fp_datap);
			tran_netlink_bio_destroy(tran_fp_datap);
			mutex_lock(&tran_fp_datap->release_lock);
			if (tran_fp_datap->input == NULL) {
				mutex_unlock(&tran_fp_datap->release_lock);
				break;
			}
			input_unregister_device(tran_fp_datap->input);
			tran_fp_datap->input = NULL;
			mutex_unlock(&tran_fp_datap->release_lock);
			sysfs_remove_group(&tran_fp_datap->tran_fp_dev->dev.kobj, &tran_fp_attribute_group);
			misc_deregister(&tran_fp_datap->miscdev);
			list_del(&tran_fp_datap->device_entry);
			tran_fp_datap->power_onoff(tran_fp_datap, false);
			tran_fp_get_power_val(tran_fp_datap, &power_val);
			tran_fp_datap->power_init(tran_fp_datap, false);
			tran_fp_datap->spi_clk_off(tran_fp_datap);
			mutex_lock(&tran_fp_datap->release_lock);
			if (tran_fp_datap->tran_buffer != NULL) {
				kfree(tran_fp_datap->tran_buffer);
				tran_fp_datap->tran_buffer = NULL;
			}
			mutex_unlock(&tran_fp_datap->release_lock);
			spi_set_drvdata(tran_fp_datap->tran_fp_dev, NULL);
			tran_fp_datap->tran_fp_dev = NULL;
			mutex_destroy(&tran_fp_datap->buf_lock);
			mutex_destroy(&tran_fp_datap->release_lock);
			TRAN_FP_INFO(" TRAN_IOC_REMOVE leave======\n");
			break;


		case TRAN_FP_TEE_AFINITY:
//			if (copy_from_user(&val, (int __user *)arg, sizeof(val)))
//				TRAN_FP_INFO(" TRAN_FP_TEE_AFINITY val = %x \n",val);
//			if(val) {
//				set_tee_worker_threads_on_big_core(true,tran_fp_datap->tran_tee_afinity);
//				tee_set_affinity();
//				TRAN_FP_ERROR("set_tee_worker_threads_on_big_core true \n");
//			} else {
//				set_tee_worker_threads_on_big_core(false,0x00);
//				tee_set_affinity();
//				TRAN_FP_ERROR("set_tee_worker_threads_on_big_core false \n");
//			}
			break;
		case TRAN_FP_OPENHAL_STATUS:
			if (copy_from_user(&val, (int __user *)arg, sizeof(val)))
				TRAN_FP_INFO(" TRAN_FP_OPENHAL_FLAG = %d \n",val);
			if(val)
			{
				tran_fp_datap->tran_openHal_flag = 1;
				TRAN_FP_INFO("Write openHal_flag\n");
			} else {
				if (copy_to_user((int __user *)arg, (int *)&tran_fp_datap->tran_openHal_flag, sizeof(int))) {
					TRAN_FP_ERROR("Failed to copy data to user\n");
					retval = -EFAULT;
					break;
				}
				TRAN_FP_INFO("Read  openHal_flag\n");
			}
			break;

		case TRAN_FP_OPT_SUPPORT:
			tran_fp_datap->tran_opt_fp_support = 1;
			TRAN_FP_INFO(" opt fingerprint support");
			break;
        case TRAN_FP_HISTORY_SUPPORT:
            if (copy_to_user((bool __user *)arg, (bool *)&tran_fp_datap->history_support, sizeof(bool))) {
                TRAN_FP_ERROR("Failed to copy history support to user\n");
            }
            break;
		default:
			TRAN_FP_ERROR("tran doesn't support this command(%#08x), _IOC_TYPE(cmd) = %c, _IOC_NR(cmd) = %#08x", cmd, _IOC_TYPE(cmd), _IOC_NR(cmd));
			break;
	}
	mutex_unlock(&tran_fp_datap->buf_lock);
	spi_dev_put(tran_fp_dev);
	return retval;
}

#ifdef CONFIG_COMPAT
static long tran_fp_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	return tran_fp_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#else
#define tran_fp_compat_ioctl NULL
#endif /* CONFIG_COMPAT */

static unsigned int tran_fp_poll( struct file *filp, struct poll_table_struct *wait)
{
	unsigned int ret = 0;
	poll_wait(filp, &g_tran_fp_datap->wq_irq_return, wait);
	if (g_tran_fp_datap->cancel == 1) {
		TRAN_FP_INFO(" chipone cancle\n");
		ret =  POLLERR;
		g_tran_fp_datap->cancel = 0;
		return ret;
	} else if ( g_tran_fp_datap->RcvIRQ) {
		if (g_tran_fp_datap->RcvIRQ == 2) {
			TRAN_FP_INFO("chipone get fp on notify\n");
			ret |= POLLIN;
			ret |= POLLHUP;
		} else {
			TRAN_FP_INFO("chipone get irq\n");
			ret |= POLLIN;
			ret |= POLLRDNORM;
		}
	}  else {
		ret = 0;
	}
	return ret;
}

static ssize_t vendor_name_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t vendor_name_store(struct kobject *kobj,struct kobj_attribute *attr,const char *buf, size_t n);
static ssize_t ic_name_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t ic_name_store(struct kobject *kobj,struct kobj_attribute *attr,const char *buf, size_t n);
static ssize_t fod_location_xy_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t fod_location_xy_store(struct kobject *kobj,struct kobj_attribute *attr,const char *buf, size_t n);

#define tran_attr(_name) \
	static struct kobj_attribute _name##_attr = {	\
		.attr	= {				\
			.name = __stringify(_name),	\
			.mode = 0664,			\
		},					\
		.show	= _name##_show,			\
		.store	= _name##_store,		\
	}

tran_attr(vendor_name);
tran_attr(ic_name);
tran_attr(fod_location_xy);


static struct attribute * g[] = {
	&vendor_name_attr.attr,
	&ic_name_attr.attr,
	&fod_location_xy_attr.attr,
	NULL,
};

static struct attribute_group vendor_attr_group = {
	.attrs = g,
};

static DEFINE_MUTEX(vendor_name_lock);
static DEFINE_MUTEX(ic_name_lock);

static ssize_t vendor_name_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	char *str = buf;

	mutex_lock(&vendor_name_lock);
	str += sprintf(str, "%s\n", g_tran_fp_datap->vendor_name);
	//    *str++ = '\n';
	mutex_unlock(&vendor_name_lock);
	return (str - buf);
}

static ssize_t vendor_name_store(struct kobject *kobj,struct kobj_attribute *attr,const char *buf, size_t n)
{
	mutex_lock(&vendor_name_lock);
	TRAN_FP_INFO("[%s]has not been implemented.\n", __func__);
	mutex_unlock(&vendor_name_lock);
	return n;
}
static ssize_t ic_name_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	char *str = buf;
	mutex_lock(&ic_name_lock);
	str += sprintf(str, "%s\n", g_tran_fp_datap->ic_name);
	//    *str++ = '\n';
	mutex_unlock(&ic_name_lock);
	return (str - buf);
}
static ssize_t ic_name_store(struct kobject *kobj,struct kobj_attribute *attr,const char *buf, size_t n)
{
	mutex_lock(&ic_name_lock);
	TRAN_FP_INFO("[%s]has not been implemented.\n", __func__);
	mutex_unlock(&ic_name_lock);
	return n;
}

static ssize_t fod_location_xy_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	char *str = buf;
	mutex_lock(&ic_name_lock);
	str += sprintf(str, "%s", g_tran_fp_datap->fod_location_xy);
	mutex_unlock(&ic_name_lock);
	return (str - buf);
}
static ssize_t fod_location_xy_store(struct kobject *kobj,struct kobj_attribute *attr,const char *buf, size_t n)
{
	mutex_lock(&ic_name_lock);
	TRAN_FP_INFO("[%s]has not been implemented.\n", __func__);
	mutex_unlock(&ic_name_lock);
	return n;
}


static int tran_fp_open(struct inode *inode, struct file *filp)
{
	struct tran_fp_data	*tran_fp_datap;
	int	status = -ENXIO;
	TRAN_FP_INFO("entered!");

	mutex_lock(&device_list_lock);
	list_for_each_entry(tran_fp_datap, &device_list, device_entry)
	{
		if (tran_fp_datap->devno == inode->i_rdev) {
			status = 0;
			break;
		}
	}
	if (status == 0) {
		if (!tran_fp_datap->tran_buffer) {
			tran_fp_datap->tran_buffer = kmalloc(bufsiz, GFP_KERNEL);
			if (!tran_fp_datap->tran_buffer) {
				TRAN_FP_ERROR("open no memory\n");
				status = -ENOMEM;
			}
		}
		if (status == 0) {
			tran_fp_datap->users++;
			tran_fp_datap->device_available = 1;
			filp->private_data = tran_fp_datap;
			nonseekable_open(inode, filp);
		}

	} else
		TRAN_FP_INFO("tran_fp_datap: nothing for minor %d\n", iminor(inode));
	mutex_unlock(&device_list_lock);
	return status;
}

static int tran_fp_release(struct inode *inode, struct file *filp)
{
	struct tran_fp_data	*tran_fp_datap;
	int			status = 0;

	TRAN_FP_INFO("entered!");
	mutex_lock(&device_list_lock);
	tran_fp_datap = filp->private_data;
	filp->private_data = NULL;
	//tran_fp_datap->spi_clk_off(tran_fp_datap);

	/* last close? */
	tran_fp_datap->users--;
	if (tran_fp_datap->users <= 0) {
		tran_fp_datap->disable_irq(tran_fp_datap);
		if (tran_fp_datap->tran_buffer) {
			kfree(tran_fp_datap->tran_buffer);
			tran_fp_datap->tran_buffer = NULL;
		}
		/* ... after we unbound from the underlying device? */
	}
	tran_fp_datap->device_available = 0;
	mutex_unlock(&device_list_lock);
	return status;
}

static const struct file_operations tran_fp_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = tran_fp_ioctl,
	.compat_ioctl = tran_fp_compat_ioctl,
	.open = tran_fp_open,
	.release = tran_fp_release,
	.poll = tran_fp_poll,
};


static int  tran_fp_probe(struct spi_device *tran_fp_dev)
{
	struct tran_fp_data	*tran_fp_datap;
	int	status = 0;
	// unsigned char *chip_id_str = NULL;
	PWR_VAL_U power_val;


	TRAN_FP_INFO("enter");
	tran_fp_datap = kzalloc(sizeof(*tran_fp_datap), GFP_KERNEL);
	if (!tran_fp_datap) {
		TRAN_FP_ERROR("no enough mem for tran_fp_datap!");
		status = -ENOMEM;
		goto tran_kzalloc_err;
	}

	g_tran_fp_datap = tran_fp_datap;
	tran_fp_datap->tran_fp_dev = tran_fp_dev;
	tran_fp_datap->miscdev.minor = TRAN_FP_STATIC_MINOR;
	tran_fp_datap->miscdev.name = "tran_fp";
	tran_fp_datap->miscdev.fops = &tran_fp_fops;
	tran_fp_datap->inter_delay = 10000;
	tran_fp_datap->post_delay = 10000;

	if (tran_fp_datap->tran_buffer == NULL) {
		tran_fp_datap->tran_buffer = kzalloc(bufsiz, GFP_KERNEL);
		if (tran_fp_datap->tran_buffer == NULL) {
			status = -ENOMEM;
			goto err_buf;
		}
	}
	status = tran_fp_parse_dts_file(tran_fp_datap);
	if (status < 0) {
		TRAN_FP_ERROR(" tran_fp_parse_dts_file failed");
		goto dts_parse_err;
	}

	tran_register_base_ops(tran_fp_datap);
	status = tran_fp_datap->resource_init(tran_fp_datap);
	if (status < 0) {
		TRAN_FP_ERROR("resource_init failed! status = %d", status);
		goto resource_init_err;
	}
	if(tran_fp_datap->fp_regulator_support) {
		status = tran_fp_datap->power_init(tran_fp_datap, true);
		if (status < 0) {
			TRAN_FP_ERROR("power init failed! err=%d", status);
			goto tran_pwr_init_err;
		}
		tran_fp_reset_gpio_low(tran_fp_datap);
		mdelay(10);
		status = tran_fp_datap->power_onoff(tran_fp_datap, true);
		if (status < 0) {
			TRAN_FP_ERROR("power on failed! err=%d\n", status);
			goto tran_pwr_onoff_err;
		}
		mdelay(10);
		status = tran_fp_get_power_val(tran_fp_datap, &power_val);
	}
	if(IS_ERR(tran_fp_datap->pins_spi_default)){
		TRAN_FP_ERROR("pinctrl_lookup_state spi_default failed!");
	} else {
		status = pinctrl_select_state(tran_fp_datap->tran_pinctrl, tran_fp_datap->pins_spi_default);
		if(status) {
			TRAN_FP_ERROR("[tran]cannot set spi default state, ret = %d \n", status);
		} else
			TRAN_FP_INFO("[tran] set spi default state success!");
	}
	tran_fp_datap->spi_clk_on(tran_fp_datap);

	//tran_fp_datap->reset(tran_fp_datap, TRAN_INIT_INTER_CHIP_RESET_DELAY, TRAN_INIT_POST_CHIP_RESET_DELAY);
	fp_sys_kobj = kobject_create_and_add("tran_fp", kernel_kobj);
	if (!fp_sys_kobj) {
		TRAN_FP_ERROR("%s, Failed to create /sys/kernel/tran_fp dir\n", __func__);
		goto sys_dir_tran_fp_create_err;
	}
			TRAN_FP_INFO("[tran] kobject_create_and_add");
	status = sysfs_create_group(fp_sys_kobj, &vendor_attr_group);
	if (status) {
		TRAN_FP_ERROR("%s, Failed to create /sys/kernel/tran_fp/vendor_name node\n", __func__);
		goto sys_fp_node_create_err;
	}
			TRAN_FP_INFO("[tran] sysfs_create_group");
	status = misc_register(&tran_fp_datap->miscdev);
	if (status < 0) {
		TRAN_FP_ERROR(" misc_register failed");
		goto misc_register_err;
	}
	if (status == 0) {
		mutex_lock(&device_list_lock);
		tran_fp_datap->devno = MKDEV(MISC_MAJOR, tran_fp_datap->miscdev.minor);
		list_add(&tran_fp_datap->device_entry, &device_list);
		mutex_unlock(&device_list_lock);
	}

	status = tran_proc_init(tran_fp_datap);
	if (status < 0) {
		TRAN_FP_ERROR(" tran_proc_init failed");
		goto proc_init_err;
	}

	status = tran_fp_input_init();
	if (status < 0) {
		TRAN_FP_ERROR(" tran_fp_input_init failed");
		goto input_init_err;
	}

//	status = tran_fp_irq_init();
//	if (status < 0) {
//		TRAN_FP_ERROR(" tran_fp_irq_init failed");
//		goto irq_init_err;
//	}

	status = tran_netlink_init(tran_fp_datap);
	if (status < 0) {
		TRAN_FP_ERROR(" tran_netlink_init failed");
		goto netlink_init_err;
	}
	status = tran_netlink_bio_init(tran_fp_datap);
	if (status < 0) {
		TRAN_FP_ERROR(" tran_netlink_bio_init failed");
		goto netlink_init_err;
	}
#ifdef CONFIG_HAS_EARLYSUSPEND
	register_early_suspend(&tran_fp_datap->early_suspend);
#else
	//fb_register_client(&tran_fp_datap->notifier);
	mtk_disp_notifier_register("FINGERPRINT", &tran_fp_datap->notifier);
#endif
	//app_get_fingerprint_name(tran_fp_datap->modalias);
	tran_fp_datap->probe_finish = 1;
	tran_fp_datap->is_sleep_mode = 0;
	tran_fp_datap->device_available = 1;

	if (status == 0) {
		TRAN_FP_INFO("%s success! \n", __func__);
		spi_set_drvdata(tran_fp_dev, tran_fp_datap);
		tran_fp_datap->spi_clk_off(tran_fp_datap);
	} else {
		tran_fp_datap->shutdown(tran_fp_datap);
netlink_init_err:
		if (tran_fp_datap->irq && g_tran_fp_req_irq_times == 1) {
			free_irq(tran_fp_datap->irq,tran_fp_datap);
			tran_fp_datap->irq_count = 0;
			g_tran_fp_req_irq_times = 0;
		}
		input_unregister_device(tran_fp_datap->input);
		tran_fp_datap->input = NULL;
input_init_err:
		tran_proc_deinit(tran_fp_datap);
proc_init_err:
		mutex_lock(&device_list_lock);
		list_del(&tran_fp_datap->device_entry);
		mutex_unlock(&device_list_lock);
		misc_deregister(&tran_fp_datap->miscdev);

misc_register_err:
		sysfs_remove_group(fp_sys_kobj, &vendor_attr_group);
sys_fp_node_create_err:
		kobject_del(fp_sys_kobj);
sys_dir_tran_fp_create_err:
		tran_fp_datap->spi_clk_off(tran_fp_datap);
dts_parse_err:
		if (tran_fp_datap->tran_pinctrl) {
			devm_pinctrl_put(tran_fp_datap->tran_pinctrl);
			tran_fp_datap->tran_pinctrl = NULL;
		}
#ifndef CONFIG_TRAN_FP_DRV_MODE_SPI
		devm_clk_put(&tran_fp_datap->tran_fp_dev->dev, tran_fp_datap->clk);
#endif
		tran_fp_datap->power_onoff(tran_fp_datap, false);
tran_pwr_onoff_err:
		tran_fp_datap->power_init(tran_fp_datap, false);
		destroy_workqueue(tran_fp_datap->wqueue);
#ifdef CONFIG_PM_SLEEP
		wakeup_source_unregister(tran_fp_datap->tran_wakelock);
#else
		wake_lock_destroy(&tran_fp_datap->tran_wakelock);
#endif
		mutex_destroy(&tran_fp_datap->buf_lock);
		mutex_destroy(&tran_fp_datap->release_lock);

tran_pwr_init_err:
resource_init_err:
		kfree(tran_fp_datap->tran_buffer);
		tran_fp_datap->tran_buffer = NULL;
err_buf:
		//#ifdef CONFIG_TRAN_FP_DRV_MODE_SPI
		//spi_setup_fail:
		//#endif
		kfree(tran_fp_datap);
		tran_fp_datap = NULL;
	}
tran_kzalloc_err:
	return status;

}

static void tran_fp_remove(struct spi_device * spi)
{
	struct tran_fp_data  *tran_fp_datap = spi_get_drvdata(spi);
	PWR_VAL_U power_val;
	int status = 0;
	TRAN_FP_INFO(" entered!\n");
	/* make sure ops on existing fds can abort cleanly */
	tran_fp_datap->shutdown(tran_fp_datap);
	tran_netlink_destroy(tran_fp_datap);
	tran_netlink_bio_destroy(tran_fp_datap);
	if (tran_fp_datap->irq && g_tran_fp_req_irq_times == 1) {
		free_irq(tran_fp_datap->irq,tran_fp_datap);
		tran_fp_datap->irq_count = 0;
		g_tran_fp_req_irq_times = 0;
	}
	input_unregister_device(tran_fp_datap->input);
	tran_fp_datap->input = NULL;
	tran_proc_deinit(tran_fp_datap);
	sysfs_remove_group(&tran_fp_datap->miscdev.this_device->kobj, &tran_fp_attribute_group);
	misc_deregister(&tran_fp_datap->miscdev);
	sysfs_remove_group(fp_sys_kobj, &vendor_attr_group);
	kobject_del(fp_sys_kobj);
	devm_pinctrl_put(tran_fp_datap->tran_pinctrl);
	tran_fp_datap->tran_pinctrl = NULL;
#ifndef CONFIG_TRAN_FP_DRV_MODE_SPI
	devm_clk_put(&tran_fp_datap->tran_fp_dev->dev, tran_fp_datap->clk);
#endif
	spin_lock_irq(&tran_fp_datap->spi_lock);
	tran_fp_datap->tran_fp_dev = NULL;
	spi_set_drvdata(spi, NULL);
	spin_unlock_irq(&tran_fp_datap->spi_lock);
	status = tran_fp_datap->power_onoff(tran_fp_datap, false);
	if (status < 0)
		TRAN_FP_ERROR("power off failed! err=%d\n", status);
	status = tran_fp_get_power_val(tran_fp_datap, &power_val);
	status = tran_fp_datap->power_init(tran_fp_datap, false);
	if (status < 0)
		TRAN_FP_ERROR("power deinit failed! err=%d", status);
	/* prevent new opens */
	if (tran_fp_datap->wqueue) {
		destroy_workqueue(tran_fp_datap->wqueue);
		tran_fp_datap->wqueue = NULL;
	}
#ifdef CONFIG_HAS_EARLYSUSPEND
	if (tran_fp_datap->early_suspend.suspend) {
		unregister_early_suspend(&tran_fp_datap->early_suspend);
	}
#else
	mtk_disp_notifier_unregister(&tran_fp_datap->notifier);
	//fb_unregister_client(&tran_fp_datap->notifier);
#endif /* CONFIG_HAS_EARLYSUSPEND */
#ifdef CONFIG_PM_SLEEP
	wakeup_source_unregister(tran_fp_datap->tran_wakelock);
#else
	wake_lock_destroy(&tran_fp_datap->tran_wakelock);
#endif
	mutex_destroy(&tran_fp_datap->buf_lock);
	mutex_destroy(&tran_fp_datap->release_lock);
	mutex_lock(&device_list_lock);
	list_del(&tran_fp_datap->device_entry);
	clear_bit(MINOR(tran_fp_datap->devno), minors);
	mutex_unlock(&device_list_lock);
	//tran_fp_free_gpios(tran_fp_datap);

	kfree(tran_fp_datap->tran_buffer);
	tran_fp_datap->tran_buffer = NULL;
	kfree(tran_fp_datap);
	tran_fp_datap = NULL;
	return;
}

#ifdef CONFIG_PM_SLEEP
static int tran_fp_suspend(struct device *spi)
{
	//struct tran_fp_data *tran_fp_datap = dev_get_drvdata(spi);
	//enable_irq_wake(tran_fp_datap->irq);
	TRAN_FP_INFO(" entering ");
	return 0;
}

static int tran_fp_resume(struct device *spi)
{
	//struct tran_fp_data  *tran_fp_datap = dev_get_drvdata(spi);
	//disable_irq_wake(tran_fp_datap->irq);
	TRAN_FP_INFO(" entering ");
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(tran_fp_pm_ops, tran_fp_suspend, tran_fp_resume);

#ifdef CONFIG_OF
static const struct of_device_id tran_of_match[] = {
	{.compatible = "tran_fp",},
	{},
};
MODULE_DEVICE_TABLE(of, tran_of_match);
#endif

static const struct spi_device_id tran_id_table[] = {
	{.name = "tran_fp",},
	{},
};

static struct spi_driver tran_fp_driver = {
	.driver = {
		.name =		"tran_fp",
		.owner =	THIS_MODULE,
		.pm 	= &tran_fp_pm_ops,
#ifdef CONFIG_OF
		.of_match_table = tran_of_match,
#endif
	},
	.probe =		tran_fp_probe,
	.remove =		tran_fp_remove,
	.id_table = 	tran_id_table,
};



module_spi_driver(tran_fp_driver);

MODULE_DESCRIPTION("User mode SPI device interface");
MODULE_LICENSE("GPL");
