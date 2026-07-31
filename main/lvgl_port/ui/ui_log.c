/*
 * Landscape recognition timeline.
 */
#include "lvgl.h"
#include "service/app_state.h"
#include "service/history_service.h"
#include "lvgl_port/ui/ui_theme.h"
#include "esp_log.h"
#include <stdio.h>

#define MAX_ROWS 10   /* 一屏最多显示 10 条历史记录 */
#define PAGE_PAD 16
#define ROW_H    36

static lv_obj_t *s_scr;
static lv_obj_t *s_title;
static lv_obj_t *s_clear_btn;
static lv_obj_t *s_empty;
static lv_obj_t *s_panel;
/* 预创建的行对象池：固定创建，刷新时只改内容/显隐，
 * 避免反复创建销毁控件（嵌入式 UI 的性能准则）。 */
static lv_obj_t *s_rows[MAX_ROWS];
static lv_obj_t *s_dot[MAX_ROWS];
static lv_obj_t *s_name[MAX_ROWS];
static lv_obj_t *s_meta[MAX_ROWS];

/* 刷新历史列表：从 NVS 读取最近记录，填进预创建的行 */
void ui_log_refresh(void);

/* 每个音频类别给一个专属颜色（用于圆点和文字） */
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
    if (s_clear_btn) {
        lv_obj_set_style_bg_color(s_clear_btn, t->card_bg, 0);
        lv_obj_set_style_border_color(s_clear_btn, t->danger, 0);
    }
    if (s_empty) {
        lv_obj_set_style_text_color(s_empty, t->text_muted, 0);
    }
}

/* "Clear" 按钮回调：清空 NVS 历史 + 内存统计 */
static void clear_cb(lv_event_t *e)
{
    (void)e;
    ESP_ERROR_CHECK_WITHOUT_ABORT(history_clear());
    app_state_clear_audio();
    ui_log_refresh();
}

/* 刷新历史列表：从 NVS 读取最近记录，填进预创建的行 */
void ui_log_refresh(void)
{
    const ui_theme_t *t = ui_theme_get();
    history_record_t records[MAX_ROWS];
    char buf[64];

    refresh_theme();

    int count = (int)history_get_recent(records, MAX_ROWS);
    if (s_empty) {
        if (count == 0) {
            lv_obj_clear_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
        }
    }

    for (int i = 0; i < MAX_ROWS; i++) {
        if (i >= count) {
            lv_obj_add_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        const history_record_t *item = &records[i];
        lv_obj_clear_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
        /* 斑马纹：奇数行用 bar_bg 颜色，偶数行用 card_bg */
        lv_obj_set_style_bg_color(s_rows[i], (i % 2) ? t->bar_bg : t->card_bg, 0);
        lv_obj_set_style_bg_color(s_dot[i], class_color(item->class_id), 0);

        lv_label_set_text(s_name[i], app_audio_class_title(item->class_id));
        lv_obj_set_style_text_color(s_name[i], class_color(item->class_id), 0);
        lv_obj_set_style_text_font(s_name[i], &lv_font_montserrat_24, 0);

        snprintf(buf, sizeof(buf), "+%lus  %u%%",
                 (unsigned long)(item->timestamp_ms / 1000),
                 item->confidence_pct);
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

    s_clear_btn = lv_btn_create(scr);
    lv_obj_remove_style_all(s_clear_btn);
    lv_obj_set_size(s_clear_btn, 88, 36);
    lv_obj_set_pos(s_clear_btn, LV_HOR_RES - PAGE_PAD - 88, 20);
    lv_obj_set_style_radius(s_clear_btn, 10, 0);
    lv_obj_set_style_bg_color(s_clear_btn, ui_theme_get()->card_bg, 0);
    lv_obj_set_style_bg_opa(s_clear_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_clear_btn, 1, 0);
    lv_obj_set_style_border_color(s_clear_btn, ui_theme_get()->danger, 0);
    lv_obj_add_event_cb(s_clear_btn, clear_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *clear_label = lv_label_create(s_clear_btn);
    lv_label_set_text(clear_label, "Clear");
    lv_obj_set_style_text_font(clear_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(clear_label, ui_theme_get()->danger, 0);
    lv_obj_center(clear_label);

    ui_theme_create_toggle_btn(scr);

    s_panel = lv_obj_create(scr);
    lv_obj_remove_style_all(s_panel);
    ui_theme_apply_card(s_panel);
    lv_obj_set_pos(s_panel, PAGE_PAD, 64);
    lv_obj_set_size(s_panel, LV_HOR_RES - PAGE_PAD * 2, LV_VER_RES - 86);

    s_empty = lv_label_create(s_panel);
    lv_label_set_text(s_empty, "No history");
    lv_obj_set_style_text_font(s_empty, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_empty, ui_theme_get()->text_muted, 0);
    lv_obj_center(s_empty);

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
