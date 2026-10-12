/*
 * Interface matérielle vue par l'UI. Implémentée deux fois :
 *   src/hal/device/  — T-Watch 2020 (ESP32)
 *   src/hal/sim/     — simulateur macOS (valeurs factices pilotables)
 * L'UI n'inclut jamais rien d'autre que ce fichier pour parler au matériel.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { KG_REV_UNKNOWN = 0, KG_REV_V1 = 1, KG_REV_V2 = 2, KG_REV_V3 = 3 } kg_rev_t;

/* Inventaire détecté au démarrage (scan I²C / sondes). */
typedef struct {
    kg_rev_t rev;
    bool pmu;        /* AXP202 @0x35 */
    bool rtc;        /* PCF8563 @0x51 */
    bool accel;      /* BMA423 @0x19 */
    bool touch;      /* FT6336 @0x38 */
    bool motor;
    bool speaker;    /* MAX98357A I²S */
    bool ir_tx;
    bool gps;        /* V2 */
    bool sdcard;     /* V2 */
    bool mic;        /* V3 */
    bool psram;
    uint32_t flash_mb;
    uint32_t psram_kb;
} kg_inventory_t;

typedef struct {
    int percent;      /* 0..100, -1 si inconnu */
    int millivolts;
    bool charging;
    bool usb;
} kg_battery_t;

const kg_inventory_t *hal_inventory(void);

/* Heure locale (America/Toronto, DST géré par la libc via TZ). */
void hal_now(struct tm *out, uint32_t *ms);

/* Faux tant que l'heure n'a été ni lue d'une RTC fiable ni synchronisée. */
bool hal_time_valid(void);

/* Horodatage de la dernière synchro NTP réussie (0 = jamais). */
time_t hal_last_sync(void);

/* Texte court pour l'utilisateur si l'heure ne peut pas être réglée, sinon NULL. */
const char *hal_time_hint(void);

kg_battery_t hal_battery(void);
uint32_t hal_steps_today(void);

void hal_backlight(uint8_t level); /* 0..255 */

/* Réglages persistants (NVS sur la montre). */
int32_t hal_setting_get(const char *key, int32_t def);
void hal_setting_set(const char *key, int32_t value);

/* Luminosité choisie par l'utilisateur (8..255, persistante) et délai de veille (s). */
uint8_t hal_brightness_get(void);
void hal_brightness_set(uint8_t level);
uint16_t hal_screen_timeout_get(void);
void hal_screen_timeout_set(uint16_t seconds);

/* Wi-Fi : identifiants saisis sur la montre (NVS, prioritaires sur secrets.h). */
void hal_wifi_scan_start(void);
int hal_wifi_scan_count(void);                       /* -1 : scan en cours */
bool hal_wifi_scan_get(int i, char *ssid, size_t n, int *rssi);
void hal_wifi_set_credentials(const char *ssid, const char *pass); /* déclenche une synchro */
const char *hal_wifi_ssid(void);                     /* réseau configuré ("" si aucun) */
const char *hal_net_status(void);                    /* état lisible : « synchronisé »… */

/* Facteur de luminosité demandé par le cadran affiché (100 = réglage utilisateur). */
void hal_brightness_scale(uint8_t pct);

/* Gros tampons (images de cadran) : PSRAM sur la montre, malloc en simulateur. */
void *hal_alloc_large(size_t bytes);
/* Retour haptique, non bloquant. Motif : durées alternées marche/arrêt, en commençant par marche. */
void hal_vibrate(uint16_t ms);
void hal_vibrate_pattern(const uint16_t *ms, uint8_t n);

/* Sons synthétisés (haut-parleur si présent), non bloquant. hz = 0 : silence. */
typedef struct {
    uint16_t hz;
    uint16_t ms;
} kg_tone_t;
void hal_tones(const kg_tone_t *tones, uint8_t n);
bool hal_sound_available(void);

/* Réveil programmé : la montre doit être éveillée à `when` (alarme, minuteur). 0 = aucun. */
void hal_schedule_wakeup(time_t when);

/* Historique des pas : out[0] = hier … out[6] = il y a 7 jours (0 si inconnu). */
void hal_steps_history(uint32_t out[7]);

#ifdef __cplusplus
}
#endif
