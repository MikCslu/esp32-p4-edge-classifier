#include "service/speech_service.h"

#include "esp_log.h"
#include <stdint.h>

static const char *TAG = "SPEECH_SVC";

extern const uint8_t speech_hello_wav_start[] asm("_binary_hello_wav_start");
extern const uint8_t speech_hello_wav_end[] asm("_binary_hello_wav_end");
extern const uint8_t speech_morning_wav_start[] asm("_binary_morning_wav_start");
extern const uint8_t speech_morning_wav_end[] asm("_binary_morning_wav_end");
extern const uint8_t speech_welcome_back_wav_start[] asm("_binary_welcome_back_wav_start");
extern const uint8_t speech_welcome_back_wav_end[] asm("_binary_welcome_back_wav_end");
extern const uint8_t speech_see_you_wav_start[] asm("_binary_see_you_wav_start");
extern const uint8_t speech_see_you_wav_end[] asm("_binary_see_you_wav_end");
extern const uint8_t speech_thanks_wav_start[] asm("_binary_thanks_wav_start");
extern const uint8_t speech_thanks_wav_end[] asm("_binary_thanks_wav_end");
extern const uint8_t speech_help_wav_start[] asm("_binary_help_wav_start");
extern const uint8_t speech_help_wav_end[] asm("_binary_help_wav_end");
extern const uint8_t speech_wait_wav_start[] asm("_binary_wait_wav_start");
extern const uint8_t speech_wait_wav_end[] asm("_binary_wait_wav_end");
extern const uint8_t speech_ok_wav_start[] asm("_binary_ok_wav_start");
extern const uint8_t speech_ok_wav_end[] asm("_binary_ok_wav_end");
extern const uint8_t speech_sorry_wav_start[] asm("_binary_sorry_wav_start");
extern const uint8_t speech_sorry_wav_end[] asm("_binary_sorry_wav_end");
extern const uint8_t speech_busy_wav_start[] asm("_binary_busy_wav_start");
extern const uint8_t speech_busy_wav_end[] asm("_binary_busy_wav_end");
extern const uint8_t speech_later_wav_start[] asm("_binary_later_wav_start");
extern const uint8_t speech_later_wav_end[] asm("_binary_later_wav_end");
extern const uint8_t speech_need_help_wav_start[] asm("_binary_need_help_wav_start");
extern const uint8_t speech_need_help_wav_end[] asm("_binary_need_help_wav_end");

typedef struct {
    const uint8_t *start;
    const uint8_t *end;
    const char *name;
} speech_clip_t;

static const speech_clip_t s_clips[SPEECH_COUNT] = {
    [SPEECH_HELLO] = {
        .start = speech_hello_wav_start,
        .end = speech_hello_wav_end,
        .name = "hello",
    },
    [SPEECH_GOOD_MORNING] = {
        .start = speech_morning_wav_start,
        .end = speech_morning_wav_end,
        .name = "morning",
    },
    [SPEECH_WELCOME_BACK] = {
        .start = speech_welcome_back_wav_start,
        .end = speech_welcome_back_wav_end,
        .name = "welcome_back",
    },
    [SPEECH_SEE_YOU] = {
        .start = speech_see_you_wav_start,
        .end = speech_see_you_wav_end,
        .name = "see_you",
    },
    [SPEECH_THANKS] = {
        .start = speech_thanks_wav_start,
        .end = speech_thanks_wav_end,
        .name = "thanks",
    },
    [SPEECH_HELP] = {
        .start = speech_help_wav_start,
        .end = speech_help_wav_end,
        .name = "help",
    },
    [SPEECH_WAIT] = {
        .start = speech_wait_wav_start,
        .end = speech_wait_wav_end,
        .name = "wait",
    },
    [SPEECH_OK] = {
        .start = speech_ok_wav_start,
        .end = speech_ok_wav_end,
        .name = "ok",
    },
    [SPEECH_SORRY] = {
        .start = speech_sorry_wav_start,
        .end = speech_sorry_wav_end,
        .name = "sorry",
    },
    [SPEECH_BUSY] = {
        .start = speech_busy_wav_start,
        .end = speech_busy_wav_end,
        .name = "busy",
    },
    [SPEECH_LATER] = {
        .start = speech_later_wav_start,
        .end = speech_later_wav_end,
        .name = "later",
    },
    [SPEECH_NEED_HELP] = {
        .start = speech_need_help_wav_start,
        .end = speech_need_help_wav_end,
        .name = "need_help",
    },
};

esp_err_t speech_service_say(speech_id_t id, audio_play_priority_t prio)
{
    if (id < 0 || id >= SPEECH_COUNT) {
        ESP_LOGW(TAG, "Unknown speech id %d", id);
        return ESP_ERR_INVALID_ARG;
    }

    const speech_clip_t *clip = &s_clips[id];
    if (!clip->start || !clip->end || clip->end <= clip->start) {
        ESP_LOGW(TAG, "Speech clip %d is empty", id);
        return ESP_ERR_INVALID_STATE;
    }

    audio_play_request_t req = {
        .cmd = AUDIO_PLAY_CMD_WAV,
        .priority = prio,
        .volume = 0,
    };
    req.pcm.data = clip->start;
    req.pcm.size = (size_t)(clip->end - clip->start);
    req.pcm.sample_rate = 16000;
    req.pcm.bits_per_sample = 16;
    req.pcm.channels = 1;
    req.pcm.free_on_complete = false;

    esp_err_t ret = audio_playback_replace(&req);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to play speech %s: %s", clip->name, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Speech %s started (%u bytes, prio %d)",
             clip->name,
             (unsigned)req.pcm.size,
             prio);
    return ESP_OK;
}

void speech_service_set_root(const char *spiffs_root)
{
    (void)spiffs_root;
}
