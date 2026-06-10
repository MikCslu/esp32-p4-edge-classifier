#pragma once

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_config_init(void);
esp_err_t app_config_load_audio_tuning(float *thresholds, float *margins, size_t count);
esp_err_t app_config_save_audio_class_tuning(int class_id, float threshold, float margin);

#ifdef __cplusplus
}
#endif
