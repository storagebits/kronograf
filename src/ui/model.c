#include "model.h"
#include "../hal/hal.h"
#include "kg_tick.h"
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------- stopwatch */

static uint32_t s_chrono_base;   /* accumulated time before the last start */
static uint32_t s_chrono_t0;     /* tick of the last start */
static bool s_chrono_running;
static uint32_t s_laps[KG_MAX_LAPS];
static int s_nlaps;

uint32_t model_chrono_elapsed_ms(void)
{
    return s_chrono_base + (s_chrono_running ? kg_tick_ms() - s_chrono_t0 : 0);
}

bool model_chrono_running(void) { return s_chrono_running; }

void model_chrono_start_stop(void)
{
    if (s_chrono_running) {
        s_chrono_base = model_chrono_elapsed_ms();
        s_chrono_running = false;
    } else {
        s_chrono_t0 = kg_tick_ms();
        s_chrono_running = true;
    }
}

void model_chrono_reset(void)
{
    s_chrono_running = false;
    s_chrono_base = 0;
    s_nlaps = 0;
}

void model_chrono_lap(void)
{
    if (!s_chrono_running) return;
    memmove(&s_laps[1], &s_laps[0], (KG_MAX_LAPS - 1) * sizeof s_laps[0]);
    s_laps[0] = model_chrono_elapsed_ms();
    if (s_nlaps < KG_MAX_LAPS) s_nlaps++;
}

int model_chrono_laps(uint32_t out[KG_MAX_LAPS])
{
    memcpy(out, s_laps, sizeof s_laps);
    return s_nlaps;
}

/* ------------------------------------------------------------------ timer
 * Deadline kept in wall-clock time (time_t) so it survives light sleep. */

static uint32_t s_timer_duration = 5 * 60 * 1000;
static uint32_t s_timer_left;      /* while paused */
static time_t s_timer_end;         /* while running (s) */
static uint32_t s_timer_end_ms;    /* ms part of the deadline */
static kg_timer_state_t s_timer_state = KG_TIMER_IDLE;
static bool s_timer_fired;

static uint64_t now_ms(void)
{
    struct tm t;
    uint32_t ms;
    hal_now(&t, &ms);
    return (uint64_t)time(NULL) * 1000 + ms;
}

void model_timer_set(uint32_t ms)
{
    s_timer_duration = ms;
    s_timer_state = KG_TIMER_IDLE;
}

uint32_t model_timer_duration(void) { return s_timer_duration; }

uint32_t model_timer_remaining_ms(void)
{
    switch (s_timer_state) {
    case KG_TIMER_RUNNING: {
        uint64_t end = (uint64_t)s_timer_end * 1000 + s_timer_end_ms, now = now_ms();
        return end > now ? (uint32_t)(end - now) : 0;
    }
    case KG_TIMER_PAUSED: return s_timer_left;
    case KG_TIMER_DONE: return 0;
    default: return s_timer_duration;
    }
}

void model_timer_start_pause(void)
{
    if (s_timer_state == KG_TIMER_RUNNING) {
        s_timer_left = model_timer_remaining_ms();
        s_timer_state = KG_TIMER_PAUSED;
        return;
    }
    uint32_t left = s_timer_state == KG_TIMER_PAUSED ? s_timer_left : s_timer_duration;
    uint64_t end = now_ms() + left;
    s_timer_end = (time_t)(end / 1000);
    s_timer_end_ms = (uint32_t)(end % 1000);
    s_timer_state = KG_TIMER_RUNNING;
    s_timer_fired = false;
}

void model_timer_reset(void) { s_timer_state = KG_TIMER_IDLE; }

kg_timer_state_t model_timer_state(void)
{
    if (s_timer_state == KG_TIMER_RUNNING && model_timer_remaining_ms() == 0) s_timer_state = KG_TIMER_DONE;
    return s_timer_state;
}

/* ----------------------------------------------------------------- alarms
 * NVS setting "alN" = enabled << 16 | hour << 8 | minute. */

static kg_alarm_t s_alarms[KG_MAX_ALARMS];
static time_t s_snooze_at;        /* pending snooze (0 = none) */
static time_t s_last_fired_min;   /* minute (time_t / 60) already rung */

void model_alarms_load(void)
{
    for (int i = 0; i < KG_MAX_ALARMS; i++) {
        char key[8];
        snprintf(key, sizeof key, "al%d", i);
        int32_t v = hal_setting_get(key, (7 << 8) | (i * 15)); /* defaults: 7:00, 7:15… disabled */
        s_alarms[i] = (kg_alarm_t){.enabled = (v >> 16) & 1, .hour = (v >> 8) & 0xFF, .min = v & 0xFF};
    }
}

kg_alarm_t model_alarm_get(int i) { return s_alarms[i]; }

void model_alarm_set(int i, kg_alarm_t a)
{
    s_alarms[i] = a;
    char key[8];
    snprintf(key, sizeof key, "al%d", i);
    hal_setting_set(key, (a.enabled ? 1 << 16 : 0) | (a.hour << 8) | a.min);
}

/* Next occurrence (time_t) of a daily alarm, strictly after `after`. */
static time_t next_occurrence(kg_alarm_t a, time_t after)
{
    struct tm t;
    localtime_r(&after, &t);
    t.tm_hour = a.hour;
    t.tm_min = a.min;
    t.tm_sec = 0;
    t.tm_isdst = -1;
    time_t when = mktime(&t);
    if (when <= after) {
        t.tm_mday += 1;
        t.tm_isdst = -1;
        when = mktime(&t);
    }
    return when;
}

static time_t next_alarm_time(void)
{
    time_t now = time(NULL), best = 0;
    if (s_snooze_at > now) best = s_snooze_at;
    for (int i = 0; i < KG_MAX_ALARMS; i++) {
        if (!s_alarms[i].enabled) continue;
        time_t w = next_occurrence(s_alarms[i], now - 59); /* the current minute still counts */
        if (!best || w < best) best = w;
    }
    return best;
}

bool model_next_alarm(struct tm *out)
{
    time_t w = next_alarm_time();
    if (!w) return false;
    if (out) localtime_r(&w, out);
    return true;
}

void model_alarm_snooze(void) { s_snooze_at = time(NULL) + 5 * 60; }

void model_alarm_dismiss(void) { s_snooze_at = 0; }

/* ----------------------------------------------------------------- events */

kg_event_t model_poll(void)
{
    if (!s_timer_fired && model_timer_state() == KG_TIMER_DONE) {
        s_timer_fired = true;
        return KG_EVT_TIMER_DONE;
    }
    if (!hal_time_valid()) return KG_EVT_NONE;
    time_t now = time(NULL);
    time_t this_min = now / 60;
    if (this_min == s_last_fired_min) return KG_EVT_NONE;
    if (s_snooze_at && now >= s_snooze_at) {
        s_snooze_at = 0;
        s_last_fired_min = this_min;
        return KG_EVT_ALARM;
    }
    struct tm t;
    localtime_r(&now, &t);
    for (int i = 0; i < KG_MAX_ALARMS; i++) {
        if (s_alarms[i].enabled && s_alarms[i].hour == t.tm_hour && s_alarms[i].min == t.tm_min) {
            s_last_fired_min = this_min;
            return KG_EVT_ALARM;
        }
    }
    return KG_EVT_NONE;
}

time_t model_next_event_time(void)
{
    time_t best = next_alarm_time();
    if (s_timer_state == KG_TIMER_RUNNING) {
        time_t t = s_timer_end + (s_timer_end_ms ? 1 : 0);
        if (!best || t < best) best = t;
    }
    return best;
}
