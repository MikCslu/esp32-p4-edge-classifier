#pragma once
#include "service/audio_playback_service.h"
#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPEECH_HELLO = 0,
    SPEECH_GOOD_MORNING,
    SPEECH_WELCOME_BACK,
    SPEECH_SEE_YOU,
    SPEECH_THANKS,
    SPEECH_HELP,
    SPEECH_WAIT,
    SPEECH_OK,
    SPEECH_SORRY,
    SPEECH_BUSY,
    SPEECH_LATER,
    SPEECH_NEED_HELP,
    SPEECH_COUNT,
} speech_id_t;

/**
 * Play a preset speech clip at the given priority.
 * Returns ESP_OK if queued successfully; the actual output is async.
 */
esp_err_t speech_service_say(speech_id_t id, audio_play_priority_t prio);

/* Kept as a compatibility hook for future external-file speech packs. */
void speech_service_set_root(const char *spiffs_root);

#ifdef __cplusplus
}
#endif
