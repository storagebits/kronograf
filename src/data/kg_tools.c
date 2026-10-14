#include "kg_tools.h"
#include "kg_net.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define LOCK() env->lock()
#define UNLOCK() env->unlock()
#define CANCELLED() (*env->cancel)

/* ------------------------------------------------------------- helpers */

char *kg_fmt_rtt(uint32_t us, char *out, int n)
{
    if (us < 100000) snprintf(out, (size_t)n, "%.1f ms", us / 1000.0);
    else snprintf(out, (size_t)n, "%lu ms", (unsigned long)(us / 1000));
    return out;
}

char *kg_fmt_mac(const uint8_t m[6], char *out)
{
    snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
    return out;
}

const char *kg_ble_company(uint16_t id)
{
    static const struct { uint16_t id; const char *name; } K[] = {
        {0x0006, "Microsoft"}, {0x004C, "Apple"}, {0x0059, "Nordic"}, {0x0075, "Samsung"}, {0x0087, "Garmin"},
        {0x009E, "Bose"}, {0x00E0, "Google"}, {0x012D, "Sony"}, {0x0171, "Amazon"}, {0x02E5, "Espressif"},
        {0x038F, "Xiaomi"},
    };
    for (size_t i = 0; i < sizeof K / sizeof K[0]; i++)
        if (K[i].id == id) return K[i].name;
    return NULL;
}

void kg_tool_begin(kg_tool_state_t *st, kg_tool_t tool, const char *target)
{
    uint32_t seq = st->seq;
    memset(st, 0, sizeof *st);
    st->seq = seq + 1;
    st->tool = tool;
    st->running = true;
    snprintf(st->target, sizeof st->target, "%s", target ? target : "");
}

void kg_tool_status(kg_tool_state_t *st, const kg_tools_env_t *env, bool failed, const char *fmt, ...)
{
    LOCK();
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(st->status, sizeof st->status, fmt, ap);
    va_end(ap);
    st->failed = failed;
    st->seq++;
    UNLOCK();
}

void kg_tool_add_row(kg_tool_state_t *st, const kg_tools_env_t *env, const kg_tool_row_t *row, bool sorted)
{
    LOCK();
    int at = st->n;
    if (sorted)
        for (at = 0; at < st->n && st->rows[at].v >= row->v; at++) {
        }
    if (at < KG_TOOL_ROWS) {
        int last = st->n < KG_TOOL_ROWS ? st->n : KG_TOOL_ROWS - 1;
        memmove(&st->rows[at + 1], &st->rows[at], (size_t)(last - at) * sizeof *row);
        st->rows[at] = *row;
        if (st->n < KG_TOOL_ROWS) st->n++;
    }
    st->seq++;
    UNLOCK();
}

void kg_tool_end(kg_tool_state_t *st, const kg_tools_env_t *env)
{
    LOCK();
    st->running = false;
    st->seq++;
    UNLOCK();
}

/* Sleeps `ms` in short steps; false if cancelled meanwhile. */
static bool nap(const kg_tools_env_t *env, uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 50) {
        if (CANCELLED()) return false;
        usleep(50000);
    }
    return !CANCELLED();
}

bool kg_tool_wait_link(kg_tool_state_t *st, const kg_tools_env_t *env)
{
    if (env->link_up()) return true;
    kg_tool_status(st, env, false, "Connecting to Wi-Fi…");
    for (int i = 0; i < 300 && !env->link_up(); i++) /* 15 s */
        if (!nap(env, 50)) return false;
    if (env->link_up()) return true;
    kg_tool_status(st, env, true, "Wi-Fi unreachable");
    return false;
}

/* Target → address (st->ip). "" and "gateway" mean the default gateway. */
static bool resolve_target(kg_tool_state_t *st, const kg_tools_env_t *env)
{
    char a[16];
    if (!st->target[0] || !strcmp(st->target, "gateway")) {
        uint32_t gw = env->gateway();
        if (!gw) {
            kg_tool_status(st, env, true, "No gateway");
            return false;
        }
        LOCK();
        st->ip = gw;
        snprintf(st->target, sizeof st->target, "gateway %s", kg_ip_str(gw, a));
        UNLOCK();
        return true;
    }
    kg_tool_status(st, env, false, "Resolving %s…", st->target);
    uint32_t ip, ms;
    if (kg_resolve(st->target, &ip, 1, &ms) != 1) {
        kg_tool_status(st, env, true, "Unknown host");
        return false;
    }
    LOCK();
    st->ip = ip;
    UNLOCK();
    return true;
}

/* ---------------------------------------------------------------- ping */

static void run_ping(kg_tool_state_t *st, const kg_tools_env_t *env)
{
    int fd = kg_icmp_open();
    if (fd < 0) {
        kg_tool_status(st, env, true, "No ICMP socket");
        return;
    }
    uint16_t id = (uint16_t)(kg_net_us() & 0xFFFF);
    kg_tool_status(st, env, false, "Pinging…");
    for (uint16_t seq = 1; seq <= 3600 && !CANCELLED(); seq++) {
        int64_t t0 = kg_net_us();
        kg_probe_t r = kg_icmp_probe(fd, st->ip, 64, id, seq, 1000);
        LOCK();
        st->sent++;
        int32_t v = -1;
        if (r.kind == KG_PROBE_REPLY) {
            st->recv++;
            st->last_us = r.rtt_us;
            st->sum_us += r.rtt_us;
            if (!st->min_us || r.rtt_us < st->min_us) st->min_us = r.rtt_us;
            if (r.rtt_us > st->max_us) st->max_us = r.rtt_us;
            v = (int32_t)r.rtt_us;
        }
        if (st->hist_n == KG_PING_HIST) memmove(st->hist, st->hist + 1, sizeof st->hist - sizeof st->hist[0]);
        else st->hist_n++;
        st->hist[st->hist_n - 1] = v;
        snprintf(st->status, sizeof st->status, "%u sent · %u %% loss", st->sent,
                 (unsigned)((st->sent - st->recv) * 100 / st->sent));
        st->seq++;
        UNLOCK();
        int64_t left = 1000 - (kg_net_us() - t0) / 1000;
        if (left > 0 && !nap(env, (uint32_t)left)) break;
    }
    kg_icmp_close(fd);
}

/* ---------------------------------------------------------- traceroute */

#define TRACE_MAX_HOPS 24

static void run_trace(kg_tool_state_t *st, const kg_tools_env_t *env)
{
    int fd = kg_icmp_open();
    if (fd < 0) {
        kg_tool_status(st, env, true, "No ICMP socket");
        return;
    }
    uint16_t id = (uint16_t)(kg_net_us() & 0xFFFF), seq = 0;
    bool reached = false;
    int ttl;
    char ip[16], rtt[16];
    for (ttl = 1; ttl <= TRACE_MAX_HOPS && !reached && !CANCELLED(); ttl++) {
        kg_tool_status(st, env, false, "Hop %d…", ttl);
        kg_probe_t r = {KG_PROBE_TIMEOUT, 0, 0};
        for (int tries = 0; tries < 2 && r.kind == KG_PROBE_TIMEOUT && !CANCELLED(); tries++)
            r = kg_icmp_probe(fd, st->ip, (uint8_t)ttl, id, ++seq, 1000);
        kg_tool_row_t row = {.v = -1, .kind = KG_ROW_DIM};
        if (r.kind == KG_PROBE_TTL || r.kind == KG_PROBE_REPLY || r.kind == KG_PROBE_UNREACH) {
            snprintf(row.a, sizeof row.a, "%2d  %s", ttl, kg_ip_str(r.from, ip));
            snprintf(row.b, sizeof row.b, "%s", kg_fmt_rtt(r.rtt_us, rtt, sizeof rtt));
            row.v = (int32_t)r.rtt_us;
            row.kind = r.kind == KG_PROBE_REPLY ? KG_ROW_GOOD : r.kind == KG_PROBE_UNREACH ? KG_ROW_WARN : KG_ROW_NORMAL;
            reached = r.kind != KG_PROBE_TTL;
        } else if (r.kind == KG_PROBE_ERROR) {
            kg_icmp_close(fd);
            kg_tool_status(st, env, true, "Send failed");
            return;
        } else {
            snprintf(row.a, sizeof row.a, "%2d  *", ttl);
            snprintf(row.b, sizeof row.b, "no reply");
        }
        kg_tool_add_row(st, env, &row, false);
    }
    kg_icmp_close(fd);
    if (CANCELLED()) kg_tool_status(st, env, false, "Stopped");
    else if (reached) kg_tool_status(st, env, false, "Reached in %d hops", ttl - 1);
    else kg_tool_status(st, env, true, "No answer within %d hops", TRACE_MAX_HOPS);
}

/* ----------------------------------------------------------------- DNS */

static void run_dns(kg_tool_state_t *st, const kg_tools_env_t *env)
{
    if (!st->target[0]) {
        kg_tool_status(st, env, true, "No host name");
        return;
    }
    kg_tool_status(st, env, false, "Looking up %s…", st->target);
    uint32_t ips[8], ms;
    int n = kg_resolve(st->target, ips, 8, &ms);
    char a[16];
    for (int i = 0; i < n; i++) {
        kg_tool_row_t row = {.v = i, .kind = KG_ROW_GOOD};
        snprintf(row.a, sizeof row.a, "A");
        kg_ip_str(ips[i], row.b);
        kg_tool_add_row(st, env, &row, false);
    }
    kg_tool_row_t srv = {.kind = KG_ROW_DIM};
    snprintf(srv.a, sizeof srv.a, "Server");
    snprintf(srv.b, sizeof srv.b, "%s", env->dns() ? kg_ip_str(env->dns(), a) : "—");
    kg_tool_add_row(st, env, &srv, false);
    if (n) {
        LOCK();
        st->ip = ips[0];
        UNLOCK();
        kg_tool_status(st, env, false, "%d address%s in %lu ms", n, n > 1 ? "es" : "", (unsigned long)ms);
    } else {
        kg_tool_status(st, env, true, "Not found (%lu ms)", (unsigned long)ms);
    }
}

/* --------------------------------------------------------------- ports */

static void run_ports(kg_tool_state_t *st, const kg_tools_env_t *env)
{
    static const uint16_t P[] = {22, 53, 80, 443, 21, 23, 25, 110, 143, 445, 554, 1883, 3389, 5900, 8080, 8443};
    int open = 0, done = 0;
    char rtt[16];
    for (size_t i = 0; i < sizeof P / sizeof P[0] && !CANCELLED(); i++) {
        kg_tool_status(st, env, false, "Port %u…", P[i]);
        uint32_t us;
        kg_port_t r = kg_tcp_probe(st->ip, P[i], 800, &us);
        kg_tool_row_t row = {.v = P[i]};
        snprintf(row.a, sizeof row.a, "%u  %s", P[i], kg_port_name(P[i]));
        if (r == KG_PORT_OPEN) {
            snprintf(row.b, sizeof row.b, "open · %s", kg_fmt_rtt(us, rtt, sizeof rtt));
            row.kind = KG_ROW_GOOD;
            open++;
        } else {
            snprintf(row.b, sizeof row.b, "%s", r == KG_PORT_CLOSED ? "closed" : r == KG_PORT_FILTERED ? "filtered" : "error");
            row.kind = r == KG_PORT_CLOSED ? KG_ROW_WARN : KG_ROW_DIM;
        }
        kg_tool_add_row(st, env, &row, false);
        done++;
    }
    kg_tool_status(st, env, false, "%d open of %d ports", open, done);
}

/* ---------------------------------------------------------- speed test */

#define SPEED_HOST "speed.cloudflare.com"
#define SPEED_PATH "/__down?bytes=50000000"
#define SPEED_SECONDS 10

typedef struct {
    kg_tool_state_t *st;
    const kg_tools_env_t *env;
    int64_t t_first, t_sample;
    uint32_t bytes_sample;
} speed_ctx_t;

static bool speed_sink(const uint8_t *data, size_t len, void *p)
{
    (void)data;
    speed_ctx_t *c = p;
    const kg_tools_env_t *env = c->env;
    kg_tool_state_t *st = c->st;
    int64_t now = kg_net_us();
    if (!c->t_first) c->t_first = c->t_sample = now;
    LOCK();
    st->bytes += (uint32_t)len;
    st->elapsed_ms = (uint32_t)((now - c->t_first) / 1000);
    if (now - c->t_sample >= 250000) { /* one sample every 250 ms */
        float mbps = (float)(st->bytes - c->bytes_sample) * 8.0f / (float)(now - c->t_sample);
        if (st->speed_n < KG_SPEED_HIST) st->speed_hist[st->speed_n++] = mbps;
        if (mbps > st->peak_mbps) st->peak_mbps = mbps;
        c->t_sample = now;
        c->bytes_sample = st->bytes;
    }
    if (st->elapsed_ms > 0) st->mbps = (float)st->bytes * 8.0f / (float)st->elapsed_ms / 1000.0f;
    st->seq++;
    UNLOCK();
    return st->elapsed_ms < SPEED_SECONDS * 1000;
}

static void run_speed(kg_tool_state_t *st, const kg_tools_env_t *env)
{
    kg_tool_status(st, env, false, "Downloading from Cloudflare…");
    speed_ctx_t c = {st, env, 0, 0, 0};
    uint32_t ttfb;
    int code = kg_http_get(SPEED_HOST, SPEED_PATH, speed_sink, &c, (SPEED_SECONDS + 5) * 1000, &ttfb, env->cancel);
    LOCK();
    st->ttfb_ms = ttfb / 1000;
    UNLOCK();
    if (code != 200) kg_tool_status(st, env, true, code == -1 ? "Cannot resolve " SPEED_HOST : "HTTP error %d", code);
    else if (CANCELLED()) kg_tool_status(st, env, false, "Stopped");
    else kg_tool_status(st, env, false, "%.1f MB in %.1f s", st->bytes / 1e6, st->elapsed_ms / 1000.0);
}

/* ----------------------------------------------------------- public IP */

static bool text_sink(const uint8_t *d, size_t n, void *ctx)
{
    char *s = ctx;
    size_t l = strlen(s);
    if (l + n > 511) n = 511 - l;
    memcpy(s + l, d, n);
    s[l + n] = 0;
    return l + n < 511;
}

static void run_public(kg_tool_state_t *st, const kg_tools_env_t *env)
{
    kg_tool_status(st, env, false, "Asking ip-api.com…");
    char body[512] = "";
    int code = kg_http_get("ip-api.com", "/json/?fields=status,query,isp,as,city,regionName,countryCode", text_sink, body,
                           8000, NULL, env->cancel);
    char v[48], v2[48];
    if (code != 200 || !kg_json_str(body, "query", v, sizeof v)) {
        kg_tool_status(st, env, true, "Public IP unavailable (%d)", code);
        return;
    }
    static const char *const KEYS[][2] = {{"query", "Public IP"}, {"isp", "Provider"}, {"as", "Network"}};
    for (int i = 0; i < 3; i++) {
        kg_tool_row_t row = {.v = i, .kind = i ? KG_ROW_NORMAL : KG_ROW_GOOD};
        snprintf(row.a, sizeof row.a, "%s", KEYS[i][1]);
        if (!kg_json_str(body, KEYS[i][0], row.b, sizeof row.b)) snprintf(row.b, sizeof row.b, "—");
        kg_tool_add_row(st, env, &row, false);
    }
    kg_tool_row_t loc = {.v = 3};
    snprintf(loc.a, sizeof loc.a, "Location");
    if (kg_json_str(body, "city", v, sizeof v) && kg_json_str(body, "countryCode", v2, sizeof v2))
        snprintf(loc.b, sizeof loc.b, "%.20s, %.8s", v, v2);
    kg_tool_add_row(st, env, &loc, false);
    kg_tool_status(st, env, false, "via ip-api.com");
}

/* ----------------------------------------------------------------- run */

void kg_tool_run(kg_tool_t tool, const char *target, kg_tool_state_t *st, const kg_tools_env_t *env)
{
    LOCK();
    kg_tool_begin(st, tool, target);
    UNLOCK();
    if (kg_tool_wait_link(st, env)) {
        bool needs_ip = tool == KG_TOOL_PING || tool == KG_TOOL_TRACE || tool == KG_TOOL_PORTS;
        if (!needs_ip || resolve_target(st, env)) {
            switch (tool) {
            case KG_TOOL_PING: run_ping(st, env); break;
            case KG_TOOL_TRACE: run_trace(st, env); break;
            case KG_TOOL_DNS: run_dns(st, env); break;
            case KG_TOOL_PORTS: run_ports(st, env); break;
            case KG_TOOL_SPEED: run_speed(st, env); break;
            case KG_TOOL_PUBLIC: run_public(st, env); break;
            default: kg_tool_status(st, env, true, "Not available"); break;
            }
        }
    }
    kg_tool_end(st, env);
}
