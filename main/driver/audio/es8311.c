/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file es8311_codec.c
 * @brief ES8311 I2S audio codec driver — thin wrapper around BSP + esp_codec_dev
 *
 * All BSP / esp_codec_dev dependencies are confined to this file.
 * Higher layers (HAL, Service, App) include only driver/ headers.
 */

#include "driver/audio/es8311.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"

static const char *TAG = "ES8311";
static esp_codec_dev_handle_t s_codec = NULL;
static bool s_initialized = false;

esp_err_t es8311_codec_init(es8311_dir_t dir)
{
    if (s_initialized) {
        return ESP_OK;
    }

    /* Init I2S peripheral */
    esp_err_t ret = bsp_audio_init(NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bsp_audio_init failed: %d", ret);
        return ret;
    }

    /* Init codec device */
    switch (dir) {
    case ES8311_DIR_OUTPUT:
        s_codec = bsp_audio_codec_speaker_init();
        break;
    case ES8311_DIR_INPUT:
    case ES8311_DIR_DUPLEX:
    default:
        s_codec = bsp_audio_codec_microphone_init();
        break;
    }

    if (!s_codec) {
        ESP_LOGE(TAG, "bsp_audio_codec_*_init returned NULL");
        return ESP_FAIL;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "ES8311 initialized (dir=%d)", dir);
    return ESP_OK;
}

esp_err_t es8311_codec_configure(uint32_t sample_rate, uint16_t bits, uint8_t channels)
{
    if (!s_codec) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_codec_dev_sample_info_t fs = {
        .channel = channels,
        .sample_rate = sample_rate,
        .bits_per_sample = bits,
    };

    esp_err_t ret = esp_codec_dev_open(s_codec, &fs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_codec_dev_open failed: %d", ret);
    }
    return ret;
}

esp_err_t es8311_codec_read(int16_t *buffer, size_t frames)
{
    if (!s_codec) {
        return ESP_ERR_INVALID_STATE;
    }
    int ret = esp_codec_dev_read(s_codec, buffer, frames * sizeof(int16_t));
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t es8311_codec_write(const int16_t *buffer, size_t frames)
{
    if (!s_codec) {
        return ESP_ERR_INVALID_STATE;
    }
    int ret = esp_codec_dev_write(s_codec, buffer, frames * sizeof(int16_t));
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t es8311_codec_set_volume(uint8_t vol)
{
    if (!s_codec) {
        return ESP_ERR_INVALID_STATE;
    }
    if (vol > 100) vol = 100;
    return esp_codec_dev_set_out_vol(s_codec, (int)vol);
}

esp_err_t es8311_codec_deinit(void)
{
    if (s_codec) {
        esp_codec_dev_close(s_codec);
        s_codec = NULL;
    }
    s_initialized = false;
    ESP_LOGI(TAG, "ES8311 deinitialized");
    return ESP_OK;
}
