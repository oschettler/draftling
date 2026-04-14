/*
 * Keyboard layout translation module.
 *
 * Translates HID keycodes + modifier flags into UTF-8 strings
 * for US-English, Ukrainian, German, and French layouts.
 *
 * Non-ASCII characters are encoded as UTF-8 byte sequences using
 * hex escapes to keep the source file ASCII-only.
 */

#include <cstring>
#include "sdkconfig.h"
#include "kb_layout.h"

/* Modifier flags (same as ble_keyboard.h) */
#define MOD_LSHIFT 0x02
#define MOD_RSHIFT 0x20
#define MOD_RALT   0x40  /* AltGr on international keyboards */

static kb_layout_id_t s_layout = (kb_layout_id_t)0;

/* HID keycodes for letter/number/symbol keys: 0x04..0x38 (53 keys) */
#define KC_A     0x04
#define KC_Z     0x1D
#define KC_1     0x1E
#define KC_0     0x27
#define KC_ENTER 0x28
#define KC_SPACE 0x2C
#define KC_MINUS 0x2D
#define KC_EQUAL 0x2E
#define KC_LBRAC 0x2F
#define KC_RBRAC 0x30
#define KC_BSLSH 0x31
#define KC_HASH  0x32  /* non-US # */
#define KC_SCOLN 0x33
#define KC_QUOTE 0x34
#define KC_GRAVE 0x35
#define KC_COMMA 0x36
#define KC_DOT   0x37
#define KC_SLASH 0x38

#ifdef CONFIG_KB_LAYOUT_ENABLE_US
/* ------------------------------------------------------------------ */
/* US-English layout                                                  */
/* ------------------------------------------------------------------ */

/* unshifted: keycodes 0x04..0x38 */
static const char *US_NORMAL[] = {
    "a","b","c","d","e","f","g","h","i","j","k","l",  /* 04-0F */
    "m","n","o","p","q","r","s","t","u","v","w","x",  /* 10-1B */
    "y","z",                                          /* 1C-1D */
    "1","2","3","4","5","6","7","8","9","0",           /* 1E-27 */
    NULL,NULL,NULL,NULL," ",                           /* 28-2C */
    "-","=","[","]","\\",                              /* 2D-31 */
    NULL,";","'","`",",",".","/",                      /* 32-38 */
};

/* shifted */
static const char *US_SHIFT[] = {
    "A","B","C","D","E","F","G","H","I","J","K","L",
    "M","N","O","P","Q","R","S","T","U","V","W","X",
    "Y","Z",
    "!","@","#","$","%","^","&","*","(",")",
    NULL,NULL,NULL,NULL," ",
    "_","+","{","}","|",
    NULL,":","\x22","~","<",">","?",
};
#endif /* CONFIG_KB_LAYOUT_ENABLE_US */

#ifdef CONFIG_KB_LAYOUT_ENABLE_DE
/* ------------------------------------------------------------------ */
/* German layout                                                      */
/* ------------------------------------------------------------------ */

/* German remaps Y<->Z, and several symbol keys differ.
 * Umlauts use UTF-8 encoding:
 *   ae = \xC3\xA4,  AE = \xC3\x84
 *   oe = \xC3\xB6,  OE = \xC3\x96
 *   ue = \xC3\xBC,  UE = \xC3\x9C
 *   ss = \xC3\x9F
 */

static const char *DE_NORMAL[] = {
    "a","b","c","d","e","f","g","h","i","j","k","l",  /* 04-0F */
    "m","n","o","p","q","r","s","t","u","v","w","x",  /* 10-1B */
    "z","y",                                          /* 1C-1D: Y and Z swapped */
    "1","2","3","4","5","6","7","8","9","0",           /* 1E-27 */
    NULL,NULL,NULL,NULL," ",                           /* 28-2C */
    "\xC3\x9F",                                       /* 2D: ss (Eszett) */
    "\xC2\xB4",                                       /* 2E: acute accent */
    "\xC3\xBC",                                       /* 2F: ue */
    "+",                                              /* 30 */
    "#",                                              /* 31 */
    NULL,                                             /* 32 */
    "\xC3\xB6",                                       /* 33: oe */
    "\xC3\xA4",                                       /* 34: ae */
    "^",                                              /* 35 */
    ",",".","-",                                       /* 36-38 */
};

static const char *DE_SHIFT[] = {
    "A","B","C","D","E","F","G","H","I","J","K","L",
    "M","N","O","P","Q","R","S","T","U","V","W","X",
    "Z","Y",
    "!","\x22",                                       /* 1E-1F */
    "\xC2\xA7",                                       /* 20: section sign */
    "$","%","&","/","(",")",                           /* 21-27 */
    "=",                                              /* (shifted 0) - stored at idx 9 */
    NULL,NULL,NULL,NULL," ",
    "?","`",
    "\xC3\x9C",                                       /* 2F: Ue */
    "*","'",NULL,
    "\xC3\x96",                                       /* 33: Oe */
    "\xC3\x84",                                       /* 34: Ae */
    "\xC2\xB0",                                       /* 35: degree sign */
    ";",":",                                          /* 36-37 */
    "_",                                              /* 38 */
};

/* German AltGr layer (Right Alt) */
static const char *DE_ALTGR[] = {
    NULL,NULL,NULL,NULL,                              /* 04-07 */
    "\xe2\x82\xac",                                   /* 08 (e) -> Euro sign */
    NULL,NULL,NULL,NULL,NULL,NULL,NULL,                /* 09-0F */
    NULL,NULL,NULL,NULL,                              /* 10-13 */
    "@",                                              /* 14 (q) -> @ */
    NULL,NULL,NULL,NULL,NULL,NULL,NULL,                /* 15-1B */
    NULL,NULL,                                        /* 1C-1D */
    NULL,                                             /* 1E */
    "\xC2\xB2",                                       /* 1F (2) -> superscript 2 */
    "\xC2\xB3",                                       /* 20 (3) -> superscript 3 */
    NULL,NULL,NULL,                                   /* 21-23 */
    "{",                                              /* 24 (7) -> { */
    "[",                                              /* 25 (8) -> [ */
    "]",                                              /* 26 (9) -> ] */
    "}",                                              /* 27 (0) -> } */
    NULL,NULL,NULL,NULL,NULL,                          /* 28-2C */
    "\\",                                             /* 2D -> backslash */
    NULL,NULL,                                        /* 2E-2F */
    "~",                                              /* 30 -> ~ */
    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,           /* 31-38 */
};
#endif /* CONFIG_KB_LAYOUT_ENABLE_DE */

#ifdef CONFIG_KB_LAYOUT_ENABLE_FR
/* ------------------------------------------------------------------ */
/* French (AZERTY) layout                                             */
/* ------------------------------------------------------------------ */
/* French remaps many keys: A<->Q, Z<->W, M moves, numbers are shifted.
 * Accented chars use UTF-8:
 *   e-grave = \xC3\xA8,  E-grave = \xC3\x88
 *   e-acute = \xC3\xA9
 *   a-grave = \xC3\xA0
 *   u-grave = \xC3\xB9
 *   c-cedilla = \xC3\xA7, C-cedilla = \xC3\x87
 */

static const char *FR_NORMAL[] = {
    "q","b","c","d","e","f","g","h","i","j","k","l",  /* 04-0F: a->q */
    ",","n","o","p","a","r","s","t","u","v","z","x",  /* 10-1B: m->,q->a,w->z */
    "y","w",                                          /* 1C-1D: z->w (AZERTY) */
    "&",                                              /* 1E: 1 -> & */
    "\xC3\xA9",                                       /* 1F: 2 -> e-acute */
    "\x22",                                           /* 20: 3 -> " */
    "'",                                              /* 21: 4 -> ' */
    "(",                                              /* 22: 5 -> ( */
    "-",                                              /* 23: 6 -> - */
    "\xC3\xA8",                                       /* 24: 7 -> e-grave */
    "_",                                              /* 25: 8 -> _ */
    "\xC3\xA7",                                       /* 26: 9 -> c-cedilla */
    "\xC3\xA0",                                       /* 27: 0 -> a-grave */
    NULL,NULL,NULL,NULL," ",                           /* 28-2C */
    ")",                                              /* 2D -> ) */
    "=",                                              /* 2E -> = */
    "^",                                              /* 2F */
    "$",                                              /* 30 */
    "*",                                              /* 31 */
    NULL,                                             /* 32 */
    "m",                                              /* 33: semicolon -> m */
    "\xC3\xB9",                                       /* 34: quote -> u-grave */
    "\xC2\xB2",                                       /* 35: grave -> superscript 2 */
    ";",":",                                          /* 36-37 */
    "!",                                              /* 38 */
};

static const char *FR_SHIFT[] = {
    "Q","B","C","D","E","F","G","H","I","J","K","L",
    "?","N","O","P","A","R","S","T","U","V","Z","X",
    "Y","W",
    "1","2","3","4","5","6","7","8","9","0",
    NULL,NULL,NULL,NULL," ",
    "\xC2\xB0",                                       /* 2D: ) shifted -> degree */
    "+",                                              /* 2E */
    "\xC2\xA8",                                       /* 2F: diaeresis */
    "\xC2\xA3",                                       /* 30: pound sign */
    "\xC2\xB5",                                       /* 31: micro sign */
    NULL,
    "M",                                              /* 33 */
    "%",                                              /* 34 */
    NULL,                                             /* 35 */
    ".","/",                                          /* 36-37 */
    "\xC2\xA7",                                       /* 38: section sign */
};

/* French AltGr layer */
static const char *FR_ALTGR[] = {
    NULL,NULL,NULL,NULL,                              /* 04-07 */
    "\xe2\x82\xac",                                   /* 08 (e) -> Euro sign */
    NULL,NULL,NULL,NULL,NULL,NULL,NULL,                /* 09-0F */
    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,           /* 10-17 */
    NULL,NULL,NULL,NULL,                              /* 18-1B */
    NULL,NULL,                                        /* 1C-1D */
    NULL,                                             /* 1E */
    "~",                                              /* 1F (2) -> ~ */
    "#",                                              /* 20 (3) -> # */
    "{",                                              /* 21 (4) -> { */
    "[",                                              /* 22 (5) -> [ */
    "|",                                              /* 23 (6) -> | */
    "`",                                              /* 24 (7) -> ` */
    "\\",                                             /* 25 (8) -> backslash */
    "^",                                              /* 26 (9) -> ^ */
    "@",                                              /* 27 (0) -> @ */
    NULL,NULL,NULL,NULL,NULL,                          /* 28-2C */
    "]",                                              /* 2D -> ] */
    "}",                                              /* 2E -> } */
    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL, /* 2F-38 */
};
#endif /* CONFIG_KB_LAYOUT_ENABLE_FR */

#ifdef CONFIG_KB_LAYOUT_ENABLE_KO
/* ------------------------------------------------------------------ */
/* Korean Dubeolsik layout                                            */
/* ------------------------------------------------------------------ */
/* Standard 2-set (dubeolsik) Hangul layout.
 * Maps physical keys to Hangul Compatibility Jamo (U+3131..U+3163).
 * Hangul Jamo UTF-8 encoding: 0xE3 0x84 0xB1 .. 0xE3 0x85 0xA3
 */

static const char *KO_NORMAL[] = {
    /* 04 a */ "\xE3\x85\x81",   /* U+3141 Jamo mieum */
    /* 05 b */ "\xE3\x85\xA0",   /* U+3160 Jamo yu */
    /* 06 c */ "\xE3\x85\x8A",   /* U+314A Jamo chieuch */
    /* 07 d */ "\xE3\x85\x87",   /* U+3147 Jamo ieung */
    /* 08 e */ "\xE3\x84\xB7",   /* U+3137 Jamo digeud */
    /* 09 f */ "\xE3\x84\xB9",   /* U+3139 Jamo rieul */
    /* 0A g */ "\xE3\x85\x8E",   /* U+314E Jamo hieuh */
    /* 0B h */ "\xE3\x85\x97",   /* U+3157 Jamo o */
    /* 0C i */ "\xE3\x85\x91",   /* U+3151 Jamo ya */
    /* 0D j */ "\xE3\x85\x93",   /* U+3153 Jamo eo */
    /* 0E k */ "\xE3\x85\x8F",   /* U+314F Jamo a */
    /* 0F l */ "\xE3\x85\xA3",   /* U+3163 Jamo i */
    /* 10 m */ "\xE3\x85\xA1",   /* U+3161 Jamo eu */
    /* 11 n */ "\xE3\x85\x9C",   /* U+315C Jamo u */
    /* 12 o */ "\xE3\x85\x90",   /* U+3150 Jamo ae */
    /* 13 p */ "\xE3\x85\x94",   /* U+3154 Jamo e */
    /* 14 q */ "\xE3\x85\x82",   /* U+3142 Jamo bieub */
    /* 15 r */ "\xE3\x84\xB1",   /* U+3131 Jamo giyeog */
    /* 16 s */ "\xE3\x84\xB4",   /* U+3134 Jamo nieun */
    /* 17 t */ "\xE3\x85\x85",   /* U+3145 Jamo sios */
    /* 18 u */ "\xE3\x85\x95",   /* U+3155 Jamo yeo */
    /* 19 v */ "\xE3\x85\x8D",   /* U+314D Jamo pieup */
    /* 1A w */ "\xE3\x85\x88",   /* U+3148 Jamo jieuj */
    /* 1B x */ "\xE3\x85\x8C",   /* U+314C Jamo tieut */
    /* 1C y */ "\xE3\x85\x9B",   /* U+315B Jamo yo */
    /* 1D z */ "\xE3\x85\x8B",   /* U+314B Jamo kieuk */
    /* 1E-27: numbers */
    "1","2","3","4","5","6","7","8","9","0",
    /* 28-2C */ NULL, NULL, NULL, NULL, " ",
    /* 2D-38: symbols same as US */
    "-","=","[","]","\\",
    NULL,";","'","`",",",".","/",
};

static const char *KO_SHIFT[] = {
    /* 04 A */ "\xE3\x85\x81",   /* U+3141 mieum (no shift variant) */
    /* 05 B */ "\xE3\x85\xA0",   /* U+3160 yu */
    /* 06 C */ "\xE3\x85\x8A",   /* U+314A chieuch */
    /* 07 D */ "\xE3\x85\x87",   /* U+3147 ieung */
    /* 08 E */ "\xE3\x84\xB8",   /* U+3138 ssangdigeud */
    /* 09 F */ "\xE3\x84\xB9",   /* U+3139 rieul */
    /* 0A G */ "\xE3\x85\x8E",   /* U+314E hieuh */
    /* 0B H */ "\xE3\x85\x97",   /* U+3157 o */
    /* 0C I */ "\xE3\x85\x91",   /* U+3151 ya */
    /* 0D J */ "\xE3\x85\x93",   /* U+3153 eo */
    /* 0E K */ "\xE3\x85\x8F",   /* U+314F a */
    /* 0F L */ "\xE3\x85\xA3",   /* U+3163 i */
    /* 10 M */ "\xE3\x85\xA1",   /* U+3161 eu */
    /* 11 N */ "\xE3\x85\x9C",   /* U+315C u */
    /* 12 O */ "\xE3\x85\x92",   /* U+3152 yae */
    /* 13 P */ "\xE3\x85\x96",   /* U+3156 ye */
    /* 14 Q */ "\xE3\x85\x83",   /* U+3143 ssangbieub */
    /* 15 R */ "\xE3\x84\xB2",   /* U+3132 ssanggiyeog */
    /* 16 S */ "\xE3\x84\xB4",   /* U+3134 nieun */
    /* 17 T */ "\xE3\x85\x86",   /* U+3146 ssangsios */
    /* 18 U */ "\xE3\x85\x95",   /* U+3155 yeo */
    /* 19 V */ "\xE3\x85\x8D",   /* U+314D pieup */
    /* 1A W */ "\xE3\x85\x89",   /* U+3149 ssangjieuj */
    /* 1B X */ "\xE3\x85\x8C",   /* U+314C tieut */
    /* 1C Y */ "\xE3\x85\x9B",   /* U+315B yo */
    /* 1D Z */ "\xE3\x85\x8B",   /* U+314B kieuk */
    /* 1E-27: shifted numbers (symbols) */
    "!","@","#","$","%","^","&","*","(",")",
    /* 28-2C */ NULL, NULL, NULL, NULL, " ",
    "_","+","{","}","|",
    NULL,":","\x22","~","<",">","?",
};
#endif /* CONFIG_KB_LAYOUT_ENABLE_KO */

#ifdef CONFIG_KB_LAYOUT_ENABLE_JA
/* ------------------------------------------------------------------ */
/* Japanese Kana layout (JIS X 6002)                                  */
/* ------------------------------------------------------------------ */
/* Direct kana input: each key produces a Hiragana character.
 * Hiragana range: U+3041..U+3093, UTF-8: 0xE3 0x81 0x81 .. 0xE3 0x82 0x93
 * Katakana (shifted): U+30A1..U+30F3
 * Long vowel mark: U+30FC (katakana)
 */

static const char *JA_NORMAL[] = {
    /* 04 a */ "\xE3\x81\xA1",   /* U+3061 chi */
    /* 05 b */ "\xE3\x81\x93",   /* U+3053 ko */
    /* 06 c */ "\xE3\x81\x9D",   /* U+305D so */
    /* 07 d */ "\xE3\x81\x97",   /* U+3057 shi */
    /* 08 e */ "\xE3\x81\x84",   /* U+3044 i */
    /* 09 f */ "\xE3\x81\xAF",   /* U+306F ha */
    /* 0A g */ "\xE3\x81\x8D",   /* U+304D ki */
    /* 0B h */ "\xE3\x81\x8F",   /* U+304F ku */
    /* 0C i */ "\xE3\x81\xAB",   /* U+306B ni */
    /* 0D j */ "\xE3\x81\xBE",   /* U+307E ma */
    /* 0E k */ "\xE3\x81\xAE",   /* U+306E no */
    /* 0F l */ "\xE3\x82\x8A",   /* U+308A ri */
    /* 10 m */ "\xE3\x82\x82",   /* U+3082 mo */
    /* 11 n */ "\xE3\x81\xBF",   /* U+307F mi */
    /* 12 o */ "\xE3\x82\x89",   /* U+3089 ra */
    /* 13 p */ "\xE3\x81\x9B",   /* U+305B se */
    /* 14 q */ "\xE3\x81\x9F",   /* U+305F ta */
    /* 15 r */ "\xE3\x81\x99",   /* U+3059 su */
    /* 16 s */ "\xE3\x81\xA8",   /* U+3068 to */
    /* 17 t */ "\xE3\x81\x8B",   /* U+304B ka */
    /* 18 u */ "\xE3\x81\xAA",   /* U+306A na */
    /* 19 v */ "\xE3\x81\xB2",   /* U+3072 hi */
    /* 1A w */ "\xE3\x81\xA6",   /* U+3066 te */
    /* 1B x */ "\xE3\x81\x95",   /* U+3055 sa */
    /* 1C y */ "\xE3\x82\x93",   /* U+3093 n */
    /* 1D z */ "\xE3\x81\xA4",   /* U+3064 tsu */
    /* 1E 1 */ "\xE3\x81\xAC",   /* U+306C nu */
    /* 1F 2 */ "\xE3\x81\xB5",   /* U+3075 fu */
    /* 20 3 */ "\xE3\x81\x82",   /* U+3042 a */
    /* 21 4 */ "\xE3\x81\x86",   /* U+3046 u */
    /* 22 5 */ "\xE3\x81\x88",   /* U+3048 e */
    /* 23 6 */ "\xE3\x81\x8A",   /* U+304A o */
    /* 24 7 */ "\xE3\x82\x84",   /* U+3084 ya */
    /* 25 8 */ "\xE3\x82\x86",   /* U+3086 yu */
    /* 26 9 */ "\xE3\x82\x88",   /* U+3088 yo */
    /* 27 0 */ "\xE3\x82\x8F",   /* U+308F wa */
    /* 28-2C */ NULL, NULL, NULL, NULL, " ",
    /* 2D - */ "\xE3\x81\xBB",   /* U+307B ho */
    /* 2E = */ "\xE3\x81\xB8",   /* U+3078 he */
    /* 2F [ */ "\xE3\x82\x9B",   /* U+309B dakuten */
    /* 30 ] */ "\xE3\x82\x80",   /* U+3080 mu */
    /* 31 \ */ "\xE3\x83\xBC",   /* U+30FC long vowel mark */
    /* 32   */ NULL,
    /* 33 ; */ "\xE3\x82\x8C",   /* U+308C re */
    /* 34 ' */ "\xE3\x81\x91",   /* U+3051 ke */
    /* 35 ` */ "\xE3\x82\x9C",   /* U+309C handakuten */
    /* 36 , */ "\xE3\x81\xAD",   /* U+306D ne */
    /* 37 . */ "\xE3\x82\x8B",   /* U+308B ru */
    /* 38 / */ "\xE3\x82\x81",   /* U+3081 me */
};

/* Shifted: small kana and katakana variants */
static const char *JA_SHIFT[] = {
    /* 04 A */ "\xE3\x81\xA1",   /* U+3061 chi */
    /* 05 B */ "\xE3\x81\x93",   /* U+3053 ko */
    /* 06 C */ "\xE3\x81\x9D",   /* U+305D so */
    /* 07 D */ "\xE3\x81\x97",   /* U+3057 shi */
    /* 08 E */ "\xE3\x81\x84",   /* U+3044 i */
    /* 09 F */ "\xE3\x81\xAF",   /* U+306F ha */
    /* 0A G */ "\xE3\x81\x8D",   /* U+304D ki */
    /* 0B H */ "\xE3\x81\x8F",   /* U+304F ku */
    /* 0C I */ "\xE3\x81\xAB",   /* U+306B ni */
    /* 0D J */ "\xE3\x81\xBE",   /* U+307E ma */
    /* 0E K */ "\xE3\x81\xAE",   /* U+306E no */
    /* 0F L */ "\xE3\x82\x8A",   /* U+308A ri */
    /* 10 M */ "\xE3\x82\x82",   /* U+3082 mo */
    /* 11 N */ "\xE3\x81\xBF",   /* U+307F mi */
    /* 12 O */ "\xE3\x82\x89",   /* U+3089 ra */
    /* 13 P */ "\xE3\x81\x9B",   /* U+305B se */
    /* 14 Q */ "\xE3\x81\x9F",   /* U+305F ta */
    /* 15 R */ "\xE3\x81\x99",   /* U+3059 su */
    /* 16 S */ "\xE3\x81\xA8",   /* U+3068 to */
    /* 17 T */ "\xE3\x81\x8B",   /* U+304B ka */
    /* 18 U */ "\xE3\x81\xAA",   /* U+306A na */
    /* 19 V */ "\xE3\x81\xB2",   /* U+3072 hi */
    /* 1A W */ "\xE3\x81\xA6",   /* U+3066 te */
    /* 1B X */ "\xE3\x81\x95",   /* U+3055 sa */
    /* 1C Y */ "\xE3\x82\x93",   /* U+3093 n */
    /* 1D Z */ "\xE3\x81\xA3",   /* U+3063 small tsu */
    /* 1E 1 */ "\xE3\x81\xAC",   /* U+306C nu */
    /* 1F 2 */ "\xE3\x81\xB5",   /* U+3075 fu */
    /* 20 3 */ "\xE3\x81\x81",   /* U+3041 small a */
    /* 21 4 */ "\xE3\x81\x85",   /* U+3045 small u */
    /* 22 5 */ "\xE3\x81\x87",   /* U+3047 small e */
    /* 23 6 */ "\xE3\x81\x89",   /* U+3049 small o */
    /* 24 7 */ "\xE3\x82\x83",   /* U+3083 small ya */
    /* 25 8 */ "\xE3\x82\x85",   /* U+3085 small yu */
    /* 26 9 */ "\xE3\x82\x87",   /* U+3087 small yo */
    /* 27 0 */ "\xE3\x82\x92",   /* U+3092 wo */
    /* 28-2C */ NULL, NULL, NULL, NULL, " ",
    /* 2D - */ "\xE3\x81\xBB",   /* U+307B ho */
    /* 2E = */ "\xE3\x81\xB8",   /* U+3078 he */
    /* 2F [ */ "\xE3\x82\x9B",   /* U+309B dakuten */
    /* 30 ] */ "\xE3\x82\x80",   /* U+3080 mu */
    /* 31 \ */ "\xE3\x83\xBC",   /* U+30FC long vowel mark */
    /* 32   */ NULL,
    /* 33 ; */ "\xE3\x82\x8C",   /* U+308C re */
    /* 34 ' */ "\xE3\x81\x91",   /* U+3051 ke */
    /* 35 ` */ "\xE3\x82\x9C",   /* U+309C handakuten */
    /* 36 , */ "\xE3\x80\x81",   /* U+3001 ideographic comma */
    /* 37 . */ "\xE3\x80\x82",   /* U+3002 ideographic period */
    /* 38 / */ "\xE3\x83\xBB",   /* U+30FB katakana middle dot */
};
#endif /* CONFIG_KB_LAYOUT_ENABLE_JA */

#ifdef CONFIG_KB_LAYOUT_ENABLE_ZH
/* ------------------------------------------------------------------ */
/* Chinese Zhuyin (Bopomofo) layout                                   */
/* ------------------------------------------------------------------ */
/* Standard Zhuyin layout on a US physical keyboard.
 * Maps keys to Bopomofo symbols (U+3105..U+3129).
 * UTF-8: 0xE3 0x84 0x85 .. 0xE3 0x84 0xA9
 *
 * Also includes tone marks on number keys:
 * 2 -> U+02CA (acute), 3 -> U+02C7 (caron), 4 -> U+02CB (grave)
 * (Tone 1 is unmarked, Tone 5/neutral uses dot U+02D9)
 */

static const char *ZH_NORMAL[] = {
    /* 04 a -> U+3107 */ "\xE3\x84\x87",   /* mieum (Bopomofo m) */
    /* 05 b -> U+3116 */ "\xE3\x84\x96",   /* Bopomofo r */
    /* 06 c -> U+310F */ "\xE3\x84\x8F",   /* Bopomofo h */
    /* 07 d -> U+3114 */ "\xE3\x84\x94",   /* Bopomofo ch */
    /* 08 e -> U+3110 */ "\xE3\x84\x90",   /* Bopomofo j */
    /* 09 f -> U+3117 */ "\xE3\x84\x97",   /* Bopomofo z */
    /* 0A g -> U+3118 */ "\xE3\x84\x98",   /* Bopomofo c */
    /* 0B h -> U+3119 */ "\xE3\x84\x99",   /* Bopomofo s */
    /* 0C i -> U+311A */ "\xE3\x84\x9A",   /* Bopomofo a */
    /* 0D j -> U+311B */ "\xE3\x84\x9B",   /* Bopomofo o */
    /* 0E k -> U+311C */ "\xE3\x84\x9C",   /* Bopomofo e */
    /* 0F l -> U+311D */ "\xE3\x84\x9D",   /* Bopomofo eh */
    /* 10 m -> U+3128 */ "\xE3\x84\xA8",   /* Bopomofo u */
    /* 11 n -> U+3123 */ "\xE3\x84\xA3",   /* Bopomofo en */
    /* 12 o -> U+311E */ "\xE3\x84\x9E",   /* Bopomofo ai */
    /* 13 p -> U+311F */ "\xE3\x84\x9F",   /* Bopomofo ei */
    /* 14 q -> U+3105 */ "\xE3\x84\x85",   /* Bopomofo b */
    /* 15 r -> U+3111 */ "\xE3\x84\x91",   /* Bopomofo q */
    /* 16 s -> U+310B */ "\xE3\x84\x8B",   /* Bopomofo n */
    /* 17 t -> U+3112 */ "\xE3\x84\x92",   /* Bopomofo x */
    /* 18 u -> U+3120 */ "\xE3\x84\xA0",   /* Bopomofo ao */
    /* 19 v -> U+3129 */ "\xE3\x84\xA9",   /* Bopomofo yu */
    /* 1A w -> U+3106 */ "\xE3\x84\x86",   /* Bopomofo p */
    /* 1B x -> U+310C */ "\xE3\x84\x8C",   /* Bopomofo l */
    /* 1C y -> U+3121 */ "\xE3\x84\xA1",   /* Bopomofo ou */
    /* 1D z -> U+310A */ "\xE3\x84\x8A",   /* Bopomofo t */
    /* 1E 1 -> tone 1 (unmarked, pass through) */ "1",
    /* 1F 2 -> tone 2 (acute accent) */ "\xCB\x8A",  /* U+02CA */
    /* 20 3 -> tone 3 (caron) */ "\xCB\x87",          /* U+02C7 */
    /* 21 4 -> tone 4 (grave accent) */ "\xCB\x8B",   /* U+02CB */
    /* 22 5 -> U+02D9 (dot above, neutral tone) */ "\xCB\x99", /* U+02D9 */
    /* 23 6 -> U+3113 */ "\xE3\x84\x93",   /* Bopomofo zh */
    /* 24 7 -> U+3122 */ "\xE3\x84\xA2",   /* Bopomofo an */
    /* 25 8 -> U+3124 */ "\xE3\x84\xA4",   /* Bopomofo ang */
    /* 26 9 -> U+3125 */ "\xE3\x84\xA5",   /* Bopomofo eng */
    /* 27 0 -> U+3126 */ "\xE3\x84\xA6",   /* Bopomofo er */
    /* 28-2C */ NULL, NULL, NULL, NULL, " ",
    /* 2D - -> U+3127 */ "\xE3\x84\xA7",   /* Bopomofo i */
    /* 2E-38: remaining symbols pass through */
    "=","[","]","\\",
    NULL,";","'","`",",",".","/",
};

static const char *ZH_SHIFT[] = {
    /* Shifted: same Bopomofo symbols (no case distinction) */
    /* 04 A */ "\xE3\x84\x87",   /* U+3107 */
    /* 05 B */ "\xE3\x84\x96",   /* U+3116 */
    /* 06 C */ "\xE3\x84\x8F",   /* U+310F */
    /* 07 D */ "\xE3\x84\x94",   /* U+3114 */
    /* 08 E */ "\xE3\x84\x90",   /* U+3110 */
    /* 09 F */ "\xE3\x84\x97",   /* U+3117 */
    /* 0A G */ "\xE3\x84\x98",   /* U+3118 */
    /* 0B H */ "\xE3\x84\x99",   /* U+3119 */
    /* 0C I */ "\xE3\x84\x9A",   /* U+311A */
    /* 0D J */ "\xE3\x84\x9B",   /* U+311B */
    /* 0E K */ "\xE3\x84\x9C",   /* U+311C */
    /* 0F L */ "\xE3\x84\x9D",   /* U+311D */
    /* 10 M */ "\xE3\x84\xA8",   /* U+3128 */
    /* 11 N */ "\xE3\x84\xA3",   /* U+3123 */
    /* 12 O */ "\xE3\x84\x9E",   /* U+311E */
    /* 13 P */ "\xE3\x84\x9F",   /* U+311F */
    /* 14 Q */ "\xE3\x84\x85",   /* U+3105 */
    /* 15 R */ "\xE3\x84\x91",   /* U+3111 */
    /* 16 S */ "\xE3\x84\x8B",   /* U+310B */
    /* 17 T */ "\xE3\x84\x92",   /* U+3112 */
    /* 18 U */ "\xE3\x84\xA0",   /* U+3120 */
    /* 19 V */ "\xE3\x84\xA9",   /* U+3129 */
    /* 1A W */ "\xE3\x84\x86",   /* U+3106 */
    /* 1B X */ "\xE3\x84\x8C",   /* U+310C */
    /* 1C Y */ "\xE3\x84\xA1",   /* U+3121 */
    /* 1D Z */ "\xE3\x84\x8A",   /* U+310A */
    /* 1E-27: shifted numbers -> symbols */
    "!","@","#","$","%","^","&","*","(",")",
    /* 28-2C */ NULL, NULL, NULL, NULL, " ",
    "_","+","{","}","|",
    NULL,":","\x22","~","<",">","?",
};
#endif /* CONFIG_KB_LAYOUT_ENABLE_ZH */

#ifdef CONFIG_KB_LAYOUT_ENABLE_UA
/* ------------------------------------------------------------------ */
/* Ukrainian layout                                                   */
/* ------------------------------------------------------------------ */
/* Ukrainian Cyrillic layout mapped on a US physical keyboard.
 * Each entry is a UTF-8 encoded Ukrainian letter.
 *
 * Physical key -> Ukrainian letter mapping (unshifted):
 * q->j  w->ts  e->u  r->k  t->e  y->n  u->g  i->sh  o->shch  p->z
 * a->f  s->i   d->v  f->a  g->p  h->r  j->o  k->l   l->d
 * z->ya x->ch  c->s  v->m  b->y  n->t  [->kh ]->yi   ;->zh    '->ye
 * `->ghe
 */

/* Lowercase Ukrainian letters as UTF-8 hex escapes.
 * Cyrillic block: U+0400..U+04FF -> UTF-8: 0xD0 0x80..0xBF, 0xD1 0x80..0xBF
 */

static const char *UA_NORMAL[] = {
    /* 04 a */ "\xD1\x84",   /* f (Cyrillic) */
    /* 05 b */ "\xD0\xB8",   /* i (short) */
    /* 06 c */ "\xD1\x81",   /* s */
    /* 07 d */ "\xD0\xB2",   /* v */
    /* 08 e */ "\xD1\x83",   /* u */
    /* 09 f */ "\xD0\xB0",   /* a */
    /* 0A g */ "\xD0\xBF",   /* p */
    /* 0B h */ "\xD1\x80",   /* r */
    /* 0C i */ "\xD1\x88",   /* sh */
    /* 0D j */ "\xD0\xBE",   /* o */
    /* 0E k */ "\xD0\xBB",   /* l */
    /* 0F l */ "\xD0\xB4",   /* d */
    /* 10 m */ "\xD1\x8C",   /* soft sign */
    /* 11 n */ "\xD1\x82",   /* t */
    /* 12 o */ "\xD1\x89",   /* shch */
    /* 13 p */ "\xD0\xB7",   /* z */
    /* 14 q */ "\xD0\xB9",   /* j (short i) */
    /* 15 r */ "\xD0\xBA",   /* k */
    /* 16 s */ "\xD1\x96",   /* i (Ukrainian) */
    /* 17 t */ "\xD0\xB5",   /* e */
    /* 18 u */ "\xD0\xB3",   /* g */
    /* 19 v */ "\xD0\xBC",   /* m */
    /* 1A w */ "\xD1\x86",   /* ts */
    /* 1B x */ "\xD1\x87",   /* ch */
    /* 1C y */ "\xD0\xBD",   /* n */
    /* 1D z */ "\xD1\x8F",   /* ya */
    /* 1E 1 */ "1",
    /* 1F 2 */ "2",
    /* 20 3 */ "3",
    /* 21 4 */ "4",
    /* 22 5 */ "5",
    /* 23 6 */ "6",
    /* 24 7 */ "7",
    /* 25 8 */ "8",
    /* 26 9 */ "9",
    /* 27 0 */ "0",
    /* 28-2C */ NULL, NULL, NULL, NULL, " ",
    /* 2D - */ "-",
    /* 2E = */ "=",
    /* 2F [ */ "\xD1\x85",   /* kh */
    /* 30 ] */ "\xD1\x97",   /* yi */
    /* 31 \ */ "\xD2\x91",   /* ghe with upturn (lowercase) */
    /* 32   */ NULL,
    /* 33 ; */ "\xD0\xB6",   /* zh */
    /* 34 ' */ "\xD1\x94",   /* ye (Ukrainian) */
    /* 35 ` */ "\xD2\x91",   /* ghe with upturn (lowercase) */
    /* 36 , */ ",",
    /* 37 . */ ".",
    /* 38 / */ "/",
};

/* Uppercase Ukrainian */
static const char *UA_SHIFT[] = {
    /* 04 A */ "\xD0\xA4",   /* F */
    /* 05 B */ "\xD0\x98",   /* I (short) */
    /* 06 C */ "\xD0\xA1",   /* S */
    /* 07 D */ "\xD0\x92",   /* V */
    /* 08 E */ "\xD0\xA3",   /* U */
    /* 09 F */ "\xD0\x90",   /* A */
    /* 0A G */ "\xD0\x9F",   /* P */
    /* 0B H */ "\xD0\xA0",   /* R */
    /* 0C I */ "\xD0\xA8",   /* Sh */
    /* 0D J */ "\xD0\x9E",   /* O */
    /* 0E K */ "\xD0\x9B",   /* L */
    /* 0F L */ "\xD0\x94",   /* D */
    /* 10 M */ "\xD0\xAC",   /* soft sign (upper) */
    /* 11 N */ "\xD0\xA2",   /* T */
    /* 12 O */ "\xD0\xA9",   /* Shch */
    /* 13 P */ "\xD0\x97",   /* Z */
    /* 14 Q */ "\xD0\x99",   /* J */
    /* 15 R */ "\xD0\x9A",   /* K */
    /* 16 S */ "\xD0\x86",   /* I (Ukrainian, upper) */
    /* 17 T */ "\xD0\x95",   /* E */
    /* 18 U */ "\xD0\x93",   /* G */
    /* 19 V */ "\xD0\x9C",   /* M */
    /* 1A W */ "\xD0\xA6",   /* Ts */
    /* 1B X */ "\xD0\xA7",   /* Ch */
    /* 1C Y */ "\xD0\x9D",   /* N */
    /* 1D Z */ "\xD0\xAF",   /* Ya */
    /* 1E-27 */ "!","\"","#","$","%","^","&","*","(",")",
    /* 28-2C */ NULL, NULL, NULL, NULL, " ",
    /* 2D */ "_",
    /* 2E */ "+",
    /* 2F */ "\xD0\xA5",     /* Kh */
    /* 30 */ "\xD0\x87",     /* Yi (upper) */
    /* 31 */ "\xD2\x90",     /* Ghe with upturn (upper) */
    /* 32 */ NULL,
    /* 33 */ "\xD0\x96",     /* Zh */
    /* 34 */ "\xD0\x84",     /* Ye (upper, Ukrainian) */
    /* 35 */ "\xD2\x90",     /* Ghe with upturn (upper) */
    /* 36 */ "<",
    /* 37 */ ">",
    /* 38 */ "?",
};
#endif /* CONFIG_KB_LAYOUT_ENABLE_UA */

/* ------------------------------------------------------------------ */
/* Layout table structure                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    const char **normal;
    const char **shift;
    const char **altgr;  /* NULL if layout has no AltGr layer */
    int table_size;      /* number of entries (keycodes 0x04..0x04+size-1) */
} layout_table_t;

#define ARRAY_SIZE(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const layout_table_t s_layouts[KB_LAYOUT_COUNT] = {
#ifdef CONFIG_KB_LAYOUT_ENABLE_US
    [KB_LAYOUT_US] = { US_NORMAL, US_SHIFT, NULL,      ARRAY_SIZE(US_NORMAL) },
#endif
#ifdef CONFIG_KB_LAYOUT_ENABLE_UA
    [KB_LAYOUT_UA] = { UA_NORMAL, UA_SHIFT, NULL,      ARRAY_SIZE(UA_NORMAL) },
#endif
#ifdef CONFIG_KB_LAYOUT_ENABLE_DE
    [KB_LAYOUT_DE] = { DE_NORMAL, DE_SHIFT, DE_ALTGR,  ARRAY_SIZE(DE_NORMAL) },
#endif
#ifdef CONFIG_KB_LAYOUT_ENABLE_FR
    [KB_LAYOUT_FR] = { FR_NORMAL, FR_SHIFT, FR_ALTGR,  ARRAY_SIZE(FR_NORMAL) },
#endif
#ifdef CONFIG_KB_LAYOUT_ENABLE_KO
    [KB_LAYOUT_KO] = { KO_NORMAL, KO_SHIFT, NULL,      ARRAY_SIZE(KO_NORMAL) },
#endif
#ifdef CONFIG_KB_LAYOUT_ENABLE_JA
    [KB_LAYOUT_JA] = { JA_NORMAL, JA_SHIFT, NULL,      ARRAY_SIZE(JA_NORMAL) },
#endif
#ifdef CONFIG_KB_LAYOUT_ENABLE_ZH
    [KB_LAYOUT_ZH] = { ZH_NORMAL, ZH_SHIFT, NULL,      ARRAY_SIZE(ZH_NORMAL) },
#endif
};

static const char *s_layout_names[KB_LAYOUT_COUNT] = {
#ifdef CONFIG_KB_LAYOUT_ENABLE_US
    [KB_LAYOUT_US] = "US",
#endif
#ifdef CONFIG_KB_LAYOUT_ENABLE_UA
    [KB_LAYOUT_UA] = "UA",
#endif
#ifdef CONFIG_KB_LAYOUT_ENABLE_DE
    [KB_LAYOUT_DE] = "DE",
#endif
#ifdef CONFIG_KB_LAYOUT_ENABLE_FR
    [KB_LAYOUT_FR] = "FR",
#endif
#ifdef CONFIG_KB_LAYOUT_ENABLE_KO
    [KB_LAYOUT_KO] = "KO",
#endif
#ifdef CONFIG_KB_LAYOUT_ENABLE_JA
    [KB_LAYOUT_JA] = "JA",
#endif
#ifdef CONFIG_KB_LAYOUT_ENABLE_ZH
    [KB_LAYOUT_ZH] = "ZH",
#endif
};

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

extern "C" void kb_layout_set(kb_layout_id_t layout)
{
    if (layout >= KB_LAYOUT_COUNT) layout = (kb_layout_id_t)0;
    s_layout = layout;
}

extern "C" kb_layout_id_t kb_layout_get(void)
{
    return s_layout;
}

extern "C" const char *kb_layout_name(kb_layout_id_t layout)
{
    if (layout >= KB_LAYOUT_COUNT) return "??";
    return s_layout_names[layout];
}

extern "C" kb_layout_id_t kb_layout_next(void)
{
    s_layout = (kb_layout_id_t)((s_layout + 1) % KB_LAYOUT_COUNT);
    return s_layout;
}

extern "C" const char *kb_layout_translate(uint8_t keycode, uint8_t modifier)
{
    if (keycode < 0x04) return NULL;

    const layout_table_t *lt = &s_layouts[s_layout];
    int idx = keycode - 0x04;
    if (idx >= lt->table_size) return NULL;

    bool shift = (modifier & (MOD_LSHIFT | MOD_RSHIFT)) != 0;
    bool altgr = (modifier & MOD_RALT) != 0;

    const char *result = NULL;

    if (altgr && lt->altgr && idx < lt->table_size) {
        result = lt->altgr[idx];
    }
    if (!result) {
        result = shift ? lt->shift[idx] : lt->normal[idx];
    }

    return result;
}
