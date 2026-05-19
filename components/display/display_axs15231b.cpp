#include "sdkconfig.h"
#if defined(CONFIG_DRAFTLING_DISPLAY_AXS15231B)

/*
 * AXS15231B QSPI color-LCD driver.
 *
 * Used by:
 *   - Waveshare ESP32-S3-Touch-LCD-3.49 (640x172)
 *   - Guition JC3248W535               (480x320)
 *
 * The AXS15231B is a small color TFT controller that uses a 4-line
 * (Quad-SPI) interface for both commands and pixel data. Commands
 * are sent in single-line mode wrapped in a 32-bit "address phase"
 * preamble (0x02, 0x00, cmd, 0x00) and pixel writes are sent in QSPI
 * mode after a (0x32, 0x00, 0x2C, 0x00) preamble that selects the
 * controller's "Memory Write" command (0x2C).
 *
 * Architecture
 * ------------
 * Unlike the monochrome backends, this driver keeps its own RGB565
 * framebuffer in PSRAM (sized to width * height * 2). The LVGL
 * port's flush_cb pushes RGB565 rectangles into the framebuffer via
 * display_push_rgb565(), and display_flush() walks the accumulated
 * dirty bounding box and DMA-streams just that region to the panel
 * over QSPI. There is no per-pixel display_set_pixel fast-path on
 * color hardware (the LVGL port always takes the RGB565 path), but
 * we provide a degraded fallback that interprets 0/0xFF as
 * black/white in case the framebuffer is queried from elsewhere
 * (e.g. the splash-screen logo in editor_ui.cpp).
 *
 * Status
 * ------
 * Driver protocol implementation cross-referenced against the
 * Arduino_GFX Arduino_ESP32QSPI / Arduino_AXS15231B references and
 * corrected on real hardware (Guition JC3248W535): vendor commands
 * are sent with the cmd/addr phases on all 4 data lines
 * (SPI_TRANS_MULTILINE_CMD|_ADDR), the QSPI memory-write opcode
 * uses operand 0x3C (not the MIPI 0x2C), and pixel-write bursts
 * keep CS low across all chunks (preamble on the first chunk only,
 * zero-bit cmd/addr/dummy on continuation chunks). The init
 * sequence is the proven Arduino_GFX "Type-1" 320x480 vendor
 * block.
 *
 * Pins are passed in via display_axs15231b_init() (struct-based,
 * since the controller needs 9 GPIOs -- more than the legacy
 * display_init() can carry). Call display_axs15231b_init() from
 * main.cpp; the legacy display_init() is unsupported on this
 * backend and will abort.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <driver/ledc.h>

#include "display.h"

static const char *TAG = "DisplayAXS";

/* LEDC PWM configuration for the backlight. We keep timer 0 / channel
 * 0 dedicated to the LCD backlight; nothing else in Draftling uses
 * LEDC. 5 kHz is well above any visible flicker and is comfortably
 * within the LEDC peripheral's range at 10-bit duty resolution. */
#define BL_LEDC_TIMER       LEDC_TIMER_0
#define BL_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BL_LEDC_CHANNEL     LEDC_CHANNEL_0
#define BL_LEDC_DUTY_RES    LEDC_TIMER_10_BIT
#define BL_LEDC_DUTY_MAX    ((1 << 10) - 1)
#define BL_LEDC_FREQ_HZ     5000

/* Backlight GPIO captured at init(); declared here so
 * display_set_backlight() (below) can reference it before the rest
 * of the static state is defined further down. */
static int s_bl_pin = -1;

static void backlight_pwm_init(int bl_pin)
{
    if (bl_pin < 0) return;

    ledc_timer_config_t t = {};
    t.speed_mode      = BL_LEDC_MODE;
    t.duty_resolution = BL_LEDC_DUTY_RES;
    t.timer_num       = BL_LEDC_TIMER;
    t.freq_hz         = BL_LEDC_FREQ_HZ;
    t.clk_cfg         = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&t));

    ledc_channel_config_t c = {};
    c.gpio_num   = bl_pin;
    c.speed_mode = BL_LEDC_MODE;
    c.channel    = BL_LEDC_CHANNEL;
    c.timer_sel  = BL_LEDC_TIMER;
    c.intr_type  = LEDC_INTR_DISABLE;
    /* Start at 0 (off) -- the caller bumps the duty up to the
     * configured brightness after panel init so the user never sees a
     * black/garbage flash. */
    c.duty       = 0;
    c.hpoint     = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

extern "C" void display_set_backlight(int percent)
{
    if (s_bl_pin < 0) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint32_t duty = (uint32_t)((BL_LEDC_DUTY_MAX * percent) / 100);
    ESP_ERROR_CHECK(ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL));
}


#define AXS_SPI_HOST            SPI2_HOST
#define AXS_SPI_CLOCK_HZ        (40 * 1000 * 1000)

/* AXS15231B QSPI protocol constants.
 *
 * The AXS15231B uses a non-standard QSPI command framing borrowed
 * from QSPI NOR flash:
 *
 *   - Vendor / DCS commands ("register writes"):
 *       cmd byte = 0x02   (QSPI "write register" opcode)
 *       addr     = 0x00 <reg> 0x00   (24-bit, reg = MIPI DCS code)
 *       data     = optional parameter bytes (single-line)
 *     Both the cmd byte and the 24-bit address MUST be shifted out
 *     on all four data lines (SPI_TRANS_MULTILINE_CMD|_ADDR), or
 *     the controller does not recognize the frame at all.
 *
 *   - Pixel writes (memory-write):
 *       cmd byte = 0x32   (QSPI "memory write" opcode)
 *       addr     = 0x00 0x3C 0x00   (NB: 0x3C, not the MIPI 0x2C)
 *       data     = raw RGB565 pixels in QSPI (4-line) mode
 *     The preamble is only sent for the first chunk of a memory-
 *     write burst; subsequent chunks of the same burst send raw
 *     pixel bytes with zero-bit cmd/addr/dummy phases. CS must
 *     stay LOW across the whole burst -- if CS toggles between
 *     chunks the address pointer is reset and only the first
 *     chunk lands at the CASET/RASET origin.
 */
#define AXS_CMD_PREAMBLE        0x02
#define AXS_DATA_PREAMBLE_BYTE  0x32
#define AXS_QSPI_MEMWR_OPERAND  0x3C

static spi_device_handle_t s_spi = NULL;
static int s_width  = 0;
static int s_height = 0;
/* Software 90-deg-CW rotation flag. See display_axs15231b_config_t.swap_xy.
 * Set in display_axs15231b_init() from cfg->swap_xy and read by
 * display_flush() to decide between the direct and the transposed
 * per-row path. */
static bool s_swap_xy = false;
static int s_rst_pin = -1;
static int s_te_pin  = -1;
static int s_cs_pin  = -1;
/* s_bl_pin is defined near the top of this file. */


/* RGB565 framebuffer (host-side). uint16_t in little-endian; we send
 * it byte-swapped to match the panel's big-endian RGB565 wire format. */
static uint16_t *s_fb = NULL;
static size_t    s_fb_pixels = 0;

/* Logical-to-panel pixel scale. Read from Kconfig at compile time.
 * Each logical LVGL pixel is rendered as SCALE x SCALE physical
 * panel pixels via nearest-neighbor expansion in display_push_rgb565().
 * LVGL renders into a logical canvas of size (s_width / SCALE) by
 * (s_height / SCALE), so without this scaling the LVGL output would
 * occupy only the top-left 1/SCALE^2 of the panel. */
#ifdef CONFIG_DRAFTLING_DISPLAY_SCALE
#define AXS_SCALE CONFIG_DRAFTLING_DISPLAY_SCALE
#else
#define AXS_SCALE 1
#endif

/* Accumulated dirty bounding box (inclusive) since the last flush.
 * (-1, -1, -1, -1) means clean. */
static int s_dirty_x1 = -1;
static int s_dirty_y1 = -1;
static int s_dirty_x2 = -1;
static int s_dirty_y2 = -1;

/* One-shot panel-refresh clip rectangle (set by display_set_partial_clip
 * and consumed by the next display_flush). w<=0 or h<=0 means inactive. */
static int s_clip_x = 0, s_clip_y = 0, s_clip_w = 0, s_clip_h = 0;

/* DMA scratch buffer for one row's worth of byte-swapped pixels. We
 * stream the dirty rectangle row by row to avoid allocating a full
 * intermediate copy; the largest row is 1024 px = 2 KB. */
static uint8_t *s_row_buf = NULL;

/* ---------------- Low-level SPI helpers ---------------- */

static inline void cs_low(void)
{
    if (s_cs_pin >= 0) gpio_set_level((gpio_num_t)s_cs_pin, 0);
}

static inline void cs_high(void)
{
    if (s_cs_pin >= 0) gpio_set_level((gpio_num_t)s_cs_pin, 1);
}

/* Send a vendor / DCS command (with optional parameter bytes).
 *
 * Frame layout on the wire (all sent within one CS-low pulse):
 *   cmd phase (8 bits,  QSPI mode): 0x02
 *   addr phase (24 bits, QSPI mode): 0x00, reg, 0x00
 *   data phase (n_params*8 bits, single-line): parameter bytes
 *
 * Marking cmd/addr as MULTILINE (4-line) is mandatory: the
 * AXS15231B does not accept these phases on a single line and the
 * frame is silently ignored if MULTILINE is not set.
 */
static void spi_send_cmd(uint8_t cmd, const uint8_t *params, size_t n_params)
{
    cs_low();

    spi_transaction_ext_t tx = {};
    tx.base.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    tx.base.cmd   = AXS_CMD_PREAMBLE;
    tx.base.addr  = ((uint32_t)cmd) << 8;

    if (n_params > 0) {
        tx.base.length    = n_params * 8;
        tx.base.tx_buffer = params;
    }

    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, (spi_transaction_t *)&tx));

    cs_high();
}

/* Stream pixel data as a single CS-low memory-write burst.
 *
 * The first chunk carries the QSPI memory-write preamble
 * (cmd 0x32, addr 0x003C00) and selects 4-line data mode; every
 * subsequent chunk in the same burst sends raw pixel bytes with
 * cmd/addr/dummy phases sized to zero (SPI_TRANS_VARIABLE_*
 * with the corresponding _bits fields cleared).
 *
 * The caller is responsible for ensuring no other transaction
 * touches the bus mid-burst (we acquire the bus in display_flush).
 */
static void spi_send_pixels_first(const uint8_t *data, size_t n_bytes)
{
    spi_transaction_ext_t tx = {};
    tx.base.flags = SPI_TRANS_MODE_QIO;
    tx.base.cmd   = AXS_DATA_PREAMBLE_BYTE;
    tx.base.addr  = ((uint32_t)AXS_QSPI_MEMWR_OPERAND) << 8;
    tx.base.length    = n_bytes * 8;
    tx.base.tx_buffer = data;

    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, (spi_transaction_t *)&tx));
}

static void spi_send_pixels_cont(const uint8_t *data, size_t n_bytes)
{
    spi_transaction_ext_t tx = {};
    tx.base.flags = SPI_TRANS_MODE_QIO |
                    SPI_TRANS_VARIABLE_CMD |
                    SPI_TRANS_VARIABLE_ADDR |
                    SPI_TRANS_VARIABLE_DUMMY;
    /* No cmd / addr / dummy on continuation chunks. */
    tx.command_bits = 0;
    tx.address_bits = 0;
    tx.dummy_bits   = 0;
    tx.base.length    = n_bytes * 8;
    tx.base.tx_buffer = data;

    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, (spi_transaction_t *)&tx));
}

static void hw_reset(void)
{
    if (s_rst_pin < 0) return;
    gpio_set_level((gpio_num_t)s_rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)s_rst_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)s_rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

static void axs15231b_init_sequence(void)
{
    /* AXS15231B vendor init sequence. The opcodes and parameter
     * blocks are cross-referenced with the working JC3248W535(C)
     * IDF-native driver at thingsapart/esp32_lcd_controllers and
     * the Arduino_GFX `axs15231b_320480_type1_init_operations`
     * reference; the JC3248W535 source was preferred where they
     * differ, since it's the board we have logs from.
     *
     * The critical first command is the `0xBB` UNLOCK with magic
     * bytes `0x5A 0xA5`: the AXS15231B's vendor registers
     * (`0xA0`, `0xA2`, `0xD0`, ...) are write-protected after
     * reset, and silently drop every write until this unlock has
     * been received. The Arduino_GFX Type-1 sequence omits this
     * unlock (it targets a panel that ships unlocked), which is
     * what caused the JC3248W535 to power up with all vendor
     * regs at their defaults -- producing a brief flash of
     * garbage on display-on, then nothing. The all-zero `0xBB`
     * issued near the end of this block re-locks the registers
     * once the panel is configured. */

    /* Vendor register unlock (magic key 0x5A, 0xA5). */
    static const uint8_t init_bb_unlock[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A, 0xA5
    };
    spi_send_cmd(0xBB, init_bb_unlock, sizeof(init_bb_unlock));

    static const uint8_t init_a0[] = {
        0xC0, 0x10, 0x00, 0x02, 0x00, 0x00, 0x04, 0x3F,
        0x20, 0x05, 0x3F, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    spi_send_cmd(0xA0, init_a0, sizeof(init_a0));

    static const uint8_t init_a2[] = {
        0x30, 0x3C, 0x24, 0x14, 0xD0, 0x20, 0xFF, 0xE0,
        0x40, 0x19, 0x80, 0x80, 0x80, 0x20, 0xF9, 0x10,
        0x02, 0xFF, 0xFF, 0xF0, 0x90, 0x01, 0x32, 0xA0,
        0x91, 0xE0, 0x20, 0x7F, 0xFF, 0x00, 0x5A
    };
    spi_send_cmd(0xA2, init_a2, sizeof(init_a2));

    static const uint8_t init_d0[] = {
        0xE0, 0x40, 0x51, 0x24, 0x08, 0x05, 0x10, 0x01,
        0x20, 0x15, 0x42, 0xC2, 0x22, 0x22, 0xAA, 0x03,
        0x10, 0x12, 0x60, 0x14, 0x1E, 0x51, 0x15, 0x00,
        0x8A, 0x20, 0x00, 0x03, 0x3A, 0x12
    };
    spi_send_cmd(0xD0, init_d0, sizeof(init_d0));

    static const uint8_t init_a3[] = {
        0xA0, 0x06, 0xAA, 0x00, 0x08, 0x02, 0x0A, 0x04,
        0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
        0x04, 0x04, 0x04, 0x00, 0x55, 0x55
    };
    spi_send_cmd(0xA3, init_a3, sizeof(init_a3));

    static const uint8_t init_c1[] = {
        0x31, 0x04, 0x02, 0x02, 0x71, 0x05, 0x24, 0x55,
        0x02, 0x00, 0x41, 0x00, 0x53, 0xFF, 0xFF, 0xFF,
        0x4F, 0x52, 0x00, 0x4F, 0x52, 0x00, 0x45, 0x3B,
        0x0B, 0x02, 0x0D, 0x00, 0xFF, 0x40
    };
    spi_send_cmd(0xC1, init_c1, sizeof(init_c1));

    static const uint8_t init_c3[] = {
        0x00, 0x00, 0x00, 0x50, 0x03, 0x00, 0x00, 0x00,
        0x01, 0x80, 0x01
    };
    spi_send_cmd(0xC3, init_c3, sizeof(init_c3));

    static const uint8_t init_c4[] = {
        0x00, 0x24, 0x33, 0x80, 0x00, 0xEA, 0x64, 0x32,
        0xC8, 0x64, 0xC8, 0x32, 0x90, 0x90, 0x11, 0x06,
        0xDC, 0xFA, 0x00, 0x00, 0x80, 0xFE, 0x10, 0x10,
        0x00, 0x0A, 0x0A, 0x44, 0x50
    };
    spi_send_cmd(0xC4, init_c4, sizeof(init_c4));

    static const uint8_t init_c5[] = {
        0x18, 0x00, 0x00, 0x03, 0xFE, 0x3A, 0x4A, 0x20,
        0x30, 0x10, 0x88, 0xDE, 0x0D, 0x08, 0x0F, 0x0F,
        0x01, 0x3A, 0x4A, 0x20, 0x10, 0x10, 0x00
    };
    spi_send_cmd(0xC5, init_c5, sizeof(init_c5));

    static const uint8_t init_c6[] = {
        0x05, 0x0A, 0x05, 0x0A, 0x00, 0xE0, 0x2E, 0x0B,
        0x12, 0x22, 0x12, 0x22, 0x01, 0x03, 0x00, 0x3F,
        0x6A, 0x18, 0xC8, 0x22
    };
    spi_send_cmd(0xC6, init_c6, sizeof(init_c6));

    static const uint8_t init_c7[] = {
        0x50, 0x32, 0x28, 0x00, 0xA2, 0x80, 0x8F, 0x00,
        0x80, 0xFF, 0x07, 0x11, 0x9C, 0x67, 0xFF, 0x24,
        0x0C, 0x0D, 0x0E, 0x0F
    };
    spi_send_cmd(0xC7, init_c7, sizeof(init_c7));

    static const uint8_t init_c9[] = { 0x33, 0x44, 0x44, 0x01 };
    spi_send_cmd(0xC9, init_c9, sizeof(init_c9));

    static const uint8_t init_cf[] = {
        0x2C, 0x1E, 0x88, 0x58, 0x13, 0x18, 0x56, 0x18,
        0x1E, 0x68, 0x88, 0x00, 0x65, 0x09, 0x22, 0xC4,
        0x0C, 0x77, 0x22, 0x44, 0xAA, 0x55, 0x08, 0x08,
        0x12, 0xA0, 0x08
    };
    spi_send_cmd(0xCF, init_cf, sizeof(init_cf));

    static const uint8_t init_d5[] = {
        0x40, 0x8E, 0x8D, 0x01, 0x35, 0x04, 0x92, 0x74,
        0x04, 0x92, 0x74, 0x04, 0x08, 0x6A, 0x04, 0x46,
        0x03, 0x03, 0x03, 0x03, 0x82, 0x01, 0x03, 0x00,
        0xE0, 0x51, 0xA1, 0x00, 0x00, 0x00
    };
    spi_send_cmd(0xD5, init_d5, sizeof(init_d5));

    static const uint8_t init_d6[] = {
        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
        0x93, 0x00, 0x01, 0x83, 0x07, 0x07, 0x00, 0x07,
        0x07, 0x00, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
        0x00, 0x84, 0x00, 0x20, 0x01, 0x00
    };
    spi_send_cmd(0xD6, init_d6, sizeof(init_d6));

    static const uint8_t init_d7[] = {
        0x03, 0x01, 0x0B, 0x09, 0x0F, 0x0D, 0x1E, 0x1F,
        0x18, 0x1D, 0x1F, 0x19, 0x40, 0x8E, 0x04, 0x00,
        0x20, 0xA0, 0x1F
    };
    spi_send_cmd(0xD7, init_d7, sizeof(init_d7));

    static const uint8_t init_d8[] = {
        0x02, 0x00, 0x0A, 0x08, 0x0E, 0x0C, 0x1E, 0x1F,
        0x18, 0x1D, 0x1F, 0x19
    };
    spi_send_cmd(0xD8, init_d8, sizeof(init_d8));

    static const uint8_t init_d9[] = {
        0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
        0x1F, 0x1F, 0x1F, 0x1F
    };
    spi_send_cmd(0xD9, init_d9, sizeof(init_d9));

    static const uint8_t init_dd[] = {
        0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
        0x1F, 0x1F, 0x1F, 0x1F
    };
    spi_send_cmd(0xDD, init_dd, sizeof(init_dd));

    static const uint8_t init_df[] = {
        0x44, 0x73, 0x4B, 0x69, 0x00, 0x0A, 0x02, 0x90
    };
    spi_send_cmd(0xDF, init_df, sizeof(init_df));

    static const uint8_t init_e0[] = {
        0x3B, 0x28, 0x10, 0x16, 0x0C, 0x06, 0x11, 0x28,
        0x5C, 0x21, 0x0D, 0x35, 0x13, 0x2C, 0x33, 0x28,
        0x0D
    };
    spi_send_cmd(0xE0, init_e0, sizeof(init_e0));

    static const uint8_t init_e1[] = {
        0x37, 0x28, 0x10, 0x16, 0x0B, 0x06, 0x11, 0x28,
        0x5C, 0x21, 0x0D, 0x35, 0x14, 0x2C, 0x33, 0x28,
        0x0F
    };
    spi_send_cmd(0xE1, init_e1, sizeof(init_e1));

    static const uint8_t init_e2[] = {
        0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35,
        0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F,
        0x0D
    };
    spi_send_cmd(0xE2, init_e2, sizeof(init_e2));

    static const uint8_t init_e3[] = {
        0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35,
        0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x32, 0x2F,
        0x0F
    };
    spi_send_cmd(0xE3, init_e3, sizeof(init_e3));

    static const uint8_t init_e4[] = {
        0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39,
        0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F,
        0x0D
    };
    spi_send_cmd(0xE4, init_e4, sizeof(init_e4));

    static const uint8_t init_e5[] = {
        0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39,
        0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F,
        0x0F
    };
    spi_send_cmd(0xE5, init_e5, sizeof(init_e5));

    static const uint8_t init_a4_1[] = {
        0x85, 0x85, 0x95, 0x82, 0xAF, 0xAA, 0xAA, 0x80,
        0x10, 0x30, 0x40, 0x40, 0x20, 0xFF, 0x60, 0x30
    };
    spi_send_cmd(0xA4, init_a4_1, sizeof(init_a4_1));

    static const uint8_t init_a4_2[] = { 0x85, 0x85, 0x95, 0x85 };
    spi_send_cmd(0xA4, init_a4_2, sizeof(init_a4_2));

    /* Vendor register-block re-lock. Writing 0xBB with eight zero
     * bytes (no 0x5A/0xA5 magic key) closes the vendor register
     * window opened at the start of this sequence. */
    static const uint8_t init_bb_lock[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    spi_send_cmd(0xBB, init_bb_lock, sizeof(init_bb_lock));

    /* Memory Access Control (MADCTL). Always 0x00 on this driver:
     * not every AXS15231B silicon revision honours the MV
     * (row/column exchange) bit -- the Guition JC3248W535 is known
     * to silently ignore it (the Tactility project's JC3248W535
     * driver carries the same observation). Boards mounted in
     * portrait whose application wants landscape opt in to
     * software rotation via display_axs15231b_config_t.swap_xy,
     * which display_flush() honours by transposing per-row into
     * the DMA scratch buffer and addressing the panel in its
     * native portrait coords. Boards whose panel is natively
     * landscape (e.g. Waveshare Touch-LCD-3.49, 640x172) leave
     * swap_xy false and write to the panel in landscape directly.
     * RGB pixel order. */
    uint8_t madctl = 0x00;
    spi_send_cmd(0x36, &madctl, 1);

    /* TE line on (V-blank only). The matching IDF-native init at
     * thingsapart/esp32_lcd_controllers also enables TE here; we
     * read the GPIO directly in display_flush() instead of using
     * an ISR, so the panel-side enable is what matters. */
    static const uint8_t te[] = { 0x00 };
    spi_send_cmd(0x35, te, sizeof(te));

    /* COLMOD (0x3A) is intentionally NOT sent: the vendor 0xA0
     * block above already sets the panel to 16 bpp RGB565 in QSPI
     * mode, and issuing a MIPI COLMOD after the vendor regs were
     * configured was observed to leave the panel in an
     * intermediate state where it accepted display-on but did not
     * latch streamed pixel data. The proven JC3248W535 init at
     * thingsapart/esp32_lcd_controllers also omits COLMOD. */

    /* Normal Display Mode On (NORON). Mirrors the working
     * JC3248W535 init -- the panel needs to leave partial-mode
     * before SLPOUT or the first frame after wakeup is dropped. */
    spi_send_cmd(0x13, NULL, 0);

    /* Sleep out, wait, then display on. */
    spi_send_cmd(0x11, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    spi_send_cmd(0x29, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Prime the panel write pointer with a single dummy memory-write
     * (4 zero bytes). Matches Arduino_GFX's init terminator. */
    static const uint8_t init_2c[] = { 0x00, 0x00, 0x00, 0x00 };
    spi_send_cmd(0x2C, init_2c, sizeof(init_2c));
}

static void set_addr_window(int x, int y, int w, int h)
{
    int x2 = x + w - 1;
    int y2 = y + h - 1;

    uint8_t caset[] = {
        (uint8_t)((x  >> 8) & 0xFF), (uint8_t)(x  & 0xFF),
        (uint8_t)((x2 >> 8) & 0xFF), (uint8_t)(x2 & 0xFF)
    };
    uint8_t raset[] = {
        (uint8_t)((y  >> 8) & 0xFF), (uint8_t)(y  & 0xFF),
        (uint8_t)((y2 >> 8) & 0xFF), (uint8_t)(y2 & 0xFF)
    };
    spi_send_cmd(0x2A, caset, sizeof(caset));
    spi_send_cmd(0x2B, raset, sizeof(raset));
}

/* ---------------- Public API ---------------- */

extern "C" void display_init(int /*pin_a*/, int /*pin_b*/, int /*pin_c*/,
                             int /*pin_d*/, int /*pin_e*/, int /*pin_f*/,
                             int /*width*/, int /*height*/)
{
    /* The AXS15231B driver needs 9 GPIOs which do not fit in
     * display_init()'s 6 pin slots, so it exposes its own
     * struct-based init (display_axs15231b_init). Calling the
     * generic display_init on an AXS15231B build is a programming
     * error; abort loudly. */
    ESP_LOGE(TAG, "display_init() is not supported on AXS15231B; "
                  "call display_axs15231b_init() instead");
    abort();
}

extern "C" void display_axs15231b_init(const display_axs15231b_config_t *cfg)
{
    assert(cfg);
    s_width  = cfg->width;
    s_height = cfg->height;
    s_swap_xy = cfg->swap_xy;
    s_rst_pin = cfg->rst;
    s_te_pin  = cfg->te;
    s_bl_pin  = cfg->bl;
    s_cs_pin  = cfg->cs;

    /* Reset / TE / Backlight GPIOs */
    if (s_rst_pin >= 0) {
        gpio_config_t g = {};
        g.intr_type    = GPIO_INTR_DISABLE;
        g.mode         = GPIO_MODE_OUTPUT;
        g.pin_bit_mask = (1ULL << s_rst_pin);
        ESP_ERROR_CHECK(gpio_config(&g));
    }
    if (s_bl_pin >= 0) {
        /* Backlight is driven by LEDC PWM. The LEDC channel is set
         * up here with duty 0 (off); the editor calls
         * display_set_backlight() with the user-configured
         * (NVS-persisted) percent after the panel is fully
         * initialised, so there is no black/garbage flash. */
        backlight_pwm_init(s_bl_pin);
    }
    if (s_te_pin >= 0) {
        gpio_config_t g = {};
        g.intr_type    = GPIO_INTR_DISABLE;
        g.mode         = GPIO_MODE_INPUT;
        g.pin_bit_mask = (1ULL << s_te_pin);
        g.pull_up_en   = GPIO_PULLUP_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&g));
    }

    /* CS pin: manage manually rather than letting the SPI peripheral
     * toggle it per-transaction. The AXS15231B's QSPI memory-write
     * protocol expects the cmd/addr preamble to be sent once at the
     * start of a write burst and then raw pixel chunks streamed
     * without CS pulsing in between (otherwise the column/page
     * pointer is reset and only the first chunk lands at the
     * CASET/RASET origin). spics_io_num below is set to -1; we
     * drive this GPIO from cs_low() / cs_high(). */
    if (s_cs_pin >= 0) {
        gpio_config_t g = {};
        g.intr_type    = GPIO_INTR_DISABLE;
        g.mode         = GPIO_MODE_OUTPUT;
        g.pin_bit_mask = (1ULL << s_cs_pin);
        ESP_ERROR_CHECK(gpio_config(&g));
        gpio_set_level((gpio_num_t)s_cs_pin, 1);
    }

    /* QSPI bus: 4 data lines (D0..D3) + SCK. CS is managed by us
     * (spics_io_num = -1) so that pixel-write bursts can keep CS
     * low across multiple transactions.
     *
     * max_transfer_sz is sized to the longer of width/height because
     * when s_swap_xy is set, display_flush() addresses the panel in
     * portrait coords and each panel row carries s_height (logical
     * column) pixels rather than s_width. */
    int max_row_pixels = (s_width > s_height) ? s_width : s_height;
    spi_bus_config_t bus_cfg = {};
    bus_cfg.data0_io_num = cfg->d0;
    bus_cfg.data1_io_num = cfg->d1;
    bus_cfg.data2_io_num = cfg->d2;
    bus_cfg.data3_io_num = cfg->d3;
    bus_cfg.sclk_io_num  = cfg->sck;
    bus_cfg.max_transfer_sz = max_row_pixels * 2 + 16;
    bus_cfg.flags        = SPICOMMON_BUSFLAG_QUAD;
    ESP_ERROR_CHECK(spi_bus_initialize(AXS_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.command_bits   = 8;
    dev_cfg.address_bits   = 24;
    dev_cfg.mode           = 0;
    dev_cfg.clock_speed_hz = AXS_SPI_CLOCK_HZ;
    dev_cfg.spics_io_num   = -1;
    dev_cfg.queue_size     = 7;
    dev_cfg.flags          = SPI_DEVICE_HALFDUPLEX;
    ESP_ERROR_CHECK(spi_bus_add_device(AXS_SPI_HOST, &dev_cfg, &s_spi));

    /* Allocate framebuffer (RGB565) and the per-row DMA scratch
     * buffer. The scratch must be DMA-capable internal RAM because
     * spi_device_polling_transmit() does not stream from PSRAM. */
    s_fb_pixels = (size_t)s_width * s_height;
    s_fb = (uint16_t *)heap_caps_malloc(s_fb_pixels * sizeof(uint16_t),
                                        MALLOC_CAP_SPIRAM);
    s_row_buf = (uint8_t *)heap_caps_malloc(max_row_pixels * 2,
                                            MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    assert(s_fb && s_row_buf);
    memset(s_fb, 0, s_fb_pixels * sizeof(uint16_t));

    hw_reset();
    axs15231b_init_sequence();

    /* Backlight LEDC channel is initialised in init() with duty 0;
     * the editor calls display_set_backlight() once the user-
     * configured (NVS-persisted) percent is loaded, so we do not
     * push a default value here and avoid a black/garbage flash. */

    /* Initial blank to avoid showing whatever junk was in panel RAM. */
    display_clear(0x00);
    display_full_refresh();

    ESP_LOGI(TAG, "AXS15231B %dx%d initialized", s_width, s_height);
}

extern "C" void display_clear(uint8_t color)
{
    /* The legacy 1-bpp API: 0xFF means "white", 0x00 means "black".
     * Map both bytes of the RGB565 pixel to the same value so memset
     * works directly on the framebuffer. */
    memset(s_fb, color ? 0xFF : 0x00, s_fb_pixels * sizeof(uint16_t));
    s_dirty_x1 = 0;
    s_dirty_y1 = 0;
    s_dirty_x2 = s_width  - 1;
    s_dirty_y2 = s_height - 1;
}

extern "C" void display_set_pixel(uint16_t x, uint16_t y, uint8_t color)
{
    /* Caller passes *logical* coordinates; expand to a SCALE x SCALE
     * block of panel pixels to match display_push_rgb565(). */
    int px = (int)x * AXS_SCALE;
    int py = (int)y * AXS_SCALE;
    if (px >= s_width || py >= s_height) return;
    uint16_t v = (color == 0) ? 0x0000 : 0xFFFF;
    int x_end = px + AXS_SCALE; if (x_end > s_width)  x_end = s_width;
    int y_end = py + AXS_SCALE; if (y_end > s_height) y_end = s_height;
    for (int yy = py; yy < y_end; yy++) {
        uint16_t *row = s_fb + (size_t)yy * s_width + px;
        for (int xx = px; xx < x_end; xx++) *row++ = v;
    }

    int x2 = x_end - 1;
    int y2 = y_end - 1;
    if (s_dirty_x1 < 0) {
        s_dirty_x1 = px;  s_dirty_y1 = py;
        s_dirty_x2 = x2;  s_dirty_y2 = y2;
    } else {
        if (px < s_dirty_x1) s_dirty_x1 = px;
        if (x2 > s_dirty_x2) s_dirty_x2 = x2;
        if (py < s_dirty_y1) s_dirty_y1 = py;
        if (y2 > s_dirty_y2) s_dirty_y2 = y2;
    }
}

extern "C" bool display_push_rgb565(int x, int y, int w, int h,
                                    const void *color_map)
{
    if (w <= 0 || h <= 0) return true;
    /* Caller passes *logical* coordinates and a tightly-packed
     * (logical w * logical h) RGB565 buffer. Nearest-neighbor expand
     * each source pixel into a SCALE x SCALE panel-pixel block in the
     * framebuffer. */
    int px = x * AXS_SCALE;
    int py = y * AXS_SCALE;
    int pw = w * AXS_SCALE;
    int ph = h * AXS_SCALE;
    if (px < 0 || py < 0) return true;
    int x2 = px + pw - 1;
    int y2 = py + ph - 1;
    if (x2 >= s_width)  x2 = s_width  - 1;
    if (y2 >= s_height) y2 = s_height - 1;
    int eff_w = x2 - px + 1;
    int eff_h = y2 - py + 1;
    if (eff_w <= 0 || eff_h <= 0) return true;

    const uint16_t *src = (const uint16_t *)color_map;
    if (AXS_SCALE == 1) {
        for (int row = 0; row < eff_h; row++) {
            uint16_t *dst = s_fb + (size_t)(py + row) * s_width + px;
            memcpy(dst, src, eff_w * sizeof(uint16_t));
            src += w;
        }
    } else {
        /* Expand each source row into one panel row, then memcpy-replicate
         * (AXS_SCALE - 1) more times. */
        for (int sy = 0; sy < h; sy++) {
            int dy0 = py + sy * AXS_SCALE;
            if (dy0 >= s_height) break;
            uint16_t *drow0 = s_fb + (size_t)dy0 * s_width + px;
            int eff_w_row = eff_w;
            /* Build the first scaled row in place. */
            uint16_t *p = drow0;
            int written = 0;
            for (int sx = 0; sx < w && written < eff_w_row; sx++) {
                uint16_t v = src[(size_t)sy * w + sx];
                for (int k = 0; k < AXS_SCALE && written < eff_w_row; k++) {
                    *p++ = v;
                    written++;
                }
            }
            /* Replicate that row scale-1 more times (clipped at panel). */
            for (int k = 1; k < AXS_SCALE; k++) {
                int dy = dy0 + k;
                if (dy >= s_height) break;
                memcpy(s_fb + (size_t)dy * s_width + px, drow0,
                       (size_t)eff_w_row * sizeof(uint16_t));
            }
        }
    }

    if (s_dirty_x1 < 0) {
        s_dirty_x1 = px;  s_dirty_y1 = py;
        s_dirty_x2 = x2;  s_dirty_y2 = y2;
    } else {
        if (px < s_dirty_x1) s_dirty_x1 = px;
        if (x2 > s_dirty_x2) s_dirty_x2 = x2;
        if (py < s_dirty_y1) s_dirty_y1 = py;
        if (y2 > s_dirty_y2) s_dirty_y2 = y2;
    }
    return true;
}

extern "C" void display_set_partial_clip(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) {
        s_clip_w = s_clip_h = 0;
        return;
    }
    /* Caller passes *logical* coordinates; the dirty bbox and the
     * panel-write window operate in panel pixels, so scale up. */
    s_clip_x = x * AXS_SCALE;
    s_clip_y = y * AXS_SCALE;
    s_clip_w = w * AXS_SCALE;
    s_clip_h = h * AXS_SCALE;
}

extern "C" void display_flush(void)
{
    if (s_dirty_x1 < 0) return;  /* nothing dirty */

    int x1 = s_dirty_x1, y1 = s_dirty_y1;
    int x2 = s_dirty_x2, y2 = s_dirty_y2;

    /* Apply one-shot clip if pending */
    if (s_clip_w > 0 && s_clip_h > 0) {
        int cx2 = s_clip_x + s_clip_w - 1;
        int cy2 = s_clip_y + s_clip_h - 1;
        if (s_clip_x > x1) x1 = s_clip_x;
        if (s_clip_y > y1) y1 = s_clip_y;
        if (cx2      < x2) x2 = cx2;
        if (cy2      < y2) y2 = cy2;
        s_clip_w = s_clip_h = 0;
    }

    /* Reset accumulator regardless of whether the clipped region is
     * empty -- we still consumed it. */
    s_dirty_x1 = s_dirty_y1 = s_dirty_x2 = s_dirty_y2 = -1;

    if (x1 > x2 || y1 > y2) return;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= s_width)  x2 = s_width  - 1;
    if (y2 >= s_height) y2 = s_height - 1;

    int w = x2 - x1 + 1;
    int h = y2 - y1 + 1;

    /* Acquire the bus for the whole flush so that the spi_device
     * driver does not interleave any other transactions between our
     * pixel-stream chunks. CASET / RASET are normal vendor commands
     * (CS-pulsed per command); the pixel write that follows is a
     * single CS-low burst whose first chunk carries the QSPI
     * Memory-Write preamble and subsequent chunks send raw data. */
    ESP_ERROR_CHECK(spi_device_acquire_bus(s_spi, portMAX_DELAY));

    if (!s_swap_xy) {
        /* Direct path: framebuffer orientation matches panel
         * orientation, so a logical row is a panel row. */
        set_addr_window(x1, y1, w, h);

        /* Pixel-write burst. Drive CS low ourselves and keep it low
         * until every row of the dirty region has been streamed. */
        cs_low();
        for (int row = 0; row < h; row++) {
            const uint16_t *src = s_fb + (size_t)(y1 + row) * s_width + x1;
            uint8_t *dst = s_row_buf;
            for (int col = 0; col < w; col++) {
                uint16_t px = src[col];
                *dst++ = (uint8_t)(px >> 8);
                *dst++ = (uint8_t)(px & 0xFF);
            }
            if (row == 0) {
                spi_send_pixels_first(s_row_buf, (size_t)w * 2);
            } else {
                spi_send_pixels_cont(s_row_buf, (size_t)w * 2);
            }
        }
        cs_high();
    } else {
        /* Software 90-deg-CW rotation. The framebuffer is in logical
         * landscape coords (s_width x s_height); the panel scans out
         * in native portrait (s_height x s_width). Map each logical
         * point (lx, ly) to a panel point as
         *
         *     panel_x = (s_height - 1) - ly      (0 .. s_height-1)
         *     panel_y = lx                       (0 .. s_width-1)
         *
         * so the logical dirty rect (x1,y1)-(x2,y2) becomes a panel
         * window of width (y2-y1+1) and height (x2-x1+1), starting
         * at panel_x = (s_height-1) - y2, panel_y = x1.
         *
         * For each panel row (constant panel_y = lx) we walk a
         * logical *column* (fixed lx, ly from y2 down to y1) and
         * transpose those pixels into the DMA scratch buffer. The
         * column walk is a strided read over the framebuffer, so it
         * does cost some PSRAM cache pressure; this is acceptable
         * for a once-per-frame flush. */
        int panel_w = y2 - y1 + 1;
        int panel_h = x2 - x1 + 1;
        int panel_x_start = (s_height - 1) - y2;
        int panel_y_start = x1;

        set_addr_window(panel_x_start, panel_y_start, panel_w, panel_h);

        cs_low();
        for (int row = 0; row < panel_h; row++) {
            int lx = x1 + row;                  /* logical column */
            const uint16_t *col_base = s_fb + lx;
            uint8_t *dst = s_row_buf;
            for (int col = 0; col < panel_w; col++) {
                int ly = y2 - col;              /* logical row, decreasing */
                uint16_t px = col_base[(size_t)ly * s_width];
                *dst++ = (uint8_t)(px >> 8);
                *dst++ = (uint8_t)(px & 0xFF);
            }
            if (row == 0) {
                spi_send_pixels_first(s_row_buf, (size_t)panel_w * 2);
            } else {
                spi_send_pixels_cont(s_row_buf, (size_t)panel_w * 2);
            }
        }
        cs_high();
    }

    spi_device_release_bus(s_spi);
}

extern "C" void display_full_refresh(void)
{
    /* Color LCDs do not accumulate ghosting; a "full refresh" is just
     * a flush of the entire framebuffer. */
    s_dirty_x1 = 0;
    s_dirty_y1 = 0;
    s_dirty_x2 = s_width  - 1;
    s_dirty_y2 = s_height - 1;
    /* Cancel any partial clip so the whole panel really gets pushed. */
    s_clip_w = s_clip_h = 0;
    display_flush();
}

extern "C" uint8_t *display_get_buffer(void)
{
    return (uint8_t *)s_fb;
}

extern "C" int display_get_buffer_size(void)
{
    return (int)(s_fb_pixels * sizeof(uint16_t));
}

#endif /* CONFIG_DRAFTLING_DISPLAY_AXS15231B */
