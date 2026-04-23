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
 *  Waveshare RLCD (display_rlcd.cpp):
 *      mosi, sck, dc, cs, rst, busy=-1, width, height
 *
 *  UC8179 e-paper (display_uc8179.cpp) - covers both the Seeed
 *  reTerminal E1001 and the Waveshare E-Paper Driver HAT:
 *      mosi, sck, dc, cs, rst, busy, width, height
 *
 *  M5Stack PaperS3 (display_eds3.cpp):
 *      all pin parameters are ignored - the m5stack/M5GFX library
 *      configures the parallel-bus and control GPIOs internally
 *      based on the M5PaperS3 board id. Width/height must still
 *      match the panel (540 x 960).
 */
void display_init(int pin_a, int pin_b, int pin_c, int pin_d,
                  int pin_e, int pin_f, int width, int height);

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

#include <stdbool.h>

/*
 * Optional fast path: push an LVGL RGB565 framebuffer area directly
 * to the panel without going through the per-pixel display_set_pixel
 * conversion. Returns true if the backend handled the push (in which
 * case the caller still needs to call display_flush() to trigger the
 * panel refresh), or false to fall back to the legacy per-pixel
 * path.
 *
 * Implemented by the M5GFX-based PaperS3 backend, which can convert
 * RGB565 -> grayscale internally far more efficiently than our
 * RGB565 -> 1-bit -> 8-bit grayscale round-trip. Other backends
 * return false.
 */
bool display_push_rgb565(int x, int y, int w, int h, const void *color_map);

/*
 * Optional: clip the next display_flush()'s panel refresh region.
 *
 * After this call, the *next* display_flush() refreshes only pixels
 * inside the intersection of its accumulated dirty bounding box and
 * the rectangle (x, y, w, h). The clip is one-shot: it is consumed
 * (and reset) by the next display_flush(), regardless of whether the
 * intersection was empty.
 *
 * The caller must guarantee that no pixels outside the clip rectangle
 * actually changed since the previous flush -- otherwise stale
 * content remains visible on the e-paper. The framebuffer pushed via
 * display_push_rgb565()/display_set_pixel() is always written in
 * full; only the panel refresh is narrowed.
 *
 * Pass w <= 0 or h <= 0 to clear any pending clip.
 *
 * Implemented by the M5Stack PaperS3 backend (display_eds3.cpp). On
 * other backends this is a no-op (the full dirty bbox is refreshed).
 */
void display_set_partial_clip(int x, int y, int w, int h);

#ifdef __cplusplus
}
#endif
