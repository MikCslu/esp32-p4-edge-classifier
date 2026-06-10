#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/display_hal.h"
#include "hal/audio_hal.h"
#include "driver/display/dsi_lcd.h"
#include "service/event_service.h"
#include "service/app_config_service.h"
#include "service/camera_service.h"
#include "service/audio_frame_bus.h"
#include "service/telemetry_service.h"
#include "app/audio_event_app.h"
#include "app/display_app.h"
#include "runtime/task_config.h"
#include "tasks/audio_capture_task.h"
#include "tasks/audio_inference_task.h"
#include "tasks/ui_task.h"

static const char *TAG = "APP_MAIN";

static esp_err_t start_pinned_task(TaskFunction_t task,
                                   const char *name,
                                   uint32_t stack,
                                   UBaseType_t priority,
                                   BaseType_t core,
                                   TaskHandle_t *handle)
{
    BaseType_t ok = xTaskCreatePinnedToCore(task,
                                            name,
                                            stack,
                                            NULL,
                                            priority,
                                            handle,
                                            core);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task %s", name);
        return ESP_FAIL;
    }
    return ESP_OK;
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESP32-P4 Edge Classifier v1.0");
    ESP_LOGI(TAG, "========================================");

    ESP_ERROR_CHECK(app_config_init());
    ESP_ERROR_CHECK(event_srv_init());
    ESP_ERROR_CHECK(audio_frame_bus_init());

    display_hal_get_instance()->init(NULL, NULL);

    audio_event_app_init();
    display_app_init();

    TaskHandle_t audio_capture_handle = NULL;
    TaskHandle_t audio_infer_handle = NULL;
    TaskHandle_t ui_handle = NULL;

    ESP_ERROR_CHECK(start_pinned_task(audio_capture_task,
                                      AUDIO_CAPTURE_TASK_NAME,
                                      AUDIO_CAPTURE_TASK_STACK,
                                      AUDIO_CAPTURE_TASK_PRIO,
                                      AUDIO_CAPTURE_TASK_CORE,
                                      &audio_capture_handle));
    ESP_ERROR_CHECK(start_pinned_task(audio_inference_task,
                                      AUDIO_INFER_TASK_NAME,
                                      AUDIO_INFER_TASK_STACK,
                                      AUDIO_INFER_TASK_PRIO,
                                      AUDIO_INFER_TASK_CORE,
                                      &audio_infer_handle));
    ESP_ERROR_CHECK(camera_service_start());
    ESP_ERROR_CHECK(start_pinned_task(ui_task,
                                      UI_TASK_NAME,
                                      UI_TASK_STACK,
                                      UI_TASK_PRIO,
                                      UI_TASK_CORE,
                                      &ui_handle));

    ESP_ERROR_CHECK(telemetry_register_task(AUDIO_CAPTURE_TASK_NAME, audio_capture_handle));
    ESP_ERROR_CHECK(telemetry_register_task(AUDIO_INFER_TASK_NAME, audio_infer_handle));
    ESP_ERROR_CHECK(telemetry_register_task(CAMERA_SERVICE_TASK_NAME, camera_service_get_task_handle()));
    ESP_ERROR_CHECK(telemetry_register_task(UI_TASK_NAME, ui_handle));
    ESP_ERROR_CHECK(telemetry_service_start());

    ESP_LOGI(TAG, "All tasks spawned. System ready.");

    vTaskDelete(NULL);
}
