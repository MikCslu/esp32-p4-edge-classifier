#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*tts_done_cb_t)(const uint8_t *wav_data, size_t wav_size, int err);

void tts_client_init(const char *backend_url);
void tts_client_synthesize(const char *text, const char *voice,
                           tts_done_cb_t cb);
bool tts_client_is_busy(void);

#ifdef __cplusplus
}
#endif
