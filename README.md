# Draftling

A distraction-free Markdown text editor for ESP32-S3-based development
boards with reflective LCD, e-paper or small color LCD displays.


## Supported hardware

Draftling currently runs on five ESP32-S3 development boards. All of
them share the same firmware image; the target board is picked at
build time with `idf.py menuconfig` -> **DRAFTLING Configuration >
Hardware Model**. Display resolution, driver, pin map, touch
controller and the deep-sleep wake source are derived automatically
from that choice.

| Board | Display | Touch | Battery | Wake source | Storage |
|-------|---------|-------|---------|-------------|---------|
| [Waveshare ESP32-S3-RLCD-4.2](https://www.waveshare.com/wiki/ESP32-S3-RLCD-4.2) | 4.2" reflective LCD, 400x300, SPI | -- | GPIO4 ADC (3:1) | GPIO18 button | On-board MicroSD (SDMMC 1-bit) |
| [M5Stack PaperS3](https://docs.m5stack.com/en/core/papers3) | 4.7" e-paper ED047TC1, 540x960, parallel I80 (via `m5stack/M5GFX`) | GT911 (I2C) | GPIO3 ADC (1:2) | BOOT (GPIO0); optionally touch (`CONFIG_DRAFTLING_STANDBY_WAKE_ON_TOUCH`) | On-board MicroSD (SPI3) |
| [Waveshare ESP32-S3-Touch-LCD-3.49](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.49) | 3.49" IPS color, 640x172, AXS15231B QSPI | AXS5106-family (I2C addr 0x3B) | -- | BOOT (GPIO0) | External SD on SPI |
| Guition JC3248W535 | 3.5" IPS color, 480x320, AXS15231B QSPI | AXS5106L (I2C) | -- | Touch INT (no user buttons) | External SD on SPI |
| [LilyGO T-Display-S3](https://github.com/Xinyuan-LilyGO/T-Display-S3) | 1.9" IPS color, 320x170, ST7789 8-bit i80 | -- | GPIO4 ADC (1:2) | BOOT (GPIO0) | External SD on SPI (no on-board slot) |

All boards use an ESP32-S3 with at least 8 MB of PSRAM and 16 MB of
flash, BLE for the HID keyboard and 802.11 b/g/n WiFi for Git sync.

A few [demo videos](https://youtube.com/playlist?list=PLbRMZQ9npKJRDrk0BhtI4gXMBIHM0c_v_) are available on my YouTube channel.

### Choosing a board

The **Waveshare ESP32-S3-RLCD-4.2** provides the smoothest and most
responsive user interaction. But the screen is very fragile, and the
device needs a proper enclosure, preferably with a protective glass.
The contrast is very low, so it needs good lighting for comfortable
work. (The screen broke during my tests.)

The **M5Stack PaperS3** is so far the most usable option: it is
compact, packed in a good enclosure with magnets on the back, and the
contrast is much higher than that of the RLCD display. The reaction is
significantly slower than with RLCD, but still acceptable. The M5GFX
driver uses the single-pulse `epd_fast` waveform for partial refreshes
(one visible flash, ~80-150 ms per update); a full refresh (3-5 s) is
performed every `DRAFTLING_EPD_FULL_REFRESH_INTERVAL` partials
(default 30) to clear residual ghosting. Draftling feeds the panel
1-bpp black-and-white content, but M5GFX keeps a 4-bpp grayscale
framebuffer internally and dithers automatically, so future grayscale
rendering is a software-only change.

The **Guition JC3248W535** is a color-LCD board with no user buttons,
so touch is the only local input besides the BLE keyboard: deep-sleep
wake is armed on the touch INT line and any tap wakes the device.
Touch also drives the editor and the menu lists; see
[Touch Operations](#touch-operations) below.

The **LilyGO T-Display-S3** has a small 1.9" panel; it works as a
secondary / pocket device. It has no on-board MicroSD slot, so an
external SD card must be wired to a free SPI bus (default pins
documented in `main/app_config.h`).

The **Waveshare ESP32-S3-Touch-LCD-3.49** drives a 640x172 landscape
AXS15231B color LCD (natively 172x640 portrait, software-rotated to
landscape) and an AXS5106-family capacitive touch controller at I2C
address 0x3B. The BOOT button on GPIO0 is the deep-sleep wake source.

UC8179-based e-paper displays (such as those used by the Seeed Studio
reTerminal E1001 and the Waveshare E-Paper Driver HAT) were previously
supported but proved too slow for an interactive Markdown editor: even
with fast partial updates, the panel cannot keep up with typing and
quickly accumulates ghosting artefacts. Support for UC8179 has
therefore been removed from the codebase.

On color LCD boards (Touch-LCD-3.49, JC3248W535, T-Display-S3) the
editor offers a runtime-selectable color theme (F1 -> Settings ->
Color theme): light green on black (default), dark green on black,
amber/orange on black, or white on black. You can also adjust the
**Display rotation angle** in the same `idf.py menuconfig` menu.

The user connects a Bluetooth keyboard and edits Markdown files stored on the
SD card. The reflective LCD needs no backlight and works well in daylight.
On request the device connects to WiFi and synchronizes files with a remote
Git repository via the GitHub REST API.

## Features

- **WYSIWYG Markdown editing** on reflective LCD, e-paper or
  small color LCD displays
- **Bluetooth keyboard** input with auto-discovery and pairing
- **File browser** to open and manage `.md` files on the SD card
  (entries sorted alphabetically, directories first)
- **Markdown rendering**: headings (H1-H4), bullet and numbered lists,
  blockquotes, code fences, horizontal rules, inline bold / italic /
  code / strikethrough
- **Gap buffer** text engine for efficient editing. The document size
  limit is sized at boot from the PSRAM that is free when the editor
  starts (with a minimum of 64 KB and an upper bound queried from the
  Git sync client, so the editor cannot produce a document larger than
  what Git can push) and surfaced read-only
  in F1 -> Settings. Typical limits range from a few hundred KB to a
  few MB depending on the board.
- **WiFi** station mode with credentials from NVS or `/sdcard/wifi.cfg`
- **Git sync** via GitHub REST API (pull and push `.md` files,
  including deletion / rename propagation in both directions; the
  last-synced per-file blob SHAs are kept in a hidden `.git_state`
  file on the SD card so the next sync can do a proper 3-way merge)
- **Per-file metadata sidecars**: when a `.md` file is closed (or
  before the device enters deep sleep), the editor records the
  current cursor position and scroll line in a hidden sidecar file
  named `.<basename>.meta` next to the document (for example
  `notes.md` -> `.notes.md.meta`). The next time the file is
  opened, the cursor is restored to its previous position and the
  view scrolls so the cursor is visible. The `.meta` files are
  hidden from the file browser (they start with a dot) and are
  ignored by Git sync (which only handles `*.md`).

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| F1 | Open main menu (BLE, WiFi, Git, Layout, Settings...) |
| Arrow keys | Move cursor |
| Home / End | Start / end of line |
| PgUp / PgDn | Scroll by page |
| Ctrl+S | Save file |
| Ctrl+O | Open file browser |
| Ctrl+N | New file |
| Ctrl+L | Cycle keyboard layout |
| Ctrl+G | Git sync (pull + push) |
| Ctrl+W | Toggle WiFi (connect / disconnect) |
| Ctrl+F | Find |
| Ctrl+H | Find + Replace (Tab switches field, Enter = next match, Ctrl+Enter = replace + next) |
| Ctrl+C / Ctrl+X / Ctrl+V | Copy / Cut / Paste the current selection |
| Ctrl+A | Select all |
| Ctrl+R | Force full e-paper refresh (clears ghosting; e-paper boards only) |
| Ctrl+Home/End | Start / end of document |
| Ctrl+Left/Right | Word movement |
| Escape | Switch to file browser |

## Touch Operations

On boards with a touchscreen (currently the Guition JC3248W535 with
its on-board AXS5106L controller, and the M5Stack PaperS3 with its
on-board GT911 controller), touch input works alongside the Bluetooth
keyboard -- you can use either, or both. All gestures are summarized
below.

### In the editor

| Gesture | Action |
|---------|--------|
| Single tap | Move the cursor to the tapped position |
| Double tap | Select the word at the tapped position |
| Drag up / down | Scroll the document line by line, following the finger (one line per line-height of travel) |
| Swipe up / down (fast flick) | Scroll by roughly one screen |

A drag that moves more than a few pixels never moves the caret -- the
tap-to-cursor action only fires for short, stationary taps.

On the e-paper PaperS3 the gestures are the same, but the slower
refresh rate (~80-150 ms per partial update, several seconds for a
full refresh) means the visible response to a drag is less smooth
than on the color-LCD JC3248W535.

### In menus and the file browser

| Gesture | Action |
|---------|--------|
| Tap a row | Highlight that row (same as moving with arrow keys) |
| Tap the highlighted row again | Activate it (same as pressing Enter) |

This two-step "highlight then activate" flow mirrors the keyboard
"arrows + Enter" interaction and avoids accidental activations on
imprecise taps.

### Wake from sleep

On boards without user buttons (such as the JC3248W535) the touch
controller's INT line is wired to the deep-sleep wake source, so any
tap on the screen wakes the device.

The M5Stack PaperS3 has a BOOT button on GPIO0 which is the default
deep-sleep wake source; the GT911 touchscreen is used as a regular
input device but not as a wake source by default. Set
`CONFIG_DRAFTLING_STANDBY_WAKE_ON_TOUCH=y` in menuconfig to wake on
touch instead.

## Keyboard Layouts

The editor supports four keyboard layouts that can be switched with
**Ctrl+L** or through the **F1 menu**:

| Code | Layout |
|------|--------|
| US | US-English (QWERTY) |
| UA | Ukrainian (Cyrillic) |
| DE | German (QWERTZ with umlauts) |
| FR | French (AZERTY with accents) |

The current layout is shown in the title bar.

### Configuring enabled layouts

The set of compiled-in keyboard layouts is configurable via
`idf.py menuconfig` under **DRAFTLING Keyboard Layouts**. Each layout
can be independently enabled or disabled. By default **US-English** and
**Ukrainian** are enabled. Disabling unused layouts saves flash space.

## Building

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/)
v5.3 or later. ESP-IDF 6.0 and newer are **not** supported yet because
the `m5stack/M5GFX` managed component (used by the M5Stack PaperS3
display backend) is not compatible with ESP-IDF 6.x. Use any ESP-IDF
v5.x release (v5.3 - v5.5 confirmed working).

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

If you update `sdkconfig.defaults` or pull new changes, delete the generated
`sdkconfig` so the defaults are re-applied:

```bash
rm -f sdkconfig
idf.py set-target esp32s3
idf.py build
```

## Menuconfig Options

Run `idf.py menuconfig` to open the interactive configuration UI.
Draftling adds three custom menus described below (**DRAFTLING
Configuration**, **DRAFTLING Keyboard Layouts** and **DRAFTLING
Editor**). All other options (Bluetooth, LVGL fonts, etc.) use the
ESP-IDF defaults from `sdkconfig.defaults` and normally do not need
to be changed.

### DRAFTLING Configuration

Found at the top-level **DRAFTLING Configuration** menu.

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| **Hardware Model** | choice | M5Stack PaperS3 | Select the target board. Display resolution, driver, pin map, touch controller and the deep-sleep wake source are derived automatically. See the [Supported hardware](#supported-hardware) table for all five options. |
| **Display rotation angle** | choice | 0 degrees | Rotate the display by 0, 90, 180, or 270 degrees. |
| **E-paper full-refresh interval** | int | 30 | E-paper boards only: number of partial refreshes between full refreshes. |
| **Enable touchscreen input** | bool | y on PaperS3 and JC3248W535, n otherwise | Enable the I2C touch driver and LVGL pointer input device. |
| **Standby: wake from deep sleep on touchscreen tap** | bool | y on JC3248W535, n otherwise | Arm EXT0 on the touch INT line so any tap wakes the device. |

> Backlight brightness on color-LCD boards is not a menuconfig option:
> it is set at runtime from F1 -> Settings -> Backlight and persisted
> in NVS (default 50%). The backend drives the BL GPIO with an LEDC
> PWM signal (~5 kHz, 10-bit). 0% = off, 100% = full brightness.

> **Note about the M5Stack PaperS3:** the M5GFX-based driver uses the
> single-pulse `epd_fast` waveform for partial refreshes (one visible
> flash, ~80-150 ms per update). A full refresh (3-5 s) is performed
> automatically every `DRAFTLING_EPD_FULL_REFRESH_INTERVAL` partials
> (default 30) to clear residual ghosting; tune the interval in
> `idf.py menuconfig`.

### DRAFTLING Keyboard Layouts

Found at the top-level **DRAFTLING Keyboard Layouts** menu. Each layout
can be independently enabled or disabled. Disabling unused layouts saves
flash space.

| Option | Default | Description |
|--------|---------|-------------|
| US-English (QWERTY) | **y** | Standard US keyboard layout |
| Ukrainian (Cyrillic) | **y** | Ukrainian Cyrillic layout |
| German (QWERTZ) | n | German layout with umlauts |
| French (AZERTY) | n | French layout with accents |

## Configuration Files

Place these on the SD card root:

### WiFi (`wifi.cfg`)

```
MySSID
MyPassword
```

### Git Sync (`git.cfg`)

```
repo_url=https://github.com/user/repo
branch=main
token=ghp_xxxxxxxxxxxx
path=docs/
```

The `token` is a GitHub Personal Access Token with `repo` scope.
**Keep this file private.**

## Project Structure

```
main/               Application entry point, pin definitions, Kconfig
components/
  battery/           Battery voltage monitor (ADC + smoothing)
  ble_keyboard/      BLE HID keyboard host (Bluedroid)
  display/           Display backends (RLCD SPI, EDS3 e-paper via
                     M5GFX, AXS15231B QSPI, ST7789 8-bit i80) and
                     LVGL v9 port
  editor/            Gap-buffer editor, Markdown parser, LVGL UI, menu
  fonts/             Custom LVGL fonts (Latin, Latin-1 Supplement, Cyrillic)
  git_sync/          GitHub REST API file synchronization
  kb_layout/         Keyboard layout translation (US/UA/DE/FR)
  sd_card/           SD card (SDMMC or SPI) file operations
  standby/           Deep-sleep / standby timer manager
  touchscreen/       I2C touch driver (AXS5106L, GT911) + LVGL pointer indev
  wifi_manager/      WiFi STA connection manager
```

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for
details.
