#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void display_init(int mosi, int sck, int dc, int cs, int rst, int width, int height);
void display_clear(uint8_t color);
void display_set_pixel(uint16_t x, uint16_t y, uint8_t color);
void display_flush(void);
uint8_t *display_get_buffer(void);
int display_get_buffer_size(void);

#ifdef __cplusplus
}
#endif
