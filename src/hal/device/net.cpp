/*
 * On-demand networking: a low-priority background task runs the requested "jobs" (NTP
 * sync, weather, buses), with Wi-Fi on only for the requests and then off (never
 * permanently). Without Wi-Fi credentials everything is disabled and the UI shows a readable
 * hint; without an STM key, buses are simply absent.
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

/* Credentials: those entered on the watch (NVS "kgnet") take precedence over secrets.h. */
static char s_ssid[33], s_pass[65];
static char s_stops[40];
static char s_stm_key[96];

static void load_credentials(void)
{
    Preferences p;
    p.begin("kgnet", true);
    String ssid = p.getString("ssid", ""), pass = p.getString("pass", ""), stops = p.getString("stops", "");
    String key = p.getString("stmkey", "");
    p.end();
    snprintf(s_stm_key, sizeof s_stm_key, "%s", key.length() ? key.c_str() : KG_STM_API_KEY);
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
static const char *volatile s_status = "Idle";

/* ------------------------------------------------------------------- jobs */

enum : uint32_t { JOB_NTP = 1, JOB_WX = 2, JOB_BUS = 4, JOB_LINK = 8 /* just connect (toolbox lease) */ };
static uint32_t s_jobs;
static TaskHandle_t s_task;
static bool s_task_running;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_scanning;

/* Toolbox lease (hal_net_hold): Wi-Fi stays on until it lapses; s_kept marks a connection
 * kept up only for the lease, switched off by net_workshop_loop() once it has lapsed. */
static volatile uint32_t s_hold_until;
static bool s_kept;
static constexpr uint32_t HOLD_MS = 20000;

static volatile bool s_keep; /* radio playing */

bool net_hold_active(void) { return s_keep || (s_hold_until && (int32_t)(s_hold_until - millis()) > 0); }

static constexpr uint32_t WIFI_TIMEOUT_MS = 15000;
static constexpr uint32_t NTP_TIMEOUT_MS = 10000;

/* Data shared with the UI (core 1): copied under a lock. */
static kg_weather_t s_wx;
static kg_bus_t s_bus;
static const char *volatile s_wx_status = "No reading yet";
static const char *volatile s_bus_status = "No data yet";
static volatile bool s_wx_busy, s_bus_busy;
static volatile int s_bus_err; /* last HTTP error code for buses (console diagnostics) */

static void do_ntp(void)
{
    s_status = "NTP sync";
    configTzTime(KG_TZ, "pool.ntp.org", "time.nrc.ca");
    uint32_t t0 = millis();
    /* sntp_get_sync_status() resets the state when read: one read per iteration. */
    bool synced = false;
    while (!synced && millis() - t0 < NTP_TIMEOUT_MS) {
        synced = sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED;
        if (!synced) vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (synced) {
        rtc_from_system();
        s_last_sync = time(nullptr);
        s_status = "Synced";
        log_i("NTP: time set in %lu ms", (unsigned long)(millis() - t0));
    } else {
        s_status = "No NTP response";
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
            Preferences p; /* cache: the app shows the last reading right from boot */
            p.begin("kgwx", false);
            p.putBytes("wx", &wx, sizeof wx);
            p.end();
            s_wx_status = nullptr;
            Serial.printf("event weather %.1f° code %u\n", wx.temp, wx.code);
        } else {
            s_wx_status = "Unreadable weather response";
        }
    } else {
        s_wx_status = code < 0 ? "Open-Meteo unreachable" : "Open-Meteo service error";
        Serial.printf("event weather error %d\n", code);
    }
    http.end();
}

/* The STM feed (~1 MB, chunked transfer) is handed to the parser as it arrives: a Stream
 * whose write() feeds kg_gtfs_feed(). HTTPClient::writeToStream handles the chunking. */
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

static constexpr size_t GTFS_BUF = 16384; /* largest entity seen: ~3 KB */

static void do_bus(void)
{
    if (!s_stops[0]) {
        s_bus_status = "No stop selected";
        return;
    }
    uint8_t *buf = (uint8_t *)hal_alloc_large(GTFS_BUF);
    kg_gtfs_t *g = (kg_gtfs_t *)hal_alloc_large(sizeof(kg_gtfs_t));
    if (!buf || !g) {
        s_bus_status = "Out of memory";
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
    http.addHeader("apiKey", s_stm_key);
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
            s_bus_status = "Incomplete STM feed";
        }
        Serial.printf("event bus %u B, %lu entities, %u departures, %lu ms, min heap %lu\n", (unsigned)sink.total(),
                      (unsigned long)g->entities, r->n, (unsigned long)(millis() - t0),
                      (unsigned long)ESP.getMinFreeHeap());
    } else {
        s_bus_status = code == 401 || code == 403 ? "STM key rejected"
                       : code < 0                 ? "STM unreachable"
                                                  : "STM service error";
        Serial.printf("event bus error %d (%s)\n", code, HTTPClient::errorToString(code).c_str());
        s_bus_err = code;
    }
    (void)heap_min;
    http.end();
    free(buf);
    free(g);
}

static bool wifi_up(void)
{
    if (WiFi.status() == WL_CONNECTED) return true; /* workshop-mode connection */
    s_status = "Connecting to Wi-Fi";
    WiFi.persistent(false); /* do not write the credentials to NVS */
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
            if (!up) s_status = "Wi-Fi unreachable";
        }
        if (!up) {
            if (jobs & JOB_WX) s_wx_status = "Wi-Fi unreachable";
            if (jobs & JOB_BUS) s_bus_status = "Wi-Fi unreachable";
        } else {
            if (jobs & JOB_NTP) do_ntp();
            if (jobs & JOB_WX) do_weather();
            if (jobs & JOB_BUS) do_bus();
            if (s_status != nullptr && !strncmp(s_status, "Connecting", 10)) s_status = "Connected";
        }
        if (jobs & JOB_WX) s_wx_busy = false;
        if (jobs & JOB_BUS) s_bus_busy = false;
    }
    /* On USB, workshop mode takes the connection over as is: no disconnect that would drop a
     * freshly opened TCP console. The toolbox lease keeps it too. */
    if (net_hold_active()) {
        s_kept = true;
    } else if (!(KG_WORKSHOP && power_usb_stable() && WiFi.status() == WL_CONNECTED)) {
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
    while (s_task) vTaskDelay(1); /* the previous task is still switching Wi-Fi off */
    /* Core 0 (the Wi-Fi core), low priority: the UI on core 1 stays smooth. The stack must
     * hold the TLS handshake (mbedTLS). */
    xTaskCreatePinnedToCore(net_task, "kg_net", 8192, nullptr, 1, &s_task, 0);
}

/* TLS buffers (≈ 2 × 16 KB per connection) in PSRAM: in internal RAM, Wi-Fi + BLE + TLS
 * left only 6 KB free during the STM download. The ESP32 crypto hardware does not use DMA,
 * so PSRAM is fine. Falls back to internal RAM if PSRAM runs out. */
static void *tls_calloc(size_t n, size_t size)
{
    if (n * size >= 4096) { /* large record buffers only */
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
        s_status = "No Wi-Fi configured";
        s_wx_status = "Wi-Fi not configured";
        s_bus_status = "Wi-Fi not configured";
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
    net_request(JOB_NTP | JOB_WX); /* a single connection at boot for both time and weather */
}

const char *net_status(void) { return s_status; }

time_t hal_last_sync(void) { return s_last_sync; }

const char *hal_time_hint(void)
{
    if (hal_time_valid()) return nullptr;
    return have_wifi() ? "Syncing the time…" : "Set up Wi-Fi (Settings)";
}

/* ------------------------------------------------------------ weather, bus */

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
    if (!s_task_running) s_wx_busy = false; /* refused: no Wi-Fi, or a scan is running */
}

const char *hal_weather_status(void) { return s_wx_status; }

bool hal_weather_busy(void) { return s_wx_busy; }

bool hal_bus_available(void) { return s_stm_key[0] != 0; }

void hal_bus_set_key(const char *key)
{
    Preferences p;
    p.begin("kgnet", false);
    if (key[0]) p.putString("stmkey", key);
    else p.remove("stmkey");
    p.end();
    snprintf(s_stm_key, sizeof s_stm_key, "%s", key[0] ? key : KG_STM_API_KEY);
    portENTER_CRITICAL(&s_mux);
    s_bus.valid = false;
    s_bus.n = 0;
    portEXIT_CRITICAL(&s_mux);
    s_bus_status = "No data yet";
}

void hal_bus_key_hint(char *out, size_t n)
{
    size_t len = strlen(s_stm_key);
    snprintf(out, n, "%s%s", len ? "…" : "", len > 4 ? s_stm_key + len - 4 : "");
}

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
        s_bus_status = "No stop selected";
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
    if (!hal_bus_available()) return "No STM API key (Settings)";
    return s_bus_busy ? "Updating…" : s_bus_status;
}

/* ---------------------------------------------------------- workshop mode
 * Watch on USB (so on mains power) with Wi-Fi configured: Wi-Fi stays on, OTA and the TCP
 * console are active. Unplugged: everything is off and battery life is untouched.
 * Can be disabled at build time (-D KG_WORKSHOP=0). */
#ifndef KG_OTA_PASS
#define KG_OTA_PASS ""
#endif

static bool s_workshop, s_ota_ready;

static void workshop_start(void)
{
    s_workshop = true;
    s_ota_ready = false;
    if (WiFi.status() == WL_CONNECTED) { /* connection inherited from the NTP sync */
        s_status = "Workshop: connecting";
        return;
    }
    WiFi.persistent(false);
    WiFi.setHostname("kronograf");
    WiFi.mode(WIFI_STA);
    WiFi.begin(s_ssid, s_pass);
    s_status = "Workshop: connecting";
}

static void workshop_stop(void)
{
    s_workshop = false;
    if (s_ota_ready) ArduinoOTA.end();
    s_ota_ready = false;
    console_tcp_enable(false);
    if (net_hold_active()) { /* the toolbox is open: its lease owns the connection now */
        s_kept = true;
        return;
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    s_status = "Idle";
}

void hal_net_hold(void)
{
    if (!have_wifi()) return;
    s_hold_until = (millis() + HOLD_MS) | 1;
    if (WiFi.status() == WL_CONNECTED || s_task || s_scanning) return;
    net_request(JOB_LINK);
}

void hal_net_release(void) { s_hold_until = 0; }

void net_keep(bool on)
{
    s_keep = on;
    if (on) hal_net_hold();
}

/* Lease lapsed (toolbox closed, screen off): stop the tools, then Wi-Fi. */
static void hold_check(void)
{
    if (net_hold_active()) return;
    nettools_cancel();
    if (!s_kept || s_task || s_scanning || s_workshop) return;
    s_kept = false;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    s_status = "Idle";
}

void net_workshop_loop(void)
{
    hold_check();
    if (!KG_WORKSHOP || !have_wifi() || s_task || s_scanning) return; /* network jobs and scans come first */
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
        /* The OTA transfer runs inside loop(): feed the loop watchdog. */
        ArduinoOTA.onProgress([](unsigned int, unsigned int) {
            esp_task_wdt_reset();
            guard_feed(); /* the transfer may outlast the RWDT's 90 s */
        });
        ArduinoOTA.onError([](ota_error_t e) { Serial.printf("event ota error %d\n", e); });
        ArduinoOTA.begin();
        console_tcp_enable(true);
        s_ota_ready = true;
        s_status = "Workshop: OTA + TCP console";
        Serial.printf("workshop: kronograf.local / %s, TCP console 2323\n", WiFi.localIP().toString().c_str());
    }
    if (s_ota_ready) ArduinoOTA.handle();
}

/* ------------------------------------------------------------ Wi-Fi setup */

bool hal_wifi_scan_start(void)
{
    if (s_task) return false; /* connecting: a scan would fail */
    s_scanning = true;
    if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
    WiFi.scanDelete();
    if (WiFi.scanNetworks(true /* asynchronous */, true /* hidden too */) == WIFI_SCAN_FAILED) {
        s_scanning = false;
        return false;
    }
    return true;
}

int hal_wifi_scan_count(void)
{
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return -1;
    if (s_scanning) {
        s_scanning = false;
        /* Never leave Wi-Fi on after a scan, unless something else owns it. */
        if (!s_workshop && !net_hold_active() && WiFi.status() != WL_CONNECTED) WiFi.mode(WIFI_OFF);
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

bool hal_wifi_scan_ap(int i, kg_ap_t *out)
{
    if (i < 0 || i >= WiFi.scanComplete()) return false;
    memset(out, 0, sizeof *out);
    snprintf(out->ssid, sizeof out->ssid, "%s", WiFi.SSID(i).c_str());
    const uint8_t *b = WiFi.BSSID(i);
    if (b) memcpy(out->bssid, b, 6);
    out->rssi = (int8_t)WiFi.RSSI(i);
    out->channel = (uint8_t)WiFi.channel(i);
    switch (WiFi.encryptionType(i)) {
    case WIFI_AUTH_OPEN: out->auth = KG_AUTH_OPEN; break;
    case WIFI_AUTH_WEP: out->auth = KG_AUTH_WEP; break;
    case WIFI_AUTH_WPA_PSK: out->auth = KG_AUTH_WPA; break;
    case WIFI_AUTH_WPA3_PSK:
    case WIFI_AUTH_WPA2_WPA3_PSK: out->auth = KG_AUTH_WPA3; break;
    case WIFI_AUTH_WPA2_ENTERPRISE: out->auth = KG_AUTH_ENTERPRISE; break;
    default: out->auth = KG_AUTH_WPA2; break;
    }
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
    net_start_time_sync(); /* reloads the credentials and tries a sync */
}

const char *hal_wifi_ssid(void) { return s_ssid; }

const char *hal_net_status(void) { return s_status; }
