cmake_minimum_required(VERSION 3.22)

set(COMPONENTS main)
set(EXCLUDE_COMPONENTS
    espressif__esp32_p4_function_ev_board
    espressif__esp_lcd_ili9881c
    espressif__esp_lcd_lt8912b
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-p4-edge-classifier)
