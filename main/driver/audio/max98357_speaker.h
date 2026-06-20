#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t max98357_speaker_init(void);
bool max98357_speaker_is_available(void);
void max98357_speaker_tone(uint16_t freq_hz, uint16_t duration_ms, uint8_t volume_percent);

#ifdef __cplusplus
}
#endif
