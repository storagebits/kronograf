/*
 * Simulated HAL. Values are controlled by environment variables for reproducible
 * screenshots:
 *   KG_TIME="10:09:30"   frozen time (otherwise the Mac's clock)
 *   KG_BATT=72           battery percentage
 *   KG_STEPS=6420        today's steps
 *   KG_REV=1|2|3         simulated board revision
 */
#include "../hal.h"
#include "kg_mem.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static kg_inventory_t s_inv;

static int env_int(const char *name, int def)
{
    const char *v = getenv(name);
    return v ? atoi(v) : def;
}

uint32_t kg_tick_ms(void)
{
    return SDL_GetTicks();
}

const kg_inventory_t *hal_inventory(void)
{
    if (s_inv.rev == KG_REV_UNKNOWN) {
        kg_rev_t rev = (kg_rev_t)env_int("KG_REV", 1);
        s_inv = (kg_inventory_t){
            .rev = rev, .pmu = true, .rtc = true, .accel = true, .touch = true,
            .motor = rev != KG_REV_V2, .speaker = rev != KG_REV_V2, .ir_tx = true,
            .gps = rev == KG_REV_V2, .sdcard = rev == KG_REV_V2, .mic = rev == KG_REV_V3,
            .psram = true, .flash_mb = 16, .psram_kb = 4096,
        };
    }
    return &s_inv;
}

void hal_now(struct tm *out, uint32_t *ms)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t now = tv.tv_sec;
    localtime_r(&now, out);
    if (ms) *ms = (uint32_t)(tv.tv_usec / 1000);

    const char *fixed = getenv("KG_TIME");
    int h, m, s;
    if (fixed && sscanf(fixed, "%d:%d:%d", &h, &m, &s) == 3) {
        /* KG_TIME_RUN=1: the clock starts at KG_TIME and keeps running (recordings). */
        uint32_t run = getenv("KG_TIME_RUN") ? SDL_GetTicks() : 0;
        int t = h * 3600 + m * 60 + s + (int)(run / 1000);
        out->tm_hour = t / 3600 % 24;
        out->tm_min = t / 60 % 60;
        out->tm_sec = t % 60;
        if (ms) *ms = run % 1000;
    }
}

kg_battery_t hal_battery(void)
{
    int pct = env_int("KG_BATT", 72);
    return (kg_battery_t){
        .percent = pct, .millivolts = 3500 + pct * 7, .charging = false, .usb = false,
    };
}

uint32_t hal_steps_today(void)
{
    return (uint32_t)env_int("KG_STEPS", 6420);
}

void hal_backlight(uint8_t level) { (void)level; }

void hal_vibrate(uint16_t ms) { printf("[sim] vibration %u ms\n", ms); }

void hal_vibrate_pattern(const uint16_t *ms, uint8_t n)
{
    printf("[sim] vibration pattern:");
    for (int i = 0; i < n; i++) printf(" %u", ms[i]);
    printf("\n");
}

void hal_tones(const kg_tone_t *t, uint8_t n)
{
    printf("[sim] sounds:");
    for (int i = 0; i < n; i++) printf(" %uHz/%ums", t[i].hz, t[i].ms);
    printf("\n");
}

bool hal_sound_available(void) { return true; }

void hal_schedule_wakeup(time_t when) { (void)when; }

void hal_steps_history(uint32_t out[7])
{
    static const uint32_t demo[7] = {8240, 11320, 5410, 9870, 3020, 12150, 7600};
    for (int i = 0; i < 7; i++) out[i] = demo[i];
}

/* Filled with a pattern: the watch's PSRAM is not initialised, so the simulator must not
 * hide a missing fill behind freshly zeroed memory. */
void *hal_alloc_large(size_t bytes)
{
    void *p = malloc(bytes);
    if (p) memset(p, 0xA5, bytes);
    return p;
}

void *kg_lv_pool_alloc(size_t bytes) { return malloc(bytes); }

bool hal_time_valid(void) { return getenv("KG_NOTIME") == NULL; }

time_t hal_last_sync(void) { return time(NULL) - 12 * 60; }

const char *hal_time_hint(void) { return hal_time_valid() ? NULL : "Set up in secrets.h"; }

/* Settings: in-memory table (the simulator starts from the defaults). */
static struct { char key[16]; int32_t value; } s_settings[16];

int32_t hal_setting_get(const char *key, int32_t def)
{
    for (int i = 0; i < 16; i++)
        if (!strcmp(s_settings[i].key, key)) return s_settings[i].value;
    return def;
}

void hal_setting_set(const char *key, int32_t value)
{
    for (int i = 0; i < 16; i++) {
        if (!s_settings[i].key[0] || !strcmp(s_settings[i].key, key)) {
            snprintf(s_settings[i].key, sizeof s_settings[i].key, "%s", key);
            s_settings[i].value = value;
            return;
        }
    }
}

void hal_brightness_scale(uint8_t pct) { printf("[sim] brightness %u %%\n", pct); }

static uint8_t s_bl = 160;
static uint16_t s_timeout = 10;
static char s_ssid[33] = "Home";
uint8_t hal_brightness_get(void) { return s_bl; }
void hal_brightness_set(uint8_t level) { s_bl = level; }
uint16_t hal_screen_timeout_get(void) { return s_timeout; }
void hal_screen_timeout_set(uint16_t seconds) { s_timeout = seconds; }

bool hal_wifi_scan_get(int i, char *ssid, size_t n, int *rssi)
{
    kg_ap_t ap;
    if (!hal_wifi_scan_ap(i, &ap)) return false;
    snprintf(ssid, n, "%s", ap.ssid);
    *rssi = ap.rssi;
    return true;
}
void hal_wifi_set_credentials(const char *ssid, const char *pass)
{
    (void)pass;
    snprintf(s_ssid, sizeof s_ssid, "%s", ssid);
}
const char *hal_wifi_ssid(void) { return s_ssid; }
const char *hal_net_status(void) { return "Synced"; }

/* Weather: a real recorded Open-Meteo response (tests/data), hours shifted to now.
 *   KG_WX=0 no reading; KG_WX_AGE=minutes; KG_WX_CODE / KG_WX_TEMP for screenshots. */
bool hal_weather(kg_weather_t *out)
{
    memset(out, 0, sizeof *out);
    if (env_int("KG_WX", 1) == 0) return false;
    static char json[4096];
    static size_t len;
    if (!len) {
        FILE *f = fopen(KG_ROOT "/tests/data/openmeteo-montreal.json", "rb");
        if (!f) return false;
        len = fread(json, 1, sizeof json - 1, f);
        fclose(f);
    }
    if (!kg_weather_parse(json, len, out)) return false;
    time_t now = time(NULL);
    out->fetched = now - env_int("KG_WX_AGE", 7) * 60;
    for (int i = 0; i < out->n_hours; i++) out->hours[i].t = now - now % 3600 + i * 3600;
    if (getenv("KG_WX_CODE")) out->code = (uint8_t)env_int("KG_WX_CODE", 3);
    if (getenv("KG_WX_TEMP")) out->temp = (float)env_int("KG_WX_TEMP", 15);
    return true;
}

void hal_weather_refresh(uint32_t max_age_s) { (void)max_age_s; }

const char *hal_weather_status(void) { return env_int("KG_WX", 1) ? NULL : "Wi-Fi not configured"; }

bool hal_weather_busy(void) { return false; }

/* Buses: KG_BUS=0 without an STM key, KG_BUS_STOPS="" with no stop selected. */
static char s_stops[40] = "52345";
static char s_key[96] = "demo";
bool hal_bus_available(void) { return env_int("KG_BUS", 1) != 0 && s_key[0]; }
void hal_bus_set_key(const char *key) { snprintf(s_key, sizeof s_key, "%s", key); }
void hal_bus_key_hint(char *out, size_t n)
{
    size_t len = strlen(s_key);
    snprintf(out, n, "%s%s", len ? "…" : "", len > 4 ? s_key + len - 4 : s_key);
}
const char *hal_bus_stops(void)
{
    const char *e = getenv("KG_BUS_STOPS");
    return e ? e : s_stops;
}
void hal_bus_set_stops(const char *stops) { snprintf(s_stops, sizeof s_stops, "%s", stops); }

bool hal_bus(kg_bus_t *out)
{
    memset(out, 0, sizeof *out);
    if (!hal_bus_available() || !hal_bus_stops()[0]) return false;
    static const struct { const char *route; int min; } demo[] = {{"24", 3}, {"55", 6}, {"24", 11}, {"80", 14}, {"55", 21}};
    time_t now = time(NULL);
    for (int i = 0; i < 5; i++) {
        kg_bus_dep_t *d = &out->deps[out->n++];
        snprintf(d->route, sizeof d->route, "%s", demo[i].route);
        snprintf(d->stop, sizeof d->stop, "%s", "52345");
        d->t = now + demo[i].min * 60 + 20;
    }
    out->valid = true;
    out->fetched = now - 40;
    return true;
}

void hal_bus_refresh(uint32_t max_age_s) { (void)max_age_s; }

const char *hal_bus_status(void)
{
    if (!hal_bus_available()) return "No STM API key (Settings)";
    return hal_bus_stops()[0] ? NULL : "No stop selected";
}

/* BLE / notifications: KG_BLE=off|waiting|pairing|ready (default ready), KG_NOTIF=number of
 * fake notifications (default 3), KG_NOTIF_CAT for the first one. */
static uint32_t s_dismissed; /* bit mask of cleared notifications */
static bool s_ble_on = true;

kg_ble_state_t hal_ble_state(void)
{
    const char *e = getenv("KG_BLE");
    if (!s_ble_on || (e && !strcmp(e, "off"))) return KG_BLE_OFF;
    if (e && !strcmp(e, "waiting")) return KG_BLE_WAITING;
    if (e && !strcmp(e, "pairing")) return KG_BLE_PAIRING;
    return KG_BLE_READY;
}
uint32_t hal_ble_passkey(void) { return 482915; }
bool hal_ble_enabled(void) { return s_ble_on && hal_ble_state() != KG_BLE_OFF; }
void hal_ble_enable(bool on) { s_ble_on = on; }
void hal_ble_forget(void) {}

static const kg_notif_t DEMO_NOTIFS[] = {
    {101, KG_CAT_SOCIAL, "com.apple.MobileSMS", "Doc Brown", "Marty, the flux capacitor is fluxing! Meet me at the clock tower at 10:04 sharp.", "20261012T171205"},
    {102, KG_CAT_EMAIL, "com.apple.mobilemail", "STM — Service info", "Route 24: detour via Sherbrooke Street until further notice.", "20261012T164010"},
    {103, KG_CAT_SCHEDULE, "com.apple.mobilecal", "Dentist", "Tomorrow at 9:30 · 1250 Sainte-Catherine St.", "20261012T150000"},
    {104, KG_CAT_CALL, "com.apple.mobilephone", "Mom", "Incoming call", "20261012T171500"},
};

int hal_notif_count(void)
{
    int n = env_int("KG_NOTIF", 3), k = 0;
    for (int i = 0; i < n && i < 4; i++)
        if (!(s_dismissed & 1u << i)) k++;
    return hal_ble_state() == KG_BLE_OFF ? 0 : k;
}

/* ANCS date "YYYYMMDDTHHMMSS" for today at `minutes` past midnight. */
static void notif_date(kg_notif_t *n, const struct tm *t, int minutes)
{
    char buf[48];
    if (minutes < 0) minutes = 0;
    snprintf(buf, sizeof buf, "%04d%02d%02dT%02d%02d00", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, minutes / 60,
             minutes % 60);
    snprintf(n->date, sizeof n->date, "%.15s", buf);
}

bool hal_notif_get(int i, kg_notif_t *out)
{
    int n = env_int("KG_NOTIF", 3);
    for (int k = 0; k < n && k < 4; k++) {
        if (s_dismissed & 1u << k) continue;
        if (i-- == 0) {
            *out = DEMO_NOTIFS[k];
            /* Received today, a few minutes before the (simulated) current time. */
            static const int MIN_AGO[] = {1, 25, 70, 0};
            struct tm t;
            hal_now(&t, NULL);
            notif_date(out, &t, t.tm_hour * 60 + t.tm_min - MIN_AGO[k]);
            if (k == 0 && env_int("KG_NOTIF_CAT", -1) == KG_CAT_CALL) { /* the incoming call comes first */
                *out = DEMO_NOTIFS[3];
                notif_date(out, &t, t.tm_hour * 60 + t.tm_min);
            } else if (k == 0 && getenv("KG_NOTIF_CAT")) {
                out->category = (uint8_t)env_int("KG_NOTIF_CAT", 4);
            }
            return true;
        }
    }
    return false;
}

/* A "new" notification arrives shortly after start-up (or at KG_NOTIF_AT ms) when
 * KG_NOTIF_ALERT=1. */
uint32_t hal_notif_seq(void)
{
    return SDL_GetTicks() > (uint32_t)env_int("KG_NOTIF_AT", 150) ? (uint32_t)env_int("KG_NOTIF_ALERT", 0) : 0;
}

void hal_notif_dismiss(uint32_t uid)
{
    for (int k = 0; k < 4; k++)
        if (DEMO_NOTIFS[k].uid == uid) s_dismissed |= 1u << k;
}
