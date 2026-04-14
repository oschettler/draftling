#pragma once

/*
 * Custom Montserrat fonts with Latin + Cyrillic glyph coverage.
 * Generated from Google Montserrat-Regular.ttf via lv_font_conv.
 * Unicode ranges: 0x0020-0x007F, 0x00A0-0x00FF, 0x0400-0x04FF.
 *
 * These fonts are compiled only when a non-ASCII layout
 * (Ukrainian, German, or French) is enabled in Kconfig.
 */

#include "sdkconfig.h"

#if defined(CONFIG_KB_LAYOUT_ENABLE_UA) || \
    defined(CONFIG_KB_LAYOUT_ENABLE_DE) || \
    defined(CONFIG_KB_LAYOUT_ENABLE_FR)

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t montserrat_cyrillic_10;
extern const lv_font_t montserrat_cyrillic_12;
extern const lv_font_t montserrat_cyrillic_14;
extern const lv_font_t montserrat_cyrillic_16;
extern const lv_font_t montserrat_cyrillic_18;

#ifdef __cplusplus
}
#endif

#define HAVE_CYRILLIC_FONTS 1
#else
#define HAVE_CYRILLIC_FONTS 0
#endif
