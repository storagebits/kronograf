/*
 * Kronograf — firmware T-Watch 2020. Point d'entrée de la cible montre.
 */
#include "hal/device/hal_device.h"
#include "hal/hal.h"
#include "lvgl.h"
#include "ui/ui.h"
#include <Arduino.h>

void setup()
{
    Serial.begin(115200);
    Serial.println("\nKronograf — démarrage");

    hal_device_init();
    lv_init();
    display_init();
    touch_init();

    uint32_t t0 = millis();
    lv_scr_load(ui_face_aviator_create());
    uint32_t t1 = millis();
    lv_refr_now(nullptr);
    Serial.printf("cadran : rendu du fond %lu ms, première image %lu ms\n",
                  (unsigned long)(t1 - t0), (unsigned long)(millis() - t1));
    hal_backlight(160); /* allumé seulement une fois la première image envoyée : pas de flash blanc */

    const kg_inventory_t *inv = hal_inventory();
    Serial.printf("Kronograf prêt : V%d pmu=%d rtc=%d accel=%d touch=%d psram=%luKo flash=%luMo\n",
                  inv->rev, inv->pmu, inv->rtc, inv->accel, inv->touch,
                  (unsigned long)inv->psram_kb, (unsigned long)inv->flash_mb);
}

void loop()
{
    console_poll();
    uint32_t wait = lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(wait < 5 ? 1 : (wait > 20 ? 20 : wait)));
}
