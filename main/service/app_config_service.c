#include "service/app_config_service.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdint.h>
#include <stdio.h>

static const char *TAG = "APP_CONFIG";

#define AUDIO_CFG_NS "audio_cfg"
#define AUDIO_CFG_VERSION 2
#define AUDIO_CFG_VERSION_KEY "ver"

static bool s_initialized;

static int16_t float_to_permille(float value)
{
    if (value < 0.0f) {
        value = 0.0f;
    } else if (value > 1.0f) {
        value = 1.0f;
    }
    return (int16_t)(value * 1000.0f + 0.5f);
}

static float permille_to_float(int16_t value)
{
    if (value < 0) {
        value = 0;
    } else if (value > 1000) {
        value = 1000;
    }
    return (float)value / 1000.0f;
}

esp_err_t app_config_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Config service initialized");
    return ESP_OK;
}

esp_err_t app_config_load_audio_tuning(float *thresholds, float *margins, size_t count)
{
    if (!thresholds || !margins || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(AUDIO_CFG_NS, NVS_READWRITE, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }

    uint16_t version = 0;
    ret = nvs_get_u16(handle, AUDIO_CFG_VERSION_KEY, &version);
    if (ret != ESP_OK || version != AUDIO_CFG_VERSION) {
        ESP_LOGI(TAG, "Audio tuning version changed, resetting NVS audio tuning");
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_erase_all(handle));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_u16(handle,
                                                  AUDIO_CFG_VERSION_KEY,
                                                  AUDIO_CFG_VERSION));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_commit(handle));
        nvs_close(handle);
        return ESP_OK;
    }

    uint32_t loaded = 0;
    for (size_t i = 0; i < count; i++) {
        char key[16];
        int16_t stored = 0;

        snprintf(key, sizeof(key), "th%02u", (unsigned)i);
        if (nvs_get_i16(handle, key, &stored) == ESP_OK) {
            thresholds[i] = permille_to_float(stored);
            loaded++;
        }

        snprintf(key, sizeof(key), "mg%02u", (unsigned)i);
        if (nvs_get_i16(handle, key, &stored) == ESP_OK) {
            margins[i] = permille_to_float(stored);
            loaded++;
        }
    }

    nvs_close(handle);
    if (loaded > 0) {
        ESP_LOGI(TAG, "Loaded %lu audio tuning values from NVS", (unsigned long)loaded);
    }
    return ESP_OK;
}

esp_err_t app_config_save_audio_class_tuning(int class_id, float threshold, float margin)
{
    if (class_id < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(AUDIO_CFG_NS, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    char key[16];
    nvs_set_u16(handle, AUDIO_CFG_VERSION_KEY, AUDIO_CFG_VERSION);
    snprintf(key, sizeof(key), "th%02d", class_id);
    ret = nvs_set_i16(handle, key, float_to_permille(threshold));
    if (ret == ESP_OK) {
        snprintf(key, sizeof(key), "mg%02d", class_id);
        ret = nvs_set_i16(handle, key, float_to_permille(margin));
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);
    return ret;
}
