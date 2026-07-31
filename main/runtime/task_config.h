#pragma once

/*
 * 中央 FreeRTOS / 运行时配置（面试重点：任务、优先级、核心绑定）
 *
 * 设计原则：
 *  - 所有任务的大小/优先级/核心归属集中在这里定义，便于统一调整调度策略；
 *  - 服务层（service）只关心业务行为，运行时层（runtime）只关心“任务怎么跑”；
 *  - ESP32-P4 是双核（Core 0 / Core 1），通过 xTaskCreatePinnedToCore()
 *    把实时性要求高的任务固定到指定核心，避免被调度器随意迁移。
 */

/* 核心划分：0 号核跑音频实时链路，1 号核跑 UI/相机（LVGL 渲染也在核 1） */
#define APP_CORE_REALTIME       0
#define APP_CORE_UI_CAMERA      1

/* 事件队列深度：音频/视觉分类结果、触摸、系统事件统一走这条队列 */
#define APP_EVENT_QUEUE_DEPTH   10

/* 音频参数：16kHz 采样，1600 点 = 100ms 一帧，1 秒窗口 = 10 帧拼一个推理窗口 */
#define AUDIO_SAMPLE_RATE_HZ    16000
#define AUDIO_FRAME_SAMPLES     1600
#define AUDIO_WINDOW_SAMPLES    AUDIO_SAMPLE_RATE_HZ
/* 音频帧总线信号量最大计数（帧信号可累计的深度） */
#define AUDIO_QUEUE_DEPTH       4

/* ---- 任务 1：音频采集（生产者） ----
 * 优先级 10（最高），绑定 Core 0，栈 4KB。
 * 只做两件事：阻塞读 I2S 麦克风 100ms 一帧，然后投递到音频帧总线。
 * 高优先级 + 阻塞读 = 及时取走 DMA 数据，避免覆盖。
 */
#define AUDIO_CAPTURE_TASK_NAME     "audio_capture"
#define AUDIO_CAPTURE_TASK_STACK    4096
#define AUDIO_CAPTURE_TASK_PRIO     10
#define AUDIO_CAPTURE_TASK_CORE     APP_CORE_REALTIME

/* ---- 任务 2：音频推理（消费者） ----
 * 优先级 4，绑定 Core 0。栈 12KB（要容纳 ESP-DL 神经网络推理的局部变量）。
 * 从帧总线拼出 1 秒窗口，做 mel 频谱 + 模型推理，结果发到事件队列。
 */
#define AUDIO_INFER_TASK_NAME       "audio_inf"
#define AUDIO_INFER_TASK_STACK      12288
#define AUDIO_INFER_TASK_PRIO       4
#define AUDIO_INFER_TASK_CORE       APP_CORE_REALTIME

/* ---- 任务 3：UI 任务 ----
 * 优先级 5，绑定 Core 1（与 LVGL 渲染适配器同核，减少跨核锁竞争）。
 * 阻塞收事件队列，收到分类结果后在 LVGL 锁内刷新界面。
 */
#define UI_TASK_NAME                "ui_task"
#define UI_TASK_STACK               6144
#define UI_TASK_PRIO                5
#define UI_TASK_CORE                APP_CORE_UI_CAMERA

/* ---- 任务 4：相机服务 ----
 * 优先级 3，绑定 Core 1。负责 SC2336 传感器取流、缩放出预览图/224x224 推理图。
 */
#define CAMERA_SERVICE_TASK_NAME    "camera_svc"
#define CAMERA_SERVICE_TASK_STACK   5120
#define CAMERA_SERVICE_TASK_PRIO    3
#define CAMERA_SERVICE_TASK_CORE    APP_CORE_UI_CAMERA

/* ---- 任务 5：视觉推理 ----
 * 优先级 2，绑定 Core 1。栈 16KB（ESP-DL 人脸检测 + 情绪分类模型）。
 */
#define VISUAL_INFER_TASK_NAME      "visual_inf"
#define VISUAL_INFER_TASK_STACK     16384
#define VISUAL_INFER_TASK_PRIO      2
#define VISUAL_INFER_TASK_CORE      APP_CORE_UI_CAMERA

/* ---- 任务 6：遥测 ----
 * 优先级 1（最低），每 10 秒打印一次堆/队列/各服务统计，用于调试与压测。
 */
#define TELEMETRY_TASK_NAME         "telemetry"
#define TELEMETRY_TASK_STACK        4096
#define TELEMETRY_TASK_PRIO         1
#define TELEMETRY_TASK_CORE         APP_CORE_UI_CAMERA
#define TELEMETRY_PERIOD_MS         10000
#define TELEMETRY_STACK_EVERY       3
#define TELEMETRY_MAX_TASKS         8

/* ---- 任务 7：告警反馈（蜂鸣音 + 马达振动） ----
 * 优先级 3，绑定 Core 0。通过队列接收“需要关注”的分类事件，
 * 按类别播放提示音/振动模式；阻塞在队列上，平时不占 CPU。
 */
#define ALERT_FEEDBACK_TASK_NAME    "alert_fb"
#define ALERT_FEEDBACK_TASK_STACK   4096
#define ALERT_FEEDBACK_TASK_PRIO    3
#define ALERT_FEEDBACK_TASK_CORE    APP_CORE_REALTIME
#define ALERT_FEEDBACK_QUEUE_DEPTH  4