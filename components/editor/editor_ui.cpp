#include <cstdio>
#include <cstring>
#include <esp_log.h>
#include "sdkconfig.h"
#include "lvgl.h"

#include "editor_ui.h"
#include "editor.h"
#include "md_parser.h"
#include "ble_keyboard.h"
#include "kb_layout.h"
#include "wifi_manager.h"
#include "git_sync.h"
#include "sd_card.h"
#include "lvgl_port.h"
#include "standby.h"
#include "draftling_logo.h"
#include "montserrat_cyrillic.h"
#include "montserrat_cjk.h"

/*
 * Font aliases with fallbacks.
 *
 * When a non-ASCII keyboard layout (UA, DE, FR) is enabled, the custom
 * Montserrat fonts with extended Unicode coverage (Latin + Cyrillic)
 * are used so that all characters can be rendered correctly.
 *
 * When a CJK keyboard layout (KO, JA, ZH) is enabled, the custom
 * Montserrat fonts with CJK glyph coverage (Jamo, Hiragana, Katakana,
 * Bopomofo) are used.
 *
 * Otherwise the built-in LVGL Montserrat fonts are used.
 * montserrat_14 is always available (LVGL default).
 */
#if HAVE_CJK_FONTS
#define FONT_10 (&montserrat_cjk_10)
#define FONT_12 (&montserrat_cjk_12)
#define FONT_14 (&montserrat_cjk_14)
#define FONT_16 (&montserrat_cjk_16)
#define FONT_18 (&montserrat_cjk_18)
#elif HAVE_CYRILLIC_FONTS
#define FONT_10 (&montserrat_cyrillic_10)
#define FONT_12 (&montserrat_cyrillic_12)
#define FONT_14 (&montserrat_cyrillic_14)
#define FONT_16 (&montserrat_cyrillic_16)
#define FONT_18 (&montserrat_cyrillic_18)
#else
#if LV_FONT_MONTSERRAT_10
#define FONT_10 (&lv_font_montserrat_10)
#else
#define FONT_10 (&lv_font_montserrat_14)
#endif

#if LV_FONT_MONTSERRAT_12
#define FONT_12 (&lv_font_montserrat_12)
#else
#define FONT_12 (&lv_font_montserrat_14)
#endif

#define FONT_14 (&lv_font_montserrat_14)

#if LV_FONT_MONTSERRAT_16
#define FONT_16 (&lv_font_montserrat_16)
#else
#define FONT_16 (&lv_font_montserrat_14)
#endif

#if LV_FONT_MONTSERRAT_18
#define FONT_18 (&lv_font_montserrat_18)
#else
#define FONT_18 (&lv_font_montserrat_14)
#endif
#endif /* HAVE_CJK_FONTS / HAVE_CYRILLIC_FONTS */

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
#define LINE_H       14
#define VISIBLE_LINES (EDITOR_H / LINE_H)
#define CHAR_W       7   /* approx width of montserrat_12 char */
#define LIST_H       (SCR_H - 18)  /* height for list panels below header */

/* LVGL objects */
static lv_obj_t *s_scr       = NULL;
static lv_obj_t *s_lbl_title = NULL;
static lv_obj_t *s_cont_edit = NULL;
static lv_obj_t *s_lbl_status= NULL;
static lv_obj_t *s_cursor    = NULL;
static lv_obj_t *s_scr_browser = NULL;
static lv_obj_t *s_list_files  = NULL;
static lv_obj_t *s_img_logo    = NULL;

/* Menu overlay objects */
static lv_obj_t *s_scr_menu     = NULL;
static lv_obj_t *s_menu_list    = NULL;
static lv_obj_t *s_lbl_menu_hdr = NULL;
static int       s_menu_sel     = 0;
static bool      s_menu_open    = false;

/* Number of menu items */
#define MENU_ITEM_COUNT 8

static lv_timer_t *s_blink_timer = NULL;
static bool s_cursor_visible = true;
static int  s_browser_sel    = 0;
static int  s_browser_count  = 0;
static sd_card_file_entry_t s_browser_entries[64];

/* Settings screen objects */
static lv_obj_t *s_scr_settings  = NULL;
static lv_obj_t *s_settings_list = NULL;
static int       s_settings_sel  = 0;
static bool      s_settings_open = false;

/* Passkey overlay objects */
static lv_obj_t *s_passkey_panel = NULL;
static lv_obj_t *s_passkey_label = NULL;

/* Standby timeout options in seconds: 0=Off, 300=5min, 600=10min, etc. */
static const uint32_t TIMEOUT_OPTIONS[] = { 0, 300, 600, 900, 1800, 3600 };
static const char *TIMEOUT_LABELS[]     = { "Off", "5 min", "10 min",
                                            "15 min", "30 min", "60 min" };
#define TIMEOUT_OPTION_COUNT  6

/* Line label pool */
#define MAX_LINE_LABELS 24
static lv_obj_t *s_line_labels[MAX_LINE_LABELS] = {NULL};

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
    lv_style_set_text_font(&s_style_body, FONT_12);
    lv_style_set_text_color(&s_style_body, lv_color_black());
    lv_style_set_pad_all(&s_style_body, 0);

    lv_style_init(&s_style_h1);
    lv_style_set_text_font(&s_style_h1, FONT_18);
    lv_style_set_text_color(&s_style_h1, lv_color_black());

    lv_style_init(&s_style_h2);
    lv_style_set_text_font(&s_style_h2, FONT_16);
    lv_style_set_text_color(&s_style_h2, lv_color_black());

    lv_style_init(&s_style_h3);
    lv_style_set_text_font(&s_style_h3, FONT_14);
    lv_style_set_text_color(&s_style_h3, lv_color_black());

    lv_style_init(&s_style_code);
    lv_style_set_text_font(&s_style_code, FONT_12);
    lv_style_set_text_color(&s_style_code, lv_color_black());
    lv_style_set_border_width(&s_style_code, 1);
    lv_style_set_border_color(&s_style_code, lv_color_black());
    lv_style_set_pad_left(&s_style_code, 4);

    lv_style_init(&s_style_quote);
    lv_style_set_text_font(&s_style_quote, FONT_12);
    lv_style_set_text_color(&s_style_quote, lv_color_black());
    lv_style_set_border_side(&s_style_quote, LV_BORDER_SIDE_LEFT);
    lv_style_set_border_width(&s_style_quote, 2);
    lv_style_set_border_color(&s_style_quote, lv_color_black());
    lv_style_set_pad_left(&s_style_quote, 8);
}

static void cursor_blink_cb(lv_timer_t *timer)
{
    (void)timer;
    s_cursor_visible = !s_cursor_visible;
    if (s_cursor) {
        if (s_cursor_visible) lv_obj_remove_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_title_bar(void)
{
    const char *path = editor_get_file_path();
    const char *name = "Untitled";
    if (path) {
        const char *slash = strrchr(path, '/');
        name = slash ? slash + 1 : path;
    }
    int line, col;
    editor_get_cursor_pos(&line, &col);
    char batt_str[24] = "";
    int batt = ble_keyboard_get_battery_level();
    if (batt >= 0) {
        snprintf(batt_str, sizeof(batt_str), " Bat:%d%%", batt);
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "%s%s  L:%d C:%d  [%s]%s",
             name, editor_is_modified() ? " *" : "",
             line + 1, col + 1,
             kb_layout_name(kb_layout_get()), batt_str);
    lv_label_set_text(s_lbl_title, buf);
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

extern "C" void editor_ui_refresh(void)
{
    if (editor_get_mode() != EDITOR_MODE_EDITING) return;

    /* Show logo when no file is loaded and buffer is empty */
    size_t text_len = 0;
    editor_get_text(&text_len);
    bool show_logo = (editor_get_file_path() == NULL && text_len == 0);
    if (s_img_logo) {
        if (show_logo) {
            lv_obj_remove_flag(s_img_logo, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_img_logo, LV_OBJ_FLAG_HIDDEN);
        }
    }

    int scroll = editor_get_scroll_line();
    int total  = editor_get_line_count();
    bool in_code = false;

    /* Track code fence state up to scroll line */
    for (int i = 0; i < scroll && i < total; i++) {
        size_t ll;
        const char *lt = editor_get_line(i, &ll);
        if (md_is_code_fence(lt, ll)) in_code = !in_code;
    }

    /* Render visible lines */
    char line_buf[256];
    for (int i = 0; i < MAX_LINE_LABELS; i++) {
        int line_idx = scroll + i;
        if (line_idx >= total) {
            if (s_line_labels[i]) lv_obj_add_flag(s_line_labels[i], LV_OBJ_FLAG_HIDDEN);
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

        /* Create or reuse label */
        if (!s_line_labels[i]) {
            s_line_labels[i] = lv_label_create(s_cont_edit);
            lv_obj_set_width(s_line_labels[i], SCR_W - 4);
            lv_label_set_long_mode(s_line_labels[i], LV_LABEL_LONG_CLIP);
        }
        lv_obj_remove_flag(s_line_labels[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_style_all(s_line_labels[i]);
        lv_obj_add_style(s_line_labels[i], style_for_type(mi.type), 0);
        lv_label_set_text_static(s_line_labels[i], "");

        /* Copy text to a persistent buffer (label needs it) */
        /* Use lv_label_set_text which copies internally */
        char tmp[256];
        size_t clen = disp_len < sizeof(tmp) - 1 ? disp_len : sizeof(tmp) - 1;
        memcpy(tmp, disp_text, clen);
        tmp[clen] = '\0';
        lv_label_set_text(s_line_labels[i], tmp);
        lv_obj_set_pos(s_line_labels[i], 2, i * LINE_H);
    }

    /* Update cursor position */
    int cur_line, cur_col;
    editor_get_cursor_pos(&cur_line, &cur_col);
    int vis_line = cur_line - scroll;
    if (vis_line >= 0 && vis_line < VISIBLE_LINES && s_cursor) {
        lv_obj_set_pos(s_cursor, 2 + cur_col * CHAR_W, vis_line * LINE_H);
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

    /* Show a prompt when no BLE keyboard is connected */
    if (!ble_keyboard_is_connected()) {
        lv_obj_t *hint = lv_list_add_btn(s_list_files, NULL,
            "No keyboard connected. Waiting for BLE...");
        lv_obj_set_style_text_color(hint, lv_color_make(128, 128, 128), 0);
    }

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
}

extern "C" void editor_ui_show_file_browser(void)
{
    editor_set_mode(EDITOR_MODE_NORMAL);
    refresh_file_list();
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
    if (s_lbl_status) lv_label_set_text(s_lbl_status, msg);
}

/* ---- Menu system ---- */

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
    uint32_t count = lv_obj_get_child_count(s_menu_list);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *child = lv_obj_get_child(s_menu_list, i);
        if ((int)i == s_menu_sel) {
            lv_obj_set_style_bg_color(child, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(child, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(child, lv_color_white(), 0);
        } else {
            lv_obj_set_style_bg_opa(child, LV_OPA_TRANSP, 0);
            lv_obj_set_style_text_color(child, lv_color_black(), 0);
        }
    }
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

    /* 1: Sleep now */
    lv_list_add_btn(s_settings_list, NULL, "Sleep now");

    /* 2: Back */
    lv_list_add_btn(s_settings_list, NULL, "Back (Esc)");

    /* Highlight selection */
    uint32_t count = lv_obj_get_child_count(s_settings_list);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *child = lv_obj_get_child(s_settings_list, i);
        if ((int)i == s_settings_sel) {
            lv_obj_set_style_bg_color(child, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(child, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(child, lv_color_white(), 0);
        } else {
            lv_obj_set_style_bg_opa(child, LV_OPA_TRANSP, 0);
            lv_obj_set_style_text_color(child, lv_color_black(), 0);
        }
    }
}

static void show_settings(void)
{
    s_settings_open = true;
    s_menu_open = false;
    s_settings_sel = 0;
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
    case 1:
        /* Sleep now -- auto-save first */
        if (editor_get_mode() == EDITOR_MODE_EDITING && editor_is_modified()) {
            editor_save_file();
        }
        standby_enter_sleep();
        break;
    case 2:
        close_settings();
        break;
    default:
        break;
    }
}

#define SETTINGS_ITEM_COUNT 3

static void handle_settings_key(const kb_event_t *ev)
{
    switch (ev->keycode) {
    case KB_KEY_UP:
        if (s_settings_sel > 0) s_settings_sel--;
        refresh_settings_items();
        break;
    case KB_KEY_DOWN:
        if (s_settings_sel < SETTINGS_ITEM_COUNT - 1) s_settings_sel++;
        refresh_settings_items();
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
            wifi_manager_connect();
        }
        break;
    case 3: /* WiFi disconnect */
        wifi_manager_disconnect();
        editor_ui_set_status("WiFi: disconnected");
        close_menu();
        break;
    case 4: /* Git sync */
        if (git_sync_is_configured() && wifi_manager_is_connected()) {
            editor_ui_set_status("Git: syncing...");
            close_menu();
            git_sync_start(GIT_SYNC_BOTH);
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
        refresh_menu_items();
        break;
    case KB_KEY_DOWN:
        if (s_menu_sel < MENU_ITEM_COUNT - 1) s_menu_sel++;
        refresh_menu_items();
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

/* ---- Keyboard handler (registered as BLE callback) ---- */

static void handle_editor_key(const kb_event_t *ev)
{
    bool ctrl = (ev->modifier & (KB_MOD_LCTRL | KB_MOD_RCTRL)) != 0;

    /* F1 opens the menu */
    if (ev->keycode == KB_KEY_F1) {
        show_menu();
        return;
    }

    /* Ctrl shortcuts */
    if (ctrl) {
        switch (ev->character) {
        case 's': editor_save_file(); editor_ui_set_status("Saved"); break;
        case 'n': editor_new_file(); break;
        case 'o': editor_ui_show_file_browser(); return;
        case 'g':
            if (git_sync_is_configured() && wifi_manager_is_connected()) {
                editor_ui_set_status("Git: syncing...");
                git_sync_start(GIT_SYNC_BOTH);
            } else {
                editor_ui_set_status("Git: connect WiFi first (F1)");
            }
            break;
        case 'w':
            if (!wifi_manager_is_connected()) {
                editor_ui_set_status("WiFi: connecting...");
                wifi_manager_connect();
            } else {
                editor_ui_set_status("WiFi: already connected");
            }
            break;
        case 'l':
            /* Ctrl+L: cycle keyboard layout */
            kb_layout_next();
            break;
        default: break;
        }
        /* Ctrl+arrow for word movement */
        if (ev->keycode == KB_KEY_LEFT)  { editor_move_word_left();  }
        if (ev->keycode == KB_KEY_RIGHT) { editor_move_word_right(); }
        if (ev->keycode == KB_KEY_HOME)  { editor_move_doc_start();  }
        if (ev->keycode == KB_KEY_END)   { editor_move_doc_end();    }
        ensure_cursor_visible();
        editor_ui_refresh();
        return;
    }

    /* Special keys */
    switch (ev->keycode) {
    case KB_KEY_LEFT:      editor_move_left();  break;
    case KB_KEY_RIGHT:     editor_move_right(); break;
    case KB_KEY_UP:        editor_move_up();    break;
    case KB_KEY_DOWN:      editor_move_down();  break;
    case KB_KEY_HOME:      editor_move_home();  break;
    case KB_KEY_END:       editor_move_end();   break;
    case KB_KEY_PAGEUP:    editor_move_page_up(VISIBLE_LINES);   break;
    case KB_KEY_PAGEDOWN:  editor_move_page_down(VISIBLE_LINES); break;
    case KB_KEY_BACKSPACE: editor_delete_back();    break;
    case KB_KEY_DELETE:    editor_delete_forward(); break;
    case KB_KEY_ENTER:     editor_insert_newline(); break;
    case KB_KEY_TAB:       editor_insert_text("    ", 4); break;
    case KB_KEY_ESCAPE:    editor_ui_show_file_browser(); return;
    default: {
        /* Use keyboard layout to translate keycode to UTF-8 */
        const char *text = kb_layout_translate(ev->keycode, ev->modifier);
        if (text) {
            editor_insert_text(text, strlen(text));
        }
        break;
    }
    }

    ensure_cursor_visible();
    editor_ui_refresh();
}

static void handle_browser_key(const kb_event_t *ev)
{
    /* F1 opens the menu from browser too */
    if (ev->keycode == KB_KEY_F1) {
        show_menu();
        return;
    }

    uint32_t child_count = lv_obj_get_child_count(s_list_files);
    if (child_count == 0) {
        if (ev->character == 'n') {
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
        if (ev->character == 'n') {
            editor_new_file();
            editor_ui_show_editor();
            return;
        }
        break;
    }

    /* Highlight selected item */
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(s_list_files, i);
        if ((int)i == s_browser_sel) {
            lv_obj_set_style_bg_color(child, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(child, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(child, lv_color_white(), 0);
        } else {
            lv_obj_set_style_bg_opa(child, LV_OPA_TRANSP, 0);
            lv_obj_set_style_text_color(child, lv_color_black(), 0);
        }
    }
}

extern "C" void editor_ui_handle_key(const void *event)
{
    const kb_event_t *ev = (const kb_event_t *)event;
    if (!ev->pressed) return; /* only handle key-down */

    /* Reset standby inactivity timer on key-down */
    standby_reset_timer();

    if (!lvgl_port_lock(100)) return;

    if (s_settings_open) {
        handle_settings_key(ev);
    } else if (s_menu_open) {
        handle_menu_key(ev);
    } else if (editor_get_mode() == EDITOR_MODE_EDITING) {
        handle_editor_key(ev);
    } else {
        handle_browser_key(ev);
    }

    lvgl_port_unlock();
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

/* ---- Initialization ---- */

extern "C" void editor_ui_init(void)
{
    init_styles();

    /* Editor init */
    editor_init();

    /* ---- Editor screen ---- */
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_white(), 0);

    /* Title bar */
    s_lbl_title = lv_label_create(s_scr);
    lv_obj_set_pos(s_lbl_title, 2, 0);
    lv_obj_set_width(s_lbl_title, SCR_W - 4);
    lv_obj_set_style_text_font(s_lbl_title, FONT_10, 0);
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

    /* Logo image shown when no file is open */
    s_img_logo = lv_image_create(s_cont_edit);
    lv_image_set_src(s_img_logo, &draftling_logo);
    lv_obj_set_pos(s_img_logo,
                   (SCR_W - draftling_logo.header.w) / 2,
                   (EDITOR_H - draftling_logo.header.h) / 2);
    lv_obj_add_flag(s_img_logo, LV_OBJ_FLAG_HIDDEN);

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
    lv_obj_set_style_text_font(s_lbl_status, FONT_10, 0);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_black(), 0);
    lv_label_set_text(s_lbl_status,
        "F1:Menu Ctrl+S:Save Ctrl+L:Layout Ctrl+G:Git Esc:Files");

    /* Cursor blink timer */
    s_blink_timer = lv_timer_create(cursor_blink_cb, 500, NULL);

    /* ---- File browser screen ---- */
    s_scr_browser = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_browser, lv_color_white(), 0);

    lv_obj_t *br_title = lv_label_create(s_scr_browser);
    lv_obj_set_pos(br_title, 2, 0);
    lv_obj_set_style_text_font(br_title, FONT_12, 0);
    lv_obj_set_style_text_color(br_title, lv_color_black(), 0);
    lv_label_set_text(br_title, "File Browser - Up/Down, Enter to open, N for new");

    s_list_files = lv_list_create(s_scr_browser);
    lv_obj_set_pos(s_list_files, 0, 18);
    lv_obj_set_size(s_list_files, SCR_W, LIST_H);
    lv_obj_set_style_border_width(s_list_files, 0, 0);
    lv_obj_set_style_radius(s_list_files, 0, 0);
    lv_obj_set_style_pad_all(s_list_files, 0, 0);

    /* Register keyboard callback */
    ble_keyboard_set_callback((kb_event_callback_t)editor_ui_handle_key);

    /* ---- Menu screen ---- */
    s_scr_menu = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_menu, lv_color_white(), 0);

    s_lbl_menu_hdr = lv_label_create(s_scr_menu);
    lv_obj_set_pos(s_lbl_menu_hdr, 2, 0);
    lv_obj_set_style_text_font(s_lbl_menu_hdr, FONT_12, 0);
    lv_obj_set_style_text_color(s_lbl_menu_hdr, lv_color_black(), 0);
    lv_label_set_text(s_lbl_menu_hdr,
                      "Menu - Up/Down, Enter to select, Esc to close");

    s_menu_list = lv_list_create(s_scr_menu);
    lv_obj_set_pos(s_menu_list, 0, 18);
    lv_obj_set_size(s_menu_list, SCR_W, LIST_H);
    lv_obj_set_style_border_width(s_menu_list, 0, 0);
    lv_obj_set_style_radius(s_menu_list, 0, 0);
    lv_obj_set_style_pad_all(s_menu_list, 0, 0);

    /* ---- Settings screen ---- */
    s_scr_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_settings, lv_color_white(), 0);

    lv_obj_t *set_hdr = lv_label_create(s_scr_settings);
    lv_obj_set_pos(set_hdr, 2, 0);
    lv_obj_set_style_text_font(set_hdr, FONT_12, 0);
    lv_obj_set_style_text_color(set_hdr, lv_color_black(), 0);
    lv_label_set_text(set_hdr,
                      "Settings - Up/Down, Enter to change, Esc to go back");

    s_settings_list = lv_list_create(s_scr_settings);
    lv_obj_set_pos(s_settings_list, 0, 18);
    lv_obj_set_size(s_settings_list, SCR_W, LIST_H);
    lv_obj_set_style_border_width(s_settings_list, 0, 0);
    lv_obj_set_style_radius(s_settings_list, 0, 0);
    lv_obj_set_style_pad_all(s_settings_list, 0, 0);

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

    /* Start on file browser */
    editor_ui_show_file_browser();

    ESP_LOGI(TAG, "Editor UI initialized");
}
