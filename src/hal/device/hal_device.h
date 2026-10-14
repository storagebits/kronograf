/* Watch-target functions (not visible to the UI). */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../../data/kg_tools.h"
class Print;

/* America/Toronto time zone with North American DST rules. */
#define KG_TZ "EST5EDT,M3.2.0,M11.1.0"

void hal_device_init(void);

void display_init(void);
float display_fps(void);
/* Average render + transfer time per frame (ms) and pixels redrawn per frame. */
void display_frame_stats(float *ms, uint32_t *px);
void display_full_stats(float *avg_ms, uint32_t *max_ms, uint32_t *n);
void display_test_pattern(void);

void touch_init(void);
bool touch_last(int16_t *x, int16_t *y);

/* PCF8563 clock */
void rtc_from_system(void);

/* AXP202: button / USB interrupts (no rail is ever changed) */
#define PMU_IRQ_BUTTON 0x1
#define PMU_IRQ_VBUS   0x2
void pmu_enable_button_irq(void);
void pmu_clear_irq(void);
uint32_t pmu_read_irq(void);
float pmu_discharge_ma(void);
float pmu_vbus_ma(void);
float pmu_charge_ma(void);
int pmu_vbus_mv(void);
float pmu_temperature(void);
float pmu_discharged_mah(void);
void pmu_audio_power(bool on);

/* Vibration motor and sounds */
void feedback_init(void);

/* Battery-life log (on battery) */
void battlog_print(Print &out);

/* Display */
void display_sleep(bool sleep);

/* Power */
void power_init(void);
void power_loop(void);
bool power_screen_on(void);
const char *power_wake_reason(void);
void power_set_brightness(uint8_t level);
uint8_t power_brightness(void);
void power_set_timeout(uint32_t ms);
void power_force(bool on);
void power_counters(uint32_t *wakes, uint32_t *light_sleeps);

/* Network: background NTP sync when Wi-Fi credentials are available */
void net_start_time_sync(void);
const char *net_status(void);
int net_bus_last_error(void);

/* Safeguards (guard.cpp): RTC watchdog and breadcrumbs in RTC memory. */
enum {
    KG_CRUMB_NONE, KG_CRUMB_LOOP, KG_CRUMB_LVGL, KG_CRUMB_CONSOLE, KG_CRUMB_POWER, KG_CRUMB_PRE_SLEEP,
    KG_CRUMB_POST_SLEEP, KG_CRUMB_SCREEN_OFF, KG_CRUMB_SCREEN_ON, KG_CRUMB_WORKSHOP_ON, KG_CRUMB_WORKSHOP_OFF,
    KG_CRUMB_NET,
};
void guard_init(void);
void guard_feed(void);
void guard_suspend(bool off);
bool guard_safe_mode(void);
void guard_crumb(uint32_t id);
const char *guard_report(char *buf, size_t n);

/* Stable USB (3 s debounce): drives the Wi-Fi and light-sleep decisions. */
bool power_usb_stable(void);
void power_battery_sim(bool on);
void power_measure_start(uint32_t minutes);
void power_measure_print(Print &out);

void console_poll(void);
void console_tcp_enable(bool on);

/* Workshop mode: on USB, Wi-Fi + OTA + TCP console (see DECISIONS). Also expires the
 * toolbox's Wi-Fi lease. */
void net_workshop_loop(void);
bool net_hold_active(void);

/* Network toolbox task (nettools.cpp). */
void nettools_cancel(void);              /* lease lapsed: stop the running tool */
void ble_tool_scan(kg_tool_state_t *st, const kg_tools_env_t *env); /* ble.cpp */

/* Speaker stream for the radio (feedback.cpp): 16-bit mono. */
bool audio_stream_open(uint32_t rate);
void audio_stream_write(const int16_t *samples, size_t n);
void audio_stream_close(void);

/* Radio and video state for the console (media.cpp). */
void media_print(Print &out);

/* Wi-Fi kept on by the radio, screen off included (net.cpp). */
void net_keep(bool on);

/* Infrared transmitter (ir.cpp). */
void ir_print(Print &out);

/* BLE / iPhone notifications (ble.cpp) */
void ble_start(void);
bool ble_active(void);     /* controller on: no light sleep (no 32 kHz crystal) */
bool ble_take_wake(void);  /* a notification or pairing wants the screen */
void ble_diag(Print &out); /* ANCS counters for the console */
