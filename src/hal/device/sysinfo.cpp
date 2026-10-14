/*
 * System information for the System app and the console: memory, CPU, frame rate,
 * battery and AXP202 measurements, uptime, sleep counters, reset cause, firmware size.
 */
#include "../hal.h"
#include "hal_device.h"
#include "kg_version.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <rom/rtc.h>

static const char *reset_name(void)
{
    if (rtc_get_reset_reason(0) == RTCWDT_RTC_RESET) return "RTC watchdog";
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "software (reboot / OTA)";
    case ESP_RST_PANIC: return "crash (coredump saved)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    default: return "other";
    }
}

void hal_sysinfo(kg_sysinfo_t *s)
{
    memset(s, 0, sizeof *s);
    s->uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
    s->heap_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    s->heap_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    s->heap_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    s->psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    s->psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    s->cpu_mhz = (uint16_t)getCpuFrequencyMhz();
    s->fps = display_fps();
    kg_battery_t b = hal_battery();
    s->batt_mv = b.millivolts;
    s->batt_pct = b.percent;
    s->usb = b.usb;
    s->charging = b.charging;
    s->batt_ma = b.charging ? pmu_charge_ma() : -pmu_discharge_ma();
    s->vbus_mv = pmu_vbus_mv();
    s->pmu_temp = pmu_temperature();
    power_counters(&s->wakes, &s->light_sleeps);
    s->reset_reason = reset_name();
    const esp_partition_t *p = esp_ota_get_running_partition();
    s->app_slot_kb = p ? p->size / 1024 : 0;
    static uint32_t app_kb; /* getSketchSize() reads the whole image back from flash: once */
    if (!app_kb) app_kb = ESP.getSketchSize() / 1024;
    s->app_kb = app_kb;
    s->version = KG_VERSION;
}
