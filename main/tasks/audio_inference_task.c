#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tasks/audio_inference_task.h"
#include "service/audio_classify_service.h"
#include "service/audio_frame_bus.h"
#include "service/event_service.h"
#include "runtime/task_config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <stdint.h>

static const char *TAG = "AUDIO_INFERENCE";

#include "models/audio_model.h"

/* 单次推理超过 800ms 就告警（正常应远低于此） */
#define AUDIO_INFER_SLOW_WARN_MS    800

/* 推理窗口缓冲：1 秒 = 16000 个 int16 = 32KB，优先放 PSRAM，
 * 因为片内 RAM 很宝贵（还要留给 LVGL 渲染缓冲和系统）。 */
static int16_t *alloc_audio_frame_buffer(void)
{
    size_t size = AUDIO_WINDOW_SAMPLES * sizeof(int16_t);
    /* PSRAM 优先：32KB 的窗口放片内 RAM 太奢侈 */
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

/*
 * 音频推理任务（FreeRTOS 消费者角色）
 * ====================================================
 * 数据流：audio_frame_bus(环形缓冲) -> 本任务 -> audio_cls_srv(ESP-DL) -> 事件队列 -> UI
 *
 * 注意这是 .c 文件但按 C++ 编译（见 CMakeLists.txt），
 * 因为 ESP-DL 是 C++ 库，需要 static_cast/reinterpret_cast。
 */
void audio_inference_task(void *pvParameters)
{
    /* 加载嵌入 flash 的音频模型（audio_model.h 是编译期生成的数组） */
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
        /* 从帧总线取最近 1 秒窗口（内部是环形缓冲，会自动拼出最新 16000 点）。
         * 信号量通知：每来一帧信号量 +1，凑够一帧就唤醒这里。 */
        ret = audio_frame_bus_read_window(buffer,
                                          AUDIO_WINDOW_SAMPLES,
                                          UINT32_MAX);
        if (ret == ESP_ERR_INVALID_SIZE) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        if (ret != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        /* 推理：mel 频谱(128x100) -> 神经网络 -> 12 类结果 */
        cls_result_t result;
        int64_t start_us = esp_timer_get_time();
        /* 核心计算：DSP 预处理 + 模型推理，用 esp_timer 计时监控性能 */
        ret = audio_cls_srv_process(buffer, AUDIO_WINDOW_SAMPLES, &result);
        int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
        audio_cls_srv_record_process_time((uint32_t)elapsed_ms,
                                          ret,
                                          ret == ESP_OK && result.triggered);
        /* 性能告警：单帧推理太慢说明模型/内存带宽是瓶颈 */
        if (elapsed_ms > AUDIO_INFER_SLOW_WARN_MS) {
            ESP_LOGW(TAG, "Slow audio inference: %lld ms status=%s class=%d confidence=%.3f",
                     (long long)elapsed_ms,
                     esp_err_to_name(ret),
                     ret == ESP_OK ? result.class_id : -1,
                     ret == ESP_OK ? (double)result.confidence : 0.0);
        }
        processed_count++;
        if ((processed_count % 20) == 0 && ret == ESP_OK) {
            ESP_LOGI(TAG, "Audio result: class=%d confidence=%.3f time=%lld ms",
                     result.class_id,
                     (double)result.confidence,
                     (long long)elapsed_ms);
        }
        /* 只有"触发"的结果才发事件（内部已做消抖/阈值/冷却）：
         * 事件会进入全局事件队列，由 UI 任务消费并刷新界面。 */
        if (ret == ESP_OK && result.triggered) {
            event_t evt = {};
            evt.type = EVENT_AUDIO_CLASSIFICATION;
            evt.audio_result = result;
            evt.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
            event_srv_post(&evt);
            ESP_LOGI(TAG, "EVENT: class=%d confidence=%.3f",
                     result.class_id, result.confidence);
        }

        vTaskDelay(1);  /* 让出 1 tick，避免空转饿死低优先级任务 */
    }
}
