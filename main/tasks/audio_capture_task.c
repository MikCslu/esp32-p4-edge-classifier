#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/audio_hal.h"
#include "service/audio_frame_bus.h"
#include "runtime/task_config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <stdint.h>

static const char *TAG = "AUDIO_CAPTURE";
static uint32_t s_drop_count = 0;
static uint32_t s_read_count = 0;

static int16_t *alloc_audio_frame_buffer(void)
{
    size_t size = AUDIO_FRAME_SAMPLES * sizeof(int16_t);
    int16_t *buffer = (int16_t *)heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (buffer) {
        ESP_LOGI(TAG, "Audio capture buffer allocated in internal RAM (%u bytes)", (unsigned)size);
        return buffer;
    }

    ESP_LOGW(TAG, "Internal RAM audio capture buffer unavailable, using PSRAM");
    return (int16_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void audio_capture_task(void *pvParameters)
{
    esp_err_t ret;

    // 初始化音频 HAL
    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_HZ,
        .bits_per_sample = 16,
        .channels = 1,
        .dir = AUDIO_DIR_INPUT,
    };

    ret = audio_hal_get_instance()->init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio HAL init failed");
        vTaskDelete(NULL);
        return;
    }

    // 创建队列
    int16_t *buffer = alloc_audio_frame_buffer();
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Audio capture task started");

    while (1) {
        ret = audio_hal_get_instance()->read(buffer, AUDIO_FRAME_SAMPLES, portMAX_DELAY);
        if (ret == ESP_OK) {
            s_read_count++;
            if ((s_read_count % 50) == 0) {
                int16_t min_sample = INT16_MAX;
                int16_t max_sample = INT16_MIN;
                uint64_t abs_sum = 0;
                uint64_t square_sum = 0;
                uint32_t near_zero = 0;

                for (int i = 0; i < AUDIO_FRAME_SAMPLES; i++) {
                    int32_t sample = buffer[i];
                    int32_t abs_sample = sample < 0 ? -sample : sample;
                    if (sample < min_sample) min_sample = (int16_t)sample;
                    if (sample > max_sample) max_sample = (int16_t)sample;
                    abs_sum += (uint32_t)abs_sample;
                    square_sum += (uint64_t)((int64_t)sample * (int64_t)sample);
                    if (abs_sample <= 8) near_zero++;
                }

                float mean_abs = (float)abs_sum / (float)AUDIO_FRAME_SAMPLES;
                float rms = sqrtf((float)((double)square_sum / (double)AUDIO_FRAME_SAMPLES));
                ESP_LOGI(TAG, "PCM stats: min=%d max=%d mean_abs=%.1f rms=%.1f near_zero=%lu/%d",
                         (int)min_sample,
                         (int)max_sample,
                         (double)mean_abs,
                         (double)rms,
                         (unsigned long)near_zero,
                         AUDIO_FRAME_SAMPLES);
            }

            if (audio_frame_bus_post(buffer, 10) != ESP_OK) {
                s_drop_count++;
                if ((s_drop_count % 50) == 1) {
                    ESP_LOGW(TAG, "Audio queue full, dropped %lu frames",
                             (unsigned long)s_drop_count);
                }
            }
        }
        // 100ms 周期由 audio HAL read 阻塞控制
    }
}
