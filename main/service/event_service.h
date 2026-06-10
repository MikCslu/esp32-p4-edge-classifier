#pragma once
#include "esp_err.h"
#include "service/audio_types.h"
#include <stdint.h>

typedef enum {
    EVENT_AUDIO_CLASSIFICATION,
    EVENT_TOUCH,
    EVENT_SYSTEM,
} event_type_t;

typedef struct {
    event_type_t type;
    audio_class_result_t audio_result;
    uint32_t timestamp_ms;
} event_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t event_srv_init(void);
esp_err_t event_srv_post(const event_t *event);
esp_err_t event_srv_receive(event_t *event, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
