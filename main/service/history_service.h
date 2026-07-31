#pragma once

#include "esp_err.h"
#include "service/audio_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HISTORY_AUDIO_MAX 128

typedef struct {
    uint32_t timestamp_ms;
    int8_t class_id;
    uint8_t confidence_pct;
    uint8_t source;
    uint8_t flags;
} history_record_t;

typedef enum {
    HISTORY_SOURCE_AUDIO = 0,
    HISTORY_SOURCE_VISUAL = 1,
} history_source_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t history_service_init(void);
esp_err_t history_record_audio(const audio_class_result_t *result,
                               uint32_t timestamp_ms,
                               bool attention);
size_t history_get_recent(history_record_t *out, size_t max);
size_t history_count(void);
esp_err_t history_clear(void);

#ifdef __cplusplus
}
#endif
