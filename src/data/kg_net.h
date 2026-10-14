/*
 * Network toolbox primitives, plain C over BSD sockets: the same code runs on the watch
 * (lwIP) and on the Mac (simulator, host tests).
 *   - ICMP echo with a chosen TTL (ping, traceroute)
 *   - TCP connect probe (port check)
 *   - DNS lookup
 *   - streamed HTTP/1.0 GET (speed test, public IP)
 *   - IPv4 / CIDR parsing and subnet arithmetic
 * Addresses are uint32_t in network byte order (struct in_addr.s_addr).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* "192.168.1.20" → address; false if not a dotted quad. */
bool kg_ip_parse(const char *s, uint32_t *ip);
/* Address → "192.168.1.20" (out: at least 16 bytes). */
char *kg_ip_str(uint32_t ip, char *out);

/* --------------------------------------------------------------- subnets */

typedef struct {
    uint32_t ip, mask, network, broadcast, first, last; /* network byte order */
    uint8_t prefix;
    uint32_t hosts;      /* usable hosts (/31: 2, /32: 1) */
    bool is_private;     /* RFC 1918, CGNAT (100.64/10), link-local or loopback */
} kg_subnet_t;

/* "10.0.0.7/22", "10.0.0.7 255.255.252.0" or "10.0.0.7" (/24 assumed). */
bool kg_subnet_parse(const char *s, kg_subnet_t *out);
void kg_subnet_calc(uint32_t ip, uint8_t prefix, kg_subnet_t *out);
/* Netmask → prefix length, -1 if the mask is not contiguous. */
int kg_mask_prefix(uint32_t mask);

/* ------------------------------------------------------------------ ICMP */

typedef enum {
    KG_PROBE_TIMEOUT = 0,
    KG_PROBE_REPLY,      /* echo reply from the destination */
    KG_PROBE_TTL,        /* time exceeded: a router on the path */
    KG_PROBE_UNREACH,    /* destination unreachable */
    KG_PROBE_ERROR,      /* socket error */
} kg_probe_kind_t;

typedef struct {
    kg_probe_kind_t kind;
    uint32_t from;       /* who answered */
    uint32_t rtt_us;
} kg_probe_t;

/* ICMP socket (raw on lwIP, datagram on macOS / Linux, no root needed): -1 on failure. */
int kg_icmp_open(void);
void kg_icmp_close(int fd);
/* One echo request with the given TTL, waits up to timeout_ms for the matching answer. */
kg_probe_t kg_icmp_probe(int fd, uint32_t dst, uint8_t ttl, uint16_t id, uint16_t seq, uint32_t timeout_ms);

/* ------------------------------------------------------------------- TCP */

typedef enum { KG_PORT_FILTERED = 0, KG_PORT_OPEN, KG_PORT_CLOSED, KG_PORT_ERROR } kg_port_t;
kg_port_t kg_tcp_probe(uint32_t ip, uint16_t port, uint32_t timeout_ms, uint32_t *rtt_us);
/* Usual service name of a port ("ssh", "https"…), "" if unknown. */
const char *kg_port_name(uint16_t port);

/* ------------------------------------------------------------------- DNS */

/* IPv4 addresses of `host` (or the address itself if it is a dotted quad). Returns the
 * count (0: not found), and the lookup time in *ms. */
int kg_resolve(const char *host, uint32_t *ips, int max, uint32_t *ms);

/* ------------------------------------------------------------------ HTTP */

/* "http://host[:port]/path" → parts; false for anything else (https:// included). */
typedef struct {
    char host[64];
    uint16_t port;
    char path[192];
} kg_url_t;
bool kg_url_parse(const char *url, kg_url_t *u);

/* Body sink: return false to stop the transfer. */
typedef bool (*kg_http_sink_t)(const uint8_t *data, size_t len, void *ctx);
/* Plain HTTP/1.0 GET (no TLS: speed test and public-IP services only). Returns the HTTP
 * status, or a negative value: -1 DNS, -2 connection, -3 timeout, -4 malformed. *ttfb_us is
 * the time to the first byte of the response. `cancel` may be NULL. */
int kg_http_get(const char *host, const char *path, kg_http_sink_t sink, void *ctx, uint32_t timeout_ms,
                uint32_t *ttfb_us, volatile bool *cancel);

/* General HTTP/1.0 GET for streams: port, up to 3 redirects, "ICY 200" status lines,
 * extra request headers ("Icy-MetaData: 1\r\n"), response headers through a callback,
 * an idle timeout (no data for idle_ms ends it) and an optional total timeout (0: none).
 * Returns the final HTTP status or a negative error as kg_http_get(); -5 too many
 * redirects or a redirect to https, -6 unsupported URL. */
typedef struct {
    const char *url;
    const char *headers;
    void (*on_header)(const char *name, const char *value, void *ctx);
    kg_http_sink_t sink;
    void *ctx;
    uint32_t timeout_ms, idle_ms;
    uint32_t *ttfb_us;
    volatile bool *cancel;
} kg_http_req_t;
int kg_http_request(const kg_http_req_t *r);

/* Copies the string value of "key" from a flat JSON object; false if absent. */
bool kg_json_str(const char *json, const char *key, char *out, size_t n);

/* Monotonic microseconds. */
int64_t kg_net_us(void);

#ifdef __cplusplus
}
#endif
