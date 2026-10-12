/*
 * Réseau à la demande : une tâche de fond basse priorité exécute les « travaux »
 * demandés (synchro NTP, météo, bus), Wi-Fi allumé le temps des requêtes puis coupé
 * (jamais en permanence). Sans identifiants Wi-Fi, tout se désactive et l'UI affiche
 * un indice lisible ; sans clé STM, le bus est simplement absent.
 */
#include "../hal.h"
#include "certs.h"
#include "hal_device.h"
#include "kg_secrets.h"
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_sntp.h>
#include <esp_task_wdt.h>
#include <mbedtls/platform.h>

#ifndef KG_WORKSHOP
#define KG_WORKSHOP 1
#endif

static volatile time_t s_last_sync;

/* Identifiants : ceux saisis sur la montre (NVS « kgnet ») priment sur secrets.h. */
static char s_ssid[33], s_pass[65];
static char s_stops[40];

static void load_credentials(void)
{
    Preferences p;
    p.begin("kgnet", true);
    String ssid = p.getString("ssid", ""), pass = p.getString("pass", ""), stops = p.getString("stops", "");
    p.end();
    if (ssid.length()) {
        snprintf(s_ssid, sizeof s_ssid, "%s", ssid.c_str());
        snprintf(s_pass, sizeof s_pass, "%s", pass.c_str());
    } else {
        snprintf(s_ssid, sizeof s_ssid, "%s", KG_WIFI_SSID);
        snprintf(s_pass, sizeof s_pass, "%s", KG_WIFI_PASS);
    }
    snprintf(s_stops, sizeof s_stops, "%s", stops.c_str());
}

static bool have_wifi(void) { return s_ssid[0] != 0; }
static const char *volatile s_status = "inactif";

/* ------------------------------------------------------------------ travaux */

enum : uint32_t { JOB_NTP = 1, JOB_WX = 2, JOB_BUS = 4 };
static uint32_t s_jobs;
static TaskHandle_t s_task;
static bool s_task_running;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_scanning;

static constexpr uint32_t WIFI_TIMEOUT_MS = 15000;
static constexpr uint32_t NTP_TIMEOUT_MS = 10000;

/* Données partagées avec l'UI (cœur 1) : copiées sous verrou. */
static kg_weather_t s_wx;
static kg_bus_t s_bus;
static const char *volatile s_wx_status = "Pas encore de relevé";
static const char *volatile s_bus_status = "Pas encore de relevé";
static volatile bool s_wx_busy, s_bus_busy;
static volatile int s_bus_err; /* dernier code d'erreur HTTP du bus (diagnostic console) */

static void do_ntp(void)
{
    s_status = "synchro NTP";
    configTzTime(KG_TZ, "pool.ntp.org", "time.nrc.ca");
    uint32_t t0 = millis();
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

static void do_weather(void)
{
    HTTPClient http;
    http.setTimeout(8000);
    http.begin(KG_WX_URL);
    int code = http.GET();
    if (code == 200) {
        String body = http.getString();
        kg_weather_t wx = {};
        if (kg_weather_parse(body.c_str(), body.length(), &wx)) {
            wx.fetched = time(nullptr);
            portENTER_CRITICAL(&s_mux);
            s_wx = wx;
            portEXIT_CRITICAL(&s_mux);
            Preferences p; /* cache : l'app affiche le dernier relevé dès le démarrage */
            p.begin("kgwx", false);
            p.putBytes("wx", &wx, sizeof wx);
            p.end();
            s_wx_status = nullptr;
            Serial.printf("event météo %.1f° code %u\n", wx.temp, wx.code);
        } else {
            s_wx_status = "Réponse météo illisible";
        }
    } else {
        s_wx_status = code < 0 ? "Open-Meteo injoignable" : "Open-Meteo : erreur du service";
        Serial.printf("event météo erreur %d\n", code);
    }
    http.end();
}

/* Le flux STM (~1 Mo, découpé « chunked ») est passé à l'analyseur au fil de l'eau : un
 * Stream dont write() alimente kg_gtfs_feed(). HTTPClient::writeToStream gère le découpage. */
class GtfsSink : public Stream {
  public:
    explicit GtfsSink(kg_gtfs_t *g) : m_g(g) {}
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t *b, size_t n) override
    {
        kg_gtfs_feed(m_g, b, n);
        m_total += n;
        esp_task_wdt_reset();
        return n;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    size_t total() const { return m_total; }

  private:
    kg_gtfs_t *m_g;
    size_t m_total = 0;
};

static constexpr size_t GTFS_BUF = 16384; /* plus grande entité observée : ~3 Ko */

static void do_bus(void)
{
    if (!s_stops[0]) {
        s_bus_status = "Aucun arrêt choisi";
        return;
    }
    uint8_t *buf = (uint8_t *)hal_alloc_large(GTFS_BUF);
    kg_gtfs_t *g = (kg_gtfs_t *)hal_alloc_large(sizeof(kg_gtfs_t));
    if (!buf || !g) {
        s_bus_status = "Mémoire insuffisante";
        free(buf);
        free(g);
        return;
    }
    uint32_t t0 = millis();
    kg_gtfs_init(g, buf, GTFS_BUF, s_stops, time(nullptr));
    WiFiClientSecure tls;
    tls.setCACert(KG_STM_ROOT_CA);
    HTTPClient http;
    http.setTimeout(10000);
    http.begin(tls, KG_BUS_URL);
    http.addHeader("apiKey", KG_STM_API_KEY);
    http.addHeader("Accept-Encoding", "identity");
    uint32_t heap_min = ESP.getMinFreeHeap();
    int code = http.GET();
    if (code == 200) {
        GtfsSink sink(g);
        int n = http.writeToStream(&sink);
        const kg_bus_t *r = kg_gtfs_finish(g);
        if (n > 0 && r->valid) {
            kg_bus_t bus = *r;
            bus.fetched = time(nullptr);
            portENTER_CRITICAL(&s_mux);
            s_bus = bus;
            portEXIT_CRITICAL(&s_mux);
            s_bus_status = nullptr;
        } else {
            s_bus_status = "Flux STM incomplet";
        }
        Serial.printf("event bus %u o, %lu entités, %u passages, %lu ms, tas min %lu\n", (unsigned)sink.total(),
                      (unsigned long)g->entities, r->n, (unsigned long)(millis() - t0),
                      (unsigned long)ESP.getMinFreeHeap());
    } else {
        s_bus_status = code == 401 || code == 403 ? "Clé STM refusée"
                       : code < 0                 ? "STM injoignable"
                                                  : "STM : erreur du service";
        Serial.printf("event bus erreur %d (%s)\n", code, HTTPClient::errorToString(code).c_str());
        s_bus_err = code;
    }
    (void)heap_min;
    http.end();
    free(buf);
    free(g);
}

static bool wifi_up(void)
{
    if (WiFi.status() == WL_CONNECTED) return true; /* connexion du mode atelier */
    s_status = "connexion Wi-Fi";
    WiFi.persistent(false); /* ne pas écrire les identifiants en NVS */
    WiFi.setHostname("kronograf");
    WiFi.mode(WIFI_STA);
    WiFi.begin(s_ssid, s_pass);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS) vTaskDelay(pdMS_TO_TICKS(100));
    return WiFi.status() == WL_CONNECTED;
}

static void net_task(void *)
{
    bool tried = false, up = false;
    for (;;) {
        portENTER_CRITICAL(&s_mux);
        uint32_t jobs = s_jobs;
        s_jobs = 0;
        if (!jobs) s_task_running = false;
        portEXIT_CRITICAL(&s_mux);
        if (!jobs) break;

        if (!tried) {
            up = wifi_up();
            tried = true;
            if (!up) s_status = "Wi-Fi injoignable";
        }
        if (!up) {
            if (jobs & JOB_WX) s_wx_status = "Wi-Fi injoignable";
            if (jobs & JOB_BUS) s_bus_status = "Wi-Fi injoignable";
        } else {
            if (jobs & JOB_NTP) do_ntp();
            if (jobs & JOB_WX) do_weather();
            if (jobs & JOB_BUS) do_bus();
            if (s_status != nullptr && !strncmp(s_status, "connexion", 9)) s_status = "connecté";
        }
        if (jobs & JOB_WX) s_wx_busy = false;
        if (jobs & JOB_BUS) s_bus_busy = false;
    }
    /* Sur USB, le mode atelier reprend la connexion telle quelle : pas de coupure qui
     * ferait sauter une console TCP fraîchement ouverte. */
    if (!(KG_WORKSHOP && power_usb_stable() && WiFi.status() == WL_CONNECTED)) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
    s_task = nullptr;
    vTaskDelete(nullptr);
}

static void net_request(uint32_t jobs)
{
    if (!have_wifi() || s_scanning) return;
    portENTER_CRITICAL(&s_mux);
    s_jobs |= jobs;
    bool start = !s_task_running;
    if (start) s_task_running = true;
    portEXIT_CRITICAL(&s_mux);
    if (!start) return;
    while (s_task) vTaskDelay(1); /* la tâche précédente finit de couper le Wi-Fi */
    /* Cœur 0 (celui du Wi-Fi), priorité basse : l'UI sur le cœur 1 reste fluide. La pile
     * doit tenir la poignée de main TLS (mbedTLS). */
    xTaskCreatePinnedToCore(net_task, "kg_net", 8192, nullptr, 1, &s_task, 0);
}

/* Tampons TLS (≈ 2 × 16 Ko par connexion) en PSRAM : en mémoire interne, Wi-Fi + BLE +
 * TLS ne laissaient que 6 Ko libres pendant le téléchargement STM. Le chiffrement matériel
 * de l'ESP32 n'utilise pas de DMA : la PSRAM convient. Repli en interne si la PSRAM manque. */
static void *tls_calloc(size_t n, size_t size)
{
    if (n * size >= 4096) { /* gros tampons d'enregistrement seulement */
        void *p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p) return p;
    }
    return heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void net_start_time_sync(void)
{
    mbedtls_platform_set_calloc_free(tls_calloc, free);
    load_credentials();
    if (!have_wifi()) {
        s_status = "pas de Wi-Fi configuré";
        s_wx_status = "Wi-Fi non configuré";
        s_bus_status = "Wi-Fi non configuré";
        return;
    }
    Preferences p;
    p.begin("kgwx", true);
    kg_weather_t wx;
    if (p.getBytes("wx", &wx, sizeof wx) == sizeof wx && wx.valid) {
        s_wx = wx;
        s_wx_status = nullptr;
    }
    p.end();
    s_wx_busy = true;
    net_request(JOB_NTP | JOB_WX); /* une seule connexion au démarrage pour l'heure et la météo */
}

const char *net_status(void) { return s_status; }

time_t hal_last_sync(void) { return s_last_sync; }

const char *hal_time_hint(void)
{
    if (hal_time_valid()) return nullptr;
    return have_wifi() ? "Synchro de l'heure…" : "Configurer le Wi-Fi (Réglages)";
}

/* -------------------------------------------------------------- météo, bus */

bool hal_weather(kg_weather_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_wx;
    portEXIT_CRITICAL(&s_mux);
    return out->valid;
}

void hal_weather_refresh(uint32_t max_age_s)
{
    if (s_wx_busy) return;
    time_t now = time(nullptr);
    if (s_wx.valid && now - s_wx.fetched < (time_t)max_age_s) return;
    s_wx_busy = true;
    net_request(JOB_WX);
    if (!s_task_running) s_wx_busy = false; /* refusé : pas de Wi-Fi ou scan en cours */
}

const char *hal_weather_status(void) { return s_wx_status; }

bool hal_weather_busy(void) { return s_wx_busy; }

bool hal_bus_available(void) { return sizeof(KG_STM_API_KEY) > 1; }

const char *hal_bus_stops(void) { return s_stops; }

void hal_bus_set_stops(const char *stops)
{
    snprintf(s_stops, sizeof s_stops, "%s", stops);
    Preferences p;
    p.begin("kgnet", false);
    p.putString("stops", s_stops);
    p.end();
    portENTER_CRITICAL(&s_mux);
    s_bus.valid = false;
    s_bus.n = 0;
    portEXIT_CRITICAL(&s_mux);
}

bool hal_bus(kg_bus_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_bus;
    portEXIT_CRITICAL(&s_mux);
    return out->valid;
}

void hal_bus_refresh(uint32_t max_age_s)
{
    if (!hal_bus_available() || s_bus_busy) return;
    if (!s_stops[0]) {
        s_bus_status = "Aucun arrêt choisi";
        return;
    }
    if (s_bus.valid && time(nullptr) - s_bus.fetched < (time_t)max_age_s) return;
    s_bus_busy = true;
    net_request(JOB_BUS);
    if (!s_task_running) s_bus_busy = false;
}

int net_bus_last_error(void) { return s_bus_err; }

const char *hal_bus_status(void)
{
    if (!hal_bus_available()) return "Clé STM absente (secrets.h)";
    return s_bus_busy ? "mise à jour…" : s_bus_status;
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
    WiFi.begin(s_ssid, s_pass);
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
    if (!KG_WORKSHOP || !have_wifi() || s_task || s_scanning) return; /* travaux réseau et scan ont la priorité */
    bool usb = power_usb_stable();
    if (usb && !s_workshop) {
        guard_crumb(KG_CRUMB_WORKSHOP_ON);
        workshop_start();
    } else if (!usb && s_workshop) {
        guard_crumb(KG_CRUMB_WORKSHOP_OFF);
        workshop_stop();
    }
    if (!s_workshop) return;

    if (!s_ota_ready && WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.setHostname("kronograf");
        if (sizeof(KG_OTA_PASS) > 1) ArduinoOTA.setPassword(KG_OTA_PASS);
        ArduinoOTA.onStart([] { Serial.println("event ota start"); });
        ArduinoOTA.onEnd([] { Serial.println("event ota end"); });
        /* Le transfert OTA tourne dans loop() : nourrir le chien de garde de la boucle. */
        ArduinoOTA.onProgress([](unsigned int, unsigned int) {
            esp_task_wdt_reset();
            guard_feed(); /* le transfert peut dépasser les 90 s du RWDT */
        });
        ArduinoOTA.onError([](ota_error_t e) { Serial.printf("event ota error %d\n", e); });
        ArduinoOTA.begin();
        console_tcp_enable(true);
        s_ota_ready = true;
        s_status = "atelier : OTA + console TCP";
        Serial.printf("atelier : kronograf.local / %s, console TCP 2323\n", WiFi.localIP().toString().c_str());
    }
    if (s_ota_ready) ArduinoOTA.handle();
}

/* ------------------------------------------------------------- saisie Wi-Fi */

void hal_wifi_scan_start(void)
{
    if (s_task) return;
    s_scanning = true;
    if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
    WiFi.scanDelete();
    WiFi.scanNetworks(true /* asynchrone */, false);
}

int hal_wifi_scan_count(void)
{
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return -1;
    if (s_scanning) {
        s_scanning = false;
        if (!s_workshop) WiFi.mode(WIFI_OFF); /* pas de Wi-Fi laissé allumé après un scan */
    }
    return n < 0 ? 0 : n;
}

bool hal_wifi_scan_get(int i, char *ssid, size_t n, int *rssi)
{
    if (i < 0 || i >= WiFi.scanComplete()) return false;
    snprintf(ssid, n, "%s", WiFi.SSID(i).c_str());
    *rssi = WiFi.RSSI(i);
    return true;
}

void hal_wifi_set_credentials(const char *ssid, const char *pass)
{
    Preferences p;
    p.begin("kgnet", false);
    p.putString("ssid", ssid);
    p.putString("pass", pass);
    p.end();
    if (s_workshop) workshop_stop();
    net_start_time_sync(); /* recharge les identifiants et tente une synchro */
}

const char *hal_wifi_ssid(void) { return s_ssid; }

const char *hal_net_status(void) { return s_status; }
