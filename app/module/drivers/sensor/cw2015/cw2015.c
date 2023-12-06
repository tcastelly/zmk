/*
 * Copyright (c) 2022 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT cellwise_cw2015

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/sensor.h>

#include "cw2015.h"

#define LOG_LEVEL CONFIG_SENSOR_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sensor_cw2015);

static int read_register(const struct device *dev, uint8_t reg, uint16_t *value) {

    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }

    struct cw2015_config *config = (struct cw2015_config *)dev->config;

    uint8_t data[2] = {0};
    int ret = i2c_burst_read_dt(&config->i2c_bus, reg, &data[0], sizeof(data));
    if (ret != 0) {
        LOG_DBG("i2c_write_read FAIL %d\n", ret);
        return ret;
    }

    // the register values are returned in big endian (MSB first)
    *value = sys_get_be16(data);
    return 0;
}

static int write_register(const struct device *dev, uint8_t reg, uint16_t value) {

    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }

    struct cw2015_config *config = (struct cw2015_config *)dev->config;

    uint8_t data[2] = {0};
    sys_put_be16(value, &data[0]);

    return i2c_burst_write_dt(&config->i2c_bus, reg, &data[0], sizeof(data));
}

static int set_sleep_enabled(const struct device *dev, bool sleep) {

    struct cw2015_drv_data *const drv_data = (struct cw2015_drv_data *const)dev->data;
    k_sem_take(&drv_data->lock, K_FOREVER);

    int err;

    uint16_t tmp = 0;

    if (sleep) {
        tmp |= 0xC0;
    }

    err = write_register(dev, REG_MODE, tmp);
    if (err != 0) {
        goto done;
    }

    LOG_DBG("sleep mode %s", sleep ? "enabled" : "disabled");

done:
    k_sem_give(&drv_data->lock);
    return err;
}

static int cw2015_sample_fetch(const struct device *dev, enum sensor_channel chan) {

    struct cw2015_drv_data *const drv_data = dev->data;
    k_sem_take(&drv_data->lock, K_FOREVER);

    int err = 0;

    if (chan == SENSOR_CHAN_GAUGE_STATE_OF_CHARGE || chan == SENSOR_CHAN_ALL) {
        err = read_register(dev, REG_STATE_OF_CHARGE, &drv_data->raw_state_of_charge);
        if (err != 0) {
            LOG_WRN("failed to read state-of-charge: %d", err);
            goto done;
        }
        LOG_DBG("read soc: %d", drv_data->raw_state_of_charge);
    }
    if (chan == SENSOR_CHAN_GAUGE_VOLTAGE || chan == SENSOR_CHAN_ALL) {
        err = read_register(dev, REG_VCELL, &drv_data->raw_vcell);
        if (err != 0) {
            LOG_WRN("failed to read vcell: %d", err);
            goto done;
        }
        LOG_DBG("read vcell: %d", drv_data->raw_vcell);
    }
    if (chan == SENSOR_CHAN_GAUGE_TIME_TO_EMPTY || chan == SENSOR_CHAN_ALL) {
        err = read_register(dev, REG_REMAINING_RUN_TIME, &drv_data->remaining_run_time);
        if (err != 0) {
            LOG_WRN("failed to read remaining run time: %d", err);
            goto done;
        }
        LOG_DBG("read remaining run time: %d", drv_data->remaining_run_time);

    } else {
        LOG_DBG("unsupported channel %d", chan);
        err = -ENOTSUP;
    }

done:
    k_sem_give(&drv_data->lock);
    return err;
}

static int cw2015_channel_get(const struct device *dev, enum sensor_channel chan,
                              struct sensor_value *val) {
    int err = 0;

    struct cw2015_drv_data *const drv_data = dev->data;
    k_sem_take(&drv_data->lock, K_FOREVER);

    struct cw2015_drv_data *const data = dev->data;
    unsigned int tmp = 0;

    switch (chan) {
    case SENSOR_CHAN_GAUGE_VOLTAGE:
        // 1250 / 16 = 78.125
        tmp = data->raw_vcell * 305;
        val->val1 = tmp / 1000000;
        val->val2 = tmp % 1000000;
        break;

    case SENSOR_CHAN_GAUGE_STATE_OF_CHARGE:
        val->val1 = (data->raw_state_of_charge >> 8);
        val->val2 = (data->raw_state_of_charge & 0xFF) * 1000000 / 256;
        break;

    case SENSOR_CHAN_GAUGE_TIME_TO_EMPTY:
        val->val1 = data->remaining_run_time;
        val->val2 = 0;
        break;

    default:
        err = -ENOTSUP;
        break;
    }

    k_sem_give(&drv_data->lock);
    return err;
}

static int cw2015_init(const struct device *dev) {
    struct cw2015_drv_data *drv_data = dev->data;
    const struct cw2015_config *config = dev->config;

    if (!device_is_ready(config->i2c_bus.bus)) {
        LOG_WRN("i2c bus not ready!");
        return -EINVAL;
    }

    // the functions below need the semaphore, so initialise it here
    k_sem_init(&drv_data->lock, 1, 1);

    // bring the device out of sleep
    set_sleep_enabled(dev, false);

    LOG_INF("device initialised at 0x%x", config->i2c_bus.addr);

    return 0;
}

static const struct sensor_driver_api cw2015_api_table = {.sample_fetch = cw2015_sample_fetch,
                                                          .channel_get = cw2015_channel_get};

#define CW2015_INIT(inst)                                                                          \
    static struct cw2015_config cw2015_##inst##_config = {.i2c_bus = I2C_DT_SPEC_INST_GET(inst)};  \
                                                                                                   \
    static struct cw2015_drv_data cw2015_##inst##_drvdata = {                                      \
        .raw_state_of_charge = 0,                                                                  \
        .remaining_run_time = 0,                                                                   \
        .raw_vcell = 0,                                                                            \
    };                                                                                             \
                                                                                                   \
    /* This has to init after I2C master */                                                        \
    DEVICE_DT_INST_DEFINE(inst, cw2015_init, NULL, &cw2015_##inst##_drvdata,                       \
                          &cw2015_##inst##_config, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,       \
                          &cw2015_api_table);

DT_INST_FOREACH_STATUS_OKAY(CW2015_INIT)
