#include "app/audio_event_app.h"
#include "esp_log.h"

static const char *TAG = "AUDIO_EVENT_APP";

/* 音频事件应用初始化：实际的事件消费在 ui_task 里完成，
 * 这里保留一个初始化钩子（历史原因），可放置播放联动逻辑。 */
void audio_event_app_init(void)
{
    ESP_LOGI(TAG, "Audio event app initialized");
    // 事件处理已在 ui_task 中通过 event_srv_receive 完成
}
