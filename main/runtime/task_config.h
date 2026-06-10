#pragma once

/*
 * Central FreeRTOS/runtime sizing.
 *
 * Keep task placement here so service code owns behavior, while the runtime
 * layer owns scheduling policy for ESP32-P4's two cores.
 */

#define APP_CORE_REALTIME       0
#define APP_CORE_UI_CAMERA      1

#define APP_EVENT_QUEUE_DEPTH   10

#define AUDIO_SAMPLE_RATE_HZ    16000
#define AUDIO_FRAME_SAMPLES     1600
#define AUDIO_WINDOW_SAMPLES    AUDIO_SAMPLE_RATE_HZ
#define AUDIO_QUEUE_DEPTH       4

#define AUDIO_CAPTURE_TASK_NAME     "audio_capture"
#define AUDIO_CAPTURE_TASK_STACK    4096
#define AUDIO_CAPTURE_TASK_PRIO     10
#define AUDIO_CAPTURE_TASK_CORE     APP_CORE_REALTIME

#define AUDIO_INFER_TASK_NAME       "audio_inf"
#define AUDIO_INFER_TASK_STACK      12288
#define AUDIO_INFER_TASK_PRIO       4
#define AUDIO_INFER_TASK_CORE       APP_CORE_REALTIME

#define UI_TASK_NAME                "ui_task"
#define UI_TASK_STACK               6144
#define UI_TASK_PRIO                5
#define UI_TASK_CORE                APP_CORE_UI_CAMERA

#define CAMERA_SERVICE_TASK_NAME    "camera_svc"
#define CAMERA_SERVICE_TASK_STACK   5120
#define CAMERA_SERVICE_TASK_PRIO    3
#define CAMERA_SERVICE_TASK_CORE    APP_CORE_UI_CAMERA

#define TELEMETRY_TASK_NAME         "telemetry"
#define TELEMETRY_TASK_STACK        4096
#define TELEMETRY_TASK_PRIO         1
#define TELEMETRY_TASK_CORE         APP_CORE_UI_CAMERA
#define TELEMETRY_PERIOD_MS         10000
#define TELEMETRY_STACK_EVERY       3
#define TELEMETRY_MAX_TASKS         8
