if(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)
    set(DISPLAY_SRC "display_epaper.cpp")
else()
    set(DISPLAY_SRC "display.cpp")
endif()

idf_component_register(
    SRCS "${DISPLAY_SRC}" "lvgl_port.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES driver esp_lcd esp_timer lvgl__lvgl
)
