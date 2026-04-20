# Draftling

A distraction-free Markdown text editor for ESP32-S3-based development boards
with reflective displays.


### Supported hardware

| Board | Display |
|-------|---------|
| [Waveshare ESP32-S3-RLCD-4.2](https://www.waveshare.com/wiki/ESP32-S3-RLCD-4.2) | 4.2" reflective LCD, 400x300 |

A demo video with the [Waveshare ESP32-S3-RLCD-4.2](https://www.youtube.com/watch?v=PgSaroeM3CE). Also, the fragile screen broke during the tests, so this device really needs a protective glass.

### Hardware selection

Before building, select the target board with `idf.py menuconfig`.
Navigate to **DRAFTLING Configuration > Hardware Model** and choose
the board you are building for:

- **Waveshare ESP32-S3-RLCD-4.2** -- 4.2" reflective LCD (400x300)

The display resolution and driver are configured automatically based
on the selected model. You can also adjust the **Display rotation
angle** in the same menu.

The user connects a Bluetooth keyboard and edits Markdown files stored on the
SD card. The reflective LCD needs no backlight and works well in daylight.
On request the device connects to WiFi and synchronizes files with a remote
Git repository via the GitHub REST API.

## Hardware

| Feature | Waveshare RLCD-4.2 |
|---------|--------------------|
| MCU | ESP32-S3 (16 MB flash, 8 MB OPI PSRAM) |
| Display | 4.2" reflective LCD, 400x300, SPI |
| Storage | MicroSD (SDMMC 1-bit) |
| Input | BLE HID keyboard |
| Connectivity | WiFi 802.11 b/g/n |
| Wake from sleep | GPIO18 button |

## Features

- **WYSIWYG Markdown editing** on reflective display
- **Bluetooth keyboard** input with auto-discovery and pairing
- **File browser** to open and manage `.md` files on the SD card
- **Markdown rendering**: headings (H1-H4), bullet and numbered lists,
  blockquotes, code fences, horizontal rules, inline bold/italic/code
- **Gap buffer** text engine for efficient editing (256 KB document limit)
- **WiFi** station mode with credentials from NVS or `/sdcard/wifi.cfg`
- **Git sync** via GitHub REST API (pull and push `.md` files)

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
v5.3 or later.

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
| **Display rotation angle** | choice | 0 degrees | Rotate the display by 0, 90, 180, or 270 degrees. |

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
