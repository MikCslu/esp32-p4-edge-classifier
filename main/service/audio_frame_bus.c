#include "service/audio_frame_bus.h"

#include "runtime/task_config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "AUDIO_FRAME_BUS";

/*
 * 音频帧总线（FreeRTOS 同步原语综合应用，面试重点）
 * ====================================================
 * 角色：音频采集任务(生产者) -> 本总线 -> 音频推理任务(消费者)
 *
 * 为什么不用 FreeRTOS 队列装音频数据？
 *  - 音频是"流"不是"消息"：推理需要最近 1 秒(16000 点)的连续窗口，
 *    队列按元素拷贝既慢又难拼窗口，所以这里用共享环形缓冲；
 *  - 同步分两层：
 *      a) 互斥锁 s_lock        保护环形缓冲的读写（数据一致性）；
 *      b) 二值信号量 s_frame_signal 做"帧到达"通知（唤醒消费者）。
 *
 * 滑动窗口原理：生产者每写入 1600 点(100ms)，消费者读末尾 16000 点，
 * 即"每次推理都基于最新的 1 秒音频"。
 */

/* 互斥锁：保护环形缓冲（写者/读者不能同时操作） */
static SemaphoreHandle_t s_lock;
/* 二值信号量：一帧写好后 give，消费者 take 到即被唤醒。
 * give 失败(信号量已被占)说明消费者还没消费完上一帧 -> coalesced++。 */
static SemaphoreHandle_t s_frame_signal;
/* 环形缓冲本体：AUDIO_WINDOW_SAMPLES(16000) 个 int16，放 PSRAM */
static int16_t *s_ring;
static size_t s_write_pos;         /* 写位置（环形游标） */
static size_t s_valid_samples;     /* 已写入的有效样本数（启动阶段不满 1 秒时用） */
static uint32_t s_signal_coalesced_count;
static uint32_t s_post_failed_count;

static TickType_t timeout_to_ticks(uint32_t timeout_ms)
{
    return timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
}

esp_err_t audio_frame_bus_init(void)
{
    if (s_ring) {
        return ESP_OK;
    }

    /* xSemaphoreCreateMutex：创建互斥锁（带优先级继承，防优先级反转） */
    s_lock = xSemaphoreCreateMutex();
    /* xSemaphoreCreateBinary：创建二值信号量（初始为 0 = 无帧可读） */
    s_frame_signal = xSemaphoreCreateBinary();
    if (!s_lock || !s_frame_signal) {
        ESP_LOGE(TAG, "Failed to create audio bus sync primitives");
        return ESP_ERR_NO_MEM;
    }

    size_t ring_size = AUDIO_WINDOW_SAMPLES * sizeof(int16_t);
    s_ring = (int16_t *)heap_caps_malloc(ring_size,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_ring) {
        s_ring = (int16_t *)heap_caps_malloc(ring_size,
                                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!s_ring) {
        ESP_LOGE(TAG, "Failed to allocate audio window ring (%u bytes)",
                 (unsigned)ring_size);
        return ESP_ERR_NO_MEM;
    }

    memset(s_ring, 0, ring_size);
    ESP_LOGI(TAG, "Audio window bus initialized (window=%d samples, frame=%d samples)",
             AUDIO_WINDOW_SAMPLES,
             AUDIO_FRAME_SAMPLES);
    return ESP_OK;
}

esp_err_t audio_frame_bus_post(const int16_t *samples, uint32_t timeout_ms)
{
    if (!samples) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ring || !s_lock || !s_frame_signal) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 写者加锁；拿不到锁说明读者正在读，超时则本次投递失败 */
    if (xSemaphoreTake(s_lock, timeout_to_ticks(timeout_ms)) != pdTRUE) {
        s_post_failed_count++;
        return ESP_ERR_TIMEOUT;
    }

    /* 环形写入：先写尾部剩余空间，写满则回绕到头部（所以叫"环形"） */
    size_t first = AUDIO_WINDOW_SAMPLES - s_write_pos;
    if (first > AUDIO_FRAME_SAMPLES) {
        first = AUDIO_FRAME_SAMPLES;
    }
    memcpy(&s_ring[s_write_pos], samples, first * sizeof(int16_t));
    if (first < AUDIO_FRAME_SAMPLES) {
        memcpy(s_ring, samples + first,
               (AUDIO_FRAME_SAMPLES - first) * sizeof(int16_t));
    }
    s_write_pos = (s_write_pos + AUDIO_FRAME_SAMPLES) % AUDIO_WINDOW_SAMPLES;
    if (s_valid_samples < AUDIO_WINDOW_SAMPLES) {
        s_valid_samples += AUDIO_FRAME_SAMPLES;
        if (s_valid_samples > AUDIO_WINDOW_SAMPLES) {
            s_valid_samples = AUDIO_WINDOW_SAMPLES;
        }
    }

    xSemaphoreGive(s_lock);   /* 写完解锁 */

    /* 通知消费者：give 成功 = 有新帧；give 失败 = 信号量还没被 take，
     * 说明消费者仍处理上一帧，本次通知合并(coalesced)，无需阻塞。 */

    if (xSemaphoreGive(s_frame_signal) != pdTRUE) {
        s_signal_coalesced_count++;
    }
    return ESP_OK;
}

esp_err_t audio_frame_bus_read_window(int16_t *samples,
                                      size_t sample_count,
                                      uint32_t timeout_ms)
{
    if (!samples || sample_count == 0 || sample_count > AUDIO_WINDOW_SAMPLES) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ring || !s_lock || !s_frame_signal) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 消费者先等"帧到达"信号（阻塞点，省 CPU），
     * 而不是死等锁——信号量保证只有新数据来了才去抢锁。 */
    if (xSemaphoreTake(s_frame_signal, timeout_to_ticks(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* 拿到信号后再短暂抢锁（5ms），读取期间写者最多等 5ms */
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* 启动初期有效样本不足一个窗口：返回 INVALID_SIZE，调用方稍后重试 */
    if (s_valid_samples < sample_count) {
        xSemaphoreGive(s_lock);   /* 数据不足：先解锁再返回，让写者继续 */
        return ESP_ERR_INVALID_SIZE;
    }

    /* 从"写位置往前数 window 长度"处开始读 = 读到的总是最新的窗口 */
    size_t start = (s_write_pos + AUDIO_WINDOW_SAMPLES - sample_count) % AUDIO_WINDOW_SAMPLES;
    size_t first = AUDIO_WINDOW_SAMPLES - start;
    if (first > sample_count) {
        first = sample_count;
    }
    memcpy(samples, &s_ring[start], first * sizeof(int16_t));
    if (first < sample_count) {
        memcpy(samples + first, s_ring, (sample_count - first) * sizeof(int16_t));
    }

    xSemaphoreGive(s_lock);   /* 读完解锁：写者可以继续投递新帧 */
    return ESP_OK;
}

esp_err_t audio_frame_bus_receive(int16_t *samples, uint32_t timeout_ms)
{
    return audio_frame_bus_read_window(samples, AUDIO_FRAME_SAMPLES, timeout_ms);
}

void audio_frame_bus_get_stats(audio_frame_bus_stats_t *stats)
{
    if (!stats) {
        return;
    }

    stats->queued = s_frame_signal ? (uint32_t)uxSemaphoreGetCount(s_frame_signal) : 0;
    stats->overwritten = s_signal_coalesced_count;
    stats->post_failed = s_post_failed_count;
}
