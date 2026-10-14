# Blockers

## B1 — Watch not detected over USB (solved, 2026-10-12)

**Symptom:** no USB serial device. `system_profiler SPUSBDataType` only lists a hub, a Logitech receiver and a DisplayPort adapter. No `/dev/cu.usbserial-*`, `cu.wchusbserial*` or `cu.SLAB_USBtoUART*`.

**Hypotheses tested:**
1. Missing driver → no: `com.silabs.cp210x` 6.0.2 is loaded; CH9102 bridges are handled natively by recent macOS.
2. Watch switched off → unlikely as the only cause: the USB-serial bridge is bus-powered and enumerates even with the watch off.
3. Remaining: a charge-only cable, a faulty port / hub, or the watch's micro-USB connector.

**Consequence (rule 2):** no flash backup possible → **no writes to the watch**. Work continued in the simulator (stage 0: audit, toolchain, simulator, screenshots); hardware verification would resume as soon as the port showed up.

**Next lead:** a known-good USB data cable, plugged straight into the Mac (no hub); check with `ls /dev/cu.*`.

**Update 2026-10-12:** after changing the cable the port appeared (`/dev/cu.usbserial-XXXX`, CP210x). `chip_id`/`flash_id` succeeded: **ESP32-D0WDQ6 rev v1.0**, 40 MHz, Winbond flash (ef/4018) **16 MB**. Then the port **vanished** during `read_flash` at 921,600 baud and did not come back → intermittent connection (micro-USB plug not fully seated, or a fragile cable). Nothing written; no partial backup kept.

**Solved:** plug pushed back in; full backup succeeded at 230,400 baud (see DECISIONS D8).

## B2 — Recurring USB dropouts (open, 2026-10-12)

**Symptom:** during an upload, the CP210x disappears from the USB bus; the `/dev/cu.usbserial-XXXX` and `cu.SLAB_USBtoUART` nodes remain but every open fails (`termios.error: (22, 'Invalid argument')`).
**Probable cause:** the same loose contact as B1 (the watch's micro-USB plug).
**Workaround:** work without the watch (simulator), batch flashing when the port comes back; the user re-seats the plug. Lasting options: a short cable, the watch lying flat, or Wi-Fi OTA (stage 4) to stop depending on the cable.

**Update (2026-10-12):** dropouts also happen **during a flash**, with the ESP32 in its ROM loader (our firmware is not running): so the cause is hardware (micro-USB connector / cable), not software. They do not reboot the watch (seen: dropout at 10:56:49, watch stayed awake, `uptime` continuous). Workarounds: flashing in verified, resumable 64 KB blocks (`tools/flash_chunks.py`), OTA in workshop mode whenever possible, TCP console.

**Update (2026-10-12, evening):** with the watch still plugged in, USB stopped delivering power altogether (no charging indicator, the CP210x no longer enumerated). The watch then behaves as if on battery — workshop Wi-Fi switches off and it vanishes from the network — which first looked like a firmware hang. Re-seating the cable did not help; the cable / connector pair is the suspect.

## B3 — Hang after a scheduled wake-up, watch frozen until a forced power-off (2026-10-12)

**Symptom:** during a timer test (screen off, unstable USB), the watch stopped answering over Wi-Fi and serial, the button had no effect; a forced power-off was needed. No coredump, no watchdog reset (`reset=1` after powering back on).
**Hypotheses and fixes applied:**
1. `gpio_wakeup_disable()` after light sleep reset the AXP202/BMA423 pins to "no interrupt": after a first light sleep, the button and the wrist woke nothing while the watch stayed awake. **Fixed** (NEGEDGE/POSEDGE restored).
2. USB micro-dropouts (connector): workshop Wi-Fi toggling on/off in a loop, with light sleep in between. **Fixed** (3 s debounce, `power_usb_stable()`).
3. The deadline was only scheduled by the UI (screen on). **Fixed** (power management queries the model).
**Safety net tried:** RTC watchdog (90 s) + breadcrumbs in RTC memory. Tested with a light sleep that has no wake-up source: **the watch did not reboot**, even after clearing `RTC_CNTL_WDT_PAUSE_IN_SLP` (ESP-IDF's sleep code seems to take the RWDT over, or the reboot stalls at boot — not verified). The RWDT is kept for hangs while awake; the tested scenario (sleep without wake-up) cannot happen in Kronograf, which always schedules a timed wake-up ≤ 60 s.
**Next lead:** if a hang happens again, read the breadcrumbs (`status` → "guard" line); consider moving to pure ESP-IDF to control the RWDT configuration during sleep.

**Update (2026-10-12, 11:00) — main cause found and fixed.** On battery, the watch froze at the very first light sleep (screen off ~20 s after use): fully black, button ineffective, forced power-off needed. That code path had never run on the watch (USB prevents light sleep). Retained cause: the RWDT armed by `guard.cpp` (`RESET_SYSTEM` action, `WDT_PAUSE_IN_SLP` cleared) stayed active during `esp_light_sleep_start()`. **Fix**: RWDT suspended around light sleep (`guard_suspend`) and re-armed on wake-up; `RESET_RTC` action; `PAUSE_IN_SLP` left at its default. **Validated** on the watch in `battsim 1` mode (battery behaviour, serial log): repeated light sleep → timed wake-up cycles without a hang (`event lsleep in/out`).

**Update (11:20) — second defect, fixed.** With the RWDT out of the way, timed wake-ups worked but **a button press froze the watch** (last trace: `lsleep in`, never `lsleep out`). Cause: `gpio_wakeup_enable()` switches the pin to a *level* interrupt while the Arduino ISR stays active; on wake-up the AXP202 INT line stays low until the I²C acknowledgement, so the interrupt fires endlessly on core 1 (an interrupt storm) and never returns to the code. **Fix**: `gpio_intr_disable()` on both pins before sleeping, edge types + `gpio_intr_enable()` afterwards. Validated by the user (watch in `battsim 1`), then on a real battery: repeated button wake-ups without a hang. Wrist-raise and double-tap wake-ups use the same path; the user reports they work, if somewhat erratically (BMA423 thresholds to tune).
