#ifndef __TRAN_HR_SPO2_H__
#define __TRAN_HR_SPO2_H__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>

#include "hf_manager.h"


#define TRAN_INFO(fmt, args...) do { \
	printk(KERN_INFO"[TRAN_HR_SPO2]%s %d:"fmt, __func__, __LINE__, ##args); \
} while (0)

#define TRAN_ERROR(fmt, args...) do { \
	printk(KERN_ERR "[TRAN_HR_SPO2]%s %d:"fmt, __func__, __LINE__, ##args); \
} while (0)

struct tran_hr_spo2_data {
	uint8_t spo2_result;
	uint8_t hr_result;
	uint8_t hrs_wear_status;
	uint8_t spo2_living_status;
	uint8_t spo2_alg_status;
	uint8_t motion;
};

struct tran_cust_hr_spo2_data{
	uint8_t cust_enable;
	uint8_t spo2_result;
	uint8_t hr_result;
	uint8_t hrs_wear_status;
	uint8_t spo2_living_status;
	uint8_t spo2_alg_status;
	uint8_t motion;
};

struct tran_hr_spo2_ops {
	void (*tran_hr_enable)(int enable);
	void (*tran_spo2_enable)(int enable);
	void (*tran_hr_spo2_data_get)(struct tran_hr_spo2_data *data);
	void (*tran_cust_data_get)(struct tran_cust_hr_spo2_data *data);
};

struct tran_hrs_controller {
	struct tran_hr_spo2_ops *ops;
	struct tran_hr_spo2_data *data;
	struct tran_cust_hr_spo2_data *cust_data;
};
#if 0
int register_hr_spo2_controller(struct tran_hrs_controller *controller);
int tran_spo2_register(void);
int tran_spo2_destroy(void);
int tran_hr_register(void);
int tran_hr_destroy(void);
#endif
#endif

