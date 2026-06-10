/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file event_srv.c
 * @brief Event service implementation using FreeRTOS queue
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

    /* Post with 0 timeout — drop if queue is full */
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

    if (xQueueReceive(s_event_queue, event, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}
