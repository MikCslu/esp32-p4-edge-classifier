#pragma once
#include "esp_err.h"
#include "lvgl.h"

/** 页面索引 */
typedef enum {
    DISPLAY_PAGE_MAIN,
    DISPLAY_PAGE_LOG,
    DISPLAY_PAGE_SETTINGS,
    DISPLAY_PAGE_EMOTION,
    DISPLAY_PAGE_COUNT,
} display_page_t;

#ifdef __cplusplus
extern "C" {
#endif

void     display_app_init(void);
void     display_app_switch_page(int page_idx);
int      display_app_get_current_page(void);
lv_obj_t *display_app_get_page(int page_idx);
void     display_app_refresh_page(int page_idx);
void     display_app_refresh_current(void);

#ifdef __cplusplus
}
#endif
