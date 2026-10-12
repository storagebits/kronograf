/*
 * Notifications iPhone par BLE (ANCS), avec NimBLE (hôte en PSRAM).
 *
 * La montre est un périphérique qui annonce la « sollicitation » du service ANCS : l'iPhone
 * la liste dans Réglages → Bluetooth. À la connexion, la montre demande le chiffrement ;
 * l'iPhone affiche une demande d'appairage et l'utilisateur saisit le code à 6 chiffres
 * affiché sur la montre (pas de « Just Works » : protection MITM). Une fois appairé, l'iPhone
 * se reconnecte seul. La montre devient alors client GATT de l'ANCS de l'iPhone, sur la
 * même connexion : Notification Source (événements) → requête d'attributs sur le Control
 * Point → réponse sur la Data Source.
 *
 * Fils d'exécution : les rappels NimBLE (tâche hôte, cœur 0) ne font que poser des
 * événements ; une tâche « kg_ancs » fait la découverte et les requêtes (opérations
 * bloquantes). L'UI lit une copie de la liste sous verrou.
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
static QueueHandle_t s_uids; /* uid | (flags << 32) : événements à traiter */

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static kg_notif_t s_list[MAX_NOTIFS];
static int s_count;
static volatile uint32_t s_seq;
static volatile bool s_wake_req;

static kg_ancs_rx_t s_rx;
static kg_notif_t s_last_rx; /* réponse décodée par le rappel Data Source */
static NimBLERemoteCharacteristic *s_cp;

/* -------------------------------------------------------------- liste */

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

/* En tête de liste (la plus récente d'abord) ; une notification modifiée remplace l'ancienne. */
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

/* ---------------------------------------------------------- rappels BLE */

static void on_notif_source(NimBLERemoteCharacteristic *, uint8_t *data, size_t len, bool)
{
    kg_ancs_event_t ev;
    if (!kg_ancs_parse_event(data, len, &ev)) return;
    if (ev.event == KG_ANCS_REMOVED) {
        list_remove(ev.uid);
        return;
    }
    uint64_t item = ev.uid | (uint64_t)ev.flags << 32 | (uint64_t)ev.category << 40 | (uint64_t)ev.event << 48;
    if (xQueueSend(s_uids, &item, 0) != pdTRUE) { /* file pleine (reconnexion) : on garde les plus récentes */
        uint64_t drop;
        xQueueReceive(s_uids, &drop, 0);
        xQueueSend(s_uids, &item, 0);
    }
}

static void on_data_source(NimBLERemoteCharacteristic *, uint8_t *data, size_t len, bool)
{
    if (kg_ancs_rx_feed(&s_rx, data, len, &s_last_rx)) xEventGroupSetBits(s_events, EV_RESPONSE);
}

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *, NimBLEConnInfo &info) override
    {
        s_conn = info.getConnHandle();
        s_state = KG_BLE_CONNECTED;
        Serial.printf("event ble connexion %s\n", info.getIdAddress().toString().c_str());
        /* Déjà appairé : le chiffrement reprend avec la clé mémorisée. Sinon : appairage. */
        NimBLEDevice::startSecurity(s_conn);
    }

    void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int reason) override
    {
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        s_cp = nullptr;
        s_state = s_enabled ? KG_BLE_WAITING : KG_BLE_OFF;
        xEventGroupSetBits(s_events, EV_DISCONNECTED);
        Serial.printf("event ble déconnexion %d\n", reason);
    }

    uint32_t onPassKeyDisplay() override
    {
        s_passkey = 100000 + esp_random() % 900000;
        s_state = KG_BLE_PAIRING;
        s_wake_req = true; /* le code doit être visible : on allume l'écran */
        Serial.println("event ble code d'appairage affiché");
        return s_passkey;
    }

    void onAuthenticationComplete(NimBLEConnInfo &info) override
    {
        if (!info.isEncrypted()) {
            Serial.println("event ble appairage refusé");
            s_state = KG_BLE_CONNECTED;
            NimBLEDevice::getServer()->disconnect(info.getConnHandle());
            return;
        }
        s_state = KG_BLE_CONNECTED;
        s_passkey = 0;
        xEventGroupSetBits(s_events, EV_SECURED);
    }
};

/* --------------------------------------------------------------- tâche ANCS */

static bool ancs_attach(void)
{
    NimBLEConnInfo info = s_server->getPeerInfoByHandle(s_conn);
    NimBLEClient *client = s_server->getClient(info);
    if (!client) return false;
    NimBLERemoteService *svc = client->getService(KG_ANCS_SERVICE);
    if (!svc) {
        Serial.println("event ble pas d'ANCS (pas un iPhone ?)");
        return false;
    }
    NimBLERemoteCharacteristic *ns = svc->getCharacteristic(KG_ANCS_NOTIF_SOURCE);
    NimBLERemoteCharacteristic *ds = svc->getCharacteristic(KG_ANCS_DATA_SOURCE);
    s_cp = svc->getCharacteristic(KG_ANCS_CONTROL_POINT);
    if (!ns || !ds || !s_cp) return false;
    /* Data Source d'abord : les réponses ne doivent pas arriver avant l'abonnement. */
    if (!ds->subscribe(true, on_data_source) || !ns->subscribe(true, on_notif_source)) return false;
    /* Intervalle de connexion économe, dans les bornes acceptées par iOS :
     * 300–330 ms, latence 4 (≤ 2 s entre échanges), délai de supervision 6 s. */
    client->updateConnParams(240, 264, 4, 600);
    return true;
}

static constexpr uint8_t EVENT_DISMISS = 0xFE; /* pseudo-événement : effacer sur l'iPhone */

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
    if (!s_cp || !s_cp->writeValue(req, n, true)) return;
    EventBits_t bits = xEventGroupWaitBits(s_events, EV_RESPONSE | EV_DISCONNECTED, pdFALSE, pdFALSE, pdMS_TO_TICKS(3000));
    if (!(bits & EV_RESPONSE) || s_last_rx.uid != uid) return;
    kg_notif_t note = s_last_rx;
    note.category = category;
    list_put(note);
    bool alert = event == KG_ANCS_ADDED && !(flags & (KG_ANCS_FLAG_PREEXISTING | KG_ANCS_FLAG_SILENT));
    if (alert) {
        s_seq++;
        s_wake_req = true;
    }
    Serial.printf("event notification cat=%u %s%s\n", category, note.app, alert ? " (alerte)" : "");
}

static void ancs_task(void *)
{
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(s_events, EV_SECURED, pdTRUE, pdFALSE, portMAX_DELAY);
        if (!(bits & EV_SECURED)) continue;
        xEventGroupClearBits(s_events, EV_DISCONNECTED);
        xQueueReset(s_uids);
        if (!ancs_attach()) continue;
        s_state = KG_BLE_READY;
        Serial.println("event ble ANCS prêt");
        while (!(xEventGroupGetBits(s_events) & EV_DISCONNECTED)) {
            uint64_t item;
            if (xQueueReceive(s_uids, &item, pdMS_TO_TICKS(500)) == pdTRUE) fetch(item);
        }
    }
}

/* ---------------------------------------------------------------- pilotage */

static void ble_up(void)
{
    if (s_inited) return;
    NimBLEDevice::init("Kronograf");
    NimBLEDevice::setSecurityAuth(true, true, true); /* appairage, MITM, connexion sécurisée */
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(new ServerCallbacks());
    s_server->advertiseOnDisconnect(true);
    s_server->start();

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    NimBLEAdvertisementData data;
    data.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    data.setAppearance(0x00C0); /* montre */
    /* Sollicitation du service ANCS (type 0x15, UUID 128 bits petit-boutiste) : c'est ce qui
     * fait apparaître la montre dans Réglages → Bluetooth de l'iPhone. */
    uint8_t sol[18] = {17, 0x15};
    NimBLEUUID uuid(KG_ANCS_SERVICE);
    memcpy(sol + 2, uuid.getValue(), 16);
    data.addData(sol, sizeof sol);
    adv->setAdvertisementData(data);
    NimBLEAdvertisementData scan;
    scan.setName("Kronograf");
    adv->setScanResponseData(scan);
    adv->setMinInterval(400); /* 250–500 ms : l'iPhone retrouve vite la montre, sans gaspiller */
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
    if (!s_events) return; /* mode sans échec : BLE jamais initialisé */
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

void hal_notif_dismiss(uint32_t uid)
{
    list_remove(uid);
    if (s_state != KG_BLE_READY) return;
    uint64_t item = uid | (uint64_t)EVENT_DISMISS << 48; /* écriture avec réponse : faite par la tâche */
    xQueueSendToFront(s_uids, &item, 0);
}
