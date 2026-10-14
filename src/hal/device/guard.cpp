/*
 * Safeguards against hangs:
 *
 *  - RTC watchdog (RWDT, 90 s), on top of the task and interrupt watchdogs: a watch frozen
 *    while awake reboots by itself instead of waiting for a forced power-off. It is
 *    disabled during light sleep (see guard_suspend).
 *  - "Breadcrumbs" in uninitialised RTC memory: they survive that reboot and tell which
 *    step the watch was stuck in (shown by `status`).
 */
#include "hal_device.h"
#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/rtc_wdt.h>
#include <rom/rtc.h>

static constexpr uint32_t MAGIC = 0x4B47524Bu; /* "KGRK" */
static constexpr uint32_t RWDT_MS = 90000;     /* > longest light sleep (60 s), with margin for the RC oscillator */

static RTC_NOINIT_ATTR uint32_t r_magic, r_crumb, r_prev, r_uptime_s, r_crash_boots;
static uint32_t s_last_crumb = KG_CRUMB_NONE, s_last_prev = KG_CRUMB_NONE, s_last_uptime;
static bool s_had_hang;

static const char *const NAMES[] = {
    "none", "loop", "lvgl", "console", "power", "before light sleep", "after light sleep",
    "screen off", "screen on", "workshop on", "workshop off", "network",
};

static void arm(void)
{
    rtc_wdt_protect_off();
    rtc_wdt_disable();
    rtc_wdt_set_length_of_reset_signal(RTC_WDT_SYS_RESET_SIG, RTC_WDT_LENGTH_3_2us);
    /* RESET_RTC (the whole chip) rather than RESET_SYSTEM: a "system" reset triggered outside
     * the awake state can leave the chip stuck (seen with `hangtest`). */
    rtc_wdt_set_stage(RTC_WDT_STAGE0, RTC_WDT_STAGE_ACTION_RESET_RTC);
    rtc_wdt_set_time(RTC_WDT_STAGE0, RWDT_MS);
    rtc_wdt_enable();
    /* The RWDT stays paused during sleep (the default): counting during sleep was tried and
     * left the watch unrecoverable on battery (B3). */
    rtc_wdt_protect_on();
}

void guard_init(void)
{
    esp_reset_reason_t why = esp_reset_reason();
    /* esp_reset_reason() reports an RWDT RESET_RTC as "POWERON": read the raw ROM cause to
     * recognise it. */
    bool rwdt = rtc_get_reset_reason(0) == RTCWDT_RTC_RESET;
    if (rwdt) why = ESP_RST_WDT;
    if (r_magic == MAGIC && why != ESP_RST_POWERON) {
        s_last_crumb = r_crumb;
        s_last_prev = r_prev;
        s_last_uptime = r_uptime_s;
        s_had_hang = why == ESP_RST_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_INT_WDT || why == ESP_RST_PANIC;
    }
    /* Repeated crashes right after boot (< 60 s): safe mode on the third one. */
    bool early_crash = r_magic == MAGIC && s_had_hang && s_last_uptime < 60;
    r_crash_boots = early_crash ? r_crash_boots + 1 : 0;
    r_magic = MAGIC;
    r_crumb = r_prev = KG_CRUMB_NONE;

    arm();
}

void guard_feed(void)
{
    rtc_wdt_feed();
    if (r_crash_boots && millis() > 60000) r_crash_boots = 0; /* one stable minute: forget them */
}

/* Safe mode: recent or optional modules (BLE) are not started, so the watch stays
 * reachable (workshop Wi-Fi, OTA) even if one of them crashes at boot. */
bool guard_safe_mode(void) { return r_crash_boots >= 2; }

/* Disables the RWDT around esp_light_sleep_start(): light sleep is entered in exactly the
 * state the IDF expects (it manages the RTC WDT itself on wake-up). */
void guard_suspend(bool off)
{
    if (off) {
        rtc_wdt_protect_off();
        rtc_wdt_disable(); /* also resets the stages to OFF: arm() sets them up again */
        rtc_wdt_protect_on();
    } else {
        arm();
    }
}

void guard_crumb(uint32_t id)
{
    if (id == r_crumb) return;
    r_prev = r_crumb;
    r_crumb = id;
    r_uptime_s = millis() / 1000;
}

const char *guard_report(char *buf, size_t n)
{
    auto name = [](uint32_t id) { return id < sizeof NAMES / sizeof NAMES[0] ? NAMES[id] : "?"; };
    if (!s_had_hang) return "no previous hang";
    snprintf(buf, n, "previous hang at %lus: step \"%s\" (before: \"%s\")", (unsigned long)s_last_uptime,
             name(s_last_crumb), name(s_last_prev));
    return buf;
}
