#pragma once

/*
 * Custom Montserrat fonts with CJK glyph coverage.
 * Generated from Google Montserrat-Regular.ttf (Latin + Cyrillic) and
 * Noto Sans JP (Jamo, Hiragana, Katakana, Bopomofo) via lv_font_conv.
 * Unicode ranges: 0x0020-0x007F, 0x00A0-0x00FF, 0x0400-0x04FF,
 *   0x3001-0x3003 (CJK punctuation),
 *   0x3041-0x3096 (Hiragana),
 *   0x30A1-0x30FC (Katakana),
 *   0x3105-0x312F (Bopomofo),
 *   0x3131-0x3163 (Hangul Compatibility Jamo).
 *
 * These fonts include Cyrillic coverage as well, so they are a
 * superset of the montserrat_cyrillic fonts.
 *
 * Compiled only when a CJK layout (Korean, Japanese, or Chinese)
 * is enabled in Kconfig.
 */

#include "sdkconfig.h"

#if defined(CONFIG_KB_LAYOUT_ENABLE_KO) || \
    defined(CONFIG_KB_LAYOUT_ENABLE_JA) || \
    defined(CONFIG_KB_LAYOUT_ENABLE_ZH)

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t montserrat_cjk_10;
extern const lv_font_t montserrat_cjk_12;
extern const lv_font_t montserrat_cjk_14;
extern const lv_font_t montserrat_cjk_16;
extern const lv_font_t montserrat_cjk_18;

#ifdef __cplusplus
}
#endif

#define HAVE_CJK_FONTS 1
#else
#define HAVE_CJK_FONTS 0
#endif
