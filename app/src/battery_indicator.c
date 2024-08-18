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

#define RED 0
#define GREEN 1
#define BLUE 2

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

static int set_bar_leds(uint32_t color, uint8_t bar_length) {
    int err;

    uint8_t red = (color >> 16) & 0xFF;
    uint8_t green = (color >> 8) & 0xFF;
    uint8_t blue = color & 0xFF;

    for (int i = 0; i < bar_length * 3; i++) {
        if (battery_indicator_color_map[i] == RED) {
            err = led_set_brightness(battery_indicator_dev, battery_indicator_map_array[i], red);
        } else if (battery_indicator_color_map[i] == GREEN) {
            err = led_set_brightness(battery_indicator_dev, battery_indicator_map_array[i], green);
        } else if (battery_indicator_color_map[i] == BLUE) {
            err = led_set_brightness(battery_indicator_dev, battery_indicator_map_array[i], blue);
        } else {
            LOG_ERR("Invalid color map value: %d", battery_indicator_color_map[i]);
            return -EINVAL;
        }
    }

    for (int i = bar_length * 3; i < BATTERY_IND_NUM_LEDS; i++) {
        err = led_set_brightness(battery_indicator_dev, battery_indicator_map_array[i], 0);
    }

    return err;
}

static int set_color_all_leds(uint32_t color, uint8_t state_of_charge) {

    int err;

    uint8_t red = (color >> 16) & 0xFF;
    uint8_t green = (color >> 8) & 0xFF;
    uint8_t blue = color & 0xFF;

    uint32_t scaled_red = (red * state_of_charge) / 100;
    uint32_t scaled_green = (green * state_of_charge) / 100;
    uint32_t scaled_blue = (blue * state_of_charge) / 100;

    for (int i = 0; i < BATTERY_IND_NUM_LEDS; i++) {
        if (battery_indicator_color_map[i] == RED) {
            err = led_set_brightness(battery_indicator_dev, battery_indicator_map_array[i],
                                     scaled_red);
        } else if (battery_indicator_color_map[i] == GREEN) {
            err = led_set_brightness(battery_indicator_dev, battery_indicator_map_array[i],
                                     scaled_green);
        } else if (battery_indicator_color_map[i] == BLUE) {
            err = led_set_brightness(battery_indicator_dev, battery_indicator_map_array[i],
                                     scaled_blue);
        } else {
            LOG_ERR("Invalid color map value: %d", battery_indicator_color_map[i]);
            return -EINVAL;
        }
    }
}

static int set_default_indicator(void) {
    int err;
    err = set_color_all_leds(CONFIG_ZMK_BATTERY_INDICATOR_DEFAULT_COLOR, 100);
    if (err) {
        LOG_ERR("Unable to set default indicator: %d", err);
    }
    return err;
}

static int zmk_battery_indicator_update() {
    // TODO: get battery value and update zmk_battery_indicator_map
    uint8_t state_of_charge = zmk_battery_state_of_charge();

    LOG_DBG("Battery state of charge: %d", state_of_charge);

    int err;

#ifdef CONFIG_ZMK_BATTERY_INDICATOR_BRIGHTNESS
    err = set_color_all_leds(CONFIG_ZMK_BATTERY_INDICATOR_COLOR, state_of_charge);
    if (err) {
        LOG_ERR("Unable to update battery indicator: %d", err);
    }
#endif

#ifdef CONFIG_ZMK_BATTERY_INDICATOR_BAR
    uint8_t bar_length = ((state_of_charge * BATTERY_IND_NUM_LEDS) / 100) / 3;
    err = set_bar_leds(CONFIG_ZMK_BATTERY_INDICATOR_COLOR, bar_length);
    if (err) {
        LOG_ERR("Unable to update battery indicator: %d", err);
    }
#endif

    return err;
}

static void zmk_battery_indicator_update_work(struct k_work *work) {
    int err = zmk_battery_indicator_update();
    if (err) {
        LOG_ERR("Unable to update indicator %d", err);
    }
}

K_WORK_DEFINE(battery_indicator_update_work, zmk_battery_indicator_update_work);

static void zmk_battery_indicator_default_work(struct k_work *work) {
    int err = set_default_indicator();
    if (err) {
        LOG_ERR("Unable to disable indicator %d", err);
    }
}

K_WORK_DEFINE(battery_indicator_default_work, zmk_battery_indicator_default_work);

static void zmk_battery_indicator_default_timer(struct k_timer *timer) {
    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &battery_indicator_default_work);
}

K_TIMER_DEFINE(battery_indicator_timer, zmk_battery_indicator_default_timer, NULL);

int zmk_battery_indicator_show() {
    k_timer_start(&battery_indicator_timer, K_SECONDS(CONFIG_ZMK_BATTERY_INDICATOR_DURATION),
                  K_NO_WAIT);
    return k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &battery_indicator_update_work);
}

int zmk_battery_indicator_init(void) { return set_default_indicator(); }

SYS_INIT(zmk_battery_indicator_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);