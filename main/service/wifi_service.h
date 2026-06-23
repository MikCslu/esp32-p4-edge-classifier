#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wifi_connect_cb_t)(bool connected);

esp_err_t wifi_service_init(void);
esp_err_t wifi_service_connect(const char *ssid, const char *password);
bool     wifi_service_is_connected(void);
void     wifi_service_set_callback(wifi_connect_cb_t cb);

#ifdef __cplusplus
}
#endif
