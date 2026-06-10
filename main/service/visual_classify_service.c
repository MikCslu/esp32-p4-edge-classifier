/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file visual_cls_srv.c
 * @brief Visual classification service stub (deferred implementation)
 *
 * Reserved for future camera-based image classification. Currently
 * returns ESP_ERR_NOT_SUPPORTED on all operations.
 */

#include "service/visual_classify_service.h"
#include "esp_log.h"

static const char *TAG = "VISUAL_CLS_SRV";

esp_err_t visual_cls_srv_init(void)
{
    ESP_LOGW(TAG, "Visual classification not implemented (deferred)");
    return ESP_ERR_NOT_SUPPORTED;
}
