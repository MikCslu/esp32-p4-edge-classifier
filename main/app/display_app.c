#include "app/display_app.h"
#include "hal/display_hal.h"
#include "driver/display/dsi_lcd.h"
#include "lvgl_port/ui/ui_emotion.h"
#include "esp_log.h"

static const char *TAG = "DISPLAY_APP";
#define DISPLAY_SWITCH_ANIM_MS 160

static int s_current_page = DISPLAY_PAGE_MAIN;
static lv_obj_t *s_pages[DISPLAY_PAGE_COUNT];
static bool s_init_done = false;

extern void ui_main_create(lv_obj_t *scr);
extern void ui_log_create(lv_obj_t *scr);
extern void ui_settings_create(lv_obj_t *scr);
extern void ui_main_refresh(void);
extern void ui_log_refresh(void);
extern void ui_settings_refresh(void);

void display_app_refresh_page(int page_idx)
{
    switch (page_idx) {
    case DISPLAY_PAGE_MAIN:
        ui_main_refresh();
        break;
    case DISPLAY_PAGE_LOG:
        ui_log_refresh();
        break;
    case DISPLAY_PAGE_SETTINGS:
        ui_settings_refresh();
        break;
    default:
        break;
    }
}

void display_app_refresh_current(void)
{
    display_app_refresh_page(s_current_page);
}

static void _switch_page_anim(int page_idx, lv_screen_load_anim_t anim)
{
    if (page_idx < 0 || page_idx >= DISPLAY_PAGE_COUNT) {
        return;
    }
    if (!s_pages[page_idx] || page_idx == s_current_page) {
        return;
    }

    display_app_refresh_page(page_idx);
    lv_scr_load_anim(s_pages[page_idx], anim, DISPLAY_SWITCH_ANIM_MS, 0, false);
    s_current_page = page_idx;

    ESP_LOGI(TAG, "Switched to page %d", page_idx);
}

static void _gesture_event_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT) {
        _switch_page_anim((s_current_page + 1) % DISPLAY_PAGE_COUNT,
                          LV_SCR_LOAD_ANIM_FADE_ON);
    } else if (dir == LV_DIR_RIGHT) {
        _switch_page_anim((s_current_page + DISPLAY_PAGE_COUNT - 1) % DISPLAY_PAGE_COUNT,
                          LV_SCR_LOAD_ANIM_FADE_ON);
    }
}

void display_app_init(void)
{
    if (s_init_done) {
        return;
    }

    lv_display_t *disp = display_hal_get_lv_display();
    if (!disp) {
        ESP_LOGE(TAG, "No LVGL display, abort");
        return;
    }

    if (!mipi_dsi_lcd_lock(3000)) {
        ESP_LOGE(TAG, "Failed to acquire DSI lock for init");
        return;
    }

    for (int i = 0; i < DISPLAY_PAGE_COUNT; i++) {
        s_pages[i] = lv_obj_create(NULL);
        lv_obj_set_size(s_pages[i], LV_HOR_RES, LV_VER_RES);
        lv_obj_set_style_border_width(s_pages[i], 0, 0);
        lv_obj_set_style_pad_all(s_pages[i], 0, 0);
        lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(s_pages[i], _gesture_event_cb, LV_EVENT_GESTURE, NULL);

        switch (i) {
        case DISPLAY_PAGE_MAIN:
            ui_main_create(s_pages[i]);
            break;
        case DISPLAY_PAGE_LOG:
            ui_log_create(s_pages[i]);
            break;
        case DISPLAY_PAGE_SETTINGS:
            ui_settings_create(s_pages[i]);
            break;
        case DISPLAY_PAGE_EMOTION:
            ui_emotion_create(s_pages[i]);
            break;
        default:
            break;
        }
    }

    lv_scr_load(s_pages[DISPLAY_PAGE_EMOTION]);
    s_current_page = DISPLAY_PAGE_EMOTION;

    mipi_dsi_lcd_unlock();

    s_init_done = true;
    ESP_LOGI(TAG, "Display app initialized (%d fullscreen pages, swipe navigation)",
             DISPLAY_PAGE_COUNT);
}

void display_app_switch_page(int page_idx)
{
    _switch_page_anim(page_idx, LV_SCR_LOAD_ANIM_FADE_ON);
}

int display_app_get_current_page(void)
{
    return s_current_page;
}

lv_obj_t *display_app_get_page(int page_idx)
{
    if (page_idx < 0 || page_idx >= DISPLAY_PAGE_COUNT) {
        return NULL;
    }
    return s_pages[page_idx];
}
