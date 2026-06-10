#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "tasks/ui_task.h"
#include "driver/display/dsi_lcd.h"
#include "service/event_service.h"
#include "service/app_state.h"
#include "app/display_app.h"
#include "lvgl_port/ui/ui_emotion.h"
#include "lvgl_port/ui/ui_notify.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_TASK";

#define AUDIO_CLASS_KNOCK 2

void ui_task(void *pvParameters)
{
    ESP_LOGI(TAG, "UI task started");

    while (1) {
        event_t evt;
        if (event_srv_receive(&evt, 100) == ESP_OK) {
            if (evt.type == EVENT_AUDIO_CLASSIFICATION) {
                if (mipi_dsi_lcd_lock(1000)) {
                    app_state_record_audio(&evt.audio_result, evt.timestamp_ms);
                    if (evt.audio_result.class_id == AUDIO_CLASS_KNOCK) {
                        ui_emotion_show_welcome_home(evt.audio_result.confidence);
                        ESP_LOGI(TAG, "Knock detected: welcome home (confidence=%.3f)",
                                 (double)evt.audio_result.confidence);
                    } else {
                        ui_emotion_set_by_audio(evt.audio_result.class_id,
                                                evt.audio_result.confidence);
                    }
                    bool attention = app_audio_class_needs_attention(evt.audio_result.class_id);
                    if (attention && display_app_get_current_page() != DISPLAY_PAGE_EMOTION) {
                        display_app_switch_page(DISPLAY_PAGE_EMOTION);
                    } else if (!attention &&
                               display_app_get_current_page() != DISPLAY_PAGE_EMOTION &&
                               evt.audio_result.class_id >= 0) {
                        char text[96];
                        snprintf(text, sizeof(text), "%s  %d%%",
                                 app_audio_class_title(evt.audio_result.class_id),
                                 (int)(evt.audio_result.confidence * 100.0f + 0.5f));
                        ui_notify_show(text, 2800);
                    }
                    display_app_refresh_current();
                    mipi_dsi_lcd_unlock();
                }
            }
        }
    }
}
