/*
 * Network toolbox on the watch: link details, and a background task (core 0, low
 * priority) that runs one tool at a time — the shared runners of kg_tools.c, plus the LAN
 * scan (ARP, below) and the Bluetooth scan (ble.cpp). Requests never block the UI: a new
 * one cancels the running tool and is picked up as soon as it returns.
 */
#include "../hal.h"
#include "hal_device.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <lwip/etharp.h>
#include <lwip/netif.h>
#include <lwip/priv/tcpip_priv.h>

/* Tool state (2.6 KB) in PSRAM: internal RAM stays for Wi-Fi, BLE and TLS. */
static kg_tool_state_t *s_st;
static SemaphoreHandle_t s_mx;
static volatile bool s_cancel;
static bool s_task_on, s_pending;
static kg_tool_t s_req;
static char s_req_target[48];

static void lock(void) { xSemaphoreTake(s_mx, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mx); }
static bool link_up(void) { return WiFi.status() == WL_CONNECTED && (uint32_t)WiFi.localIP() != 0; }
static uint32_t gateway(void) { return link_up() ? (uint32_t)WiFi.gatewayIP() : 0; }
static uint32_t dns(void) { return link_up() ? (uint32_t)WiFi.dnsIP(0) : 0; }
static const kg_tools_env_t ENV = {lock, unlock, link_up, gateway, dns, &s_cancel};

void hal_net_link(kg_link_t *l)
{
    memset(l, 0, sizeof *l);
    esp_read_mac(l->mac, ESP_MAC_WIFI_STA);
    snprintf(l->hostname, sizeof l->hostname, "kronograf");
    if (!link_up()) return;
    l->up = true;
    snprintf(l->ssid, sizeof l->ssid, "%s", WiFi.SSID().c_str());
    const uint8_t *b = WiFi.BSSID();
    if (b) memcpy(l->bssid, b, 6);
    l->rssi = (int8_t)WiFi.RSSI();
    l->channel = (uint8_t)WiFi.channel();
    l->ip = (uint32_t)WiFi.localIP();
    l->mask = (uint32_t)WiFi.subnetMask();
    l->gw = (uint32_t)WiFi.gatewayIP();
    l->dns = (uint32_t)WiFi.dnsIP(0);
}

/* ------------------------------------------------------------- LAN scan
 * ARP requests in batches of 8 (lwIP keeps only ~10 ARP entries), then the replies are read
 * back from the ARP table. lwIP is not thread-safe here: both steps run in its own thread
 * through tcpip_api_call(). */

#define ARP_BATCH 8

struct arp_call {
    struct tcpip_api_call_data call; /* first: lwIP casts back to it */
    uint32_t first;                  /* host order */
    int count;
    bool request;
    int found;
    uint32_t ips[ARP_BATCH];
    uint8_t macs[ARP_BATCH][6];
};

static err_t arp_fn(struct tcpip_api_call_data *c)
{
    arp_call *a = (arp_call *)c;
    struct netif *nif = netif_default;
    if (!nif) return ERR_IF;
    for (int i = 0; i < a->count; i++) {
        ip4_addr_t ip;
        ip.addr = htonl(a->first + (uint32_t)i);
        if (a->request) {
            etharp_request(nif, &ip);
            continue;
        }
        struct eth_addr *eth;
        const ip4_addr_t *found;
        if (etharp_find_addr(nif, &ip, &eth, &found) >= 0 && a->found < ARP_BATCH) {
            a->ips[a->found] = ip.addr;
            memcpy(a->macs[a->found], eth->addr, 6);
            a->found++;
        }
    }
    return ERR_OK;
}

static void lan_scan(void)
{
    lock();
    kg_tool_begin(s_st, KG_TOOL_LAN, "");
    unlock();
    if (!kg_tool_wait_link(s_st, &ENV)) return;
    uint32_t me = ntohl((uint32_t)WiFi.localIP()), gw = ntohl((uint32_t)WiFi.gatewayIP());
    int prefix = kg_mask_prefix((uint32_t)WiFi.subnetMask());
    if (prefix < 24) prefix = 24; /* larger networks: the /24 around the watch */
    uint32_t net = me & (0xFFFFFFFFu << (32 - prefix)), hosts = (1u << (32 - prefix)) - 2;
    char a[16], m[18];
    int devices = 0;
    for (uint32_t h = 1; h <= hosts && !s_cancel; h += ARP_BATCH) {
        int count = (int)(hosts - h + 1 < ARP_BATCH ? hosts - h + 1 : ARP_BATCH);
        kg_tool_status(s_st, &ENV, false, "Scanning… %lu / %lu", (unsigned long)h, (unsigned long)hosts);
        arp_call c = {};
        c.first = net + h;
        c.count = count;
        c.request = true;
        tcpip_api_call(arp_fn, &c.call);
        vTaskDelay(pdMS_TO_TICKS(300));
        c.request = false;
        tcpip_api_call(arp_fn, &c.call);
        for (uint32_t k = 0; k < (uint32_t)count; k++) { /* this watch is not in its own ARP table */
            uint32_t ip = net + h + k;
            if (ip != me) continue;
            kg_tool_row_t row = {};
            row.v = -(int32_t)(h + k); /* sorted by address */
            row.kind = KG_ROW_WARN;
            kg_ip_str(htonl(ip), row.a);
            snprintf(row.b, sizeof row.b, "this watch");
            kg_tool_add_row(s_st, &ENV, &row, true);
            devices++;
        }
        for (int i = 0; i < c.found; i++) {
            kg_tool_row_t row = {};
            uint32_t hip = ntohl(c.ips[i]);
            row.v = -(int32_t)(hip - net); /* sorted by address */
            kg_ip_str(c.ips[i], row.a);
            kg_fmt_mac(c.macs[i], m);
            snprintf(row.b, sizeof row.b, "%s", m);
            /* Locally administered MAC: a phone's private address. */
            row.kind = hip == gw ? KG_ROW_GOOD : (c.macs[i][0] & 2) ? KG_ROW_DIM : KG_ROW_NORMAL;
            kg_tool_add_row(s_st, &ENV, &row, true);
            devices++;
        }
    }
    kg_ip_str(htonl(net), a);
    if (s_cancel) kg_tool_status(s_st, &ENV, false, "Stopped · %d devices", devices);
    else kg_tool_status(s_st, &ENV, false, "%d devices on %s/%d", devices, a, prefix);
}

/* ------------------------------------------------------------------ task */

static void tool_task(void *)
{
    for (;;) {
        lock();
        if (!s_pending) {
            s_task_on = false;
            unlock();
            break;
        }
        s_pending = false;
        s_cancel = false;
        kg_tool_t tool = s_req;
        char target[48];
        memcpy(target, s_req_target, sizeof target);
        unlock();
        if (tool == KG_TOOL_LAN) lan_scan();
        else if (tool == KG_TOOL_BLE) {
            lock();
            kg_tool_begin(s_st, KG_TOOL_BLE, "");
            unlock();
            ble_tool_scan(s_st, &ENV);
        } else kg_tool_run(tool, target, s_st, &ENV);
        kg_tool_end(s_st, &ENV);
    }
    vTaskDelete(nullptr);
}

static bool ensure_state(void)
{
    if (!s_st) s_st = (kg_tool_state_t *)heap_caps_calloc(1, sizeof *s_st, MALLOC_CAP_SPIRAM);
    if (!s_mx) s_mx = xSemaphoreCreateMutex();
    return s_st && s_mx;
}

void hal_tool_start(kg_tool_t tool, const char *target)
{
    if (!ensure_state()) return;
    lock();
    s_req = tool;
    snprintf(s_req_target, sizeof s_req_target, "%s", target ? target : "");
    s_pending = true;
    s_cancel = true;                 /* the running tool, if any, returns early */
    kg_tool_begin(s_st, tool, target); /* the UI sees the new tool right away */
    bool start = !s_task_on;
    s_task_on = true;
    unlock();
    /* Core 0 next to Wi-Fi, low priority; the HTTP buffer and the formatting fit in 6 KB. */
    if (start && xTaskCreatePinnedToCore(tool_task, "kg_tool", 6144, nullptr, 1, nullptr, 0) != pdPASS) {
        lock();
        s_task_on = s_pending = false;
        s_st->running = false;
        snprintf(s_st->status, sizeof s_st->status, "Out of memory");
        s_st->failed = true;
        unlock();
    }
}

void hal_tool_stop(void)
{
    if (!s_st || !s_mx) return;
    lock();
    s_pending = false;
    s_cancel = true;
    if (!s_task_on) s_st->running = false;
    s_st->seq++;
    unlock();
}

void nettools_cancel(void)
{
    if (s_st && s_mx && s_task_on) hal_tool_stop();
}

void hal_tool_get(kg_tool_state_t *out)
{
    if (!s_st || !s_mx) {
        memset(out, 0, sizeof *out);
        return;
    }
    lock();
    memcpy(out, s_st, sizeof *out);
    unlock();
}
