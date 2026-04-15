#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <esp_log.h>
#include <esp_heap_caps.h>

#include "editor.h"
#include "sd_card.h"

static const char *TAG = "Editor";

static char *s_buf      = NULL;   /* gap buffer */
static char *s_flat     = NULL;   /* flattened text cache */
static size_t s_gap_start = 0;
static size_t s_gap_end   = 0;
static size_t s_buf_size  = 0;
static char s_path[256]   = "";
static bool s_modified    = false;
static bool s_flat_dirty  = true;
static editor_mode_t s_mode = EDITOR_MODE_NORMAL;
static int s_scroll_line  = 0;

/* Content length (excluding gap) */
static inline size_t content_len(void) { return s_buf_size - (s_gap_end - s_gap_start); }

/* Character at logical position p (0..content_len-1) */
static inline char char_at(size_t p)
{
    return (p < s_gap_start) ? s_buf[p] : s_buf[p + (s_gap_end - s_gap_start)];
}

static void invalidate_flat(void) { s_flat_dirty = true; }

static void ensure_gap(size_t need)
{
    size_t gap_sz = s_gap_end - s_gap_start;
    if (gap_sz >= need) return;
    /* Should not happen with 256K buffer and reasonable docs */
    ESP_LOGE(TAG, "Gap buffer full");
}

extern "C" void editor_init(void)
{
    s_buf_size = EDITOR_MAX_DOC_SIZE;
    s_buf  = (char *)heap_caps_malloc(s_buf_size, MALLOC_CAP_SPIRAM);
    s_flat = (char *)heap_caps_malloc(s_buf_size + 1, MALLOC_CAP_SPIRAM);
    assert(s_buf && s_flat);
    s_gap_start = 0;
    s_gap_end   = s_buf_size;
    s_modified  = false;
    s_path[0]   = '\0';
    invalidate_flat();
    ESP_LOGI(TAG, "Editor initialized (%d KB buffer)", EDITOR_MAX_DOC_SIZE / 1024);
}

extern "C" esp_err_t editor_open_file(const char *path)
{
    char *data = NULL;
    size_t len = 0;
    esp_err_t ret = sd_card_read_file(path, &data, &len);
    if (ret != ESP_OK) return ret;

    if (len > s_buf_size - 64) { free(data); return ESP_ERR_NO_MEM; }

    memcpy(s_buf, data, len);
    s_gap_start = len;
    s_gap_end   = s_buf_size;
    free(data);

    strncpy(s_path, path, sizeof(s_path) - 1);
    s_path[sizeof(s_path) - 1] = '\0';
    s_modified = false;
    s_scroll_line = 0;
    invalidate_flat();
    s_mode = EDITOR_MODE_EDITING;

    ESP_LOGI(TAG, "Opened: %s (%zu bytes)", path, len);
    return ESP_OK;
}

extern "C" esp_err_t editor_save_file(void)
{
    if (s_path[0] == '\0') return ESP_ERR_INVALID_STATE;
    size_t len;
    const char *text = editor_get_text(&len);
    esp_err_t ret = sd_card_write_file(s_path, text, len);
    if (ret == ESP_OK) { s_modified = false; ESP_LOGI(TAG, "Saved: %s", s_path); }
    return ret;
}

extern "C" esp_err_t editor_save_file_as(const char *path)
{
    strncpy(s_path, path, sizeof(s_path) - 1);
    s_path[sizeof(s_path) - 1] = '\0';
    return editor_save_file();
}

extern "C" void editor_new_file(void)
{
    s_gap_start = 0;
    s_gap_end   = s_buf_size;
    s_path[0]   = '\0';
    s_modified  = false;
    s_scroll_line = 0;
    invalidate_flat();
    s_mode = EDITOR_MODE_EDITING;
}

extern "C" void editor_close_file(void)
{
    s_gap_start = 0;
    s_gap_end   = s_buf_size;
    s_path[0]   = '\0';
    s_modified  = false;
    s_scroll_line = 0;
    invalidate_flat();
    s_mode = EDITOR_MODE_NORMAL;
}

extern "C" const char *editor_get_file_path(void) { return s_path[0] ? s_path : NULL; }
extern "C" bool editor_is_modified(void) { return s_modified; }

extern "C" const char *editor_get_text(size_t *out_len)
{
    if (s_flat_dirty) {
        memcpy(s_flat, s_buf, s_gap_start);
        memcpy(s_flat + s_gap_start, s_buf + s_gap_end, s_buf_size - s_gap_end);
        size_t len = content_len();
        s_flat[len] = '\0';
        s_flat_dirty = false;
    }
    if (out_len) *out_len = content_len();
    return s_flat;
}

extern "C" size_t editor_get_cursor(void) { return s_gap_start; }

extern "C" void editor_get_cursor_pos(int *line, int *col)
{
    int l = 0, c = 0;
    for (size_t i = 0; i < s_gap_start; i++) {
        if (s_buf[i] == '\n') { l++; c = 0; }
        else {
            /* Count only the first byte of each UTF-8 character.
             * Continuation bytes have the form 10xxxxxx (0x80..0xBF). */
            if ((s_buf[i] & 0xC0) != 0x80) c++;
        }
    }
    if (line) *line = l;
    if (col)  *col  = c;
}

extern "C" int editor_get_line_count(void)
{
    int count = 1;
    size_t len = content_len();
    for (size_t i = 0; i < len; i++) {
        if (char_at(i) == '\n') count++;
    }
    return count;
}

extern "C" const char *editor_get_line(int line_num, size_t *out_len)
{
    /* Flatten first, then find the line in the flat buffer */
    size_t total;
    const char *text = editor_get_text(&total);
    const char *p = text;
    int cur = 0;
    while (cur < line_num && p < text + total) {
        if (*p == '\n') cur++;
        p++;
    }
    if (cur < line_num) { if (out_len) *out_len = 0; return ""; }
    const char *end = p;
    while (end < text + total && *end != '\n') end++;
    if (out_len) *out_len = end - p;
    return p;
}

extern "C" int editor_get_scroll_line(void) { return s_scroll_line; }
extern "C" void editor_set_scroll_line(int line) { s_scroll_line = line < 0 ? 0 : line; }

/* ---- Cursor movement ---- */

extern "C" void editor_move_left(void)
{
    if (s_gap_start > 0) {
        s_gap_end--;
        s_buf[s_gap_end] = s_buf[s_gap_start - 1];
        s_gap_start--;
        invalidate_flat();
    }
}

extern "C" void editor_move_right(void)
{
    if (s_gap_end < s_buf_size) {
        s_buf[s_gap_start] = s_buf[s_gap_end];
        s_gap_start++;
        s_gap_end++;
        invalidate_flat();
    }
}

static size_t find_line_start(size_t pos)
{
    if (pos == 0) return 0;
    size_t i = pos - 1;
    while (i > 0 && s_buf[i] != '\n') i--;
    return (s_buf[i] == '\n') ? i + 1 : 0;
}

static size_t find_line_end_in_gap(void)
{
    /* Find end of current line after cursor (in the part after gap) */
    size_t i = s_gap_end;
    while (i < s_buf_size && s_buf[i] != '\n') i++;
    return i;
}

extern "C" void editor_move_up(void)
{
    size_t line_start = find_line_start(s_gap_start);
    int col = (int)(s_gap_start - line_start);
    if (line_start == 0) return; /* already on first line */
    /* Go to previous line */
    size_t prev_end = line_start - 1; /* the '\n' before this line */
    size_t prev_start = (prev_end == 0) ? 0 : find_line_start(prev_end);
    size_t prev_len = prev_end - prev_start;
    size_t target = prev_start + ((size_t)col < prev_len ? (size_t)col : prev_len);
    /* Move cursor to target */
    while (s_gap_start > target) editor_move_left();
    while (s_gap_start < target) editor_move_right();
}

extern "C" void editor_move_down(void)
{
    size_t line_start = find_line_start(s_gap_start);
    int col = (int)(s_gap_start - line_start);
    /* Find next line start (after gap) */
    size_t next_start = find_line_end_in_gap();
    if (next_start >= s_buf_size) return; /* no next line */
    next_start++; /* skip the '\n' */
    /* Find next line length */
    size_t next_end = next_start;
    while (next_end < s_buf_size && s_buf[next_end] != '\n') next_end++;
    size_t next_len = next_end - next_start;
    /* Move cursor right to reach target */
    size_t steps_to_eol = find_line_end_in_gap() - s_gap_end;
    size_t steps_into_next = ((size_t)col < next_len) ? (size_t)col : next_len;
    size_t total_steps = steps_to_eol + 1 + steps_into_next;
    for (size_t i = 0; i < total_steps; i++) editor_move_right();
}

extern "C" void editor_move_home(void)
{
    size_t line_start = find_line_start(s_gap_start);
    while (s_gap_start > line_start) editor_move_left();
}

extern "C" void editor_move_end(void)
{
    while (s_gap_end < s_buf_size && s_buf[s_gap_end] != '\n')
        editor_move_right();
}

extern "C" void editor_move_page_up(int visible_lines)
{
    for (int i = 0; i < visible_lines; i++) editor_move_up();
}

extern "C" void editor_move_page_down(int visible_lines)
{
    for (int i = 0; i < visible_lines; i++) editor_move_down();
}

extern "C" void editor_move_doc_start(void)
{
    while (s_gap_start > 0) editor_move_left();
}

extern "C" void editor_move_doc_end(void)
{
    while (s_gap_end < s_buf_size) editor_move_right();
}

extern "C" void editor_move_word_left(void)
{
    if (s_gap_start == 0) return;
    editor_move_left();
    /* Skip whitespace */
    while (s_gap_start > 0 && (s_buf[s_gap_start - 1] == ' ' || s_buf[s_gap_start - 1] == '\t'))
        editor_move_left();
    /* Skip word chars */
    while (s_gap_start > 0 && s_buf[s_gap_start - 1] != ' ' &&
           s_buf[s_gap_start - 1] != '\t' && s_buf[s_gap_start - 1] != '\n')
        editor_move_left();
}

extern "C" void editor_move_word_right(void)
{
    if (s_gap_end >= s_buf_size) return;
    /* Skip word chars */
    while (s_gap_end < s_buf_size && s_buf[s_gap_end] != ' ' &&
           s_buf[s_gap_end] != '\t' && s_buf[s_gap_end] != '\n')
        editor_move_right();
    /* Skip whitespace */
    while (s_gap_end < s_buf_size && (s_buf[s_gap_end] == ' ' || s_buf[s_gap_end] == '\t'))
        editor_move_right();
}

/* ---- Editing ---- */

extern "C" void editor_insert_char(char c)
{
    ensure_gap(1);
    if (s_gap_start >= s_gap_end) return;
    s_buf[s_gap_start++] = c;
    s_modified = true;
    invalidate_flat();
}

extern "C" void editor_insert_text(const char *text, size_t len)
{
    for (size_t i = 0; i < len; i++) editor_insert_char(text[i]);
}

extern "C" void editor_insert_newline(void)
{
    editor_insert_char('\n');
}

extern "C" void editor_delete_back(void)
{
    if (s_gap_start > 0) {
        s_gap_start--;
        s_modified = true;
        invalidate_flat();
    }
}

extern "C" void editor_delete_forward(void)
{
    if (s_gap_end < s_buf_size) {
        s_gap_end++;
        s_modified = true;
        invalidate_flat();
    }
}

extern "C" void editor_delete_line(void)
{
    editor_move_home();
    size_t end = s_gap_end;
    while (end < s_buf_size && s_buf[end] != '\n') end++;
    if (end < s_buf_size) end++; /* include the newline */
    s_gap_end = end;
    s_modified = true;
    invalidate_flat();
}

extern "C" editor_mode_t editor_get_mode(void) { return s_mode; }
extern "C" void editor_set_mode(editor_mode_t mode) { s_mode = mode; }
