/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * Change Logs:
 * Date           Author        Notes
 * 2022-01-21     Zhu Shiqiang  Initialize.
 */

/**
 * @brief   IPC Dev
 * @date    2022-01-21
 */

#include <interrupt.h>
#include <irq.h>
#include "ipc_printk.h"
#include "ipc_dev.h"
#include <clk_consumer.h>
#include <suspend.h>
#ifdef SV_ENABLE
#include "ipc_sv.h"
#endif

/* FIXME: cannot include hw header file here */
#include "ipcm.h"

#define IPCDEV_NO_ACK		0x0

static struct ipc_dev *ipc_dev;

static void ipc_dev_send_ack(struct ipc_vdev *vdev)
{
	struct ipc_dev *ipcdev = vdev->dev;

	if (ipcdev->ack_mode == IPCDEV_NO_ACK)
		return;

	ipc_send_ack(&vdev->chan, ipcdev->ack_mode);

	rt_event_send(&ipcdev->event, BIT(0));
}

static int ipc_dev_wait_ack(struct ipc_vdev *vdev, int timeout)
{
	int ret;
	rt_uint32_t recv_ev;
	struct ipc_dev *ipcdev = vdev->dev;

	if (ipcdev->ack_mode == IPCDEV_NO_ACK)
		return 0;

	ret = rt_event_recv(&ipcdev->event, BIT(0), RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
			    rt_tick_from_millisecond(timeout), &recv_ev);
	if (ret)
		return -RT_ERROR;

	if (ipcdev->ack_mode != IPCDEV_NO_ACK)
		ipc_release(&vdev->chan);

	return 0;
}

struct ipc_vdev *ipc_dev_open(struct ipc_dev *dev, struct user_config *config)
{
	int ret;
	struct ipc_vdev *vdev = RT_NULL;
	struct ipc_channel *chan = RT_NULL;
	struct chan_config *vdev_chan_cfg = RT_NULL;
	struct ipc_dev *ipcdev = dev;
	uint32_t chan_id = 0;

	if (!ipcdev)
		ipcdev = ipc_dev;

	if (!config) {
		ipc_err("dev or config val is null\n");
		goto config_err;
	}

	vdev = rt_malloc(sizeof(struct ipc_vdev));
	if (!vdev) {
		ipc_err("failed to alloc memory\n");
		goto alloc_err;
	}

	chan_id = ipc_find_chan_by_name(ipcdev->dev_cfg, config->name);
	if (chan_id >= IPC_CHANID_MAX) {
		ipc_err("not support chan id:%s\n", config->name);
		goto chan_id_err;
	}

	chan = &vdev->chan;
	vdev_chan_cfg = &ipcdev->dev_cfg[chan_id];

	/* bind vdev and mboxid */
	ipcdev->vdev[vdev_chan_cfg->mboxid] = vdev;

	init_waitqueue_head(&vdev->ipc_event);
	vdev->ipc_event_flag = 0;

	chan->chan_cfg = vdev_chan_cfg;
	chan->mst = IPC_MASTER_M7_0;
	ret = ipc_chan_open(chan);
	if (ret) {
		ipc_err("ipc chan open err: %d\n", ret);
		goto chan_id_err;
	}

	vdev->dev = ipcdev;

	ipc_debug("vdev mboxid: %d chan adr: %p, chan_id: %d chan name: %s\n",
		  vdev_chan_cfg->mboxid, &vdev->chan, chan_id,
		  vdev_chan_cfg->name);

	return vdev;

chan_id_err:
	rt_free(vdev);

alloc_err:
config_err:
	return RT_NULL;
}

int ipc_dev_close(struct ipc_vdev *vdev)
{
	if (!vdev) {
		ipc_err("vdev is null\n");
		return -RT_ERROR;
	}
	ipc_chan_close(&vdev->chan);

	rt_free(vdev);

	return RT_EOK;
}

int ipc_dev_send(struct ipc_vdev *vdev, struct ipc_send_msg *msg)
{
	int ret = 0;
	struct ipc_dev *ipcdev = RT_NULL;
	struct ipc_channel *chan = RT_NULL;

	if (!vdev || !msg) {
		ipc_err("vdev or msg is null\n");
		ret = -RT_ERROR;
		goto out_err;
	}
	ipcdev = vdev->dev;
	chan = &vdev->chan;

	if (!(msg->buf)) {
		ipc_err("msg buf is null\n");
		ret = -RT_ERROR;
		goto out_err;
	}

	mutex_lock(&ipcdev->send_lock);

	ret = ipc_send(chan, msg->buf, msg->size);
	if (ret) {
		ipc_err("ipc send error\n");
		goto send_err;
	}

	ret = ipc_dev_wait_ack(vdev, msg->timeout_ms);
	if (ret) {
		ipc_err("ipc wait ack error\n");
		goto wait_err;
	}
	mutex_unlock(&ipcdev->send_lock);

	return ret;

wait_err:
send_err:
	mutex_unlock(&ipcdev->send_lock);

out_err:
	return ret;
}

int ipc_dev_recv(struct ipc_vdev *vdev, struct ipc_recv_msg *msg)
{
	int ret = 0;
	struct ipc_dev *ipcdev = RT_NULL;
	struct ipc_channel *chan = RT_NULL;

	if (!vdev || !msg) {
		ipc_err("vdev or msg is null\n");
		return -EINVAL;
	}
	chan = &vdev->chan;
	ipcdev = vdev->dev;

	if (!(msg->buf)) {
		ipc_err("msg buf is null\n");
		return -EINVAL;
	}

	mutex_lock(&ipcdev->recv_lock);
	ret = ipc_recv(chan, msg->buf, msg->size);
	if (!ret) {
		mutex_unlock(&ipcdev->recv_lock);
		vdev->ipc_event_flag = 0;
		return ret;
	}
	mutex_unlock(&ipcdev->recv_lock);
	ret = wait_event_timeout(vdev->ipc_event, vdev->ipc_event_flag, msg->timeout_ms);
	if (ret == 0) {
		ret = -RT_ERROR;
		goto wait_timeout;
	}

	mutex_lock(&ipcdev->recv_lock);
	ret = ipc_recv(chan, msg->buf, msg->size);
	if (ret) {
		ipc_warn("recv data failed\n");
		goto recv_err;
	}
	mutex_unlock(&ipcdev->recv_lock);
	vdev->ipc_event_flag = 0;

	return ret;

recv_err:
	mutex_unlock(&ipcdev->recv_lock);

wait_timeout:
	return ret;
}

static rt_err_t ipcdev_open(struct rt_device *dev, rt_uint16_t oflag)
{
	return RT_EOK;
}

static rt_err_t ipcdev_control(struct rt_device *dev, int cmd, void *arg)
{
	/* TODO: */
	return RT_EOK;
}

static void ipc_irq_handler(int irq, void *dev)
{
	int ret, val, index;
	struct ipc_vdev *vdev = RT_NULL;
	struct ipc_dev *ipcdev = (struct ipc_dev *)dev;

	val = ipc_irq_status();
	ipc_debug("irq status 0x%x irq_cnt:%d\n", val, ipcdev->irq_cnt);
	while (val) {
		index = ffs(val) - 0x1;
		if (index >= IPC_VDEV_MAX) {
			ipc_warn("index(%d) is larger then max(%d)", index, IPC_VDEV_MAX);
			val &= (~(0x1 << index));
			continue;
		}
		vdev = ipcdev->vdev[index];
		if (!vdev) {
			ipc_err("vedv is null, irq status: 0x%x", val);
			/*
			 * vdev status check before message send from AP side
			 * is needed to avoid null pointer scenario. if vedev
			 * null occurs, which is caused by not doing vdev check.
			 * the workaround now is to release the corresponding
			 * channel.
			 */
			ipcm_release(index);
			return;
		}

		ret = ipc_irq_clear(&vdev->chan, ipcdev->ack_mode);
		if (ret)
			return;

		ret = ipc_data_store(&vdev->chan);

		ipc_release(&vdev->chan);

		vdev->ipc_event_flag = 1;
		wake_up(&vdev->ipc_event);

		ipc_dev_send_ack(vdev);

		val &= (~(0x1 << index));
	}
}

#ifdef TETRAS_SUSPEND_FRAMEWORK
static int ipc_dev_suspend(void *data)
{
	struct clk *ipc_hclk = (struct clk *)data;
	clk_disable(ipc_hclk);
	TLOG_I("IPC clk disable\n");

	return 0;
}

static int ipc_dev_resume(void *data)
{
	struct clk *ipc_hclk = (struct clk *)data;
	clk_enable(ipc_hclk);
	TLOG_I("IPC clk enable\n");

	return 0;
}

static struct dev_pm_ops ipc_pm_ops = {
	.suspend = ipc_dev_suspend,
	.resume = ipc_dev_resume,
};
#endif

static int ipc_driver_init(struct ipc_dev *config)
{
	int ret;
	struct rt_device *device;
	struct ipc_dev *ipcdev = RT_NULL;
	const struct chan_config init_cfg[] = IPC_CHAN_CONFIGS;
	struct clk *ipc_hclk;

	ipcdev = rt_malloc(sizeof(struct ipc_dev) + sizeof(init_cfg));
	if (!ipcdev) {
		ipc_err("ipc dev malloc failed!\n");
		ret = -RT_ENOMEM;
		goto malloc_err;
	}
	rt_memset(ipcdev, 0x0, sizeof(struct ipc_dev) + sizeof(init_cfg));

	ipcdev->name = config->name;
	ipcdev->irq = config->irq;

	rt_memcpy(ipcdev->dev_cfg, init_cfg, sizeof(init_cfg));

	/*
	 * ipc has two interrupt lines, irq 21 and irq 22
	 * now only ipc irq 21 is used in ipc work routine.
	 */
	rt_hw_interrupt_install(ipcdev->irq, ipc_irq_handler, ipcdev, "ipc_int0");

	mutex_init(&ipcdev->send_lock);
	mutex_init(&ipcdev->recv_lock);

	ret = rt_event_init(&ipcdev->event, "event", RT_IPC_FLAG_FIFO);
	if (ret != RT_EOK) {
		ipc_err("init event failed.\n");
		goto event_init_err;
	}

	/* initialize device interface */
	device = &ipcdev->device;
#ifdef RT_USING_DEVICE_OPS
	device->ops			= RT_NULL;
#else
	device->init			= RT_NULL;
	device->open			= ipcdev_open;
	device->close			= RT_NULL;
	device->read			= RT_NULL;
	device->write			= RT_NULL;
	device->control			= ipcdev_control;
#endif
	ret = rt_device_register(device, ipcdev->name, RT_DEVICE_FLAG_RDWR);
	if (ret) {
		ipc_err("ipc driver register failed!\n");
		goto register_err;
	}

	ipc_dev = ipcdev;

	/* get clk */
	if (config->hclk_name) {
		ipc_hclk = get_clk_by_name(config->name, config->hclk_name);
		if (!ipc_hclk) {
			ret = -RT_ERROR;
			goto clk_init_err;
		}
		ipcdev->ipc_hclk = ipc_hclk;
		clk_enable(ipc_hclk);
#ifdef TETRAS_SUSPEND_FRAMEWORK
		pm_dev_register(config->name, &ipc_pm_ops, (void *)ipcdev->ipc_hclk);
#endif
	}

#ifdef SV_ENABLE
	ipc_sv_init(ipcdev);
#endif

	return ret;

clk_init_err:
	rt_device_unregister(device);
register_err:
event_init_err:
	mutex_deinit(&ipcdev->send_lock);
	mutex_deinit(&ipcdev->recv_lock);
	rt_free(ipcdev);

malloc_err:
	ipcdev = RT_NULL;
	return ret;
}
DEVICE_COMMON_INIT(ipc, ipc_driver_init);
