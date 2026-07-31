#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_PLAY_CMD_TONE,   /* play a square-wave tone */
    AUDIO_PLAY_CMD_WAV,    /* play raw PCM or WAV blob */
    AUDIO_PLAY_CMD_STOP,   /* stop current playback */
    AUDIO_PLAY_CMD_VOLUME, /* change volume (0–100) */
} audio_play_cmd_t;

typedef enum {
    AUDIO_PLAY_PRIO_BG       = 0,  /* background / idle sounds */
    AUDIO_PLAY_PRIO_NORMAL   = 1,  /* user-requested speech */
    AUDIO_PLAY_PRIO_ALERT    = 2,  /* detection alerts */
    AUDIO_PLAY_PRIO_URGENT   = 3,  /* highest priority, preempts */
} audio_play_priority_t;

typedef struct {
    audio_play_cmd_t     cmd;
    audio_play_priority_t priority;
    union {
        struct {
            uint16_t freq_hz;
            uint16_t duration_ms;
        } tone;
        struct {
            const uint8_t *data;
            size_t          size;
            uint32_t        sample_rate;
            uint8_t         bits_per_sample;
            uint8_t         channels;
            bool            free_on_complete;
        } pcm;
    };
    uint8_t volume;           /* 0–100, 0 = keep current */
} audio_play_request_t;

esp_err_t audio_playback_service_start(void);
esp_err_t audio_playback_submit(const audio_play_request_t *req);
esp_err_t audio_playback_replace(const audio_play_request_t *req);
void     audio_playback_stop(void);
void     audio_playback_set_volume(uint8_t vol);
uint8_t  audio_playback_get_volume(void);
bool     audio_playback_is_output_active(void);

#ifdef __cplusplus
}
#endif
