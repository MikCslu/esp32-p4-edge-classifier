#include "service/wifi_service.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "WIFI_SVC";
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_MAX_RETRY     3

static EventGroupHandle_t s_evt;
static wifi_connect_cb_t  s_cb;
static bool s_conn;
static int  s_retry;

static void _evt(void *a, esp_event_base_t b, int32_t id, void *d) {
    (void)a;(void)d;
    if (b==WIFI_EVENT && id==WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (b==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        bool was = s_conn; s_conn=false;
        if (s_retry<WIFI_MAX_RETRY){esp_wifi_connect();s_retry++;}
        else{xEventGroupSetBits(s_evt,WIFI_FAIL_BIT);if(s_cb&&was)s_cb(false);}
    } else if (b==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        s_retry=0;s_conn=true;xEventGroupSetBits(s_evt,WIFI_CONNECTED_BIT);
        if(s_cb)s_cb(true);
    }
}

esp_err_t wifi_service_init(void) {
    s_evt=xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,_evt,NULL,NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_GOT_IP,_evt,NULL,NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    return ESP_OK;
}

esp_err_t wifi_service_connect(const char *ssid, const char *pass) {
    wifi_config_t c={0};
    strncpy((char*)c.sta.ssid,ssid,32);
    strncpy((char*)c.sta.password,pass,63);
    c.sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&c));
    ESP_ERROR_CHECK(esp_wifi_start());
    EventBits_t b=xEventGroupWaitBits(s_evt,WIFI_CONNECTED_BIT|WIFI_FAIL_BIT,pdFALSE,pdFALSE,pdMS_TO_TICKS(15000));
    if(b&WIFI_CONNECTED_BIT){ESP_LOGI(TAG,"Connected %s",ssid);return ESP_OK;}
    return ESP_FAIL;
}

bool wifi_service_is_connected(void){return s_conn;}
void wifi_service_set_callback(wifi_connect_cb_t cb){s_cb=cb;}
