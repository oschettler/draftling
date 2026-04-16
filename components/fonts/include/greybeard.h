#pragma once

/*
 * Greybeard fonts for LVGL.
 * Generated from Greybeard TTF v1.0.0 via lv_font_conv.
 * Unicode ranges: 0x0020-0x007F, 0x00A0-0x00FF, 0x0400-0x04FF.
 *
 * Greybeard is a monospaced bitmap font (vector port of UW ttyp0).
 * Each TTF file is designed for a single native pixel size, so
 * every size below renders pixel-perfect at 1 bpp with no scaling.
 *
 * Native sizes and metrics (advance widths in 1/16-px units):
 *   11 px: adv_w  96 -> char width  6, line_height 11
 *   14 px: adv_w 112 -> char width  7, line_height 13
 *   16 px: adv_w 128 -> char width  8, line_height 15
 *   18 px: adv_w 144 -> char width  9, line_height 17
 *
 * All sizes include Latin, Latin-1 Supplement, and Cyrillic
 * coverage, so a single set of fonts serves every layout
 * (US, UA, DE, FR).
 *
 * License: MIT
 * https://github.com/flowchartsman/greybeard
 */

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t greybeard_11;
extern const lv_font_t greybeard_14;
extern const lv_font_t greybeard_16;
extern const lv_font_t greybeard_18;

#ifdef __cplusplus
}
#endif
