/*
 * Simulated network toolbox, sensors, infrared and system information.
 * By default the tools return fixed demo data (reproducible screenshots, documentation
 * addresses only). KG_NET_REAL=1 runs the real tools (src/data/kg_tools.c) from the Mac in
 * a thread, exactly as the watch does; KG_NET_GW sets the gateway (default 192.168.2.1).
 *   KG_NET=down        no Wi-Fi link
 *   KG_NET_TARGET=…    initial target shown by the demo data
 *   KG_ACCEL="x,y,z"   accelerometer reading in g
 */
#include "../hal.h"
#include "kg_version.h"
#include <SDL.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t ip4(const char *s)
{
    uint32_t ip = 0;
    kg_ip_parse(s, &ip);
    return ip;
}

/* ------------------------------------------------------------------ link */

static bool link_up(void) { return !getenv("KG_NET") || strcmp(getenv("KG_NET"), "down"); }

void hal_net_hold(void) {}
void hal_net_release(void) {}

void hal_net_link(kg_link_t *l)
{
    memset(l, 0, sizeof *l);
    static const uint8_t BSSID[6] = {0x3c, 0x84, 0x6a, 0x12, 0x9e, 0x40}, MAC[6] = {0x30, 0xae, 0xa4, 0x7b, 0x1c, 0x08};
    memcpy(l->mac, MAC, 6);
    snprintf(l->hostname, sizeof l->hostname, "kronograf");
    if (!link_up()) return;
    l->up = true;
    snprintf(l->ssid, sizeof l->ssid, "%s", hal_wifi_ssid());
    memcpy(l->bssid, BSSID, 6);
    l->rssi = -52;
    l->channel = 6;
    l->ip = ip4("192.168.2.34");
    l->mask = ip4("255.255.255.0");
    l->gw = ip4(getenv("KG_NET_GW") ? getenv("KG_NET_GW") : "192.168.2.1");
    l->dns = l->gw;
}

static const kg_ap_t APS[] = {
    {"Home", {0x3c, 0x84, 0x6a, 0x12, 0x9e, 0x40}, -48, 6, KG_AUTH_WPA2},
    {"Home-5G", {0x3c, 0x84, 0x6a, 0x12, 0x9e, 0x44}, -55, 36, KG_AUTH_WPA3},
    {"Atelier", {0x10, 0x27, 0xf5, 0x01, 0x02, 0x03}, -63, 1, KG_AUTH_WPA2},
    {"Videotron-4F2A", {0xa0, 0x63, 0x91, 0x4f, 0x2a, 0x10}, -69, 3, KG_AUTH_WPA2},
    {"Corner Café", {0x02, 0x1a, 0x11, 0xf0, 0x00, 0x01}, -71, 11, KG_AUTH_OPEN},
    {"HP-Print-3C", {0x9c, 0x2a, 0x70, 0x3c, 0x00, 0x01}, -76, 11, KG_AUTH_WPA2},
    {"BELL123", {0x44, 0xe9, 0xdd, 0x12, 0x34, 0x56}, -83, 9, KG_AUTH_WPA2},
    {"", {0x0e, 0x00, 0x00, 0x00, 0x00, 0x01}, -86, 4, KG_AUTH_WPA2},
};
#define N_APS ((int)(sizeof APS / sizeof APS[0]))
static uint32_t s_scan_at;

bool hal_wifi_scan_start(void)
{
    s_scan_at = SDL_GetTicks() + 600; /* a scan takes a moment */
    return true;
}

int hal_wifi_scan_count(void) { return s_scan_at && SDL_GetTicks() < s_scan_at && !getenv("KG_SHOT_NOW") ? -1 : N_APS; }

bool hal_wifi_scan_ap(int i, kg_ap_t *out)
{
    if (i < 0 || i >= N_APS) return false;
    *out = APS[i];
    return true;
}

/* ----------------------------------------------------------------- tools */

static kg_tool_state_t s_st;
static pthread_mutex_t s_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_t s_thread;
static bool s_thread_on;
static volatile bool s_cancel;
static kg_tool_t s_req_tool;
static char s_req_target[48];

static void lock(void) { pthread_mutex_lock(&s_mx); }
static void unlock(void) { pthread_mutex_unlock(&s_mx); }
static uint32_t gw(void)
{
    kg_link_t l;
    hal_net_link(&l);
    return l.gw;
}
static uint32_t dns(void) { return gw(); }
static const kg_tools_env_t ENV = {lock, unlock, link_up, gw, dns, &s_cancel};

static void *worker(void *arg)
{
    (void)arg;
    kg_tool_run(s_req_tool, s_req_target, &s_st, &ENV);
    return NULL;
}

static void row(const char *a, const char *b, int32_t v, uint8_t kind)
{
    kg_tool_row_t r = {.v = v, .kind = kind};
    snprintf(r.a, sizeof r.a, "%s", a);
    snprintf(r.b, sizeof r.b, "%s", b);
    s_st.rows[s_st.n++] = r;
}

/* Demo results: documentation (RFC 5737) and private addresses only. */
static void demo(kg_tool_t tool, const char *target)
{
    kg_tool_begin(&s_st, tool, target);
    if (!link_up()) {
        snprintf(s_st.status, sizeof s_st.status, "Wi-Fi unreachable");
        s_st.failed = true;
        s_st.running = false;
        return;
    }
    s_st.ip = ip4("1.1.1.1");
    switch (tool) {
    case KG_TOOL_PING: {
        static const int16_t RTT[] = {52, 48, 61, 49, 55, 47, 50, 93, 51, 46, 49, 58, 50, -1, 48, 47, 52, 66, 49, 51, 48,
                                      50, 47, 55, 49, 120, 52, 48, 50, 47, 49, 53, 51, 48, 46, 50, 49, 57, 48, 52, 49, 51};
        for (size_t i = 0; i < sizeof RTT / sizeof RTT[0]; i++) {
            int32_t us = RTT[i] < 0 ? -1 : RTT[i] * 100;
            s_st.hist[s_st.hist_n++] = us;
            s_st.sent++;
            if (us < 0) continue;
            s_st.recv++;
            s_st.sum_us += (uint32_t)us;
            s_st.last_us = (uint32_t)us;
            if (!s_st.min_us || (uint32_t)us < s_st.min_us) s_st.min_us = (uint32_t)us;
            if ((uint32_t)us > s_st.max_us) s_st.max_us = (uint32_t)us;
        }
        snprintf(s_st.status, sizeof s_st.status, "%u sent · %u %% loss", s_st.sent,
                 (unsigned)((s_st.sent - s_st.recv) * 100 / s_st.sent));
        return; /* keeps "running", as a live ping would */
    }
    case KG_TOOL_TRACE: {
        static const char *const H[][2] = {
            {" 1  192.168.2.1", "1.2 ms"}, {" 2  100.64.12.1", "6.8 ms"}, {" 3  *", "no reply"},
            {" 4  203.0.113.9", "8.1 ms"}, {" 5  198.51.100.17", "9.4 ms"}, {" 6  198.51.100.42", "9.9 ms"},
            {" 7  203.0.113.77", "10.6 ms"}, {" 8  1.1.1.1", "9.8 ms"},
        };
        for (int i = 0; i < 8; i++) row(H[i][0], H[i][1], i, i == 2 ? KG_ROW_DIM : i == 7 ? KG_ROW_GOOD : KG_ROW_NORMAL);
        snprintf(s_st.status, sizeof s_st.status, "Reached in 8 hops");
        break;
    }
    case KG_TOOL_DNS:
        row("A", "140.82.112.3", 0, KG_ROW_GOOD);
        row("A", "140.82.112.4", 1, KG_ROW_GOOD);
        row("Server", "192.168.2.1", 2, KG_ROW_DIM);
        snprintf(s_st.status, sizeof s_st.status, "2 addresses in 23 ms");
        break;
    case KG_TOOL_PORTS: {
        static const struct { const char *a, *b; uint8_t k; } P[] = {
            {"22  ssh", "closed", KG_ROW_WARN}, {"53  dns", "open · 1.4 ms", KG_ROW_GOOD},
            {"80  http", "open · 2.1 ms", KG_ROW_GOOD}, {"443  https", "open · 2.3 ms", KG_ROW_GOOD},
            {"21  ftp", "filtered", KG_ROW_DIM}, {"23  telnet", "filtered", KG_ROW_DIM},
            {"25  smtp", "filtered", KG_ROW_DIM}, {"110  pop3", "filtered", KG_ROW_DIM},
        };
        for (int i = 0; i < 8; i++) row(P[i].a, P[i].b, i, P[i].k);
        snprintf(s_st.status, sizeof s_st.status, "3 open of 16 ports");
        break;
    }
    case KG_TOOL_LAN: {
        static const char *const L[][2] = {
            {"192.168.2.1", "3c:84:6a:12:9e:3f"}, {"192.168.2.10", "a4:83:e7:5d:21:90"}, {"192.168.2.17", "this watch"},
            {"192.168.2.23", "b8:27:eb:44:0c:7e"}, {"192.168.2.34", "this watch"}, {"192.168.2.41", "9c:2a:70:3c:5e:12"},
            {"192.168.2.52", "5e:91:0a:c2:77:31"}, {"192.168.2.60", "f0:9f:c2:18:a6:04"},
        };
        for (int i = 0; i < 8; i++) {
            if (i == 2) continue;
            row(L[i][0], L[i][1], i, i == 0 ? KG_ROW_GOOD : i == 4 ? KG_ROW_WARN : i == 6 ? KG_ROW_DIM : KG_ROW_NORMAL);
        }
        snprintf(s_st.status, sizeof s_st.status, "7 devices on 192.168.2.0/24");
        break;
    }
    case KG_TOOL_BLE: {
        static const struct { const char *a; int rssi; } B[] = {
            {"iPhone · Apple", -44}, {"Kronograf watch", -51}, {"AirPods Pro · Apple", -58}, {"Galaxy Buds · Samsung", -67},
            {"Hue lamp", -72}, {"Ruuvi 3F21", -79}, {"Microsoft", -84}, {"d4:3a:2c:91:07:5e", -90},
        };
        for (int i = 0; i < 8; i++) {
            char b[16];
            snprintf(b, sizeof b, "%d dBm", B[i].rssi);
            row(B[i].a, b, B[i].rssi, B[i].rssi > -60 ? KG_ROW_GOOD : B[i].rssi > -80 ? KG_ROW_NORMAL : KG_ROW_DIM);
        }
        snprintf(s_st.status, sizeof s_st.status, "23 devices · strongest first");
        break;
    }
    case KG_TOOL_SPEED: {
        static const float S[] = {9.1f, 15.2f, 17.8f, 19.4f, 18.2f, 20.6f, 21.9f, 19.7f, 18.9f, 20.3f, 22.1f, 19.8f, 17.4f,
                                  18.8f, 20.1f, 19.2f, 18.6f, 19.9f, 21.2f, 20.4f, 18.3f, 19.5f, 20.8f, 19.1f, 18.7f, 20.2f,
                                  19.6f, 21.4f, 20.0f, 19.3f, 18.5f, 19.7f, 20.9f, 19.4f, 18.8f, 20.5f, 19.0f, 19.8f, 20.3f, 19.6f};
        for (int i = 0; i < KG_SPEED_HIST; i++) { /* scaled to what the watch measures (D52) */
            float v = S[i] * 0.08f;
            s_st.speed_hist[s_st.speed_n++] = v;
            if (v > s_st.peak_mbps) s_st.peak_mbps = v;
        }
        s_st.mbps = 1.55f;
        s_st.bytes = 1940000;
        s_st.elapsed_ms = 10000;
        s_st.ttfb_ms = 31;
        snprintf(s_st.status, sizeof s_st.status, "1.9 MB in 10.0 s");
        break;
    }
    case KG_TOOL_PUBLIC:
        row("Public IP", "203.0.113.42", 0, KG_ROW_GOOD);
        row("Provider", "Example ISP", 1, KG_ROW_NORMAL);
        row("Network", "AS64500 Example ISP", 2, KG_ROW_NORMAL);
        row("Location", "Montreal, CA", 3, KG_ROW_NORMAL);
        snprintf(s_st.status, sizeof s_st.status, "via ip-api.com");
        break;
    default:
        break;
    }
    s_st.running = false;
}

void hal_tool_stop(void)
{
    if (s_thread_on) {
        s_cancel = true;
        pthread_join(s_thread, NULL);
        s_thread_on = false;
    }
    lock();
    s_st.running = false;
    s_st.seq++;
    unlock();
}

void hal_tool_start(kg_tool_t tool, const char *target)
{
    hal_tool_stop();
    bool real = getenv("KG_NET_REAL") && tool != KG_TOOL_LAN && tool != KG_TOOL_BLE;
    if (!real) {
        lock();
        demo(tool, target);
        unlock();
        return;
    }
    s_cancel = false;
    s_req_tool = tool;
    snprintf(s_req_target, sizeof s_req_target, "%s", target ? target : "");
    s_thread_on = pthread_create(&s_thread, NULL, worker, NULL) == 0;
}

void hal_tool_get(kg_tool_state_t *out)
{
    lock();
    *out = s_st;
    unlock();
}

/* ------------------------------------------------------- sensors, IR, system */

bool hal_accel(float *x, float *y, float *z)
{
    const char *e = getenv("KG_ACCEL");
    *x = 0.037f, *y = -0.022f, *z = 0.999f;
    if (e) sscanf(e, "%f,%f,%f", x, y, z);
    return true;
}

bool hal_ir_available(void) { return hal_inventory()->ir_tx; }

void hal_ir_send(kg_ir_proto_t proto, uint32_t code)
{
    kg_ir_frame_t f;
    kg_ir_encode(proto, code, &f);
    printf("[sim] IR proto %d code 0x%lX: %u pulses at %lu Hz ×%u\n", proto, (unsigned long)code, f.n,
           (unsigned long)f.carrier_hz, f.repeats);
}

void hal_brightness_boost(bool on) { printf("[sim] brightness boost %s\n", on ? "on" : "off"); }

void hal_sysinfo(kg_sysinfo_t *s)
{
    memset(s, 0, sizeof *s);
    s->uptime_s = 2 * 86400 + 3 * 3600 + 12 * 60 + SDL_GetTicks() / 1000;
    s->heap_total = 317 * 1024;
    s->heap_free = 131 * 1024;
    s->heap_min = 104 * 1024;
    s->psram_total = 4 * 1024 * 1024 - 64 * 1024;
    s->psram_free = 3 * 1024 * 1024 + 210 * 1024;
    s->cpu_mhz = 240;
    s->fps = 60;
    kg_battery_t b = hal_battery();
    s->batt_mv = b.millivolts;
    s->batt_pct = b.percent;
    s->batt_ma = -24.6f;
    s->pmu_temp = 36.5f;
    s->wakes = 214;
    s->light_sleeps = 1893;
    s->reset_reason = "power-on";
    s->app_kb = 1964;
    s->app_slot_kb = 6144;
    s->version = KG_VERSION;
}
