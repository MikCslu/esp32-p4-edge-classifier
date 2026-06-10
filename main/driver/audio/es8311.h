#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ES8311_DIR_INPUT,
    ES8311_DIR_OUTPUT,
    ES8311_DIR_DUPLEX,
} es8311_dir_t;

/**
 * @brief Initialize ES8311 codec (I2S + codec)
 *
 * Wraps bsp_audio_init() + bsp_audio_codec_speaker/microphone_init().
 * Must be called once before any read/write operations.
 *
 * @param dir       Input / Output / Duplex
 * @return ESP_OK on success
 */
esp_err_t es8311_codec_init(es8311_dir_t dir);

/**
 * @brief Configure audio sample format
 *
 * Must be called after init and before first read/write.
 *
 * @param sample_rate  e.g. 16000
 * @param bits         e.g. 16
 * @param channels     e.g. 1
 * @return ESP_OK on success
 */
esp_err_t es8311_codec_configure(uint32_t sample_rate, uint16_t bits, uint8_t channels);

/**
 * @brief Read audio data from codec
 * @param buffer  Destination buffer
 * @param frames  Number of 16-bit frames to read
 * @return ESP_OK on success
 */
esp_err_t es8311_codec_read(int16_t *buffer, size_t frames);

/**
 * @brief Write audio data to codec
 * @param buffer  Source buffer
 * @param frames  Number of 16-bit frames to write
 * @return ESP_OK on success
 */
esp_err_t es8311_codec_write(const int16_t *buffer, size_t frames);

/**
 * @brief Set output volume (0–100)
 */
esp_err_t es8311_codec_set_volume(uint8_t vol);

/**
 * @brief Deinitialize and release resources
 */
esp_err_t es8311_codec_deinit(void);

#ifdef __cplusplus
}
#endif
