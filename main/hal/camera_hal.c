/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file camera_hal.c
 * @brief Camera HAL — delegates to SC2336 camera driver
 *
 * @note Camera capture is deferred (visual classification not yet implemented).
 *       This stub provides the init/deinit path while capture returns
 *       ESP_ERR_NOT_SUPPORTED.
 */

#include "hal/camera_hal.h"
#include "driver/sensor/sc2336_camera.h"
#include "esp_log.h"

static const char *TAG = "CAM_HAL";
static bool initialized = false;

static esp_err_t cam_init(const camera_config_t *cfg)
{
    (void)cfg;

    if (initialized) {
        return ESP_OK;
    }

    esp_err_t ret = sc2336_cam_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "sc2336_cam_init returned %d (camera may not be connected)", ret);
        // Do not fail — allow the system to boot without a camera.
    }

    initialized = true;
    ESP_LOGI(TAG, "Camera HAL initialized (visual capture deferred)");
    return ESP_OK;
}

static esp_err_t cam_capture(frame_t *out)
{
    (void)out;
    ESP_LOGW(TAG, "Camera capture not implemented (visual classification deferred)");
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t cam_set_resolution(uint16_t w, uint16_t h)
{
    (void)w;
    (void)h;
    ESP_LOGW(TAG, "Camera set_resolution not implemented (stub)");
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t cam_deinit(void)
{
    initialized = false;
    ESP_LOGI(TAG, "Camera HAL deinitialized");
    return ESP_OK;
}

static const camera_hal_t hal = {
    .init = cam_init,
    .capture = cam_capture,
    .set_resolution = cam_set_resolution,
    .deinit = cam_deinit,
};

const camera_hal_t *camera_hal_get_instance(void)
{
    return &hal;
}
