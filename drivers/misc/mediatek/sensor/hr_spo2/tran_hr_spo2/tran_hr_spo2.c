#include "tran_hr_spo2.h"

struct hrs_device {
	struct hf_device hf_dev;
};
struct tran_hrs_controller *core_controller;

struct hrs_device hr_device;
struct hrs_device spo2_device;

static struct sensor_info support_sensors1[] = {
	{
		.sensor_type = SENSOR_TYPE_HEART_RATE,
		.gain = 1,
		.name = "heart_rate",
		.vendor = "mtk",
	},
};

static struct sensor_info support_sensors2[] = {
	{
		.sensor_type = SENSOR_TYPE_OXIMETRY,
		.gain = 1,
		.name = "oximetry",
		.vendor = "mtk",
	},
};
int register_hr_spo2_controller(struct tran_hrs_controller *controller)
{
	TRAN_INFO("register start \n");
	if(!IS_ERR_OR_NULL(controller)){
		core_controller = controller;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(register_hr_spo2_controller);

/* Heart Rate start*/
static int hr_enable(struct hf_device *hfdev, int sensor_type, int en)
{
	TRAN_INFO("%s sensor_type:%d val:%d\n", __func__, sensor_type, en);
	if(IS_ERR_OR_NULL(core_controller)){
		return -EINVAL;
	}
	if (sensor_type == SENSOR_TYPE_HEART_RATE && !IS_ERR_OR_NULL(core_controller->ops->tran_hr_enable)) {
		core_controller->ops->tran_hr_enable(en);
	} else {
		return -EINVAL;
	}
	return 0;
}

static int hr_batch(struct hf_device *hfdev, int sensor_type,
		int64_t delay, int64_t latency)
{
	TRAN_INFO("%s id:%d delay:%lld latency:%lld\n", __func__, sensor_type, delay, latency);
	return 0;
}
static int hr_flush(struct hf_device *hfdev, int sensor_type)
{
	struct hrs_device *driver_dev = hf_device_get_private_data(hfdev);
	struct hf_manager *manager = driver_dev->hf_dev.manager;
	struct hf_manager_event event;
	struct tran_hr_spo2_data data = {0};

//	TRAN_INFO("%s %s hr_state=%d\n", __func__, driver_dev->hf_dev.dev_name, g_hr_result);
	memset(&data, 0, sizeof(struct tran_hr_spo2_data));
	if(IS_ERR_OR_NULL(core_controller)){
		return -EINVAL;
	}
	if(!IS_ERR_OR_NULL(core_controller->ops->tran_hr_spo2_data_get)){
		core_controller->ops->tran_hr_spo2_data_get(&data);
	}
	memset(&event, 0, sizeof(struct hf_manager_event));
	event.timestamp = get_interrupt_timestamp(manager);
	event.sensor_type = SENSOR_TYPE_HEART_RATE;
	event.accurancy = SENSOR_ACCURANCY_HIGH;
	event.action = FLUSH_ACTION;
	event.word[0] = data.hr_result;
	manager->report(manager, &event);
	manager->complete(manager);
	return 0;
}
static int hr_sample(struct hf_device *hfdev)
{
	struct hrs_device *driver_dev = hf_device_get_private_data(hfdev);
	struct hf_manager *manager = driver_dev->hf_dev.manager;
	struct hf_manager_event event;
	struct tran_hr_spo2_data data = {0};

//	TRAN_INFO("%s %s hr_state=%d\n", __func__, driver_dev->hf_dev.dev_name, g_hr_result);
	memset(&data, 0, sizeof(struct tran_hr_spo2_data));
	if(IS_ERR_OR_NULL(core_controller)){
		return -EINVAL;
	}
	if(!IS_ERR_OR_NULL(core_controller->ops->tran_hr_spo2_data_get)){
		core_controller->ops->tran_hr_spo2_data_get(&data);
	}
	memset(&event, 0, sizeof(struct hf_manager_event));
	event.timestamp = get_interrupt_timestamp(manager);
	event.sensor_type = SENSOR_TYPE_HEART_RATE;
	event.accurancy = SENSOR_ACCURANCY_HIGH;
	event.action = DATA_ACTION;
	event.word[0] = data.hr_result;
	manager->report(manager, &event);
	manager->complete(manager);
	return 0;
}

int tran_hr_register(void)
{
	int err = 0;

	hr_device.hf_dev.dev_name = "heart_rate";
	hr_device.hf_dev.device_poll = HF_DEVICE_IO_POLLING;
	hr_device.hf_dev.device_bus = HF_DEVICE_IO_SYNC;
	hr_device.hf_dev.support_list = support_sensors1;
	hr_device.hf_dev.support_size = ARRAY_SIZE(support_sensors1);
	hr_device.hf_dev.enable = hr_enable;
	hr_device.hf_dev.batch = hr_batch;
	hr_device.hf_dev.flush = hr_flush;
	hr_device.hf_dev.sample = hr_sample;
	hf_device_set_private_data(&hr_device.hf_dev, &hr_device);
	err = hf_device_register_manager_create(&hr_device.hf_dev);
	if (err < 0) {
		TRAN_ERROR("%s hf_manager_create fail\n", __func__);
		goto hr_device_register_err;
	}
	TRAN_INFO(" %s success\n", __func__);
	return 0;

hr_device_register_err:
	TRAN_ERROR(" %s failed\n", __func__);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(tran_hr_register);
int tran_hr_destroy(void)
{
    hf_manager_destroy(hr_device.hf_dev.manager);
    return 0;
}
/* Heart Rate end*/

/* Oxygen Saturation start*/
static int spo2_enable(struct hf_device *hfdev, int sensor_type, int en)
{
	TRAN_INFO("%s sensor_type:%d val:%d\n", __func__, sensor_type, en);
	if(IS_ERR_OR_NULL(core_controller)){
		TRAN_INFO("error 1\n");
		return -EINVAL;
	}
	if (sensor_type == SENSOR_TYPE_OXIMETRY && !IS_ERR_OR_NULL(core_controller->ops->tran_spo2_enable)) {
		TRAN_INFO("success 1\n");
		core_controller->ops->tran_spo2_enable(en);
	} else {
		TRAN_INFO("error 2\n");
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(tran_hr_destroy);

static int spo2_batch(struct hf_device *hfdev, int sensor_type,
		int64_t delay, int64_t latency)
{
	TRAN_INFO("%s id:%d delay:%lld latency:%lld\n", __func__, sensor_type, delay, latency);
	return 0;
}
static int spo2_flush(struct hf_device *hfdev, int sensor_type)
{
	struct hrs_device *driver_dev = hf_device_get_private_data(hfdev);
	struct hf_manager *manager = driver_dev->hf_dev.manager;
	struct hf_manager_event event;
	struct tran_hr_spo2_data data = {0};
	struct tran_cust_hr_spo2_data cust_data= {0};

	//TRAN_INFO("%s %s spo2_state=%d\n", __func__, driver_dev->hf_dev.dev_name, g_hr_result);
	memset(&data, 0, sizeof(struct tran_hr_spo2_data));
	memset(&cust_data, 0, sizeof(struct tran_cust_hr_spo2_data));
	if(IS_ERR_OR_NULL(core_controller)){
		return -EINVAL;
	}
	if(!IS_ERR_OR_NULL(core_controller->ops->tran_hr_spo2_data_get)){
		core_controller->ops->tran_hr_spo2_data_get(&data);
	}
	if(!IS_ERR_OR_NULL(core_controller->ops->tran_cust_data_get)){
		core_controller->ops->tran_cust_data_get(&cust_data);
	}
	memset(&event, 0, sizeof(struct hf_manager_event));
	event.timestamp = get_interrupt_timestamp(manager);
	event.sensor_type = SENSOR_TYPE_OXIMETRY;
	event.accurancy = SENSOR_ACCURANCY_HIGH;
	event.action = FLUSH_ACTION;
	if (cust_data.cust_enable == 1) {
		event.word[0] = cust_data.spo2_result;
		event.word[1] = cust_data.hr_result;
		event.word[2] = cust_data.hrs_wear_status;
		event.word[3] = cust_data.spo2_living_status;
		event.word[4] = cust_data.spo2_alg_status;
		event.word[5] = cust_data.motion;
	} else {
		event.word[0] = data.spo2_result;
		event.word[1] = data.hr_result;
		event.word[2] = data.hrs_wear_status;
		event.word[3] = data.spo2_living_status;
		event.word[4] = data.spo2_alg_status;
		event.word[5] = data.motion;
	}
	manager->report(manager, &event);
	manager->complete(manager);
	return 0;
}

static int spo2_sample(struct hf_device *hfdev)
{
	struct hrs_device *driver_dev = hf_device_get_private_data(hfdev);
	struct hf_manager *manager = driver_dev->hf_dev.manager;
	struct hf_manager_event event;
	struct tran_hr_spo2_data data = {0};
	struct tran_cust_hr_spo2_data cust_data= {0};

	//TRAN_INFO("%s %s spo2_state=%d\n", __func__, driver_dev->hf_dev.dev_name, g_hr_result);
	memset(&data, 0, sizeof(struct tran_hr_spo2_data));
	memset(&cust_data, 0, sizeof(struct tran_cust_hr_spo2_data));
	if(IS_ERR_OR_NULL(core_controller)){
		return -EINVAL;
	}
	if(!IS_ERR_OR_NULL(core_controller->ops->tran_hr_spo2_data_get)){
		core_controller->ops->tran_hr_spo2_data_get(&data);
	}
	if(!IS_ERR_OR_NULL(core_controller->ops->tran_cust_data_get)){
		core_controller->ops->tran_cust_data_get(&cust_data);
	}
	memset(&event, 0, sizeof(struct hf_manager_event));
	event.timestamp = get_interrupt_timestamp(manager);
	event.sensor_type = SENSOR_TYPE_OXIMETRY;
	event.accurancy = SENSOR_ACCURANCY_HIGH;
	event.action = DATA_ACTION;
	if (cust_data.cust_enable == 1) {
		event.word[0] = cust_data.spo2_result;
		event.word[1] = cust_data.hr_result;
		event.word[2] = cust_data.hrs_wear_status;
		event.word[3] = cust_data.spo2_living_status;
		event.word[4] = cust_data.spo2_alg_status;
		event.word[5] = cust_data.motion;
	} else {
		event.word[0] = data.spo2_result;
		event.word[1] = data.hr_result;
		event.word[2] = data.hrs_wear_status;
		event.word[3] = data.spo2_living_status;
		event.word[4] = data.spo2_alg_status;
		event.word[5] = data.motion;
	}
	manager->report(manager, &event);
	manager->complete(manager);
	return 0;
}

int tran_spo2_register(void)
{
	int err = 0;

	spo2_device.hf_dev.dev_name = "oximetry";
	spo2_device.hf_dev.device_poll = HF_DEVICE_IO_POLLING;
	spo2_device.hf_dev.device_bus = HF_DEVICE_IO_SYNC;
	spo2_device.hf_dev.support_list = support_sensors2;
	spo2_device.hf_dev.support_size = ARRAY_SIZE(support_sensors2);
	spo2_device.hf_dev.enable = spo2_enable;
	spo2_device.hf_dev.batch = spo2_batch;
	spo2_device.hf_dev.flush = spo2_flush;
	spo2_device.hf_dev.sample = spo2_sample;
	hf_device_set_private_data(&spo2_device.hf_dev, &spo2_device);
	err = hf_device_register_manager_create(&spo2_device.hf_dev);
	if (err < 0) {
		TRAN_ERROR("%s hf_manager_create fail\n", __func__);
		goto spo2_device_register_err;
	}
	TRAN_INFO(" %s success\n", __func__);
	return 0;

spo2_device_register_err:
	TRAN_ERROR(" %s failed\n", __func__);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(tran_spo2_register);
int tran_spo2_destroy(void)
{
    hf_manager_destroy(spo2_device.hf_dev.manager);
    return 0;
}
EXPORT_SYMBOL_GPL(tran_spo2_destroy);
/* Oxygen Saturation end*/

static int __init tran_module_init(void)
{
	TRAN_INFO("tran hr_spo2 manager module init");
	return 0;
}

static void __exit tran_module_exit(void)
{
	TRAN_INFO("tran hr_spo2 manager module exit");
}

module_init(tran_module_init);
module_exit(tran_module_exit);

MODULE_DESCRIPTION("Heart Rate and Oxygen Saturation");
MODULE_AUTHOR("Transsion");
MODULE_LICENSE("GPL v2");
