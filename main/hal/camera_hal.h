#pragma once
#include "esp_err.h"

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t *buffer;
    size_t buffer_size;
} frame_t;

typedef struct {
    uint16_t width;
    uint16_t height;
} camera_config_t;

typedef struct {
    esp_err_t (*init)(const camera_config_t *cfg);
    esp_err_t (*capture)(frame_t *out);
    esp_err_t (*set_resolution)(uint16_t w, uint16_t h);
    esp_err_t (*deinit)(void);
} camera_hal_t;

const camera_hal_t *camera_hal_get_instance(void);
