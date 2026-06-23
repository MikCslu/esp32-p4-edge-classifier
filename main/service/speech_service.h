#pragma once
#include "service/audio_playback_service.h"
#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPEECH_WELCOME_HOME = 0,
    SPEECH_ALARM_WARNING,
    SPEECH_GLASS_ALERT,
    SPEECH_DOORBELL_VISITOR,
    SPEECH_COUNT,
} speech_id_t;

/**
 * Play a preset speech clip at the given priority.
 * Returns ESP_OK if queued successfully; the actual output is async.
 */
esp_err_t speech_service_say(speech_id_t id, audio_play_priority_t prio);

/* Future SPIFFS integration */
void speech_service_set_root(const char *spiffs_root);

#ifdef __cplusplus
}
#endif
