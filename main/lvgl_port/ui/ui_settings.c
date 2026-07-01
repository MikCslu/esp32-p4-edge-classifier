/*
 * Landscape threshold tuning page.
 */
#include "lvgl.h"
#include "service/app_state.h"
#include "service/audio_classify_service.h"
#include "lvgl_port/ui/ui_theme.h"
#include <stdio.h>

#define PAGE_PAD 16
#define CARD_W   (LV_HOR_RES - PAGE_PAD * 2)

static lv_obj_t *s_scr;
static lv_obj_t *s_title;
static lv_obj_t *s_class_card;
static lv_obj_t *s_threshold_card;
static lv_obj_t *s_margin_card;
static lv_obj_t *s_status_card;
static lv_obj_t *s_selected_label;
static lv_obj_t *s_dropdown;
static lv_obj_t *s_threshold_slider;
static lv_obj_t *s_margin_slider;
static lv_obj_t *s_threshold_value;
static lv_obj_t *s_margin_value;
static lv_obj_t *s_status_label;
static int s_selected_class = 6;

static lv_obj_t *make_card_at(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    ui_theme_apply_card(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    return obj;
}

static void style_slider(lv_obj_t *slider, lv_color_t color)
{
    const ui_theme_t *t = ui_theme_get();
    lv_obj_set_style_bg_color(slider, t->slider_track, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, color, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, color, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, color, LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 2, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 0, LV_PART_KNOB);
}

static void refresh_theme(void)
{
    if (!s_scr) return;
    const ui_theme_t *t = ui_theme_get();
    lv_obj_set_style_bg_color(s_scr, t->bg, 0);
    if (s_title) {
        lv_obj_set_style_text_font(s_title, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(s_title, t->text_primary, 0);
    }
    if (s_class_card) ui_theme_apply_card(s_class_card);
    if (s_threshold_card) ui_theme_apply_card(s_threshold_card);
    if (s_margin_card) ui_theme_apply_card(s_margin_card);
    if (s_status_card) ui_theme_apply_card(s_status_card);
    if (s_dropdown) {
        lv_obj_set_style_bg_color(s_dropdown, t->card_bg, 0);
        lv_obj_set_style_border_color(s_dropdown, t->accent, 0);
        lv_obj_set_style_text_color(s_dropdown, t->text_primary, 0);
    }
    if (s_threshold_slider) style_slider(s_threshold_slider, t->accent);
    if (s_margin_slider) style_slider(s_margin_slider, t->accent_alt);
}

static void refresh_controls(void)
{
    const ui_theme_t *t = ui_theme_get();
    float threshold = audio_cls_srv_get_class_threshold(s_selected_class);
    float margin = audio_cls_srv_get_class_margin(s_selected_class);
    int tp = (int)(threshold * 100.0f + 0.5f);
    int mp = (int)(margin * 100.0f + 0.5f);

    refresh_theme();
    lv_slider_set_value(s_threshold_slider, tp, LV_ANIM_OFF);
    lv_slider_set_value(s_margin_slider, mp, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_threshold_value, "%d%%", tp);
    lv_label_set_text_fmt(s_margin_value, "%d%%", mp);
    lv_label_set_text(s_selected_label, app_audio_class_title(s_selected_class));
    lv_obj_set_style_text_color(s_selected_label, t->accent, 0);
    lv_obj_set_style_text_font(s_selected_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_threshold_value, t->accent, 0);
    lv_obj_set_style_text_color(s_margin_value, t->accent_alt, 0);
    lv_obj_set_style_text_color(s_status_label, t->text_muted, 0);
}

static void dropdown_cb(lv_event_t *e)
{
    (void)e;
    s_selected_class = lv_dropdown_get_selected(s_dropdown);
    refresh_controls();
}

static void threshold_cb(lv_event_t *e)
{
    (void)e;
    int v = lv_slider_get_value(s_threshold_slider);
    audio_cls_srv_set_class_threshold(s_selected_class, (float)v / 100.0f);
    refresh_controls();
}

static void margin_cb(lv_event_t *e)
{
    (void)e;
    int v = lv_slider_get_value(s_margin_slider);
    audio_cls_srv_set_class_margin(s_selected_class, (float)v / 100.0f);
    refresh_controls();
}

void ui_settings_refresh(void)
{
    refresh_controls();
}

void ui_settings_create(lv_obj_t *scr)
{
    s_scr = scr;
    const ui_theme_t *t = ui_theme_get();
    ui_theme_apply_bg(scr);

    s_title = lv_label_create(scr);
    lv_label_set_text(s_title, "Tuning");
    lv_obj_set_pos(s_title, PAGE_PAD, 18);

    ui_theme_create_toggle_btn(scr);

    const int left_w = 360;
    const int right_w = LV_HOR_RES - PAGE_PAD * 3 - left_w;

    s_class_card = make_card_at(scr, PAGE_PAD, 64, left_w, 160);
    lv_obj_t *class_caption = lv_label_create(s_class_card);
    lv_label_set_text(class_caption, "Class");
    lv_obj_set_pos(class_caption, 16, 14);
    lv_obj_set_style_text_font(class_caption, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(class_caption, t->text_secondary, 0);

    s_selected_label = lv_label_create(s_class_card);
    lv_obj_set_width(s_selected_label, left_w - 32);
    lv_label_set_long_mode(s_selected_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(s_selected_label, 16, 44);

    s_dropdown = lv_dropdown_create(s_class_card);
    lv_dropdown_set_options(s_dropdown,
        "Alarm\nCar horn\nKnocking\nClapping\nDog bark\nFootsteps\n"
        "Glass break\nDoorbell\nBaby crying\nEngine\nTraffic\nBackground");
    lv_dropdown_set_selected(s_dropdown, s_selected_class);
    lv_obj_set_size(s_dropdown, left_w - 32, 42);
    lv_obj_set_pos(s_dropdown, 16, 96);
    lv_obj_set_style_text_font(s_dropdown, &lv_font_montserrat_24, 0);
    lv_obj_set_style_border_width(s_dropdown, 1, 0);
    lv_obj_set_style_radius(s_dropdown, 8, 0);
    lv_obj_add_event_cb(s_dropdown, dropdown_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_threshold_card = make_card_at(scr,
                                    PAGE_PAD * 2 + left_w,
                                    64,
                                    right_w,
                                    160);
    lv_obj_t *threshold_label = lv_label_create(s_threshold_card);
    lv_label_set_text(threshold_label, "Confidence");
    lv_obj_set_pos(threshold_label, 16, 18);
    lv_obj_set_style_text_font(threshold_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(threshold_label, t->text_primary, 0);
    s_threshold_value = lv_label_create(s_threshold_card);
    lv_obj_align(s_threshold_value, LV_ALIGN_TOP_RIGHT, -16, 18);
    lv_obj_set_style_text_font(s_threshold_value, &lv_font_montserrat_28, 0);
    s_threshold_slider = lv_slider_create(s_threshold_card);
    lv_obj_set_size(s_threshold_slider, right_w - 32, 26);
    lv_obj_align(s_threshold_slider, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_slider_set_range(s_threshold_slider, 40, 95);
    lv_obj_add_event_cb(s_threshold_slider, threshold_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_margin_card = make_card_at(scr,
                                 PAGE_PAD,
                                 244,
                                 left_w,
                                 160);
    lv_obj_t *margin_label = lv_label_create(s_margin_card);
    lv_label_set_text(margin_label, "Margin");
    lv_obj_set_pos(margin_label, 16, 18);
    lv_obj_set_style_text_font(margin_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(margin_label, t->text_primary, 0);
    s_margin_value = lv_label_create(s_margin_card);
    lv_obj_align(s_margin_value, LV_ALIGN_TOP_RIGHT, -16, 18);
    lv_obj_set_style_text_font(s_margin_value, &lv_font_montserrat_28, 0);
    s_margin_slider = lv_slider_create(s_margin_card);
    lv_obj_set_size(s_margin_slider, left_w - 32, 26);
    lv_obj_align(s_margin_slider, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_slider_set_range(s_margin_slider, 0, 35);
    lv_obj_add_event_cb(s_margin_slider, margin_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_status_card = make_card_at(scr,
                                 PAGE_PAD * 2 + left_w,
                                 244,
                                 right_w,
                                 160);
    s_status_label = lv_label_create(s_status_card);
    lv_label_set_text(s_status_label, "Swipe to return. Alerts jump to the face page.");
    lv_obj_set_width(s_status_label, right_w - 32);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_status_label, 16, 20);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_24, 0);

    ui_settings_theme_refresh = refresh_theme;
    refresh_controls();
}
