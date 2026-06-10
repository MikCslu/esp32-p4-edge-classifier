#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAMERA_PREVIEW_W 160
#define CAMERA_PREVIEW_H 94
#define CAMERA_PREVIEW_BYTES (CAMERA_PREVIEW_W * CAMERA_PREVIEW_H * 2)

typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t pixelformat;
    uint32_t sequence;
    bool byte_swap;
} camera_preview_info_t;

typedef struct {
    const uint8_t *data;
    size_t data_size;
    camera_preview_info_t info;
} camera_preview_frame_t;

typedef struct {
    uint32_t frames;
    uint32_t acquire_failed;
    uint32_t no_buffer;
    uint32_t ui_missed;
    uint32_t last_build_us;
    uint32_t min_build_us;
    uint32_t max_build_us;
    uint32_t avg_build_us;
} camera_service_stats_t;

esp_err_t camera_service_start(void);
TaskHandle_t camera_service_get_task_handle(void);
bool camera_service_is_ready(void);
esp_err_t camera_service_acquire_preview(uint32_t last_sequence,
                                         camera_preview_frame_t *frame);
esp_err_t camera_service_copy_preview(uint8_t *dst,
                                      size_t dst_size,
                                      camera_preview_info_t *info);
void camera_service_get_stats(camera_service_stats_t *stats);

#ifdef __cplusplus
}
#endif
