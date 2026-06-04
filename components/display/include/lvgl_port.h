#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

void draftling_lvgl_port_init(int width, int height, int rotate_deg);
bool draftling_lvgl_port_lock(int timeout_ms);
void draftling_lvgl_port_unlock(void);

#ifdef __cplusplus
}
#endif
