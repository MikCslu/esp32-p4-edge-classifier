/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file gt911_touch.c
 * @brief GT911 touch controller driver — thin wrapper around BSP + esp_lcd_touch
 *
 * All BSP / esp_lcd_touch dependencies are confined to this file.
 * Higher layers (HAL, Service, App) include only driver/ headers.
 */

#include "driver/touch/gt911.h"
#include "bsp/esp-bsp.h"
#include "bsp/touch.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"

static const char *TAG = "GT911";
static esp_lcd_touch_handle_t s_tp = NULL;
static bool s_initialized = false;

esp_err_t gt911_touch_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    /* BSP touch init handles I2C init internally */
    bsp_touch_config_t cfg = {0};
    esp_err_t ret = bsp_touch_new(&cfg, &s_tp);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bsp_touch_new failed: %d", ret);
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "GT911 touch initialized");
    return ESP_OK;
}

esp_err_t gt911_touch_read(gt911_point_t *points, uint8_t *count, uint8_t max_points)
{
    if (!s_tp || !s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!points || !count || max_points == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Read raw data from touch controller */
    esp_err_t ret = esp_lcd_touch_read_data(s_tp);
    if (ret != ESP_OK) {
        *count = 0;
        return ret;
    }

    /* Get touch points */
    uint8_t pt_cnt = 0;
    esp_lcd_touch_point_data_t raw[CONFIG_ESP_LCD_TOUCH_MAX_POINTS];
    ret = esp_lcd_touch_get_data(s_tp, raw, &pt_cnt,
                                 max_points < CONFIG_ESP_LCD_TOUCH_MAX_POINTS
                                     ? max_points : CONFIG_ESP_LCD_TOUCH_MAX_POINTS);
    if (ret != ESP_OK) {
        *count = 0;
        return ret;
    }

    *count = pt_cnt;
    for (uint8_t i = 0; i < pt_cnt && i < max_points; i++) {
        points[i].x = raw[i].x;
        points[i].y = raw[i].y;
        points[i].touched = 1;
    }
    return ESP_OK;
}
