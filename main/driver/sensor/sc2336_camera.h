#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize SC2336 camera sensor via V4L2
 *
 * Wraps bsp_camera_start() and opens /dev/video0.
 * @return ESP_OK on success
 */
esp_err_t sc2336_cam_init(void);

/**
 * @brief Get current camera stream information
 */
esp_err_t sc2336_cam_get_info(uint32_t *width, uint32_t *height,
                              uint32_t *pixelfmt, size_t *frame_size);

typedef struct {
    uint8_t *data;
    size_t bytes_used;
    size_t length;
    int index;
} sc2336_frame_ref_t;

/**
 * @brief Dequeue a captured frame without copying.
 *
 * The caller owns the dequeued buffer until sc2336_cam_release_frame() is called.
 */
esp_err_t sc2336_cam_acquire_frame(sc2336_frame_ref_t *out);

/**
 * @brief Return a previously acquired frame to the V4L2 driver.
 */
esp_err_t sc2336_cam_release_frame(int index);

/**
 * @brief Mark whether the LVGL preview page is actively consuming frames.
 *
 * Low-priority background users should avoid dequeuing V4L2 buffers while the
 * preview is active, because the preview displays mmap buffers zero-copy.
 */
void sc2336_cam_set_preview_active(bool active);
bool sc2336_cam_is_preview_active(void);

/**
 * @brief Grab a single frame
 *
 * @param[out] buffer     Pre-allocated buffer (RGB565, width×height×2 bytes)
 * @param[in]  buffer_size Buffer size in bytes
 * @return ESP_OK on success
 */
esp_err_t sc2336_cam_capture_frame(uint8_t *buffer, size_t buffer_size);

/**
 * @brief Grab a single frame and report copied byte count
 */
esp_err_t sc2336_cam_capture_frame_ex(uint8_t *buffer, size_t buffer_size,
                                      size_t *bytes_used);

/**
 * @brief Deinitialize camera
 */
esp_err_t sc2336_cam_deinit(void);

#ifdef __cplusplus
}
#endif
