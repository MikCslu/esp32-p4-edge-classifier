#include "service/speech_service.h"
#include "service/tts_client.h"
#include "service/wifi_service.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SPEECH_SVC";

/* ─── Embedded WAV blobs (linked by CMake target_add_binary_data) ─── */
extern const uint8_t speech_hello_wav_start[]   asm("_binary_speech_hello_wav_start");
extern const uint8_t speech_hello_wav_end[]     asm("_binary_speech_hello_wav_end");
extern const uint8_t speech_morning_wav_start[] asm("_binary_speech_morning_wav_start");
extern const uint8_t speech_morning_wav_end[]   asm("_binary_speech_morning_wav_end");
extern const uint8_t speech_welcome_wav_start[] asm("_binary_speech_welcome_wav_start");
extern const uint8_t speech_welcome_wav_end[]   asm("_binary_speech_welcome_wav_end");
extern const uint8_t speech_seeyou_wav_start[]  asm("_binary_speech_seeyou_wav_start");
extern const uint8_t speech_seeyou_wav_end[]    asm("_binary_speech_seeyou_wav_end");

static inline size_t _blob_len(const uint8_t *start, const uint8_t *end)
{
    return (size_t)(end - start);
}

static esp_err_t _play_wav(const uint8_t *data, size_t size,
                           audio_play_priority_t prio, bool free_after)
{
    audio_play_request_t req = {
        .cmd       = AUDIO_PLAY_CMD_WAV,
        .priority  = prio,
        .volume    = 0,
    };
    req.pcm.data             = data;
    req.pcm.size             = size;
    req.pcm.sample_rate      = 0;
    req.pcm.bits_per_sample  = 0;
    req.pcm.channels         = 0;
    req.pcm.free_on_complete = free_after;
    return audio_playback_submit(&req);
}

esp_err_t speech_service_say(speech_id_t id, audio_play_priority_t prio)
{
    switch (id) {
    case SPEECH_HELLO:
        return _play_wav(speech_hello_wav_start,
                         _blob_len(speech_hello_wav_start, speech_hello_wav_end),
                         prio, false);
    case SPEECH_GOOD_MORNING:
        return _play_wav(speech_morning_wav_start,
                         _blob_len(speech_morning_wav_start, speech_morning_wav_end),
                         prio, false);
    case SPEECH_WELCOME_BACK:
        return _play_wav(speech_welcome_wav_start,
                         _blob_len(speech_welcome_wav_start, speech_welcome_wav_end),
                         prio, false);
    case SPEECH_SEE_YOU:
        return _play_wav(speech_seeyou_wav_start,
                         _blob_len(speech_seeyou_wav_start, speech_seeyou_wav_end),
                         prio, false);
    default:
        ESP_LOGW(TAG, "Unknown speech id %d", id);
        return ESP_ERR_INVALID_ARG;
    }
}

/* ─── Online TTS with local fallback ─── */

static speech_id_t            s_fallback_id;
static audio_play_priority_t  s_fallback_prio;

static void _tts_done_cb(const uint8_t *wav_data, size_t wav_size, int err)
{
    if (err == 0 && wav_data && wav_size > 0) {
        _play_wav(wav_data, wav_size, s_fallback_prio, true);
        ESP_LOGI(TAG, "Online TTS OK, playing %d bytes", (int)wav_size);
    } else {
        if (wav_data) heap_caps_free((void *)wav_data);
        ESP_LOGW(TAG, "Online TTS failed (err=%d), fallback local %d",
                 err, s_fallback_id);
        speech_service_say(s_fallback_id, s_fallback_prio);
    }
}

esp_err_t speech_service_say_online(const char *text, const char *voice,
                                    speech_id_t fallback,
                                    audio_play_priority_t prio)
{
    if (!wifi_service_is_connected()) {
        ESP_LOGI(TAG, "WiFi down, direct fallback local %d", fallback);
        return speech_service_say(fallback, prio);
    }
    s_fallback_id   = fallback;
    s_fallback_prio = prio;
    tts_client_synthesize(text, voice ? voice : "default", _tts_done_cb);
    return ESP_OK;
}

void speech_service_set_root(const char *spiffs_root)
{
    (void)spiffs_root;
    ESP_LOGI(TAG, "SPIFFS root set to '%s' (future WAV support)", spiffs_root);
}
