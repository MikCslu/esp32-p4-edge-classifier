/*
 * Eyes-only emotion engine — giant eyes fill the screen.
 * Toast, camera preview, and labels preserved.
 */

#include "lvgl_port/ui/ui_emotion.h"
#include "lvgl_port/ui/ui_theme.h"
#include "service/app_state.h"
#include "service/camera_service.h"
#include "service/visual_classify_service.h"
#include "esp_log.h"
#include "esp_random.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "EMOTION";

/*
 * 表情引擎（LVGL 动画/定时器/相机预览的综合应用，面试重点）
 * ====================================================
 * 核心思想：用两个"眼睛"形状的 lv_obj 表达设备情绪。
 *  - 场景表(scene)：每种音频/视觉类别对应一组 眼睛形状+背景色+文案；
 *  - _morph_to()：用 lv_anim 把眼睛从当前形状"形变"到目标形状；
 *  - 多个 lv_timer 驱动：眨眼、紧急脉冲、自动复位、相机预览；
 *  - 相机预览：lv_timer 每 33ms 从 camera_service 拉一帧缩略图，
 *    用 lv_image 显示在角落（只在这个页面开启，省 CPU）。
 */

/* Forward decl */
static void _emotion_theme_refresh(void);

/* 表情自动复位时间：4.5 秒无新事件就回到"Listening"待机态 */
#define EMOTION_AUTO_CLEAR_MS    4500
/* 眨眼间隔：2.4~4.2 秒随机，模拟自然的眨眼节奏 */
#define EMOTION_BLINK_MAX_MS     4200
#define EMOTION_BLINK_HOLD_MS    120
#define MORPH_DURATION_MS        280
#define PULSE_DURATION_MS        320
#define PULSE_COUNT              3
#define TOAST_COUNT              4
#define TOAST_HIDE_MS            3200
#define CAMERA_PREVIEW_PULL_MS   33

#define EYE_L_CX        (LV_HOR_RES / 2 - 92)
#define EYE_R_CX        (LV_HOR_RES / 2 + 92)
#define EYE_CY          (LV_VER_RES / 2)
#define EYE_DIAMETER    150
#define HL_SIZE         26

#define AUDIO_CLASS_ALARM        0
#define AUDIO_CLASS_CAR_HORN     1
#define AUDIO_CLASS_KNOCK        2
#define AUDIO_CLASS_CLAPPING     3
#define AUDIO_CLASS_DOG_BARK     4
#define AUDIO_CLASS_FOOTSTEPS    5
#define AUDIO_CLASS_GLASS_BREAK  6
#define AUDIO_CLASS_DOORBELL     7
#define AUDIO_CLASS_CRYING_BABY  8
#define AUDIO_CLASS_ENGINE       9
#define AUDIO_CLASS_TRAFFIC      10
#define AUDIO_CLASS_BACKGROUND   11

typedef struct { int16_t w, h, radius, y_off; lv_color_t color; } eye_shape_t;

typedef struct {
    eye_shape_t eye_l, eye_r;
    lv_color_t  bg_color, accent;
    emotion_t   emotion;
    bool        urgent;
    const char *title, *subtitle;
} emotion_scene_t;

/* 关键对象：根容器、左右眼、左右高光；其余是文案/提示条/相机面板 */
static lv_obj_t *s_root, *s_eye_l, *s_eye_r, *s_hl_l, *s_hl_r;
static lv_obj_t *s_title_label, *s_subtitle_label;
static lv_obj_t *s_toasts[TOAST_COUNT], *s_toast_labels[TOAST_COUNT];
static char s_toast_text[TOAST_COUNT][96];
static lv_timer_t *s_toast_hide_timer;
static lv_obj_t *s_camera_panel, *s_camera_image, *s_camera_status;
static lv_image_dsc_t s_camera_img_dsc;
static uint32_t s_camera_last_sequence;
static lv_timer_t *s_camera_preview_timer;
static bool s_camera_live_shown;
static lv_timer_t *s_clear_timer, *s_blink_timer, *s_blink_restore_timer, *s_pulse_timer;
static const emotion_scene_t *s_active_scene;
static emotion_t s_current = EMOTION_IDLE;
static float s_last_confidence;
static bool s_blinking;
static int s_last_audio_class = -1;

/* ─── Scenes (dark) ─── */

#define _C(r,g,b) LV_COLOR_MAKE(r,g,b)
#define EYE(d,c) {d,d,d/2,0,_C c}

/* ---------- 场景定义（深色主题）----------
 * 每个场景 = 眼睛几何 + 颜色 + 情绪标签 + 标题文案。
 * 数据驱动设计：新增情绪只需加一条场景表，无需改动画代码。 */
static const emotion_scene_t s_idle_scene = {
    .eye_l = EYE(EYE_DIAMETER, (0x5a,0x82,0xb8)),
    .eye_r = EYE(EYE_DIAMETER, (0x5a,0x82,0xb8)),
    .bg_color=_C(0x0c,0x17,0x28), .accent=_C(0x7d,0xd3,0xfc),
    .emotion=EMOTION_LISTENING, .urgent=false,
    .title="Listening", .subtitle="Waiting for sound",
};

static const emotion_scene_t s_happy_scene = {
    .eye_l={EYE_DIAMETER,EYE_DIAMETER/4,EYE_DIAMETER/8,-26,_C(0x88,0x5a,0x9e)},
    .eye_r={EYE_DIAMETER,EYE_DIAMETER/4,EYE_DIAMETER/8,-26,_C(0x88,0x5a,0x9e)},
    .bg_color=_C(0x14,0x10,0x24), .accent=_C(0xff,0x8f,0xb3),
    .emotion=EMOTION_HAPPY, .urgent=false,
    .title="Happy", .subtitle="Recognized",
};

static const emotion_scene_t s_surprised_scene = {
    .eye_l={EYE_DIAMETER+90,EYE_DIAMETER+90,(EYE_DIAMETER+90)/2,0,_C(0xa0,0x44,0x44)},
    .eye_r={EYE_DIAMETER+90,EYE_DIAMETER+90,(EYE_DIAMETER+90)/2,0,_C(0xa0,0x44,0x44)},
    .bg_color=_C(0x22,0x0a,0x0e), .accent=_C(0xff,0x5a,0x66),
    .emotion=EMOTION_SURPRISED, .urgent=true,
    .title="Alert", .subtitle="Attention needed",
};

static const emotion_scene_t s_sad_scene = {
    .eye_l={EYE_DIAMETER*2/3,EYE_DIAMETER,EYE_DIAMETER/3,20,_C(0x8a,0x52,0x62)},
    .eye_r={EYE_DIAMETER*2/3,EYE_DIAMETER,EYE_DIAMETER/3,20,_C(0x8a,0x52,0x62)},
    .bg_color=_C(0x1a,0x0e,0x14), .accent=_C(0xf9,0xa8,0xd4),
    .emotion=EMOTION_SAD, .urgent=false,
    .title="Concern", .subtitle="Need attention",
};

static const emotion_scene_t s_thinking_scene = {
    .eye_l={EYE_DIAMETER,EYE_DIAMETER/6,EYE_DIAMETER/12,10,_C(0x72,0x7c,0x8a)},
    .eye_r={EYE_DIAMETER,EYE_DIAMETER/6,EYE_DIAMETER/12,10,_C(0x72,0x7c,0x8a)},
    .bg_color=_C(0x12,0x16,0x1c), .accent=_C(0xb8,0xc4,0xd6),
    .emotion=EMOTION_THINKING, .urgent=false,
    .title="Thinking", .subtitle="Processing audio",
};

#define SZ(id,ew,eh,er,ey,bgr,bgg,bgb,acr,acg,acb,em,ur,ti,su) \
    [AUDIO_CLASS_##id] = { \
        .eye_l={ew,eh,er,ey,_C(0xd0,0xd0,0xd0)}, .eye_r={ew,eh,er,ey,_C(0xd0,0xd0,0xd0)}, \
        .bg_color=_C(bgr,bgg,bgb), .accent=_C(acr,acg,acb), \
        .emotion=em, .urgent=ur, .title=ti, .subtitle=su, }

static const emotion_scene_t s_class_scenes[] = {
    SZ(ALARM, EYE_DIAMETER+90,EYE_DIAMETER+90,(EYE_DIAMETER+90)/2,0, 0x30,0x10,0x10,0xff,0x44,0x55,EMOTION_SURPRISED,true,"Alarm","Siren detected"),
    SZ(CAR_HORN, EYE_DIAMETER+70,EYE_DIAMETER+70,(EYE_DIAMETER+70)/2,0, 0x28,0x1e,0x10,0xff,0xc8,0x57,EMOTION_SURPRISED,true,"Car horn","Road noise"),
    SZ(KNOCK, EYE_DIAMETER,EYE_DIAMETER/4,EYE_DIAMETER/8,-26, 0x14,0x10,0x24,0xff,0x8f,0xb3,EMOTION_HAPPY,false,"Welcome","Someone is here"),
    SZ(CLAPPING, EYE_DIAMETER,EYE_DIAMETER/4,EYE_DIAMETER/8,-26, 0x18,0x22,0x10,0xd6,0xf4,0x65,EMOTION_HAPPY,false,"Clapping","Applause"),
    SZ(DOG_BARK, EYE_DIAMETER,EYE_DIAMETER/4,EYE_DIAMETER/8,-26, 0x10,0x1e,0x14,0x6e,0xd6,0x8a,EMOTION_HAPPY,false,"Dog bark","Dog sound"),
    SZ(FOOTSTEPS, EYE_DIAMETER-20,EYE_DIAMETER-20,(EYE_DIAMETER-20)/2,0, 0x0c,0x1a,0x22,0x67,0xe8,0xf9,EMOTION_LISTENING,false,"Footsteps","Movement nearby"),
    SZ(GLASS_BREAK, EYE_DIAMETER+90,EYE_DIAMETER+90,(EYE_DIAMETER+90)/2,0, 0x20,0x10,0x26,0xc0,0x84,0xfc,EMOTION_SURPRISED,true,"Glass break","Sharp impact"),
    SZ(DOORBELL, EYE_DIAMETER,EYE_DIAMETER/4,EYE_DIAMETER/8,-26, 0x14,0x16,0x26,0x93,0xc5,0xfd,EMOTION_HAPPY,false,"Doorbell","Visitor"),
    SZ(CRYING_BABY, EYE_DIAMETER*2/3,EYE_DIAMETER,EYE_DIAMETER/3,20, 0x20,0x12,0x1a,0xf9,0xa8,0xd4,EMOTION_SAD,true,"Baby crying","Needs attention"),
    SZ(ENGINE, EYE_DIAMETER,EYE_DIAMETER/6,EYE_DIAMETER/12,10, 0x14,0x18,0x18,0x94,0xa3,0xb8,EMOTION_THINKING,false,"Engine","Motor sound"),
    SZ(TRAFFIC, EYE_DIAMETER-20,EYE_DIAMETER-20,(EYE_DIAMETER-20)/2,0, 0x10,0x18,0x22,0x60,0xa5,0xfa,EMOTION_LISTENING,false,"Traffic","Road ambience"),
    SZ(BACKGROUND, EYE_DIAMETER,EYE_DIAMETER,EYE_DIAMETER/2,0, 0x0c,0x17,0x28,0x7d,0xd3,0xfc,EMOTION_LISTENING,false,"Listening","Background"),
};

/* ─── Scenes (light mode) ─── */
/* Brighter bg, darker eyes for contrast on white */

static const emotion_scene_t s_idle_scene_light = {
    .eye_l = EYE(EYE_DIAMETER, (0x4a,0x6e,0x9e)),
    .eye_r = EYE(EYE_DIAMETER, (0x4a,0x6e,0x9e)),
    .bg_color=_C(0xe8,0xee,0xf5), .accent=_C(0x29,0x73,0xb8),
    .emotion=EMOTION_LISTENING, .urgent=false,
    .title="Listening", .subtitle="Waiting for sound",
};

static const emotion_scene_t s_happy_scene_light = {
    .eye_l={EYE_DIAMETER,EYE_DIAMETER/4,EYE_DIAMETER/8,-26,_C(0x72,0x48,0x86)},
    .eye_r={EYE_DIAMETER,EYE_DIAMETER/4,EYE_DIAMETER/8,-26,_C(0x72,0x48,0x86)},
    .bg_color=_C(0xf5,0xee,0xf8), .accent=_C(0xc4,0x4a,0x6e),
    .emotion=EMOTION_HAPPY, .urgent=false,
    .title="Happy", .subtitle="Recognized",
};

static const emotion_scene_t s_surprised_scene_light = {
    .eye_l={EYE_DIAMETER+90,EYE_DIAMETER+90,(EYE_DIAMETER+90)/2,0,_C(0x8e,0x36,0x36)},
    .eye_r={EYE_DIAMETER+90,EYE_DIAMETER+90,(EYE_DIAMETER+90)/2,0,_C(0x8e,0x36,0x36)},
    .bg_color=_C(0xfc,0xf0,0xf0), .accent=_C(0xd0,0x30,0x3a),
    .emotion=EMOTION_SURPRISED, .urgent=true,
    .title="Alert", .subtitle="Attention needed",
};

static const emotion_scene_t s_sad_scene_light = {
    .eye_l={EYE_DIAMETER*2/3,EYE_DIAMETER,EYE_DIAMETER/3,20,_C(0x72,0x44,0x52)},
    .eye_r={EYE_DIAMETER*2/3,EYE_DIAMETER,EYE_DIAMETER/3,20,_C(0x72,0x44,0x52)},
    .bg_color=_C(0xf8,0xf0,0xf2), .accent=_C(0xc4,0x4a,0x6e),
    .emotion=EMOTION_SAD, .urgent=false,
    .title="Concern", .subtitle="Need attention",
};

static const emotion_scene_t s_thinking_scene_light = {
    .eye_l={EYE_DIAMETER,EYE_DIAMETER/6,EYE_DIAMETER/12,10,_C(0x58,0x62,0x6e)},
    .eye_r={EYE_DIAMETER,EYE_DIAMETER/6,EYE_DIAMETER/12,10,_C(0x58,0x62,0x6e)},
    .bg_color=_C(0xf0,0xf2,0xf4), .accent=_C(0x5a,0x6d,0x84),
    .emotion=EMOTION_THINKING, .urgent=false,
    .title="Thinking", .subtitle="Processing audio",
};

/* Light class scenes: lighter bg, darker eye pupil (0x70 gray) */
#define SZL(id,ew,eh,er,ey,bgr,bgg,bgb,acr,acg,acb,em,ur,ti,su) \
    [AUDIO_CLASS_##id] = { \
        .eye_l={ew,eh,er,ey,_C(0x70,0x70,0x70)}, .eye_r={ew,eh,er,ey,_C(0x70,0x70,0x70)}, \
        .bg_color=_C(bgr,bgg,bgb), .accent=_C(acr,acg,acb), \
        .emotion=em, .urgent=ur, .title=ti, .subtitle=su, }

static const emotion_scene_t s_class_scenes_light[] = {
    SZL(ALARM, EYE_DIAMETER+90,EYE_DIAMETER+90,(EYE_DIAMETER+90)/2,0, 0xfc,0xf0,0xf0,0xd0,0x30,0x3a,EMOTION_SURPRISED,true,"Alarm","Siren detected"),
    SZL(CAR_HORN, EYE_DIAMETER+70,EYE_DIAMETER+70,(EYE_DIAMETER+70)/2,0, 0xfc,0xf6,0xf0,0xd0,0x80,0x20,EMOTION_SURPRISED,true,"Car horn","Road noise"),
    SZL(KNOCK, EYE_DIAMETER,EYE_DIAMETER/4,EYE_DIAMETER/8,-26, 0xf5,0xee,0xf8,0xc4,0x4a,0x6e,EMOTION_HAPPY,false,"Welcome","Someone is here"),
    SZL(CLAPPING, EYE_DIAMETER,EYE_DIAMETER/4,EYE_DIAMETER/8,-26, 0xf2,0xf8,0xf0,0x22,0x8c,0x58,EMOTION_HAPPY,false,"Clapping","Applause"),
    SZL(DOG_BARK, EYE_DIAMETER,EYE_DIAMETER/4,EYE_DIAMETER/8,-26, 0xf0,0xf8,0xf4,0x1a,0x8c,0x5a,EMOTION_HAPPY,false,"Dog bark","Dog sound"),
    SZL(FOOTSTEPS, EYE_DIAMETER-20,EYE_DIAMETER-20,(EYE_DIAMETER-20)/2,0, 0xee,0xf4,0xfa,0x29,0x73,0xb8,EMOTION_LISTENING,false,"Footsteps","Movement nearby"),
    SZL(GLASS_BREAK, EYE_DIAMETER+90,EYE_DIAMETER+90,(EYE_DIAMETER+90)/2,0, 0xf8,0xf0,0xfc,0x6e,0x30,0xc0,EMOTION_SURPRISED,true,"Glass break","Sharp impact"),
    SZL(DOORBELL, EYE_DIAMETER,EYE_DIAMETER/4,EYE_DIAMETER/8,-26, 0xf0,0xf2,0xfc,0x29,0x73,0xb8,EMOTION_HAPPY,false,"Doorbell","Visitor"),
    SZL(CRYING_BABY, EYE_DIAMETER*2/3,EYE_DIAMETER,EYE_DIAMETER/3,20, 0xfc,0xf0,0xf2,0xc4,0x4a,0x6e,EMOTION_SAD,true,"Baby crying","Needs attention"),
    SZL(ENGINE, EYE_DIAMETER,EYE_DIAMETER/6,EYE_DIAMETER/12,10, 0xf2,0xf4,0xf6,0x5a,0x6d,0x84,EMOTION_THINKING,false,"Engine","Motor sound"),
    SZL(TRAFFIC, EYE_DIAMETER-20,EYE_DIAMETER-20,(EYE_DIAMETER-20)/2,0, 0xee,0xf4,0xfa,0x29,0x73,0xb8,EMOTION_LISTENING,false,"Traffic","Road ambience"),
    SZL(BACKGROUND, EYE_DIAMETER,EYE_DIAMETER,EYE_DIAMETER/2,0, 0xe8,0xee,0xf5,0x29,0x73,0xb8,EMOTION_LISTENING,false,"Listening","Background"),
};
#undef SZ
#undef SZL

static bool _is_attention_class(int c){switch(c){case 0:case 1:case 2:case 3:case 6:case 7:case 8:return true;default:return false;}}
static inline int32_t _c2i(lv_color_t c){return((int32_t)c.red<<16)|((int32_t)c.green<<8)|(int32_t)c.blue;}

static void _ew(void*o,int32_t v){lv_obj_set_width((lv_obj_t*)o,v);}
static void _eh(void*o,int32_t v){lv_obj_set_height((lv_obj_t*)o,v);}
static void _ey(void*o,int32_t v){lv_obj_set_y((lv_obj_t*)o,v);}
static void _ex(void*o,int32_t v){lv_obj_set_x((lv_obj_t*)o,v);}
static void _bgc(void*o,int32_t v){lv_obj_set_style_bg_color((lv_obj_t*)o,lv_color_hex(v),0);}
static void _tx(void*o,int32_t v){lv_obj_set_x((lv_obj_t*)o,v);}
static void _to(void*o,int32_t v){lv_obj_set_style_opa((lv_obj_t*)o,(lv_opa_t)v,0);}

static void _hl_pos(lv_obj_t *hl, int eye_cx, int eye_top, int eye_w, int eye_h) {
    int ecy = eye_top + eye_h / 2;
    lv_obj_set_pos(hl, eye_cx + eye_w/2 - HL_SIZE - 8, ecy - eye_h/2 + 10);
}

/* 单只眼睛的形变动画：宽度/高度/Y/X 四个属性并行 lv_anim，
 * 效果是"眼睛从旧形状平滑变成新形状"。 */
static void _morph_eye(lv_obj_t *eye, int cx, const eye_shape_t *from, const eye_shape_t *to) {
    if (!eye) return;
    int by = EYE_CY - to->h / 2;
    lv_anim_t a;
    lv_anim_init(&a);lv_anim_set_var(&a,eye);lv_anim_set_exec_cb(&a,_ew);lv_anim_set_values(&a,from->w,to->w);lv_anim_set_time(&a,MORPH_DURATION_MS);lv_anim_set_path_cb(&a,lv_anim_path_ease_in_out);lv_anim_start(&a);
    lv_anim_init(&a);lv_anim_set_var(&a,eye);lv_anim_set_exec_cb(&a,_eh);lv_anim_set_values(&a,from->h,to->h);lv_anim_set_time(&a,MORPH_DURATION_MS);lv_anim_set_path_cb(&a,lv_anim_path_ease_in_out);lv_anim_start(&a);
    lv_anim_init(&a);lv_anim_set_var(&a,eye);lv_anim_set_exec_cb(&a,_ey);lv_anim_set_values(&a,by+from->y_off,by+to->y_off);lv_anim_set_time(&a,MORPH_DURATION_MS);lv_anim_set_path_cb(&a,lv_anim_path_ease_in_out);lv_anim_start(&a);
    lv_anim_init(&a);lv_anim_set_var(&a,eye);lv_anim_set_exec_cb(&a,_ex);lv_anim_set_values(&a,cx-from->w/2,cx-to->w/2);lv_anim_set_time(&a,MORPH_DURATION_MS);lv_anim_set_path_cb(&a,lv_anim_path_ease_in_out);lv_anim_start(&a);
    lv_obj_set_style_radius(eye, to->radius, 0);
    lv_obj_set_style_bg_color(eye, to->color, 0);
}

/* 切换到目标场景：同时动画两只眼睛 + 背景色渐变 + 更新文案。
 * 这是整个表情引擎的"状态机跳转"入口。 */
static void _morph_to(const emotion_scene_t *to) {
    if (!s_eye_l || !s_eye_r || !to) return;
    emotion_scene_t from = s_active_scene ? *s_active_scene : s_idle_scene;
    _morph_eye(s_eye_l, EYE_L_CX, &from.eye_l, &to->eye_l);
    _morph_eye(s_eye_r, EYE_R_CX, &from.eye_r, &to->eye_r);
    if (s_hl_l) _hl_pos(s_hl_l, EYE_L_CX, EYE_CY - to->eye_l.h/2 + to->eye_l.y_off, to->eye_l.w, to->eye_l.h);
    if (s_hl_r) _hl_pos(s_hl_r, EYE_R_CX, EYE_CY - to->eye_r.h/2 + to->eye_r.y_off, to->eye_r.w, to->eye_r.h);
    if (s_root) { lv_anim_t a; lv_anim_init(&a);lv_anim_set_var(&a,s_root);lv_anim_set_exec_cb(&a,_bgc);lv_anim_set_values(&a,_c2i(from.bg_color),_c2i(to->bg_color));lv_anim_set_time(&a,MORPH_DURATION_MS);lv_anim_set_path_cb(&a,lv_anim_path_ease_in_out);lv_anim_start(&a); }
    if (s_title_label) { lv_label_set_text(s_title_label, to->title); lv_obj_set_style_text_color(s_title_label, to->accent, 0); }
    if (s_subtitle_label) { lv_label_set_text(s_subtitle_label, to->subtitle); lv_obj_set_style_text_color(s_subtitle_label, to->accent, 0); }
    s_active_scene = to; s_current = to->emotion;
}

static void _pulse_restore(void*o,int32_t v){lv_obj_set_style_bg_color((lv_obj_t*)o,lv_color_hex(v),0);}
/* 紧急脉冲：定时器交替把背景色闪成强调色，制造"警报感" */
static void _pulse_flash_cb(lv_timer_t *t) {
    static int c=0;
    if(!s_root||!s_active_scene){lv_timer_del(t);s_pulse_timer=NULL;return;}
    lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,s_root);lv_anim_set_exec_cb(&a,_pulse_restore);lv_anim_set_time(&a,140);lv_anim_set_path_cb(&a,lv_anim_path_ease_in_out);
    if(c%2==0)lv_anim_set_values(&a,_c2i(s_active_scene->bg_color),_c2i(s_active_scene->accent));
    else lv_anim_set_values(&a,_c2i(s_active_scene->accent),_c2i(s_active_scene->bg_color));
    lv_anim_start(&a);
    if(++c>=PULSE_COUNT*2){lv_timer_del(t);s_pulse_timer=NULL;}
}
static void _trigger_urgency_pulse(void){if(s_pulse_timer)lv_timer_del(s_pulse_timer);s_pulse_timer=lv_timer_create(_pulse_flash_cb,PULSE_DURATION_MS/2,NULL);}

static void _blink_restore_cb(lv_timer_t *t){(void)t;s_blink_restore_timer=NULL;s_blinking=false;
    if(s_active_scene){lv_obj_set_height(s_eye_l,s_active_scene->eye_l.h);lv_obj_set_height(s_eye_r,s_active_scene->eye_r.h);}}
static void _blink_cb(lv_timer_t *t){(void)t;if(!s_eye_l||!s_eye_r||s_blinking)return;
    if(s_current!=EMOTION_IDLE&&s_current!=EMOTION_LISTENING&&s_current!=EMOTION_THINKING)return;
    s_blinking=true;lv_obj_set_height(s_eye_l,8);lv_obj_set_height(s_eye_r,8);
    if(s_blink_restore_timer)lv_timer_del(s_blink_restore_timer);
    s_blink_restore_timer=lv_timer_create(_blink_restore_cb,EMOTION_BLINK_HOLD_MS,NULL);lv_timer_set_repeat_count(s_blink_restore_timer,1);}
/* 重置眨眼定时器：随机间隔 + 只眨眼一次（眼睛压扁 120ms 后恢复） */
static void _reset_blink_timer(void){if(s_blink_timer)lv_timer_del(s_blink_timer);int interval=EMOTION_BLINK_MIN_MS+(esp_random()%(EMOTION_BLINK_MAX_MS-EMOTION_BLINK_MIN_MS));s_blink_timer=lv_timer_create(_blink_cb,interval,NULL);}

/* 自动复位回调：超时后把表情切回待机态（class_id=-1 表示无类别） */
static void _clear_timer_cb(lv_timer_t *t){(void)t;s_clear_timer=NULL;ui_emotion_set_by_audio(-1,0.0f);}
static void _restart_clear_timer(void){if(s_clear_timer){lv_timer_del(s_clear_timer);s_clear_timer=NULL;}s_clear_timer=lv_timer_create(_clear_timer_cb,EMOTION_AUTO_CLEAR_MS,NULL);lv_timer_set_repeat_count(s_clear_timer,1);}
static void _cancel_clear_timer(void){if(s_clear_timer){lv_timer_del(s_clear_timer);s_clear_timer=NULL;}}

static void _animate_toast(lv_obj_t *t){if(!t)return;lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,t);lv_anim_set_exec_cb(&a,_tx);lv_anim_set_values(&a,-28,16);lv_anim_set_time(&a,260);lv_anim_set_path_cb(&a,lv_anim_path_ease_out);lv_anim_start(&a);lv_anim_init(&a);lv_anim_set_var(&a,t);lv_anim_set_exec_cb(&a,_to);lv_anim_set_values(&a,0,240);lv_anim_set_time(&a,260);lv_anim_set_path_cb(&a,lv_anim_path_ease_out);lv_anim_start(&a);}
static void _hide_toasts_cb(lv_timer_t *t){(void)t;s_toast_hide_timer=NULL;for(int i=0;i<TOAST_COUNT;i++){if(s_toasts[i]){s_toast_text[i][0]='\0';lv_obj_add_flag(s_toasts[i],LV_OBJ_FLAG_HIDDEN);lv_obj_set_style_opa(s_toasts[i],LV_OPA_TRANSP,0);}}}
static void _restart_toast_hide_timer(void){if(s_toast_hide_timer){lv_timer_del(s_toast_hide_timer);s_toast_hide_timer=NULL;}s_toast_hide_timer=lv_timer_create(_hide_toasts_cb,TOAST_HIDE_MS,NULL);lv_timer_set_repeat_count(s_toast_hide_timer,1);}
static void _push_toast_text(const char *text){if(!s_toasts[0]||!text)return;for(int i=TOAST_COUNT-1;i>0;i--)memcpy(s_toast_text[i],s_toast_text[i-1],sizeof(s_toast_text[i]));snprintf(s_toast_text[0],sizeof(s_toast_text[0]),"%s",text);for(int i=0;i<TOAST_COUNT;i++){if(s_toast_text[i][0]=='\0'){lv_obj_add_flag(s_toasts[i],LV_OBJ_FLAG_HIDDEN);continue;}lv_obj_clear_flag(s_toasts[i],LV_OBJ_FLAG_HIDDEN);lv_label_set_text(s_toast_labels[i],s_toast_text[i]);lv_obj_set_style_opa(s_toasts[i],i==0?LV_OPA_TRANSP:(lv_opa_t)(220-i*32),0);lv_color_t bg=i==0?lv_color_mix(s_active_scene?s_active_scene->accent:lv_color_hex(0x7dd3fc),lv_color_hex(0x080c14),140):lv_color_hex(0x141a24);lv_obj_set_style_bg_color(s_toasts[i],bg,0);if(i==0)_animate_toast(s_toasts[i]);}_restart_toast_hide_timer();}
/* 把"非紧急"类别显示成顶部小提示条，而不是霸屏的表情变化 */
static void _push_toast(int class_id, float confidence) {
    if (class_id < 0 || class_id == AUDIO_CLASS_BACKGROUND) return;
    char t[96]; snprintf(t, sizeof(t), "%s  %d%%", app_audio_class_title(class_id), (int)(confidence*100.0f+0.5f));
    _push_toast_text(t);
}

/* 相机预览定时器：约 30fps 从相机服务拉最新帧刷新 lv_image。
 * 只在表情页开启；切走页面会 pause，省 CPU 和内存带宽。 */
static void _camera_preview_timer_cb(lv_timer_t *t){(void)t;if(!s_camera_image)return;
    camera_preview_frame_t frame={0};if(camera_service_acquire_preview(s_camera_last_sequence,&frame)!=ESP_OK)return;
    memset(&s_camera_img_dsc,0,sizeof(s_camera_img_dsc));s_camera_img_dsc.header.w=frame.info.width;
    s_camera_img_dsc.header.h=frame.info.height;
    s_camera_img_dsc.header.cf=frame.info.byte_swap?LV_COLOR_FORMAT_RGB565_SWAPPED:LV_COLOR_FORMAT_RGB565;
    s_camera_img_dsc.data_size=frame.data_size;s_camera_img_dsc.data=frame.data;
    lv_image_set_src(s_camera_image,&s_camera_img_dsc);
    if(s_camera_status&&!s_camera_live_shown){lv_label_set_text(s_camera_status,"LIVE");s_camera_live_shown=true;}
    lv_obj_invalidate(s_camera_panel);s_camera_last_sequence=frame.info.sequence;}

static void _build_face(lv_obj_t *parent) {
    s_eye_l = lv_obj_create(parent); lv_obj_remove_style_all(s_eye_l);
    lv_obj_set_size(s_eye_l, EYE_DIAMETER, EYE_DIAMETER);
    lv_obj_set_style_radius(s_eye_l, EYE_DIAMETER/2, 0);
    lv_obj_set_style_bg_color(s_eye_l, s_idle_scene.eye_l.color, 0);
    lv_obj_set_style_bg_opa(s_eye_l, LV_OPA_COVER, 0);
    lv_obj_set_pos(s_eye_l, EYE_L_CX - EYE_DIAMETER/2, EYE_CY - EYE_DIAMETER/2);

    s_eye_r = lv_obj_create(parent); lv_obj_remove_style_all(s_eye_r);
    lv_obj_set_size(s_eye_r, EYE_DIAMETER, EYE_DIAMETER);
    lv_obj_set_style_radius(s_eye_r, EYE_DIAMETER/2, 0);
    lv_obj_set_style_bg_color(s_eye_r, s_idle_scene.eye_r.color, 0);
    lv_obj_set_style_bg_opa(s_eye_r, LV_OPA_COVER, 0);
    lv_obj_set_pos(s_eye_r, EYE_R_CX - EYE_DIAMETER/2, EYE_CY - EYE_DIAMETER/2);

    s_hl_l = lv_obj_create(parent); lv_obj_remove_style_all(s_hl_l);
    lv_obj_set_size(s_hl_l, HL_SIZE, HL_SIZE);
    lv_obj_set_style_radius(s_hl_l, HL_SIZE/2, 0);
    lv_obj_set_style_bg_color(s_hl_l, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(s_hl_l, LV_OPA_70, 0);
    _hl_pos(s_hl_l, EYE_L_CX, EYE_CY - EYE_DIAMETER/2, EYE_DIAMETER, EYE_DIAMETER);

    s_hl_r = lv_obj_create(parent); lv_obj_remove_style_all(s_hl_r);
    lv_obj_set_size(s_hl_r, HL_SIZE, HL_SIZE);
    lv_obj_set_style_radius(s_hl_r, HL_SIZE/2, 0);
    lv_obj_set_style_bg_color(s_hl_r, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(s_hl_r, LV_OPA_70, 0);
    _hl_pos(s_hl_r, EYE_R_CX, EYE_CY - EYE_DIAMETER/2, EYE_DIAMETER, EYE_DIAMETER);

    s_title_label = lv_label_create(parent);
    lv_obj_set_width(s_title_label, LV_HOR_RES);
    lv_obj_set_style_text_font(s_title_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_align(s_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_title_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(s_title_label, 0, LV_VER_RES - 62);

    s_subtitle_label = lv_label_create(parent);
    lv_obj_set_width(s_subtitle_label, LV_HOR_RES);
    lv_obj_set_style_text_font(s_subtitle_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(s_subtitle_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_subtitle_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(s_subtitle_label, 0, LV_VER_RES - 30);
}

/* 创建表情页：眼睛、文案、提示条、相机面板，并启动各定时器 */
void ui_emotion_create(lv_obj_t *parent) {
    s_root = parent;
    lv_obj_set_style_bg_color(parent, s_idle_scene.bg_color, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(parent, 0, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);
    _build_face(parent);

    for (int i = 0; i < TOAST_COUNT; i++) {
        s_toast_text[i][0] = '\0';
        s_toasts[i] = lv_obj_create(parent); lv_obj_remove_style_all(s_toasts[i]);
        lv_obj_set_size(s_toasts[i], 248, 40); lv_obj_set_style_radius(s_toasts[i], 10, 0);
        lv_obj_set_style_bg_opa(s_toasts[i], LV_OPA_80, 0);
        lv_obj_set_style_bg_color(s_toasts[i], lv_color_hex(0x10161f), 0);
        lv_obj_set_style_border_width(s_toasts[i], 1, 0);
        lv_obj_set_style_border_color(s_toasts[i], lv_color_hex(0x304060), 0);
        lv_obj_set_style_pad_hor(s_toasts[i], 12, 0); lv_obj_set_style_pad_ver(s_toasts[i], 8, 0);
        lv_obj_align(s_toasts[i], LV_ALIGN_TOP_LEFT, 16, 16 + i * 46);
        lv_obj_clear_flag(s_toasts[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_toasts[i], LV_OBJ_FLAG_HIDDEN);
        s_toast_labels[i] = lv_label_create(s_toasts[i]);
        lv_label_set_text(s_toast_labels[i], "");
        lv_obj_set_style_text_font(s_toast_labels[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_toast_labels[i], lv_color_hex(0xf7fafc), 0);
        lv_obj_set_width(s_toast_labels[i], 220);
        lv_label_set_long_mode(s_toast_labels[i], LV_LABEL_LONG_CLIP);
        lv_obj_align(s_toast_labels[i], LV_ALIGN_CENTER, 0, 0);
    }

    s_camera_panel = lv_obj_create(parent); lv_obj_remove_style_all(s_camera_panel);
    lv_obj_set_size(s_camera_panel, CAMERA_PREVIEW_W, CAMERA_PREVIEW_H);
    lv_obj_set_style_radius(s_camera_panel, 8, 0); lv_obj_set_style_clip_corner(s_camera_panel, true, 0);
    lv_obj_set_style_bg_color(s_camera_panel, lv_color_hex(0x080b10), 0);
    lv_obj_set_style_bg_opa(s_camera_panel, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_camera_panel, 1, 0);
    lv_obj_set_style_border_color(s_camera_panel, lv_color_hex(0x304052), 0);
    lv_obj_clear_flag(s_camera_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_camera_panel, LV_ALIGN_TOP_RIGHT, -16, 16);
    s_camera_image = lv_image_create(s_camera_panel);
    lv_obj_set_size(s_camera_image, CAMERA_PREVIEW_W, CAMERA_PREVIEW_H);
    lv_obj_align(s_camera_image, LV_ALIGN_CENTER, 0, 0);
    s_camera_status = lv_label_create(s_camera_panel);
    lv_label_set_text(s_camera_status, "VISION");
    lv_obj_set_style_text_font(s_camera_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_camera_status, lv_color_hex(0xdce8f7), 0);
    lv_obj_set_style_bg_color(s_camera_status, lv_color_hex(0x05080c), 0);
    lv_obj_set_style_bg_opa(s_camera_status, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(s_camera_status, 8, 0); lv_obj_set_style_pad_ver(s_camera_status, 4, 0);
    lv_obj_align(s_camera_status, LV_ALIGN_BOTTOM_RIGHT, -6, -6);

    s_active_scene = ui_theme_is_light() ? &s_idle_scene_light : &s_idle_scene;
    s_current = EMOTION_IDLE;
    _morph_to(s_active_scene);
    _reset_blink_timer();
    if (!s_camera_preview_timer) s_camera_preview_timer = lv_timer_create(_camera_preview_timer_cb, CAMERA_PREVIEW_PULL_MS, NULL);
    s_camera_live_shown = false;
    ui_theme_create_toggle_btn(parent);
    ui_emotion_theme_refresh = _emotion_theme_refresh;
    ESP_LOGI(TAG, "Eyes engine created");
}

void ui_emotion_set(emotion_t e) {
    if (ui_theme_is_light()) {
        switch (e) {
        case EMOTION_HAPPY: _morph_to(&s_happy_scene_light); break;
        case EMOTION_SURPRISED: _morph_to(&s_surprised_scene_light); break;
        case EMOTION_SAD: _morph_to(&s_sad_scene_light); break;
        case EMOTION_THINKING: _morph_to(&s_thinking_scene_light); break;
        default: _morph_to(&s_idle_scene_light); break;
        }
    } else {
        switch (e) {
        case EMOTION_HAPPY: _morph_to(&s_happy_scene); break;
        case EMOTION_SURPRISED: _morph_to(&s_surprised_scene); break;
        case EMOTION_SAD: _morph_to(&s_sad_scene); break;
        case EMOTION_THINKING: _morph_to(&s_thinking_scene); break;
        default: _morph_to(&s_idle_scene); break;
        }
    }
}

emotion_t ui_emotion_get_current(void) { return s_current; }

/* 音频分类驱动表情：紧急类别全屏变脸，非紧急只弹提示条 */
void ui_emotion_set_by_audio(int class_id, float confidence) {
    s_last_audio_class = class_id; s_last_confidence = confidence;
    if (class_id < 0 || class_id == AUDIO_CLASS_BACKGROUND || confidence < 0.1f) {
        _cancel_clear_timer();
        _morph_to(ui_theme_is_light() ? &s_idle_scene_light : &s_idle_scene);
        return;
    }
    if (!_is_attention_class(class_id)) { _push_toast(class_id, confidence); return; }
    if (class_id >= 0 && class_id < (int)(sizeof(s_class_scenes)/sizeof(s_class_scenes[0]))) {
        const emotion_scene_t *sc = ui_theme_is_light() ? &s_class_scenes_light[class_id] : &s_class_scenes[class_id];
        _morph_to(sc); _push_toast(class_id, confidence);
        if (sc->urgent) _trigger_urgency_pulse();
        _restart_clear_timer(); return;
    }
    _morph_to(ui_theme_is_light() ? &s_thinking_scene_light : &s_thinking_scene);
    _restart_clear_timer();
}

/* 视觉(人脸情绪)驱动表情：加置信度和人脸分双重门槛，防止误报 */
void ui_emotion_set_by_visual(int emotion_id, float confidence, float face_score) {
    if (emotion_id < 0 || face_score < 0.70f || confidence < 0.45f) {
        return;
    }

    char text[96];
    snprintf(text, sizeof(text), "Face %s  %d%%",
             visual_emotion_name(emotion_id),
             (int)(confidence * 100.0f + 0.5f));
    _push_toast_text(text);

    switch (emotion_id) {
    case VISUAL_EMOTION_HAPPY:
        ui_emotion_set(EMOTION_HAPPY);
        break;
    case VISUAL_EMOTION_SURPRISE:
    case VISUAL_EMOTION_FEAR:
    case VISUAL_EMOTION_ANGER:
        ui_emotion_set(EMOTION_SURPRISED);
        if (confidence >= 0.70f) {
            _trigger_urgency_pulse();
        }
        break;
    case VISUAL_EMOTION_SAD:
    case VISUAL_EMOTION_DISGUST:
        ui_emotion_set(EMOTION_SAD);
        break;
    case VISUAL_EMOTION_NEUTRAL:
    default:
        return;
    }

    _restart_clear_timer();
}

void ui_emotion_show_welcome_home(float confidence) {
    s_last_audio_class = AUDIO_CLASS_KNOCK; s_last_confidence = confidence;
    const emotion_scene_t *sc = ui_theme_is_light() ? &s_class_scenes_light[AUDIO_CLASS_KNOCK] : &s_class_scenes[AUDIO_CLASS_KNOCK];
    _morph_to(sc);
    _push_toast(AUDIO_CLASS_KNOCK, confidence);
    _restart_clear_timer();
}

void ui_emotion_notify_system(bool is_error) {
    if (is_error) {
        _morph_to(ui_theme_is_light() ? &s_sad_scene_light : &s_sad_scene);
        _restart_clear_timer();
    } else {
        ui_emotion_set_by_audio(-1, 0.0f);
    }
}

/* 暂停相机预览（页面切走时调用，释放渲染带宽） */
void ui_emotion_pause_preview(void) {
    if (s_camera_preview_timer) { lv_timer_del(s_camera_preview_timer); s_camera_preview_timer = NULL; }
}
/* 恢复相机预览（页面切回时调用） */
void ui_emotion_resume_preview(void) {
    if (!s_camera_preview_timer && s_root)
        s_camera_preview_timer = lv_timer_create(_camera_preview_timer_cb, CAMERA_PREVIEW_PULL_MS, NULL);
}

static void _emotion_theme_refresh(void) {
    if (!s_root) return;
    const ui_theme_t *t = ui_theme_get();
    /* Page background — only override if scene is idle */
    if (s_current == EMOTION_IDLE || s_current == EMOTION_LISTENING) {
        lv_obj_set_style_bg_color(s_root, t->bg, 0);
    }
    /* Labels */
    if (s_title_label) lv_obj_set_style_text_color(s_title_label, t->text_primary, 0);
    if (s_subtitle_label) lv_obj_set_style_text_color(s_subtitle_label, t->text_secondary, 0);
    /* Toast backgrounds */
    for (int i = 0; i < TOAST_COUNT; i++) {
        if (s_toasts[i]) {
            lv_obj_set_style_bg_color(s_toasts[i], t->card_bg, 0);
            lv_obj_set_style_border_color(s_toasts[i], t->card_border, 0);
            if (s_toast_labels[i]) lv_obj_set_style_text_color(s_toast_labels[i], t->text_primary, 0);
        }
    }
    /* Camera panel */
    if (s_camera_panel) { lv_obj_set_style_bg_color(s_camera_panel, t->card_bg, 0); lv_obj_set_style_border_color(s_camera_panel, t->card_border, 0); }
    if (s_camera_status) lv_obj_set_style_text_color(s_camera_status, t->text_primary, 0);
}
