idf_component_register(
    SRCS "editor.cpp" "md_parser.cpp" "editor_ui.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES display sd_card ble_keyboard kb_layout wifi_manager git_sync standby lvgl__lvgl fonts
)
