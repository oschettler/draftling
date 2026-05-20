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
 * Put the display panel into low-power sleep mode.
 *
 * On color-LCD backends (AXS15231B, ST7789) this turns the backlight
 * off and sends DISPOFF + SLPIN so the controller drops to its
 * sleep current. On e-paper / reflective-LCD / RLCD backends where
 * the panel retains its image without power the call is a no-op.
 *
 * Used by the standby manager's CONFIG_DRAFTLING_STANDBY_DISPLAY_OFF
 * code path, which keeps the MCU running but blanks the display
 * until an input event arrives. Pair with display_wake() to bring
 * the panel back without re-running the full init sequence.
 */
void display_sleep(void);

/*
 * Wake the display panel from display_sleep(). Sends SLPOUT + DISPON
 * on backends that implement display_sleep(), turns the backlight
 * back on, and (where supported) requests a full repaint on the
 * next display_flush() so any framebuffer drift is corrected. No-op
 * on backends with no display_sleep().
 */
void display_wake(void);

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
    /* Software 90-deg-CW rotation. Set to true on boards whose
     * AXS15231B silently ignores the MADCTL MV (swap-XY) bit but
     * whose physical panel is mounted in portrait orientation
     * (native scanout is height x width). When set, the backend
     * keeps its internal framebuffer in logical orientation
     * (width x height) so push/clear/set-pixel paths stay
     * unchanged, and transposes per-row into the DMA scratch
     * buffer at flush time, addressing the panel in its native
     * portrait coordinate space. Known true on Guition
     * JC3248W535; left false on Waveshare Touch-LCD-3.49 whose
     * 640x172 panel is natively landscape and works with
     * MADCTL=0x00 directly. */
    bool swap_xy;
    /* Skip the long AXS15231B vendor-register init block
     * (0xBB unlock + 0xA0/0xA2/0xC1/0xC4..0xE2/... + 0xBB lock).
     *
     * The vendor block our driver sends was hand-copied from the
     * Guition JC3248W535 reference; it is the panel-vendor recipe
     * for that board's 480x320 panel and is required to make it
     * display anything (the JC3248W535 ships with vendor regs at
     * defaults that produce only a brief flash of garbage on
     * display-on). Other AXS15231B boards may ship pre-tuned at
     * their factory POR defaults and do NOT want our JC3248W535
     * recipe written on top -- the Waveshare ESP32-S3-Touch-LCD-3.49
     * is one such board: its 172x640 panel is correct out of POR,
     * and its reference firmware (using the upstream
     * `espressif/esp_lcd_axs15231b` IDF component) supplies only
     * `{SLPOUT, DISPON}` as init commands. Sending the JC3248W535
     * vendor values to that panel clobbers the correct factory
     * defaults with values intended for a different panel geometry,
     * producing a black screen on cold boot until the user presses
     * RESET (warm reset masks the bug because the panel had pixels
     * in internal RAM from the previous run, and the 250 ms RST-LOW
     * pulse does not fully wipe that state on a panel whose VCC
     * has been continuous).
     *
     * Set true on boards whose panel does NOT need our JC3248W535
     * vendor block; leave false (default) on JC3248W535 itself. */
    bool skip_vendor_init;
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
