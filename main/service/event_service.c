/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file event_srv.c
 * @brief Event service implementation using FreeRTOS queue
 *
 * 面试要点：
 *  - 生产-消费者模型：音频推理/视觉推理任务 post，UI 任务 receive；
 *  - FreeRTOS 队列是线程安全的（内部用临界区/锁保护），
 *    队列元素按值拷贝（这里拷的是 event_t 结构体）；
 *  - 队列满时 post 选择"直接丢弃"而不是阻塞，保证生产者不被拖慢。
 *
 * Provides thread-safe event posting and receiving for inter-task
 * communication (e.g., audio classification → UI).
 */

#include "service/event_service.h"
#include "runtime/task_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "EVENT_SRV";
/* FreeRTOS 队列句柄（全局唯一，init 时创建） */
static QueueHandle_t s_event_queue = NULL;

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

esp_err_t event_srv_init(void)
{
    if (s_event_queue) {
        ESP_LOGW(TAG, "Event service already initialized");
        return ESP_OK;
    }

    /* xQueueCreate(队列长度, 单个元素大小)：
     * 这里每个元素是一个 event_t（音频/视觉结果+时间戳），深度 10。 */
    s_event_queue = xQueueCreate(APP_EVENT_QUEUE_DEPTH, sizeof(event_t));
    if (!s_event_queue) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Event service initialized (queue depth=%d)", APP_EVENT_QUEUE_DEPTH);
    return ESP_OK;
}

esp_err_t event_srv_post(const event_t *event)
{
    if (!s_event_queue) {
        ESP_LOGE(TAG, "Event service not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!event) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 0 超时 = 非阻塞发送；队列满则立即返回失败并计数，宁可丢事件也不阻塞调用方 */
    /* xQueueSend 把事件按值拷入队列，pdTRUE=入队成功 */
    if (xQueueSend(s_event_queue, event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Event queue full, dropping event type=%d",
                 (int)event->type);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t event_srv_receive(event_t *event, uint32_t timeout_ms)
{
    if (!s_event_queue) {
        ESP_LOGE(TAG, "Event service not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!event) {
        return ESP_ERR_INVALID_ARG;
    }

    /* xQueueReceive 阻塞等待：pdMS_TO_TICKS 把毫秒转成 tick。
     * 注意：这是"阻塞点"——UI 任务平时就睡在这里，不消耗 CPU。 */
    if (xQueueReceive(s_event_queue, event, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}
