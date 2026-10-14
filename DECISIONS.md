# Decisions

Each entry: decision — rationale. Most recent at the bottom.

## Phase 0 — 2026-10-12

**D1. Name: "Kronograf".** Kept: short, Nordic-sounding, evokes watchmaking without clashing with an existing brand. The folder stays `~/projects/kronograph`, created by the user (the brief said `~/projets/kronograf`, which did not exist; `~/projects` is the real working folder).

**D2. Reference:** `~/projects/My-TTGO-Watch` (read-only). It is a pristine upstream clone; the user's tastes show more clearly in their 2021 **bttfWatch** project (see `docs/AUDIT.md`).

**D3. Board revision: not yet detected** (watch absent from the USB bus, see `BLOCKERS.md` B1). Working assumption **V1**, from bttfWatch's `config.h` (`LILYGO_WATCH_2020_V1`), to be confirmed by `esptool chip_id/flash_id` and an I²C/UART scan before any flashing. The revision is a build parameter (`-D KG_BOARD_V1|V2|V3`) **and** the inventory is checked at run time (I²C scan): an optional peripheral missing from the scan does not appear in the UI.

**D4. New project rather than a fork.** My-TTGO-Watch: LVGL 7, an Arduino framework fork frozen in 2021, ~40 generic apps and an auto-registration system — exactly the "watch OS" anti-pattern the brief warns against. We reuse the knowledge (AXP202/BMA423 sequences, pinout, pitfalls), not the code.

**D5. Stack:**
- PlatformIO + **Arduino-ESP32 2.0.x** (`espressif32@6.x`, IDF 4.4): NimBLE, Wi-Fi, NVS and OTA available; pure IDF brings no decisive gain here and lengthens development.
- **LVGL 8.3.11** rather than 9.x: stable, well-documented API, smaller RAM footprint, `lv_snapshot` and image rotation available; 9.x had performance regressions on ESP32 and a moving SDL port. Can be revisited if a 9.x feature becomes necessary.
- Thin in-house drivers: ST7789 through `esp_lcd` (SPI DMA), FT6336 and PCF8563 via direct I²C registers. **XPowersLib** (lewisxhe, maintained) for the AXP202, **SensorLib/BMA423** for the accelerometer (Bosch's feature firmware is required for steps/tilt/double tap). No TTGO_TWatch_Library (monolithic, bundles LVGL 7).

**D6. Power safety (rule 3):** the HAL exposes **no** function to set DCDC3 or to switch off DCDC3/LDO2-ESP. My-TTGO-Watch's practice of lowering DCDC3 in sleep is not carried over. LDO2 (backlight + ST7789 on V1) stays on; the panel goes into `SLPIN` and the PWM to 0.

**D7. Simulator:** CMake + SDL2 (Homebrew), `ui/` sources shared verbatim with the ESP32 target, controllable simulated HAL. Headless mode writes PNGs through `stb_image_write` (public domain, single header).

## Stage 0 — hardware — 2026-10-12

**D8. Original firmware backed up.** `backups/twatch-original-20261012.bin` (outside git) — 16,777,216 bytes, **SHA-256 `bda0e42e9bc03c051208ea4511217f8fbeff7cd537400aa86ae74b2397ed7898`**. Read in 1 MB blocks at 230,400 baud (`tools/backup_flash.sh`), each block MD5-verified by esptool; 460,800 baud produced serial noise on this cable. Bootloader magic 0xE9 at 0x1000. Original partition table: nvs 20K @0x9000, otadata, app0/app1 6400K, spiffs 3520K @0xC90000 (16 MB, two OTA slots — most likely My-TTGO-Watch firmware). Restore with: `esptool.py --port <port> --baud 230400 write_flash 0 backups/twatch-original-20261012.bin`.

**D9. Chip:** ESP32-D0WDQ6 rev v1.0, 40 MHz crystal, 16 MB Winbond flash. Compatible with V1/V2 (V3 normally ships an ESP32 rev3); V1 build first, confirmed by the I²C scan and the display.

**D10. Serial port:** `/dev/cu.usbserial-*` (Apple driver), not `/dev/cu.SLAB_USBtoUART` (Silicon Labs driver, repeated failures). Uploads at 230,400 baud.

**D11. First flash (stage 0) verified.** Clean boot; I²C scan: AXP202, PCF8563, BMA423, FT6336 present. 4 MB PSRAM. `status`: internal heap 74 % free (222 KB / 298 KB, budget ≥ 30 %), 4.19 MB PSRAM free, LVGL 18 %. RTC with the VL bit set (time unreliable) → NTP setup in stage 1. The screenshot taken from the watch over serial matched the simulator's. While charging, the battery value turns green instead of prepending "Charging ·" (it overflowed onto the clock, seen on the watch capture).

**D12. Black screen on first flash — fixed.** Symptom: backlight on, black image, while the LVGL capture was correct. Cause: with no RST pin, `esp_lcd` does a software reset and then `SLPOUT` after 20 ms; if the panel was active (warm reboot), the ST7789 datasheet requires 120 ms → `SLPOUT` ignored, panel asleep. Fix: 150 ms after the reset, `SLPOUT` sent again, 120 ms. Validated with a photo from the user: orientation, RGB order and inversion are correct. Added the `lcdtest` command (R|G|B pattern outside LVGL).

**D13. V1 revision confirmed**: the backlight responds on GPIO12 (V2: GPIO25, V3: GPIO15), vibration motor and four I²C peripherals present.

**D14. Display SPI capped at 40 MHz.** The T-Watch pinout (MOSI 19, SCLK 18) does not match the native IO_MUX pins of any SPI host: the signals go through the GPIO matrix, limited to 40 MHz on the ESP32. 80 MHz is not reliably achievable. Consequence: a full frame (115 KB) takes ~23 ms → 60 fps animations must redraw partial areas (hands, segments), never the whole screen.

## Stage 1 — 2026-10-12

**D15. Validated on the watch:** NTP (Wi-Fi on for ~3 s then off, time written to the PCF8563 in UTC), touch without inversion (top-left corner → (29,28), bottom-right → (196,210)), circular bezel drag, auto screen-off after 10 s, wake and sleep with the button. **Still to validate:** wrist-raise and double-tap wake-up (BMA423 remap `0x88` = identity axes, unverified — the user cannot wear the watch for now).

**D16. Seconds hand:** 8 Hz, ~17–20 ms per frame at 240 MHz (render + SPI) for 15–21 kpx, i.e. ~15 % CPU. Vector hands (`lv_line` on a bounding box) chosen; rotated images were not prototyped: rotating a 100 px image in LVGL 8 would cost a transform of every pixel in the box per render, with no sharpness gain on straight batons.

**D17. Workshop mode (brought forward from stage 4).** USB dropouts (B2) prevented validating the stages. On USB **and** with Wi-Fi configured: Wi-Fi stays on, OTA (`pio run -e twatch-v1-ota -t upload`, `kronograf.local`) and a TCP console on port 2323 (`tools/kg.py --host kronograf.local …`). Unplugged: Wi-Fi off. Justification against the "always-on Wi-Fi" anti-pattern: on mains power, battery life is not at stake; can be disabled with `-D KG_WORKSHOP=0`. Optional OTA password (`KG_OTA_PASS`).

**D18. Pitfalls noted:** `sntp_get_sync_status()` resets its state when read; opening the serial port on macOS reboots the ESP32 (CP210x auto-reset); logs from other tasks interleave with the serial output (row-indexed captures + retry).

## Stage 2 — 2026-10-12

**D19. Face 3 = "Night"** (time only, Barlow Condensed 104 px, dark red `#9A140C`, backlight at 25 % through `hal_brightness_scale`, one update per minute) rather than a blend of the two other worlds: the first two faces are already rich; what was missing was a sober face, readable at night without glare, and the most power-frugal (no animation, dimmed backlight).

**D20. 7-segment displays drawn in code** (`src/ui/seg7.c`, anti-aliased polygons, 0.09 slant) rather than a 7-segment font: full control over the unlit segments (18/255 mix), the glow (two widened semi-transparent polygons), the central filament and the ignition flicker (40→200→70→230→150→255 curve over 180 ms, only on newly lit segments). Stencil labels: Allerta Stencil (OFL).

**D21. Row contents:** red = next alarm (dashes + "NO ALARM" until stage 3 existed); green = present time; amber = last NTP sync (more telling than "last wake-up", which would always read "just now"). Easter egg: long press on the green row, or 10,000 steps reached (once a day) → three flashes of branching lightning over 0.7 s + vibration; the discharge sound was planned with the I²S driver.

**D22. Snapshot transitions.** `lv_scr_load_anim` re-composed both faces on every frame: 43 ms/frame (≈ 23 fps, below the 30 fps floor). Now: two `lv_snapshot`s in PSRAM (2 × 115 KB), then two images are slid → **25 ms/frame on average (≈ 40 fps)**, bounded by SPI; one isolated frame at ~94 ms on the real face's first render. Bonus: `lv_scr_load_anim` is no longer used — it **crashes in LVGL 8.3.11** when called during a transition (`scr_load_internal` sets `scr_to_load` to NULL before `lv_obj_set_pos`); requests that arrive during a transition are queued.

**D23. Persistent settings** (NVS, namespace `kg`): `face` (written 5 s after the last change — never during a transition, no wear on every swipe), `brightness`. Verified across a reboot.

**D24. Hang on wake-up (watchdog) — fixed.** Symptom: after a few sleep/wake cycles in workshop mode, no response, then `reset=6` (TWDT). Coredump: core 0 monopolised by the `wifi` task. Cause: `setCpuFrequencyMhz(80)` called on every loop iteration with the screen off, and 240 MHz on wake-up, **with Wi-Fi active** — which breaks Arduino-ESP32's Wi-Fi stack. Fix: the frequency only changes on a real transition and never with Wi-Fi on; no light sleep with Wi-Fi on. Validated: 30 sleep/wake/face cycles with no reset or coredump (before: failure in < 3 cycles).

**D25. Diagnostic tooling:** main loop under an 8 s watchdog in panic mode (coredump instead of a silent reset), fed during OTA; `coredump`, `coredump raw` (decoded by `esp-coredump` with every stack) and `reboot` commands; every build archives its ELF as `build-archive/<sha256[:16]>.elf` (the hash written into coredumps) — a dump can always be decoded.

## Stage 3 — 2026-10-12

**D26. Launcher = a grid of "sub-dials"** (2 × 3, round recessed buttons like the aviator face's counters, vector pictograms drawn in code) rather than a list: consistent with the watchmaking world, and everything fits on one screen without scrolling. Gestures: swipe up from a face → launcher; down → back to the face; swipe right inside an app → launcher. Snapshot transitions (`transition.c`, shared with the faces).

**D27. Model without LVGL timers** (`model.c`): stopwatch, timer and alarms are computed from timestamps (tick for the stopwatch, wall-clock time for the timer and alarms); they keep running with the screen off and in light sleep. Power management queries `model_next_event_time()` to schedule its timed wake-up and turns the screen on when something is due; the UI "shell" then shows the alert (vibration + chime, repeated every 2.2 s for one minute at most; an unacknowledged alarm snoozes automatically for 5 min). Validated on the watch: timer started with the screen off → `due` wake-up → alert screen.

**D28. Synthesised sounds**: dedicated audio task (core 0), sine waves with a 6 ms attack/release (no clicks), I²S at 16 kHz to the MAX98357A; the amplifier (LDO3, the V1 audio rail — not an ESP32 rail) is only powered while playing. Vibration motor driven by an `esp_timer` (non-blocking patterns), 12 ms haptic tick on every button.

**D29. Steps over 7 days**: the first time a new day is seen, the BMA423 count is pushed into a rolling NVS history (`st0`…`st6`) before being reset; days when the watch was not running count as 0.

**D30. Wi-Fi entered on the watch** (Settings → Wi-Fi: asynchronous scan, touch keyboard, password masked and cleared from the screen once confirmed), stored in NVS (`kgnet`) and taking precedence over `secrets.h`. Scanning only turns Wi-Fi on for the duration of the scan. Keyboard in Montserrat 14 (the only font with LVGL's keyboard symbols; our generated fonts are `const`, with no fallback font).

**D31. Settings**: brightness (slider), screen timeout 5/10/15/30 s, default face (applied when returning to the faces), 24-hour clock (digital faces), about. All persistent (NVS).

**D32. Robustness (see B3)**: GPIO interrupts restored after light sleep; 3 s USB debounce; all-or-nothing TCP writes and the watchdog fed during long commands (a Wi-Fi screenshot exceeded the 8 s watchdog — seen in a coredump); RTC watchdog + RTC breadcrumbs (limited effect in light sleep, documented in B3).

**D33. Reliable light sleep (see B3): RTC watchdog suspended and GPIO interrupts disabled during sleep.** The RWDT now only watches the awake watch; it is disabled just before `esp_light_sleep_start()` and re-armed on wake-up, with the `RESET_RTC` action. Letting it count during sleep froze the watch on battery at the first sleep. Test mode `battsim 1` (console): simulates battery power while plugged in, to observe light sleep over the serial cable.

## Stage 4 — 2026-10-12

**D34. On-demand network "jobs".** A single background task (core 0, low priority, 8 KB stack for TLS) runs the pending jobs — NTP sync, weather, buses — over one connection, then turns Wi-Fi off (except in workshop mode on USB). At boot: NTP + weather in one connection. With the screen off nothing triggers Wi-Fi; light sleep stays forbidden while Wi-Fi is on.

**D35. Open-Meteo weather over plain HTTP** (no key, public coordinates of downtown Montréal, ~1.3 KB response): no TLS, hence no mbedTLS memory or handshake. Minimal in-house JSON parser (`src/data/kg_weather.c`, tested against a real response: `tests/test_data.c`), no ArduinoJson dependency. Persistent NVS cache (`kgwx`): the app and the complication show the last reading right from boot, with its age ("UPDATED 7 MIN AGO", "OFFLINE" in amber when the last update failed — nothing is erased). Refresh: when the app opens if > 10 min old; by the shell (screen on only) if > 30 min old.

**D36. STM buses: GTFS realtime read as a stream.** The TripUpdates feed is ~1 MB (1,277 entities, the largest ~3 KB): it is never stored. A `Stream` receives the bytes from `HTTPClient::writeToStream` (which handles chunked transfer) and hands them to an in-house protobuf parser (`src/data/kg_bus.c`) that keeps one entity at a time in a 16 KB PSRAM buffer and only retains departures at the followed stops (12 at most, sorted). HTTPS is verified against the pinned DigiCert Global Root G2 (`certs.h`, expires in 2038): the API key is never sent to a third party. Stops are entered on the watch (Settings → STM buses, numeric keypad, "." separates several codes) or from the console (`bus stops 52345,52346`). Buses are only refreshed while the app is open (every 60 s): no bus complication on the face, since a 1 MB download every minute with the screen on would cost too much. Without a key: no Settings section, a hint line in the app.

**D37. "Weather" app in the launcher's 6th slot**, rather than a separate Bus app: the 2 × 3 grid stays scroll-free. Vector weather pictograms (`wx_icon.c`, 9 families of WMO codes), temperature + pictogram complication in the aviator face's bottom-left corner (mirroring the day / battery at the top). Fonts regenerated with "°", the real minus sign (U+2212) and a few accented stencil capitals.

## Stage 5 — 2026-10-12

**D38. ANCS with NimBLE-Arduino 2.5.1** rather than Arduino's Bluedroid stack (≈ 100 KB more RAM). NimBLE host in PSRAM (`CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL`), a single connection, no scanning, logs off. The watch is a peripheral advertising an ANCS service solicitation (AD type 0x15). It becomes a GATT client of the iPhone over the same connection (`NimBLEServer::getClient`). The protocol is decoded in plain, host-tested C (`src/data/kg_ancs.c`: events, attribute requests, Data Source reassembly, UTF-8 clean-up for our fonts, app names).

**D39. Passkey pairing (MITM), not "Just Works"**: "display only" capability, 6-digit code drawn by `esp_random()`, shown large on the watch (the screen turns on), typed on the iPhone. Bonds stored by NimBLE in NVS; "Forget the iPhone" in Settings.

**D40. Memory room for BLE.** Linking the BLE controller overflowed DRAM (12 KB) and IRAM (2.9 KB). DRAM: the 64 KB LVGL pool moved to PSRAM (`LV_MEM_POOL_ALLOC`), static RAM 38 % → 22 %. IRAM: `esp_ringbuf` placed in flash by a `sections.ld` regenerated on every build (`tools/ld_ringbuf_flash.py`, equivalent to the IDF option `RINGBUF_PLACE_FUNCTIONS_INTO_FLASH`, safe because no IRAM ISR uses a ring buffer).

**D41. Power: no light sleep while BLE is active.** This board has no 32 kHz crystal; Arduino-ESP32's BLE controller then takes its low-power clock from the main crystal (`BTDM_CTRL_LPCLK_SEL_MAIN_XTAL`), which rules out light sleep. With the screen off, the watch stays in modem sleep at 80 MHz. Connection interval requested from the iPhone: 300–330 ms, latency 4. Bluetooth can be turned off in Settings (back to light sleep). Battery-life measurement: see the final report.

**D42. Safe mode**: three crashes in a row within the first minute → BLE is not started (workshop Wi-Fi and OTA stay available). An essential safeguard for updating, remotely, a watch whose USB cable is unreliable (B2).

**D43. Notification UI**: full-screen card on arrival (short vibration, screen on), "Clear" (also removes it on the iPhone, ANCS negative action) or "Decline" for a call; list of the last 8 by swiping down from a face; orange count badge on the aviator face; vector category pictograms.

**D44. Internal memory with BLE + Wi-Fi + TLS (measured on the watch).** With BLE, free internal heap dropped to 28 % at rest (workshop Wi-Fi on) and to **6 KB** during the STM download: a failure, one step away from a crash. Fixes: (1) mbedTLS's large buffers (≥ 4 KB: TLS records) go to PSRAM through `mbedtls_platform_set_calloc_free`; putting everything in PSRAM made the handshake fail (error −1), so small structures stay internal; (2) display buffers 40 → 30 lines (−9.6 KB of internal DMA RAM); (3) ANCS task stack 6 → 5 KB. Result: **33 % free at rest** (BLE + Wi-Fi), 36 KB minimum during an STM download with BLE active, departures received.

**D45. iPhone pairing and link recovery (verified on the watch).** iOS does not list a generic BLE accessory in Settings → Bluetooth, even with the ANCS solicitation: the first contact is made from an app like nRF Connect, passkey pairing follows, and the iPhone then reconnects by itself (seen after every OTA). Timers and alarms from the Clock app do not go through ANCS (calls, messages, apps do). After 15 min out of range, one reconnection stayed stuck at "connected" without encryption: the ANCS task now drops the link if encryption is not established within 15 s or if discovery fails, and the iPhone reconnects cleanly. The `ble` console command shows connections, encryptions, the last disconnect reason, the task's step, and GAP/ANCS counters.

## Publication — 2026-10-13

**D46. Everything in English.** Code, comments, the watch UI, console output, documentation and commit messages are in English, so the repository can be published and read by anyone. The fonts keep their accented glyphs: iPhone notifications and place names (Montréal) still need them. The Time circuits face now reads like the film: MONTH / DAY / HOUR MIN, DESTINATION TIME, PRESENT TIME, LAST TIME DEPARTED.

**D47. STM API key entered on the watch.** A firmware built without `secrets.h` (a public release) must still be able to show buses. The key can be typed in Settings → STM buses (touch keyboard, cleared from the screen once confirmed) or sent over the console (`bus key <key>`, `bus key clear`). It is stored in NVS (`kgnet`/`stmkey`), like the Wi-Fi password, and takes precedence over `secrets.h`. It is never displayed or echoed in full: only its last four characters ("…ab12").

**D48. Releases built by GitHub Actions only.** A `firmware.bin` built locally embeds the contents of `secrets.h` in clear text (Wi-Fi password, STM key): it must never be published. Pushing a `v*` tag runs `release.yml`, which builds on GitHub — where `secrets.h` does not exist, and the job checks it — then publishes the application binary, a merged image to flash at 0x0, and their SHA-256 sums.
