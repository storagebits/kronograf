/*
 * Network toolbox screens with their own layout: Wi-Fi analyser (2.4 GHz channel map +
 * network list), link details with the public IP, ping with an RTT history, download speed
 * test and the subnet calculator (offline).
 */
#include "kit.h"
#include "net_ui.h"
#include "theme.h"
#include "fonts/kg_fonts.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static lv_obj_t *kv_row(lv_obj_t *list, const char *key, lv_obj_t **value)
{
    lv_obj_t *r = lv_obj_create(list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 224, 24);
    lv_obj_set_style_bg_color(r, KG_C_SURFACE, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r, 6, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *k = kit_label(r, &kg_font_text_16, KG_C_TEXT_DIM, key);
    lv_obj_align(k, LV_ALIGN_LEFT_MID, 8, 0);
    *value = kit_label(r, &kg_font_text_16, KG_C_TEXT, "");
    lv_obj_align(*value, LV_ALIGN_RIGHT_MID, -8, 0);
    return r;
}

static lv_obj_t *column(lv_obj_t *scr, lv_coord_t y, lv_coord_t h)
{
    lv_obj_t *l = lv_obj_create(scr);
    lv_obj_remove_style_all(l);
    lv_obj_set_pos(l, 0, y);
    lv_obj_set_size(l, KG_W, h);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(l, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(l, 3, 0);
    lv_obj_set_style_pad_bottom(l, 6, 0);
    lv_obj_set_scroll_dir(l, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(l, LV_SCROLLBAR_MODE_OFF);
    return l;
}

static lv_obj_t *section(lv_obj_t *list, const char *title)
{
    lv_obj_t *t = kit_label(list, &kg_font_stencil_11, KG_C_TEXT_DIM, title);
    lv_obj_set_style_text_letter_space(t, 2, 0);
    lv_obj_set_style_pad_top(t, 5, 0);
    return t;
}

static const char *auth_name(kg_auth_t a)
{
    static const char *const N[] = {"open", "WEP", "WPA", "WPA2", "WPA3", "802.1X"};
    return N[a <= KG_AUTH_ENTERPRISE ? a : 0];
}

/* ========================================================= Wi-Fi analyser */

#define MAX_APS 24
#define G_X0 16
#define G_DX 17      /* pixels per 2.4 GHz channel (5 MHz) */
#define G_TOP 22
#define G_BASE 104   /* -95 dBm */

static kg_ap_t s_aps[MAX_APS];
static int s_n_aps;
static lv_obj_t *w_graph, *w_status, *w_list;
static bool w_scanning;
static uint32_t w_next_scan;
static uint8_t w_connected_bssid[6];

static const uint32_t PALETTE[] = {0xFF5A1F, 0x3FD47A, 0x5FA8FF, 0xFFB020, 0xC77DFF, 0x4DD8D0, 0xFF6F9C, 0xB9B2A3};

static lv_color_t ap_color(int i) { return lv_color_hex(PALETTE[i % 8]); }

static void graph_draw(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target(e), &a);
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.width = 1;
    d.color = KG_C_HAIRLINE;
    for (int dbm = -90; dbm <= -30; dbm += 20) { /* grid */
        lv_coord_t y = a.y1 + G_BASE - (dbm + 95) * (G_BASE - G_TOP) / 65;
        lv_point_t p1 = {a.x1 + 8, y}, p2 = {a.x2 - 4, y};
        lv_draw_line(ctx, &d, &p1, &p2);
    }
    /* One bell per network, ±2 channels wide (22 MHz), height = signal; strongest last (on top). */
    for (int i = s_n_aps - 1; i >= 0; i--) {
        const kg_ap_t *ap = &s_aps[i];
        if (ap->channel < 1 || ap->channel > 13) continue;
        bool mine = !memcmp(ap->bssid, w_connected_bssid, 6);
        d.color = ap_color(i);
        d.width = mine ? 3 : 2;
        d.round_start = d.round_end = 1;
        lv_coord_t cx = a.x1 + G_X0 + (ap->channel - 1) * G_DX;
        int h = (ap->rssi + 95) * (G_BASE - G_TOP) / 65;
        if (h < 4) h = 4;
        lv_point_t prev = {cx - 2 * G_DX, a.y1 + G_BASE};
        for (int k = -15; k <= 16; k++) {
            float u = k / 16.0f; /* −1 … 1 */
            lv_point_t p = {cx + (lv_coord_t)(u * 2 * G_DX), a.y1 + G_BASE - (lv_coord_t)(h * (1 - u * u))};
            lv_draw_line(ctx, &d, &prev, &p);
            prev = p;
        }
    }
    d.color = KG_C_TRACK;
    d.width = 1;
    lv_point_t b1 = {a.x1 + 4, a.y1 + G_BASE}, b2 = {a.x2 - 4, a.y1 + G_BASE};
    lv_draw_line(ctx, &d, &b1, &b2);
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.font = &kg_font_digits_11;
    ld.color = KG_C_TEXT_DIM;
    for (int ch = 1; ch <= 13; ch++) {
        char t[4];
        snprintf(t, sizeof t, "%d", ch);
        lv_coord_t x = a.x1 + G_X0 + (ch - 1) * G_DX;
        lv_area_t la = {x - 8, a.y1 + G_BASE + 2, x + 8, a.y1 + G_BASE + 14};
        ld.align = LV_TEXT_ALIGN_CENTER;
        ld.color = (ch == 1 || ch == 6 || ch == 11) ? KG_C_TEXT : KG_C_TEXT_DIM;
        lv_draw_label(ctx, &ld, &la, t, NULL);
    }
}

static void wifi_scan(void)
{
    w_scanning = hal_wifi_scan_start();
    lv_label_set_text(w_status, w_scanning ? "Scanning…" : "Wi-Fi busy, retrying…");
    w_next_scan = lv_tick_get() + (w_scanning ? 10000 : 1500);
}

static void wifi_tap(lv_event_t *e)
{
    (void)e;
    if (!w_scanning) wifi_scan();
}

static void wifi_collect(void)
{
    int n = hal_wifi_scan_count();
    if (n < 0) return;
    w_scanning = false;
    s_n_aps = 0;
    for (int i = 0; i < n && s_n_aps < MAX_APS; i++)
        if (hal_wifi_scan_ap(i, &s_aps[s_n_aps])) s_n_aps++;
    kg_link_t l;
    hal_net_link(&l);
    memcpy(w_connected_bssid, l.up ? l.bssid : (const uint8_t[6]){0}, 6);

    int per_ch[14] = {0}, busiest = 0;
    for (int i = 0; i < s_n_aps; i++)
        if (s_aps[i].channel >= 1 && s_aps[i].channel <= 13 && ++per_ch[s_aps[i].channel] > per_ch[busiest]) busiest = s_aps[i].channel;
    if (busiest) lv_label_set_text_fmt(w_status, "%d networks · busiest channel %d", s_n_aps, busiest);
    else lv_label_set_text_fmt(w_status, "%d networks", s_n_aps);
    lv_obj_invalidate(w_graph);

    lv_obj_clean(w_list);
    for (int i = 0; i < s_n_aps; i++) {
        const kg_ap_t *ap = &s_aps[i];
        lv_obj_t *r = lv_obj_create(w_list);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, 224, 24);
        lv_obj_set_style_bg_color(r, KG_C_SURFACE, 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(r, 6, 0);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        if (!memcmp(ap->bssid, w_connected_bssid, 6)) {
            lv_obj_set_style_border_color(r, KG_C_OK, 0);
            lv_obj_set_style_border_width(r, 1, 0);
        }
        lv_obj_t *sw = lv_obj_create(r);
        lv_obj_remove_style_all(sw);
        lv_obj_set_size(sw, 4, 14);
        lv_obj_set_style_radius(sw, 2, 0);
        lv_obj_set_style_bg_color(sw, ap->channel <= 13 ? ap_color(i) : KG_C_TEXT_DIM, 0);
        lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
        lv_obj_align(sw, LV_ALIGN_LEFT_MID, 6, 0);
        lv_obj_t *name = kit_label(r, &kg_font_text_16, ap->ssid[0] ? KG_C_TEXT : KG_C_TEXT_DIM, ap->ssid[0] ? ap->ssid : "(hidden)");
        lv_obj_set_width(name, 110);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 16, 0);
        lv_obj_t *info = kit_label(r, &kg_font_text_16, ap->auth == KG_AUTH_OPEN ? KG_C_WARN : KG_C_TEXT_DIM, "");
        lv_label_set_text_fmt(info, "%d · %d · %s", ap->channel, ap->rssi, auth_name(ap->auth));
        lv_obj_align(info, LV_ALIGN_RIGHT_MID, -8, 0);
    }
}

static void wifi_refresh(const kg_tool_state_t *st)
{
    (void)st;
    if (w_scanning) wifi_collect();
    else if ((int32_t)(lv_tick_get() - w_next_scan) >= 0) wifi_scan();
}

static lv_obj_t *wifi_create(void)
{
    lv_obj_t *scr = net_screen("WI-FI ANALYZER");
    w_graph = lv_obj_create(scr);
    lv_obj_remove_style_all(w_graph);
    lv_obj_set_pos(w_graph, 0, 0);
    lv_obj_set_size(w_graph, KG_W, G_BASE + 16);
    lv_obj_add_flag(w_graph, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(w_graph, graph_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(w_graph, wifi_tap, LV_EVENT_CLICKED, NULL);
    w_status = kit_label(scr, &kg_font_text_16, KG_C_TEXT_DIM, "");
    lv_obj_set_width(w_status, 224);
    lv_obj_set_style_text_align(w_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(w_status, 8, G_BASE + 16);
    w_list = column(scr, G_BASE + 38, KG_H - (G_BASE + 38));
    return scr;
}

static void wifi_show(void)
{
    s_n_aps = 0;
    lv_obj_clean(w_list);
    lv_obj_invalidate(w_graph);
    wifi_scan();
}

const net_tool_t NET_WIFI = {"Wi-Fi analyzer", "Networks and channels", wifi_create, wifi_show, wifi_refresh, false};

/* ================================================================== link */

static lv_obj_t *k_ssid, *k_bssid, *k_signal, *k_ip, *k_gw, *k_dns, *k_mac, *k_host, *k_pub[4], *k_status;

static void link_refresh(const kg_tool_state_t *st)
{
    kg_link_t l;
    hal_net_link(&l);
    char b[40], ip[16];
    lv_label_set_text(k_ssid, l.up ? l.ssid : "Not connected");
    lv_label_set_text(k_bssid, l.up ? kg_fmt_mac(l.bssid, b) : "—");
    if (l.up) {
        const char *q = l.rssi > -55 ? "excellent" : l.rssi > -67 ? "good" : l.rssi > -75 ? "fair" : "weak";
        lv_label_set_text_fmt(k_signal, "%d dBm · ch %d · %s", l.rssi, l.channel, q);
        lv_obj_set_style_text_color(k_signal, l.rssi > -67 ? KG_C_OK : KG_C_WARN, 0);
        lv_label_set_text_fmt(k_ip, "%s /%d", kg_ip_str(l.ip, ip), kg_mask_prefix(l.mask));
        lv_label_set_text(k_gw, kg_ip_str(l.gw, ip));
        lv_label_set_text(k_dns, kg_ip_str(l.dns, ip));
    } else {
        lv_label_set_text(k_signal, "—");
        lv_label_set_text(k_ip, "—");
        lv_label_set_text(k_gw, "—");
        lv_label_set_text(k_dns, "—");
    }
    lv_label_set_text(k_mac, kg_fmt_mac(l.mac, b));
    lv_label_set_text(k_host, l.hostname);

    bool mine = st->tool == KG_TOOL_PUBLIC;
    for (int i = 0; i < 4; i++) lv_label_set_text(k_pub[i], mine && i < st->n ? st->rows[i].b : "…");
    lv_label_set_text(k_status, mine ? st->status : "");
    lv_obj_set_style_text_color(k_status, mine && st->failed ? KG_C_ALERT : KG_C_TEXT_DIM, 0);
    if (mine && st->n) lv_obj_set_style_text_color(k_pub[0], KG_C_OK, 0);
}

static lv_obj_t *link_create(void)
{
    lv_obj_t *scr = net_screen("LINK");
    lv_obj_t *list = column(scr, 22, KG_H - 22);
    section(list, "WI-FI");
    kv_row(list, "Network", &k_ssid);
    kv_row(list, "Access point", &k_bssid);
    kv_row(list, "Signal", &k_signal);
    section(list, "ADDRESSING");
    kv_row(list, "Address", &k_ip);
    kv_row(list, "Gateway", &k_gw);
    kv_row(list, "DNS", &k_dns);
    kv_row(list, "MAC", &k_mac);
    kv_row(list, "Hostname", &k_host);
    section(list, "INTERNET");
    static const char *const K[] = {"Public IP", "Provider", "Network", "Location"};
    for (int i = 0; i < 4; i++) kv_row(list, K[i], &k_pub[i]);
    k_status = kit_label(list, &kg_font_text_16, KG_C_TEXT_DIM, "");
    return scr;
}

static void link_show(void) { hal_tool_start(KG_TOOL_PUBLIC, ""); }

const net_tool_t NET_LINK = {"Link", "Address, gateway, public IP", link_create, link_show, link_refresh, false};

/* ================================================================== ping */

#define P_GX 12
#define P_GY 132
#define P_GW 216
#define P_GH 50

static lv_obj_t *p_chip, *p_big, *p_unit, *p_graph, *p_stats, *p_status, *p_run;

static void ping_graph_draw(lv_event_t *e)
{
    const kg_tool_state_t *st = net_state();
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target(e), &a);
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.radius = 1;
    if (st->tool != KG_TOOL_PING) return;
    int32_t top = 10000; /* at least 10 ms full scale */
    for (int i = 0; i < st->hist_n; i++)
        if (st->hist[i] > top) top = st->hist[i];
    const int bw = P_GW / KG_PING_HIST;
    for (int i = 0; i < st->hist_n; i++) {
        lv_coord_t x = a.x1 + (KG_PING_HIST - st->hist_n + i) * bw;
        if (st->hist[i] < 0) { /* lost: full-height red tick */
            r.bg_color = KG_C_ALERT;
            lv_area_t b = {x, a.y1, x + bw - 2, a.y2};
            r.bg_opa = LV_OPA_40;
            lv_draw_rect(ctx, &r, &b);
            r.bg_opa = LV_OPA_COVER;
            continue;
        }
        lv_coord_t h = (lv_coord_t)((int64_t)st->hist[i] * P_GH / top);
        if (h < 2) h = 2;
        r.bg_color = i == st->hist_n - 1 ? KG_C_ACCENT : KG_C_OK;
        lv_area_t b = {x, a.y2 - h, x + bw - 2, a.y2};
        lv_draw_rect(ctx, &r, &b);
    }
}

static void ping_start(void) { hal_tool_start(KG_TOOL_PING, net_target()); }

static void ping_run_cb(lv_event_t *e)
{
    (void)e;
    const kg_tool_state_t *st = net_state();
    if (st->tool == KG_TOOL_PING && st->running) hal_tool_stop();
    else ping_start();
}

static void ping_refresh(const kg_tool_state_t *st)
{
    bool mine = st->tool == KG_TOOL_PING;
    net_run_button_set(p_run, mine && st->running);
    char b[64], x[16], y[16], z[16];
    if (mine && st->recv) {
        if (st->hist_n && st->hist[st->hist_n - 1] < 0) lv_label_set_text(p_big, "--");
        else {
            snprintf(b, sizeof b, st->last_us < 100000 ? "%.1f" : "%.0f", st->last_us / 1000.0);
            lv_label_set_text(p_big, b);
        }
        snprintf(b, sizeof b, "min %s · avg %s · max %s", kg_fmt_rtt(st->min_us, x, sizeof x),
                 kg_fmt_rtt(st->sum_us / st->recv, y, sizeof y), kg_fmt_rtt(st->max_us, z, sizeof z));
        for (char *c = b; (c = strstr(c, " ms ·")); ) memmove(c, c + 3, strlen(c + 3) + 1); /* one "ms" at the end */
        lv_label_set_text(p_stats, b);
    } else {
        lv_label_set_text(p_big, "--");
        lv_label_set_text(p_stats, "ICMP echo, one per second");
    }
    lv_label_set_text(p_status, mine ? st->status : "");
    lv_obj_set_style_text_color(p_status, mine && st->failed ? KG_C_ALERT : KG_C_TEXT_DIM, 0);
    if (mine && st->sent && st->recv < st->sent) lv_obj_set_style_text_color(p_status, KG_C_WARN, 0);
    lv_obj_align_to(p_unit, p_big, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -10);
    lv_obj_invalidate(p_graph);
}

static lv_obj_t *ping_create(void)
{
    lv_obj_t *scr = net_screen("PING");
    p_chip = net_target_chip(scr, ping_start);
    p_big = kit_label(scr, &kg_font_digits_56, KG_C_TEXT, "--");
    lv_obj_align(p_big, LV_ALIGN_TOP_MID, -10, 56);
    p_unit = kit_label(scr, &kg_font_label_20, KG_C_TEXT_DIM, "ms");
    p_graph = lv_obj_create(scr);
    lv_obj_remove_style_all(p_graph);
    lv_obj_set_pos(p_graph, P_GX, P_GY);
    lv_obj_set_size(p_graph, P_GW, P_GH);
    lv_obj_set_style_border_side(p_graph, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(p_graph, KG_C_TRACK, 0);
    lv_obj_set_style_border_width(p_graph, 1, 0);
    lv_obj_add_event_cb(p_graph, ping_graph_draw, LV_EVENT_DRAW_MAIN, NULL);
    p_stats = kit_label(scr, &kg_font_text_16, KG_C_TEXT, "");
    lv_obj_set_width(p_stats, 230);
    lv_obj_set_style_text_align(p_stats, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(p_stats, 5, 112);
    p_status = kit_label(scr, &kg_font_text_16, KG_C_TEXT_DIM, "");
    lv_obj_set_width(p_status, 230);
    lv_obj_set_style_text_align(p_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(p_status, 5, P_GY + P_GH + 2);
    p_run = net_run_button(scr, ping_run_cb);
    return scr;
}

static void ping_show(void)
{
    net_target_chip_update(p_chip);
    ping_start();
}

const net_tool_t NET_PING = {"Ping", "Latency and packet loss", ping_create, ping_show, ping_refresh, false};

/* ============================================================ speed test */

#define SPEED_FULL 10.0f /* Mbit/s at the end of the dial: the watch tops out around 2 (D52) */

static lv_obj_t *s_arc, *s_big, *s_info, *s_graph, *s_run2;

static void speed_graph_draw(lv_event_t *e)
{
    const kg_tool_state_t *st = net_state();
    if (st->tool != KG_TOOL_SPEED || !st->speed_n) return;
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target(e), &a);
    float top = st->peak_mbps > 1 ? st->peak_mbps : 1;
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.bg_color = KG_C_RAIN;
    int w = lv_area_get_width(&a) / KG_SPEED_HIST;
    for (int i = 0; i < st->speed_n; i++) {
        lv_coord_t h = (lv_coord_t)(st->speed_hist[i] / top * lv_area_get_height(&a));
        if (h < 1) h = 1;
        lv_area_t b = {a.x1 + i * w, a.y2 - h, a.x1 + i * w + w - 2, a.y2};
        lv_draw_rect(ctx, &r, &b);
    }
}

static void speed_run_cb(lv_event_t *e)
{
    (void)e;
    const kg_tool_state_t *st = net_state();
    if (st->tool == KG_TOOL_SPEED && st->running) hal_tool_stop();
    else hal_tool_start(KG_TOOL_SPEED, "");
}

static void speed_refresh(const kg_tool_state_t *st)
{
    bool mine = st->tool == KG_TOOL_SPEED;
    net_run_button_set(s_run2, mine && st->running);
    float v = mine ? st->mbps : 0;
    lv_arc_set_value(s_arc, (int16_t)(v >= SPEED_FULL ? 1000 : v * 1000 / SPEED_FULL));
    char b[112];
    snprintf(b, sizeof b, v >= 100 ? "%.0f" : "%.1f", (double)v);
    lv_label_set_text(s_big, mine && st->bytes ? b : "--");
    if (mine && st->bytes)
        snprintf(b, sizeof b, "%s\npeak %.1f · latency %lu ms", st->status, (double)st->peak_mbps, (unsigned long)st->ttfb_ms);
    else if (mine)
        snprintf(b, sizeof b, "%s", st->status);
    else
        snprintf(b, sizeof b, "10-second download\nfrom Cloudflare");
    lv_label_set_text(s_info, b);
    lv_obj_set_style_text_color(s_info, mine && st->failed ? KG_C_ALERT : KG_C_TEXT_DIM, 0);
    lv_obj_invalidate(s_graph);
}

static lv_obj_t *speed_create(void)
{
    lv_obj_t *scr = net_screen("SPEED TEST");
    s_arc = lv_arc_create(scr);
    lv_obj_set_size(s_arc, 150, 150);
    lv_obj_align(s_arc, LV_ALIGN_TOP_MID, 0, 22);
    lv_arc_set_rotation(s_arc, 135);
    lv_arc_set_bg_angles(s_arc, 0, 270);
    lv_arc_set_range(s_arc, 0, 1000);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(s_arc, KG_C_BTN, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, KG_C_RAIN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc, 8, LV_PART_INDICATOR);
    s_big = kit_label(scr, &kg_font_digits_56, KG_C_TEXT, "--");
    lv_obj_set_width(s_big, 140);
    lv_obj_set_style_text_align(s_big, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_big, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_t *unit = kit_label(scr, &kg_font_stencil_11, KG_C_TEXT_DIM, "MBIT/S");
    lv_obj_set_style_text_letter_space(unit, 2, 0);
    lv_obj_align(unit, LV_ALIGN_TOP_MID, 0, 122);
    lv_obj_t *z = kit_label(scr, &kg_font_digits_11, KG_C_TEXT_DIM, "0");
    lv_obj_set_pos(z, 64, 152);
    lv_obj_t *f = kit_label(scr, &kg_font_digits_11, KG_C_TEXT_DIM, "10");
    lv_obj_set_pos(f, 166, 152);
    s_graph = lv_obj_create(scr);
    lv_obj_remove_style_all(s_graph);
    lv_obj_set_size(s_graph, 80, 18);
    lv_obj_align(s_graph, LV_ALIGN_TOP_MID, 0, 138);
    lv_obj_add_event_cb(s_graph, speed_graph_draw, LV_EVENT_DRAW_MAIN, NULL);
    s_info = kit_label(scr, &kg_font_text_16, KG_C_TEXT_DIM, "");
    lv_obj_set_width(s_info, 230);
    lv_obj_set_style_text_align(s_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_info, 5, 160);
    s_run2 = net_run_button(scr, speed_run_cb);
    return scr;
}

const net_tool_t NET_SPEED = {"Speed test", "Download throughput", speed_create, NULL, speed_refresh, false};

/* ======================================================= subnet calculator */

static lv_obj_t *c_input, *c_vals[8], *c_pad, *c_ta;
static char c_text[24] = "192.168.1.20/24";

static void subnet_update(void)
{
    lv_label_set_text_fmt(lv_obj_get_child(c_input, 0), "%s  >", c_text);
    kg_subnet_t s;
    bool ok = kg_subnet_parse(c_text, &s);
    char b[16];
    for (int i = 0; i < 8; i++) lv_label_set_text(c_vals[i], ok ? "" : "—");
    if (!ok) {
        lv_label_set_text(c_vals[0], "invalid");
        return;
    }
    lv_label_set_text_fmt(c_vals[0], "%s /%u", kg_ip_str(s.network, b), s.prefix);
    lv_label_set_text(c_vals[1], kg_ip_str(s.mask, b));
    lv_label_set_text(c_vals[2], kg_ip_str(~s.mask, b));
    lv_label_set_text(c_vals[3], kg_ip_str(s.first, b));
    lv_label_set_text(c_vals[4], kg_ip_str(s.last, b));
    lv_label_set_text(c_vals[5], kg_ip_str(s.broadcast, b));
    if (s.hosts >= 10000000) lv_label_set_text_fmt(c_vals[6], "%.1f million", s.hosts / 1e6);
    else lv_label_set_text_fmt(c_vals[6], "%lu", (unsigned long)s.hosts);
    lv_label_set_text(c_vals[7], s.is_private ? "private" : "public");
}

static void prefix_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    kg_subnet_t s;
    if (!kg_subnet_parse(c_text, &s)) return;
    int p = s.prefix + d;
    if (p < 0 || p > 32) return;
    char ip[16];
    snprintf(c_text, sizeof c_text, "%s/%d", kg_ip_str(s.ip, ip), p);
    subnet_update();
}

static const char *PAD_MAP[] = {"1", "2", "3", LV_SYMBOL_BACKSPACE, "\n", "4", "5", "6", ".", "\n",
                                      "7", "8", "9", "/", "\n", "C", "0", LV_SYMBOL_OK, ""};

static void pad_cb(lv_event_t *e)
{
    lv_obj_t *m = lv_event_get_target(e);
    const char *t = lv_btnmatrix_get_btn_text(m, lv_btnmatrix_get_selected_btn(m));
    if (!t) return;
    kit_tick();
    if (!strcmp(t, LV_SYMBOL_BACKSPACE)) lv_textarea_del_char(c_ta);
    else if (!strcmp(t, "C")) lv_textarea_set_text(c_ta, "");
    else if (!strcmp(t, LV_SYMBOL_OK)) {
        snprintf(c_text, sizeof c_text, "%s", lv_textarea_get_text(c_ta));
        lv_obj_add_flag(c_pad, LV_OBJ_FLAG_HIDDEN);
        subnet_update();
    } else lv_textarea_add_text(c_ta, t);
}

static void input_cb(lv_event_t *e)
{
    (void)e;
    lv_textarea_set_text(c_ta, c_text);
    lv_obj_clear_flag(c_pad, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *subnet_create(void)
{
    lv_obj_t *scr = net_screen("SUBNET");
    c_input = kit_button(scr, "", false, 166, 28, input_cb, NULL);
    lv_obj_set_pos(c_input, 37, 20);
    lv_obj_t *minus = kit_button(scr, "−", false, 30, 28, prefix_cb, (void *)(intptr_t)-1);
    lv_obj_set_pos(minus, 4, 20);
    lv_obj_t *plus = kit_button(scr, "+", false, 30, 28, prefix_cb, (void *)(intptr_t)1);
    lv_obj_set_pos(plus, KG_W - 34, 20);
    lv_obj_set_style_text_font(lv_obj_get_child(c_input, 0), &kg_font_text_16, 0);
    lv_obj_t *list = column(scr, 54, KG_H - 54);
    static const char *const K[] = {"Network", "Netmask", "Wildcard", "First host", "Last host", "Broadcast", "Hosts", "Range"};
    for (int i = 0; i < 8; i++) kv_row(list, K[i], &c_vals[i]);

    c_pad = lv_obj_create(scr);
    lv_obj_remove_style_all(c_pad);
    lv_obj_set_size(c_pad, KG_W, KG_H);
    lv_obj_set_style_bg_color(c_pad, KG_C_BG, 0);
    lv_obj_set_style_bg_opa(c_pad, LV_OPA_COVER, 0);
    lv_obj_add_flag(c_pad, LV_OBJ_FLAG_HIDDEN);
    c_ta = lv_textarea_create(c_pad);
    lv_textarea_set_one_line(c_ta, true);
    lv_textarea_set_accepted_chars(c_ta, "0123456789./");
    lv_textarea_set_max_length(c_ta, 18);
    lv_obj_set_size(c_ta, 228, 38);
    lv_obj_align(c_ta, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_text_font(c_ta, &kg_font_label_20, 0);
    lv_obj_set_style_bg_color(c_ta, KG_C_SURFACE, 0);
    lv_obj_set_style_text_color(c_ta, KG_C_TEXT, 0);
    lv_obj_set_style_border_color(c_ta, KG_C_ACCENT, 0);
    lv_obj_t *pad = lv_btnmatrix_create(c_pad);
    lv_btnmatrix_set_map(pad, PAD_MAP);
    lv_obj_set_size(pad, KG_W, 186);
    lv_obj_align(pad, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(pad, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(pad, KG_C_BG, 0);
    lv_obj_set_style_border_width(pad, 0, 0);
    lv_obj_set_style_pad_all(pad, 3, 0);
    lv_obj_set_style_pad_gap(pad, 4, 0);
    lv_obj_set_style_bg_color(pad, KG_C_BTN, LV_PART_ITEMS);
    lv_obj_set_style_text_color(pad, KG_C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_border_width(pad, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(pad, 8, LV_PART_ITEMS);
    lv_obj_add_event_cb(pad, pad_cb, LV_EVENT_VALUE_CHANGED, NULL);
    subnet_update();
    return scr;
}

static void subnet_show(void)
{
    kg_link_t l;
    hal_net_link(&l);
    if (l.up) { /* start from this watch's own network */
        char ip[16];
        snprintf(c_text, sizeof c_text, "%s/%d", kg_ip_str(l.ip, ip), kg_mask_prefix(l.mask));
    }
    lv_obj_add_flag(c_pad, LV_OBJ_FLAG_HIDDEN);
    subnet_update();
}

const net_tool_t NET_SUBNET = {"Subnet calculator", "CIDR, masks, host range", subnet_create, subnet_show, NULL, true};
