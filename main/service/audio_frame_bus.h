#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t queued;
    uint32_t overwritten;
    uint32_t post_failed;
} audio_frame_bus_stats_t;

esp_err_t audio_frame_bus_init(void);
esp_err_t audio_frame_bus_post(const int16_t *samples, uint32_t timeout_ms);
esp_err_t audio_frame_bus_receive(int16_t *samples, uint32_t timeout_ms);
esp_err_t audio_frame_bus_read_window(int16_t *samples,
                                      size_t sample_count,
                                      uint32_t timeout_ms);
void audio_frame_bus_get_stats(audio_frame_bus_stats_t *stats);

#ifdef __cplusplus
}
#endif
