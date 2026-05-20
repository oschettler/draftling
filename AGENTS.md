# Copilot Instructions

## Code Style

- Do not use non-ASCII characters in code, comments, or string literals. All source files must be ASCII-only.
- All board-specific configuration -- pin numbers, panel dimensions,
  wakeup GPIOs, brand strings, etc. -- belongs in `main/Kconfig.projbuild`
  and `main/app_config.h`. C and C++ files outside `main/` must NOT
  reference specific board models (no `#if defined(CONFIG_DRAFTLING_MODEL_*)`,
  no hard-coded board names). Use the derived feature flags
  (`CONFIG_DRAFTLING_DISPLAY_EPD`, `CONFIG_DRAFTLING_DISPLAY_RLCD`,
  `CONFIG_DRAFTLING_DISPLAY_HAS_BACKLIGHT`, `CONFIG_DRAFTLING_HAS_BATTERY`,
  `CONFIG_DRAFTLING_SD_SDMMC`, `CONFIG_DRAFTLING_WAKEUP_GPIO`, etc.)
  exposed by `main/Kconfig.projbuild` instead. To add a new model,
  introduce a new `DRAFTLING_MODEL_*` choice option, set the matching
  derived flags / `default` lines for the existing feature symbols,
  add a per-board `#elif` block in `main/app_config.h` defining the
  board's `BOARD_NAME`, pin numbers and `WAKEUP_GPIO_NUM`, and update
  `main/main.cpp`'s display / SD init switch as needed -- no other
  C / C++ file should require changes.

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
| M5Stack PaperS3 | 4.7-inch e-paper (ED047TC1), 540x960 |

UC8179-based panels (Seeed Studio reTerminal E1001 and the Waveshare
E-Paper Driver HAT) were previously supported but have been removed:
the controller proved too slow for an interactive Markdown editor
even with fast partial updates and accumulated ghosting too quickly
to be usable.

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
block defining SPI pins (MOSI, SCK, DC, CS, RST, TE), SD card pins
(CLK, CMD, D0 for SDMMC, or MOSI/MISO/SCK/CS for SPI), I2C pins, the
battery ADC pin, and the deep-sleep wakeup GPIO. The M5Stack PaperS3
block omits the panel data-bus and control-line pins because the
`m5stack/M5GFX` library configures them internally based on the board
id.

### components/battery/

Reads battery voltage through a resistive divider on a configurable
ADC pin using the ESP32 ADC. Applies exponential moving average
smoothing over 8 samples. Maps voltage to a percentage: >=4.10V is
100%, >=3.95V is 75%, >=3.80V is 50%, >=3.60V is 25%, below 3.60V
is 0%. The M5Stack PaperS3 reads its cell through ADC1 on GPIO3 with
a 1:2 divider, matching the M5Unified Power_Class configuration for
that board. When `BATT_ADC_PIN < 0`, `battery_init()` is a no-op and
`battery_read_percent()` returns -1; the editor UI hides the battery
icon in that case.

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
- **display_eds3.cpp** -- M5Stack PaperS3 ED047TC1 panel via the
  `m5stack/M5GFX` managed component. Unlike the other backends this
  one does not maintain its own 1-bpp framebuffer; M5GFX already
  keeps a 4-bpp grayscale framebuffer internally, and the LVGL port
  pushes RGB565 pixels straight into it through the optional
  `display_push_rgb565()` fast path. The backend accumulates a
  dirty bounding box across pushes and, on `display_flush()`, calls
  `M5GFX::display(x,y,w,h)` over that rect using the single-pulse
  `epd_fast` waveform (one visible flash, ~80-150 ms);
  every `CONFIG_DRAFTLING_EPD_FULL_REFRESH_INTERVAL` partials (or
  whenever the dirty area covers most of the screen, or after
  `display_clear()` / `display_full_refresh()`) the next refresh is
  promoted to a full-screen `epd_quality` pass to clear ghosting.
  Two extras keep typing snappy: a 120 ms flush debounce coalesces
  bursts of keystrokes into a single panel refresh (deferred via
  `esp_timer`, taking `lvgl_port_lock()` for thread safety), and the
  optional one-shot `display_set_partial_clip(x,y,w,h)` API lets the
  editor narrow the next refresh to the area around the typed
  character (cursor + edited columns) when it knows the rest of the
  LVGL-pushed line pixels are unchanged. The backend also honours
  `CONFIG_DRAFTLING_DISPLAY_SCALE` (default 2 on PaperS3): every
  logical LVGL pixel is rendered as SCALE x SCALE physical panel
  pixels via nearest-neighbor expansion in `display_push_rgb565()`,
  and the dirty-bbox / partial-clip math is converted to panel
  coordinates internally. Grayscale UI is still TODO.
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
`display_set_partial_clip()`, `display_set_backlight()`,
`display_get_buffer()`,
`display_get_buffer_size()`, `lvgl_port_init()`,
`lvgl_port_lock()`, `lvgl_port_unlock()`.

### components/editor/

The largest component. Contains:

- **editor.cpp** -- gap-buffer text engine with a document size limit
  sized dynamically at `editor_init()` from the PSRAM that is free
  when the editor starts (the gap buffer and flat cache are each
  allocated at that size, so the editor's SPIRAM cost is ~2x the
  limit; the value is clamped to a minimum of 64 KB and an upper
  bound returned by `git_sync_max_file_size()` -- so the editor never
  produces a document larger than what git_sync can push -- and a
  ~512 KB headroom is reserved for BLE / WiFi / Git sync / LVGL
  widget growth). Exposes `editor_get_max_doc_size()` for the UI,
  which surfaces the value read-only in F1 -> Settings. Provides
  cursor movement, selection, clipboard, insert/delete, and file I/O.
- **editor_ui.cpp** -- LVGL-based user interface: title bar with battery
  and layout indicators, scrollable text area with Markdown rendering,
  status bar, file browser dialog, and settings menu (F1).
- **md_parser.cpp** -- single-pass Markdown line parser. Recognizes
  headings H1-H4, bullet and numbered lists, blockquotes, code fences,
  horizontal rules, and inline bold/italic/code/strikethrough spans.
- **draftling_logo.c** -- embedded LVGL image for the splash screen.

The F1 menu opens an in-line **Settings** list with: standby
timeout, base font size, **Backlight** (NN%, only on boards with
`CONFIG_DRAFTLING_DISPLAY_HAS_BACKLIGHT` -- the value is persisted
in NVS under the `editor` namespace and applied at boot via
`display_set_backlight()`; default 50%), color theme (only on
`CONFIG_DRAFTLING_DISPLAY_COLOR`), sleep-now, factory reset and
back. Picking a new color theme does NOT reboot the device:
`rebuild_screens_for_theme()` deletes every screen / overlay /
screen-bound timer, re-runs `init_styles()` under the new palette,
calls `build_screens()` again and restores the screen the user was
on. The persistent state (open document, NVS-backed font size /
theme / backlight / standby timeout) survives the rebuild.

Public API: `editor_init()`, `editor_open_file()`, `editor_save_file()`,
`editor_ui_init()`, `editor_ui_handle_key()`, `editor_find()`,
`editor_replace_range()`, `md_parse_line()`, and many
cursor/selection/clipboard helpers.

Editor shortcuts include `Ctrl+F` (Find) and `Ctrl+H` (Find +
Replace). Both open a modal overlay; in Find+Replace mode, `Tab`
switches between the Find and Replace fields, `Enter` jumps to the
next match (wrapping at end-of-document), and `Ctrl+Enter` replaces
the current match and advances to the next.

The title bar shows `L %d/%d` (current line / total lines) on every
build; on non-EPD targets the column counter is appended as well.

The bottom status bar of both the editor and the file browser
displays a small Wi-Fi icon (`components/editor/wifi_icon.c`) in the
right corner whenever `wifi_manager_is_connected()` is true. The
Greybeard fonts cover U+0020-U+04FF plus a few currency / numero
glyphs, which excludes the U+1F6DC "wireless" pictograph, so the
icon is rendered from a small embedded LVGL `LV_COLOR_FORMAT_I1`
image instead of as a font glyph. Two pre-baked descriptors are
exposed (black-on-transparent for the default theme and
white-on-transparent for `CONFIG_DRAFTLING_EPD_BLACK_BACKGROUND`).

### components/fonts/

Custom LVGL bitmap fonts generated from the Greybeard typeface. See the
dedicated section below for the full creation process.

### components/git_sync/

Synchronizes Markdown files between the SD card and a remote GitHub
repository using the GitHub REST API over HTTPS. Reads configuration
(repo URL, branch, token, path prefix) from `/sdcard/git.cfg`. Supports
pull, push, or bidirectional sync with state callbacks and error tracking.

The last-synced per-file blob SHAs are persisted in a hidden
`.git_state` file under the SD mount point. Every sync does a 3-way
comparison (saved vs local vs remote) so that:

- a file modified only on one side is propagated to the other;
- a file deleted on the remote is also deleted locally (provided the
  local copy still matches the last-synced SHA); otherwise the local
  edits "win" and the file is recreated on the remote next push;
- a file deleted locally is also deleted on the remote (via the GitHub
  Contents DELETE API, conditional on the saved SHA still being current);
  otherwise the remote-modified copy "wins" and is re-downloaded next
  pull;
- a file renamed on either side is handled correctly because Git models
  renames as a delete + add of two distinct paths, which the rules
  above cover automatically (no duplicate copies left behind).

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
auto-save before power-down. The wake source is an EXT0 trigger on
the per-board RTC-capable GPIO selected by `CONFIG_DRAFTLING_WAKEUP_GPIO`
in `main/Kconfig.projbuild` (defaults: GPIO18 on Waveshare RLCD-4.2,
GPIO0 / BOOT on every other board). The standby code itself is
board-agnostic and never tests `DRAFTLING_MODEL_*` directly. On the
M5Stack PaperS3, GPIO0 is used because earlier revisions tried
GPIO21 (the on-board buzzer pin -- floated low under some
speaker-driver states and woke the device instantly) and GPIO48
(the GT911 touch INT) with a light-sleep + `esp_restart()`
workaround; both woke the device immediately, the latter because
M5GFX initializes only the e-paper panel (not the touch
controller), so the GT911 is left uninitialized and holds INT low.

Before arming EXT0, `standby_enter_sleep()` enables the chip's
internal RTC pull-up on the wake GPIO and disables any pull-down.
The supported boards (RLCD-4.2 button on GPIO18 and PaperS3 BOOT on
GPIO0 / strapping pin) already have external pull-ups, so this is
harmless: the two pull-ups simply parallel. The internal pull-up is
always enabled so the behaviour is consistent across boards.

In addition to the inactivity timer, `standby_init()` also arms a
"no keyboard connected" countdown of `CONFIG_DRAFTLING_NO_KEYBOARD_SLEEP_SEC`
seconds (default 180, 0 = disabled). When the timer fires it polls
`ble_keyboard_is_connected()` and only enters deep sleep if no
Bluetooth keyboard has paired by then. This conserves battery when
the device is powered on accidentally or no paired keyboard is in
range.

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

A `choice` that selects the target board. Both options are
ESP32-S3-only (`depends on IDF_TARGET_ESP32S3`):

- **DRAFTLING_MODEL_WAVESHARE_RLCD42** -- Waveshare ESP32-S3-RLCD-4.2
  with a 400x300 reflective LCD and GPIO18 deep-sleep wakeup button.
  *Requires ESP32-S3.*
- **DRAFTLING_MODEL_M5STACK_PAPERS3** -- M5Stack PaperS3 with a
  4.7" 540x960 ED047TC1 e-paper driven by the `m5stack/M5GFX`
  library, on-board MicroSD on SPI3, BOOT button on GPIO0 used as
  the EXT0 deep-sleep wake source (the only RTC-capable user-input
  GPIO on the board). *Requires ESP32-S3.*

The hardware-model selection drives two non-prompted `int` symbols
consumed in `main/app_config.h` as `DISPLAY_WIDTH` / `DISPLAY_HEIGHT`:

- **DRAFTLING_DISPLAY_WIDTH** -- 400 (RLCD), 960 (PaperS3).
- **DRAFTLING_DISPLAY_HEIGHT** -- 300 (RLCD), 540 (PaperS3).

Both symbols are non-prompted (no menuconfig entry); to support a
new board with a different resolution, add a model `config` block
inside the `DRAFTLING_HARDWARE_MODEL` choice and extend the
per-model `default` lines on these symbols.

#### E-paper full-refresh interval (DRAFTLING_EPD_FULL_REFRESH_INTERVAL)

`int` used by e-paper backends only (gated on
`DRAFTLING_DISPLAY_EPD`). Number of partial refreshes between
full refreshes; default 30.

#### Derived feature flags (no menuconfig prompt)

The hardware-model choice also drives a set of hidden `bool` /
`int` symbols that carry the user's board pick to every component
without anyone needing to test individual `DRAFTLING_MODEL_*` ids
in C / C++ code:

| Symbol | Purpose | Set by |
|--------|---------|--------|
| DRAFTLING_DISPLAY_RLCD            | Selects `display_rlcd.cpp`        | RLCD-4.2 |
| DRAFTLING_DISPLAY_EPD             | Selects `display_eds3.cpp`; gates EPD-only options (BLACK_BACKGROUND, full-refresh interval) and the editor's no-blink cursor / 120 ms flush debounce | PaperS3 |
| DRAFTLING_DISPLAY_AXS15231B       | Selects `display_axs15231b.cpp`   | Touch-LCD-3.49, JC3248W535 |
| DRAFTLING_DISPLAY_ST7789          | Selects `display_st7789.cpp`      | T-Display-S3 |
| DRAFTLING_DISPLAY_COLOR           | Enables the color-theme picker; PARTIAL render mode in `lvgl_port.cpp` | AXS15231B + ST7789 boards |
| DRAFTLING_DISPLAY_HAS_BACKLIGHT   | Adds the "Backlight: NN%" entry to F1 -> Settings; calls `display_set_backlight()` at boot from NVS | AXS15231B + ST7789 boards |
| DRAFTLING_HAS_BATTERY             | Creates the battery-percentage status-bar label and its poll timer | RLCD-4.2, PaperS3, T-Display-S3 |
| DRAFTLING_SD_SDMMC                | Routes SD init through the on-chip SDMMC peripheral (1-bit) instead of generic SPI | RLCD-4.2 |
| DRAFTLING_WAKEUP_GPIO             | RTC-capable EXT0 wake-up GPIO; consumed by `components/standby/standby.cpp` | per-model defaults |

Components MUST key off these derived symbols; they MUST NOT
test `DRAFTLING_MODEL_*` directly. Adding a new model means
adding new `default` lines to each of the symbols above (plus
the width / height / rotate-default symbols), an `#elif` block
in `main/app_config.h`, and the matching display / SD init
branch in `main/main.cpp`.

#### Display Rotation (DRAFTLING_DISPLAY_ROTATE)

A `choice` that sets the display rotation angle. Options are 0, 90, 180,
and 270 degrees. The default is 0 (no rotation). The selected angle is
exposed as the hidden `int` symbol **DRAFTLING_DISPLAY_ROTATE_ANGLE**,
consumed in `app_config.h` as `DISPLAY_ROTATE`.

#### Display scale factor (DRAFTLING_DISPLAY_SCALE)

Non-prompted derived `int` (range 1-8). Logical pixel size: every
LVGL pixel is rendered as SCALE x SCALE physical panel pixels via
nearest-neighbor expansion in the display backend. The editor and
LVGL canvas operate in *logical* coordinates (panel size divided by
SCALE); only the display backend deals in physical panel pixels.
Defaults: 2 for the M5Stack PaperS3 (so the high-density 540x960
panel renders Greybeard text at a comfortably readable size), 1 for
every other board. Because the symbol is non-prompted, the per-model
default re-applies automatically whenever `DRAFTLING_HARDWARE_MODEL`
changes -- no need to delete `sdkconfig`. To override, set the value
in `sdkconfig.defaults`.

Currently the `display_eds3` (PaperS3 e-paper), `display_axs15231b`
(Touch-LCD-3.49 / JC3248W535) and `display_st7789` (T-Display-S3)
backends implement the up-scaling; on the RLCD backend a value > 1
has no visible effect because the LVGL framebuffer already matches
the panel size.

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

Requires ESP-IDF v5.3 or later. ESP-IDF 6.0 and newer are not supported
yet because the `m5stack/M5GFX` managed component is not compatible
with ESP-IDF 6.x; the top-level `CMakeLists.txt` enforces this with a
`FATAL_ERROR` on IDF major version >= 6.

PSRAM is required on every supported board. The editor gap buffer
(sized dynamically at startup from the PSRAM that is free when
`editor_init()` runs -- typically a few hundred KB up to a few MB
depending on the board), the display
framebuffers, the LVGL widget heap (`CONFIG_LV_USE_CUSTOM_MALLOC` routes
through PSRAM), the Git-sync HTTPS response buffers + task stack, and
the Bluedroid host environment (`CONFIG_BT_BLE_DYNAMIC_ENV_MEMORY` plus
`CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST`) all assume `MALLOC_CAP_SPIRAM`
is available. The top-level `CMakeLists.txt` aborts the configure step
with a `FATAL_ERROR` if `CONFIG_SPIRAM` is not set. Targets without
on-chip PSRAM support (e.g. ESP32-S2, bare ESP32-C3 modules without
PSRAM) are not supported.

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
