#pragma once
#include "esp_err.h"
#include "service/audio_types.h"
#include <stddef.h>
#include <stdint.h>

#define DEBOUNCE_FRAMES   5
#define CONFIDENCE_THRESH 0.85f
#define MAX_CLASSES       12

typedef audio_class_result_t cls_result_t;

typedef struct {
    uint32_t processed;
    uint32_t active_inferences;
    uint32_t idle_skipped;
    uint32_t triggered;
    uint32_t errors;
    uint32_t last_time_ms;
    uint32_t min_time_ms;
    uint32_t max_time_ms;
    uint32_t avg_time_ms;
} audio_cls_srv_stats_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_cls_srv_init(const char *model_data, size_t model_len);
esp_err_t audio_cls_srv_process(const int16_t *audio_frame, size_t frames, cls_result_t *result);
void audio_cls_srv_record_process_time(uint32_t elapsed_ms, esp_err_t status, bool triggered);
void audio_cls_srv_get_stats(audio_cls_srv_stats_t *stats);
void audio_cls_srv_set_threshold(float confidence);
float audio_cls_srv_get_threshold(void);
void audio_cls_srv_set_class_threshold(int class_id, float confidence);
float audio_cls_srv_get_class_threshold(int class_id);
void audio_cls_srv_set_class_margin(int class_id, float margin);
float audio_cls_srv_get_class_margin(int class_id);

#ifdef __cplusplus
}
#endif
