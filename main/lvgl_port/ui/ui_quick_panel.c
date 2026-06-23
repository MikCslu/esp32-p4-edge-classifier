#include "lvgl_port/ui/ui_quick_panel.h"

#include "lvgl_port/ui/ui_theme.h"
#include "service/alert_feedback_service.h"
#include "service/app_config_service.h"
#include "service/audio_playback_service.h"
#include "esp_err.h"
#include "esp_log.h"
#include <stdint.h>
#include <stdio.h>

#define PANEL_H 430
#define PAD 18

static lv_obj_t *s_overlay;
static lv_obj_t *s_panel;
static lv_obj_t *s_motor_switch;
static lv_obj_t *s_motor_slider;
static lv_obj_t *s_motor_value;
static lv_obj_t *s_volume_slider;
static lv_obj_t *s_volume_value;
static lv_obj_t *s_theme_btn;
static lv_obj_t *s_theme_label;

static const char *TAG = "QUICK_PANEL";

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, int x, int y, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, ui_theme_get()->text_primary, 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    ui_theme_apply_card(card);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    return card;
}

static lv_obj_t *make_action_btn(lv_obj_t *parent, const char *text, int x, int y, int w, int h)
{
    const ui_theme_t *t = ui_theme_get();
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_bg_color(btn, t->card_bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, t->card_border, 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(label, t->text_primary, 0);
    lv_obj_center(label);
    return btn;
}

static void sync_values(void)
{
    if (s_motor_switch) {
        if (alert_feedback_get_motor_enabled()) {
            lv_obj_add_state(s_motor_switch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(s_motor_switch, LV_STATE_CHECKED);
        }
    }
    if (s_motor_slider) {
        uint8_t strength = alert_feedback_get_motor_strength();
        lv_slider_set_value(s_motor_slider, strength, LV_ANIM_OFF);
        if (s_motor_value) lv_label_set_text_fmt(s_motor_value, "%u%%", strength);
    }
    if (s_volume_slider) {
        uint8_t volume = audio_playback_get_volume();
        lv_slider_set_value(s_volume_slider, volume, LV_ANIM_OFF);
        if (s_volume_value) lv_label_set_text_fmt(s_volume_value, "%u%%", volume);
    }
    if (s_theme_label) {
        lv_label_set_text(s_theme_label, ui_theme_is_dark() ? "Dark" : "Light");
    }
}

static void close_cb(lv_event_t *e)
{
    (void)e;
    ui_quick_panel_hide();
}

static void panel_gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_TOP) {
        ui_quick_panel_hide();
    }
}

static void motor_switch_cb(lv_event_t *e)
{
    bool enabled = lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED);
    alert_feedback_set_motor_enabled(enabled);
    ESP_ERROR_CHECK_WITHOUT_ABORT(app_config_save_motor_enabled(enabled));
}

static void motor_slider_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int value = lv_slider_get_value(s_motor_slider);
    alert_feedback_set_motor_strength((uint8_t)value);
    if (s_motor_value) lv_label_set_text_fmt(s_motor_value, "%d%%", value);
    if (code == LV_EVENT_RELEASED) {
        esp_err_t ret = app_config_save_motor_strength((uint8_t)value);
        if (ret != ESP_OK) ESP_LOGW(TAG, "Save motor strength failed: %s", esp_err_to_name(ret));
    }
}

static void volume_slider_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int value = lv_slider_get_value(s_volume_slider);
    audio_playback_set_volume((uint8_t)value);
    if (s_volume_value) lv_label_set_text_fmt(s_volume_value, "%d%%", value);
    if (code == LV_EVENT_RELEASED) {
        esp_err_t ret = app_config_save_volume((uint8_t)value);
        if (ret != ESP_OK) ESP_LOGW(TAG, "Save volume failed: %s", esp_err_to_name(ret));
    }
}

static void theme_cb(lv_event_t *e)
{
    (void)e;
    ui_theme_toggle();
    ui_quick_panel_refresh();
}

static void stop_audio_cb(lv_event_t *e)
{
    (void)e;
    audio_playback_stop();
}

static void create_panel(void)
{
    const ui_theme_t *t = ui_theme_get();
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_40, 0);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_overlay, close_cb, LV_EVENT_CLICKED, NULL);

    s_panel = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_size(s_panel, LV_HOR_RES, PANEL_H);
    lv_obj_set_pos(s_panel, 0, 0);
    lv_obj_set_style_bg_color(s_panel, t->bg, 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_panel, 0, 0);
    lv_obj_set_style_pad_all(s_panel, 0, 0);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_panel, panel_gesture_cb, LV_EVENT_GESTURE, NULL);

    make_label(s_panel, "Quick", PAD, 18, &lv_font_montserrat_28);

    lv_obj_t *motor_card = make_card(s_panel, PAD, 68, LV_HOR_RES - PAD * 2, 112);
    make_label(motor_card, "Vibration", 16, 12, &lv_font_montserrat_24);
    s_motor_switch = lv_switch_create(motor_card);
    lv_obj_set_size(s_motor_switch, 66, 34);
    lv_obj_align(s_motor_switch, LV_ALIGN_TOP_RIGHT, -16, 14);
    lv_obj_add_event_cb(s_motor_switch, motor_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_motor_value = lv_label_create(motor_card);
    lv_obj_set_style_text_font(s_motor_value, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_motor_value, t->accent, 0);
    lv_obj_align(s_motor_value, LV_ALIGN_BOTTOM_RIGHT, -16, -16);

    s_motor_slider = lv_slider_create(motor_card);
    lv_obj_set_size(s_motor_slider, LV_HOR_RES - PAD * 2 - 112, 22);
    lv_obj_align(s_motor_slider, LV_ALIGN_BOTTOM_LEFT, 16, -20);
    lv_slider_set_range(s_motor_slider, 0, 100);
    lv_obj_set_style_bg_color(s_motor_slider, t->slider_track, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_motor_slider, t->accent, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_motor_slider, t->accent, LV_PART_KNOB);
    lv_obj_add_event_cb(s_motor_slider, motor_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_motor_slider, motor_slider_cb, LV_EVENT_RELEASED, NULL);

    lv_obj_t *volume_card = make_card(s_panel, PAD, 194, LV_HOR_RES - PAD * 2, 96);
    make_label(volume_card, "Sound", 16, 12, &lv_font_montserrat_24);
    s_volume_value = lv_label_create(volume_card);
    lv_obj_set_style_text_font(s_volume_value, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_volume_value, t->accent, 0);
    lv_obj_align(s_volume_value, LV_ALIGN_TOP_RIGHT, -16, 12);

    s_volume_slider = lv_slider_create(volume_card);
    lv_obj_set_size(s_volume_slider, LV_HOR_RES - PAD * 2 - 32, 22);
    lv_obj_align(s_volume_slider, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_slider_set_range(s_volume_slider, 0, 100);
    lv_obj_set_style_bg_color(s_volume_slider, t->slider_track, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_volume_slider, t->accent, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_volume_slider, t->accent, LV_PART_KNOB);
    lv_obj_add_event_cb(s_volume_slider, volume_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_volume_slider, volume_slider_cb, LV_EVENT_RELEASED, NULL);

    s_theme_btn = make_action_btn(s_panel, "Theme", PAD, 312, (LV_HOR_RES - PAD * 3) / 2, 72);
    lv_obj_add_event_cb(s_theme_btn, theme_cb, LV_EVENT_CLICKED, NULL);
    s_theme_label = lv_label_create(s_theme_btn);
    lv_obj_set_style_text_font(s_theme_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_theme_label, t->accent, 0);
    lv_obj_align(s_theme_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    lv_obj_t *stop_btn = make_action_btn(s_panel, "Stop Audio", PAD * 2 + (LV_HOR_RES - PAD * 3) / 2,
                                         312, (LV_HOR_RES - PAD * 3) / 2, 72);
    lv_obj_add_event_cb(stop_btn, stop_audio_cb, LV_EVENT_CLICKED, NULL);

    sync_values();
}

void ui_quick_panel_show(void)
{
    if (s_overlay) {
        sync_values();
        lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_overlay);
        return;
    }
    create_panel();
}

void ui_quick_panel_hide(void)
{
    if (s_overlay) {
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

bool ui_quick_panel_is_open(void)
{
    return s_overlay && !lv_obj_has_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

void ui_quick_panel_refresh(void)
{
    if (!s_overlay) return;
    lv_obj_t *old = s_overlay;
    s_overlay = NULL;
    s_panel = NULL;
    s_motor_switch = NULL;
    s_motor_slider = NULL;
    s_motor_value = NULL;
    s_volume_slider = NULL;
    s_volume_value = NULL;
    s_theme_btn = NULL;
    s_theme_label = NULL;
    lv_obj_delete(old);
    create_panel();
}
