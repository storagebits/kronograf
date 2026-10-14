# Kronograf

[![CI](https://github.com/storagebits/kronograf/actions/workflows/ci.yml/badge.svg)](https://github.com/storagebits/kronograf/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

**A from-scratch firmware for the LilyGO T-Watch 2020**: an aviation chronograph face, "time circuits" 7-segment displays straight out of *Back to the Future*, weather and real-time bus departures, iPhone notifications, a network engineer's toolbox on the wrist (Wi-Fi analyzer, ping, traceroute, LAN scan, speed test…), web radio, an MJPEG camera viewer, a TV remote, a world clock and a sun & moon almanac. ESP32, LVGL 8.3, and a macOS simulator that builds exactly the same interface.

<p align="center">
  <img src="docs/screens/kronograf-demo.gif" width="360" alt="Demo: aviator face, time circuits with the lightning easter egg, night face, launcher, weather, an iPhone notification, then the network toolbox and its Wi-Fi analyzer">
</p>

![Kronograf: watch faces, launcher, weather, notification, timer, alarms, Wi-Fi analyzer, ping, traceroute, speed test, world clock, sun and moon, TV remote, level](docs/screens/kronograf-overview.png)

## What the watch does

| | |
|---|---|
| **Faces** | *Aviator chronograph* (smooth hands, battery / minutes / steps sub-dials, date window, weather complication, notification badge) · *Time circuits* (three 7-segment rows: next alarm, present time, last sync) · *Night* (time only, dark red, dimmed backlight). Swipe sideways between faces. |
| **Apps** | Stopwatch, timer, alarms, world clock, weather + buses, sun & moon, 7-day step history, level, torch, network toolbox, radio, video, TV remote, system monitor, settings — a launcher of 3 × 3 pages drawn like sub-dials, swiped sideways. |
| **Radio & video** | *Radio*: MP3 web radio on the speaker (Radio Paradise, BBC World Service, KEXP, FIP, Radio Swiss Classic, or any http:// MP3 stream), song titles from the stream, volume, keeps playing with the screen off · *Video*: an MJPEG camera (ESP32-CAM, IP camera, 3D printer) or a JPEG snapshot URL, full screen at ~10–15 fps. Decoded on the watch (minimp3, TJpgDec). |
| **Network toolbox** | *Wi-Fi analyzer* (2.4 GHz channel map, every network with channel, signal and security) · *Link* (SSID, BSSID, signal, address, gateway, DNS, MAC, public IP and provider) · *Ping* with an RTT history and loss · *Traceroute* (ICMP, TTL 1–24) · *DNS lookup* · *Port check* (16 common TCP services) · *LAN scan* (ARP sweep of the /24, private MACs spotted) · *Speed test* (10 s download) · *Bluetooth scan* (BLE devices and their makers) · *Subnet calculator* (offline). Wi-Fi only stays on while the toolbox is open. |
| **World & sky** | Twelve cities from Honolulu to Auckland with summer time, day / night and day shift, plus Zulu time · sunrise, sunset, civil twilight, day length and its daily change, the Moon's phase drawn to scale and the next full moon — computed offline. |
| **Hardware tools** | *TV remote* over the infrared LED (Samsung, LG, Sony, Philips; power, volume, channels, mute, input, and "all TVs") · *Level* from the accelerometer, zeroed on any surface · *Torch* in white or red at full backlight · *System*: battery voltage and current from the AXP202, its temperature, RAM and PSRAM, CPU, frame rate, uptime, reset cause. |
| **Weather** | [Open-Meteo](https://open-meteo.com) (no key needed): conditions, feels-like, the next 6 hours, vector pictograms; age of the reading or "offline". |
| **STM buses** | Real-time departures at up to 3 chosen stops, from the STM GTFS-realtime feed (~1 MB, parsed as a stream on the watch, pinned certificate). Optional API key. |
| **iPhone** | Notifications over ANCS (Bluetooth Low Energy): a card with vibration on arrival, a list, and "Clear" or "Decline" sent back to the iPhone. Pairing protected by a 6-digit passkey. |
| **Time** | On-demand NTP (Wi-Fi on for a few seconds, then off), PCF8563 RTC as a fallback. |
| **Power** | Light sleep with the screen off; wake on button, wrist raise or double tap (BMA423); alarms and timers fire on time while asleep. |

Fonts are OFL-licensed and keep their accented glyphs, so notifications in any Latin-1 language display correctly. Hardware missing from a board revision (GPS, SD card…) is detected at boot and never shows up in the interface.

## Hardware

LilyGO **T-Watch 2020 V1** (ESP32-D0WDQ6, 16 MB flash, 4 MB PSRAM, 240 × 240 ST7789 display, AXP202, BMA423, PCF8563, FT6336, vibration motor, MAX98357A speaker, infrared). V2 and V3 environments exist in `platformio.ini`, but only the V1 has been tested.

## Getting started

```sh
brew install platformio sdl2 cmake
git clone --recurse-submodules https://github.com/storagebits/kronograf.git && cd kronograf
cp include/secrets.example.h include/secrets.h   # optional: Wi-Fi, STM key, OTA password
```

### Simulator (macOS)

```sh
cmake -S sim -B sim/build -DCMAKE_BUILD_TYPE=Release && cmake --build sim/build -j8
sim/build/kronograf_sim                                       # ×3 window, the mouse is your finger
KG_TIME=10:09:30 sim/build/kronograf_sim --shot shot.png      # headless PNG screenshot
KG_NET_REAL=1 KG_APP=net:trace sim/build/kronograf_sim        # the toolbox, for real, from the Mac
KG_NET_REAL=1 KG_APP=radio sim/build/kronograf_sim            # web radio through the Mac's speakers
tools/mjpeg_test_server.py                                    # an MJPEG test stream for the Video app
tools/make_screens.sh                                         # screenshots + the overview sheet
tools/make_demo_gif.sh                                        # re-records the demo GIF (ffmpeg)
```

The simulated HAL is driven by environment variables: `KG_TIME`, `KG_FACE`, `KG_APP` (`chrono`, `weather`, `notifs`, `net:ping`…), `KG_PAGE`, `KG_BATT`, `KG_STEPS`, `KG_REV`, `KG_WX_CODE`, `KG_WX_TEMP`, `KG_BUS_STOPS`, `KG_BLE`, `KG_NOTIF_CAT`, `KG_ALERT`, `KG_NET`, `KG_NET_REAL`, `KG_ACCEL`… The toolbox's demo data only uses documentation addresses.

### Watch

**Prebuilt firmware**: every [release](https://github.com/storagebits/kronograf/releases) ships binaries built by GitHub Actions, with no credentials inside, and flashing instructions. Wi-Fi and the STM key are then set up on the watch.

**From source:**

```sh
# First of all: back up the original flash (16 MB, a few minutes)
tools/backup_flash.sh

pio run -e twatch-v1 -t upload --upload-port /dev/cu.usbserial-XXXX
```

Use `/dev/cu.usbserial-*` rather than `cu.SLAB_USBtoUART`.

**Workshop mode.** With the watch on USB and Wi-Fi configured, it stays connected, accepts OTA updates and opens a TCP console. Unplugged, Wi-Fi is off.

```sh
pio run -e twatch-v1-ota -t upload        # OTA to kronograf.local
tools/kg.py status                        # console: TCP when the watch is reachable, serial otherwise
```

### Secrets and configuration

`include/secrets.h` is ignored by git; `include/secrets.example.h` is the template. An empty value cleanly disables the matching feature.

| Key | Purpose |
|---|---|
| `KG_WIFI_SSID`, `KG_WIFI_PASS` | Default network (it can also be picked on the watch: Settings → Wi-Fi) |
| `KG_STM_API_KEY` | Key from the [STM developer portal](https://www.stm.info/en/about/developers) for bus departures — it can also be entered on the watch (Settings → STM buses) or over the console (`tools/kg.py bus key <key>`) |
| `KG_OTA_PASS` | Password for OTA updates (recommended) |

Bus stops are entered on the watch (Settings → STM buses). The weather location lives in `src/data/kg_weather.h` (downtown Montréal by default).

**Pairing an iPhone.** iOS does not list generic BLE accessories in Settings → Bluetooth. For the first connection, open an app such as *nRF Connect*, tap "Connect" next to "Kronograf", then type the code shown on the watch. From then on the iPhone reconnects by itself.

## Diagnostic console

`tools/kg.py <command>`, over the serial port or over TCP in workshop mode:

```
status        memory, FPS, battery, cause of the last reboot
screenshot f  LVGL capture → PNG              lcdtest       R | G | B test pattern outside LVGL
battlog       battery-life log                power <min>   current measured on USB, simulated battery
weather [refresh]   bus [refresh | stops <codes> | key <key>]   ble [on | off | forget]
net [<tool> [target]]   network toolbox: ping, trace, dns, ports, lan, speed, public, ble
radio [play <n> | stop | vol <n> | url <u>]   video [play | stop | url <u>]
sys           system information             accel         accelerometer in g
ir [<brand> <key>]  sends a TV code          app net:<tool>  opens a network tool
sleep | wake | bl <n> | timeout <s> | face <n> | app <name> | timer <s> | coredump | reboot
```

## Architecture

```
src/ui/          LVGL interface, identical on watch and simulator (faces, apps, theme, fonts)
src/data/        plain C, host-tested: Open-Meteo, GTFS-RT (streamed protobuf), ANCS, network probes and
                 toolbox runners (BSD sockets), time zones, sun and moon, infrared codes, radio and video
src/third_party/ minimp3 (CC0) and TJpgDec (ChaN), vendored — see its README
src/hal/hal.h    the only hardware interface the UI sees
src/hal/device/  T-Watch: display (esp_lcd, DMA), AXP202, BMA423, network, BLE, safeguards
src/hal/sim/     simulated implementation
sim/             SDL2 harness, PNG screenshots, scripted demo recording
tests/           parser tests (cc -I src tests/test_data.c src/data/kg_*.c)
tools/           backup and block flashing, console, fonts, screenshots and overview, demo GIF, screen inlay
```

A few structural choices, detailed in [`DECISIONS.md`](DECISIONS.md):

- **One UI, two targets.** The interface only knows `hal.h`; the simulator compiles the same files. Every screenshot in the repository comes from one or the other.
- **The ESP32 core voltage is never touched.** Unlike some firmwares, Kronograf never adjusts the AXP202's DCDC3 and never cuts the ESP32's power rail.
- **Network jobs.** A background task chains NTP sync, weather and buses over a single connection, then turns Wi-Fi off.
- **One toolbox, two targets.** The network tools are plain C over BSD sockets, so the simulator runs the very same ping, traceroute or speed test from the Mac. On the watch, the toolbox holds a Wi-Fi lease that lapses by itself once the screen is off.
- **Memory.** The LVGL heap and the large TLS buffers live in PSRAM, keeping more than 30 % of internal RAM free with BLE, Wi-Fi and HTTPS all active.
- **Safeguards.** An RTC watchdog with "breadcrumbs" in RTC memory, coredumps in flash, and a safe mode: after three early crashes, BLE is no longer started and OTA stays reachable.

Hardware issues met along the way, and how they were worked around, are logged in [`BLOCKERS.md`](BLOCKERS.md).

## Project status

Development went in stages, each one flashed and verified on the watch.

- ✅ **0–3**: hardware diagnostics, faces, sleep and wake-ups, launcher, stopwatch, timer, alarms, steps, settings, sounds, vibration.
- ✅ **4**: weather and STM buses.
- ✅ **5**: iPhone notifications.
- 🧪 **6**: infrared TV remote — built and simulated, to be checked on the watch.
- 🧪 **8**: network toolbox, world clock, sun & moon, level, torch, system monitor — built, simulated, network tools run for real from the simulator; on-watch check pending.
- 🧪 **9**: web radio and MJPEG video — on the `feature/media` branch, in pre-release.
- 🔜 **7**: polish and a battery-life campaign.

**Known limitations.** This board has no 32 kHz crystal, so light sleep is impossible while BLE is active: battery life with notifications is being measured (D41). The development unit's micro-USB connector is unreliable (B2), hence OTA updates and block flashing.

## Development workflow

`main` only receives tested work. New features go on a branch (`feature/…`) with a pull request: CI builds and tests it, and a **pre-release** can be cut from the branch for testing on a watch — tag it `vX.Y.Z-beta.N` and GitHub Actions publishes it as a pre-release, never as "Latest". Once validated, the pull request is merged into `main` and a plain `vX.Y.Z` tag publishes the release.

```sh
git switch -c feature/my-idea          # work, commit
git push -u origin feature/my-idea     # then open a pull request: CI runs on it
git tag -a v0.7.0-beta.1 -m "…" && git push origin v0.7.0-beta.1   # pre-release for testing
```

## Licence and credits

Kronograf's code is released under the [MIT licence](LICENSE). Dependencies and assets:

- [minimp3](https://github.com/lieff/minimp3) (CC0) and [TJpgDec](http://elm-chan.org/fsw/tjpgd/) (ChaN's free licence), in `src/third_party/`.
- [LVGL](https://lvgl.io) 8.3 (MIT), [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) (Apache 2.0), [XPowersLib](https://github.com/lewisxhe/XPowersLib) and [SensorLib](https://github.com/lewisxhe/SensorLib) (MIT), [stb_image_write](https://github.com/nothings/stb) (public domain / MIT).
- [Barlow Condensed](https://fonts.google.com/specimen/Barlow+Condensed) and [Allerta Stencil](https://fonts.google.com/specimen/Allerta+Stencil) fonts, under the SIL Open Font License (`assets/fonts/OFL-*.txt`).
- [My-TTGO-Watch](https://github.com/sharandac/My-TTGO-Watch) and the LilyGO library served as references for the pinout and init sequences, without reusing their code (see `docs/AUDIT.md`).
- Weather: [Open-Meteo](https://open-meteo.com) (CC BY 4.0). Buses: [STM](https://www.stm.info/en/about/developers) open data.
