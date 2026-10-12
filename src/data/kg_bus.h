/*
 * Prochains passages de bus STM : lecture en flux du GTFS temps réel (protobuf
 * TripUpdates, ~1 Mo), sans jamais le stocker en entier. On ne garde que les
 * passages aux arrêts suivis. Code C pur, commun à la montre et au simulateur.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KG_BUS_URL "https://api.stm.info/pub/od/gtfs-rt/ic/v2/tripUpdates"
#define KG_BUS_MAX_STOPS 3
#define KG_BUS_MAX_DEPS 12

typedef struct {
    char route[8];
    char stop[12];
    time_t t; /* passage prévu (temps réel) */
} kg_bus_dep_t;

typedef struct {
    bool valid;
    time_t fetched; /* réception (0 = jamais) */
    time_t feed_ts; /* horodatage du flux STM */
    uint8_t n;
    kg_bus_dep_t deps[KG_BUS_MAX_DEPS]; /* triés par heure */
} kg_bus_t;

/* Analyseur en flux. `buf` reçoit une entité à la fois (quelques Ko ; PSRAM sur la montre). */
typedef struct {
    char stops[KG_BUS_MAX_STOPS][12];
    uint8_t n_stops;
    time_t not_before;
    uint8_t *buf;
    size_t cap;
    /* état interne */
    uint8_t phase; /* 0 : tag, 1 : longueur, 2 : charge utile, 3 : saut */
    uint64_t varint;
    uint8_t shift;
    uint32_t field, wire;
    size_t need, have;
    uint32_t entities, skipped;
    kg_bus_t out;
} kg_gtfs_t;

/* `stops` : codes d'arrêt séparés par des virgules (« 52345,52346 »). Passages antérieurs à
 * `now - 60 s` ignorés. */
void kg_gtfs_init(kg_gtfs_t *g, uint8_t *buf, size_t cap, const char *stops, time_t now);
void kg_gtfs_feed(kg_gtfs_t *g, const uint8_t *data, size_t n);
/* Termine et retourne le résultat (valide même si aucun passage trouvé). */
const kg_bus_t *kg_gtfs_finish(kg_gtfs_t *g);

#ifdef __cplusplus
}
#endif
