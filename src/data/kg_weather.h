/*
 * Météo Open-Meteo : modèle, analyse de la réponse JSON, libellés français.
 * Code C pur, commun à la montre et au simulateur (testé sur une vraie réponse).
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
    uint8_t code; /* code météo OMM */
    uint8_t pop;  /* probabilité de précipitation, % */
} kg_wx_hour_t;

typedef struct {
    bool valid;
    time_t fetched; /* heure de réception (0 = jamais) */
    time_t obs;     /* heure de l'observation « actuelle » */
    float temp, feels, wind_kmh;
    uint8_t code;
    bool day;
    float tmax, tmin; /* aujourd'hui */
    uint8_t pop_max;
    float tmax2, tmin2; /* demain */
    uint8_t code2, pop2;
    uint8_t n_hours;
    kg_wx_hour_t hours[KG_WX_HOURS];
} kg_weather_t;

typedef enum {
    KG_WX_SUN, KG_WX_MOON, KG_WX_PARTLY, KG_WX_CLOUD, KG_WX_FOG,
    KG_WX_DRIZZLE, KG_WX_RAIN, KG_WX_SNOW, KG_WX_STORM,
} kg_wx_icon_t;

/* Requête Open-Meteo (HTTP simple : aucune donnée personnelle, coordonnées publiques de Montréal). */
#define KG_WX_URL                                                                                          \
    "http://api.open-meteo.com/v1/forecast?latitude=45.5017&longitude=-73.5673"                             \
    "&current=temperature_2m,apparent_temperature,weather_code,is_day,wind_speed_10m"                       \
    "&hourly=temperature_2m,weather_code,precipitation_probability"                                         \
    "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"               \
    "&timezone=America%2FToronto&forecast_days=2&forecast_hours=12&timeformat=unixtime"

/* Analyse la réponse ; `out->fetched` n'est pas touché. Faux si un champ essentiel manque. */
bool kg_weather_parse(const char *json, size_t len, kg_weather_t *out);

/* Libellé court en français (« Pluie », « Neige »…). */
const char *kg_weather_label(uint8_t code);
kg_wx_icon_t kg_weather_icon(uint8_t code, bool day);

#ifdef __cplusplus
}
#endif
