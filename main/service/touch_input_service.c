#include "service/touch_input_service.h"

#include "app/display_app.h"
#include "driver/display/dsi_lcd.h"
#include "hal/display_hal.h"
#include "hal/touch_hal.h"
#include "lvgl.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TOUCH_INPUT";

/*
 * 触摸输入服务（FreeRTOS 任务 + 临界区 + LVGL indev，面试重点）
 * ====================================================
 * 双角色设计：
 *  1) touch_input_task：独立任务，每 20ms 轮询 GT911 触摸屏，
 *     把最新坐标写进 s_cache（用自旋锁保护，因为会被两个上下文访问）；
 *  2) touch_lvgl_read_cb：LVGL 注册的回调，渲染循环每次刷新时调用它
 *     读取坐标，喂给 LVGL 事件系统（点击/滑动/长按都由此产生）。
 *
 * 为什么用自旋锁(portMUX)而不是互斥锁？
 *  - 临界区极短（拷贝 3 个字段），且可能被 LVGL 高优先级上下文调用；
 *  - portENTER_CRITICAL 会关本地中断，适合这种微秒级共享数据保护。
 *
 * 坐标变换：GT911 物理坐标系(竖屏 480x800) -> LVGL 横屏(800x480)，
 * 即 (x,y) -> (y, 479-x)，对应 DSI 面板旋转 90 度。
 */

#define TOUCH_INPUT_TASK_STACK 3072
#define TOUCH_INPUT_TASK_PRIO  2
#define TOUCH_INPUT_TASK_CORE  1
#define TOUCH_POLL_MS          20
#define TOUCH_PHYS_W           480

typedef struct {
    bool pressed;
    uint16_t x;
    uint16_t y;
    uint32_t sequence;
} touch_cache_t;

/* 自旋锁：保护 s_cache（触摸任务写 / LVGL 回调读） */
static portMUX_TYPE s_cache_lock = portMUX_INITIALIZER_UNLOCKED;
static touch_cache_t s_cache;
static lv_indev_t *s_indev;
static TaskHandle_t s_task;
static bool s_started;

/* LVGL 输入设备回调：LVGL 每次刷新前调用它取触摸数据。
 * 注意运行在 LVGL 渲染上下文里，不能阻塞，所以只做加锁拷贝。 */
static void touch_lvgl_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    touch_cache_t cache;
    /* 临界区：关中断保护共享数据（微秒级，可接受） */
    portENTER_CRITICAL(&s_cache_lock);
    cache = s_cache;
    portEXIT_CRITICAL(&s_cache_lock);

    data->point.x = (lv_coord_t)cache.x;
    data->point.y = (lv_coord_t)cache.y;
    /* LVGL 据此状态机产生点击/释放/滑动事件 */
    data->state = cache.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* 触摸任务写入共享缓存（带坐标旋转到横屏） */
static void touch_cache_update(bool pressed, uint16_t x, uint16_t y)
{
    if (pressed) {
        uint16_t lx = y;
        uint16_t ly = (TOUCH_PHYS_W > x) ? (TOUCH_PHYS_W - 1U - x) : 0;
        x = lx;
        y = ly;
    }

    /* 临界区：关中断保护共享数据（微秒级，可接受） */
    portENTER_CRITICAL(&s_cache_lock);
    s_cache.pressed = pressed;
    if (pressed) {
        s_cache.x = x;
        s_cache.y = y;
    }
    s_cache.sequence++;
    portEXIT_CRITICAL(&s_cache_lock);
}

static void touch_input_task(void *arg)
{
    (void)arg;
    uint32_t error_count = 0;
    bool was_pressed = false;

    ESP_LOGI(TAG, "Touch polling task started");

    /* 轮询循环：20ms 一次，触摸是低速设备，无需中断驱动 */
    while (true) {
        touch_point_t points[1] = {0};
        uint8_t count = 0;
        esp_err_t ret = touch_hal_get_instance()->read(points, &count, 1);

        if (ret == ESP_OK) {
            error_count = 0;
            if (count > 0 && points[0].touched) {
                touch_cache_update(true, points[0].x, points[0].y);
                was_pressed = true;
            } else {
                if (was_pressed) {
                    touch_cache_update(false, 0, 0);
                    was_pressed = false;
                }
            }
        } else {
            if (was_pressed) {
                touch_cache_update(false, 0, 0);
                was_pressed = false;
            }
            error_count++;
            if ((error_count % 100) == 1) {
                ESP_LOGW(TAG, "Touch read failed: %d", ret);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));
    }
}

esp_err_t touch_input_service_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    if (!display_app_is_ready()) {
        ESP_LOGW(TAG, "Display app not ready, touch input disabled");
        return ESP_ERR_INVALID_STATE;
    }

    lv_display_t *disp = display_hal_get_lv_display();
    if (!disp) {
        ESP_LOGW(TAG, "No LVGL display, touch input disabled");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = touch_hal_get_instance()->init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Touch HAL init failed, touch disabled: %d", ret);
        return ret;
    }

    if (!mipi_dsi_lcd_lock(1000)) {
        ESP_LOGW(TAG, "Failed to lock LVGL for touch registration");
        return ESP_ERR_TIMEOUT;
    }

    s_indev = lv_indev_create();
    if (s_indev) {
        lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(s_indev, disp);
        lv_indev_set_read_cb(s_indev, touch_lvgl_read_cb);
        lv_indev_set_gesture_min_distance(s_indev, 35);
        lv_indev_set_gesture_min_velocity(s_indev, 3);
    }

    mipi_dsi_lcd_unlock();

    if (!s_indev) {
        ESP_LOGW(TAG, "LVGL touch input allocation failed");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(touch_input_task,
                                            "touch_in",
                                            TOUCH_INPUT_TASK_STACK,
                                            NULL,
                                            TOUCH_INPUT_TASK_PRIO,
                                            &s_task,
                                            TOUCH_INPUT_TASK_CORE);
    if (ok != pdPASS) {
        ESP_LOGW(TAG, "Failed to create touch polling task");
        return ESP_FAIL;
    }

    s_started = true;
    ESP_LOGI(TAG, "Touch input service started");
    return ESP_OK;
}
