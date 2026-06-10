#pragma once
#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 表情状态 */
typedef enum {
    EMOTION_IDLE,        // ◕‿◕   待机/聆听中
    EMOTION_HAPPY,       // ◕▽◕✿ 识别到目标
    EMOTION_SURPRISED,   // ⊙▽⊙   识别到异常/意外
    EMOTION_THINKING,    // (￣_￣) 正在推理处理
    EMOTION_LISTENING,   // >_>   专注监听
    EMOTION_SAD,         // ◕︵◕   系统异常/无识别
    EMOTION_COUNT,
} emotion_t;

/** 表情状态对应名称 */
const char *emotion_to_str(emotion_t e);

// ─── 页面接口 ─────────────────────────────────

/** 创建表情页面（在 display_app 中注册为新页面） */
void ui_emotion_create(lv_obj_t *parent);

/** 设置当前表情 */
void ui_emotion_set(emotion_t e);

/** 获取当前表情 */
emotion_t ui_emotion_get_current(void);

// ─── 模型识别结果接口 ────────────────────────

/**
 * @brief 根据音频分类结果更新表情
 *
 * 内部映射：高置信度 → HAPPY / SURPRISED
 *           低置信度 → THINKING
 *           无结果    → LISTENING
 *
 * @param class_id   分类 ID（-1 表示无结果）
 * @param confidence 置信度 (0.0 ~ 1.0)
 */
void ui_emotion_set_by_audio(int class_id, float confidence);

/**
 * @brief 根据视觉分类结果更新表情（预留）
 */
void ui_emotion_show_welcome_home(float confidence);

/**
 * @brief 通知系统状态事件（错误、警告等）
 * @param is_error true=错误 → SAD, false=恢复 → IDLE
 */
void ui_emotion_notify_system(bool is_error);

/** Pause/resume camera preview when switching pages */
void ui_emotion_pause_preview(void);
void ui_emotion_resume_preview(void);

#ifdef __cplusplus
}
#endif
