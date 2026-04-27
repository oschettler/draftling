#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>
#include <esp_err.h>

#define EDITOR_MAX_DOC_SIZE  (256 * 1024)

typedef enum {
    EDITOR_MODE_NORMAL,
    EDITOR_MODE_EDITING,
} editor_mode_t;

void editor_init(void);
esp_err_t editor_open_file(const char *path);
esp_err_t editor_save_file(void);
esp_err_t editor_save_file_as(const char *path);
void editor_new_file(void);
void editor_close_file(void);
/* Persist per-file metadata (cursor, scroll line, ...) to the
 * sidecar next to the currently-open file. No-op if no file is
 * open. Safe to call repeatedly. */
void editor_save_meta(void);
const char *editor_get_file_path(void);
bool editor_is_modified(void);
const char *editor_get_text(size_t *out_len);
size_t editor_get_cursor(void);
void editor_get_cursor_pos(int *line, int *col);
int editor_get_line_count(void);
const char *editor_get_line(int line_num, size_t *out_len);
int editor_get_scroll_line(void);
void editor_set_scroll_line(int line);

void editor_move_left(void);
void editor_move_right(void);
void editor_move_up(void);
void editor_move_down(void);
void editor_move_home(void);
void editor_move_end(void);
void editor_move_page_up(int visible_lines);
void editor_move_page_down(int visible_lines);
void editor_move_doc_start(void);
void editor_move_doc_end(void);
void editor_move_word_left(void);
void editor_move_word_right(void);

void editor_insert_char(char c);
void editor_insert_text(const char *text, size_t len);
void editor_insert_newline(void);
void editor_delete_back(void);
void editor_delete_forward(void);
void editor_delete_line(void);

void editor_set_cursor(size_t pos);

/* Find / Replace */
int editor_find(const char *needle, size_t from_pos);
esp_err_t editor_replace_range(size_t start, size_t end, const char *replacement);

/* Selection */
bool editor_selection_active(void);
void editor_set_selection_anchor(void);
void editor_clear_selection(void);
void editor_get_selection_range(size_t *start, size_t *end);
bool editor_delete_selection(void);
void editor_select_all(void);

/* Clipboard */
bool editor_copy(void);
bool editor_cut(void);
void editor_paste(void);
bool editor_has_clipboard(void);

editor_mode_t editor_get_mode(void);
void editor_set_mode(editor_mode_t mode);

#ifdef __cplusplus
}
#endif
