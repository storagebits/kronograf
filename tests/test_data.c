/*
 * Tests hôte des analyseurs de données (macOS) :
 *   cc -Wall -Wextra -Werror -I src tests/test_data.c src/data/kg_weather.c src/data/kg_bus.c -o /tmp/kg_test
 *   /tmp/kg_test tests/data/openmeteo-montreal.json [tripUpdates.pb arrêts]
 * Le flux STM (~1 Mo, téléchargé avec la clé) n'est pas versionné.
 */
#include "data/kg_bus.h"
#include "data/kg_weather.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc(*len + 1);
    *len = fread(b, 1, *len, f);
    b[*len] = 0;
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    size_t len;
    char *json = slurp(argv[1], &len);
    assert(json);
    kg_weather_t wx = {0};
    assert(kg_weather_parse(json, len, &wx));
    printf("météo : %.1f° (ressenti %.1f°) %s, vent %.0f km/h, jour=%d ; min %.1f max %.1f pop %u%% ; "
           "demain %.1f/%.1f %s ; %u heures, h+5 %.1f° pop %u%%\n",
           wx.temp, wx.feels, kg_weather_label(wx.code), wx.wind_kmh, wx.day, wx.tmin, wx.tmax, wx.pop_max,
           wx.tmin2, wx.tmax2, kg_weather_label(wx.code2), wx.n_hours, wx.hours[5].temp, wx.hours[5].pop);
    assert(wx.n_hours == 12 && wx.hours[0].t > 0);
    assert(!kg_weather_parse("{\"current\":{}}", 14, &(kg_weather_t){0}));

    if (argc < 4) return 0;
    uint8_t *raw = (uint8_t *)slurp(argv[2], &len);
    assert(raw);
    static uint8_t buf[32768];
    kg_gtfs_t g;
    kg_gtfs_init(&g, buf, sizeof buf, argv[3], 0);
    /* Morceaux de tailles irrégulières, comme sur le réseau. */
    for (size_t off = 0, k = 1; off < len; off += k, k = k * 7 % 1499 + 1)
        kg_gtfs_feed(&g, raw + off, off + k > len ? len - off : k);
    const kg_bus_t *b = kg_gtfs_finish(&g);
    printf("bus : %u entités, %u ignorées, flux %ld, %u passages\n", g.entities, g.skipped, (long)b->feed_ts, b->n);
    for (int i = 0; i < b->n; i++) printf("  ligne %s arrêt %s à %ld\n", b->deps[i].route, b->deps[i].stop, (long)b->deps[i].t);
    assert(b->valid && g.skipped == 0);
    return 0;
}
