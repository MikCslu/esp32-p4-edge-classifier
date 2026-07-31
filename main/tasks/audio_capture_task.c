#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/audio_hal.h"
#include "service/audio_frame_bus.h"
#include "runtime/task_config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <stdint.h>

/*
 * 音频采集任务（FreeRTOS 生产者角色）
 * ====================================================
 * 数据流：ES8311 麦克风(I2S) -> 本任务 -> audio_frame_bus(环形缓冲) -> 推理任务
 *
 * 为什么优先级最高(10)且阻塞读？
 *  - I2S DMA 收到的音频数据必须及时取走，否则新数据会覆盖旧数据；
 *  - 阻塞在 audio HAL read 上时任务不占 CPU，等数据到了才被唤醒；
 *  - 高优先级保证它总能抢在推理/UI 之前执行，是"实时链路"的第一环。
 */

static const char *TAG = "AUDIO_CAPTURE";
/* 统计用：总线队列满导致丢帧的计数（用于排查性能瓶颈） */
static uint32_t s_drop_count = 0;
static uint32_t s_read_count = 0;

static int16_t *alloc_audio_frame_buffer(void)
{
    size_t size = AUDIO_FRAME_SAMPLES * sizeof(int16_t);
    /* heap_caps_malloc 可指定内存类型：
     * MALLOC_CAP_INTERNAL = 片内 SRAM（快但少，优先）；
     * 失败再退回 PSRAM（大但慢，且需 8 位可访问）。 */
    int16_t *buffer = (int16_t *)heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (buffer) {
        ESP_LOGI(TAG, "Audio capture buffer allocated in internal RAM (%u bytes)", (unsigned)size);
        return buffer;
    }

    ESP_LOGW(TAG, "Internal RAM audio capture buffer unavailable, using PSRAM");
    return (int16_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void audio_capture_task(void *pvParameters)
{
    esp_err_t ret;

    /* 初始化音频 HAL（ES8311 编解码器）：16kHz、16bit、单声道、输入方向 */
    // 初始化音频 HAL
    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_HZ,
        .bits_per_sample = 16,
        .channels = 1,
        .dir = AUDIO_DIR_INPUT,
    };

    /* 单例模式：所有 HAL 都通过 xxx_hal_get_instance() 获取 */
    ret = audio_hal_get_instance()->init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio HAL init failed");
        vTaskDelete(NULL);
        return;
    }

    /* 分配一帧(1600 个 int16 = 100ms)的采集缓冲，尽量放片内 RAM 减少延迟 */
    // 创建队列
    int16_t *buffer = alloc_audio_frame_buffer();
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Audio capture task started");

    /* 生产循环：阻塞读 -> 统计 -> 投递，永不退出 */
    while (1) {
        /* 阻塞读一帧(100ms)：portMAX_DELAY = 无限等待。
         * I2S 驱动内部有 DMA，这里等的是 DMA 缓冲满一个 frame。 */
        ret = audio_hal_get_instance()->read(buffer, AUDIO_FRAME_SAMPLES, portMAX_DELAY);
        if (ret == ESP_OK) {
            s_read_count++;
            /* 每 50 帧(5 秒)打印一次 PCM 波形统计，便于调试麦克风是否正常 */
            if ((s_read_count % 50) == 0) {
                int16_t min_sample = INT16_MAX;
                int16_t max_sample = INT16_MIN;
                uint64_t abs_sum = 0;
                uint64_t square_sum = 0;
                uint32_t near_zero = 0;

                for (int i = 0; i < AUDIO_FRAME_SAMPLES; i++) {
                    int32_t sample = buffer[i];
                    int32_t abs_sample = sample < 0 ? -sample : sample;
                    if (sample < min_sample) min_sample = (int16_t)sample;
                    if (sample > max_sample) max_sample = (int16_t)sample;
                    abs_sum += (uint32_t)abs_sample;
                    square_sum += (uint64_t)((int64_t)sample * (int64_t)sample);
                    if (abs_sample <= 8) near_zero++;
                }

                float mean_abs = (float)abs_sum / (float)AUDIO_FRAME_SAMPLES;
                float rms = sqrtf((float)((double)square_sum / (double)AUDIO_FRAME_SAMPLES));
                ESP_LOGI(TAG, "PCM stats: min=%d max=%d mean_abs=%.1f rms=%.1f near_zero=%lu/%d",
                         (int)min_sample,
                         (int)max_sample,
                         (double)mean_abs,
                         (double)rms,
                         (unsigned long)near_zero,
                         AUDIO_FRAME_SAMPLES);
            }

            /* 投递到帧总线；超时 10ms，满则丢帧计数（宁可丢也不能阻塞采集） */
            if (audio_frame_bus_post(buffer, 10) != ESP_OK) {
                s_drop_count++;
                if ((s_drop_count % 50) == 1) {
                    ESP_LOGW(TAG, "Audio queue full, dropped %lu frames",
                             (unsigned long)s_drop_count);
                }
            }
        }
        /* 循环节奏由上面的阻塞 read 控制：大约每 100ms 转一圈 */
    }
}
