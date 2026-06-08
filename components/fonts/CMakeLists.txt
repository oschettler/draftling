set(srcs
    "greybeard.c"
    "greybeard_11.c"
    "greybeard_14.c"
    "greybeard_16.c"
    "greybeard_18.c"
    "greybeard_22.c"
    "greybeard_26.c"
)

if(CONFIG_KB_LAYOUT_ENABLE_UA)
    list(APPEND srcs
        "greybeard_cyrillic_11.c"
        "greybeard_cyrillic_14.c"
        "greybeard_cyrillic_16.c"
        "greybeard_cyrillic_18.c"
        "greybeard_cyrillic_22.c"
        "greybeard_cyrillic_26.c"
    )
endif()

if(CONFIG_KB_LAYOUT_ENABLE_HE)
    list(APPEND srcs
        "greybeard_hebrew_11.c"
        "greybeard_hebrew_14.c"
        "greybeard_hebrew_16.c"
        "greybeard_hebrew_18.c"
        "greybeard_hebrew_22.c"
        "greybeard_hebrew_26.c"
    )
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES lvgl__lvgl
)
