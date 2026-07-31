#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VISUAL_EMOTION_CLASS_COUNT 7

typedef enum {
    VISUAL_EMOTION_SURPRISE = 0,
    VISUAL_EMOTION_FEAR,
    VISUAL_EMOTION_DISGUST,
    VISUAL_EMOTION_HAPPY,
    VISUAL_EMOTION_SAD,
    VISUAL_EMOTION_ANGER,
    VISUAL_EMOTION_NEUTRAL,
} visual_emotion_class_t;

typedef struct {
    bool face_detected;
    int emotion_id;
    float emotion_confidence;
    float face_score;
    uint16_t face_x;
    uint16_t face_y;
    uint16_t face_w;
    uint16_t face_h;
    uint16_t rotation_deg;
    uint8_t crop_id;
    uint32_t timestamp_ms;
    uint32_t sequence;
    uint32_t detect_time_ms;
    uint32_t emotion_time_ms;
    uint32_t total_time_ms;
} visual_cls_result_t;

typedef struct {
    uint32_t processed;
    uint32_t faces;
    uint32_t emotions;
    uint32_t skipped_no_frame;
    uint32_t errors;
    uint32_t last_time_ms;
    uint32_t avg_time_ms;
    uint32_t min_time_ms;
    uint32_t max_time_ms;
    visual_cls_result_t last_result;
} visual_cls_srv_stats_t;

const char *visual_emotion_name(int emotion_id);
esp_err_t visual_cls_srv_init(void);
TaskHandle_t visual_cls_srv_get_task_handle(void);
void visual_cls_srv_get_stats(visual_cls_srv_stats_t *stats);

#ifdef __cplusplus
}
#endif
