/*
 * Battery voltage monitor for the Waveshare ESP32-S3-RLCD-4.2.
 *
 * GPIO4 is connected to ADC1 channel 3 through a 3:1 resistive divider
 * (200 K + 100 K).  The ESP-IDF ADC oneshot driver with curve-fitting
 * calibration converts the raw ADC reading to millivolts at the pin,
 * and we multiply by 3 to recover the actual cell voltage.
 */

#include "battery.h"

#include <esp_log.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>

static const char *TAG = "Battery";

/* ADC handles */
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t         s_cali_handle = NULL;
static adc_channel_t             s_channel = ADC_CHANNEL_3;
static bool                      s_initialized = false;

/* Exponential moving average state */
#define SMOOTH_SAMPLES 8
static uint32_t s_smooth_acc = 0;   /* accumulated mV * SMOOTH_SAMPLES */

/* Approximate max voltage (mV) at the ADC pin with 12 dB attenuation */
#define ADC_VREF_MV 3100

/* ---- voltage-to-percent look-up ----
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

/* Voltage divider ratio on the board (200 K + 100 K -> 3:1) */
#define DIVIDER_RATIO  3

/* Map GPIO number to ADC1 channel.  Only a handful of pins
 * are wired to ADC1 on the ESP32-S3; we support the ones
 * that Waveshare could plausibly use. */
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

/* ---- public API ---- */

extern "C" int battery_init(int gpio_num)
{
    if (s_initialized) return 0;

    s_channel = gpio_to_channel(gpio_num);

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
    int raw = 0;
    adc_oneshot_read(s_adc_handle, s_channel, &raw);
    int pin_mv = 0;
    if (s_cali_handle) {
        adc_cali_raw_to_voltage(s_cali_handle, raw, &pin_mv);
    } else {
        pin_mv = (raw * ADC_VREF_MV) / 4095;
    }
    int bat_mv = pin_mv * DIVIDER_RATIO;
    s_smooth_acc = (uint32_t)bat_mv * SMOOTH_SAMPLES;

    s_initialized = true;
    ESP_LOGI(TAG, "Battery monitor initialized on GPIO%d (ch%d), "
             "initial %d mV", gpio_num, (int)s_channel, bat_mv);
    return 0;
}

extern "C" int battery_read_mv(void)
{
    if (!s_initialized) return 0;

    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc_handle, s_channel, &raw);
    if (err != ESP_OK) return 0;

    int pin_mv = 0;
    if (s_cali_handle) {
        adc_cali_raw_to_voltage(s_cali_handle, raw, &pin_mv);
    } else {
        pin_mv = (raw * ADC_VREF_MV) / 4095;
    }

    int bat_mv = pin_mv * DIVIDER_RATIO;

    /* Update exponential moving average */
    s_smooth_acc = s_smooth_acc - (s_smooth_acc / SMOOTH_SAMPLES) + (uint32_t)bat_mv;
    return (int)(s_smooth_acc / SMOOTH_SAMPLES);
}

extern "C" int battery_read_percent(void)
{
    if (!s_initialized) return -1;

    int mv = battery_read_mv();
    return mv_to_percent(mv);
}
