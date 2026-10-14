/*
 * Kronograf — T-Watch 2020 firmware. Entry point of the watch target.
 */
#include "hal/device/hal_device.h"
#include "hal/hal.h"
#include "lvgl.h"
#include "ui/ui.h"
#include "ui/faces.h"
#include "ui/nav.h"
#include <Arduino.h>
#include <esp_task_wdt.h>

void setup()
{
    Serial.begin(115200);
    Serial.println("\nKronograf — booting");
    /* Watched main loop: a stall > 8 s triggers a panic, hence a coredump in flash with every
     * task's stack (instead of a silent reset). */
    esp_task_wdt_init(8, true);
    enableLoopWDT();
    guard_init();

    hal_device_init();
    lv_init();
    display_init();
    touch_init();
    power_init();

    uint32_t t0 = millis();
    ui_faces_start();
    ui_nav_init();
    uint32_t t1 = millis();
    lv_refr_now(nullptr);
    Serial.printf("face: background rendered in %lu ms, first frame %lu ms\n",
                  (unsigned long)(t1 - t0), (unsigned long)(millis() - t1));
    hal_brightness_scale(100); /* light up at the saved level once the first frame is out */
    net_start_time_sync();
    if (guard_safe_mode()) Serial.println("safe mode: repeated crashes at boot, BLE not started");
    else ble_start();

    const kg_inventory_t *inv = hal_inventory();
    Serial.printf("Kronograf ready: V%d pmu=%d rtc=%d accel=%d touch=%d psram=%luKB flash=%luMB\n",
                  inv->rev, inv->pmu, inv->rtc, inv->accel, inv->touch,
                  (unsigned long)inv->psram_kb, (unsigned long)inv->flash_mb);
}

void loop()
{
    guard_feed();
    guard_crumb(KG_CRUMB_NET);
    net_workshop_loop();
    guard_crumb(KG_CRUMB_CONSOLE);
    console_poll();
    guard_crumb(KG_CRUMB_POWER);
    power_loop();
    guard_crumb(KG_CRUMB_LOOP);
    if (!power_screen_on()) { /* screen off: no LVGL rendering */
        vTaskDelay(pdMS_TO_TICKS(50));
        return;
    }
    guard_crumb(KG_CRUMB_LVGL);
    uint32_t wait = lv_timer_handler();
    guard_crumb(KG_CRUMB_LOOP);
    vTaskDelay(pdMS_TO_TICKS(wait < 5 ? 1 : (wait > 20 ? 20 : wait)));
}
