/*
 * Voice playback page with preset buttons and volume control.
 * Playback is async via audio_playback_service; UI callbacks never block.
 */
#include "lvgl.h"
#include "lvgl_port/ui/ui_voice.h"
#include "lvgl_port/ui/ui_theme.h"
#include "service/app_config_service.h"
#include "service/audio_playback_service.h"
#include "service/speech_service.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define PAGE_PAD 16
#define TOP_H    54
#define GAP      10
#define GRID_COLS 4
#define GRID_ROWS 3
#define TAP_MOVE_LIMIT_PX 18   /* 防误触：按下到抬起移动超过 18px 视为滑动，不算点击 */

static lv_obj_t *s_scr;
static lv_obj_t *s_title;
static lv_obj_t *s_vol_slider;
static lv_obj_t *s_vol_label;

typedef struct {
    lv_point_t down;
    bool armed;
} voice_btn_state_t;

/* 每个语音按钮记录按下位置，用于区分"点击"和"滑动"（LVGL 手势防误触） */
static voice_btn_state_t s_btn_state[12];

static const struct {
    const char *label;
    speech_id_t id;
} s_presets[] = {
    {"Hello", SPEECH_HELLO},
    {"Morning", SPEECH_GOOD_MORNING},
    {"Welcome Back", SPEECH_WELCOME_BACK},
    {"See You", SPEECH_SEE_YOU},
    {"Thanks", SPEECH_THANKS},
    {"Need Help", SPEECH_HELP},
    {"Wait", SPEECH_WAIT},
    {"OK", SPEECH_OK},
    {"Sorry", SPEECH_SORRY},
    {"I'm Busy", SPEECH_BUSY},
    {"Talk Later", SPEECH_LATER},
    {"Help Me", SPEECH_NEED_HELP},
};
#define PRESET_COUNT (sizeof(s_presets) / sizeof(s_presets[0]))

/* 语音按钮回调：用 PRESSED/RELEASED 两段式判断真正点击，
 * 避免用户在按钮上滑动时误触发语音。播放是异步的（speech_service_say
 * 只投递请求到播放队列），UI 回调绝不做阻塞操作。 */
static void _play_cb(lv_event_t *e)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    if (index >= PRESET_COUNT) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p = {0};
    if (indev) {
        lv_indev_get_point(indev, &p);
    }

    if (code == LV_EVENT_PRESSED) {
        s_btn_state[index].down = p;
        s_btn_state[index].armed = true;
        return;
    }

    if (code != LV_EVENT_RELEASED || !s_btn_state[index].armed) {
        return;
    }

    s_btn_state[index].armed = false;
    int dx = abs((int)p.x - (int)s_btn_state[index].down.x);
    int dy = abs((int)p.y - (int)s_btn_state[index].down.y);
    if (dx > TAP_MOVE_LIMIT_PX || dy > TAP_MOVE_LIMIT_PX) {
        return;
    }

    /* 异步播放：投递到 audio_playback_service 的队列，立即返回 */
    speech_service_say(s_presets[index].id, AUDIO_PLAY_PRIO_NORMAL);
}

static void _stop_cb(lv_event_t *e)
{
    (void)e;
    audio_playback_stop();
}

/* 音量滑条：拖动实时调音量，松手(RELEASED)才持久化到 NVS（减少 NVS 写入次数） */
static void _vol_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int v = lv_slider_get_value(s_vol_slider);
    audio_playback_set_volume((uint8_t)v);
    if (s_vol_label) {
        lv_label_set_text_fmt(s_vol_label, "%d%%", v);
    }
    if (code == LV_EVENT_RELEASED) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(app_config_save_volume((uint8_t)v));
    }
}

static void _refresh_theme(void)
{
    if (!s_scr) {
        return;
    }
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

    s_title = lv_label_create(scr);
    lv_label_set_text(s_title, "Voice");
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_title, t->text_primary, 0);
    lv_obj_set_pos(s_title, PAGE_PAD, 14);

    lv_obj_t *stop_btn = lv_btn_create(scr);
    lv_obj_remove_style_all(stop_btn);
    lv_obj_set_pos(stop_btn, LV_HOR_RES - PAGE_PAD - 108, 12);
    lv_obj_set_size(stop_btn, 108, 42);
    lv_obj_set_style_radius(stop_btn, 12, 0);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0xff4455), 0);
    lv_obj_set_style_bg_opa(stop_btn, LV_OPA_70, 0);

    lv_obj_t *stop_lbl = lv_label_create(stop_btn);
    lv_label_set_text(stop_lbl, "Stop");
    lv_obj_set_style_text_font(stop_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(stop_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_center(stop_lbl);
    lv_obj_add_event_cb(stop_btn, _stop_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *vol_card = lv_obj_create(scr);
    lv_obj_remove_style_all(vol_card);
    ui_theme_apply_card(vol_card);
    lv_obj_set_pos(vol_card, LV_HOR_RES - PAGE_PAD - 288, 12);
    lv_obj_set_size(vol_card, 168, 42);

    s_vol_label = lv_label_create(vol_card);
    lv_label_set_text_fmt(s_vol_label, "%u%%", audio_playback_get_volume());
    lv_obj_set_style_text_font(s_vol_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_vol_label, t->accent, 0);
    lv_obj_set_pos(s_vol_label, 12, 11);

    s_vol_slider = lv_slider_create(vol_card);
    lv_obj_set_size(s_vol_slider, 92, 16);
    lv_slider_set_range(s_vol_slider, 0, 100);
    lv_slider_set_value(s_vol_slider, audio_playback_get_volume(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_vol_slider, t->slider_track, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_vol_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_vol_slider, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_vol_slider, t->accent, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_vol_slider, t->accent, LV_PART_KNOB);
    lv_obj_set_style_border_width(s_vol_slider, 0, LV_PART_KNOB);
    lv_obj_align(s_vol_slider, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_add_event_cb(s_vol_slider, _vol_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_vol_slider, _vol_cb, LV_EVENT_RELEASED, NULL);

    /* 网格布局：12 个语音按钮按 4 列 x 3 行排布（按钮尺寸按屏幕动态计算） */
    int grid_top = TOP_H + PAGE_PAD;
    int grid_w = LV_HOR_RES - PAGE_PAD * 2;
    int grid_h = LV_VER_RES - grid_top - PAGE_PAD;
    int btn_w = (grid_w - GAP * (GRID_COLS - 1)) / GRID_COLS;
    int btn_h = (grid_h - GAP * (GRID_ROWS - 1)) / GRID_ROWS;
    for (size_t i = 0; i < PRESET_COUNT; i++) {
        lv_obj_t *btn = lv_btn_create(scr);
        lv_obj_remove_style_all(btn);
        int col = (int)(i % GRID_COLS);
        int row = (int)(i / GRID_COLS);
        lv_obj_set_pos(btn, PAGE_PAD + col * (btn_w + GAP), grid_top + row * (btn_h + GAP));
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_bg_color(btn, t->card_bg, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, t->card_border, 0);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, s_presets[i].label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(lbl, t->accent, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl, btn_w - 18);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, _play_cb, LV_EVENT_PRESSED, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(btn, _play_cb, LV_EVENT_RELEASED, (void *)(uintptr_t)i);
    }

    ui_voice_theme_refresh = _refresh_theme;
}
