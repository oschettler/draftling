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
 * On the M5Stack PaperS3 backend this performs an automatic frame
 * diff against the last displayed frame and uses a partial refresh
 * when only a small region changed, falling back to a full refresh
 * once every N partials to clear residual ghosting (the threshold
 * is configurable via Kconfig). On the RLCD model it always pushes
 * the entire framebuffer.
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

/*
 * AXS15231B QSPI color-LCD driver init.
 *
 * Used by boards with CONFIG_DRAFTLING_DISPLAY_AXS15231B (Waveshare
 * ESP32-S3-Touch-LCD-3.49 and Guition JC3248W535). The driver needs
 * 9 GPIOs -- more than display_init()'s 6 pin slots can carry --
 * so it has its own struct-based init. Call this *instead of*
 * display_init() on AXS15231B boards.
 *
 * After this returns, display_clear/display_set_pixel/
 * display_push_rgb565/display_flush behave like on any other backend.
 */
typedef struct {
    int cs;
    int sck;
    int d0;
    int d1;
    int d2;
    int d3;
    int rst;
    int te;       /* tearing-effect input, -1 if unused */
    int bl;       /* backlight enable, -1 if always-on */
    int width;
    int height;
} display_axs15231b_config_t;

void display_axs15231b_init(const display_axs15231b_config_t *cfg);

#ifdef __cplusplus
}
#endif
