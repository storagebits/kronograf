#include "kg_ancs.h"
#include <stdio.h>
#include <string.h>

enum { ATTR_APP = 0, ATTR_TITLE = 1, ATTR_MESSAGE = 3, ATTR_DATE = 5 };
#define N_ATTRS 4

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

bool kg_ancs_parse_event(const uint8_t *d, size_t len, kg_ancs_event_t *out)
{
    if (len < 8) return false;
    *out = (kg_ancs_event_t){d[0], d[1], d[2], d[3], rd32(d + 4)};
    return out->event <= KG_ANCS_REMOVED;
}

static void put_uid(uint8_t *p, uint32_t uid)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(uid >> (8 * i));
}

size_t kg_ancs_build_request(uint8_t *o, size_t cap, uint32_t uid)
{
    if (cap < 19) return 0;
    size_t n = 0;
    o[n++] = 0; /* CommandIDGetNotificationAttributes */
    put_uid(o + n, uid);
    n += 4;
    o[n++] = ATTR_APP;
    o[n++] = ATTR_TITLE; /* titre et message : longueur maximale demandée (16 bits) */
    o[n++] = sizeof(((kg_notif_t *)0)->title) - 1;
    o[n++] = 0;
    o[n++] = ATTR_MESSAGE;
    o[n++] = sizeof(((kg_notif_t *)0)->message) - 1;
    o[n++] = 0;
    o[n++] = ATTR_DATE;
    return n;
}

size_t kg_ancs_build_action(uint8_t *o, size_t cap, uint32_t uid, uint8_t action)
{
    if (cap < 6) return 0;
    o[0] = 2; /* CommandIDPerformNotificationAction */
    put_uid(o + 1, uid);
    o[5] = action;
    return 6;
}

void kg_ancs_rx_reset(kg_ancs_rx_t *rx) { rx->len = 0; }

bool kg_ancs_rx_feed(kg_ancs_rx_t *rx, const uint8_t *d, size_t len, kg_notif_t *out)
{
    if (rx->len + len > sizeof rx->buf) { /* réponse plus longue que prévu : on repart de zéro */
        rx->len = 0;
        if (len > sizeof rx->buf) return false;
    }
    memcpy(rx->buf + rx->len, d, len);
    rx->len += len;

    const uint8_t *p = rx->buf, *end = rx->buf + rx->len;
    if (rx->len < 5 || p[0] != 0) {
        if (rx->len >= 1 && p[0] != 0) rx->len = 0; /* pas une réponse d'attributs */
        return false;
    }
    kg_notif_t n;
    memset(&n, 0, sizeof n);
    n.uid = rd32(p + 1);
    p += 5;
    for (int k = 0; k < N_ATTRS; k++) {
        if (end - p < 3) return false; /* attendre la suite */
        uint8_t id = p[0];
        size_t l = p[1] | p[2] << 8;
        if ((size_t)(end - p - 3) < l) return false;
        const char *s = (const char *)p + 3;
        switch (id) {
        case ATTR_APP: kg_text_sanitize(n.app, sizeof n.app, s, l); break;
        case ATTR_TITLE: kg_text_sanitize(n.title, sizeof n.title, s, l); break;
        case ATTR_MESSAGE: kg_text_sanitize(n.message, sizeof n.message, s, l); break;
        case ATTR_DATE: kg_text_sanitize(n.date, sizeof n.date, s, l); break;
        default: break;
        }
        p += 3 + l;
    }
    rx->len = 0;
    *out = n;
    return true;
}

/* ----------------------------------------------------------------- texte */

static bool in_font(uint32_t c)
{
    if (c >= 0x20 && c <= 0x7E) return true;
    if (c >= 0xA0 && c <= 0xFF) return true;
    switch (c) {
    case 0x152: case 0x153: case 0x2013: case 0x2014: case 0x2018: case 0x2019: case 0x201C: case 0x201D:
    case 0x2022: case 0x2026: case 0x20AC: case 0x2212: return true;
    default: return false;
    }
}

void kg_text_sanitize(char *out, size_t n, const char *in, size_t in_len)
{
    size_t o = 0;
    const uint8_t *p = (const uint8_t *)in, *end = p + in_len;
    bool space = true; /* pas d'espace en tête */
    while (p < end && *p) {
        uint32_t c;
        int k;
        if (*p < 0x80) c = *p, k = 1;
        else if ((*p & 0xE0) == 0xC0) c = *p & 0x1F, k = 2;
        else if ((*p & 0xF0) == 0xE0) c = *p & 0x0F, k = 3;
        else if ((*p & 0xF8) == 0xF0) c = *p & 0x07, k = 4;
        else { p++; continue; }
        if (end - p < k) break;
        for (int i = 1; i < k; i++) c = c << 6 | (p[i] & 0x3F);
        const uint8_t *src = p;
        p += k;

        if (c == '\r' || c == '\t') c = ' ';
        if (c == 0xA0 || c == 0x202F) c = ' '; /* espaces insécables → espace simple */
        if (c == '\n') {
            if (o && out[o - 1] == '\n') continue;
            while (o && out[o - 1] == ' ') o--;
            if (o + 1 >= n) break;
            out[o++] = '\n';
            space = true;
            continue;
        }
        if (c == ' ') {
            if (space) continue;
            space = true;
            if (o + 1 >= n) break;
            out[o++] = ' ';
            continue;
        }
        if (!in_font(c)) continue; /* émojis, symboles : retirés */
        if (o + (size_t)k >= n) break;
        memcpy(out + o, src, (size_t)k);
        o += (size_t)k;
        space = false;
    }
    while (o && (out[o - 1] == ' ' || out[o - 1] == '\n')) o--;
    out[o] = 0;
}

static const struct {
    const char *bundle, *name;
} APPS[] = {
    {"com.apple.MobileSMS", "Messages"},      {"com.apple.mobilephone", "Téléphone"},
    {"com.apple.mobilemail", "Mail"},         {"com.apple.mobilecal", "Calendrier"},
    {"com.apple.reminders", "Rappels"},       {"com.apple.facetime", "FaceTime"},
    {"com.apple.Passbook", "Cartes"},         {"com.apple.Health", "Santé"},
    {"com.apple.news", "News"},               {"com.apple.weather", "Météo"},
    {"com.apple.Maps", "Plans"},              {"com.apple.Home", "Maison"},
    {"com.apple.findmy", "Localiser"},        {"net.whatsapp.WhatsApp", "WhatsApp"},
    {"com.facebook.Messenger", "Messenger"},  {"ph.telegra.Telegraph", "Telegram"},
    {"org.whispersystems.signal", "Signal"},  {"com.tinyspeck.chatlyio", "Slack"},
    {"com.microsoft.Office.Outlook", "Outlook"}, {"com.google.Gmail", "Gmail"},
    {"com.burbn.instagram", "Instagram"},     {"com.atebits.Tweetie2", "X"},
    {"com.hammerandchisel.discord", "Discord"},
};

const char *kg_ancs_app_name(const char *bundle, char *buf, size_t n)
{
    for (size_t i = 0; i < sizeof APPS / sizeof APPS[0]; i++)
        if (!strcmp(bundle, APPS[i].bundle)) return APPS[i].name;
    const char *dot = strrchr(bundle, '.');
    snprintf(buf, n, "%s", dot && dot[1] ? dot + 1 : bundle);
    if (buf[0] >= 'a' && buf[0] <= 'z') buf[0] = (char)(buf[0] - 32);
    return buf;
}
