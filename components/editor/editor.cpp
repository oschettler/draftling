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
static int s_sel_anchor   = -1;   /* logical byte offset, -1 = no selection */
static char *s_clipboard  = NULL;
static size_t s_clip_len  = 0;

/* Cached line count of the flattened text. -1 means "not computed".
 * Invalidated alongside the flat buffer in invalidate_flat(). */
static int s_line_count_cache = -1;

/* Forward-only line-lookup cache used by editor_get_line().
 * Stores the byte offset into s_flat where line s_line_cache_num starts.
 * editor_ui_refresh() iterates lines in increasing order, so this
 * lets each refresh walk the flat buffer at most once instead of
 * O(N^2). Invalidated alongside the flat buffer in invalidate_flat(). */
static int    s_line_cache_num    = -1;
static size_t s_line_cache_offset = 0;

/* Content length (excluding gap) */
static inline size_t content_len(void) { return s_buf_size - (s_gap_end - s_gap_start); }

/* Character at logical position p (0..content_len-1) */
static inline char char_at(size_t p)
{
    return (p < s_gap_start) ? s_buf[p] : s_buf[p + (s_gap_end - s_gap_start)];
}

static void invalidate_flat(void)
{
    s_flat_dirty        = true;
    s_line_count_cache  = -1;
    s_line_cache_num    = -1;
    s_line_cache_offset = 0;
}

static void ensure_gap(size_t need)
{
    size_t gap_sz = s_gap_end - s_gap_start;
    if (gap_sz >= need) return;
    /* Should not happen with 256K buffer and reasonable docs */
    ESP_LOGE(TAG, "Gap buffer full");
}

extern "C" void editor_init(void)
{
    /* Idempotent: the gap buffer and flat cache are large PSRAM
     * allocations and should be made exactly once for the lifetime
     * of the process. Subsequent calls (e.g. from the file-browser
     * "open" handler) only reset the in-memory document state. */
    if (s_buf == NULL) {
        s_buf_size = EDITOR_MAX_DOC_SIZE;
        s_buf  = (char *)heap_caps_malloc(s_buf_size, MALLOC_CAP_SPIRAM);
        s_flat = (char *)heap_caps_malloc(s_buf_size + 1, MALLOC_CAP_SPIRAM);
        assert(s_buf && s_flat);
        ESP_LOGI(TAG, "Editor initialized (%u KB buffer, PSRAM)",
                 (unsigned)(s_buf_size / 1024));
    }
    s_gap_start = 0;
    s_gap_end   = s_buf_size;
    s_modified  = false;
    s_path[0]   = '\0';
    invalidate_flat();
}

/* ---- Per-file metadata sidecar ----
 *
 * Each opened .md file gets a tiny key=value text sidecar saved next
 * to it that records the cursor position and visible scroll line so
 * the editor can resume exactly where the user left off. The sidecar
 * file name is derived by prefixing the basename with a dot and
 * appending ".meta" -- e.g. /sdcard/notes.md -> /sdcard/.notes.md.meta.
 *
 * The leading dot keeps the sidecar hidden from sd_card_list_dir()
 * (which already filters dotfiles), and the .meta extension keeps it
 * out of the git_sync push/pull (which only matches *.md). The format
 * is line-oriented "key=value" so future metadata fields can be
 * appended without breaking older sidecars.
 */
static void meta_path_for(const char *file_path, char *out, size_t out_sz)
{
    if (!file_path || !out || out_sz == 0) { if (out_sz) out[0] = '\0'; return; }
    const char *slash = strrchr(file_path, '/');
    if (slash) {
        size_t dir_len = (size_t)(slash - file_path) + 1; /* include '/' */
        if (dir_len + 1 + strlen(slash + 1) + 5 + 1 > out_sz) {
            out[0] = '\0';
            return;
        }
        memcpy(out, file_path, dir_len);
        snprintf(out + dir_len, out_sz - dir_len, ".%s.meta", slash + 1);
    } else {
        snprintf(out, out_sz, ".%s.meta", file_path);
    }
}

static void editor_load_meta(void)
{
    if (s_path[0] == '\0') return;
    char meta[320];
    meta_path_for(s_path, meta, sizeof(meta));
    if (meta[0] == '\0') return;
    if (!sd_card_file_exists(meta)) return;

    char *data = NULL;
    size_t len = 0;
    if (sd_card_read_file(meta, &data, &len) != ESP_OK || !data) return;

    long cursor = -1;
    long scroll = -1;
    /* Parse line-oriented key=value pairs in-place. */
    size_t i = 0;
    while (i < len) {
        size_t line_start = i;
        while (i < len && data[i] != '\n' && data[i] != '\r') i++;
        size_t line_end = i;
        while (i < len && (data[i] == '\n' || data[i] == '\r')) i++;
        if (line_end == line_start) continue;
        /* Find '=' separator within the line. */
        size_t eq = line_start;
        while (eq < line_end && data[eq] != '=') eq++;
        if (eq >= line_end) continue;
        size_t key_len = eq - line_start;
        const char *val = data + eq + 1;
        size_t val_len = line_end - eq - 1;
        char vbuf[32];
        if (val_len >= sizeof(vbuf)) val_len = sizeof(vbuf) - 1;
        memcpy(vbuf, val, val_len);
        vbuf[val_len] = '\0';
        if (key_len == 6 && memcmp(data + line_start, "cursor", 6) == 0) {
            cursor = strtol(vbuf, NULL, 10);
        } else if (key_len == 6 && memcmp(data + line_start, "scroll", 6) == 0) {
            scroll = strtol(vbuf, NULL, 10);
        }
    }
    free(data);

    size_t doc_len = content_len();
    if (cursor >= 0) {
        if ((size_t)cursor > doc_len) cursor = (long)doc_len;
        editor_set_cursor((size_t)cursor);
    }
    if (scroll >= 0) {
        /* Clamp against the current document line count in case the
         * file shrank since the sidecar was written (e.g. via a
         * git pull). */
        int lc = editor_get_line_count();
        if (lc <= 0) lc = 1;
        if (scroll > lc - 1) scroll = lc - 1;
        s_scroll_line = (int)scroll;
    }
}

extern "C" void editor_save_meta(void)
{
    if (s_path[0] == '\0') return;
    char meta[320];
    meta_path_for(s_path, meta, sizeof(meta));
    if (meta[0] == '\0') return;

    char body[96];
    int n = snprintf(body, sizeof(body),
                     "cursor=%zu\nscroll=%d\n",
                     s_gap_start, s_scroll_line);
    if (n <= 0) return;
    if ((size_t)n >= sizeof(body)) n = (int)sizeof(body) - 1;
    sd_card_write_file(meta, body, (size_t)n);
}

extern "C" esp_err_t editor_open_file(const char *path)
{
    /* Cheap size pre-check: avoid loading hundreds of KB into a
     * temporary heap allocation just to discover that the file does
     * not fit in the editor buffer. */
    long fsize = sd_card_file_size(path);
    if (fsize >= 0 && (size_t)fsize > s_buf_size - 64) {
        ESP_LOGW(TAG, "Refusing to open %s: %ld bytes > editor buffer (%u KB)",
                 path, fsize, (unsigned)(s_buf_size / 1024));
        return ESP_ERR_NO_MEM;
    }

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
    s_sel_anchor = -1;
    invalidate_flat();
    s_mode = EDITOR_MODE_EDITING;

    /* Restore cursor position and scroll line from the metadata
     * sidecar (no-op if the file was never opened before). */
    editor_load_meta();

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
    s_sel_anchor = -1;
    invalidate_flat();
    s_mode = EDITOR_MODE_EDITING;
}

extern "C" void editor_close_file(void)
{
    /* Persist cursor/scroll metadata before we drop the path so the
     * next open can resume at the same position. */
    editor_save_meta();
    s_gap_start = 0;
    s_gap_end   = s_buf_size;
    s_path[0]   = '\0';
    s_modified  = false;
    s_scroll_line = 0;
    s_sel_anchor = -1;
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
    if (s_line_count_cache >= 0) return s_line_count_cache;
    /* Use the flat buffer (single contiguous scan, no gap branch per
     * byte) instead of char_at() so this is fast even for large
     * documents. editor_get_text() also rebuilds the flat buffer if
     * needed, keeping this and editor_get_line() in sync. */
    size_t total;
    const char *text = editor_get_text(&total);
    int count = 1;
    for (size_t i = 0; i < total; i++) {
        if (text[i] == '\n') count++;
    }
    s_line_count_cache = count;
    return count;
}

extern "C" const char *editor_get_line(int line_num, size_t *out_len)
{
    /* Flatten first, then find the line in the flat buffer */
    size_t total;
    const char *text = editor_get_text(&total);

    /* Forward-only cache: if the requested line is at or after the
     * cached line, walk forward from the cached offset instead of
     * restarting from byte 0. The renderer iterates lines in order,
     * so this collapses the per-refresh cost from O(N^2) to O(N). */
    const char *p;
    int cur;
    if (line_num >= s_line_cache_num && s_line_cache_num >= 0 &&
        s_line_cache_offset <= total) {
        p   = text + s_line_cache_offset;
        cur = s_line_cache_num;
    } else {
        p   = text;
        cur = 0;
    }
    while (cur < line_num && p < text + total) {
        if (*p == '\n') cur++;
        p++;
    }
    if (cur < line_num) { if (out_len) *out_len = 0; return ""; }
    /* Update the cache to the line we just landed on. */
    s_line_cache_num    = line_num;
    s_line_cache_offset = (size_t)(p - text);
    const char *end = p;
    while (end < text + total && *end != '\n') end++;
    if (out_len) *out_len = end - p;
    return p;
}

extern "C" int editor_get_scroll_line(void) { return s_scroll_line; }
extern "C" void editor_set_scroll_line(int line) { s_scroll_line = line < 0 ? 0 : line; }

/* ---- Cursor movement ---- */

/* Byte-level gap shifts (internal primitives). */
static void gap_shift_left(void)
{
    s_gap_end--;
    s_buf[s_gap_end] = s_buf[s_gap_start - 1];
    s_gap_start--;
    invalidate_flat();
}

static void gap_shift_right(void)
{
    s_buf[s_gap_start] = s_buf[s_gap_end];
    s_gap_start++;
    s_gap_end++;
    invalidate_flat();
}

/* Move the gap to a given logical position using memmove (faster than
 * byte-by-byte shifting for large distances). */
static void move_gap_to(size_t pos)
{
    if (pos == s_gap_start) return;
    if (pos < s_gap_start) {
        size_t n = s_gap_start - pos;
        memmove(s_buf + s_gap_end - n, s_buf + pos, n);
        s_gap_start = pos;
        s_gap_end -= n;
    } else {
        size_t n = pos - s_gap_start;
        memmove(s_buf + s_gap_start, s_buf + s_gap_end, n);
        s_gap_start += n;
        s_gap_end += n;
    }
    invalidate_flat();
}

/* Count UTF-8 characters in s_buf[start..end) (before the gap). */
static int utf8_char_count(size_t start, size_t end)
{
    int n = 0;
    for (size_t i = start; i < end; i++) {
        if ((s_buf[i] & 0xC0) != 0x80) n++;
    }
    return n;
}

extern "C" void editor_move_left(void)
{
    if (s_gap_start == 0) return;
    /* Move back one byte, then keep going while we are on a
     * UTF-8 continuation byte (10xxxxxx) to land on the start
     * byte of the previous character. */
    do {
        gap_shift_left();
    } while (s_gap_start > 0 && (s_buf[s_gap_start] & 0xC0) == 0x80);
}

extern "C" void editor_move_right(void)
{
    if (s_gap_end >= s_buf_size) return;
    /* Move forward one byte, then skip any continuation bytes
     * so we land at the start of the next character. */
    gap_shift_right();
    while (s_gap_end < s_buf_size && (s_buf[s_gap_end] & 0xC0) == 0x80)
        gap_shift_right();
}

static size_t find_line_start(size_t pos)
{
    if (pos == 0) return 0;
    size_t i = pos - 1;
    while (i > 0 && s_buf[i] != '\n') i--;
    return (s_buf[i] == '\n') ? i + 1 : 0;
}

extern "C" void editor_move_up(void)
{
    size_t line_start = find_line_start(s_gap_start);
    if (line_start == 0) return; /* already on first line */

    /* Remember the cursor column in UTF-8 characters */
    int col_chars = utf8_char_count(line_start, s_gap_start);

    /* Move to beginning of current line (byte-level) */
    while (s_gap_start > line_start) gap_shift_left();

    /* Step past the '\n' onto the end of the previous line */
    gap_shift_left();

    /* Find the start of the previous line and move there */
    size_t prev_start = find_line_start(s_gap_start);
    while (s_gap_start > prev_start) gap_shift_left();

    /* Advance by col_chars characters (or to end of line) */
    for (int i = 0; i < col_chars; i++) {
        if (s_gap_end >= s_buf_size || s_buf[s_gap_end] == '\n') break;
        editor_move_right();
    }
}

extern "C" void editor_move_down(void)
{
    size_t line_start = find_line_start(s_gap_start);

    /* Remember the cursor column in UTF-8 characters */
    int col_chars = utf8_char_count(line_start, s_gap_start);

    /* Move to end of current line (byte-level) */
    while (s_gap_end < s_buf_size && s_buf[s_gap_end] != '\n')
        gap_shift_right();

    /* If at end of buffer, there is no next line */
    if (s_gap_end >= s_buf_size) return;

    /* Skip the '\n' to reach the start of the next line */
    gap_shift_right();

    /* Advance by col_chars characters (or to end of line) */
    for (int i = 0; i < col_chars; i++) {
        if (s_gap_end >= s_buf_size || s_buf[s_gap_end] == '\n') break;
        editor_move_right();
    }
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

extern "C" bool editor_insert_char(char c)
{
    /* Reserve a small safety margin so we never completely fill the
     * buffer (matches editor_open_file() / editor_replace_range()). */
    if (content_len() + 1 > s_buf_size - 64) {
        ESP_LOGW(TAG, "Editor buffer full (%u KB), rejecting insert",
                 (unsigned)(s_buf_size / 1024));
        return false;
    }
    ensure_gap(1);
    if (s_gap_start >= s_gap_end) return false;
    s_buf[s_gap_start++] = c;
    s_modified = true;
    invalidate_flat();
    return true;
}

extern "C" bool editor_insert_text(const char *text, size_t len)
{
    if (!text || len == 0) return true;
    if (content_len() + len > s_buf_size - 64) {
        ESP_LOGW(TAG, "Editor buffer full (%u KB), rejecting %zu-byte insert",
                 (unsigned)(s_buf_size / 1024), len);
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (!editor_insert_char(text[i])) return false;
    }
    return true;
}

extern "C" bool editor_insert_newline(void)
{
    return editor_insert_char('\n');
}

extern "C" void editor_delete_back(void)
{
    if (s_gap_start == 0) return;
    /* Remove all bytes of the previous UTF-8 character by expanding
     * the gap leftward past continuation bytes and the start byte. */
    do {
        s_gap_start--;
    } while (s_gap_start > 0 && (s_buf[s_gap_start] & 0xC0) == 0x80);
    s_modified = true;
    invalidate_flat();
}

extern "C" void editor_delete_forward(void)
{
    if (s_gap_end >= s_buf_size) return;
    /* Remove the start byte and any following continuation bytes. */
    s_gap_end++;
    while (s_gap_end < s_buf_size && (s_buf[s_gap_end] & 0xC0) == 0x80)
        s_gap_end++;
    s_modified = true;
    invalidate_flat();
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

extern "C" void editor_set_cursor(size_t pos)
{
    size_t len = content_len();
    if (pos > len) pos = len;
    move_gap_to(pos);
}

/* ---- Find / Replace ----
 *
 * Forward case-sensitive substring search starting at byte offset
 * from_pos in the flat text. Returns the byte offset of the first
 * match at or after from_pos, or -1 when nothing matches. An empty
 * needle is treated as "no match" so callers can safely loop on the
 * return value.
 */
extern "C" int editor_find(const char *needle, size_t from_pos)
{
    if (!needle || needle[0] == '\0') return -1;
    size_t total;
    const char *text = editor_get_text(&total);
    size_t nlen = strlen(needle);
    if (nlen > total) return -1;
    if (from_pos > total - nlen) return -1;
    /* Plain memmem-style scan over the flat buffer. */
    for (size_t i = from_pos; i + nlen <= total; i++) {
        if (memcmp(text + i, needle, nlen) == 0) return (int)i;
    }
    return -1;
}

/* Replace the byte range [start, end) with the supplied replacement
 * text. The cursor is left positioned at the end of the replacement,
 * any active selection is cleared, and the document is marked
 * modified. Returns ESP_OK on success or ESP_ERR_NO_MEM if the
 * replacement would overflow the gap buffer. */
extern "C" esp_err_t editor_replace_range(size_t start, size_t end,
                                          const char *replacement)
{
    size_t len = content_len();
    if (start > len) start = len;
    if (end > len) end = len;
    if (start > end) { size_t t = start; start = end; end = t; }

    size_t rlen = replacement ? strlen(replacement) : 0;
    size_t old_span = end - start;
    /* New content size must fit in the buffer with the editor's usual
     * 64-byte safety margin (matches editor_open_file()). */
    if (len - old_span + rlen > s_buf_size - 64) return ESP_ERR_NO_MEM;

    /* Delete the old range, then insert the replacement at the same
     * position. Going through the existing helpers preserves all
     * gap/cache invariants. */
    move_gap_to(end);
    s_gap_start = start;
    s_sel_anchor = -1;
    if (rlen > 0) {
        ensure_gap(rlen);
        memcpy(s_buf + s_gap_start, replacement, rlen);
        s_gap_start += rlen;
    }
    s_modified = true;
    invalidate_flat();
    return ESP_OK;
}

/* ---- Selection ---- */

extern "C" bool editor_selection_active(void)
{
    return s_sel_anchor >= 0 && (size_t)s_sel_anchor != s_gap_start;
}

extern "C" void editor_set_selection_anchor(void)
{
    if (s_sel_anchor < 0) s_sel_anchor = (int)s_gap_start;
}

extern "C" void editor_clear_selection(void)
{
    s_sel_anchor = -1;
}

extern "C" void editor_get_selection_range(size_t *start, size_t *end)
{
    if (!editor_selection_active()) {
        if (start) *start = s_gap_start;
        if (end)   *end   = s_gap_start;
        return;
    }
    size_t a = (size_t)s_sel_anchor;
    size_t c = s_gap_start;
    if (start) *start = (a < c) ? a : c;
    if (end)   *end   = (a > c) ? a : c;
}

extern "C" bool editor_delete_selection(void)
{
    if (!editor_selection_active()) return false;
    size_t sel_s, sel_e;
    editor_get_selection_range(&sel_s, &sel_e);
    move_gap_to(sel_e);
    s_gap_start = sel_s;
    s_sel_anchor = -1;
    s_modified = true;
    invalidate_flat();
    return true;
}

extern "C" void editor_select_all(void)
{
    s_sel_anchor = 0;
    move_gap_to(content_len());
}

/* ---- Clipboard ---- */

extern "C" bool editor_copy(void)
{
    if (!editor_selection_active()) return false;
    size_t sel_s, sel_e;
    editor_get_selection_range(&sel_s, &sel_e);
    size_t len = sel_e - sel_s;

    const char *text = editor_get_text(NULL);

    if (s_clipboard) { free(s_clipboard); s_clipboard = NULL; s_clip_len = 0; }
    s_clipboard = (char *)heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM);
    if (!s_clipboard) return false;
    memcpy(s_clipboard, text + sel_s, len);
    s_clipboard[len] = '\0';
    s_clip_len = len;
    return true;
}

extern "C" bool editor_cut(void)
{
    if (!editor_copy()) return false;
    editor_delete_selection();
    return true;
}

extern "C" bool editor_paste(void)
{
    if (!s_clipboard || s_clip_len == 0) return true;
    editor_delete_selection();
    return editor_insert_text(s_clipboard, s_clip_len);
}

extern "C" bool editor_has_clipboard(void)
{
    return s_clipboard != NULL && s_clip_len > 0;
}

extern "C" editor_mode_t editor_get_mode(void) { return s_mode; }
extern "C" void editor_set_mode(editor_mode_t mode) { s_mode = mode; }
