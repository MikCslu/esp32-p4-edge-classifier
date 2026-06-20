idf_component_register(SRCS
    "app_main.c"
    "app/audio_event_app.c"
    "app/display_app.c"
    "service/app_config_service.c"
    "service/alert_feedback_service.c"
    "service/audio_classify_service.c"
    "service/audio_frame_bus.c"
    "service/app_state.c"
    "service/camera_service.c"
    "service/event_service.c"
    "service/touch_input_service.c"
    "service/telemetry_service.c"
    "service/visual_classify_service.c"
    "tasks/audio_capture_task.c"
    "tasks/audio_inference_task.c"
    "tasks/ui_task.c"
    "lvgl_port/lvgl_port.c"
    "lvgl_port/ui/ui_main.c"
    "lvgl_port/ui/ui_log.c"
    "lvgl_port/ui/ui_settings.c"
    "lvgl_port/ui/ui_notify.c"
    "lvgl_port/ui/ui_emotion.c"
    "lvgl_port/ui/ui_theme.c"
    "hal/display_hal.c"
    "hal/audio_hal.c"
    "hal/mic_hal.c"
    "hal/touch_hal.c"
    "driver/display/dsi_lcd.c"
    "driver/actuator/motor_driver.c"
    "driver/audio/es8311.c"
    "driver/audio/max98357_speaker.c"
    "driver/touch/gt911.c"
    "driver/sensor/sc2336_camera.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp-dl lvgl__lvgl espressif__esp_lvgl_port
                              nvs_flash
                              esp_driver_ppa
                              waveshare__esp32_p4_platform
                              espressif__esp_codec_dev
                              espressif__esp_lcd_touch
                              espressif__esp_lcd_touch_gt911
                              espressif__esp_cam_sensor
                              espressif__esp_video
)

# ESP-DL requires C++ compilation
set_source_files_properties(service/audio_classify_service.c PROPERTIES LANGUAGE CXX)
set_source_files_properties(service/visual_classify_service.c PROPERTIES LANGUAGE CXX)
set_source_files_properties(tasks/audio_inference_task.c PROPERTIES LANGUAGE CXX)
set_source_files_properties(app_main.c PROPERTIES LANGUAGE CXX)

target_add_binary_data(${COMPONENT_LIB}
    "${CMAKE_CURRENT_SOURCE_DIR}/models/emotion_model.espdl"
    RENAME_TO "emotion_model_espdl"
)

target_add_binary_data(${COMPONENT_LIB}
    "${CMAKE_CURRENT_SOURCE_DIR}/models/espdet_pico_224_224_face.espdl"
    RENAME_TO "espdet_pico_224_224_face_espdl"
)
