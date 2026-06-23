#include "service/alert_feedback_service.h"
#include "service/audio_playback_service.h"
#include "service/speech_service.h"

#include "driver/actuator/motor_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "runtime/task_config.h"
#include "service/app_state.h"

static const char *TAG = "ALERT_FB";

typedef struct {
    int class_id;
    float confidence;
} alert_feedback_event_t;

typedef struct {
    uint16_t duration_ms;
    uint16_t gap_ms;
    uint8_t power;
} motor_step_t;

static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static bool s_running;
static int64_t s_last_trigger_us[APP_AUDIO_CLASS_COUNT];

static void sleep_ms(uint32_t ms)
{
    if (ms > 0) vTaskDelay(pdMS_TO_TICKS(ms));
}

static void play_motor_pattern(const motor_step_t *steps, size_t count)
{
    if (!motor_driver_is_available()) return;
    for (size_t i = 0; i < count; ++i) {
        motor_driver_set_power(steps[i].power);
        sleep_ms(steps[i].duration_ms);
        motor_driver_stop();
        sleep_ms(steps[i].gap_ms);
    }
}

/* ─── Submit a tone to audio_playback_service (async, non-blocking) ─── */
static void submit_tone(uint16_t freq, uint16_t dur_ms, uint8_t vol,
                        audio_play_priority_t prio)
{
    audio_play_request_t req = {
        .cmd       = AUDIO_PLAY_CMD_TONE,
        .priority  = prio,
        .volume    = vol,
    };
    req.tone.freq_hz     = freq;
    req.tone.duration_ms = dur_ms;
    audio_playback_submit(&req);
}

/* ─── Feedback per class ─── */
static void play_feedback(int class_id, float confidence)
{
    (void)confidence;

    switch (class_id) {
    case 0: { /* alarm → speech + motor */
        speech_service_say(SPEECH_ALARM_WARNING, AUDIO_PLAY_PRIO_ALERT);
        const motor_step_t motor[] = {{1200,120,90},{650,0,75}};
        play_motor_pattern(motor, 2);
        break;
    }
    case 1: { /* car horn */
        submit_tone(420, 260, 42, AUDIO_PLAY_PRIO_ALERT);
        const motor_step_t motor[] = {{420,130,75},{420,0,75}};
        play_motor_pattern(motor, 2);
        submit_tone(420, 260, 42, AUDIO_PLAY_PRIO_ALERT);
        break;
    }
    case 2: { /* knocking → speech */
        speech_service_say(SPEECH_WELCOME_HOME, AUDIO_PLAY_PRIO_ALERT);
        const motor_step_t motor[] = {{95,85,55},{95,0,55}};
        play_motor_pattern(motor, 2);
        break;
    }
    case 3: { /* clapping */
        submit_tone(900, 70, 25, AUDIO_PLAY_PRIO_ALERT);
        const motor_step_t motor[] = {{90,0,45}};
        play_motor_pattern(motor, 1);
        break;
    }
    case 6: { /* glass break → speech */
        speech_service_say(SPEECH_GLASS_ALERT, AUDIO_PLAY_PRIO_URGENT);
        const motor_step_t motor[] = {{80,55,90},{80,55,90},{120,0,90}};
        play_motor_pattern(motor, 3);
        break;
    }
    case 7: { /* doorbell → speech */
        speech_service_say(SPEECH_DOORBELL_VISITOR, AUDIO_PLAY_PRIO_ALERT);
        const motor_step_t motor[] = {{120,120,40},{160,0,40}};
        play_motor_pattern(motor, 2);
        break;
    }
    case 8: { /* crying baby */
        submit_tone(520, 120, 25, AUDIO_PLAY_PRIO_ALERT);
        const motor_step_t motor[] = {{260,80,45},{260,80,60},{360,0,45}};
        play_motor_pattern(motor, 3);
        submit_tone(620, 120, 25, AUDIO_PLAY_PRIO_ALERT);
        submit_tone(520, 120, 25, AUDIO_PLAY_PRIO_ALERT);
        break;
    }
    default:
        break;
    }
}

/* ─── Worker task ─── */
static void feedback_task(void *arg)
{
    (void)arg;
    alert_feedback_event_t event;

    while (1) {
        if (xQueueReceive(s_queue, &event, portMAX_DELAY) == pdTRUE) {
            play_feedback(event.class_id, event.confidence);
        }
    }
}

/* ─── Public ─── */

esp_err_t alert_feedback_service_start(void)
{
    if (s_running) return ESP_OK;

    esp_err_t motor_ret = motor_driver_init();
    if (motor_ret != ESP_OK)
        ESP_LOGW(TAG, "Motor disabled: %d", motor_ret);

    /* audio_playback_service already initialises the speaker,
     * no need to call max98357_speaker_init() here. */

    s_queue = xQueueCreate(ALERT_FEEDBACK_QUEUE_DEPTH,
                           sizeof(alert_feedback_event_t));
    if (!s_queue) return ESP_ERR_NO_MEM;

    BaseType_t ok = xTaskCreatePinnedToCore(
        feedback_task, ALERT_FEEDBACK_TASK_NAME, ALERT_FEEDBACK_TASK_STACK,
        NULL, ALERT_FEEDBACK_TASK_PRIO, &s_task, ALERT_FEEDBACK_TASK_CORE);
    if (ok != pdPASS) {
        vQueueDelete(s_queue); s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_running = true;
    ESP_LOGI(TAG, "Alert feedback ready");
    return ESP_OK;
}

void alert_feedback_trigger(int class_id, float confidence)
{
    if (!s_running || !app_audio_class_needs_attention(class_id)) return;

    int64_t now_us = esp_timer_get_time();
    if (class_id >= 0 && class_id < APP_AUDIO_CLASS_COUNT) {
        int64_t quiet = (class_id == 0 || class_id == 6) ? 1500000LL : 2300000LL;
        if (now_us - s_last_trigger_us[class_id] < quiet) return;
        s_last_trigger_us[class_id] = now_us;
    }

    alert_feedback_event_t event = { .class_id = class_id, .confidence = confidence };
    if (xQueueSend(s_queue, &event, 0) != pdTRUE) {
        alert_feedback_event_t dropped;
        xQueueReceive(s_queue, &dropped, 0);
        xQueueSend(s_queue, &event, 0);
    }
}

bool alert_feedback_service_is_running(void) { return s_running; }
