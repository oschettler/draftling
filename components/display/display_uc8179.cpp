#include "sdkconfig.h"
#if defined(CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001) || \
    defined(CONFIG_DRAFTLING_MODEL_WAVESHARE_EPD_HAT)

/*
 * UC8179 e-paper display driver. Used by both:
 *   - Seeed reTerminal E1001 (800 x 480, fixed pinout)
 *   - Waveshare E-Paper Driver HAT on any BLE-capable ESP32 host
 *     (resolution and every SPI/control pin configurable via Kconfig)
 *
 * 1 bit per pixel, partial-region refresh with periodic full refresh.
 *
 * The driver implements the same public API as the Waveshare RLCD
 * driver so that the LVGL port (lvgl_port.cpp) can stay generic:
 *
 *   display_init(mosi, sck, dc, cs, rst, busy, width, height)
 *   display_clear(color)
 *   display_set_pixel(x, y, color)
 *   display_flush()
 *
 * The SPI host id is fixed (SPI2 / HSPI). The BUSY input is now
 * accepted as an init-time parameter so each board can supply its
 * own pin (the reTerminal E1001 wires it to GPIO13; the HAT default
 * is GPIO9, overridable via menuconfig).
 *
 * Frame buffer layout (UC8179 in B/W mode):
 *   - 1 bit per pixel, 8 horizontally adjacent pixels per byte,
 *     MSB = leftmost pixel, packed row-major.
 *   - White pixel = bit set (1), black pixel = bit clear (0).
 *
 * Refresh strategy: each call to display_flush() compares the new
 * framebuffer against the last frame actually shown and:
 *   - skips the panel cycle entirely if nothing changed;
 *   - performs a partial refresh (~300 ms) over the byte-aligned
 *     bounding box of the diff for small or local changes;
 *   - performs a full refresh (~3 s) every
 *     CONFIG_DRAFTLING_EPD_FULL_REFRESH_INTERVAL partials, or
 *     immediately when the dirty region covers most of the screen.
 * display_full_refresh() is exposed so callers can force a clean,
 * ghost-free repaint on demand (e.g. after opening a new file).
 *
 * Note: the PWR/CDI/PLL constants below are tuned for the Waveshare
 * 7.5" V2 BW panel used by the reTerminal E1001 and the default HAT
 * panel. Smaller panels (4.2", 5.83") may need different values; if
 * you see washed-out or noisy output on those panels, refer to their
 * datasheet and override the constants here.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>

#include "display.h"

static const char *TAG = "DisplayEPD";

/* Polarity convention for the 1-bpp framebuffer:
 *   - default (CONFIG_DRAFTLING_EPD_INVERT not set): bit = 1 is white,
 *     bit = 0 is black. Matches the Waveshare 7.5" V2 panel soldered to
 *     the Seeed reTerminal E1001 and many HAT panels.
 *   - inverted: bit = 1 is black, bit = 0 is white. Some standalone
 *     Waveshare 7.5" panels sold for the E-Paper Driver HAT use this
 *     opposite KW data polarity even though the controller is the
 *     same UC8179. Selectable via menuconfig.
 *
 * Keep all polarity-sensitive sites going through these constants so
 * the convention swap is one byte per call. */
#if defined(CONFIG_DRAFTLING_EPD_INVERT)
#  define EPD_WHITE_BYTE 0x00
#  define EPD_BLACK_BYTE 0xFF
#  define EPD_PIXEL_WHITE_SET 0   /* 0 = clear bit for white */
#else
#  define EPD_WHITE_BYTE 0xFF
#  define EPD_BLACK_BYTE 0x00
#  define EPD_PIXEL_WHITE_SET 1   /* 1 = set bit for white */
#endif

/* The reTerminal E1001 shares the SPI bus between the e-paper and the
 * SD card. We use HSPI (SPI2) so SPI3 is left free for any future
 * peripheral (and to mirror the upstream Seeed_GFX setup). The HAT
 * model uses the same host id; if a host board needs SPI3 instead
 * for the e-paper, override here. */
#define EPD_SPI_HOST    SPI2_HOST

/* BUSY input from the panel (active-low). Now provided as an init-time
 * parameter so each board can supply its own pin. */

/* UC8179 commands used in this driver */
#define UC8179_CMD_PSR        0x00  /* Panel Setting */
#define UC8179_CMD_PWR        0x01  /* Power Setting */
#define UC8179_CMD_PON        0x04  /* Power ON */
#define UC8179_CMD_POF        0x02  /* Power OFF */
#define UC8179_CMD_BTST       0x06  /* Booster Soft Start */
#define UC8179_CMD_DSLP       0x07  /* Deep Sleep */
#define UC8179_CMD_DTM1       0x10  /* Display Start Transmission 1 (old) */
#define UC8179_CMD_DRF        0x12  /* Display Refresh */
#define UC8179_CMD_DTM2       0x13  /* Display Start Transmission 2 (new) */
#define UC8179_CMD_PLL        0x30  /* PLL Control */
#define UC8179_CMD_CDI        0x50  /* VCOM and data interval setting */
#define UC8179_CMD_TCON       0x60  /* TCON Setting */
#define UC8179_CMD_TRES       0x61  /* Resolution Setting */
#define UC8179_CMD_VDCS       0x82  /* VCM_DC Setting */
#define UC8179_CMD_PTL        0x90  /* Partial Window */
#define UC8179_CMD_PTIN       0x91  /* Partial In */
#define UC8179_CMD_PTOUT      0x92  /* Partial Out */

static esp_lcd_panel_io_handle_t s_io_handle = NULL;
static uint8_t *s_disp_buf = NULL;       /* current frame composed by caller */
static uint8_t *s_prev_buf = NULL;       /* last frame actually shown on panel */
static int s_disp_len = 0;
static int s_width  = 0;
static int s_height = 0;
static int s_rst_pin  = -1;
static int s_busy_pin = -1;
static int s_stride   = 0;     /* bytes per row */

/* Small DMA-capable bounce buffer used by send_buffer() to stream
 * pixel data into the SPI master regardless of where the caller's
 * source buffer lives. The framebuffers live in PSRAM (one 48 KB
 * buffer would not fit in the ~135 KiB of contiguous internal DRAM
 * available on the HAT model at display_init() time, let alone two);
 * that storage is fine for CPU access but the panel-IO SPI driver
 * cannot DMA from it directly when the cache is busy with BLE/Wi-Fi
 * traffic. Instead, send_buffer() copies the pixel data into this
 * small internal-RAM buffer in chunks and does N synchronous
 * tx_param() calls.
 *
 * A single small DMA bounce buffer fits comfortably (well under the
 * 32 KiB internal-DMA reserve). UC8179 DTM1/DTM2 take a continuous
 * pixel stream after the command, so chunking is fine as long as no
 * other command is interleaved. */
#define EPD_DMA_BOUNCE_BYTES 4096
static uint8_t *s_dma_bounce = NULL;

/* How many partial refreshes we have done since the last full refresh.
 * When this hits CONFIG_DRAFTLING_EPD_FULL_REFRESH_INTERVAL we force a
 * full refresh to clear residual ghosting. */
static int s_partial_count = 0;

/* ---- low-level helpers ---- */

static void send_command(uint8_t cmd)
{
    esp_lcd_panel_io_tx_param(s_io_handle, cmd, NULL, 0);
}

static void send_data(uint8_t data)
{
    esp_lcd_panel_io_tx_param(s_io_handle, -1, &data, 1);
}

static void send_buffer(const uint8_t *data, int len)
{
    /* Stream `data` to the panel through the small internal-RAM DMA
     * bounce buffer. We copy in chunks of EPD_DMA_BOUNCE_BYTES and
     * call esp_lcd_panel_io_tx_param() for each chunk.
     *
     * IMPORTANT: tx_param is synchronous (it uses spi_device_polling_
     * transmit under the hood), so the bounce buffer can be safely
     * reused as soon as it returns. We deliberately do NOT use
     * tx_color() here -- tx_color is async (queued, depth = 10 in
     * io_cfg) and the SPI driver may still be DMAing from the bounce
     * buffer when the loop's next memcpy() overwrites it, producing
     * garbage on the panel. The garbage holds BUSY low until our
     * 5 s wait_busy() timeout, which freezes the LVGL task and
     * blocks every other subsystem that needs the LVGL mutex (the
     * editor UI, the BLE connect callback, etc.).
     *
     * UC8179 DTM1/DTM2 take a continuous pixel stream after the
     * command, so chunking is fine as long as no other command is
     * interleaved.
     *
     * s_dma_bounce is allocated and asserted in display_init(), so it
     * is non-NULL by the time any caller can reach this function. */
    assert(s_dma_bounce);
    int sent = 0;
    while (sent < len) {
        int chunk = len - sent;
        if (chunk > EPD_DMA_BOUNCE_BYTES) chunk = EPD_DMA_BOUNCE_BYTES;
        memcpy(s_dma_bounce, data + sent, chunk);
        esp_lcd_panel_io_tx_param(s_io_handle, -1, s_dma_bounce, chunk);
        sent += chunk;
    }
}

static void hw_reset(void)
{
    gpio_set_level((gpio_num_t)s_rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)s_rst_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level((gpio_num_t)s_rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
}

/* The UC8179 BUSY line goes LOW while the controller is busy and
 * returns HIGH when it is ready to accept the next command. */
static void wait_busy(int timeout_ms)
{
    const TickType_t step  = pdMS_TO_TICKS(10);
    int waited = 0;
    while (gpio_get_level((gpio_num_t)s_busy_pin) == 0) {
        vTaskDelay(step);
        waited += 10;
        if (timeout_ms > 0 && waited >= timeout_ms) {
            ESP_LOGW(TAG, "BUSY timeout after %d ms", waited);
            return;
        }
    }
}

/* ---- fast-partial LUT data (HAT panel) ---- */

#if defined(CONFIG_DRAFTLING_EPD_FAST_PARTIAL)
/* Tracks whether the panel currently has the fast-partial LUTs loaded
 * (PSR = 0x3F, register LUTs, partial-mode CDI/VDCS) or the OTP full-
 * refresh waveform (PSR = 0x1F). We switch lazily so that a sequence
 * of partial refreshes pays the mode-switch cost only once. */
static bool s_in_partial_mode = false;

/* Single-stage partial-refresh LUTs taken verbatim from the GxEPD2
 * community library's GxEPD2_750_T7 driver (GDEW075T7 panel, UC8179
 * controller). These are the exact values shipped in production by
 * GxEPD2 and known to work on the Waveshare 7.5" V2 BW panel.
 *
 * The UC8179 expects 42 bytes per LUT (7 stages x 6 bytes); we use
 * one active stage and zero-pad the remaining six. Each stage encodes
 * the source-driver level byte plus four frame-count fields T1..T4
 * and an RP (stage repeat) count:
 *   T1 -- charge-balance pre-phase frames
 *   T2 -- optional extension of T1
 *   T3 -- color-change phase frames (the actual transition pulse)
 *   T4 -- optional extension of T3
 *   RP -- number of times to repeat the stage
 *
 * Critical property: LUTWW and LUTKK use level byte 0x00, i.e. no
 * drive for unchanged pixels. That is what suppresses the full-screen
 * border flash on each partial refresh -- only pixels that actually
 * transitioned (KW and WK) receive any pulse.
 *
 * NB: an earlier revision of this driver used T3 = 0 (no color-change
 * phase) which meant pixels never got the actual transition pulse, so
 * letters never appeared on screen even though the cursor stroke did.
 * Keep T3 nonzero. */
#define EPD_PART_T1 30
#define EPD_PART_T2  5
#define EPD_PART_T3 30
#define EPD_PART_T4  5

/* VCOM (cmd 0x20) -- LUTC. */
static const uint8_t LUT_VCOM_PARTIAL[42] = {
    0x00, EPD_PART_T1, EPD_PART_T2, EPD_PART_T3, EPD_PART_T4, 1,
    /* remaining 6 stages zero-padded (RP=0 -> skipped) */
};

/* W->W (cmd 0x21) -- LUTWW. Level 0x00: no drive on unchanged white. */
static const uint8_t LUT_WW_PARTIAL[42] = {
    0x00, EPD_PART_T1, EPD_PART_T2, EPD_PART_T3, EPD_PART_T4, 1,
};

/* K->W (cmd 0x22) -- LUTKW. Default 0x5A = "more white" pull-up
 * pattern from GxEPD2; gives a slightly cleaner white than the
 * textbook 0x48. With CONFIG_DRAFTLING_EPD_PARTIAL_CANONICAL_LUT the
 * textbook single-pulse value 0x80 is used instead (drive VSH solidly
 * across the entire T1..T4 window with no charge-balance sub-pulses). */
#if defined(CONFIG_DRAFTLING_EPD_PARTIAL_CANONICAL_LUT)
#  define EPD_LUT_KW_LEVEL 0x80
#  define EPD_LUT_WK_LEVEL 0x40
#else
#  define EPD_LUT_KW_LEVEL 0x5A
#  define EPD_LUT_WK_LEVEL 0x84
#endif
static const uint8_t LUT_KW_PARTIAL[42] = {
    EPD_LUT_KW_LEVEL, EPD_PART_T1, EPD_PART_T2, EPD_PART_T3, EPD_PART_T4, 1,
};

/* W->K (cmd 0x23) -- LUTWK. Default 0x84 = pull-down pattern (GxEPD2);
 * canonical override 0x40 = drive VSL solidly across T1..T4. */
static const uint8_t LUT_WK_PARTIAL[42] = {
    EPD_LUT_WK_LEVEL, EPD_PART_T1, EPD_PART_T2, EPD_PART_T3, EPD_PART_T4, 1,
};

/* K->K (cmd 0x24) -- LUTKK. Level 0x00: no drive on unchanged black. */
static const uint8_t LUT_KK_PARTIAL[42] = {
    0x00, EPD_PART_T1, EPD_PART_T2, EPD_PART_T3, EPD_PART_T4, 1,
};

/* Border (cmd 0x25) -- LUTBD. Level 0x00: no border drive, no flash. */
static const uint8_t LUT_BD_PARTIAL[42] = {
    0x00, EPD_PART_T1, EPD_PART_T2, EPD_PART_T3, EPD_PART_T4, 1,
};

static void send_lut(uint8_t cmd, const uint8_t *lut)
{
    send_command(cmd);
    send_buffer(lut, 42);
}
#endif /* CONFIG_DRAFTLING_EPD_FAST_PARTIAL */

/* ---- public API ---- */

extern "C" void display_init(int mosi, int sck, int dc, int cs, int rst,
                             int busy, int width, int height)
{
    s_width   = width;
    s_height  = height;
    s_stride  = (width + 7) / 8;
    s_rst_pin = rst;
    s_busy_pin = busy;

    /* Initialize the SPI bus (HSPI). The SD card driver may attach to
     * the same bus afterwards as a second device. */
    spi_bus_config_t bus_cfg = {};
    bus_cfg.miso_io_num     = -1;
    bus_cfg.mosi_io_num     = mosi;
    bus_cfg.sclk_io_num     = sck;
    bus_cfg.quadwp_io_num   = -1;
    bus_cfg.quadhd_io_num   = -1;
    bus_cfg.max_transfer_sz = (width * height) / 8 + 32;
    esp_err_t bus_ret = spi_bus_initialize(EPD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (bus_ret != ESP_OK && bus_ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(bus_ret);
    }

    /* Create LCD panel IO over SPI */
    esp_lcd_panel_io_spi_config_t io_cfg = {};
    io_cfg.dc_gpio_num       = dc;
    io_cfg.cs_gpio_num       = cs;
    io_cfg.pclk_hz           = 10 * 1000 * 1000;
    io_cfg.lcd_cmd_bits      = 8;
    io_cfg.lcd_param_bits    = 8;
    io_cfg.spi_mode          = 0;
    io_cfg.trans_queue_depth = 10;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)EPD_SPI_HOST, &io_cfg, &s_io_handle));

    /* Configure RST as output and BUSY as input */
    gpio_config_t out_cfg = {};
    out_cfg.intr_type    = GPIO_INTR_DISABLE;
    out_cfg.mode         = GPIO_MODE_OUTPUT;
    out_cfg.pin_bit_mask = (1ULL << rst);
    out_cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&out_cfg));

    gpio_config_t in_cfg = {};
    in_cfg.intr_type    = GPIO_INTR_DISABLE;
    in_cfg.mode         = GPIO_MODE_INPUT;
    in_cfg.pin_bit_mask = (1ULL << s_busy_pin);
    in_cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&in_cfg));

    /* Framebuffers live in PSRAM (1 bpp; on the 800x480 HAT panel
     * each is ~48 KB, more than the contiguous internal DRAM left
     * over after BLE/WiFi reservations). The actual SPI transfers
     * are bounced through s_dma_bounce below, so DMA-capability of
     * the framebuffer storage does not matter.
     *
     * We keep two buffers: s_disp_buf is the frame being composed by
     * the LVGL flush callback, s_prev_buf is the frame currently
     * displayed on the panel. The diff between them drives partial-
     * refresh decisions. */
    s_disp_len = s_stride * height;
    s_disp_buf = (uint8_t *)heap_caps_malloc(s_disp_len, MALLOC_CAP_SPIRAM);
    s_prev_buf = (uint8_t *)heap_caps_malloc(s_disp_len, MALLOC_CAP_SPIRAM);
    assert(s_disp_buf && s_prev_buf);
    memset(s_disp_buf, EPD_WHITE_BYTE, s_disp_len);
    memset(s_prev_buf, EPD_WHITE_BYTE, s_disp_len);

    /* Small DMA-capable bounce buffer for SPI sends. See the comment
     * on s_dma_bounce above for why we cannot just hand the panel a
     * PSRAM source pointer on the HAT/BLE configuration. */
    s_dma_bounce = (uint8_t *)heap_caps_malloc(EPD_DMA_BOUNCE_BYTES,
                                               MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    assert(s_dma_bounce);

    /* Hardware reset and UC8179 init sequence (B/W mode, full update) */
    hw_reset();

    send_command(UC8179_CMD_PWR);
    send_data(0x07);                       /* Internal DC/DC, VGH/VGL/VSH/VSL */
    send_data(0x07);
    send_data(0x3F);                       /* VDH = 15 V */
    send_data(0x3F);                       /* VDL = -15 V */

    send_command(UC8179_CMD_PON);
    wait_busy(5000);

    send_command(UC8179_CMD_PSR);
    send_data(0x1F);                       /* KW-BF, KWR-AF, Scan up, Shift right */

    send_command(UC8179_CMD_TRES);
    send_data((s_width >> 8) & 0xFF);
    send_data(s_width & 0xFF);
    send_data((s_height >> 8) & 0xFF);
    send_data(s_height & 0xFF);

    send_command(UC8179_CMD_VDCS);
    send_data(0x22);                       /* VCOM_DC */

    send_command(UC8179_CMD_TCON);
    send_data(0x22);

    send_command(UC8179_CMD_CDI);
    send_data(0x10);                       /* Border = white, default interval */
    send_data(0x07);

    send_command(UC8179_CMD_PLL);
    send_data(0x06);                       /* 50 Hz */

    /* Push the white framebuffer once so the panel is in a known state */
    display_full_refresh();

    ESP_LOGI(TAG, "UC8179 e-paper %dx%d initialized", width, height);
}

extern "C" void display_clear(uint8_t color)
{
    /* Caller convention: 0x00 = black, non-zero = white. Map that to
     * the framebuffer-byte convention selected at compile time. */
    memset(s_disp_buf, color ? EPD_WHITE_BYTE : EPD_BLACK_BYTE, s_disp_len);
}

extern "C" void display_set_pixel(uint16_t x, uint16_t y, uint8_t color)
{
    if (x >= s_width || y >= s_height) return;
    int idx = y * s_stride + (x >> 3);
    uint8_t mask = 0x80 >> (x & 7);
    /* color != 0 means "draw white". EPD_PIXEL_WHITE_SET decides
     * whether white corresponds to bit-set (default) or bit-clear
     * (CONFIG_DRAFTLING_EPD_INVERT). */
    bool want_white = (color != 0);
    bool set_bit    = (want_white == (EPD_PIXEL_WHITE_SET != 0));
    if (set_bit) {
        s_disp_buf[idx] |= mask;
    } else {
        s_disp_buf[idx] &= ~mask;
    }
}

/* ---- refresh helpers ---- */

#if defined(CONFIG_DRAFTLING_EPD_FAST_PARTIAL)
/* Switch the panel into fast-partial mode: load custom LUTs into
 * registers 0x20-0x25, set PSR to use those registers (KW = 0x3F)
 * instead of the OTP waveform (BWOTP = 0x1F), and tweak CDI/VDCS to
 * the partial-mode values from the GxEPD2 reference. Cheap to re-call
 * but we still gate on s_in_partial_mode so that a burst of partial
 * refreshes only pays the LUT upload once.
 *
 * Must be called before issuing PTIN/PTL/DTM1/DTM2/DRF for a partial. */
static void epd_enter_partial_mode(void)
{
    if (s_in_partial_mode) return;

    send_command(UC8179_CMD_PSR);
    send_data(0x3F);                       /* KW, register-LUT mode */

    send_command(UC8179_CMD_VDCS);
    send_data(CONFIG_DRAFTLING_EPD_PARTIAL_VCOM_BYTE);  /* VCOM_DC; default 0x26 = -2.0 V (GxEPD2 partial), see Kconfig */

#if !defined(CONFIG_DRAFTLING_EPD_PARTIAL_CDI_DEFAULT)
    send_command(UC8179_CMD_CDI);
    send_data(0x39);                       /* LUTBD border, N2OCP copy new->old */
    send_data(0x07);
#endif

    send_lut(0x20, LUT_VCOM_PARTIAL);
    send_lut(0x21, LUT_WW_PARTIAL);
    send_lut(0x22, LUT_KW_PARTIAL);
    send_lut(0x23, LUT_WK_PARTIAL);
    send_lut(0x24, LUT_KK_PARTIAL);
    send_lut(0x25, LUT_BD_PARTIAL);

#if defined(CONFIG_DRAFTLING_EPD_PARTIAL_PWR_CYCLE)
    /* Experiment A: some UC8179 panel revisions need a power cycle
     * to latch the new PSR/CDI/VDCS values after switching from the
     * OTP waveform to the register-LUT waveform. Mirrors GxEPD2's
     * internal _PowerOn() call after every register-state change. */
    send_command(UC8179_CMD_POF);
    wait_busy(5000);
    send_command(UC8179_CMD_PON);
    wait_busy(5000);
#endif

    s_in_partial_mode = true;
}

/* Restore the OTP full-refresh waveform and the original CDI/VDCS so
 * that the next display_full_refresh() does a proper ghost-clearing
 * pass instead of the gentle partial-mode drive. */
static void epd_enter_full_mode(void)
{
    if (!s_in_partial_mode) return;

    send_command(UC8179_CMD_PSR);
    send_data(0x1F);                       /* KW-BF, BWOTP, scan up, shift right */

    send_command(UC8179_CMD_VDCS);
    send_data(0x22);                       /* VCOM_DC, original init value */

    send_command(UC8179_CMD_CDI);
    send_data(0x10);                       /* white border, default interval */
    send_data(0x07);

    s_in_partial_mode = false;
}
#else
static inline void epd_enter_partial_mode(void) {}
static inline void epd_enter_full_mode(void)    {}
#endif

/* Send the entire framebuffer using the full-update waveform.
 * Always called from display_full_refresh() and as a fallback when the
 * partial-refresh interval expires or every byte of the frame changed. */
static void epd_full_refresh(void)
{
    /* Full refresh always uses the OTP ghost-clearing waveform; if we
     * had switched into fast-partial mode for typing, restore the
     * full-mode PSR/CDI/VDCS first. */
    epd_enter_full_mode();

    /* Old data buffer (DTM1) -- the previous frame so the controller
     * has a valid reference when generating the full waveform. */
    send_command(UC8179_CMD_DTM1);
    send_buffer(s_prev_buf, s_disp_len);

    /* New data buffer (DTM2) -- the frame to display. */
    send_command(UC8179_CMD_DTM2);
    send_buffer(s_disp_buf, s_disp_len);

    send_command(UC8179_CMD_DRF);
    /* Full refresh takes several seconds; wait generously. */
    vTaskDelay(pdMS_TO_TICKS(100));
    wait_busy(30000);

    memcpy(s_prev_buf, s_disp_buf, s_disp_len);
    s_partial_count = 0;
}

/* Crop bytes [byte_x0, byte_x1] (inclusive) from rows [y0, y1] (inclusive)
 * of the source buffer into a freshly allocated tightly-packed window
 * buffer. The caller must free the returned pointer. */
static uint8_t *crop_window(const uint8_t *src,
                            int byte_x0, int byte_x1,
                            int y0, int y1)
{
    int row_bytes = byte_x1 - byte_x0 + 1;
    int rows      = y1 - y0 + 1;
    size_t len    = (size_t)row_bytes * rows;
    /* The cropped window can live in PSRAM -- send_buffer() will
     * stream it to the panel through the small internal-RAM DMA
     * bounce buffer, so the source memory region does not need to
     * be DMA-capable. */
    uint8_t *out  = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!out) return NULL;
    for (int y = 0; y < rows; y++) {
        memcpy(out + (size_t)y * row_bytes,
               src + (size_t)(y + y0) * s_stride + byte_x0,
               row_bytes);
    }
    return out;
}

/* Perform a UC8179 partial refresh covering pixel columns
 * [px_x0, px_x1] and rows [y0, y1]. px_x0 must be a multiple of 8 and
 * px_x1 must be of the form 8k+7 (byte-aligned window, as required by
 * the UC8179 PTL command). */
static void epd_partial_refresh(int px_x0, int px_x1, int y0, int y1)
{
#if defined(CONFIG_DRAFTLING_EPD_PARTIAL_FULL_FRAME)
    /* Experiment C: keep the register-LUT waveform but skip the
     * partial-window machinery -- send the entire framebuffer through
     * DTM1/DTM2 every time. Decouples LUT correctness from PTL/PTIN
     * handling. The window arguments are ignored. */
    (void)px_x0; (void)px_x1; (void)y0; (void)y1;

    epd_enter_partial_mode();

    send_command(UC8179_CMD_DTM1);
    send_buffer(s_prev_buf, s_disp_len);

    send_command(UC8179_CMD_DTM2);
    send_buffer(s_disp_buf, s_disp_len);

    send_command(UC8179_CMD_DRF);
    vTaskDelay(pdMS_TO_TICKS(20));
    wait_busy(5000);

    memcpy(s_prev_buf, s_disp_buf, s_disp_len);
    s_partial_count++;
    return;
#else
    int byte_x0 = px_x0 >> 3;
    int byte_x1 = px_x1 >> 3;
    int row_bytes = byte_x1 - byte_x0 + 1;
    int rows      = y1 - y0 + 1;
    size_t win_len = (size_t)row_bytes * rows;

    uint8_t *old_win = crop_window(s_prev_buf, byte_x0, byte_x1, y0, y1);
    uint8_t *new_win = crop_window(s_disp_buf, byte_x0, byte_x1, y0, y1);
    if (!old_win || !new_win) {
        ESP_LOGW(TAG, "Partial window allocation failed, falling back to full refresh");
        free(old_win);
        free(new_win);
        epd_full_refresh();
        return;
    }

    /* Load the fast-partial LUTs (and switch PSR to register-LUT mode)
     * the first time we do a partial after init or after a full refresh.
     * No-op on subsequent partials and on builds without the option. */
    epd_enter_partial_mode();

    send_command(UC8179_CMD_PTIN);

    /* Set partial window. PTL takes 9 bytes for the 800x480 panel:
     *   x_start_HI, x_start_LO, x_end_HI, x_end_LO,
     *   y_start_HI, y_start_LO, y_end_HI, y_end_LO,
     *   PT_SCAN. */
    send_command(UC8179_CMD_PTL);
    send_data((px_x0 >> 8) & 0xFF);
    send_data(px_x0 & 0xFF);
    send_data((px_x1 >> 8) & 0xFF);
    send_data(px_x1 & 0xFF);
    send_data((y0 >> 8) & 0xFF);
    send_data(y0 & 0xFF);
    send_data((y1 >> 8) & 0xFF);
    send_data(y1 & 0xFF);
    send_data(0x01);                       /* PT_SCAN: scan inside region */

    /* Old data for the window -- needed by the partial waveform so the
     * controller can compute the per-pixel transition. */
    send_command(UC8179_CMD_DTM1);
    send_buffer(old_win, win_len);

    /* New data for the window. */
    send_command(UC8179_CMD_DTM2);
    send_buffer(new_win, win_len);

    send_command(UC8179_CMD_DRF);
    vTaskDelay(pdMS_TO_TICKS(20));
    wait_busy(5000);

    send_command(UC8179_CMD_PTOUT);

    free(old_win);
    free(new_win);

    /* Mirror the change into the previous-frame buffer so the next diff
     * is computed against what the panel is actually showing. */
    for (int y = y0; y <= y1; y++) {
        memcpy(s_prev_buf + (size_t)y * s_stride + byte_x0,
               s_disp_buf + (size_t)y * s_stride + byte_x0,
               row_bytes);
    }
    s_partial_count++;
#endif /* CONFIG_DRAFTLING_EPD_PARTIAL_FULL_FRAME */
}

extern "C" void display_flush(void)
{
    /* Compute the bounding box of bytes that changed since the last
     * frame we actually pushed to the panel. The X bounds are recorded
     * in *byte* units (8 horizontally adjacent pixels) because the
     * UC8179 PTL command requires byte-aligned windows. */
    int byte_stride = s_stride;
    int min_by = byte_stride;   /* sentinels */
    int max_by = -1;
    int min_y  = s_height;
    int max_y  = -1;

    for (int y = 0; y < s_height; y++) {
        const uint8_t *a = s_disp_buf + y * byte_stride;
        const uint8_t *b = s_prev_buf + y * byte_stride;
        if (memcmp(a, b, byte_stride) == 0) continue;
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;
        for (int bx = 0; bx < byte_stride; bx++) {
            if (a[bx] == b[bx]) continue;
            if (bx < min_by) min_by = bx;
            if (bx > max_by) max_by = bx;
        }
    }

    /* Nothing changed -- skip the panel cycle entirely. */
    if (max_by < 0) return;

    /* Force a periodic full refresh to clear ghosting that accumulates
     * after many consecutive partial updates. */
#ifdef CONFIG_DRAFTLING_EPD_FULL_REFRESH_INTERVAL
    int full_interval = CONFIG_DRAFTLING_EPD_FULL_REFRESH_INTERVAL;
#else
    int full_interval = 50;
#endif

    /* If the change covers the bulk of the screen, the partial refresh
     * costs more SPI traffic than a single full refresh, so just go full. */
    int total_window_bytes = (max_by - min_by + 1) * (max_y - min_y + 1);
    bool dirty_is_huge = (total_window_bytes * 4 >= s_disp_len * 3);

    if (s_partial_count >= full_interval || dirty_is_huge) {
        epd_full_refresh();
        return;
    }

    int px_x0 = min_by * 8;
    int px_x1 = max_by * 8 + 7;
    epd_partial_refresh(px_x0, px_x1, min_y, max_y);
}

extern "C" void display_full_refresh(void)
{
    epd_full_refresh();
}

extern "C" uint8_t *display_get_buffer(void)
{
    return s_disp_buf;
}

extern "C" int display_get_buffer_size(void)
{
    return s_disp_len;
}

extern "C" bool display_push_rgb565(int /*x*/, int /*y*/, int /*w*/, int /*h*/,
                                    const void * /*color_map*/)
{
    /* No fast path on UC8179 - lvgl_port.cpp falls back to the
     * per-pixel display_set_pixel conversion. */
    return false;
}

extern "C" void display_set_partial_clip(int /*x*/, int /*y*/,
                                         int /*w*/, int /*h*/)
{
    /* UC8179 already computes its partial-refresh window from the
     * automatic frame diff in display_flush(); the editor's clip
     * hint adds nothing on top of that. No-op. */
}

#endif /* CONFIG_DRAFTLING_MODEL_SEEED_RETERMINAL_E1001 ||
        * CONFIG_DRAFTLING_MODEL_WAVESHARE_EPD_HAT */
