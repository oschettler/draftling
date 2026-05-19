idf_component_register(
    SRCS "touchscreen.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_i2c esp_driver_gpio lvgl__lvgl
)
