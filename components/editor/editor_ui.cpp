#include <cstdio>
#include <cstring>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "sdkconfig.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "editor_ui.h"
#include "editor.h"
#include "md_parser.h"
#include "ble_keyboard.h"
#include "kb_layout.h"
#include "wifi_manager.h"
#include "git_sync.h"
#include "sd_card.h"
#include "lvgl_port.h"
#include "display.h"
#include "standby.h"
#include "greybeard.h"
#include "battery.h"
#include "freertos/task.h"

/*
 * Font aliases.
 *
 * Greybeard is a monospaced bitmap font that includes Latin,
 * Latin-1 Supplement, and Cyrillic glyphs in every size, so a
 * single set of fonts covers all enabled keyboard layouts.
 *
 * The body font is selected at runtime via the base font size
 * user setting (11, 14, or 16 px).  Heading fonts are scaled
 * relative to the body size, using the 26 px font for the
 * largest headings.  Status bars always use FONT_11 regardless
 * of the body font setting.
 */
#define FONT_11 (&greybeard_11)
#define FONT_14 (&greybeard_14)
#define FONT_16 (&greybeard_16)
#define FONT_18 (&greybeard_18)
#define FONT_22 (&greybeard_22)
#define FONT_26 (&greybeard_26)

static const char *TAG = "EditorUI";

/* Layout constants -- account for display rotation.
 * At 90 or 270 degrees, the logical width and height are swapped. */
#if CONFIG_DRAFTLING_DISPLAY_ROTATE_ANGLE == 90 || CONFIG_DRAFTLING_DISPLAY_ROTATE_ANGLE == 270
#define SCR_W        CONFIG_DRAFTLING_DISPLAY_HEIGHT
#define SCR_H        CONFIG_DRAFTLING_DISPLAY_WIDTH
#else
#define SCR_W        CONFIG_DRAFTLING_DISPLAY_WIDTH
#define SCR_H        CONFIG_DRAFTLING_DISPLAY_HEIGHT
#endif
#define HEADER_H     16
#define STATUS_H     16
#define EDITOR_Y     HEADER_H
#define EDITOR_H     (SCR_H - HEADER_H - STATUS_H)
#define LIST_PANEL_H (SCR_H - 18)  /* height for list panels below header */

/* ---- Base font size setting ----
 * The user can pick 11, 14, or 16 px as the editor body font.
 * Heading fonts are scaled up from the body size. */
#define FONT_SIZE_COUNT 3
static const int FONT_SIZE_OPTIONS[FONT_SIZE_COUNT] = { 11, 14, 16 };
static const char *FONT_SIZE_LABELS[FONT_SIZE_COUNT] = { "11 px", "14 px", "16 px" };

/* NVS namespace/key for font size */
#define NVS_NS_EDITOR   "editor"
#define NVS_KEY_FONTSZ  "fontsz"

/* Current body font size in pixels (default 11) */
static int s_font_size = 11;

/* Derived layout values -- recomputed when font size changes */
static int s_line_h       = 11;
static int s_visible_lines = 0;   /* computed after s_line_h is set */
static int s_char_w       = 6;

/* Accessor macros that used to be compile-time constants */
#define LINE_H        s_line_h
#define VISIBLE_LINES s_visible_lines
#define CHAR_W        s_char_w

/* Forward declaration (defined below) */
static int char_width_for_font(const lv_font_t *font);

/* Return the body font for the current size setting. */
static const lv_font_t *body_font(void)
{
    if (s_font_size == 16) return FONT_16;
    if (s_font_size == 14) return FONT_14;
    return FONT_11;
}

/* Return heading fonts (h1/h2/h3) scaled relative to the body size.
 *   body 11 -> h3 14, h2 16, h1 18
 *   body 14 -> h3 16, h2 18, h1 22
 *   body 16 -> h3 18, h2 22, h1 26
 */
static const lv_font_t *h1_font(void)
{
    if (s_font_size == 16) return FONT_26;
    if (s_font_size == 14) return FONT_22;
    return FONT_18;
}

static const lv_font_t *h2_font(void)
{
    if (s_font_size == 16) return FONT_22;
    if (s_font_size == 14) return FONT_18;
    return FONT_16;
}

static const lv_font_t *h3_font(void)
{
    if (s_font_size == 16) return FONT_18;
    if (s_font_size == 14) return FONT_16;
    return FONT_14;
}

/* Recalculate derived layout values from the current body font. */
static void recalc_layout(void)
{
    const lv_font_t *bf = body_font();
    s_line_h = lv_font_get_line_height(bf);
    s_char_w = char_width_for_font(bf);
    s_visible_lines = EDITOR_H / s_line_h;
}

/* Return the index into FONT_SIZE_OPTIONS for the given pixel size.
 * Falls back to 0 (11 px) when the size is not recognized. */
static int find_font_size_option(int sz)
{
    for (int i = 0; i < FONT_SIZE_COUNT; i++) {
        if (FONT_SIZE_OPTIONS[i] == sz) return i;
    }
    return 0;
}

static void load_font_size_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_EDITOR, NVS_READONLY, &h) == ESP_OK) {
        uint8_t val = 0;
        if (nvs_get_u8(h, NVS_KEY_FONTSZ, &val) == ESP_OK) {
            if (val == 11 || val == 14 || val == 16)
                s_font_size = val;
        }
        nvs_close(h);
    }
}

static void save_font_size_to_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_EDITOR, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_KEY_FONTSZ, (uint8_t)s_font_size);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* LVGL objects */
static lv_obj_t *s_scr       = NULL;
static lv_obj_t *s_lbl_title = NULL;
static lv_obj_t *s_cont_edit = NULL;
static lv_obj_t *s_lbl_status= NULL;
static lv_obj_t *s_cursor    = NULL;
static lv_obj_t *s_scr_browser = NULL;
static lv_obj_t *s_list_files  = NULL;
static lv_obj_t *s_lbl_br_status = NULL;
static lv_obj_t *s_img_logo    = NULL;

/* BLE connection prompt screen */
static lv_obj_t *s_scr_ble_prompt  = NULL;
static lv_obj_t *s_ble_prompt_lbl  = NULL;

/* Menu overlay objects */
static lv_obj_t *s_scr_menu     = NULL;
static lv_obj_t *s_menu_list    = NULL;
static lv_obj_t *s_lbl_menu_hdr = NULL;
static int       s_menu_sel     = 0;
/* Previously highlighted menu item, so update_list_highlight() can
 * clear just the old selection on Up/Down navigation instead of
 * re-styling every item (which would invalidate the entire menu list
 * and trigger a full e-paper refresh on the PaperS3 backend). */
static int       s_menu_sel_prev = -1;
static bool      s_menu_open    = false;

/* Number of menu items */
#define MENU_ITEM_COUNT 8

#if !(defined(CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001) || \
      defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_EPD_HAT) || \
      defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3))
static lv_timer_t *s_blink_timer = NULL;
#endif
static bool s_cursor_visible = true;
static int  s_browser_sel    = 0;
/* See s_menu_sel_prev. */
static int  s_browser_sel_prev = -1;
static int  s_browser_count  = 0;
static sd_card_file_entry_t s_browser_entries[64];

/* Settings screen objects */
static lv_obj_t *s_scr_settings  = NULL;
static lv_obj_t *s_settings_list = NULL;
static int       s_settings_sel  = 0;
/* See s_menu_sel_prev above. */
static int       s_settings_sel_prev = -1;
static bool      s_settings_open = false;
static bool      s_factory_reset_confirm = false; /* awaiting second Enter */

/* Passkey overlay objects */
static lv_obj_t *s_passkey_panel = NULL;
static lv_obj_t *s_passkey_label = NULL;

/* Save-prompt overlay objects */
static lv_obj_t *s_save_panel    = NULL;
static lv_obj_t *s_save_hdr_lbl  = NULL;
static lv_obj_t *s_save_name_lbl = NULL;
static lv_obj_t *s_save_cur      = NULL;   /* blinking cursor in name field */
static bool      s_save_open     = false;
static char      s_save_buf[128] = "";     /* editable filename (no directory) */
static int       s_save_pos      = 0;      /* cursor position in s_save_buf (byte) */

/* Escape-save-prompt: when true the user has been warned about unsaved
 * changes and a second Esc will discard + close. */
static bool s_esc_pending = false;

/* ---- Device battery display (Waveshare RLCD42) ---- */
#if defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_RLCD42) || defined(CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001)
static lv_obj_t  *s_lbl_dev_batt    = NULL;  /* editor screen */
static lv_obj_t  *s_lbl_br_dev_batt = NULL;  /* file browser screen */
static lv_timer_t *s_batt_timer     = NULL;
#define BATT_POLL_MS 30000  /* refresh battery every 30 s */
#endif

/* ---- Key-event input queue ----
 * The BLE callback runs on the HID host task and must not block on the
 * LVGL mutex.  Key events are enqueued here from the BLE context (ISR-
 * safe) and drained by an LVGL timer running on the GUI task. */
#define KEY_QUEUE_LEN       CONFIG_DRAFTLING_KEY_QUEUE_LEN
#define KEY_DRAIN_PERIOD_MS CONFIG_DRAFTLING_KEY_DRAIN_PERIOD_MS
static QueueHandle_t s_key_queue = NULL;
static lv_timer_t   *s_key_drain_timer = NULL;

/* Maximum number of auto-generated draft filenames (draft_001..draft_999) */
#define MAX_DRAFT_SEQ 999

/* ---- Key repeat ----
 * When a key is held down the BLE HID report keeps sending the same
 * keycodes, but some keyboards only send a single report.  We implement
 * software key repeat: after an initial delay (KEY_REPEAT_DELAY_MS) we
 * start re-injecting the last key-down event at KEY_REPEAT_RATE_MS
 * intervals.  The repeat is cancelled on key-up. */
#define KEY_REPEAT_DELAY_MS   500
#define KEY_REPEAT_RATE_MS    50
static kb_event_t s_repeat_ev    = {};      /* last key-down event */
static bool       s_repeat_held  = false;   /* is a key currently held? */
static uint32_t   s_repeat_start = 0;       /* tick when key was pressed */
static bool       s_repeat_firing = false;  /* past initial delay? */
static lv_timer_t *s_repeat_timer = NULL;

/* Standby timeout options in seconds: 0=Off, 300=5min, 600=10min, etc. */
static const uint32_t TIMEOUT_OPTIONS[] = { 0, 300, 600, 900, 1800, 3600 };
static const char *TIMEOUT_LABELS[]     = { "Off", "5 min", "10 min",
                                            "15 min", "30 min", "60 min" };
#define TIMEOUT_OPTION_COUNT  6

/* Line label pool */
#define MAX_LINE_LABELS 24
static lv_obj_t *s_line_labels[MAX_LINE_LABELS] = {NULL};

/* Selection highlight rectangle pool (one per visible line) */
static lv_obj_t *s_sel_rects[MAX_LINE_LABELS] = {NULL};

/* Per-slot render cache.  The editor body is by far the largest part
 * of the screen, and on every keystroke editor_ui_refresh() is called
 * which previously rewrote the text and style of every visible line.
 * Each lv_label_set_text / lv_obj_remove_style_all / lv_obj_set_pos
 * invalidates the corresponding LVGL area, so the union of dirty
 * rectangles ended up covering nearly the whole editor area.  On the
 * M5Stack PaperS3 backend that crosses the >75% "huge area" threshold
 * in display_eds3.cpp and triggers a slow full-screen e-paper refresh
 * on every keystroke (and on every menu navigation step, which used a
 * similar full-rebuild pattern).
 *
 * The cache below records what we last drew into each slot so we can
 * skip the LVGL mutations whenever the visible content is unchanged.
 * The fast-path covers the common case of typing or moving the cursor
 * with no active selection: only the slot whose text actually changed
 * (and the cursor bar / title bar) gets invalidated, so M5GFX can run
 * a fast partial-region waveform instead of a full refresh. */
static char s_prev_line_text[MAX_LINE_LABELS][256];
static int  s_prev_line_type[MAX_LINE_LABELS];     /* md_line_type_t, -1 if cache empty */
static int  s_prev_line_y[MAX_LINE_LABELS];        /* y_pos last used for this slot, -1 if cache empty */
static int  s_prev_line_h[MAX_LINE_LABELS];        /* rendered_h last computed */
static bool s_prev_line_visible[MAX_LINE_LABELS];  /* slot was visible (not hidden) */
static bool s_prev_line_was_selected[MAX_LINE_LABELS]; /* line intersected the selection */

static void invalidate_render_cache(void)
{
    for (int i = 0; i < MAX_LINE_LABELS; i++) {
        s_prev_line_text[i][0]      = '\0';
        s_prev_line_type[i]         = -1;
        s_prev_line_y[i]            = -1;
        s_prev_line_h[i]            = 0;
        s_prev_line_visible[i]      = false;
        s_prev_line_was_selected[i] = false;
    }
}

/* Styles */
static lv_style_t s_style_body;
static lv_style_t s_style_h1;
static lv_style_t s_style_h2;
static lv_style_t s_style_h3;
static lv_style_t s_style_code;
static lv_style_t s_style_quote;

static void init_styles(void)
{
    lv_style_init(&s_style_body);
    lv_style_set_text_font(&s_style_body, body_font());
    lv_style_set_text_color(&s_style_body, lv_color_black());
    lv_style_set_pad_all(&s_style_body, 0);

    lv_style_init(&s_style_h1);
    lv_style_set_text_font(&s_style_h1, h1_font());
    lv_style_set_text_color(&s_style_h1, lv_color_black());

    lv_style_init(&s_style_h2);
    lv_style_set_text_font(&s_style_h2, h2_font());
    lv_style_set_text_color(&s_style_h2, lv_color_black());

    lv_style_init(&s_style_h3);
    lv_style_set_text_font(&s_style_h3, h3_font());
    lv_style_set_text_color(&s_style_h3, lv_color_black());

    lv_style_init(&s_style_code);
    lv_style_set_text_font(&s_style_code, body_font());
    lv_style_set_text_color(&s_style_code, lv_color_black());
    lv_style_set_border_width(&s_style_code, 1);
    lv_style_set_border_color(&s_style_code, lv_color_black());
    lv_style_set_pad_left(&s_style_code, 4);

    lv_style_init(&s_style_quote);
    lv_style_set_text_font(&s_style_quote, body_font());
    lv_style_set_text_color(&s_style_quote, lv_color_black());
    lv_style_set_border_side(&s_style_quote, LV_BORDER_SIDE_LEFT);
    lv_style_set_border_width(&s_style_quote, 2);
    lv_style_set_border_color(&s_style_quote, lv_color_black());
    lv_style_set_pad_left(&s_style_quote, 8);

    recalc_layout();

    /* Styles changed -> all cached label content is now stale. */
    invalidate_render_cache();
}

#if !(defined(CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001) || \
      defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_EPD_HAT) || \
      defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3))
static void cursor_blink_cb(lv_timer_t *timer)
{
    (void)timer;
    s_cursor_visible = !s_cursor_visible;
    if (s_cursor) {
        if (s_cursor_visible) lv_obj_remove_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
    }
}
#endif

#if defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_RLCD42) || defined(CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001)
/* Build a battery level string for the status bar. */
static void format_batt_str(char *buf, size_t len)
{
    int pct = battery_read_percent();
    if (pct < 0) {
        snprintf(buf, len, "----");
        return;
    }
    snprintf(buf, len, "%d%%", pct);
}

static void batt_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    char batt[20];
    format_batt_str(batt, sizeof(batt));
    if (s_lbl_dev_batt)    lv_label_set_text(s_lbl_dev_batt, batt);
    if (s_lbl_br_dev_batt) lv_label_set_text(s_lbl_br_dev_batt, batt);
}
#endif

static void update_title_bar(void)
{
    const char *path = editor_get_file_path();
    const char *name = "Untitled";
    if (path) {
        const char *slash = strrchr(path, '/');
        name = slash ? slash + 1 : path;
    }
    char batt_str[24] = "";
    int batt = ble_keyboard_get_battery_level();
    if (batt >= 0) {
        snprintf(batt_str, sizeof(batt_str), " Bat:%d%%", batt);
    }
    char buf[128];
#if defined(CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001) || \
    defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_EPD_HAT) || \
    defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)
    /* On e-paper boards the cursor moves on every keystroke, so
     * including the line/column counter in the title bar would dirty
     * the top of the screen on every edit and double the e-paper
     * refresh time. Skip it. */
    snprintf(buf, sizeof(buf), "%s%s  [%s]%s",
             name, editor_is_modified() ? " *" : "",
             kb_layout_name(kb_layout_get()), batt_str);
#else
    int line, col;
    editor_get_cursor_pos(&line, &col);
    snprintf(buf, sizeof(buf), "%s%s  L:%d C:%d  [%s]%s",
             name, editor_is_modified() ? " *" : "",
             line + 1, col + 1,
             kb_layout_name(kb_layout_get()), batt_str);
#endif

    /* lv_label_set_text() in LVGL v9 unconditionally invalidates the
     * label even when the new text is identical to the current one,
     * which on e-paper backends dirties the entire title-bar strip
     * on every keystroke.  Combined with the dirty region from the
     * edited line that spans the top and bottom of the screen and
     * triggers a full-screen refresh in display_eds3.cpp's ">75%"
     * huge-area path.  Compare against the last text we pushed and
     * skip the call when unchanged. */
    static char s_prev_title[128] = { 0 };
    if (strcmp(s_prev_title, buf) != 0) {
        lv_label_set_text(s_lbl_title, buf);
        /* snprintf truncates to at most sizeof(buf)-1 chars, which
         * also fits in s_prev_title[128]. */
        strncpy(s_prev_title, buf, sizeof(s_prev_title) - 1);
        s_prev_title[sizeof(s_prev_title) - 1] = '\0';
    }
}

static lv_style_t *style_for_type(md_line_type_t type)
{
    switch (type) {
    case MD_LINE_H1:           return &s_style_h1;
    case MD_LINE_H2:           return &s_style_h2;
    case MD_LINE_H3:
    case MD_LINE_H4:           return &s_style_h3;
    case MD_LINE_CODE_FENCE:
    case MD_LINE_CODE_CONTENT: return &s_style_code;
    case MD_LINE_BLOCKQUOTE:   return &s_style_quote;
    default:                   return &s_style_body;
    }
}

/* Return the monospace cell width (in pixels) for a given Greybeard
 * font size.  The values are the advance widths stored in the generated
 * font data, divided by 16 (LVGL stores advances in 1/16-px units). */
static int char_width_for_font(const lv_font_t *font)
{
    if (font == FONT_26) return 13;   /* adv_w 208 / 16 = 13 */
    if (font == FONT_22) return 11;   /* adv_w 176 / 16 = 11 */
    if (font == FONT_18) return 9;    /* adv_w 144 / 16 = 9 */
    if (font == FONT_16) return 8;    /* adv_w 128 / 16 = 8 */
    if (font == FONT_14) return 7;    /* adv_w 112 / 16 = 7 */
    return 6;                         /* FONT_11: adv_w  96 / 16 = 6 */
}

/* Count UTF-8 characters in the first byte_len bytes of text. */
static int utf8_chars_in_bytes(const char *text, size_t byte_len)
{
    int count = 0;
    for (size_t i = 0; i < byte_len; i++) {
        if ((text[i] & 0xC0) != 0x80) count++;
    }
    return count;
}

/* Return the byte offset of the n-th UTF-8 character in text. */
static size_t utf8_char_offset(const char *text, int n)
{
    size_t off = 0;
    int ch = 0;
    while (text[off] && ch < n) {
        unsigned char c = (unsigned char)text[off];
        if (c < 0x80) off += 1;
        else if ((c & 0xE0) == 0xC0) off += 2;
        else if ((c & 0xF0) == 0xE0) off += 3;
        else off += 4;
        ch++;
    }
    return off;
}

extern "C" void editor_ui_refresh(void)
{
    if (editor_get_mode() != EDITOR_MODE_EDITING) return;

    /* Show logo when no file is loaded and buffer is empty */
    size_t text_len = 0;
    const char *flat_text = editor_get_text(&text_len);
    bool show_logo = (editor_get_file_path() == NULL && text_len == 0);
    if (s_img_logo) {
        if (show_logo) {
            lv_obj_remove_flag(s_img_logo, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_img_logo, LV_OBJ_FLAG_HIDDEN);
        }
    }

    int total  = editor_get_line_count();

    /* Selection range (byte offsets in flat text) */
    size_t sel_start = 0, sel_end = 0;
    bool has_sel = editor_selection_active();
    if (has_sel) editor_get_selection_range(&sel_start, &sel_end);

    int cur_line, cur_col;
    editor_get_cursor_pos(&cur_line, &cur_col);

    int cur_y = -1;      /* cursor y position (set when cursor line is rendered) */
    int cur_x = -1;      /* cursor x position */
    int cur_h = LINE_H;  /* cursor height (matches font of cursor line) */

    /* Render visible lines.  Wrapped lines consume more vertical space
     * than a single LINE_H row, so the cursor may be pushed off-screen
     * even though ensure_cursor_visible() thought it was in range.
     * When that happens, increment scroll and re-render (bounded). */
    for (int _scroll_retry = 0; _scroll_retry < MAX_LINE_LABELS;
         _scroll_retry++) {
        int scroll = editor_get_scroll_line();
        bool in_code = false;

        /* Track code fence state up to scroll line */
        for (int i = 0; i < scroll && i < total; i++) {
            size_t ll;
            const char *lt = editor_get_line(i, &ll);
            if (md_is_code_fence(lt, ll)) in_code = !in_code;
        }

        char line_buf[256];
        int y_pos = 0;       /* running y position in editor content area */
        cur_y = -1;
        cur_x = -1;
        cur_h = LINE_H;

        for (int i = 0; i < MAX_LINE_LABELS; i++) {
            int line_idx = scroll + i;
            if (line_idx >= total || y_pos >= EDITOR_H) {
                /* Only invalidate the slot when its visible state
                 * actually changes; otherwise lv_obj_add_flag()
                 * dirties the previous label rectangle on every
                 * refresh and the union of all those rectangles
                 * fills most of the editor area, defeating the
                 * partial-update path on e-paper backends. */
                if (s_line_labels[i] && s_prev_line_visible[i]) {
                    lv_obj_add_flag(s_line_labels[i], LV_OBJ_FLAG_HIDDEN);
                }
                if (s_sel_rects[i] && s_prev_line_was_selected[i]) {
                    lv_obj_add_flag(s_sel_rects[i], LV_OBJ_FLAG_HIDDEN);
                }
                /* Always clear the cached state for hidden slots so a
                 * later transition back to visible is forced through
                 * the full re-render path with a clean baseline. */
                s_prev_line_visible[i]      = false;
                s_prev_line_was_selected[i] = false;
                continue;
            }

            size_t ll;
            const char *lt = editor_get_line(line_idx, &ll);

            md_line_info_t mi;
            md_parse_line(lt, ll, &mi, in_code);
            if (mi.type == MD_LINE_CODE_FENCE) in_code = !in_code;

            /* Prepare display text */
            const char *disp_text = mi.content;
            size_t disp_len = mi.content_len;
            if (mi.type == MD_LINE_BULLET) {
                int prefix = mi.indent_level * 2;
                int n = snprintf(line_buf, sizeof(line_buf), "%*s* ", prefix, "");
                if (disp_len > 0 && (size_t)n + disp_len < sizeof(line_buf) - 1) {
                    memcpy(line_buf + n, disp_text, disp_len);
                    line_buf[n + disp_len] = '\0';
                } else {
                    line_buf[n] = '\0';
                }
                disp_text = line_buf;
                disp_len = strlen(line_buf);
            } else if (mi.type == MD_LINE_HR) {
                memset(line_buf, '-', 40);
                line_buf[40] = '\0';
                disp_text = line_buf;
                disp_len = 40;
            } else if (mi.type == MD_LINE_EMPTY) {
                disp_text = " ";
                disp_len = 1;
            }

            /* Build the final display string up-front so we can compare
             * against the cached previous content before touching any
             * LVGL state. */
            char tmp[256];
            size_t clen = disp_len < sizeof(tmp) - 1 ? disp_len : sizeof(tmp) - 1;
            memcpy(tmp, disp_text, clen);
            tmp[clen] = '\0';

            /* Determine whether this line intersects the active
             * selection (used both for the highlight branches below
             * and to disable the fast-path cache when selection is
             * involved -- the inversion / overlay rendering needs
             * full re-evaluation). */
            bool line_intersects_sel = false;
            if (has_sel) {
                size_t loff = (size_t)(lt - flat_text);
                size_t leoff = loff + ll;
                if (sel_start < leoff && sel_end > loff) {
                    line_intersects_sel = true;
                } else if (leoff < text_len &&
                           flat_text[leoff] == '\n' &&
                           sel_start < leoff + 1 && sel_end > loff) {
                    line_intersects_sel = true;
                }
            }

            /* Fast path: when the visible text, markdown style, and y
             * position for this slot all match the previous refresh
             * AND no selection touches this line (now or last time),
             * we can leave the LVGL label untouched -- which means no
             * dirty rectangle for this slot and the e-paper backend
             * issues a tight partial refresh covering only the line
             * that actually changed. */
            bool can_skip = s_line_labels[i] != NULL &&
                            s_prev_line_visible[i] &&
                            !line_intersects_sel &&
                            !s_prev_line_was_selected[i] &&
                            s_prev_line_type[i] == (int)mi.type &&
                            s_prev_line_y[i] == y_pos &&
                            strcmp(s_prev_line_text[i], tmp) == 0;

            int line_h;
            int rendered_h;
            if (can_skip) {
                /* Reuse cached layout metrics; the label still holds
                 * the same text/font/width so its rendered geometry
                 * is unchanged. lv_label_get_letter_pos() (used for
                 * the cursor below) is non-mutating and works on the
                 * existing label state. */
                rendered_h = s_prev_line_h[i];
                const lv_font_t *line_font = lv_obj_get_style_text_font(
                                                s_line_labels[i], LV_PART_MAIN);
                line_h = lv_font_get_line_height(line_font ? line_font : body_font());
            } else {
                /* Create or reuse label */
                if (!s_line_labels[i]) {
                    s_line_labels[i] = lv_label_create(s_cont_edit);
                    lv_obj_set_width(s_line_labels[i], SCR_W - 4);
                    lv_label_set_long_mode(s_line_labels[i], LV_LABEL_LONG_WRAP);
                }
                lv_obj_remove_flag(s_line_labels[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_style_all(s_line_labels[i]);
                lv_obj_add_style(s_line_labels[i], style_for_type(mi.type), 0);
                /* Re-apply width after style reset (remove_style_all clears it)
                 * so that LV_LABEL_LONG_WRAP can wrap at the correct boundary. */
                lv_obj_set_width(s_line_labels[i], SCR_W - 4);
                lv_label_set_text_static(s_line_labels[i], "");
                lv_label_set_text(s_line_labels[i], tmp);
                lv_obj_set_pos(s_line_labels[i], 2, y_pos);

                /* Get the font for this line to compute correct line height */
                const lv_font_t *line_font = lv_obj_get_style_text_font(
                                                s_line_labels[i], LV_PART_MAIN);
                line_h = lv_font_get_line_height(line_font ? line_font : body_font());

                /* Determine actual rendered height (may be taller if text wraps) */
                lv_obj_update_layout(s_line_labels[i]);
                rendered_h = lv_obj_get_height(s_line_labels[i]);
                if (rendered_h < line_h) rendered_h = line_h;

                /* Update cache with what we just drew. */
                memcpy(s_prev_line_text[i], tmp, clen + 1);
                s_prev_line_type[i]    = (int)mi.type;
                s_prev_line_y[i]       = y_pos;
                s_prev_line_h[i]       = rendered_h;
                s_prev_line_visible[i] = true;
            }

            /* Selection highlight.  Fully-selected lines and multi-row
             * partial selections use color inversion on the label
             * itself (black bg, white text).  Single-row partial
             * selections use an overlay label (s_sel_rects) that
             * renders only the selected substring in white on black,
             * positioned exactly over those characters. */
            if (has_sel) {
                size_t line_off = (size_t)(lt - flat_text);
                size_t line_end_off = line_off + ll;
                bool fully = (sel_start <= line_off &&
                              sel_end >= line_end_off);
                bool partial = !fully &&
                    (sel_start < line_end_off && sel_end > line_off);
                /* Include trailing newline for overlap check */
                if (!fully && !partial && line_end_off < text_len &&
                    flat_text[line_end_off] == '\n') {
                    partial = (sel_start < line_end_off + 1 &&
                               sel_end > line_off);
                }
                if (fully) {
                    lv_obj_set_style_bg_color(s_line_labels[i],
                                              lv_color_black(), 0);
                    lv_obj_set_style_bg_opa(s_line_labels[i],
                                            LV_OPA_COVER, 0);
                    lv_obj_set_style_text_color(s_line_labels[i],
                                                lv_color_white(), 0);
                    if (s_sel_rects[i])
                        lv_obj_add_flag(s_sel_rects[i], LV_OBJ_FLAG_HIDDEN);
                } else if (partial && s_sel_rects[i]) {
                    /* Compute selection byte range within this line */
                    size_t raw_s = (sel_start > line_off)
                                       ? sel_start - line_off : 0;
                    size_t raw_e = (sel_end < line_end_off)
                                       ? sel_end - line_off : ll;
                    /* Convert raw byte offsets to display char indices.
                     * md_parse_line may strip a prefix (e.g. "# ") or
                     * a bullet formatter may prepend characters. */
                    size_t prefix_bytes = (size_t)(mi.content - lt);
                    int disp_s, disp_e;
                    if (raw_s <= prefix_bytes) disp_s = 0;
                    else disp_s = utf8_chars_in_bytes(
                                      mi.content, raw_s - prefix_bytes);
                    if (raw_e <= prefix_bytes) disp_e = 0;
                    else disp_e = utf8_chars_in_bytes(
                                      mi.content, raw_e - prefix_bytes);
                    if (mi.type == MD_LINE_BULLET) {
                        int bp = mi.indent_level * 2 + 2;
                        disp_s += bp;
                        disp_e += bp;
                    }
                    if (disp_e > disp_s) {
                        lv_point_t sp, ep;
                        lv_label_get_letter_pos(s_line_labels[i],
                                                (uint32_t)disp_s, &sp);
                        lv_label_get_letter_pos(s_line_labels[i],
                                                (uint32_t)disp_e, &ep);
                        if (sp.y == ep.y) {
                            /* Single visual row: overlay label with
                             * only the selected substring in white
                             * on black background. */
                            size_t byte_s = utf8_char_offset(tmp, disp_s);
                            size_t byte_e = utf8_char_offset(tmp, disp_e);
                            char sel_buf[256];
                            size_t sel_len = byte_e - byte_s;
                            if (sel_len >= sizeof(sel_buf))
                                sel_len = sizeof(sel_buf) - 1;
                            memcpy(sel_buf, tmp + byte_s, sel_len);
                            sel_buf[sel_len] = '\0';

                            const lv_font_t *sf =
                                lv_obj_get_style_text_font(
                                    s_line_labels[i], LV_PART_MAIN);
                            lv_obj_set_style_text_font(
                                s_sel_rects[i],
                                sf ? sf : body_font(), 0);
                            lv_label_set_text(s_sel_rects[i], sel_buf);
                            lv_obj_set_pos(s_sel_rects[i],
                                           2 + sp.x, y_pos + sp.y);
                            lv_obj_move_foreground(s_sel_rects[i]);
                            lv_obj_remove_flag(s_sel_rects[i],
                                               LV_OBJ_FLAG_HIDDEN);
                        } else {
                            /* Multi-row partial: fall back to
                             * full-line inversion on the label. */
                            lv_obj_set_style_bg_color(s_line_labels[i],
                                                      lv_color_black(), 0);
                            lv_obj_set_style_bg_opa(s_line_labels[i],
                                                    LV_OPA_COVER, 0);
                            lv_obj_set_style_text_color(s_line_labels[i],
                                                        lv_color_white(), 0);
                            lv_obj_add_flag(s_sel_rects[i],
                                            LV_OBJ_FLAG_HIDDEN);
                        }
                    } else {
                        lv_obj_add_flag(s_sel_rects[i],
                                        LV_OBJ_FLAG_HIDDEN);
                    }
                } else {
                    if (s_sel_rects[i])
                        lv_obj_add_flag(s_sel_rects[i], LV_OBJ_FLAG_HIDDEN);
                }
            } else {
                if (s_sel_rects[i])
                    lv_obj_add_flag(s_sel_rects[i], LV_OBJ_FLAG_HIDDEN);
            }

            /* Remember whether this slot is currently rendering a
             * selected line so the next refresh can decide whether
             * to invoke the full re-render path that clears the
             * inversion / overlay. */
            s_prev_line_was_selected[i] = line_intersects_sel;

            /* If this is the cursor line, compute its pixel position.
             * For headings, md_parse_line strips the "# " prefix from content,
             * so the cursor column (which counts from the raw line start) needs
             * to be adjusted. */
            if (line_idx == cur_line) {
                int col_in_display = cur_col;
                /* content pointer offset from raw line start, in UTF-8 chars */
                if (mi.content > lt) {
                    int prefix_chars = 0;
                    for (const char *pp = lt; pp < mi.content; pp++) {
                        if ((*pp & 0xC0) != 0x80) prefix_chars++;
                    }
                    col_in_display -= prefix_chars;
                    if (col_in_display < 0) col_in_display = 0;
                }

                /* Use LVGL to find the actual pixel position of the cursor
                 * character.  This correctly handles word-level wrapping
                 * where the break point differs from a simple chars_per_row
                 * calculation. */
                lv_point_t lpos;
                lv_label_get_letter_pos(s_line_labels[i],
                                        (uint32_t)col_in_display, &lpos);
                cur_x = 2 + lpos.x;
                cur_y = y_pos + lpos.y;
                cur_h = line_h;
            }

            y_pos += rendered_h;
        }

        /* If the cursor line is in the expected range but wrapped lines
         * pushed it off-screen, increment scroll and re-render.
         * Use cur_y + cur_h to ensure the full cursor row is visible. */
        if (cur_line >= scroll && scroll < cur_line &&
            (cur_y < 0 || cur_y + cur_h > EDITOR_H)) {
            editor_set_scroll_line(scroll + 1);
            continue;
        }
        break;
    }

    /* Update cursor position */
    if (cur_y >= 0 && cur_y + cur_h <= EDITOR_H && s_cursor) {
        lv_obj_set_size(s_cursor, 2, cur_h);
        lv_obj_set_pos(s_cursor, cur_x, cur_y);
        lv_obj_remove_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
        s_cursor_visible = true;
    } else if (s_cursor) {
        lv_obj_add_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
    }

    update_title_bar();
}

static void ensure_cursor_visible(void)
{
    int cur_line, cur_col;
    (void)cur_col;
    editor_get_cursor_pos(&cur_line, &cur_col);
    int scroll = editor_get_scroll_line();
    if (cur_line < scroll) {
        editor_set_scroll_line(cur_line);
    } else if (cur_line >= scroll + VISIBLE_LINES) {
        editor_set_scroll_line(cur_line - VISIBLE_LINES + 1);
    }
}

/* ---- File browser ---- */

static void refresh_file_list(void)
{
    const char *mp = sd_card_get_mount_point();
    s_browser_count = sd_card_list_dir(mp, s_browser_entries, 64);
    if (s_browser_count < 0) s_browser_count = 0;

    /* Filter to show only .md files and directories */
    lv_obj_clean(s_list_files);

    for (int i = 0; i < s_browser_count; i++) {
        const char *name = s_browser_entries[i].name;
        bool show = s_browser_entries[i].is_dir;
        if (!show) {
            size_t nlen = strlen(name);
            show = (nlen > 3 && strcmp(name + nlen - 3, ".md") == 0);
        }
        if (show) {
            char label[sizeof(s_browser_entries[0].name) + 8];
            if (s_browser_entries[i].is_dir)
                snprintf(label, sizeof(label), "[DIR] %.255s", name);
            else
                snprintf(label, sizeof(label), "  %.255s", name);
            lv_obj_t *btn = lv_list_add_btn(s_list_files, NULL, label);
            lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        }
    }
    s_browser_sel = 0;
    /* List was rebuilt -- no item carries a highlight yet, so the
     * next update_list_highlight() call should not try to "clear"
     * a stale previous selection. */
    s_browser_sel_prev = -1;
}

extern "C" void editor_ui_show_file_browser(void)
{
    editor_close_file();
    refresh_file_list();

    /* Show wifi status in the browser status bar */
    if (wifi_manager_is_connected()) {
        char buf[80];
        snprintf(buf, sizeof(buf), "WiFi: %s (%s)",
                 wifi_manager_get_ssid(), wifi_manager_get_ip());
        if (s_lbl_br_status) lv_label_set_text(s_lbl_br_status, buf);
    } else {
        if (s_lbl_br_status) lv_label_set_text(s_lbl_br_status,
                                                "F1:Menu  N:New file");
    }

    lv_scr_load(s_scr_browser);
}

extern "C" void editor_ui_show_editor(void)
{
    editor_set_mode(EDITOR_MODE_EDITING);
    lv_scr_load(s_scr);
    editor_ui_refresh();
}

extern "C" void editor_ui_set_status(const char *msg)
{
    ESP_LOGI(TAG, "Status: %s", msg);
    if (s_lbl_status) lv_label_set_text(s_lbl_status, msg);
    if (s_lbl_br_status) lv_label_set_text(s_lbl_br_status, msg);
}

/* ---- Menu system ---- */

/* Apply the "selected" styling to item `sel` and the "unselected"
 * styling to every other item.  Used after a full list rebuild. */
static void apply_list_selection_styles(lv_obj_t *list, int sel)
{
    uint32_t count = lv_obj_get_child_count(list);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *child = lv_obj_get_child(list, i);
        if ((int)i == sel) {
            lv_obj_set_style_bg_color(child, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(child, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(child, lv_color_white(), 0);
        } else {
            lv_obj_set_style_bg_opa(child, LV_OPA_TRANSP, 0);
            lv_obj_set_style_text_color(child, lv_color_black(), 0);
        }
    }
}

/* Move the highlight from `prev_sel` to `sel` by restyling only those
 * two items.  This keeps the LVGL dirty rectangle limited to a couple
 * of menu rows instead of the whole list, so e-paper backends can
 * issue a partial refresh of just the affected area on Up/Down
 * navigation. */
static void update_list_highlight(lv_obj_t *list, int sel, int prev_sel)
{
    if (prev_sel == sel) return;
    uint32_t count = lv_obj_get_child_count(list);
    if (prev_sel >= 0 && (uint32_t)prev_sel < count) {
        lv_obj_t *prev = lv_obj_get_child(list, prev_sel);
        lv_obj_set_style_bg_opa(prev, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(prev, lv_color_black(), 0);
    }
    if (sel >= 0 && (uint32_t)sel < count) {
        lv_obj_t *cur = lv_obj_get_child(list, sel);
        lv_obj_set_style_bg_color(cur, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(cur, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(cur, lv_color_white(), 0);
    }
}

static void refresh_menu_items(void)
{
    lv_obj_clean(s_menu_list);

    char buf[80];

    /* 0: BLE status */
    snprintf(buf, sizeof(buf), "BLE: %s",
             ble_keyboard_is_connected()
                 ? ble_keyboard_get_device_name()
                 : "not connected");
    lv_list_add_btn(s_menu_list, NULL, buf);

    /* 1: BLE scan */
    lv_list_add_btn(s_menu_list, NULL, "BLE: Start scan");

    /* 2: WiFi status / connect */
    if (wifi_manager_is_connected()) {
        snprintf(buf, sizeof(buf), "WiFi: %s (%s)",
                 wifi_manager_get_ssid(), wifi_manager_get_ip());
    } else {
        snprintf(buf, sizeof(buf), "WiFi: Connect");
    }
    lv_list_add_btn(s_menu_list, NULL, buf);

    /* 3: WiFi disconnect */
    lv_list_add_btn(s_menu_list, NULL, "WiFi: Disconnect");

    /* 4: Git sync */
    snprintf(buf, sizeof(buf), "Git Sync%s",
             git_sync_is_configured() ? "" : " (not configured)");
    lv_list_add_btn(s_menu_list, NULL, buf);

    /* 5: Keyboard layout */
    snprintf(buf, sizeof(buf), "Keyboard: %s  (Enter to cycle)",
             kb_layout_name(kb_layout_get()));
    lv_list_add_btn(s_menu_list, NULL, buf);

    /* 6: Settings */
    lv_list_add_btn(s_menu_list, NULL, "Settings...");

    /* 7: Close menu */
    lv_list_add_btn(s_menu_list, NULL, "Close menu (Esc / F1)");

    /* Highlight selection */
    apply_list_selection_styles(s_menu_list, s_menu_sel);
    s_menu_sel_prev = s_menu_sel;
}

/* Move only the highlight bar without rebuilding the list. */
static void update_menu_highlight(void)
{
    update_list_highlight(s_menu_list, s_menu_sel, s_menu_sel_prev);
    s_menu_sel_prev = s_menu_sel;
}

static void show_menu(void)
{
    s_menu_open = true;
    s_menu_sel = 0;
    refresh_menu_items();
    lv_scr_load(s_scr_menu);
}

static void close_menu(void)
{
    s_menu_open = false;
    if (editor_get_mode() == EDITOR_MODE_EDITING)
        editor_ui_show_editor();
    else
        editor_ui_show_file_browser();
}

/* ---- Settings screen ---- */

static int find_timeout_option(uint32_t sec)
{
    for (int i = 0; i < TIMEOUT_OPTION_COUNT; i++) {
        if (TIMEOUT_OPTIONS[i] == sec) return i;
    }
    return 2; /* default to 10 min */
}

static void refresh_settings_items(void)
{
    lv_obj_clean(s_settings_list);

    char buf[80];
    uint32_t cur = standby_get_timeout();
    const char *cur_label = "custom";
    int idx = find_timeout_option(cur);
    if (idx >= 0 && idx < TIMEOUT_OPTION_COUNT)
        cur_label = TIMEOUT_LABELS[idx];

    /* 0: Standby timeout */
    snprintf(buf, sizeof(buf), "Standby timeout: %s", cur_label);
    lv_list_add_btn(s_settings_list, NULL, buf);

    /* 1: Base font size */
    {
        int fi = find_font_size_option(s_font_size);
        snprintf(buf, sizeof(buf), "Base font size: %s",
                 FONT_SIZE_LABELS[fi]);
        lv_list_add_btn(s_settings_list, NULL, buf);
    }

    /* 2: Sleep now */
    lv_list_add_btn(s_settings_list, NULL, "Sleep now");

    /* 3: Factory reset */
    if (s_factory_reset_confirm) {
        lv_list_add_btn(s_settings_list, NULL,
                        "Factory reset -- ENTER again to confirm");
    } else {
        lv_list_add_btn(s_settings_list, NULL, "Factory reset");
    }

    /* 4: Back */
    lv_list_add_btn(s_settings_list, NULL, "Back (Esc)");

    /* Highlight selection */
    apply_list_selection_styles(s_settings_list, s_settings_sel);
    s_settings_sel_prev = s_settings_sel;
}

/* Move only the highlight bar without rebuilding the list. */
static void update_settings_highlight(void)
{
    update_list_highlight(s_settings_list, s_settings_sel, s_settings_sel_prev);
    s_settings_sel_prev = s_settings_sel;
}

static void show_settings(void)
{
    s_settings_open = true;
    s_menu_open = false;
    s_settings_sel = 0;
    s_factory_reset_confirm = false;
    refresh_settings_items();
    lv_scr_load(s_scr_settings);
}

static void close_settings(void)
{
    s_settings_open = false;
    show_menu();
}

static void settings_activate_item(int idx)
{
    /* Moving away from the factory-reset item cancels confirmation */
    if (idx != 3 && s_factory_reset_confirm) {
        s_factory_reset_confirm = false;
        refresh_settings_items();
    }

    switch (idx) {
    case 0: {
        /* Cycle to next timeout option */
        uint32_t cur = standby_get_timeout();
        int opt = find_timeout_option(cur);
        opt = (opt + 1) % TIMEOUT_OPTION_COUNT;
        standby_set_timeout(TIMEOUT_OPTIONS[opt]);
        refresh_settings_items();
        break;
    }
    case 1: {
        /* Cycle to next font size option */
        int fi = find_font_size_option(s_font_size);
        fi = (fi + 1) % FONT_SIZE_COUNT;
        s_font_size = FONT_SIZE_OPTIONS[fi];
        save_font_size_to_nvs();
        init_styles();
        refresh_settings_items();
        break;
    }
    case 2:
        /* Sleep now -- auto-save first */
        if (editor_get_mode() == EDITOR_MODE_EDITING && editor_is_modified()) {
            editor_save_file();
        }
        standby_enter_sleep();
        break;
    case 3:
        /* Factory reset -- requires double-press confirmation */
        if (s_factory_reset_confirm) {
            ESP_LOGW(TAG, "Factory reset: erasing NVS and restarting");
            nvs_flash_erase();
            esp_restart();
            /* does not return */
        } else {
            s_factory_reset_confirm = true;
            refresh_settings_items();
        }
        break;
    case 4:
        close_settings();
        break;
    default:
        break;
    }
}

#define SETTINGS_ITEM_COUNT 5

static void handle_settings_key(const kb_event_t *ev)
{
    switch (ev->keycode) {
    case KB_KEY_UP:
        if (s_settings_sel > 0) s_settings_sel--;
        if (s_factory_reset_confirm) {
            /* Cancelling confirmation changes the item label, so we
             * need a full rebuild; otherwise just move the highlight. */
            s_factory_reset_confirm = false;
            refresh_settings_items();
        } else {
            update_settings_highlight();
        }
        break;
    case KB_KEY_DOWN:
        if (s_settings_sel < SETTINGS_ITEM_COUNT - 1) s_settings_sel++;
        if (s_factory_reset_confirm) {
            s_factory_reset_confirm = false;
            refresh_settings_items();
        } else {
            update_settings_highlight();
        }
        break;
    case KB_KEY_ENTER:
        settings_activate_item(s_settings_sel);
        break;
    case KB_KEY_ESCAPE:
        close_settings();
        break;
    default:
        break;
    }
}

/* Forward declarations for WiFi background connect (defined below) */
static void wifi_connect_task(void *arg);
static void wifi_connect_async(void);

static void menu_activate_item(int idx)
{
    switch (idx) {
    case 0: /* BLE status -- no action */
        break;
    case 1: /* BLE scan */
        ble_keyboard_start_scan();
        editor_ui_set_status("BLE: scanning...");
        close_menu();
        break;
    case 2: /* WiFi connect */
        if (!wifi_manager_is_connected()) {
            editor_ui_set_status("WiFi: connecting...");
            close_menu();
            wifi_connect_async();
        }
        break;
    case 3: /* WiFi disconnect */
        wifi_manager_disconnect();
        editor_ui_set_status("WiFi: disconnected");
        close_menu();
        break;
    case 4: /* Git sync */
        /* Auto-save unsaved edits so the sync task pushes the latest content. */
        if (editor_is_modified() && editor_get_file_path()) {
            editor_save_file();
        }
        if (git_sync_is_configured() && wifi_manager_is_connected()) {
            close_menu();
            if (git_sync_start(GIT_SYNC_BOTH) == ESP_OK) {
                editor_ui_set_status("Git: syncing...");
            } else {
                char sbuf[128];
                const char *err = git_sync_get_last_error();
                snprintf(sbuf, sizeof(sbuf), "Git: %s",
                         (err && err[0]) ? err : "failed to start sync");
                editor_ui_set_status(sbuf);
            }
        } else if (!wifi_manager_is_connected()) {
            editor_ui_set_status("Git: connect WiFi first");
        } else {
            editor_ui_set_status("Git: not configured");
        }
        break;
    case 5: /* Keyboard layout cycle */
        kb_layout_next();
        refresh_menu_items();
        break;
    case 6: /* Settings */
        show_settings();
        break;
    case 7: /* Close menu */
        close_menu();
        break;
    default:
        break;
    }
}

static void handle_menu_key(const kb_event_t *ev)
{
    switch (ev->keycode) {
    case KB_KEY_UP:
        if (s_menu_sel > 0) s_menu_sel--;
        update_menu_highlight();
        break;
    case KB_KEY_DOWN:
        if (s_menu_sel < MENU_ITEM_COUNT - 1) s_menu_sel++;
        update_menu_highlight();
        break;
    case KB_KEY_ENTER:
        menu_activate_item(s_menu_sel);
        break;
    case KB_KEY_ESCAPE:
    case KB_KEY_F1:
        close_menu();
        break;
    default:
        break;
    }
}

/* ---- Save-prompt overlay ---- */

/* Compute a default filename for a new (untitled) document.
 * Returns the bare filename (no directory prefix). */
static bool generate_default_name(char *buf, size_t buf_size)
{
    const char *mp = sd_card_get_mount_point();
    if (!mp) return false;
    char path[256];
    for (int seq = 1; seq <= MAX_DRAFT_SEQ; seq++) {
        snprintf(path, sizeof(path), "%s/draft_%03d.md", mp, seq);
        if (!sd_card_file_exists(path)) {
            snprintf(buf, buf_size, "draft_%03d.md", seq);
            return true;
        }
    }
    return false;
}

static void refresh_save_prompt(void)
{
    if (!s_save_panel) return;
    lv_label_set_text(s_save_name_lbl, s_save_buf);

    /* Position the thin cursor bar after the character at s_save_pos.
     * We measure the pixel width of the text up to the cursor position
     * using the monospace character width. */
    int cw = char_width_for_font(FONT_11);
    /* Count UTF-8 characters up to s_save_pos bytes */
    int chars = 0;
    for (int i = 0; i < s_save_pos; i++) {
        if ((s_save_buf[i] & 0xC0) != 0x80) chars++;
    }
    int cx = chars * cw;
    lv_obj_set_pos(s_save_cur, cx, 20);
    lv_obj_remove_flag(s_save_cur, LV_OBJ_FLAG_HIDDEN);
}

static void show_save_prompt(void)
{
    /* Pre-fill with existing filename (bare name, no directory) */
    const char *path = editor_get_file_path();
    if (path) {
        const char *slash = strrchr(path, '/');
        const char *name = slash ? slash + 1 : path;
        strncpy(s_save_buf, name, sizeof(s_save_buf) - 1);
        s_save_buf[sizeof(s_save_buf) - 1] = '\0';
    } else {
        if (!generate_default_name(s_save_buf, sizeof(s_save_buf))) {
            editor_ui_set_status("Save failed: too many drafts");
            return;
        }
    }
    s_save_pos = (int)strlen(s_save_buf);
    s_save_open = true;
    lv_obj_remove_flag(s_save_panel, LV_OBJ_FLAG_HIDDEN);
    refresh_save_prompt();
}

static void close_save_prompt(void)
{
    s_save_open = false;
    lv_obj_add_flag(s_save_panel, LV_OBJ_FLAG_HIDDEN);
}

static void save_prompt_confirm(void)
{
    if (s_save_buf[0] == '\0') {
        /* Empty name -- ignore */
        return;
    }
    const char *mp = sd_card_get_mount_point();
    if (!mp) {
        close_save_prompt();
        editor_ui_set_status("Save failed: SD card not ready");
        return;
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", mp, s_save_buf);
    esp_err_t err = editor_save_file_as(path);
    close_save_prompt();
    if (err == ESP_OK) {
        char msg[80];
        snprintf(msg, sizeof(msg), "Saved as %.40s", s_save_buf);
        editor_ui_set_status(msg);
    } else {
        editor_ui_set_status("Save failed!");
    }
    editor_ui_refresh();
}

static void handle_save_prompt_key(const kb_event_t *ev)
{
    switch (ev->keycode) {
    case KB_KEY_ENTER:
        save_prompt_confirm();
        return;
    case KB_KEY_ESCAPE:
        close_save_prompt();
        editor_ui_set_status(
            "F1:Menu Ctrl+S:Save Ctrl+L:Layout Ctrl+G:Git Esc:Files");
        return;
    case KB_KEY_LEFT:
        if (s_save_pos > 0) {
            /* Move back one UTF-8 character */
            do { s_save_pos--; }
            while (s_save_pos > 0 &&
                   (s_save_buf[s_save_pos] & 0xC0) == 0x80);
        }
        break;
    case KB_KEY_RIGHT:
        if (s_save_pos < (int)strlen(s_save_buf)) {
            /* Move forward one UTF-8 character */
            s_save_pos++;
            while (s_save_pos < (int)strlen(s_save_buf) &&
                   (s_save_buf[s_save_pos] & 0xC0) == 0x80)
                s_save_pos++;
        }
        break;
    case KB_KEY_HOME:
        s_save_pos = 0;
        break;
    case KB_KEY_END:
        s_save_pos = (int)strlen(s_save_buf);
        break;
    case KB_KEY_BACKSPACE:
        if (s_save_pos > 0) {
            /* Delete the previous UTF-8 character */
            int prev = s_save_pos - 1;
            while (prev > 0 && (s_save_buf[prev] & 0xC0) == 0x80)
                prev--;
            int len = (int)strlen(s_save_buf);
            memmove(s_save_buf + prev, s_save_buf + s_save_pos,
                    (size_t)(len - s_save_pos + 1));
            s_save_pos = prev;
        }
        break;
    case KB_KEY_DELETE: {
        int len = (int)strlen(s_save_buf);
        if (s_save_pos < len) {
            /* Find end of current UTF-8 character */
            int next = s_save_pos + 1;
            while (next < len && (s_save_buf[next] & 0xC0) == 0x80)
                next++;
            memmove(s_save_buf + s_save_pos, s_save_buf + next,
                    (size_t)(len - next + 1));
        }
        break;
    }
    default: {
        /* Insert typed character (use layout translation) */
        bool ctrl = (ev->modifier & (KB_MOD_LCTRL | KB_MOD_RCTRL)) != 0;
        if (ctrl) break; /* ignore ctrl combos in filename */
        const char *text = kb_layout_translate(ev->keycode, ev->modifier);
        if (text && text[0]) {
            size_t tlen = strlen(text);
            size_t cur_len = strlen(s_save_buf);
            /* Reject path separators and control chars */
            if (text[0] == '/' || text[0] == '\\' || text[0] < 0x20) break;
            if (cur_len + tlen < sizeof(s_save_buf) - 1) {
                memmove(s_save_buf + s_save_pos + tlen,
                        s_save_buf + s_save_pos,
                        cur_len - (size_t)s_save_pos + 1);
                memcpy(s_save_buf + s_save_pos, text, tlen);
                s_save_pos += (int)tlen;
            }
        }
        break;
    }
    }
    refresh_save_prompt();
}

/* ---- Keyboard handler (registered as BLE callback) ---- */

#if defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)
/* Information captured before a key event is processed, used by
 * try_partial_clip_for_typing() to decide whether the next e-paper
 * refresh can be narrowed to just the area around the typed
 * character. */
struct typing_pre_state_t {
    bool        valid;        /* false -> skip the fast path */
    int         line;         /* cursor line before the edit */
    int         col;          /* cursor column before the edit (UTF-8 chars) */
    int         cur_x;        /* absolute screen x of cursor before */
    int         cur_y;        /* absolute screen y of cursor before */
    int         cur_h;        /* cursor height before */
    bool        modified;     /* editor_is_modified() before */
    int         line_chars;   /* UTF-8 char count of the line before */
    int         char_w;       /* monospace cell width at body font */
};

/* Capture pre-edit state for the fast partial-clip path. Fills *out
 * with valid=false when conditions disqualify the edit (selection
 * active, line is not a plain paragraph, etc.); subsequent
 * try_partial_clip_for_typing() calls then short-circuit. */
static void capture_typing_pre_state(typing_pre_state_t *out)
{
    out->valid = false;
    if (s_cursor == NULL) return;
    if (lv_obj_has_flag(s_cursor, LV_OBJ_FLAG_HIDDEN)) return;
    if (editor_selection_active()) return;

    int line, col;
    editor_get_cursor_pos(&line, &col);
    (void)col;
    int total = editor_get_line_count();
    if (line < 0 || line >= total) return;

    size_t ll = 0;
    const char *lt = editor_get_line(line, &ll);
    if (!lt) return;

    /* Single-line classification (no surrounding code-fence context).
     * We only fast-path lines whose visible glyphs are exactly the
     * raw text -- i.e. plain paragraphs and empty lines. Any line
     * inside a fenced code block, a heading/bullet/quote/etc. has a
     * styled rendering whose pixel layout we can't safely predict
     * here. */
    md_line_info_t mi;
    md_parse_line(lt, ll, &mi, false);
    if (mi.type != MD_LINE_PARAGRAPH && mi.type != MD_LINE_EMPTY) return;

    int chars = utf8_chars_in_bytes(lt, ll);

    out->valid       = true;
    out->line        = line;
    out->col         = col;
    out->cur_x       = lv_obj_get_x(s_cursor);
    out->cur_y       = EDITOR_Y + lv_obj_get_y(s_cursor);
    out->cur_h       = lv_obj_get_height(s_cursor);
    out->modified    = editor_is_modified();
    out->line_chars  = chars;
    out->char_w      = CHAR_W;
}

/* Compute and apply a one-shot panel-refresh clip rectangle for an
 * edit that affects only one body line and the cursor.  Returns true
 * if the clip was set, false if the post-edit state disqualifies the
 * fast path (caller should leave the dirty bbox un-clipped, which
 * yields today's full-line refresh behaviour). */
static bool try_partial_clip_for_typing(const typing_pre_state_t *pre)
{
    if (!pre->valid) return false;
    if (s_cursor == NULL) return false;
    if (lv_obj_has_flag(s_cursor, LV_OBJ_FLAG_HIDDEN)) return false;
    if (editor_selection_active()) return false;
    if (editor_is_modified() != pre->modified) {
        /* Modified-flag flip -> title-bar text changed -> the title
         * strip is also dirty and must be refreshed alongside the
         * line. Fall back to the full bbox. */
        return false;
    }

    int line, col;
    editor_get_cursor_pos(&line, &col);
    (void)col;
    if (line != pre->line) return false; /* edit crossed a line boundary */

    int total = editor_get_line_count();
    if (line < 0 || line >= total) return false;

    size_t ll = 0;
    const char *lt = editor_get_line(line, &ll);
    if (!lt) return false;

    md_line_info_t mi;
    md_parse_line(lt, ll, &mi, false);
    if (mi.type != MD_LINE_PARAGRAPH && mi.type != MD_LINE_EMPTY) return false;

    int post_chars = utf8_chars_in_bytes(lt, ll);
    int char_w     = pre->char_w;
    int pre_w      = pre->line_chars * char_w;
    int post_w     = post_chars * char_w;
    int max_w      = pre_w > post_w ? pre_w : post_w;
    /* Bail if the line text wraps -- rendered_h would exceed line_h
     * and the dirty area covers multiple visual rows. */
    if (2 + max_w + 4 > SCR_W) return false;

    int post_cur_x = lv_obj_get_x(s_cursor);
    int post_cur_y = EDITOR_Y + lv_obj_get_y(s_cursor);
    int post_cur_h = lv_obj_get_height(s_cursor);
    /* Cursor moved to a different visual row -> the row geometry
     * changed (wrap or scroll), refresh the whole dirty bbox. */
    if (post_cur_y != pre->cur_y || post_cur_h != pre->cur_h) return false;

    /* X-extent of the changed pixels: from the leftmost-touched
     * column (min of pre/post cursor x) to the rightmost edge of the
     * line content (max of pre/post text width), plus the cursor's
     * own 2 px width on the right side. */
    int x_min = pre->cur_x < post_cur_x ? pre->cur_x : post_cur_x;
    int x_text_end = 2 + max_w;
    int x_cur_end  = (pre->cur_x > post_cur_x ? pre->cur_x : post_cur_x) + 2;
    int x_max = x_text_end > x_cur_end ? x_text_end : x_cur_end;

    /* Pad by a few pixels on each side so antialiased / overhanging
     * glyph outlines (Greybeard is bitmapped, so this is mostly
     * paranoia) and the cursor bar's own 2 px width are always
     * included. */
    x_min -= 4;
    x_max += 4;
    if (x_min < 0) x_min = 0;
    if (x_max > SCR_W) x_max = SCR_W;
    if (x_max <= x_min) return false;

    display_set_partial_clip(x_min, pre->cur_y, x_max - x_min, pre->cur_h);
    return true;
}
#endif /* CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3 */

#if defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)
/* RAII guard that runs the partial-clip post-processing on every
 * return path of handle_editor_key (including the F1 / Ctrl+? / Esc
 * early returns). Without this, a typing event in one drain batch
 * could leave a narrow clip set when a subsequent navigation event
 * runs early-return -- the next flush would then refresh only the
 * old typing region and clip away everything else. */
struct ClipGuard {
    typing_pre_state_t pre_state;
    bool eligible;
    ClipGuard() : eligible(false) {
        capture_typing_pre_state(&pre_state);
    }
    ~ClipGuard() {
        bool clipped = false;
        if (eligible) clipped = try_partial_clip_for_typing(&pre_state);
        if (!clipped) display_set_partial_clip(0, 0, 0, 0);
    }
};
#endif /* CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3 */

static void handle_editor_key(const kb_event_t *ev)
{
    bool ctrl  = (ev->modifier & (KB_MOD_LCTRL | KB_MOD_RCTRL)) != 0;
    bool shift = (ev->modifier & (KB_MOD_LSHIFT | KB_MOD_RSHIFT)) != 0;

#if defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)
    /* Snapshot before-edit state for the partial-refresh fast path,
     * and ensure the clip is updated (set or cleared) on every
     * return path. */
    ClipGuard clip_guard;
    bool &fast_path_eligible = clip_guard.eligible;
#endif

    /* Clear the escape-save-prompt on any key other than Esc */
    if (ev->keycode != KB_KEY_ESCAPE && s_esc_pending) {
        s_esc_pending = false;
        editor_ui_set_status(
            "F1:Menu Ctrl+S:Save Ctrl+L:Layout Ctrl+G:Git Esc:Files");
    }

    /* F1 opens the menu */
    if (ev->keycode == KB_KEY_F1) {
        show_menu();
        return;
    }

    /* Ctrl shortcuts */
    if (ctrl) {
        /* Interpret Ctrl shortcuts by physical key position (HID keycode),
         * NOT by the current layout translation.  This ensures that
         * Ctrl+S, Ctrl+L, etc. work regardless of the active layout.
         * HID keycodes 0x04..0x1D map to a..z. */
        char ch = 0;
        if (ev->keycode >= 0x04 && ev->keycode <= 0x1D) {
            ch = 'a' + (ev->keycode - 0x04);
        }
        switch (ch) {
        case 's':
            show_save_prompt();
            break;
        case 'n': editor_new_file(); break;
        case 'o': editor_ui_show_file_browser(); return;
        case 'c':
            if (editor_copy())
                editor_ui_set_status("Copied");
            ensure_cursor_visible();
            editor_ui_refresh();
            return;
        case 'x':
            if (editor_cut())
                editor_ui_set_status("Cut");
            ensure_cursor_visible();
            editor_ui_refresh();
            return;
        case 'v':
            editor_paste();
            ensure_cursor_visible();
            editor_ui_refresh();
            return;
        case 'a':
            editor_select_all();
            ensure_cursor_visible();
            editor_ui_refresh();
            return;
        case 'g':
            /* Auto-save the current file so the sync task picks up
             * the latest edits (it reads from disk). */
            if (editor_is_modified() && editor_get_file_path()) {
                editor_save_file();
            }
            if (git_sync_is_configured() && wifi_manager_is_connected()) {
                if (git_sync_start(GIT_SYNC_BOTH) == ESP_OK) {
                    editor_ui_set_status("Git: syncing...");
                } else {
                    char sbuf[128];
                    const char *err = git_sync_get_last_error();
                    snprintf(sbuf, sizeof(sbuf), "Git: %s",
                             (err && err[0]) ? err : "failed to start sync");
                    editor_ui_set_status(sbuf);
                }
            } else if (!wifi_manager_is_connected()) {
                editor_ui_set_status("Git: connect WiFi first (F1)");
            } else {
                editor_ui_set_status("Git: not configured");
            }
            break;
        case 'w':
            if (!wifi_manager_is_connected()) {
                editor_ui_set_status("WiFi: connecting...");
                wifi_connect_async();
            } else {
                wifi_manager_disconnect();
                editor_ui_set_status("WiFi: disconnecting...");
            }
            break;
        case 'l':
            /* Ctrl+L: cycle keyboard layout */
            kb_layout_next();
            break;
        default: break;
        }
        /* Ctrl+arrow / Ctrl+Home / Ctrl+End for word/doc movement */
        if (ev->keycode == KB_KEY_LEFT) {
            if (shift) editor_set_selection_anchor();
            else editor_clear_selection();
            editor_move_word_left();
        }
        if (ev->keycode == KB_KEY_RIGHT) {
            if (shift) editor_set_selection_anchor();
            else editor_clear_selection();
            editor_move_word_right();
        }
        if (ev->keycode == KB_KEY_HOME) {
            if (shift) editor_set_selection_anchor();
            else editor_clear_selection();
            editor_move_doc_start();
        }
        if (ev->keycode == KB_KEY_END) {
            if (shift) editor_set_selection_anchor();
            else editor_clear_selection();
            editor_move_doc_end();
        }
        ensure_cursor_visible();
        editor_ui_refresh();
        return;
    }

    /* Special keys */
    switch (ev->keycode) {
    case KB_KEY_LEFT:
        if (shift) { editor_set_selection_anchor(); editor_move_left(); }
        else if (editor_selection_active()) {
            size_t s, e; editor_get_selection_range(&s, &e);
            editor_clear_selection(); editor_set_cursor(s);
        } else { editor_move_left(); }
        break;
    case KB_KEY_RIGHT:
        if (shift) { editor_set_selection_anchor(); editor_move_right(); }
        else if (editor_selection_active()) {
            size_t s, e; editor_get_selection_range(&s, &e);
            editor_clear_selection(); editor_set_cursor(e);
        } else { editor_move_right(); }
        break;
    case KB_KEY_UP:
        if (shift) editor_set_selection_anchor();
        else editor_clear_selection();
        editor_move_up();
        break;
    case KB_KEY_DOWN:
        if (shift) editor_set_selection_anchor();
        else editor_clear_selection();
        editor_move_down();
        break;
    case KB_KEY_HOME:
        if (shift) editor_set_selection_anchor();
        else editor_clear_selection();
        editor_move_home();
        break;
    case KB_KEY_END:
        if (shift) editor_set_selection_anchor();
        else editor_clear_selection();
        editor_move_end();
        break;
    case KB_KEY_PAGEUP:
        if (shift) editor_set_selection_anchor();
        else editor_clear_selection();
        editor_move_page_up(VISIBLE_LINES);
        break;
    case KB_KEY_PAGEDOWN:
        if (shift) editor_set_selection_anchor();
        else editor_clear_selection();
        editor_move_page_down(VISIBLE_LINES);
        break;
    case KB_KEY_BACKSPACE:
        if (!editor_delete_selection()) {
            editor_delete_back();
#if defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)
            fast_path_eligible = true;
#endif
        }
        break;
    case KB_KEY_DELETE:
        if (!editor_delete_selection()) {
            editor_delete_forward();
#if defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)
            fast_path_eligible = true;
#endif
        }
        break;
    case KB_KEY_ENTER:
        editor_delete_selection();
        editor_insert_newline();
        break;
    case KB_KEY_TAB:
        editor_delete_selection();
        editor_insert_text("    ", 4);
        break;
    case KB_KEY_ESCAPE:
        if (editor_is_modified()) {
            if (s_esc_pending) {
                /* Second Esc -- discard and close */
                s_esc_pending = false;
                editor_ui_show_file_browser();
                return;
            }
            /* First Esc -- warn the user */
            s_esc_pending = true;
            editor_ui_set_status("Unsaved! Ctrl+S:Save  Esc:Discard");
        } else {
            s_esc_pending = false;
            editor_ui_show_file_browser();
            return;
        }
        break;
    default: {
        /* Use keyboard layout to translate keycode to UTF-8 */
        const char *text = kb_layout_translate(ev->keycode, ev->modifier);
        if (text) {
            bool had_sel = editor_selection_active();
            editor_delete_selection();
            editor_insert_text(text, strlen(text));
#if defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)
            /* The fast-path clip only handles edits that started
             * without a selection -- replacing a selection
             * potentially repaints multiple lines. */
            if (!had_sel) fast_path_eligible = true;
#else
            (void)had_sel;
#endif
        }
        break;
    }
    }

    ensure_cursor_visible();
    editor_ui_refresh();
    /* Partial-refresh clip is set/cleared by ClipGuard's destructor
     * on the M5Stack PaperS3 backend. */
}

static void handle_browser_key(const kb_event_t *ev)
{
    bool ctrl = (ev->modifier & (KB_MOD_LCTRL | KB_MOD_RCTRL)) != 0;

    /* F1 opens the menu from browser too */
    if (ev->keycode == KB_KEY_F1) {
        show_menu();
        return;
    }

    /* Ctrl+G triggers git sync from the file browser as well */
    if (ctrl) {
        char ck = 0;
        if (ev->keycode >= 0x04 && ev->keycode <= 0x1D) {
            ck = 'a' + (ev->keycode - 0x04);
        }
        if (ck == 'g') {
            if (git_sync_is_configured() && wifi_manager_is_connected()) {
                if (git_sync_start(GIT_SYNC_BOTH) == ESP_OK) {
                    editor_ui_set_status("Git: syncing...");
                } else {
                    char sbuf[128];
                    const char *err = git_sync_get_last_error();
                    snprintf(sbuf, sizeof(sbuf), "Git: %s",
                             (err && err[0]) ? err : "failed to start sync");
                    editor_ui_set_status(sbuf);
                }
            } else if (!wifi_manager_is_connected()) {
                editor_ui_set_status("Git: connect WiFi first (F1)");
            } else {
                editor_ui_set_status("Git: not configured");
            }
            return;
        }
    }

    /* Translate keycode to character for letter-key checks.
     * ev->character is always 0 because ble_keyboard does not fill
     * it -- all translation goes through kb_layout. */
    const char *br_t = kb_layout_translate(ev->keycode, ev->modifier);
    char ch = (br_t && br_t[0] && !br_t[1]) ? br_t[0] : 0;

    uint32_t child_count = lv_obj_get_child_count(s_list_files);
    if (child_count == 0) {
        if (ch == 'n' || ch == 'N') {
            editor_new_file();
            editor_ui_show_editor();
        }
        return;
    }

    switch (ev->keycode) {
    case KB_KEY_UP:
        if (s_browser_sel > 0) s_browser_sel--;
        break;
    case KB_KEY_DOWN:
        if (s_browser_sel < (int)child_count - 1) s_browser_sel++;
        break;
    case KB_KEY_ENTER: {
        lv_obj_t *btn = lv_obj_get_child(s_list_files, s_browser_sel);
        if (btn) {
            int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
            if (idx >= 0 && idx < s_browser_count) {
                if (!s_browser_entries[idx].is_dir) {
                    char path[512];
                    snprintf(path, sizeof(path), "%s/%s",
                             sd_card_get_mount_point(), s_browser_entries[idx].name);
                    editor_init();
                    editor_open_file(path);
                    editor_ui_show_editor();
                    return;
                }
            }
        }
        break;
    }
    default:
        if (ch == 'n' || ch == 'N') {
            editor_new_file();
            editor_ui_show_editor();
            return;
        }
        break;
    }

    /* Highlight selected item -- restyle only the items whose state
     * changed (previous and current selection) so the LVGL dirty
     * region stays small enough for a partial e-paper refresh. */
    update_list_highlight(s_list_files, s_browser_sel, s_browser_sel_prev);
    s_browser_sel_prev = s_browser_sel;
}

/* Process a single key event (must be called with LVGL lock held). */
static void process_key_event(const kb_event_t *ev)
{
    /* Normalize Keypad Enter to regular Enter so all handlers
     * only need to check for KB_KEY_ENTER. */
    kb_event_t norm = *ev;
    if (norm.keycode == KB_KEY_KP_ENTER) {
        norm.keycode = KB_KEY_ENTER;
    }
    const kb_event_t *e = &norm;

    if (s_save_open) {
        handle_save_prompt_key(e);
    } else if (s_settings_open) {
        handle_settings_key(e);
    } else if (s_menu_open) {
        handle_menu_key(e);
    } else if (editor_get_mode() == EDITOR_MODE_EDITING) {
        handle_editor_key(e);
    } else {
        handle_browser_key(e);
    }
}

/* LVGL timer callback: drains the key-event queue in a batch.
 * This runs inside lv_timer_handler() which already holds the LVGL
 * mutex, so we must NOT call lvgl_port_lock() here. */
static void key_drain_cb(lv_timer_t *timer)
{
    (void)timer;
    kb_event_t ev;

    while (xQueueReceive(s_key_queue, &ev, 0) == pdTRUE) {
        process_key_event(&ev);
    }
}

/* BLE callback: enqueue the event and return immediately.
 * This function runs on the HID host task and must not touch LVGL. */
extern "C" void editor_ui_handle_key(const void *event)
{
    const kb_event_t *ev = (const kb_event_t *)event;

    if (ev->pressed) {
        /* Reset standby inactivity timer on key-down */
        standby_reset_timer();

        /* Start key-repeat tracking */
        s_repeat_ev     = *ev;
        s_repeat_held   = true;
        s_repeat_start  = xTaskGetTickCount();
        s_repeat_firing = false;

        if (s_key_queue) {
            xQueueSend(s_key_queue, ev, 0);
        }
    } else {
        /* Key released -- cancel repeat for the released key */
        if (s_repeat_held && ev->keycode == s_repeat_ev.keycode) {
            s_repeat_held  = false;
            s_repeat_firing = false;
        }
    }
}

/* ---- Key repeat timer ---- */

/* Called periodically by an LVGL timer.  If a key is held longer than
 * KEY_REPEAT_DELAY_MS, inject the stored key event at the repeat rate. */
static void key_repeat_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_repeat_held) return;

    uint32_t now = xTaskGetTickCount();
    uint32_t elapsed = (now - s_repeat_start) * portTICK_PERIOD_MS;

    if (!s_repeat_firing) {
        if (elapsed >= KEY_REPEAT_DELAY_MS) {
            s_repeat_firing = true;
            /* Inject the first repeat event */
            process_key_event(&s_repeat_ev);
        }
    } else {
        /* Already past the initial delay -- inject at the repeat rate.
         * The timer fires every KEY_REPEAT_RATE_MS so one event per
         * invocation is sufficient. */
        process_key_event(&s_repeat_ev);
    }
}

/* ---- Passkey display callback ---- */

static void passkey_display_cb(uint32_t passkey)
{
    if (!lvgl_port_lock(100)) return;

    if (passkey == BLE_PASSKEY_DISMISS) {
        /* Hide the passkey overlay */
        if (s_passkey_panel) {
            lv_obj_add_flag(s_passkey_panel, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        /* Show the passkey overlay with the 6-digit code */
        if (s_passkey_panel) {
            char buf[48];
            snprintf(buf, sizeof(buf), "Enter on keyboard:\n%06lu",
                     (unsigned long)passkey);
            lv_label_set_text(s_passkey_label, buf);
            lv_obj_remove_flag(s_passkey_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lvgl_port_unlock();
}

/* ---- BLE connection status callback ---- */

static void ble_connect_status_cb(bool connected)
{
    ESP_LOGI("EditorUI", "BLE connect status: %s",
             connected ? "CONNECTED" : "DISCONNECTED");

    if (!lvgl_port_lock(500)) {
        ESP_LOGW("EditorUI", "LVGL lock timeout in connect_status_cb");
        return;
    }

    if (connected) {
        /* Flush any stale key events that accumulated while
         * the keyboard was disconnected / reconnecting. */
        if (s_key_queue) {
            xQueueReset(s_key_queue);
        }

        /* Keyboard just connected -- if we are on the BLE prompt screen,
         * return to whatever the user was doing before disconnect. */
        if (lv_scr_act() == s_scr_ble_prompt) {
            if (editor_get_mode() == EDITOR_MODE_EDITING) {
                /* Restore the editor -- file contents are still in
                 * the gap buffer; no need to close/reopen. */
                lv_scr_load(s_scr);
                editor_ui_refresh();
            } else {
                editor_ui_show_file_browser();
            }
        }
        if (s_ble_prompt_lbl) {
            lv_label_set_text(s_ble_prompt_lbl,
                "Keyboard connected!");
        }
    } else {
        /* Keyboard disconnected -- close any open overlays so the
         * UI is in a clean state when we reconnect.  Do NOT call
         * editor_close_file() -- preserve the user's work. */
        s_menu_open = false;
        s_settings_open = false;
        s_save_open = false;
        if (s_save_panel) lv_obj_add_flag(s_save_panel, LV_OBJ_FLAG_HIDDEN);
        /* Cancel any in-progress key repeat so stale keys do not
         * keep firing after reconnection. */
        s_repeat_held = false;
        s_repeat_firing = false;
        if (s_ble_prompt_lbl) {
            lv_label_set_text(s_ble_prompt_lbl,
                "Keyboard disconnected.\nReconnecting...");
        }
        lv_scr_load(s_scr_ble_prompt);
    }

    lvgl_port_unlock();
}

/* BLE status text callback -- update the BLE prompt label with
 * connection progress messages from the BLE keyboard component. */
static void ble_status_text_cb(const char *text)
{
    if (!s_ble_prompt_lbl) return;
    if (!lvgl_port_lock(200)) return;

    /* Only update when the BLE prompt screen is active */
    if (lv_scr_act() == s_scr_ble_prompt) {
        lv_label_set_text(s_ble_prompt_lbl, text);
    }

    lvgl_port_unlock();
}

/* ---- WiFi connect task ----
 * wifi_manager_connect() blocks for up to 30 seconds while waiting for
 * the connection.  Running it on a separate task keeps the LVGL render
 * loop responsive so the user sees status updates in real time. */
static void wifi_connect_task(void *arg)
{
    (void)arg;
    esp_err_t ret = wifi_manager_connect();
    if (ret == ESP_ERR_NOT_FOUND) {
        /* No credentials -- the wifi_state callback will not fire,
         * so update the status bar directly. */
        if (lvgl_port_lock(200)) {
            editor_ui_set_status("WiFi: no credentials found");
            lvgl_port_unlock();
        }
    }
    vTaskDelete(NULL);
}

/* Start WiFi connection on a background task. */
static void wifi_connect_async(void)
{
    BaseType_t rc = xTaskCreatePinnedToCore(wifi_connect_task, "wifi_conn",
                                            4 * 1024, NULL, 3, NULL, 0);
    if (rc != pdPASS) {
        editor_ui_set_status("WiFi: failed to start task");
    }
}

/* ---- WiFi state callback ----
 * Called from the WiFi manager (event handler context) when the
 * connection state changes.  Must take the LVGL lock before touching
 * any UI objects. */
static void wifi_state_cb(wifi_state_t state)
{
    if (!lvgl_port_lock(200)) return;

    switch (state) {
    case WIFI_STATE_CONNECTED:
    {
        char buf[80];
        snprintf(buf, sizeof(buf), "WiFi: %s (%s)",
                 wifi_manager_get_ssid(), wifi_manager_get_ip());
        editor_ui_set_status(buf);
        break;
    }
    case WIFI_STATE_ERROR:
        editor_ui_set_status("WiFi: connection failed");
        break;
    case WIFI_STATE_DISCONNECTED:
        editor_ui_set_status("WiFi: disconnected");
        break;
    default:
        break;
    }

    lvgl_port_unlock();
}

/* ---- Git sync callback ----
 * Called from the git_sync task when the sync state changes.
 * Must take the LVGL lock before touching any UI objects. */
static void git_sync_cb(git_sync_state_t state, const char *message)
{
    if (!lvgl_port_lock(200)) return;

    switch (state) {
    case GIT_SYNC_IN_PROGRESS:
    {
        char buf[80];
        snprintf(buf, sizeof(buf), "Git: %s",
                 message ? message : "syncing...");
        editor_ui_set_status(buf);
        break;
    }
    case GIT_SYNC_SUCCESS:
        editor_ui_set_status("Git: sync complete");
        /* If a file is currently open in the editor, reload it from disk
         * so the user sees any changes that were pulled from the remote. */
        if (editor_get_mode() == EDITOR_MODE_EDITING && editor_get_file_path()) {
            const char *path = editor_get_file_path();
            editor_open_file(path);
            editor_ui_refresh();
        }
        break;
    case GIT_SYNC_ERROR:
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "Git: %s",
                 message ? message : "error");
        editor_ui_set_status(buf);
        break;
    }
    default:
        break;
    }

    lvgl_port_unlock();
}

/* ---- Initialization ---- */

extern "C" void editor_ui_init(void)
{
    load_font_size_from_nvs();
    init_styles();

    /* Create key-event queue (must exist before BLE callback is set) */
    s_key_queue = xQueueCreate(KEY_QUEUE_LEN, sizeof(kb_event_t));
    assert(s_key_queue);

    /* Editor init */
    editor_init();

    /* ---- Editor screen ---- */
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_white(), 0);

    /* Title bar */
    s_lbl_title = lv_label_create(s_scr);
    lv_obj_set_pos(s_lbl_title, 2, 0);
    lv_obj_set_width(s_lbl_title, SCR_W - 4);
    lv_obj_set_style_text_font(s_lbl_title, FONT_11, 0);
    lv_obj_set_style_text_color(s_lbl_title, lv_color_black(), 0);
    lv_label_set_text(s_lbl_title, "Draftling");

    /* Header separator line */
    lv_obj_t *hline = lv_obj_create(s_scr);
    lv_obj_set_size(hline, SCR_W, 1);
    lv_obj_set_pos(hline, 0, HEADER_H - 1);
    lv_obj_set_style_bg_color(hline, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(hline, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hline, 0, 0);
    lv_obj_set_style_radius(hline, 0, 0);
    lv_obj_set_style_pad_all(hline, 0, 0);

    /* Editor content area */
    s_cont_edit = lv_obj_create(s_scr);
    lv_obj_set_pos(s_cont_edit, 0, EDITOR_Y);
    lv_obj_set_size(s_cont_edit, SCR_W, EDITOR_H);
    lv_obj_set_style_bg_opa(s_cont_edit, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_cont_edit, 0, 0);
    lv_obj_set_style_pad_all(s_cont_edit, 0, 0);
    lv_obj_set_style_radius(s_cont_edit, 0, 0);
    lv_obj_remove_flag(s_cont_edit, LV_OBJ_FLAG_SCROLLABLE);

    /* "draftling" text label shown when no file is open */
    s_img_logo = lv_label_create(s_cont_edit);
    lv_obj_set_width(s_img_logo, SCR_W);
    lv_obj_set_style_text_font(s_img_logo, FONT_18, 0);
    lv_obj_set_style_text_color(s_img_logo, lv_color_make(0x80, 0x80, 0x80), 0);
    lv_obj_set_style_text_align(s_img_logo, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_img_logo, "draftling");
    lv_obj_set_pos(s_img_logo, 0,
                   (EDITOR_H - lv_font_get_line_height(FONT_18)) / 2);
    lv_obj_add_flag(s_img_logo, LV_OBJ_FLAG_HIDDEN);

    /* Selection overlay labels (created before cursor and line labels
     * so their initial z-order is behind text; partial-selection code
     * calls lv_obj_move_foreground() to bring them on top).  These
     * display the selected text in white on a black background for
     * proper inversion on the monochrome e-paper display. */
    for (int i = 0; i < MAX_LINE_LABELS; i++) {
        s_sel_rects[i] = lv_label_create(s_cont_edit);
        lv_obj_set_style_bg_color(s_sel_rects[i],
                                  lv_color_black(), 0);
        lv_obj_set_style_bg_opa(s_sel_rects[i], LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(s_sel_rects[i],
                                    lv_color_white(), 0);
        lv_obj_set_style_border_width(s_sel_rects[i], 0, 0);
        lv_obj_set_style_radius(s_sel_rects[i], 0, 0);
        lv_obj_set_style_pad_all(s_sel_rects[i], 0, 0);
        lv_label_set_text(s_sel_rects[i], "");
        lv_obj_add_flag(s_sel_rects[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* Cursor (thin vertical bar) */
    s_cursor = lv_obj_create(s_cont_edit);
    lv_obj_set_size(s_cursor, 2, LINE_H);
    lv_obj_set_style_bg_color(s_cursor, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_cursor, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cursor, 0, 0);
    lv_obj_set_style_radius(s_cursor, 0, 0);
    lv_obj_set_style_pad_all(s_cursor, 0, 0);

    /* Status bar */
    lv_obj_t *sline = lv_obj_create(s_scr);
    lv_obj_set_size(sline, SCR_W, 1);
    lv_obj_set_pos(sline, 0, SCR_H - STATUS_H);
    lv_obj_set_style_bg_color(sline, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(sline, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sline, 0, 0);
    lv_obj_set_style_radius(sline, 0, 0);
    lv_obj_set_style_pad_all(sline, 0, 0);

    s_lbl_status = lv_label_create(s_scr);
    lv_obj_set_pos(s_lbl_status, 2, SCR_H - STATUS_H + 2);
    lv_obj_set_width(s_lbl_status, SCR_W - 4);
    lv_obj_set_style_text_font(s_lbl_status, FONT_11, 0);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_black(), 0);
    lv_label_set_text(s_lbl_status,
        "F1:Menu Ctrl+S:Save Ctrl+L:Layout Ctrl+G:Git Esc:Files");

#if defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_RLCD42) || defined(CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001)
    /* Device battery label (right-aligned in editor status bar) */
    s_lbl_dev_batt = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_lbl_dev_batt, FONT_11, 0);
    lv_obj_set_style_text_color(s_lbl_dev_batt, lv_color_black(), 0);
    lv_obj_set_style_text_align(s_lbl_dev_batt, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_lbl_dev_batt, SCR_W - 80, SCR_H - STATUS_H + 2);
    lv_obj_set_width(s_lbl_dev_batt, 78);
    lv_label_set_text(s_lbl_dev_batt, "");
#endif

    /* Cursor blink timer.
     *
     * On e-paper backends a 500 ms blink causes a full panel refresh
     * twice per second, which is both visually distracting and bad
     * for the panel. Keep the cursor solid (always visible) on EPD
     * targets; only the reflective LCD blinks. */
#if defined(CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001) || \
    defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_EPD_HAT) || \
    defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)
    s_cursor_visible = true;
    if (s_cursor) lv_obj_remove_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
#else
    s_blink_timer = lv_timer_create(cursor_blink_cb, 500, NULL);
#endif

    /* Key-event drain timer -- runs every 20 ms (50 Hz) to process
     * queued keyboard events in a batch.  This decouples BLE input
     * from the LVGL render cycle so fast typing never drops keys. */
    s_key_drain_timer = lv_timer_create(key_drain_cb, KEY_DRAIN_PERIOD_MS, NULL);

    /* ---- File browser screen ---- */
    s_scr_browser = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_browser, lv_color_white(), 0);

    lv_obj_t *br_title = lv_label_create(s_scr_browser);
    lv_obj_set_pos(br_title, 2, 0);
    lv_obj_set_style_text_font(br_title, FONT_11, 0);
    lv_obj_set_style_text_color(br_title, lv_color_black(), 0);
    lv_label_set_text(br_title, "File Browser - Up/Down, Enter to open, N for new");

    s_list_files = lv_list_create(s_scr_browser);
    lv_obj_set_pos(s_list_files, 0, 18);
    lv_obj_set_size(s_list_files, SCR_W, LIST_PANEL_H - STATUS_H);
    lv_obj_set_style_border_width(s_list_files, 0, 0);
    lv_obj_set_style_radius(s_list_files, 0, 0);
    lv_obj_set_style_pad_all(s_list_files, 0, 0);
    lv_obj_set_style_text_font(s_list_files, FONT_14, 0);

    /* File browser status bar */
    lv_obj_t *br_sline = lv_obj_create(s_scr_browser);
    lv_obj_set_size(br_sline, SCR_W, 1);
    lv_obj_set_pos(br_sline, 0, SCR_H - STATUS_H);
    lv_obj_set_style_bg_color(br_sline, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(br_sline, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(br_sline, 0, 0);
    lv_obj_set_style_radius(br_sline, 0, 0);
    lv_obj_set_style_pad_all(br_sline, 0, 0);

    s_lbl_br_status = lv_label_create(s_scr_browser);
    lv_obj_set_pos(s_lbl_br_status, 2, SCR_H - STATUS_H + 2);
    lv_obj_set_width(s_lbl_br_status, SCR_W - 4);
    lv_obj_set_style_text_font(s_lbl_br_status, FONT_11, 0);
    lv_obj_set_style_text_color(s_lbl_br_status, lv_color_black(), 0);
    lv_label_set_text(s_lbl_br_status, "F1:Menu  N:New  Ctrl+G:Git");

#if defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_RLCD42) || defined(CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001)
    /* Device battery label (right-aligned in browser status bar) */
    s_lbl_br_dev_batt = lv_label_create(s_scr_browser);
    lv_obj_set_style_text_font(s_lbl_br_dev_batt, FONT_11, 0);
    lv_obj_set_style_text_color(s_lbl_br_dev_batt, lv_color_black(), 0);
    lv_obj_set_style_text_align(s_lbl_br_dev_batt, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_lbl_br_dev_batt, SCR_W - 80, SCR_H - STATUS_H + 2);
    lv_obj_set_width(s_lbl_br_dev_batt, 78);
    lv_label_set_text(s_lbl_br_dev_batt, "");

    /* Battery poll timer + first reading */
    s_batt_timer = lv_timer_create(batt_timer_cb, BATT_POLL_MS, NULL);
    batt_timer_cb(NULL);  /* show initial value immediately */
#endif

    /* Register keyboard callback */
    ble_keyboard_set_callback((kb_event_callback_t)editor_ui_handle_key);

    /* ---- BLE connection prompt screen ---- */
    s_scr_ble_prompt = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_ble_prompt, lv_color_white(), 0);

    /* "draftling" title centered near the top */
    {
        lv_obj_t *title = lv_label_create(s_scr_ble_prompt);
        lv_obj_set_width(title, SCR_W);
        lv_obj_set_style_text_font(title, FONT_18, 0);
        lv_obj_set_style_text_color(title, lv_color_black(), 0);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(title, "draftling");
        int fh = lv_font_get_line_height(FONT_18);
        lv_obj_set_pos(title, 0, SCR_H / 4 - fh / 2);
    }

    /* Prompt label below center */
    s_ble_prompt_lbl = lv_label_create(s_scr_ble_prompt);
    lv_obj_set_width(s_ble_prompt_lbl, SCR_W - 20);
    lv_obj_set_style_text_font(s_ble_prompt_lbl, FONT_14, 0);
    lv_obj_set_style_text_color(s_ble_prompt_lbl, lv_color_black(), 0);
    lv_obj_set_style_text_align(s_ble_prompt_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_ble_prompt_lbl,
        "Searching for BLE keyboard...\nPlease turn on your keyboard");
    lv_obj_set_pos(s_ble_prompt_lbl, 10, SCR_H / 2 + 10);

    /* Register BLE status callbacks */
    ble_keyboard_set_connect_callback(ble_connect_status_cb);
    ble_keyboard_set_status_text_callback(ble_status_text_cb);

    /* ---- Menu screen ---- */
    s_scr_menu = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_menu, lv_color_white(), 0);

    s_lbl_menu_hdr = lv_label_create(s_scr_menu);
    lv_obj_set_pos(s_lbl_menu_hdr, 2, 0);
    lv_obj_set_style_text_font(s_lbl_menu_hdr, FONT_11, 0);
    lv_obj_set_style_text_color(s_lbl_menu_hdr, lv_color_black(), 0);
    lv_label_set_text(s_lbl_menu_hdr,
                      "Menu - Up/Down, Enter to select, Esc to close");

    s_menu_list = lv_list_create(s_scr_menu);
    lv_obj_set_pos(s_menu_list, 0, 18);
    lv_obj_set_size(s_menu_list, SCR_W, LIST_PANEL_H);
    lv_obj_set_style_border_width(s_menu_list, 0, 0);
    lv_obj_set_style_radius(s_menu_list, 0, 0);
    lv_obj_set_style_pad_all(s_menu_list, 0, 0);
    lv_obj_set_style_text_font(s_menu_list, FONT_14, 0);

    /* ---- Settings screen ---- */
    s_scr_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_settings, lv_color_white(), 0);

    lv_obj_t *set_hdr = lv_label_create(s_scr_settings);
    lv_obj_set_pos(set_hdr, 2, 0);
    lv_obj_set_style_text_font(set_hdr, FONT_11, 0);
    lv_obj_set_style_text_color(set_hdr, lv_color_black(), 0);
    lv_label_set_text(set_hdr,
                      "Settings - Up/Down, Enter to change, Esc to go back");

    s_settings_list = lv_list_create(s_scr_settings);
    lv_obj_set_pos(s_settings_list, 0, 18);
    lv_obj_set_size(s_settings_list, SCR_W, LIST_PANEL_H);
    lv_obj_set_style_border_width(s_settings_list, 0, 0);
    lv_obj_set_style_radius(s_settings_list, 0, 0);
    lv_obj_set_style_pad_all(s_settings_list, 0, 0);
    lv_obj_set_style_text_font(s_settings_list, FONT_14, 0);

    /* ---- Passkey overlay (shown on the editor screen) ---- */
    s_passkey_panel = lv_obj_create(s_scr);
    lv_obj_set_size(s_passkey_panel, SCR_W - 20, 60);
    lv_obj_set_pos(s_passkey_panel,
                   10, (SCR_H - 60) / 2);
    lv_obj_set_style_bg_color(s_passkey_panel, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_passkey_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_passkey_panel, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_passkey_panel, 2, 0);
    lv_obj_set_style_radius(s_passkey_panel, 4, 0);
    lv_obj_set_style_pad_all(s_passkey_panel, 6, 0);
    lv_obj_remove_flag(s_passkey_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_passkey_panel, LV_OBJ_FLAG_HIDDEN);

    s_passkey_label = lv_label_create(s_passkey_panel);
    lv_obj_set_style_text_font(s_passkey_label, FONT_16, 0);
    lv_obj_set_style_text_color(s_passkey_label, lv_color_black(), 0);
    lv_obj_set_style_text_align(s_passkey_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_passkey_label, SCR_W - 20 - 12);
    lv_obj_center(s_passkey_label);
    lv_label_set_text(s_passkey_label, "");

    /* Register passkey display callback */
    ble_keyboard_set_passkey_callback(passkey_display_cb);

    /* ---- Save-prompt overlay (shown on the editor screen) ---- */
    s_save_panel = lv_obj_create(s_scr);
    lv_obj_set_size(s_save_panel, SCR_W - 20, 46);
    lv_obj_set_pos(s_save_panel, 10, (SCR_H - 46) / 2);
    lv_obj_set_style_bg_color(s_save_panel, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_save_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_save_panel, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_save_panel, 2, 0);
    lv_obj_set_style_radius(s_save_panel, 4, 0);
    lv_obj_set_style_pad_all(s_save_panel, 6, 0);
    lv_obj_remove_flag(s_save_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_save_panel, LV_OBJ_FLAG_HIDDEN);

    s_save_hdr_lbl = lv_label_create(s_save_panel);
    lv_obj_set_style_text_font(s_save_hdr_lbl, FONT_11, 0);
    lv_obj_set_style_text_color(s_save_hdr_lbl, lv_color_black(), 0);
    lv_label_set_text(s_save_hdr_lbl, "Save as (Enter/Esc):");
    lv_obj_set_pos(s_save_hdr_lbl, 0, 0);

    s_save_name_lbl = lv_label_create(s_save_panel);
    lv_obj_set_style_text_font(s_save_name_lbl, FONT_11, 0);
    lv_obj_set_style_text_color(s_save_name_lbl, lv_color_black(), 0);
    lv_obj_set_width(s_save_name_lbl, SCR_W - 20 - 12);
    lv_label_set_text(s_save_name_lbl, "");
    lv_obj_set_pos(s_save_name_lbl, 0, 20);

    /* Thin cursor bar inside the save prompt name field */
    s_save_cur = lv_obj_create(s_save_panel);
    lv_obj_set_size(s_save_cur, 2, LINE_H);
    lv_obj_set_style_bg_color(s_save_cur, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_save_cur, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_save_cur, 0, 0);
    lv_obj_set_style_radius(s_save_cur, 0, 0);
    lv_obj_set_style_pad_all(s_save_cur, 0, 0);
    lv_obj_add_flag(s_save_cur, LV_OBJ_FLAG_HIDDEN);

    /* ---- Key repeat timer ---- */
    s_repeat_timer = lv_timer_create(key_repeat_cb, KEY_REPEAT_RATE_MS, NULL);

    /* Register WiFi and Git sync status callbacks */
    wifi_manager_set_callback(wifi_state_cb);
    git_sync_set_callback(git_sync_cb);

    /* Start on BLE prompt screen (transitions to file browser on connect) */
    lv_scr_load(s_scr_ble_prompt);

    ESP_LOGI(TAG, "Editor UI initialized");
}
