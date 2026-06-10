#include "lvgl.h"
#include "service/app_state.h"

#include <stdio.h>

#define MODEL_VERSION_TEXT "MobileNetV2 Audio 12-class"
#define MODEL_SIZE_TEXT    "ESP-DL 761856 bytes"

static lv_obj_t *s_last_label;
static lv_obj_t *s_total_label;
static lv_obj_t *s_stats_label;
static lv_obj_t *s_model_label;

static void _set_text(lv_obj_t *obj, const char *text)
{
    if (obj) {
        lv_label_set_text(obj, text);
    }
}

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

void ui_main_refresh(void)
{
    const app_audio_stats_t *stats = app_state_get_audio();
    char buf[640];

    if (stats->last_class_id >= 0) {
        snprintf(buf, sizeof(buf), "Last: %s  %d%%  t+%lus",
                 app_audio_class_title(stats->last_class_id),
                 (int)(stats->last_confidence * 100.0f + 0.5f),
                 (unsigned long)(stats->last_timestamp_ms / 1000));
    } else {
        snprintf(buf, sizeof(buf), "Last: waiting for audio");
    }
    _set_text(s_last_label, buf);

    snprintf(buf, sizeof(buf), "Events: %lu",
             (unsigned long)stats->total_events);
    _set_text(s_total_label, buf);

    size_t off = 0;
    for (int i = 0; i < APP_AUDIO_CLASS_COUNT - 1 && off < sizeof(buf); i++) {
        uint32_t count = stats->class_counts[i];
        int avg = (int)(app_state_audio_avg_confidence(i) * 100.0f + 0.5f);
        int written = snprintf(buf + off, sizeof(buf) - off,
                               "%-12s %3lu  avg %2d%%\n",
                               app_audio_class_name(i),
                               (unsigned long)count,
                               avg);
        if (written < 0) {
            break;
        }
        off += (size_t)written;
    }
    _set_text(s_stats_label, buf);

    snprintf(buf, sizeof(buf), "%s\n%s", MODEL_VERSION_TEXT, MODEL_SIZE_TEXT);
    _set_text(s_model_label, buf);
}

void ui_main_create(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x10151c), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = _make_label(scr, "Audio Console",
                                  &lv_font_montserrat_48,
                                  lv_color_hex(0xf5f7fb));
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 32, 28);

    lv_obj_t *subtitle = _make_label(scr, "System overview, model state, and recognition totals",
                                     &lv_font_montserrat_24,
                                     lv_color_hex(0x9fb2c8));
    lv_obj_set_width(subtitle, LV_HOR_RES - 64);
    lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 34, 86);

    s_last_label = _make_label(scr, "Last: waiting for audio",
                               &lv_font_montserrat_28,
                               lv_color_hex(0x8ee6c9));
    lv_obj_set_width(s_last_label, LV_HOR_RES - 64);
    lv_obj_align(s_last_label, LV_ALIGN_TOP_LEFT, 34, 146);

    s_total_label = _make_label(scr, "Events: 0",
                                &lv_font_montserrat_28,
                                lv_color_hex(0xffd166));
    lv_obj_align(s_total_label, LV_ALIGN_TOP_RIGHT, -36, 146);

    s_model_label = _make_label(scr, MODEL_VERSION_TEXT,
                                &lv_font_montserrat_24,
                                lv_color_hex(0xd7e3ef));
    lv_obj_set_width(s_model_label, 420);
    lv_obj_align(s_model_label, LV_ALIGN_BOTTOM_LEFT, 36, -42);

    s_stats_label = _make_label(scr, "",
                                &lv_font_montserrat_24,
                                lv_color_hex(0xe8edf3));
    lv_obj_set_width(s_stats_label, LV_HOR_RES - 80);
    lv_obj_align(s_stats_label, LV_ALIGN_TOP_LEFT, 40, 220);

    ui_main_refresh();
}
