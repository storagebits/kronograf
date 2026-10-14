/*
 * Host tests for the data parsers (macOS):
 *   cc -Wall -Wextra -Werror -I src tests/test_data.c src/data/kg_*.c -o /tmp/kg_test
 *   /tmp/kg_test tests/data/openmeteo-montreal.json [tripUpdates.pb stops]
 * The STM feed (~1 MB, downloaded with the API key) is not committed.
 */
#include "data/kg_ancs.h"
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

/* ANCS: synthetic attributes response, cut into 20-byte frames as over BLE. */
static void test_ancs(void)
{
    uint8_t req[32];
    assert(kg_ancs_build_request(req, sizeof req, 0x01020304) == 13 && req[1] == 4 && req[4] == 1);
    kg_ancs_event_t ev;
    const uint8_t ns[8] = {0, KG_ANCS_FLAG_IMPORTANT, KG_CAT_SOCIAL, 1, 7, 0, 0, 0};
    assert(kg_ancs_parse_event(ns, 8, &ev) && ev.uid == 7 && ev.category == KG_CAT_SOCIAL);

    const char *app = "com.apple.MobileSMS", *title = "Ma\xC3\xAFt\xC3\xA9 \xF0\x9F\x98\x80 Lou",
               *msg = "Caf\xC3\xA9 at 6?\n\n\xE2\x80\x9COK\xE2\x80\x9D hors d\xE2\x80\x99\xC5\x93uvres \xE2\x82\xAC", *date = "20261012T143005";
    uint8_t resp[256];
    size_t n = 0;
    resp[n++] = 0;
    resp[n++] = 7, resp[n++] = 0, resp[n++] = 0, resp[n++] = 0;
    const char *vals[] = {app, title, msg, date};
    const uint8_t ids[] = {0, 1, 3, 5};
    for (int i = 0; i < 4; i++) {
        size_t l = strlen(vals[i]);
        resp[n++] = ids[i], resp[n++] = (uint8_t)l, resp[n++] = 0;
        memcpy(resp + n, vals[i], l);
        n += l;
    }
    kg_ancs_rx_t rx;
    kg_ancs_rx_reset(&rx);
    kg_notif_t out;
    int done = 0;
    for (size_t off = 0; off < n; off += 20) done += kg_ancs_rx_feed(&rx, resp + off, off + 20 > n ? n - off : 20, &out);
    assert(done == 1 && out.uid == 7);
    char buf[40];
    printf("ancs: [%s] \"%s\" \"%s\" %s\n", kg_ancs_app_name(out.app, buf, sizeof buf), out.title, out.message, out.date);
    assert(!strcmp(out.title, "Maïté Lou"));
    assert(!strcmp(kg_ancs_app_name("com.example.superApp", buf, sizeof buf), "SuperApp"));
    char tr[8];
    kg_text_sanitize(tr, sizeof tr, "abcd\xC3\xA9\xC3\xA9", 8); /* never cut in the middle of a character */
    assert(!strcmp(tr, "abcd\xC3\xA9"));
}

int main(int argc, char **argv)
{
    test_ancs();
    size_t len;
    char *json = slurp(argv[1], &len);
    assert(json);
    kg_weather_t wx = {0};
    assert(kg_weather_parse(json, len, &wx));
    printf("weather: %.1f° (feels %.1f°) %s, wind %.0f km/h, day=%d; low %.1f high %.1f pop %u%%; "
           "tomorrow %.1f/%.1f %s; %u hours, h+5 %.1f° pop %u%%\n",
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
    /* Irregular chunk sizes, as on the network. */
    for (size_t off = 0, k = 1; off < len; off += k, k = k * 7 % 1499 + 1)
        kg_gtfs_feed(&g, raw + off, off + k > len ? len - off : k);
    const kg_bus_t *b = kg_gtfs_finish(&g);
    printf("bus: %u entities, %u skipped, feed %ld, %u departures\n", g.entities, g.skipped, (long)b->feed_ts, b->n);
    for (int i = 0; i < b->n; i++) printf("  route %s stop %s at %ld\n", b->deps[i].route, b->deps[i].stop, (long)b->deps[i].t);
    assert(b->valid && g.skipped == 0);
    return 0;
}
