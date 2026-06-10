#include "lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "LVGL_PORT";

esp_err_t lvgl_port_init(void)
{
    ESP_LOGI(TAG, "LVGL port ready (managed by BSP via driver)");
    return ESP_OK;
}
