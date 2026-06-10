/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file audio_hal.c
 * @brief Unified Audio HAL — delegates to ES8311 codec driver
 *
 * Supports both microphone input and speaker output via ES8311.
 */

#include "hal/audio_hal.h"
#include "driver/audio/es8311.h"
#include "esp_log.h"

static const char *TAG = "AUDIO_HAL";
static bool initialized = false;
static es8311_dir_t current_dir = ES8311_DIR_INPUT;

static esp_err_t audio_init(const audio_config_t *cfg)
{
    if (initialized) return ESP_OK;

    es8311_dir_t dir = ES8311_DIR_INPUT;
    if (cfg) {
        switch (cfg->dir) {
        case AUDIO_DIR_OUTPUT:  dir = ES8311_DIR_OUTPUT;  break;
        case AUDIO_DIR_DUPLEX:  dir = ES8311_DIR_DUPLEX;  break;
        default:                dir = ES8311_DIR_INPUT;    break;
        }
    }
    current_dir = dir;

    esp_err_t ret = es8311_codec_init(dir);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "es8311_codec_init failed: %d", ret);
        return ret;
    }

    uint32_t sample_rate = cfg ? cfg->sample_rate : 16000;
    uint16_t bits = cfg ? cfg->bits_per_sample : 16;
    uint8_t channels = cfg ? cfg->channels : 1;
    ret = es8311_codec_configure(sample_rate, bits, channels);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "es8311_codec_configure failed: %d", ret);
        return ret;
    }

    initialized = true;
    ESP_LOGI(TAG, "Audio HAL initialized: %lu Hz, %u-bit, %u ch",
             (unsigned long)sample_rate, bits, channels);
    return ESP_OK;
}

static esp_err_t audio_read(int16_t *buffer, size_t frames, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (current_dir == ES8311_DIR_OUTPUT) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return es8311_codec_read(buffer, frames);
}

static esp_err_t audio_write(const int16_t *buffer, size_t frames, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (current_dir == ES8311_DIR_INPUT) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return es8311_codec_write(buffer, frames);
}

static esp_err_t audio_set_volume(uint8_t vol)
{
    if (current_dir != ES8311_DIR_OUTPUT) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return es8311_codec_set_volume(vol);
}

static esp_err_t audio_deinit(void)
{
    initialized = false;
    return es8311_codec_deinit();
}

static const audio_hal_t hal = {
    .init = audio_init,
    .read = audio_read,
    .write = audio_write,
    .set_volume = audio_set_volume,
    .deinit = audio_deinit,
};

const audio_hal_t *audio_hal_get_instance(void)
{
    return &hal;
}
