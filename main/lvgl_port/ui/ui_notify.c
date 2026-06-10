/*
 * ui_notify.c — System notification popup, theme-aware
 */
#include "lvgl_port/ui/ui_notify.h"
#include "lvgl_port/ui/ui_theme.h"
#include "lvgl.h"

static lv_obj_t *s_notify_box;
static lv_obj_t *s_notify_label;
static lv_obj_t *s_notify_bar;  /* left accent stripe */
static lv_timer_t *s_notify_timer;

static void notify_timeout_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_notify_box) {
        lv_obj_del(s_notify_box);
        s_notify_box = NULL;
        s_notify_label = NULL;
        s_notify_bar = NULL;
    }
    if (s_notify_timer) {
        lv_timer_del(s_notify_timer);
        s_notify_timer = NULL;
    }
}

static void notify_x_anim(void *obj, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)obj, value);
}

static void notify_opa_anim(void *obj, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)value, 0);
}

void ui_notify_show(const char *text, uint32_t duration_ms)
{
    const ui_theme_t *t = ui_theme_get();

    if (!text || text[0] == '\0') return;

    if (s_notify_box) {
        lv_obj_del(s_notify_box);
        s_notify_box = NULL;
        s_notify_label = NULL;
        s_notify_bar = NULL;
    }
    if (s_notify_timer) {
        lv_timer_del(s_notify_timer);
        s_notify_timer = NULL;
    }

    s_notify_box = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_notify_box);
    lv_obj_set_size(s_notify_box, 300, 48);
    lv_obj_set_style_radius(s_notify_box, 10, 0);
    lv_obj_set_style_bg_color(s_notify_box, t->card_bg, 0);
    lv_obj_set_style_bg_opa(s_notify_box, (lv_opa_t)235, 0);
    lv_obj_set_style_border_width(s_notify_box, 1, 0);
    lv_obj_set_style_border_color(s_notify_box, t->card_border, 0);
    lv_obj_set_style_pad_hor(s_notify_box, 16, 0);
    lv_obj_set_style_pad_ver(s_notify_box, 10, 0);
    lv_obj_clear_flag(s_notify_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_notify_box, LV_ALIGN_TOP_LEFT, 16, 16);
    lv_obj_set_style_opa(s_notify_box, LV_OPA_TRANSP, 0);

    /* Left accent bar */
    s_notify_bar = lv_obj_create(s_notify_box);
    lv_obj_remove_style_all(s_notify_bar);
    lv_obj_set_size(s_notify_bar, 4, 28);
    lv_obj_set_style_radius(s_notify_bar, 2, 0);
    lv_obj_set_style_bg_color(s_notify_bar, t->accent, 0);
    lv_obj_set_style_bg_opa(s_notify_bar, LV_OPA_COVER, 0);
    lv_obj_align(s_notify_bar, LV_ALIGN_LEFT_MID, 2, 0);

    s_notify_label = lv_label_create(s_notify_box);
    lv_label_set_text(s_notify_label, text);
    lv_obj_set_style_text_font(s_notify_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_notify_label, t->text_primary, 0);
    lv_obj_set_width(s_notify_label, 264);
    lv_label_set_long_mode(s_notify_label, LV_LABEL_LONG_CLIP);
    lv_obj_align(s_notify_label, LV_ALIGN_CENTER, 0, 0);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_notify_box);
    lv_anim_set_exec_cb(&anim, notify_x_anim);
    lv_anim_set_values(&anim, -28, 16);
    lv_anim_set_time(&anim, 240);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_notify_box);
    lv_anim_set_exec_cb(&anim, notify_opa_anim);
    lv_anim_set_values(&anim, 0, 240);
    lv_anim_set_time(&anim, 240);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);

    s_notify_timer = lv_timer_create(notify_timeout_cb,
                                     duration_ms > 0 ? duration_ms : 2800, NULL);
    lv_timer_set_repeat_count(s_notify_timer, 1);
}
