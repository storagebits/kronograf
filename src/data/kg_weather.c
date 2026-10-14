#include "kg_weather.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Minimal JSON reader, enough for the Open-Meteo response (no strings containing braces,
 * known structure): find a section, then keys inside it. */

typedef struct {
    const char *p, *end;
} span_t;

/* Top-level `"name":{…}` (or `[…]`) section in `in`. */
static bool section(span_t in, const char *name, span_t *out)
{
    char pat[48];
    size_t n = (size_t)snprintf(pat, sizeof pat, "\"%s\":", name);
    for (const char *p = in.p; p + n < in.end; p++) {
        if (memcmp(p, pat, n)) continue;
        const char *s = p + n;
        char open = *s, close = open == '{' ? '}' : open == '[' ? ']' : 0;
        if (!close) return false;
        int depth = 0;
        for (const char *q = s; q < in.end; q++) {
            if (*q == open) depth++;
            else if (*q == close && --depth == 0) {
                *out = (span_t){s + 1, q};
                return true;
            }
        }
        return false;
    }
    return false;
}

static bool number(span_t in, const char *key, double *v)
{
    char pat[48];
    size_t n = (size_t)snprintf(pat, sizeof pat, "\"%s\":", key);
    for (const char *p = in.p; p + n < in.end; p++) {
        if (memcmp(p, pat, n)) continue;
        char *e;
        *v = strtod(p + n, &e);
        return e != p + n;
    }
    return false;
}

/* Number array `"key":[…]`; returns how many values were read (≤ max). */
static int numbers(span_t in, const char *key, double *v, int max)
{
    span_t arr;
    if (!section(in, key, &arr)) return 0;
    int k = 0;
    const char *p = arr.p;
    while (p < arr.end && k < max) {
        char *e;
        double x = strtod(p, &e);
        if (e == p) { /* null or separator */
            if (!strncmp(p, "null", 4)) {
                x = 0;
                e = (char *)p + 4;
            } else {
                p++;
                continue;
            }
        }
        v[k++] = x;
        p = e;
    }
    return k;
}

bool kg_weather_parse(const char *json, size_t len, kg_weather_t *out)
{
    span_t all = {json, json + len}, cur, hourly, daily;
    if (!section(all, "current", &cur) || !section(all, "hourly", &hourly) || !section(all, "daily", &daily))
        return false;

    double t, v;
    if (!number(cur, "temperature_2m", &v)) return false;
    out->temp = (float)v;
    out->feels = number(cur, "apparent_temperature", &v) ? (float)v : out->temp;
    out->wind_kmh = number(cur, "wind_speed_10m", &v) ? (float)v : 0;
    out->code = number(cur, "weather_code", &v) ? (uint8_t)v : 0;
    out->day = number(cur, "is_day", &v) ? v != 0 : true;
    out->obs = number(cur, "time", &t) ? (time_t)t : 0;

    double ht[KG_WX_HOURS], htemp[KG_WX_HOURS], hcode[KG_WX_HOURS], hpop[KG_WX_HOURS];
    int n = numbers(hourly, "time", ht, KG_WX_HOURS);
    int nt = numbers(hourly, "temperature_2m", htemp, KG_WX_HOURS);
    int nc = numbers(hourly, "weather_code", hcode, KG_WX_HOURS);
    int np = numbers(hourly, "precipitation_probability", hpop, KG_WX_HOURS);
    if (nt < n) n = nt;
    if (nc < n) n = nc;
    for (int i = 0; i < n; i++) {
        out->hours[i] = (kg_wx_hour_t){(time_t)ht[i], (float)htemp[i], (uint8_t)hcode[i],
                                       (uint8_t)(i < np ? hpop[i] : 0)};
    }
    out->n_hours = (uint8_t)n;

    double dmax[2], dmin[2], dcode[2], dpop[2];
    int nd = numbers(daily, "temperature_2m_max", dmax, 2);
    if (numbers(daily, "temperature_2m_min", dmin, 2) < nd || numbers(daily, "weather_code", dcode, 2) < nd) return false;
    int ndp = numbers(daily, "precipitation_probability_max", dpop, 2);
    if (nd < 1) return false;
    out->tmax = (float)dmax[0];
    out->tmin = (float)dmin[0];
    out->pop_max = ndp > 0 ? (uint8_t)dpop[0] : 0;
    if (nd > 1) {
        out->tmax2 = (float)dmax[1];
        out->tmin2 = (float)dmin[1];
        out->code2 = (uint8_t)dcode[1];
        out->pop2 = ndp > 1 ? (uint8_t)dpop[1] : 0;
    }
    out->valid = true;
    return true;
}

const char *kg_weather_label(uint8_t c)
{
    if (c == 0) return "Clear";
    if (c == 1) return "Mostly clear";
    if (c == 2) return "Partly cloudy";
    if (c == 3) return "Overcast";
    if (c == 45 || c == 48) return "Fog";
    if (c >= 51 && c <= 55) return "Drizzle";
    if (c == 56 || c == 57) return "Freezing drizzle";
    if (c == 61) return "Light rain";
    if (c == 63) return "Rain";
    if (c == 65) return "Heavy rain";
    if (c == 66 || c == 67) return "Freezing rain";
    if (c == 71) return "Light snow";
    if (c == 73) return "Snow";
    if (c == 75) return "Heavy snow";
    if (c == 77) return "Snow grains";
    if (c >= 80 && c <= 82) return "Showers";
    if (c == 85 || c == 86) return "Snow showers";
    if (c == 95) return "Thunderstorm";
    if (c == 96 || c == 99) return "Thunderstorm, hail";
    return "—";
}

kg_wx_icon_t kg_weather_icon(uint8_t c, bool day)
{
    if (c == 0 || c == 1) return day ? KG_WX_SUN : KG_WX_MOON;
    if (c == 2) return KG_WX_PARTLY;
    if (c == 3) return KG_WX_CLOUD;
    if (c == 45 || c == 48) return KG_WX_FOG;
    if (c >= 51 && c <= 57) return KG_WX_DRIZZLE;
    if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) return KG_WX_RAIN;
    if ((c >= 71 && c <= 77) || c == 85 || c == 86) return KG_WX_SNOW;
    if (c >= 95) return KG_WX_STORM;
    return KG_WX_CLOUD;
}
