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

/* 敲门声类别 ID（音频模型 12 类中的第 2 类），触发"欢迎回家"特效 */
#define AUDIO_CLASS_KNOCK 2

/*
 * UI 任务（FreeRTOS 消费者 + LVGL 桥接层）
 * ====================================================
 * 职责：阻塞等待全局事件队列，收到分类结果后在 LVGL 锁内刷新界面。
 *
 * 关键点（面试常问）：
 *  - LVGL 不是线程安全的！所有 lv_* 调用必须在 mipi_dsi_lcd_lock()/unlock()
 *    之间执行，否则会和 LVGL 渲染任务（esp_lv_adapter 内部）竞争对象树；
 *  - 本任务阻塞在 event_srv_receive(100ms 超时) 上，平时不占 CPU；
 *  - 音频分类和视觉分类共用同一个事件队列，按 type 分发。
 */
void ui_task(void *pvParameters)
{
    ESP_LOGI(TAG, "UI task started");

    /* 主循环：收事件 -> 处理 -> 继续收 */
    while (1) {
        event_t evt;
        /* 阻塞收事件（100ms 超时防止永久挂起），失败就继续循环 */
        if (event_srv_receive(&evt, 100) == ESP_OK) {
            /* UI 还没初始化完（相机/页面创建中），丢弃事件避免空指针 */
            if (!display_app_is_ready()) {
                ESP_LOGW(TAG, "Display app is not ready, UI event dropped");
                continue;
            }
            /* ------- 音频分类事件 ------- */
            if (evt.type == EVENT_AUDIO_CLASSIFICATION) {
                /* 本地正在播提示音/语音时忽略分类，防止"自己声音触发自己" */
                if (audio_playback_is_output_active()) {
                    ESP_LOGI(TAG, "Audio classification ignored during local playback");
                    continue;
                }
                /* 进入 LVGL 临界区（最多等 1 秒），之后才能安全操作 UI */
                if (mipi_dsi_lcd_lock(1000)) {
                    /* 如果快捷面板开着就先关掉，避免遮挡表情页 */
                    if (ui_quick_panel_is_open()) {
                        ui_quick_panel_close_now();
                    }
                    /* 1) 更新内存统计（UI 数据源） */
                    app_state_record_audio(&evt.audio_result, evt.timestamp_ms);
                    /* 2) 判断该类别是否需要"关注"（警报/砸玻璃/婴儿哭等） */
                    bool attention = app_audio_class_needs_attention(evt.audio_result.class_id);
                    /* 3) 写入 NVS 历史（失败不重启，只打日志） */
                    ESP_ERROR_CHECK_WITHOUT_ABORT(history_record_audio(&evt.audio_result,
                                                                       evt.timestamp_ms,
                                                                       attention));
                    /* 4a) 敲门 -> 特殊"欢迎回家"特效 */
                    if (evt.audio_result.class_id == AUDIO_CLASS_KNOCK) {
                        ui_emotion_show_welcome_home(evt.audio_result.confidence);
                        ESP_LOGI(TAG, "Knock detected: welcome home (confidence=%.3f)",
                                 (double)evt.audio_result.confidence);
                    /* 4b) 其他类别 -> 按类别切换表情/眼睛造型 */
                    } else {
                        ui_emotion_set_by_audio(evt.audio_result.class_id,
                                                evt.audio_result.confidence);
                    }
                    /* 5) 需要关注 -> 触发蜂鸣音+马达（异步投递到反馈任务） */
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
                    /* 6) 刷新当前页面 + 释放 LVGL 锁 */
                    display_app_refresh_current();
                    mipi_dsi_lcd_unlock();
                }
            /* ------- 视觉分类事件（人脸检测+情绪） ------- */
            } else if (evt.type == EVENT_VISUAL_CLASSIFICATION) {
                /* 进入 LVGL 临界区（最多等 1 秒），之后才能安全操作 UI */
                if (mipi_dsi_lcd_lock(1000)) {
                    /* 如果快捷面板开着就先关掉，避免遮挡表情页 */
                    if (ui_quick_panel_is_open()) {
                        ui_quick_panel_close_now();
                    }
                    /* 当前在表情页则直接改眼睛表情，否则弹通知条 */
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
                    /* 6) 刷新当前页面 + 释放 LVGL 锁 */
                    display_app_refresh_current();
                    mipi_dsi_lcd_unlock();
                }
            }
        }
    }
}
