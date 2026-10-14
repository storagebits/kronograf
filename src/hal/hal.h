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
#include "../data/kg_ir.h"
#include "../data/kg_media.h"
#include "../data/kg_net.h"
#include "../data/kg_tools.h"
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
bool hal_wifi_scan_start(void);                      /* false if Wi-Fi is busy (retry later) */
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

/* ------------------------------------------------------- network toolbox
 * While the toolbox is open the UI holds a Wi-Fi lease, renewed by its timers; it lapses
 * 20 s after the last renewal (screen off, app closed) and Wi-Fi goes back off. One tool
 * runs at a time in the background, the UI polls a snapshot of its state. */
void hal_net_hold(void);         /* takes or renews the lease (connects if needed) */
void hal_net_release(void);

typedef struct {
    bool up;                     /* associated, with an IP address */
    char ssid[33];
    uint8_t bssid[6], mac[6];
    int8_t rssi;
    uint8_t channel;
    uint32_t ip, mask, gw, dns;  /* network byte order */
    char hostname[24];
} kg_link_t;
void hal_net_link(kg_link_t *out);

/* Wi-Fi scan details (after hal_wifi_scan_count() >= 0). */
typedef enum { KG_AUTH_OPEN, KG_AUTH_WEP, KG_AUTH_WPA, KG_AUTH_WPA2, KG_AUTH_WPA3, KG_AUTH_ENTERPRISE } kg_auth_t;
typedef struct {
    char ssid[33];
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    kg_auth_t auth;
} kg_ap_t;
bool hal_wifi_scan_ap(int i, kg_ap_t *out);

/* Starts a tool on `target` (host name or address; ignored by LAN, speed, public IP and BLE
 * scans). Stops the previous one. */
void hal_tool_start(kg_tool_t tool, const char *target);
void hal_tool_stop(void);
void hal_tool_get(kg_tool_state_t *out);

/* ------------------------------------------------------------------ media
 * Radio: MP3 web radio on the speaker; keeps Wi-Fi on while it plays, screen off included.
 * Video: MJPEG camera stream; runs while the app is on screen (the UI holds the Wi-Fi lease).
 * URLs are http:// only (no TLS). */
void hal_radio_play(const char *name, const char *url);
void hal_radio_stop(void);
void hal_radio_status(kg_radio_status_t *out);
void hal_radio_set_volume(uint8_t volume);         /* 0..KG_RADIO_VOL_MAX, persistent */
uint8_t hal_radio_volume(void);
const char *hal_radio_custom_url(void);            /* "" if none */
void hal_radio_set_custom_url(const char *url);    /* persistent */

void hal_video_start(const char *url);
void hal_video_stop(void);
void hal_video_status(kg_video_status_t *out);
/* Latest frame (KG_VIDEO_W × KG_VIDEO_H, LVGL's RGB565), valid until the next call; NULL if none. */
const uint16_t *hal_video_frame(uint32_t *seq);
const char *hal_video_url(void);                   /* "" if none */
void hal_video_set_url(const char *url);           /* persistent */

/* ---------------------------------------------------------------- sensors */

/* Acceleration in g, watch frame: x towards 3 o'clock, y towards 12 o'clock, z out of the
 * screen (lying flat face up: z ≈ +1). False without an accelerometer. */
bool hal_accel(float *x, float *y, float *z);

/* ---------------------------------------------------------------- infrared */

bool hal_ir_available(void);
/* Sends one code, repeated as the protocol requires (see kg_ir.h for the code layout). */
void hal_ir_send(kg_ir_proto_t proto, uint32_t code);

/* ---------------------------------------------------------------- system */

typedef struct {
    uint32_t uptime_s;
    uint32_t heap_free, heap_min, heap_total;  /* internal RAM */
    uint32_t psram_free, psram_total;
    uint16_t cpu_mhz;
    float fps;
    int batt_mv, batt_pct;
    float batt_ma;              /* > 0 charging, < 0 discharging */
    int vbus_mv;
    float pmu_temp;             /* AXP202 die temperature, °C */
    bool usb, charging;
    uint32_t wakes, light_sleeps;
    const char *reset_reason;
    uint32_t app_kb, app_slot_kb;
    const char *version;
} kg_sysinfo_t;
void hal_sysinfo(kg_sysinfo_t *out);

/* Full backlight regardless of the brightness setting (torch). */
void hal_brightness_boost(bool on);

#ifdef __cplusplus
}
#endif
