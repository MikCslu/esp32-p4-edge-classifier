/*
 * ui_log.c — Recognition Log with scrolling table, theme-aware
 */
#include "lvgl.h"
#include "service/app_state.h"
#include "lvgl_port/ui/ui_theme.h"
#include <stdio.h>

static lv_obj_t *s_scr;
static lv_obj_t *s_log_area;
static lv_obj_t *s_log_rows[20];
static lv_obj_t *s_log_nums[20];
static lv_obj_t *s_log_texts[20];
static lv_obj_t *s_log_times[20];
static int s_row_count;

static lv_color_t _class_color(int class_id)
{
    /* Rotate through a fixed palette for visual distinction */
    static const lv_color_t pal[] = {
        LV_COLOR_MAKE(0x7d,0xd3,0xfc), LV_COLOR_MAKE(0xff,0x8f,0xb3),
        LV_COLOR_MAKE(0xff,0xd1,0x66), LV_COLOR_MAKE(0x8e,0xe6,0xc9),
        LV_COLOR_MAKE(0xc0,0x84,0xfc), LV_COLOR_MAKE(0x67,0xe8,0xf9),
        LV_COLOR_MAKE(0xf9,0xa8,0xd4), LV_COLOR_MAKE(0x6e,0xd6,0x8a),
        LV_COLOR_MAKE(0x94,0xa3,0xb8), LV_COLOR_MAKE(0xf4,0x71,0x6b),
        LV_COLOR_MAKE(0x60,0xa5,0xfa), LV_COLOR_MAKE(0x93,0xc5,0xfd),
    };
    int idx = class_id;
    if (idx < 0) idx = 0;
    if (idx >= 12) idx = idx % 12;
    return pal[idx];
}

static void _refresh_log_rows(void)
{
    const app_audio_stats_t *stats = app_state_get_audio();
    const ui_theme_t *t = ui_theme_get();
    size_t total = stats->recent_count;
    int i;

    if (total == 0) {
        for (i = 0; i < 20 && s_log_rows[i]; i++) {
            lv_obj_add_flag(s_log_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    /* Show most recent 20 entries, newest at top */
    int start = (int)total > 20 ? (int)total - 20 : 0;
    int count = (int)total - start;
    if (count > 20) count = 20;

    for (i = 0; i < count && i < 20; i++) {
        const app_audio_recent_t *item = &stats->recent[start + count - 1 - i];
        lv_color_t c = _class_color(item->class_id);

        lv_obj_clear_flag(s_log_rows[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_log_rows[i], (i % 2 == 0) ? t->card_bg : t->bar_bg, 0);

        char num[6];
        snprintf(num, sizeof(num), "%02u", (unsigned)(i + 1));
        lv_label_set_text(s_log_nums[i], num);
        lv_obj_set_style_text_color(s_log_nums[i], t->text_muted, 0);

        lv_label_set_text(s_log_texts[i], app_audio_class_title(item->class_id));
        lv_obj_set_style_text_color(s_log_texts[i], c, 0);

        char tm[20];
        snprintf(tm, sizeof(tm), "+%lus  %d%%",
                 (unsigned long)(item->timestamp_ms / 1000),
                 (int)(item->confidence * 100.0f + 0.5f));
        lv_label_set_text(s_log_times[i], tm);
        lv_obj_set_style_text_color(s_log_times[i], t->text_muted, 0);
    }

    for (i = count; i < 20 && s_log_rows[i]; i++) {
        lv_obj_add_flag(s_log_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void _theme_refresh(void)
{
    if (!s_scr) return;
    const ui_theme_t *t = ui_theme_get();
    lv_obj_set_style_bg_color(s_scr, t->bg, 0);
    _refresh_log_rows();
}

void ui_log_refresh(void)
{
    _refresh_log_rows();
}

void ui_log_create(lv_obj_t *scr)
{
    s_scr = scr;
    const ui_theme_t *t = ui_theme_get();
    int i;

    ui_theme_apply_bg(scr);

    /* Title card */
    lv_obj_t *title_card = lv_obj_create(scr);
    lv_obj_remove_style_all(title_card);
    ui_theme_apply_card(title_card);
    lv_obj_set_size(title_card, LV_HOR_RES - 32, 96);
    lv_obj_set_pos(title_card, 16, 12);

    lv_obj_t *title = lv_label_create(title_card);
    lv_label_set_text(title, "Recognition Log");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(title, t->text_primary, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, -8);

    lv_obj_t *sub = lv_label_create(title_card);
    lv_label_set_text(sub, "Latest detections with confidence and elapsed time");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(sub, t->text_secondary, 0);
    lv_obj_align(sub, LV_ALIGN_LEFT_MID, 8, 24);

    /* Table header */
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_HOR_RES - 32, 36);
    lv_obj_set_pos(header, 16, 120);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);

    lv_obj_t *h_num = lv_label_create(header);
    lv_label_set_text(h_num, "#");
    lv_obj_set_style_text_font(h_num, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(h_num, t->text_muted, 0);
    lv_obj_align(h_num, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t *h_class = lv_label_create(header);
    lv_label_set_text(h_class, "Class");
    lv_obj_set_style_text_font(h_class, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(h_class, t->text_muted, 0);
    lv_obj_align(h_class, LV_ALIGN_LEFT_MID, 64, 0);

    lv_obj_t *h_info = lv_label_create(header);
    lv_label_set_text(h_info, "Time / Confidence");
    lv_obj_set_style_text_font(h_info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(h_info, t->text_muted, 0);
    lv_obj_align(h_info, LV_ALIGN_RIGHT_MID, -8, 0);

    /* Log card with rows */
    lv_obj_t *log_card = lv_obj_create(scr);
    lv_obj_remove_style_all(log_card);
    ui_theme_apply_card(log_card);
    lv_obj_set_size(log_card, LV_HOR_RES - 32, LV_VER_RES - 176);
    lv_obj_set_pos(log_card, 16, 160);

    s_log_area = lv_obj_create(log_card);
    lv_obj_remove_style_all(s_log_area);
    lv_obj_set_size(s_log_area, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(s_log_area, 4, 0);
    lv_obj_set_style_border_width(s_log_area, 0, 0);
    lv_obj_set_flex_flow(s_log_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_log_area, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_log_area, 1, 0);
    lv_obj_set_scrollbar_mode(s_log_area, LV_SCROLLBAR_MODE_ON);
    s_row_count = 20;

    for (i = 0; i < 20; i++) {
        s_log_rows[i] = lv_obj_create(s_log_area);
        lv_obj_remove_style_all(s_log_rows[i]);
        lv_obj_set_size(s_log_rows[i], LV_PCT(100), 30);
        lv_obj_set_style_radius(s_log_rows[i], 6, 0);
        lv_obj_set_style_bg_color(s_log_rows[i], (i % 2 == 0) ? t->card_bg : t->bar_bg, 0);
        lv_obj_set_style_bg_opa(s_log_rows[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_log_rows[i], 0, 0);
        lv_obj_clear_flag(s_log_rows[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(s_log_rows[i], 0, 0);
        lv_obj_add_flag(s_log_rows[i], LV_OBJ_FLAG_HIDDEN);

        s_log_nums[i] = lv_label_create(s_log_rows[i]);
        lv_label_set_text(s_log_nums[i], "");
        lv_obj_set_style_text_font(s_log_nums[i], &lv_font_montserrat_24, 0);
        lv_obj_align(s_log_nums[i], LV_ALIGN_LEFT_MID, 8, 0);

        s_log_texts[i] = lv_label_create(s_log_rows[i]);
        lv_label_set_text(s_log_texts[i], "");
        lv_obj_set_style_text_font(s_log_texts[i], &lv_font_montserrat_24, 0);
        lv_label_set_long_mode(s_log_texts[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(s_log_texts[i], 540);
        lv_obj_align(s_log_texts[i], LV_ALIGN_LEFT_MID, 64, 0);

        s_log_times[i] = lv_label_create(s_log_rows[i]);
        lv_label_set_text(s_log_times[i], "");
        lv_obj_set_style_text_font(s_log_times[i], &lv_font_montserrat_24, 0);
        lv_obj_align(s_log_times[i], LV_ALIGN_RIGHT_MID, -8, 0);
    }

    ui_log_theme_refresh = _theme_refresh;
    ui_log_refresh();
}
