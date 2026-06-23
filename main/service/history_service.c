#include "service/history_service.h"

#include "esp_log.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "HISTORY";

#define HISTORY_NS "history"
#define HISTORY_VERSION 1
#define HISTORY_BLOB_KEY "audio"

typedef struct {
    uint16_t version;
    uint16_t count;
    history_record_t records[HISTORY_AUDIO_MAX];
} history_store_t;

static history_store_t s_store = {
    .version = HISTORY_VERSION,
};
static bool s_initialized;

static uint8_t confidence_to_pct(float confidence)
{
    if (confidence < 0.0f) {
        confidence = 0.0f;
    } else if (confidence > 1.0f) {
        confidence = 1.0f;
    }
    return (uint8_t)(confidence * 100.0f + 0.5f);
}

static esp_err_t save_store(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(HISTORY_NS, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_blob(handle, HISTORY_BLOB_KEY, &s_store, sizeof(s_store));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

esp_err_t history_service_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(HISTORY_NS, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Open history NVS failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t size = sizeof(s_store);
    ret = nvs_get_blob(handle, HISTORY_BLOB_KEY, &s_store, &size);
    if (ret != ESP_OK || size != sizeof(s_store) ||
        s_store.version != HISTORY_VERSION ||
        s_store.count > HISTORY_AUDIO_MAX) {
        memset(&s_store, 0, sizeof(s_store));
        s_store.version = HISTORY_VERSION;
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_blob(handle, HISTORY_BLOB_KEY,
                                                   &s_store, sizeof(s_store)));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_commit(handle));
    }

    nvs_close(handle);
    s_initialized = true;
    ESP_LOGI(TAG, "History service initialized (%u records)", s_store.count);
    return ESP_OK;
}

esp_err_t history_record_audio(const audio_class_result_t *result,
                               uint32_t timestamp_ms,
                               bool attention)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!result || result->class_id < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_store.count >= HISTORY_AUDIO_MAX) {
        memmove(&s_store.records[0],
                &s_store.records[1],
                (HISTORY_AUDIO_MAX - 1) * sizeof(s_store.records[0]));
        s_store.count = HISTORY_AUDIO_MAX - 1;
    }

    history_record_t *record = &s_store.records[s_store.count++];
    record->timestamp_ms = timestamp_ms;
    record->class_id = (int8_t)result->class_id;
    record->confidence_pct = confidence_to_pct(result->confidence);
    record->source = HISTORY_SOURCE_AUDIO;
    record->flags = attention ? 0x01 : 0x00;

    esp_err_t ret = save_store();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Save history failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

size_t history_get_recent(history_record_t *out, size_t max)
{
    if (!out || max == 0) {
        return 0;
    }

    size_t count = s_store.count;
    if (count > max) {
        count = max;
    }

    for (size_t i = 0; i < count; i++) {
        out[i] = s_store.records[s_store.count - 1 - i];
    }
    return count;
}

size_t history_count(void)
{
    return s_store.count;
}

esp_err_t history_clear(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_store, 0, sizeof(s_store));
    s_store.version = HISTORY_VERSION;
    esp_err_t ret = save_store();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "History cleared");
    }
    return ret;
}
