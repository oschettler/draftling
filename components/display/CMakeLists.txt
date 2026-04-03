idf_component_register(
    SRCS "display.cpp" "lvgl_port.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES driver esp_lcd esp_timer
)
