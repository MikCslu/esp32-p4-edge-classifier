/*
 * ui_theme.c — Light/Dark dual-theme system
 */
#include "lvgl_port/ui/ui_theme.h"
#include "esp_log.h"

static const char *TAG = "THEME";

static ui_theme_id_t s_theme_id = UI_THEME_DARK;
static ui_theme_t    s_theme;

/* Per-page refresh hooks */
void (*ui_main_theme_refresh)(void) = NULL;
void (*ui_log_theme_refresh)(void) = NULL;
void (*ui_settings_theme_refresh)(void) = NULL;
void (*ui_emotion_theme_refresh)(void) = NULL;

#define MAKE(r,g,b) LV_COLOR_MAKE(r,g,b)

static const ui_theme_t s_dark_theme = {
    .bg           = MAKE(0x0c,0x17,0x28),
    .card_bg      = MAKE(0x10,0x18,0x21),
    .card_border  = MAKE(0x1e,0x30,0x48),
    .text_primary = MAKE(0xf7,0xfa,0xfc),
    .text_secondary = MAKE(0x9f,0xb2,0xc8),
    .text_muted   = MAKE(0x55,0x6a,0x80),
    .accent       = MAKE(0x7d,0xd3,0xfc),
    .accent_alt   = MAKE(0xff,0x8f,0xb3),
    .accent_green = MAKE(0x8e,0xe6,0xc9),
    .accent_yellow = MAKE(0xff,0xd1,0x66),
    .slider_track = MAKE(0x1a,0x2a,0x3c),
    .slider_fill  = MAKE(0x7d,0xd3,0xfc),
    .bar_bg       = MAKE(0x14,0x1e,0x2c),
    .danger       = MAKE(0xff,0x4d,0x5a),
    .success      = MAKE(0x4e,0xc9,0x8a),
    .card_radius  = 12,
    .border_width = 1,
};

static const ui_theme_t s_light_theme = {
    .bg           = MAKE(0xf0,0xf4,0xf8),
    .card_bg      = MAKE(0xff,0xff,0xff),
    .card_border  = MAKE(0xd1,0xdb,0xe6),
    .text_primary = MAKE(0x1a,0x23,0x32),
    .text_secondary = MAKE(0x5a,0x6d,0x84),
    .text_muted   = MAKE(0x90,0xa4,0xb8),
    .accent       = MAKE(0x29,0x73,0xb8),
    .accent_alt   = MAKE(0xc4,0x4a,0x6e),
    .accent_green = MAKE(0x1a,0x8c,0x5a),
    .accent_yellow = MAKE(0xd4,0x89,0x00),
    .slider_track = MAKE(0xe0,0xe5,0xec),
    .slider_fill  = MAKE(0x29,0x73,0xb8),
    .bar_bg       = MAKE(0xe8,0xed,0xf2),
    .danger       = MAKE(0xd0,0x30,0x3a),
    .success      = MAKE(0x22,0x8c,0x58),
    .card_radius  = 12,
    .border_width = 1,
};

/* ─── Public ─── */

void ui_theme_init(void)
{
    s_theme_id = UI_THEME_DARK;
    s_theme = s_dark_theme;
    ESP_LOGI(TAG, "Theme: dark");
}

void ui_theme_toggle(void)
{
    if (s_theme_id == UI_THEME_DARK) {
        s_theme_id = UI_THEME_LIGHT;
        s_theme = s_light_theme;
    } else {
        s_theme_id = UI_THEME_DARK;
        s_theme = s_dark_theme;
    }
    ESP_LOGI(TAG, "Theme: %s", ui_theme_is_dark() ? "dark" : "light");

    /* Notify pages to refresh */
    if (ui_main_theme_refresh)      ui_main_theme_refresh();
    if (ui_log_theme_refresh)       ui_log_theme_refresh();
    if (ui_settings_theme_refresh)  ui_settings_theme_refresh();
    if (ui_emotion_theme_refresh)   ui_emotion_theme_refresh();
}

ui_theme_id_t ui_theme_get_id(void)    { return s_theme_id; }
const ui_theme_t *ui_theme_get(void)   { return &s_theme; }

void ui_theme_apply_bg(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_set_style_bg_color(obj, s_theme.bg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

void ui_theme_apply_card(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_set_style_bg_color(obj, s_theme.card_bg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, s_theme.border_width, 0);
    lv_obj_set_style_border_color(obj, s_theme.card_border, 0);
    lv_obj_set_style_radius(obj, s_theme.card_radius, 0);
    lv_obj_set_style_pad_all(obj, 16, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void ui_theme_apply_card_btn(lv_obj_t *obj)
{
    ui_theme_apply_card(obj);
    lv_obj_set_style_bg_color(obj, s_theme.card_bg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}
