// SPDX-FileCopyrightText: 2024 Infineon Technologies AG
//
// SPDX-License-Identifier: MIT

/**
 * @file   authon_main.c
 * @date   Dec, 2023
 * @brief  main file for OPTIGA™ Authenticate On driver
 */
#define pr_fmt(fmt) \
	"%s:%s:%d: " fmt, KBUILD_MODNAME, __func__, __LINE__

#include <linux/version.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/poll.h>
#include <linux/cpufreq.h>
#include <linux/dmaengine.h>
#include <linux/power_supply.h>

#include "authon_api.h"
#include "platform/authon_platform.h"
#include "ecc/ecc.h"
#include "nvm/nvm.h"
#include "host_auth/host_auth.h"
#include "interface/interface.h"

//#include "tc_charger_class.h"
//#include "tc_adapter_class.h"
//#include "tc_ta_class.h"
#include "tc_common_class.h"
#include "tc_misc_intf.h"
#include "tc_charger.h"

/**
 * Optional features:
 * CPUS_FREQ_CONFIG : Set the current core to maximum frequency while setting others to minimum frequency
 *                    to reduce the impact of bus arbitration.
 * DMA_CONFIG       : Trigger dma_channel_rebalance().
 * USE_SYSTEM_WQ    : Use the system workqueue
 *                    (e.g., system_wq, system_unbound_wq, etc.).
 * USE_MMIO_RW      : RPI4 only. Access GPIO registers directly.
 * USE_TIMER_CNTVCT : RPI4 only. Use the CNTVCT counter directly to provide timing.
 * USE_SPINLOCK     : Use spin_lock_irq apis instead of local_irq_ with preempt_.
 * USE_YIELD        : Voluntarily yield the CPU before disabling interrupts and after restoring them.
 */
//#define CPUS_FREQ_CONFIG
//#define DMA_CONFIG
//#define USE_SYSTEM_WQ
//#define USE_MMIO_RW
//#define USE_TIMER_CNTVCT
//#define USE_SPINLOCK
//#define USE_YIELD

/**
 * Scheduling policy:
 * 0: Schedule on a fixed core (core: RUN_ON_CPU) to run sequentially.
 * 1: Schedule on a fixed core (core: RUN_ON_CPU) to run concurrently.
 * 2: Schedule on any core to run concurrently.
 */
#define SCHEDULING_POLICY 2

#define RUN_ON_CPU 0

#define AUTHON_NUM_DEVICES 65536

#define DEVICE_NAME "AuthOn"
#define CLASS_NAME "AuthOnClass"

#define MAX_1TAU 153
#define MAX_3TAU (3 * MAX_1TAU)
#define MAX_5TAU (5 * MAX_1TAU)
#define MAX_10TAU (10 * MAX_1TAU)

#define MAX_RETRY 3
#define READ_MAX_RETRY 3

enum IOCTL_CMD {
	SWI_DEV_POWERUP = 0x20,
	SWI_DEV_POWERDOWN = 0x21,
	SWI_DEV_RESET = 0x22,

	SWI_REG_GET_VALUE = 0x70,
	SWI_REG_GET_LOCK_VALUE = 0x72,

	SWI_GET_UID = 0x80,

	SWI_NVM_READ = 0x90,
	SWI_NVM_WRITE = 0x91,
	SWI_NVM_SET_PAGE_LOCK = 0x92,

	SWI_HA_GET_RAND_A = 0xA0,
	SWI_HA_SEND_RAND_B = 0xA1,
	SWI_HA_SEND_TAG_A = 0xA2,
	SWI_HA_GET_TAG_B = 0xA3,

	SWI_ECC_GET_CERT = 0xB0,
	SWI_ECC_SEND_CHALLENGE_GET_RESPONSE = 0xB1,
	SWI_ECC_SEND_MAC_BYTE = 0xB2,
};

struct nvm_message {
	uint8_t nvm_data[256];
	uint8_t offset;
	uint8_t page_count;
	uint16_t status;
};

struct ecc_message {
	uint8_t cert[12 + 72];
	uint8_t challenge[ECC_CHALLENGE_LEN];
	uint8_t response[44];
	uint8_t key_slot;
	uint16_t status;
	uint8_t mac_byte[10];
};

struct reg_message {
	uint16_t address;
	uint8_t length;
	uint8_t reg_data[256];
	uint16_t status;
};

struct lock_message {
	uint8_t user_sts_1;
	uint8_t user_sts_2;
	uint8_t user_sts_3;
	uint8_t user_sts_4;
	uint8_t user_sts_5;
	uint8_t user_sts_6;
	uint8_t user_sts_7;
	uint8_t user_sts_8;
	uint8_t lsc_feat_sts;
	uint8_t kill_sts;
	uint8_t lsc1[4];
	uint8_t lsc2[4];
	uint8_t ack;
};

enum kthread_lifecycle {
	KTHREAD_NULL,
	KTHREAD_FREE, /* Ready to accept a new job. */
	KTHREAD_BUSY, /* Currently busy executing a job. */
	KTHREAD_DEAD, /* The thread has been terminated. */
};

/* This context pertains to an individual instance of a CPU core. */
struct cpu_core_context {
	int busy_count;
	int fail_count;

	struct mutex lock;

	struct list_head list;
};

/* This context pertains to an individual instance of a physical chip. */
struct authon_chip_context {
	/* Context related to Linux character devices. */
	struct device dev;
	struct cdev cdev;
	//struct tran_device *bat_dev;
	//struct tran_properties bat_props;
	//struct charger_device *swchg_dev;
	int dev_num;
	unsigned long
		is_open; /* Only one instance is allowed to be open at a time. */

	/* AuthOn context. */
	struct authon_context on_ctx;
	unsigned char chip;

	struct power_supply_desc psy_desc;
	struct power_supply_config psy_cfg;
	struct power_supply *ctx_psy;
	struct power_supply *chg_psy;
	struct power_supply *master_charger_psy;
	struct work_struct test_work;
	struct delayed_work update_work;
	struct notifier_block fb_notifier;
	struct tran_device *tc_lcd;
	bool update_fg;
	int lcd_on;
	int ic_test_cycle;
};

struct job_context {
	struct list_head list;
	struct file *file;

	uint8_t cmd; /* A byte size command code. */
	uint8_t data[1024];
	ssize_t rc;
	size_t cmd_len;
	size_t resp_len;

	struct work_struct work;

	bool is_wait_for_read;

	wait_queue_head_t waitq; /* A waitqueue for fops_ioctl to wait on. */
	bool is_busy; /* The waitqueue waiting condition. */

	struct mutex lock;
};

/* This context relates to a session of file operations. */
struct session_context {
	uint8_t nonce_a[MAC_BYTE_LEN];
	uint8_t nonce_b[MAC_BYTE_LEN];
	uint8_t tag_a[MAC_BYTE_LEN];
	uint8_t tag_b[MAC_BYTE_LEN];
};

struct fops_context {
	struct authon_chip_context *chip_ctx;
	struct session_context s_ctx;
	struct job_context job_ctx;
};

/*
 * This is the AuthOn platform-dependent context.
 * It is stored in 'chip_ctx->on_ctx.swi_ctx.pf_ctx'
 */
struct this_platform_context {
	/* This mandatory common context must be placed first. */
	struct authon_platform_context_common common;

	/* Platform dependent contexts. */

#ifdef CPUS_FREQ_CONFIG
	unsigned int *cpus_freq;
#endif
#ifdef USE_MMIO_RW
	void __iomem *mmio;
#endif
	struct gpio_desc *gpio_swi;
	struct gpio_desc *gpio_debug;
	struct gpio_desc *board_gpio;
#ifdef USE_SPINLOCK
	spinlock_t spinlock;
#endif
	unsigned long irq_flags;
	int irq_semaphore;
	int gpiobase;
	bool boardver_gpios_not_support;
};

/* Define global accessible variables. */

static DEFINE_IDR(dev_nums_idr);
static DEFINE_MUTEX(idr_lock);
static dev_t authon_devt = 0;
static struct class *authon_class = NULL;
static LIST_HEAD(
	job_list); /* The sole purpose is for cleaning up jobs before mod_exit */
static struct mutex job_list_lock;
static bool is_mod_exit = false;
static bool board_support = false;
#ifndef USE_SYSTEM_WQ
static struct workqueue_struct *local_wq;
#endif

static int irq_disable(authon_platform_context *ctx);
static int irq_restore(authon_platform_context *ctx);

#ifdef USE_TIMER_CNTVCT
static u64 cntvct_to_ns(void)
{
	u64 cntvct = arch_timer_read_counter();
	u32 freq = arch_timer_get_cntfrq();
	u64 ns = (cntvct * 1000000000ULL) / freq; // Convert to nanoseconds
	mb();
	return ns;
}
#endif

#include <linux/gpio/driver.h>
#include "../../../pinctrl/mediatek/pinctrl-mtk-common-v2.h"

static int tran_gpio_direction_output(struct gpio_chip *swi_gc,unsigned int gpio,int value)
{
	struct gpio_chip *gc = swi_gc;
	struct mtk_pinctrl *hw = gpiochip_get_data(gc);
	const struct mtk_pin_desc *mtk_desc;
	if (gpio >= hw->soc->npins)
		return -EINVAL;

	mtk_desc = (const struct mtk_pin_desc *)&hw->soc->pins[gpio];

	(void)mtk_hw_set_value(hw, mtk_desc, PINCTRL_PIN_REG_DO, !!value);

	return mtk_hw_set_value(hw, mtk_desc, PINCTRL_PIN_REG_DIR, MTK_OUTPUT);
}

static int tran_gpio_direction_input(struct gpio_chip *swi_gc,unsigned int gpio)
{
	struct gpio_chip *gc = swi_gc;
	struct mtk_pinctrl *hw = gpiochip_get_data(gc);
	const struct mtk_pin_desc *mtk_desc;
	if (gpio >= hw->soc->npins)
		return -EINVAL;

	mtk_desc = (const struct mtk_pin_desc *)&hw->soc->pins[gpio];
	return mtk_hw_set_value(hw, mtk_desc, PINCTRL_PIN_REG_DIR, MTK_INPUT);
}

static void print_uid(uint8_t *uid)
{
	pr_info("uid: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x ",
		uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6], uid[7],
		uid[8], uid[9], uid[10], uid[11]);
}

/*
 * This function should not be executed while job_ctx->lock is held.
 */
static void reset_job_context(struct job_context *job_ctx)
{
	mutex_lock(&job_list_lock);
	flush_work(&job_ctx->work);
	if (!list_empty(&job_ctx->list)) {
		list_del_init(&job_ctx->list);
	}
	mutex_unlock(&job_list_lock);

	memset(job_ctx->data, 0, sizeof(job_ctx->data));
	job_ctx->is_wait_for_read = false;
	job_ctx->is_busy = false;
	job_ctx->cmd = 0;
	job_ctx->rc = 0;
	job_ctx->cmd_len = 0;
	job_ctx->resp_len = 0;
}

static void work_execution(struct work_struct *work)
{
	ssize_t rc;
	struct job_context *job_ctx;
	struct fops_context *fops_ctx;
	struct authon_chip_context *chip_ctx;
	struct authon_context *on_ctx;
	struct session_context *s_ctx;
	struct swi_context *swi_ctx;

	int retry = MAX_RETRY;
	uint16_t ret;

	uint16_t reg_addr;
	uint8_t rd_data;
	uint8_t busy = 1;
	uint8_t timeout = 10;

	uint8_t ecc_pubkey[PUBLICKEY_BYTE_LEN];
	uint8_t gf2n_odc[((ODC_BYTE_LEN + PAGE_SIZE_BYTE - 1) / PAGE_SIZE_BYTE) *
			 PAGE_SIZE_BYTE];

	uint8_t challenge[ECC_CHALLENGE_LEN] = { 0 };
	uint8_t response[ECC_RESPONSE_LEN] = { 0 };

	uint8_t uid[UID_SIZE_BYTE];
	uint8_t tid[2];
	uint8_t pid[2];
	uint8_t nvm_page_lock;

	struct reg_message rr_msg;
	struct reg_message wr_msg;
	struct ecc_message ecc_msg_1;
	struct ecc_message ecc_msg_2;
	struct nvm_message r_msg;
	struct nvm_message w_msg;
	struct lock_message lock_msg;

	memset(&rr_msg, 0, sizeof(rr_msg));
	memset(&wr_msg, 0, sizeof(wr_msg));
	memset(&ecc_msg_1, 0, sizeof(ecc_msg_1));
	memset(&ecc_msg_2, 0, sizeof(ecc_msg_2));
	memset(&r_msg, 0, sizeof(r_msg));
	memset(&w_msg, 0, sizeof(w_msg));
	memset(&lock_msg, 0, sizeof(lock_msg));

	if (!(job_ctx = container_of(work, struct job_context, work)) ||
	    !(fops_ctx = job_ctx->file->private_data) ||
	    !(chip_ctx = fops_ctx->chip_ctx) || !(s_ctx = &fops_ctx->s_ctx) ||
	    !(on_ctx = &chip_ctx->on_ctx) || !(swi_ctx = &on_ctx->swi_ctx)) {
		pr_err("Failed to retrieve contexts.\n");
		rc = -EFAULT;
		goto out;
	}

	mutex_lock(&job_ctx->lock);

	pr_info("Received IOCTL command: 0x%x\n", job_ctx->cmd);

	switch (job_ctx->cmd) {
	case SWI_DEV_POWERUP:
		pr_info("SWI_DEV_POWERUP:\n");
		ret = authon_exe_power_up(on_ctx);
		if (ret != EXE_SUCCESS) {
			pr_err("Error: device power up\n");
			rc = -EFAULT;
			goto out;
		}
		break;
	case SWI_DEV_POWERDOWN:
		pr_info("SWI_DEV_POWERDOWN:\n");
		ret = authon_exe_power_down(on_ctx);
		if (ret != EXE_SUCCESS) {
			pr_err("Error: device power down\n");
			rc = -EFAULT;
			goto out;
		}
		break;
	case SWI_DEV_RESET:
		pr_info("SWI_DEV_RESET:\n");
		ret = authon_exe_reset(on_ctx);
		if (ret != EXE_SUCCESS) {
			pr_err("Error: device reset\n");
			rc = -EFAULT;
			goto out;
		}

		ret = authon_exe_power_down(on_ctx);
		if (ret != EXE_SUCCESS) {
			pr_err("Error: device power down\n");
			rc = -EFAULT;
			goto out;
		}

		ret = authon_exe_power_up(on_ctx);
		if (ret != EXE_SUCCESS) {
			pr_err("Error: device power up\n");
			rc = -EFAULT;
			goto out;
		}

		swi_bus_command_eda(swi_ctx, 0);
		swi_bus_command_sda(swi_ctx, ON_DEFAULT_ADDRESS);

		break;
	case SWI_GET_UID:
		pr_info("SWI_GET_UID:\n");

		memset(&on_ctx->cap, 0, sizeof(on_ctx->cap));

		while (retry) {
			ret = authon_init_sdk(on_ctx);
			if (ret != SDK_SUCCESS) {
				pr_err("Error: failed to initialize SDK ret=0x%X retry:%d",
				       ret, retry);
			} else {
				pr_info("Found %d device(s) on the bus\n",
					on_ctx->enumeration.device_found);
				if (on_ctx->enumeration.device_found != 0) {
					memcpy(job_ctx->data,
					       &(on_ctx->cap.uid[0][0]),
					       UID_BYTE_LEN);
					job_ctx->resp_len = UID_BYTE_LEN;

					rc = 0;
					break;
				} else {
					pr_err("Error: Device not found retry_left:%d\n",
					       retry);
				}
			}
			retry--;
		}

		break;
	case SWI_REG_GET_VALUE:
		pr_info("SWI_REG_GET_VALUE:\n");

		memcpy(&rr_msg, job_ctx->data, sizeof(rr_msg));

		ret = authon_read_swi_sfr(on_ctx, rr_msg.address,
					  rr_msg.reg_data, rr_msg.length);
		if (ret != SDK_INTERFACE_SUCCESS) {
			pr_err("Error: failed to read reg ret=0x%X ", ret);
			rc = -EFAULT;
			goto out;
		}

		memcpy(job_ctx->data, &rr_msg, sizeof(rr_msg));
		job_ctx->resp_len = sizeof(rr_msg);

		break;
	case SWI_NVM_SET_PAGE_LOCK:
		pr_info("SWI_NVM_SET_PAGE_LOCK:\n");

		memcpy(&nvm_page_lock, job_ctx->data, sizeof(nvm_page_lock));

		pr_info("Selected NVM page lock:%d\n", nvm_page_lock);

		if (nvm_page_lock > 64) {
			pr_err("Error: invalid nvm lock page\n");
			rc = -EFAULT;
			goto out;
		} else {
			ret = authon_set_nvm_page_lock(on_ctx, nvm_page_lock);
			if (ret != APP_NVM_SUCCESS) {
				pr_err("Error: unable to lock the NVM page\n");
			} else {
				pr_err("NVM page done");
			}
		}

		break;
	case SWI_REG_GET_LOCK_VALUE:
		pr_info("SWI_REG_LOCK_VALUE:\n");

		retry = READ_MAX_RETRY;
		while (retry) {
			ret = authon_read_swi_sfr(on_ctx,
						  ON_SFR_USER_NVM_LOCK_STS_1,
						  &(lock_msg.user_sts_1), 1);
			if (ret == SDK_SUCCESS) {
				pr_info("ON_SFR_USER_NVM_LOCK_STS_1: 0x%x\n",
					lock_msg.user_sts_1);
				lock_msg.ack = 0x00;
				break;
			}
			retry--;
		}

		if (retry == 0) {
			pr_err("error: read ON_SFR_USER_NVM_LOCK_STS_1 ret=%x\n",
			       ret);
			lock_msg.ack = 0xff;
			goto exit_SWI_REG_GET_LOCK_VALUE;
		}

		retry = READ_MAX_RETRY;
		while (retry) {
			ret = authon_read_swi_sfr(on_ctx,
						  ON_SFR_USER_NVM_LOCK_STS_2,
						  &(lock_msg.user_sts_2), 1);
			if (ret == SDK_SUCCESS) {
				pr_info("ON_SFR_USER_NVM_LOCK_STS_2: 0x%x \n",
					lock_msg.user_sts_2);
				lock_msg.ack = 0x00;
				break;
			}
			retry--;
		}

		if (retry == 0) {
			pr_err("error: read ON_SFR_USER_NVM_LOCK_STS_2 ret=%x \n",
			       ret);
			lock_msg.ack = 0xff;
			goto exit_SWI_REG_GET_LOCK_VALUE;
		}

		retry = READ_MAX_RETRY;
		while (retry) {
			ret = authon_read_swi_sfr(on_ctx,
						  ON_SFR_USER_NVM_LOCK_STS_3,
						  &(lock_msg.user_sts_3), 1);
			if (ret == SDK_SUCCESS) {
				pr_info("ON_SFR_USER_NVM_LOCK_STS_3: 0x%x \n",
					lock_msg.user_sts_3);
				lock_msg.ack = 0x00;
				break;
			}
			retry--;
		}

		if (retry == 0) {
			pr_err("error: read ON_SFR_USER_NVM_LOCK_STS_3 ret=%x \n",
			       ret);
			lock_msg.ack = 0xff;
			goto exit_SWI_REG_GET_LOCK_VALUE;
		}

		retry = READ_MAX_RETRY;
		while (retry) {
			ret = authon_read_swi_sfr(on_ctx,
						  ON_SFR_USER_NVM_LOCK_STS_4,
						  &(lock_msg.user_sts_4), 1);
			if (ret == SDK_SUCCESS) {
				pr_info("ON_SFR_USER_NVM_LOCK_STS_4: 0x%x \n",
					lock_msg.user_sts_4);
				lock_msg.ack = 0x00;
				break;
			}
			retry--;
		}

		if (retry == 0) {
			pr_err("error: read ON_SFR_USER_NVM_LOCK_STS_4 ret=%x \n",
			       ret);
			lock_msg.ack = 0xff;
			goto exit_SWI_REG_GET_LOCK_VALUE;
		}

		retry = READ_MAX_RETRY;
		while (retry) {
			ret = authon_read_swi_sfr(on_ctx,
						  ON_SFR_USER_NVM_LOCK_STS_5,
						  &(lock_msg.user_sts_5), 1);
			if (ret == SDK_SUCCESS) {
				pr_info("ON_SFR_USER_NVM_LOCK_STS_5: 0x%x \n",
					lock_msg.user_sts_5);
				lock_msg.ack = 0x00;
				break;
			}
			retry--;
		}

		if (retry == 0) {
			pr_err("error: read ON_SFR_USER_NVM_LOCK_STS_5 ret=%x \n",
			       ret);
			lock_msg.ack = 0xff;
			goto exit_SWI_REG_GET_LOCK_VALUE;
		}

		retry = READ_MAX_RETRY;
		while (retry) {
			ret = authon_read_swi_sfr(on_ctx,
						  ON_SFR_USER_NVM_LOCK_STS_6,
						  &(lock_msg.user_sts_6), 1);
			if (ret == SDK_SUCCESS) {
				pr_info("ON_SFR_USER_NVM_LOCK_STS_6: 0x%x \n",
					lock_msg.user_sts_6);
				lock_msg.ack = 0x00;
				break;
			}
			retry--;
		}

		if (retry == 0) {
			pr_err("error: read ON_SFR_USER_NVM_LOCK_STS_6 ret=%x \n",
			       ret);
			lock_msg.ack = 0xff;
			goto exit_SWI_REG_GET_LOCK_VALUE;
		}

		retry = READ_MAX_RETRY;
		while (retry) {
			ret = authon_read_swi_sfr(on_ctx,
						  ON_SFR_USER_NVM_LOCK_STS_7,
						  &(lock_msg.user_sts_7), 1);
			if (ret == SDK_SUCCESS) {
				pr_info("ON_SFR_USER_NVM_LOCK_STS_7: 0x%x \n",
					lock_msg.user_sts_7);
				lock_msg.ack = 0x00;
				break;
			}
			retry--;
		}

		if (retry == 0) {
			pr_err("error: read ON_SFR_USER_NVM_LOCK_STS_7 ret=%x \n",
			       ret);
			lock_msg.ack = 0xff;
			goto exit_SWI_REG_GET_LOCK_VALUE;
		}

		retry = READ_MAX_RETRY;
		while (retry) {
			ret = authon_read_swi_sfr(on_ctx,
						  ON_SFR_USER_NVM_LOCK_STS_8,
						  &(lock_msg.user_sts_8), 1);
			if (ret == SDK_SUCCESS) {
				pr_info("ON_SFR_USER_NVM_LOCK_STS_8: 0x%x \n",
					lock_msg.user_sts_8);
				lock_msg.ack = 0x00;
				break;
			}
			retry--;
		}

		if (retry == 0) {
			pr_err("error: read ON_SFR_USER_NVM_LOCK_STS_8 ret=%x \n",
			       ret);
			lock_msg.ack = 0xff;
			goto exit_SWI_REG_GET_LOCK_VALUE;
		}

		retry = READ_MAX_RETRY;
		while (retry) {
			ret = authon_read_swi_sfr(on_ctx, ON_SFR_LSC_FEAT_STS,
						  &(lock_msg.lsc_feat_sts), 1);
			if (ret == SDK_SUCCESS) {
				pr_info("ON_SFR_LSC_FEAT_STS: 0x%x \n",
					lock_msg.lsc_feat_sts);
				lock_msg.ack = 0x00;
				break;
			}
			retry--;
		}

		if (retry == 0) {
			pr_err("error: read sfr ON_SFR_LSC_FEAT_STS ret=%x \n",
			       ret);
			lock_msg.ack = 0xff;
			goto exit_SWI_REG_GET_LOCK_VALUE;
		}

		retry = READ_MAX_RETRY;
		while (retry) {
			ret = authon_read_swi_sfr(on_ctx,
						  ON_SFR_LSC_KILL_ACT_STS,
						  &(lock_msg.kill_sts), 1);
			if (ret == SDK_SUCCESS) {
				pr_info("ON_SFR_LSC_KILL_ACT_STS: 0x%x \n",
					lock_msg.kill_sts);
				lock_msg.ack = 0x00;
				break;
			}
			retry--;
		}

		if (retry == 0) {
			pr_err("error: read ON_SFR_LSC_KILL_ACT_STS ret=%x \n",
			       ret);
			lock_msg.ack = 0xff;
			goto exit_SWI_REG_GET_LOCK_VALUE;
		}

		retry = READ_MAX_RETRY;
		while (retry) {
			ret = authon_read_nvm(on_ctx, ON_LSC_1, lock_msg.lsc1,
					      1);
			if (ret == SDK_SUCCESS) {
				pr_info("lock_msg.lsc1: %.2x %.2x %.2x %.2x \n",
					lock_msg.lsc1[0], lock_msg.lsc1[1],
					lock_msg.lsc1[2], lock_msg.lsc1[3]);
				break;
			}
			retry--;
		}

		if (retry == 0) {
			pr_err("error: read lsc1 %x \n", ret);
			lock_msg.ack = 0xff;
			goto exit_SWI_REG_GET_LOCK_VALUE;
		}

		retry = READ_MAX_RETRY;
		while (retry) {
			ret = authon_read_nvm(on_ctx, ON_LSC_2, lock_msg.lsc2,
					      1);
			if (ret == SDK_SUCCESS) {
				pr_info("lock_msg.lsc2: %.2x %.2x %.2x %.2x \n",
					lock_msg.lsc2[0], lock_msg.lsc2[1],
					lock_msg.lsc2[2], lock_msg.lsc2[3]);
				break;
			}
			retry--;
		}

		if (retry == 0) {
			pr_err("error: read lsc2 %x \n", ret);
			lock_msg.ack = 0xff;
		}

exit_SWI_REG_GET_LOCK_VALUE:
		memcpy(job_ctx->data, &lock_msg, sizeof(struct lock_message));
		job_ctx->resp_len = sizeof(struct lock_message);

		break;
	case SWI_NVM_READ:
		memcpy(&r_msg, job_ctx->data, sizeof(r_msg));

		pr_info("SWI_NVM_READ: %d %d\n", r_msg.page_count,
			r_msg.offset);

		ret = authon_read_nvm(on_ctx, r_msg.offset, r_msg.nvm_data,
				      r_msg.page_count);
		if (APP_NVM_SUCCESS != ret) {
			pr_err("Error: AUTNON NVM failed,read ret=0x%x\n", ret);
			r_msg.status = ret;
			rc = -EFAULT;
			goto out;
		} else {
			r_msg.status = 0x0;
		}

		memcpy(job_ctx->data, &r_msg, sizeof(r_msg));
		job_ctx->resp_len = sizeof(r_msg);
		r_msg.status = 0x0;
		rc = 0;

		break;
	case SWI_NVM_WRITE:
		memcpy(&w_msg, job_ctx->data, sizeof(w_msg));

		swi_bus_command_eda(swi_ctx, 0);
		swi_bus_command_sda(swi_ctx, ON_DEFAULT_ADDRESS);

		pr_info("SWI_NVM_WRITE: %d %d\n", w_msg.offset,
			w_msg.page_count);

		ret = authon_write_nvm(on_ctx, w_msg.offset, w_msg.nvm_data,
				       w_msg.page_count);

		if (ret == APP_NVM_PAGE_LOCKED) {
			pr_err("Error: NVM page is locked\n");
			w_msg.status = APP_NVM_PAGE_LOCKED;
			goto exit_SWI_NVM_WRITE;
		}

		if (APP_NVM_SUCCESS != ret) {
			pr_err("Error: AUTNON NVM failed, write ret=0x%x remain retry: %d\n",
			       ret, retry);
			rc = -EFAULT;
			w_msg.status = ret;
			goto out;
		} else {
			w_msg.status = 0x0;
			rc = 0;
		}

exit_SWI_NVM_WRITE:
		memcpy(job_ctx->data, &w_msg, sizeof(w_msg));
		job_ctx->resp_len = sizeof(w_msg);

		break;
	case SWI_HA_GET_RAND_A:
		pr_info("SWI_HA_GET_RAND_A:\n");

		ret = get_random_number(on_ctx, s_ctx->nonce_a);
		if (ret != INF_SWI_SUCCESS) {
			pr_err("Error: get_random_number. ret=%x", ret);
			rc = -EFAULT;
			goto out;
		}

		memcpy(job_ctx->data, s_ctx->nonce_a, sizeof(s_ctx->nonce_a));
		job_ctx->resp_len = sizeof(s_ctx->nonce_a);

		break;
	case SWI_HA_SEND_RAND_B:
		pr_info("SWI_HA_SEND_RAND_B:\n");

		memcpy(s_ctx->nonce_b, job_ctx->data, sizeof(s_ctx->nonce_b));

		swi_irq_disable(swi_ctx);
		ret = swi_bus_command_drres(swi_ctx, s_ctx->nonce_b);
		swi_irq_restore(swi_ctx);
		if (INF_SWI_SUCCESS != ret) {
			pr_err("Error: swi_bus_command_drres. ret=%x", ret);
			rc = -EFAULT;
			goto out;
		}

		break;
	case SWI_HA_SEND_TAG_A:
		pr_info("SWI_HA_SEND_TAG_A:\n");

		memcpy(s_ctx->tag_a, job_ctx->data, sizeof(s_ctx->tag_a));

		swi_irq_disable(swi_ctx);
		ret = swi_bus_command_hreq1(swi_ctx, s_ctx->tag_a);
		swi_irq_restore(swi_ctx);
		if (INF_SWI_SUCCESS != ret) {
			pr_err("Error: swi_bus_command_hreq1 %x\n", ret);
			rc = -EFAULT;
			goto out;
		}

		/* Required extra time required for HREQ1 */
		mdelay(10);

		/* Wait for MAC done */
		reg_addr = ON_SFR_BUSY_STS;
		timeout = 10;
		do {
			ret = intf_read_register(swi_ctx, reg_addr, &rd_data,
						 1);
			if (SDK_INTERFACE_SUCCESS != ret) {
				pr_err("Error: intf_read_register %x\n", ret);
				rd_data =
					(0x1 << BIT_AUTH_MAC_BUSY) |
					(0x1
					 << BIT_RANDOM_NUM_BUSY); //default busy if unable to read actual register value
			} else {
				busy = (rd_data >> BIT_AUTH_MAC_BUSY) & 0x01;
			}
			timeout--;
			if (timeout == 0) {
				rc = -EFAULT;
				goto out;
			}
			mdelay(1);
		} while (busy == 1);

		break;
	case SWI_HA_GET_TAG_B:
		pr_info("SWI_HA_GET_TAG_B:\n");

		swi_irq_disable(swi_ctx);
		ret = swi_bus_command_dres1(swi_ctx, s_ctx->tag_b);
		swi_irq_restore(swi_ctx);
		if (INF_SWI_SUCCESS != ret) {
			pr_err("Error: swi_bus_command_dres1 ret=%x", ret);
			rc = -EFAULT;
			goto out;
		}

		mdelay(10);

		memcpy(job_ctx->data, s_ctx->tag_b, sizeof(s_ctx->tag_b));
		job_ctx->resp_len = sizeof(s_ctx->tag_b);

		break;
	case SWI_ECC_GET_CERT:
		pr_info("SWI_ECC_GET_CERT:\n");

		memcpy(&ecc_msg_1, job_ctx->data, sizeof(ecc_msg_1));

		ret = authon_exe_read_uid(on_ctx, uid, tid, pid);
		if (ret != EXE_SUCCESS) {
			pr_err("Error: Read UID failed\n");
			ecc_msg_1.status = ret;
			rc = -EFAULT;
			goto out;
		}

		/* Read ODC and public key */
		ret = authon_get_odc(on_ctx, ecc_msg_1.key_slot, gf2n_odc);
		if (ret != APP_ECC_SUCCESS) {
			ecc_msg_1.status = ret;
			pr_err("Error: Read ODC failed\n");
			/* For device with host auth enabled, ensure that host authentication is executed prior to reading the ODC. */
			rc = -EFAULT;
			goto out;
		} else {
			pr_info("SWI_ECC_GET_CERT: Read Public key\n");

			ret = authon_get_ecc_publickey(
				on_ctx, ecc_msg_1.key_slot, ecc_pubkey);
			if (ret != APP_ECC_SUCCESS) {
				ecc_msg_1.status = ret;
				pr_err("Error: Read Public key failed\n");
				rc = -EFAULT;
				goto out;
			} else {
				ecc_msg_1.status = 0;
			}
		}

		memcpy(ecc_msg_1.cert, uid, sizeof(uid));
		memcpy(ecc_msg_1.cert + sizeof(uid), ecc_pubkey,
		       PUBLICKEY_BYTE_LEN);
		memcpy(ecc_msg_1.cert + sizeof(uid) + PUBLICKEY_BYTE_LEN,
		       gf2n_odc, ODC_BYTE_LEN);
		ecc_msg_1.status = 0;

		rc = 0;

		memcpy(job_ctx->data, &ecc_msg_1, sizeof(ecc_msg_1));
		job_ctx->resp_len = sizeof(ecc_msg_1);

		break;
	case SWI_ECC_SEND_CHALLENGE_GET_RESPONSE:
		pr_info("SWI_ECC_SEND_CHALLENGE_GET_RESPONSE:\n");

		memcpy(&ecc_msg_2, job_ctx->data, sizeof(ecc_msg_2));
		memcpy(challenge, ecc_msg_2.challenge,
		       sizeof(ecc_msg_2.challenge));

		ret = authon_send_challenge_and_get_response(
			on_ctx, (uint8_t *)challenge, response,
			ecc_msg_2.key_slot, FIXED_WAIT);
		if (APP_ECC_SUCCESS != ret) {
			pr_err("Error: Send Challenge and Get Response failed\n");
			ecc_msg_2.status = ret;
			rc = -EFAULT;
			goto out;
		}

		memcpy(ecc_msg_2.response, response, sizeof(response));

		memcpy(job_ctx->data, &ecc_msg_2, sizeof(ecc_msg_2));
		job_ctx->resp_len = sizeof(ecc_msg_2);

		break;
	case SWI_ECC_SEND_MAC_BYTE:
		pr_info("SWI_ECC_SEND_MAC_BYTE:\n");

		memcpy(&ecc_msg_1, job_ctx->data, sizeof(ecc_msg_1));

		pr_info("SWI_ECC_SEND_MAC_BYTE: %x %x %x %x %x %x %x %x %x %x\n",
			ecc_msg_1.mac_byte[0], ecc_msg_1.mac_byte[1],
			ecc_msg_1.mac_byte[2], ecc_msg_1.mac_byte[3],
			ecc_msg_1.mac_byte[4], ecc_msg_1.mac_byte[5],
			ecc_msg_1.mac_byte[6], ecc_msg_1.mac_byte[7],
			ecc_msg_1.mac_byte[8], ecc_msg_1.mac_byte[9]);

		authon_unlock_nvm_locked(on_ctx, ecc_msg_1.mac_byte);

		break;
	default:
		rc = -EINVAL;
		pr_err("Invalid IOCTL command: 0x%x\n", job_ctx->cmd);
		goto out;
	}

	rc = 0;
out:
	job_ctx->rc = rc;
	job_ctx->is_busy = false;
	mutex_unlock(&job_ctx->lock);
	wake_up_interruptible_all(&job_ctx->waitq);
}

static int fops_open(struct inode *inode, struct file *file)
{
	int rc;
	struct authon_chip_context *chip_ctx;
	struct fops_context *fops_ctx;
	struct job_context *job_ctx;

	chip_ctx =
		container_of(inode->i_cdev, struct authon_chip_context, cdev);
	if (IS_ERR_OR_NULL(chip_ctx)) {
		pr_err("Failed to retrieve the chip context.\n");
		rc = -EFAULT;
		goto out;
	}

	if (test_and_set_bit(0, &chip_ctx->is_open)) {
		pr_info("Device is busy.\n");
		rc = -EBUSY;
		goto out;
	}

	fops_ctx = kzalloc(sizeof(*fops_ctx), GFP_KERNEL);
	if (!fops_ctx) {
		pr_err("kzalloc has failed.\n");
		rc = -ENOMEM;
		goto out;
	}

	/* Initialize the job_ctx. */

	job_ctx = &fops_ctx->job_ctx;
	job_ctx->file = file;
	init_waitqueue_head(&job_ctx->waitq);
	mutex_init(&job_ctx->lock);
	INIT_WORK(&job_ctx->work, work_execution);

	fops_ctx->chip_ctx = chip_ctx;
	file->private_data = fops_ctx;

	return 0;
out:
	return rc;
}

static int fops_release(struct inode *inode, struct file *file)
{
	struct authon_chip_context *chip_ctx;
	struct fops_context *fops_ctx;
	struct job_context *job_ctx;

	chip_ctx =
		container_of(inode->i_cdev, struct authon_chip_context, cdev);
	if (IS_ERR_OR_NULL(chip_ctx)) {
		pr_err("Failed to retrieve the chip context.\n");
		return -EFAULT;
	}

	if (!(fops_ctx = file->private_data)) {
		pr_err("Failed to retrieve contexts.\n");
		return -EFAULT;
	}

	job_ctx = &fops_ctx->job_ctx;
	reset_job_context(job_ctx);
	mutex_destroy(&job_ctx->lock);
	kfree(fops_ctx);
	file->private_data = NULL;

	clear_bit(0, &chip_ctx->is_open);

	return 0;
}

static ssize_t fops_write(struct file *file, const char __user *buf,
			  size_t size, loff_t *off)
{
	ssize_t ret = 0;
	struct fops_context *fops_ctx = file->private_data;
	struct job_context *job_ctx;

	if (!size) {
		ret = -EINVAL;
		return ret;
	}

	if (IS_ERR_OR_NULL(fops_ctx)) {
		pr_err("Failed to retrieve contexts.\n");
		ret = -EFAULT;
		return ret;
	}

	job_ctx = &fops_ctx->job_ctx;

	mutex_lock(&job_ctx->lock);

	if (job_ctx->is_wait_for_read) {
		ret = -EBUSY;
		goto out;
	}

	/* Read the command code. */
	if (copy_from_user(&job_ctx->cmd, buf, 1) != 0) {
		pr_err("copy_from_user() has failed.\n");
		ret = -EFAULT;
		goto out;
	}

	/*
     * Auto detect the payload length based on
     * the command code.
     */
	switch (job_ctx->cmd) {
	case SWI_REG_GET_VALUE:
		job_ctx->cmd_len = sizeof(struct reg_message);
		break;
	case SWI_NVM_SET_PAGE_LOCK:
		job_ctx->cmd_len = sizeof(uint8_t);
		break;
	case SWI_NVM_READ:
	case SWI_NVM_WRITE:
		job_ctx->cmd_len = sizeof(struct nvm_message);
		break;
	case SWI_HA_SEND_RAND_B:
	case SWI_HA_SEND_TAG_A:
		job_ctx->cmd_len = MAC_BYTE_LEN;
		break;
	case SWI_ECC_GET_CERT:
	case SWI_ECC_SEND_CHALLENGE_GET_RESPONSE:
	case SWI_ECC_SEND_MAC_BYTE:
		job_ctx->cmd_len = sizeof(struct ecc_message);
		break;
	default:
		job_ctx->cmd_len = 0;
	}

	/* Read the payload. */
	if (job_ctx->cmd_len > 0) {
		if (copy_from_user(job_ctx->data, buf + 1, job_ctx->cmd_len) !=
		    0) {
			pr_err("copy_from_user() has failed.\n");
			ret = -EFAULT;
			goto out;
		}
	}

	mutex_lock(&job_list_lock);

	if (is_mod_exit) {
		mutex_unlock(&job_list_lock);
		pr_err("module_exit is invoked.\n");
		ret = -ENODEV;
		goto out;
	}

#ifdef USE_SYSTEM_WQ
	/*
     * Queue the job. Use the system workqueues.
     * system_wq: Execute on a specific CPU.
     * system_unbound_wq: Not bound to any specific CPU.
     */
#if SCHEDULING_POLICY == 0
#error "The system workqueue does not support scheduling policy mode 0."
#elif SCHEDULING_POLICY == 1
	if (!queue_work_on(RUN_ON_CPU, system_wq, &job_ctx->work)) {
#elif SCHEDULING_POLICY == 2
	if (!queue_work(system_unbound_wq, &job_ctx->work)) {
#else /* SCHEDULING_POLICY */
#error "Invalid scheduling policy."
#endif /* SCHEDULING_POLICY */
#else /* USE_SYSTEM_WQ */
	/*
     * Queue the job. Use the local workqueue.
     */
#if SCHEDULING_POLICY == 0 || SCHEDULING_POLICY == 1
	if (!queue_work_on(RUN_ON_CPU, local_wq, &job_ctx->work)) {
#elif SCHEDULING_POLICY == 2
	if (!queue_work(local_wq, &job_ctx->work)) {
#else /* SCHEDULING_POLICY */
#error "Invalid scheduling policy."
#endif /* SCHEDULING_POLICY */
#endif /* USE_SYSTEM_WQ */
		mutex_unlock(&job_list_lock);
		pr_err("Unable to queue the job.\n");
		ret = -EFAULT;
		goto out;
	}

	job_ctx->is_busy = true;
	job_ctx->is_wait_for_read = true;

	/* Insert the job to the global job list */
	list_add_tail(&job_ctx->list, &job_list);

	mutex_unlock(&job_list_lock);

	/*
	* The received command will be processed immediately;
	* therefore, partial write operations are not supported here.
	*/
	*off = 0;

	ret = size;
out:
	mutex_unlock(&job_ctx->lock);
	return ret;
}

static ssize_t fops_read(struct file *file, char __user *buf, size_t size,
			 loff_t *off)
{
	ssize_t ret = 0;
	struct fops_context *fops_ctx = NULL;
	struct job_context *job_ctx = NULL;

	if (!(fops_ctx = file->private_data)) {
		pr_err("Failed to retrieve contexts.\n");
		ret = -EFAULT;
		goto out;
	}

	job_ctx = &fops_ctx->job_ctx;

	if (!(file->f_flags & O_NONBLOCK)) {
		if (wait_event_interruptible(job_ctx->waitq,
					     !job_ctx->is_busy)) {
			ret = -EINTR;
			pr_warn("wait_event_interruptible interrupted by a signal.\n");
			mutex_lock(&job_ctx->lock);
			goto out;
		}
	}

	mutex_lock(&job_ctx->lock);

	if (job_ctx->is_busy) {
		ret = 0;
		goto out;
	}

	if (job_ctx->rc) {
		ret = job_ctx->rc;
		goto out_clean;
	}

	/* Partial read is not supported for now... */
	if (size < job_ctx->resp_len) {
		pr_err("User buffer size is too small.\n");
		ret = -EINVAL;
		goto out;
	}

	/* Copy data from kernel to user space. */

	if (job_ctx->resp_len) {
		if (copy_to_user(buf, job_ctx->data, job_ctx->resp_len) != 0) {
			pr_err("copy_to_user() has failed.\n");
			ret = -EFAULT;
			goto out_clean;
		}
	}

	ret = job_ctx->resp_len;

out_clean:
	/* Clean up the job. */
	mutex_unlock(&job_ctx->lock);
	reset_job_context(job_ctx);
	return ret;
out:
	mutex_unlock(&job_ctx->lock);
	return ret;
}

static __poll_t fops_poll(struct file *file,
			  struct poll_table_struct *poll_table)
{
	struct fops_context *fops_ctx = NULL;
	struct job_context *job_ctx = NULL;
	__poll_t mask = 0;

	if (!(fops_ctx = file->private_data)) {
		pr_err("Failed to retrieve contexts.\n");
		return 0;
	}

	job_ctx = &fops_ctx->job_ctx;

	poll_wait(file, &job_ctx->waitq, poll_table);

	mutex_lock(&job_ctx->lock);

	if (job_ctx->is_busy) {
		mask = 0;
	} else {
		mask = EPOLLIN | EPOLLRDNORM; /* Ready for fops_read. */
	}

	mutex_unlock(&job_ctx->lock);

	return mask;
}

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = fops_open,
	.release = fops_release,
	.read = fops_read,
	.write = fops_write,
	.poll = fops_poll,
	.llseek = no_llseek,
};

static int gpio_write(authon_platform_context *ctx, int value)
{
	struct this_platform_context *pf_ctx =
		(struct this_platform_context *)ctx;

	//irq_disable(ctx);

#ifdef USE_MMIO_RW
#define GPSET0 0x1c
#define GPCLR0 0x28
	uint32_t *gpset0 = (uint32_t *)(pf_ctx->mmio + GPSET0);
	uint32_t *gpclr0 = (uint32_t *)(pf_ctx->mmio + GPCLR0);

	if (value) { /* Set */
		*gpset0 |= (1u << desc_to_gpio(pf_ctx->gpio_swi));
	} else { /* Clear */
		*gpclr0 |= (1u << desc_to_gpio(pf_ctx->gpio_swi));
	}
#else
	gpiod_set_value(pf_ctx->gpio_swi, value);
#endif
	wmb();

	//irq_restore(ctx);

	return 0;
}

static int gpio_read(authon_platform_context *ctx)
{
	struct this_platform_context *pf_ctx =
		(struct this_platform_context *)ctx;
	int value;

	//irq_disable(ctx);

#ifdef USE_MMIO_RW
#define GPLEV0 0x34
	uint32_t *gplev0 = (uint32_t *)(pf_ctx->mmio + GPLEV0);
	value = (*gplev0 >> desc_to_gpio(pf_ctx->gpio_swi)) & 0x1;
#else
	value = gpiod_get_value(pf_ctx->gpio_swi);
#endif
	rmb();

	//irq_restore(ctx);

	return value;
}

static int gpio_conf(authon_platform_context *ctx, int direction, int value)
{
	struct this_platform_context *pf_ctx =
		(struct this_platform_context *)ctx;

	//irq_disable(ctx);

	if (direction == SWI_GPIO_DIRECTION_OUT) {
		//gpiod_direction_output(pf_ctx->gpio_swi, value);
		tran_gpio_direction_output(gpiod_to_chip(pf_ctx->gpio_swi),
				desc_to_gpio(pf_ctx->gpio_swi) - pf_ctx->gpiobase, value);
	} else {
		tran_gpio_direction_input(gpiod_to_chip(pf_ctx->gpio_swi),
				desc_to_gpio(pf_ctx->gpio_swi) - pf_ctx->gpiobase);
		//gpiod_direction_input(pf_ctx->gpio_swi);
	}

	//irq_restore(ctx);

	return 0;
}

#ifdef CPUS_FREQ_CONFIG
/**
 * Slow down other cores to reduce the delay caused by
 * the bus arbitration protocol.
 */
static void cpus_freq_set(struct this_platform_context *pf_ctx)
{
	int cpu;
	struct cpufreq_policy *policy;
	unsigned int freq, relation;

	for_each_online_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (policy) {
			pf_ctx->cpus_freq[cpu] = cpufreq_get(cpu);
			if (cpu == RUN_ON_CPU) {
				freq = policy->max;
				relation = CPUFREQ_RELATION_L;
			} else {
				freq = policy->min;
				relation = CPUFREQ_RELATION_H;
			}

			//pr_info("Setting CPU %d frequency to %u kHz\n", cpu, freq);
			cpufreq_driver_target(policy, freq, relation);
			cpufreq_cpu_put(policy);
		} else {
			pr_err("Failed to get CPU %d policy\n", cpu);
		}
	}
}

static void cpus_freq_restore(struct this_platform_context *pf_ctx)
{
	int cpu;
	struct cpufreq_policy *policy;
	unsigned int freq, relation;

	for_each_online_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (policy) {
			freq = pf_ctx->cpus_freq[cpu];
			cpufreq_driver_target(policy, freq, relation);
			cpufreq_cpu_put(policy);
		} else {
			pr_err("Failed to get CPU %d policy\n", cpu);
		}
	}
}
#endif

#ifdef DMA_CONFIG
static void dma_channels_config(void)
{
#if 0
    struct dma_chan chan;
    chan.local = NULL;

    /* A dummy call to trigger dma_channel_rebalance(). */
    /* Only works for >= v6.6 */
    dma_async_device_channel_unregister(NULL, &chan);
#else
	/* Dummy call to trigger dma_channel_rebalance(). */
	/* Not guaranteed due to (dmaengine_ref_count == 1) checks. */
	dmaengine_get();
	dmaengine_put();
#endif
}
#endif

static int irq_disable(authon_platform_context *ctx)
{
	struct this_platform_context *pf_ctx =
		(struct this_platform_context *)ctx;

	pf_ctx->irq_semaphore++;

	if (pf_ctx->irq_semaphore > 1) {
		return 0;
	}

#ifdef CONFIG_PREEMPT_COUNT
	if (preempt_count() > 0) {
		pr_warn("Double preemption disable occurred.\n");
	}
#endif
	if (irqs_disabled()) {
		pr_warn("Double irq disable occurred.\n");
	}

	//pr_info("irq disable occurred.\n");

#ifdef USE_YIELD
	yield();
#endif

#ifdef DMA_CONFIG
	dma_channels_config();
#endif

#ifdef CPUS_FREQ_CONFIG
	cpus_freq_set(pf_ctx);
#endif

#ifdef USE_SPINLOCK
	spin_lock_irqsave(&pf_ctx->spinlock, pf_ctx->irq_flags);
#else
	local_irq_save(pf_ctx->irq_flags);
	preempt_disable();
#endif

	return 0;
}

static int irq_restore(authon_platform_context *ctx)
{
	struct this_platform_context *pf_ctx =
		(struct this_platform_context *)ctx;

	if (pf_ctx->irq_semaphore == 0 || --pf_ctx->irq_semaphore > 0) {
		return 0;
	}

#ifdef CONFIG_PREEMPT_COUNT
	if (preempt_count() == 0) {
		pr_warn("Double preemption enable occurred.\n");
	}
#endif
	if (!irqs_disabled()) {
		pr_warn("Double irq restore occurred.\n");
	}

#ifdef USE_SPINLOCK
	spin_unlock_irqrestore(&pf_ctx->spinlock, pf_ctx->irq_flags);
#else
	local_irq_restore(pf_ctx->irq_flags);
	preempt_enable();
#endif

#ifdef CPUS_FREQ_CONFIG
	cpus_freq_restore(pf_ctx);
#endif

#ifdef USE_YIELD
	yield();
#endif

	//pr_info("irq restore occurred.\n");

	return 0;
}

static int delay_us(authon_platform_context *ctx, uint32_t us)
{
	udelay(us);
	return 0;
}

static int delay_ms(authon_platform_context *ctx, uint32_t ms)
{
	mdelay(ms);
	return 0;
}

static uint64_t tick_ns(authon_platform_context *ctx)
{
	uint64_t tick;
#ifdef USE_TIMER_CNTVCT
	tick = cntvct_to_ns();
#else
	tick = ktime_get_ns();
#endif
	rmb();
	return tick;
}

void print(authon_platform_context *ctx, const char *fmt, ...)
{
}

void print_cont(authon_platform_context *ctx, const char *fmt, ...)
{
}

struct gpio_desc *g_board_gpio;
static int dt_parse(struct authon_chip_context *chip_ctx)
{
	int rc;
	const char *compatible;
	const char *power_mode;
	const struct device_node *node = chip_ctx->dev.parent->of_node;
	const char *node_name = of_node_full_name(
		node); /* "<no-node>" is returned if the node is not found. */
	struct this_platform_context *pf_ctx;
	struct swi_context *swi_ctx;
#ifdef CPUS_FREQ_CONFIG
	unsigned int cpus;
#endif
#ifdef USE_MMIO_RW
	struct device_node *aliases, *gpio_node;
	const char *gpio_alias;
	struct resource res;
#endif

	pf_ctx = kzalloc(sizeof(*pf_ctx), GFP_KERNEL);
	if (!pf_ctx) {
		pr_err("kzalloc has failed.\n");
		rc = -ENOMEM;
		goto out;
	}

#ifdef CPUS_FREQ_CONFIG
	cpus = num_online_cpus();
	pr_info("Number of online cpu: %d\n", cpus);

	pf_ctx->cpus_freq = kzalloc(sizeof(unsigned int) * cpus, GFP_KERNEL);
	if (!pf_ctx->cpus_freq) {
		pr_err("kzalloc has failed.\n");
		rc = -ENOMEM;
		goto out_free_pf_ctx;
	}
#endif

#ifdef USE_SPINLOCK
	spin_lock_init(&pf_ctx->spinlock);
#endif

	/* 1. Initialize AuthOn Platform Context (assign platform apis). */

	pf_ctx->common.gpio_read = gpio_read;
	pf_ctx->common.gpio_write = gpio_write;
	pf_ctx->common.gpio_conf = gpio_conf;
	pf_ctx->common.irq_disable = irq_disable;
	pf_ctx->common.irq_restore = irq_restore;
	pf_ctx->common.delay_us = delay_us;
	pf_ctx->common.delay_ms = delay_ms;
	pf_ctx->common.tick_ns = tick_ns;
	pf_ctx->common.alt_rng = NULL;
	pf_ctx->common.print = print;
	pf_ctx->common.print_cont = print_cont;

	authon_set_platform(&chip_ctx->on_ctx, pf_ctx);
	swi_ctx = &chip_ctx->on_ctx.swi_ctx;

	/* 2. Initialize SWI parameters. */

	rc = of_property_read_string(node, "compatible", &compatible);
	if (rc) {
		pr_err("Compatible property not found.\n");
		goto out_free_cpus_freq;
	} else {
		pr_info("Full name of the discovered device tree node: %s.\n",
			node_name);
		pr_info("Compatible property: %s.\n", compatible);
	}

	rc = of_property_read_string(node, "power-mode", &power_mode);
	if (rc) {
		pr_err("power-mode property not found.\n");
		goto out_free_cpus_freq;
	} else {
		if (!strcmp(power_mode, "direct")) {
			swi_ctx->power_down_delay_time = 2000;
			swi_ctx->power_up_delay_time = 8000;
		} else {
			swi_ctx->power_down_delay_time = 2500;
			swi_ctx->power_up_delay_time = 10000;
		}
	}

	rc = of_property_read_u32(node, "baud-low-value", &swi_ctx->baud_low);
	if (rc) {
		pr_err("baud-low-value property not found.\n");
		goto out_free_cpus_freq;
	} else {
		/* For indirect power mode, it is recommended to use a Tau value above 3 µs. */
		if (!strcmp(power_mode, "indirect") &&
		    (swi_ctx->baud_low < 3)) {
			pr_warn("baud-low-value: %d us (the recommended value should be larger than %d us).\n",
				swi_ctx->baud_low, 3);
		}

		if (swi_ctx->baud_low > MAX_1TAU) {
			pr_warn("baud-low-value: %d us (the recommended value should be less than %d us).\n",
				swi_ctx->baud_low, MAX_1TAU);
		} else {
			pr_info("baud-low-value: %d us.\n", swi_ctx->baud_low);
		}
	}

	rc = of_property_read_u32(node, "baud-high-value", &swi_ctx->baud_high);
	if (rc) {
		pr_err("baud-high-value property not found.\n");
		goto out_free_cpus_freq;
	} else {
		if (swi_ctx->baud_high > MAX_3TAU) {
			pr_warn("baud-high-value: %d us (the recommended value should be less than %d us).\n",
				swi_ctx->baud_high, MAX_3TAU);
		} else {
			pr_info("baud-high-value: %d us.\n",
				swi_ctx->baud_high);
		}
	}

	rc = of_property_read_u32(node, "baud-stop-value", &swi_ctx->baud_stop);
	if (rc) {
		pr_err("baud-stop-value property not found.\n");
		goto out_free_cpus_freq;
	} else {
		if (swi_ctx->baud_stop > MAX_5TAU) {
			pr_warn("baud-stop-value: %d us (the recommended value should be less than %d us).\n",
				swi_ctx->baud_stop, MAX_5TAU);
		} else {
			pr_info("baud-stop-value: %d us.\n",
				swi_ctx->baud_stop);
		}
	}

	/**
     * Worst case bus timeout 1: time 90 x 9 uS = 810 uS
     * Worst case bus timeout 2: time 10 x 153 uS = 1530 uS
     */
	rc = of_property_read_u32(node, "response-timeout-value",
				  &swi_ctx->response_timeout);
	if (rc) {
		pr_err("response-timeout-value property not found.\n");
		goto out_free_cpus_freq;
	} else {
		if (swi_ctx->response_timeout > MAX_10TAU) {
			pr_warn("response-timeout-value: %d us (the recommended value should be less than %d us).\n",
				swi_ctx->response_timeout, MAX_10TAU);
		} else {
			pr_info("response-timeout-value: %d us.\n",
				swi_ctx->response_timeout);
		}
	}

	pf_ctx->boardver_gpios_not_support = of_property_read_bool(node, "boardver-gpios-not-support");
	if (pf_ctx->boardver_gpios_not_support) {
		pr_info("board gpio not support\n");
	}

	pf_ctx->gpio_swi =
		devm_gpiod_get(chip_ctx->dev.parent, "swi", GPIOD_OUT_LOW);
	if (IS_ERR(pf_ctx->gpio_swi)) {
		pr_err("gpio-swi not found.\n");
		rc = -ENODEV;
		goto out_free_cpus_freq;
	}
	pf_ctx->gpiobase = gpiod_to_chip(pf_ctx->gpio_swi)->base;
	pr_err("gpio-swi, base %d\n", pf_ctx->gpiobase);
	chip_ctx->chip = 1;

	//pf_ctx->gpio_debug =
	//	gpiod_get(chip_ctx->dev.parent, "debug", GPIOD_OUT_HIGH);
	//if (IS_ERR(pf_ctx->gpio_debug)) {
	//	pr_err("gpio-debug not found.\n");
	//	rc = -ENODEV;
	//	goto out_gpiod_put_swi;
	//}

	pf_ctx->board_gpio = devm_gpiod_get(chip_ctx->dev.parent, "boardver", GPIOD_IN);
	if (IS_ERR_OR_NULL(pf_ctx->board_gpio)) {
		pr_err("board gpio not find\n");
	} else  {
		g_board_gpio = pf_ctx->board_gpio;
		//gpiod_direction_input(pf_ctx->board_gpio);
		pr_err("board gpio val pull= %d\n", gpiod_get_value(pf_ctx->board_gpio));
	}
#ifdef USE_MMIO_RW
	/* For establishing direct access to GPIO MMIO. */

	aliases = of_find_node_by_path("/aliases");
	if (!aliases) {
		pr_err("Failed to find /aliases node\n");
		rc = -ENODEV;
		goto out_gpiod_put_debug;
	}

	if (of_property_read_string(aliases, "gpio", &gpio_alias)) {
		pr_err("Failed to read 'gpio' property\n");
		rc = -EINVAL;
		goto out_of_put_aliases;
	}

	gpio_node = of_find_node_by_path(gpio_alias);
	if (!gpio_node) {
		pr_err("Failed to find GPIO node: %s\n", gpio_alias);
		rc = -ENODEV;
		goto out_of_put_aliases;
	}

	// Parse the address of the GPIO node
	rc = of_address_to_resource(gpio_node, 0, &res);
	if (rc) {
		pr_err("Failed to parse address for GPIO node\n");
		rc = -ENODEV;
		goto out_of_put_gpio;
	}

	pr_info("GPIO node address: start=0x%lx, end=0x%lx, size=0x%lx\n",
		(unsigned long)res.start, (unsigned long)res.end,
		(unsigned long)resource_size(&res));

	pf_ctx->mmio = ioremap(res.start, resource_size(&res));
	if (!pf_ctx->mmio) {
		pr_err("Failed to remap GPIO memory\n");
		rc = -ENOMEM;
		goto out_of_put_gpio;
	}

	of_node_put(gpio_node);
	of_node_put(aliases);

	return 0;
out_of_put_gpio:
	of_node_put(gpio_node);
out_of_put_aliases:
	of_node_put(aliases);
out_gpiod_put_debug:
	gpiod_put(pf_ctx->gpio_debug);
#else
	return 0;
#endif
//out_gpiod_put_swi:
	gpiod_put(pf_ctx->gpio_swi);
out_free_cpus_freq:
#ifdef CPUS_FREQ_CONFIG
	kfree(pf_ctx->cpus_freq);
out_free_pf_ctx:
#endif
	kfree(pf_ctx);
out:
	return rc;
}

static void dt_clean(const struct authon_chip_context *chip_ctx)
{
	struct this_platform_context *pf_ctx =
		(struct this_platform_context *)chip_ctx->on_ctx.swi_ctx.pf_ctx;

#ifdef USE_MMIO_RW
	iounmap(pf_ctx->mmio);
#endif
	gpiod_put(pf_ctx->gpio_swi);
	gpiod_put(pf_ctx->gpio_debug);
#ifdef CPUS_FREQ_CONFIG
	kfree(pf_ctx->cpus_freq);
#endif
	kfree(pf_ctx);
}

static void dev_release(struct device *dev)
{
	/* struct authon_chip_context *chip_ctx = container_of(dev, struct authon_chip_context, dev); */

	/* The release is managed by authon_platform_driver_remove(). Leave this empty for the time being. */
}

static uint16_t authon_test(struct authon_context *on_ctx)
{
	int retry = MAX_RETRY;
	int i = 0;
	uint16_t device_address;
	uint16_t ret;

	while (retry) {
		ret = authon_init_sdk(on_ctx);
		if (ret != SDK_SUCCESS) {
			pr_err("Error: failed to initialize SDK ret=0x%x retry:%d",
			       ret, retry);
		} else {
			if (on_ctx->enumeration.device_found != 0) {
				pr_info("Found %d device(s) on the bus\n",
					on_ctx->enumeration.device_found);

				for (i = 0;
				     i < on_ctx->enumeration.device_found;
				     i++) {
					print_uid((uint8_t *)&(
						on_ctx->cap.uid[i][0]));
				}

				ret = authon_get_swi_address(on_ctx,
							     &device_address);
				if (ret == SDK_SUCCESS) {
					pr_info("Device address=0x%x\n",
						device_address);
				} else {
					pr_err("Error: unable to read device address ret=0x%x\n",
					       ret);
				}

				return SDK_SUCCESS;
			} else {
				pr_err("Error: Device not found retry_left:%d\n",
				       retry);
			}
		}

		memset(&on_ctx->enumeration, 0, sizeof(on_ctx->enumeration));
		memset(&on_ctx->cap, 0, sizeof(on_ctx->cap));
		retry--;
	}
	return SDK_INIT;
}

static int fb_notifier_callback(struct notifier_block *nb,
	unsigned long event, void *v)
{
	struct authon_chip_context *chip_ctx = container_of(nb, struct authon_chip_context, fb_notifier);
	static int last_st;

	switch (event) {
	case TRAN_DEV_NOTIFY_SCREEN_ON:
		chip_ctx->lcd_on = 1;
		break;
	case TRAN_DEV_NOTIFY_SCREEN_OFF:
		chip_ctx->lcd_on = 0;
		break;
	default:
		pr_err("Unknown event:%lu\n", event);
		break;
	}

	if (chip_ctx->lcd_on != last_st)
		schedule_delayed_work(&chip_ctx->update_work, msecs_to_jiffies(1000));
	last_st = chip_ctx->lcd_on;

	return 0;
}

static bool get_bat_authent_support(struct authon_chip_context *chip_ctx)
{
	struct this_platform_context *pf_ctx = NULL;
	int val;

	if (IS_ERR_OR_NULL(chip_ctx))
		return false;

	pf_ctx = (struct this_platform_context *)chip_ctx->on_ctx.swi_ctx.pf_ctx;

	if (IS_ERR_OR_NULL(pf_ctx))
		return false;

	if (pf_ctx->boardver_gpios_not_support) {
		pr_info("boardver gpio not support\n");
		board_support = true;
		return true;
	}

	if (IS_ERR_OR_NULL(pf_ctx->board_gpio))
		return false;

	val = gpiod_get_value(pf_ctx->board_gpio);
	pr_err("show board gpio val = %d\n", val);
	if (val == 0) {
		board_support = true;
		return true;
	}

	return false;
}

static int tran_send_up_ic_cycle_count(struct authon_chip_context *chip_ctx, struct tc_charger *info)
{
	char buf[64];
	char *env[2] = { NULL, NULL };
	int bat_cycle = 0;

	bat_cycle = tc_get_battery_raw_cycle();
	if (chip_ctx->ic_test_cycle > 0 && chip_ctx->ic_test_cycle < 2000)
		snprintf(buf, sizeof(buf),"BAT_CYCLE_IC:%d", chip_ctx->ic_test_cycle);
	else
		snprintf(buf, sizeof(buf),"BAT_CYCLE_IC:%d", bat_cycle);
	pr_info("authon update event\n");
	env[0] = buf;
	kobject_uevent_env(&info->pdev->dev.kobj, KOBJ_CHANGE, env);

	return 0;
}

static ssize_t test_bat_cycle_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct authon_chip_context *chip_ctx = dev->driver_data;
	return sprintf(buf, "%d\n", chip_ctx->ic_test_cycle);
}
static ssize_t test_bat_cycle_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct authon_chip_context *chip_ctx = dev->driver_data;
	int test = 0;

	if (kstrtoint(buf, 10, &test) == 0)
		chip_ctx->ic_test_cycle = test;
	else
		chip_ctx->ic_test_cycle = 0;
	pr_info("ic_test_cycle = %d\n", chip_ctx->ic_test_cycle);
	return size;
}
static DEVICE_ATTR(test_bat_cycle, 0664, test_bat_cycle_show, test_bat_cycle_store);

#define TEMP_H 50
#define VBAT_L 3600
static void do_update_work(struct work_struct *work)
{
	struct authon_chip_context *chip_ctx = container_of(to_delayed_work(work),
				struct authon_chip_context, update_work);
	union power_supply_propval prop = {0};
	struct tran_device * tc_charger_dev = NULL;
	struct tc_charger *info = NULL;
	int ret;
	int temp = 0, vol = 0;

	pr_info("update work begin lcd_on = %d\n", chip_ctx->lcd_on);
	if (IS_ERR_OR_NULL(chip_ctx->chg_psy)) {
		chip_ctx->chg_psy = power_supply_get_by_name("charger");
		if (IS_ERR_OR_NULL(chip_ctx->chg_psy)) {
			pr_err("get charger_psy failed\n");
			return;
		}
	}
	ret = power_supply_get_property(chip_ctx->chg_psy,
		POWER_SUPPLY_PROP_ONLINE, &prop);
	if(!prop.intval) {
		chip_ctx->update_fg = false;
		pr_info("chg out\n");
		return;
	}

	if (chip_ctx->lcd_on)
		return;

	tc_charger_dev = tran_get_by_name("tc_charger");
	if (IS_ERR_OR_NULL(tc_charger_dev)) {
		pr_err("can't find tc_charger_dev ***\n");
		return;
	}

	vol = tc_get_battery_voltage();
	pr_info("%s val = %d\n", __func__, vol);
	if (vol < VBAT_L) {
		goto wait;
	}

	temp = tc_get_tpcb_temp();
	if (temp > TEMP_H) {
		goto wait;
	}

	if (chip_ctx->update_fg)
		return;

	pr_info("update work\n");
	info = tran_get_data(tc_charger_dev);
	if (IS_ERR_OR_NULL(info)) {
		pr_err("failed to get tc_charger info\n");
		return;
	}
	tran_send_up_ic_cycle_count(chip_ctx, info);
	chip_ctx->update_fg = true;
	return;

wait:
	pr_info("update work wait condition:temp=%d,vol=%d\n", temp, vol);
	schedule_delayed_work(&chip_ctx->update_work, msecs_to_jiffies(10000));
}

static enum power_supply_property ctx_psy_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
};

static int ctx_prop_is_writeable(struct power_supply *psy,
				       enum power_supply_property prop)
{
	return 0;
}

static int ctx_get_property(struct power_supply *psy, enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct authon_chip_context *chip_ctx = power_supply_get_drvdata(psy);
	int ret = 0;

	if (IS_ERR_OR_NULL(chip_ctx))
		return -EINVAL;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = board_support;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int ctx_set_property(struct power_supply *psy,
			       enum power_supply_property prop,
			       const union power_supply_propval *val)
{
	return 0;
}

static void do_test_work(struct work_struct *work)
{
	struct authon_chip_context *chip_ctx = container_of(work,
				struct authon_chip_context, test_work);

	if (IS_ERR_OR_NULL(chip_ctx))
		return;

	msleep(3000);
	pr_err("Device hard test suc.\n");
}

static int authon_platform_driver_probe(struct platform_device *pdev)
{
	struct authon_chip_context *chip_ctx;
	char unique_name[256];
	struct pinctrl *swi_dev = NULL;
	struct pinctrl_state *swi_8mA = NULL;
	struct pinctrl_state *boardver_pull = NULL;
	struct this_platform_context *pf_ctx = NULL;
	int rc;

	pr_err("%s\n", __func__);
	chip_ctx = kzalloc(sizeof(*chip_ctx), GFP_KERNEL);
	if (!chip_ctx) {
		pr_err("kzalloc has failed.\n");
		rc = -ENOMEM;
		goto out;
	}

	/* Allocate an unused ID and associate it with the chip pointer. */
	mutex_lock(&idr_lock);
	rc = idr_alloc(&dev_nums_idr, chip_ctx, 0, AUTHON_NUM_DEVICES,
		       GFP_KERNEL);
	mutex_unlock(&idr_lock);
	if (rc < 0) {
		pr_err("Failed to allocate an unused ID. Reached the maximum allowed devices (%d)\n",
		       AUTHON_NUM_DEVICES);
		goto out_free_chip;
	}
	chip_ctx->dev_num = rc;

	/* Create an unique name. */
	sprintf(unique_name, "%s%d", DEVICE_NAME, chip_ctx->dev_num);

	/* Initialize a cdev structure. */
	cdev_init(&chip_ctx->cdev, &fops);
	chip_ctx->cdev.owner = THIS_MODULE;

	/* Initialize a device structure. */
	device_initialize(&chip_ctx->dev);

	/* Set associated device tree node. */
	chip_ctx->dev.of_node = pdev->dev.of_node;

	/* Set the class structure. */
	chip_ctx->dev.class = authon_class;

	/* Set parent */
	chip_ctx->dev.parent = &pdev->dev;

	/* Set the release function to be invoked when put_device is called. */
	chip_ctx->dev.release = dev_release;

	/* Set attribute groups (in sysfs). */
	/* chip_ctx->dev.groups = attr_groups; */

	/* Set the major and minor number. */
	chip_ctx->dev.devt = MKDEV(MAJOR(authon_devt), chip_ctx->dev_num);

	/* Set the device file name. */
	rc = dev_set_name(&chip_ctx->dev,"%s",unique_name);
	if (rc) {
		pr_err("Failed to set the device file name.\n");
		goto out_put_device;
	}

	/* Create a character device and its associated device file. */
	rc = cdev_device_add(&chip_ctx->cdev, &chip_ctx->dev);
	if (rc) {
		pr_err("Failed to create a character device and its associated device file.\n");
		goto out_put_device;
	} else {
		pr_info("Created char device: %s, major %d, minor %d.\n",
			dev_name(&chip_ctx->dev), MAJOR(chip_ctx->dev.devt),
			MINOR(chip_ctx->dev.devt));
	}

	swi_dev = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR_OR_NULL(swi_dev)) {
		pr_err("devm_pinctrl_get error.\n");
	} else {
		swi_8mA = pinctrl_lookup_state(swi_dev, "opt_authon_8mA");
		if (IS_ERR_OR_NULL(swi_8mA)) {
			pr_err("pinctrl_lookup_state error.\n");
		} else {
			rc = pinctrl_select_state(swi_dev, swi_8mA);
			if (rc < 0) {
				pr_err("pinctrl_select_state error. %d\n", rc);
			}
		}
		boardver_pull = pinctrl_lookup_state(swi_dev, "boardver_pull");
		if (IS_ERR_OR_NULL(boardver_pull)) {
			pr_err("pinctrl_lookup_state error.\n");
		} else {
			rc = pinctrl_select_state(swi_dev, boardver_pull);
			if (rc < 0) {
				pr_err("pinctrl_select_state error. %d\n", rc);
			}
		}
	}

	/* Device tree scanning. */
	rc = dt_parse(chip_ctx);
	if (rc) {
		pr_err("Failed to parse the device tree node.\n");
		goto out_cdev_device_del;
	}
	
	pf_ctx = (struct this_platform_context *)chip_ctx->on_ctx.swi_ctx.pf_ctx;
	
	if (get_bat_authent_support(chip_ctx)) {
		if (authon_test(&chip_ctx->on_ctx) != SDK_SUCCESS) {
			pr_err("Device testing has failed.\n");
			goto out_cdev_device_del;
		}
	} else {
		pr_err("no support board\n");
		goto out_cdev_device_del;
	}

	chip_ctx->tc_lcd = tran_get_by_name("tc_lcd");
	if (!IS_ERR_OR_NULL(chip_ctx->tc_lcd)) {
		chip_ctx->fb_notifier.notifier_call = fb_notifier_callback;
		rc = register_tran_device_notifier(chip_ctx->tc_lcd, &chip_ctx->fb_notifier);
		if (rc < 0) {
			pr_err("Failed to register fb notifier\n");
		} else
			pr_err("suc to register fb notifier\n");
	}

	chip_ctx->psy_desc.name = "ctx_psy";
	chip_ctx->psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
	chip_ctx->psy_desc.properties = ctx_psy_props;
	chip_ctx->psy_desc.num_properties = ARRAY_SIZE(ctx_psy_props);
	chip_ctx->psy_desc.get_property = ctx_get_property;
	chip_ctx->psy_desc.set_property = ctx_set_property;
	chip_ctx->psy_desc.property_is_writeable = ctx_prop_is_writeable;
	chip_ctx->psy_cfg.drv_data = chip_ctx;
	chip_ctx->ctx_psy = devm_power_supply_register(&chip_ctx->dev,
						&chip_ctx->psy_desc,
						&chip_ctx->psy_cfg);

	INIT_WORK(&chip_ctx->test_work, do_test_work);
	INIT_DELAYED_WORK(&chip_ctx->update_work, do_update_work);
	schedule_work(&chip_ctx->test_work);

	dev_set_drvdata(&(chip_ctx->dev), chip_ctx);
	rc = device_create_file(&(chip_ctx->dev), &dev_attr_test_bat_cycle);

	pr_err("%s suc\n", __func__);
	return 0;

out_cdev_device_del:
	if ((!IS_ERR_OR_NULL(pf_ctx)) && (!IS_ERR_OR_NULL(pf_ctx->board_gpio)))
		gpiod_put(pf_ctx->board_gpio);
	cdev_device_del(&chip_ctx->cdev, &chip_ctx->dev);
out_put_device:
	put_device(&chip_ctx->dev);
	mutex_lock(&idr_lock);
	idr_remove(&dev_nums_idr, chip_ctx->dev_num);
	mutex_unlock(&idr_lock);
out_free_chip:
	kfree(chip_ctx);
out:
	return rc;
}

static int authon_platform_driver_remove(struct platform_device *pdev)
{
	struct authon_chip_context *chip_ctx;
	int chip_num;
	const char *node_name = of_node_full_name(
		pdev->dev.of_node); /* "<no-node>" is returned if the node is not found. */

	/* Search for the chip context and remove it from the id-radix. */
	mutex_lock(&idr_lock);

	idr_for_each_entry(&dev_nums_idr, chip_ctx, chip_num) {
		if (chip_ctx &&
		    of_node_name_eq(chip_ctx->dev.of_node, node_name)) {
			idr_remove(&dev_nums_idr, chip_ctx->dev_num);
			cdev_device_del(&chip_ctx->cdev, &chip_ctx->dev);
			dt_clean(chip_ctx);
			put_device(&chip_ctx->dev);
			kfree(chip_ctx);

			break;
		}
	}

	mutex_unlock(&idr_lock);

	return 0;
}

static struct of_device_id of_authon_gpio_match[] = {
	{
		.compatible = "optiga_authon,optiga_authon_dev",
	},
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_authon_gpio_match);

static struct platform_driver authon_platform_driver = {
    .driver = {
        .name = "authon_device_driver",
        .of_match_table = of_authon_gpio_match,
    },
    .probe = authon_platform_driver_probe,
    .remove = authon_platform_driver_remove,
};

static int __init mod_init(void)
{
	int rc;
	pr_err("%s suc\n", __func__);
	/* Allocate a major number and register a range of char device numbers */
	rc = alloc_chrdev_region(&authon_devt, 0, AUTHON_NUM_DEVICES,
				 DEVICE_NAME);
	if (rc < 0) {
		pr_err("Failed to allocate major number.\n");
		goto out;
	}

	/* Initialize a class structure. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	authon_class = class_create(CLASS_NAME);
#else
	authon_class = class_create(THIS_MODULE, CLASS_NAME);
#endif
	if (IS_ERR(authon_class)) {
		pr_err("Couldn't create device class.\n");
		rc = PTR_ERR(authon_class);
		goto out_unreg_chrdev;
	}

	rc = platform_driver_register(&authon_platform_driver);
	if (rc) {
		goto out_class_destroy;
	}

	/* Initialize job_list */

	mutex_init(&job_list_lock);

#ifndef USE_SYSTEM_WQ
	/* Initialize a local workqueue */
#if SCHEDULING_POLICY == 0
	local_wq = alloc_workqueue("authon_wq", 0, 1);
#elif SCHEDULING_POLICY == 1
	local_wq = alloc_workqueue("authon_wq", 0, 0);
#elif SCHEDULING_POLICY == 2
	local_wq =
		alloc_workqueue("authon_wq", WQ_UNBOUND, WQ_UNBOUND_MAX_ACTIVE);
#else /* SCHEDULING_POLICY */
#error "Invalid scheduling policy."
#endif /* SCHEDULING_POLICY */
	if (!local_wq) {
		rc = -ENOMEM;
		goto out_class_destroy;
	}
#endif /* USE_SYSTEM_WQ */

	return 0;
out_class_destroy:
	class_destroy(authon_class);
out_unreg_chrdev:
	unregister_chrdev_region(authon_devt, AUTHON_NUM_DEVICES);
out:
	return rc;
}

static void __exit mod_exit(void)
{
	struct job_context *job_cur, *job_next;

	mutex_lock(&job_list_lock);
	is_mod_exit = true;
	list_for_each_entry_safe(job_cur, job_next, &job_list, list) {
		cancel_work_sync(&job_cur->work);
		list_del_init(&job_cur->list);
	}
	mutex_unlock(&job_list_lock);
	mutex_destroy(&job_list_lock);

#ifndef USE_SYSTEM_WQ
	destroy_workqueue(local_wq);
#endif

	platform_driver_unregister(&authon_platform_driver);
	unregister_chrdev_region(authon_devt, AUTHON_NUM_DEVICES);
	class_destroy(authon_class);
}

module_init(mod_init);
module_exit(mod_exit);

/* Driver meta information */
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("OPTIGA™ Authenticate On");
MODULE_AUTHOR("Infineon Technologies AG");
MODULE_VERSION("20240807");
