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
/* LVGL 显示驱动句柄：register 后由 LVGL 持有，负责把绘制好的帧刷到面板 */
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
    /* LVGL 渲染适配器配置：它内部会创建一个渲染任务（优先级 4，绑核 1）。
     * 这就是"LVGL 独立线程"的来源——UI 逻辑线程和渲染线程靠锁同步。 */
    adapter_cfg.task_priority = 4;
    adapter_cfg.task_core_id = 1;
    /* 渲染任务空闲时的最小/最大休眠，动态调节帧率省电 */
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
    /* 注册显示驱动：告诉 LVGL 面板接口、分辨率、旋转、缓冲配置 */
    esp_lv_adapter_display_config_t disp_cfg = {
        .panel = s_panel,
        .panel_io = s_panel_io,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_MIPI_DSI,
            /* 面板物理 480x800 竖屏，旋转 90 度变成 800x480 横屏 */
            .rotation = ESP_LV_ADAPTER_ROTATE_90,
            .hor_res = BSP_LCD_H_RES,
            .ver_res = BSP_LCD_V_RES,
            /* 渲染缓冲高度 20 行（部分缓冲模式，省内存） */
            .buffer_height = 20,
            /* 渲染缓冲放 PSRAM（片内 RAM 留给系统/模型） */
            .use_psram = true,
            .enable_ppa_accel = false,
            .require_double_buffer = false,
        },
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_FULL,
        .te_sync = ESP_LV_ADAPTER_TE_SYNC_DISABLED(),
    };

    /* 注册成功后 LVGL 就有了一个 display 驱动，
     * 之后 lv_scr_load()/lv_obj_create() 都作用于它。 */
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
    /* 启动渲染循环：LVGL 渲染任务开始周期性执行 lv_timer_handler() */
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

/* LVGL 互斥锁：所有跨线程的 lv_* 操作必须先拿锁！
 * esp_lv_adapter 内部用 FreeRTOS 互斥锁实现，防的是渲染线程和 UI 线程
 * 同时操作对象树导致崩溃。timeout 0 = 无限等。 */
bool mipi_dsi_lcd_lock(uint32_t timeout_ms)
{
    return esp_lv_adapter_lock((int32_t)timeout_ms) == ESP_OK;
}

/* 释放 LVGL 锁（与 lock 配对，绝不能漏） */
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
