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

kg_battery_t hal_battery(void);
uint32_t hal_steps_today(void);

void hal_backlight(uint8_t level); /* 0..255 */

/* Gros tampons (images de cadran) : PSRAM sur la montre, malloc en simulateur. */
void *hal_alloc_large(size_t bytes);
void hal_vibrate(uint16_t ms);

#ifdef __cplusplus
}
#endif
