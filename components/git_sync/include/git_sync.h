#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <esp_err.h>
#include <stdbool.h>

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
