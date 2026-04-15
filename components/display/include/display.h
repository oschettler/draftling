#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*
 * Initialize the display hardware.
 *
 * Parameter mapping depends on the selected hardware model:
 *
 *  Waveshare RLCD:   mosi, sck, dc, cs, rst, width, height
 */
void display_init(int pin_a, int pin_b, int pin_c, int pin_d,
                  int pin_e, int width, int height);

void display_clear(uint8_t color);
void display_set_pixel(uint16_t x, uint16_t y, uint8_t color);
void display_flush(void);
uint8_t *display_get_buffer(void);
int display_get_buffer_size(void);

#ifdef __cplusplus
}
#endif
