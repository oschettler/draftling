#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void editor_ui_init(void);
void editor_ui_refresh(void);
void editor_ui_handle_key(const void *event);
void editor_ui_show_file_browser(void);
void editor_ui_show_editor(void);
void editor_ui_set_status(const char *msg);

/* Display a persistent (non-auto-clearing) status message. Used for
 * fatal boot-time errors where the firmware halts and the message
 * must remain on screen until the user resets the device. */
void editor_ui_show_fatal(const char *msg);

#ifdef __cplusplus
}
#endif
