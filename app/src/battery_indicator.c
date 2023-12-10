/*
 * Copyright (c) 2021 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>

#include <zmk/workqueue.h>
#include <zmk/activity.h>
#include <zmk/battery.h>
#include <zmk/event_manager.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static const struct device *const battery_indicator_dev =
    DEVICE_DT_GET(DT_CHOSEN(zmk_battery_indicator));

BUILD_ASSERT(DT_HAS_CHOSEN(zmk_battery),
             "CONFIG_ZMK_BATTERY_INDICATOR is enabled but no zmk,battery chosen node found");

BUILD_ASSERT(
    DT_HAS_CHOSEN(zmk_battery_indicator_map),
    "CONFIG_ZMK_BATTERY_INDICATOR is enabled but no zmk,battery_indicator_map chosen node found");

#define BATTERY_IND_NUM_LEDS DT_PROP_LEN(DT_CHOSEN(zmk_battery_indicator_map), led_channels)

#define GET_LED_CHANNEL_INDEX(idx, node_id)                                                        \
    DT_PROP(DT_PHANDLE_BY_IDX(node_id, led_channels, idx), index)

static uint32_t battery_indicator_map_array[] = {LISTIFY(
    BATTERY_IND_NUM_LEDS, GET_LED_CHANNEL_INDEX, (, ), DT_CHOSEN(zmk_battery_indicator_map))};

static uint8_t battery_indicator_color_map[] =
    DT_PROP(DT_CHOSEN(zmk_battery_indicator_map), colors);

// TODO: When indicator is on, set up a workqueue item to get the state of charge and update
// zmk_battery_indicator_map
// should use zmk_battery_state_of_charge from zmk/battery.h

static int set_brightness_all_leds(uint8_t brightness) {
    int err;

    uint8_t red = (CONFIG_ZMK_BATTERY_INDICATOR_COLOR >> 16) & 0xFF;
    uint8_t green = (CONFIG_ZMK_BATTERY_INDICATOR_COLOR >> 8) & 0xFF;
    uint8_t blue = CONFIG_ZMK_BATTERY_INDICATOR_COLOR & 0xFF;

    // Scaling the RGB components
    uint32_t scaled_red = (red * brightness) / 255;
    uint32_t scaled_green = (green * brightness) / 255;
    uint32_t scaled_blue = (blue * brightness) / 255;

    for (int i = 0; i < BATTERY_IND_NUM_LEDS; i++) {
        if (battery_indicator_color_map[i] == 0) {
            err = led_set_brightness(battery_indicator_dev, battery_indicator_map_array[i],
                                     scaled_red);
        } else if (battery_indicator_color_map[i] == 1) {
            err = led_set_brightness(battery_indicator_dev, battery_indicator_map_array[i],
                                     scaled_green);
        } else if (battery_indicator_color_map[i] == 2) {
            err = led_set_brightness(battery_indicator_dev, battery_indicator_map_array[i],
                                     scaled_blue);
        } else {
            LOG_ERR("Invalid color map value: %d", battery_indicator_color_map[i]);
            return -EINVAL;
        }

        if (err != 0) {
            LOG_ERR("Failed to update battery indicator LED %d: %d", i, err);
            return err;
        }
    }
    return err;
}

static int turn_off_indicator(void) { return set_brightness_all_leds(0); }

static int zmk_battery_indicator_update() {
    // TODO: get battery value and update zmk_battery_indicator_map
    uint8_t state_of_charge = zmk_battery_state_of_charge();

    LOG_DBG("Battery state of charge: %d", state_of_charge);

    int err = set_brightness_all_leds(state_of_charge);
    if (err) {
        LOG_ERR("Unable to update battery indicator: %d", err);
    }
    return err;
}

static void zmk_battery_indicator_update_work(struct k_work *work) {
    int err = zmk_battery_indicator_update();
    if (err) {
        LOG_ERR("Unable to update indicator %d", err);
    }
}

K_WORK_DEFINE(battery_indicator_update_work, zmk_battery_indicator_update_work);

static void zmk_battery_indicator_off_work(struct k_work *work) {
    int err = turn_off_indicator();
    if (err) {
        LOG_ERR("Unable to disable indicator %d", err);
    }
}

K_WORK_DEFINE(battery_indicator_off_work, zmk_battery_indicator_off_work);

static void zmk_battery_indicator_off_timer(struct k_timer *timer) {
    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &battery_indicator_off_work);
}

K_TIMER_DEFINE(battery_indicator_timer, zmk_battery_indicator_off_timer, NULL);

int zmk_battery_indicator_show() {
    k_timer_start(&battery_indicator_timer, K_SECONDS(CONFIG_ZMK_BATTERY_INDICATOR_DURATION),
                  K_NO_WAIT);
    return k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &battery_indicator_update_work);
}