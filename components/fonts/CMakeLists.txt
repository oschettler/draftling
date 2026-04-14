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

if(CONFIG_KB_LAYOUT_ENABLE_KO OR CONFIG_KB_LAYOUT_ENABLE_JA OR CONFIG_KB_LAYOUT_ENABLE_ZH)
    list(APPEND FONT_SRCS
        "montserrat_cjk_10.c"
        "montserrat_cjk_12.c"
        "montserrat_cjk_14.c"
        "montserrat_cjk_16.c"
        "montserrat_cjk_18.c"
    )
endif()

if(FONT_SRCS)
    idf_component_register(
        SRCS ${FONT_SRCS}
        INCLUDE_DIRS "include"
        PRIV_REQUIRES lvgl__lvgl
    )
else()
    idf_component_register(
        INCLUDE_DIRS "include"
    )
endif()
