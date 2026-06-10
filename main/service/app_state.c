#include "service/app_state.h"

#include <string.h>

static const char *s_class_names[APP_AUDIO_CLASS_COUNT] = {
    "alarm",
    "car_horn",
    "knocking",
    "clapping",
    "dog_bark",
    "footsteps",
    "glass_break",
    "doorbell",
    "crying_baby",
    "engine",
    "traffic",
    "background",
};

static const char *s_class_titles[APP_AUDIO_CLASS_COUNT] = {
    "Alarm",
    "Car horn",
    "Knocking",
    "Clapping",
    "Dog bark",
    "Footsteps",
    "Glass break",
    "Doorbell",
    "Baby crying",
    "Engine",
    "Traffic",
    "Background",
};

static app_audio_stats_t s_audio_stats = {
    .last_class_id = -1,
};

const char *app_audio_class_name(int class_id)
{
    if (class_id >= 0 && class_id < APP_AUDIO_CLASS_COUNT) {
        return s_class_names[class_id];
    }
    return "unknown";
}

const char *app_audio_class_title(int class_id)
{
    if (class_id >= 0 && class_id < APP_AUDIO_CLASS_COUNT) {
        return s_class_titles[class_id];
    }
    return "Unknown";
}

bool app_audio_class_needs_attention(int class_id)
{
    switch (class_id) {
    case 0:  /* alarm */
    case 1:  /* car horn */
    case 2:  /* knocking */
    case 3:  /* clapping */
    case 6:  /* glass break */
    case 7:  /* doorbell */
    case 8:  /* crying baby */
        return true;
    default:
        return false;
    }
}

void app_state_record_audio(const audio_class_result_t *result, uint32_t timestamp_ms)
{
    if (!result || result->class_id < 0 || result->class_id >= APP_AUDIO_CLASS_COUNT) {
        return;
    }

    int class_id = result->class_id;
    s_audio_stats.total_events++;
    s_audio_stats.class_counts[class_id]++;
    s_audio_stats.confidence_sum[class_id] += result->confidence;
    s_audio_stats.last_class_id = class_id;
    s_audio_stats.last_confidence = result->confidence;
    s_audio_stats.last_timestamp_ms = timestamp_ms;

    if (s_audio_stats.recent_count < APP_AUDIO_RECENT_MAX) {
        s_audio_stats.recent_count++;
    }
    memmove(&s_audio_stats.recent[1], &s_audio_stats.recent[0],
            (s_audio_stats.recent_count - 1) * sizeof(s_audio_stats.recent[0]));
    s_audio_stats.recent[0].class_id = class_id;
    s_audio_stats.recent[0].confidence = result->confidence;
    s_audio_stats.recent[0].timestamp_ms = timestamp_ms;
}

const app_audio_stats_t *app_state_get_audio(void)
{
    return &s_audio_stats;
}

float app_state_audio_avg_confidence(int class_id)
{
    if (class_id < 0 || class_id >= APP_AUDIO_CLASS_COUNT ||
        s_audio_stats.class_counts[class_id] == 0) {
        return 0.0f;
    }
    return s_audio_stats.confidence_sum[class_id] /
           (float)s_audio_stats.class_counts[class_id];
}
