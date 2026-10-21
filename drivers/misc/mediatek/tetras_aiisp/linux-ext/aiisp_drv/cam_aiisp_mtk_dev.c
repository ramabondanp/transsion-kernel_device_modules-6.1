/*
 * (C) Copyright 2021-2022, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Change Logs:
 * Date         Author          Notes
 * 2022-12-08   Gu Xiao         Initialize.
 *
 * AI ISP Driver for MTK platform
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/uio_driver.h>
#include <linux/timer.h>

#ifdef QCOM_PLATFORM_CRM
#include "cam_aiisp_qcom_dev.h"
#else
#include "cam_aiisp_mtk_dev.h"
#endif
#include "cam_aiisp_internal.h"
#include "cam_aiisp_dbg.h"
#include "cam_aiisp_dev.h"

/* #define DEBUG_MTK_VSYNC_SIMU */

#ifdef DEBUG_MTK_VSYNC_SIMU
static struct timer_list test_timer;
static void test_send_vsync(unsigned long var)
{
	struct v4l2_event event;

	event.type = V4L2_EVENT_VSYNC;
	event.id = 0;

	v4l2_event_queue(sync_info->aiisp_dev->vdev, &event);

	AIISP_INFO(CAM_AIISP, "send vsync event");
	mod_timer(&test_timer, jiffies + msecs_to_jiffies(1000));
}
#endif

#ifdef USE_MTK_EXTISP
void cam_notify_fcnt_mismatch(void *hdl)
{
	struct aiisp_sync_sensor_info *sync_info = hdl;

	if (atomic_read(&sync_info->frame_sync_state) == FRAME_SYNC_OK)
		atomic_set(&sync_info->frame_sync_state, FRAME_MISMATCHED);
}
/**
 * NOTE: this structure is defined and used as copy of the one of the same name in
 * mtk_cam-ctrl.h. that header could not be included due to dependancy problem.
 * The structure MUST be kept the same as the original.
 */
struct mtk_cam_external_vsync_info {
	unsigned int frm_cnt;
};

extern void mtk_cam_sensor_vsync_event(struct mtk_cam_external_vsync_info *v_info);

/* FIXME: should be configed */
extern struct target_id send_to_tid;

#define AD8_PREISP_FCNT_ADDR 0x5125a484
static uint32_t merge_offset;
static irqreturn_t cam_fsync_mtk_irq_thread(int irq, void *dev_id)
{
	int ret;
	u32 real_fcnt;
	struct aiisp_sync_sensor_info *sync_info = dev_id;
	struct mtk_cam_external_vsync_info v_info;

	ret = read_data_for_kernel_thread(AD8_PREISP_FCNT_ADDR, (char *)&real_fcnt, 4,
					  &send_to_tid);
	if (ret != 0) {
		AIISP_ERR(CAM_AIISP, "fcnt read failure: %d", ret);
		atomic_set(&sync_info->frame_sync_state, FRAME_SYNC_ERR);
		return IRQ_HANDLED;
	}
	real_fcnt &= 0x3FFFFFFF;
	sync_info->aiisp_dev->sns_req_mgr.fcnt = real_fcnt + 1;
	AIISP_INFO(CAM_AIISP, "fsync corrected: %d", real_fcnt);
	if (sync_info->aiisp_dev->sns_req_mgr.fcnt > 1) {
		v_info.frm_cnt = sync_info->aiisp_dev->sns_req_mgr.fcnt - 1 - merge_offset;
		mtk_cam_sensor_vsync_event(&v_info);
	}
	atomic_set(&sync_info->frame_sync_state, FRAME_SYNC_OK);

	return IRQ_HANDLED;
}
static irqreturn_t cam_fsync_mtk_irq_handler(int irq, void *dev)
{
	struct aiisp_sync_sensor_info *sync_info = dev;
	struct mtk_cam_external_vsync_info v_info;
	enum FRAME_SYNC_STATE state = atomic_read(&sync_info->frame_sync_state);

	if (state == FRAME_SYNCING)
		return IRQ_HANDLED;
	else if (state == FRAME_MISMATCHED) {
		atomic_set(&sync_info->frame_sync_state, FRAME_SYNCING);
		return IRQ_WAKE_THREAD;
	} else if (unlikely(state == FRAME_SYNC_ERR))
		return IRQ_WAKE_THREAD;

	if (sync_info->aiisp_dev->sns_req_mgr.fcnt == 0) {
		if (sync_info->aiisp_dev->pipe_delay > 1) {
			sync_info->aiisp_dev->sns_req_mgr.fcnt++;
			merge_offset = 1;
			return IRQ_HANDLED;
		} else {
			merge_offset = 0;
		}
	}

	v_info.frm_cnt = ++sync_info->aiisp_dev->sns_req_mgr.fcnt - 1 - merge_offset;
	mtk_cam_sensor_vsync_event(&v_info);

	return IRQ_HANDLED;
}
#else
static irqreturn_t cam_fsync_mtk_irq_thread(int irq, void *dev_id)
{
	struct aiisp_sync_sensor_info *sync_info = dev_id;
	struct v4l2_event event;
	struct timespec64 t64;

	event.type = V4L2_EVENT_FRAME_SYNC;
	event.id = sync_info->slot_idx;
	ktime_get_ts64(&t64);

	event.timestamp.tv_sec = t64.tv_sec;
	event.timestamp.tv_nsec = t64.tv_nsec;

	v4l2_event_queue(sync_info->aiisp_dev->vdev, &event);

	AIISP_INFO(CAM_AIISP, "fsync%d irq #%d",
		   sync_info->slot_idx, sync_info->aiisp_dev->sns_req_mgr.fcnt++);

	return IRQ_HANDLED;
}

static irqreturn_t cam_fsync_mtk_irq_handler(int irq, void *dev)
{
	return IRQ_WAKE_THREAD;
}
#endif

int cam_fsync_mtk_gpio_init(struct aiisp_device *aiisp_dev)
{
	int ret, irq_num;
	bool found0 = false;
	struct device *dev = aiisp_dev->dev;
	struct aiisp_sync_sensor_info *sync_info;

	/* fysnc 0 */
	sync_info = aiisp_dev->syncs_info;
	sync_info->fsync_gpio = of_get_named_gpio_flags(dev->of_node, "fsync-intr0", 0, NULL);
	if (sync_info->fsync_gpio == -ENOENT)
		goto detect_fsync1;
	AIISP_DBG(CAM_AIISP, "fsync0 gpio: %d", sync_info->fsync_gpio);
	if (!gpio_is_valid(sync_info->fsync_gpio)) {
		AIISP_ERR(CAM_AIISP, "invalid gpio %d", sync_info->fsync_gpio);
		ret = -ENODEV;
		goto err;
	}

	ret = devm_gpio_request(dev, sync_info->fsync_gpio, "aiisp-fsync0");
	if (ret) {
		AIISP_ERR(CAM_AIISP, "fsync0 unable to request gpio [%d]", sync_info->fsync_gpio);
		goto err;
	}

	ret = gpio_direction_input(sync_info->fsync_gpio);
	if (ret) {
		AIISP_ERR(CAM_AIISP, "fsync0 gpio set direction for gpio [%d] failed",
				sync_info->fsync_gpio);
		goto err;
	}

	irq_num = gpio_to_irq(sync_info->fsync_gpio);
	AIISP_INFO(CAM_AIISP, "fsync0 irq num: %d", irq_num);
	sync_info->irq_num = irq_num;
	sync_info->slot_idx = 0;
	sync_info->aiisp_dev = aiisp_dev;

	ret = devm_request_threaded_irq(dev, irq_num,
					cam_fsync_mtk_irq_handler,
					cam_fsync_mtk_irq_thread,
					IRQF_TRIGGER_RISING,
					"fsync_irq0", sync_info);
	if (ret) {
		AIISP_ERR(CAM_AIISP, "cam fsync0 failed irq=%d request ret = %d",
				irq_num, ret);
		goto err;
	}
	found0 = true;
	aiisp_dev->fsync_gpio_num++;

	/* fysnc 1 */
detect_fsync1:
	sync_info = aiisp_dev->syncs_info + 1;
	sync_info->fsync_gpio = of_get_named_gpio_flags(dev->of_node, "fsync-intr1", 0, NULL);
	if (sync_info->fsync_gpio == -ENOENT) {
		if (found0)
			AIISP_INFO(CAM_AIISP, "fsync gpio init finished, found fsync0");
		else
			AIISP_INFO(CAM_AIISP, "no fsync gpio found");
		return 0;
	}
	AIISP_DBG(CAM_AIISP, "fsync1 gpio: %d", sync_info->fsync_gpio);
	if (!gpio_is_valid(sync_info->fsync_gpio)) {
		AIISP_ERR(CAM_AIISP, "invalid gpio %d", sync_info->fsync_gpio);
		ret = -ENODEV;
		goto err;
	}

	ret = devm_gpio_request(dev, sync_info->fsync_gpio, "aiisp-fsync1");
	if (ret) {
		AIISP_ERR(CAM_AIISP, "fsync1 unable to request gpio [%d]", sync_info->fsync_gpio);
		goto err;
	}

	ret = gpio_direction_input(sync_info->fsync_gpio);
	if (ret) {
		AIISP_ERR(CAM_AIISP, "fsync1 gpio set direction for gpio [%d] failed",
				sync_info->fsync_gpio);
		goto err;
	}

	irq_num = gpio_to_irq(sync_info->fsync_gpio);
	AIISP_DBG(CAM_AIISP, "fsync1 irq num: %d", irq_num);
	sync_info->irq_num = irq_num;
	sync_info->slot_idx = 1;
	sync_info->aiisp_dev = aiisp_dev;

	ret = devm_request_threaded_irq(dev, irq_num,
					cam_fsync_mtk_irq_handler,
					cam_fsync_mtk_irq_thread,
					IRQF_TRIGGER_RISING,
					"fsync_irq1", sync_info);
	if (ret) {
		AIISP_ERR(CAM_AIISP, "cam fsync1 failed irq=%d request ret = %d",
				irq_num, ret);
		goto err;
	}
	aiisp_dev->fsync_gpio_num++;
	if (found0)
		AIISP_INFO(CAM_AIISP, "fsync gpio init finished, found fsync0&fsync1");
	else
		AIISP_INFO(CAM_AIISP, "fsync gpio init finished, found fsync1");

#ifdef DEBUG_MTK_VSYNC_SIMU
	setup_timer(&test_timer, test_send_vsync, 0);
	mod_timer(&test_timer, jiffies + msecs_to_jiffies(1000));
#endif
	return 0;

err:
	AIISP_ERR(CAM_AIISP, "fsync gpio init error %d", ret);
	return -ENODEV;
}
