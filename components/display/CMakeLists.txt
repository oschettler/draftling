idf_component_register(
    SRCS "display_rlcd.cpp" "display_uc8179.cpp" "display_eds3.cpp" "lvgl_port.cpp" "lv_mem_psram.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES driver esp_lcd esp_timer lvgl__lvgl
)
