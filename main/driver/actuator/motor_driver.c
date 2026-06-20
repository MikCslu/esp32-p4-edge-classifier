#include "driver/actuator/motor_driver.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "MOTOR_DRV";

#define MOTOR_AIN1_GPIO         GPIO_NUM_21
#define MOTOR_AIN2_GPIO         GPIO_NUM_20
#define MOTOR_PWMA_GPIO         GPIO_NUM_26
#define MOTOR_LEDC_MODE         LEDC_LOW_SPEED_MODE
#define MOTOR_LEDC_TIMER        LEDC_TIMER_2
#define MOTOR_LEDC_CHANNEL      LEDC_CHANNEL_6
#define MOTOR_PWM_FREQ_HZ       5000
#define MOTOR_PWM_RESOLUTION    LEDC_TIMER_12_BIT
#define MOTOR_PWM_MAX_DUTY      ((1U << 12) - 1U)

static bool s_initialized;
static bool s_available;

esp_err_t motor_driver_init(void)
{
    if (s_initialized) {
        return s_available ? ESP_OK : ESP_FAIL;
    }
    s_initialized = true;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MOTOR_AIN1_GPIO) | (1ULL << MOTOR_AIN2_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "GPIO init failed: %d", ret);
        return ret;
    }

    ledc_timer_config_t timer_conf = {
        .speed_mode = MOTOR_LEDC_MODE,
        .duty_resolution = MOTOR_PWM_RESOLUTION,
        .timer_num = MOTOR_LEDC_TIMER,
        .freq_hz = MOTOR_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LEDC timer init failed: %d", ret);
        return ret;
    }

    ledc_channel_config_t channel_conf = {
        .gpio_num = MOTOR_PWMA_GPIO,
        .speed_mode = MOTOR_LEDC_MODE,
        .channel = MOTOR_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = MOTOR_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = 0,
    };
    ret = ledc_channel_config(&channel_conf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LEDC channel init failed: %d", ret);
        return ret;
    }

    gpio_set_level(MOTOR_AIN1_GPIO, 1);
    gpio_set_level(MOTOR_AIN2_GPIO, 0);
    s_available = true;
    ESP_LOGI(TAG, "Motor ready: AIN1=%d AIN2=%d PWMA=%d",
             MOTOR_AIN1_GPIO, MOTOR_AIN2_GPIO, MOTOR_PWMA_GPIO);
    return ESP_OK;
}

bool motor_driver_is_available(void)
{
    return s_available;
}

void motor_driver_set_power(uint8_t percent)
{
    if (!s_available) {
        return;
    }
    if (percent > 100) {
        percent = 100;
    }
    uint32_t duty = (MOTOR_PWM_MAX_DUTY * percent) / 100U;
    gpio_set_level(MOTOR_AIN1_GPIO, percent > 0 ? 1 : 0);
    gpio_set_level(MOTOR_AIN2_GPIO, 0);
    ledc_set_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CHANNEL, duty);
    ledc_update_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CHANNEL);
}

void motor_driver_stop(void)
{
    if (!s_available) {
        return;
    }
    ledc_set_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CHANNEL, 0);
    ledc_update_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CHANNEL);
    gpio_set_level(MOTOR_AIN1_GPIO, 0);
    gpio_set_level(MOTOR_AIN2_GPIO, 0);
}
