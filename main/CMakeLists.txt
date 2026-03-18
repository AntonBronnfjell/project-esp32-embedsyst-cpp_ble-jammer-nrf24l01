idf_component_register(SRCS "jammer_main.cpp" "led_strip_encoder.c"
                    INCLUDE_DIRS "."
                    REQUIRES RF24 driver esp_wifi nvs_flash bt esp_driver_rmt)
