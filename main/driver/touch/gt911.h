#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Single touch point */
typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t  touched;
} gt911_point_t;

/**
 * @brief Initialize GT911 touch controller via I2C
 *
 * Wraps bsp_i2c_init() + bsp_touch_new(). Must be called before read().
 * @return ESP_OK on success
 */
esp_err_t gt911_touch_init(void);

/**
 * @brief Read touch points
 *
 * @param[out] points     Array to receive touch data
 * @param[out] count      Number of valid points returned
 * @param[in]  max_points Max points to fill in points[]
 * @return ESP_OK on success
 */
esp_err_t gt911_touch_read(gt911_point_t *points, uint8_t *count, uint8_t max_points);

#ifdef __cplusplus
}
#endif
