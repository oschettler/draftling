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
| Seeed Studio reTerminal E1001 | 7.5-inch e-paper (UC8179), 800x480 |
| Waveshare E-Paper Driver HAT (on any BLE-capable ESP32 host, default ESP32-S3-DevKitC-1 wiring) | UC8179 e-paper, configurable resolution (default 800x480) |
| M5Stack PaperS3 | 4.7-inch e-paper (ED047TC1), 540x960 |

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
block defining SPI pins (MOSI, SCK, DC, CS, RST, TE/BUSY), SD card pins
(CLK, CMD, D0 for SDMMC, or MOSI/MISO/SCK/CS for SPI), I2C pins, the
battery ADC pin, and the deep-sleep wakeup GPIO. The Waveshare E-Paper
Driver HAT block resolves all of its pin macros from `CONFIG_DRAFTLING_HAT_*`
Kconfig values so users can adapt the HAT to a different ESP32-S3 host
board without editing source. The M5Stack PaperS3 block omits the panel
data-bus and control-line pins because the `m5stack/M5GFX` library
configures them internally based on the board id.

### components/battery/

Reads battery voltage through a resistive divider on a configurable
ADC pin using the ESP32 ADC. Applies exponential moving average
smoothing over 8 samples. Maps voltage to a percentage: >=4.10V is
100%, >=3.95V is 75%, >=3.80V is 50%, >=3.60V is 25%, below 3.60V
is 0%. Boards with no on-board battery monitor (the bare Waveshare
EPD HAT and the M5Stack PaperS3) pass `BATT_ADC_PIN = -1`, which
makes `battery_init()` a no-op and causes `battery_read_percent()`
to return -1; the editor UI hides the battery icon in that case.

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

Per-board display backends behind a single C API:

- **display_rlcd.cpp** -- Waveshare ESP32-S3-RLCD-4.2 reflective LCD
  over SPI.
- **display_uc8179.cpp** -- UC8179 e-paper over SPI. Shared by the
  Seeed reTerminal E1001 and the Waveshare E-Paper Driver HAT;
  resolution and pinout (including BUSY) are passed in at init.
- **display_eds3.cpp** -- M5Stack PaperS3 ED047TC1 panel via the
  `m5stack/M5GFX` managed component. Unlike the other backends this
  one does not maintain its own 1-bpp framebuffer; M5GFX already
  keeps a 4-bpp grayscale framebuffer internally, and the LVGL port
  pushes RGB565 pixels straight into it through the optional
  `display_push_rgb565()` fast path. Each `display_flush()` is a
  single full panel refresh in `epd_quality` mode; partial refresh
  and grayscale UI are TODO.
- **lvgl_port.cpp** -- creates the LVGL display object, sets up a
  flush callback that first tries `display_push_rgb565()` (used by
  the PaperS3 backend) and otherwise converts LVGL's RGB565 output
  to the backend's 1-bpp pixel format via `display_set_pixel()`, and
  runs the LVGL tick/task timer. Thread safety is provided by a
  mutex exposed as `lvgl_port_lock()` / `lvgl_port_unlock()`.

The component's `idf_component.yml` declares the `m5stack/m5gfx`
dependency required by the PaperS3 backend. (It is downloaded for
every build; the source itself is gated by `#if defined(CONFIG_DRAFTLING_MODEL_M5STACK_PAPERS3)`
so non-PaperS3 builds do not link it into the final image.)

Public API: `display_init()`, `display_clear()`, `display_set_pixel()`,
`display_flush()`, `display_full_refresh()`, `display_push_rgb565()`,
`display_get_buffer()`, `display_get_buffer_size()`, `lvgl_port_init()`,
`lvgl_port_lock()`, `lvgl_port_unlock()`.

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

A `choice` that selects the target board. The first three options are
ESP32-S3-only (`depends on IDF_TARGET_ESP32S3`); the Waveshare HAT is
selectable on any ESP-IDF target with a BLE radio (`depends on
SOC_BLE_SUPPORTED`):

- **DRAFTLING_MODEL_WAVESHARE_RLCD42** -- Waveshare ESP32-S3-RLCD-4.2
  with a 400x300 reflective LCD and GPIO18 deep-sleep wakeup button.
  *Requires ESP32-S3.*
- **DRAFTLING_MODEL_SEEED_RETERMINAL_E1001** -- Seeed reTerminal E1001
  with a 7.5" 800x480 UC8179 e-paper, SD card on the same SPI bus,
  GPIO3 (KEY0) deep-sleep wakeup. *Requires ESP32-S3.*
- **DRAFTLING_MODEL_WAVESHARE_EPD_HAT** -- Waveshare E-Paper Driver
  HAT (UC8179) on any BLE-capable ESP32 host (ESP32, ESP32-S3,
  ESP32-C2/C3/C6, ESP32-H2 - i.e. anything except the BLE-less
  ESP32-S2). **Also requires PSRAM** (`SOC_SPIRAM_SUPPORTED && SPIRAM`)
  -- the editor gap buffer, framebuffers, and LVGL buffers all live in
  external SPI RAM, so the option is hidden until PSRAM is enabled
  under "Component config -> ESP PSRAM". Resolution and every
  SPI/control pin are user-editable; defaults match the
  ESP32-S3-DevKitC-1 wiring used by Waveshare's example projects.
- **DRAFTLING_MODEL_M5STACK_PAPERS3** -- M5Stack PaperS3 with a
  4.7" 540x960 ED047TC1 e-paper driven by the `m5stack/M5GFX`
  library, on-board MicroSD on SPI3, GPIO21 (power button) wakeup.
  *Requires ESP32-S3.*

The hardware model selection drives two `int` symbols consumed in
`main/app_config.h` as `DISPLAY_WIDTH` / `DISPLAY_HEIGHT`:

- **DRAFTLING_DISPLAY_WIDTH** -- 400 (RLCD), 800 (reTerminal),
  800 (HAT default, user-editable), 540 (PaperS3).
- **DRAFTLING_DISPLAY_HEIGHT** -- 300 (RLCD), 480 (reTerminal),
  480 (HAT default, user-editable), 960 (PaperS3).

When the HAT model is selected the symbols are visible as
`int "Display width (px)"` / `int "Display height (px)"` so the
user can match a different Waveshare panel; for all other boards
they remain hidden with their fixed defaults.

#### Waveshare E-Paper Driver HAT pinout (DRAFTLING_HAT_*)

A sub-menu that is visible only when `DRAFTLING_MODEL_WAVESHARE_EPD_HAT`
is selected. Each option is an `int` GPIO number with a sensible default
for the ESP32-S3-DevKitC-1:

| Symbol | Default | Use |
|--------|---------|-----|
| `DRAFTLING_HAT_EPD_MOSI_PIN` | 11 | SPI MOSI / DIN |
| `DRAFTLING_HAT_EPD_SCK_PIN`  | 12 | SPI clock |
| `DRAFTLING_HAT_EPD_DC_PIN`   | 13 | Data/command |
| `DRAFTLING_HAT_EPD_CS_PIN`   | 10 | Chip select |
| `DRAFTLING_HAT_EPD_RST_PIN`  | 14 | Reset |
| `DRAFTLING_HAT_EPD_BUSY_PIN` | 9  | BUSY input |
| `DRAFTLING_HAT_WAKEUP_GPIO`  | 0  | EXT0 deep-sleep wakeup pin |
| `DRAFTLING_HAT_HAS_SD`       | n  | Opt in to an SD card on SPI3 |
| `DRAFTLING_HAT_SD_MOSI/MISO/SCK/CS_PIN` | 35 / 37 / 36 / 34 | Visible only when SD support is on |

#### E-paper full-refresh interval (DRAFTLING_EPD_FULL_REFRESH_INTERVAL)

`int` shared by the reTerminal E1001 and HAT models (UC8179 backend).
Number of partial refreshes between full refreshes; default 50.

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
