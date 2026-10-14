/*
 * Sun and Moon, computed offline: sunrise / sunset and civil twilight (NOAA "sunrise
 * equation", ±1 min at mid-latitudes), and the Moon's phase (Meeus, about an hour).
 */
#pragma once
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    time_t rise, set, noon;   /* UTC instants; rise/set are 0 on a polar day or night */
    time_t dawn, dusk;        /* civil twilight (sun 6° below the horizon) */
    bool polar_day, polar_night;
} kg_sun_t;

/* Sun events of the civil date y-m-d at latitude / longitude (degrees, east positive). */
void kg_sun_day(int y, int m, int d, double lat, double lon, kg_sun_t *out);

typedef struct {
    double age;     /* phase × mean lunation, 0..29.53 days */
    double phase;   /* 0 new, 0.25 first quarter, 0.5 full, 0.75 last quarter */
    double illum;   /* illuminated fraction 0..1 */
    const char *name;
    time_t next_full, next_new;
} kg_moon_t;

void kg_moon(time_t t, kg_moon_t *out);

#ifdef __cplusplus
}
#endif
