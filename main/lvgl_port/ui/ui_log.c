/*
 * ui_log.c — Recognition Log with scrolling rows, theme-aware
 */
#include "lvgl.h"
#include "service/app_state.h"
#include "lvgl_port/ui/ui_theme.h"
#include <stdio.h>

#define MAX_ROWS 20
#define ROW_H    30

static lv_obj_t *s_scr;
static lv_obj_t *s_log_card;
static lv_obj_t *s_log_rows[MAX_ROWS];
static lv_obj_t *s_log_nums[MAX_ROWS];
static lv_obj_t *s_log_texts[MAX_ROWS];
static lv_obj_t *s_log_times[MAX_ROWS];

static lv_color_t _class_color(int class_id)
{
    static const lv_color_t pal[] = {
        LV_COLOR_MAKE(0x7d,0xd3,0xfc), LV_COLOR_MAKE(0xff,0x8f,0xb3),
        LV_COLOR_MAKE(0xff,0xd1,0x66), LV_COLOR_MAKE(0x8e,0xe6,0xc9),
        LV_COLOR_MAKE(0xc0,0x84,0xfc), LV_COLOR_MAKE(0x67,0xe8,0xf9),
        LV_COLOR_MAKE(0xf9,0xa8,0xd4), LV_COLOR_MAKE(0x6e,0xd6,0x8a),
        LV_COLOR_MAKE(0x94,0xa3,0xb8), LV_COLOR_MAKE(0xf4,0x71,0x6b),
        LV_COLOR_MAKE(0x60,0xa5,0xfa), LV_COLOR_MAKE(0x93,0xc5,0xfd),
    };
    int idx = class_id < 0 ? 0 : (class_id >= 12 ? class_id % 12 : class_id);
    return pal[idx];
}

static void _refresh_log_rows(void)
{
    const app_audio_stats_t *stats = app_state_get_audio();
    const ui_theme_t *t = ui_theme_get();
    size_t total = stats->recent_count;
    int i;

    if (total == 0) {
        for (i = 0; i < MAX_ROWS; i++) {
            if (s_log_rows[i]) lv_obj_add_flag(s_log_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    int start = (int)total > MAX_ROWS ? (int)total - MAX_ROWS : 0;
    int count = (int)total - start;
    if (count > MAX_ROWS) count = MAX_ROWS;

    for (i = 0; i < count; i++) {
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

    for (i = count; i < MAX_ROWS; i++) {
        if (s_log_rows[i]) lv_obj_add_flag(s_log_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void _theme_refresh(void)
{
    if (!s_scr) return;
    const ui_theme_t *t = ui_theme_get();
    lv_obj_set_style_bg_color(s_scr, t->bg, 0);
    if (s_log_card) {
        lv_obj_set_style_bg_color(s_log_card, t->card_bg, 0);
        lv_obj_set_style_border_color(s_log_card, t->card_border, 0);
    }
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
    lv_obj_t *tc = lv_obj_create(scr);
    lv_obj_remove_style_all(tc);
    ui_theme_apply_card(tc);
    lv_obj_set_pos(tc, 16, 12);
    lv_obj_set_size(tc, LV_HOR_RES - 32, 96);

    lv_obj_t *title = lv_label_create(tc);
    lv_label_set_text(title, "Recognition Log");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(title, t->text_primary, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 12, -10);

    lv_obj_t *sub = lv_label_create(tc);
    lv_label_set_text(sub, "Latest detections, confidence and elapsed time");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(sub, t->text_secondary, 0);
    lv_obj_align(sub, LV_ALIGN_LEFT_MID, 12, 22);

    /* Table header */
    lv_obj_t *h_num = lv_label_create(scr);
    lv_label_set_text(h_num, "#");
    lv_obj_set_style_text_font(h_num, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(h_num, t->text_muted, 0);
    lv_obj_set_pos(h_num, 32, 124);

    lv_obj_t *h_cls = lv_label_create(scr);
    lv_label_set_text(h_cls, "Class");
    lv_obj_set_style_text_font(h_cls, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(h_cls, t->text_muted, 0);
    lv_obj_set_pos(h_cls, 76, 124);

    lv_obj_t *h_tm = lv_label_create(scr);
    lv_label_set_text(h_tm, "Time/Confidence");
    lv_obj_set_style_text_font(h_tm, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(h_tm, t->text_muted, 0);
    lv_obj_set_pos(h_tm, LV_HOR_RES - 230, 124);

    /* Log card with rows */
    s_log_card = lv_obj_create(scr);
    lv_obj_remove_style_all(s_log_card);
    ui_theme_apply_card(s_log_card);
    lv_obj_set_pos(s_log_card, 16, 148);
    lv_obj_set_size(s_log_card, LV_HOR_RES - 32, LV_VER_RES - 168);

    /* Row divider line */
    lv_obj_t *div = lv_obj_create(s_log_card);
    lv_obj_remove_style_all(div);
    lv_obj_set_size(div, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(div, t->card_border, 0);
    lv_obj_set_pos(div, 0, 0);

    for (i = 0; i < MAX_ROWS; i++) {
        int y = 4 + i * ROW_H;
        s_log_rows[i] = lv_obj_create(s_log_card);
        lv_obj_remove_style_all(s_log_rows[i]);
        lv_obj_set_size(s_log_rows[i], LV_PCT(100), ROW_H - 1);
        lv_obj_set_style_radius(s_log_rows[i], 4, 0);
        lv_obj_set_style_bg_color(s_log_rows[i], (i % 2 == 0) ? t->card_bg : t->bar_bg, 0);
        lv_obj_set_style_bg_opa(s_log_rows[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_log_rows[i], 0, 0);
        lv_obj_clear_flag(s_log_rows[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(s_log_rows[i], 2, y);

        s_log_nums[i] = lv_label_create(s_log_rows[i]);
        lv_label_set_text(s_log_nums[i], "");
        lv_obj_set_style_text_font(s_log_nums[i], &lv_font_montserrat_24, 0);
        lv_obj_align(s_log_nums[i], LV_ALIGN_LEFT_MID, 8, 0);

        s_log_texts[i] = lv_label_create(s_log_rows[i]);
        lv_label_set_text(s_log_texts[i], "");
        lv_obj_set_style_text_font(s_log_texts[i], &lv_font_montserrat_24, 0);
        lv_label_set_long_mode(s_log_texts[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(s_log_texts[i], LV_HOR_RES - 280);
        lv_obj_align(s_log_texts[i], LV_ALIGN_LEFT_MID, 56, 0);

        s_log_times[i] = lv_label_create(s_log_rows[i]);
        lv_label_set_text(s_log_times[i], "");
        lv_obj_set_style_text_font(s_log_times[i], &lv_font_montserrat_24, 0);
        lv_obj_align(s_log_times[i], LV_ALIGN_RIGHT_MID, -8, 0);
    }

    ui_log_theme_refresh = _theme_refresh;
    ui_log_refresh();
}
