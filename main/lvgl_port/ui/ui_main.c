/*
 * ui_main.c — Audio Console: cards, bar charts, theme-aware (no flex)
 */
#include "lvgl.h"
#include "service/app_state.h"
#include "lvgl_port/ui/ui_theme.h"
#include <stdio.h>

#define MODEL_VERSION_TEXT "MobileNetV2 Audio 12-class"
#define MODEL_SIZE_TEXT    "ESP-DL 761856 bytes"
#define BAR_COUNT          12
#define BAR_AREA_W         (LV_HOR_RES - 60)

static lv_obj_t *s_scr;
static lv_obj_t *s_last_label, *s_last_conf_label;
static lv_obj_t *s_total_label, *s_total_num_label;
static lv_obj_t *s_model_label;
static lv_obj_t *s_bar_bgs[BAR_COUNT];
static lv_obj_t *s_bars[BAR_COUNT];
static lv_obj_t *s_bar_labels[BAR_COUNT];
static lv_obj_t *s_bar_nums[BAR_COUNT];

/* ── Helpers ── */
static lv_obj_t *_card_create(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    ui_theme_apply_card(c);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    return c;
}

static lv_obj_t *_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    if (x >= 0 || y >= 0) lv_obj_set_pos(l, x, y);
    return l;
}

/* ── Bar chart ── */
static void _build_bars(lv_obj_t *parent)
{
    int y_off = 30;
    int i;
    for (i = 0; i < BAR_COUNT; i++) {
        int y = y_off + i * 26;

        s_bar_labels[i] = _label(parent, app_audio_class_name(i),
                                 &lv_font_montserrat_14,
                                 ui_theme_get()->text_secondary, 8, y);

        s_bar_bgs[i] = lv_obj_create(parent);
        lv_obj_remove_style_all(s_bar_bgs[i]);
        lv_obj_set_size(s_bar_bgs[i], BAR_AREA_W - 180, 20);
        lv_obj_set_style_radius(s_bar_bgs[i], 5, 0);
        lv_obj_set_style_bg_color(s_bar_bgs[i], ui_theme_get()->bar_bg, 0);
        lv_obj_set_style_bg_opa(s_bar_bgs[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(s_bar_bgs[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(s_bar_bgs[i], 130, y);

        s_bars[i] = lv_obj_create(parent);
        lv_obj_remove_style_all(s_bars[i]);
        lv_obj_set_size(s_bars[i], 4, 18);
        lv_obj_set_style_radius(s_bars[i], 4, 0);
        lv_obj_set_style_bg_color(s_bars[i], ui_theme_get()->accent, 0);
        lv_obj_set_style_bg_opa(s_bars[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(s_bars[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(s_bars[i], 131, y + 1);

        s_bar_nums[i] = _label(parent, "0",
                               &lv_font_montserrat_14,
                               ui_theme_get()->text_muted,
                               BAR_AREA_W - 50, y);
    }
}

static void _refresh_bars(void)
{
    const app_audio_stats_t *stats = app_state_get_audio();
    const ui_theme_t *t = ui_theme_get();
    uint32_t max_count = 1;
    int i;
    for (i = 0; i < BAR_COUNT; i++) {
        if (stats->class_counts[i] > max_count) max_count = stats->class_counts[i];
    }
    for (i = 0; i < BAR_COUNT; i++) {
        if (!s_bars[i]) continue;
        int bar_w = (int)((float)stats->class_counts[i] / (float)max_count * (float)(BAR_AREA_W - 190));
        if (bar_w < 4 && stats->class_counts[i] > 0) bar_w = 4;
        lv_obj_set_width(s_bars[i], bar_w);
        lv_obj_set_style_bg_color(s_bars[i], t->accent, 0);
        if (s_bar_bgs[i]) lv_obj_set_style_bg_color(s_bar_bgs[i], t->bar_bg, 0);
        if (s_bar_labels[i]) lv_obj_set_style_text_color(s_bar_labels[i], t->text_secondary, 0);
        if (s_bar_nums[i]) {
            char num[8];
            snprintf(num, sizeof(num), "%lu", (unsigned long)stats->class_counts[i]);
            lv_label_set_text(s_bar_nums[i], num);
        }
    }
}

/* ── Theme refresh ── */
static void _theme_refresh(void)
{
    const ui_theme_t *t = ui_theme_get();
    if (!s_scr) return;
    lv_obj_set_style_bg_color(s_scr, t->bg, 0);
    if (s_last_label) lv_obj_set_style_text_color(s_last_label, t->accent_green, 0);
    if (s_model_label) lv_obj_set_style_text_color(s_model_label, t->text_muted, 0);
    _refresh_bars();
}

/* ── Public ── */
void ui_main_refresh(void)
{
    const app_audio_stats_t *stats = app_state_get_audio();
    const ui_theme_t *t = ui_theme_get();
    char buf[128];

    if (s_last_label) {
        if (stats->last_class_id >= 0)
            snprintf(buf, sizeof(buf), "Last: %s", app_audio_class_title(stats->last_class_id));
        else
            snprintf(buf, sizeof(buf), "Last: waiting for audio");
        lv_label_set_text(s_last_label, buf);
    }
    if (s_last_conf_label) {
        if (stats->last_class_id >= 0)
            snprintf(buf, sizeof(buf), "%d%%  t+%lus",
                     (int)(stats->last_confidence * 100.0f + 0.5f),
                     (unsigned long)(stats->last_timestamp_ms / 1000));
        else
            snprintf(buf, sizeof(buf), "0%%  t+0s");
        lv_label_set_text(s_last_conf_label, buf);
    }
    if (s_total_num_label) {
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats->total_events);
        lv_label_set_text(s_total_num_label, buf);
        lv_obj_set_style_text_color(s_total_num_label, t->accent_yellow, 0);
    }
    _refresh_bars();
}

void ui_main_create(lv_obj_t *scr)
{
    s_scr = scr;
    const ui_theme_t *t = ui_theme_get();
    int card_w = (LV_HOR_RES - 48) / 2;

    ui_theme_apply_bg(scr);

    /* Title card */
    lv_obj_t *tc = _card_create(scr, 16, 12, LV_HOR_RES - 32, 96);
    _label(tc, "Audio Console", &lv_font_montserrat_48, t->text_primary, 12, 6);
    _label(tc, "System overview, model state, recognition totals",
           &lv_font_montserrat_24, t->text_secondary, 12, 52);

    /* Last detection card */
    lv_obj_t *lc = _card_create(scr, 16, 120, card_w, 100);
    _label(lc, "Last Detection", &lv_font_montserrat_24, t->text_secondary, 8, 6);
    s_last_label = _label(lc, "waiting for audio",
                          &lv_font_montserrat_28, t->accent_green, 8, 36);
    s_last_conf_label = _label(lc, "0%  t+0s",
                               &lv_font_montserrat_24, t->text_muted, 8, 66);

    /* Total events card */
    lv_obj_t *rc = _card_create(scr, 16 + card_w + 16, 120, card_w, 100);
    _label(rc, "Total Events", &lv_font_montserrat_24, t->text_secondary, 8, 6);
    s_total_label = _label(rc, "", &lv_font_montserrat_14, t->text_secondary, 8, 6);  /* unused but kept for ref */
    s_total_num_label = _label(rc, "0", &lv_font_montserrat_48, t->accent_yellow, 8, 30);

    /* Stats card */
    lv_obj_t *sc = _card_create(scr, 16, 232, LV_HOR_RES - 32, 332);
    _label(sc, "Class Distribution", &lv_font_montserrat_24, t->text_secondary, 8, 4);
    _build_bars(sc);

    /* Footer */
    s_model_label = _label(scr, MODEL_VERSION_TEXT "\n" MODEL_SIZE_TEXT,
                           &lv_font_montserrat_24, t->text_muted, 16, LV_VER_RES - 48);

    ui_main_theme_refresh = _theme_refresh;
    ui_main_refresh();
}
