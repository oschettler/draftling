/*
 * Power-latch + PWR-button driver. See include/power.h for the
 * public API.
 *
 * Architecture
 * ------------
 * The TCA9554 IO-expander is the only device this code talks to over
 * I2C, so we open a dedicated I2C master bus on the pins provided
 * via power_config_t (the same ESP-IDF v5.x i2c_master API the
 * touchscreen component uses, which avoids pulling in a heavy
 * esp_io_expander_tca9554 dependency for the two writes we need to
 * make).
 *
 * TCA9554 register layout (only the two writes we issue):
 *   0x01  Output Port      -- HIGH bit = pin high
 *   0x03  Configuration    -- 0 = output, 1 = input  (POR default = all-input)
 *
 * Sequence in power_init():
 *   1. Write Output    : latch_bit = 1 (so the pin goes HIGH as soon
 *                                       as it becomes an output)
 *   2. Write Config    : latch_bit = 0 (output), every other bit = 1 (input)
 *
 * Sequence in power_off():
 *   1. Write Output    : latch_bit = 0 -> rail drops out.
 *
 * PWR-button polling
 * ------------------
 * A 50 ms periodic esp_timer reads the PWR button GPIO (configured
 * as input with internal pull-up enabled). Consecutive samples that
 * read LOW grow a press counter; when it reaches POWER_LONG_PRESS_MS
 * the pre-off callback runs and we call power_off(). Releasing the
 * button before the threshold resets the counter. We deliberately
 * do not act on a short press: the user uses short presses to
 * *power on* the board (which is handled by hardware, before our
 * code even runs), so short presses while powered should be a
 * no-op.
 */
#include "sdkconfig.h"
#include "power.h"

#if !defined(CONFIG_DRAFTLING_HAS_POWER_LATCH)
/* Stubs for boards without a power latch. */

extern "C" int  power_init(const power_config_t *cfg)         { (void)cfg; return 0; }
extern "C" void power_set_pre_off_cb(power_pre_off_cb_t cb)   { (void)cb; }
extern "C" void power_off(void)                               { /* no-op */ }

#else /* CONFIG_DRAFTLING_HAS_POWER_LATCH */

#include <cstring>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "Power";

/* TCA9554 register addresses. */
#define TCA9554_REG_OUTPUT  0x01
#define TCA9554_REG_CONFIG  0x03

/* Polling period for the PWR button. 50 ms is fast enough that the
 * worst-case latency before we notice a press / release is
 * imperceptible, and slow enough that the polling cost is
 * negligible. */
#define POWER_BUTTON_POLL_MS 50

static power_config_t          s_cfg = {};
static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static esp_timer_handle_t      s_btn_timer = NULL;
static power_pre_off_cb_t      s_pre_off_cb = NULL;
static int                     s_press_ms = 0;     /* accumulated hold time */
static bool                    s_long_press_fired = false;

/* Drive the latch pin to `level` (0 = LOW, 1 = HIGH). The other 7
 * expander pins are left as inputs by power_init(); writing the
 * Output register without touching the Config register is therefore
 * safe (the value latches but has no electrical effect on the input
 * pins). */
static esp_err_t write_latch(int level)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t buf[2] = {
        TCA9554_REG_OUTPUT,
        (uint8_t)((level ? 1 : 0) << s_cfg.latch_bit),
    };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

/* Configure the latch pin as output, every other pin as input. The
 * POR default for the Config register is all-input (0xFF); writing
 * it once with just the latch bit cleared is sufficient. */
static esp_err_t write_config_latch_output(void)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t buf[2] = {
        TCA9554_REG_CONFIG,
        (uint8_t)(0xFF & ~(1U << s_cfg.latch_bit)),
    };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

/* PWR-button polling timer callback. */
static void btn_poll_cb(void *arg)
{
    (void)arg;
    int level = gpio_get_level((gpio_num_t)s_cfg.pwr_button_gpio);
    if (level == 0) {
        /* Button is being held down. */
        s_press_ms += POWER_BUTTON_POLL_MS;
        if (!s_long_press_fired && s_press_ms >= POWER_LONG_PRESS_MS) {
            s_long_press_fired = true;
            ESP_LOGI(TAG, "PWR long press (%d ms) -- powering off",
                     s_press_ms);
            /* power_off() invokes the pre-off hook itself (so callers
             * other than this timer also get the save), so we do not
             * call s_pre_off_cb here. */
            power_off();
        }
    } else {
        /* Button released -- reset the counter so the next press
         * starts from zero. We also clear the latched flag so a
         * second long press after this one still fires. */
        s_press_ms = 0;
        s_long_press_fired = false;
    }
}

extern "C" int power_init(const power_config_t *cfg)
{
    if (s_bus) return 0;  /* already initialized */
    if (!cfg || cfg->pwr_button_gpio < 0 || cfg->i2c_sda < 0 || cfg->i2c_scl < 0) {
        ESP_LOGE(TAG, "power_init: invalid config");
        return -1;
    }
    s_cfg = *cfg;

    /* I2C master bus on the dedicated TCA9554 pins. */
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port          = -1;   /* auto-pick a free port */
    bus_cfg.sda_io_num        = (gpio_num_t)s_cfg.i2c_sda;
    bus_cfg.scl_io_num        = (gpio_num_t)s_cfg.i2c_scl;
    bus_cfg.clk_source        = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        s_bus = NULL;
        return -1;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = s_cfg.tca9554_addr;
    dev_cfg.scl_speed_hz    = 300000;

    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
        s_dev = NULL;
        return -1;
    }

    /* Drive the latch HIGH (rail held on) before changing its
     * direction to output, so the pin never glitches LOW mid-init
     * and brown-outs us off battery. */
    err = write_latch(1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 output write failed: %s", esp_err_to_name(err));
        return -1;
    }
    err = write_config_latch_output();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 config write failed: %s", esp_err_to_name(err));
        return -1;
    }
    ESP_LOGI(TAG, "Power latch closed via TCA9554 IO%d (addr 0x%02X)",
             (int)s_cfg.latch_bit, (unsigned)s_cfg.tca9554_addr);

    /* Configure PWR button as input with internal pull-up. The board
     * has a hardware pull-up too; the internal pull is added in
     * parallel for robustness. */
    gpio_config_t g = {};
    g.intr_type    = GPIO_INTR_DISABLE;
    g.mode         = GPIO_MODE_INPUT;
    g.pin_bit_mask = (1ULL << s_cfg.pwr_button_gpio);
    g.pull_up_en   = GPIO_PULLUP_ENABLE;
    g.pull_down_en = GPIO_PULLDOWN_DISABLE;
    if (gpio_config(&g) != ESP_OK) {
        ESP_LOGW(TAG, "PWR button GPIO%d config failed", s_cfg.pwr_button_gpio);
    }

    /* Start the polling timer. */
    esp_timer_create_args_t args = {};
    args.callback = btn_poll_cb;
    args.name     = "pwr_btn";
    err = esp_timer_create(&args, &s_btn_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(err));
        return -1;
    }
    err = esp_timer_start_periodic(s_btn_timer,
                                   (uint64_t)POWER_BUTTON_POLL_MS * 1000ULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_start_periodic failed: %s", esp_err_to_name(err));
        return -1;
    }

    ESP_LOGI(TAG, "PWR button GPIO%d armed (long-press = %d ms)",
             s_cfg.pwr_button_gpio, POWER_LONG_PRESS_MS);
    return 0;
}

extern "C" void power_set_pre_off_cb(power_pre_off_cb_t cb)
{
    s_pre_off_cb = cb;
}

extern "C" void power_off(void)
{
    ESP_LOGI(TAG, "Cutting power latch -- bye");

    /* Run the pre-off hook (auto-save). Use a static guard so calling
     * power_off() multiple times (e.g. PWR long-press while the
     * standby manager is also tearing down) only invokes the hook
     * once. */
    static bool pre_off_done = false;
    if (s_pre_off_cb && !pre_off_done) {
        pre_off_done = true;
        s_pre_off_cb();
    }

    /* Drive the latch LOW. On battery the 3V3 rail collapses
     * within a few ms and we are gone before this function
     * returns. On USB the latch has no electrical effect; we
     * return so the caller can fall through to a regular
     * esp_deep_sleep_start(). */
    if (s_dev) {
        esp_err_t err = write_latch(0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "TCA9554 output write failed: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "power_off() called before power_init()");
    }

    /* Give the rail time to drop. On battery we never get past this
     * point; on USB the wait keeps us from racing the caller's
     * deep-sleep entry. */
    vTaskDelay(pdMS_TO_TICKS(200));
}

#endif /* CONFIG_DRAFTLING_HAS_POWER_LATCH */
