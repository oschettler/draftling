# LilyGO T5 E-Paper S3 Pro / Pro Lite — deep-sleep power budget

The LilyGO T5 board carries an ESP32-S3, a 1500 mAh NP-F-class
Li-ion pack, an EPDIY-driven e-paper panel (TPS65185 + PCA9535
expander), a GT911 capacitive touch controller, a BQ27220 fuel
gauge, a BQ25896 charger, a PCF8563TS RTC, a MicroSD slot, and
on the *Pro* variant an SX1262 LoRa radio plus a MIA-M10Q GPS.

With Draftling's default deep-sleep path the board drew **~30 mA**
in sleep, draining the pack in roughly two days. The cause was a
handful of peripherals that the ESP-IDF default `esp_deep_sleep_start()`
does not touch -- it powers down the digital domain but leaves the
RTC peripherals up and leaves analog peripherals in whatever state
their drivers left them.

The fix is `pre_sleep_t5_deinit()` in `main/main.cpp`, registered
as the standby pre-sleep callback under
`CONFIG_DRAFTLING_MODEL_LILYGO_T5_EPD_S3_PRO[_LITE]`. It walks the
following list of peripherals immediately before
`esp_deep_sleep_start()` runs:

| Peripheral             | Sleep state                            | Mechanism                                                                                                  |
| ---------------------- | -------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| GT911 touch controller | sleep mode (~8 µA)                     | `touchscreen_sleep()` writes `0x05` to register `0x8040` (GT911 DS §6).                                    |
| EPDIY (TPS65185 + PCA9535) | TPS65185 OFF (<1 µA)               | `display_deep_sleep_prepare()` → `epd_poweroff()` drives `WAKEUP` LOW via the PCA9535, then `epd_deinit()`. |
| Front-light LED (GPIO 11) | held LOW across deep sleep         | `display_deep_sleep_prepare()` zeroes LEDC duty, then `gpio_reset_pin` + `gpio_set_level(0)` + `gpio_hold_en` so the pad stays at 0 V; `gpio_deep_sleep_hold_en()` latches the hold. |
| SX1262 LoRa (Pro only) | SetSleep cold start (~600 nA)         | `t5_lora_sleep()` issues the 2-byte SPI sequence `0x84 0x00` over SPI3 (sx126x DS §13.1.7). Harmless NACK on the Lite variant where the chip is depopulated. |
| MicroSD card           | bus released, card to standby          | `sd_card_deinit()` calls `esp_vfs_fat_sdcard_unmount`, removes the SPI device, then `spi_bus_free(SPI3_HOST)`. |
| Unused RTC-IO pins     | isolated                                | `t5_isolate_unused_gpios()` calls `rtc_gpio_isolate()` on every pin in GPIO 0–21 that is not in use (skipping BOOT, the front-light, and the four SPI3 pins). |
| RTC peripherals domain | powered off                             | `esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF)` -- safe because Draftling has no ULP and no RTC SLOW data to retain across wake. |
| BQ25896 charger        | autonomous (intentional)               | left running so a USB plug-in still tops the pack up during sleep. |
| BQ27220 fuel gauge     | autonomous (intentional)               | low-power coulomb counter, idle current ~7 µA. |
| PCF8563 RTC            | autonomous (intentional)               | idle current ~250 nA. |

The sequence in `pre_sleep_t5_deinit()` matters: the touch
controller and EPDIY must be put to sleep *before* their I2C bus is
torn down by `epd_deinit()`, and the SX1262 must be put to sleep
*before* `sd_card_deinit()` frees SPI3.

After this hook, measured sleep current is in the tens of µA --
several orders of magnitude below the original 30 mA -- giving
roughly a month of idle on a fully charged pack.

## What we do **not** touch

- `pre_sleep_autosave()` runs first inside `pre_sleep_t5_deinit()`
  to save the open document, persist cursor/scroll metadata, and
  wipe the panel to white. This is the standard cross-board
  pre-sleep work shared with PaperS3.
- The BQ25896 charger and BQ27220 fuel gauge are left on so the
  battery keeps charging while the MCU sleeps and the SoC has an
  accurate percentage reading on the next wake.
- The PCF8563 RTC is left on so the on-screen clock survives sleep.

## Cross-references

- `components/touchscreen/touchscreen.cpp` -- `touchscreen_sleep()` (GT911 0x8040 cmd 0x05).
- `components/display/display_epdiy.cpp` -- `display_deep_sleep_prepare()` (LEDC zero, GPIO 11 hold, epdiy teardown).
- `components/sd_card/sd_card.cpp` -- `sd_card_deinit()` (SPI bus free).
- `main/main.cpp` -- `pre_sleep_t5_deinit()` (orchestrator), `t5_lora_sleep()`, `t5_isolate_unused_gpios()`.
- `components/standby/standby.cpp` -- `standby_enter_sleep()` calls the registered pre-sleep callback before `esp_deep_sleep_start()`.
