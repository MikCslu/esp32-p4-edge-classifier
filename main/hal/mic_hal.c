/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file mic_hal.c
 * @brief Microphone HAL — delegates to ES8311 codec driver
 *
 * Provides a minimal read-only mic interface (no speaker).
 * Most users should use audio_hal instead for unified audio control.
 */

#include "hal/mic_hal.h"
#include "driver/audio/es8311.h"
#include "esp_log.h"

static const char *TAG = "MIC_HAL";
static bool initialized = false;

static esp_err_t mic_init(const mic_config_t *cfg)
{
    if (initialized) return ESP_OK;

    esp_err_t ret = es8311_codec_init(ES8311_DIR_INPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "es8311_codec_init failed: %d", ret);
        return ret;
    }

    uint32_t sample_rate = cfg ? cfg->sample_rate : 16000;
    uint16_t bits = cfg ? cfg->bits_per_sample : 16;
    uint8_t channel = cfg ? cfg->channel : 1;

    ret = es8311_codec_configure(sample_rate, bits, channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "es8311_codec_configure failed: %d", ret);
        return ret;
    }

    initialized = true;
    ESP_LOGI(TAG, "Mic HAL initialized: %lu Hz, %u-bit, %u ch",
             (unsigned long)sample_rate, bits, channel);
    return ESP_OK;
}

static esp_err_t mic_read(int16_t *buffer, size_t frames)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;
    return es8311_codec_read(buffer, frames);
}

static esp_err_t mic_deinit(void)
{
    initialized = false;
    return es8311_codec_deinit();
}

static const mic_hal_t hal = {
    .init = mic_init,
    .read = mic_read,
    .deinit = mic_deinit,
};

const mic_hal_t *mic_hal_get_instance(void)
{
    return &hal;
}
