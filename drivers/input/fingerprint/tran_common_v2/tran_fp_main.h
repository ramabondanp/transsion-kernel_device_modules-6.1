#ifndef __TRAN_FP_MAIN_H__
#define __TRAN_FP_MAIN_H__

#include <linux/types.h>
#include <linux/spi/spi.h>
#include <linux/input.h>
#include <linux/miscdevice.h>
#include <net/sock.h>
#include <linux/netlink.h>

struct tran_ioc_chip_info {
	u8 vendor_id;
	u8 mode;
	u8 operation;
	char icname[10];
	u8 reserved[5];
};

typedef enum g_fp_name {
	SUNWAVE,
	FORTSENSE,
	CHIPONE,
	GOODIX,
	SILEAD,
	CDFINGER,
	OXI,
	FPC,
	FOCALTECH,
	EGIS,
	TRAN_FP_NAME_MAX
} g_fp_name_e;

#define TRAN_FP_RESET_HIGH "tran_fp_reset_high"
#define TRAN_FP_RESET_LOW "tran_fp_reset_low"
#define TRAN_FP_IRQ "tran_fp_pin_irq"
#define TRAN_FP_SPI_DEFAULT "tran_fp_spi_default"
#define TRAN_FP_SPI_GPIO "tran_fp_spi_gpio"

#define TRAN_NETLINK_ROUTE 30
#define TRAN_BIO_NETLINK_ROUTE 29
#define TRAN_NL_MSG_LEN            16

#include "tran_fp_common_data_def.h"

#define TRAN_FP_VDD_MIN_UV	2600000
#define TRAN_FP_VDD_MAX_UV	3300000
#define TRAN_FP_VIO_MIN_UV	1750000
#define TRAN_FP_VIO_MAX_UV	1950000

#define TRAN_LOG_TAG "TRAN_CODE"

#define TRAN_FP_FUNC_ENTER() printk("[Tran_FP]%s: Enter\n", __func__)
#define TRAN_FP_FUNC_EXIT()  printk("[Tran_FP]%s: Exit(%d)\n", __func__, __LINE__)

#define TRAN_INT_NAME "tran_int"
#define TRAN_DRV_VERSION_LEN 32

struct tran_ioc_transfer {
	u8 cmd;    /* spi read = 0, spi  write = 1 */
	u8 reserved;
	u16 addr;
	u32 len;
	u8 *buf;
};

int tran_init_eint(struct tran_fp_data*tran_fp_dev);
void tran_enable_irq(struct tran_fp_data *tran_fp_dev);
void tran_disable_irq(struct tran_fp_data *tran_fp_dev);
int tran_register_base_ops(struct tran_fp_data *tran_fp_datap);
int tran_fp_report_key_nav_event(struct input_dev *input, tran_key_nav_t *kevent);
int gf_key_nav_adjust_type_value(tran_key_nav_event_t *key_nav_p, unsigned int cmd);
int sf_key_nav_adjust_type_value(tran_key_nav_event_t *key_nav_p, unsigned int cmd);
void fpsensor_dev_cleanup(struct tran_fp_data *fpsensor);
int tran_fp_parse_dts_file(struct tran_fp_data *tran_fp_dev);
int tran_fp_input_init(void);
int tran_fp_irq_init(void);
int tran_netlink_init(struct tran_fp_data *tran_fp_datap);
int tran_netlink_bio_init(struct tran_fp_data *tran_fp_datap);
int tran_netlink_destroy(struct tran_fp_data *tran_fp_datap);
int tran_netlink_bio_destroy(struct tran_fp_data *tran_fp_datap);
void tran_netlink_send(struct tran_fp_data *tran_fp_datap, const int cmd);
void tran_netlink_bio_send(struct tran_fp_data *tran_fp_datap, const int cmd);
void tran_netlink_recv(struct sk_buff *__skb);
void tran_register_board_info(void);
int tran_fp_get_power_val(struct tran_fp_data *tran_fp_dev,PWR_VAL_U * cur_val);
void tran_fp_reset_gpio_high(struct tran_fp_data *tran_fp_dev);
void tran_fp_reset_gpio_low(struct tran_fp_data *tran_fp_dev);
void tran_fasync_for_lcm_full_HBM(void);
void setVendorName(void);


#include "tran_fp_macro_transfer.h"
#endif /* __TRAN_FP_MAIN_H__ */
