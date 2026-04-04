#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Default standby timeout in seconds (10 minutes). 0 = disabled. */
#define STANDBY_DEFAULT_TIMEOUT_SEC  600

/*
 * Initialize standby manager.
 * Loads timeout from NVS and starts the inactivity timer.
 * Must be called after nvs_flash_init().
 */
void standby_init(void);

/*
 * Reset the inactivity timer.
 * Call this on every keyboard event or user activity.
 */
void standby_reset_timer(void);

/*
 * Get/set the standby timeout in seconds.  0 = disabled.
 * set persists the value in NVS.
 */
uint32_t standby_get_timeout(void);
void     standby_set_timeout(uint32_t seconds);

/*
 * Enter deep sleep immediately.  Wakes on GPIO18 low.
 * The editor should save any unsaved work before calling this.
 */
void standby_enter_sleep(void);

#ifdef __cplusplus
}
#endif
