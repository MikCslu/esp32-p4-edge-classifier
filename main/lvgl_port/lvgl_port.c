#include "lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "LVGL_PORT";

/* LVGL 移植入口：本工程的 LVGL 实际由 Waveshare BSP 的
 * esp_lv_adapter 管理（见 driver/display/dsi_lcd.c），这里只做兼容占位。 */
esp_err_t lvgl_port_init(void)
{
    ESP_LOGI(TAG, "LVGL port ready (managed by BSP via driver)");
    return ESP_OK;
}
