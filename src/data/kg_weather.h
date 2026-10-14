/*
 * Open-Meteo weather: model, JSON response parser, labels.
 * Plain C shared by the watch and the simulator (tested against a real response).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KG_WX_HOURS 12

typedef struct {
    time_t t;
    float temp;
    uint8_t code; /* WMO weather code */
    uint8_t pop;  /* precipitation probability, % */
} kg_wx_hour_t;

typedef struct {
    bool valid;
    time_t fetched; /* time received (0 = never) */
    time_t obs;     /* time of the "current" observation */
    float temp, feels, wind_kmh;
    uint8_t code;
    bool day;
    float tmax, tmin; /* today */
    uint8_t pop_max;
    float tmax2, tmin2; /* tomorrow */
    uint8_t code2, pop2;
    uint8_t n_hours;
    kg_wx_hour_t hours[KG_WX_HOURS];
} kg_weather_t;

typedef enum {
    KG_WX_SUN, KG_WX_MOON, KG_WX_PARTLY, KG_WX_CLOUD, KG_WX_FOG,
    KG_WX_DRIZZLE, KG_WX_RAIN, KG_WX_SNOW, KG_WX_STORM,
} kg_wx_icon_t;

/* Home location (downtown Montréal): weather, and sunrise / sunset in the Sky app. */
#define KG_HOME_LAT 45.5017
#define KG_HOME_LON -73.5673
#define KG_STR_(x) #x
#define KG_STR(x) KG_STR_(x)

/* Open-Meteo request (plain HTTP: no personal data, public coordinates). */
#define KG_WX_URL                                                                                          \
    "http://api.open-meteo.com/v1/forecast?latitude=" KG_STR(KG_HOME_LAT) "&longitude=" KG_STR(KG_HOME_LON)  \
    "&current=temperature_2m,apparent_temperature,weather_code,is_day,wind_speed_10m"                       \
    "&hourly=temperature_2m,weather_code,precipitation_probability"                                         \
    "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"               \
    "&timezone=America%2FToronto&forecast_days=2&forecast_hours=12&timeformat=unixtime"

/* Parses the response; `out->fetched` is left untouched. False if an essential field is missing. */
bool kg_weather_parse(const char *json, size_t len, kg_weather_t *out);

/* Short label ("Rain", "Snow"…). */
const char *kg_weather_label(uint8_t code);
kg_wx_icon_t kg_weather_icon(uint8_t code, bool day);

#ifdef __cplusplus
}
#endif
