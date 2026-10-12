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
