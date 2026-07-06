#include "service/visual_classify_service.h"

#include "service/app_state.h"
#include "service/camera_service.h"
#include "service/event_service.h"
#include "runtime/task_config.h"
#include "dl_detect_espdet_postprocessor.hpp"
#include "dl_image_preprocessor.hpp"
#include "dl_model_base.hpp"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <vector>

static const char *TAG = "VISUAL_CLS_SRV";

extern const uint8_t emotion_model_espdl_start[] asm("_binary_emotion_model_espdl_start");
extern const uint8_t emotion_model_espdl_end[] asm("_binary_emotion_model_espdl_end");
extern const uint8_t espdet_pico_224_224_face_espdl_start[] asm("_binary_espdet_pico_224_224_face_espdl_start");
extern const uint8_t espdet_pico_224_224_face_espdl_end[] asm("_binary_espdet_pico_224_224_face_espdl_end");

#define EMOTION_INPUT_W 96
#define EMOTION_INPUT_H 96
#define EMOTION_INPUT_C 3
#define EMOTION_INPUT_SIZE (EMOTION_INPUT_W * EMOTION_INPUT_H * EMOTION_INPUT_C)
#define VISUAL_FACE_SCORE_THRESHOLD 0.45f
#define VISUAL_EMOTION_THRESHOLD 0.30f
#define VISUAL_START_DELAY_MS 20000
#define VISUAL_PERIOD_MS 2000
#define VISUAL_EVENT_MIN_INTERVAL_MS 2500

typedef struct {
    int x;
    int y;
    int w;
    int h;
    float score;
} visual_face_box_t;

static const char *s_emotion_names[VISUAL_EMOTION_CLASS_COUNT] = {
    "surprise",
    "fear",
    "disgust",
    "happy",
    "sad",
    "anger",
    "neutral",
};

static TaskHandle_t s_task;
static uint8_t *s_frame_buf;
static uint8_t *s_face_rgb;
static int8_t *s_emotion_input;
static dl::TensorBase *s_emotion_tensor;
static dl::Model *s_face_model;
static dl::image::ImagePreprocessor *s_face_preprocessor;
static dl::detect::ESPDetPostProcessor *s_face_postprocessor;
static dl::Model *s_emotion_model;
static int s_emotion_exponent = -7;
static bool s_emotion_nchw = true;
static uint32_t s_last_event_ms;
static int s_last_event_emotion = -1;
static visual_cls_srv_stats_t s_stats;

const char *visual_emotion_name(int emotion_id)
{
    if (emotion_id >= 0 && emotion_id < VISUAL_EMOTION_CLASS_COUNT) {
        return s_emotion_names[emotion_id];
    }
    return "unknown";
}

static inline uint16_t read_rgb565_pixel(const uint8_t *frame, int x, int y, int width, bool byte_swap)
{
    const int offset = (y * width + x) * 2;
    if (byte_swap) {
        return ((uint16_t)frame[offset] << 8) | frame[offset + 1];
    }
    return (uint16_t)frame[offset] | ((uint16_t)frame[offset + 1] << 8);
}

static inline void preview_pixel_to_rgb(const uint8_t *frame,
                                        int x,
                                        int y,
                                        int width,
                                        bool byte_swap,
                                        uint8_t *r,
                                        uint8_t *g,
                                        uint8_t *b)
{
    uint16_t pixel = read_rgb565_pixel(frame, x, y, width, byte_swap);

    *b = (uint8_t)(((pixel >> 11) & 0x1f) << 3);
    *g = (uint8_t)(((pixel >> 5) & 0x3f) << 2);
    *r = (uint8_t)((pixel & 0x1f) << 3);
}

static void crop_face_to_rgb96(const uint8_t *frame,
                               int width,
                               int height,
                               bool byte_swap,
                               const visual_face_box_t *face,
                               uint8_t *out_rgb)
{
    int side = std::max(face->w, face->h);
    side = std::max(side, 16);
    side = side * 6 / 5;

    int cx = face->x + face->w / 2;
    int cy = face->y + face->h / 2;
    int crop_x = cx - side / 2;
    int crop_y = cy - side / 2;

    if (crop_x < 0) {
        crop_x = 0;
    }
    if (crop_y < 0) {
        crop_y = 0;
    }
    if (crop_x + side > width) {
        crop_x = std::max(0, width - side);
    }
    if (crop_y + side > height) {
        crop_y = std::max(0, height - side);
    }
    if (side > width) {
        side = width;
        crop_x = 0;
    }
    if (side > height) {
        side = height;
        crop_y = 0;
    }

    for (int dy = 0; dy < EMOTION_INPUT_H; dy++) {
        int sy = crop_y + (dy * side) / EMOTION_INPUT_H;
        sy = std::min(std::max(sy, 0), height - 1);
        for (int dx = 0; dx < EMOTION_INPUT_W; dx++) {
            int sx = crop_x + (dx * side) / EMOTION_INPUT_W;
            sx = std::min(std::max(sx, 0), width - 1);

            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            preview_pixel_to_rgb(frame, sx, sy, width, byte_swap, &r, &g, &b);

            int out = (dy * EMOTION_INPUT_W + dx) * 3;
            out_rgb[out + 0] = r;
            out_rgb[out + 1] = g;
            out_rgb[out + 2] = b;
        }
    }
}

static esp_err_t init_face_model(void)
{
    if (s_face_model) {
        return ESP_OK;
    }

    size_t model_size = (size_t)(espdet_pico_224_224_face_espdl_end - espdet_pico_224_224_face_espdl_start);
    ESP_LOGI(TAG, "Loading face model (%.1f KB)", (double)model_size / 1024.0);

    s_face_model = new dl::Model((const char *)espdet_pico_224_224_face_espdl_start,
                                 fbs::MODEL_LOCATION_IN_FLASH_RODATA);
    if (!s_face_model) {
        return ESP_ERR_NO_MEM;
    }

    s_face_preprocessor = new dl::image::ImagePreprocessor(s_face_model,
                                                           std::array<float, 3>{0.0f, 0.0f, 0.0f},
                                                           std::array<float, 3>{255.0f, 255.0f, 255.0f});
    if (!s_face_preprocessor) {
        return ESP_ERR_NO_MEM;
    }
    s_face_preprocessor->enable_letterbox(std::array<uint8_t, 3>{114, 114, 114});

    s_face_postprocessor = new dl::detect::ESPDetPostProcessor(
        s_face_model,
        s_face_preprocessor,
        0.30f,
        0.50f,
        3,
        {{8, 8, 4, 4}, {16, 16, 8, 8}, {32, 32, 16, 16}});
    if (!s_face_postprocessor) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t init_emotion_model(void)
{
    if (s_emotion_model) {
        return ESP_OK;
    }

    size_t model_size = (size_t)(emotion_model_espdl_end - emotion_model_espdl_start);
    ESP_LOGI(TAG, "Loading emotion model (%.1f KB)", (double)model_size / 1024.0);

    s_emotion_model = new dl::Model((const char *)emotion_model_espdl_start,
                                    fbs::MODEL_LOCATION_IN_FLASH_RODATA);
    if (!s_emotion_model) {
        return ESP_ERR_NO_MEM;
    }

    auto inputs = s_emotion_model->get_inputs();
    if (inputs.empty()) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    dl::TensorBase *input = inputs.begin()->second;
    s_emotion_exponent = (int)input->exponent;
    if ((int)input->shape[1] == EMOTION_INPUT_C &&
        (int)input->shape[2] == EMOTION_INPUT_H &&
        (int)input->shape[3] == EMOTION_INPUT_W) {
        s_emotion_nchw = true;
    } else if ((int)input->shape[1] == EMOTION_INPUT_H &&
               (int)input->shape[2] == EMOTION_INPUT_W &&
               (int)input->shape[3] == EMOTION_INPUT_C) {
        s_emotion_nchw = false;
    } else {
        ESP_LOGE(TAG, "Unsupported emotion input shape [%d,%d,%d,%d]",
                 (int)input->shape[0],
                 (int)input->shape[1],
                 (int)input->shape[2],
                 (int)input->shape[3]);
        return ESP_ERR_INVALID_SIZE;
    }

    std::vector<int> shape = s_emotion_nchw
                                 ? std::vector<int>{1, EMOTION_INPUT_C, EMOTION_INPUT_H, EMOTION_INPUT_W}
                                 : std::vector<int>{1, EMOTION_INPUT_H, EMOTION_INPUT_W, EMOTION_INPUT_C};
    s_emotion_tensor = new dl::TensorBase(shape,
                                          s_emotion_input,
                                          s_emotion_exponent,
                                          dl::DATA_TYPE_INT8,
                                          false,
                                          MALLOC_CAP_SPIRAM);
    if (!s_emotion_tensor) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Emotion input layout=%s exponent=%d",
             s_emotion_nchw ? "NCHW" : "NHWC",
             s_emotion_exponent);
    return ESP_OK;
}

static esp_err_t init_buffers(void)
{
    if (!s_frame_buf) {
        s_frame_buf = (uint8_t *)heap_caps_aligned_alloc(64,
                                                         CAMERA_VISION_BYTES,
                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
    }
    if (!s_face_rgb) {
        s_face_rgb = (uint8_t *)heap_caps_aligned_alloc(64,
                                                        EMOTION_INPUT_SIZE,
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_emotion_input) {
        s_emotion_input = (int8_t *)heap_caps_aligned_alloc(64,
                                                            EMOTION_INPUT_SIZE,
                                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_frame_buf || !s_face_rgb || !s_emotion_input) {
        ESP_LOGE(TAG, "Visual buffers allocation failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static int detect_face(const uint8_t *preview,
                       const camera_preview_info_t *info,
                       visual_face_box_t *best_face,
                       uint32_t *detect_ms)
{
    int64_t start_us = esp_timer_get_time();
    dl::image::img_t img = {};
    img.data = (void *)preview;
    img.width = info->width;
    img.height = info->height;
    img.pix_type = info->byte_swap ? dl::image::DL_IMAGE_PIX_TYPE_BGR565BE
                                   : dl::image::DL_IMAGE_PIX_TYPE_BGR565LE;

    s_face_preprocessor->preprocess(img);
    s_face_model->run(dl::RUNTIME_MODE_SINGLE_CORE);
    s_face_postprocessor->clear_result();
    s_face_postprocessor->postprocess();
    auto &results = s_face_postprocessor->get_result(info->width, info->height);

    int faces = 0;
    visual_face_box_t candidate = {};
    for (const auto &res : results) {
        int x1 = std::max(0, (int)res.box[0]);
        int y1 = std::max(0, (int)res.box[1]);
        int x2 = std::min((int)info->width - 1, (int)res.box[2]);
        int y2 = std::min((int)info->height - 1, (int)res.box[3]);
        int w = std::max(0, x2 - x1);
        int h = std::max(0, y2 - y1);
        if (w < 12 || h < 12) {
            continue;
        }
        faces++;
        if (res.score > candidate.score) {
            candidate.x = x1;
            candidate.y = y1;
            candidate.w = w;
            candidate.h = h;
            candidate.score = res.score;
        }
    }

    if (best_face) {
        *best_face = candidate;
    }
    if (detect_ms) {
        *detect_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);
    }
    return faces;
}

static esp_err_t run_emotion(const uint8_t *rgb96,
                             int *emotion_id,
                             float *confidence,
                             uint32_t *emotion_ms)
{
    static const float mean[3] = {0.485f, 0.456f, 0.406f};
    static const float stddev[3] = {0.229f, 0.224f, 0.225f};
    const int pixels = EMOTION_INPUT_W * EMOTION_INPUT_H;
    const float input_scale = powf(2.0f, (float)s_emotion_exponent);

    for (int y = 0; y < EMOTION_INPUT_H; y++) {
        for (int x = 0; x < EMOTION_INPUT_W; x++) {
            for (int c = 0; c < EMOTION_INPUT_C; c++) {
                int src = (y * EMOTION_INPUT_W + x) * EMOTION_INPUT_C + c;
                float normalized = ((float)rgb96[src] / 255.0f - mean[c]) / stddev[c];
                int q = (int)lrintf(normalized / input_scale);
                q = std::min(127, std::max(-128, q));
                int dst = s_emotion_nchw ? (c * pixels + y * EMOTION_INPUT_W + x) : src;
                s_emotion_input[dst] = (int8_t)q;
            }
        }
    }

    int64_t start_us = esp_timer_get_time();
    s_emotion_model->run(s_emotion_tensor, dl::RUNTIME_MODE_SINGLE_CORE);
    if (emotion_ms) {
        *emotion_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);
    }

    dl::TensorBase *output = s_emotion_model->get_output();
    if (!output || !output->data) {
        return ESP_FAIL;
    }

    float raw[VISUAL_EMOTION_CLASS_COUNT] = {};
    if (output->dtype == dl::DATA_TYPE_FLOAT) {
        float *data = (float *)output->data;
        for (int i = 0; i < VISUAL_EMOTION_CLASS_COUNT; i++) {
            raw[i] = data[i];
        }
    } else {
        int8_t *data = (int8_t *)output->data;
        float scale = powf(2.0f, (float)((int)output->exponent));
        for (int i = 0; i < VISUAL_EMOTION_CLASS_COUNT; i++) {
            raw[i] = (float)data[i] * scale;
        }
    }

    float max_val = -FLT_MAX;
    for (float v : raw) {
        max_val = std::max(max_val, v);
    }
    float probs[VISUAL_EMOTION_CLASS_COUNT] = {};
    float sum = 0.0f;
    for (int i = 0; i < VISUAL_EMOTION_CLASS_COUNT; i++) {
        probs[i] = expf(raw[i] - max_val);
        sum += probs[i];
    }

    int best = -1;
    float best_prob = 0.0f;
    if (sum > 0.0f) {
        for (int i = 0; i < VISUAL_EMOTION_CLASS_COUNT; i++) {
            probs[i] /= sum;
            if (probs[i] > best_prob) {
                best_prob = probs[i];
                best = i;
            }
        }
    }

    if (best_prob < VISUAL_EMOTION_THRESHOLD) {
        best = -1;
        best_prob = 0.0f;
    }
    if (emotion_id) {
        *emotion_id = best;
    }
    if (confidence) {
        *confidence = best_prob;
    }
    return ESP_OK;
}

static void update_stats(const visual_cls_result_t *result, bool error)
{
    if (error) {
        s_stats.errors++;
        return;
    }

    s_stats.processed++;
    s_stats.last_result = *result;
    s_stats.last_time_ms = result->total_time_ms;
    s_stats.avg_time_ms = (uint32_t)((((uint64_t)s_stats.avg_time_ms * (s_stats.processed - 1)) +
                                      result->total_time_ms) /
                                     s_stats.processed);
    if (s_stats.min_time_ms == 0 || result->total_time_ms < s_stats.min_time_ms) {
        s_stats.min_time_ms = result->total_time_ms;
    }
    if (result->total_time_ms > s_stats.max_time_ms) {
        s_stats.max_time_ms = result->total_time_ms;
    }
    if (result->face_detected) {
        s_stats.faces++;
    }
    if (result->emotion_id >= 0) {
        s_stats.emotions++;
    }
}

static esp_err_t init_visual_pipeline(void)
{
    ESP_RETURN_ON_ERROR(init_buffers(), TAG, "buffer init failed");
    ESP_RETURN_ON_ERROR(init_face_model(), TAG, "face model init failed");
    ESP_RETURN_ON_ERROR(init_emotion_model(), TAG, "emotion model init failed");
    return ESP_OK;
}

static void visual_cls_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Visual classification delayed %d ms for UI/audio bring-up",
             VISUAL_START_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(VISUAL_START_DELAY_MS));

    esp_err_t ret = init_visual_pipeline();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Visual pipeline init failed: %s", esp_err_to_name(ret));
        while (true) {
            s_stats.errors++;
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    ESP_LOGI(TAG, "Visual classification task started");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(VISUAL_PERIOD_MS));

        camera_preview_info_t info = {};
        ret = camera_service_copy_vision(s_frame_buf, CAMERA_VISION_BYTES, &info);
        if (ret != ESP_OK) {
            ret = camera_service_copy_preview(s_frame_buf, CAMERA_PREVIEW_BYTES, &info);
        }
        if (ret != ESP_OK) {
            s_stats.skipped_no_frame++;
            continue;
        }

        int64_t start_us = esp_timer_get_time();
        visual_cls_result_t result = {};
        result.emotion_id = -1;
        result.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
        result.sequence = info.sequence;
        result.crop_id = info.crop_id;

        visual_face_box_t face = {};
        int face_count = detect_face(s_frame_buf, &info, &face, &result.detect_time_ms);
        const uint8_t *detect_frame = s_frame_buf;

        if (face_count < 0) {
            update_stats(&result, true);
            continue;
        }

        if (face_count > 0 && face.score >= VISUAL_FACE_SCORE_THRESHOLD) {
            result.face_detected = true;
            result.face_score = face.score;
            result.face_x = (uint16_t)face.x;
            result.face_y = (uint16_t)face.y;
            result.face_w = (uint16_t)face.w;
            result.face_h = (uint16_t)face.h;
            result.rotation_deg = 0;

            crop_face_to_rgb96(detect_frame,
                               info.width,
                               info.height,
                               info.byte_swap,
                               &face,
                               s_face_rgb);
            ret = run_emotion(s_face_rgb,
                              &result.emotion_id,
                              &result.emotion_confidence,
                              &result.emotion_time_ms);
            if (ret != ESP_OK) {
                update_stats(&result, true);
                continue;
            }
        }

        result.total_time_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);
        update_stats(&result, false);
        app_state_record_visual(&result);

        if (result.face_detected && result.emotion_id >= 0) {
            bool should_post = result.emotion_id != s_last_event_emotion ||
                               result.timestamp_ms - s_last_event_ms >= VISUAL_EVENT_MIN_INTERVAL_MS;
            if (should_post) {
                event_t event = {};
                event.type = EVENT_VISUAL_CLASSIFICATION;
                event.visual_result = result;
                event.timestamp_ms = result.timestamp_ms;
                if (event_srv_post(&event) == ESP_OK) {
                    s_last_event_emotion = result.emotion_id;
                    s_last_event_ms = result.timestamp_ms;
                }
            }
        }

        if (result.face_detected && result.emotion_id >= 0) {
            ESP_LOGI(TAG,
                     "Visual result: face=%.2f emotion=%s %.2f crop=%u rot=%u time=%ums detect=%ums emotion=%ums",
                     (double)result.face_score,
                     visual_emotion_name(result.emotion_id),
                     (double)result.emotion_confidence,
                     (unsigned)result.crop_id,
                     (unsigned)result.rotation_deg,
                     (unsigned)result.total_time_ms,
                     (unsigned)result.detect_time_ms,
                     (unsigned)result.emotion_time_ms);
        } else if ((s_stats.processed % 10) == 0) {
            ESP_LOGI(TAG,
                     "Visual idle: face_count=%d best_score=%.2f time=%ums",
                     face_count,
                     (double)face.score,
                     (unsigned)result.total_time_ms);
        }
    }
}

esp_err_t visual_cls_srv_init(void)
{
    if (s_task) {
        return ESP_OK;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(visual_cls_task,
                                            VISUAL_INFER_TASK_NAME,
                                            VISUAL_INFER_TASK_STACK,
                                            NULL,
                                            VISUAL_INFER_TASK_PRIO,
                                            &s_task,
                                            VISUAL_INFER_TASK_CORE);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}

TaskHandle_t visual_cls_srv_get_task_handle(void)
{
    return s_task;
}

void visual_cls_srv_get_stats(visual_cls_srv_stats_t *stats)
{
    if (!stats) {
        return;
    }
    *stats = s_stats;
    if (stats->min_time_ms == UINT32_MAX || stats->min_time_ms == 0) {
        stats->min_time_ms = 0;
    }
}
