/*
 * HAL simulée. Valeurs pilotables par variables d'environnement pour des
 * captures reproductibles :
 *   KG_TIME="10:09:30"   heure figée (sinon horloge du Mac)
 *   KG_BATT=72           pourcentage batterie
 *   KG_STEPS=6420        pas du jour
 *   KG_REV=1|2|3         révision simulée
 */
#include "../hal.h"
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
        out->tm_hour = h;
        out->tm_min = m;
        out->tm_sec = s;
        if (ms) *ms = 0;
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

void hal_vibrate(uint16_t ms) { printf("[sim] vibreur %u ms\n", ms); }

void hal_vibrate_pattern(const uint16_t *ms, uint8_t n)
{
    printf("[sim] vibreur motif :");
    for (int i = 0; i < n; i++) printf(" %u", ms[i]);
    printf("\n");
}

void hal_tones(const kg_tone_t *t, uint8_t n)
{
    printf("[sim] sons :");
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

/* Remplie de motif : la PSRAM de la montre n'est pas initialisée, le simulateur ne doit pas
 * masquer un oubli de remplissage par une mémoire fraîchement mise à zéro. */
void *hal_alloc_large(size_t bytes)
{
    void *p = malloc(bytes);
    if (p) memset(p, 0xA5, bytes);
    return p;
}

bool hal_time_valid(void) { return getenv("KG_NOTIME") == NULL; }

time_t hal_last_sync(void) { return time(NULL) - 12 * 60; }

const char *hal_time_hint(void) { return hal_time_valid() ? NULL : "Configurer dans secrets.h"; }

/* Réglages : table en mémoire (le simulateur repart des valeurs par défaut). */
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

void hal_brightness_scale(uint8_t pct) { printf("[sim] luminosité %u %%\n", pct); }

static uint8_t s_bl = 160;
static uint16_t s_timeout = 10;
static char s_ssid[33] = "Maison";
uint8_t hal_brightness_get(void) { return s_bl; }
void hal_brightness_set(uint8_t level) { s_bl = level; }
uint16_t hal_screen_timeout_get(void) { return s_timeout; }
void hal_screen_timeout_set(uint16_t seconds) { s_timeout = seconds; }

static int s_scan = -2;
void hal_wifi_scan_start(void) { s_scan = 4; }
int hal_wifi_scan_count(void) { return s_scan < 0 ? -1 : s_scan; }
bool hal_wifi_scan_get(int i, char *ssid, size_t n, int *rssi)
{
    static const char *const names[] = {"Maison", "Maison-5G", "Café du coin", "BELL123"};
    static const int levels[] = {-48, -55, -71, -83};
    if (i < 0 || i >= 4) return false;
    snprintf(ssid, n, "%s", names[i]);
    *rssi = levels[i];
    return true;
}
void hal_wifi_set_credentials(const char *ssid, const char *pass)
{
    (void)pass;
    snprintf(s_ssid, sizeof s_ssid, "%s", ssid);
}
const char *hal_wifi_ssid(void) { return s_ssid; }
const char *hal_net_status(void) { return "synchronisé"; }
