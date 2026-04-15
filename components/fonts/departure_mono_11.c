/*******************************************************************************
 * Size: 11 px
 * Bpp: 1
 * Opts: --font /tmp/DepartureMono-Regular.otf -r 0x20-0x7F,0xA0-0xFF,0x400-0x4FF --size 11 --bpp 1 --format lvgl --no-compress --lv-font-name departure_mono_11 -o /tmp/font_output/departure_mono_11.c
 ******************************************************************************/

#include "lvgl.h"

#ifndef DEPARTURE_MONO_11
#define DEPARTURE_MONO_11 1
#endif

#if DEPARTURE_MONO_11

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0,

    /* U+0021 "!" */
    0xfd,

    /* U+0022 "\"" */
    0xb6, 0x80,

    /* U+0023 "#" */
    0x14, 0x29, 0xf9, 0x42, 0x9f, 0x94, 0x28,

    /* U+0024 "$" */
    0x23, 0xab, 0x47, 0x14, 0xb5, 0x71, 0x0,

    /* U+0025 "%" */
    0x46, 0x94, 0x84, 0x21, 0x29, 0x62,

    /* U+0026 "&" */
    0x62, 0x48, 0x11, 0x9a, 0x49, 0x18,

    /* U+0027 "'" */
    0xe0,

    /* U+0028 "(" */
    0x2a, 0x49, 0x24, 0x44,

    /* U+0029 ")" */
    0x88, 0x92, 0x49, 0x50,

    /* U+002A "*" */
    0x25, 0x5d, 0x52, 0x0,

    /* U+002B "+" */
    0x21, 0x3e, 0x42, 0x0,

    /* U+002C "," */
    0x58,

    /* U+002D "-" */
    0xe0,

    /* U+002E "." */
    0xc0,

    /* U+002F "/" */
    0x8, 0x44, 0x22, 0x11, 0x8, 0x84, 0x0,

    /* U+0030 "0" */
    0x74, 0x67, 0x5c, 0xc6, 0x2e,

    /* U+0031 "1" */
    0x27, 0x8, 0x42, 0x10, 0x9f,

    /* U+0032 "2" */
    0x74, 0x42, 0x22, 0x22, 0x1f,

    /* U+0033 "3" */
    0x74, 0x42, 0x60, 0x86, 0x2e,

    /* U+0034 "4" */
    0x19, 0x53, 0x18, 0xfc, 0x21,

    /* U+0035 "5" */
    0xfc, 0x21, 0xe0, 0x86, 0x2e,

    /* U+0036 "6" */
    0x74, 0x61, 0xe8, 0xc6, 0x2e,

    /* U+0037 "7" */
    0xf8, 0x42, 0x22, 0x21, 0x8,

    /* U+0038 "8" */
    0x74, 0x62, 0xe8, 0xc6, 0x2e,

    /* U+0039 "9" */
    0x74, 0x63, 0x17, 0x86, 0x2e,

    /* U+003A ":" */
    0xcc,

    /* U+003B ";" */
    0x50, 0x58,

    /* U+003C "<" */
    0x12, 0x48, 0x42, 0x10,

    /* U+003D "=" */
    0xf8, 0x3e,

    /* U+003E ">" */
    0x84, 0x21, 0x24, 0x80,

    /* U+003F "?" */
    0x74, 0x42, 0x22, 0x10, 0x4,

    /* U+0040 "@" */
    0x74, 0x42, 0xda, 0xd6, 0xaa,

    /* U+0041 "A" */
    0x22, 0xa3, 0x1f, 0xc6, 0x31,

    /* U+0042 "B" */
    0xf4, 0x63, 0xe8, 0xc6, 0x3e,

    /* U+0043 "C" */
    0x74, 0x61, 0x8, 0x42, 0x2e,

    /* U+0044 "D" */
    0xf4, 0x63, 0x18, 0xc6, 0x3e,

    /* U+0045 "E" */
    0xfc, 0x21, 0xe8, 0x42, 0x1f,

    /* U+0046 "F" */
    0xfc, 0x21, 0xe8, 0x42, 0x10,

    /* U+0047 "G" */
    0x74, 0x61, 0x9, 0xc6, 0x2e,

    /* U+0048 "H" */
    0x8c, 0x63, 0xf8, 0xc6, 0x31,

    /* U+0049 "I" */
    0xf9, 0x8, 0x42, 0x10, 0x9f,

    /* U+004A "J" */
    0x38, 0x42, 0x10, 0x86, 0x2e,

    /* U+004B "K" */
    0x8c, 0xa9, 0xc9, 0x4a, 0x31,

    /* U+004C "L" */
    0x84, 0x21, 0x8, 0x42, 0x1f,

    /* U+004D "M" */
    0x8c, 0x77, 0x5a, 0xc6, 0x31,

    /* U+004E "N" */
    0x8c, 0x73, 0x59, 0xc6, 0x31,

    /* U+004F "O" */
    0x74, 0x63, 0x18, 0xc6, 0x2e,

    /* U+0050 "P" */
    0xf4, 0x63, 0x1f, 0x42, 0x10,

    /* U+0051 "Q" */
    0x74, 0x63, 0x18, 0xc6, 0x2e, 0x18,

    /* U+0052 "R" */
    0xf4, 0x63, 0x1f, 0x4a, 0x31,

    /* U+0053 "S" */
    0x74, 0x60, 0xe0, 0x86, 0x2e,

    /* U+0054 "T" */
    0xf9, 0x8, 0x42, 0x10, 0x84,

    /* U+0055 "U" */
    0x8c, 0x63, 0x18, 0xc6, 0x2e,

    /* U+0056 "V" */
    0x8c, 0x63, 0x18, 0xa9, 0x44,

    /* U+0057 "W" */
    0x8c, 0x63, 0x5a, 0xa9, 0x4a,

    /* U+0058 "X" */
    0x8c, 0x54, 0x45, 0x46, 0x31,

    /* U+0059 "Y" */
    0x8c, 0x62, 0xa2, 0x10, 0x84,

    /* U+005A "Z" */
    0xf8, 0x44, 0x44, 0x42, 0x1f,

    /* U+005B "[" */
    0xf2, 0x49, 0x24, 0x9c,

    /* U+005C "\\" */
    0x84, 0x10, 0x82, 0x10, 0x42, 0x8, 0x40,

    /* U+005D "]" */
    0xe4, 0x92, 0x49, 0x3c,

    /* U+005E "^" */
    0x54,

    /* U+005F "_" */
    0xf8,

    /* U+0060 "`" */
    0x90,

    /* U+0061 "a" */
    0x70, 0x5f, 0x19, 0xb4,

    /* U+0062 "b" */
    0x84, 0x2d, 0x98, 0xc7, 0x36,

    /* U+0063 "c" */
    0x74, 0x61, 0x8, 0xb8,

    /* U+0064 "d" */
    0x8, 0x5b, 0x38, 0xc6, 0x6d,

    /* U+0065 "e" */
    0x74, 0x7f, 0x8, 0xb8,

    /* U+0066 "f" */
    0x3a, 0x3e, 0x84, 0x21, 0x1e,

    /* U+0067 "g" */
    0x6c, 0xe3, 0x19, 0xb4, 0x2e,

    /* U+0068 "h" */
    0x84, 0x2d, 0x98, 0xc6, 0x31,

    /* U+0069 "i" */
    0x20, 0x38, 0x42, 0x10, 0x9f,

    /* U+006A "j" */
    0x10, 0x71, 0x11, 0x11, 0x96,

    /* U+006B "k" */
    0x84, 0x23, 0x2a, 0x72, 0x51,

    /* U+006C "l" */
    0xe1, 0x8, 0x42, 0x10, 0x9f,

    /* U+006D "m" */
    0xd5, 0x6b, 0x5a, 0xd4,

    /* U+006E "n" */
    0xb6, 0x63, 0x18, 0xc4,

    /* U+006F "o" */
    0x74, 0x63, 0x18, 0xb8,

    /* U+0070 "p" */
    0xb6, 0x63, 0x1c, 0xda, 0x10,

    /* U+0071 "q" */
    0x6c, 0xe3, 0x19, 0xb4, 0x21,

    /* U+0072 "r" */
    0xdb, 0x10, 0x84, 0x78,

    /* U+0073 "s" */
    0x74, 0x5c, 0x18, 0xb8,

    /* U+0074 "t" */
    0x42, 0x3e, 0x84, 0x21, 0x26,

    /* U+0075 "u" */
    0x8c, 0x63, 0x19, 0xb4,

    /* U+0076 "v" */
    0x8c, 0x62, 0xa5, 0x10,

    /* U+0077 "w" */
    0xad, 0x6b, 0x5a, 0xac,

    /* U+0078 "x" */
    0x8a, 0x88, 0xa8, 0xc4,

    /* U+0079 "y" */
    0x8c, 0x62, 0xa5, 0x10, 0x98,

    /* U+007A "z" */
    0xf8, 0x88, 0x88, 0x7c,

    /* U+007B "{" */
    0x19, 0x8, 0x4c, 0x10, 0x84, 0x20, 0xc0,

    /* U+007C "|" */
    0xff, 0xc0,

    /* U+007D "}" */
    0xc1, 0x8, 0x41, 0x90, 0x84, 0x26, 0x0,

    /* U+007E "~" */
    0x45, 0x44,

    /* U+00A0 " " */
    0x0,

    /* U+00A1 "¡" */
    0xbf,

    /* U+00A2 "¢" */
    0x23, 0xab, 0x4a, 0xb8, 0x80,

    /* U+00A3 "£" */
    0x32, 0x50, 0x8f, 0x21, 0x1f,

    /* U+00A4 "¤" */
    0xaa, 0xa3, 0x15, 0x54,

    /* U+00A5 "¥" */
    0x8c, 0x54, 0x4f, 0x93, 0xe4,

    /* U+00A6 "¦" */
    0xf3, 0xc0,

    /* U+00A7 "§" */
    0x74, 0x70, 0xe8, 0xb8, 0x71, 0x70,

    /* U+00A8 "¨" */
    0xa0,

    /* U+00A9 "©" */
    0x74, 0x40, 0x45, 0x21, 0xa, 0x20, 0x22, 0xe0,

    /* U+00AA "ª" */
    0x7c, 0x63, 0x36, 0x80,

    /* U+00AB "«" */
    0x25, 0x29, 0x12, 0x24,

    /* U+00AC "¬" */
    0xf8, 0x42,

    /* U+00AD "­" */
    0xe0,

    /* U+00AE "®" */
    0x74, 0x40, 0xc5, 0x29, 0x8a, 0x50, 0x22, 0xe0,

    /* U+00AF "¯" */
    0xe0,

    /* U+00B0 "°" */
    0x55, 0x0,

    /* U+00B1 "±" */
    0x21, 0x3e, 0x42, 0x3, 0xe0,

    /* U+00B2 "²" */
    0x74, 0x44, 0x44, 0x7c,

    /* U+00B3 "³" */
    0x74, 0x4c, 0x18, 0xb8,

    /* U+00B4 "´" */
    0x60,

    /* U+00B5 "µ" */
    0x8c, 0x63, 0x19, 0xf6, 0x10,

    /* U+00B6 "¶" */
    0x7f, 0x7b, 0xd6, 0x84, 0x21,

    /* U+00B7 "·" */
    0xc0,

    /* U+00B8 "¸" */
    0x98,

    /* U+00B9 "¹" */
    0x59, 0x25, 0xc0,

    /* U+00BA "º" */
    0x74, 0x63, 0x17, 0x0,

    /* U+00BB "»" */
    0x91, 0x22, 0x52, 0x90,

    /* U+00BC "¼" */
    0x41, 0x81, 0x2, 0xe, 0x0, 0xce, 0x60, 0x6,
    0x14, 0x48, 0xf0, 0x20,

    /* U+00BD "½" */
    0x41, 0x81, 0x2, 0xe, 0x0, 0xce, 0x60, 0xc,
    0x24, 0x10, 0x41, 0xe0,

    /* U+00BE "¾" */
    0x61, 0x20, 0x84, 0x86, 0x0, 0xce, 0x60, 0x6,
    0x14, 0x48, 0xf0, 0x20,

    /* U+00BF "¿" */
    0x20, 0x8, 0x44, 0x42, 0x2e,

    /* U+00C0 "À" */
    0x41, 0x0, 0x45, 0x46, 0x3f, 0x8c, 0x62,

    /* U+00C1 "Á" */
    0x11, 0x0, 0x45, 0x46, 0x3f, 0x8c, 0x62,

    /* U+00C2 "Â" */
    0x22, 0x80, 0x45, 0x46, 0x3f, 0x8c, 0x62,

    /* U+00C3 "Ã" */
    0x2a, 0x80, 0x45, 0x46, 0x3f, 0x8c, 0x62,

    /* U+00C4 "Ä" */
    0x50, 0x8, 0xa8, 0xc7, 0xf1, 0x8c, 0x40,

    /* U+00C5 "Å" */
    0x22, 0x88, 0x45, 0x46, 0x3f, 0x8c, 0x62,

    /* U+00C6 "Æ" */
    0x3e, 0xa2, 0x44, 0xef, 0x12, 0x24, 0x4f,

    /* U+00C7 "Ç" */
    0x74, 0x61, 0x8, 0x42, 0x2e, 0x20, 0x88,

    /* U+00C8 "È" */
    0x41, 0x1, 0xf8, 0x43, 0xd0, 0x84, 0x3e,

    /* U+00C9 "É" */
    0x11, 0x1, 0xf8, 0x43, 0xd0, 0x84, 0x3e,

    /* U+00CA "Ê" */
    0x22, 0x81, 0xf8, 0x43, 0xd0, 0x84, 0x3e,

    /* U+00CB "Ë" */
    0x50, 0x3f, 0x8, 0x7a, 0x10, 0x87, 0xc0,

    /* U+00CC "Ì" */
    0x41, 0x1, 0xf2, 0x10, 0x84, 0x21, 0x3e,

    /* U+00CD "Í" */
    0x11, 0x1, 0xf2, 0x10, 0x84, 0x21, 0x3e,

    /* U+00CE "Î" */
    0x22, 0x81, 0xf2, 0x10, 0x84, 0x21, 0x3e,

    /* U+00CF "Ï" */
    0x50, 0x3e, 0x42, 0x10, 0x84, 0x27, 0xc0,

    /* U+00D0 "Ð" */
    0x79, 0x14, 0x7d, 0x45, 0x14, 0x5e,

    /* U+00D1 "Ñ" */
    0x2a, 0x81, 0x18, 0xe6, 0xb3, 0x8c, 0x62,

    /* U+00D2 "Ò" */
    0x41, 0x0, 0xe8, 0xc6, 0x31, 0x8c, 0x5c,

    /* U+00D3 "Ó" */
    0x11, 0x0, 0xe8, 0xc6, 0x31, 0x8c, 0x5c,

    /* U+00D4 "Ô" */
    0x22, 0x80, 0xe8, 0xc6, 0x31, 0x8c, 0x5c,

    /* U+00D5 "Õ" */
    0x2a, 0x80, 0xe8, 0xc6, 0x31, 0x8c, 0x5c,

    /* U+00D6 "Ö" */
    0x50, 0x1d, 0x18, 0xc6, 0x31, 0x8b, 0x80,

    /* U+00D7 "×" */
    0x8a, 0x88, 0xa8, 0x80,

    /* U+00D8 "Ø" */
    0x3a, 0x89, 0x32, 0xa6, 0x48, 0xb1, 0x1c,

    /* U+00D9 "Ù" */
    0x41, 0x1, 0x18, 0xc6, 0x31, 0x8c, 0x5c,

    /* U+00DA "Ú" */
    0x11, 0x1, 0x18, 0xc6, 0x31, 0x8c, 0x5c,

    /* U+00DB "Û" */
    0x22, 0x81, 0x18, 0xc6, 0x31, 0x8c, 0x5c,

    /* U+00DC "Ü" */
    0x50, 0x23, 0x18, 0xc6, 0x31, 0x8b, 0x80,

    /* U+00DD "Ý" */
    0x11, 0x1, 0x18, 0xc5, 0x44, 0x21, 0x8,

    /* U+00DE "Þ" */
    0x87, 0xa3, 0x18, 0xfa, 0x10,

    /* U+00DF "ß" */
    0x64, 0xa5, 0x28, 0xc6, 0x36, 0x84, 0x0,

    /* U+00E0 "à" */
    0x41, 0x0, 0xe0, 0xbe, 0x33, 0x68,

    /* U+00E1 "á" */
    0x11, 0x0, 0xe0, 0xbe, 0x33, 0x68,

    /* U+00E2 "â" */
    0x22, 0x80, 0xe0, 0xbe, 0x33, 0x68,

    /* U+00E3 "ã" */
    0x2a, 0x80, 0xe0, 0xbe, 0x33, 0x68,

    /* U+00E4 "ä" */
    0x50, 0x1c, 0x17, 0xc6, 0x6d,

    /* U+00E5 "å" */
    0x22, 0x88, 0x7, 0x5, 0xf1, 0x9b, 0x40,

    /* U+00E6 "æ" */
    0x6c, 0x25, 0xfc, 0x89, 0x2d, 0x80,

    /* U+00E7 "ç" */
    0x74, 0x61, 0x8, 0xb8, 0x82, 0x20,

    /* U+00E8 "è" */
    0x41, 0x0, 0xe8, 0xfe, 0x11, 0x70,

    /* U+00E9 "é" */
    0x11, 0x0, 0xe8, 0xfe, 0x11, 0x70,

    /* U+00EA "ê" */
    0x22, 0x80, 0xe8, 0xfe, 0x11, 0x70,

    /* U+00EB "ë" */
    0x50, 0x1d, 0x1f, 0xc2, 0x2e,

    /* U+00EC "ì" */
    0x41, 0x1, 0xc2, 0x10, 0x84, 0xf8,

    /* U+00ED "í" */
    0x11, 0x1, 0xc2, 0x10, 0x84, 0xf8,

    /* U+00EE "î" */
    0x22, 0x81, 0xc2, 0x10, 0x84, 0xf8,

    /* U+00EF "ï" */
    0x50, 0x38, 0x42, 0x10, 0x9f,

    /* U+00F0 "ð" */
    0x68, 0x9b, 0x38, 0xc6, 0x2e,

    /* U+00F1 "ñ" */
    0x2a, 0x81, 0x6c, 0xc6, 0x31, 0x88,

    /* U+00F2 "ò" */
    0x41, 0x0, 0xe8, 0xc6, 0x31, 0x70,

    /* U+00F3 "ó" */
    0x11, 0x0, 0xe8, 0xc6, 0x31, 0x70,

    /* U+00F4 "ô" */
    0x22, 0x80, 0xe8, 0xc6, 0x31, 0x70,

    /* U+00F5 "õ" */
    0x2a, 0x80, 0xe8, 0xc6, 0x31, 0x70,

    /* U+00F6 "ö" */
    0x50, 0x1d, 0x18, 0xc6, 0x2e,

    /* U+00F7 "÷" */
    0x20, 0x3e, 0x2, 0x0,

    /* U+00F8 "ø" */
    0x3a, 0x89, 0x32, 0xa6, 0x4f, 0x20, 0x0,

    /* U+00F9 "ù" */
    0x41, 0x1, 0x18, 0xc6, 0x33, 0x68,

    /* U+00FA "ú" */
    0x11, 0x1, 0x18, 0xc6, 0x33, 0x68,

    /* U+00FB "û" */
    0x22, 0x81, 0x18, 0xc6, 0x33, 0x68,

    /* U+00FC "ü" */
    0x50, 0x23, 0x18, 0xc6, 0x6d,

    /* U+00FD "ý" */
    0x11, 0x1, 0x18, 0xc5, 0x4a, 0x21, 0x30,

    /* U+00FE "þ" */
    0x84, 0x2d, 0x98, 0xc7, 0x36, 0x84, 0x0,

    /* U+00FF "ÿ" */
    0x50, 0x23, 0x18, 0xa9, 0x44, 0x26, 0x0,

    /* U+0400 "Ѐ" */
    0x41, 0x1, 0xf8, 0x43, 0xd0, 0x84, 0x3e,

    /* U+0401 "Ё" */
    0x50, 0x3f, 0x8, 0x7a, 0x10, 0x87, 0xc0,

    /* U+0402 "Ђ" */
    0xf1, 0x4, 0x16, 0x65, 0x14, 0x56,

    /* U+0403 "Ѓ" */
    0x11, 0x1, 0xf4, 0xa1, 0x8, 0x42, 0x38,

    /* U+0404 "Є" */
    0x74, 0x61, 0xc8, 0x42, 0x2e,

    /* U+0405 "Ѕ" */
    0x74, 0x60, 0xe0, 0x86, 0x2e,

    /* U+0406 "І" */
    0xf9, 0x8, 0x42, 0x10, 0x9f,

    /* U+0407 "Ї" */
    0x50, 0x3e, 0x42, 0x10, 0x84, 0x27, 0xc0,

    /* U+0408 "Ј" */
    0x38, 0x42, 0x10, 0x86, 0x2e,

    /* U+0409 "Љ" */
    0x70, 0xa1, 0x42, 0xe5, 0x32, 0x64, 0xce,

    /* U+040A "Њ" */
    0x91, 0x22, 0x47, 0xe9, 0x32, 0x64, 0xce,

    /* U+040B "Ћ" */
    0xf1, 0x4, 0x16, 0x65, 0x14, 0x51,

    /* U+040C "Ќ" */
    0x11, 0x1, 0x19, 0x4b, 0x92, 0x94, 0x62,

    /* U+040D "Ѝ" */
    0x41, 0x1, 0x18, 0xce, 0xb9, 0x8c, 0x62,

    /* U+040E "Ў" */
    0x8b, 0x81, 0x18, 0xc5, 0x4a, 0x21, 0x30,

    /* U+040F "Џ" */
    0x8c, 0x63, 0x18, 0xc6, 0x3f, 0x21, 0x0,

    /* U+0410 "А" */
    0x22, 0xa3, 0x1f, 0xc6, 0x31,

    /* U+0411 "Б" */
    0xf4, 0x21, 0xe8, 0xc6, 0x3e,

    /* U+0412 "В" */
    0xf4, 0x63, 0xe8, 0xc6, 0x3e,

    /* U+0413 "Г" */
    0xfa, 0x50, 0x84, 0x21, 0x1c,

    /* U+0414 "Д" */
    0x3c, 0x48, 0x91, 0x22, 0x48, 0x91, 0x7f, 0x83,
    0x4,

    /* U+0415 "Е" */
    0xfc, 0x21, 0xe8, 0x42, 0x1f,

    /* U+0416 "Ж" */
    0x92, 0xa9, 0x51, 0xc5, 0x4a, 0x95, 0x49,

    /* U+0417 "З" */
    0x74, 0x42, 0x60, 0x86, 0x2e,

    /* U+0418 "И" */
    0x8c, 0x67, 0x5c, 0xc6, 0x31,

    /* U+0419 "Й" */
    0x8b, 0x81, 0x18, 0xce, 0xb9, 0x8c, 0x62,

    /* U+041A "К" */
    0x8c, 0xa5, 0xc9, 0x4a, 0x31,

    /* U+041B "Л" */
    0x7a, 0x52, 0x94, 0xc6, 0x31,

    /* U+041C "М" */
    0x8c, 0x77, 0x5a, 0xc6, 0x31,

    /* U+041D "Н" */
    0x8c, 0x63, 0xf8, 0xc6, 0x31,

    /* U+041E "О" */
    0x74, 0x63, 0x18, 0xc6, 0x2e,

    /* U+041F "П" */
    0xfc, 0x63, 0x18, 0xc6, 0x31,

    /* U+0420 "Р" */
    0xf4, 0x63, 0x1f, 0x42, 0x10,

    /* U+0421 "С" */
    0x74, 0x61, 0x8, 0x42, 0x2e,

    /* U+0422 "Т" */
    0xf9, 0x8, 0x42, 0x10, 0x84,

    /* U+0423 "У" */
    0x8c, 0x62, 0xa5, 0x10, 0x98,

    /* U+0424 "Ф" */
    0x23, 0xab, 0x5a, 0xd5, 0xc4,

    /* U+0425 "Х" */
    0x8c, 0x54, 0x45, 0x46, 0x31,

    /* U+0426 "Ц" */
    0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xbf, 0x4, 0x10,

    /* U+0427 "Ч" */
    0x8c, 0x63, 0x36, 0x84, 0x21,

    /* U+0428 "Ш" */
    0xad, 0x6b, 0x5a, 0xd6, 0xbf,

    /* U+0429 "Щ" */
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xbf, 0x4, 0x10,

    /* U+042A "Ъ" */
    0xc1, 0x4, 0x1e, 0x45, 0x14, 0x5e,

    /* U+042B "Ы" */
    0x8c, 0x63, 0x9a, 0xd6, 0xb9,

    /* U+042C "Ь" */
    0x84, 0x21, 0xe8, 0xc6, 0x3e,

    /* U+042D "Э" */
    0x74, 0x42, 0x70, 0x86, 0x2e,

    /* U+042E "Ю" */
    0x9a, 0x9a, 0x79, 0xa6, 0x9a, 0x66,

    /* U+042F "Я" */
    0x7c, 0x63, 0x17, 0xa6, 0x31,

    /* U+0430 "а" */
    0x70, 0x5f, 0x19, 0xb4,

    /* U+0431 "б" */
    0xb, 0xa1, 0xe8, 0xc6, 0x2e,

    /* U+0432 "в" */
    0xf4, 0x7d, 0x18, 0xf8,

    /* U+0433 "г" */
    0xfa, 0x50, 0x84, 0x70,

    /* U+0434 "д" */
    0x3c, 0x48, 0x91, 0x24, 0x5f, 0xe0, 0xc1,

    /* U+0435 "е" */
    0x74, 0x7f, 0x8, 0xb8,

    /* U+0436 "ж" */
    0x92, 0xa8, 0xe2, 0xa5, 0x52, 0x40,

    /* U+0437 "з" */
    0x74, 0x4c, 0x18, 0xb8,

    /* U+0438 "и" */
    0x8c, 0xeb, 0x98, 0xc4,

    /* U+0439 "й" */
    0x8b, 0x81, 0x19, 0xd7, 0x31, 0x88,

    /* U+043A "к" */
    0x8c, 0xa9, 0xc9, 0x44,

    /* U+043B "л" */
    0x7a, 0x52, 0x98, 0xc4,

    /* U+043C "м" */
    0x8e, 0xeb, 0x58, 0xc4,

    /* U+043D "н" */
    0x8c, 0x7f, 0x18, 0xc4,

    /* U+043E "о" */
    0x74, 0x63, 0x18, 0xb8,

    /* U+043F "п" */
    0xfc, 0x63, 0x18, 0xc4,

    /* U+0440 "р" */
    0xb6, 0x63, 0x1c, 0xda, 0x10,

    /* U+0441 "с" */
    0x74, 0x61, 0x8, 0xb8,

    /* U+0442 "т" */
    0xf9, 0x8, 0x42, 0x10,

    /* U+0443 "у" */
    0x8c, 0x62, 0xa5, 0x10, 0x98,

    /* U+0444 "ф" */
    0x21, 0x1d, 0x5a, 0xd6, 0xae, 0x21, 0x0,

    /* U+0445 "х" */
    0x8a, 0x88, 0xa8, 0xc4,

    /* U+0446 "ц" */
    0x8a, 0x28, 0xa2, 0x8b, 0xf0, 0x41,

    /* U+0447 "ч" */
    0x8c, 0x66, 0xd0, 0x84,

    /* U+0448 "ш" */
    0xad, 0x6b, 0x5a, 0xfc,

    /* U+0449 "щ" */
    0xaa, 0xaa, 0xaa, 0xab, 0xf0, 0x41,

    /* U+044A "ъ" */
    0xc1, 0x7, 0x91, 0x45, 0xe0,

    /* U+044B "ы" */
    0x8c, 0x73, 0x5a, 0xe4,

    /* U+044C "ь" */
    0x84, 0x3d, 0x18, 0xf8,

    /* U+044D "э" */
    0x74, 0x4e, 0x18, 0xb8,

    /* U+044E "ю" */
    0x9a, 0x9e, 0x69, 0xa6, 0x60,

    /* U+044F "я" */
    0x7c, 0x62, 0xf4, 0xc4,

    /* U+0450 "ѐ" */
    0x41, 0x0, 0xe8, 0xfe, 0x11, 0x70,

    /* U+0451 "ё" */
    0x50, 0x1d, 0x1f, 0xc2, 0x2e,

    /* U+0452 "ђ" */
    0x43, 0xc4, 0x16, 0x65, 0x14, 0x51, 0x4, 0x20,

    /* U+0453 "ѓ" */
    0x11, 0x1, 0xf4, 0xa1, 0x8, 0xe0,

    /* U+0454 "є" */
    0x74, 0x79, 0x8, 0xb8,

    /* U+0455 "ѕ" */
    0x74, 0x5c, 0x18, 0xb8,

    /* U+0456 "і" */
    0x20, 0x38, 0x42, 0x10, 0x9f,

    /* U+0457 "ї" */
    0x50, 0x38, 0x42, 0x10, 0x9f,

    /* U+0458 "ј" */
    0x10, 0x71, 0x11, 0x11, 0x96,

    /* U+0459 "љ" */
    0x70, 0xa1, 0x72, 0x99, 0x33, 0x80,

    /* U+045A "њ" */
    0x91, 0x23, 0xf4, 0x99, 0x33, 0x80,

    /* U+045B "ћ" */
    0x43, 0xc4, 0x16, 0x65, 0x14, 0x51,

    /* U+045C "ќ" */
    0x11, 0x1, 0x19, 0x53, 0x92, 0x88,

    /* U+045D "ѝ" */
    0x41, 0x1, 0x19, 0xd7, 0x31, 0x88,

    /* U+045E "ў" */
    0x8b, 0x81, 0x18, 0xc5, 0x4a, 0x21, 0x30,

    /* U+045F "џ" */
    0x8c, 0x63, 0x18, 0xfc, 0x84,

    /* U+0462 "Ѣ" */
    0x43, 0xc4, 0x1e, 0x45, 0x14, 0x5e,

    /* U+0463 "ѣ" */
    0x41, 0xf, 0x10, 0x79, 0x14, 0x5e,

    /* U+0474 "Ѵ" */
    0x86, 0x28, 0xa2, 0x89, 0x45, 0x8,

    /* U+0475 "ѵ" */
    0x86, 0x28, 0x94, 0x50, 0x80,

    /* U+048A "Ҋ" */
    0x89, 0xc0, 0x22, 0x8a, 0x6a, 0xb2, 0x8a, 0x28,
    0xc1, 0x8,

    /* U+048B "ҋ" */
    0x89, 0xc0, 0x22, 0x9a, 0xac, 0xa2, 0x8c, 0x10,
    0x80,

    /* U+048C "Ҍ" */
    0x43, 0xc4, 0x1e, 0x45, 0x14, 0x5e,

    /* U+048D "ҍ" */
    0x41, 0xf, 0x10, 0x79, 0x14, 0x5e,

    /* U+048E "Ҏ" */
    0xf4, 0x63, 0x5f, 0x46, 0x10,

    /* U+048F "ҏ" */
    0xb3, 0x28, 0xaa, 0xda, 0xe8, 0x60,

    /* U+0490 "Ґ" */
    0xf, 0xd2, 0x84, 0x21, 0x8, 0xe0,

    /* U+0491 "ґ" */
    0xf, 0xd2, 0x84, 0x23, 0x80,

    /* U+0492 "Ғ" */
    0x7d, 0x4, 0x3e, 0x41, 0x4, 0x10,

    /* U+0493 "ғ" */
    0x7d, 0xf, 0x90, 0x41, 0x0,

    /* U+0494 "Ҕ" */
    0xf4, 0x21, 0x6c, 0xc6, 0x31, 0x8, 0x80,

    /* U+0495 "ҕ" */
    0xf4, 0x2d, 0x98, 0xc4, 0x22,

    /* U+0496 "Җ" */
    0x92, 0xa9, 0x51, 0xc5, 0x4a, 0x95, 0x49, 0x2,
    0x4,

    /* U+0497 "җ" */
    0x92, 0xa8, 0xe2, 0xa5, 0x52, 0x40, 0x81,

    /* U+0498 "Ҙ" */
    0x74, 0x42, 0x60, 0x86, 0x2e, 0x20, 0x88,

    /* U+0499 "ҙ" */
    0x74, 0x4c, 0x18, 0xb8, 0x82, 0x20,

    /* U+049A "Қ" */
    0x8a, 0x49, 0x38, 0x92, 0x48, 0xa3, 0x4, 0x10,

    /* U+049B "қ" */
    0x8a, 0x4a, 0x38, 0x92, 0x30, 0x41,

    /* U+049E "Ҟ" */
    0x47, 0xa4, 0x9c, 0x49, 0x24, 0x51,

    /* U+049F "ҟ" */
    0x43, 0x84, 0x52, 0x51, 0xc4, 0x91,

    /* U+04A0 "Ҡ" */
    0xc5, 0x24, 0x9c, 0x49, 0x24, 0x51,

    /* U+04A1 "ҡ" */
    0xc5, 0x25, 0x1c, 0x49, 0x10,

    /* U+04A2 "Ң" */
    0x8a, 0x28, 0xbe, 0x8a, 0x28, 0xa3, 0x4, 0x10,

    /* U+04A3 "ң" */
    0x8a, 0x2f, 0xa2, 0x8a, 0x30, 0x41,

    /* U+04A4 "Ҥ" */
    0x8e, 0x28, 0xbe, 0x8a, 0x28, 0xa2,

    /* U+04A5 "ҥ" */
    0x8e, 0x2f, 0xa2, 0x8a, 0x20,

    /* U+04A6 "Ҧ" */
    0xe5, 0x29, 0x6a, 0xd6, 0xb5, 0x8, 0x80,

    /* U+04A7 "ҧ" */
    0xe5, 0x2d, 0x5a, 0xd4, 0x22,

    /* U+04A8 "Ҩ" */
    0x22, 0x25, 0x5a, 0xd6, 0xb2, 0x68,

    /* U+04A9 "ҩ" */
    0x44, 0xab, 0x5a, 0xc9, 0xa0,

    /* U+04AA "Ҫ" */
    0x74, 0x61, 0x8, 0x42, 0x2e, 0x20, 0x88,

    /* U+04AB "ҫ" */
    0x74, 0x61, 0x8, 0xb8, 0x82, 0x20,

    /* U+04AC "Ҭ" */
    0xf9, 0x8, 0x42, 0x10, 0x86, 0x10, 0x80,

    /* U+04AD "ҭ" */
    0xf9, 0x8, 0x42, 0x18, 0x42,

    /* U+04AE "Ү" */
    0x8c, 0x62, 0xa2, 0x10, 0x84,

    /* U+04AF "ү" */
    0x8c, 0x62, 0xa5, 0x10, 0x84,

    /* U+04B0 "Ұ" */
    0x8c, 0x62, 0xa2, 0x38, 0x84,

    /* U+04B1 "ұ" */
    0x8c, 0x62, 0xa5, 0x11, 0xc4,

    /* U+04B2 "Ҳ" */
    0x8a, 0x25, 0x8, 0x52, 0x28, 0xa3, 0x4, 0x10,

    /* U+04B3 "ҳ" */
    0x89, 0x42, 0x14, 0x8a, 0x30, 0x41,

    /* U+04B4 "Ҵ" */
    0xe4, 0x89, 0x12, 0x24, 0x48, 0x91, 0x3f, 0x2,
    0x4,

    /* U+04B5 "ҵ" */
    0xe4, 0x89, 0x12, 0x24, 0x4f, 0xc0, 0x81,

    /* U+04B6 "Ҷ" */
    0x8a, 0x28, 0xa6, 0x68, 0x20, 0x83, 0x4, 0x10,

    /* U+04B7 "ҷ" */
    0x8a, 0x29, 0x9a, 0x8, 0x30, 0x41,

    /* U+04BA "Һ" */
    0x84, 0x21, 0x6c, 0xc6, 0x31,

    /* U+04BB "һ" */
    0x84, 0x2d, 0x98, 0xc6, 0x31,

    /* U+04BC "Ҽ" */
    0x39, 0x14, 0x7f, 0x41, 0x4, 0x4e,

    /* U+04BD "ҽ" */
    0x39, 0x1f, 0xd0, 0x44, 0xe0,

    /* U+04BE "Ҿ" */
    0x39, 0x14, 0x7f, 0x41, 0x4, 0x4e, 0x10, 0x40,

    /* U+04BF "ҿ" */
    0x39, 0x1f, 0xd0, 0x44, 0xe1, 0x4,

    /* U+04C0 "Ӏ" */
    0xf9, 0x8, 0x42, 0x10, 0x9f,

    /* U+04C1 "Ӂ" */
    0x44, 0x70, 0x4, 0x95, 0x4a, 0x8e, 0x2a, 0x54,
    0xaa, 0x48,

    /* U+04C2 "ӂ" */
    0x44, 0x70, 0x4, 0x95, 0x47, 0x15, 0x2a, 0x92,

    /* U+04C3 "Ӄ" */
    0x8c, 0xa5, 0xc9, 0x46, 0x31, 0x8, 0x80,

    /* U+04C4 "ӄ" */
    0x8c, 0xa9, 0xc9, 0x44, 0x22,

    /* U+04C5 "Ӆ" */
    0x79, 0x24, 0x92, 0x4a, 0x28, 0xa3, 0x4, 0x20,

    /* U+04C6 "ӆ" */
    0x79, 0x24, 0x92, 0x8a, 0x30, 0x42,

    /* U+04C7 "Ӈ" */
    0x8c, 0x63, 0xf8, 0xc6, 0x31, 0x8, 0x80,

    /* U+04C8 "ӈ" */
    0x8c, 0x7f, 0x18, 0xc4, 0x22,

    /* U+04C9 "Ӊ" */
    0x8a, 0x28, 0xbe, 0x8a, 0x28, 0xa3, 0x4, 0x20,

    /* U+04CA "ӊ" */
    0x8a, 0x2f, 0xa2, 0x8a, 0x30, 0x42,

    /* U+04CB "Ӌ" */
    0x8c, 0x63, 0x36, 0x84, 0x23, 0x10, 0x80,

    /* U+04CC "ӌ" */
    0x8c, 0x66, 0xd0, 0x8c, 0x42,

    /* U+04CD "Ӎ" */
    0x8a, 0x2d, 0xaa, 0xaa, 0x28, 0xa3, 0x4, 0x20,

    /* U+04CE "ӎ" */
    0x8b, 0x6a, 0xaa, 0x8a, 0x30, 0x42,

    /* U+04CF "ӏ" */
    0xf9, 0x8, 0x42, 0x10, 0x9f,

    /* U+04D0 "Ӑ" */
    0x8b, 0x80, 0x45, 0x46, 0x3f, 0x8c, 0x62,

    /* U+04D1 "ӑ" */
    0x8b, 0x80, 0xe0, 0xbe, 0x33, 0x68,

    /* U+04D2 "Ӓ" */
    0x50, 0x8, 0xa8, 0xc7, 0xf1, 0x8c, 0x40,

    /* U+04D3 "ӓ" */
    0x50, 0x1c, 0x17, 0xc6, 0x6d,

    /* U+04D4 "Ӕ" */
    0x3e, 0xa2, 0x44, 0xef, 0x12, 0x24, 0x4f,

    /* U+04D5 "ӕ" */
    0x6c, 0x25, 0xfc, 0x89, 0x2d, 0x80,

    /* U+04D6 "Ӗ" */
    0x8b, 0x81, 0xf8, 0x43, 0xd0, 0x84, 0x3e,

    /* U+04D7 "ӗ" */
    0x8b, 0x80, 0xe8, 0xfe, 0x11, 0x70,

    /* U+04DC "Ӝ" */
    0x28, 0x2, 0x4a, 0xa5, 0x47, 0x15, 0x2a, 0x55,
    0x24,

    /* U+04DD "ӝ" */
    0x28, 0x2, 0x4a, 0xa3, 0x8a, 0x95, 0x49,

    /* U+04DE "Ӟ" */
    0x50, 0x1d, 0x10, 0x98, 0x21, 0x8b, 0x80,

    /* U+04DF "ӟ" */
    0x50, 0x1d, 0x13, 0x6, 0x2e,

    /* U+04E0 "Ӡ" */
    0xf8, 0x88, 0x20, 0x86, 0x2e,

    /* U+04E1 "ӡ" */
    0xf8, 0x88, 0x20, 0x86, 0x2e,

    /* U+04E2 "Ӣ" */
    0x70, 0x23, 0x19, 0xd7, 0x31, 0x8c, 0x40,

    /* U+04E3 "ӣ" */
    0x70, 0x23, 0x3a, 0xe6, 0x31,

    /* U+04E4 "Ӥ" */
    0x50, 0x23, 0x19, 0xd7, 0x31, 0x8c, 0x40,

    /* U+04E5 "ӥ" */
    0x50, 0x23, 0x3a, 0xe6, 0x31,

    /* U+04E6 "Ӧ" */
    0x50, 0x1d, 0x18, 0xc6, 0x31, 0x8b, 0x80,

    /* U+04E7 "ӧ" */
    0x50, 0x1d, 0x18, 0xc6, 0x2e,

    /* U+04E8 "Ө" */
    0x74, 0x63, 0xf8, 0xc6, 0x2e,

    /* U+04E9 "ө" */
    0x74, 0x7f, 0x18, 0xb8,

    /* U+04EA "Ӫ" */
    0x50, 0x1d, 0x18, 0xfe, 0x31, 0x8b, 0x80,

    /* U+04EB "ӫ" */
    0x50, 0x1d, 0x1f, 0xc6, 0x2e,

    /* U+04EC "Ӭ" */
    0x50, 0x1d, 0x10, 0x9c, 0x21, 0x8b, 0x80,

    /* U+04ED "ӭ" */
    0x50, 0x1d, 0x13, 0x86, 0x2e,

    /* U+04EE "Ӯ" */
    0x70, 0x23, 0x18, 0xa9, 0x44, 0x26, 0x0,

    /* U+04EF "ӯ" */
    0x70, 0x23, 0x18, 0xa9, 0x44, 0x26, 0x0,

    /* U+04F0 "Ӱ" */
    0x50, 0x23, 0x18, 0xa9, 0x44, 0x26, 0x0,

    /* U+04F1 "ӱ" */
    0x50, 0x23, 0x18, 0xa9, 0x44, 0x26, 0x0,

    /* U+04F2 "Ӳ" */
    0x52, 0x81, 0x18, 0xc5, 0x4a, 0x21, 0x30,

    /* U+04F3 "ӳ" */
    0x52, 0x81, 0x18, 0xc5, 0x4a, 0x21, 0x30,

    /* U+04F4 "Ӵ" */
    0x50, 0x23, 0x18, 0xcd, 0xa1, 0x8, 0x40,

    /* U+04F5 "ӵ" */
    0x50, 0x23, 0x19, 0xb4, 0x21,

    /* U+04F6 "Ӷ" */
    0xfa, 0x50, 0x84, 0x21, 0x1c, 0x21, 0x0,

    /* U+04F7 "ӷ" */
    0xfa, 0x50, 0x84, 0x70, 0x84,

    /* U+04F8 "Ӹ" */
    0x50, 0x23, 0x18, 0xe6, 0xb5, 0xae, 0x40,

    /* U+04F9 "ӹ" */
    0x50, 0x23, 0x1c, 0xd6, 0xb9,

    /* U+04FC "Ӽ" */
    0x8c, 0x54, 0x45, 0x46, 0x31, 0x8, 0x80,

    /* U+04FD "ӽ" */
    0x8a, 0x88, 0xa8, 0xc4, 0x22
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 112, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 112, .box_w = 1, .box_h = 8, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 2, .adv_w = 112, .box_w = 3, .box_h = 3, .ofs_x = 2, .ofs_y = 5},
    {.bitmap_index = 4, .adv_w = 112, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 11, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 18, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 24, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 30, .adv_w = 112, .box_w = 1, .box_h = 3, .ofs_x = 3, .ofs_y = 5},
    {.bitmap_index = 31, .adv_w = 112, .box_w = 3, .box_h = 10, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 35, .adv_w = 112, .box_w = 3, .box_h = 10, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 39, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 43, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 47, .adv_w = 112, .box_w = 2, .box_h = 3, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 48, .adv_w = 112, .box_w = 3, .box_h = 1, .ofs_x = 2, .ofs_y = 3},
    {.bitmap_index = 49, .adv_w = 112, .box_w = 1, .box_h = 2, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 50, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 57, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 62, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 67, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 72, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 77, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 82, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 87, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 92, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 97, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 102, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 107, .adv_w = 112, .box_w = 1, .box_h = 6, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 108, .adv_w = 112, .box_w = 2, .box_h = 7, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 110, .adv_w = 112, .box_w = 4, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 114, .adv_w = 112, .box_w = 5, .box_h = 3, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 116, .adv_w = 112, .box_w = 4, .box_h = 7, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 120, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 125, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 130, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 135, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 140, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 145, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 150, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 155, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 160, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 165, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 170, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 175, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 180, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 185, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 190, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 195, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 200, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 205, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 210, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 216, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 221, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 226, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 231, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 236, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 241, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 246, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 251, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 256, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 261, .adv_w = 112, .box_w = 3, .box_h = 10, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 265, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 272, .adv_w = 112, .box_w = 3, .box_h = 10, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 276, .adv_w = 112, .box_w = 3, .box_h = 2, .ofs_x = 2, .ofs_y = 6},
    {.bitmap_index = 277, .adv_w = 112, .box_w = 5, .box_h = 1, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 278, .adv_w = 112, .box_w = 2, .box_h = 2, .ofs_x = 2, .ofs_y = 7},
    {.bitmap_index = 279, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 283, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 288, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 292, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 297, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 301, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 306, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 311, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 316, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 321, .adv_w = 112, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 326, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 331, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 336, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 340, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 344, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 348, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 353, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 358, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 362, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 366, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 371, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 375, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 379, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 383, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 387, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 392, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 396, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 403, .adv_w = 112, .box_w = 1, .box_h = 10, .ofs_x = 3, .ofs_y = -1},
    {.bitmap_index = 405, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 412, .adv_w = 112, .box_w = 5, .box_h = 3, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 414, .adv_w = 112, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 415, .adv_w = 112, .box_w = 1, .box_h = 8, .ofs_x = 3, .ofs_y = -2},
    {.bitmap_index = 416, .adv_w = 112, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 421, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 426, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 430, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 435, .adv_w = 112, .box_w = 1, .box_h = 10, .ofs_x = 3, .ofs_y = -1},
    {.bitmap_index = 437, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 443, .adv_w = 112, .box_w = 3, .box_h = 1, .ofs_x = 2, .ofs_y = 7},
    {.bitmap_index = 444, .adv_w = 112, .box_w = 5, .box_h = 12, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 452, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 456, .adv_w = 112, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 460, .adv_w = 112, .box_w = 5, .box_h = 3, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 462, .adv_w = 112, .box_w = 3, .box_h = 1, .ofs_x = 2, .ofs_y = 3},
    {.bitmap_index = 463, .adv_w = 112, .box_w = 5, .box_h = 12, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 471, .adv_w = 112, .box_w = 3, .box_h = 1, .ofs_x = 2, .ofs_y = 7},
    {.bitmap_index = 472, .adv_w = 112, .box_w = 3, .box_h = 3, .ofs_x = 2, .ofs_y = 5},
    {.bitmap_index = 474, .adv_w = 112, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 479, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 483, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 487, .adv_w = 112, .box_w = 2, .box_h = 2, .ofs_x = 3, .ofs_y = 7},
    {.bitmap_index = 488, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 493, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 498, .adv_w = 112, .box_w = 1, .box_h = 2, .ofs_x = 3, .ofs_y = 3},
    {.bitmap_index = 499, .adv_w = 112, .box_w = 2, .box_h = 3, .ofs_x = 3, .ofs_y = -3},
    {.bitmap_index = 500, .adv_w = 112, .box_w = 3, .box_h = 6, .ofs_x = 2, .ofs_y = 3},
    {.bitmap_index = 503, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 507, .adv_w = 112, .box_w = 6, .box_h = 5, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 511, .adv_w = 112, .box_w = 7, .box_h = 13, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 523, .adv_w = 112, .box_w = 7, .box_h = 13, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 535, .adv_w = 112, .box_w = 7, .box_h = 13, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 547, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 552, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 559, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 566, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 573, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 580, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 587, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 594, .adv_w = 112, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 601, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 608, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 615, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 622, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 629, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 636, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 643, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 650, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 657, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 664, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 670, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 677, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 684, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 691, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 698, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 705, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 712, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 716, .adv_w = 112, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 723, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 730, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 737, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 744, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 751, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 758, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 763, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 770, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 776, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 782, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 788, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 794, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 799, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 806, .adv_w = 112, .box_w = 7, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 812, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 818, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 824, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 830, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 836, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 841, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 847, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 853, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 859, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 864, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 869, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 875, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 881, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 887, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 893, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 899, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 904, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 908, .adv_w = 112, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 915, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 921, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 927, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 933, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 938, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 945, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 952, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 959, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 966, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 973, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 979, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 986, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 991, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 996, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1001, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1008, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1013, .adv_w = 112, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1020, .adv_w = 112, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1027, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1033, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1040, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1047, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1054, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1061, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1066, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1071, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1076, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1081, .adv_w = 112, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1090, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1095, .adv_w = 112, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1102, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1107, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1112, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1119, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1124, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1129, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1134, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1139, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1144, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1149, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1154, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1159, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1164, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1169, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1174, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1179, .adv_w = 112, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1187, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1192, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1197, .adv_w = 112, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1205, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1211, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1216, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1221, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1226, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1232, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1237, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1241, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1246, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1250, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1254, .adv_w = 112, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1261, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1265, .adv_w = 112, .box_w = 7, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1271, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1275, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1279, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1285, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1289, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1293, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1297, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1301, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1305, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1309, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1314, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1318, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1322, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1327, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1334, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1338, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1344, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1348, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1352, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1358, .adv_w = 112, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1363, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1367, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1371, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1375, .adv_w = 112, .box_w = 6, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1380, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1384, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1390, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1395, .adv_w = 112, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1403, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1409, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1413, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1417, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1422, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1427, .adv_w = 112, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1432, .adv_w = 112, .box_w = 7, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1438, .adv_w = 112, .box_w = 7, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1444, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1450, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1456, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1462, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1469, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1474, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1480, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1486, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1492, .adv_w = 112, .box_w = 6, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1497, .adv_w = 112, .box_w = 6, .box_h = 13, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1507, .adv_w = 112, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1516, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1522, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1528, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1533, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1539, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1545, .adv_w = 112, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1550, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1556, .adv_w = 112, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1561, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1568, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1573, .adv_w = 112, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1582, .adv_w = 112, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1589, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 1596, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 1602, .adv_w = 112, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1610, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1616, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1622, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1628, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1634, .adv_w = 112, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1639, .adv_w = 112, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1647, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1653, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1659, .adv_w = 112, .box_w = 6, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1664, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1671, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1676, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 1682, .adv_w = 112, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 1687, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 1694, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 1700, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1707, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1712, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1717, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1722, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1727, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1732, .adv_w = 112, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1740, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1746, .adv_w = 112, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1755, .adv_w = 112, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1762, .adv_w = 112, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1770, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1776, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1781, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1786, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1792, .adv_w = 112, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1797, .adv_w = 112, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1805, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1811, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1816, .adv_w = 112, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1826, .adv_w = 112, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1834, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1841, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1846, .adv_w = 112, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1854, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1860, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1867, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1872, .adv_w = 112, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1880, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1886, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1893, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1898, .adv_w = 112, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1906, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1912, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1917, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1924, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1930, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1937, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1942, .adv_w = 112, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1949, .adv_w = 112, .box_w = 7, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1955, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1962, .adv_w = 112, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1968, .adv_w = 112, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1977, .adv_w = 112, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1984, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1991, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1996, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2001, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 2006, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2013, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2018, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2025, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2030, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2037, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2042, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2047, .adv_w = 112, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2051, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2058, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2063, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2070, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2075, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2082, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 2089, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2096, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 2103, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2110, .adv_w = 112, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 2117, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2124, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2129, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 2136, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 2141, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2148, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2153, .adv_w = 112, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 2160, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_3[] = {
    0x0, 0x1, 0x12, 0x13
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 160, .range_length = 96, .glyph_id_start = 96,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 1024, .range_length = 96, .glyph_id_start = 192,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 1122, .range_length = 20, .glyph_id_start = 288,
        .unicode_list = unicode_list_3, .glyph_id_ofs_list = NULL, .list_length = 4, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    },
    {
        .range_start = 1162, .range_length = 18, .glyph_id_start = 292,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 1182, .range_length = 26, .glyph_id_start = 310,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 1210, .range_length = 30, .glyph_id_start = 336,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 1244, .range_length = 30, .glyph_id_start = 366,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 1276, .range_length = 2, .glyph_id_start = 396,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 9,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t departure_mono_11 = {
#else
lv_font_t departure_mono_11 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 14,          /*The maximum line height required by the font*/
    .base_line = 3,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if DEPARTURE_MONO_11*/

