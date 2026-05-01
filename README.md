# Draftling

A distraction-free Markdown text editor for ESP32-S3-based development boards
with reflective displays.


### Supported hardware

| Board | Display |
|-------|---------|
| [Waveshare ESP32-S3-RLCD-4.2](https://www.waveshare.com/wiki/ESP32-S3-RLCD-4.2) | 4.2" reflective LCD, 400x300 |
| [M5Stack PaperS3](https://docs.m5stack.com/en/core/papers3) | 4.7" e-paper (ED047TC1), 540x960 |

A few [demo videos](https://youtube.com/playlist?list=PLbRMZQ9npKJRDrk0BhtI4gXMBIHM0c_v_) are available on my YouTube channel.

The Waveshare ESP32-S3-RLCD-4.2 provides the smoothest and most
responsive user interaction. But the screen is very fragile, and the
device needs a proper enclosure, preferably with a protective
glass. Also, the contrast is very low, so iit needs a good lighting
for comfortable work. The screen broke during the tests.

The M5Stack PaperS3 is so far the most usable option: it is compact,
packed in a good enclosure with magnets on the back, and the contrast
is much higher than that of the RLCD display. The reaction is
significantly slower than with RLCD, but still acceptable.

UC8179-based e-paper displays (such as those used by the Seeed
Studio reTerminal E1001 and the Waveshare E-Paper Driver HAT, which
were tested with both the Waveshare 7.5" V2 panel and a no-name
5.83" 648x480 panel with a built-in UC8179 driver) have been tested
and proved too slow for an interactive Markdown editor: even with
fast partial updates the panel cannot keep up with typing and
quickly accumulates ghosting artefacts. Support for UC8179 has
therefore been removed from the codebase.


### Hardware selection

Before building, select the target board with `idf.py menuconfig`.
Navigate to **DRAFTLING Configuration > Hardware Model** and choose
the board you are building for:

- **Waveshare ESP32-S3-RLCD-4.2** -- 4.2" reflective LCD (400x300)
- **M5Stack PaperS3** -- 4.7" e-paper (ED047TC1, 540x960). Driver is
  a 1-bpp B/W shim over the official `m5stack/M5GFX` managed
  component; partial refresh and grayscale are not implemented yet.

The display resolution and driver are configured automatically based
on the selected model. You can also adjust the **Display rotation
angle** in the same menu.

The user connects a Bluetooth keyboard and edits Markdown files stored on the
SD card. The reflective LCD needs no backlight and works well in daylight.
On request the device connects to WiFi and synchronizes files with a remote
Git repository via the GitHub REST API.

## Hardware

| Feature | Waveshare RLCD-4.2 | M5Stack PaperS3 |
|---------|--------------------|-----------------|
| MCU | ESP32-S3 (16 MB flash, 8 MB OPI PSRAM) | ESP32-S3 (16 MB flash, 16 MB PSRAM) |
| Display | 4.2" reflective LCD, 400x300, SPI | 4.7" e-paper ED047TC1, 540x960, parallel I80 |
| Storage | MicroSD (SDMMC 1-bit) | Onboard MicroSD (SPI3) |
| Input | BLE HID keyboard | BLE HID keyboard |
| Connectivity | WiFi 802.11 b/g/n | WiFi 802.11 b/g/n |
| Battery monitor | GPIO4 ADC (3:1 divider) | GPIO3 ADC (2:1 divider) |
| Wake from sleep | GPIO18 button | Power button (GPIO21) |

## Features

- **WYSIWYG Markdown editing** on reflective display
- **Bluetooth keyboard** input with auto-discovery and pairing
- **File browser** to open and manage `.md` files on the SD card
  (entries sorted alphabetically, directories first)
- **Markdown rendering**: headings (H1-H4), bullet and numbered lists,
  blockquotes, code fences, horizontal rules, inline bold/italic/code
- **Gap buffer** text engine for efficient editing (256 KB document limit)
- **WiFi** station mode with credentials from NVS or `/sdcard/wifi.cfg`
- **Git sync** via GitHub REST API (pull and push `.md` files)
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
| F1 | Open settings menu (BLE, WiFi, Git, Layout) |
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
| Ctrl+R | Force full e-paper refresh (clears ghosting; e-paper boards only) |
| Ctrl+Home/End | Start / end of document |
| Ctrl+Left/Right | Word movement |
| Escape | Switch to file browser |

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
Draftling adds two custom menus described below. All other options
(Bluetooth, LVGL fonts, etc.) use the ESP-IDF defaults from
`sdkconfig.defaults` and normally do not need to be changed.

### DRAFTLING Configuration

Found at the top-level **DRAFTLING Configuration** menu.

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| **Hardware Model** | choice | Waveshare ESP32-S3-RLCD-4.2 | Select the target board. Display resolution and driver are set automatically. |
| -- Waveshare ESP32-S3-RLCD-4.2 | | | 4.2" reflective LCD, 400x300 |
| -- M5Stack PaperS3 | | | 4.7" e-paper, ED047TC1, 540x960 (driver via m5stack/M5GFX) |
| **Display rotation angle** | choice | 0 degrees | Rotate the display by 0, 90, 180, or 270 degrees. |
| **E-paper full-refresh interval** | int | 30 | M5Stack PaperS3 only: number of partial refreshes between full refreshes. |

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
  display/           Display driver (RLCD SPI) and LVGL v9 port
  sd_card/           SD card (SDMMC) file operations
  ble_keyboard/      BLE HID keyboard host (Bluedroid)
  kb_layout/         Keyboard layout translation (US/UA/DE/FR)
  fonts/             Custom LVGL fonts (Latin, Latin-1 Supplement, Cyrillic)
  editor/            Gap-buffer editor, Markdown parser, LVGL UI, menu
  wifi_manager/      WiFi STA connection manager
  git_sync/          GitHub REST API file synchronization
  standby/           Deep-sleep / standby timer manager
```

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for
details.
