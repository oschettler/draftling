#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Battery monitor.
 *
 * Two backends are exposed:
 *
 *   - ADC + resistive divider (battery_init): used on boards that
 *     wire the LiPo cell through a divider into an ESP32-S3 ADC1 pin
 *     (Waveshare ESP32-S3-RLCD-4.2, M5Stack PaperS3,
 *     Waveshare Touch-LCD-3.49). Voltage maps
 *     to percentage via a fixed 5-step LiPo discharge table:
 *       >= 4.10 V -> 100 %  (full)
 *       >= 3.95 V ->  75 %
 *       >= 3.80 V ->  50 %
 *       >= 3.60 V ->  25 %
 *       <  3.60 V ->   0 %  (empty)
 *     Readings are smoothed with an exponential moving average over
 *     8 samples to reduce ADC noise.
 *
 *   - TI BQ27220 fuel gauge over I2C (battery_init_bq27220): used on
 *     boards that route the cell through a dedicated coulomb counter
 *     (LilyGO T5 E-Paper S3 Pro / Pro Lite). The percentage comes
 *     straight from the gauge's StateOfCharge register and does not
 *     need software smoothing or a voltage-to-percent table.
 */

/*
 * Initialize the ADC for battery voltage measurement.
 * Must be called once before any other battery_* function.
 *
 *   gpio_num     -- ADC1 GPIO connected to the divider output
 *   enable_gpio  -- GPIO that powers the divider, or -1 if always on
 *   divider      -- multiplier from measured pin voltage back to the
 *                   battery voltage. Use 2 for a balanced 1:1 divider
 *                   (R_top == R_bottom, V_pin = V_bat / 2) and 3 for
 *                   a 2:1 divider where R_top is twice R_bottom
 *                   (V_pin = V_bat / 3).
 *
 * Returns 0 on success, non-zero on failure.
 */
int battery_init(int gpio_num, int enable_gpio, int divider);

/*
 * Initialize a BQ27220 fuel-gauge backend on an existing I2C master
 * bus (driver/i2c_master.h). The bus must already have been created
 * by the caller and remains owned by the caller (the battery
 * component only attaches a device handle).
 *
 *   i2c_master_bus -- opaque pointer to an i2c_master_bus_handle_t
 *                     (declared as void * here so the public header
 *                     does not need to pull in driver/i2c_master.h).
 *
 * Returns 0 on success, non-zero on failure. After a successful call,
 * `battery_read_mv()` reports the cell voltage in millivolts and
 * `battery_read_percent()` reports the StateOfCharge register
 * (0x2C) directly.
 *
 * Used on boards that route the LiPo cell through a TI BQ27220
 * coulomb counter on I2C (addr 0x55) instead of a GPIO ADC divider,
 * such as the LilyGO T5 E-Paper S3 Pro / Pro Lite.
 */
int battery_init_bq27220(void *i2c_master_bus);

/*
 * Initialize a TI INA226 power-monitor backend on an existing I2C
 * master bus. The INA226 measures battery rail voltage on its BUS
 * input; this driver only reads the bus voltage register and maps
 * it to percentage via a Li-ion discharge curve scaled by `cells`.
 *
 *   i2c_master_bus -- opaque pointer to an i2c_master_bus_handle_t
 *                     (driver/i2c_master.h).
 *   i2c_addr       -- I2C address of the INA226 (0x40..0x4F). The
 *                     M5Stack Tab5 wires the part at 0x41.
 *   cells          -- number of Li-ion cells in series the INA226
 *                     bus rail measures. Use 1 for a single 18650 /
 *                     LiPo cell (3.0-4.2 V) and 2 for an NP-F550-
 *                     style 2S pack (6.0-8.4 V, e.g. the Tab5).
 *
 * Returns 0 on success, non-zero on failure. After a successful
 * call, `battery_read_mv()` reports the per-cell voltage (so the
 * existing 3.0-4.2 V LiPo mapping in mv_to_percent() applies
 * regardless of `cells`) and `battery_read_percent()` runs that
 * voltage through the same lookup table the ADC backend uses.
 *
 * The INA226 has no SoC / coulomb-count state, so percentage is
 * voltage-derived and approximate.
 */
int battery_init_ina226(void *i2c_master_bus, int i2c_addr, int cells);

/*
 * Read the current battery voltage in millivolts.
 * Returns 0 if the sensor has not been initialized.
 */
int battery_read_mv(void);

/*
 * Read the battery charge percentage (0-100).
 * Returns -1 if the sensor has not been initialized.
 */
int battery_read_percent(void);

/*
 * Report whether the battery is currently being charged from an
 * external source (USB-C / DC jack).
 *
 * Returns:
 *    1 -- charging (current flowing into the cell)
 *    0 -- discharging or idle (running on battery only / full)
 *   -1 -- unknown (backend cannot detect charge state, or not initialized)
 *
 * Backend support matrix:
 *   - INA226   (M5Stack Tab5): reads the signed shunt-voltage register
 *               and reports the polarity. Sign convention is positive =
 *               into the cell on this board.
 *   - BQ27220  (LilyGO T5 Pro / Pro Lite): reads the Flags register
 *               (0x06); the DSG bit (bit 0) is 0 while charging or
 *               full, 1 while discharging.
 *   - ADC backend / no backend: always returns -1.
 *
 * Callers (e.g. the editor status bar) should hide the charging
 * indicator when this function returns -1.
 */
int battery_read_charging(void);

#ifdef __cplusplus
}
#endif
