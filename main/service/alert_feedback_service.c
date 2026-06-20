#include "service/alert_feedback_service.h"

#include "driver/actuator/motor_driver.h"
#include "driver/audio/max98357_speaker.h"
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
    uint16_t freq_hz;
    uint16_t duration_ms;
    uint16_t gap_ms;
    uint8_t volume;
} tone_step_t;

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
    if (ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

static void play_motor_pattern(const motor_step_t *steps, size_t count)
{
    if (!motor_driver_is_available()) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        motor_driver_set_power(steps[i].power);
        sleep_ms(steps[i].duration_ms);
        motor_driver_stop();
        sleep_ms(steps[i].gap_ms);
    }
}

static void play_tone_pattern(const tone_step_t *steps, size_t count)
{
    if (!max98357_speaker_is_available()) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        max98357_speaker_tone(steps[i].freq_hz, steps[i].duration_ms, steps[i].volume);
        sleep_ms(steps[i].gap_ms);
    }
}

static void play_feedback(int class_id, float confidence)
{
    (void)confidence;

    switch (class_id) {
    case 0: { /* alarm */
        const motor_step_t motor[] = {{1200, 120, 90}, {650, 0, 75}};
        const tone_step_t tones[] = {{1200, 160, 80, 45}, {1600, 160, 80, 45}, {1200, 220, 0, 45}};
        play_motor_pattern(motor, sizeof(motor) / sizeof(motor[0]));
        play_tone_pattern(tones, sizeof(tones) / sizeof(tones[0]));
        break;
    }
    case 1: { /* car horn */
        const motor_step_t motor[] = {{420, 130, 75}, {420, 0, 75}};
        const tone_step_t tones[] = {{420, 260, 90, 42}, {420, 260, 0, 42}};
        play_motor_pattern(motor, sizeof(motor) / sizeof(motor[0]));
        play_tone_pattern(tones, sizeof(tones) / sizeof(tones[0]));
        break;
    }
    case 2: { /* knocking */
        const motor_step_t motor[] = {{95, 85, 55}, {95, 0, 55}};
        const tone_step_t tones[] = {{720, 80, 70, 30}, {720, 80, 0, 30}};
        play_motor_pattern(motor, sizeof(motor) / sizeof(motor[0]));
        play_tone_pattern(tones, sizeof(tones) / sizeof(tones[0]));
        break;
    }
    case 3: { /* clapping */
        const motor_step_t motor[] = {{90, 0, 45}};
        const tone_step_t tones[] = {{900, 70, 0, 25}};
        play_motor_pattern(motor, sizeof(motor) / sizeof(motor[0]));
        play_tone_pattern(tones, sizeof(tones) / sizeof(tones[0]));
        break;
    }
    case 6: { /* glass break */
        const motor_step_t motor[] = {{80, 55, 90}, {80, 55, 90}, {120, 0, 90}};
        const tone_step_t tones[] = {{1800, 70, 45, 45}, {2200, 70, 45, 45}, {1800, 120, 0, 45}};
        play_motor_pattern(motor, sizeof(motor) / sizeof(motor[0]));
        play_tone_pattern(tones, sizeof(tones) / sizeof(tones[0]));
        break;
    }
    case 7: { /* doorbell */
        const motor_step_t motor[] = {{120, 120, 40}, {160, 0, 40}};
        const tone_step_t tones[] = {{660, 140, 80, 30}, {880, 180, 0, 30}};
        play_motor_pattern(motor, sizeof(motor) / sizeof(motor[0]));
        play_tone_pattern(tones, sizeof(tones) / sizeof(tones[0]));
        break;
    }
    case 8: { /* crying baby */
        const motor_step_t motor[] = {{260, 80, 45}, {260, 80, 60}, {360, 0, 45}};
        const tone_step_t tones[] = {{520, 120, 90, 25}, {620, 120, 90, 25}, {520, 120, 0, 25}};
        play_motor_pattern(motor, sizeof(motor) / sizeof(motor[0]));
        play_tone_pattern(tones, sizeof(tones) / sizeof(tones[0]));
        break;
    }
    default:
        break;
    }
}

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

esp_err_t alert_feedback_service_start(void)
{
    if (s_running) {
        return ESP_OK;
    }

    esp_err_t motor_ret = motor_driver_init();
    if (motor_ret != ESP_OK) {
        ESP_LOGW(TAG, "Motor feedback disabled: %d", motor_ret);
    }

    esp_err_t speaker_ret = max98357_speaker_init();
    if (speaker_ret != ESP_OK) {
        ESP_LOGW(TAG, "Speaker feedback disabled: %d", speaker_ret);
    }

    s_queue = xQueueCreate(ALERT_FEEDBACK_QUEUE_DEPTH, sizeof(alert_feedback_event_t));
    if (!s_queue) {
        ESP_LOGW(TAG, "Queue create failed");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(feedback_task,
                                            ALERT_FEEDBACK_TASK_NAME,
                                            ALERT_FEEDBACK_TASK_STACK,
                                            NULL,
                                            ALERT_FEEDBACK_TASK_PRIO,
                                            &s_task,
                                            ALERT_FEEDBACK_TASK_CORE);
    if (ok != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        ESP_LOGW(TAG, "Task create failed");
        return ESP_ERR_NO_MEM;
    }

    s_running = true;
    ESP_LOGI(TAG, "Alert feedback service ready");
    return ESP_OK;
}

void alert_feedback_trigger(int class_id, float confidence)
{
    if (!s_running || !app_audio_class_needs_attention(class_id)) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if (class_id >= 0 && class_id < APP_AUDIO_CLASS_COUNT) {
        int64_t quiet_us = (class_id == 0 || class_id == 6) ? 1500000LL : 2300000LL;
        if (now_us - s_last_trigger_us[class_id] < quiet_us) {
            return;
        }
        s_last_trigger_us[class_id] = now_us;
    }

    alert_feedback_event_t event = {
        .class_id = class_id,
        .confidence = confidence,
    };

    if (xQueueSend(s_queue, &event, 0) != pdTRUE) {
        alert_feedback_event_t dropped;
        (void)xQueueReceive(s_queue, &dropped, 0);
        (void)xQueueSend(s_queue, &event, 0);
    }
}

bool alert_feedback_service_is_running(void)
{
    return s_running;
}
