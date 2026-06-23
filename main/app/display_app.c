#include "app/display_app.h"
#include "hal/display_hal.h"
#include "driver/display/dsi_lcd.h"
#include "lvgl_port/ui/ui_emotion.h"
#include "lvgl_port/ui/ui_theme.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DISPLAY_APP";
#define DISPLAY_SWITCH_ANIM_MS 0

static int s_current_page = DISPLAY_PAGE_MAIN;
static lv_obj_t *s_pages[DISPLAY_PAGE_COUNT];
static bool s_init_done = false;

extern void ui_main_create(lv_obj_t *scr);
extern void ui_log_create(lv_obj_t *scr);
extern void ui_settings_create(lv_obj_t *scr);
extern void ui_voice_create(lv_obj_t *scr);
extern void ui_main_refresh(void);
extern void ui_log_refresh(void);
extern void ui_settings_refresh(void);
extern void ui_voice_refresh(void);

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
    case DISPLAY_PAGE_VOICE:
        ui_voice_refresh();
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

    /* Manage camera preview: only active on emotion page */
    if (s_current_page == DISPLAY_PAGE_EMOTION) ui_emotion_pause_preview();
    if (page_idx == DISPLAY_PAGE_EMOTION) ui_emotion_resume_preview();

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
                          LV_SCR_LOAD_ANIM_NONE);
    } else if (dir == LV_DIR_RIGHT) {
        _switch_page_anim((s_current_page + DISPLAY_PAGE_COUNT - 1) % DISPLAY_PAGE_COUNT,
                          LV_SCR_LOAD_ANIM_NONE);
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

    bool locked = false;
    for (int i = 0; i < 20; i++) {
        if (mipi_dsi_lcd_lock(1000)) {
            locked = true;
            break;
        }
        ESP_LOGW(TAG, "Waiting for LVGL lock before UI init (%d/20)", i + 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!locked) {
        ESP_LOGE(TAG, "Failed to acquire LVGL lock for UI init");
        return;
    }

    ESP_LOGI(TAG, "Creating UI pages");
    ui_theme_init();

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
        case DISPLAY_PAGE_VOICE:
            ui_voice_create(s_pages[i]);
            break;
        default:
            break;
        }
    }

    lv_scr_load(s_pages[DISPLAY_PAGE_EMOTION]);
    lv_obj_invalidate(s_pages[DISPLAY_PAGE_EMOTION]);
    s_current_page = DISPLAY_PAGE_EMOTION;

    esp_err_t start_ret = mipi_dsi_lcd_start();
    if (start_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start LVGL rendering: %d", start_ret);
        mipi_dsi_lcd_unlock();
        return;
    }

    mipi_dsi_lcd_unlock();

    s_init_done = true;
    ESP_LOGI(TAG, "Display app initialized (%d fullscreen pages, swipe navigation)",
             DISPLAY_PAGE_COUNT);
}

bool display_app_is_ready(void)
{
    return s_init_done;
}

void display_app_switch_page(int page_idx)
{
    _switch_page_anim(page_idx, LV_SCR_LOAD_ANIM_NONE);
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
