/*
 * World clock: local time in other time zones from POSIX TZ rules
 * ("CET-1CEST,M3.5.0,M10.5.0/3"), without touching the process TZ (the watch's own
 * local time stays on libc's TZ).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Offset east of UTC (s) of a POSIX TZ rule at UTC instant `t`; *dst tells whether summer
 * time applies (may be NULL). Rules with only "Mm.w.d" transitions are supported. */
int32_t kg_tz_offset(const char *tz, time_t t, bool *dst);

/* Broken-down local time in that zone. */
void kg_tz_local(const char *tz, time_t t, struct tm *out, bool *dst);

/* Days since 1970-01-01 of a civil date (proleptic Gregorian), and the reverse. */
int64_t kg_days_from_civil(int y, int m, int d);
void kg_civil_from_days(int64_t z, int *y, int *m, int *d);

typedef struct {
    const char *city;
    const char *code; /* IATA-style tag shown next to the city */
    const char *tz;
} kg_zone_t;

/* World clock cities, west to east. */
extern const kg_zone_t KG_ZONES[];
extern const int KG_N_ZONES;

#ifdef __cplusplus
}
#endif
