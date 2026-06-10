/*
 * ui_theme.h — Light/Dark dual-theme system
 */
#ifndef UI_THEME_H
#define UI_THEME_H

#include "lvgl.h"

typedef enum { UI_THEME_DARK = 0, UI_THEME_LIGHT = 1 } ui_theme_id_t;

typedef struct {
    lv_color_t bg;
    lv_color_t card_bg;
    lv_color_t card_border;
    lv_color_t text_primary;
    lv_color_t text_secondary;
    lv_color_t text_muted;
    lv_color_t accent;
    lv_color_t accent_alt;
    lv_color_t accent_green;
    lv_color_t accent_yellow;
    lv_color_t slider_track;
    lv_color_t slider_fill;
    lv_color_t bar_bg;
    lv_color_t danger;
    lv_color_t success;
    uint8_t     card_radius;
    uint8_t     border_width;
} ui_theme_t;

void ui_theme_init(void);
void ui_theme_toggle(void);
ui_theme_id_t ui_theme_get_id(void);
const ui_theme_t *ui_theme_get(void);

static inline bool ui_theme_is_dark(void)  { return ui_theme_get_id() == UI_THEME_DARK; }
static inline bool ui_theme_is_light(void) { return ui_theme_get_id() == UI_THEME_LIGHT; }

/* Helpers */
void ui_theme_apply_bg(lv_obj_t *obj);
void ui_theme_apply_card(lv_obj_t *obj);
lv_obj_t *ui_theme_create_toggle_btn(lv_obj_t *parent);  /* small btn, top-right */

/* Per-page refresh hooks */
extern void (*ui_main_theme_refresh)(void);
extern void (*ui_log_theme_refresh)(void);
extern void (*ui_settings_theme_refresh)(void);
extern void (*ui_emotion_theme_refresh)(void);

#endif
