# WriterDeck

A distraction-free Markdown text editor for the
[Waveshare ESP32-S3-RLCD-4.2](https://www.waveshare.com/wiki/ESP32-S3-RLCD-4.2)
development board.

The user connects a Bluetooth keyboard and edits Markdown files stored on the
SD card. The reflective LCD needs no backlight and works well in daylight.
On request the device connects to WiFi and synchronizes files with a remote
Git repository via the GitHub REST API.

## Hardware

| Feature | Detail |
|---------|--------|
| MCU | ESP32-S3 (dual-core, 16 MB flash, 8 MB OPI PSRAM) |
| Display | 4.2" reflective LCD, 400x300, monochrome, SPI |
| Storage | MicroSD card (SDMMC 1-bit) |
| Input | Bluetooth Classic HID keyboard |
| Connectivity | WiFi 802.11 b/g/n for Git sync |

## Features

- **WYSIWYG Markdown editing** on a 400x300 reflective LCD
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
| Arrow keys | Move cursor |
| Home / End | Start / end of line |
| PgUp / PgDn | Scroll by page |
| Ctrl+S | Save file |
| Ctrl+O | Open file browser |
| Ctrl+N | New file |
| Ctrl+G | Git sync (pull + push) |
| Ctrl+W | Connect WiFi |
| Ctrl+Home/End | Start / end of document |
| Ctrl+Left/Right | Word movement |
| Escape | Switch to file browser |

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
main/               Application entry point and pin definitions
components/
  display/           RLCD SPI driver and LVGL v9 port
  sd_card/           SD card (SDMMC) file operations
  bt_keyboard/       Bluetooth Classic HID keyboard host
  editor/            Gap-buffer editor, Markdown parser, LVGL UI
  wifi_manager/      WiFi STA connection manager
  git_sync/          GitHub REST API file synchronization
```

## License

See LICENSE file for details.