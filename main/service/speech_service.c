#include "service/speech_service.h"

#include "esp_log.h"

static const char *TAG = "SPEECH_SVC";

static void submit_tone(audio_play_request_t *req, uint16_t freq_hz, uint16_t duration_ms)
{
    req->tone.freq_hz = freq_hz;
    req->tone.duration_ms = duration_ms;
    (void)audio_playback_submit(req);
}

esp_err_t speech_service_say(speech_id_t id, audio_play_priority_t prio)
{
    audio_play_request_t req = {
        .cmd = AUDIO_PLAY_CMD_TONE,
        .priority = prio,
        .volume = 0,
    };

    switch (id) {
    case SPEECH_HELLO:
        submit_tone(&req, 660, 120);
        submit_tone(&req, 0, 45);
        submit_tone(&req, 880, 180);
        break;

    case SPEECH_GOOD_MORNING:
        submit_tone(&req, 523, 100);
        submit_tone(&req, 659, 100);
        submit_tone(&req, 784, 220);
        break;

    case SPEECH_WELCOME_BACK:
        submit_tone(&req, 587, 150);
        submit_tone(&req, 0, 55);
        submit_tone(&req, 740, 220);
        break;

    case SPEECH_SEE_YOU:
        submit_tone(&req, 784, 130);
        submit_tone(&req, 0, 45);
        submit_tone(&req, 587, 220);
        break;

    default:
        ESP_LOGW(TAG, "Unknown speech id %d", id);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Speech %d queued at prio %d", id, prio);
    return ESP_OK;
}

void speech_service_set_root(const char *spiffs_root)
{
    (void)spiffs_root;
    ESP_LOGI(TAG, "SPIFFS root set to '%s' (future WAV support)", spiffs_root);
}
