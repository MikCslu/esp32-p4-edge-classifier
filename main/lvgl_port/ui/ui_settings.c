#include "lvgl.h"
#include "service/app_state.h"
#include "service/audio_classify_service.h"

#include <stdio.h>

static lv_obj_t *s_dropdown;
static lv_obj_t *s_threshold_slider;
static lv_obj_t *s_margin_slider;
static lv_obj_t *s_threshold_label;
static lv_obj_t *s_margin_label;
static lv_obj_t *s_hint_label;
static int s_selected_class = 6;

static lv_obj_t *_make_label(lv_obj_t *parent, const char *text,
                             const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

static void _refresh_controls(void)
{
    float threshold = audio_cls_srv_get_class_threshold(s_selected_class);
    float margin = audio_cls_srv_get_class_margin(s_selected_class);
    int threshold_pct = (int)(threshold * 100.0f + 0.5f);
    int margin_pct = (int)(margin * 100.0f + 0.5f);

    if (s_threshold_slider) {
        lv_slider_set_value(s_threshold_slider, threshold_pct, LV_ANIM_OFF);
    }
    if (s_margin_slider) {
        lv_slider_set_value(s_margin_slider, margin_pct, LV_ANIM_OFF);
    }
    if (s_threshold_label) {
        lv_label_set_text_fmt(s_threshold_label, "Confidence threshold: %d%%", threshold_pct);
    }
    if (s_margin_label) {
        lv_label_set_text_fmt(s_margin_label, "Top1 margin: %d%%", margin_pct);
    }
    if (s_hint_label) {
        lv_label_set_text_fmt(s_hint_label,
                              "Editing %s. Higher values reduce false triggers; lower values react faster.",
                              app_audio_class_title(s_selected_class));
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

void ui_settings_refresh(void)
{
    _refresh_controls();
}

void ui_settings_create(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x111820), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = _make_label(scr, "Tuning",
                                  &lv_font_montserrat_48,
                                  lv_color_hex(0xf7fafc));
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 32, 28);

    s_hint_label = _make_label(scr, "",
                               &lv_font_montserrat_24,
                               lv_color_hex(0x9fb2c8));
    lv_obj_set_width(s_hint_label, LV_HOR_RES - 64);
    lv_obj_align(s_hint_label, LV_ALIGN_TOP_LEFT, 34, 92);

    s_dropdown = lv_dropdown_create(scr);
    lv_dropdown_set_options(s_dropdown,
                            "Alarm\nCar horn\nKnocking\nClapping\nDog bark\nFootsteps\nGlass break\nDoorbell\nBaby crying\nEngine\nTraffic\nBackground");
    lv_dropdown_set_selected(s_dropdown, s_selected_class);
    lv_obj_set_size(s_dropdown, 360, 54);
    lv_obj_set_style_text_font(s_dropdown, &lv_font_montserrat_24, 0);
    lv_obj_align(s_dropdown, LV_ALIGN_TOP_LEFT, 42, 164);
    lv_obj_add_event_cb(s_dropdown, _dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_threshold_label = _make_label(scr, "",
                                    &lv_font_montserrat_28,
                                    lv_color_hex(0xf4d06f));
    lv_obj_align(s_threshold_label, LV_ALIGN_TOP_LEFT, 44, 260);

    s_threshold_slider = lv_slider_create(scr);
    lv_obj_set_size(s_threshold_slider, LV_HOR_RES - 120, 24);
    lv_slider_set_range(s_threshold_slider, 40, 95);
    lv_obj_align(s_threshold_slider, LV_ALIGN_TOP_LEFT, 44, 312);
    lv_obj_add_event_cb(s_threshold_slider, _threshold_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_margin_label = _make_label(scr, "",
                                 &lv_font_montserrat_28,
                                 lv_color_hex(0x8ee6c9));
    lv_obj_align(s_margin_label, LV_ALIGN_TOP_LEFT, 44, 382);

    s_margin_slider = lv_slider_create(scr);
    lv_obj_set_size(s_margin_slider, LV_HOR_RES - 120, 24);
    lv_slider_set_range(s_margin_slider, 0, 35);
    lv_obj_align(s_margin_slider, LV_ALIGN_TOP_LEFT, 44, 434);
    lv_obj_add_event_cb(s_margin_slider, _margin_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *note = _make_label(scr,
                                 "Suggested: alarm 60/06, clapping 72/14, glass break 86/30.",
                                 &lv_font_montserrat_24,
                                 lv_color_hex(0xd7e3ef));
    lv_obj_set_width(note, LV_HOR_RES - 84);
    lv_obj_align(note, LV_ALIGN_BOTTOM_LEFT, 44, -44);

    _refresh_controls();
}
