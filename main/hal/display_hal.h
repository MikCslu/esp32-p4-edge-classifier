#pragma once
#include "esp_err.h"
#include "lvgl.h"

typedef struct {
    uint16_t h_res;
    uint16_t v_res;
} display_config_t;

typedef struct {
    esp_err_t (*init)(const display_config_t *cfg, lv_display_t **disp);
    esp_err_t (*flush)(int16_t x1, int16_t y1, int16_t x2, int16_t y2, const lv_color_t *color_map);
    esp_err_t (*backlight)(uint8_t brightness);
} display_hal_t;

#ifdef __cplusplus
extern "C" {
#endif

const display_hal_t *display_hal_get_instance(void);
lv_display_t       *display_hal_get_lv_display(void);

#ifdef __cplusplus
}
#endif
