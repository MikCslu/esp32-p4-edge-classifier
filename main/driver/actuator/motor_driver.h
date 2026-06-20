#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t motor_driver_init(void);
bool motor_driver_is_available(void);
void motor_driver_stop(void);
void motor_driver_set_power(uint8_t percent);

#ifdef __cplusplus
}
#endif
