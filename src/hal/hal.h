/*
 * Hardware interface as seen by the UI. Implemented twice:
 *   src/hal/device/  — T-Watch 2020 (ESP32)
 *   src/hal/sim/     — macOS simulator (controllable fake values)
 * The UI never includes anything but this file to talk to the hardware.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "../data/kg_ancs.h"
#include "../data/kg_bus.h"
#include "../data/kg_weather.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { KG_REV_UNKNOWN = 0, KG_REV_V1 = 1, KG_REV_V2 = 2, KG_REV_V3 = 3 } kg_rev_t;

/* Inventory detected at boot (I²C scan / probes). */
typedef struct {
    kg_rev_t rev;
    bool pmu;        /* AXP202 @0x35 */
    bool rtc;        /* PCF8563 @0x51 */
    bool accel;      /* BMA423 @0x19 */
    bool touch;      /* FT6336 @0x38 */
    bool motor;
    bool speaker;    /* MAX98357A I²S */
    bool ir_tx;
    bool gps;        /* V2 */
    bool sdcard;     /* V2 */
    bool mic;        /* V3 */
    bool psram;
    uint32_t flash_mb;
    uint32_t psram_kb;
} kg_inventory_t;

typedef struct {
    int percent;      /* 0..100, -1 if unknown */
    int millivolts;
    bool charging;
    bool usb;
} kg_battery_t;

const kg_inventory_t *hal_inventory(void);

/* Local time (America/Toronto, DST handled by libc through TZ). */
void hal_now(struct tm *out, uint32_t *ms);

/* False until the time has been read from a trusted RTC or synchronised. */
bool hal_time_valid(void);

/* Timestamp of the last successful NTP sync (0 = never). */
time_t hal_last_sync(void);

/* Short user-facing text when the time cannot be set, NULL otherwise. */
const char *hal_time_hint(void);

kg_battery_t hal_battery(void);
uint32_t hal_steps_today(void);

void hal_backlight(uint8_t level); /* 0..255 */

/* Persistent settings (NVS on the watch). */
int32_t hal_setting_get(const char *key, int32_t def);
void hal_setting_set(const char *key, int32_t value);

/* User brightness (8..255, persistent) and screen timeout (s). */
uint8_t hal_brightness_get(void);
void hal_brightness_set(uint8_t level);
uint16_t hal_screen_timeout_get(void);
void hal_screen_timeout_set(uint16_t seconds);

/* Wi-Fi: credentials entered on the watch (NVS, take precedence over secrets.h). */
void hal_wifi_scan_start(void);
int hal_wifi_scan_count(void);                       /* -1: scan in progress */
bool hal_wifi_scan_get(int i, char *ssid, size_t n, int *rssi);
void hal_wifi_set_credentials(const char *ssid, const char *pass); /* triggers a sync */
const char *hal_wifi_ssid(void);                     /* configured network ("" if none) */
const char *hal_net_status(void);                    /* readable status: "Synced"… */

/* Brightness scale requested by the face on screen (100 = user setting). */
void hal_brightness_scale(uint8_t pct);

/* Large buffers (face images): PSRAM on the watch, malloc in the simulator. */
void *hal_alloc_large(size_t bytes);
/* Haptic feedback, non-blocking. Pattern: alternating on/off durations, starting with on. */
void hal_vibrate(uint16_t ms);
void hal_vibrate_pattern(const uint16_t *ms, uint8_t n);

/* Synthesised sounds (speaker if present), non-blocking. hz = 0: silence. */
typedef struct {
    uint16_t hz;
    uint16_t ms;
} kg_tone_t;
void hal_tones(const kg_tone_t *tones, uint8_t n);
bool hal_sound_available(void);

/* Scheduled wake-up: the watch must be awake at `when` (alarm, timer). 0 = none. */
void hal_schedule_wakeup(time_t when);

/* Step history: out[0] = yesterday … out[6] = 7 days ago (0 if unknown). */
void hal_steps_history(uint32_t out[7]);

/* Weather (Open-Meteo, Montréal). Copy of the latest reading (persistent cache); false if
 * none. hal_weather_refresh() starts a background update when the reading is older than
 * `max_age_s` seconds (Wi-Fi on for the request). Status: NULL when all is well. */
bool hal_weather(kg_weather_t *out);
void hal_weather_refresh(uint32_t max_age_s);
const char *hal_weather_status(void); /* last error, NULL if the last request succeeded */
bool hal_weather_busy(void);

/* STM buses (GTFS realtime): only when an API key is available — entered on the watch or
 * over the console (NVS), or from secrets.h. */
bool hal_bus_available(void);
void hal_bus_set_key(const char *key);   /* persistent; "" = back to the secrets.h key, if any */
void hal_bus_key_hint(char *out, size_t n); /* "…ab12" (last 4 characters), "" when there is no key */
const char *hal_bus_stops(void);          /* followed stop codes, "52345,52346" ("": none) */
void hal_bus_set_stops(const char *stops);
bool hal_bus(kg_bus_t *out);
void hal_bus_refresh(uint32_t max_age_s);
const char *hal_bus_status(void);

/* iPhone notifications (BLE, ANCS). The first connection is made from a BLE app such as
 * nRF Connect; the pairing code shows on the watch, then the iPhone reconnects by itself. */
typedef enum { KG_BLE_OFF, KG_BLE_WAITING, KG_BLE_PAIRING, KG_BLE_CONNECTED, KG_BLE_READY } kg_ble_state_t;
kg_ble_state_t hal_ble_state(void);
uint32_t hal_ble_passkey(void);  /* 6-digit code, valid in KG_BLE_PAIRING */
bool hal_ble_enabled(void);
void hal_ble_enable(bool on);    /* persistent */
void hal_ble_forget(void);       /* forgets the paired iPhone */
int hal_notif_count(void);
bool hal_notif_get(int i, kg_notif_t *out); /* 0 = most recent */
uint32_t hal_notif_seq(void);    /* +1 for each new notification to announce */
void hal_notif_dismiss(uint32_t uid); /* removes it from the watch and clears it on the iPhone */

#ifdef __cplusplus
}
#endif
