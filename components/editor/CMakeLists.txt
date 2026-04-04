idf_component_register(
    SRCS "editor.cpp" "md_parser.cpp" "editor_ui.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES display sd_card bt_keyboard lvgl__lvgl
)
