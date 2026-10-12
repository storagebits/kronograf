/*
 * Garde-fous contre les gels :
 *
 *  - Chien de garde RTC (RWDT, 90 s), en plus de ceux des tâches et des interruptions :
 *    une montre figée éveillée redémarre seule au lieu d'attendre une extinction forcée.
 *    Il est coupé pendant le sommeil léger (voir guard_suspend).
 *  - « Miettes de pain » en mémoire RTC non initialisée : elles survivent à ce redémarrage
 *    et disent à quelle étape la montre s'était arrêtée (affichées par `status`).
 */
#include "hal_device.h"
#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/rtc_wdt.h>
#include <rom/rtc.h>

static constexpr uint32_t MAGIC = 0x4B47524Bu; /* « KGRK » */
static constexpr uint32_t RWDT_MS = 90000;     /* > sommeil léger max (60 s) avec marge pour l'oscillateur RC */

static RTC_NOINIT_ATTR uint32_t r_magic, r_crumb, r_prev, r_uptime_s, r_crash_boots;
static uint32_t s_last_crumb = KG_CRUMB_NONE, s_last_prev = KG_CRUMB_NONE, s_last_uptime;
static bool s_had_hang;

static const char *const NAMES[] = {
    "aucune", "boucle", "lvgl", "console", "énergie", "avant sommeil léger", "après sommeil léger",
    "écran éteint", "écran allumé", "atelier on", "atelier off", "réseau",
};

static void arm(void)
{
    rtc_wdt_protect_off();
    rtc_wdt_disable();
    rtc_wdt_set_length_of_reset_signal(RTC_WDT_SYS_RESET_SIG, RTC_WDT_LENGTH_3_2us);
    /* RESET_RTC (tout le circuit) plutôt que RESET_SYSTEM : une réinitialisation « système »
     * déclenchée hors de l'état éveillé peut laisser la puce bloquée (vu avec `hangtest`). */
    rtc_wdt_set_stage(RTC_WDT_STAGE0, RTC_WDT_STAGE_ACTION_RESET_RTC);
    rtc_wdt_set_time(RTC_WDT_STAGE0, RWDT_MS);
    rtc_wdt_enable();
    /* Le RWDT reste en pause pendant le sommeil (réglage par défaut) : le compter en sommeil
     * a été essayé et rendait la montre irrécupérable sur batterie (B3). */
    rtc_wdt_protect_on();
}

void guard_init(void)
{
    esp_reset_reason_t why = esp_reset_reason();
    /* Le RWDT en mode RESET_RTC est rapporté « POWERON » par esp_reset_reason() : on lit la
     * cause brute de la ROM pour le reconnaître. */
    bool rwdt = rtc_get_reset_reason(0) == RTCWDT_RTC_RESET;
    if (rwdt) why = ESP_RST_WDT;
    if (r_magic == MAGIC && why != ESP_RST_POWERON) {
        s_last_crumb = r_crumb;
        s_last_prev = r_prev;
        s_last_uptime = r_uptime_s;
        s_had_hang = why == ESP_RST_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_INT_WDT || why == ESP_RST_PANIC;
    }
    /* Plantages en série dès le démarrage (< 60 s) : au 3e, mode sans échec. */
    bool early_crash = r_magic == MAGIC && s_had_hang && s_last_uptime < 60;
    r_crash_boots = early_crash ? r_crash_boots + 1 : 0;
    r_magic = MAGIC;
    r_crumb = r_prev = KG_CRUMB_NONE;

    arm();
}

void guard_feed(void)
{
    rtc_wdt_feed();
    if (r_crash_boots && millis() > 60000) r_crash_boots = 0; /* une minute stable : on oublie */
}

/* Mode sans échec : les modules récents ou optionnels (BLE) ne démarrent pas, pour que la
 * montre reste joignable (Wi-Fi atelier, OTA) même si l'un d'eux plante au démarrage. */
bool guard_safe_mode(void) { return r_crash_boots >= 2; }

/* Coupe le RWDT autour de esp_light_sleep_start() : on laisse le sommeil léger dans
 * l'état exact que l'IDF attend (il gère lui-même le RTC WDT à la sortie de sommeil). */
void guard_suspend(bool off)
{
    if (off) {
        rtc_wdt_protect_off();
        rtc_wdt_disable(); /* remet aussi les étapes à OFF : arm() les reconfigure */
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
    if (!s_had_hang) return "aucun gel précédent";
    snprintf(buf, n, "gel précédent à %lus : étape « %s » (avant : « %s »)", (unsigned long)s_last_uptime,
             name(s_last_crumb), name(s_last_prev));
    return buf;
}
