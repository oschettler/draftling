# Draftling

A distraction-free Markdown text editor for ESP32-S3-based development boards
with reflective displays.


### Supported hardware

| Board | Display |
|-------|---------|
| [Waveshare ESP32-S3-RLCD-4.2](https://www.waveshare.com/wiki/ESP32-S3-RLCD-4.2) | 4.2" reflective LCD, 400x300 |
| [Seeed Studio reTerminal E1001](https://wiki.seeedstudio.com/getting_started_with_reterminal_e1001/) | 7.5" e-paper (UC8179), 800x480 |
| [Waveshare E-Paper Driver HAT](https://www.waveshare.com/wiki/E-Paper_Driver_HAT) on a generic ESP32 host (any BLE-capable target: ESP32, S3, C2, C3, C6, H2) | Configurable, default 7.5" V2 BW (UC8179), 800x480 |
| [M5Stack PaperS3](https://docs.m5stack.com/en/core/papers3) | 4.7" e-paper (ED047TC1), 540x960 |

A demo video with the [Waveshare ESP32-S3-RLCD-4.2](https://www.youtube.com/watch?v=PgSaroeM3CE). Also, the fragile screen broke during the tests, so this device really needs a protective glass.

### Hardware selection

Before building, select the target board with `idf.py menuconfig`.
Navigate to **DRAFTLING Configuration > Hardware Model** and choose
the board you are building for:

- **Waveshare ESP32-S3-RLCD-4.2** -- 4.2" reflective LCD (400x300)
- **Seeed Studio reTerminal E1001** -- 7.5" e-paper, UC8179 (800x480)
- **Waveshare E-Paper Driver HAT** -- UC8179 SPI HAT on any
  BLE-capable ESP32 host (ESP32, ESP32-S3, ESP32-C2/C3/C6, ESP32-H2;
  the ESP32-S2 is not supported because it has no BLE radio).
  **PSRAM is required** -- enable "ESP PSRAM" in menuconfig before
  selecting this model, since the editor buffer, framebuffers, and
  LVGL display buffers all live in external SPI RAM.
  Display resolution and every SPI/control pin are user-editable in
  the same menu (see the **Waveshare E-Paper Driver HAT pinout**
  sub-menu); defaults match the ESP32-S3-DevKitC-1 wiring used by
  Waveshare's example projects.
- **M5Stack PaperS3** -- 4.7" e-paper (ED047TC1, 540x960). Driver is
  a 1-bpp B/W shim over the official `m5stack/M5GFX` managed
  component; partial refresh and grayscale are not implemented yet.

The display resolution and driver are configured automatically based
on the selected model (with the HAT-specific overrides described
above). You can also adjust the **Display rotation angle** in the
same menu.

The user connects a Bluetooth keyboard and edits Markdown files stored on the
SD card. The reflective LCD needs no backlight and works well in daylight.
On request the device connects to WiFi and synchronizes files with a remote
Git repository via the GitHub REST API.

## Hardware

| Feature | Waveshare RLCD-4.2 | Seeed reTerminal E1001 | Waveshare EPD HAT (DevKitC-1) | M5Stack PaperS3 |
|---------|--------------------|------------------------|-------------------------------|-----------------|
| MCU | ESP32-S3 (16 MB flash, 8 MB OPI PSRAM) | ESP32-S3 (XIAO module, 8 MB PSRAM) | ESP32-S3-DevKitC-1 (or any ESP32-S3 board) | ESP32-S3 (16 MB flash, 16 MB PSRAM) |
| Display | 4.2" reflective LCD, 400x300, SPI | 7.5" e-paper UC8179, 800x480, SPI | UC8179 e-paper HAT, panel preset (default Waveshare 7.5" V2 / GDEW075T7, 800x480), SPI | 4.7" e-paper ED047TC1, 540x960, parallel I80 |
| Storage | MicroSD (SDMMC 1-bit) | MicroSD (SPI, shared bus with display) | Optional SD on dedicated SPI3 (off by default) | Onboard MicroSD (SPI3) |
| Input | BLE HID keyboard | BLE HID keyboard | BLE HID keyboard | BLE HID keyboard |
| Connectivity | WiFi 802.11 b/g/n | WiFi 802.11 b/g/n | WiFi 802.11 b/g/n | WiFi 802.11 b/g/n |
| Battery monitor | GPIO4 ADC (3:1 divider) | GPIO1 ADC (2:1, GPIO21 enable) | not present | GPIO3 ADC (2:1 divider) |
| Wake from sleep | GPIO18 button | KEY0 / right green button (GPIO3) | GPIO0 (BOOT button, configurable) | Power button (GPIO21) |

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
| -- Seeed Studio reTerminal E1001 | | | 7.5" e-paper, UC8179, 800x480 |
| -- Waveshare E-Paper Driver HAT | | | UC8179 SPI HAT on any BLE-capable ESP32 host (resolution and pinout configurable; not available on ESP32-S2) |
| -- M5Stack PaperS3 | | | 4.7" e-paper, ED047TC1, 540x960 (driver via m5stack/M5GFX) |
| **Display width / height (px)** | int | depends on model | Editable for the HAT model only; fixed for all other boards. |
| **Display rotation angle** | choice | 0 degrees | Rotate the display by 0, 90, 180, or 270 degrees. |
| **E-paper full-refresh interval** | int | 50 | UC8179-only: number of partial refreshes between full refreshes. |

#### Waveshare E-Paper Driver HAT pinout sub-menu

Visible only when the HAT model is selected. Defaults match the
ESP32-S3-DevKitC-1 wiring used in Waveshare's example projects, but
every option can be changed to match a different host board.

| Option | Default GPIO | Description |
|--------|--------------|-------------|
| EPD MOSI / DIN | 11 | SPI MOSI |
| EPD SCK / CLK | 12 | SPI clock |
| EPD DC | 13 | Data / command |
| EPD CS | 10 | Chip select |
| EPD RST | 14 | Reset |
| EPD BUSY | 9 | Busy input from the panel |
| Wakeup GPIO | 0 | EXT0 wakeup pin (default GPIO0 = BOOT button) |
| Enable SD card on a separate SPI bus | n | Opt in to an optional SD card on SPI3 |
| SD MOSI / MISO / SCK / CS | 35 / 37 / 36 / 34 | Visible only when SD support is enabled |

> **Note about the e-paper models:** the UC8179 driver (used by the
> reTerminal E1001 and the Waveshare HAT) uses partial refresh by
> default. Small changes (typing, cursor blink, status-bar updates)
> take roughly 300 ms; a full refresh (3-5 s) is performed
> automatically every `DRAFTLING_EPD_FULL_REFRESH_INTERVAL` partials
> (default 50) to clear residual ghosting. Tune the interval in
> `idf.py menuconfig`. The PaperS3 driver currently performs a full
> refresh on every flush (partial-refresh support is a TODO).

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
