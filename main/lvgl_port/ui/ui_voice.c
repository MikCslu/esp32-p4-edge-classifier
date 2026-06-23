/*
 * ui_voice.c — Voice playback page with preset buttons and volume.
 * All playback is async via audio_playback_service; no blocking in UI.
 */
#include "lvgl.h"
#include "lvgl_port/ui/ui_voice.h"
#include "lvgl_port/ui/ui_theme.h"
#include "service/app_config_service.h"
#include "service/audio_playback_service.h"
#include "service/speech_service.h"
#include "esp_err.h"
#include <stdio.h>

#define PAGE_PAD 16
#define BTN_H    64
#define CARD_W   (LV_HOR_RES - PAGE_PAD * 2)

static lv_obj_t *s_scr;
static lv_obj_t *s_title;
static lv_obj_t *s_vol_slider;
static lv_obj_t *s_vol_label;

static const struct {
    const char *label;
    speech_id_t id;
} s_presets[] = {
    {"Hello",        SPEECH_HELLO},
    {"Morning",      SPEECH_GOOD_MORNING},
    {"Welcome Back", SPEECH_WELCOME_BACK},
    {"See You",      SPEECH_SEE_YOU},
};
#define PRESET_COUNT (sizeof(s_presets)/sizeof(s_presets[0]))

static void _play_cb(lv_event_t *e)
{
    speech_id_t id = (speech_id_t)(uintptr_t)lv_event_get_user_data(e);
    speech_service_say(id, AUDIO_PLAY_PRIO_NORMAL);
}

static void _stop_cb(lv_event_t *e)
{
    (void)e;
    audio_playback_stop();
}

static void _vol_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int v = lv_slider_get_value(s_vol_slider);
    audio_playback_set_volume((uint8_t)v);
    if (s_vol_label) lv_label_set_text_fmt(s_vol_label, "%d%%", v);
    if (code == LV_EVENT_RELEASED) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(app_config_save_volume((uint8_t)v));
    }
}

static void _refresh_theme(void)
{
    if (!s_scr) return;
    lv_obj_set_style_bg_color(s_scr, ui_theme_get()->bg, 0);
}

void ui_voice_refresh(void)
{
    _refresh_theme();
}

void ui_voice_create(lv_obj_t *scr)
{
    s_scr = scr;
    const ui_theme_t *t = ui_theme_get();
    lv_obj_set_style_bg_color(scr, t->bg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Title */
    s_title = lv_label_create(scr);
    lv_label_set_text(s_title, "Voice");
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_title, t->text_primary, 0);
    lv_obj_set_pos(s_title, PAGE_PAD, 18);

    /* Preset buttons */
    int y = 64;
    for (size_t i = 0; i < PRESET_COUNT; i++) {
        lv_obj_t *btn = lv_btn_create(scr);
        lv_obj_remove_style_all(btn);
        lv_obj_set_pos(btn, PAGE_PAD, y);
        lv_obj_set_size(btn, CARD_W, BTN_H);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_bg_color(btn, t->card_bg, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, t->card_border, 0);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, s_presets[i].label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(lbl, t->accent, 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, _play_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)s_presets[i].id);
        y += BTN_H + 12;
    }

    /* Volume slider */
    y += 8;
    lv_obj_t *vol_card = lv_obj_create(scr);
    lv_obj_remove_style_all(vol_card);
    ui_theme_apply_card(vol_card);
    lv_obj_set_pos(vol_card, PAGE_PAD, y);
    lv_obj_set_size(vol_card, CARD_W, 84);

    lv_obj_t *vol_lbl = lv_label_create(vol_card);
    lv_label_set_text(vol_lbl, "Volume");
    lv_obj_set_style_text_font(vol_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(vol_lbl, t->text_primary, 0);
    lv_obj_set_pos(vol_lbl, 16, 10);

    s_vol_label = lv_label_create(vol_card);
    lv_label_set_text_fmt(s_vol_label, "%u%%", audio_playback_get_volume());
    lv_obj_set_style_text_font(s_vol_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_vol_label, t->accent, 0);
    lv_obj_align(s_vol_label, LV_ALIGN_TOP_RIGHT, -16, 10);

    s_vol_slider = lv_slider_create(vol_card);
    lv_obj_set_size(s_vol_slider, CARD_W - 32, 22);
    lv_slider_set_range(s_vol_slider, 0, 100);
    lv_slider_set_value(s_vol_slider, audio_playback_get_volume(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_vol_slider, t->slider_track, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_vol_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_vol_slider, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_vol_slider, t->accent, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_vol_slider, t->accent, LV_PART_KNOB);
    lv_obj_set_style_border_width(s_vol_slider, 2, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_vol_slider, 0, LV_PART_KNOB);
    lv_obj_align(s_vol_slider, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(s_vol_slider, _vol_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_vol_slider, _vol_cb, LV_EVENT_RELEASED, NULL);

    /* Stop button */
    y += 96;
    lv_obj_t *stop_btn = lv_btn_create(scr);
    lv_obj_remove_style_all(stop_btn);
    lv_obj_set_pos(stop_btn, PAGE_PAD, y);
    lv_obj_set_size(stop_btn, CARD_W, BTN_H);
    lv_obj_set_style_radius(stop_btn, 12, 0);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0xff4455), 0);
    lv_obj_set_style_bg_opa(stop_btn, LV_OPA_60, 0);

    lv_obj_t *stop_lbl = lv_label_create(stop_btn);
    lv_label_set_text(stop_lbl, "Stop");
    lv_obj_set_style_text_font(stop_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(stop_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_center(stop_lbl);

    lv_obj_add_event_cb(stop_btn, _stop_cb, LV_EVENT_CLICKED, NULL);

    ui_voice_theme_refresh = _refresh_theme;
}
