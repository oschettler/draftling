#include <cstdio>
#include <cstring>
#include <esp_log.h>
#include "lvgl.h"

#include "editor_ui.h"
#include "editor.h"
#include "md_parser.h"
#include "bt_keyboard.h"
#include "sd_card.h"
#include "lvgl_port.h"

/*
 * Font aliases with fallbacks.
 * montserrat_14 is always available (LVGL default).
 * Other sizes require CONFIG_LV_FONT_MONTSERRAT_XX=y in sdkconfig.
 * If a size was not compiled, fall back to montserrat_14.
 */
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

static const char *TAG = "EditorUI";

/* Layout constants */
#define HEADER_H     16
#define STATUS_H     16
#define EDITOR_Y     HEADER_H
#define EDITOR_H     (300 - HEADER_H - STATUS_H)
#define LINE_H       14
#define VISIBLE_LINES (EDITOR_H / LINE_H)
#define CHAR_W       7   /* approx width of montserrat_12 char */

/* LVGL objects */
static lv_obj_t *s_scr       = NULL;
static lv_obj_t *s_lbl_title = NULL;
static lv_obj_t *s_cont_edit = NULL;
static lv_obj_t *s_lbl_status= NULL;
static lv_obj_t *s_cursor    = NULL;
static lv_obj_t *s_scr_browser = NULL;
static lv_obj_t *s_list_files  = NULL;

static lv_timer_t *s_blink_timer = NULL;
static bool s_cursor_visible = true;
static int  s_browser_sel    = 0;
static int  s_browser_count  = 0;
static sd_card_file_entry_t s_browser_entries[64];

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
    char buf[80];
    snprintf(buf, sizeof(buf), "%s%s  L:%d C:%d",
             name, editor_is_modified() ? " *" : "", line + 1, col + 1);
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
            lv_obj_set_width(s_line_labels[i], 396);
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

/* ---- Keyboard handler (registered as BT callback) ---- */

static void handle_editor_key(const kb_event_t *ev)
{
    bool ctrl = (ev->modifier & (KB_MOD_LCTRL | KB_MOD_RCTRL)) != 0;

    /* Ctrl shortcuts */
    if (ctrl) {
        switch (ev->character) {
        case 's': editor_save_file(); editor_ui_set_status("Saved"); break;
        case 'n': editor_new_file(); break;
        case 'o': editor_ui_show_file_browser(); return;
        case 'g': editor_ui_set_status("Git sync: use Ctrl+G when WiFi connected"); break;
        case 'w': editor_ui_set_status("WiFi: connecting..."); break;
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
    default:
        if (ev->character >= 0x20 && ev->character < 0x7F) {
            editor_insert_char(ev->character);
        }
        break;
    }

    ensure_cursor_visible();
    editor_ui_refresh();
}

static void handle_browser_key(const kb_event_t *ev)
{
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

    if (!lvgl_port_lock(100)) return;

    if (editor_get_mode() == EDITOR_MODE_EDITING)
        handle_editor_key(ev);
    else
        handle_browser_key(ev);

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
    lv_obj_set_width(s_lbl_title, 396);
    lv_obj_set_style_text_font(s_lbl_title, FONT_10, 0);
    lv_obj_set_style_text_color(s_lbl_title, lv_color_black(), 0);
    lv_label_set_text(s_lbl_title, "WriterDeck");

    /* Header separator line */
    lv_obj_t *hline = lv_obj_create(s_scr);
    lv_obj_set_size(hline, 400, 1);
    lv_obj_set_pos(hline, 0, HEADER_H - 1);
    lv_obj_set_style_bg_color(hline, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(hline, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hline, 0, 0);
    lv_obj_set_style_radius(hline, 0, 0);
    lv_obj_set_style_pad_all(hline, 0, 0);

    /* Editor content area */
    s_cont_edit = lv_obj_create(s_scr);
    lv_obj_set_pos(s_cont_edit, 0, EDITOR_Y);
    lv_obj_set_size(s_cont_edit, 400, EDITOR_H);
    lv_obj_set_style_bg_opa(s_cont_edit, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_cont_edit, 0, 0);
    lv_obj_set_style_pad_all(s_cont_edit, 0, 0);
    lv_obj_set_style_radius(s_cont_edit, 0, 0);
    lv_obj_remove_flag(s_cont_edit, LV_OBJ_FLAG_SCROLLABLE);

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
    lv_obj_set_size(sline, 400, 1);
    lv_obj_set_pos(sline, 0, 300 - STATUS_H);
    lv_obj_set_style_bg_color(sline, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(sline, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sline, 0, 0);
    lv_obj_set_style_radius(sline, 0, 0);
    lv_obj_set_style_pad_all(sline, 0, 0);

    s_lbl_status = lv_label_create(s_scr);
    lv_obj_set_pos(s_lbl_status, 2, 300 - STATUS_H + 2);
    lv_obj_set_width(s_lbl_status, 396);
    lv_obj_set_style_text_font(s_lbl_status, FONT_10, 0);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_black(), 0);
    lv_label_set_text(s_lbl_status, "Ctrl+O:Open  Ctrl+S:Save  Ctrl+N:New  Esc:Browser");

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
    lv_obj_set_size(s_list_files, 400, 282);
    lv_obj_set_style_border_width(s_list_files, 0, 0);
    lv_obj_set_style_radius(s_list_files, 0, 0);
    lv_obj_set_style_pad_all(s_list_files, 0, 0);

    /* Register keyboard callback */
    bt_keyboard_set_callback((kb_event_callback_t)editor_ui_handle_key);

    /* Start on file browser */
    editor_ui_show_file_browser();

    ESP_LOGI(TAG, "Editor UI initialized");
}
