/*******************************************************************************
 * Size: 11 px
 * Bpp: 1
 * Opts: --font greybeard/Greybeard-11px.ttf -r 0x20-0x7F,0xA0-0xFF,0x400-0x4FF --size 11 --bpp 1 --format lvgl --no-compress --lv-font-name greybeard_11 -o greybeard_11.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef GREYBEARD_11
#define GREYBEARD_11 1
#endif

#if GREYBEARD_11

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0,

    /* U+0021 "!" */
    0xfb,

    /* U+0022 "\"" */
    0xb6, 0x80,

    /* U+0023 "#" */
    0x52, 0xbe, 0xaf, 0xa9, 0x40,

    /* U+0024 "$" */
    0x23, 0xab, 0x47, 0x16, 0xae, 0x20,

    /* U+0025 "%" */
    0x4d, 0x54, 0x42, 0x2a, 0xb2,

    /* U+0026 "&" */
    0x45, 0x28, 0x8a, 0xca, 0x4d,

    /* U+0027 "'" */
    0xe0,

    /* U+0028 "(" */
    0x29, 0x49, 0x22, 0x44,

    /* U+0029 ")" */
    0x89, 0x12, 0x4a, 0x50,

    /* U+002A "*" */
    0x51, 0x3e, 0x45, 0x0,

    /* U+002B "+" */
    0x21, 0x3e, 0x42, 0x0,

    /* U+002C "," */
    0x6d, 0x40,

    /* U+002D "-" */
    0xf0,

    /* U+002E "." */
    0xf0,

    /* U+002F "/" */
    0x8, 0x44, 0x22, 0x11, 0x8, 0x84, 0x0,

    /* U+0030 "0" */
    0x69, 0x9b, 0xd9, 0x96,

    /* U+0031 "1" */
    0x2e, 0x92, 0x49,

    /* U+0032 "2" */
    0xe9, 0x11, 0x24, 0x8f,

    /* U+0033 "3" */
    0xe1, 0x16, 0x11, 0x1e,

    /* U+0034 "4" */
    0x26, 0x6a, 0xaf, 0x22,

    /* U+0035 "5" */
    0xf8, 0x8e, 0x11, 0x1e,

    /* U+0036 "6" */
    0x68, 0x8e, 0x99, 0x96,

    /* U+0037 "7" */
    0xf1, 0x12, 0x24, 0x44,

    /* U+0038 "8" */
    0x69, 0x96, 0x99, 0x96,

    /* U+0039 "9" */
    0x69, 0x99, 0x71, 0x16,

    /* U+003A ":" */
    0xf3, 0xc0,

    /* U+003B ";" */
    0x6c, 0x36, 0xa0,

    /* U+003C "<" */
    0x12, 0x48, 0x42, 0x10,

    /* U+003D "=" */
    0xf8, 0x3e,

    /* U+003E ">" */
    0x84, 0x21, 0x24, 0x80,

    /* U+003F "?" */
    0x74, 0x42, 0x22, 0x0, 0x84,

    /* U+0040 "@" */
    0x32, 0x67, 0x5a, 0xcd, 0x7,

    /* U+0041 "A" */
    0x66, 0x99, 0xf9, 0x99,

    /* U+0042 "B" */
    0xe9, 0x9e, 0x99, 0x9e,

    /* U+0043 "C" */
    0x69, 0x88, 0x88, 0x96,

    /* U+0044 "D" */
    0xca, 0x99, 0x99, 0xac,

    /* U+0045 "E" */
    0xf8, 0x8e, 0x88, 0x8f,

    /* U+0046 "F" */
    0xf8, 0x8e, 0x88, 0x88,

    /* U+0047 "G" */
    0x69, 0x88, 0xb9, 0x97,

    /* U+0048 "H" */
    0x99, 0x9f, 0x99, 0x99,

    /* U+0049 "I" */
    0xe9, 0x24, 0x97,

    /* U+004A "J" */
    0x31, 0x11, 0x19, 0x96,

    /* U+004B "K" */
    0x99, 0xac, 0xaa, 0x99,

    /* U+004C "L" */
    0x88, 0x88, 0x88, 0x8f,

    /* U+004D "M" */
    0x8e, 0xf7, 0x5a, 0xc6, 0x31,

    /* U+004E "N" */
    0x99, 0xdd, 0xbb, 0x99,

    /* U+004F "O" */
    0x69, 0x99, 0x99, 0x96,

    /* U+0050 "P" */
    0xe9, 0x99, 0xe8, 0x88,

    /* U+0051 "Q" */
    0x69, 0x99, 0x9d, 0xb6, 0x10,

    /* U+0052 "R" */
    0xe9, 0x9e, 0xaa, 0x99,

    /* U+0053 "S" */
    0x69, 0x84, 0x21, 0x96,

    /* U+0054 "T" */
    0xf9, 0x8, 0x42, 0x10, 0x84,

    /* U+0055 "U" */
    0x99, 0x99, 0x99, 0x96,

    /* U+0056 "V" */
    0x8c, 0x63, 0x15, 0x28, 0x84,

    /* U+0057 "W" */
    0x8c, 0x63, 0x5a, 0xfd, 0x4a,

    /* U+0058 "X" */
    0x99, 0x96, 0x69, 0x99,

    /* U+0059 "Y" */
    0x8c, 0x54, 0xa2, 0x10, 0x84,

    /* U+005A "Z" */
    0xf1, 0x22, 0x44, 0x8f,

    /* U+005B "[" */
    0xf2, 0x49, 0x24, 0x9c,

    /* U+005C "\\" */
    0x84, 0x10, 0x82, 0x10, 0x42, 0x8, 0x40,

    /* U+005D "]" */
    0xe4, 0x92, 0x49, 0x3c,

    /* U+005E "^" */
    0x22, 0xa2,

    /* U+005F "_" */
    0xf0,

    /* U+0060 "`" */
    0xa4,

    /* U+0061 "a" */
    0x61, 0x79, 0x97,

    /* U+0062 "b" */
    0x88, 0xe9, 0x99, 0x9e,

    /* U+0063 "c" */
    0x69, 0x88, 0x87,

    /* U+0064 "d" */
    0x11, 0x79, 0x99, 0x97,

    /* U+0065 "e" */
    0x69, 0xf8, 0x87,

    /* U+0066 "f" */
    0x34, 0x4f, 0x44, 0x44,

    /* U+0067 "g" */
    0x79, 0x99, 0xf1, 0x9e,

    /* U+0068 "h" */
    0x88, 0xbd, 0x99, 0x99,

    /* U+0069 "i" */
    0x48, 0x64, 0x92, 0xe0,

    /* U+006A "j" */
    0x11, 0x3, 0x11, 0x11, 0x19, 0x60,

    /* U+006B "k" */
    0x88, 0x9a, 0xca, 0x99,

    /* U+006C "l" */
    0xc9, 0x24, 0x97,

    /* U+006D "m" */
    0xfd, 0x6b, 0x5a, 0xd4,

    /* U+006E "n" */
    0xbd, 0x99, 0x99,

    /* U+006F "o" */
    0x69, 0x99, 0x96,

    /* U+0070 "p" */
    0xe9, 0x99, 0x9e, 0x88,

    /* U+0071 "q" */
    0x79, 0x99, 0x97, 0x11,

    /* U+0072 "r" */
    0xf5, 0x44, 0x44,

    /* U+0073 "s" */
    0x78, 0xc3, 0x1e,

    /* U+0074 "t" */
    0x44, 0xf4, 0x44, 0x43,

    /* U+0075 "u" */
    0x99, 0x99, 0x97,

    /* U+0076 "v" */
    0x8c, 0x54, 0xa2, 0x10,

    /* U+0077 "w" */
    0x8c, 0x6b, 0x57, 0x28,

    /* U+0078 "x" */
    0x99, 0x66, 0x99,

    /* U+0079 "y" */
    0x99, 0x95, 0x62, 0x4c,

    /* U+007A "z" */
    0xf1, 0x24, 0x8f,

    /* U+007B "{" */
    0x19, 0x8, 0x4c, 0x60, 0x84, 0x20, 0xc0,

    /* U+007C "|" */
    0xff, 0xc0,

    /* U+007D "}" */
    0xc1, 0x8, 0x41, 0x8c, 0x84, 0x26, 0x0,

    /* U+007E "~" */
    0x4d, 0x64,

    /* U+00A0 " " */
    0x0,

    /* U+00A1 "¡" */
    0xdf,

    /* U+00A2 "¢" */
    0x27, 0xaa, 0xaa, 0x72,

    /* U+00A3 "£" */
    0x32, 0x51, 0xc4, 0x22, 0x3f,

    /* U+00A4 "¤" */
    0x8b, 0x94, 0xe8, 0x80,

    /* U+00A5 "¥" */
    0x8c, 0x55, 0xf2, 0x7c, 0x84,

    /* U+00A6 "¦" */
    0xf3, 0xc0,

    /* U+00A7 "§" */
    0x69, 0x46, 0x99, 0x62, 0x96,

    /* U+00A8 "¨" */
    0xb4,

    /* U+00A9 "©" */
    0x31, 0x2b, 0x71, 0xc7, 0x1b, 0x52, 0x30,

    /* U+00AA "ª" */
    0x61, 0x7f, 0xf,

    /* U+00AB "«" */
    0x4c, 0xa4, 0x90,

    /* U+00AC "¬" */
    0xf1, 0x10,

    /* U+00AD "­" */
    0xe0,

    /* U+00AE "®" */
    0x31, 0x2b, 0x6b, 0xae, 0xda, 0xd2, 0x30,

    /* U+00AF "¯" */
    0xf0,

    /* U+00B0 "°" */
    0x69, 0x96,

    /* U+00B1 "±" */
    0x21, 0x3e, 0x42, 0x3, 0xe0,

    /* U+00B2 "²" */
    0x69, 0x24, 0xf0,

    /* U+00B3 "³" */
    0xe1, 0x61, 0xe0,

    /* U+00B4 "´" */
    0x2a, 0x0,

    /* U+00B5 "µ" */
    0x99, 0x99, 0xbd, 0x88,

    /* U+00B6 "¶" */
    0x7d, 0x6a, 0xd2, 0x94, 0xa5,

    /* U+00B7 "·" */
    0xf0,

    /* U+00B8 "¸" */
    0x22, 0x1e,

    /* U+00B9 "¹" */
    0x75, 0x40,

    /* U+00BA "º" */
    0x69, 0x96, 0xf,

    /* U+00BB "»" */
    0x92, 0x53, 0x20,

    /* U+00BC "¼" */
    0x46, 0x12, 0xa6, 0x2a, 0xc6, 0x78, 0x80,

    /* U+00BD "½" */
    0x46, 0x12, 0xa6, 0x2a, 0xa1, 0x11, 0xc0,

    /* U+00BE "¾" */
    0xc1, 0x12, 0x6e, 0x2a, 0xc6, 0x78, 0x80,

    /* U+00BF "¿" */
    0x21, 0x0, 0x44, 0x42, 0x2e,

    /* U+00C0 "À" */
    0x42, 0x66, 0x9f, 0x99, 0x90,

    /* U+00C1 "Á" */
    0x24, 0x66, 0x9f, 0x99, 0x90,

    /* U+00C2 "Â" */
    0x25, 0x66, 0x9f, 0x99, 0x90,

    /* U+00C3 "Ã" */
    0x5a, 0x66, 0x9f, 0x99, 0x90,

    /* U+00C4 "Ä" */
    0xd8, 0x18, 0xc9, 0x7a, 0x52, 0x90,

    /* U+00C5 "Å" */
    0x69, 0x66, 0x9f, 0x99, 0x90,

    /* U+00C6 "Æ" */
    0x7b, 0x29, 0x7e, 0x52, 0x97,

    /* U+00C7 "Ç" */
    0x69, 0x88, 0x88, 0x96, 0x24,

    /* U+00C8 "È" */
    0x42, 0xf8, 0x8e, 0x88, 0xf0,

    /* U+00C9 "É" */
    0x24, 0xf8, 0x8e, 0x88, 0xf0,

    /* U+00CA "Ê" */
    0x25, 0xf8, 0x8e, 0x88, 0xf0,

    /* U+00CB "Ë" */
    0xd8, 0x3d, 0x8, 0x72, 0x10, 0xf0,

    /* U+00CC "Ì" */
    0x8b, 0xa4, 0x92, 0xe0,

    /* U+00CD "Í" */
    0x2b, 0xa4, 0x92, 0xe0,

    /* U+00CE "Î" */
    0x57, 0xa4, 0x92, 0xe0,

    /* U+00CF "Ï" */
    0xd8, 0x1c, 0x42, 0x10, 0x84, 0x70,

    /* U+00D0 "Ð" */
    0x62, 0x93, 0xd4, 0xa5, 0x4c,

    /* U+00D1 "Ñ" */
    0x5a, 0x9d, 0xdb, 0xb9, 0x90,

    /* U+00D2 "Ò" */
    0x42, 0x69, 0x99, 0x99, 0x60,

    /* U+00D3 "Ó" */
    0x24, 0x69, 0x99, 0x99, 0x60,

    /* U+00D4 "Ô" */
    0x25, 0x69, 0x99, 0x99, 0x60,

    /* U+00D5 "Õ" */
    0x5a, 0x69, 0x99, 0x99, 0x60,

    /* U+00D6 "Ö" */
    0xd8, 0x19, 0x29, 0x4a, 0x52, 0x60,

    /* U+00D7 "×" */
    0x8a, 0x88, 0xa8, 0x80,

    /* U+00D8 "Ø" */
    0x4, 0xe4, 0x96, 0x59, 0xa6, 0x92, 0x72, 0x0,

    /* U+00D9 "Ù" */
    0x42, 0x99, 0x99, 0x99, 0x60,

    /* U+00DA "Ú" */
    0x24, 0x99, 0x99, 0x99, 0x60,

    /* U+00DB "Û" */
    0x25, 0x9, 0x99, 0x99, 0x60,

    /* U+00DC "Ü" */
    0xd8, 0x25, 0x29, 0x4a, 0x52, 0x60,

    /* U+00DD "Ý" */
    0x11, 0x23, 0x15, 0x10, 0x84, 0x20,

    /* U+00DE "Þ" */
    0x88, 0xe9, 0x9e, 0x88,

    /* U+00DF "ß" */
    0x69, 0xaa, 0x99, 0x9b,

    /* U+00E0 "à" */
    0x42, 0x6, 0x17, 0x99, 0x70,

    /* U+00E1 "á" */
    0x24, 0x6, 0x17, 0x99, 0x70,

    /* U+00E2 "â" */
    0x25, 0x6, 0x17, 0x99, 0x70,

    /* U+00E3 "ã" */
    0x5a, 0x6, 0x17, 0x99, 0x70,

    /* U+00E4 "ä" */
    0x55, 0x6, 0x17, 0x99, 0x70,

    /* U+00E5 "å" */
    0x25, 0x26, 0x17, 0x99, 0x70,

    /* U+00E6 "æ" */
    0xd9, 0x5f, 0x4a, 0x6c,

    /* U+00E7 "ç" */
    0x69, 0x88, 0x87, 0x24,

    /* U+00E8 "è" */
    0x42, 0x6, 0x9f, 0x88, 0x70,

    /* U+00E9 "é" */
    0x24, 0x6, 0x9f, 0x88, 0x70,

    /* U+00EA "ê" */
    0x25, 0x6, 0x9f, 0x88, 0x70,

    /* U+00EB "ë" */
    0x55, 0x6, 0x9f, 0x88, 0x70,

    /* U+00EC "ì" */
    0x88, 0x64, 0x92, 0xe0,

    /* U+00ED "í" */
    0x50, 0x64, 0x92, 0xe0,

    /* U+00EE "î" */
    0x54, 0x64, 0x92, 0xe0,

    /* U+00EF "ï" */
    0xb4, 0x64, 0x92, 0xe0,

    /* U+00F0 "ð" */
    0xac, 0x25, 0x99, 0x96,

    /* U+00F1 "ñ" */
    0x5a, 0xb, 0xd9, 0x99, 0x90,

    /* U+00F2 "ò" */
    0x42, 0x6, 0x99, 0x99, 0x60,

    /* U+00F3 "ó" */
    0x24, 0x6, 0x99, 0x99, 0x60,

    /* U+00F4 "ô" */
    0x25, 0x6, 0x99, 0x99, 0x60,

    /* U+00F5 "õ" */
    0x5a, 0x6, 0x99, 0x99, 0x60,

    /* U+00F6 "ö" */
    0x55, 0x6, 0x99, 0x99, 0x60,

    /* U+00F7 "÷" */
    0x21, 0x1, 0xf0, 0x10, 0x80,

    /* U+00F8 "ø" */
    0x4, 0xe4, 0x96, 0x69, 0x27, 0x20,

    /* U+00F9 "ù" */
    0x42, 0x9, 0x99, 0x99, 0x70,

    /* U+00FA "ú" */
    0x24, 0x9, 0x99, 0x99, 0x70,

    /* U+00FB "û" */
    0x25, 0x9, 0x99, 0x99, 0x70,

    /* U+00FC "ü" */
    0x55, 0x9, 0x99, 0x99, 0x70,

    /* U+00FD "ý" */
    0x24, 0x9, 0x99, 0x56, 0x24, 0xc0,

    /* U+00FE "þ" */
    0x88, 0xad, 0x99, 0xda, 0x88,

    /* U+00FF "ÿ" */
    0x55, 0x9, 0x99, 0x56, 0x24, 0xc0,

    /* U+0400 "Ѐ" */
    0x42, 0xf8, 0x8e, 0x88, 0xf0,

    /* U+0401 "Ё" */
    0xd8, 0x3d, 0x8, 0x72, 0x10, 0xf0,

    /* U+0402 "Ђ" */
    0xf8, 0x82, 0xe, 0x24, 0x92, 0x49, 0x4, 0x60,

    /* U+0403 "Ѓ" */
    0x24, 0xf8, 0x88, 0x88, 0x80,

    /* U+0404 "Є" */
    0x69, 0x8e, 0x88, 0x96,

    /* U+0405 "Ѕ" */
    0x69, 0x84, 0x21, 0x96,

    /* U+0406 "І" */
    0xe9, 0x24, 0x97,

    /* U+0407 "Ї" */
    0xd8, 0x1c, 0x42, 0x10, 0x84, 0x70,

    /* U+0408 "Ј" */
    0x31, 0x11, 0x19, 0x96,

    /* U+0409 "Љ" */
    0x31, 0x45, 0x16, 0x55, 0x55, 0x76,

    /* U+040A "Њ" */
    0xa5, 0x29, 0xea, 0xd6, 0xb6,

    /* U+040B "Ћ" */
    0xf8, 0x82, 0xe, 0x24, 0x92, 0x49,

    /* U+040C "Ќ" */
    0x24, 0x99, 0xac, 0xa9, 0x90,

    /* U+040D "Ѝ" */
    0x42, 0x99, 0xbb, 0xdd, 0x90,

    /* U+040E "Ў" */
    0x51, 0x23, 0x15, 0x10, 0x88, 0xc0,

    /* U+040F "Џ" */
    0x99, 0x99, 0x99, 0x9f, 0x22,

    /* U+0410 "А" */
    0x66, 0x99, 0xf9, 0x99,

    /* U+0411 "Б" */
    0xe8, 0x8e, 0x99, 0x9e,

    /* U+0412 "В" */
    0xe9, 0x9e, 0x99, 0x9e,

    /* U+0413 "Г" */
    0xf8, 0x88, 0x88, 0x88,

    /* U+0414 "Д" */
    0x72, 0x94, 0xa5, 0x29, 0x5f, 0x8c, 0x40,

    /* U+0415 "Е" */
    0xf8, 0x8e, 0x88, 0x8f,

    /* U+0416 "Ж" */
    0xad, 0x6a, 0xea, 0xd6, 0xb5,

    /* U+0417 "З" */
    0x69, 0x16, 0x11, 0x96,

    /* U+0418 "И" */
    0x99, 0xbb, 0xdd, 0x99,

    /* U+0419 "Й" */
    0x52, 0x99, 0xbb, 0xdd, 0x90,

    /* U+041A "К" */
    0x99, 0xac, 0xaa, 0x99,

    /* U+041B "Л" */
    0x19, 0x52, 0x94, 0xa5, 0x39,

    /* U+041C "М" */
    0x8e, 0xf7, 0x5a, 0xc6, 0x31,

    /* U+041D "Н" */
    0x99, 0x9f, 0x99, 0x99,

    /* U+041E "О" */
    0x69, 0x99, 0x99, 0x96,

    /* U+041F "П" */
    0xf9, 0x99, 0x99, 0x99,

    /* U+0420 "Р" */
    0xe9, 0x99, 0xe8, 0x88,

    /* U+0421 "С" */
    0x69, 0x88, 0x88, 0x96,

    /* U+0422 "Т" */
    0xf9, 0x8, 0x42, 0x10, 0x84,

    /* U+0423 "У" */
    0x8c, 0x54, 0xa2, 0x11, 0x18,

    /* U+0424 "Ф" */
    0x23, 0xab, 0x5a, 0xd5, 0xc4,

    /* U+0425 "Х" */
    0x99, 0x96, 0x69, 0x99,

    /* U+0426 "Ц" */
    0x94, 0xa5, 0x29, 0x4a, 0x5f, 0x8, 0x40,

    /* U+0427 "Ч" */
    0x99, 0x99, 0x71, 0x11,

    /* U+0428 "Ш" */
    0xad, 0x6b, 0x5a, 0xd6, 0xbf,

    /* U+0429 "Щ" */
    0xad, 0x6b, 0x5a, 0xd6, 0xbf, 0x8, 0x40,

    /* U+042A "Ъ" */
    0xe1, 0x8, 0x62, 0x94, 0xa6,

    /* U+042B "Ы" */
    0x8c, 0x63, 0x9a, 0xd6, 0xb9,

    /* U+042C "Ь" */
    0x88, 0x8e, 0x99, 0x9e,

    /* U+042D "Э" */
    0x69, 0x17, 0x11, 0x96,

    /* U+042E "Ю" */
    0x95, 0x6b, 0xda, 0xd6, 0xb2,

    /* U+042F "Я" */
    0x79, 0x97, 0x55, 0x99,

    /* U+0430 "а" */
    0x61, 0x79, 0x97,

    /* U+0431 "б" */
    0x68, 0x8e, 0x99, 0x96,

    /* U+0432 "в" */
    0xe9, 0xe9, 0x9e,

    /* U+0433 "г" */
    0xf8, 0x88, 0x88,

    /* U+0434 "д" */
    0x72, 0x94, 0xa5, 0x7e, 0x31,

    /* U+0435 "е" */
    0x69, 0xf8, 0x87,

    /* U+0436 "ж" */
    0xad, 0x5d, 0x5a, 0xd4,

    /* U+0437 "з" */
    0x69, 0x21, 0x96,

    /* U+0438 "и" */
    0x99, 0x9b, 0xd9,

    /* U+0439 "й" */
    0x57, 0x9, 0x99, 0xbd, 0x90,

    /* U+043A "к" */
    0x99, 0xae, 0x99,

    /* U+043B "л" */
    0x19, 0x52, 0x94, 0xc4,

    /* U+043C "м" */
    0x8c, 0x77, 0x5a, 0xd4,

    /* U+043D "н" */
    0x99, 0xf9, 0x99,

    /* U+043E "о" */
    0x69, 0x99, 0x96,

    /* U+043F "п" */
    0xf9, 0x99, 0x99,

    /* U+0440 "р" */
    0xe9, 0x99, 0x9e, 0x88,

    /* U+0441 "с" */
    0x69, 0x88, 0x87,

    /* U+0442 "т" */
    0xf9, 0x8, 0x42, 0x10,

    /* U+0443 "у" */
    0x99, 0x95, 0x62, 0x4c,

    /* U+0444 "ф" */
    0x21, 0x1d, 0x5a, 0xd6, 0xae, 0x21, 0x0,

    /* U+0445 "х" */
    0x99, 0x66, 0x99,

    /* U+0446 "ц" */
    0x94, 0xa5, 0x29, 0x7c, 0x21,

    /* U+0447 "ч" */
    0x99, 0x97, 0x11,

    /* U+0448 "ш" */
    0xad, 0x6b, 0x5a, 0xfc,

    /* U+0449 "щ" */
    0xad, 0x6b, 0x5a, 0xfc, 0x21,

    /* U+044A "ъ" */
    0xe5, 0xc, 0x52, 0x98,

    /* U+044B "ы" */
    0x8c, 0x73, 0x5a, 0xe4,

    /* U+044C "ь" */
    0x88, 0xe9, 0x9e,

    /* U+044D "э" */
    0xe1, 0x71, 0x1e,

    /* U+044E "ю" */
    0x95, 0x7b, 0x5a, 0xc8,

    /* U+044F "я" */
    0x79, 0x75, 0x99,

    /* U+0450 "ѐ" */
    0x42, 0x6, 0x9f, 0x88, 0x70,

    /* U+0451 "ё" */
    0x55, 0x6, 0x9f, 0x88, 0x70,

    /* U+0452 "ђ" */
    0x47, 0x90, 0xe4, 0xa5, 0x29, 0x11, 0x0,

    /* U+0453 "ѓ" */
    0x24, 0xf, 0x88, 0x88, 0x80,

    /* U+0454 "є" */
    0x78, 0xe8, 0x87,

    /* U+0455 "ѕ" */
    0x78, 0xc3, 0x1e,

    /* U+0456 "і" */
    0x48, 0x64, 0x92, 0xe0,

    /* U+0457 "ї" */
    0xb4, 0x64, 0x92, 0xe0,

    /* U+0458 "ј" */
    0x11, 0x3, 0x11, 0x11, 0x19, 0x60,

    /* U+0459 "љ" */
    0x31, 0x45, 0x95, 0x57, 0x60,

    /* U+045A "њ" */
    0xa5, 0x3d, 0x5a, 0xd8,

    /* U+045B "ћ" */
    0x47, 0x90, 0xb6, 0xa5, 0x29,

    /* U+045C "ќ" */
    0x24, 0x9, 0x9a, 0xe9, 0x90,

    /* U+045D "ѝ" */
    0x42, 0x9, 0x99, 0xbd, 0x90,

    /* U+045E "ў" */
    0x57, 0x9, 0x99, 0x56, 0x24, 0xc0,

    /* U+045F "џ" */
    0x99, 0x99, 0x9f, 0x22,

    /* U+0460 "Ѡ" */
    0x8d, 0x6b, 0x5a, 0xd6, 0xaa,

    /* U+0461 "ѡ" */
    0x8d, 0x6b, 0x5a, 0xa8,

    /* U+0462 "Ѣ" */
    0x23, 0xe2, 0xe, 0x24, 0x92, 0x4e,

    /* U+0463 "ѣ" */
    0x42, 0x3c, 0x87, 0x25, 0x2e,

    /* U+0464 "Ѥ" */
    0x9d, 0x69, 0xfa, 0x52, 0xb3,

    /* U+0465 "ѥ" */
    0x9d, 0x3f, 0x4a, 0x4c,

    /* U+0466 "Ѧ" */
    0x21, 0x14, 0xa7, 0x56, 0xb5,

    /* U+0467 "ѧ" */
    0x21, 0x14, 0xea, 0xd4,

    /* U+0468 "Ѩ" */
    0x92, 0x49, 0x2a, 0xaa, 0xfd, 0x75,

    /* U+0469 "ѩ" */
    0x92, 0x4e, 0xae, 0xd7, 0x50,

    /* U+046A "Ѫ" */
    0xfc, 0x54, 0xea, 0xd6, 0xb5,

    /* U+046B "ѫ" */
    0xfc, 0x54, 0xea, 0xd4,

    /* U+046C "Ѭ" */
    0xfe, 0xaa, 0xbc, 0x92, 0xed, 0x75,

    /* U+046D "ѭ" */
    0xff, 0x1a, 0xa4, 0xbb, 0x50,

    /* U+046E "Ѯ" */
    0x52, 0x69, 0x16, 0x11, 0x68, 0x60,

    /* U+046F "ѯ" */
    0x52, 0x69, 0x21, 0x16, 0x86,

    /* U+0470 "Ѱ" */
    0xad, 0x6b, 0x57, 0x10, 0x84,

    /* U+0471 "ѱ" */
    0x21, 0x2b, 0x5a, 0xd6, 0xae, 0x21, 0x0,

    /* U+0472 "Ѳ" */
    0x69, 0x9f, 0x99, 0x96,

    /* U+0473 "ѳ" */
    0x69, 0xf9, 0x96,

    /* U+0474 "Ѵ" */
    0x8c, 0x64, 0xa5, 0x30, 0x84,

    /* U+0475 "ѵ" */
    0x8c, 0x94, 0xc2, 0x10,

    /* U+0476 "Ѷ" */
    0x92, 0x63, 0x25, 0x29, 0x84, 0x20,

    /* U+0477 "ѷ" */
    0x92, 0x41, 0x19, 0x29, 0x84, 0x20,

    /* U+0478 "Ѹ" */
    0x42, 0x8b, 0x6d, 0xb6, 0xba, 0x92, 0x10, 0x40,

    /* U+0479 "ѹ" */
    0x56, 0xdb, 0x6b, 0xa9, 0x21, 0x4,

    /* U+047A "Ѻ" */
    0x23, 0xab, 0x18, 0xc6, 0x35, 0x71, 0x0,

    /* U+047B "ѻ" */
    0x23, 0xab, 0x18, 0xd5, 0xc4,

    /* U+047C "Ѽ" */
    0x65, 0xe3, 0x18, 0xd6, 0xb5, 0x50,

    /* U+047D "ѽ" */
    0x65, 0xc1, 0x18, 0xc6, 0xb5, 0x50,

    /* U+047E "Ѿ" */
    0xf9, 0x23, 0x5a, 0xd6, 0xb5, 0x50,

    /* U+047F "ѿ" */
    0xfd, 0x41, 0x1a, 0xd6, 0xb5, 0x50,

    /* U+0480 "Ҁ" */
    0x69, 0x88, 0x88, 0x86, 0x22,

    /* U+0481 "ҁ" */
    0x79, 0x88, 0x86, 0x22,

    /* U+0482 "҂" */
    0x11, 0x63, 0xc6, 0x88,

    /* U+0483 "҃" */
    0x3e, 0x0,

    /* U+0484 "҄" */
    0x69,

    /* U+0485 "҅" */
    0xe4,

    /* U+0486 "҆" */
    0xd8,

    /* U+0487 "҇" */
    0x64, 0xc0,

    /* U+048A "Ҋ" */
    0x51, 0x25, 0x2b, 0x5b, 0x52, 0xb8, 0x88,

    /* U+048B "ҋ" */
    0x53, 0x81, 0x29, 0x5b, 0x52, 0xb8, 0x88,

    /* U+048C "Ҍ" */
    0x47, 0x10, 0xe4, 0xa5, 0x2e,

    /* U+048D "ҍ" */
    0x47, 0x10, 0xe4, 0xb8,

    /* U+048E "Ҏ" */
    0xe9, 0x9b, 0xe9, 0x88,

    /* U+048F "ҏ" */
    0xe9, 0x99, 0xbe, 0x98,

    /* U+0490 "Ґ" */
    0x11, 0xf8, 0x88, 0x88, 0x80,

    /* U+0491 "ґ" */
    0x11, 0xf8, 0x88, 0x88,

    /* U+0492 "Ғ" */
    0x7a, 0x10, 0x8f, 0x21, 0x8,

    /* U+0493 "ғ" */
    0x7a, 0x11, 0xe4, 0x20,

    /* U+0494 "Ҕ" */
    0xf8, 0x88, 0xe9, 0x99, 0x16,

    /* U+0495 "ҕ" */
    0xf8, 0x8e, 0x99, 0x16,

    /* U+0496 "Җ" */
    0xad, 0x6a, 0xea, 0xd6, 0xb5, 0x8, 0x40,

    /* U+0497 "җ" */
    0xad, 0x5d, 0x5a, 0xd4, 0x21,

    /* U+0498 "Ҙ" */
    0x69, 0x16, 0x11, 0x96, 0x24,

    /* U+0499 "ҙ" */
    0x69, 0x21, 0x96, 0x24,

    /* U+049A "Қ" */
    0x94, 0xa9, 0x8a, 0x52, 0x53, 0x8, 0x40,

    /* U+049B "қ" */
    0x94, 0xa9, 0xc9, 0x4c, 0x21,

    /* U+049C "Ҝ" */
    0x8d, 0x6d, 0xcb, 0x56, 0x31,

    /* U+049D "ҝ" */
    0xad, 0xb9, 0x6a, 0xc4,

    /* U+049E "Ҟ" */
    0x4f, 0x54, 0xc5, 0x29, 0x29,

    /* U+049F "ҟ" */
    0x47, 0x90, 0x95, 0x39, 0x29,

    /* U+04A0 "Ҡ" */
    0xe4, 0x92, 0x8c, 0x28, 0xa2, 0x49,

    /* U+04A1 "ҡ" */
    0xe4, 0x92, 0x8e, 0x24, 0x90,

    /* U+04A2 "Ң" */
    0x94, 0xa5, 0xe9, 0x4a, 0x53, 0x8, 0x40,

    /* U+04A3 "ң" */
    0x94, 0xbd, 0x29, 0x4c, 0x21,

    /* U+04A4 "Ҥ" */
    0xbd, 0x29, 0xca, 0x52, 0x94,

    /* U+04A5 "ҥ" */
    0xbd, 0x39, 0x4a, 0x50,

    /* U+04A6 "Ҧ" */
    0xe5, 0x29, 0x6a, 0xd6, 0xb5, 0x9, 0x80,

    /* U+04A7 "ҧ" */
    0xe5, 0x2d, 0x5a, 0xd4, 0x26,

    /* U+04A8 "Ҩ" */
    0x74, 0x65, 0x5a, 0xd6, 0x4f,

    /* U+04A9 "ҩ" */
    0x74, 0x65, 0x59, 0x3c,

    /* U+04AA "Ҫ" */
    0x69, 0x88, 0x88, 0x96, 0x24,

    /* U+04AB "ҫ" */
    0x79, 0x88, 0x87, 0x24,

    /* U+04AC "Ҭ" */
    0xf9, 0x8, 0x42, 0x10, 0x86, 0x10, 0x80,

    /* U+04AD "ҭ" */
    0xf9, 0x8, 0x42, 0x18, 0x42,

    /* U+04AE "Ү" */
    0x8c, 0x54, 0xa2, 0x10, 0x84,

    /* U+04AF "ү" */
    0x8c, 0x54, 0xa2, 0x10, 0x84,

    /* U+04B0 "Ұ" */
    0x8c, 0x54, 0xa2, 0x7c, 0x84,

    /* U+04B1 "ұ" */
    0x8c, 0x54, 0xa2, 0x7c, 0x84,

    /* U+04B2 "Ҳ" */
    0x94, 0xa4, 0xc6, 0x4a, 0x53, 0x8, 0x40,

    /* U+04B3 "ҳ" */
    0x94, 0x98, 0xc9, 0x4c, 0x21,

    /* U+04B4 "Ҵ" */
    0xe9, 0x24, 0x92, 0x49, 0x24, 0x9f, 0x4, 0x10,

    /* U+04B5 "ҵ" */
    0xe9, 0x24, 0x92, 0x49, 0xf0, 0x41,

    /* U+04B6 "Ҷ" */
    0x94, 0xa5, 0x27, 0x8, 0x43, 0x8, 0x40,

    /* U+04B7 "ҷ" */
    0x94, 0xa4, 0xe1, 0xc, 0x21,

    /* U+04B8 "Ҹ" */
    0x8c, 0x6b, 0x57, 0x94, 0xa1,

    /* U+04B9 "ҹ" */
    0x8d, 0x6a, 0xf2, 0x84,

    /* U+04BA "Һ" */
    0x88, 0x8e, 0x99, 0x99,

    /* U+04BB "һ" */
    0x88, 0xbd, 0x99, 0x99,

    /* U+04BC "Ҽ" */
    0xb6, 0x72, 0xf4, 0x21, 0x26,

    /* U+04BD "ҽ" */
    0xb6, 0x7e, 0x84, 0x1c,

    /* U+04BE "Ҿ" */
    0xb6, 0x72, 0xf4, 0x21, 0x26, 0x10, 0x80,

    /* U+04BF "ҿ" */
    0xb6, 0x7e, 0x84, 0x1c, 0x42,

    /* U+04C0 "Ӏ" */
    0xe9, 0x24, 0x97,

    /* U+04C1 "Ӂ" */
    0x51, 0x23, 0x5a, 0xba, 0xb5, 0xa8,

    /* U+04C2 "ӂ" */
    0x53, 0x81, 0x5a, 0xba, 0xb5, 0xa8,

    /* U+04C3 "Ӄ" */
    0x99, 0xac, 0xa9, 0x99, 0x16,

    /* U+04C4 "ӄ" */
    0x99, 0xae, 0x99, 0x16,

    /* U+04C5 "Ӆ" */
    0x18, 0xa4, 0x92, 0x49, 0x24, 0xb7, 0x8, 0x40,

    /* U+04C6 "ӆ" */
    0x18, 0xa4, 0x92, 0x4a, 0x70, 0x84,

    /* U+04C7 "Ӈ" */
    0x99, 0x9f, 0x99, 0x99, 0x16,

    /* U+04C8 "ӈ" */
    0x99, 0xf9, 0x99, 0x16,

    /* U+04C9 "Ӊ" */
    0x94, 0xa5, 0xe9, 0x4a, 0x57, 0x11, 0x0,

    /* U+04CA "ӊ" */
    0x94, 0xbd, 0x29, 0x5c, 0x44,

    /* U+04CB "Ӌ" */
    0x99, 0x99, 0x71, 0x13, 0x22,

    /* U+04CC "ӌ" */
    0x99, 0x97, 0x13, 0x22,

    /* U+04CD "Ӎ" */
    0x8e, 0xf7, 0x5a, 0xc6, 0x33, 0x8, 0x80,

    /* U+04CE "ӎ" */
    0x8e, 0xeb, 0x58, 0xcc, 0x22,

    /* U+04CF "ӏ" */
    0xe9, 0x24, 0x97,

    /* U+04D0 "Ӑ" */
    0x96, 0x66, 0x9f, 0x99, 0x90,

    /* U+04D1 "ӑ" */
    0x96, 0x6, 0x17, 0x99, 0x70,

    /* U+04D2 "Ӓ" */
    0xd8, 0x18, 0xc9, 0x7a, 0x52, 0x90,

    /* U+04D3 "ӓ" */
    0x55, 0x6, 0x17, 0x99, 0x70,

    /* U+04D4 "Ӕ" */
    0x7b, 0x29, 0x7e, 0x52, 0x97,

    /* U+04D5 "ӕ" */
    0xd9, 0x5f, 0x4a, 0x6c,

    /* U+04D6 "Ӗ" */
    0x96, 0xf8, 0x8e, 0x88, 0xf0,

    /* U+04D7 "ӗ" */
    0x96, 0x6, 0x9f, 0x88, 0x70,

    /* U+04D8 "Ә" */
    0x69, 0x11, 0xf9, 0x96,

    /* U+04D9 "ә" */
    0xe1, 0x1f, 0x96,

    /* U+04DA "Ӛ" */
    0xd8, 0x19, 0x21, 0x7a, 0x52, 0x60,

    /* U+04DB "ӛ" */
    0x55, 0xe, 0x11, 0xf9, 0x60,

    /* U+04DC "Ӝ" */
    0xd8, 0x2b, 0x5a, 0xba, 0xb5, 0xa8,

    /* U+04DD "ӝ" */
    0x52, 0x81, 0x5a, 0xba, 0xb5, 0xa8,

    /* U+04DE "Ӟ" */
    0xd8, 0x19, 0x21, 0x30, 0x52, 0x60,

    /* U+04DF "ӟ" */
    0x55, 0x6, 0x92, 0x19, 0x60,

    /* U+04E0 "Ӡ" */
    0xf1, 0x26, 0x11, 0x96,

    /* U+04E1 "ӡ" */
    0xf1, 0x26, 0x11, 0x96,

    /* U+04E2 "Ӣ" */
    0xf0, 0x99, 0xbb, 0xdd, 0x90,

    /* U+04E3 "ӣ" */
    0xf0, 0x99, 0x9b, 0xd9,

    /* U+04E4 "Ӥ" */
    0xd8, 0x25, 0x2b, 0x5b, 0x5a, 0x90,

    /* U+04E5 "ӥ" */
    0x55, 0x9, 0x99, 0xbd, 0x90,

    /* U+04E6 "Ӧ" */
    0xd8, 0x19, 0x29, 0x4a, 0x52, 0x60,

    /* U+04E7 "ӧ" */
    0x55, 0x6, 0x99, 0x99, 0x60,

    /* U+04E8 "Ө" */
    0x69, 0x9f, 0x99, 0x96,

    /* U+04E9 "ө" */
    0x69, 0xf9, 0x96,

    /* U+04EA "Ӫ" */
    0xd8, 0x19, 0x29, 0x7a, 0x52, 0x60,

    /* U+04EB "ӫ" */
    0x55, 0x6, 0x9f, 0x99, 0x60,

    /* U+04EC "Ӭ" */
    0xd8, 0x19, 0x21, 0x38, 0x52, 0x60,

    /* U+04ED "ӭ" */
    0xaa, 0xe, 0x17, 0x11, 0xe0,

    /* U+04EE "Ӯ" */
    0x70, 0x23, 0x15, 0x10, 0x88, 0xc0,

    /* U+04EF "ӯ" */
    0x70, 0x99, 0x95, 0x62, 0x4c,

    /* U+04F0 "Ӱ" */
    0xd8, 0x23, 0x15, 0x10, 0x88, 0xc0,

    /* U+04F1 "ӱ" */
    0x55, 0x9, 0x99, 0x56, 0x24, 0xc0,

    /* U+04F2 "Ӳ" */
    0x4c, 0x81, 0x18, 0xa8, 0x88, 0xc0,

    /* U+04F3 "ӳ" */
    0x4c, 0x81, 0x29, 0x49, 0x4c, 0x22, 0x30,

    /* U+04F4 "Ӵ" */
    0xd8, 0x25, 0x29, 0x38, 0x42, 0x10,

    /* U+04F5 "ӵ" */
    0x55, 0x9, 0x99, 0x71, 0x10,

    /* U+04F6 "Ӷ" */
    0xf8, 0x88, 0x88, 0x8c, 0x44,

    /* U+04F7 "ӷ" */
    0xf8, 0x88, 0x8c, 0x44,

    /* U+04F8 "Ӹ" */
    0xd8, 0x23, 0x1c, 0xd6, 0xb5, 0xc8,

    /* U+04F9 "ӹ" */
    0x52, 0x81, 0x18, 0xe6, 0xb5, 0xc8,

    /* U+04FA "Ӻ" */
    0x7a, 0x10, 0x8f, 0x21, 0xc, 0x13, 0x0,

    /* U+04FB "ӻ" */
    0x7a, 0x11, 0xe4, 0x30, 0x4c,

    /* U+04FC "Ӽ" */
    0x99, 0x96, 0x69, 0x99, 0x16,

    /* U+04FD "ӽ" */
    0x99, 0x66, 0x99, 0x16,

    /* U+04FE "Ӿ" */
    0x99, 0x6f, 0x69, 0x99,

    /* U+04FF "ӿ" */
    0x96, 0xf6, 0x99
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 96, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 96, .box_w = 1, .box_h = 8, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 2, .adv_w = 96, .box_w = 3, .box_h = 3, .ofs_x = 2, .ofs_y = 5},
    {.bitmap_index = 4, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 9, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 15, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 20, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 25, .adv_w = 96, .box_w = 1, .box_h = 3, .ofs_x = 3, .ofs_y = 6},
    {.bitmap_index = 26, .adv_w = 96, .box_w = 3, .box_h = 10, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 30, .adv_w = 96, .box_w = 3, .box_h = 10, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 34, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 38, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 42, .adv_w = 96, .box_w = 3, .box_h = 4, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 44, .adv_w = 96, .box_w = 4, .box_h = 1, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 45, .adv_w = 96, .box_w = 2, .box_h = 2, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 46, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 53, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 57, .adv_w = 96, .box_w = 3, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 60, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 64, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 68, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 72, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 76, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 80, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 84, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 88, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 92, .adv_w = 96, .box_w = 2, .box_h = 5, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 94, .adv_w = 96, .box_w = 3, .box_h = 7, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 97, .adv_w = 96, .box_w = 4, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 101, .adv_w = 96, .box_w = 5, .box_h = 3, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 103, .adv_w = 96, .box_w = 4, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 107, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 112, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 117, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 121, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 125, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 129, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 133, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 137, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 141, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 145, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 149, .adv_w = 96, .box_w = 3, .box_h = 8, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 152, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 156, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 160, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 164, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 169, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 173, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 177, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 181, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 186, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 190, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 194, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 199, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 203, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 208, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 213, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 217, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 222, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 226, .adv_w = 96, .box_w = 3, .box_h = 10, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 230, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 237, .adv_w = 96, .box_w = 3, .box_h = 10, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 241, .adv_w = 96, .box_w = 5, .box_h = 3, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 243, .adv_w = 96, .box_w = 4, .box_h = 1, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 244, .adv_w = 96, .box_w = 2, .box_h = 3, .ofs_x = 2, .ofs_y = 5},
    {.bitmap_index = 245, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 248, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 252, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 255, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 259, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 262, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 266, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 270, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 274, .adv_w = 96, .box_w = 3, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 278, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 284, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 288, .adv_w = 96, .box_w = 3, .box_h = 8, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 291, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 295, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 298, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 301, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 305, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 309, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 312, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 315, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 319, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 322, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 326, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 330, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 333, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 337, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 340, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 347, .adv_w = 96, .box_w = 1, .box_h = 10, .ofs_x = 3, .ofs_y = -1},
    {.bitmap_index = 349, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 356, .adv_w = 96, .box_w = 5, .box_h = 3, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 358, .adv_w = 96, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 359, .adv_w = 96, .box_w = 1, .box_h = 8, .ofs_x = 3, .ofs_y = -2},
    {.bitmap_index = 360, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 364, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 369, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 373, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 378, .adv_w = 96, .box_w = 1, .box_h = 10, .ofs_x = 3, .ofs_y = -1},
    {.bitmap_index = 380, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 385, .adv_w = 96, .box_w = 3, .box_h = 2, .ofs_x = 2, .ofs_y = 6},
    {.bitmap_index = 386, .adv_w = 96, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 393, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 396, .adv_w = 96, .box_w = 5, .box_h = 4, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 399, .adv_w = 96, .box_w = 4, .box_h = 3, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 401, .adv_w = 96, .box_w = 3, .box_h = 1, .ofs_x = 2, .ofs_y = 3},
    {.bitmap_index = 402, .adv_w = 96, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 409, .adv_w = 96, .box_w = 4, .box_h = 1, .ofs_x = 1, .ofs_y = 7},
    {.bitmap_index = 410, .adv_w = 96, .box_w = 4, .box_h = 4, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 412, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 417, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 420, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 423, .adv_w = 96, .box_w = 3, .box_h = 3, .ofs_x = 2, .ofs_y = 5},
    {.bitmap_index = 425, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 429, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 434, .adv_w = 96, .box_w = 2, .box_h = 2, .ofs_x = 2, .ofs_y = 2},
    {.bitmap_index = 435, .adv_w = 96, .box_w = 4, .box_h = 4, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 437, .adv_w = 96, .box_w = 2, .box_h = 5, .ofs_x = 2, .ofs_y = 4},
    {.bitmap_index = 439, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 442, .adv_w = 96, .box_w = 5, .box_h = 4, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 445, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 452, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 459, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 466, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 471, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 476, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 481, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 486, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 491, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 497, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 502, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 507, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 512, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 517, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 522, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 527, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 533, .adv_w = 96, .box_w = 3, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 537, .adv_w = 96, .box_w = 3, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 541, .adv_w = 96, .box_w = 3, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 545, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 551, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 556, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 561, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 566, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 571, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 576, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 581, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 587, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 591, .adv_w = 96, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 599, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 604, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 609, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 614, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 620, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 626, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 630, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 634, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 639, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 644, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 649, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 654, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 659, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 664, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 668, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 672, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 677, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 682, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 687, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 692, .adv_w = 96, .box_w = 3, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 696, .adv_w = 96, .box_w = 3, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 700, .adv_w = 96, .box_w = 3, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 704, .adv_w = 96, .box_w = 3, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 708, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 712, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 717, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 722, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 727, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 732, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 737, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 742, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 747, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 753, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 758, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 763, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 768, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 773, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 779, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 784, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 790, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 795, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 801, .adv_w = 96, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 809, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 814, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 818, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 822, .adv_w = 96, .box_w = 3, .box_h = 8, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 825, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 831, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 835, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 841, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 846, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 852, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 857, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 862, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 868, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 873, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 877, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 881, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 885, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 889, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 896, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 900, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 905, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 909, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 913, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 918, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 922, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 927, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 932, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 936, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 940, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 944, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 948, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 952, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 957, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 962, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 967, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 971, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 978, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 982, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 987, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 994, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 999, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1004, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1008, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1012, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1017, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1021, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1024, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1028, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1031, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1034, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1039, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1042, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1046, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1049, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1052, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1057, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1060, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1064, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1068, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1071, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1074, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1077, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1081, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1084, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1088, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1092, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1099, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1102, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1107, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1110, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1114, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1119, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1123, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1127, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1130, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1133, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1137, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1140, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1145, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1150, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1157, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1162, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1165, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1168, .adv_w = 96, .box_w = 3, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 1172, .adv_w = 96, .box_w = 3, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 1176, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1182, .adv_w = 96, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1187, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1191, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1196, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1201, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1206, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1212, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1216, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1221, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1225, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1231, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1236, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1241, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1245, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1250, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1254, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1260, .adv_w = 96, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1265, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1270, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1274, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1280, .adv_w = 96, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1285, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1291, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1296, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1301, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1308, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1312, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1315, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1320, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1324, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1330, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1336, .adv_w = 96, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1344, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1350, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 1357, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 1362, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1368, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1374, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1380, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1386, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1391, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1395, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1399, .adv_w = 96, .box_w = 3, .box_h = 3, .ofs_x = 2, .ofs_y = 6},
    {.bitmap_index = 1401, .adv_w = 96, .box_w = 4, .box_h = 2, .ofs_x = 2, .ofs_y = 7},
    {.bitmap_index = 1402, .adv_w = 96, .box_w = 2, .box_h = 3, .ofs_x = 2, .ofs_y = 6},
    {.bitmap_index = 1403, .adv_w = 96, .box_w = 2, .box_h = 3, .ofs_x = 2, .ofs_y = 6},
    {.bitmap_index = 1404, .adv_w = 96, .box_w = 5, .box_h = 2, .ofs_x = 1, .ofs_y = 7},
    {.bitmap_index = 1406, .adv_w = 96, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1413, .adv_w = 96, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1420, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1425, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1429, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1433, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1437, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 1442, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1446, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1451, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1455, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1460, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1464, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1471, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1476, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1481, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1485, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1492, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1497, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1502, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1506, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1511, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1516, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1522, .adv_w = 96, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1527, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1534, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1539, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1544, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1548, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1555, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1560, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1565, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1569, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1574, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1578, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1585, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1590, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1595, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1600, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1605, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1610, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1617, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1622, .adv_w = 96, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1630, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1636, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1643, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1648, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1653, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1657, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1661, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1665, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1670, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1674, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1681, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1686, .adv_w = 96, .box_w = 3, .box_h = 8, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 1689, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1695, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1701, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1706, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1710, .adv_w = 96, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1718, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1724, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1729, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1733, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1740, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1745, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1750, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1754, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1761, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1766, .adv_w = 96, .box_w = 3, .box_h = 8, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 1769, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1774, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1779, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1785, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1790, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1795, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1799, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1804, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1809, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1813, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1816, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1822, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1827, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1833, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1839, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1845, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1850, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1854, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1858, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1863, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1867, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1873, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1878, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1884, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1889, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1893, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1896, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1902, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1907, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1913, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1918, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1924, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1929, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1935, .adv_w = 96, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1941, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1947, .adv_w = 96, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1954, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1960, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1965, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 1970, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1974, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1980, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1986, .adv_w = 96, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1993, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1998, .adv_w = 96, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 2003, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 2007, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2011, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



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
        .range_start = 1024, .range_length = 136, .glyph_id_start = 192,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 1162, .range_length = 118, .glyph_id_start = 328,
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
    .cmap_num = 4,
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
const lv_font_t greybeard_11 = {
#else
lv_font_t greybeard_11 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 11,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
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



#endif /*#if GREYBEARD_11*/

