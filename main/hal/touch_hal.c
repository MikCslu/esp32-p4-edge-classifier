/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file touch_hal.c
 * @brief Touch HAL — delegates to GT911 touch driver
 */

#include "hal/touch_hal.h"
#include "driver/touch/gt911.h"
#include "esp_log.h"

static const char *TAG = "TP_HAL";
static bool initialized = false;

static esp_err_t tp_init(void)
{
    if (initialized) return ESP_OK;

    esp_err_t ret = gt911_touch_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gt911_touch_init failed: %d", ret);
        return ret;
    }

    initialized = true;
    ESP_LOGI(TAG, "Touch HAL initialized");
    return ESP_OK;
}

static esp_err_t tp_read(touch_point_t *points, uint8_t *count, uint8_t max_points)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;

    gt911_point_t gt911_pts[max_points > 0 ? max_points : 1];
    uint8_t got = 0;

    esp_err_t ret = gt911_touch_read(gt911_pts, &got, max_points);
    if (ret != ESP_OK) {
        *count = 0;
        return ret;
    }

    *count = got;
    for (uint8_t i = 0; i < got; i++) {
        points[i].x = gt911_pts[i].x;
        points[i].y = gt911_pts[i].y;
        points[i].touched = gt911_pts[i].touched;
    }
    return ESP_OK;
}

static const touch_hal_t hal = {
    .init = tp_init,
    .read = tp_read,
};

const touch_hal_t *touch_hal_get_instance(void)
{
    return &hal;
}
