#include "service/audio_playback_service.h"
#include "driver/audio/max98357_speaker.h"
#include "service/app_state.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "AUDIO_PLAY";

/*
 * 音频播放服务（FreeRTOS 队列 + 阻塞播放任务，面试重点）
 * ====================================================
 * 设计动机：I2S 写播放是"慢操作"（几 KB 数据要排队发完），
 * 不能阻塞调用方（UI 回调/告警逻辑），所以：
 *  - 任意线程 submit() 只往队列投一个请求（音量/优先级/内容），立即返回；
 *  - 播放任务阻塞在 xQueueReceive 上，按优先级取请求、逐块写 I2S。
 * 还支持：优先级抢占（紧急音打断普通音）、音量缩放、停止标志。
 * 输出状态 s_output_active 供 UI 任务判断"是不是自己在发声"，
 * 避免麦克风采集到扬声器声音又触发分类（回声/自激）。
 */

#define AP_QUEUE_DEPTH     8
#define AP_CHUNK_SAMPLES    256
#define AP_SAMPLE_RATE      16000
#define AP_OUTPUT_TAIL_MS   900

static QueueHandle_t s_queue;
static TaskHandle_t    s_task;
static bool            s_running;
static volatile bool   s_stop_flag;
static volatile uint8_t s_current_prio;
/* 是否正在输出：UI 任务用它抑制"自触发"（防止自己声音触发自己） */
static volatile bool   s_output_active;
static volatile int64_t s_output_tail_until_ms;
/* 音量 0~100（由设置页/快捷面板调节，持久化到 NVS） */
static uint8_t         s_volume = 50;
static int16_t         s_buf[AP_CHUNK_SAMPLES];

/* ─── low-level I2S helpers (reuse max98357's channel) ─── */

static esp_err_t _write_silence(size_t samples)
{
    memset(s_buf, 0, sizeof(s_buf));
    size_t remaining = samples;
    while (remaining > 0) {
        size_t n = remaining > AP_CHUNK_SAMPLES ? AP_CHUNK_SAMPLES : remaining;
        size_t written = 0;
        esp_err_t r = i2s_channel_write(max98357_get_tx_handle(),
                                        s_buf, n * 2, &written, pdMS_TO_TICKS(30));
        if (r != ESP_OK) return r;
        remaining -= written / 2;
    }
    return ESP_OK;
}

/* 把一个数据块分片写入 I2S TX（I2S 写可能只写一部分，所以要循环） */
static esp_err_t _write_block(const int16_t *data, size_t samples)
{
    size_t remaining = samples;
    size_t offset = 0;
    while (remaining > 0 && !s_stop_flag) {
        size_t n = remaining > AP_CHUNK_SAMPLES ? AP_CHUNK_SAMPLES : remaining;
        size_t written = 0;
        esp_err_t r = i2s_channel_write(max98357_get_tx_handle(),
                                        &data[offset], n * 2,
                                        &written, pdMS_TO_TICKS(100));
        if (r != ESP_OK && r != ESP_ERR_TIMEOUT) {
            if (!s_stop_flag) ESP_LOGW(TAG, "I2S write err: %d", r);
            return r;
        }
        size_t written_samples = written / 2;
        remaining -= written_samples;
        offset += written_samples;
    }
    return s_stop_flag ? ESP_ERR_INVALID_STATE : ESP_OK;
}

static void _mark_output_active(bool active)
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    s_output_active = active;
    s_output_tail_until_ms = now_ms + AP_OUTPUT_TAIL_MS;
}

static void _free_request_payload(audio_play_request_t *req)
{
    if (req && req->cmd == AUDIO_PLAY_CMD_WAV &&
        req->pcm.free_on_complete && req->pcm.data) {
        heap_caps_free((void *)req->pcm.data);
        req->pcm.data = NULL;
    }
}

static void _drop_pending_requests(void)
{
    if (!s_queue) {
        return;
    }
    audio_play_request_t dropped;
    while (xQueueReceive(s_queue, &dropped, 0) == pdTRUE) {
        _free_request_payload(&dropped);
    }
}

static esp_err_t _write_block_scaled(const int16_t *data, size_t samples, uint8_t vol)
{
    if (vol >= 100) {
        return _write_block(data, samples);
    }

    size_t remaining = samples;
    size_t offset = 0;
    while (remaining > 0 && !s_stop_flag) {
        size_t n = remaining > AP_CHUNK_SAMPLES ? AP_CHUNK_SAMPLES : remaining;
        for (size_t i = 0; i < n; i++) {
            s_buf[i] = (int16_t)(((int32_t)data[offset + i] * (int32_t)vol) / 100);
        }

        size_t written = 0;
        esp_err_t r = i2s_channel_write(max98357_get_tx_handle(),
                                        s_buf, n * 2,
                                        &written, pdMS_TO_TICKS(100));
        if (r != ESP_OK && r != ESP_ERR_TIMEOUT) {
            if (!s_stop_flag) ESP_LOGW(TAG, "I2S write err: %d", r);
            return r;
        }
        size_t written_samples = written / 2;
        remaining -= written_samples;
        offset += written_samples;
    }
    return s_stop_flag ? ESP_ERR_INVALID_STATE : ESP_OK;
}

/* ─── tone generator ─── */

/* 生成正弦提示音（无音频文件也能响，用于告警） */
static esp_err_t _play_tone(uint16_t freq, uint16_t dur_ms, uint8_t vol)
{
    if (dur_ms == 0) {
        return ESP_OK;
    }
    if (freq == 0) {
        const size_t silence_samples = ((size_t)AP_SAMPLE_RATE * dur_ms) / 1000U;
        return _write_silence(silence_samples);
    }

    const int amp = (8000 * (int)vol) / 100;
    const uint32_t total = ((uint32_t)AP_SAMPLE_RATE * dur_ms) / 1000U;
    const uint32_t half = (uint32_t)AP_SAMPLE_RATE / ((uint32_t)freq * 2U);
    uint32_t phase = 0, written = 0;

    while (written < total && !s_stop_flag) {
        uint32_t n = total - written;
        if (n > AP_CHUNK_SAMPLES) n = AP_CHUNK_SAMPLES;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t idx = written + i;
            int env = amp;
            const uint32_t fade = AP_SAMPLE_RATE / 200U;
            if (idx < fade)
                env = (amp * (int)idx) / (int)fade;
            else if (total > fade && idx > total - fade)
                env = (amp * (int)(total - idx)) / (int)fade;
            s_buf[i] = (phase < half) ? (int16_t)env : (int16_t)-env;
            phase++;
            if (half > 0 && phase >= half * 2U) phase = 0;
        }
        size_t bw = 0;
        esp_err_t r = i2s_channel_write(max98357_get_tx_handle(),
                                        s_buf, n * 2, &bw, pdMS_TO_TICKS(dur_ms + 50));
        if (r != ESP_OK) return r;
        written += (uint32_t)(bw / 2);
    }
    return s_stop_flag ? ESP_ERR_INVALID_STATE : ESP_OK;
}

/* ─── WAV parser / PCM player ─── */

typedef struct {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wav_header_t;

static bool _parse_wav(const uint8_t *data, size_t size,
                       wav_header_t *hdr, const uint8_t **pcm, size_t *pcm_len)
{
    if (size < 44) return false;
    if (memcmp(data, "RIFF", 4) || memcmp(data + 8, "WAVE", 4)) return false;
    if (memcmp(data + 12, "fmt ", 4)) return false;

    uint32_t subchunk_size = data[16] | (data[17]<<8) | (data[18]<<16) | (data[19]<<24);
    if (subchunk_size < 16) return false;

    hdr->audio_format   = data[20] | (data[21]<<8);
    hdr->num_channels   = data[22] | (data[23]<<8);
    hdr->sample_rate    = data[24] | (data[25]<<8) | (data[26]<<16) | (data[27]<<24);
    hdr->bits_per_sample = data[34] | (data[35]<<8);

    size_t cur = 20 + subchunk_size;
    while (cur + 8 <= size) {
        if (!memcmp(data + cur, "data", 4)) {
            uint32_t dlen = data[cur+4] | (data[cur+5]<<8) | (data[cur+6]<<16) | (data[cur+7]<<24);
            if (cur + 8 + dlen > size) dlen = (uint32_t)(size - cur - 8);
            *pcm     = data + cur + 8;
            *pcm_len = dlen;
            return true;
        }
        uint32_t clen = data[cur+4] | (data[cur+5]<<8) | (data[cur+6]<<16) | (data[cur+7]<<24);
        cur += 8 + clen;
    }
    return false;
}

static esp_err_t _play_wav(const uint8_t *data, size_t size, uint8_t vol)
{
    wav_header_t hdr;
    const uint8_t *pcm;
    size_t pcm_len;
    if (!_parse_wav(data, size, &hdr, &pcm, &pcm_len)) {
        /* Treat as raw 16-bit mono 16000 Hz PCM */
        pcm     = data;
        pcm_len = size;
    }
    size_t samples = pcm_len / 2;
    return _write_block_scaled((const int16_t *)pcm, samples, vol);
}

/* ─── worker task ─── */

static void _worker(void *arg)
{
    (void)arg;
    audio_play_request_t req;

    while (1) {
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) != pdTRUE) continue;

        if (req.cmd == AUDIO_PLAY_CMD_VOLUME) {
            s_volume = req.volume;
            if (s_volume > 100) s_volume = 100;
            ESP_LOGI(TAG, "Volume → %u%%", s_volume);
            continue;
        }

        if (req.cmd == AUDIO_PLAY_CMD_STOP) {
            s_stop_flag = true;
            continue;
        }

        if (!max98357_speaker_is_available()) {
            ESP_LOGW(TAG, "Speaker not available, dropping cmd %d", req.cmd);
            continue;
        }

        uint8_t vol = req.volume ? req.volume : s_volume;
        s_stop_flag = false;
        s_current_prio = (uint8_t)req.priority;
        _mark_output_active(true);

        switch (req.cmd) {
        case AUDIO_PLAY_CMD_TONE:
            _play_tone(req.tone.freq_hz, req.tone.duration_ms, vol);
            break;
        case AUDIO_PLAY_CMD_WAV:
            _play_wav(req.pcm.data, req.pcm.size, vol);
            if (req.pcm.free_on_complete && req.pcm.data) {
                heap_caps_free((void *)req.pcm.data);
            }
            break;
        default:
            break;
        }

        s_current_prio = 0;
        _mark_output_active(false);

        _write_silence(AP_CHUNK_SAMPLES); /* flush tail */
    }
}

/* ─── public API ─── */

esp_err_t audio_playback_service_start(void)
{
    if (s_running) return ESP_OK;

    esp_err_t ret = max98357_speaker_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "max98357 init failed: %d", ret);
        return ret;
    }

    s_queue = xQueueCreate(AP_QUEUE_DEPTH, sizeof(audio_play_request_t));
    if (!s_queue) return ESP_ERR_NO_MEM;

    BaseType_t ok = xTaskCreatePinnedToCore(
        _worker, "audio_play", 4096, NULL, 7, &s_task, 0);
    if (ok != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_running = true;
    ESP_LOGI(TAG, "Audio playback service ready");
    return ESP_OK;
}

esp_err_t audio_playback_submit(const audio_play_request_t *req)
{
    if (!s_running || !s_queue) return ESP_ERR_INVALID_STATE;
    if (!req) return ESP_ERR_INVALID_ARG;

    /* Stop overrides any pending commands */
    if (req->cmd == AUDIO_PLAY_CMD_STOP) {
        s_stop_flag = true;
        _drop_pending_requests();
        _mark_output_active(false);
        return ESP_OK;
    }

    /* Preempt lower-priority playback */
    if (req->priority > s_current_prio) {
        s_stop_flag = true;
    }

    if (req->cmd == AUDIO_PLAY_CMD_WAV || req->cmd == AUDIO_PLAY_CMD_TONE) {
        _mark_output_active(true);
    }

    /* Higher priority: drop oldest in queue and prepend */
    if (xQueueSendToBack(s_queue, req, 0) != pdTRUE) {
        audio_play_request_t front;
        if (xQueuePeek(s_queue, &front, 0) == pdTRUE &&
            req->priority > front.priority) {
            xQueueReceive(s_queue, &front, 0); /* drop front */
            xQueueSendToBack(s_queue, req, 0);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Queue full, dropped request (cmd=%d pri=%d)",
                 req->cmd, req->priority);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t audio_playback_replace(const audio_play_request_t *req)
{
    if (!s_running || !s_queue) return ESP_ERR_INVALID_STATE;
    if (!req) return ESP_ERR_INVALID_ARG;

    s_stop_flag = true;
    _drop_pending_requests();
    if (req->cmd == AUDIO_PLAY_CMD_WAV || req->cmd == AUDIO_PLAY_CMD_TONE) {
        _mark_output_active(true);
    }

    if (xQueueSendToFront(s_queue, req, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to replace playback request (cmd=%d pri=%d)",
                 req->cmd, req->priority);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void audio_playback_stop(void)
{
    s_stop_flag = true;
    _drop_pending_requests();
    _mark_output_active(false);
}

void audio_playback_set_volume(uint8_t vol)
{
    s_volume = vol > 100 ? 100 : vol;
}

uint8_t audio_playback_get_volume(void)
{
    return s_volume;
}

bool audio_playback_is_output_active(void)
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    return s_output_active || now_ms < s_output_tail_until_ms;
}
