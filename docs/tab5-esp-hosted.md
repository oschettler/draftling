# Wi-Fi + Bluetooth on the M5Stack Tab5 (ESP-Hosted-MCU)

The ESP32-P4 has no on-chip 2.4 GHz radio. On the M5Stack Tab5 the
P4 is paired with an on-board ESP32-C6 module that provides Wi-Fi
and BLE; the two chips are wired together over SDIO. Draftling
reaches the C6 through
[ESP-Hosted-MCU](https://github.com/espressif/esp-hosted-mcu):

- `wifi_manager` (Git sync) calls stock `esp_wifi_*` APIs, which
  `espressif/esp_wifi_remote` redirects into hosted RPCs.
- `ble_keyboard` (BLE HID host) brings up Bluedroid the usual way
  but attaches a hosted-VHCI HCI driver instead of the native
  `esp_bt_controller` (the latter does not exist on the P4). The
  switch is gated on `CONFIG_BT_CONTROLLER_DISABLED`, which the
  Tab5 sdkconfig sets.

The P4-side wiring is automatic: it is enabled in
`sdkconfig.defaults.esp32p4`, and `main/main.cpp` calls
`esp_hosted_init()` + `esp_hosted_connect_to_slave()` before
`ble_keyboard_init()` / `wifi_manager_connect()`. The host
component is pulled in by `main/idf_component.yml` under a
`target == esp32p4` rule, so ESP32-S3 builds are unaffected.

## Tab5 P4↔C6 pin map

| Function | P4 GPIO |
|----------|--------:|
| SDIO CLK | 12 |
| SDIO CMD | 13 |
| SDIO D0  | 11 |
| SDIO D1  | 10 |
| SDIO D2  |  9 |
| SDIO D3  |  8 |
| C6 RESET (active LOW) | 15 |

These match the M5Tab5-UserDemo reference firmware and the Tab5
schematic. They are encoded in `sdkconfig.defaults.esp32p4` as the
per-slot `CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_*_SLOT_1` symbols plus
`CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE` (slot 1 is the P4 default
and is the SDMMC slot wired to the on-board C6).

> **Pitfall.** In `espressif/esp_hosted` ≥ 2.x the
> `CONFIG_ESP_HOSTED_SDIO_PIN_CLK / _CMD / _D0..D3` symbols are
> *non-promptable derived ints* whose values are computed from the
> per-slot `_PRIV_SDIO_PIN_*_SLOT_{0,1}` defaults. Setting
> `CONFIG_ESP_HOSTED_SDIO_PIN_CLK=12` in `sdkconfig.defaults` is
> silently dropped by Kconfig, the upstream P4/SLOT_1 defaults
> (CLK 18, CMD 19, D0 14, D1 15, D2 16, D3 17) win, and SDIO
> bring-up fails with `sdmmc_init_ocr: send_op_cond (1) returned
> 0x107` because the upstream D1 default (15) is the same pin the
> Tab5 uses for the C6 reset line. Always override the
> `_PRIV_SDIO_PIN_*_SLOT_1` knobs instead.

## One-time C6 slave firmware flashing

The C6 needs the ESP-Hosted slave firmware. Draftling does not
build slave firmware from source; use M5Stack's prebuilt image
which is already paired with the C6's USB / UART pads on the Tab5:

- Image: `ESP32C6-WiFi-SDIO-Interface-V1.4.1-96bea3a_0x0.bin`
  (from `platforms/tab5/wifi_c6_fw/` in
  [`m5stack/M5Tab5-UserDemo`](https://github.com/m5stack/M5Tab5-UserDemo)).
  This is the same image M5Stack ships, built from a known-good
  esp-hosted-mcu commit. Flash it once at offset `0x0`:

  ```bash
  esptool.py --chip esp32c6 \
             -b 1500000 \
             --before default_reset --after hard_reset \
             write_flash 0x0 ESP32C6-WiFi-SDIO-Interface-V1.4.1-96bea3a_0x0.bin
  ```

  On the Tab5 the C6 is exposed through a dedicated USB-CDC /
  UART path; consult the Tab5 manual for the exact bootloader
  enter sequence. Refer to `flash.sh` in the M5Tab5-UserDemo
  repository for the canonical command line.

- Alternative: build and flash from
  [`espressif/esp-hosted-mcu`](https://github.com/espressif/esp-hosted-mcu)
  with `idf.py set-target esp32c6` against the `slave/` project,
  then flash the resulting binary at offset `0x0` the same way.

The C6 firmware persists across P4 reflashes; re-flashing it is
only necessary if you upgrade the host-side `espressif/esp_hosted`
component to a version whose RPC interface is incompatible with
the slave image you currently have on the C6.

## Expected boot log

On a successful link bring-up `main` prints lines similar to:

```
I (xxxx) Draftling: Initializing ESP-Hosted link to ESP32-C6...
I (xxxx) transport: Attempt connection with slave: retry[0]
I (xxxx) transport: Reset slave using GPIO[15]
I (xxxx) sdio_wrapper: SDIO master: Data-Lines: 4-bit Freq(KHz)[40000 KHz]
I (xxxx) sdio_wrapper: GPIOs: CLK[12] CMD[13] D0[11] D1[10] D2[9] D3[8] Slave_Reset[15]
I (xxxx) H_SDIO_DRV: SDIO Host operating in PACKET MODE
I (xxxx) transport: Received INIT event from ESP32 peripheral
I (xxxx) transport: capabilities: 0xd
I (xxxx) transport:      * WLAN
I (xxxx) transport:        - HCI over SDIO
I (xxxx) transport:        - BLE only
I (xxxx) Draftling: ESP-Hosted link up
I (xxxx) Draftling: Initializing Bluetooth keyboard...
I (xxxx) BLEKeyboard: Hosted BT controller enabled (BLE on C6)
I (xxxx) BLEKeyboard: Hosted VHCI attached to Bluedroid
I (xxxx) BLEKeyboard: Bluedroid enabled
```

## Common bring-up failures

- **`Reset slave using GPIO[15]` then a hang or
  `Failed to communicate with slave`** -- the C6 is not running
  slave firmware, or the slave image is incompatible with the
  host-side `espressif/esp_hosted` version. Re-flash the C6 with
  the image listed above.
- **`SDIO read/write error` or `CMD53 timeout`** -- usually a
  signal-integrity issue with the SDIO bus; lower
  `CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ` (e.g. to `20000`) and
  re-test.
- **`esp_hosted_connect_to_slave failed (-1)`** -- the SDIO pin
  map does not match the actual Tab5 wiring. Verify the
  `CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_*_SLOT_1` values (CLK / CMD /
  D0 / D1_4BIT_BUS / D2_4BIT_BUS / D3_4BIT_BUS) against the
  schematic of your specific Tab5 revision; do **not** edit the
  `CONFIG_ESP_HOSTED_SDIO_PIN_*` symbols (see "Pitfall" above).
- **Wi-Fi associates but BLE never connects** -- the C6 image is
  Wi-Fi-only. Re-flash with a build that has
  `HCI over SDIO / BLE only` in its capabilities line (see the
  capabilities log above).

## Deep sleep

The Tab5 standby path is real ESP32-P4 deep sleep
(`esp_deep_sleep_start()`). The GT911 touch INT (GPIO 23) and the
on-board USER button are not wired to LP_IO pins (GPIO 0-15) on
the P4, so no GPIO wake source is available. The chip therefore
wakes only via the hardware **RESET** button: it cold-boots and
the editor restores from autosave on the next run.

Implications:
- Editor state in PSRAM is lost across standby; rely on autosave.
- The ESP32-C6 co-processor stays powered through P4 deep sleep
  (the C6 has no enable pin we control); the SDIO link is torn
  down implicitly when the P4's SDIO peripheral powers off, and
  `esp_hosted_init()` re-establishes it on the next cold boot.
  No explicit teardown is needed in `standby_enter_sleep()`.
- `DRAFTLING_STANDBY_WAKE_ON_TOUCH` must NOT be enabled on Tab5:
  GPIO 23 is not LP_IO and the EXT0 API is not available on the
  P4 at all (`SOC_PM_SUPPORT_EXT0_WAKEUP` is undefined).

If a future Tab5 hardware revision wires a user input to an LP_IO
pin, switch `components/standby/standby.cpp` to
`esp_deep_sleep_enable_gpio_wakeup()` (the P4 equivalent of EXT0)
for that pin.
