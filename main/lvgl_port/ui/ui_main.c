/*
 * ui_main.c — Audio Console: cards, bar charts, theme-aware
 */
#include "lvgl.h"
#include "service/app_state.h"
#include "lvgl_port/ui/ui_theme.h"
#include <stdio.h>

#define MODEL_VERSION_TEXT "MobileNetV2 Audio 12-class"
#define MODEL_SIZE_TEXT    "ESP-DL 761856 bytes"
#define BAR_COUNT          12

static lv_obj_t *s_scr;
static lv_obj_t *s_last_label, *s_last_conf_label;
static lv_obj_t *s_total_label, *s_total_num_label;
static lv_obj_t *s_model_label;
static lv_obj_t *s_bars[BAR_COUNT];
static lv_obj_t *s_bar_labels[BAR_COUNT];
static lv_obj_t *s_bar_nums[BAR_COUNT];
static lv_obj_t *s_bar_max_rect;

static lv_obj_t *_make_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(l, ui_theme_get()->text_primary, 0);
    return l;
}

static lv_obj_t *_make_subtitle(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(l, ui_theme_get()->text_secondary, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

static void _build_bars(lv_obj_t *parent)
{
    const app_audio_stats_t *stats = app_state_get_audio();
    uint32_t max_count = 1;
    int i;

    for (i = 0; i < BAR_COUNT; i++) {
        if (stats->class_counts[i] > max_count) max_count = stats->class_counts[i];
    }

    for (i = 0; i < BAR_COUNT; i++) {
        /* Row container */
        lv_obj_t *row = lv_obj_create(parent);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 32);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        /* Name label */
        s_bar_labels[i] = lv_label_create(row);
        lv_label_set_text(s_bar_labels[i], app_audio_class_name(i));
        lv_obj_set_style_text_font(s_bar_labels[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_bar_labels[i], ui_theme_get()->text_secondary, 0);
        lv_obj_set_width(s_bar_labels[i], 120);
        lv_obj_align(s_bar_labels[i], LV_ALIGN_LEFT_MID, 0, 0);

        /* Bar background */
        lv_obj_t *bar_bg = lv_obj_create(row);
        lv_obj_remove_style_all(bar_bg);
        lv_obj_set_size(bar_bg, LV_HOR_RES - 280, 22);
        lv_obj_set_style_radius(bar_bg, 6, 0);
        lv_obj_set_style_bg_color(bar_bg, ui_theme_get()->bar_bg, 0);
        lv_obj_set_style_bg_opa(bar_bg, LV_OPA_COVER, 0);
        lv_obj_clear_flag(bar_bg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(bar_bg, LV_ALIGN_LEFT_MID, 128, 0);

        /* Bar fill */
        s_bars[i] = lv_obj_create(bar_bg);
        lv_obj_remove_style_all(s_bars[i]);
        int bar_w = (int)((float)stats->class_counts[i] / (float)max_count * (float)(LV_HOR_RES - 290));
        if (bar_w < 4) bar_w = 4;
        lv_obj_set_size(s_bars[i], bar_w, 20);
        lv_obj_set_style_radius(s_bars[i], 5, 0);
        lv_obj_set_style_bg_color(s_bars[i], ui_theme_get()->accent, 0);
        lv_obj_set_style_bg_opa(s_bars[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(s_bars[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(s_bars[i], LV_ALIGN_LEFT_MID, 1, 0);

        /* Count number */
        char num[8];
        snprintf(num, sizeof(num), "%lu", (unsigned long)stats->class_counts[i]);
        s_bar_nums[i] = lv_label_create(row);
        lv_label_set_text(s_bar_nums[i], num);
        lv_obj_set_style_text_font(s_bar_nums[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_bar_nums[i], ui_theme_get()->text_muted, 0);
        lv_obj_align(s_bar_nums[i], LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

static void _refresh_bars(void)
{
    const app_audio_stats_t *stats = app_state_get_audio();
    uint32_t max_count = 1;
    int i;
    for (i = 0; i < BAR_COUNT; i++) {
        if (stats->class_counts[i] > max_count) max_count = stats->class_counts[i];
    }
    for (i = 0; i < BAR_COUNT && s_bars[i]; i++) {
        const ui_theme_t *t = ui_theme_get();
        int bar_w = (int)((float)stats->class_counts[i] / (float)max_count * (float)(LV_HOR_RES - 290));
        if (bar_w < 4) bar_w = 4;
        lv_obj_set_width(s_bars[i], bar_w);
        lv_obj_set_style_bg_color(s_bars[i], t->accent, 0);
        char num[8];
        snprintf(num, sizeof(num), "%lu", (unsigned long)stats->class_counts[i]);
        if (s_bar_nums[i]) lv_label_set_text(s_bar_nums[i], num);
        if (s_bar_labels[i]) lv_obj_set_style_text_color(s_bar_labels[i], t->text_secondary, 0);
    }
}

/* ─── Theme refresh ─── */
static void _theme_refresh(void)
{
    const ui_theme_t *t = ui_theme_get();
    if (!s_scr) return;

    lv_obj_set_style_bg_color(s_scr, t->bg, 0);

    /* Last card */
    lv_obj_t *last_parent = s_last_label ? lv_obj_get_parent(s_last_label) : NULL;
    if (last_parent) {
        lv_obj_set_style_bg_color(last_parent, t->card_bg, 0);
        lv_obj_set_style_border_color(last_parent, t->card_border, 0);
    }
    lv_obj_set_style_text_color(s_last_label, t->accent_green, 0);
    lv_obj_set_style_text_color(s_last_conf_label, t->text_secondary, 0);

    /* Total card */
    lv_obj_t *total_parent = s_total_label ? lv_obj_get_parent(s_total_label) : NULL;
    if (total_parent) {
        lv_obj_set_style_bg_color(total_parent, t->card_bg, 0);
        lv_obj_set_style_border_color(total_parent, t->card_border, 0);
    }
    lv_obj_set_style_text_color(s_total_label, t->text_secondary, 0);
    lv_obj_set_style_text_color(s_total_num_label, t->accent_yellow, 0);

    /* Model */
    lv_obj_set_style_text_color(s_model_label, t->text_muted, 0);

    _refresh_bars();
}

void ui_main_refresh(void)
{
    const app_audio_stats_t *stats = app_state_get_audio();
    const ui_theme_t *t = ui_theme_get();
    char buf[128];

    /* Last detection */
    if (s_last_label) {
        if (stats->last_class_id >= 0) {
            snprintf(buf, sizeof(buf), "Last: %s", app_audio_class_title(stats->last_class_id));
        } else {
            snprintf(buf, sizeof(buf), "Last: waiting for audio");
        }
        lv_label_set_text(s_last_label, buf);
        lv_obj_set_style_text_color(s_last_label, t->accent_green, 0);
    }
    if (s_last_conf_label) {
        if (stats->last_class_id >= 0) {
            snprintf(buf, sizeof(buf), "%d%%  t+%lus",
                     (int)(stats->last_confidence * 100.0f + 0.5f),
                     (unsigned long)(stats->last_timestamp_ms / 1000));
        } else {
            snprintf(buf, sizeof(buf), "0%%  t+0s");
        }
        lv_label_set_text(s_last_conf_label, buf);
    }

    /* Total */
    if (s_total_num_label) {
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats->total_events);
        lv_label_set_text(s_total_num_label, buf);
    }

    _refresh_bars();
}

void ui_main_create(lv_obj_t *scr)
{
    s_scr = scr;
    const ui_theme_t *t = ui_theme_get();

    ui_theme_apply_bg(scr);

    /* ── Title card ── */
    lv_obj_t *title_card = lv_obj_create(scr);
    lv_obj_remove_style_all(title_card);
    ui_theme_apply_card(title_card);
    lv_obj_set_size(title_card, LV_HOR_RES - 32, 96);
    lv_obj_set_pos(title_card, 16, 12);

    lv_obj_t *title = _make_title(title_card, "Audio Console");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, -8);

    lv_obj_t *sub = _make_subtitle(title_card, "System overview, model state, recognition totals");
    lv_obj_align(sub, LV_ALIGN_LEFT_MID, 8, 24);

    /* ── Two side-by-side cards ── */
    int card_w = (LV_HOR_RES - 48) / 2;
    int card_h = 100;
    int card_y = 120;

    /* Left: last detection */
    lv_obj_t *card_l = lv_obj_create(scr);
    lv_obj_remove_style_all(card_l);
    ui_theme_apply_card(card_l);
    lv_obj_set_size(card_l, card_w, card_h);
    lv_obj_set_pos(card_l, 16, card_y);

    s_last_label = lv_label_create(card_l);
    lv_obj_set_style_text_font(s_last_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_last_label, t->accent_green, 0);
    lv_label_set_text(s_last_label, "Last: waiting");
    lv_obj_align(s_last_label, LV_ALIGN_LEFT_MID, 8, -12);

    s_last_conf_label = lv_label_create(card_l);
    lv_obj_set_style_text_font(s_last_conf_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_last_conf_label, t->text_secondary, 0);
    lv_label_set_text(s_last_conf_label, "0%  t+0s");
    lv_obj_align(s_last_conf_label, LV_ALIGN_LEFT_MID, 8, 18);

    /* Right: total events */
    lv_obj_t *card_r = lv_obj_create(scr);
    lv_obj_remove_style_all(card_r);
    ui_theme_apply_card(card_r);
    lv_obj_set_size(card_r, card_w, card_h);
    lv_obj_set_pos(card_r, 16 + card_w + 16, card_y);

    s_total_label = lv_label_create(card_r);
    lv_label_set_text(s_total_label, "Total Events");
    lv_obj_set_style_text_font(s_total_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_total_label, t->text_secondary, 0);
    lv_obj_align(s_total_label, LV_ALIGN_LEFT_MID, 8, -12);

    s_total_num_label = lv_label_create(card_r);
    lv_label_set_text(s_total_num_label, "0");
    lv_obj_set_style_text_font(s_total_num_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_total_num_label, t->accent_yellow, 0);
    lv_obj_align(s_total_num_label, LV_ALIGN_LEFT_MID, 8, 18);

    /* ── Stats bar chart card ── */
    lv_obj_t *stats_card = lv_obj_create(scr);
    lv_obj_remove_style_all(stats_card);
    ui_theme_apply_card(stats_card);
    lv_obj_set_size(stats_card, LV_HOR_RES - 32, LV_VER_RES - 266);
    lv_obj_set_pos(stats_card, 16, card_y + card_h + 12);

    lv_obj_t *stats_title = _make_subtitle(stats_card, "Class Distribution");
    lv_obj_align(stats_title, LV_ALIGN_TOP_LEFT, 8, 6);

    lv_obj_t *bar_area = lv_obj_create(stats_card);
    lv_obj_remove_style_all(bar_area);
    lv_obj_set_size(bar_area, LV_PCT(100), LV_VER_RES - 316);
    lv_obj_set_style_pad_all(bar_area, 4, 0);
    lv_obj_set_style_border_width(bar_area, 0, 0);
    lv_obj_clear_flag(bar_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bar_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bar_area, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(bar_area, 2, 0);
    lv_obj_align(bar_area, LV_ALIGN_TOP_LEFT, 0, 36);

    _build_bars(bar_area);

    /* ── Footer model info ── */
    s_model_label = lv_label_create(scr);
    lv_label_set_text(s_model_label, MODEL_VERSION_TEXT "\n" MODEL_SIZE_TEXT);
    lv_obj_set_style_text_font(s_model_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_model_label, t->text_muted, 0);
    lv_obj_align(s_model_label, LV_ALIGN_BOTTOM_MID, 0, -6);

    /* Register theme hook */
    ui_main_theme_refresh = _theme_refresh;

    ui_main_refresh();
}
