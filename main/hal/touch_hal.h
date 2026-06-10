#pragma once
#include "esp_err.h"

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t touched;
} touch_point_t;

typedef struct {
    esp_err_t (*init)(void);
    esp_err_t (*read)(touch_point_t *points, uint8_t *count, uint8_t max_points);
} touch_hal_t;

const touch_hal_t *touch_hal_get_instance(void);
