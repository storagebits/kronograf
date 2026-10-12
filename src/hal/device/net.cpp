/*
 * Synchro NTP à la demande : tâche de fond basse priorité, Wi-Fi allumé le temps
 * de la synchro puis coupé (jamais en permanence). Sans identifiants Wi-Fi dans
 * secrets.h, la fonctionnalité se désactive et l'UI affiche un indice lisible.
 */
#include "../hal.h"
#include "hal_device.h"
#include "kg_secrets.h"
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_task_wdt.h>

#ifndef KG_WORKSHOP
#define KG_WORKSHOP 1
#endif

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
        /* sntp_get_sync_status() remet l'état à zéro quand on le lit : une seule lecture par tour. */
        bool synced = false;
        while (!synced && millis() - t0 < NTP_TIMEOUT_MS) {
            synced = sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED;
            if (!synced) vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (synced) {
            rtc_from_system();
            s_last_sync = time(nullptr);
            s_status = "synchronisé";
            log_i("NTP : heure réglée en %lu ms", (unsigned long)(millis() - t0));
        } else {
            s_status = "NTP sans réponse";
        }
        sntp_stop();
    }
    /* Sur USB, le mode atelier reprend la connexion telle quelle : pas de coupure qui
     * ferait sauter une console TCP fraîchement ouverte. */
    if (!(KG_WORKSHOP && hal_battery().usb && WiFi.status() == WL_CONNECTED)) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
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

/* ------------------------------------------------------------ mode atelier
 * Montre sur USB (donc sur secteur) et Wi-Fi configuré : Wi-Fi maintenu, OTA et
 * console TCP actifs. Débranchée : tout est coupé, l'autonomie n'est pas touchée.
 * Désactivable à la compilation (-D KG_WORKSHOP=0). */
#ifndef KG_OTA_PASS
#define KG_OTA_PASS ""
#endif

static bool s_workshop, s_ota_ready;

static void workshop_start(void)
{
    s_workshop = true;
    s_ota_ready = false;
    if (WiFi.status() == WL_CONNECTED) { /* connexion héritée de la synchro NTP */
        s_status = "atelier : connexion";
        return;
    }
    WiFi.persistent(false);
    WiFi.setHostname("kronograf");
    WiFi.mode(WIFI_STA);
    WiFi.begin(KG_WIFI_SSID, KG_WIFI_PASS);
    s_status = "atelier : connexion";
}

static void workshop_stop(void)
{
    s_workshop = false;
    if (s_ota_ready) ArduinoOTA.end();
    s_ota_ready = false;
    console_tcp_enable(false);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    s_status = "inactif";
}

void net_workshop_loop(void)
{
    if (!KG_WORKSHOP || !KG_HAS_WIFI || s_task) return; /* la synchro NTP a la priorité */
    bool usb = hal_battery().usb;
    if (usb && !s_workshop) workshop_start();
    else if (!usb && s_workshop) workshop_stop();
    if (!s_workshop) return;

    if (!s_ota_ready && WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.setHostname("kronograf");
        if (sizeof(KG_OTA_PASS) > 1) ArduinoOTA.setPassword(KG_OTA_PASS);
        ArduinoOTA.onStart([] { Serial.println("event ota start"); });
        ArduinoOTA.onEnd([] { Serial.println("event ota end"); });
        /* Le transfert OTA tourne dans loop() : nourrir le chien de garde de la boucle. */
        ArduinoOTA.onProgress([](unsigned int, unsigned int) { esp_task_wdt_reset(); });
        ArduinoOTA.onError([](ota_error_t e) { Serial.printf("event ota error %d\n", e); });
        ArduinoOTA.begin();
        console_tcp_enable(true);
        s_ota_ready = true;
        s_status = "atelier : OTA + console TCP";
        Serial.printf("atelier : kronograf.local / %s, console TCP 2323\n", WiFi.localIP().toString().c_str());
    }
    if (s_ota_ready) ArduinoOTA.handle();
}
