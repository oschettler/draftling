# Copilot Instructions

## Code Style

- Do not use non-ASCII characters in code, comments, or string literals. All source files must be ASCII-only.

## Project Overview

Draftling is a distraction-free Markdown text editor for ESP32-S3-based
development boards with reflective LCD displays. It is built with the
ESP-IDF framework (v5.3+) and uses LVGL v9 for the graphical interface.

The user connects a Bluetooth keyboard and edits Markdown files stored on
a MicroSD card. The reflective LCD needs no backlight and works well in
daylight. On request the device connects to WiFi and synchronizes files
with a remote Git repository via the GitHub REST API.

### Supported Hardware

| Board | Display |
|-------|---------|
| Waveshare ESP32-S3-RLCD-4.2 | 4.2-inch reflective LCD, 400x300 |

## Repository Layout

```
CMakeLists.txt              Top-level CMake project file
partitions.csv              Custom partition table (16 MB flash)
sdkconfig.defaults          Common Kconfig defaults for all targets
sdkconfig.defaults.esp32s3  ESP32-S3-specific defaults (PSRAM, BLE, WiFi)
main/                       Application entry point and hardware config
  main.cpp                  app_main(): initializes all subsystems
  app_config.h              Pin definitions and display macros per board
  Kconfig.projbuild         Menuconfig: hardware model, display size, rotation
  idf_component.yml         IDF component manifest (depends on lvgl ^9.2)
  CMakeLists.txt            Registers main as an IDF component
components/                 Reusable IDF components
  battery/                  Battery voltage monitor (ADC)
  ble_keyboard/             BLE HID keyboard host (Bluedroid)
  display/                  RLCD SPI display driver and LVGL port
  editor/                   Gap-buffer text editor, Markdown parser, LVGL UI
  fonts/                    Custom LVGL bitmap fonts (Greybeard family)
  git_sync/                 GitHub REST API file synchronization
  kb_layout/                Keyboard layout translation (US/UA/DE/FR)
  sd_card/                  SD card (SDMMC 1-bit) file operations
  standby/                  Deep-sleep / standby inactivity timer
  wifi_manager/             WiFi STA connection manager
```

## Component Details

### main/

The application entry point. `app_main()` in `main.cpp` initializes every
subsystem in order: NVS flash, display hardware, LVGL, battery monitor,
editor UI, SD card, BLE keyboard, WiFi manager, Git sync, and standby
timer. It also registers an auto-save callback that persists the current
document before entering deep sleep.

`app_config.h` maps Kconfig hardware model selections to concrete GPIO pin
numbers and display dimensions. Each supported board has its own `#if`
block defining SPI pins (MOSI, SCK, DC, CS, RST, TE), SD card pins (CLK,
CMD, D0), I2C pins, the battery ADC pin, and the deep-sleep wakeup GPIO.

### components/battery/

Reads battery voltage through a 3:1 resistive divider on GPIO4 using the
ESP32 ADC. Applies exponential moving average smoothing over 8 samples.
Maps voltage to a percentage: >=4.10V is 100%, >=3.95V is 75%, >=3.80V
is 50%, >=3.60V is 25%, below 3.60V is 0%.

Public API: `battery_init()`, `battery_read_mv()`, `battery_read_percent()`.

### components/ble_keyboard/

BLE HID keyboard host built on ESP-IDF Bluedroid. Handles device scanning,
pairing with passkey authentication, connection/disconnection callbacks,
and keyboard event dispatching. Each key event carries the HID keycode,
ASCII character, modifier flags, and pressed/released state.

Public API: `ble_keyboard_init()`, `ble_keyboard_start_scan()`,
`ble_keyboard_is_connected()`, `ble_keyboard_set_callback()`, and
several other callback registration functions.

### components/display/

Two-file driver. `display.cpp` talks directly to the Waveshare RLCD
hardware over SPI (pins configured in `app_config.h`). `lvgl_port.cpp`
creates the LVGL display object, sets up a flush callback, and runs the
LVGL tick/task timer. Thread safety is provided by a mutex exposed as
`lvgl_port_lock()` / `lvgl_port_unlock()`.

Public API: `display_init()`, `display_clear()`, `display_flush()`,
`lvgl_port_init()`, `lvgl_port_lock()`, `lvgl_port_unlock()`.

### components/editor/

The largest component. Contains:

- **editor.cpp** -- gap-buffer text engine (256 KB document limit) with
  cursor movement, selection, clipboard, insert/delete, and file I/O.
- **editor_ui.cpp** -- LVGL-based user interface: title bar with battery
  and layout indicators, scrollable text area with Markdown rendering,
  status bar, file browser dialog, and settings menu (F1).
- **md_parser.cpp** -- single-pass Markdown line parser. Recognizes
  headings H1-H4, bullet and numbered lists, blockquotes, code fences,
  horizontal rules, and inline bold/italic/code/strikethrough spans.
- **draftling_logo.c** -- embedded LVGL image for the splash screen.

Public API: `editor_init()`, `editor_open_file()`, `editor_save_file()`,
`editor_ui_init()`, `editor_ui_handle_key()`, `md_parse_line()`, and many
cursor/selection/clipboard helpers.

### components/fonts/

Custom LVGL bitmap fonts generated from the Greybeard typeface. See the
dedicated section below for the full creation process.

### components/git_sync/

Synchronizes Markdown files between the SD card and a remote GitHub
repository using the GitHub REST API over HTTPS. Reads configuration
(repo URL, branch, token, path prefix) from `/sdcard/git.cfg`. Supports
pull, push, or bidirectional sync with state callbacks and error tracking.

Public API: `git_sync_init()`, `git_sync_start()`,
`git_sync_get_state()`, `git_sync_is_configured()`.

### components/kb_layout/

Translates HID keycodes and modifier flags into UTF-8 character strings
for the active keyboard layout. Supports US-English (QWERTY), Ukrainian
(Cyrillic), German (QWERTZ), and French (AZERTY). Each layout can be
independently enabled or disabled at build time via Kconfig (see
`components/kb_layout/Kconfig.projbuild`). The active layout is cycled
at runtime with `kb_layout_next()`.

Public API: `kb_layout_translate()`, `kb_layout_set()`, `kb_layout_get()`,
`kb_layout_name()`, `kb_layout_next()`.

### components/sd_card/

Initializes the MicroSD card over SDMMC 1-bit interface and mounts it as
a FAT filesystem at `/sdcard`. Provides standard file operations (read,
write, append, delete, rename, existence check, size query) and directory
operations (mkdir, list).

Public API: `sd_card_init()`, `sd_card_read_file()`,
`sd_card_write_file()`, `sd_card_list_dir()`, `sd_card_file_exists()`,
and others.

### components/standby/

Monitors user inactivity and enters ESP32 deep sleep after a configurable
timeout (default 600 seconds / 10 minutes). The timeout is persisted in
NVS so it survives reboots. A pre-sleep callback allows the editor to
auto-save before power-down. On the Waveshare RLCD board, wakeup is
triggered by pressing the GPIO18 button (EXT0, active-low).

Public API: `standby_init()`, `standby_reset_timer()`,
`standby_set_timeout()`, `standby_set_pre_sleep_cb()`,
`standby_enter_sleep()`.

### components/wifi_manager/

Manages WiFi in station mode. Reads credentials from NVS or from
`/sdcard/wifi.cfg`. Provides connect, disconnect, and state query
functions with an event callback for connection state changes (idle,
connecting, connected, disconnected, error). Required by `git_sync` for
network access.

Public API: `wifi_manager_init()`, `wifi_manager_connect()`,
`wifi_manager_disconnect()`, `wifi_manager_is_connected()`,
`wifi_manager_get_ip()`, `wifi_manager_get_ssid()`.

## Font Creation Process

The `components/fonts/` directory contains custom LVGL bitmap fonts
generated from the **Greybeard** typeface, a monospaced bitmap font that
is a vector port of UW ttyp0 (MIT-licensed, source:
https://github.com/flowchartsman/greybeard).

### Source Files

Greybeard ships as a set of TTF files, each designed for a single native
pixel size. Because the outlines trace exact pixel boundaries, rendering
at the native size with 1-bit-per-pixel (no antialiasing) produces
pixel-perfect glyphs with no scaling artifacts.

### Generation Tool

The fonts were converted to LVGL C source files using **lv_font_conv**,
the official LVGL font conversion utility. The exact command line is
recorded in the header comment of each generated `.c` file. For example,
the 14 px font was generated with:

```
lv_font_conv \
  --font Greybeard-14px.ttf \
  -r 0x20-0x7F,0xA0-0xFF,0x400-0x4FF,0x20AC,0x20B4,0x2116 \
  --size 14 \
  --bpp 1 \
  --format lvgl \
  --no-compress \
  --lv-font-name greybeard_14 \
  -o greybeard_14.c
```

### Unicode Ranges

Every font file includes the same set of Unicode ranges so that all
keyboard layouts (US, UA, DE, FR) can share a single font set:

| Range | Description |
|-------|-------------|
| U+0020 - U+007F | Basic Latin (ASCII printable characters) |
| U+00A0 - U+00FF | Latin-1 Supplement (accented Latin characters, symbols) |
| U+0400 - U+04FF | Cyrillic (Ukrainian and other Slavic alphabets) |
| U+20AC | Euro sign |
| U+20B4 | Hryvnia sign (Ukrainian currency) |
| U+2116 | Numero sign |

### Sizes and Metrics

Six sizes are provided. All except the 26 px variant are rendered at
their native TTF pixel size. The 26 px font is scaled from the 22 px
TTF source.

| File | Pixel Size | Char Width | Line Height | Notes |
|------|-----------|------------|-------------|-------|
| greybeard_11.c | 11 | 6 | 11 | Smallest, used for compact UI elements |
| greybeard_14.c | 14 | 7 | 13 | Default body text |
| greybeard_16.c | 16 | 8 | 15 | |
| greybeard_18.c | 18 | 9 | 17 | |
| greybeard_22.c | 22 | 11 | 21 | Headings |
| greybeard_26.c | 26 | 13 | 25 | Largest heading (scaled from 22 px TTF) |

All fonts are declared in `components/fonts/include/greybeard.h` as
`extern const lv_font_t greybeard_NN` and compiled as an IDF component
that depends on `lvgl__lvgl`.

### Rendering Settings

- **Bits per pixel (bpp):** 1 (pure black and white, no antialiasing).
  This matches the reflective LCD which has no grayscale capability.
- **Compression:** disabled (`--no-compress`) for faster glyph lookup on
  the microcontroller.
- **Format:** `lvgl` (native LVGL font structure).

## Hardware Definitions in Kconfig.projbuild

There are two Kconfig.projbuild files that expose project-specific
menuconfig options.

### main/Kconfig.projbuild -- Hardware Configuration

This file defines the **DRAFTLING Configuration** menu with the following
options:

#### Hardware Model (DRAFTLING_HARDWARE_MODEL)

A `choice` that selects the target board. Currently the only option is:

- **DRAFTLING_MODEL_WAVESHARE_RLCD42** -- Waveshare ESP32-S3-RLCD-4.2
  with a 400x300 reflective LCD and GPIO18 deep-sleep wakeup button.
  This option depends on `IDF_TARGET_ESP32S3`.

The hardware model selection drives two hidden `int` symbols that other
code reads at compile time:

- **DRAFTLING_DISPLAY_WIDTH** -- display width in pixels (400 for the
  Waveshare RLCD).
- **DRAFTLING_DISPLAY_HEIGHT** -- display height in pixels (300 for the
  Waveshare RLCD).

These values are consumed in `main/app_config.h` as `DISPLAY_WIDTH` and
`DISPLAY_HEIGHT` macros.

#### Display Rotation (DRAFTLING_DISPLAY_ROTATE)

A `choice` that sets the display rotation angle. Options are 0, 90, 180,
and 270 degrees. The default is 0 (no rotation). The selected angle is
exposed as the hidden `int` symbol **DRAFTLING_DISPLAY_ROTATE_ANGLE**,
consumed in `app_config.h` as `DISPLAY_ROTATE`.

### components/kb_layout/Kconfig.projbuild -- Keyboard Layouts

This file defines the **DRAFTLING Keyboard Layouts** menu. Each layout
is an independent `bool` option that can be enabled or disabled:

| Symbol | Layout | Default |
|--------|--------|---------|
| KB_LAYOUT_ENABLE_US | US-English (QWERTY) | y (enabled) |
| KB_LAYOUT_ENABLE_UA | Ukrainian (Cyrillic) | y (enabled) |
| KB_LAYOUT_ENABLE_DE | German (QWERTZ) | n (disabled) |
| KB_LAYOUT_ENABLE_FR | French (AZERTY) | n (disabled) |

Disabling unused layouts saves flash space because the translation
tables for disabled layouts are excluded from the build. The `kb_layout`
component reads these symbols at compile time to conditionally compile
only the enabled layout tables.

## Building

Requires ESP-IDF v5.3 or later.

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

If you update `sdkconfig.defaults` or pull new changes, delete the
generated `sdkconfig` so the defaults are re-applied:

```bash
rm -f sdkconfig
idf.py set-target esp32s3
idf.py build
```
