#include "lvgl.h"
#include "service/app_state.h"

#include <stdio.h>

static lv_obj_t *s_log_label;

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

void ui_log_refresh(void)
{
    if (!s_log_label) {
        return;
    }

    const app_audio_stats_t *stats = app_state_get_audio();
    char buf[1024];
    size_t off = 0;

    if (stats->recent_count == 0) {
        lv_label_set_text(s_log_label, "No recognition events yet.");
        return;
    }

    for (size_t i = 0; i < stats->recent_count && off < sizeof(buf); i++) {
        const app_audio_recent_t *item = &stats->recent[i];
        int written = snprintf(buf + off, sizeof(buf) - off,
                               "%02u  +%lus  %-12s  %3d%%\n",
                               (unsigned)(i + 1),
                               (unsigned long)(item->timestamp_ms / 1000),
                               app_audio_class_name(item->class_id),
                               (int)(item->confidence * 100.0f + 0.5f));
        if (written < 0) {
            break;
        }
        off += (size_t)written;
    }
    lv_label_set_text(s_log_label, buf);
}

void ui_log_create(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0d1218), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = _make_label(scr, "Recognition Log",
                                  &lv_font_montserrat_48,
                                  lv_color_hex(0xf7fafc));
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 32, 28);

    lv_obj_t *subtitle = _make_label(scr, "Latest detections, confidence, and device uptime",
                                     &lv_font_montserrat_24,
                                     lv_color_hex(0x9fb2c8));
    lv_obj_set_width(subtitle, LV_HOR_RES - 64);
    lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 34, 86);

    s_log_label = _make_label(scr, "",
                              &lv_font_montserrat_24,
                              lv_color_hex(0xe6edf5));
    lv_obj_set_width(s_log_label, LV_HOR_RES - 80);
    lv_obj_align(s_log_label, LV_ALIGN_TOP_LEFT, 40, 154);

    ui_log_refresh();
}
