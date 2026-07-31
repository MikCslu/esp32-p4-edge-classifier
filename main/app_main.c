#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/display_hal.h"
#include "hal/audio_hal.h"
#include "driver/display/dsi_lcd.h"
#include "service/event_service.h"
#include "service/app_config_service.h"
#include "service/app_state.h"
#include "service/alert_feedback_service.h"
#include "service/audio_playback_service.h"
#include "service/camera_service.h"
#include "service/touch_input_service.h"
#include "service/visual_classify_service.h"
#include "service/history_service.h"
#include "service/audio_frame_bus.h"
#include "service/telemetry_service.h"
#include "app/audio_event_app.h"
#include "app/display_app.h"
#include "runtime/task_config.h"
#include "tasks/audio_capture_task.h"
#include "tasks/audio_inference_task.h"
#include "tasks/ui_task.h"

/*
 * ============================================================
 * 系统启动流程（面试重点：FreeRTOS 多任务初始化顺序）
 * ============================================================
 * 1. 基础服务：NVS 配置、历史记录、事件队列、音频帧总线；
 * 2. 显示初始化（MIPI-DSI + LVGL 适配器），创建 5 个 UI 页面；
 * 3. 启动相机服务，等待摄像头就绪；
 * 4. 启动触摸、音频播放、告警反馈等外设服务（各自内部再建任务）；
 * 5. 创建 3 个核心任务：audio_capture(生产者) / audio_inference(消费者) / ui_task(事件消费者)；
 * 6. 把各任务句柄注册给遥测服务，用于周期性打印运行统计。
 * app_main 本身创建完任务后自杀（vTaskDelete(NULL)），
 * 之后系统完全由上述任务驱动。
 */

static const char *TAG = "APP_MAIN";

/* 开机时把 NVS 里持久化的历史记录回填到内存统计（app_state），
 * 这样 UI 上的"最近分类/总数"在重启后不会清零。 */
static void restore_audio_stats_from_history(void)
{
    history_record_t records[HISTORY_AUDIO_MAX];
    size_t count = history_get_recent(records, HISTORY_AUDIO_MAX);

    for (size_t i = 0; i < count; i++) {
        const history_record_t *record = &records[count - 1 - i];
        /* 注意历史记录只存了百分比整数，这里还原成 0~1 的 float */
        audio_class_result_t result = {
            .class_id = record->class_id,
            .confidence = (float)record->confidence_pct / 100.0f,
            .triggered = (record->flags & 0x01) != 0,
        };
        app_state_record_audio(&result, record->timestamp_ms);
    }
    if (count > 0) {
        ESP_LOGI(TAG, "Restored %u audio history records", (unsigned)count);
    }
}

/* FreeRTOS 核心绑定任务创建封装：
 * xTaskCreatePinnedToCore() 是 xTaskCreate() 的双核版本，
 * 第 6 个参数 core 指定任务固定在哪个 CPU 核上运行。
 * 返回值 pdPASS 表示创建成功。 */
static esp_err_t start_pinned_task(TaskFunction_t task,
                                   const char *name,
                                   uint32_t stack,
                                   UBaseType_t priority,
                                   BaseType_t core,
                                   TaskHandle_t *handle)
{
    BaseType_t ok = xTaskCreatePinnedToCore(task,
                                            name,
                                            stack,
                                            NULL,
                                            priority,
                                            handle,
                                            core);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task %s", name);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ESP-IDF 入口函数（C++ 编译，所以加 extern "C" 避免名字修饰） */
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESP32-P4 Edge Classifier v1.0");
    ESP_LOGI(TAG, "========================================");

    /* 1. NVS 初始化（配置持久化） */
    ESP_ERROR_CHECK(app_config_init());
    /* 2. 历史记录服务（分类事件持久化到 NVS） */
    ESP_ERROR_CHECK(history_service_init());
    /* 3. 回填历史统计到内存，UI 直接读内存 */
    restore_audio_stats_from_history();
    /* 4. 全局事件队列：任务间通信的"总线" */
    ESP_ERROR_CHECK(event_srv_init());
    /* 5. 音频帧总线：采集任务 -> 推理任务的环形缓冲 */
    ESP_ERROR_CHECK(audio_frame_bus_init());

    TaskHandle_t audio_capture_handle = NULL;
    TaskHandle_t audio_infer_handle = NULL;
    TaskHandle_t ui_handle = NULL;

    /* 6. 初始化 MIPI-DSI 屏 + LVGL（内部会启动 LVGL 渲染任务） */
    display_hal_get_instance()->init(NULL, NULL);

    /* 7. 音频事件应用层初始化（播放/语音联动逻辑） */
    audio_event_app_init();
    /* 8. 创建 5 个 LVGL 全屏页面（Audio/Timeline/Tuning/Eyes/Voice） */
    display_app_init();

    /* 9. 启动相机任务（Core 1 上取流 + 缩放） */
    ESP_ERROR_CHECK(camera_service_start());
    /* 最多等 3 秒让摄像头出图，避免后续推理拿到空帧 */
    for (int i = 0; i < 30 && !camera_service_is_ready(); i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    /* 10. 触摸服务：轮询 GT911，喂给 LVGL 输入设备 */
    esp_err_t touch_ret = touch_input_service_start();
    if (touch_ret != ESP_OK) {
        ESP_LOGW(TAG, "Touch input disabled: %d", touch_ret);
    }

    /* 11. 音频播放服务：I2S 出音（提示音/语音），内部一个任务 */
    esp_err_t audio_ret = audio_playback_service_start();
    if (audio_ret != ESP_OK) {
        ESP_LOGW(TAG, "Audio playback disabled: %d", audio_ret);
    }

    /* 12. 告警反馈：根据分类结果播蜂鸣音 + 马达振动 */
    esp_err_t feedback_ret = alert_feedback_service_start();
    if (feedback_ret != ESP_OK) {
        ESP_LOGW(TAG, "Alert feedback disabled: %d", feedback_ret);
    }

    /* 13. 从 NVS 恢复用户设置（音量/马达开关/强度） */
    app_device_settings_t settings;
    if (app_config_load_device_settings(&settings) == ESP_OK) {
        audio_playback_set_volume(settings.volume);
        alert_feedback_set_motor_enabled(settings.motor_enabled);
        alert_feedback_set_motor_strength(settings.motor_strength);
    }

    /* 14. 创建音频采集任务：Core 0，最高优先级 10 */
    ESP_ERROR_CHECK(start_pinned_task(audio_capture_task,
                                      AUDIO_CAPTURE_TASK_NAME,
                                      AUDIO_CAPTURE_TASK_STACK,
                                      AUDIO_CAPTURE_TASK_PRIO,
                                      AUDIO_CAPTURE_TASK_CORE,
                                      &audio_capture_handle));
    /* 15. 创建音频推理任务：Core 0，优先级 4 */
    ESP_ERROR_CHECK(start_pinned_task(audio_inference_task,
                                      AUDIO_INFER_TASK_NAME,
                                      AUDIO_INFER_TASK_STACK,
                                      AUDIO_INFER_TASK_PRIO,
                                      AUDIO_INFER_TASK_CORE,
                                      &audio_infer_handle));

    /* 16. 初始化视觉分类（人脸检测 + 情绪模型），内部再启动推理任务 */
    ESP_ERROR_CHECK(visual_cls_srv_init());
    /* 17. 创建 UI 任务：Core 1，消费事件队列刷新 LVGL */
    ESP_ERROR_CHECK(start_pinned_task(ui_task,
                                      UI_TASK_NAME,
                                      UI_TASK_STACK,
                                      UI_TASK_PRIO,
                                      UI_TASK_CORE,
                                      &ui_handle));

    /* 18. 注册所有任务句柄给遥测，10 秒周期打印运行统计 */
    ESP_ERROR_CHECK(telemetry_register_task(AUDIO_CAPTURE_TASK_NAME, audio_capture_handle));
    ESP_ERROR_CHECK(telemetry_register_task(AUDIO_INFER_TASK_NAME, audio_infer_handle));
    ESP_ERROR_CHECK(telemetry_register_task(CAMERA_SERVICE_TASK_NAME, camera_service_get_task_handle()));
    ESP_ERROR_CHECK(telemetry_register_task(VISUAL_INFER_TASK_NAME, visual_cls_srv_get_task_handle()));
    ESP_ERROR_CHECK(telemetry_register_task(UI_TASK_NAME, ui_handle));
    /* 19. 启动遥测任务（最低优先级，只在空闲时跑） */
    ESP_ERROR_CHECK(telemetry_service_start());

    ESP_LOGI(TAG, "All tasks spawned. System ready.");

    /* 20. app_main 任务自我删除：启动完毕，让位给业务任务 */
    vTaskDelete(NULL);
}
