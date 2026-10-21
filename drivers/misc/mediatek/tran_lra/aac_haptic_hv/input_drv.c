/*
 * drivers/haptic/input_drv.c
 *
 * Copyright (c) 2023 ICSense Semiconductor CO., LTD
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation (version 2 of the License only).
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/miscdevice.h>
#include <linux/kfifo.h>
#include <linux/syscalls.h>
#include <linux/power_supply.h>
#include <linux/pm_qos.h>
#include <linux/fb.h>
#include <linux/vmalloc.h>
#include <linux/regmap.h>
#include <linux/mman.h>

#include "input_drv.h"
#include "rt6010.h"

static int32_t ics_haptic_upload_effect(
    struct input_dev *dev,
    struct ff_effect *effect,
    struct ff_effect *old)
{
    struct ics_haptic_data *haptic_data = input_get_drvdata(dev);
    ktime_t rem;
    s64 time_us;

    ics_dbg("%s: effect->type=0x%x,FF_CONSTANT=0x%x,FF_PERIODIC=0x%x\n",
        __func__, effect->type, FF_CONSTANT, FF_PERIODIC);

    if (hrtimer_active(&haptic_data->input_timer))
    {
        rem = hrtimer_get_remaining(&haptic_data->input_timer);
        time_us = ktime_to_us(rem);
        ics_info("waiting for playing clear sequence: %lld us\n", time_us);
        usleep_range(time_us, time_us + 100);
    }
    haptic_data->effect_type = effect->type;
    mutex_lock(&haptic_data->input_lock);

    if (haptic_data->effect_type == FF_CONSTANT)
    {
        ics_dbg("%s: effect_type is  FF_CONSTANT! id = %d, duration = %d\n",
            __func__, effect->id, effect->replay.length);
        haptic_data->duration = effect->replay.length;
        haptic_data->activate_mode = PLAY_MODE_RAM_LOOP;
        haptic_data->effect_id = 0;
    }
    else if (haptic_data->effect_type == FF_PERIODIC)
    {
        ics_dbg("%s: effect_type is  FF_PERIODIC! Does NOT supported\n", __func__);
    }
    else
    {
        ics_err("%s Unsupported effect type: %d\n", __func__, effect->type);
    }

    mutex_unlock(&haptic_data->input_lock);
    return 0;
}

static int32_t ics_haptic_playback(struct input_dev *dev, int32_t effect_id, int32_t val)
{
    struct ics_haptic_data *haptic_data = input_get_drvdata(dev);
    int ret = 0;

    ics_dbg("%s: effect_id=%d , activate_mode = %d val = %d\n",
        __func__, haptic_data->effect_id, haptic_data->activate_mode, val);

    if (val > 0)
    {
        haptic_data->state = 1;
    }
    if (val <= 0)
    {
        haptic_data->state = 0;
    }
    hrtimer_cancel(&haptic_data->input_timer);

    if (haptic_data->effect_type == FF_CONSTANT &&
        haptic_data->activate_mode == PLAY_MODE_RAM_LOOP)
    {
        ics_dbg("%s: enter PLAY_MODE_RAM_LOOP\n", __func__);
        queue_work(haptic_data->input_work_queue, &haptic_data->input_vibrator_work);
    }
    else
    {
        /*other mode */
    }

    return ret;
}

static int32_t ics_haptic_erase(struct input_dev *dev, int32_t effect_id)
{
    struct ics_haptic_data *haptic_data = input_get_drvdata(dev);
    int rc = 0;

    ics_dbg("%s: enter\n", __func__);
    haptic_data->effect_type = 0;
    haptic_data->duration = 0;
    return rc;
}

static void ics_haptic_set_gain(struct input_dev *dev, uint16_t gain)
{
    return;
}

static enum hrtimer_restart input_vibrator_timer_func(struct hrtimer *timer)
{
    struct ics_haptic_data *haptic_data = container_of(timer, struct ics_haptic_data, timer);

    haptic_data->state = 0;
    schedule_work(&haptic_data->input_vibrator_work);

    return HRTIMER_NORESTART;
}

static int32_t ics_haptic_play_effect_seq(struct ics_haptic_data *haptic_data)
{
    uint8_t buf[6];

    ics_info("%s: effect_id = %d state=%d activate_mode = %d\n", __func__,
        haptic_data->effect_id, haptic_data->state, haptic_data->activate_mode);

    haptic_data->func->play_stop(haptic_data);
    if (haptic_data->state)
    {
        buf[0] = 0x01;
        buf[1] = 0x00;
        if (haptic_data->activate_mode == PLAY_MODE_RAM)
        {
            ics_info("%s: PLAY_MODE_RAM activate_mode = %d\n", __func__, haptic_data->activate_mode);
            buf[2] = 0x01;
            buf[3] = (uint8_t)haptic_data->effect_id;
        }
        else if (haptic_data->activate_mode == PLAY_MODE_RAM_LOOP)
        {
            ics_info("%s: PLAY_MODE_RAM_LOOP activate_mode = %d\n", __func__, haptic_data->activate_mode);
            buf[2] = 0x7F;
            buf[3] = (uint8_t)haptic_data->effect_id;
        }
        buf[4] = 0x00;
        buf[5] = 0x00;

        ics_info("%s: level = %d\n", __func__, haptic_data->level);
        haptic_data->func->set_play_list(haptic_data, buf, sizeof(buf));
        haptic_data->func->set_play_mode(haptic_data, PLAY_MODE_RAM);
        haptic_data->func->set_bst_vol(haptic_data, haptic_data->chip_config.boost_vol);
        haptic_data->func->set_gain(haptic_data, haptic_data->level);
        haptic_data->func->play_go(haptic_data);
    }

    return 0;
}

static void ics_input_vibrator_work_routine(struct work_struct *work)
{
    struct ics_haptic_data *haptic_data = container_of(
        work, struct ics_haptic_data, input_vibrator_work);
    uint32_t reg_val = 0, count = 40;

    ics_dbg("%s enter\n", __func__);
    ics_info("%s: effect_id = %d state=%d activate_mode = %d duration = %d\n",
        __func__,
        haptic_data->effect_id, haptic_data->state, haptic_data->activate_mode,
        haptic_data->duration);
    mutex_lock(&haptic_data->input_lock);

    if (haptic_data->current_mode == PLAY_MODE_RAM)
    {
        ics_info("%s: wait previous playing done if there is any!\n", __func__);
        while (count)
        {
            haptic_data->func->get_reg(haptic_data, RT6010_REG_PLAY_CTRL, &reg_val);
            if (reg_val == 0)
            {
                ics_info("%s: in stop mode!\n", __func__);
                break;
            }
            ics_info("%s: waiting for stop!\n", __func__);
            count--;
            usleep_range(2000, 2500);
        }
    }
    else
    {
        haptic_data->func->play_stop(haptic_data);
    }

    if (haptic_data->state)
    {
        if (haptic_data->activate_mode == PLAY_MODE_RAM)
        {
            ics_info("%s: PLAY_MODE_RAM\n", __func__);
            haptic_data->current_mode = PLAY_MODE_RAM;
            haptic_data->level = haptic_data->chip_config.gain;
            ics_haptic_play_effect_seq(haptic_data);
        }
        else if (haptic_data->activate_mode == PLAY_MODE_RAM_LOOP)
        {
            ics_info("%s: PLAY_MODE_RAM_LOOP\n", __func__);
            haptic_data->current_mode = PLAY_MODE_RAM_LOOP;
            haptic_data->level = haptic_data->chip_config.gain;
            ics_haptic_play_effect_seq(haptic_data);
            hrtimer_start(&haptic_data->input_timer,
                      ktime_set(haptic_data->duration / 1000,
                        (haptic_data->duration % 1000) *
                        1000000), HRTIMER_MODE_REL);
        }
        else
        {
            /*other mode */
        }
    }
    mutex_unlock(&haptic_data->input_lock);
    ics_dbg("%s exit\n", __func__);
}

int32_t ics_input_irq_handler(void *data)
{
    //struct ics_haptic_data *haptic_data = (struct ics_haptic_data *)data;

    return 0;
}

int32_t ics_input_dev_register(struct ics_haptic_data *haptic_data)
{
    int32_t ret = -1;
    struct input_dev *input_dev;
    struct ff_device *ff;

    ics_info("%s: start register input dev\n", __func__);
    hrtimer_init(&haptic_data->input_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    haptic_data->input_timer.function = input_vibrator_timer_func;
    mutex_init(&haptic_data->input_lock);
    INIT_WORK(&haptic_data->input_vibrator_work, ics_input_vibrator_work_routine);

    haptic_data->input_work_queue = create_singlethread_workqueue("ics_haptic_work_queue");
    if (!haptic_data->input_work_queue)
    {
        ics_err("%s: failed to create ics_haptic_work_queue\n", __func__);
        ret = -1;
        goto input_err;
    }

    device_init_wakeup(haptic_data->dev, true);
    input_dev = devm_input_allocate_device(haptic_data->dev);
    if (input_dev == NULL)
    {
        ret = -ENOMEM;
        goto input_err;
    }
    haptic_data->input_dev = input_dev;

    input_dev->name = "ics_haptic";
    input_set_drvdata(input_dev, haptic_data);
    input_set_capability(input_dev, EV_FF, FF_CONSTANT);
    input_set_capability(input_dev, EV_FF, FF_GAIN);
    input_set_capability(input_dev, EV_FF, FF_PERIODIC);

    ret = input_ff_create(input_dev, MAX_EFFECT_COUNT);
    if (ret < 0)
    {
        ics_err("%s failed to create FF input device, ret=%d\n", __func__, ret);
        goto input_err;
    }

    ff = input_dev->ff;
    ff->upload = ics_haptic_upload_effect;
    ff->playback = ics_haptic_playback;
    ff->erase = ics_haptic_erase;
    ff->set_gain = ics_haptic_set_gain;
    ret = input_register_device(input_dev);
    if (ret < 0)
    {
        ics_err("%s failed to register input device, ret=%d\n", __func__, ret);
        goto input_err;
    }

    ics_info("%s: end register input dev\n", __func__);
    return 0;

input_err:
    ics_input_dev_remove(haptic_data);
    return ret;
}

int32_t ics_input_dev_remove(struct ics_haptic_data *haptic_data)
{
    if (haptic_data->input_work_queue != NULL)
    {
        flush_workqueue(haptic_data->input_work_queue);
        destroy_workqueue(haptic_data->input_work_queue);
    }
    cancel_work_sync(&haptic_data->input_vibrator_work);
    mutex_destroy(&haptic_data->input_lock);
    if (haptic_data->input_timer.function != NULL)
    {
        hrtimer_cancel(&haptic_data->input_timer);
    }
    if (haptic_data->input_dev != NULL)
    {
        input_ff_destroy(haptic_data->input_dev);
    }
    device_init_wakeup(haptic_data->dev, false);

    return 0;
}
