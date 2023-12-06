/*
 * Copyright (c) 2022 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REG_VCELL 0x02
#define REG_STATE_OF_CHARGE 0x04
#define REG_REMAINING_RUN_TIME 0x06
#define REG_CONFIG 0x08
#define REG_MODE 0x0A

struct cw2015_config {
    struct i2c_dt_spec i2c_bus;
};

struct cw2015_drv_data {
    struct k_sem lock;

    uint16_t raw_state_of_charge;
    uint16_t remaining_run_time;
    uint16_t raw_vcell;
};

#ifdef __cplusplus
}
#endif
