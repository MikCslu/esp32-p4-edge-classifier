/*
 * ui_theme.c — Light/Dark dual-theme system with self-updating toggle button
 */
#include "lvgl_port/ui/ui_theme.h"
#include "esp_log.h"

static const char *TAG = "THEME";

static ui_theme_id_t s_theme_id = UI_THEME_DARK;
static ui_theme_t    s_theme;
static lv_obj_t     *s_toggle_btn;
static lv_obj_t     *s_toggle_label;

/* Per-page refresh hooks */
void (*ui_main_theme_refresh)(void) = NULL;
void (*ui_log_theme_refresh)(void) = NULL;
void (*ui_settings_theme_refresh)(void) = NULL;
void (*ui_emotion_theme_refresh)(void) = NULL;

#define M(r,g,b) LV_COLOR_MAKE(r,g,b)

static const ui_theme_t s_dark_theme = {
    .bg = M(0x0c,0x17,0x28), .card_bg = M(0x10,0x18,0x21), .card_border = M(0x1e,0x30,0x48),
    .text_primary = M(0xf7,0xfa,0xfc), .text_secondary = M(0x9f,0xb2,0xc8), .text_muted = M(0x55,0x6a,0x80),
    .accent = M(0x7d,0xd3,0xfc), .accent_alt = M(0xff,0x8f,0xb3), .accent_green = M(0x8e,0xe6,0xc9),
    .accent_yellow = M(0xff,0xd1,0x66), .slider_track = M(0x1a,0x2a,0x3c), .slider_fill = M(0x7d,0xd3,0xfc),
    .bar_bg = M(0x14,0x1e,0x2c), .danger = M(0xff,0x4d,0x5a), .success = M(0x4e,0xc9,0x8a),
    .card_radius = 12, .border_width = 1,
};

static const ui_theme_t s_light_theme = {
    .bg = M(0xf0,0xf4,0xf8), .card_bg = M(0xff,0xff,0xff), .card_border = M(0xd1,0xdb,0xe6),
    .text_primary = M(0x1a,0x23,0x32), .text_secondary = M(0x5a,0x6d,0x84), .text_muted = M(0x90,0xa4,0xb8),
    .accent = M(0x29,0x73,0xb8), .accent_alt = M(0xc4,0x4a,0x6e), .accent_green = M(0x1a,0x8c,0x5a),
    .accent_yellow = M(0xd4,0x89,0x00), .slider_track = M(0xe0,0xe5,0xec), .slider_fill = M(0x29,0x73,0xb8),
    .bar_bg = M(0xe8,0xed,0xf2), .danger = M(0xd0,0x30,0x3a), .success = M(0x22,0x8c,0x58),
    .card_radius = 12, .border_width = 1,
};

/* ─── Toggle button callback ─── */
static void _toggle_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_theme_toggle();
    /* Update button appearance */
    if (s_toggle_btn) {
        lv_obj_set_style_bg_color(s_toggle_btn, s_theme.card_bg, 0);
        lv_obj_set_style_border_color(s_toggle_btn, s_theme.accent, 0);
    }
    if (s_toggle_label) {
        lv_label_set_text(s_toggle_label, ui_theme_is_dark() ? "Dark" : "Light");
        lv_obj_set_style_text_color(s_toggle_label, s_theme.text_primary, 0);
    }
}

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
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *ui_theme_create_toggle_btn(lv_obj_t *parent)
{
    s_toggle_btn = lv_btn_create(parent);
    lv_obj_remove_style_all(s_toggle_btn);
    lv_obj_set_size(s_toggle_btn, 80, 36);
    lv_obj_set_style_radius(s_toggle_btn, 8, 0);
    lv_obj_set_style_bg_color(s_toggle_btn, s_theme.card_bg, 0);
    lv_obj_set_style_bg_opa(s_toggle_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_toggle_btn, 1, 0);
    lv_obj_set_style_border_color(s_toggle_btn, s_theme.accent, 0);
    lv_obj_set_style_shadow_width(s_toggle_btn, 0, 0);
    lv_obj_set_pos(s_toggle_btn, LV_HOR_RES - 100, LV_VER_RES - 48);
    lv_obj_add_event_cb(s_toggle_btn, _toggle_btn_cb, LV_EVENT_CLICKED, NULL);

    s_toggle_label = lv_label_create(s_toggle_btn);
    lv_label_set_text(s_toggle_label, ui_theme_is_dark() ? "Dark" : "Light");
    lv_obj_set_style_text_font(s_toggle_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_toggle_label, s_theme.text_primary, 0);
    lv_obj_center(s_toggle_label);

    return s_toggle_btn;
}
