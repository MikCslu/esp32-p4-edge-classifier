#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t alert_feedback_service_start(void);
void alert_feedback_trigger(int class_id, float confidence);
bool alert_feedback_service_is_running(void);

#ifdef __cplusplus
}
#endif
