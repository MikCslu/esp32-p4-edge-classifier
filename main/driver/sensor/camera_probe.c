/**
 * @file camera_probe.c
 * @brief Minimal camera SCCB/I2C probe - tests if SC2336 sensor responds
 */
#include "driver/i2c_master.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CAM_PROBE";

#define SC2336_SCCB_ADDR    0x30
#define SC2336_CHIP_ID_HIGH 0x3107
#define SC2336_CHIP_ID_LOW  0x3108

static esp_err_t sccb_read_reg(i2c_master_dev_handle_t dev,
                                uint16_t reg, uint8_t *val)
{
    uint8_t reg_buf[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    return i2c_master_transmit_receive(dev, reg_buf, 2, val, 1, 100);
}

/* Test using the existing BSP I2C handle (GPIO 7,8 on I2C_NUM_1) */
static bool test_bsp_i2c(void)
{
    ESP_LOGI(TAG, "--- Test 0: BSP shared I2C (GPIO 7,8) ---");

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) {
        ESP_LOGE(TAG, "  BSP I2C handle is NULL!");
        return false;
    }

    esp_err_t ret = i2c_master_probe(bus, SC2336_SCCB_ADDR, 100);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "  Probe 0x30 FAILED: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "  Address 0x30 ACK on BSP I2C ✓");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SC2336_SCCB_ADDR,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t dev = NULL;
    ret = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "  Add device failed: %s", esp_err_to_name(ret));
        return false;
    }

    uint8_t id_high = 0, id_low = 0;
    ret = sccb_read_reg(dev, SC2336_CHIP_ID_HIGH, &id_high);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "  Read CHIP_ID_HIGH FAILED: %s", esp_err_to_name(ret));
        i2c_master_bus_rm_device(dev);
        return false;
    }
    ret = sccb_read_reg(dev, SC2336_CHIP_ID_LOW, &id_low);
    i2c_master_bus_rm_device(dev);

    uint16_t chip_id = ((uint16_t)id_high << 8) | id_low;
    ESP_LOGI(TAG, "  CHIP_ID = 0x%04X %s", chip_id,
             (chip_id == 0x2336) ? "MATCH ✓" : "(unexpected)");
    return (ret == ESP_OK);
}

/* Test on I2C_NUM_0 with different GPIO pairs */
typedef struct {
    int sda, scl;
    const char *desc;
} pin_pair_t;

static const pin_pair_t ALT_PINS[] = {
    /* Waveshare ESP32-P4 board has MIPI-CSI on a dedicated connector.
     * SCCB pins are often different from the touch/codec I2C bus.
     * Common ESP32-P4 camera SCCB pins: */
    { 17, 18, "GPIO 17,18" },
    { 15, 16, "GPIO 15,16" },
    { 5, 6,   "GPIO 5,6" },
    { 19, 20, "GPIO 19,20" },
    { 21, 22, "GPIO 21,22" },
    { 47, 48, "GPIO 47,48" },
};

static bool test_i2c_port0(int sda, int scl, const char *desc)
{
    ESP_LOGI(TAG, "--- Test: %s on I2C_NUM_0 ---", desc);

    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = 0,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus = NULL;
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &bus);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "  i2c_new_master_bus failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = i2c_master_probe(bus, SC2336_SCCB_ADDR, 100);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "  Probe 0x30 FAILED: %s", esp_err_to_name(ret));
        i2c_del_master_bus(bus);
        return false;
    }
    ESP_LOGI(TAG, "  Address 0x30 ACK ✓");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SC2336_SCCB_ADDR,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t dev = NULL;
    ret = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (ret != ESP_OK) {
        i2c_del_master_bus(bus);
        return false;
    }

    uint8_t id_high = 0, id_low = 0;
    ret = sccb_read_reg(dev, SC2336_CHIP_ID_HIGH, &id_high);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "  Read CHIP_ID_HIGH FAILED");
        i2c_master_bus_rm_device(dev);
        i2c_del_master_bus(bus);
        return false;
    }
    ret = sccb_read_reg(dev, SC2336_CHIP_ID_LOW, &id_low);
    i2c_master_bus_rm_device(dev);
    i2c_del_master_bus(bus);

    uint16_t chip_id = ((uint16_t)id_high << 8) | id_low;
    ESP_LOGI(TAG, "  CHIP_ID = 0x%04X %s", chip_id,
             (chip_id == 0x2336) ? "MATCH ✓" : "(unexpected)");
    return (ret == ESP_OK);
}

void camera_probe_run(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " SC2336 Camera SCCB Probe v2");
    ESP_LOGI(TAG, "========================================");

    /* Test 0: use the BSP's existing I2C handle */
    bool found = test_bsp_i2c();
    if (found) {
        ESP_LOGI(TAG, "✓ BSP I2C works! Fix esp_video SCCB config.");
        return;
    }

    /* Test 1-N: try I2C_NUM_0 with different pin pairs */
    ESP_LOGI(TAG, "BSP I2C failed, trying I2C_NUM_0 alternatives...");
    for (int i = 0; i < sizeof(ALT_PINS) / sizeof(ALT_PINS[0]); i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (test_i2c_port0(ALT_PINS[i].sda, ALT_PINS[i].scl, ALT_PINS[i].desc)) {
            found = true;
            break;
        }
    }

    if (found) {
        ESP_LOGI(TAG, "✓ Camera SCCB found! Fix esp_video_init config.");
    } else {
        ESP_LOGE(TAG, "✗ Camera SCCB NOT found on any tested pins.");
        ESP_LOGE(TAG, "  Suspect: power/reset/MCLK or physical connection.");
    }
}
