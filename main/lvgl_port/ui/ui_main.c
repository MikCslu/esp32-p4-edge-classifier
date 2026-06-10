/*
 * ui_main.c — Audio Console: cards, bar charts, full theme refresh
 */
#include "lvgl.h"
#include "service/app_state.h"
#include "lvgl_port/ui/ui_theme.h"
#include <stdio.h>

#define BAR_COUNT     12
#define BAR_AREA_W    (LV_HOR_RES - 60)

static lv_obj_t *s_scr;
static lv_obj_t *s_title_card, *s_last_card, *s_total_card, *s_stats_card;
static lv_obj_t *s_last_label, *s_last_conf_label;
static lv_obj_t *s_total_num_label;
static lv_obj_t *s_model_label;
static lv_obj_t *s_title_main, *s_title_sub;
static lv_obj_t *s_stats_title;
static lv_obj_t *s_bar_bgs[BAR_COUNT];
static lv_obj_t *s_bars[BAR_COUNT];
static lv_obj_t *s_bar_labels[BAR_COUNT];
static lv_obj_t *s_bar_nums[BAR_COUNT];

/* ── Build ── */
static void _build_bars(lv_obj_t *parent)
{
    int i;
    for (i = 0; i < BAR_COUNT; i++) {
        int y = 32 + i * 26;
        s_bar_labels[i] = lv_label_create(parent);
        lv_label_set_text(s_bar_labels[i], app_audio_class_name(i));
        lv_obj_set_style_text_font(s_bar_labels[i], &lv_font_montserrat_14, 0);
        lv_obj_set_pos(s_bar_labels[i], 8, y);

        s_bar_bgs[i] = lv_obj_create(parent);
        lv_obj_remove_style_all(s_bar_bgs[i]);
        lv_obj_set_size(s_bar_bgs[i], BAR_AREA_W - 170, 20);
        lv_obj_set_style_radius(s_bar_bgs[i], 5, 0);
        lv_obj_set_style_bg_opa(s_bar_bgs[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(s_bar_bgs[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(s_bar_bgs[i], 128, y);

        s_bars[i] = lv_obj_create(parent);
        lv_obj_remove_style_all(s_bars[i]);
        lv_obj_set_size(s_bars[i], 4, 18);
        lv_obj_set_style_radius(s_bars[i], 4, 0);
        lv_obj_set_style_bg_opa(s_bars[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(s_bars[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(s_bars[i], 129, y + 1);

        s_bar_nums[i] = lv_label_create(parent);
        lv_label_set_text(s_bar_nums[i], "0");
        lv_obj_set_style_text_font(s_bar_nums[i], &lv_font_montserrat_14, 0);
        lv_obj_set_pos(s_bar_nums[i], BAR_AREA_W - 50, y);
    }
}

/* ── Refresh data ── */
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
        int bar_w = (int)((float)stats->class_counts[i] / (float)max_count * (float)(BAR_AREA_W - 180));
        if (bar_w < 4 && stats->class_counts[i] > 0) bar_w = 4;
        lv_obj_set_width(s_bars[i], bar_w);
        lv_obj_set_style_bg_color(s_bars[i], t->accent, 0);
        lv_obj_set_style_bg_color(s_bar_bgs[i], t->bar_bg, 0);
        lv_obj_set_style_text_color(s_bar_labels[i], t->text_secondary, 0);
        if (s_bar_nums[i]) {
            char num[8];
            snprintf(num, sizeof(num), "%lu", (unsigned long)stats->class_counts[i]);
            lv_label_set_text(s_bar_nums[i], num);
            lv_obj_set_style_text_color(s_bar_nums[i], t->text_muted, 0);
        }
    }
}

/* ── Full theme refresh ── */
static void _theme_refresh(void)
{
    const ui_theme_t *t = ui_theme_get();
    if (!s_scr) return;

    lv_obj_set_style_bg_color(s_scr, t->bg, 0);

    if (s_title_card) { lv_obj_set_style_bg_color(s_title_card, t->card_bg, 0); lv_obj_set_style_border_color(s_title_card, t->card_border, 0); }
    if (s_last_card)  { lv_obj_set_style_bg_color(s_last_card, t->card_bg, 0); lv_obj_set_style_border_color(s_last_card, t->card_border, 0); }
    if (s_total_card) { lv_obj_set_style_bg_color(s_total_card, t->card_bg, 0); lv_obj_set_style_border_color(s_total_card, t->card_border, 0); }
    if (s_stats_card){ lv_obj_set_style_bg_color(s_stats_card, t->card_bg, 0); lv_obj_set_style_border_color(s_stats_card, t->card_border, 0); }
    if (s_title_main) lv_obj_set_style_text_color(s_title_main, t->text_primary, 0);
    if (s_title_sub)  lv_obj_set_style_text_color(s_title_sub, t->text_secondary, 0);
    if (s_stats_title)lv_obj_set_style_text_color(s_stats_title, t->text_secondary, 0);
    if (s_last_label) lv_obj_set_style_text_color(s_last_label, t->accent_green, 0);
    if (s_last_conf_label) lv_obj_set_style_text_color(s_last_conf_label, t->text_muted, 0);
    if (s_total_num_label) lv_obj_set_style_text_color(s_total_num_label, t->accent_yellow, 0);
    if (s_model_label) lv_obj_set_style_text_color(s_model_label, t->text_muted, 0);
    _refresh_bars();
}

/* ── Public ── */
void ui_main_refresh(void)
{
    const app_audio_stats_t *stats = app_state_get_audio();
    char buf[128];

    if (s_last_label) {
        if (stats->last_class_id >= 0)
            snprintf(buf, sizeof(buf), "Last: %s", app_audio_class_title(stats->last_class_id));
        else
            snprintf(buf, sizeof(buf), "Last: waiting for audio");
        lv_label_set_text(s_last_label, buf);
    }
    if (s_last_conf_label) {
        snprintf(buf, sizeof(buf), "%d%%  t+%lus",
                 stats->last_class_id >= 0 ? (int)(stats->last_confidence * 100.0f + 0.5f) : 0,
                 stats->last_class_id >= 0 ? (unsigned long)(stats->last_timestamp_ms / 1000) : 0UL);
        lv_label_set_text(s_last_conf_label, buf);
    }
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
    int card_w = (LV_HOR_RES - 48) / 2;

    ui_theme_apply_bg(scr);
    ui_theme_create_toggle_btn(scr);

    /* Title card */
    s_title_card = lv_obj_create(scr);
    lv_obj_remove_style_all(s_title_card);
    ui_theme_apply_card(s_title_card);
    lv_obj_set_pos(s_title_card, 12, 8);
    lv_obj_set_size(s_title_card, LV_HOR_RES - 98, 78);

    s_title_main = lv_label_create(s_title_card);
    lv_label_set_text(s_title_main, "Audio Console");
    lv_obj_set_style_text_font(s_title_main, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_title_main, t->text_primary, 0);
    lv_obj_align(s_title_main, LV_ALIGN_LEFT_MID, 12, -6);

    s_title_sub = lv_label_create(s_title_card);
    lv_label_set_text(s_title_sub, "System overview and recognition totals");
    lv_obj_set_style_text_font(s_title_sub, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_title_sub, t->text_secondary, 0);
    lv_obj_align(s_title_sub, LV_ALIGN_LEFT_MID, 12, 22);

    /* Last detection card */
    s_last_card = lv_obj_create(scr);
    lv_obj_remove_style_all(s_last_card);
    ui_theme_apply_card(s_last_card);
    lv_obj_set_pos(s_last_card, 12, 96);
    lv_obj_set_size(s_last_card, card_w, 86);

    lv_obj_t *t1 = lv_label_create(s_last_card);
    lv_label_set_text(t1, "Last Detection");
    lv_obj_set_style_text_font(t1, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(t1, t->text_secondary, 0);
    lv_obj_set_pos(t1, 8, 6);

    s_last_label = lv_label_create(s_last_card);
    lv_label_set_text(s_last_label, "waiting for audio");
    lv_obj_set_style_text_font(s_last_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_last_label, t->accent_green, 0);
    lv_obj_set_pos(s_last_label, 8, 34);

    s_last_conf_label = lv_label_create(s_last_card);
    lv_label_set_text(s_last_conf_label, "0%  t+0s");
    lv_obj_set_style_text_font(s_last_conf_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_last_conf_label, t->text_muted, 0);
    lv_obj_set_pos(s_last_conf_label, 8, 60);

    /* Total events card */
    s_total_card = lv_obj_create(scr);
    lv_obj_remove_style_all(s_total_card);
    ui_theme_apply_card(s_total_card);
    lv_obj_set_pos(s_total_card, 12 + card_w + 12, 96);
    lv_obj_set_size(s_total_card, card_w, 86);

    lv_obj_t *t2 = lv_label_create(s_total_card);
    lv_label_set_text(t2, "Total Events");
    lv_obj_set_style_text_font(t2, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(t2, t->text_secondary, 0);
    lv_obj_set_pos(t2, 8, 6);

    s_total_num_label = lv_label_create(s_total_card);
    lv_label_set_text(s_total_num_label, "0");
    lv_obj_set_style_text_font(s_total_num_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_total_num_label, t->accent_yellow, 0);
    lv_obj_set_pos(s_total_num_label, 8, 28);

    /* Stats card */
    s_stats_card = lv_obj_create(scr);
    lv_obj_remove_style_all(s_stats_card);
    ui_theme_apply_card(s_stats_card);
    lv_obj_set_pos(s_stats_card, 12, 192);
    lv_obj_set_size(s_stats_card, LV_HOR_RES - 24, LV_VER_RES - 260);

    s_stats_title = lv_label_create(s_stats_card);
    lv_label_set_text(s_stats_title, "Class Distribution");
    lv_obj_set_style_text_font(s_stats_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_stats_title, t->text_secondary, 0);
    lv_obj_set_pos(s_stats_title, 8, 4);

    _build_bars(s_stats_card);

    /* Footer */
    s_model_label = lv_label_create(scr);
    lv_label_set_text(s_model_label, "MobileNetV2 Audio 12-class  |  ESP-DL 761856 bytes");
    lv_obj_set_style_text_font(s_model_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_model_label, t->text_muted, 0);
    lv_obj_set_pos(s_model_label, 16, LV_VER_RES - 34);

    ui_main_theme_refresh = _theme_refresh;
    ui_main_refresh();
}
