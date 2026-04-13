#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

void lvgl_port_init(int width, int height, int rotate_deg);
bool lvgl_port_lock(int timeout_ms);
void lvgl_port_unlock(void);

#ifdef __cplusplus
}
#endif
