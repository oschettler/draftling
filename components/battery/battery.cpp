/*
 * Battery monitor.
 *
 * Two backends are currently supported:
 *
 *   - ADC + resistive divider (battery_init):
 *       * Waveshare ESP32-S3-RLCD-4.2: GPIO4 (ADC1_CH3) with a 3:1
 *         resistive divider (200 K + 100 K), no enable pin.
 *       * M5Stack PaperS3: GPIO3 (ADC1_CH2) with a 2:1 resistive
 *         divider, no enable pin (matches the M5Unified Power_Class
 *         configuration for board_M5PaperS3: BAT_ADC = ADC1_GPIO3,
 *         adc_ratio = 2.0).
 *       The ESP-IDF ADC oneshot driver with curve-fitting calibration
 *       converts the raw ADC reading to millivolts at the pin, and the
 *       divider ratio recovers the actual cell voltage. An optional
 *       active-high enable GPIO is supported for boards that gate the
 *       divider through a transistor.
 *
 *   - TI BQ27220 fuel gauge over I2C (battery_init_bq27220):
 *       * LilyGO T5 E-Paper S3 Pro / Pro Lite: BQ27220YZFR at 0x55 on
 *         the same I2C bus that carries the GT911 touch controller
 *         and the EPDIY-owned TPS65185 / PCA9535 expander.
 *       The caller passes in an already-created
 *       i2c_master_bus_handle_t (the bus is shared with epdiy + GT911,
 *       so the battery component must not own it). Voltage comes from
 *       the 16-bit Voltage register at 0x08 (mV) and the percentage
 *       comes from the StateOfCharge register at 0x2C (already 0-100).
 */

#include "battery.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>

static const char *TAG = "Battery";

/* ---- backend selection ---- */
enum batt_backend {
    BATT_BACKEND_NONE = 0,
    BATT_BACKEND_ADC,
    BATT_BACKEND_BQ27220,
};
static enum batt_backend s_backend = BATT_BACKEND_NONE;

/* ---- ADC backend state ---- */
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t         s_cali_handle = NULL;
static adc_channel_t             s_channel = ADC_CHANNEL_3;
static int                       s_enable_gpio = -1;
static int                       s_divider = 3;

/* ---- BQ27220 backend state ---- */
#define BQ27220_I2C_ADDR        0x55
#define BQ27220_REG_VOLTAGE     0x08    /* u16 LE, mV */
#define BQ27220_REG_SOC         0x2C    /* u16 LE, 0-100 % */
static i2c_master_dev_handle_t   s_bq_dev = NULL;

/* Exponential moving average state (ADC backend only -- the BQ27220
 * does its own smoothing internally). */
#define SMOOTH_SAMPLES 8
static uint32_t s_smooth_acc = 0;   /* accumulated mV * SMOOTH_SAMPLES */

/* Approximate max voltage (mV) at the ADC pin with 12 dB attenuation */
#define ADC_VREF_MV 3100

/* ---- voltage-to-percent look-up (ADC backend) ----
 * Based on community LiPo discharge measurements for the board.
 * The table defines key-points; mv_to_percent() linearly interpolates
 * between adjacent entries for a smooth percentage value. */
struct batt_step { int mv; int pct; };
static const batt_step BATT_TABLE[] = {
    { 4100, 100 },
    { 3950,  75 },
    { 3800,  50 },
    { 3600,  25 },
    {    0,   0 },
};

static const int BATT_TABLE_N = sizeof(BATT_TABLE) / sizeof(BATT_TABLE[0]);

static int mv_to_percent(int mv)
{
    /* Above the highest entry -> cap at 100 % */
    if (mv >= BATT_TABLE[0].mv)
        return BATT_TABLE[0].pct;

    /* Walk the table and linearly interpolate between adjacent entries */
    for (int i = 0; i < BATT_TABLE_N - 1; ++i) {
        int mv_lo  = BATT_TABLE[i + 1].mv;
        if (mv >= mv_lo) {
            int mv_hi  = BATT_TABLE[i].mv;
            int pct_hi = BATT_TABLE[i].pct;
            int pct_lo = BATT_TABLE[i + 1].pct;
            return pct_lo + (pct_hi - pct_lo) * (mv - mv_lo) / (mv_hi - mv_lo);
        }
    }
    return 0;
}

/* Voltage divider ratio default (overridden in battery_init) */
#define DIVIDER_RATIO_DEFAULT  3

/* Map GPIO number to ADC1 channel.  Only a handful of pins
 * are wired to ADC1 on the ESP32-S3; we support the ones
 * commonly used by the supported boards. */
static adc_channel_t gpio_to_channel(int gpio)
{
    switch (gpio) {
    case 1:  return ADC_CHANNEL_0;
    case 2:  return ADC_CHANNEL_1;
    case 3:  return ADC_CHANNEL_2;
    case 4:  return ADC_CHANNEL_3;
    case 5:  return ADC_CHANNEL_4;
    case 6:  return ADC_CHANNEL_5;
    case 7:  return ADC_CHANNEL_6;
    case 8:  return ADC_CHANNEL_7;
    case 9:  return ADC_CHANNEL_8;
    case 10: return ADC_CHANNEL_9;
    default:
        ESP_LOGW(TAG, "Unsupported battery GPIO%d, falling back to CH3 (GPIO4)", gpio);
        return ADC_CHANNEL_3;
    }
}

/* Pulse the enable GPIO on (if any), let the divider settle, return whether
 * we actually toggled it (so the caller can turn it off afterwards). */
static bool enable_divider(void)
{
    if (s_enable_gpio < 0) return false;
    gpio_set_level((gpio_num_t)s_enable_gpio, 1);
    /* Allow the divider a few ms to settle once powered. */
    vTaskDelay(pdMS_TO_TICKS(5));
    return true;
}

static void disable_divider(bool was_enabled)
{
    if (was_enabled && s_enable_gpio >= 0) {
        gpio_set_level((gpio_num_t)s_enable_gpio, 0);
    }
}

/* ---- public API ---- */

extern "C" int battery_init(int gpio_num, int enable_gpio, int divider)
{
    if (s_backend != BATT_BACKEND_NONE) return 0;

    /* Boards with no on-board battery monitor pass gpio_num < 0 (e.g. the
     * bare Waveshare E-Paper Driver HAT). Skip ADC setup entirely;
     * battery_read_mv()/battery_read_percent() will then return 0 / -1
     * and the editor UI will hide the icon. */
    if (gpio_num < 0) {
        ESP_LOGI(TAG, "Battery monitor disabled (no ADC pin configured)");
        return 0;
    }

    s_channel     = gpio_to_channel(gpio_num);
    s_enable_gpio = enable_gpio;
    s_divider     = (divider > 0) ? divider : DIVIDER_RATIO_DEFAULT;

    /* Configure the optional enable GPIO as an output and leave it driven
     * HIGH so the very first reading after init is valid. */
    if (s_enable_gpio >= 0) {
        gpio_config_t en_cfg = {};
        en_cfg.intr_type    = GPIO_INTR_DISABLE;
        en_cfg.mode         = GPIO_MODE_OUTPUT;
        en_cfg.pin_bit_mask = (1ULL << s_enable_gpio);
        en_cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
        en_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        if (gpio_config(&en_cfg) != ESP_OK) {
            ESP_LOGW(TAG, "Battery enable GPIO%d config failed", s_enable_gpio);
        }
        gpio_set_level((gpio_num_t)s_enable_gpio, 1);
    }

    /* Create oneshot ADC unit */
    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id  = ADC_UNIT_1;
    unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;

    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC unit init failed: %s", esp_err_to_name(err));
        return -1;
    }

    /* Configure channel */
    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten    = ADC_ATTEN_DB_12;
    chan_cfg.bitwidth = ADC_BITWIDTH_12;

    err = adc_oneshot_config_channel(s_adc_handle, s_channel, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel config failed: %s", esp_err_to_name(err));
        return -1;
    }

    /* Try to create calibration scheme (curve fitting on ESP32-S3) */
    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id  = ADC_UNIT_1;
    cali_cfg.chan     = s_channel;
    cali_cfg.atten   = ADC_ATTEN_DB_12;
    cali_cfg.bitwidth = ADC_BITWIDTH_12;

    err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ADC calibration not available, using raw conversion");
        s_cali_handle = NULL;
    }

    /* Seed the smoother with the first real reading */
    bool en = enable_divider();
    int raw = 0;
    adc_oneshot_read(s_adc_handle, s_channel, &raw);
    disable_divider(en);

    int pin_mv = 0;
    if (s_cali_handle) {
        adc_cali_raw_to_voltage(s_cali_handle, raw, &pin_mv);
    } else {
        pin_mv = (raw * ADC_VREF_MV) / 4095;
    }
    int bat_mv = pin_mv * s_divider;
    s_smooth_acc = (uint32_t)bat_mv * SMOOTH_SAMPLES;

    s_backend = BATT_BACKEND_ADC;
    ESP_LOGI(TAG, "Battery monitor initialized on GPIO%d (ch%d, div=%d, en=%d), "
             "initial %d mV", gpio_num, (int)s_channel, s_divider,
             s_enable_gpio, bat_mv);
    return 0;
}

/* ---- BQ27220 backend ---- */

static int bq27220_read_u16(uint8_t reg, uint16_t *out)
{
    if (!s_bq_dev) return -1;
    uint8_t wr[1] = { reg };
    uint8_t rd[2] = { 0, 0 };
    esp_err_t err = i2c_master_transmit_receive(s_bq_dev, wr, 1, rd, 2,
                                                100 /* ms */);
    if (err != ESP_OK) return -1;
    *out = (uint16_t)rd[0] | ((uint16_t)rd[1] << 8);
    return 0;
}

extern "C" int battery_init_bq27220(void *i2c_master_bus)
{
    if (s_backend != BATT_BACKEND_NONE) return 0;

    if (i2c_master_bus == NULL) {
        ESP_LOGW(TAG, "BQ27220 init: NULL I2C bus handle");
        return -1;
    }

    i2c_master_bus_handle_t bus = (i2c_master_bus_handle_t)i2c_master_bus;

    /* Probe the device first so an absent / dead fuel gauge does not
     * leave a half-attached handle behind. */
    if (i2c_master_probe(bus, BQ27220_I2C_ADDR, 100 /* ms */) != ESP_OK) {
        ESP_LOGW(TAG, "BQ27220 not responding at 0x%02X", BQ27220_I2C_ADDR);
        return -1;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = BQ27220_I2C_ADDR;
    dev_cfg.scl_speed_hz    = 400000;
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &s_bq_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BQ27220 bus_add_device failed: %s",
                 esp_err_to_name(err));
        s_bq_dev = NULL;
        return -1;
    }

    s_backend = BATT_BACKEND_BQ27220;

    uint16_t mv = 0, soc = 0;
    bq27220_read_u16(BQ27220_REG_VOLTAGE, &mv);
    bq27220_read_u16(BQ27220_REG_SOC, &soc);
    ESP_LOGI(TAG, "BQ27220 fuel gauge initialized at 0x%02X "
             "(initial %u mV, %u%%)", BQ27220_I2C_ADDR,
             (unsigned)mv, (unsigned)soc);
    return 0;
}

extern "C" int battery_read_mv(void)
{
    if (s_backend == BATT_BACKEND_BQ27220) {
        uint16_t mv = 0;
        if (bq27220_read_u16(BQ27220_REG_VOLTAGE, &mv) != 0) return 0;
        return (int)mv;
    }
    if (s_backend != BATT_BACKEND_ADC) return 0;

    bool en = enable_divider();
    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc_handle, s_channel, &raw);
    disable_divider(en);
    if (err != ESP_OK) return 0;

    int pin_mv = 0;
    if (s_cali_handle) {
        adc_cali_raw_to_voltage(s_cali_handle, raw, &pin_mv);
    } else {
        pin_mv = (raw * ADC_VREF_MV) / 4095;
    }

    int bat_mv = pin_mv * s_divider;

    /* Update exponential moving average */
    s_smooth_acc = s_smooth_acc - (s_smooth_acc / SMOOTH_SAMPLES) + (uint32_t)bat_mv;
    return (int)(s_smooth_acc / SMOOTH_SAMPLES);
}

extern "C" int battery_read_percent(void)
{
    if (s_backend == BATT_BACKEND_BQ27220) {
        uint16_t soc = 0;
        if (bq27220_read_u16(BQ27220_REG_SOC, &soc) != 0) return -1;
        if (soc > 100) soc = 100;
        return (int)soc;
    }
    if (s_backend != BATT_BACKEND_ADC) return -1;

    int mv = battery_read_mv();
    return mv_to_percent(mv);
}
