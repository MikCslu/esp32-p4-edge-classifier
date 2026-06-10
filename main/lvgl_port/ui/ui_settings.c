/*
 * ui_settings.c — Tuning page: styled dropdown, sliders, theme toggle
 */
#include "lvgl.h"
#include "service/app_state.h"
#include "service/audio_classify_service.h"
#include "lvgl_port/ui/ui_theme.h"
#include <stdio.h>

static lv_obj_t *s_scr;
static lv_obj_t *s_dropdown;
static lv_obj_t *s_threshold_slider;
static lv_obj_t *s_margin_slider;
static lv_obj_t *s_threshold_val;
static lv_obj_t *s_margin_val;
static lv_obj_t *s_hint_label;
static lv_obj_t *s_theme_btn;
static lv_obj_t *s_theme_label;
static int s_selected_class = 6;

static void _theme_refresh(void)
{
    if (!s_scr) return;
    const ui_theme_t *t = ui_theme_get();

    lv_obj_set_style_bg_color(s_scr, t->bg, 0);

    /* Theme button */
    if (s_theme_btn) {
        lv_obj_set_style_bg_color(s_theme_btn, t->card_bg, 0);
        lv_obj_set_style_border_color(s_theme_btn, t->accent, 0);
        lv_label_set_text(s_theme_label, ui_theme_is_dark() ? "☀ Light" : "🌙 Dark");
        lv_obj_set_style_text_color(s_theme_label, t->text_primary, 0);
    }

    /* Slider styling */
    if (s_threshold_slider) {
        lv_obj_set_style_bg_color(s_threshold_slider, t->slider_track, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_threshold_slider, t->accent, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_threshold_slider, t->accent, LV_PART_KNOB);
        lv_obj_set_style_border_color(s_threshold_slider, t->accent, LV_PART_KNOB);
    }
    if (s_margin_slider) {
        lv_obj_set_style_bg_color(s_margin_slider, t->slider_track, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_margin_slider, t->accent_alt, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_margin_slider, t->accent_alt, LV_PART_KNOB);
        lv_obj_set_style_border_color(s_margin_slider, t->accent_alt, LV_PART_KNOB);
    }
}

static void _refresh_controls(void)
{
    const ui_theme_t *t = ui_theme_get();
    float threshold = audio_cls_srv_get_class_threshold(s_selected_class);
    float margin = audio_cls_srv_get_class_margin(s_selected_class);
    int threshold_pct = (int)(threshold * 100.0f + 0.5f);
    int margin_pct = (int)(margin * 100.0f + 0.5f);

    if (s_threshold_slider) lv_slider_set_value(s_threshold_slider, threshold_pct, LV_ANIM_OFF);
    if (s_margin_slider) lv_slider_set_value(s_margin_slider, margin_pct, LV_ANIM_OFF);
    if (s_threshold_val) lv_label_set_text_fmt(s_threshold_val, "%d%%", threshold_pct);
    if (s_margin_val) lv_label_set_text_fmt(s_margin_val, "%d%%", margin_pct);
    if (s_hint_label) {
        lv_label_set_text_fmt(s_hint_label, "Editing: %s", app_audio_class_title(s_selected_class));
        lv_obj_set_style_text_color(s_hint_label, t->accent, 0);
    }
}

static void _dropdown_event_cb(lv_event_t *e)
{
    (void)e;
    s_selected_class = lv_dropdown_get_selected(s_dropdown);
    _refresh_controls();
}

static void _threshold_event_cb(lv_event_t *e)
{
    (void)e;
    int val = lv_slider_get_value(s_threshold_slider);
    audio_cls_srv_set_class_threshold(s_selected_class, val / 100.0f);
    _refresh_controls();
}

static void _margin_event_cb(lv_event_t *e)
{
    (void)e;
    int val = lv_slider_get_value(s_margin_slider);
    audio_cls_srv_set_class_margin(s_selected_class, val / 100.0f);
    _refresh_controls();
}

static void _theme_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_theme_toggle();
}

void ui_settings_refresh(void)
{
    _refresh_controls();
}

void ui_settings_create(lv_obj_t *scr)
{
    s_scr = scr;
    const ui_theme_t *t = ui_theme_get();

    ui_theme_apply_bg(scr);

    /* ── Title card ── */
    lv_obj_t *title_card = lv_obj_create(scr);
    lv_obj_remove_style_all(title_card);
    ui_theme_apply_card(title_card);
    lv_obj_set_size(title_card, LV_HOR_RES - 32, 96);
    lv_obj_set_pos(title_card, 16, 12);

    lv_obj_t *title = lv_label_create(title_card);
    lv_label_set_text(title, "Tuning");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(title, t->text_primary, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, -8);

    s_hint_label = lv_label_create(title_card);
    lv_label_set_text(s_hint_label, "Editing: Glass break");
    lv_obj_set_style_text_font(s_hint_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_hint_label, t->accent, 0);
    lv_obj_align(s_hint_label, LV_ALIGN_LEFT_MID, 8, 24);

    /* ── Theme toggle button (top-right) ── */
    s_theme_btn = lv_btn_create(scr);
    lv_obj_remove_style_all(s_theme_btn);
    ui_theme_apply_card_btn(s_theme_btn);
    lv_obj_set_size(s_theme_btn, 140, 48);
    lv_obj_set_pos(s_theme_btn, LV_HOR_RES - 156, 36);
    lv_obj_add_event_cb(s_theme_btn, _theme_btn_cb, LV_EVENT_CLICKED, NULL);

    s_theme_label = lv_label_create(s_theme_btn);
    lv_label_set_text(s_theme_label, ui_theme_is_dark() ? "☀ Light" : "🌙 Dark");
    lv_obj_set_style_text_font(s_theme_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_theme_label, t->text_primary, 0);
    lv_obj_center(s_theme_label);

    /* ── Dropdown card ── */
    lv_obj_t *dd_card = lv_obj_create(scr);
    lv_obj_remove_style_all(dd_card);
    ui_theme_apply_card(dd_card);
    lv_obj_set_size(dd_card, LV_HOR_RES - 32, 88);
    lv_obj_set_pos(dd_card, 16, 120);

    lv_obj_t *dd_label = lv_label_create(dd_card);
    lv_label_set_text(dd_label, "Select class to tune");
    lv_obj_set_style_text_font(dd_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(dd_label, t->text_secondary, 0);
    lv_obj_align(dd_label, LV_ALIGN_TOP_LEFT, 4, 4);

    s_dropdown = lv_dropdown_create(dd_card);
    lv_dropdown_set_options(s_dropdown,
        "Alarm\nCar horn\nKnocking\nClapping\nDog bark\n"
        "Footsteps\nGlass break\nDoorbell\nBaby crying\nEngine\nTraffic\nBackground");
    lv_dropdown_set_selected(s_dropdown, s_selected_class);
    lv_obj_set_size(s_dropdown, 500, 46);
    lv_obj_set_style_text_font(s_dropdown, &lv_font_montserrat_24, 0);
    lv_obj_set_style_bg_color(s_dropdown, t->card_bg, 0);
    lv_obj_set_style_border_color(s_dropdown, t->accent, 0);
    lv_obj_set_style_border_width(s_dropdown, 1, 0);
    lv_obj_set_style_radius(s_dropdown, 8, 0);
    lv_obj_set_style_text_color(s_dropdown, t->text_primary, 0);
    lv_obj_align(s_dropdown, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_add_event_cb(s_dropdown, _dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ── Threshold slider card ── */
    lv_obj_t *th_card = lv_obj_create(scr);
    lv_obj_remove_style_all(th_card);
    ui_theme_apply_card(th_card);
    lv_obj_set_size(th_card, LV_HOR_RES - 32, 130);
    lv_obj_set_pos(th_card, 16, 220);

    lv_obj_t *th_header = lv_obj_create(th_card);
    lv_obj_remove_style_all(th_header);
    lv_obj_set_size(th_header, LV_PCT(100), 32);
    lv_obj_set_style_pad_all(th_header, 0, 0);
    lv_obj_set_style_border_width(th_header, 0, 0);
    lv_obj_clear_flag(th_header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *th_title = lv_label_create(th_header);
    lv_label_set_text(th_title, "Confidence Threshold");
    lv_obj_set_style_text_font(th_title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(th_title, t->text_primary, 0);
    lv_obj_align(th_title, LV_ALIGN_LEFT_MID, 0, 0);

    s_threshold_val = lv_label_create(th_header);
    lv_label_set_text(s_threshold_val, "50%");
    lv_obj_set_style_text_font(s_threshold_val, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_threshold_val, t->accent, 0);
    lv_obj_align(s_threshold_val, LV_ALIGN_RIGHT_MID, 0, 0);

    s_threshold_slider = lv_slider_create(th_card);
    lv_obj_set_size(s_threshold_slider, LV_PCT(100) - 8, 28);
    lv_slider_set_range(s_threshold_slider, 40, 95);
    lv_obj_set_style_bg_color(s_threshold_slider, t->slider_track, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_threshold_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_threshold_slider, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_threshold_slider, t->accent, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_threshold_slider, 6, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_threshold_slider, t->accent, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s_threshold_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_color(s_threshold_slider, t->accent, LV_PART_KNOB);
    lv_obj_set_style_border_width(s_threshold_slider, 2, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_threshold_slider, 0, LV_PART_KNOB);
    lv_obj_align(s_threshold_slider, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(s_threshold_slider, _threshold_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ── Margin slider card ── */
    lv_obj_t *mg_card = lv_obj_create(scr);
    lv_obj_remove_style_all(mg_card);
    ui_theme_apply_card(mg_card);
    lv_obj_set_size(mg_card, LV_HOR_RES - 32, 130);
    lv_obj_set_pos(mg_card, 16, 362);

    lv_obj_t *mg_header = lv_obj_create(mg_card);
    lv_obj_remove_style_all(mg_header);
    lv_obj_set_size(mg_header, LV_PCT(100), 32);
    lv_obj_set_style_pad_all(mg_header, 0, 0);
    lv_obj_set_style_border_width(mg_header, 0, 0);
    lv_obj_clear_flag(mg_header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *mg_title = lv_label_create(mg_header);
    lv_label_set_text(mg_title, "Top-1 Margin");
    lv_obj_set_style_text_font(mg_title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(mg_title, t->text_primary, 0);
    lv_obj_align(mg_title, LV_ALIGN_LEFT_MID, 0, 0);

    s_margin_val = lv_label_create(mg_header);
    lv_label_set_text(s_margin_val, "10%");
    lv_obj_set_style_text_font(s_margin_val, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_margin_val, t->accent_alt, 0);
    lv_obj_align(s_margin_val, LV_ALIGN_RIGHT_MID, 0, 0);

    s_margin_slider = lv_slider_create(mg_card);
    lv_obj_set_size(s_margin_slider, LV_PCT(100) - 8, 28);
    lv_slider_set_range(s_margin_slider, 0, 35);
    lv_obj_set_style_bg_color(s_margin_slider, t->slider_track, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_margin_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_margin_slider, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_margin_slider, t->accent_alt, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_margin_slider, 6, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_margin_slider, t->accent_alt, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s_margin_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_color(s_margin_slider, t->accent_alt, LV_PART_KNOB);
    lv_obj_set_style_border_width(s_margin_slider, 2, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_margin_slider, 0, LV_PART_KNOB);
    lv_obj_align(s_margin_slider, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(s_margin_slider, _margin_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ── Tips card ── */
    lv_obj_t *tip_card = lv_obj_create(scr);
    lv_obj_remove_style_all(tip_card);
    ui_theme_apply_card(tip_card);
    lv_obj_set_size(tip_card, LV_HOR_RES - 32, 70);
    lv_obj_set_pos(tip_card, 16, LV_VER_RES - 86);

    lv_obj_t *tip = lv_label_create(tip_card);
    lv_label_set_text(tip, "Suggested: alarm 60/06 | clapping 72/14 | glass break 86/30");
    lv_obj_set_style_text_font(tip, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(tip, t->text_muted, 0);
    lv_obj_align(tip, LV_ALIGN_CENTER, 0, 0);

    ui_settings_theme_refresh = _theme_refresh;
    _refresh_controls();
}
