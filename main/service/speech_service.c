#include "service/speech_service.h"
#include "esp_log.h"

static const char *TAG = "SPEECH_SVC";

/* Speech mapping: each speech ID plays a distinctive tone chime.
 * Future: load WAV from SPIFFS/voice/ partition.
 */

esp_err_t speech_service_say(speech_id_t id, audio_play_priority_t prio)
{
    audio_play_request_t req = {
        .cmd      = AUDIO_PLAY_CMD_TONE,
        .priority = prio,
        .volume   = 0,   /* use system default volume */
    };

    switch (id) {
    case SPEECH_WELCOME_HOME: {
        /* C5 → E5 rising chime */
        req.tone.freq_hz = 523;  req.tone.duration_ms = 140;
        audio_playback_submit(&req);
        req.tone.freq_hz = 659;  req.tone.duration_ms = 220;
        audio_playback_submit(&req);
        break;
    }
    case SPEECH_ALARM_WARNING: {
        /* 3 rapid beeps at 1.2kHz */
        req.tone.duration_ms = 100;
        for (int i = 0; i < 3; i++) {
            req.tone.freq_hz = 1200;
            audio_playback_submit(&req);
            if (i < 2) {
                req.tone.freq_hz = 0; req.tone.duration_ms = 60;
                audio_playback_submit(&req);
                req.tone.duration_ms = 100;
            }
        }
        break;
    }
    case SPEECH_GLASS_ALERT: {
        /* High sharp double ping */
        req.tone.freq_hz = 2200; req.tone.duration_ms = 80;
        audio_playback_submit(&req);
        req.tone.freq_hz = 0;    req.tone.duration_ms = 40;
        audio_playback_submit(&req);
        req.tone.freq_hz = 2500; req.tone.duration_ms = 120;
        audio_playback_submit(&req);
        break;
    }
    case SPEECH_DOORBELL_VISITOR: {
        /* Ding-dong (G5 → E5) */
        req.tone.freq_hz = 784;  req.tone.duration_ms = 220;
        audio_playback_submit(&req);
        req.tone.freq_hz = 0;    req.tone.duration_ms = 80;
        audio_playback_submit(&req);
        req.tone.freq_hz = 659;  req.tone.duration_ms = 300;
        audio_playback_submit(&req);
        break;
    }
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
