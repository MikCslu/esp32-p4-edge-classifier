#include "driver/audio/max98357_speaker.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "MAX98357";

#define SPK_I2S_PORT          I2S_NUM_0
#define SPK_SAMPLE_RATE_HZ    16000
#define SPK_BCLK_GPIO         GPIO_NUM_0
#define SPK_WS_GPIO           GPIO_NUM_1
#define SPK_DIN_GPIO          GPIO_NUM_6
#define SPK_CHUNK_SAMPLES     256

static i2s_chan_handle_t s_tx_chan;
static bool s_initialized;
static bool s_available;
static int16_t s_samples[SPK_CHUNK_SAMPLES];

esp_err_t max98357_speaker_init(void)
{
    if (s_initialized) {
        return s_available ? ESP_OK : ESP_FAIL;
    }
    s_initialized = true;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(SPK_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = 256;
    chan_cfg.auto_clear = true;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2S channel init failed: %d", ret);
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SPK_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = SPK_BCLK_GPIO,
            .ws = SPK_WS_GPIO,
            .dout = SPK_DIN_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    ret = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2S std init failed: %d", ret);
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        return ret;
    }

    ret = i2s_channel_enable(s_tx_chan);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2S enable failed: %d", ret);
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        return ret;
    }

    s_available = true;
    ESP_LOGI(TAG, "MAX98357 ready: I2S%d BCLK=%d WS=%d DIN=%d",
             SPK_I2S_PORT, SPK_BCLK_GPIO, SPK_WS_GPIO, SPK_DIN_GPIO);
    return ESP_OK;
}

bool max98357_speaker_is_available(void)
{
    return s_available;
}

i2s_chan_handle_t max98357_get_tx_handle(void)
{
    return s_tx_chan;
}

void max98357_speaker_tone(uint16_t freq_hz, uint16_t duration_ms, uint8_t volume_percent)
{
    if (!s_available || freq_hz == 0 || duration_ms == 0) {
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        return;
    }
    if (volume_percent > 100) {
        volume_percent = 100;
    }

    const int amplitude = (12000 * volume_percent) / 100;
    const uint32_t total_samples = (SPK_SAMPLE_RATE_HZ * (uint32_t)duration_ms) / 1000U;
    const uint32_t half_period = SPK_SAMPLE_RATE_HZ / ((uint32_t)freq_hz * 2U);
    uint32_t phase = 0;
    uint32_t written_samples = 0;

    while (written_samples < total_samples) {
        uint32_t chunk = total_samples - written_samples;
        if (chunk > SPK_CHUNK_SAMPLES) {
            chunk = SPK_CHUNK_SAMPLES;
        }

        for (uint32_t i = 0; i < chunk; ++i) {
            uint32_t sample_index = written_samples + i;
            int env = amplitude;
            const uint32_t fade = SPK_SAMPLE_RATE_HZ / 200U;
            if (sample_index < fade) {
                env = (amplitude * (int)sample_index) / (int)fade;
            } else if (total_samples > fade && sample_index > total_samples - fade) {
                env = (amplitude * (int)(total_samples - sample_index)) / (int)fade;
            }
            s_samples[i] = (phase < half_period) ? (int16_t)env : (int16_t)-env;
            phase++;
            if (half_period > 0 && phase >= half_period * 2U) {
                phase = 0;
            }
        }

        size_t bytes_written = 0;
        esp_err_t ret = i2s_channel_write(s_tx_chan,
                                          s_samples,
                                          chunk * sizeof(s_samples[0]),
                                          &bytes_written,
                                          pdMS_TO_TICKS(duration_ms + 50));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "I2S write failed: %d", ret);
            break;
        }
        written_samples += chunk;
    }

    memset(s_samples, 0, sizeof(s_samples));
    size_t bytes_written = 0;
    i2s_channel_write(s_tx_chan, s_samples, sizeof(s_samples), &bytes_written, pdMS_TO_TICKS(30));
}
