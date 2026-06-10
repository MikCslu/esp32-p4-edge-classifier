#pragma once

#include "service/audio_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_AUDIO_CLASS_COUNT 12
#define APP_AUDIO_RECENT_MAX  16

typedef struct {
    int class_id;
    float confidence;
    uint32_t timestamp_ms;
} app_audio_recent_t;

typedef struct {
    uint32_t total_events;
    uint32_t class_counts[APP_AUDIO_CLASS_COUNT];
    float confidence_sum[APP_AUDIO_CLASS_COUNT];
    app_audio_recent_t recent[APP_AUDIO_RECENT_MAX];
    size_t recent_count;
    uint32_t last_timestamp_ms;
    int last_class_id;
    float last_confidence;
} app_audio_stats_t;

#ifdef __cplusplus
extern "C" {
#endif

const char *app_audio_class_name(int class_id);
const char *app_audio_class_title(int class_id);
bool app_audio_class_needs_attention(int class_id);
void app_state_record_audio(const audio_class_result_t *result, uint32_t timestamp_ms);
const app_audio_stats_t *app_state_get_audio(void);
float app_state_audio_avg_confidence(int class_id);

#ifdef __cplusplus
}
#endif
