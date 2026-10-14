/*
 * Manual check of the network primitives against the real network (macOS / Linux, no root):
 *   cc -Wall -Wextra -Werror -I src tests/net_probe.c src/data/kg_net.c -o /tmp/kg_net
 *   /tmp/kg_net trace 1.1.1.1 | ping 1.1.1.1 | ports 1.1.1.1 | dns example.com | speed | pubip
 */
#include "data/kg_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool count_sink(const uint8_t *d, size_t n, void *ctx)
{
    (void)d;
    *(size_t *)ctx += n;
    return true;
}

static bool text_sink(const uint8_t *d, size_t n, void *ctx)
{
    char *s = ctx;
    size_t l = strlen(s);
    if (l + n > 511) n = 511 - l;
    memcpy(s + l, d, n);
    s[l + n] = 0;
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    const char *cmd = argv[1], *host = argc > 2 ? argv[2] : "1.1.1.1";
    char a[16], b[16];
    uint32_t ip, ms;
    if (!strcmp(cmd, "dns")) {
        uint32_t ips[6];
        int n = kg_resolve(host, ips, 6, &ms);
        printf("%d address(es) in %u ms\n", n, ms);
        for (int i = 0; i < n; i++) printf("  %s\n", kg_ip_str(ips[i], a));
        return n ? 0 : 1;
    }
    if (!strcmp(cmd, "speed")) {
        size_t bytes = 0;
        uint32_t ttfb;
        int64_t t0 = kg_net_us();
        int st = kg_http_get("speed.cloudflare.com", "/__down?bytes=5000000", count_sink, &bytes, 15000, &ttfb, NULL);
        double s = (kg_net_us() - t0) / 1e6;
        printf("status %d, %zu bytes in %.2f s = %.1f Mbit/s, ttfb %u ms\n", st, bytes, s, bytes * 8 / s / 1e6, ttfb / 1000);
        return st == 200 ? 0 : 1;
    }
    if (!strcmp(cmd, "pubip")) {
        char body[512] = "", f[64];
        int st = kg_http_get("ip-api.com", "/json/?fields=status,query,isp,as,city,country", text_sink, body, 8000, NULL, NULL);
        printf("status %d\n", st);
        const char *keys[] = {"isp", "as", "city", "country"};
        for (int i = 0; i < 4; i++)
            if (kg_json_str(body, keys[i], f, sizeof f)) printf("  %s: %s\n", keys[i], f);
        printf("  query present: %d\n", kg_json_str(body, "query", f, sizeof f));
        return st == 200 ? 0 : 1;
    }
    if (kg_resolve(host, &ip, 1, &ms) != 1) {
        printf("cannot resolve %s\n", host);
        return 1;
    }
    if (!strcmp(cmd, "ports")) {
        static const uint16_t P[] = {22, 53, 80, 443, 8080};
        for (int i = 0; i < 5; i++) {
            uint32_t us;
            kg_port_t r = kg_tcp_probe(ip, P[i], 1500, &us);
            printf("%5u %-6s %s %u ms\n", P[i], kg_port_name(P[i]), (const char *[]){"filtered", "open", "closed", "error"}[r], us / 1000);
        }
        return 0;
    }
    int fd = kg_icmp_open();
    if (fd < 0) {
        perror("icmp socket");
        return 1;
    }
    bool trace = !strcmp(cmd, "trace");
    for (int i = 1; i <= (trace ? 20 : 5); i++) {
        kg_probe_t r = kg_icmp_probe(fd, ip, trace ? (uint8_t)i : 64, 0x4b47, (uint16_t)i, 1500);
        static const char *K[] = {"timeout", "reply", "ttl-exceeded", "unreachable", "error"};
        printf("%2d %-12s %-15s %.1f ms\n", i, K[r.kind], r.from ? kg_ip_str(r.from, b) : "*", r.rtt_us / 1000.0);
        if (trace && (r.kind == KG_PROBE_REPLY || r.kind == KG_PROBE_UNREACH)) break;
    }
    kg_icmp_close(fd);
    return 0;
}
