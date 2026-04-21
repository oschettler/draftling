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
 *  Waveshare RLCD:           mosi, sck, dc, cs, rst, width, height
 *  Seeed reTerminal E1001:   mosi, sck, dc, cs, rst, width, height
 *                            (BUSY pin is fixed by the board and read
 *                             internally by the UC8179 driver)
 */
void display_init(int pin_a, int pin_b, int pin_c, int pin_d,
                  int pin_e, int width, int height);

void display_clear(uint8_t color);
void display_set_pixel(uint16_t x, uint16_t y, uint8_t color);

/*
 * Push the framebuffer to the panel.
 *
 * On the e-paper model (UC8179) this performs an automatic frame diff
 * against the last displayed frame and uses a partial refresh
 * (~300 ms) when only a small region changed, falling back to a full
 * refresh once every N partials to clear residual ghosting (the
 * threshold is configurable via Kconfig). On the RLCD model it always
 * pushes the entire framebuffer.
 */
void display_flush(void);

/*
 * Force a full refresh that clears any accumulated ghosting on the
 * e-paper. On the RLCD model this is equivalent to display_flush().
 */
void display_full_refresh(void);

uint8_t *display_get_buffer(void);
int display_get_buffer_size(void);

#ifdef __cplusplus
}
#endif
