#include "service/telemetry_service.h"

#include "service/audio_classify_service.h"
#include "service/audio_frame_bus.h"
#include "service/camera_service.h"
#include "runtime/task_config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TELEMETRY";

typedef struct {
    const char *name;
    TaskHandle_t handle;
} telemetry_task_entry_t;

static telemetry_task_entry_t s_tasks[TELEMETRY_MAX_TASKS];
static uint8_t s_task_count;
static TaskHandle_t s_telemetry_task;

esp_err_t telemetry_register_task(const char *name, TaskHandle_t handle)
{
    if (!name || !handle) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_task_count >= TELEMETRY_MAX_TASKS) {
        return ESP_ERR_NO_MEM;
    }

    s_tasks[s_task_count].name = name;
    s_tasks[s_task_count].handle = handle;
    s_task_count++;
    return ESP_OK;
}

static void telemetry_task(void *arg)
{
    (void)arg;

    int64_t last_log_us = esp_timer_get_time();
    uint32_t last_audio_processed = 0;
    uint32_t last_audio_active = 0;
    uint32_t last_camera_frames = 0;
    uint32_t log_count = 0;
    audio_cls_srv_stats_t initial_audio = {};
    audio_cls_srv_get_stats(&initial_audio);
    last_audio_processed = initial_audio.processed;
    last_audio_active = initial_audio.active_inferences;
    camera_service_stats_t initial_camera = {};
    camera_service_get_stats(&initial_camera);
    last_camera_frames = initial_camera.frames;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));

        int64_t now_us = esp_timer_get_time();
        float elapsed_s = (float)(now_us - last_log_us) / 1000000.0f;
        if (elapsed_s <= 0.0f) {
            elapsed_s = (float)TELEMETRY_PERIOD_MS / 1000.0f;
        }
        last_log_us = now_us;
        log_count++;

        size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

        ESP_LOGI(TAG, "heap internal=%u min=%u psram=%u largest=%u",
                 (unsigned)internal_free,
                 (unsigned)internal_min,
                 (unsigned)psram_free,
                 (unsigned)psram_largest);

        audio_frame_bus_stats_t audio_stats = {};
        audio_frame_bus_get_stats(&audio_stats);
        ESP_LOGI(TAG, "audio_bus signal=%u coalesced=%u post_failed=%u",
                 (unsigned)audio_stats.queued,
                 (unsigned)audio_stats.overwritten,
                 (unsigned)audio_stats.post_failed);

        audio_cls_srv_stats_t audio_cls = {};
        audio_cls_srv_get_stats(&audio_cls);
        uint32_t audio_processed_delta = audio_cls.processed - last_audio_processed;
        uint32_t audio_active_delta = audio_cls.active_inferences - last_audio_active;
        last_audio_processed = audio_cls.processed;
        last_audio_active = audio_cls.active_inferences;
        ESP_LOGI(TAG,
                 "audio_cls processed=%u active=%u idle_skip=%u trig=%u err=%u rate=%.1f/s active_rate=%.1f/s time_ms last=%u avg=%u min=%u max=%u",
                 (unsigned)audio_cls.processed,
                 (unsigned)audio_cls.active_inferences,
                 (unsigned)audio_cls.idle_skipped,
                 (unsigned)audio_cls.triggered,
                 (unsigned)audio_cls.errors,
                 (double)((float)audio_processed_delta / elapsed_s),
                 (double)((float)audio_active_delta / elapsed_s),
                 (unsigned)audio_cls.last_time_ms,
                 (unsigned)audio_cls.avg_time_ms,
                 (unsigned)audio_cls.min_time_ms,
                 (unsigned)audio_cls.max_time_ms);

        camera_service_stats_t camera_stats = {};
        camera_service_get_stats(&camera_stats);
        uint32_t camera_frames_delta = camera_stats.frames - last_camera_frames;
        last_camera_frames = camera_stats.frames;
        ESP_LOGI(TAG,
                 "camera frames=%u fps=%.1f acquire_failed=%u no_buffer=%u ui_same_frame=%u build_us last=%u avg=%u min=%u max=%u",
                 (unsigned)camera_stats.frames,
                 (double)((float)camera_frames_delta / elapsed_s),
                 (unsigned)camera_stats.acquire_failed,
                 (unsigned)camera_stats.no_buffer,
                 (unsigned)camera_stats.ui_missed,
                 (unsigned)camera_stats.last_build_us,
                 (unsigned)camera_stats.avg_build_us,
                 (unsigned)camera_stats.min_build_us,
                 (unsigned)camera_stats.max_build_us);

        if ((log_count % TELEMETRY_STACK_EVERY) == 0) {
            for (uint8_t i = 0; i < s_task_count; i++) {
                UBaseType_t water = uxTaskGetStackHighWaterMark(s_tasks[i].handle);
                ESP_LOGI(TAG, "stack %s high_water=%u words",
                         s_tasks[i].name,
                         (unsigned)water);
            }
        }

    }
}

esp_err_t telemetry_service_start(void)
{
    if (s_telemetry_task) {
        return ESP_OK;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(telemetry_task,
                                            TELEMETRY_TASK_NAME,
                                            TELEMETRY_TASK_STACK,
                                            NULL,
                                            TELEMETRY_TASK_PRIO,
                                            &s_telemetry_task,
                                            TELEMETRY_TASK_CORE);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}
