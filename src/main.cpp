/*
 * Kronograf — firmware T-Watch 2020. Point d'entrée de la cible montre.
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
    Serial.println("\nKronograf — démarrage");
    /* Boucle principale surveillée : un blocage > 8 s provoque une panique, donc un
     * coredump en flash avec la pile de chaque tâche (au lieu d'un reset muet). */
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
    Serial.printf("cadran : rendu du fond %lu ms, première image %lu ms\n",
                  (unsigned long)(t1 - t0), (unsigned long)(millis() - t1));
    hal_brightness_scale(100); /* allume au réglage mémorisé, une fois la première image envoyée */
    net_start_time_sync();

    const kg_inventory_t *inv = hal_inventory();
    Serial.printf("Kronograf prêt : V%d pmu=%d rtc=%d accel=%d touch=%d psram=%luKo flash=%luMo\n",
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
    if (!power_screen_on()) { /* écran éteint : aucun rendu LVGL */
        vTaskDelay(pdMS_TO_TICKS(50));
        return;
    }
    guard_crumb(KG_CRUMB_LVGL);
    uint32_t wait = lv_timer_handler();
    guard_crumb(KG_CRUMB_LOOP);
    vTaskDelay(pdMS_TO_TICKS(wait < 5 ? 1 : (wait > 20 ? 20 : wait)));
}
