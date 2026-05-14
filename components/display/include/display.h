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
    int bl;       /* backlight enable, -1 if always-on / external.
                   * When >= 0 the backend drives this GPIO with LEDC
                   * PWM whose duty is set via display_set_backlight();
                   * the editor's F1 -> Settings menu persists the
                   * brightness in NVS. */
    int width;
    int height;
} display_axs15231b_config_t;

/*
 * Set the LCD backlight brightness.
 *
 * percent is clamped to [0, 100]. 0 = backlight off, 100 = full
 * brightness. The exact PWM frequency / resolution is backend-
 * specific (LEDC PWM at ~5 kHz on the AXS15231B and ST7789
 * backends).
 *
 * No-op on backends that have no controllable backlight
 * (reflective LCD, e-paper). Boards whose displays expose a
 * backlight set CONFIG_DRAFTLING_DISPLAY_HAS_BACKLIGHT in
 * Kconfig.projbuild; the editor uses that flag to decide whether
 * to show a "Backlight" entry in the F1 Settings menu.
 */
void display_set_backlight(int percent);

void display_axs15231b_init(const display_axs15231b_config_t *cfg);

/*
 * ST7789 i80 (8-bit parallel) color-LCD driver init.
 *
 * Used by boards with CONFIG_DRAFTLING_DISPLAY_ST7789 (LilyGO
 * T-Display-S3). The driver needs 14 GPIOs (8 data + WR + DC + CS
 * + RST + (optional) BL + (optional) TE) which do not fit in
 * display_init()'s 6 pin slots, so it has its own struct-based
 * init. Call this *instead of* display_init() on ST7789 boards.
 *
 * The caller is responsible for driving any external power-enable
 * GPIO (e.g. LCD_PWR_EN_PIN on the T-Display-S3) high *before*
 * calling this function.
 *
 * After this returns, display_clear / display_set_pixel /
 * display_push_rgb565 / display_flush behave like on any other
 * backend.
 */
typedef struct {
    int data[8];      /* D0..D7 */
    int wr;           /* PCLK / write strobe */
    int dc;           /* data/command select */
    int cs;
    int rst;
    int bl;           /* backlight enable, -1 if always-on / external.
                       * When >= 0 the backend drives this GPIO with
                       * LEDC PWM whose duty is set via
                       * display_set_backlight(). */
    int width;        /* logical (after rotation) panel width */
    int height;       /* logical (after rotation) panel height */
    int x_gap;        /* column offset on the controller (35 for T-Display-S3 in landscape) */
    int y_gap;        /* row offset on the controller */
    bool swap_xy;     /* true to swap rows/columns (landscape) */
    bool mirror_x;
    bool mirror_y;
    bool invert_color;/* ST7789 panels typically need INVON */
} display_st7789_config_t;

void display_st7789_init(const display_st7789_config_t *cfg);

#ifdef __cplusplus
}
#endif
