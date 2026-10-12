#include "kg_bus.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------- protobuf en mémoire */

typedef struct {
    const uint8_t *p, *end;
} pb_t;

static bool pb_varint(pb_t *b, uint64_t *v)
{
    uint64_t x = 0;
    for (int shift = 0; b->p < b->end && shift < 64; shift += 7) {
        uint8_t c = *b->p++;
        x |= (uint64_t)(c & 0x7F) << shift;
        if (!(c & 0x80)) {
            *v = x;
            return true;
        }
    }
    return false;
}

/* Champ suivant : numéro, type, et pour les champs « longueur » la sous-zone. */
static bool pb_next(pb_t *b, uint32_t *field, uint32_t *wire, uint64_t *val, pb_t *sub)
{
    uint64_t tag;
    if (b->p >= b->end || !pb_varint(b, &tag)) return false;
    *field = (uint32_t)(tag >> 3);
    *wire = (uint32_t)(tag & 7);
    switch (*wire) {
    case 0: return pb_varint(b, val);
    case 1:
        if (b->end - b->p < 8) return false;
        b->p += 8;
        return true;
    case 2: {
        uint64_t len;
        if (!pb_varint(b, &len) || (uint64_t)(b->end - b->p) < len) return false;
        *sub = (pb_t){b->p, b->p + len};
        b->p += len;
        return true;
    }
    case 5:
        if (b->end - b->p < 4) return false;
        b->p += 4;
        return true;
    default: return false;
    }
}

static void pb_str(pb_t s, char *out, size_t n)
{
    size_t len = (size_t)(s.end - s.p);
    if (len >= n) len = n - 1;
    memcpy(out, s.p, len);
    out[len] = 0;
}

/* -------------------------------------------------------------- GTFS-RT */

static void add_dep(kg_gtfs_t *g, const char *route, const char *stop, time_t t)
{
    kg_bus_t *o = &g->out;
    if (t < g->not_before) return;
    /* Insertion triée ; au-delà de la capacité, on garde les plus proches. */
    int i = o->n;
    if (o->n == KG_BUS_MAX_DEPS) {
        if (t >= o->deps[KG_BUS_MAX_DEPS - 1].t) return;
        i = KG_BUS_MAX_DEPS - 1;
    } else {
        o->n++;
    }
    while (i > 0 && o->deps[i - 1].t > t) {
        o->deps[i] = o->deps[i - 1];
        i--;
    }
    kg_bus_dep_t *d = &o->deps[i];
    snprintf(d->route, sizeof d->route, "%s", route);
    snprintf(d->stop, sizeof d->stop, "%s", stop);
    d->t = t;
}

static time_t event_time(pb_t m)
{
    uint32_t f, w;
    uint64_t v = 0;
    pb_t sub;
    while (pb_next(&m, &f, &w, &v, &sub))
        if (f == 2 && w == 0) return (time_t)(int64_t)v;
    return 0;
}

static int watched(const kg_gtfs_t *g, const char *stop)
{
    for (int i = 0; i < g->n_stops; i++)
        if (!strcmp(g->stops[i], stop)) return i;
    return -1;
}

static void parse_trip_update(kg_gtfs_t *g, pb_t tu)
{
    char route[8] = "";
    uint32_t f, w;
    uint64_t v;
    pb_t sub, scan = tu;
    /* Le trajet (route_id) peut venir après les arrêts : premier passage pour lui. */
    while (pb_next(&scan, &f, &w, &v, &sub)) {
        if (f != 1 || w != 2) continue;
        pb_t trip = sub, s2;
        while (pb_next(&trip, &f, &w, &v, &s2))
            if (f == 5 && w == 2) pb_str(s2, route, sizeof route);
    }
    while (pb_next(&tu, &f, &w, &v, &sub)) {
        if (f != 2 || w != 2) continue;
        pb_t stu = sub, s2;
        char stop[12] = "";
        time_t arr = 0, dep = 0;
        while (pb_next(&stu, &f, &w, &v, &s2)) {
            if (f == 4 && w == 2) pb_str(s2, stop, sizeof stop);
            else if (f == 2 && w == 2) arr = event_time(s2);
            else if (f == 3 && w == 2) dep = event_time(s2);
        }
        if (stop[0] && watched(g, stop) >= 0 && (dep || arr)) add_dep(g, route, stop, dep ? dep : arr);
    }
}

static void parse_message(kg_gtfs_t *g, uint32_t field, pb_t m)
{
    uint32_t f, w;
    uint64_t v;
    pb_t sub;
    if (field == 1) { /* FeedHeader : horodatage (3) */
        while (pb_next(&m, &f, &w, &v, &sub))
            if (f == 3 && w == 0) g->out.feed_ts = (time_t)v;
        return;
    }
    g->entities++;
    while (pb_next(&m, &f, &w, &v, &sub)) /* FeedEntity : trip_update (3) */
        if (f == 3 && w == 2) parse_trip_update(g, sub);
}

void kg_gtfs_init(kg_gtfs_t *g, uint8_t *buf, size_t cap, const char *stops, time_t now)
{
    memset(g, 0, sizeof *g);
    g->buf = buf;
    g->cap = cap;
    g->not_before = now - 60;
    while (stops && *stops && g->n_stops < KG_BUS_MAX_STOPS) {
        while (*stops == ',' || *stops == ' ') stops++;
        size_t n = strcspn(stops, ", ");
        if (n && n < sizeof g->stops[0]) {
            memcpy(g->stops[g->n_stops], stops, n);
            g->stops[g->n_stops][n] = 0;
            g->n_stops++;
        }
        stops += n;
    }
}

/* Niveau supérieur (FeedMessage) en flux : tag, longueur, puis une entité entière en tampon. */
void kg_gtfs_feed(kg_gtfs_t *g, const uint8_t *data, size_t n)
{
    while (n) {
        if (g->phase <= 1) { /* varint : tag puis longueur */
            uint8_t c = *data++;
            n--;
            g->varint |= (uint64_t)(c & 0x7F) << g->shift;
            g->shift += 7;
            if (c & 0x80) continue;
            uint64_t v = g->varint;
            g->varint = 0;
            g->shift = 0;
            if (g->phase == 0) {
                g->field = (uint32_t)(v >> 3);
                g->wire = (uint32_t)(v & 7);
                if (g->wire != 2) { /* rien d'autre au niveau supérieur : flux inattendu */
                    g->phase = 3;
                    g->need = SIZE_MAX;
                    continue;
                }
                g->phase = 1;
            } else {
                g->need = (size_t)v;
                g->have = 0;
                g->phase = g->need <= g->cap ? 2 : 3;
                if (g->phase == 3) g->skipped++;
                if (g->need == 0) g->phase = 0;
            }
            continue;
        }
        size_t k = g->need - g->have < n ? g->need - g->have : n;
        if (g->phase == 2) memcpy(g->buf + g->have, data, k);
        g->have += k;
        data += k;
        n -= k;
        if (g->have == g->need) {
            if (g->phase == 2) parse_message(g, g->field, (pb_t){g->buf, g->buf + g->need});
            g->phase = 0;
        }
    }
}

const kg_bus_t *kg_gtfs_finish(kg_gtfs_t *g)
{
    g->out.valid = g->entities > 0;
    return &g->out;
}
