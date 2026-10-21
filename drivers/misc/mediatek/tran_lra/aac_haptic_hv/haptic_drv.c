/*
 * drivers/haptic/haptic_drv.c
 *
 * Copyright (c) 2022 ICSense Semiconductor CO., LTD
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation (version 2 of the License only).
 */
#define  DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/vmalloc.h>
#include <linux/regmap.h>

#include "haptic_util.h"
#include "haptic_drv.h"
#include "richtap_drv.h"
#include "input_drv.h"
#include "sfdc_drv.h"
#include "rt6010.h"

char *haptic_config_name = "haptic_config.bin";
char preset_waveform_name[][MAX_PRESET_NAME_LEN] =
{
    {"haptic_osc_24K_5s_stream.bin"},//0
    {"haptic_stream.bin"},//1
    {"haptic_lighthouse_stream.bin"},//2
    {"haptic_silk_stream.bin"},//3
    {"haptic_osc_24K_5s_stream.bin"},//4
    {"enter_game_space_stream.bin"},//5
    {"charge_in_stream.bin"},//6
    {"error_stream.bin"},//7
    {"kill_program_stream.bin"},//8
    {"voice_level_stream.bin"},//9
    {"turn_on.bin"},	//10 ai phone power on
    {"stream07.bin"},//11
    {"stream08.bin"},//12
    {"stream09.bin"},//13
    {"stream10.bin"},//14
    {"stream11.bin"},//15
};

static int32_t haptic_hw_reset(struct ics_haptic_data *haptic_data);

static ssize_t chip_id_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;

    ret = haptic_data->func->get_chip_id(haptic_data);
    check_error_return(ret);

    return snprintf(buf, PAGE_SIZE, "%02X\n", haptic_data->chip_config.chip_id);
}

static ssize_t f0_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    ssize_t len = 0;

    mutex_lock(&haptic_data->lock);
    ret = haptic_data->func->get_f0(haptic_data);
    mutex_unlock(&haptic_data->lock);
    check_error_return(ret);

    len += snprintf(buf + len, PAGE_SIZE - len, "%u\n", haptic_data->chip_config.f0);
    return len;
}

static ssize_t f0_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    int32_t val = 0;

    ret = kstrtoint(buf, 10, &val);
    check_error_return(ret);

    if (abs((int32_t)haptic_data->chip_config.f0 - val) > RESAMPLE_THRESHOLD)
    {
        haptic_data->chip_config.f0 = val;
        haptic_data->func->resample_ram_waveform(haptic_data);
    }

    return count;
}

static ssize_t reg_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    ssize_t len = 0;
    uint32_t reg_val = 0, i;

    len += snprintf(buf + len, PAGE_SIZE - len, "reg list (0x%02X):\n", haptic_data->chip_config.reg_size);
    for (i = 0; i < haptic_data->chip_config.reg_size; i++)
    {
        ret = haptic_data->func->get_reg(haptic_data, i, &reg_val);
        check_error_return(ret);
        len += snprintf(buf + len, PAGE_SIZE - len, "0x%02X=0x%02X\n", (uint8_t)i, (uint8_t)reg_val);
    }

    return len;
}

static ssize_t reg_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    uint32_t data_buf[2] = { 0, 0 };

    if (sscanf(buf, "%X %X", &data_buf[0], &data_buf[1]) == 2)
    {
        ret = haptic_data->func->set_reg(haptic_data, data_buf[0], data_buf[1]);
        check_error_return(ret);
    }

    return count;
}

static ssize_t vbst_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    ssize_t len = 0;

    len += snprintf(buf + len, PAGE_SIZE - len, "%u\n", haptic_data->chip_config.boost_vol);
    return len;
}

static ssize_t vbst_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    uint32_t val = 0;

    ret = kstrtouint(buf, 10, &val);
    check_error_return(ret);

    mutex_lock(&haptic_data->lock);
    ret = haptic_data->func->set_bst_vol(haptic_data, val);
    mutex_unlock(&haptic_data->lock);
    check_error_return(ret);

    return count;
}

static ssize_t gain_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    ssize_t len = 0;

    len += snprintf(buf + len, PAGE_SIZE - len, "%u\n", haptic_data->chip_config.gain);
    return len;
}

static ssize_t gain_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    uint32_t reg_val = 0;
    static bool is_no_pole_vibration = false;
    int gain_max = 128;
    uint8_t buf_play_list[6];

    if (haptic_data->i2c_timout_state) {
        ics_info("%s : i2c timeout !!!just return...\n", __func__);
        return count;
    }
    ret = kstrtouint(buf, 0, &reg_val);
    check_error_return(ret);
    ics_info("reg_val=0x%02x ....+\n", reg_val);
    if(reg_val == 200){// for setting notify is_no_pole_vibration with 200
        is_no_pole_vibration = true;
        return count;
    }
    if(reg_val == 201){// for setting notify adjust vibr finish with 201
        mutex_lock(&haptic_data->lock);
        ret = haptic_data->func->set_gain(haptic_data, haptic_data->chip_config.gain);
        mutex_unlock(&haptic_data->lock);
        check_error_return(ret);
        ics_info("restore gain=0x%02x\n", haptic_data->chip_config.gain);
        return count;
    }
    if (reg_val <= gain_max){
        mutex_lock(&haptic_data->lock);
        haptic_data->chip_config.gain = reg_val;
        ret = haptic_data->func->set_gain(haptic_data, reg_val);
        mutex_unlock(&haptic_data->lock);
        check_error_return(ret);

        if(is_no_pole_vibration) {
            ics_info("reg_val=0x%02x \n", reg_val);
            buf_play_list[0] = 0x01;
            buf_play_list[1] = 0x00;
            buf_play_list[2] = 0x01;  //play once
            buf_play_list[3] = 0x03;
            buf_play_list[4] = 0x00;
            buf_play_list[5] = 0x00;
            haptic_data->func->set_play_list(haptic_data, buf_play_list, sizeof(buf_play_list));
            haptic_data->func->set_play_mode(haptic_data, PLAY_MODE_RAM);
            haptic_data->func->play_go(haptic_data);
        }
    }

    return count;
}

static ssize_t vibr_level_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    ssize_t len = 0;

    len += snprintf(buf + len, PAGE_SIZE - len, "%u\n", haptic_data->chip_config.gain);
    return len;
}

static ssize_t vibr_level_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    uint32_t reg_val = 0;
    int gain_max = 128;
    uint8_t buf_play_list[6];

    if (haptic_data->i2c_timout_state) {
        ics_info("%s : i2c timeout !!!just return...\n", __func__);
        return count;
    }
    ret = kstrtouint(buf, 0, &reg_val);
    check_error_return(ret);
    ics_info("reg_val=0x%02x ....--\n", reg_val);
    if (reg_val <= gain_max){
        mutex_lock(&haptic_data->lock);
        //haptic_data->chip_config.gain = reg_val;
        ret = haptic_data->func->set_gain(haptic_data, reg_val);
        mutex_unlock(&haptic_data->lock);
        check_error_return(ret);

        buf_play_list[0] = 0x01;
        buf_play_list[1] = 0x00;
        buf_play_list[2] = 0x01;  //play once
        buf_play_list[3] = 0x03;
        buf_play_list[4] = 0x00;
        buf_play_list[5] = 0x00;
        haptic_data->func->set_play_list(haptic_data, buf_play_list, sizeof(buf_play_list));
        haptic_data->func->set_play_mode(haptic_data, PLAY_MODE_RAM);
        haptic_data->func->play_go(haptic_data);

    }

    return count;
}

static ssize_t seq_show(struct device *dev, struct device_attribute *attr,
            char *buf)
{
    size_t count = 0;
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    uint8_t buf_play_list[6];

    mutex_lock(&haptic_data->lock);
    haptic_data->func->get_play_list(haptic_data, buf_play_list, sizeof(buf_play_list));
    mutex_unlock(&haptic_data->lock);
    return snprintf(buf + count, PAGE_SIZE - count,
                  "seq0 = %d\nseq1 = %d\n", buf_play_list[3], buf_play_list[5]);
}

static ssize_t seq_store(struct device *dev, struct device_attribute *attr,
             const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);

    uint32_t databuf[2] = { 0, 0 };
    uint8_t buf_play_list[6];
    uint32_t preset_num = sizeof(preset_waveform_name) / MAX_PRESET_NAME_LEN;
    uint32_t index = 0;
    if (haptic_data->i2c_timout_state) {
        ics_info("%s : i2c timeout !!!just return...\n", __func__);
        return count;
    }

    if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
        ics_info("seq%d=0x%02X", databuf[0], databuf[1]);
        if (databuf[1] <= 0) {
            return count;
        } else if (databuf[1] >= 6) {
            haptic_data->activate_ignore = 1;
            mutex_lock(&haptic_data->lock);
            if (databuf[1] + 4 < preset_num)
            {
                haptic_data->preset_wave_index = databuf[1] + 4;
                ics_info("%s : preset_waveform_name[%u]: %s\n", __func__, databuf[1] + 4, preset_waveform_name[databuf[1] + 4]);
                schedule_work(&haptic_data->preset_work);
            }
            else
            {
                index = databuf[1];
                ics_err("%s: specified invalid preset waveform index : %d\n", __func__, index);
            }
            mutex_unlock(&haptic_data->lock);
            return count;
        }
        mutex_lock(&haptic_data->lock);
        haptic_data->func->get_play_list(haptic_data, buf_play_list, sizeof(buf_play_list));
        if (databuf[0] == 0) {
            buf_play_list[3] = databuf[1] - 1;
        } else {
            buf_play_list[5] = databuf[1] - 1;
        }
        buf_play_list[0] = 1;
        buf_play_list[1] = 0;
        haptic_data->func->set_play_list(haptic_data, buf_play_list, sizeof(buf_play_list));
        mutex_unlock(&haptic_data->lock);
    }
    return count;
}

static ssize_t loop_show(struct device *dev, struct device_attribute *attr,
             char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    size_t count = 0;
    uint8_t buf_play_list[6];

    mutex_lock(&haptic_data->lock);
    haptic_data->func->get_play_list(haptic_data, buf_play_list,
            sizeof(buf_play_list));
    mutex_unlock(&haptic_data->lock);
    return snprintf(buf + count, PAGE_SIZE - count,
                  "seq1 = %d\nseq2 = %d\nseq3 = %d\nseq4 = %d\n",
                  buf_play_list[3], buf_play_list[5],
                  buf_play_list[2], buf_play_list[4]);
}

static ssize_t loop_store(struct device *dev, struct device_attribute *attr,
              const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data,
                           vib_dev);
    uint32_t databuf[2] = { 0, 0 };
    uint8_t buf_play_list[6];
    if (haptic_data->i2c_timout_state) {
        ics_info("%s : i2c timeout !!!just return...\n", __func__);
        return count;
    }

    if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
        ics_info("seq%d loop=0x%02X", databuf[0], databuf[1]);
        mutex_lock(&haptic_data->lock);
        haptic_data->func->get_play_list(haptic_data, buf_play_list, sizeof(buf_play_list));
        if (databuf[0] == 0) {
            buf_play_list[2] = databuf[1] + 1;
        } else {
            buf_play_list[4] = databuf[1] + 1;
        }
        ics_info("play list : %u %u \n", buf_play_list[2], buf_play_list[3]);

        haptic_data->func->set_play_list(haptic_data, buf_play_list, sizeof(buf_play_list));
        mutex_unlock(&haptic_data->lock);
    }

    return count;
}
/*
static ssize_t brightness_show(struct device *dev, struct device_attribute *attr,
             char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);

    return snprintf(buf, PAGE_SIZE, "%d\n", haptic_data->activate_state);
}

static ssize_t brightness_store(struct device *dev, struct device_attribute *attr,
              const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int ret = 0;
    uint32_t val = 0;

    ret = kstrtouint(buf, 10, &val);
    check_error_return(ret);
    if (haptic_data->activate_ignore) {
        haptic_data->activate_ignore = 0;
        return count;
    }
    mutex_lock(&haptic_data->lock);
    hrtimer_cancel(&haptic_data->timer);
    haptic_data->activate_state = val;
    haptic_data->func->play_stop(haptic_data);
    haptic_data->func->set_play_mode(haptic_data, PLAY_MODE_RAM);
    haptic_data->func->play_go(haptic_data);
    mutex_unlock(&haptic_data->lock);

    return count;
}
*/
static ssize_t rtp_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    uint32_t index = 0;
    uint32_t preset_num = sizeof(preset_waveform_name) / MAX_PRESET_NAME_LEN;

    ret = kstrtouint(buf, 0, &index);
    check_error_return(ret);

    mutex_lock(&haptic_data->lock);
    if (index < preset_num)
    {
        haptic_data->preset_wave_index = index;
        ics_info("%s : preset_waveform_name[%u]: %s\n", __func__, index, preset_waveform_name[index]);
        schedule_work(&haptic_data->preset_work);
    }
    else
    {
        ics_err("%s: specified invalid preset waveform index : %d\n", __func__, index);
    }
    mutex_unlock(&haptic_data->lock);

    return count;
}

static ssize_t index_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    ssize_t len = 0;

    len += snprintf(buf + len, PAGE_SIZE - len, "%u\n", haptic_data->ram_wave_index);
    return len;
}

static ssize_t index_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    uint32_t val = 0;
    uint8_t buf_play_list[6];

    ret = kstrtouint(buf, 10, &val);
    check_error_return(ret);

    haptic_data->ram_wave_index = val;
    buf_play_list[0] = 0x01;
    buf_play_list[1] = 0x00;
    buf_play_list[2] = 0x01;  //play once
    buf_play_list[3] = (uint8_t)haptic_data->ram_wave_index;
    buf_play_list[4] = 0x00;
    buf_play_list[5] = 0x00;
    haptic_data->func->set_play_list(haptic_data, buf_play_list, sizeof(buf_play_list));
    haptic_data->func->set_play_mode(haptic_data, PLAY_MODE_RAM);
    haptic_data->func->play_go(haptic_data);

    return count;
}

static ssize_t duration_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);

    ktime_t time_rem;
    s64 time_ms = 0;

    if (hrtimer_active(&haptic_data->timer))
    {
        time_rem = hrtimer_get_remaining(&haptic_data->timer);
        time_ms = ktime_to_ms(time_rem);
    }
    return snprintf(buf, PAGE_SIZE, "%lld\n", time_ms);
}

static ssize_t duration_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    uint32_t val = 0;

    ret = kstrtouint(buf, 10, &val);
    check_error_return(ret);

    // setting 0 on duration is NOP
    if (val > 0)
    {
        haptic_data->duration = val;
    }

    return count;
}

static ssize_t activate_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);

    return snprintf(buf, PAGE_SIZE, "%d\n", haptic_data->activate_state);
}

static ssize_t activate_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int ret = 0;
    uint32_t val = 0;

    ret = kstrtouint(buf, 10, &val);
    check_error_return(ret);

    mutex_lock(&haptic_data->lock);
    hrtimer_cancel(&haptic_data->timer);
    haptic_data->activate_state = val;
    mutex_unlock(&haptic_data->lock);
    schedule_work(&haptic_data->vibrator_work);

    return count;
}

static ssize_t playlist_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    ssize_t len = 0;
    uint32_t i, size = haptic_data->chip_config.ram_size;

    if (haptic_data->gp_buf == NULL)
    {
        return 0;
    }

    ret = haptic_data->func->get_ram_data(haptic_data, haptic_data->gp_buf, &size);
    if (ret >= 0)
    {
        for(i = haptic_data->chip_config.list_base_addr; i < haptic_data->chip_config.wave_base_addr; i++)
        {
            len += snprintf(buf + len, PAGE_SIZE - len, "%02X", haptic_data->gp_buf[i]);
        }
    }
    check_error_return(ret);

    return len;
}

static ssize_t playlist_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;

    if (count > 0)
    {
        ret = haptic_data->func->set_play_list(haptic_data, (uint8_t*)buf, count);
    }
    check_error_return(ret);

    return count;
}

static ssize_t waveform_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    ssize_t len = 0;
    uint32_t i, size = haptic_data->chip_config.ram_size;

    if (haptic_data->gp_buf == NULL)
    {
        return 0;
    }

    ics_info("%s: waveform_show: %u\n", __func__, size);
    ret = haptic_data->func->get_ram_data(haptic_data, haptic_data->gp_buf, &size);
    if (ret >= 0)
    {
        for(i = haptic_data->chip_config.wave_base_addr; i < haptic_data->chip_config.ram_size; i++)
        {
            len += snprintf(buf + len, PAGE_SIZE - len, "%02X", haptic_data->gp_buf[i]);
        }
    }
    check_error_return(ret);

    return len;
}

static ssize_t waveform_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;

    if (count > 0)
    {
        if (count > haptic_data->waveform_size)
        {
            vfree(haptic_data->waveform_data);
            haptic_data->waveform_data = vmalloc(count);
            if (haptic_data->waveform_data == NULL)
            {
                ics_err("%s: failed to allocate memory for waveform data\n", __func__);
                return -1;
            }
        }
        haptic_data->waveform_size = count;
        memcpy(haptic_data->waveform_data, (uint8_t*)buf, count);
        if (abs((int32_t)haptic_data->chip_config.f0
            - (int32_t)haptic_data->chip_config.sys_f0) > RESAMPLE_THRESHOLD)
        {
            haptic_data->func->resample_ram_waveform(haptic_data);
        }
        else
        {
            ret = haptic_data->func->set_waveform_data(haptic_data, (uint8_t*)buf, count);
            check_error_return(ret);
        }
    }

    return count;
}

static ssize_t play_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    ssize_t len = 0;

    ret = haptic_data->func->get_play_status(haptic_data);
    check_error_return(ret);
    len += snprintf(buf + len, PAGE_SIZE - len, "%u", haptic_data->play_status);

    return len;
}

static ssize_t play_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    uint32_t val = 0;

    ret = kstrtouint(buf, 10, &val);
    check_error_return(ret);

    mutex_lock(&haptic_data->lock);
    if ((val & 0x01) > 0)
    {
        ret = haptic_data->func->play_go(haptic_data);
    }
    else
    {
        ret = haptic_data->func->play_stop(haptic_data);
    }
    mutex_unlock(&haptic_data->lock);
    check_error_return(ret);

    return count;
}

static ssize_t stream_start_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    uint32_t start;

    ret = kstrtouint(buf, 10, &start);
    check_error_return(ret);

    if (start > 0)
    {
        mutex_lock(&haptic_data->lock);
        haptic_data->stream_start = true;
        kfifo_reset(&haptic_data->stream_fifo);
        haptic_data->func->set_play_mode(haptic_data, PLAY_MODE_STREAM);
        haptic_data->func->clear_stream_fifo(haptic_data);
        haptic_data->func->play_go(haptic_data);
        mutex_unlock(&haptic_data->lock);
    }

    return count;
}

static ssize_t stream_data_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    ssize_t len = 0;
    uint32_t count;

    count = kfifo_avail(&haptic_data->stream_fifo);
    len += snprintf(buf + len, PAGE_SIZE - len, "%u", count);

    return len;
}

static int32_t send_stream_data(struct ics_haptic_data *haptic_data, uint32_t fifo_available_size)
{
    int32_t ret = 0;
    uint32_t buf_fifo_used = 0, size;

    if (haptic_data->gp_buf == NULL)
    {
        return 0;
    }

    buf_fifo_used = kfifo_len(&haptic_data->stream_fifo);
    size = min(fifo_available_size, buf_fifo_used);
    size = kfifo_out(&haptic_data->stream_fifo, haptic_data->gp_buf, size);
    if (size > 0)
    {
        ret = haptic_data->func->set_stream_data(haptic_data, haptic_data->gp_buf, size);
    }
    check_error_return(ret);

    return 0;
}

static ssize_t stream_data_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    uint32_t size = 0, chip_fifo_size = haptic_data->chip_config.fifo_size;
    uint32_t available = kfifo_avail(&haptic_data->stream_fifo);

    size = count;
    if (size > available)
    {
        ics_dbg("stream data size is bigger than stream fifo available size! \
            available=%u, size=%u\n", available, size);
        size = available;
    }
    if (size > 0)
    {
        kfifo_in(&haptic_data->stream_fifo, buf, size);
    }

    if (haptic_data->stream_start)
    {
        haptic_data->stream_start = false;

        ret = send_stream_data(haptic_data, chip_fifo_size);
        check_error_return(ret);
    }

    return count;
}

static ssize_t vbat_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    ssize_t len = 0;

    mutex_lock(&haptic_data->lock);
    ret = haptic_data->func->get_vbat(haptic_data);
    mutex_unlock(&haptic_data->lock);
    check_error_return(ret);

    len += snprintf(buf + len, PAGE_SIZE - len, "%u\n", haptic_data->chip_config.vbat);
    return len;
}

static ssize_t resistance_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    ssize_t len = 0;

    mutex_lock(&haptic_data->lock);
    ret = haptic_data->func->get_resistance(haptic_data);
    mutex_unlock(&haptic_data->lock);
    check_error_return(ret);

    len += snprintf(buf + len, PAGE_SIZE - len, "%u\n", haptic_data->chip_config.resistance);
    return len;
}

static ssize_t state_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    ssize_t len = 0;

    mutex_lock(&haptic_data->lock);
    ret = haptic_data->func->get_sys_state(haptic_data);
    mutex_unlock(&haptic_data->lock);
    check_error_return(ret);

    len += snprintf(buf + len, PAGE_SIZE - len, "%u\n", haptic_data->sys_state);
    return len;
}

static ssize_t reset_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    uint32_t reset = 0;

    ret = kstrtouint(buf, 10, &reset);
    check_error_return(ret);

    if (reset > 0)
    {
        if (gpio_is_valid(haptic_data->gpio_en))
        {
            haptic_hw_reset(haptic_data);
            ics_info("hardware reset successfully!\n");
        }
        else
        {
            ics_info("hardware reset gpio is NOT valid!\n");
        }
    }

    return count;
}
static ssize_t adc_offset_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    ssize_t len = 0;

    mutex_lock(&haptic_data->lock);
    ret = haptic_data->func->get_adc_offset(haptic_data);
    mutex_unlock(&haptic_data->lock);
    check_error_return(ret);

    len += snprintf(buf + len, PAGE_SIZE - len, "%u\n", haptic_data->adc_offset);
    return len;
}

static ssize_t brake_en_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    ssize_t len = 0;

    len += snprintf(buf + len, PAGE_SIZE - len, "%u\n", haptic_data->chip_config.brake_en);
    return len;
}

static ssize_t brake_en_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    uint32_t enable = 0;

    ret = kstrtouint(buf, 10, &enable);
    check_error_return(ret);

    enable = enable > 0 ? 1 : 0;
    ret = haptic_data->func->set_brake_en(haptic_data, enable);
    check_error_return(ret);

    return count;
}

static ssize_t daq_en_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    uint32_t enable = 0;

    sfdc_bemf_daq_clear();

    ret = kstrtouint(buf, 10, &enable);
    check_error_return(ret);
    enable = enable > 0 ? 1 : 0;
    ret = haptic_data->func->set_daq_en(haptic_data, enable);
    check_error_return(ret);

    return count;
}

static ssize_t f0_en_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    uint32_t enable = 0;

    ret = kstrtouint(buf, 10, &enable);
    check_error_return(ret);
    enable = enable > 0 ? 1 : 0;
    ret = haptic_data->func->set_f0_en(haptic_data, enable);
    check_error_return(ret);

    return count;
}

static ssize_t daq_data_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);

    if (haptic_data->daq_size > 0)
    {
        memcpy(buf, haptic_data->daq_data, haptic_data->daq_size);
    }

    return haptic_data->daq_size;
}

static ssize_t daq_duration_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    ssize_t len = 0;

    len += snprintf(buf + len, PAGE_SIZE - len, "%u\n", haptic_data->daq_duration);
    return len;
}

static ssize_t daq_duration_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    vib_dev_t *vdev = dev_get_drvdata(dev);
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);
    int32_t ret = 0;
    uint32_t reg_val = 0;

    ret = kstrtouint(buf, 10, &reg_val);
    check_error_return(ret);
    ret = haptic_data->daq_duration = reg_val;

    return count;
}




///////////////////////////////////////////////////////////////////////////////
// haptic sys attribute nodes
///////////////////////////////////////////////////////////////////////////////
static DEVICE_ATTR(chip_id, S_IWUSR | S_IRUGO, chip_id_show, NULL);
static DEVICE_ATTR(f0, S_IWUSR | S_IRUGO, f0_show, f0_store);
static DEVICE_ATTR(reg, S_IWUSR | S_IRUGO, reg_show, reg_store);
static DEVICE_ATTR(vbst, S_IWUSR | S_IRUGO, vbst_show, vbst_store);
static DEVICE_ATTR(gain, S_IWUSR | S_IRUGO, gain_show, gain_store);
static DEVICE_ATTR(seq, S_IWUSR | S_IRUGO, seq_show, seq_store);
static DEVICE_ATTR(loop, S_IWUSR | S_IRUGO, loop_show, loop_store);
static DEVICE_ATTR(rtp, S_IWUSR | S_IRUGO, NULL, rtp_store);
static DEVICE_ATTR(playlist, S_IWUSR | S_IRUGO, playlist_show, playlist_store);
static DEVICE_ATTR(waveform, S_IWUSR | S_IRUGO, waveform_show, waveform_store);
static DEVICE_ATTR(daq_data, S_IWUSR | S_IRUGO, daq_data_show, NULL);
static DEVICE_ATTR(daq_duration, S_IWUSR | S_IRUGO, daq_duration_show, daq_duration_store);
static DEVICE_ATTR(daq_en, S_IWUSR | S_IRUGO, NULL, daq_en_store);
static DEVICE_ATTR(brake_en, S_IWUSR | S_IRUGO, brake_en_show, brake_en_store);
static DEVICE_ATTR(f0_en, S_IWUSR | S_IRUGO, NULL, f0_en_store);
static DEVICE_ATTR(play, S_IWUSR | S_IRUGO, play_show, play_store);
static DEVICE_ATTR(stream_start, S_IWUSR | S_IRUGO, NULL, stream_start_store);
static DEVICE_ATTR(stream_data, S_IWUSR | S_IRUGO, stream_data_show, stream_data_store);
static DEVICE_ATTR(index, S_IWUSR | S_IRUGO, index_show, index_store);
static DEVICE_ATTR(duration, S_IWUSR | S_IRUGO, duration_show, duration_store);
static DEVICE_ATTR(activate, S_IWUSR | S_IRUGO, activate_show, activate_store);
static DEVICE_ATTR(vbat, S_IWUSR | S_IRUGO, vbat_show, NULL);
static DEVICE_ATTR(resistance, S_IWUSR | S_IRUGO, resistance_show, NULL);
static DEVICE_ATTR(state, S_IWUSR | S_IRUGO, state_show, NULL);
static DEVICE_ATTR(reset, S_IWUSR | S_IRUGO, NULL, reset_store);
static DEVICE_ATTR(adc_offset, S_IWUSR | S_IRUGO, adc_offset_show, NULL);

static DEVICE_ATTR(vibr_level, S_IWUSR | S_IRUGO, vibr_level_show, vibr_level_store);
static struct attribute *ics_haptic_attributes[] = {
    &dev_attr_chip_id.attr,
    &dev_attr_f0.attr,
    &dev_attr_reg.attr,
    &dev_attr_vbst.attr,
    &dev_attr_gain.attr,
    &dev_attr_seq.attr,
    &dev_attr_loop.attr,
    &dev_attr_rtp.attr,
    &dev_attr_playlist.attr,
    &dev_attr_waveform.attr,
    &dev_attr_daq_data.attr,
    &dev_attr_daq_duration.attr,
    &dev_attr_daq_en.attr,
    &dev_attr_brake_en.attr,
    &dev_attr_f0_en.attr,
    &dev_attr_play.attr,
    &dev_attr_stream_start.attr,
    &dev_attr_stream_data.attr,
    &dev_attr_index.attr,
    &dev_attr_duration.attr,
    &dev_attr_activate.attr,
    &dev_attr_vbat.attr,
    &dev_attr_resistance.attr,
    &dev_attr_state.attr,
    &dev_attr_reset.attr,
    &dev_attr_adc_offset.attr,
    &dev_attr_vibr_level.attr,
    NULL
};

static struct attribute_group ics_haptic_attribute_group = {
    .attrs = ics_haptic_attributes
};

static irqreturn_t ics_haptic_irq_handler(int irq, void *data)
{
    struct ics_haptic_data *haptic_data = data;
    int32_t ret = 0;
    uint32_t data_size, fifo_used;

    ret = haptic_data->func->get_irq_state(haptic_data);
    if (ret < 0)
    {
        goto irq_exit;
    }

    if (haptic_data->func->is_irq_protection(haptic_data))
    {
        ics_dbg("%s: clear protection\n", __func__);
        ret = haptic_data->func->clear_protection(haptic_data);
        return ret;
    }
    else if (haptic_data->func->is_irq_play_done(haptic_data))
    {
        if (haptic_data->daq_en == 1)
        {
            //ics_dbg("%s: start acquiring bemf data\n", __func__);
            haptic_data->daq_en = 0;
            ret = haptic_data->func->get_daq_data(haptic_data,
                haptic_data->daq_data, &haptic_data->daq_size);
            sfdc_wakeup_bemf_daq_poll();
        }

        if (haptic_data->f0_en == 1)
        {
            haptic_data->f0_en = 0;
        }
    }
    ics_dbg("%s: irq state = 0x%02X\n", __func__, (uint8_t)(haptic_data->irq_state));

#ifdef AAC_RICHTAP_SUPPORT
    ret = richtap_irq_handler(haptic_data);
    if (ret >= 0)
    {
         return IRQ_HANDLED;
    }
#endif

    if (haptic_data->func->is_irq_fifo_ae(haptic_data))
    {
        if (haptic_data->gp_buf == NULL)
        {
            goto irq_exit;
        }
        data_size = haptic_data->chip_config.fifo_size - haptic_data->chip_config.fifo_ae;
        fifo_used = kfifo_len(&haptic_data->stream_fifo);
        data_size = min(data_size, fifo_used);
        data_size = kfifo_out(&haptic_data->stream_fifo, haptic_data->gp_buf, data_size);
        if (data_size > 0)
        {
            ret = haptic_data->func->set_stream_data(haptic_data, haptic_data->gp_buf, data_size);
            if (ret < 0)
            {
                goto irq_exit;
            }
        }
    }

irq_exit:
    return IRQ_HANDLED;
}

static void brake_guard_work_routine(struct work_struct *work)
{
    struct ics_haptic_data *haptic_data = container_of(work, struct ics_haptic_data,
                           brake_guard_work);

    int32_t ret = 0;
    int32_t brake_timeout = 0;

    while(1)
    {
        mutex_lock(&haptic_data->lock);
        ret = haptic_data->func->get_sys_state(haptic_data);
        mutex_unlock(&haptic_data->lock);
        if (ret < 0)
        {
            return;
        }

        if (haptic_data->sys_state == 0x02)
        {
            brake_timeout++;
            if (brake_timeout >= 25)  //over 50ms
            {
                haptic_data->chip_config.brake_en = 0;
                mutex_lock(&haptic_data->lock);
                ret = haptic_data->func->chip_init(haptic_data,
                    haptic_data->waveform_data, haptic_data->waveform_size);
                mutex_unlock(&haptic_data->lock);

                return;
            }
        }
        else if (haptic_data->sys_state == 0x01)
        {
            return;
        }

        mdelay(2);
    }
}

static enum hrtimer_restart vibrator_timer_func(struct hrtimer *timer)
{
    struct ics_haptic_data *haptic_data = container_of(timer, struct ics_haptic_data, timer);

    haptic_data->activate_state = 0;
    schedule_work(&haptic_data->vibrator_work);

    return HRTIMER_NORESTART;
}

static void vibrator_work_routine(struct work_struct *work)
{
    struct ics_haptic_data *haptic_data = container_of(work, struct ics_haptic_data,
                           vibrator_work);
    uint8_t buf[6];

    if (haptic_data->i2c_timout_state) {
        ics_info("i2c timeout !!!just return...\n");
        return;
    }
    mutex_lock(&haptic_data->lock);
    haptic_data->func->play_stop(haptic_data);
    if (haptic_data->activate_state)
    {
        buf[0] = 0x01;
        buf[1] = 0x00;
        buf[2] = 0x7F;
        buf[3] = 0x04;  //fixed num0 waveform buf[3] = (uint8_t)haptic_data->ram_wave_index;
        buf[4] = 0x00;
        buf[5] = 0x00;
        haptic_data->func->set_play_list(haptic_data, buf, sizeof(buf));
        haptic_data->func->set_play_mode(haptic_data, PLAY_MODE_RAM);
        haptic_data->func->play_go(haptic_data);
        // run ms time
        hrtimer_start(&haptic_data->timer, ktime_set(haptic_data->duration / 1000,
            (haptic_data->duration % 1000) * 1000000), HRTIMER_MODE_REL);
    }
    mutex_unlock(&haptic_data->lock);
}

static void preset_work_routine(struct work_struct *work)
{
    struct ics_haptic_data *haptic_data = container_of(work, struct ics_haptic_data, preset_work);
    int32_t ret = 0;
    const struct firmware *preset_file;
    int32_t data_size, src_offset, batch_size, dst_size;
    bool resample_flag;
    uint32_t chip_fifo_size = haptic_data->chip_config.list_base_addr;

    if (haptic_data->shutdown_flag) {
        ics_info("Drv has shutdown,just return...\n");
        return;
    }
    if (haptic_data->i2c_timout_state) {
        ics_info("i2c timeout !!!just return...\n");
        return;
    }

    mutex_lock(&haptic_data->preset_lock);
    ret = request_firmware(&preset_file,
                   preset_waveform_name[haptic_data->preset_wave_index], haptic_data->dev);
    if (ret < 0)
    {
        ics_err("%s: failed to read preset file %s\n", __func__,
            preset_waveform_name[haptic_data->preset_wave_index]);
        mutex_unlock(&haptic_data->preset_lock);
        return;
    } else {
        ics_info("request %s successful...", preset_waveform_name[haptic_data->preset_wave_index]);
    }

    resample_flag = (abs((int32_t)haptic_data->chip_config.f0
        - (int32_t)haptic_data->chip_config.sys_f0) > RESAMPLE_THRESHOLD);

    data_size = (resample_flag == true)
        ? (preset_file->size * haptic_data->chip_config.sys_f0 / haptic_data->chip_config.f0 + 1)
        : preset_file->size;
    if (data_size > MAX_STREAM_FIFO_SIZE)
    {
        kfifo_free(&haptic_data->stream_fifo);
        ret = kfifo_alloc(&haptic_data->stream_fifo, data_size, GFP_KERNEL);
        if (ret < 0)
        {
            ics_err("%s: failed to allocate fifo for stream!\n", __func__);
            return;
        }
    }
    kfifo_reset(&haptic_data->stream_fifo);
    ics_resample_reset();
    src_offset = 0;
    if (resample_flag == true)
    {
        while (src_offset < preset_file->size)
        {
            batch_size = min(haptic_data->chip_config.ram_size, (uint32_t)(preset_file->size - src_offset));
            dst_size = ics_resample(
                preset_file->data + src_offset,
                batch_size,
                haptic_data->chip_config.sys_f0,
                haptic_data->gp_buf,
                haptic_data->chip_config.ram_size,
                haptic_data->chip_config.f0);
            kfifo_in(&haptic_data->stream_fifo, haptic_data->gp_buf, dst_size);
            ics_info("%s: src_offset=%d, batch_size=%d, dst_size=%d", __func__, src_offset, batch_size, dst_size);
            src_offset += batch_size;
        }
    }
    else
    {
        kfifo_in(&haptic_data->stream_fifo, preset_file->data, preset_file->size);
    }
    mutex_unlock(&haptic_data->preset_lock);
    release_firmware(preset_file);

    mutex_lock(&haptic_data->lock);
    haptic_data->func->play_stop(haptic_data);
    haptic_data->func->get_irq_state(haptic_data);
    haptic_data->func->clear_stream_fifo(haptic_data);
    haptic_data->func->set_gain(haptic_data, haptic_data->chip_config.gain);
    haptic_data->func->set_play_mode(haptic_data, PLAY_MODE_STREAM);
    haptic_data->func->play_go(haptic_data);

    send_stream_data(haptic_data, chip_fifo_size);
    mutex_unlock(&haptic_data->lock);
}

#ifdef TIMED_OUTPUT
static int vibrator_get_time(struct timed_output_dev *dev)
{
    struct ics_haptic_data *haptic_data = container_of(dev, struct ics_haptic_data, vib_dev);

    if (hrtimer_active(&haptic_data->timer))
    {
        ktime_t r = hrtimer_get_remaining(&haptic_data->timer);
        return ktime_to_ms(r);
    }
    return 0;
}

static void vibrator_enable(struct timed_output_dev *dev, int value)
{
    struct ics_haptic_data *haptic_data = container_of(dev, struct ics_haptic_data, vib_dev);

    mutex_lock(&haptic_data->lock);

    haptic_data->func->play_stop(haptic_data);
    if (value > 0)
    {
        //TODO:
        haptic_data->func->set_play_mode(haptic_data, PLAY_MODE_RAM);
        haptic_data->func->play_go(haptic_data);
    }
    mutex_unlock(&haptic_data->lock);
}
#else
static enum led_brightness brightness_get(struct led_classdev *vdev)
{
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);

    return haptic_data->activate_state;
}

static void brightness_set(struct led_classdev *vdev, enum led_brightness level)
{
    struct ics_haptic_data *haptic_data = container_of(vdev, struct ics_haptic_data, vib_dev);

    if (haptic_data->activate_ignore) {
        haptic_data->activate_ignore = 0;
        return;
    }
    if (haptic_data->i2c_timout_state) {
        ics_info("%s : i2c timeout !!!just return...\n", __func__);
        return;
    }

    mutex_lock(&haptic_data->lock);

    hrtimer_cancel(&haptic_data->timer);
    haptic_data->activate_state = level;

    haptic_data->func->play_stop(haptic_data);
    haptic_data->func->set_gain(haptic_data, haptic_data->chip_config.gain);
    haptic_data->func->set_play_mode(haptic_data, PLAY_MODE_RAM);
    haptic_data->func->play_go(haptic_data);
    mutex_unlock(&haptic_data->lock);
}
#endif

static int32_t vibrator_init(struct ics_haptic_data *haptic_data)
{
    int ret = 0;

#ifdef TIMED_OUTPUT
    ics_info("%s: TIMED_OUTPUT framework!\n", __func__);
    haptic_data->vib_dev.name = haptic_data->vib_name;
    haptic_data->vib_dev.get_time = vibrator_get_time;
    haptic_data->vib_dev.enable = vibrator_enable;

    ret = timed_output_dev_register(&haptic_data->vib_dev);
    if (ret < 0)
    {
        ics_err("%s: failed to create timed output dev!\n", __func__);
        return ret;
    }
    ret = sysfs_create_group(&haptic_data->vib_dev.dev->kobj,
                 &ics_haptic_attribute_group);
    if (ret < 0)
    {
        ics_err("%s: failed to create sysfs attr files!\n", __func__);
        return ret;
    }
#else
    ics_info("%s: led cdev framework!\n", __func__);
    haptic_data->vib_dev.name = haptic_data->vib_name;
    haptic_data->vib_dev.brightness_get = brightness_get;
    haptic_data->vib_dev.brightness_set = brightness_set;
    ret = devm_led_classdev_register(&haptic_data->client->dev, &haptic_data->vib_dev);
    if (ret < 0)
    {
        ics_err("%s: fail to create led dev\n", __func__);
        return ret;
    }
    ret = sysfs_create_group(&haptic_data->vib_dev.dev->kobj, &ics_haptic_attribute_group);
    if (ret < 0)
    {
        ics_err("%s: error creating sysfs attr files\n", __func__);
        return ret;
    }
#endif
    hrtimer_init(&haptic_data->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    haptic_data->timer.function = vibrator_timer_func;
    INIT_WORK(&haptic_data->vibrator_work, vibrator_work_routine);
    INIT_WORK(&haptic_data->preset_work, preset_work_routine);
    mutex_init(&haptic_data->lock);
    mutex_init(&haptic_data->preset_lock);

    return 0;
}

static void load_chip_config(const struct firmware *config_fw, void *context)
{
    struct ics_haptic_data *haptic_data = context;
    int ret = 0;

    ics_info("load chip config\n");

    if (!config_fw)
    {
        ics_err("%s: failed to read %s\n", __func__, haptic_config_name);
        release_firmware(config_fw);
        return;
    }
    ics_info("chip config firmware size = %lu\n", config_fw->size);
    haptic_data->waveform_size = config_fw->size;
    haptic_data->waveform_data = vmalloc(config_fw->size);
    if (haptic_data->waveform_data == NULL)
    {
        ics_err("%s: failed to allocate memory for waveform data\n", __func__);
        goto on_chip_config_err;
    }
    memcpy(haptic_data->waveform_data, (uint8_t*)(config_fw->data), config_fw->size);
    ret = haptic_data->func->chip_init(haptic_data,
        haptic_data->waveform_data, haptic_data->waveform_size);
    if (ret)
    {
        ics_err("%s: failed to initialize chip!\n", __func__);
    }
    else
    {
        haptic_data->chip_initialized = true;
    }

on_chip_config_err:

    release_firmware(config_fw);
}

static void chip_init_work_routine(struct work_struct *work)
{
    struct ics_haptic_data *haptic_data = container_of(work, struct ics_haptic_data, chip_init_work.work);

    haptic_data->chip_initialized = false;
    request_firmware_nowait(
        THIS_MODULE,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 1)
        FW_ACTION_UEVENT,
#else
        FW_ACTION_HOTPLUG,
#endif
        haptic_config_name, haptic_data->dev, GFP_KERNEL,
        haptic_data, load_chip_config);
}

static void initialize_chip(struct ics_haptic_data *haptic_data)
{
    int ram_timer_val = 8000;

    INIT_DELAYED_WORK(&haptic_data->chip_init_work, chip_init_work_routine);
    schedule_delayed_work(&haptic_data->chip_init_work, msecs_to_jiffies(ram_timer_val));
}

static int32_t haptic_parse_dt(struct ics_haptic_data *haptic_data)
{
    const char *str_val = NULL;
    struct device_node *dev_node = haptic_data->dev->of_node;
    int ret = 0;
    if (IS_ERR_OR_NULL(dev_node))
    {
        ics_err("%s: no device tree node was found\n", __func__);
        return -EINVAL;
    }

    haptic_data->gpio_en = of_get_named_gpio(dev_node, "gpio-en", 0);
    if (haptic_data->gpio_en < 0)
    {
        ics_err("%s: no gpio-en provided\n", __func__);
        return -EPERM;
    }
    else
    {
        ics_info("Get gpio-en : %d\n", haptic_data->gpio_en);
    }

    haptic_data->gpio_irq = of_get_named_gpio(dev_node, "gpio-irq", 0);
    if (haptic_data->gpio_irq < 0)
    {
        ics_err("%s: no gpio-irq provided\n", __func__);
        return -EPERM;
    }
    else
    {
        ics_info("Get gpio-irq : %d\n", haptic_data->gpio_irq);
    }

    if (of_property_read_string(dev_node, "device-name", &str_val))
    {
        ics_err("%s: can NOT find device name in DT!\n", __func__);
        memcpy(haptic_data->vib_name, DEFAULT_DEV_NAME, sizeof(DEFAULT_DEV_NAME));
    }
    else
    {
        memcpy(haptic_data->vib_name, str_val, strlen(str_val));
        ics_info("provided device name is : %s\n", haptic_data->vib_name);
    }

    if (of_property_read_string(dev_node, "richtap-name", &str_val))
    {
        ics_err("%s: can NOT find richtap name in DT!\n", __func__);
        memcpy(haptic_data->richtap_misc_name, DEFAULT_RICHTAP_NAME, sizeof(DEFAULT_RICHTAP_NAME));
    }
    else
    {
        memcpy(haptic_data->richtap_misc_name, str_val, strlen(str_val));
        ics_info("provided richtap name is : %s\n", haptic_data->richtap_misc_name);
    }

    if (gpio_is_valid(haptic_data->gpio_en)) {
        ret = devm_gpio_request_one(&haptic_data->client->dev, haptic_data->gpio_en,
                        GPIOF_OUT_INIT_LOW, "haptic_rst");
        if (ret) {
            ics_err("rst request failed \n");
            return ret;
        }
    }
    if (gpio_is_valid(haptic_data->gpio_irq)) {
        ret = devm_gpio_request_one(&haptic_data->client->dev, haptic_data->gpio_irq,
                        GPIOF_DIR_IN, "haptic_irq");
        if (ret) {
            ics_err("irq request failed \n");
            return ret;
        }
    }
    return 0;
}

int32_t haptic_hw_reset(struct ics_haptic_data *haptic_data)
{
    ics_info("haptic hw reset!\n");
    gpio_set_value_cansleep(haptic_data->gpio_en, 0);
    usleep_range(1000, 2000);
    gpio_set_value_cansleep(haptic_data->gpio_en, 1);
    usleep_range(500, 600);
    ics_info("haptic hw reset end!\n");
    return 0;
}

static struct regmap_config ics_haptic_regmap =
{
    .reg_bits = 8,
    .val_bits = 8,
};

u32 boot_mode = 11;//Unknown Default
#define KERNEL_POWER_OFF_CHARGING_BOOT 8
#define LOW_POWER_OFF_CHARGING_BOOT 9
#define RECOVERY_BOOT 2
struct tag_bootmode {
    u32 size;
    u32 tag;
    u32 bootmode;
    u32 boottype;
};

static u32 tran_get_boot_mode(void)
{
    struct device_node *boot_node = NULL;
    struct tag_bootmode *tags = NULL;

    boot_node = of_find_node_by_path("/chosen");
    if (!boot_node) {
        ics_err("of_find_node_by_path chosen error \n");
        return 11;
    } else {
        tags = (struct tag_bootmode *)of_get_property(boot_node,"atag,boot", NULL);
        if (tags) {
            ics_info("tran_tp_get_boot_mode is %x \n",tags->bootmode);
            return tags->bootmode;
        } else {
            ics_err("tran_tp_get_boot_mode error \n");
            return 11;
        }
    }
}

static int ics_haptic_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int32_t ret = 0;
    struct ics_haptic_data *haptic_data = NULL;
    struct device* dev = &client->dev;

    ics_info("ics haptic driver! ver: %s\n", ICS_HAPTIC_VERSION);
    ics_info("ics haptic probe! addr=0x%X\n", client->addr);

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
    {
        ics_err("%s: failed to check i2c functionality!\n", __func__);
        return -EIO;
    }

    haptic_data = devm_kzalloc(&client->dev, sizeof(struct ics_haptic_data), GFP_KERNEL);
    if (IS_ERR_OR_NULL(haptic_data))
    {
        ics_err("%s: failed to allocate memory for ics haptic data!\n", __func__);
        ret = -ENOMEM;
        goto probe_err;
    }

    haptic_data->dev = dev;
    haptic_data->client = client;
    dev_set_drvdata(dev, haptic_data);
    i2c_set_clientdata(client, haptic_data);

    haptic_data->regmap = devm_regmap_init_i2c(client, &ics_haptic_regmap);
    if (IS_ERR(haptic_data->regmap))
    {
        ret = PTR_ERR(haptic_data->regmap);
        ics_err("%s: failed to initialize register map: %d\n", __func__, ret);
        goto probe_err;
    }

    // TODO: assign function list according to chip id
    haptic_data->func = &rt6010_func_list;

    // enable and irp gpio configuration
    haptic_parse_dt(haptic_data);
    if (gpio_is_valid(haptic_data->gpio_en))
    {
        haptic_hw_reset(haptic_data);
    }

    // following initialization steps are not necessary for group broadcast device
    if (client->addr == 0x5C)
    {
        return ret;
    }

    ret = haptic_data->func->get_chip_id(haptic_data);
    if (ret < 0)
    {
        ics_err("%s: failed to get chipid!\n", __func__);
        goto probe_err;
    }

    ret = vibrator_init(haptic_data);
    if (ret < 0)
    {
        ics_err("%s: failed to initialize vibrator interfaces! ret = %d\n", __func__, ret);
        goto probe_err;
    }

    ret = kfifo_alloc(&haptic_data->stream_fifo, MAX_STREAM_FIFO_SIZE, GFP_KERNEL);
    if (ret < 0)
    {
        ics_err("%s: failed to allocate fifo for stream!\n", __func__);
        ret = -ENOMEM;
        goto probe_err;
    }

    haptic_data->gp_buf = kmalloc(GP_BUFFER_SIZE, GFP_KERNEL);
    if (haptic_data->gp_buf == NULL)
    {
        ics_err("%s: failed to allocate memory for gp buffer\n", __func__);
        goto probe_err;
    }

    haptic_data->daq_data = kmalloc(MAX_DAQ_BUF_SIZE, GFP_KERNEL);
    if (haptic_data->daq_data == NULL)
    {
        ics_err("%s: failed to allocate memory for daq data buffer\n", __func__);
        goto probe_err;
    }

    initialize_chip(haptic_data);
    INIT_WORK(&haptic_data->brake_guard_work, brake_guard_work_routine);

    // register irq handler
    if (gpio_is_valid(haptic_data->gpio_irq))
    {
        ret = devm_request_threaded_irq(dev, gpio_to_irq(haptic_data->gpio_irq),
            NULL, ics_haptic_irq_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
            ICS_HAPTIC_NAME, haptic_data);
        if (ret < 0)
        {
            ics_err("%s: failed to request threaded irq! ret = %d\n", __func__, ret);
            goto probe_err;
        }
    }

    // initialize input device
    ret = ics_input_dev_register(haptic_data);
    if (ret < 0)
    {
        ics_err("%s: failed to initialize input device! ret = %d\n", __func__, ret);
        goto probe_err;
    }

    // initialize sfdc misc device
    ret = sfdc_misc_register(haptic_data);
    if (ret < 0)
    {
        ics_err("%s: failed to register sfdc device! ret = %d\n", __func__, ret);
        goto probe_err;
    }

#ifdef AAC_RICHTAP_SUPPORT
    ret = richtap_misc_register(haptic_data);
    if (ret < 0)
    {
        ics_err("%s: failed to initialize richtap device! ret = %d\n", __func__, ret);
        goto probe_err;
    }
#endif

    return 0;

probe_err:
    if (haptic_data != NULL)
    {
        if (kfifo_initialized(&haptic_data->stream_fifo))
        {
            kfifo_free(&haptic_data->stream_fifo);
        }
        if (haptic_data->gp_buf != NULL)
        {
            kfree(haptic_data->gp_buf);
            haptic_data->gp_buf = NULL;
        }
        if (haptic_data->daq_data != NULL)
        {
            kfree(haptic_data->daq_data);
            haptic_data->daq_data = NULL;
        }
        //devm_free_irq(&client->dev, gpio_to_irq(haptic_data->gpio_irq), haptic_data);
        if (gpio_is_valid(haptic_data->gpio_irq))
        {
            gpio_free(haptic_data->gpio_irq);
        }

        //devm_free_irq(&client->dev, gpio_to_irq(haptic_data->gpio_irq), haptic_data);
        if (gpio_is_valid(haptic_data->gpio_en))
        {
            gpio_free(haptic_data->gpio_en);
        }
        //devm_led_classdev_unregister(&haptic_data->client->dev, &haptic_data->vib_dev);
        //devm_kfree(&client->dev, haptic_data);
        //haptic_data = NULL;
    }
    ics_err("%s failed\n", __func__);

    return ret;
}

static void ics_haptic_remove(struct i2c_client *client)
{
    struct ics_haptic_data *haptic_data = i2c_get_clientdata(client);

#ifdef AAC_RICHTAP_SUPPORT
    richtap_misc_remove(haptic_data);
#endif
    sfdc_misc_remove(haptic_data);
    ics_input_dev_remove(haptic_data);

    cancel_work_sync(&haptic_data->preset_work);
    cancel_work_sync(&haptic_data->vibrator_work);
    cancel_work_sync(&haptic_data->brake_guard_work);
    hrtimer_cancel(&haptic_data->timer);

    mutex_destroy(&haptic_data->lock);
    mutex_destroy(&haptic_data->preset_lock);

    kfifo_free(&haptic_data->stream_fifo);
    if (haptic_data->gp_buf != NULL)
    {
        kfree(haptic_data->gp_buf);
    }
    if (haptic_data->daq_data != NULL)
    {
        kfree(haptic_data->daq_data);
    }

    devm_free_irq(&client->dev, gpio_to_irq(haptic_data->gpio_irq), haptic_data);
    if (gpio_is_valid(haptic_data->gpio_irq))
    {
        gpio_free(haptic_data->gpio_irq);
    }
    if (gpio_is_valid(haptic_data->gpio_en))
    {
        gpio_free(haptic_data->gpio_en);
    }

    devm_led_classdev_unregister(&haptic_data->client->dev, &haptic_data->vib_dev);
    devm_kfree(&client->dev, haptic_data);
}

static void ics_haptic_shutdown(struct i2c_client *client)
{
    struct ics_haptic_data *haptic_data = i2c_get_clientdata(client);

    ics_info("Enter %s \n", __func__);
    if (!IS_ERR_OR_NULL(haptic_data)) {
        haptic_data->shutdown_flag = true;
    }
}

static int __maybe_unused ics_haptic_suspend(struct device *dev)
{
    int ret = 0;

    return ret;
}

static int __maybe_unused ics_haptic_resume(struct device *dev)
{
    int ret = 0;

    return ret;
}

static SIMPLE_DEV_PM_OPS(ics_haptic_pm_ops, ics_haptic_suspend, ics_haptic_resume);
static const struct i2c_device_id ics_haptic_id[] =
{
    { ICS_HAPTIC_NAME, 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, ics_haptic_id);

static struct of_device_id ics_haptic_dt_match[] =
{
    { .compatible = "ics,haptic_rt" },
    { },
};

static struct i2c_driver ics_haptic_driver =
{
    .driver =
    {
        .name = ICS_HAPTIC_NAME,
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(ics_haptic_dt_match),
        .pm = &ics_haptic_pm_ops,
    },
    .id_table = ics_haptic_id,
    .probe = ics_haptic_probe,
    .shutdown = ics_haptic_shutdown,
    .remove = ics_haptic_remove,
};

static int __init aac_i2c_init(void)
{
    int ret = 0;

    ics_info("aac_haptic driver init\n");
    boot_mode = tran_get_boot_mode();
    if (boot_mode == KERNEL_POWER_OFF_CHARGING_BOOT || boot_mode == LOW_POWER_OFF_CHARGING_BOOT || boot_mode == RECOVERY_BOOT) {
        return 0;
    }

    ret = i2c_add_driver(&ics_haptic_driver);
    if (ret) {
        ics_err("%s: fail to add aw_haptic device into i2c\n", __func__);
        return ret;
    }

    return 0;
}


static void __exit aac_i2c_exit(void)
{

    if (boot_mode != KERNEL_POWER_OFF_CHARGING_BOOT &&  boot_mode != LOW_POWER_OFF_CHARGING_BOOT && boot_mode != RECOVERY_BOOT)
        i2c_del_driver(&ics_haptic_driver);

}
module_init(aac_i2c_init);
module_exit(aac_i2c_exit);
//module_i2c_driver(ics_haptic_driver);

MODULE_DESCRIPTION("ICS Haptic Driver");
MODULE_AUTHOR("chenmaomao@icsense.com.cn, ICSense Semiconductor Co., Ltd");
MODULE_LICENSE("GPL");
