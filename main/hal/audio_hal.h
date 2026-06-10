#pragma once
#include "esp_err.h"

typedef enum {
    AUDIO_DIR_INPUT,
    AUDIO_DIR_OUTPUT,
    AUDIO_DIR_DUPLEX,
} audio_dir_t;

typedef struct {
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint8_t channels;
    audio_dir_t dir;
} audio_config_t;

typedef struct {
    esp_err_t (*init)(const audio_config_t *cfg);
    esp_err_t (*read)(int16_t *buffer, size_t frames, uint32_t timeout_ms);
    esp_err_t (*write)(const int16_t *buffer, size_t frames, uint32_t timeout_ms);
    esp_err_t (*set_volume)(uint8_t vol);  // 0-100
    esp_err_t (*deinit)(void);
} audio_hal_t;

const audio_hal_t *audio_hal_get_instance(void);
