/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file display_hal.c
 * @brief Display HAL — delegates to MIPI-DSI LCD driver
 */

#include "hal/display_hal.h"
#include "driver/display/dsi_lcd.h"
#include "esp_log.h"

static const char *TAG = "DISP_HAL";
static lv_display_t *disp = NULL;
static bool initialized = false;

static esp_err_t disp_init(const display_config_t *cfg, lv_display_t **out_disp)
{
    (void)cfg;

    if (initialized) {
        if (out_disp) *out_disp = disp;
        return ESP_OK;
    }

    esp_err_t ret = mipi_dsi_lcd_init(&disp);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mipi_dsi_lcd_init failed");
        return ret;
    }

    initialized = true;
    if (out_disp) *out_disp = disp;

    uint16_t h, v;
    mipi_dsi_lcd_get_resolution(&h, &v);
    ESP_LOGI(TAG, "Display HAL initialized (%dx%d)", h, v);
    return ESP_OK;
}

/**
 * LVGL flush is handled internally by esp_lvgl_port — no-op to satisfy the interface.
 */
static esp_err_t disp_flush(int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                            const lv_color_t *color_map)
{
    (void)x1; (void)y1; (void)x2; (void)y2; (void)color_map;
    return ESP_OK;
}

static esp_err_t disp_backlight(uint8_t brightness)
{
    return mipi_dsi_lcd_backlight(brightness);
}

static const display_hal_t hal = {
    .init = disp_init,
    .flush = disp_flush,
    .backlight = disp_backlight,
};

const display_hal_t *display_hal_get_instance(void)
{
    return &hal;
}

lv_display_t *display_hal_get_lv_display(void)
{
    return disp;
}
