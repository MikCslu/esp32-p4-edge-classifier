#include "service/camera_service.h"

#include "driver/sensor/sc2336_camera.h"
#include "runtime/task_config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "linux/videodev2.h"
#include <string.h>

#define CAMERA_PREVIEW_USE_PPA   0

#if CAMERA_PREVIEW_USE_PPA
#include "driver/ppa.h"
#endif

static const char *TAG = "CAMERA_SERVICE";

#define CAMERA_PREVIEW_BUFFERS   3

static TaskHandle_t s_task;
static SemaphoreHandle_t s_lock;
static uint8_t *s_preview_buf[CAMERA_PREVIEW_BUFFERS];
#if CAMERA_PREVIEW_USE_PPA
static ppa_client_handle_t s_ppa_srm;
static bool s_ppa_disabled;
#endif
static uint16_t s_sample_x[CAMERA_PREVIEW_W];
static uint32_t s_sample_row_offset[CAMERA_PREVIEW_H];
static int s_writing_idx = -1;
static int s_ready_idx = -1;
static int s_held_idx = -1;
static camera_preview_info_t s_info;
static bool s_ready;
static uint32_t s_frames_built;
static uint32_t s_acquire_failed;
static uint32_t s_no_buffer;
static uint32_t s_ui_missed;
static uint32_t s_last_build_us;
static uint32_t s_min_build_us = UINT32_MAX;
static uint32_t s_max_build_us;
static uint64_t s_build_sum_us;

static void init_preview_sampler(uint16_t src_w, uint16_t src_h)
{
    for (uint16_t x = 0; x < CAMERA_PREVIEW_W; x++) {
        s_sample_x[x] = (uint16_t)(((uint32_t)x * src_w) / CAMERA_PREVIEW_W);
    }
    for (uint16_t y = 0; y < CAMERA_PREVIEW_H; y++) {
        uint32_t sy = ((uint32_t)y * src_h) / CAMERA_PREVIEW_H;
        s_sample_row_offset[y] = sy * src_w;
    }
}

static void build_preview_rgb565(const uint8_t *src, uint8_t *dst)
{
    const uint16_t *src16 = (const uint16_t *)src;
    uint16_t *dst16 = (uint16_t *)dst;

    for (uint16_t y = 0; y < CAMERA_PREVIEW_H; y++) {
        const uint16_t *src_row = src16 + s_sample_row_offset[y];
        uint16_t *dst_row = dst16 + y * CAMERA_PREVIEW_W;
        for (uint16_t x = 0; x < CAMERA_PREVIEW_W; x++) {
            dst_row[x] = src_row[s_sample_x[x]];
        }
    }
}

#if CAMERA_PREVIEW_USE_PPA
static esp_err_t build_preview_ppa(const uint8_t *src,
                                   uint16_t src_w,
                                   uint16_t src_h,
                                   bool input_byte_swap,
                                   uint8_t *dst)
{
    if (!s_ppa_srm || s_ppa_disabled) {
        return ESP_ERR_INVALID_STATE;
    }

    ppa_srm_oper_config_t config = {
        .in.buffer = src,
        .in.pic_w = src_w,
        .in.pic_h = src_h,
        .in.block_w = src_w,
        .in.block_h = src_h,
        .in.block_offset_x = 0,
        .in.block_offset_y = 0,
        .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,

        .out.buffer = dst,
        .out.buffer_size = CAMERA_PREVIEW_BYTES,
        .out.pic_w = CAMERA_PREVIEW_W,
        .out.pic_h = CAMERA_PREVIEW_H,
        .out.block_offset_x = 0,
        .out.block_offset_y = 0,
        .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,

        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = (float)CAMERA_PREVIEW_W / (float)src_w,
        .scale_y = (float)CAMERA_PREVIEW_H / (float)src_h,
        .byte_swap = input_byte_swap,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    return ppa_do_scale_rotate_mirror(s_ppa_srm, &config);
}
#endif

static void camera_service_task(void *arg)
{
    (void)arg;

    esp_err_t ret = sc2336_cam_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Camera init failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t pixelformat = 0;
    size_t frame_size = 0;
    ret = sc2336_cam_get_info(&width, &height, &pixelformat, &frame_size);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Camera info failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Camera service started (%lux%lu fmt=0x%08lx frame=%u)",
             (unsigned long)width,
             (unsigned long)height,
             (unsigned long)pixelformat,
             (unsigned int)frame_size);

    init_preview_sampler((uint16_t)width, (uint16_t)height);

#if CAMERA_PREVIEW_USE_PPA
    if (!s_ppa_srm) {
        ppa_client_config_t ppa_config = {
            .oper_type = PPA_OPERATION_SRM,
        };
        ret = ppa_register_client(&ppa_config, &s_ppa_srm);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "PPA SRM preview scaler enabled");
        } else {
            ESP_LOGW(TAG, "PPA SRM unavailable, using CPU scaler: %s", esp_err_to_name(ret));
            s_ppa_disabled = true;
        }
    }
#else
    ESP_LOGI(TAG, "PPA preview scaler disabled, using optimized CPU sampler");
#endif

    while (true) {
        sc2336_frame_ref_t frame = {0};
        ret = sc2336_cam_acquire_frame(&frame);
        if (ret != ESP_OK) {
            s_acquire_failed++;
            ESP_LOGW(TAG, "Acquire frame failed: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        int next = -1;
        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) == pdTRUE) {
            for (int i = 0; i < CAMERA_PREVIEW_BUFFERS; i++) {
                if (i != s_ready_idx && i != s_held_idx && i != s_writing_idx) {
                    next = i;
                    s_writing_idx = i;
                    break;
                }
            }
            xSemaphoreGive(s_lock);
        }

        if (next < 0) {
            s_no_buffer++;
            sc2336_cam_release_frame(frame.index);
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        int64_t build_start_us = esp_timer_get_time();
        bool input_byte_swap = (pixelformat == V4L2_PIX_FMT_RGB565X);
        bool output_byte_swap = input_byte_swap;
#if CAMERA_PREVIEW_USE_PPA
        ret = build_preview_ppa(frame.data,
                                (uint16_t)width,
                                (uint16_t)height,
                                input_byte_swap,
                                s_preview_buf[next]);
        if (ret == ESP_OK) {
            output_byte_swap = false;
        } else {
            static bool fallback_logged;
            if (!fallback_logged) {
                ESP_LOGW(TAG, "PPA preview scale failed (%s), falling back to CPU scaler",
                         esp_err_to_name(ret));
                fallback_logged = true;
            }
            build_preview_rgb565(frame.data, s_preview_buf[next]);
        }
#else
        build_preview_rgb565(frame.data, s_preview_buf[next]);
#endif
        uint32_t build_us = (uint32_t)(esp_timer_get_time() - build_start_us);
        sc2336_cam_release_frame(frame.index);

        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_writing_idx = -1;
        s_ready_idx = next;
        s_info.width = CAMERA_PREVIEW_W;
        s_info.height = CAMERA_PREVIEW_H;
        s_info.pixelformat = pixelformat;
        s_info.byte_swap = output_byte_swap;
        s_info.sequence++;
        s_ready = true;
        s_frames_built++;
        s_last_build_us = build_us;
        s_build_sum_us += build_us;
        if (build_us < s_min_build_us) {
            s_min_build_us = build_us;
        }
        if (build_us > s_max_build_us) {
            s_max_build_us = build_us;
        }
        xSemaphoreGive(s_lock);

        taskYIELD();
    }
}

esp_err_t camera_service_start(void)
{
    if (s_task) {
        return ESP_OK;
    }

    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }

    for (int i = 0; i < CAMERA_PREVIEW_BUFFERS; i++) {
        if (!s_preview_buf[i]) {
            s_preview_buf[i] = (uint8_t *)heap_caps_aligned_alloc(64,
                                                                  CAMERA_PREVIEW_BYTES,
                                                                  MALLOC_CAP_SPIRAM |
                                                                  MALLOC_CAP_DMA |
                                                                  MALLOC_CAP_8BIT);
            if (!s_preview_buf[i]) {
                ESP_LOGE(TAG, "Preview buffer allocation failed");
                return ESP_ERR_NO_MEM;
            }
        }
    }

    BaseType_t ok = xTaskCreatePinnedToCore(camera_service_task,
                                            CAMERA_SERVICE_TASK_NAME,
                                            CAMERA_SERVICE_TASK_STACK,
                                            NULL,
                                            CAMERA_SERVICE_TASK_PRIO,
                                            &s_task,
                                            CAMERA_SERVICE_TASK_CORE);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}

bool camera_service_is_ready(void)
{
    return s_ready;
}

TaskHandle_t camera_service_get_task_handle(void)
{
    return s_task;
}

esp_err_t camera_service_acquire_preview(uint32_t last_sequence,
                                         camera_preview_frame_t *frame)
{
    if (!frame) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(1)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_ready || s_ready_idx < 0 || s_info.sequence == last_sequence) {
        if (s_ready && s_info.sequence == last_sequence) {
            s_ui_missed++;
        }
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    s_held_idx = s_ready_idx;
    frame->data = s_preview_buf[s_held_idx];
    frame->data_size = CAMERA_PREVIEW_BYTES;
    frame->info = s_info;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t camera_service_copy_preview(uint8_t *dst,
                                      size_t dst_size,
                                      camera_preview_info_t *info)
{
    if (!dst || dst_size < CAMERA_PREVIEW_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(1)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_ready || s_ready_idx < 0) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(dst, s_preview_buf[s_ready_idx], CAMERA_PREVIEW_BYTES);
    if (info) {
        *info = s_info;
    }
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

void camera_service_get_stats(camera_service_stats_t *stats)
{
    if (!stats) {
        return;
    }

    memset(stats, 0, sizeof(*stats));
    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(1)) == pdTRUE) {
        stats->frames = s_frames_built;
        stats->acquire_failed = s_acquire_failed;
        stats->no_buffer = s_no_buffer;
        stats->ui_missed = s_ui_missed;
        stats->last_build_us = s_last_build_us;
        stats->min_build_us = (s_min_build_us == UINT32_MAX) ? 0 : s_min_build_us;
        stats->max_build_us = s_max_build_us;
        stats->avg_build_us = s_frames_built > 0
                                  ? (uint32_t)(s_build_sum_us / s_frames_built)
                                  : 0;
        xSemaphoreGive(s_lock);
        return;
    }

    stats->frames = s_frames_built;
    stats->acquire_failed = s_acquire_failed;
    stats->no_buffer = s_no_buffer;
    stats->ui_missed = s_ui_missed;
    stats->last_build_us = s_last_build_us;
    stats->min_build_us = (s_min_build_us == UINT32_MAX) ? 0 : s_min_build_us;
    stats->max_build_us = s_max_build_us;
    stats->avg_build_us = s_frames_built > 0
                              ? (uint32_t)(s_build_sum_us / s_frames_built)
                              : 0;
}
