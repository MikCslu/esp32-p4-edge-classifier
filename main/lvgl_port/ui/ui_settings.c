/*
 * ui_settings.c — Tuning page with full theme refresh
 */
#include "lvgl.h"
#include "service/app_state.h"
#include "service/audio_classify_service.h"
#include "lvgl_port/ui/ui_theme.h"
#include <stdio.h>

static lv_obj_t *s_scr;
static lv_obj_t *s_title_card, *s_dd_card, *s_th_card, *s_mg_card, *s_tip_card;
static lv_obj_t *s_title, *s_hint_label, *s_dd_label;
static lv_obj_t *s_dropdown;
static lv_obj_t *s_threshold_slider, *s_margin_slider;
static lv_obj_t *s_th_title, *s_th_val, *s_mg_title, *s_mg_val;
static lv_obj_t *s_tip;
static int s_selected_class = 6;

static void _refresh_controls(void)
{
    float threshold = audio_cls_srv_get_class_threshold(s_selected_class);
    float margin = audio_cls_srv_get_class_margin(s_selected_class);
    int tp = (int)(threshold * 100.0f + 0.5f);
    int mp = (int)(margin * 100.0f + 0.5f);
    if (s_threshold_slider) lv_slider_set_value(s_threshold_slider, tp, LV_ANIM_OFF);
    if (s_margin_slider)    lv_slider_set_value(s_margin_slider, mp, LV_ANIM_OFF);
    if (s_th_val) lv_label_set_text_fmt(s_th_val, "%d%%", tp);
    if (s_mg_val) lv_label_set_text_fmt(s_mg_val, "%d%%", mp);
    if (s_hint_label) {
        lv_label_set_text_fmt(s_hint_label, "Editing: %s", app_audio_class_title(s_selected_class));
        lv_obj_set_style_text_color(s_hint_label, ui_theme_get()->accent, 0);
    }
}

static void _dd_cb(lv_event_t *e) { (void)e; s_selected_class = lv_dropdown_get_selected(s_dropdown); _refresh_controls(); }
static void _th_cb(lv_event_t *e) { (void)e; int v = lv_slider_get_value(s_threshold_slider); audio_cls_srv_set_class_threshold(s_selected_class, v / 100.0f); _refresh_controls(); }
static void _mg_cb(lv_event_t *e) { (void)e; int v = lv_slider_get_value(s_margin_slider); audio_cls_srv_set_class_margin(s_selected_class, v / 100.0f); _refresh_controls(); }

static void _theme_refresh(void)
{
    if (!s_scr) return;
    const ui_theme_t *t = ui_theme_get();
    lv_obj_set_style_bg_color(s_scr, t->bg, 0);
    if (s_title_card) { lv_obj_set_style_bg_color(s_title_card, t->card_bg, 0); lv_obj_set_style_border_color(s_title_card, t->card_border, 0); }
    if (s_dd_card)    { lv_obj_set_style_bg_color(s_dd_card, t->card_bg, 0); lv_obj_set_style_border_color(s_dd_card, t->card_border, 0); }
    if (s_th_card)    { lv_obj_set_style_bg_color(s_th_card, t->card_bg, 0); lv_obj_set_style_border_color(s_th_card, t->card_border, 0); }
    if (s_mg_card)    { lv_obj_set_style_bg_color(s_mg_card, t->card_bg, 0); lv_obj_set_style_border_color(s_mg_card, t->card_border, 0); }
    if (s_tip_card)   { lv_obj_set_style_bg_color(s_tip_card, t->card_bg, 0); lv_obj_set_style_border_color(s_tip_card, t->card_border, 0); }
    if (s_title) lv_obj_set_style_text_color(s_title, t->text_primary, 0);
    if (s_dd_label) lv_obj_set_style_text_color(s_dd_label, t->text_secondary, 0);
    if (s_th_title) lv_obj_set_style_text_color(s_th_title, t->text_primary, 0);
    if (s_mg_title) lv_obj_set_style_text_color(s_mg_title, t->text_primary, 0);
    if (s_th_val) lv_obj_set_style_text_color(s_th_val, t->accent, 0);
    if (s_mg_val) lv_obj_set_style_text_color(s_mg_val, t->accent_alt, 0);
    if (s_tip) lv_obj_set_style_text_color(s_tip, t->text_muted, 0);
    if (s_dropdown) {
        lv_obj_set_style_bg_color(s_dropdown, t->card_bg, 0);
        lv_obj_set_style_border_color(s_dropdown, t->accent, 0);
        lv_obj_set_style_text_color(s_dropdown, t->text_primary, 0);
    }
    /* Sliders */
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

void ui_settings_refresh(void) { _refresh_controls(); }

void ui_settings_create(lv_obj_t *scr)
{
    s_scr = scr;
    const ui_theme_t *t = ui_theme_get();

    ui_theme_apply_bg(scr);

    /* Title card */
    s_title_card = lv_obj_create(scr);
    lv_obj_remove_style_all(s_title_card);
    ui_theme_apply_card(s_title_card);
    lv_obj_set_pos(s_title_card, 12, 8);
    lv_obj_set_size(s_title_card, LV_HOR_RES - 98, 78);

    s_title = lv_label_create(s_title_card);
    lv_label_set_text(s_title, "Tuning");
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_title, t->text_primary, 0);
    lv_obj_align(s_title, LV_ALIGN_LEFT_MID, 12, -6);

    s_hint_label = lv_label_create(s_title_card);
    lv_label_set_text(s_hint_label, "Editing: Glass break");
    lv_obj_set_style_text_font(s_hint_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_hint_label, t->accent, 0);
    lv_obj_align(s_hint_label, LV_ALIGN_LEFT_MID, 12, 22);

    /* Dropdown card */
    s_dd_card = lv_obj_create(scr);
    lv_obj_remove_style_all(s_dd_card);
    ui_theme_apply_card(s_dd_card);
    lv_obj_set_pos(s_dd_card, 12, 96);
    lv_obj_set_size(s_dd_card, LV_HOR_RES - 24, 78);

    s_dd_label = lv_label_create(s_dd_card);
    lv_label_set_text(s_dd_label, "Select class to tune");
    lv_obj_set_style_text_font(s_dd_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_dd_label, t->text_secondary, 0);
    lv_obj_set_pos(s_dd_label, 8, 4);

    s_dropdown = lv_dropdown_create(s_dd_card);
    lv_dropdown_set_options(s_dropdown,
        "Alarm\nCar horn\nKnocking\nClapping\nDog bark\nFootsteps\n"
        "Glass break\nDoorbell\nBaby crying\nEngine\nTraffic\nBackground");
    lv_dropdown_set_selected(s_dropdown, s_selected_class);
    lv_obj_set_size(s_dropdown, 540, 44);
    lv_obj_set_style_text_font(s_dropdown, &lv_font_montserrat_24, 0);
    lv_obj_set_style_bg_color(s_dropdown, t->card_bg, 0);
    lv_obj_set_style_border_color(s_dropdown, t->accent, 0);
    lv_obj_set_style_border_width(s_dropdown, 1, 0);
    lv_obj_set_style_radius(s_dropdown, 8, 0);
    lv_obj_set_style_text_color(s_dropdown, t->text_primary, 0);
    lv_obj_set_pos(s_dropdown, 8, 30);
    lv_obj_add_event_cb(s_dropdown, _dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Threshold slider card */
    s_th_card = lv_obj_create(scr);
    lv_obj_remove_style_all(s_th_card);
    ui_theme_apply_card(s_th_card);
    lv_obj_set_pos(s_th_card, 12, 186);
    lv_obj_set_size(s_th_card, LV_HOR_RES - 24, 110);

    s_th_title = lv_label_create(s_th_card);
    lv_label_set_text(s_th_title, "Confidence Threshold");
    lv_obj_set_style_text_font(s_th_title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_th_title, t->text_primary, 0);
    lv_obj_set_pos(s_th_title, 8, 6);

    s_th_val = lv_label_create(s_th_card);
    lv_label_set_text(s_th_val, "50%");
    lv_obj_set_style_text_font(s_th_val, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_th_val, t->accent, 0);
    lv_obj_align(s_th_val, LV_ALIGN_TOP_RIGHT, -8, 6);

    s_threshold_slider = lv_slider_create(s_th_card);
    lv_obj_set_size(s_threshold_slider, LV_PCT(100) - 16, 24);
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
    lv_obj_add_event_cb(s_threshold_slider, _th_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Margin slider card */
    s_mg_card = lv_obj_create(scr);
    lv_obj_remove_style_all(s_mg_card);
    ui_theme_apply_card(s_mg_card);
    lv_obj_set_pos(s_mg_card, 12, 308);
    lv_obj_set_size(s_mg_card, LV_HOR_RES - 24, 110);

    s_mg_title = lv_label_create(s_mg_card);
    lv_label_set_text(s_mg_title, "Top-1 Margin");
    lv_obj_set_style_text_font(s_mg_title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_mg_title, t->text_primary, 0);
    lv_obj_set_pos(s_mg_title, 8, 6);

    s_mg_val = lv_label_create(s_mg_card);
    lv_label_set_text(s_mg_val, "10%");
    lv_obj_set_style_text_font(s_mg_val, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_mg_val, t->accent_alt, 0);
    lv_obj_align(s_mg_val, LV_ALIGN_TOP_RIGHT, -8, 6);

    s_margin_slider = lv_slider_create(s_mg_card);
    lv_obj_set_size(s_margin_slider, LV_PCT(100) - 16, 24);
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
    lv_obj_add_event_cb(s_margin_slider, _mg_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Tip card */
    s_tip_card = lv_obj_create(scr);
    lv_obj_remove_style_all(s_tip_card);
    ui_theme_apply_card(s_tip_card);
    lv_obj_set_pos(s_tip_card, 12, LV_VER_RES - 82);
    lv_obj_set_size(s_tip_card, LV_HOR_RES - 24, 66);

    s_tip = lv_label_create(s_tip_card);
    lv_label_set_text(s_tip, "Suggested: alarm 60/06 | clapping 72/14 | glass break 86/30");
    lv_obj_set_style_text_font(s_tip, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_tip, t->text_muted, 0);
    lv_obj_align(s_tip, LV_ALIGN_CENTER, 0, 0);

    ui_settings_theme_refresh = _theme_refresh;
    _refresh_controls();
}
