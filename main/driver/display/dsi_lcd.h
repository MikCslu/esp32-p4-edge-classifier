#pragma once
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize MIPI-DSI LCD + LVGL
 *
 * Wraps bsp_display_start() + bsp_display_backlight_on().
 * @param[out] disp LVGL display handle (can be NULL)
 * @return ESP_OK on success
 */
esp_err_t mipi_dsi_lcd_init(lv_display_t **disp);

/**
 * @brief Set backlight brightness
 * @param brightness 0–100 (%)
 */
esp_err_t mipi_dsi_lcd_backlight(uint8_t brightness);

/**
 * @brief Take LVGL mutex
 * @param timeout_ms 0 = block indefinitely
 * @return true if mutex acquired
 */
bool mipi_dsi_lcd_lock(uint32_t timeout_ms);

/**
 * @brief Release LVGL mutex
 */
void mipi_dsi_lcd_unlock(void);

/**
 * @brief Get LCD resolution
 */
void mipi_dsi_lcd_get_resolution(uint16_t *h_res, uint16_t *v_res);

#ifdef __cplusplus
}
#endif
