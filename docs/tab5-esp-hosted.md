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
schematic. They are encoded as `CONFIG_ESP_HOSTED_SDIO_PIN_*` and
`CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE` in
`sdkconfig.defaults.esp32p4`.

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
  map does not match the actual Tab5 wiring. Verify
  `CONFIG_ESP_HOSTED_SDIO_PIN_*` against the schematic of your
  specific Tab5 revision.
- **Wi-Fi associates but BLE never connects** -- the C6 image is
  Wi-Fi-only. Re-flash with a build that has
  `HCI over SDIO / BLE only` in its capabilities line (see the
  capabilities log above).

## Deep sleep

The Tab5 standby path is `DRAFTLING_STANDBY_DISPLAY_OFF` (MCU
stays awake, only the panel is blanked), so the C6 link is not
torn down. If the standby mode is ever switched to true deep
sleep on the Tab5, the C6 link must be deinitialised before
`esp_deep_sleep_start()` and re-initialised on wake -- see the
`host_shuts_down_slave_to_power_save` example in
`espressif/esp-hosted-mcu`.
