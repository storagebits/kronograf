/*
 * Host tests for the data parsers (macOS):
 *   cc -Wall -Wextra -Werror -I src tests/test_data.c src/data/kg_*.c src/third_party/tjpgd/tjpgd.c -lm -o /tmp/kg_test
 *   /tmp/kg_test tests/data/openmeteo-montreal.json [tripUpdates.pb stops]
 * The STM feed (~1 MB, downloaded with the API key) is not committed.
 */
#include "data/kg_ancs.h"
#include "data/kg_bus.h"
#include "data/kg_ir.h"
#include "data/kg_media.h"
#include "data/kg_net.h"
#include "data/kg_sky.h"
#include "data/kg_tz.h"
#include "data/kg_weather.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

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


/* Subnet arithmetic, address parsing, JSON field extraction (no network). */
static void test_net(void)
{
    char a[16];
    uint32_t ip;
    assert(kg_ip_parse("192.168.1.20", &ip) && !strcmp(kg_ip_str(ip, a), "192.168.1.20"));
    assert(!kg_ip_parse("192.168.1", &ip) && !kg_ip_parse("1.2.3.256", &ip) && !kg_ip_parse("1.2.3.4x", &ip));
    kg_subnet_t s;
    assert(kg_subnet_parse("10.20.30.40/22", &s) && s.prefix == 22 && s.is_private);
    assert(!strcmp(kg_ip_str(s.network, a), "10.20.28.0") && !strcmp(kg_ip_str(s.broadcast, a), "10.20.31.255"));
    assert(!strcmp(kg_ip_str(s.mask, a), "255.255.252.0") && s.hosts == 1022);
    assert(!strcmp(kg_ip_str(s.first, a), "10.20.28.1") && !strcmp(kg_ip_str(s.last, a), "10.20.31.254"));
    assert(kg_subnet_parse("172.16.5.4 255.255.255.0", &s) && s.prefix == 24 && s.is_private);
    assert(kg_subnet_parse("8.8.8.8", &s) && s.prefix == 24 && !s.is_private);
    assert(kg_subnet_parse("100.64.1.1/10", &s) && s.is_private);
    assert(kg_subnet_parse("192.0.2.1/31", &s) && s.hosts == 2);
    assert(kg_subnet_parse("192.0.2.1/32", &s) && s.hosts == 1 && s.first == s.last);
    assert(kg_subnet_parse("0.0.0.0/0", &s) && s.hosts == 0xFFFFFFFEu);
    assert(!kg_subnet_parse("1.2.3.4/33", &s) && !kg_subnet_parse("1.2.3.4 255.0.255.0", &s));
    kg_ip_parse("255.255.255.128", &ip);
    assert(kg_mask_prefix(ip) == 25);
    char f[32];
    const char *json = "{\"status\":\"success\",\"isp\":\"Bell \\\"Canada\\\"\",\"as\": \"AS577\"}";
    assert(kg_json_str(json, "isp", f, sizeof f) && !strcmp(f, "Bell \"Canada\""));
    assert(kg_json_str(json, "as", f, sizeof f) && !strcmp(f, "AS577") && !kg_json_str(json, "city", f, sizeof f));
    assert(!strcmp(kg_port_name(443), "https") && !strcmp(kg_port_name(4), ""));
    printf("net: subnets, addresses and JSON fields OK\n");
}

/* POSIX TZ rules against the C library, every 37 minutes over two years. */
static void test_tz(void)
{
    for (int z = 0; z < KG_N_ZONES; z++) {
        setenv("TZ", KG_ZONES[z].tz, 1);
        tzset();
        for (time_t t = 1767225600 /* 2026-01-01 */; t < 1767225600 + 2 * 366 * 86400; t += 37 * 60) {
            struct tm ref, got;
            bool dst;
            localtime_r(&t, &ref);
            kg_tz_local(KG_ZONES[z].tz, t, &got, &dst);
            if (ref.tm_hour != got.tm_hour || ref.tm_min != got.tm_min || ref.tm_mday != got.tm_mday ||
                (ref.tm_isdst > 0) != dst) {
                printf("tz: %s at %ld: libc %02d:%02d dst %d, kg %02d:%02d dst %d\n", KG_ZONES[z].city, (long)t,
                       ref.tm_hour, ref.tm_min, ref.tm_isdst, got.tm_hour, got.tm_min, dst);
                assert(0);
            }
        }
    }
    unsetenv("TZ");
    tzset();
    int y, m, d;
    kg_civil_from_days(kg_days_from_civil(2024, 2, 29), &y, &m, &d);
    assert(y == 2024 && m == 2 && d == 29 && kg_days_from_civil(1970, 1, 1) == 0);
    printf("tz: %d zones match the C library\n", KG_N_ZONES);
}

/* Sun and Moon against published values (timeanddate.com, ±3 min / ±1 day). */
static void test_sky(void)
{
    kg_sun_t s;
    kg_sun_day(2026, 6, 21, KG_HOME_LAT, KG_HOME_LON, &s);
    /* Montréal, June 21 2026: sunrise 5:06, sunset 20:47 EDT (9:06 / 0:47 UTC). */
    time_t day = (time_t)kg_days_from_civil(2026, 6, 21) * 86400;
    long rise = (long)(s.rise - day), set = (long)(s.set - day);
    printf("sky: Montréal 2026-06-21 rise %02ld:%02ld UTC set %02ld:%02ld UTC\n", rise / 3600, rise / 60 % 60,
           set / 3600 % 24, set / 60 % 60);
    assert(labs(rise - (9 * 3600 + 6 * 60)) < 180 && labs(set - (24 * 3600 + 47 * 60)) < 180);
    kg_sun_day(2026, 12, 21, 69.65, 18.96, &s); /* Tromsø: polar night */
    assert(s.polar_night && !s.rise);
    /* Phase times within 2 h: new moon of the 2024-04-08 total solar eclipse (18:21 UTC), full moon
     * of the 2025-03-14 total lunar eclipse (06:55 UTC). */
    kg_moon_t moon;
    kg_moon(1712600460 - 5 * 86400, &moon);
    printf("sky: new moon predicted %+ld min from the 2024-04-08 eclipse\n", (long)(moon.next_new - 1712600460) / 60);
    assert(labs((long)(moon.next_new - 1712600460)) < 7200);
    kg_moon(1741935300 - 3 * 86400, &moon);
    printf("sky: full moon predicted %+ld min from the 2025-03-14 eclipse\n", (long)(moon.next_full - 1741935300) / 60);
    assert(labs((long)(moon.next_full - 1741935300)) < 7200);
    kg_moon(1741935300, &moon);
    assert(moon.illum > 0.995 && !strcmp(moon.name, "Full moon"));
}

/* Infrared pulse trains: lengths, headers and total durations. */
static void test_ir(void)
{
    kg_ir_frame_t f;
    assert(kg_ir_encode(KG_IR_NEC, 0x20DF10EF, &f) && f.n == 67 && f.us[0] == 9000 && f.us[1] == 4500);
    assert(f.us[3] == 560 && f.us[5] == 560 && f.us[7] == 1690); /* 0x2… = 0010: third bit set */
    assert(kg_ir_encode(KG_IR_SAMSUNG, 0xE0E040BF, &f) && f.n == 67 && f.us[0] == 4500 && f.us[3] == 1690);
    assert(kg_ir_encode(KG_IR_SONY12, 0xA90, &f) && f.n == 25 && f.us[0] == 2400 && f.us[2] == 1200 &&
           f.us[4] == 600 && f.repeats == 3 && f.carrier_hz == 40000);
    /* RC5 power (address 0, command 12): 14 bits = 28 half-bits, minus the idle first one and,
     * since the last bit is a 0 (mark then space), the idle last one. */
    assert(kg_ir_encode(KG_IR_RC5, 12, &f));
    uint32_t total = 0;
    for (int i = 0; i < f.n; i++) {
        total += f.us[i];
        assert(f.us[i] == 889 || f.us[i] == 1778);
    }
    assert(total == 26 * 889 && (f.n & 1));
    printf("ir: NEC, Samsung, Sony and RC5 trains OK (%d brands)\n", KG_IR_N_BRANDS);
}

/* Media: ICY metadata, ring buffer, the radio decoder end to end, MJPEG and JPEG. */
static void media_nop(void) {}
static void *media_alloc(size_t n) { return calloc(1, n); }
static void media_sleep(uint32_t ms) { (void)ms; }
static bool media_up(void) { return true; }
static uint32_t pcm_rate;
static size_t pcm_n;
static int pcm_crossings;
static int pcm_sign;
static bool pcm_open(uint32_t rate)
{
    pcm_rate = rate;
    return true;
}
static void pcm_write(const int16_t *s, size_t n)
{
    for (size_t i = 0; i < n; i++, pcm_n++) {
        int sign = s[i] > 1000 ? 1 : s[i] < -1000 ? -1 : 0; /* hysteresis: the padding's noise is not a wave */
        if (sign && sign != pcm_sign) {
            if (pcm_sign) pcm_crossings++;
            pcm_sign = sign;
        }
    }
}
static const kg_media_env_t MEDIA_ENV = {media_nop, media_nop, media_alloc, free, media_sleep, media_up,
                                         pcm_open, pcm_write, media_nop};

static void test_media(const char *dir)
{
    /* ICY: 8 audio bytes, metadata block, 8 audio bytes, empty block, cut at awkward places. */
    uint8_t s[64], meta[32] = "StreamTitle='Air - La femme';";
    size_t n = 0;
    for (int i = 0; i < 8; i++) s[n++] = (uint8_t)('a' + i);
    s[n++] = 2;
    memcpy(s + n, meta, 32), n += 32;
    for (int i = 0; i < 8; i++) s[n++] = (uint8_t)('A' + i);
    s[n++] = 0;
    s[n++] = 'z';
    kg_icy_t icy;
    kg_icy_init(&icy, 8);
    uint8_t out[64];
    size_t got = 0;
    for (size_t off = 0; off < n; off += 5) {
        size_t k = off + 5 > n ? n - off : 5;
        memcpy(out + got, s + off, k);
        got += kg_icy_feed(&icy, out + got, k);
    }
    assert(got == 17 && !memcmp(out, "abcdefghABCDEFGHz", 17) && !strcmp(icy.title, "Air - La femme"));
    uint8_t one[40] = "x\x02StreamTitle='Caf\xe9';"; /* 1 audio byte, a 32-byte block in Latin-1 */
    kg_icy_init(&icy, 1);
    assert(kg_icy_feed(&icy, one, 34) == 1 && !strcmp(icy.title, "Caf\xC3\xA9"));

    kg_ring_t ring = {calloc(1, 10), 10, 0, 0};
    uint8_t b[16];
    assert(kg_ring_write(&ring, (const uint8_t *)"0123456789AB", 12) == 10 && kg_ring_read(&ring, b, 4) == 4);
    assert(kg_ring_write(&ring, (const uint8_t *)"CDEF", 4) == 4 && kg_ring_read(&ring, b, 16) == 10);
    assert(!memcmp(b, "456789CDEF", 10));
    free(ring.buf);

    /* The radio's decoder on a 1 s, 440 Hz MP3: the second at 44.1 kHz (plus encoder padding), ~880 sign changes. */
    char path[256];
    snprintf(path, sizeof path, "%s/sine-440hz.mp3", dir);
    size_t len;
    char *mp3 = slurp(path, &len);
    assert(mp3);
    kg_radio_t r;
    assert(kg_radio_init(&r, &MEDIA_ENV, "Test", "http://example.com/", KG_RADIO_VOL_MAX));
    assert(kg_ring_write(&r.ring, (const uint8_t *)mp3, (uint32_t)len) == len);
    r.net_done = true;
    kg_radio_play_loop(&r);
    printf("media: MP3 → %zu samples at %u Hz, %d zero crossings\n", pcm_n, pcm_rate, pcm_crossings);
    assert(pcm_rate == 44100 && pcm_n > 44100 && pcm_n < 49000 && abs(pcm_crossings - 880) < 10);
    kg_radio_free(&r);
    free(mp3);

    /* MJPEG: two copies of a JPEG between multipart headers, fed in small pieces. */
    snprintf(path, sizeof path, "%s/smpte-320x240.jpg", dir);
    char *jpg = slurp(path, &len);
    assert(jpg);
    size_t total = 2 * (len + 64);
    uint8_t *stream = malloc(total), *p = stream;
    for (int k = 0; k < 2; k++) {
        p += sprintf((char *)p, "--frame\r\nContent-Type: image/jpeg\r\n\r\n");
        memcpy(p, jpg, len), p += len;
        p += sprintf((char *)p, "\r\n");
    }
    kg_mjpeg_t mj = {malloc(64 * 1024), 64 * 1024, 0, false, false, 0};
    int frames = 0;
    for (const uint8_t *q = stream; q < p;) {
        size_t chunk = (size_t)(p - q) < 777 ? (size_t)(p - q) : 777, used;
        while (chunk) {
            if (kg_mjpeg_feed(&mj, q, chunk, &used)) {
                assert(mj.len == len && !memcmp(mj.buf, jpg, len));
                frames++;
            }
            q += used, chunk -= used;
        }
    }
    assert(frames == 2);

    /* 320 × 240 → 240 × 240: unscaled, cropped by 40 px each side. SMPTE bars: the first
     * visible bar (x = 40 of the source) is white-ish grey, the second one yellow. */
    uint16_t *fb = malloc(KG_VIDEO_W * KG_VIDEO_H * 2), w, h;
    void *work = malloc(KG_JPEG_WORK);
    assert(kg_jpeg_decode(mj.buf, mj.len, fb, false, work, &w, &h) && w == 320 && h == 240);
    uint16_t grey = fb[60 * KG_VIDEO_W + 5], yellow = fb[60 * KG_VIDEO_W + 30];
    int gr = grey >> 11 << 3, gg = (grey >> 5 & 63) << 2, gb = (grey & 31) << 3;
    int yr = yellow >> 11 << 3, yg = (yellow >> 5 & 63) << 2, yb = (yellow & 31) << 3;
    printf("media: JPEG %ux%u, pixels (%d,%d,%d) and (%d,%d,%d)\n", w, h, gr, gg, gb, yr, yg, yb);
    assert(gr > 150 && gg > 150 && gb > 150 && yr > 150 && yg > 150 && yb < 80);
    free(fb), free(work), free(mj.buf), free(stream), free(jpg);
}

int main(int argc, char **argv)
{
    test_ancs();
    test_net();
    test_tz();
    test_sky();
    test_ir();
    {
        char dir[256];
        snprintf(dir, sizeof dir, "%s", argv[1]);
        char *slash = strrchr(dir, '/');
        if (slash) *slash = 0;
        test_media(dir);
    }
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
