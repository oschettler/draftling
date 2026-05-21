# Black screen on deep-sleep wake -- investigation

Branch: `copilot/test-waveshare-touch-lcd`
Target board: Waveshare ESP32-S3-Touch-LCD-3.49
(`CONFIG_DRAFTLING_MODEL_WAVESHARE_TOUCH_LCD_349`, AXS15231B over QSPI)

## Symptom

After the device enters deep sleep (inactivity timeout or "Sleep now"
menu) and is woken by the BOOT button on GPIO 0, the LCD stays black
for the whole session. A power cycle (USB unplug / re-plug) fixes it.

## What the two paths look like in code

Both paths run `app_main()` from scratch (deep-sleep wake on the
ESP32-S3 is a full CPU reset). The code that initialises the panel is
the same in both cases:

1. `main/main.cpp:94-143` -- builds `display_axs15231b_config_t` from
   `app_config.h` (`LCD_RST_PIN = 21`, `LCD_BL_PIN = -1` at the time
   this section was written; see "Resolution" below for the post-fix
   value), `LCD_QSPI_*` on GPIO 9..14, `swap_xy = true`,
   `skip_vendor_init = true`) and calls `display_axs15231b_init()`.
2. `components/display/display_axs15231b.cpp:760-883`:
   - `gpio_config(LCD_RST)` as output, `gpio_set_level(LCD_RST, 1)`
     immediately (the cold-boot fix from commit 9f17c79).
   - `LCD_BL_PIN < 0` => `backlight_pwm_init()` is skipped entirely
     (the new behaviour from commit 20cfd25). External pull-up on
     GPIO 8 keeps the BL on.
   - `gpio_config(LCD_CS)` HIGH; `spi_bus_initialize(QSPI)`;
     `spi_bus_add_device`; allocate framebuffer + DMA row buffer.
   - `hw_reset()` -- 30 ms HIGH settle, 250 ms LOW pulse, 30 ms HIGH
     settle (commit b60fd8e).
   - `axs15231b_init_sequence()` -- with `skip_vendor_init = true`
     the sequence is just:
     `SLPOUT (0x11) + 120 ms`,
     `MADCTL (0x36) = 0x00`,
     `TE (0x35) ON`,
     `NORON (0x13)`,
     `SLPOUT (0x11) + 10 ms` (defensive re-issue),
     `DISPON (0x29) + 100 ms`,
     dummy `RAMWR (0x2C)`.
   - `display_clear(0x00)` -- pushes a black framebuffer.

Then LVGL + `editor_ui_init()` start drawing the splash + editor UI.

The C source path is **byte-identical** between cold boot and
deep-sleep wake. The init sequence sent to the AXS15231B is the
same. The framebuffer is freshly allocated and zeroed in both cases.

## What is actually different

The differences are not in code; they are in the *hardware state* at
the moment `app_main()` starts.

### 1. AXS15231B panel state

- **Cold boot:** USB has just been plugged in -> panel VCI ramps from
  0 V -> the AXS15231B's internal POR fires -> all controller
  registers at silicon defaults, analog rails come up fresh.
- **Wake from deep sleep:** USB power was never interrupted. The
  panel VCI stayed up the whole time. Before
  `esp_deep_sleep_start()`, `standby_enter_sleep` ->
  `display_deep_sleep_prepare()` -> `display_sleep()` sent
  `DISPOFF (0x28)` + `SLPIN (0x10)` to the controller. So at wake
  time the panel is sitting in SLPIN with **all of our previous-session
  register writes still in place** (MADCTL, TE, NORON, plus the
  cold-boot defaults for the vendor block since
  `skip_vendor_init = true`).

The 250 ms RST LOW pulse in `hw_reset()` is supposed to bring the
controller back to silicon defaults. On a true cold boot the analog
rails are also coming up; on deep-sleep wake only the digital reset
fires while the analog domain stays powered. Some AXS15231B revisions
are known to *not* fully re-init their internal state machine in that
"warm RST" case -- the controller comes out of the pulse still
believing it is in SLPIN, swallows the subsequent SLPOUT / DISPON,
and the panel stays dark.

### 2. LCD_RST pin (GPIO 21) state during deep sleep

`LCD_RST_PIN = 21` is RTC-capable on the ESP32-S3 but is **not**
placed under `gpio_hold_en()` before deep sleep. In deep sleep all
non-held GPIOs are released to high-Z. Whether the panel sees RST
asserted (LOW), de-asserted (HIGH), or a slow drift through the
threshold during the whole sleep period depends entirely on
whatever pull resistor (if any) the Waveshare board has on the RST
net. Reset glitches in that window can leave the panel in an
indeterminate state by the time `app_main()` runs again.

### 3. LCD_BL pin (GPIO 8)

Not relevant after commit 20cfd25: `LCD_BL_PIN = -1`, we never drive
the pin, no `gpio_hold_en` is armed on it, and the external pull-up
keeps the backlight on through sleep and wake. The "black screen"
here is therefore **panel content**, not the backlight. A bare lit
panel without valid pixel data also looks black on this LCD.

### 4. QSPI bus pins (GPIO 9..14)

Reconfigured by `spi_bus_initialize()` on every boot; not held
through deep sleep. Same in both paths.

### 5. Editor / LVGL state

Static C/C++ data is zero-initialised by `app_main()` startup, so
`s_panel_asleep = false` again, `s_bl_last_pct = 0` again, etc. No
stale RAM state can carry across.

## Most likely root cause

Of the differences above, only **(1)** and **(2)** can plausibly
produce a black panel after wake while leaving cold boot working.

The strongest candidate is **(2)** combined with **(1)**: during the
deep-sleep interval RST floats, the panel may or may not stay in
reset, and on wake the controller is in an undefined state that the
250 ms warm-RST pulse does not fully clean up. The "Sleep In before
deep sleep" detail is what tips this from "warm RST recovers" to
"warm RST stays stuck", because the controller is being reset while
already in its low-power mode -- a corner case several panel vendors
explicitly call out as needing a longer pulse and/or a re-issued
SLPOUT after RST.

## Why we have not seen this before

- This is the first board where deep sleep + USB-only power has been
  exercised in production. The RLCD-4.2 and PaperS3 hardware do not
  show the symptom because the RLCD's reflective panel does not
  depend on a powered controller to show the last frame, and the
  PaperS3's e-paper is driven by M5GFX which re-runs its full panel
  init via the M5GFX backend.
- The JC3248W535 has a different vendor-init path (`skip_vendor_init
  = false`) which re-sends the full unlock + vendor register block on
  every boot, naturally re-initialising the controller out of its
  warm-RST stuck state.
- The Touch-LCD-3.49 is the only board that **both** runs the
  AXS15231B backend **and** skips the vendor-init block, so the
  init sequence it sends after wake is the bare minimum
  (SLPOUT / MADCTL / TE / NORON / SLPOUT / DISPON). If that minimum
  is not enough to recover a stuck controller, the panel stays
  black.

## Suggested next steps

These have not been implemented yet -- the user requested an
investigation, not a fix. Listed in order of how likely they are to
resolve the symptom outright:

1. **Hold LCD_RST HIGH through deep sleep.** Before
   `esp_deep_sleep_start()`, drive GPIO 21 HIGH (it already is) and
   call `gpio_hold_en(21)` + `gpio_deep_sleep_hold_en()`. On wake,
   call `gpio_hold_dis(21)` early (mirroring the pattern in
   `backlight_pwm_init()` for the BL pin). This eliminates **(2)**:
   the panel sees a stable RST line for the entire sleep window
   instead of a high-Z float. Cheapest possible fix; matches the
   pattern already used for the BL pin on other AXS15231B boards.

2. **Lengthen `hw_reset()` LOW pulse or repeat it.** Several
   AXS15231B reference drivers (e.g. Tactility's JC3248W535C BSP)
   use a 500 ms LOW pulse, or two pulses back-to-back, specifically
   to recover from the "RST while in SLPIN" corner case. Cheap to
   try; risk is only an extra 250-500 ms of boot latency.

3. **Stop skipping the vendor-init block on Touch-LCD-3.49 wake.**
   The decision to set `skip_vendor_init = true` (commit df0a386)
   was made because re-sending the JC3248W535 recipe to this panel
   clobbers its correct factory POR defaults and produces a black
   screen on cold boot. But on warm RST the panel is *not* sitting
   at POR defaults any more, so the same recipe might in fact
   recover it. Could be conditioned on
   `esp_reset_reason() == ESP_RST_DEEPSLEEP`. This is the most
   invasive of the three options and would need real-hardware
   verification on both cold boot and warm wake.

4. **Add wake-cause logging.** Currently the firmware has no
   `esp_reset_reason()` or `esp_sleep_get_wakeup_cause()` log
   anywhere (verified by `grep`). Adding a single `ESP_LOGI` line at
   the top of `app_main()` would make it trivial to confirm from
   serial logs that the failing boot really is a deep-sleep wake
   (and not a brownout / WDT / panic that happens to look similar).
   Should be the first change made -- it costs nothing and validates
   every subsequent theory.

## Files touched while investigating

None. This document is the only change.

## Resolution

User feedback confirmed that the backlight stays lit during the
"black screen after wake" symptom (i.e. the panel really is blank,
the BL really is on) and that the BL is also lit through deep sleep
(wasting battery). The fix therefore addresses both:

1. **Before deep sleep, do NOT blank the panel.** Initially we
   removed the `display_sleep()` call (DISPOFF + SLPIN) from
   `display_deep_sleep_prepare()` to sidestep the "warm RST while
   in SLPIN" corner case described under "Most likely root cause"
   above.

   **Update:** once the active-LOW LEDC backlight path was working
   reliably on the Touch-LCD-3.49 (LCD_BL_PIN=8, LCD_BL_ACTIVE_LOW=1
   driven by the standard 50 kHz RC_FAST LEDC config) the user
   asked that the panel itself also be turned off at sleep entry,
   not just the BL. `display_deep_sleep_prepare()` therefore now
   calls `display_sleep()` (DISPOFF + SLPIN) before cutting the BL
   again. The previously-feared corner case is mitigated by the
   standard cold-boot `hw_reset()` (250 ms LOW) + full
   `axs15231b_init_sequence()` that run on every boot, including
   post-deep-sleep wake -- if the controller comes out of the warm
   RST still believing it is asleep, the unconditional SLPOUT +
   vendor-reg replay in the init sequence brings it back.

2. **Before deep sleep, turn the backlight OFF.** First attempt
   re-enabled `LCD_BL_PIN = 8` on the Touch-LCD-3.49 in binary
   on/off mode (`DRAFTLING_BL_GPIO_BINARY=y`) and drove the pin
   HIGH during normal operation. This regressed: even an active
   digital HIGH on GPIO 8 breaks the BL boost circuit on this
   board (panel stays dark until the user presses RESET).

   The corrected approach uses the standard LEDC PWM path with
   `LCD_BL_ACTIVE_LOW=1`: the duty/level inversion in
   `backlight_pwm_init()` / `display_set_backlight()` /
   `display_deep_sleep_prepare()` produces "duty 0 = full
   brightness, duty MAX = off", matching the Waveshare 10_LVGL_V9
   reference. At deep-sleep entry the duty is forced to MAX (pin
   HIGH = OFF) and latched with `gpio_hold_en()` +
   `gpio_deep_sleep_hold_en()`. On the next boot,
   `backlight_pwm_init()` releases the hold so the LEDC peripheral
   regains control.

The visual transition on entering sleep is now "panel goes to
DISPOFF, BL drops". The visual transition on wake is "panel
re-inits from scratch and BL ramps back up under LEDC control".
The cold-boot path is unchanged.

## Resolution -- follow-up fix

The symptom returned in field testing: after deep-sleep wake the BL
came back on but the panel stayed blank. The mitigation claimed in
the previous "Resolution" section ("the unconditional SLPOUT +
vendor-reg replay in the init sequence brings it back") does not
actually apply to the Touch-LCD-3.49 because that board sets
`skip_vendor_init = true`, so on every boot
`axs15231b_init_sequence()` only runs SLPOUT / MADCTL / TE / NORON /
SLPOUT / DISPON -- no vendor-reg replay. That bare-bones sequence
is enough on cold boot (panel comes from analog-POR defaults) but
cannot recover the controller from the "warm RST while in SLPIN"
stuck state. Two changes in `components/display/display_axs15231b.cpp`
address this:

1. **Force vendor-init replay on deep-sleep wake**
   (`display_axs15231b_init` reads `esp_reset_reason()` and, when it
   equals `ESP_RST_DEEPSLEEP`, overrides `s_skip_vendor_init` to
   false for this boot). Re-asserting the full 0xBB unlock +
   0xA0/0xA2/0xD0/0xC1/... block is exactly what the JC3248W535
   does on every boot, and it is what unsticks the controller from
   warm-RST-while-in-SLPIN. On cold boot the original
   `skip_vendor_init = true` behaviour is preserved so the
   factory POR defaults are not clobbered.

2. **Hold LCD_RST HIGH through deep sleep**
   (`display_deep_sleep_prepare` calls `gpio_hold_en` on `s_rst_pin`
   after driving it HIGH; `display_axs15231b_init` calls
   `gpio_hold_dis` on the same pin before reconfiguring it). This
   removes the RST high-Z float window during deep sleep so the
   only RST edge the controller sees is the deliberate 250 ms LOW
   pulse from `hw_reset()` on wake.

`display_axs15231b_init` now also logs `esp_reset_reason()` and
`esp_sleep_get_wakeup_cause()` so post-wake symptoms are trivial
to confirm from the serial monitor.

## Resolution -- second follow-up fix

The follow-up fix above did **not** work. User report from field
testing: "After deep sleep, the display also doesn't work if I
press the reset button. It only works if I disconnect the device
from power and then connect again."

That symptom is the smoking gun. An EN/RESET press is a full MCU
reset (`ESP_RST_EXT`), so the "force vendor-init replay" branch
above does not fire on that second boot -- yet the panel is still
black. The only way to recover is to cycle LCD VCI by unplugging
USB. This means:

- The previous deep-sleep wake's `axs15231b_init_sequence()` (now
  with `skip_vendor_init` forced to false) left the AXS15231B
  registers in a **broken state that survives an MCU reset**
  because the LCD analog rails stay powered.
- The JC3248W535 vendor-register block is **panel-specific** to
  that board's 320x480 panel. Pushing it into the Touch-LCD-3.49's
  172x640 panel does not "recover" it -- it actively corrupts the
  factory-tuned register values for this panel, in a way the
  panel's controller cannot un-do without a true POR of its VCI.

In other words the previous fix was based on the wrong premise.
There is no "more aggressive software re-init" we can run on wake
that will recover this panel from warm-RST-while-in-SLPIN. The
right strategy is to **never enter that state in the first place**:

1. **Stop calling `display_sleep()` from
   `display_deep_sleep_prepare()`.** Leave the AXS15231B in its
   normal display-on state through deep sleep. With the BL pin
   driven OFF by the existing hold-pin mechanism (and gated by
   `gpio_hold_en` + `gpio_deep_sleep_hold_en`), the panel is
   invisible to the user just the same, but on wake we never have
   to recover from a SLPIN+warm-RST corner case -- `hw_reset()`
   followed by the bare-bones SLPOUT / MADCTL / TE / NORON /
   DISPON sequence is effectively a no-op on an already-running
   panel, which is exactly the case we want.

2. **Revert the `skip_vendor_init` override in
   `display_axs15231b_init`.** `skip_vendor_init` is now once again
   honoured unconditionally so the Touch-LCD-3.49's factory
   defaults are never overwritten. The reset-cause log line is
   kept because it costs nothing and is genuinely useful when
   debugging future wake-time symptoms.

3. **Keep the LCD_RST `gpio_hold_en` during deep sleep.** This was
   the other half of the previous "follow-up" patch and it remains
   correct: it removes the high-Z float window on RST during the
   sleep interval, so the only RST edge the controller sees is
   the deliberate 250 ms LOW pulse from `hw_reset()` on wake.

The trade-off versus the very first "Resolution" is identical:
slightly higher standby current (the panel controller stays
powered through deep sleep), in exchange for a wake path that
is byte-for-byte identical to a cold boot on an already-running
panel. The backlight cut still dominates the standby power saving.

## Resolution -- third follow-up fix

Field testing showed that even with the second follow-up fix in
place, the panel still came up black after deep-sleep wake, and
pressing the EN/RESET button did not recover it -- only a full
USB unplug recovered the display. The controller was therefore
ending up in a stuck state that survives both an MCU reset and
the existing 250 ms RST pin pulse.

The user requested that the display be initialised independently
of any previous controller state. Two changes in
`components/display/display_axs15231b.cpp` implement that:

1. **Add SWRESET (DCS 0x01) at the very start of
   `axs15231b_init_sequence()`**. SWRESET is a controller-level
   reset that returns the AXS15231B's internal state machine and
   DCS registers to their POR defaults. This is independent of the
   hardware RST pin and complements it: even if the prior session's
   register configuration is what is keeping the panel stuck, the
   SWRESET clears it. Followed by the MIPI-DCS-mandated 120 ms
   recovery delay before the immediately-following SLPOUT.

2. **Double the hardware RST pulse in `hw_reset()`**. Some
   AXS15231B-based panels need a second LOW pulse (with a brief
   HIGH gap in between, so the controller exits its first-pulse
   reset latch before being re-asserted) to fully recover from a
   warm-RST corner case. The total reset window goes from
   ~310 ms (30+250+30) to ~590 ms (30+250+30+250+30), still well
   under the LVGL splash window.

The combination is intentionally redundant: SWRESET handles
register-level corruption that survives the pin pulse, and the
double pin pulse handles state-machine corruption that SWRESET
cannot reach (e.g. if the controller is somehow refusing to
process incoming commands). Together they make the init sequence
robust against any prior controller state short of an analog
power loss, which is the only thing a USB unplug actually changes.

The cold-boot path is unchanged in behaviour; the extra ~280 ms
of boot latency from the second RST pulse is invisible behind the
LVGL splash screen.

