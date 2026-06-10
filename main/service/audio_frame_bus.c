#include "service/audio_frame_bus.h"

#include "runtime/task_config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "AUDIO_FRAME_BUS";

static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_frame_signal;
static int16_t *s_ring;
static size_t s_write_pos;
static size_t s_valid_samples;
static uint32_t s_signal_coalesced_count;
static uint32_t s_post_failed_count;

static TickType_t timeout_to_ticks(uint32_t timeout_ms)
{
    return timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
}

esp_err_t audio_frame_bus_init(void)
{
    if (s_ring) {
        return ESP_OK;
    }

    s_lock = xSemaphoreCreateMutex();
    s_frame_signal = xSemaphoreCreateBinary();
    if (!s_lock || !s_frame_signal) {
        ESP_LOGE(TAG, "Failed to create audio bus sync primitives");
        return ESP_ERR_NO_MEM;
    }

    size_t ring_size = AUDIO_WINDOW_SAMPLES * sizeof(int16_t);
    s_ring = (int16_t *)heap_caps_malloc(ring_size,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_ring) {
        s_ring = (int16_t *)heap_caps_malloc(ring_size,
                                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!s_ring) {
        ESP_LOGE(TAG, "Failed to allocate audio window ring (%u bytes)",
                 (unsigned)ring_size);
        return ESP_ERR_NO_MEM;
    }

    memset(s_ring, 0, ring_size);
    ESP_LOGI(TAG, "Audio window bus initialized (window=%d samples, frame=%d samples)",
             AUDIO_WINDOW_SAMPLES,
             AUDIO_FRAME_SAMPLES);
    return ESP_OK;
}

esp_err_t audio_frame_bus_post(const int16_t *samples, uint32_t timeout_ms)
{
    if (!samples) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ring || !s_lock || !s_frame_signal) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_lock, timeout_to_ticks(timeout_ms)) != pdTRUE) {
        s_post_failed_count++;
        return ESP_ERR_TIMEOUT;
    }

    size_t first = AUDIO_WINDOW_SAMPLES - s_write_pos;
    if (first > AUDIO_FRAME_SAMPLES) {
        first = AUDIO_FRAME_SAMPLES;
    }
    memcpy(&s_ring[s_write_pos], samples, first * sizeof(int16_t));
    if (first < AUDIO_FRAME_SAMPLES) {
        memcpy(s_ring, samples + first,
               (AUDIO_FRAME_SAMPLES - first) * sizeof(int16_t));
    }
    s_write_pos = (s_write_pos + AUDIO_FRAME_SAMPLES) % AUDIO_WINDOW_SAMPLES;
    if (s_valid_samples < AUDIO_WINDOW_SAMPLES) {
        s_valid_samples += AUDIO_FRAME_SAMPLES;
        if (s_valid_samples > AUDIO_WINDOW_SAMPLES) {
            s_valid_samples = AUDIO_WINDOW_SAMPLES;
        }
    }

    xSemaphoreGive(s_lock);

    if (xSemaphoreGive(s_frame_signal) != pdTRUE) {
        s_signal_coalesced_count++;
    }
    return ESP_OK;
}

esp_err_t audio_frame_bus_read_window(int16_t *samples,
                                      size_t sample_count,
                                      uint32_t timeout_ms)
{
    if (!samples || sample_count == 0 || sample_count > AUDIO_WINDOW_SAMPLES) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ring || !s_lock || !s_frame_signal) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_frame_signal, timeout_to_ticks(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_valid_samples < sample_count) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_SIZE;
    }

    size_t start = (s_write_pos + AUDIO_WINDOW_SAMPLES - sample_count) % AUDIO_WINDOW_SAMPLES;
    size_t first = AUDIO_WINDOW_SAMPLES - start;
    if (first > sample_count) {
        first = sample_count;
    }
    memcpy(samples, &s_ring[start], first * sizeof(int16_t));
    if (first < sample_count) {
        memcpy(samples + first, s_ring, (sample_count - first) * sizeof(int16_t));
    }

    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t audio_frame_bus_receive(int16_t *samples, uint32_t timeout_ms)
{
    return audio_frame_bus_read_window(samples, AUDIO_FRAME_SAMPLES, timeout_ms);
}

void audio_frame_bus_get_stats(audio_frame_bus_stats_t *stats)
{
    if (!stats) {
        return;
    }

    stats->queued = s_frame_signal ? (uint32_t)uxSemaphoreGetCount(s_frame_signal) : 0;
    stats->overwritten = s_signal_coalesced_count;
    stats->post_failed = s_post_failed_count;
}
