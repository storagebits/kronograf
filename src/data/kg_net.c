#include "kg_net.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(ESP_PLATFORM)
#include <esp_timer.h>
int64_t kg_net_us(void) { return esp_timer_get_time(); }
#define KG_ICMP_SOCK SOCK_RAW   /* lwIP: raw sockets deliver ICMP errors too */
#else
#include <time.h>
int64_t kg_net_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}
#define KG_ICMP_SOCK SOCK_DGRAM /* unprivileged ICMP sockets */
#endif

/* ------------------------------------------------------------- addresses */

bool kg_ip_parse(const char *s, uint32_t *ip)
{
    unsigned a[4];
    char tail;
    if (!s || sscanf(s, "%u.%u.%u.%u%c", &a[0], &a[1], &a[2], &a[3], &tail) != 4) return false;
    for (int i = 0; i < 4; i++)
        if (a[i] > 255) return false;
    *ip = htonl(a[0] << 24 | a[1] << 16 | a[2] << 8 | a[3]);
    return true;
}

char *kg_ip_str(uint32_t ip, char *out)
{
    uint32_t h = ntohl(ip);
    snprintf(out, 16, "%u.%u.%u.%u", (unsigned)(h >> 24), (unsigned)(h >> 16 & 255), (unsigned)(h >> 8 & 255),
             (unsigned)(h & 255));
    return out;
}

/* --------------------------------------------------------------- subnets */

int kg_mask_prefix(uint32_t mask)
{
    uint32_t h = ntohl(mask);
    int n = 0;
    while (n < 32 && (h & 0x80000000u >> n)) n++;
    return (n == 32 || (h << n) == 0) ? n : -1;
}

void kg_subnet_calc(uint32_t ip, uint8_t prefix, kg_subnet_t *out)
{
    if (prefix > 32) prefix = 32;
    uint32_t h = ntohl(ip), m = prefix ? 0xFFFFFFFFu << (32 - prefix) : 0;
    uint32_t net = h & m, bc = net | ~m;
    memset(out, 0, sizeof *out);
    out->ip = ip;
    out->prefix = prefix;
    out->mask = htonl(m);
    out->network = htonl(net);
    out->broadcast = htonl(bc);
    if (prefix >= 31) { /* point-to-point (RFC 3021) and single host */
        out->first = htonl(net);
        out->last = htonl(bc);
        out->hosts = prefix == 32 ? 1 : 2;
    } else {
        out->first = htonl(net + 1);
        out->last = htonl(bc - 1);
        out->hosts = bc - net - 1;
    }
    uint8_t a = h >> 24, b = h >> 16 & 255;
    out->is_private = a == 10 || (a == 172 && (b & 0xF0) == 16) || (a == 192 && b == 168) ||
                      (a == 100 && (b & 0xC0) == 64) || (a == 169 && b == 254) || a == 127;
}

bool kg_subnet_parse(const char *s, kg_subnet_t *out)
{
    char ip_s[20], rest[20] = "";
    unsigned prefix = 24;
    int n = sscanf(s, " %19[0-9.] %19s", ip_s, rest);
    if (n < 1) return false;
    uint32_t ip;
    if (!kg_ip_parse(ip_s, &ip)) return false;
    if (n == 2 && rest[0] == '/') { /* "10.0.0.7 /22" */
        if (sscanf(rest + 1, "%u", &prefix) != 1 || prefix > 32) return false;
    } else if (n == 2) {
        uint32_t mask;
        int p;
        if (!kg_ip_parse(rest, &mask) || (p = kg_mask_prefix(mask)) < 0) return false;
        prefix = (unsigned)p;
    } else {
        const char *slash = strchr(s, '/');
        if (slash && (sscanf(slash + 1, "%u", &prefix) != 1 || prefix > 32)) return false;
    }
    kg_subnet_calc(ip, (uint8_t)prefix, out);
    return true;
}

/* ------------------------------------------------------------------ ICMP */

static uint16_t checksum(const uint8_t *p, size_t n)
{
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < n; i += 2) sum += (uint32_t)(p[i] << 8 | p[i + 1]);
    if (n & 1) sum += (uint32_t)p[n - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

int kg_icmp_open(void) { return socket(AF_INET, KG_ICMP_SOCK, IPPROTO_ICMP); }

void kg_icmp_close(int fd)
{
    if (fd >= 0) close(fd);
}

/* Skips the IPv4 header if present (raw sockets and macOS include it, Linux does not:
 * an ICMP message never starts with 0x4_). */
static const uint8_t *skip_ip(const uint8_t *b, size_t *n)
{
    if (*n >= 20 && (b[0] >> 4) == 4) {
        size_t ihl = (size_t)(b[0] & 15) * 4;
        if (ihl > *n) return NULL;
        *n -= ihl;
        return b + ihl;
    }
    return b;
}

kg_probe_t kg_icmp_probe(int fd, uint32_t dst, uint8_t ttl, uint16_t id, uint16_t seq, uint32_t timeout_ms)
{
    kg_probe_t r = {KG_PROBE_ERROR, 0, 0};
    if (fd < 0) return r;
    int t = ttl;
    setsockopt(fd, IPPROTO_IP, IP_TTL, &t, sizeof t);

    uint8_t pkt[8 + 32];
    memset(pkt, 0, sizeof pkt);
    pkt[0] = 8; /* echo request */
    pkt[4] = id >> 8, pkt[5] = id & 255, pkt[6] = seq >> 8, pkt[7] = seq & 255;
    memcpy(pkt + 8, "Kronograf network toolbox probe.", 32);
    uint16_t c = checksum(pkt, sizeof pkt);
    pkt[2] = c >> 8, pkt[3] = c & 255;

    struct sockaddr_in to = {0};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = dst;
    int64_t t0 = kg_net_us();
    if (sendto(fd, pkt, sizeof pkt, 0, (struct sockaddr *)&to, sizeof to) < 0) return r;

    r.kind = KG_PROBE_TIMEOUT;
    int64_t deadline = t0 + (int64_t)timeout_ms * 1000;
    for (;;) {
        int64_t left = deadline - kg_net_us();
        if (left <= 0) return r;
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(fd, &rd);
        struct timeval tv = {(long)(left / 1000000), (int)(left % 1000000)};
        if (select(fd + 1, &rd, NULL, NULL, &tv) <= 0) return r;
        uint8_t buf[256];
        struct sockaddr_in from;
        socklen_t fl = sizeof from;
        ssize_t got = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
        if (got <= 0) continue;
        int64_t now = kg_net_us();
        size_t n = (size_t)got;
        const uint8_t *icmp = skip_ip(buf, &n);
        if (!icmp || n < 8) continue;
        if (icmp[0] == 0) { /* echo reply: ours if id and sequence match (Linux rewrites the id) */
#if defined(__linux__)
            bool id_ok = true;
#else
            bool id_ok = (icmp[4] << 8 | icmp[5]) == id;
#endif
            if (!id_ok || (icmp[6] << 8 | icmp[7]) != seq || from.sin_addr.s_addr != dst) continue;
            r.kind = KG_PROBE_REPLY;
        } else if (icmp[0] == 11 || icmp[0] == 3) {
            /* Time exceeded / unreachable: quotes our IP header + the first 8 bytes of the echo. */
            size_t qn = n - 8;
            const uint8_t *q = skip_ip(icmp + 8, &qn);
            if (!q || qn < 8 || q[0] != 8 || (q[6] << 8 | q[7]) != seq) continue;
            r.kind = icmp[0] == 11 ? KG_PROBE_TTL : KG_PROBE_UNREACH;
        } else {
            continue;
        }
        r.from = from.sin_addr.s_addr;
        r.rtt_us = (uint32_t)(now - t0);
        return r;
    }
}

/* ------------------------------------------------------------------- TCP */

/* Waits for a non-blocking connect(): 1 connected, 0 refused, -1 timeout. */
static int wait_connect(int fd, uint32_t timeout_ms)
{
    fd_set wr;
    FD_ZERO(&wr);
    FD_SET(fd, &wr);
    struct timeval tv = {(long)(timeout_ms / 1000), (int)(timeout_ms % 1000) * 1000};
    if (select(fd + 1, NULL, &wr, NULL, &tv) <= 0) return -1;
    int err = 0;
    socklen_t el = sizeof err;
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
    if (err == 0) return 1;
    return err == ECONNREFUSED || err == ECONNRESET ? 0 : -1;
}

static int tcp_connect(uint32_t ip, uint16_t port, uint32_t timeout_ms, int *state)
{
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        *state = -2;
        return -1;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    struct sockaddr_in to = {0};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = ip;
    int rc = connect(fd, (struct sockaddr *)&to, sizeof to);
    if (rc == 0) *state = 1;
    else if (errno == EINPROGRESS) *state = wait_connect(fd, timeout_ms);
    else *state = errno == ECONNREFUSED ? 0 : -2;
    return fd;
}

kg_port_t kg_tcp_probe(uint32_t ip, uint16_t port, uint32_t timeout_ms, uint32_t *rtt_us)
{
    int64_t t0 = kg_net_us();
    int state;
    int fd = tcp_connect(ip, port, timeout_ms, &state);
    if (rtt_us) *rtt_us = (uint32_t)(kg_net_us() - t0);
    if (fd >= 0) close(fd);
    return state == 1 ? KG_PORT_OPEN : state == 0 ? KG_PORT_CLOSED : state == -1 ? KG_PORT_FILTERED : KG_PORT_ERROR;
}

const char *kg_port_name(uint16_t port)
{
    static const struct { uint16_t port; const char *name; } K[] = {
        {21, "ftp"}, {22, "ssh"}, {23, "telnet"}, {25, "smtp"}, {53, "dns"}, {80, "http"}, {110, "pop3"},
        {123, "ntp"}, {143, "imap"}, {161, "snmp"}, {179, "bgp"}, {389, "ldap"}, {443, "https"}, {445, "smb"},
        {465, "smtps"}, {554, "rtsp"}, {587, "submit"}, {631, "ipp"}, {853, "dot"}, {993, "imaps"},
        {995, "pop3s"}, {1883, "mqtt"}, {3306, "mysql"}, {3389, "rdp"}, {5000, "upnp"}, {5432, "pgsql"},
        {5900, "vnc"}, {8080, "http-alt"}, {8443, "https-alt"}, {9100, "jetdirect"},
    };
    for (size_t i = 0; i < sizeof K / sizeof K[0]; i++)
        if (K[i].port == port) return K[i].name;
    return "";
}

/* ------------------------------------------------------------------- DNS */

int kg_resolve(const char *host, uint32_t *ips, int max, uint32_t *ms)
{
    int64_t t0 = kg_net_us();
    if (ms) *ms = 0;
    if (max > 0 && kg_ip_parse(host, &ips[0])) return 1;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo(host, NULL, &hints, &res);
    if (ms) *ms = (uint32_t)((kg_net_us() - t0) / 1000);
    if (rc != 0 || !res) return 0;
    int n = 0;
    for (struct addrinfo *a = res; a && n < max; a = a->ai_next) {
        uint32_t ip = ((struct sockaddr_in *)a->ai_addr)->sin_addr.s_addr;
        bool dup = false;
        for (int k = 0; k < n; k++) dup |= ips[k] == ip;
        if (!dup) ips[n++] = ip;
    }
    freeaddrinfo(res);
    return n;
}

/* ------------------------------------------------------------------ HTTP */

int kg_http_get(const char *host, const char *path, kg_http_sink_t sink, void *ctx, uint32_t timeout_ms,
                uint32_t *ttfb_us, volatile bool *cancel)
{
    uint32_t ip;
    if (ttfb_us) *ttfb_us = 0;
    if (kg_resolve(host, &ip, 1, NULL) != 1) return -1;
    int state;
    int fd = tcp_connect(ip, 80, timeout_ms < 5000 ? timeout_ms : 5000, &state);
    if (fd < 0 || state != 1) {
        if (fd >= 0) close(fd);
        return -2;
    }
    char req[256];
    int len = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: Kronograf\r\nAccept: */*\r\n\r\n",
                       path, host);
    int64_t t0 = kg_net_us(), deadline = t0 + (int64_t)timeout_ms * 1000;
    if (len <= 0 || len >= (int)sizeof req || send(fd, req, (size_t)len, 0) != len) {
        close(fd);
        return -2;
    }

    char line[64]; /* status line */
    size_t ll = 0;
    int status = -3;
    uint32_t crlf = 0; /* last 4 header bytes, to spot the blank line */
    bool in_body = false;
    uint8_t buf[1460];
    for (;;) {
        if (cancel && *cancel) break;
        int64_t left = deadline - kg_net_us();
        if (left <= 0) break;
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(fd, &rd);
        struct timeval tv = {(long)(left / 1000000), (int)(left % 1000000)};
        if (left > 250000) tv.tv_sec = 0, tv.tv_usec = 250000; /* wakes up to check `cancel` */
        int s = select(fd + 1, &rd, NULL, NULL, &tv);
        if (s < 0) break;
        if (s == 0) continue;
        ssize_t got = recv(fd, buf, sizeof buf, 0);
        if (got <= 0) break; /* end of the response (HTTP/1.0: connection closed) */
        if (ttfb_us && !*ttfb_us) *ttfb_us = (uint32_t)(kg_net_us() - t0);
        const uint8_t *p = buf;
        size_t n = (size_t)got;
        while (!in_body && n) { /* headers, of any length: only the status line is kept */
            uint8_t ch = *p++;
            n--;
            if (status == -3 && ll < sizeof line - 1 && ch != '\r' && ch != '\n') line[ll++] = (char)ch;
            if (status == -3 && ch == '\n') {
                line[ll] = 0;
                int code;
                status = sscanf(line, "HTTP/%*d.%*d %d", &code) == 1 ? code : -4;
            }
            crlf = crlf << 8 | ch;
            in_body = crlf == 0x0D0A0D0A;
        }
        if (status == -4) break;
        if (n && !sink(p, n, ctx)) break;
    }
    close(fd);
    return status;
}

bool kg_json_str(const char *json, const char *key, char *out, size_t n)
{
    char pat[40];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p || !n) return false;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < n) {
        if (*p == '\\' && p[1]) p++;
        out[i++] = *p++;
    }
    out[i] = 0;
    return true;
}
