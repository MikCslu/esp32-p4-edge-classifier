#include <stdint.h>
#include <stddef.h>
/* Stub for missing esp_wifi_sta_get_rsnxe in P4 host WiFi libs (IDF 5.5.4) */
__attribute__((used))
uint8_t *esp_wifi_sta_get_rsnxe(uint8_t *bssid) {
    (void)bssid;
    return NULL;
}