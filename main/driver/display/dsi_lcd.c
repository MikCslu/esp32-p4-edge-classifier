/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file mipi_dsi_lcd.c
 * @brief MIPI-DSI LCD driver - thin wrapper around Waveshare ESP32-P4 BSP
 */

#include "driver/display/dsi_lcd.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_lv_adapter.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "MIPI_DSI_LCD";
static lv_display_t *s_disp = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_panel_io = NULL;
static bool s_initialized = false;
static bool s_started = false;

static void log_step_us(const char *step, int64_t start_us)
{
    ESP_LOGI(TAG, "%s done in %lld ms", step,
             (long long)((esp_timer_get_time() - start_us) / 1000));
}

esp_err_t mipi_dsi_lcd_init(lv_display_t **disp)
{
    if (s_initialized) {
        if (disp) {
            *disp = s_disp;
        }
        return ESP_OK;
    }

    int64_t t0 = esp_timer_get_time();

    esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_cfg.task_priority = 4;
    adapter_cfg.task_core_id = 1;
    adapter_cfg.task_min_delay_ms = 4;
    adapter_cfg.task_max_delay_ms = 20;
    esp_err_t ret = esp_lv_adapter_init(&adapter_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lv_adapter_init failed: %d", ret);
        return ret;
    }
    log_step_us("esp_lv_adapter_init", t0);

    t0 = esp_timer_get_time();
    ret = bsp_display_brightness_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bsp_display_brightness_init failed: %d", ret);
        return ret;
    }
    log_step_us("bsp_display_brightness_init", t0);

    t0 = esp_timer_get_time();
    ret = bsp_display_new(NULL, &s_panel, &s_panel_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bsp_display_new failed: %d", ret);
        return ret;
    }
    log_step_us("bsp_display_new", t0);

    t0 = esp_timer_get_time();
    esp_lv_adapter_display_config_t disp_cfg = {
        .panel = s_panel,
        .panel_io = s_panel_io,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_MIPI_DSI,
            .rotation = ESP_LV_ADAPTER_ROTATE_90,
            .hor_res = BSP_LCD_H_RES,
            .ver_res = BSP_LCD_V_RES,
            .buffer_height = 20,
            .use_psram = false,
            .enable_ppa_accel = false,
            .require_double_buffer = false,
        },
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .te_sync = ESP_LV_ADAPTER_TE_SYNC_DISABLED(),
    };

    s_disp = esp_lv_adapter_register_display(&disp_cfg);
    if (!s_disp) {
        ESP_LOGE(TAG, "esp_lv_adapter_register_display failed");
        return ESP_FAIL;
    }
    log_step_us("esp_lv_adapter_register_display", t0);

    s_initialized = true;
    if (disp) {
        *disp = s_disp;
    }

    ESP_LOGI(TAG, "Waveshare MIPI-DSI LCD registered without touch (%dx%d landscape)",
             BSP_LCD_V_RES, BSP_LCD_H_RES);
    return ESP_OK;
}

esp_err_t mipi_dsi_lcd_start(void)
{
    if (!s_initialized || !s_disp) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_started) {
        return ESP_OK;
    }

    int64_t t0 = esp_timer_get_time();
    esp_err_t ret = esp_lv_adapter_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lv_adapter_start failed: %d", ret);
        return ret;
    }
    log_step_us("esp_lv_adapter_start", t0);

    ret = bsp_display_backlight_on();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "bsp_display_backlight_on returned %d", ret);
    }

    s_started = true;
    ESP_LOGI(TAG, "LVGL rendering started");
    return ESP_OK;
}

esp_err_t mipi_dsi_lcd_backlight(uint8_t brightness)
{
    if (brightness > 100) {
        brightness = 100;
    }
    return bsp_display_brightness_set((int)brightness);
}

bool mipi_dsi_lcd_lock(uint32_t timeout_ms)
{
    return esp_lv_adapter_lock((int32_t)timeout_ms) == ESP_OK;
}

void mipi_dsi_lcd_unlock(void)
{
    esp_lv_adapter_unlock();
}

void mipi_dsi_lcd_get_resolution(uint16_t *h_res, uint16_t *v_res)
{
    if (h_res) {
        *h_res = BSP_LCD_V_RES;
    }
    if (v_res) {
        *v_res = BSP_LCD_H_RES;
    }
}
