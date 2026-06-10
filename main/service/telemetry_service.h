#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t telemetry_service_start(void);
esp_err_t telemetry_register_task(const char *name, TaskHandle_t handle);

#ifdef __cplusplus
}
#endif
