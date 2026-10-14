/*
 * Next STM bus departures: streaming read of the GTFS-realtime feed (protobuf
 * TripUpdates, ~1 MB) without ever storing it whole. Only departures at the followed
 * stops are kept. Plain C shared by the watch and the simulator.
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
    time_t t; /* predicted departure (real time) */
} kg_bus_dep_t;

typedef struct {
    bool valid;
    time_t fetched; /* time received (0 = never) */
    time_t feed_ts; /* STM feed timestamp */
    uint8_t n;
    kg_bus_dep_t deps[KG_BUS_MAX_DEPS]; /* sorted by time */
} kg_bus_t;

/* Streaming parser. `buf` holds one entity at a time (a few KB; PSRAM on the watch). */
typedef struct {
    char stops[KG_BUS_MAX_STOPS][12];
    uint8_t n_stops;
    time_t not_before;
    uint8_t *buf;
    size_t cap;
    /* internal state */
    uint8_t phase; /* 0: tag, 1: length, 2: payload, 3: skip */
    uint64_t varint;
    uint8_t shift;
    uint32_t field, wire;
    size_t need, have;
    uint32_t entities, skipped;
    kg_bus_t out;
} kg_gtfs_t;

/* `stops`: comma-separated stop codes ("52345,52346"). Departures before `now - 60 s`
 * are ignored. */
void kg_gtfs_init(kg_gtfs_t *g, uint8_t *buf, size_t cap, const char *stops, time_t now);
void kg_gtfs_feed(kg_gtfs_t *g, const uint8_t *data, size_t n);
/* Finishes and returns the result (valid even when no departure was found). */
const kg_bus_t *kg_gtfs_finish(kg_gtfs_t *g);

#ifdef __cplusplus
}
#endif
