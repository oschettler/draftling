set(FONT_SRCS "")

if(CONFIG_KB_LAYOUT_ENABLE_UA OR CONFIG_KB_LAYOUT_ENABLE_DE OR CONFIG_KB_LAYOUT_ENABLE_FR)
    list(APPEND FONT_SRCS
        "montserrat_cyrillic_10.c"
        "montserrat_cyrillic_12.c"
        "montserrat_cyrillic_14.c"
        "montserrat_cyrillic_16.c"
        "montserrat_cyrillic_18.c"
    )
endif()

if(FONT_SRCS)
    idf_component_register(
        SRCS ${FONT_SRCS}
        INCLUDE_DIRS "include"
        PRIV_REQUIRES lvgl__lvgl
    )
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
else()
    idf_component_register(
        INCLUDE_DIRS "include"
    )
endif()
