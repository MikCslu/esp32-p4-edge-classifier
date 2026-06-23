#include "service/tts_client.h"
#include "service/wifi_service.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG="TTS_CLIENT";
#define BUF_MAX (192*1024)

static char s_url[256];
static bool s_busy;
static tts_done_cb_t s_cb;
static char s_text[512];

static void _task(void *a) {
    (void)a;
    uint8_t *buf=NULL;size_t len=0;int ret=-1;
    s_busy=true;

    char body[1024];
    snprintf(body,sizeof(body),"{\"text\":\"%s\",\"voice\":\"default\",\"sample_rate\":16000}",s_text);

    for(int i=0;i<30;i++){if(wifi_service_is_connected())break;vTaskDelay(pdMS_TO_TICKS(200));}
    if(!wifi_service_is_connected()){ESP_LOGW(TAG,"WiFi down");goto done;}

    esp_http_client_config_t c={.url=s_url,.method=HTTP_METHOD_POST,.timeout_ms=8000,.buffer_size=2048};
    esp_http_client_handle_t h=esp_http_client_init(&c);
    if(!h)goto done;
    esp_http_client_set_header(h,"Content-Type","application/json");
    esp_http_client_set_post_field(h,body,strlen(body));

    esp_err_t e=esp_http_client_perform(h);
    int st=esp_http_client_get_status_code(h);
    int cl=esp_http_client_get_content_length(h);

    if(e!=ESP_OK||st!=200){ESP_LOGW(TAG,"HTTP e=%d st=%d",e,st);esp_http_client_cleanup(h);goto done;}
    size_t as=cl>0&&cl<BUF_MAX?(size_t)cl:BUF_MAX;
    buf=heap_caps_malloc(as,MALLOC_CAP_SPIRAM);
    if(!buf){esp_http_client_cleanup(h);goto done;}

    int total=0;char ck[1024];
    while(1){int r=esp_http_client_read(h,ck,sizeof(ck));if(r<=0)break;if(total+r>(int)as)break;memcpy(buf+total,ck,r);total+=r;}
    esp_http_client_cleanup(h);

    if(total>=4&&!memcmp(buf,"RIFF",4)){len=total;ret=0;ESP_LOGI(TAG,"OK %dB",total);}
    else{heap_caps_free(buf);buf=NULL;}

done:
    s_busy=false;s_cb(buf,len,ret);
    vTaskDelete(NULL);
}

void tts_client_init(const char *u){strncpy(s_url,u,sizeof(s_url)-1);s_url[sizeof(s_url)-1]=0;ESP_LOGI(TAG,"url=%s",s_url);}

void tts_client_synthesize(const char *text, const char *voice, tts_done_cb_t cb) {
    (void)voice;
    if(s_busy){if(cb)cb(NULL,0,-1);return;}
    s_cb=cb;strncpy(s_text,text?text:"",sizeof(s_text)-1);
    if(xTaskCreatePinnedToCore(_task,"tts",6144,NULL,1,NULL,0)!=pdPASS)s_cb=NULL;
}

bool tts_client_is_busy(void){return s_busy;}
