#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <esp_err.h>
#include <stdbool.h>
#include <stddef.h>

/* Maximum size (in bytes) of a single file that git_sync can currently
 * push to GitHub.
 *
 * Not a fixed constant: derived at call time from the SPIRAM that is
 * free *right now*. The GitHub Contents API requires the file to be
 * base64-encoded (~1.34x growth) and wrapped in a JSON body, so the
 * transient peak during a push is roughly 3x the raw size (raw +
 * base64 + cJSON-printed body). We additionally reserve some headroom
 * for the HTTPS / TLS buffers and any other concurrent allocations.
 *
 * The editor uses this same function at editor_init() time to clamp
 * its own per-buffer ceiling, so the editor never produces a document
 * larger than what git_sync can push. Git is the tighter constraint
 * and therefore the determining factor for the maximum file size. */
size_t git_sync_max_file_size(void);

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
