/*
 * Portrait recognition timeline.
 */
#include "lvgl.h"
#include "service/app_state.h"
#include "lvgl_port/ui/ui_theme.h"
#include <stdio.h>

#define MAX_ROWS 16
#define PAGE_PAD 16
#define ROW_H    40

static lv_obj_t *s_scr;
static lv_obj_t *s_title;
static lv_obj_t *s_panel;
static lv_obj_t *s_rows[MAX_ROWS];
static lv_obj_t *s_dot[MAX_ROWS];
static lv_obj_t *s_name[MAX_ROWS];
static lv_obj_t *s_meta[MAX_ROWS];

static lv_color_t class_color(int class_id)
{
    const ui_theme_t *t = ui_theme_get();
    if (class_id < 0) return t->text_muted;
    return app_audio_class_needs_attention(class_id) ? t->danger : t->accent;
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
    if (s_panel) ui_theme_apply_card(s_panel);
}

void ui_log_refresh(void)
{
    const app_audio_stats_t *stats = app_state_get_audio();
    const ui_theme_t *t = ui_theme_get();
    char buf[64];

    refresh_theme();

    int total = (int)stats->recent_count;
    int count = total > MAX_ROWS ? MAX_ROWS : total;
    int start = total - count;

    for (int i = 0; i < MAX_ROWS; i++) {
        if (i >= count) {
            lv_obj_add_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        const app_audio_recent_t *item = &stats->recent[start + count - 1 - i];
        lv_obj_clear_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_rows[i], (i % 2) ? t->bar_bg : t->card_bg, 0);
        lv_obj_set_style_bg_color(s_dot[i], class_color(item->class_id), 0);

        lv_label_set_text(s_name[i], app_audio_class_title(item->class_id));
        lv_obj_set_style_text_color(s_name[i], class_color(item->class_id), 0);
        lv_obj_set_style_text_font(s_name[i], &lv_font_montserrat_24, 0);

        snprintf(buf, sizeof(buf), "+%lus  %d%%",
                 (unsigned long)(item->timestamp_ms / 1000),
                 (int)(item->confidence * 100.0f + 0.5f));
        lv_label_set_text(s_meta[i], buf);
        lv_obj_set_style_text_color(s_meta[i], t->text_muted, 0);
        lv_obj_set_style_text_font(s_meta[i], &lv_font_montserrat_14, 0);
    }
}

void ui_log_create(lv_obj_t *scr)
{
    s_scr = scr;
    ui_theme_apply_bg(scr);

    s_title = lv_label_create(scr);
    lv_label_set_text(s_title, "Timeline");
    lv_obj_set_pos(s_title, PAGE_PAD, 18);

    ui_theme_create_toggle_btn(scr);

    s_panel = lv_obj_create(scr);
    lv_obj_remove_style_all(s_panel);
    ui_theme_apply_card(s_panel);
    lv_obj_set_pos(s_panel, PAGE_PAD, 64);
    lv_obj_set_size(s_panel, LV_HOR_RES - PAGE_PAD * 2, LV_VER_RES - 86);

    for (int i = 0; i < MAX_ROWS; i++) {
        int y = 12 + i * ROW_H;
        s_rows[i] = lv_obj_create(s_panel);
        lv_obj_remove_style_all(s_rows[i]);
        lv_obj_set_size(s_rows[i], LV_PCT(100), ROW_H - 4);
        lv_obj_set_style_radius(s_rows[i], 8, 0);
        lv_obj_set_style_bg_opa(s_rows[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(s_rows[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(s_rows[i], 0, y);

        s_dot[i] = lv_obj_create(s_rows[i]);
        lv_obj_remove_style_all(s_dot[i]);
        lv_obj_set_size(s_dot[i], 10, 10);
        lv_obj_set_style_radius(s_dot[i], 5, 0);
        lv_obj_set_style_bg_opa(s_dot[i], LV_OPA_COVER, 0);
        lv_obj_align(s_dot[i], LV_ALIGN_LEFT_MID, 14, 0);

        s_name[i] = lv_label_create(s_rows[i]);
        lv_obj_set_width(s_name[i], LV_HOR_RES - 210);
        lv_label_set_long_mode(s_name[i], LV_LABEL_LONG_CLIP);
        lv_obj_align(s_name[i], LV_ALIGN_LEFT_MID, 36, 0);

        s_meta[i] = lv_label_create(s_rows[i]);
        lv_obj_align(s_meta[i], LV_ALIGN_RIGHT_MID, -12, 0);
    }

    ui_log_theme_refresh = refresh_theme;
    ui_log_refresh();
}
