#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int class_id;
    float confidence;
    bool triggered;
} audio_class_result_t;

#ifdef __cplusplus
}
#endif
