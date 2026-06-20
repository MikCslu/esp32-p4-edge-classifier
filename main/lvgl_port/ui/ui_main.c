/*
 * Portrait audio dashboard for the 480x800 Waveshare display.
 */
#include "lvgl.h"
#include "service/app_state.h"
#include "lvgl_port/ui/ui_theme.h"
#include <stdio.h>

#define CLASS_COUNT 12
#define PAGE_PAD    16
#define BAR_W       (LV_HOR_RES - 190)

static lv_obj_t *s_scr;
static lv_obj_t *s_title;
static lv_obj_t *s_last_card;
static lv_obj_t *s_last_name;
static lv_obj_t *s_last_meta;
static lv_obj_t *s_total_card;
static lv_obj_t *s_total_value;
static lv_obj_t *s_top_card;
static lv_obj_t *s_top_name;
static lv_obj_t *s_top_meta;
static lv_obj_t *s_bars_card;
static lv_obj_t *s_bar_labels[CLASS_COUNT];
static lv_obj_t *s_bar_tracks[CLASS_COUNT];
static lv_obj_t *s_bar_fills[CLASS_COUNT];
static lv_obj_t *s_bar_values[CLASS_COUNT];
static lv_obj_t *s_model_label;

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    ui_theme_apply_card(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    return obj;
}

static void apply_text(lv_obj_t *obj, const lv_font_t *font, lv_color_t color)
{
    if (!obj) return;
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, color, 0);
}

static void refresh_theme(void)
{
    if (!s_scr) return;
    const ui_theme_t *t = ui_theme_get();
    lv_obj_set_style_bg_color(s_scr, t->bg, 0);
    if (s_title) apply_text(s_title, &lv_font_montserrat_28, t->text_primary);
    if (s_last_card) ui_theme_apply_card(s_last_card);
    if (s_total_card) ui_theme_apply_card(s_total_card);
    if (s_top_card) ui_theme_apply_card(s_top_card);
    if (s_bars_card) ui_theme_apply_card(s_bars_card);
    if (s_model_label) apply_text(s_model_label, &lv_font_montserrat_14, t->text_muted);
}

static int find_top_class(const app_audio_stats_t *stats)
{
    int best = -1;
    uint32_t best_count = 0;
    for (int i = 0; i < CLASS_COUNT; i++) {
        if (stats->class_counts[i] > best_count) {
            best_count = stats->class_counts[i];
            best = i;
        }
    }
    return best;
}

void ui_main_refresh(void)
{
    const app_audio_stats_t *stats = app_state_get_audio();
    const ui_theme_t *t = ui_theme_get();
    char buf[96];

    refresh_theme();

    if (s_last_name) {
        if (stats->last_class_id >= 0) {
            lv_label_set_text(s_last_name, app_audio_class_title(stats->last_class_id));
        } else {
            lv_label_set_text(s_last_name, "Listening");
        }
        apply_text(s_last_name, &lv_font_montserrat_28,
                   stats->last_class_id >= 0 ? t->accent_green : t->text_primary);
    }
    if (s_last_meta) {
        snprintf(buf, sizeof(buf), "%d%%  +%lus",
                 stats->last_class_id >= 0 ? (int)(stats->last_confidence * 100.0f + 0.5f) : 0,
                 stats->last_class_id >= 0 ? (unsigned long)(stats->last_timestamp_ms / 1000) : 0UL);
        lv_label_set_text(s_last_meta, buf);
        apply_text(s_last_meta, &lv_font_montserrat_24, t->text_muted);
    }
    if (s_total_value) {
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats->total_events);
        lv_label_set_text(s_total_value, buf);
        apply_text(s_total_value, &lv_font_montserrat_48, t->accent_yellow);
    }

    int top = find_top_class(stats);
    if (s_top_name) {
        lv_label_set_text(s_top_name, top >= 0 ? app_audio_class_title(top) : "--");
        apply_text(s_top_name, &lv_font_montserrat_28, t->accent);
    }
    if (s_top_meta) {
        snprintf(buf, sizeof(buf), "Top class  %lu events",
                 top >= 0 ? (unsigned long)stats->class_counts[top] : 0UL);
        lv_label_set_text(s_top_meta, buf);
        apply_text(s_top_meta, &lv_font_montserrat_24, t->text_muted);
    }

    uint32_t max_count = 1;
    for (int i = 0; i < CLASS_COUNT; i++) {
        if (stats->class_counts[i] > max_count) max_count = stats->class_counts[i];
    }

    for (int i = 0; i < CLASS_COUNT; i++) {
        int fill = (int)((float)stats->class_counts[i] / (float)max_count * (float)BAR_W);
        if (fill < 4 && stats->class_counts[i] > 0) fill = 4;
        if (fill < 4) fill = 4;
        lv_obj_set_width(s_bar_fills[i], fill);
        lv_obj_set_style_bg_color(s_bar_tracks[i], t->bar_bg, 0);
        lv_obj_set_style_bg_color(s_bar_fills[i],
                                  app_audio_class_needs_attention(i) ? t->danger : t->accent, 0);
        apply_text(s_bar_labels[i], &lv_font_montserrat_14, t->text_secondary);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats->class_counts[i]);
        lv_label_set_text(s_bar_values[i], buf);
        apply_text(s_bar_values[i], &lv_font_montserrat_14, t->text_muted);
    }
}

void ui_main_create(lv_obj_t *scr)
{
    s_scr = scr;
    const ui_theme_t *t = ui_theme_get();
    ui_theme_apply_bg(scr);

    s_title = lv_label_create(scr);
    lv_label_set_text(s_title, "Audio");
    lv_obj_set_pos(s_title, PAGE_PAD, 18);
    apply_text(s_title, &lv_font_montserrat_28, t->text_primary);

    ui_theme_create_toggle_btn(scr);

    s_last_card = make_card(scr, PAGE_PAD, 64, LV_HOR_RES - PAGE_PAD * 2, 108);
    s_last_name = lv_label_create(s_last_card);
    lv_obj_set_pos(s_last_name, 16, 18);
    s_last_meta = lv_label_create(s_last_card);
    lv_obj_set_pos(s_last_meta, 16, 62);

    int half = (LV_HOR_RES - PAGE_PAD * 3) / 2;
    s_total_card = make_card(scr, PAGE_PAD, 188, half, 122);
    lv_obj_t *total_caption = lv_label_create(s_total_card);
    lv_label_set_text(total_caption, "Total");
    lv_obj_set_pos(total_caption, 14, 12);
    apply_text(total_caption, &lv_font_montserrat_24, t->text_secondary);
    s_total_value = lv_label_create(s_total_card);
    lv_obj_set_pos(s_total_value, 14, 46);

    s_top_card = make_card(scr, PAGE_PAD * 2 + half, 188, half, 122);
    s_top_name = lv_label_create(s_top_card);
    lv_obj_set_width(s_top_name, half - 28);
    lv_label_set_long_mode(s_top_name, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(s_top_name, 14, 24);
    s_top_meta = lv_label_create(s_top_card);
    lv_obj_set_width(s_top_meta, half - 28);
    lv_label_set_long_mode(s_top_meta, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(s_top_meta, 14, 70);

    s_bars_card = make_card(scr, PAGE_PAD, 326, LV_HOR_RES - PAGE_PAD * 2, 392);
    for (int i = 0; i < CLASS_COUNT; i++) {
        int y = 14 + i * 31;
        s_bar_labels[i] = lv_label_create(s_bars_card);
        lv_label_set_text(s_bar_labels[i], app_audio_class_name(i));
        lv_obj_set_width(s_bar_labels[i], 86);
        lv_label_set_long_mode(s_bar_labels[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_pos(s_bar_labels[i], 12, y + 2);

        s_bar_tracks[i] = lv_obj_create(s_bars_card);
        lv_obj_remove_style_all(s_bar_tracks[i]);
        lv_obj_set_size(s_bar_tracks[i], BAR_W, 12);
        lv_obj_set_style_radius(s_bar_tracks[i], 6, 0);
        lv_obj_set_style_bg_opa(s_bar_tracks[i], LV_OPA_COVER, 0);
        lv_obj_set_pos(s_bar_tracks[i], 104, y + 7);

        s_bar_fills[i] = lv_obj_create(s_bars_card);
        lv_obj_remove_style_all(s_bar_fills[i]);
        lv_obj_set_size(s_bar_fills[i], 4, 12);
        lv_obj_set_style_radius(s_bar_fills[i], 6, 0);
        lv_obj_set_style_bg_opa(s_bar_fills[i], LV_OPA_COVER, 0);
        lv_obj_set_pos(s_bar_fills[i], 104, y + 7);

        s_bar_values[i] = lv_label_create(s_bars_card);
        lv_obj_set_width(s_bar_values[i], 38);
        lv_obj_set_pos(s_bar_values[i], LV_HOR_RES - 88, y + 2);
    }

    s_model_label = lv_label_create(scr);
    lv_label_set_text(s_model_label, "ESP-DL audio model");
    lv_obj_set_pos(s_model_label, PAGE_PAD, LV_VER_RES - 28);

    ui_main_theme_refresh = refresh_theme;
    ui_main_refresh();
}
