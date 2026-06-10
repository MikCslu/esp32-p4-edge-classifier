/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file sc2336_cam.c
 * @brief SC2336 camera sensor driver — V4L2 capture via esp_video
 *
 * ESP32-P4's MIPI-CSI pipeline is exposed as a V4L2 device (/dev/video0)
 * through the esp_video component. This driver uses standard V4L2 ioctls
 * for buffer management and frame capture.
 *
 * All V4L2 / BSP dependencies are confined to this file.
 */

#include "driver/sensor/sc2336_camera.h"
#include "bsp/esp-bsp.h"
#include "esp_video_device.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <linux/videodev2.h>

static const char *TAG = "SC2336";

/* ───────────────────────────────────────────── */
/*  V4L2 state                                    */
/* ───────────────────────────────────────────── */
#define CAM_DEVICE  BSP_CAMERA_DEVICE
#define NUM_BUFS    3
#define CAM_PREVIEW_WIDTH   1024
#define CAM_PREVIEW_HEIGHT  600

static int           s_fd         = -1;
static uint8_t      *s_v4l2_buf[NUM_BUFS] = {NULL};
static size_t        s_buf_len[NUM_BUFS]  = {0};
static uint32_t      s_width      = 0;
static uint32_t      s_height     = 0;
static uint32_t      s_pixelfmt   = 0;
static size_t        s_frame_size = 0;
static bool          s_streaming  = false;
static bool          s_bsp_started = false;
static volatile bool s_preview_active = false;
static SemaphoreHandle_t s_v4l2_lock = NULL;

/* ───────────────────────────────────────────── */
/*  Init                                          */
/* ───────────────────────────────────────────── */
esp_err_t sc2336_cam_init(void)
{
    if (s_fd >= 0) {
        return ESP_OK;  /* already initialized */
    }

    if (!s_v4l2_lock) {
        s_v4l2_lock = xSemaphoreCreateMutex();
        if (!s_v4l2_lock) {
            return ESP_ERR_NO_MEM;
        }
    }

    /* 1. BSP: init MIPI-CSI + sensor. esp_video devices are global, so do it once. */
    if (!s_bsp_started) {
        esp_err_t ret = bsp_camera_start(NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "bsp_camera_start failed: %d", ret);
            return ret;
        }
        s_bsp_started = true;
    }

    /* 2. Open V4L2 device */
    s_fd = open(CAM_DEVICE, O_RDWR);
    if (s_fd < 0) {
        ESP_LOGE(TAG, "Failed to open %s", CAM_DEVICE);
        return ESP_FAIL;
    }

    /* 3. Prefer a small RGB565 preview stream for LVGL. */
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = CAM_PREVIEW_WIDTH;
    fmt.fmt.pix.height = CAM_PREVIEW_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl(s_fd, VIDIOC_S_FMT, &fmt) < 0) {
        ESP_LOGW(TAG, "VIDIOC_S_FMT RGB565 %ux%u failed, using default format",
                 CAM_PREVIEW_WIDTH, CAM_PREVIEW_HEIGHT);
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    }

    if (ioctl(s_fd, VIDIOC_G_FMT, &fmt) < 0) {
        ESP_LOGE(TAG, "VIDIOC_G_FMT failed");
        close(s_fd);
        s_fd = -1;
        return ESP_FAIL;
    }

    s_width    = fmt.fmt.pix.width;
    s_height   = fmt.fmt.pix.height;
    s_pixelfmt = fmt.fmt.pix.pixelformat;
    s_frame_size = fmt.fmt.pix.sizeimage;
    if (s_frame_size == 0 && s_width > 0 && s_height > 0) {
        s_frame_size = s_width * s_height * 2;
    }
    ESP_LOGI(TAG, "Camera format: %lux%lu, pixelfmt=0x%08lx, size=%u",
             (unsigned long)s_width, (unsigned long)s_height,
             (unsigned long)s_pixelfmt, (unsigned int)s_frame_size);

    /* 4. Request buffers */
    struct v4l2_requestbuffers req = {
        .count  = NUM_BUFS,
        .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };
    if (ioctl(s_fd, VIDIOC_REQBUFS, &req) < 0) {
        ESP_LOGE(TAG, "VIDIOC_REQBUFS failed");
        close(s_fd);
        s_fd = -1;
        return ESP_FAIL;
    }

    /* 5. Query + mmap each buffer */
    for (int i = 0; i < NUM_BUFS; i++) {
        struct v4l2_buffer buf = {
            .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
            .index  = (uint32_t)i,
        };
        if (ioctl(s_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            ESP_LOGE(TAG, "VIDIOC_QUERYBUF[%d] failed", i);
            goto err;
        }
        s_buf_len[i] = buf.length;
        s_v4l2_buf[i] = mmap(NULL, buf.length,
                             PROT_READ | PROT_WRITE, MAP_SHARED,
                             s_fd, buf.m.offset);
        if (s_v4l2_buf[i] == MAP_FAILED) {
            ESP_LOGE(TAG, "mmap[%d] failed", i);
            s_v4l2_buf[i] = NULL;
            goto err;
        }
    }

    /* 6. Queue all buffers */
    for (int i = 0; i < NUM_BUFS; i++) {
        struct v4l2_buffer buf = {
            .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
            .index  = (uint32_t)i,
        };
        if (ioctl(s_fd, VIDIOC_QBUF, &buf) < 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF[%d] failed", i);
            goto err;
        }
    }

    /* 7. Start streaming */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_fd, VIDIOC_STREAMON, &type) < 0) {
        ESP_LOGE(TAG, "VIDIOC_STREAMON failed");
        goto err;
    }
    s_streaming = true;

    ESP_LOGI(TAG, "SC2336 camera initialized (%lux%lu, %d buffers)",
             (unsigned long)s_width, (unsigned long)s_height, NUM_BUFS);
    return ESP_OK;

err:
    sc2336_cam_deinit();
    return ESP_FAIL;
}

/* ───────────────────────────────────────────── */
/*  Capture single frame                          */
/* ───────────────────────────────────────────── */
esp_err_t sc2336_cam_get_info(uint32_t *width, uint32_t *height,
                              uint32_t *pixelfmt, size_t *frame_size)
{
    if (s_fd < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (width) {
        *width = s_width;
    }
    if (height) {
        *height = s_height;
    }
    if (pixelfmt) {
        *pixelfmt = s_pixelfmt;
    }
    if (frame_size) {
        *frame_size = s_frame_size;
    }
    return ESP_OK;
}

esp_err_t sc2336_cam_acquire_frame(sc2336_frame_ref_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_fd < 0 || !s_streaming) {
        return ESP_ERR_INVALID_STATE;
    }

    struct v4l2_buffer buf = {
        .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };

    /* Dequeue a filled buffer */
    xSemaphoreTake(s_v4l2_lock, portMAX_DELAY);
    if (ioctl(s_fd, VIDIOC_DQBUF, &buf) < 0) {
        xSemaphoreGive(s_v4l2_lock);
        ESP_LOGW(TAG, "VIDIOC_DQBUF failed");
        return ESP_FAIL;
    }
    xSemaphoreGive(s_v4l2_lock);

    if (buf.index >= NUM_BUFS || !s_v4l2_buf[buf.index]) {
        ESP_LOGW(TAG, "Invalid V4L2 buffer index %u", (unsigned int)buf.index);
        return ESP_FAIL;
    }

    out->data = s_v4l2_buf[buf.index];
    out->bytes_used = buf.bytesused;
    out->length = s_buf_len[buf.index];
    out->index = (int)buf.index;
    return ESP_OK;
}

esp_err_t sc2336_cam_release_frame(int index)
{
    if (s_fd < 0 || !s_streaming) {
        return ESP_ERR_INVALID_STATE;
    }
    if (index < 0 || index >= NUM_BUFS || !s_v4l2_buf[index]) {
        return ESP_ERR_INVALID_ARG;
    }

    struct v4l2_buffer buf = {
        .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
        .index  = (uint32_t)index,
    };

    xSemaphoreTake(s_v4l2_lock, portMAX_DELAY);
    if (ioctl(s_fd, VIDIOC_QBUF, &buf) < 0) {
        xSemaphoreGive(s_v4l2_lock);
        ESP_LOGW(TAG, "VIDIOC_QBUF failed");
        return ESP_FAIL;
    }
    xSemaphoreGive(s_v4l2_lock);

    return ESP_OK;
}

void sc2336_cam_set_preview_active(bool active)
{
    s_preview_active = active;
}

bool sc2336_cam_is_preview_active(void)
{
    return s_preview_active;
}

esp_err_t sc2336_cam_capture_frame_ex(uint8_t *buffer, size_t buffer_size,
                                      size_t *bytes_used)
{
    if (!buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    sc2336_frame_ref_t frame = {0};
    esp_err_t ret = sc2336_cam_acquire_frame(&frame);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t copy_sz = (frame.bytes_used < buffer_size) ? frame.bytes_used : buffer_size;
    memcpy(buffer, frame.data, copy_sz);
    if (bytes_used) {
        *bytes_used = copy_sz;
    }

    esp_err_t release_ret = sc2336_cam_release_frame(frame.index);
    return (release_ret == ESP_OK) ? ESP_OK : release_ret;
}

esp_err_t sc2336_cam_capture_frame(uint8_t *buffer, size_t buffer_size)
{
    return sc2336_cam_capture_frame_ex(buffer, buffer_size, NULL);
}

/* ───────────────────────────────────────────── */
/*  Deinit                                        */
/* ───────────────────────────────────────────── */
esp_err_t sc2336_cam_deinit(void)
{
    if (s_fd < 0) {
        return ESP_OK;
    }

    /* Stop streaming */
    if (s_streaming) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(s_fd, VIDIOC_STREAMOFF, &type);
        s_streaming = false;
    }

    /* Unmap buffers */
    for (int i = 0; i < NUM_BUFS; i++) {
        if (s_v4l2_buf[i]) {
            munmap(s_v4l2_buf[i], s_buf_len[i]);
            s_v4l2_buf[i] = NULL;
        }
        s_buf_len[i] = 0;
    }

    close(s_fd);
    s_fd = -1;
    s_width = s_height = s_pixelfmt = 0;
    s_frame_size = 0;

    ESP_LOGI(TAG, "SC2336 camera deinitialized");
    return ESP_OK;
}
