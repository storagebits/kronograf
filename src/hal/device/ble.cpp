/*
 * iPhone notifications over BLE (ANCS), with NimBLE (host in PSRAM).
 *
 * The watch is a peripheral advertising an ANCS service solicitation. iOS does not list
 * generic BLE accessories in Settings → Bluetooth, so the first connection is made from a BLE
 * app (nRF Connect…). On connection the watch requests encryption; the iPhone shows a pairing
 * prompt and the user types the 6-digit code shown on the watch (no "Just Works": MITM
 * protection). Once bonded, the iPhone reconnects by itself. The watch then becomes a GATT
 * client of the iPhone's ANCS over the same connection: Notification Source (events) →
 * attribute request on the Control Point → response on the Data Source.
 *
 * Threads: NimBLE callbacks (host task, core 0) only post events; a "kg_ancs" task does the
 * discovery and the requests (blocking operations). The UI reads a copy of the list under a
 * lock.
 */
#include "../hal.h"
#include "hal_device.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>

static constexpr int MAX_NOTIFS = 8;
static constexpr uint32_t EV_SECURED = 1, EV_DISCONNECTED = 2, EV_RESPONSE = 4;

static volatile kg_ble_state_t s_state = KG_BLE_OFF;
static volatile uint32_t s_passkey;
static bool s_enabled, s_inited;
static NimBLEServer *s_server;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static TaskHandle_t s_task;
static EventGroupHandle_t s_events;
static QueueHandle_t s_uids; /* uid | (flags << 32): events to process */

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static kg_notif_t s_list[MAX_NOTIFS];
static int s_count;
static volatile uint32_t s_seq;
static volatile bool s_wake_req;

static kg_ancs_rx_t s_rx;
static kg_notif_t s_last_rx; /* response decoded by the Data Source callback */
static NimBLERemoteCharacteristic *s_cp;

/* Diagnostics (console "ble"): counters and the last failed step. */
static volatile uint32_t d_events, d_requests, d_responses, d_timeouts, d_wfail, d_frames;
static const char *volatile d_last = "";
static const char *volatile d_phase = "";
static volatile uint32_t d_connects, d_secured;
static volatile int d_reason = -1;
static volatile uint32_t d_rx_raw; /* notifications received at GAP level, before any routing */
static volatile uint16_t d_rx_handle, d_ns_handle, d_ds_handle;
static ble_gap_event_listener s_listener;

static int gap_listener(struct ble_gap_event *ev, void *)
{
    if (ev->type == BLE_GAP_EVENT_NOTIFY_RX) {
        d_rx_raw++;
        d_rx_handle = ev->notify_rx.attr_handle;
    }
    return 0;
}

/* --------------------------------------------------------------- list */

static void list_remove(uint32_t uid)
{
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < s_count; i++) {
        if (s_list[i].uid != uid) continue;
        memmove(&s_list[i], &s_list[i + 1], (s_count - i - 1) * sizeof s_list[0]);
        s_count--;
        break;
    }
    portEXIT_CRITICAL(&s_mux);
}

/* At the head of the list (most recent first); a modified notification replaces the old one. */
static void list_put(const kg_notif_t &n)
{
    list_remove(n.uid);
    portENTER_CRITICAL(&s_mux);
    if (s_count == MAX_NOTIFS) s_count--;
    memmove(&s_list[1], &s_list[0], s_count * sizeof s_list[0]);
    s_list[0] = n;
    s_count++;
    portEXIT_CRITICAL(&s_mux);
}

/* ---------------------------------------------------------- BLE callbacks */

static void on_notif_source(NimBLERemoteCharacteristic *, uint8_t *data, size_t len, bool)
{
    kg_ancs_event_t ev;
    d_events++;
    if (!kg_ancs_parse_event(data, len, &ev)) {
        d_last = "unreadable event";
        return;
    }
    if (ev.event == KG_ANCS_REMOVED) {
        list_remove(ev.uid);
        return;
    }
    uint64_t item = ev.uid | (uint64_t)ev.flags << 32 | (uint64_t)ev.category << 40 | (uint64_t)ev.event << 48;
    if (xQueueSend(s_uids, &item, 0) != pdTRUE) { /* queue full (reconnection): keep the most recent */
        uint64_t drop;
        xQueueReceive(s_uids, &drop, 0);
        xQueueSend(s_uids, &item, 0);
    }
}

static void on_data_source(NimBLERemoteCharacteristic *, uint8_t *data, size_t len, bool)
{
    d_frames++;
    if (kg_ancs_rx_feed(&s_rx, data, len, &s_last_rx)) xEventGroupSetBits(s_events, EV_RESPONSE);
}

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *, NimBLEConnInfo &info) override
    {
        s_conn = info.getConnHandle();
        s_state = KG_BLE_CONNECTED;
        d_connects++;
        Serial.printf("event ble connected %s\n", info.getIdAddress().toString().c_str());
        /* Already bonded: encryption resumes with the stored key. Otherwise: pairing. */
        NimBLEDevice::startSecurity(s_conn);
    }

    void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int reason) override
    {
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        s_cp = nullptr;
        s_state = s_enabled ? KG_BLE_WAITING : KG_BLE_OFF;
        xEventGroupClearBits(s_events, EV_SECURED); /* no stale encryption flag for the next link */
        xEventGroupSetBits(s_events, EV_DISCONNECTED);
        d_reason = reason;
        Serial.printf("event ble disconnected %d\n", reason);
    }

    uint32_t onPassKeyDisplay() override
    {
        s_passkey = 100000 + esp_random() % 900000;
        s_state = KG_BLE_PAIRING;
        s_wake_req = true; /* the code must be visible: turn the screen on */
        Serial.println("event ble pairing code shown");
        return s_passkey;
    }

    void onAuthenticationComplete(NimBLEConnInfo &info) override
    {
        if (!info.isEncrypted()) {
            Serial.println("event ble pairing rejected");
            s_state = KG_BLE_CONNECTED;
            NimBLEDevice::getServer()->disconnect(info.getConnHandle());
            return;
        }
        d_secured++;
        if (s_state == KG_BLE_READY) return; /* the iPhone is refreshing encryption on a ready link */
        s_state = KG_BLE_CONNECTED;
        s_passkey = 0;
        xEventGroupSetBits(s_events, EV_SECURED);
    }
};

/* --------------------------------------------------------------- ANCS task */

static bool ancs_attach(void)
{
    NimBLEConnInfo info = s_server->getPeerInfoByHandle(s_conn);
    NimBLEClient *client = s_server->getClient(info);
    d_phase = "discovery";
    if (!client) {
        d_last = "no GATT client";
        return false;
    }
    NimBLERemoteService *svc = client->getService(KG_ANCS_SERVICE);
    if (!svc) {
        d_last = "ANCS service not found";
        Serial.println("event ble no ANCS (not an iPhone?)");
        return false;
    }
    NimBLERemoteCharacteristic *ns = svc->getCharacteristic(KG_ANCS_NOTIF_SOURCE);
    NimBLERemoteCharacteristic *ds = svc->getCharacteristic(KG_ANCS_DATA_SOURCE);
    s_cp = svc->getCharacteristic(KG_ANCS_CONTROL_POINT);
    if (!ns || !ds || !s_cp) {
        d_last = "ANCS characteristics missing";
        return false;
    }
    /* Data Source first: responses must not arrive before the subscription. */
    d_phase = "subscribing";
    d_ns_handle = ns->getHandle();
    d_ds_handle = ds->getHandle();
    if (!ds->subscribe(true, on_data_source)) {
        d_last = "Data Source subscription refused";
        return false;
    }
    if (!ns->subscribe(true, on_notif_source)) {
        d_last = "Notification Source subscription refused";
        return false;
    }
    /* Frugal connection interval, within the bounds iOS accepts:
     * 300–330 ms, latency 4 (≤ 2 s between exchanges), 6 s supervision timeout. */
    client->updateConnParams(240, 264, 4, 600);
    return true;
}

static constexpr uint8_t EVENT_DISMISS = 0xFE; /* pseudo-event: clear on the iPhone */

static void fetch(uint64_t item)
{
    uint32_t uid = (uint32_t)item;
    if ((uint8_t)(item >> 48) == EVENT_DISMISS) {
        uint8_t cmd[8];
        size_t n = kg_ancs_build_action(cmd, sizeof cmd, uid, 1);
        if (s_cp) s_cp->writeValue(cmd, n, true);
        return;
    }
    uint8_t flags = (uint8_t)(item >> 32), category = (uint8_t)(item >> 40), event = (uint8_t)(item >> 48);
    uint8_t req[24];
    size_t n = kg_ancs_build_request(req, sizeof req, uid);
    kg_ancs_rx_reset(&s_rx);
    xEventGroupClearBits(s_events, EV_RESPONSE);
    d_requests++;
    d_phase = "request";
    if (!s_cp || !s_cp->writeValue(req, n, true)) {
        d_wfail++;
        d_last = "Control Point request refused";
        return;
    }
    d_phase = "awaiting response";
    EventBits_t bits = xEventGroupWaitBits(s_events, EV_RESPONSE | EV_DISCONNECTED, pdFALSE, pdFALSE, pdMS_TO_TICKS(3000));
    d_phase = "ready";
    if (!(bits & EV_RESPONSE)) {
        d_timeouts++;
        d_last = s_rx.len ? "incomplete response" : "no Data Source response";
        return;
    }
    if (s_last_rx.uid != uid) {
        d_last = "response for another notification";
        return;
    }
    d_responses++;
    kg_notif_t note = s_last_rx;
    note.category = category;
    list_put(note);
    bool alert = event == KG_ANCS_ADDED && !(flags & (KG_ANCS_FLAG_PREEXISTING | KG_ANCS_FLAG_SILENT));
    if (alert) {
        s_seq++;
        s_wake_req = true;
    }
    Serial.printf("event notification cat=%u %s%s\n", category, note.app, alert ? " (alert)" : "");
}

static void ancs_task(void *)
{
    for (;;) {
        d_phase = "awaiting encryption";
        EventBits_t bits = xEventGroupWaitBits(s_events, EV_SECURED, pdTRUE, pdFALSE, pdMS_TO_TICKS(15000));
        if (!(bits & EV_SECURED)) {
            /* Connected for 15 s without encryption: resuming the link failed silently (iPhone back
             * in range). Drop it: the iPhone reconnects cleanly. */
            uint16_t conn = s_conn;
            if (s_state == KG_BLE_CONNECTED && conn != BLE_HS_CONN_HANDLE_NONE && s_server) {
                d_last = "encryption never established, reconnecting";
                s_server->disconnect(conn);
            }
            continue;
        }
        xEventGroupClearBits(s_events, EV_DISCONNECTED);
        xQueueReset(s_uids);
        if (!ancs_attach()) {
            uint16_t conn = s_conn; /* discovery failed: restart the connection */
            if (conn != BLE_HS_CONN_HANDLE_NONE && s_server) s_server->disconnect(conn);
            continue;
        }
        d_phase = "ready";
        s_state = KG_BLE_READY;
        Serial.println("event ble ANCS ready");
        while (!(xEventGroupGetBits(s_events) & EV_DISCONNECTED)) {
            uint64_t item;
            if (xQueueReceive(s_uids, &item, pdMS_TO_TICKS(500)) == pdTRUE) fetch(item);
        }
    }
}

/* ---------------------------------------------------------------- control */

static void ble_up(void)
{
    if (s_inited) return;
    NimBLEDevice::init("Kronograf");
    NimBLEDevice::setSecurityAuth(true, true, true); /* bonding, MITM, secure connections */
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(new ServerCallbacks());
    s_server->advertiseOnDisconnect(true);
    s_server->start();
    ble_gap_event_listener_register(&s_listener, gap_listener, nullptr);

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    NimBLEAdvertisementData data;
    data.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    data.setAppearance(0x00C0); /* watch */
    /* ANCS service solicitation (AD type 0x15, little-endian 128-bit UUID): tells the iPhone
     * that the watch wants to consume its notifications. */
    uint8_t sol[18] = {17, 0x15};
    NimBLEUUID uuid(KG_ANCS_SERVICE);
    memcpy(sol + 2, uuid.getValue(), 16);
    data.addData(sol, sizeof sol);
    adv->setAdvertisementData(data);
    NimBLEAdvertisementData scan;
    scan.setName("Kronograf");
    adv->setScanResponseData(scan);
    adv->setMinInterval(400); /* 250–500 ms: the iPhone finds the watch quickly without wasting power */
    adv->setMaxInterval(800);
    adv->start();
    s_inited = true;
    s_state = KG_BLE_WAITING;
}

static void ble_down(void)
{
    if (!s_inited) return;
    NimBLEDevice::deinit(true);
    s_inited = false;
    s_server = nullptr;
    s_cp = nullptr;
    s_conn = BLE_HS_CONN_HANDLE_NONE;
    s_state = KG_BLE_OFF;
    xEventGroupSetBits(s_events, EV_DISCONNECTED);
}

void ble_start(void)
{
    s_events = xEventGroupCreate();
    s_uids = xQueueCreate(24, sizeof(uint64_t));
    xTaskCreatePinnedToCore(ancs_task, "kg_ancs", 5120, nullptr, 2, &s_task, 0);
    s_enabled = hal_setting_get("ble", 1) != 0;
    if (s_enabled) ble_up();
}

bool ble_active(void) { return s_inited; }

bool ble_take_wake(void)
{
    bool w = s_wake_req;
    s_wake_req = false;
    return w;
}

/* ------------------------------------------------------------------- HAL */

kg_ble_state_t hal_ble_state(void) { return s_state; }
uint32_t hal_ble_passkey(void) { return s_passkey; }
bool hal_ble_enabled(void) { return s_enabled; }

void hal_ble_enable(bool on)
{
    if (!s_events) return; /* safe mode: BLE never initialised */
    s_enabled = on;
    hal_setting_set("ble", on);
    if (on) ble_up();
    else ble_down();
}

void hal_ble_forget(void)
{
    if (!s_inited) return;
    if (s_conn != BLE_HS_CONN_HANDLE_NONE) s_server->disconnect(s_conn);
    NimBLEDevice::deleteAllBonds();
    portENTER_CRITICAL(&s_mux);
    s_count = 0;
    portEXIT_CRITICAL(&s_mux);
}

int hal_notif_count(void) { return s_count; }

bool hal_notif_get(int i, kg_notif_t *out)
{
    portENTER_CRITICAL(&s_mux);
    bool ok = i >= 0 && i < s_count;
    if (ok) *out = s_list[i];
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

uint32_t hal_notif_seq(void) { return s_seq; }

void ble_diag(Print &out)
{
    out.printf("link: %lu connections, %lu encryptions, last disconnect 0x%x, task: %s\n",
               (unsigned long)d_connects, (unsigned long)d_secured, d_reason, d_phase);
    out.printf("gap: %lu notifications received (last handle %u; NS %u, DS %u)\n", (unsigned long)d_rx_raw,
               d_rx_handle, d_ns_handle, d_ds_handle);
    out.printf("ancs: %lu events, %lu requests, %lu responses (%lu frames), %lu unanswered, %lu writes refused%s%s\n",
               (unsigned long)d_events, (unsigned long)d_requests, (unsigned long)d_responses, (unsigned long)d_frames,
               (unsigned long)d_timeouts, (unsigned long)d_wfail, *d_last ? "; last failure: " : "", d_last);
}

void hal_notif_dismiss(uint32_t uid)
{
    list_remove(uid);
    if (s_state != KG_BLE_READY) return;
    uint64_t item = uid | (uint64_t)EVENT_DISMISS << 48; /* write with response: done by the task */
    xQueueSendToFront(s_uids, &item, 0);
}

/* --------------------------------------------------------- toolbox scan
 * Active 5 s scan for the network toolbox, alongside the iPhone link (NimBLE scans and
 * keeps its connection). Only while Bluetooth is on: starting the stack for a scan alone
 * would tie the light-sleep rules to the toolbox. */
void ble_tool_scan(kg_tool_state_t *st, const kg_tools_env_t *env)
{
    if (!s_inited) {
        kg_tool_status(st, env, true, "Bluetooth is off (Settings)");
        return;
    }
    kg_tool_status(st, env, false, "Scanning for 5 s…");
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setActiveScan(true); /* scan responses carry most names */
    scan->setInterval(100);
    scan->setWindow(60);
    scan->setDuplicateFilter(1);
    NimBLEScanResults res = scan->getResults(5000, false);
    int n = res.getCount();
    for (int i = 0; i < n && !*env->cancel; i++) {
        const NimBLEAdvertisedDevice *d = res.getDevice(i);
        if (!d) continue;
        kg_tool_row_t row = {};
        row.v = d->getRSSI();
        row.kind = row.v > -60 ? KG_ROW_GOOD : row.v > -80 ? KG_ROW_NORMAL : KG_ROW_DIM;
        const char *vendor = nullptr;
        if (d->haveManufacturerData()) {
            std::string m = d->getManufacturerData();
            if (m.size() >= 2) vendor = kg_ble_company((uint16_t)((uint8_t)m[0] | (uint8_t)m[1] << 8));
        }
        std::string name = d->getName();
        if (!name.empty() && vendor) snprintf(row.a, sizeof row.a, "%.13s · %s", name.c_str(), vendor);
        else if (!name.empty()) snprintf(row.a, sizeof row.a, "%s", name.c_str());
        else if (vendor) snprintf(row.a, sizeof row.a, "%s", vendor);
        else snprintf(row.a, sizeof row.a, "%s", d->getAddress().toString().c_str());
        snprintf(row.b, sizeof row.b, "%d dBm", (int)row.v);
        kg_tool_add_row(st, env, &row, true);
    }
    scan->clearResults();
    kg_tool_status(st, env, false, "%d device%s · strongest first", n, n == 1 ? "" : "s");
}
