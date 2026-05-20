#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <esp_err.h>
#include <stdbool.h>

/* Maximum size of a single file that git_sync can push to GitHub.
 *
 * The Contents API requires us to base64-encode the file (~1.34x growth)
 * and wrap it in a JSON body, so the transient SPIRAM peak is roughly
 * 3x the raw size (raw + base64 + cJSON-printed body).  2 MB covers any
 * realistic Markdown document while leaving headroom for BLE / WiFi /
 * LVGL on an 8 MB PSRAM module.
 *
 * The editor uses this as the upper bound on its gap-buffer size so
 * the in-memory document limit matches what git_sync can actually
 * push -- Git is the tighter constraint and therefore determines the
 * effective maximum file size for the whole application. */
#define GIT_SYNC_MAX_FILE_SIZE (2 * 1024 * 1024)

typedef enum {
    GIT_SYNC_IDLE,
    GIT_SYNC_IN_PROGRESS,
    GIT_SYNC_SUCCESS,
    GIT_SYNC_ERROR,
} git_sync_state_t;

typedef enum {
    GIT_SYNC_PULL,
    GIT_SYNC_PUSH,
    GIT_SYNC_BOTH,
} git_sync_direction_t;

typedef void (*git_sync_callback_t)(git_sync_state_t state, const char *message);

esp_err_t git_sync_init(void);
esp_err_t git_sync_start(git_sync_direction_t direction);
git_sync_state_t git_sync_get_state(void);
void git_sync_set_callback(git_sync_callback_t callback);
bool git_sync_is_configured(void);
const char *git_sync_get_last_error(void);
const char *git_sync_get_last_sync_time(void);

#ifdef __cplusplus
}
#endif
