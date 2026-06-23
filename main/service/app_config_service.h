#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t volume;
    bool motor_enabled;
    uint8_t motor_strength;
    uint8_t theme_id;
} app_device_settings_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_config_init(void);
esp_err_t app_config_load_audio_tuning(float *thresholds, float *margins, size_t count);
esp_err_t app_config_save_audio_class_tuning(int class_id, float threshold, float margin);
esp_err_t app_config_load_device_settings(app_device_settings_t *settings);
esp_err_t app_config_save_volume(uint8_t volume);
esp_err_t app_config_save_motor_enabled(bool enabled);
esp_err_t app_config_save_motor_strength(uint8_t percent);
esp_err_t app_config_save_theme(uint8_t theme_id);

#ifdef __cplusplus
}
#endif
