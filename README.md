# Draftling

A distraction-free Markdown text editor for ESP32-S3-based development boards
with reflective or e-paper displays.

### Supported hardware

| Board | Display |
|-------|---------|
| [Waveshare ESP32-S3-RLCD-4.2](https://www.waveshare.com/wiki/ESP32-S3-RLCD-4.2) | 4.2" reflective LCD, 400x300 |
| [M5Stack PaperS3](https://docs.m5stack.com/en/core/papers3) | 4.7" e-Paper (IT8951), 960x540 |

### Hardware selection

Before building, select the target board with `idf.py menuconfig`.
Navigate to **DRAFTLING Configuration > Hardware Model** and choose
the board you are building for:

- **Waveshare ESP32-S3-RLCD-4.2** -- 4.2" reflective LCD (400x300)
- **M5Stack PaperS3** -- 4.7" e-Paper with IT8951 controller (960x540)

The display resolution and driver are configured automatically based
on the selected model. You can also adjust the **Display rotation
angle** in the same menu.

The user connects a Bluetooth keyboard and edits Markdown files stored on the
SD card. The reflective LCD needs no backlight and works well in daylight.
On request the device connects to WiFi and synchronizes files with a remote
Git repository via the GitHub REST API.

## Hardware

| Feature | Waveshare RLCD-4.2 | M5Stack PaperS3 |
|---------|--------------------|--------------------|
| MCU | ESP32-S3 (16 MB flash, 8 MB OPI PSRAM) | ESP32-S3 (16 MB flash, 8 MB PSRAM) |
| Display | 4.2" reflective LCD, 400x300, SPI | 4.7" e-Paper, 960x540, IT8951 SPI |
| Storage | MicroSD (SDMMC 1-bit) | MicroSD (SDMMC 1-bit) |
| Input | BLE HID keyboard | BLE HID keyboard |
| Connectivity | WiFi 802.11 b/g/n | WiFi 802.11 b/g/n |
| Wake from sleep | GPIO18 button | Power button (PMIC reset) |

## Features

- **WYSIWYG Markdown editing** on reflective/e-paper display
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
| Ctrl+W | Connect WiFi |
| Ctrl+Home/End | Start / end of document |
| Ctrl+Left/Right | Word movement |
| Escape | Switch to file browser |

## Keyboard Layouts

The editor supports seven keyboard layouts that can be switched with
**Ctrl+L** or through the **F1 menu**:

| Code | Layout |
|------|--------|
| US | US-English (QWERTY) |
| UA | Ukrainian (Cyrillic) |
| DE | German (QWERTZ with umlauts) |
| FR | French (AZERTY with accents) |
| KO | Korean (Dubeolsik) |
| JA | Japanese (Kana) |
| ZH | Chinese (Zhuyin/Bopomofo) |

The current layout is shown in the title bar.

### Configuring enabled layouts

The set of compiled-in keyboard layouts is configurable via
`idf.py menuconfig` under **DRAFTLING Keyboard Layouts**. Each layout
can be independently enabled or disabled. By default **US-English** and
**Ukrainian** are enabled. Disabling unused layouts saves flash space.
CJK layouts (Korean, Japanese, Chinese) require additional font data
and are disabled by default to conserve flash.

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
  display/           Display drivers (RLCD SPI + IT8951 e-Paper) and LVGL v9 port
  sd_card/           SD card (SDMMC) file operations
  ble_keyboard/      BLE HID keyboard host (Bluedroid)
  kb_layout/         Keyboard layout translation (US/UA/DE/FR/KO/JA/ZH)
  fonts/             Custom LVGL fonts (Cyrillic and CJK glyph coverage)
  editor/            Gap-buffer editor, Markdown parser, LVGL UI, menu
  wifi_manager/      WiFi STA connection manager
  git_sync/          GitHub REST API file synchronization
  standby/           Deep-sleep / standby timer manager
```

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for
details.