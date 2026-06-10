/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file mipi_dsi_lcd.c
 * @brief MIPI-DSI LCD driver — thin wrapper around P4 EV Board BSP
 *
 * All BSP dependencies are confined to this file. Higher layers
 * (HAL, Service, App) never include bsp/ headers.
 */

#include "driver/display/dsi_lcd.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"

static const char *TAG = "MIPI_DSI_LCD";
static lv_display_t *s_disp = NULL;
static bool s_initialized = false;

esp_err_t mipi_dsi_lcd_init(lv_display_t **disp)
{
    if (s_initialized) {
        if (disp) *disp = s_disp;
        return ESP_OK;
    }

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .hw_cfg = {
#if CONFIG_BSP_LCD_TYPE_HDMI
            .hdmi_resolution = BSP_HDMI_RES_1280x800,
#else
            .hdmi_resolution = BSP_HDMI_RES_NONE,
#endif
            .dsi_bus = {
                .phy_clk_src = 0,
                .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS,
            }
        },
        .flags = {
#if CONFIG_BSP_LCD_COLOR_FORMAT_RGB888
            .buff_dma = false,
#else
            .buff_dma = true,
#endif
            .buff_spiram = true,
            .sw_rotate = false,
        }
    };
    cfg.lvgl_port_cfg.task_priority = 7;
    cfg.lvgl_port_cfg.task_affinity = 1;
    cfg.lvgl_port_cfg.task_max_sleep_ms = 5;

    s_disp = bsp_display_start_with_config(&cfg);
    if (!s_disp) {
        ESP_LOGE(TAG, "bsp_display_start failed");
        return ESP_FAIL;
    }

    esp_err_t ret = bsp_display_backlight_on();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "bsp_display_backlight_on returned %d", ret);
    }

    s_initialized = true;
    if (disp) *disp = s_disp;

    ESP_LOGI(TAG, "MIPI-DSI LCD initialized (%dx%d)",
             BSP_LCD_H_RES, BSP_LCD_V_RES);
    return ESP_OK;
}

esp_err_t mipi_dsi_lcd_backlight(uint8_t brightness)
{
    if (brightness > 100) brightness = 100;
    return bsp_display_brightness_set((int)brightness);
}

bool mipi_dsi_lcd_lock(uint32_t timeout_ms)
{
    return bsp_display_lock(timeout_ms);
}

void mipi_dsi_lcd_unlock(void)
{
    bsp_display_unlock();
}

void mipi_dsi_lcd_get_resolution(uint16_t *h_res, uint16_t *v_res)
{
    if (h_res) *h_res = BSP_LCD_H_RES;
    if (v_res) *v_res = BSP_LCD_V_RES;
}
