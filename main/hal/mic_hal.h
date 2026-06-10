#pragma once
#include "esp_err.h"

typedef struct {
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint8_t channel;
} mic_config_t;

typedef struct {
    esp_err_t (*init)(const mic_config_t *cfg);
    esp_err_t (*read)(int16_t *buffer, size_t frames);
    esp_err_t (*deinit)(void);
} mic_hal_t;

const mic_hal_t *mic_hal_get_instance(void);
