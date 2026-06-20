#include "service/touch_input_service.h"

#include "app/display_app.h"
#include "driver/display/dsi_lcd.h"
#include "hal/display_hal.h"
#include "hal/touch_hal.h"
#include "lvgl.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TOUCH_INPUT";

#define TOUCH_INPUT_TASK_STACK 3072
#define TOUCH_INPUT_TASK_PRIO  2
#define TOUCH_INPUT_TASK_CORE  1
#define TOUCH_POLL_MS          20

typedef struct {
    bool pressed;
    uint16_t x;
    uint16_t y;
    uint32_t sequence;
} touch_cache_t;

static portMUX_TYPE s_cache_lock = portMUX_INITIALIZER_UNLOCKED;
static touch_cache_t s_cache;
static lv_indev_t *s_indev;
static TaskHandle_t s_task;
static bool s_started;

static void touch_lvgl_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    touch_cache_t cache;
    portENTER_CRITICAL(&s_cache_lock);
    cache = s_cache;
    portEXIT_CRITICAL(&s_cache_lock);

    data->point.x = (lv_coord_t)cache.x;
    data->point.y = (lv_coord_t)cache.y;
    data->state = cache.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void touch_cache_update(bool pressed, uint16_t x, uint16_t y)
{
    portENTER_CRITICAL(&s_cache_lock);
    s_cache.pressed = pressed;
    if (pressed) {
        s_cache.x = x;
        s_cache.y = y;
    }
    s_cache.sequence++;
    portEXIT_CRITICAL(&s_cache_lock);
}

static void touch_input_task(void *arg)
{
    (void)arg;
    uint32_t error_count = 0;
    bool was_pressed = false;

    ESP_LOGI(TAG, "Touch polling task started");

    while (true) {
        touch_point_t points[1] = {0};
        uint8_t count = 0;
        esp_err_t ret = touch_hal_get_instance()->read(points, &count, 1);

        if (ret == ESP_OK) {
            error_count = 0;
            if (count > 0 && points[0].touched) {
                touch_cache_update(true, points[0].x, points[0].y);
                was_pressed = true;
            } else {
                if (was_pressed) {
                    touch_cache_update(false, 0, 0);
                    was_pressed = false;
                }
            }
        } else {
            if (was_pressed) {
                touch_cache_update(false, 0, 0);
                was_pressed = false;
            }
            error_count++;
            if ((error_count % 100) == 1) {
                ESP_LOGW(TAG, "Touch read failed: %d", ret);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));
    }
}

esp_err_t touch_input_service_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    if (!display_app_is_ready()) {
        ESP_LOGW(TAG, "Display app not ready, touch input disabled");
        return ESP_ERR_INVALID_STATE;
    }

    lv_display_t *disp = display_hal_get_lv_display();
    if (!disp) {
        ESP_LOGW(TAG, "No LVGL display, touch input disabled");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = touch_hal_get_instance()->init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Touch HAL init failed, touch disabled: %d", ret);
        return ret;
    }

    if (!mipi_dsi_lcd_lock(1000)) {
        ESP_LOGW(TAG, "Failed to lock LVGL for touch registration");
        return ESP_ERR_TIMEOUT;
    }

    s_indev = lv_indev_create();
    if (s_indev) {
        lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(s_indev, disp);
        lv_indev_set_read_cb(s_indev, touch_lvgl_read_cb);
        lv_indev_set_gesture_min_distance(s_indev, 35);
        lv_indev_set_gesture_min_velocity(s_indev, 3);
    }

    mipi_dsi_lcd_unlock();

    if (!s_indev) {
        ESP_LOGW(TAG, "LVGL touch input allocation failed");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(touch_input_task,
                                            "touch_in",
                                            TOUCH_INPUT_TASK_STACK,
                                            NULL,
                                            TOUCH_INPUT_TASK_PRIO,
                                            &s_task,
                                            TOUCH_INPUT_TASK_CORE);
    if (ok != pdPASS) {
        ESP_LOGW(TAG, "Failed to create touch polling task");
        return ESP_FAIL;
    }

    s_started = true;
    ESP_LOGI(TAG, "Touch input service started");
    return ESP_OK;
}
