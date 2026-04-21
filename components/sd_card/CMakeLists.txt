idf_component_register(
    SRCS "sd_card.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES fatfs sdmmc vfs driver
)
