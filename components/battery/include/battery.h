#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Battery voltage monitor for the Waveshare ESP32-S3-RLCD-4.2.
 *
 * The board has a 3:1 resistive voltage divider (200 K / 100 K) on GPIO4
 * (ADC1 channel 3) that scales the LiPo cell voltage into the ADC range.
 *
 * Voltage-to-percentage mapping (from community measurements):
 *   >= 4.10 V  ->  100 %   (full)
 *   >= 3.95 V  ->   75 %
 *   >= 3.80 V  ->   50 %
 *   >= 3.60 V  ->   25 %
 *   <  3.60 V  ->    0 %   (empty)
 *
 * Readings are smoothed with an exponential moving average over 8 samples
 * to reduce ADC noise.
 */

/*
 * Initialize the ADC for battery voltage measurement.
 * Must be called once before any other battery_* function.
 *
 *   gpio_num     -- ADC1 GPIO connected to the divider output
 *   enable_gpio  -- GPIO that powers the divider, or -1 if always on
 *   divider      -- voltage divider ratio (e.g. 2 for 1:1, 3 for 2:1 to GND)
 *
 * Returns 0 on success, non-zero on failure.
 */
int battery_init(int gpio_num, int enable_gpio, int divider);

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

#ifdef __cplusplus
}
#endif
