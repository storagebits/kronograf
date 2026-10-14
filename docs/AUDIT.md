# Audit — previous T-Watch 2020 work

Date: 2026-10-12. Sources examined (read-only):

| Location | Nature |
|---|---|
| `~/projects/My-TTGO-Watch` | Clone of the upstream `sharandac/My-TTGO-Watch` (the brief said `~/projets/…`, a path that did not exist) |
| Personal PlatformIO project (2021) | PlatformIO project from March 2021: **bttfWatch** ("Back to the Future" watch faces) |
| `TTGO_TWatch_Library` | Official LilyGO library v1.4.1 (bundles LVGL 7.7.2) |

## 1. My-TTGO-Watch

### Versions
- Platform `espressif32@3.3.0` + the Arduino framework **replaced** by a fork (`sharandac/arduino-esp32-hedge`) for DFS / 80 MHz flash.
- **LVGL 7.11.0** (7.7.2 in the 2020 emulator environment). Two major versions behind (v8/v9 APIs are incompatible).
- Libraries: TTGO_TWatch_Library (fork `dervomsee/lvgl_update`), ESPAsyncWebServer, AsyncTCP, ArduinoJson 6, ESP32SSDP, IRremoteESP8266 ≤2.8.4, PubSubClient, ESP8266Audio 1.9.5, ESP8266SAM, FTP server, ESP32-targz, TinyGPSPlus, NimBLE-Arduino ^1.3.8.
- Partitions: `default_16MB.csv`.

### Architecture
- `src/hardware/*`: one module per peripheral (pmu, display, framebuffer, touch, motion, rtcctl, motor, sound, wifictl, blectl, gpsctl, timesync, powermgm), communicating through **event callbacks** (`callback.cpp`).
- `src/gui/*`: tiled main bar (main / app / note / setup), status bar, quick bar, widgets, splash, screenshot.
- `src/app/*`: ~40 apps loaded through "autocall" (automatic registration).
- SDL2 emulator (`hal/sdl2`, `support/sdl2_build_extra.py`) that builds the same UI: **the idea is good and Kronograf adopts it**, not the code.

### Customisations
**None.** Clean `git status`, HEAD = latest upstream commit (d6ab1c4, 2023-09-11), no local branch, no stash, no commit by the user. Only `.pio/` and `.vscode/` (ignored) show that it was built locally.

### Reusable (as knowledge, not copied code)
- AXP202 init sequence: 300 mA charge limit, 4.2 V target, battery/VBUS ADCs, VBUS/charge IRQs, unused EXTEN/DCDC2/LDO4 switched off on V1.
- BMA423: axis remap, STEP_CNTR + TILT + WAKEUP (double tap) features, INT1 on GPIO39 rising edge.
- Light sleep: GPIO wake-up on low level for AXP202_INT (35) and high level for BMA423_INT1 (39), then `esp_light_sleep_start()`.
- 8-bit LEDC backlight.

### Outdated or to avoid
- **⚠ `setDCDC3Voltage()` in sleep** (`pmu.cpp:594-653`): lowers the ESP32 core voltage. **Forbidden by rule 3 of the brief**: not carried over.
- Switching LDO2 off in sleep (`pmu.cpp:607`): on the **V1, LDO2 powers both the backlight and the ST7789** (comment in the LilyGO library). Turning it off is acceptable (the ESP32 runs from DCDC3) but forces a display re-init on wake-up. Decision: keep LDO2 on and use the ST7789's `SLPIN` + PWM at 0.
- Forked Arduino framework frozen in 2021: unmaintained.
- LVGL 7: obsolete API, no `lv_snapshot`, slower image rotation.
- Heavy dependencies useless for our scope (FTP, SSDP, MQTT, SAM, targz, AsyncWebServer).

### Pitfalls spotted
- The backlight pin **differs between revisions**: V1 = GPIO12, V2 = GPIO25, V3 = GPIO15. GPIO25 is the I²S WS on V1/V3 → a wrong build drives the wrong peripheral.
- GPIO12 (V1) is a strapping pin (MTDI flash voltage): do not pull it high at boot.
- V2: touch is reset through the AXP202's **EXTEN**; LDO3 powers display + touch; LDO4 the GPS.
- V3: LDO4 powers audio, touch reset on GPIO14.
- `while( !ttgo->bma->readInterrupt() );` (`motion.cpp:295`): a busy-wait loop that can block.

## 2. bttfWatch (personal project, 2021)

Code by Markus Wobisch (MIT licence), retrieved and lightly adjusted: configured for **`LILYGO_WATCH_2020_V1`**, 6 BTTF faces ("flux capacitor", destination time displays…), 5 settings screens, a stopwatch.

**What it reveals about the user's tastes:** a fully embraced "Back to the Future" world, 7-segment displays, faces that change on wake-up (random / cycle), automatic night dimming (10 pm – 7 am), optional steps and battery display. CPU at 240 MHz while in use (comment: "the lowest value at which graphics stayed responsive"), 10 MHz in sleep.

**Ideas carried over:** automatic night dimming, selectable screen-on durations (7/12/20 s), discrete brightness levels.

## 3. T-Watch 2020 pinout (LilyGO library 1.4.1)

| Signal | V1 | V2 | V3 |
|---|---|---|---|
| TFT MOSI / SCLK / CS / DC | 19 / 18 / 5 / 27 | same | same |
| Backlight | **12** | **25** | **15** |
| Touch SDA/SCL/INT | 23 / 32 / 38 | same (EXTEN reset) | same, RST 14 |
| Sensors SDA/SCL | 21 / 22 | same | same |
| RTC INT / AXP INT / BMA INT1 | 37 / 35 / 39 | same | same |
| Vibration motor | 4 | — (not listed) | 4 |
| IR | 13 | 2 | 13 |
| I²S BCK/WS/DOUT | 26/25/33 | — | 26/25/33 |
| GPS RX/TX/1PPS/WAKE | — | 36/26/34/33 | — |
| SD CS/MISO/MOSI/SCLK | — | 13/4/15/14 | — |
