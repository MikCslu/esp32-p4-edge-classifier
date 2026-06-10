#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tasks/audio_inference_task.h"
#include "service/audio_classify_service.h"
#include "service/audio_frame_bus.h"
#include "service/event_service.h"
#include "runtime/task_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <stdint.h>

static const char *TAG = "AUDIO_INFERENCE";

#include "models/audio_model.h"

static int16_t *alloc_audio_frame_buffer(void)
{
    size_t size = AUDIO_WINDOW_SAMPLES * sizeof(int16_t);
    int16_t *buffer = static_cast<int16_t *>(heap_caps_malloc(size,
                                                             MALLOC_CAP_SPIRAM |
                                                             MALLOC_CAP_8BIT));
    if (buffer) {
        ESP_LOGI(TAG, "Audio inference window allocated in PSRAM (%u bytes)",
                 (unsigned)size);
        return buffer;
    }

    ESP_LOGW(TAG, "PSRAM audio inference window unavailable, using internal RAM");
    return static_cast<int16_t *>(heap_caps_malloc(size,
                                                  MALLOC_CAP_INTERNAL |
                                                  MALLOC_CAP_8BIT));
}

void audio_inference_task(void *pvParameters)
{
    esp_err_t ret = audio_cls_srv_init(
        reinterpret_cast<const char *>(audio_model_data),
        audio_model_data_len
    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio classification init failed");
        vTaskDelete(NULL);
        return;
    }

    int16_t *buffer = alloc_audio_frame_buffer();
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate inference buffer");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Audio inference task started");
    uint32_t processed_count = 0;

    while (1) {
        ret = audio_frame_bus_read_window(buffer, AUDIO_WINDOW_SAMPLES, UINT32_MAX);
        if (ret == ESP_ERR_INVALID_SIZE) {
            continue;
        }
        if (ret != ESP_OK) {
            continue;
        }

        cls_result_t result;
        int64_t start_us = esp_timer_get_time();
        ret = audio_cls_srv_process(buffer, AUDIO_WINDOW_SAMPLES, &result);
        int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
        audio_cls_srv_record_process_time((uint32_t)elapsed_ms,
                                          ret,
                                          ret == ESP_OK && result.triggered);
        processed_count++;
        if ((processed_count % 20) == 0 && ret == ESP_OK) {
            ESP_LOGI(TAG, "Audio result: class=%d confidence=%.3f time=%lld ms",
                     result.class_id,
                     (double)result.confidence,
                     (long long)elapsed_ms);
        }
        if (ret == ESP_OK && result.triggered) {
            event_t evt = {};
            evt.type = EVENT_AUDIO_CLASSIFICATION;
            evt.audio_result = result;
            evt.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
            event_srv_post(&evt);
            ESP_LOGI(TAG, "EVENT: class=%d confidence=%.3f",
                     result.class_id, result.confidence);
        }

        vTaskDelay(1);
    }
}
