#include "app/audio_event_app.h"
#include "esp_log.h"

static const char *TAG = "AUDIO_EVENT_APP";

void audio_event_app_init(void)
{
    ESP_LOGI(TAG, "Audio event app initialized");
    // 事件处理已在 ui_task 中通过 event_srv_receive 完成
}
