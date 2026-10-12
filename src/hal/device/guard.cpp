/*
 * Garde-fous contre les gels :
 *
 *  - Chien de garde RTC (RWDT, 90 s) : contrairement aux chiens de garde des tâches et des
 *    interruptions, il tourne aussi pendant le sommeil léger. Une montre figée redémarre
 *    seule au lieu d'attendre une extinction forcée au bouton.
 *  - « Miettes de pain » en mémoire RTC non initialisée : elles survivent à ce redémarrage
 *    et disent à quelle étape la montre s'était arrêtée (affichées par `status`).
 */
#include "hal_device.h"
#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/rtc_wdt.h>

static constexpr uint32_t MAGIC = 0x4B47524Bu; /* « KGRK » */
static constexpr uint32_t RWDT_MS = 90000;     /* > sommeil léger max (60 s) avec marge pour l'oscillateur RC */

static RTC_NOINIT_ATTR uint32_t r_magic, r_crumb, r_prev, r_uptime_s;
static uint32_t s_last_crumb = KG_CRUMB_NONE, s_last_prev = KG_CRUMB_NONE, s_last_uptime;
static bool s_had_hang;

static const char *const NAMES[] = {
    "aucune", "boucle", "lvgl", "console", "énergie", "avant sommeil léger", "après sommeil léger",
    "écran éteint", "écran allumé", "atelier on", "atelier off", "réseau",
};

void guard_init(void)
{
    esp_reset_reason_t why = esp_reset_reason();
    if (r_magic == MAGIC && why != ESP_RST_POWERON) {
        s_last_crumb = r_crumb;
        s_last_prev = r_prev;
        s_last_uptime = r_uptime_s;
        s_had_hang = why == ESP_RST_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_INT_WDT || why == ESP_RST_PANIC;
    }
    r_magic = MAGIC;
    r_crumb = r_prev = KG_CRUMB_NONE;

    rtc_wdt_protect_off();
    rtc_wdt_disable();
    rtc_wdt_set_length_of_reset_signal(RTC_WDT_SYS_RESET_SIG, RTC_WDT_LENGTH_3_2us);
    rtc_wdt_set_stage(RTC_WDT_STAGE0, RTC_WDT_STAGE_ACTION_RESET_SYSTEM);
    rtc_wdt_set_time(RTC_WDT_STAGE0, RWDT_MS);
    rtc_wdt_enable();
    /* Par défaut, le RWDT se met en pause pendant le sommeil (bit WDT_PAUSE_IN_SLP), ce qui
     * le rend aveugle à un sommeil léger dont on ne sortirait jamais — vérifié par
     * `hangtest`. On l'efface : le RWDT compte aussi en sommeil (≤ 60 s chez nous). */
    REG_CLR_BIT(RTC_CNTL_WDTCONFIG0_REG, RTC_CNTL_WDT_PAUSE_IN_SLP);
    rtc_wdt_protect_on();
}

void guard_feed(void) { rtc_wdt_feed(); }

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
    if (!s_had_hang) return "aucun gel précédent";
    snprintf(buf, n, "gel précédent à %lus : étape « %s » (avant : « %s »)", (unsigned long)s_last_uptime,
             name(s_last_crumb), name(s_last_prev));
    return buf;
}
