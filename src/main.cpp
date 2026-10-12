/*
 * Kronograf — firmware T-Watch 2020. Point d'entrée de la cible montre.
 */
#include "hal/device/hal_device.h"
#include "hal/hal.h"
#include "lvgl.h"
#include "ui/ui.h"
#include <Arduino.h>

static uint32_t s_last_ui_update;

void setup()
{
    Serial.begin(115200);
    Serial.println("\nKronograf — démarrage");

    hal_device_init();
    lv_init();
    display_init();

    lv_scr_load(ui_diag_create());
    lv_refr_now(nullptr);
    hal_backlight(160); /* allumé seulement une fois la première image envoyée : pas de flash blanc */

    const kg_inventory_t *inv = hal_inventory();
    Serial.printf("Kronograf prêt : V%d pmu=%d rtc=%d accel=%d touch=%d psram=%luKo flash=%luMo\n",
                  inv->rev, inv->pmu, inv->rtc, inv->accel, inv->touch,
                  (unsigned long)inv->psram_kb, (unsigned long)inv->flash_mb);
}

void loop()
{
    uint32_t now = millis();
    if (now - s_last_ui_update >= 1000) {
        s_last_ui_update = now;
        ui_diag_update();
    }
    console_poll();
    uint32_t wait = lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(wait < 5 ? 1 : (wait > 20 ? 20 : wait)));
}
