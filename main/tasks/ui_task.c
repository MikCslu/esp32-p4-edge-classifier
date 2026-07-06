#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "tasks/ui_task.h"
#include "driver/display/dsi_lcd.h"
#include "service/alert_feedback_service.h"
#include "service/audio_playback_service.h"
#include "service/event_service.h"
#include "service/app_state.h"
#include "service/history_service.h"
#include "app/display_app.h"
#include "lvgl_port/ui/ui_emotion.h"
#include "lvgl_port/ui/ui_notify.h"
#include "lvgl_port/ui/ui_quick_panel.h"
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
            if (!display_app_is_ready()) {
                ESP_LOGW(TAG, "Display app is not ready, UI event dropped");
                continue;
            }
            if (evt.type == EVENT_AUDIO_CLASSIFICATION) {
                if (audio_playback_is_output_active()) {
                    ESP_LOGI(TAG, "Audio classification ignored during local playback");
                    continue;
                }
                if (mipi_dsi_lcd_lock(1000)) {
                    if (ui_quick_panel_is_open()) {
                        ui_quick_panel_close_now();
                    }
                    app_state_record_audio(&evt.audio_result, evt.timestamp_ms);
                    bool attention = app_audio_class_needs_attention(evt.audio_result.class_id);
                    ESP_ERROR_CHECK_WITHOUT_ABORT(history_record_audio(&evt.audio_result,
                                                                       evt.timestamp_ms,
                                                                       attention));
                    if (evt.audio_result.class_id == AUDIO_CLASS_KNOCK) {
                        ui_emotion_show_welcome_home(evt.audio_result.confidence);
                        ESP_LOGI(TAG, "Knock detected: welcome home (confidence=%.3f)",
                                 (double)evt.audio_result.confidence);
                    } else {
                        ui_emotion_set_by_audio(evt.audio_result.class_id,
                                                evt.audio_result.confidence);
                    }
                    if (attention) {
                        alert_feedback_trigger(evt.audio_result.class_id,
                                               evt.audio_result.confidence);
                    }
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
            } else if (evt.type == EVENT_VISUAL_CLASSIFICATION) {
                if (mipi_dsi_lcd_lock(1000)) {
                    if (ui_quick_panel_is_open()) {
                        ui_quick_panel_close_now();
                    }
                    const visual_cls_result_t *result = &evt.visual_result;
                    if (display_app_get_current_page() == DISPLAY_PAGE_EMOTION) {
                        ui_emotion_set_by_visual(result->emotion_id,
                                                 result->emotion_confidence,
                                                 result->face_score);
                    } else if (result->emotion_id >= 0) {
                        char text[96];
                        snprintf(text, sizeof(text), "Face %s  %d%%",
                                 visual_emotion_name(result->emotion_id),
                                 (int)(result->emotion_confidence * 100.0f + 0.5f));
                        ui_notify_show(text, 2200);
                    }
                    display_app_refresh_current();
                    mipi_dsi_lcd_unlock();
                }
            }
        }
    }
}
