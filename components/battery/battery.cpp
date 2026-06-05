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
    BATT_BACKEND_INA226,
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
#define BQ27220_REG_FLAGS       0x06    /* u16 LE, bit 0 = DSG */
#define BQ27220_FLAGS_DSG       0x0001  /* 0 = charging or full */
#define BQ27220_REG_SOC         0x2C    /* u16 LE, 0-100 % */
static i2c_master_dev_handle_t   s_bq_dev = NULL;

/* ---- INA226 backend state ----
 * Only the bus-voltage and shunt-voltage registers are used.
 * INA226_REG_BUSVOLTAGE is a 16-bit unsigned value with an LSB of
 * 1.25 mV (datasheet section 7.6.3.3); reading 0x0CCC -> 4.095 V at
 * the BUS pin. The Tab5 routes the raw battery pack rail (NP-F550,
 * 2S Li-ion, ~6.0-8.4 V) to the BUS input, so the per-cell voltage
 * is bus_mv / cells.
 *
 * INA226_REG_SHUNTVOLTAGE is a signed 16-bit register with a 2.5 uV
 * LSB. Its sign indicates the direction of current flow across the
 * shunt and is therefore enough to detect "charging vs discharging"
 * without writing the calibration register (which would require the
 * exact shunt-resistor value from the schematic to convert the raw
 * value to amperes). On the Tab5 the IP2326 charger sits between
 * the USB-C input and the cell and the INA226 shunt is on the cell-
 * side rail. Empirically the shunt's IN+/IN- inputs are wired such
 * that current flowing *out* of the cell (i.e. the system running
 * on battery) produces a positive shunt-voltage reading, and current
 * flowing *into* the cell while charging produces a negative one --
 * so on the Tab5 a negative shunt voltage means "charging". A small
 * noise threshold filters out the few-microvolt offset reading the
 * INA226 returns when no current is flowing. */
#define INA226_REG_SHUNTVOLTAGE  0x01
#define INA226_REG_BUSVOLTAGE    0x02
#define INA226_BUSVOLTAGE_LSB_UV 1250    /* microvolts per LSB */
/* Noise threshold for charge detection, in raw LSB units of the
 * shunt-voltage register (2.5 uV/LSB). 8 LSB == 20 uV across the
 * shunt; with the typical 10 mohm Tab5 shunt that is ~2 mA, well
 * below idle MCU draw and well above the INA226 zero-offset. */
#define INA226_SHUNT_CHARGE_THRESHOLD 8
/* Per-cell voltage threshold below which the INA226 backend treats
 * the pack as "no battery attached". A real Li-ion cell is fully
 * discharged at ~3.0 V and the protection circuit cuts off well
 * before that, so anything below ~2.8 V/cell on the BUS rail means
 * the pack is missing (the rail then floats / leaks to a noisy few
 * volts -- on a 2S Tab5 we observed ~4-5 V total, which the
 * Li-ion discharge curve would otherwise misread as ~14-18 %). */
#define INA226_BATTERY_MIN_MV_PER_CELL 2800
static i2c_master_dev_handle_t   s_ina226_dev = NULL;
static int                       s_ina226_cells = 1;

/* Read the INA226 BUS voltage and return the per-cell value in mV,
 * or -1 if the device is not initialized or the I2C read fails. */
static int ina226_read_cell_mv(void)
{
    if (!s_ina226_dev) return -1;
    uint8_t wr[1] = { INA226_REG_BUSVOLTAGE };
    uint8_t rd[2] = { 0, 0 };
    if (i2c_master_transmit_receive(s_ina226_dev, wr, 1, rd, 2,
                                    100 /* ms */) != ESP_OK) {
        return -1;
    }
    /* Big-endian on the wire. Convert raw LSBs to mV per cell. */
    uint16_t raw = ((uint16_t)rd[0] << 8) | (uint16_t)rd[1];
    uint32_t bus_uv = (uint32_t)raw * INA226_BUSVOLTAGE_LSB_UV;
    int bus_mv = (int)(bus_uv / 1000U);
    int cells = (s_ina226_cells > 0) ? s_ina226_cells : 1;
    return bus_mv / cells;
}

/* True when the INA226 BUS rail looks like no pack is attached. */
static bool ina226_battery_absent(int cell_mv)
{
    return cell_mv < INA226_BATTERY_MIN_MV_PER_CELL;
}

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

/* ---- INA226 backend ---- */

extern "C" int battery_init_ina226(void *i2c_master_bus, int i2c_addr,
                                   int cells)
{
    if (s_backend != BATT_BACKEND_NONE) return 0;

    if (i2c_master_bus == NULL) {
        ESP_LOGW(TAG, "INA226 init: NULL I2C bus handle");
        return -1;
    }
    if (i2c_addr < 0 || i2c_addr > 0x7F) {
        ESP_LOGW(TAG, "INA226 init: invalid I2C address 0x%02X", i2c_addr);
        return -1;
    }
    if (cells < 1 || cells > 4) {
        ESP_LOGW(TAG, "INA226 init: implausible cell count %d, "
                 "clamping to 1", cells);
        cells = 1;
    }

    i2c_master_bus_handle_t bus = (i2c_master_bus_handle_t)i2c_master_bus;

    if (i2c_master_probe(bus, (uint8_t)i2c_addr, 100 /* ms */) != ESP_OK) {
        ESP_LOGW(TAG, "INA226 not responding at 0x%02X", i2c_addr);
        return -1;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = (uint16_t)i2c_addr;
    dev_cfg.scl_speed_hz    = 400000;
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &s_ina226_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "INA226 bus_add_device failed: %s",
                 esp_err_to_name(err));
        s_ina226_dev = NULL;
        return -1;
    }

    /* The INA226 powers up in continuous shunt+bus mode with 1.1 ms
     * conversion times by default, which is fine for a voltage-only
     * read. We deliberately do NOT touch the calibration register
     * (current/power LSBs depend on the shunt resistor wiring, which
     * is board-specific and irrelevant for percentage-from-voltage).
     */

    s_ina226_cells = cells;
    s_backend = BATT_BACKEND_INA226;

    int cell_mv = ina226_read_cell_mv();
    if (cell_mv < 0) {
        ESP_LOGW(TAG, "INA226 initialized at 0x%02X but BUS read failed",
                 i2c_addr);
    } else if (ina226_battery_absent(cell_mv)) {
        ESP_LOGI(TAG, "INA226 power monitor initialized at 0x%02X "
                 "(%dS pack, %d mV/cell -- no battery attached)",
                 i2c_addr, cells, cell_mv);
    } else {
        ESP_LOGI(TAG, "INA226 power monitor initialized at 0x%02X "
                 "(%dS pack, initial %d mV/cell, %d%%)", i2c_addr, cells,
                 cell_mv, mv_to_percent(cell_mv));
    }
    return 0;
}

extern "C" int battery_read_mv(void)
{
    if (s_backend == BATT_BACKEND_BQ27220) {
        uint16_t mv = 0;
        if (bq27220_read_u16(BQ27220_REG_VOLTAGE, &mv) != 0) return 0;
        return (int)mv;
    }
    if (s_backend == BATT_BACKEND_INA226) {
        int cell_mv = ina226_read_cell_mv();
        if (cell_mv < 0) return 0;
        /* Report 0 mV when the pack is missing so callers that only
         * look at voltage cannot infer a bogus "low battery" reading
         * from the floating BUS rail. */
        if (ina226_battery_absent(cell_mv)) return 0;
        return cell_mv;
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
    if (s_backend == BATT_BACKEND_INA226) {
        int cell_mv = ina226_read_cell_mv();
        if (cell_mv < 0) return -1;
        /* No battery attached -> let the status bar hide the value
         * instead of rendering ~14-18 % from the floating rail. */
        if (ina226_battery_absent(cell_mv)) return -1;
        return mv_to_percent(cell_mv);
    }
    if (s_backend != BATT_BACKEND_ADC) return -1;

    int mv = battery_read_mv();
    return mv_to_percent(mv);
}

extern "C" int battery_read_charging(void)
{
    if (s_backend == BATT_BACKEND_BQ27220) {
        /* Flags register, bit 0 (DSG): 0 -> charging or full, 1 -> discharging.
         * BQ27220 datasheet, table 12-7. */
        uint16_t flags = 0;
        if (bq27220_read_u16(BQ27220_REG_FLAGS, &flags) != 0) return -1;
        return (flags & BQ27220_FLAGS_DSG) ? 0 : 1;
    }
    if (s_backend == BATT_BACKEND_INA226) {
        if (!s_ina226_dev) return -1;
        /* If no battery is attached, charge state is meaningless. */
        int cell_mv = ina226_read_cell_mv();
        if (cell_mv < 0 || ina226_battery_absent(cell_mv)) return -1;
        uint8_t wr[1] = { INA226_REG_SHUNTVOLTAGE };
        uint8_t rd[2] = { 0, 0 };
        if (i2c_master_transmit_receive(s_ina226_dev, wr, 1, rd, 2,
                                        100 /* ms */) != ESP_OK) {
            return -1;
        }
        /* Big-endian on the wire; the register is signed two's complement. */
        int16_t raw = (int16_t)(((uint16_t)rd[0] << 8) | (uint16_t)rd[1]);
        if (raw <= -INA226_SHUNT_CHARGE_THRESHOLD) return 1;
        return 0;
    }
    /* ADC backend and "no backend" cannot tell. */
    return -1;
}
