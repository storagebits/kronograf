/*
 * Synchro NTP à la demande : tâche de fond basse priorité, Wi-Fi allumé le temps
 * de la synchro puis coupé (jamais en permanence). Sans identifiants Wi-Fi dans
 * secrets.h, la fonctionnalité se désactive et l'UI affiche un indice lisible.
 */
#include "../hal.h"
#include "hal_device.h"
#include "kg_secrets.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_sntp.h>

static volatile time_t s_last_sync;
static const char *volatile s_status = "inactif";
static TaskHandle_t s_task;

static constexpr uint32_t WIFI_TIMEOUT_MS = 15000;
static constexpr uint32_t NTP_TIMEOUT_MS = 10000;

static void sync_task(void *)
{
    s_status = "connexion Wi-Fi";
    WiFi.persistent(false); /* ne pas écrire les identifiants en NVS */
    WiFi.mode(WIFI_STA);
    WiFi.begin(KG_WIFI_SSID, KG_WIFI_PASS);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS) vTaskDelay(pdMS_TO_TICKS(100));

    if (WiFi.status() != WL_CONNECTED) {
        s_status = "Wi-Fi injoignable";
    } else {
        s_status = "synchro NTP";
        configTzTime(KG_TZ, "pool.ntp.org", "time.nrc.ca");
        t0 = millis();
        while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && millis() - t0 < NTP_TIMEOUT_MS)
            vTaskDelay(pdMS_TO_TICKS(100));
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            rtc_from_system();
            s_last_sync = time(nullptr);
            s_status = "synchronisé";
            log_i("NTP : heure réglée en %lu ms", (unsigned long)(millis() - t0));
        } else {
            s_status = "NTP sans réponse";
        }
        sntp_stop();
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    s_task = nullptr;
    vTaskDelete(nullptr);
}

void net_start_time_sync(void)
{
    if (!KG_HAS_WIFI) {
        s_status = "pas de Wi-Fi dans secrets.h";
        return;
    }
    if (s_task) return;
    /* Cœur 0 (celui du Wi-Fi), priorité basse : l'UI sur le cœur 1 reste fluide. */
    xTaskCreatePinnedToCore(sync_task, "kg_ntp", 4096, nullptr, 1, &s_task, 0);
}

const char *net_status(void) { return s_status; }

time_t hal_last_sync(void) { return s_last_sync; }

const char *hal_time_hint(void)
{
    if (hal_time_valid()) return nullptr;
    return KG_HAS_WIFI ? "Synchro de l'heure…" : "Configurer dans secrets.h";
}
