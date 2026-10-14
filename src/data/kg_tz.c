#include "kg_tz.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

const kg_zone_t KG_ZONES[] = {
    {"Honolulu", "HNL", "HST10"},
    {"Vancouver", "YVR", "PST8PDT,M3.2.0,M11.1.0"},
    {"Montréal", "YUL", "EST5EDT,M3.2.0,M11.1.0"},
    {"UTC", "Z", "UTC0"}, /* "Zulu", as aviation writes it */
    {"London", "LHR", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Paris", "CDG", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Dubai", "DXB", "<+04>-4"},
    {"Mumbai", "BOM", "IST-5:30"},
    {"Singapore", "SIN", "<+08>-8"},
    {"Tokyo", "HND", "JST-9"},
    {"Sydney", "SYD", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Auckland", "AKL", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};
const int KG_N_ZONES = sizeof KG_ZONES / sizeof KG_ZONES[0];

/* Howard Hinnant's civil-date algorithms. */
int64_t kg_days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

void kg_civil_from_days(int64_t z, int *y, int *m, int *d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400) + (*m <= 2);
}

typedef struct {
    int month, week, wday; /* Mm.w.d */
    int32_t secs;          /* local time of the change */
} rule_t;

static const char *skip_name(const char *p)
{
    if (*p == '<') {
        while (*p && *p != '>') p++;
        return *p ? p + 1 : p;
    }
    while (isalpha((unsigned char)*p)) p++;
    return p;
}

/* "[+-]hh[:mm[:ss]]" → seconds; *ok = false when absent. */
static int32_t parse_hms(const char **pp, bool *ok)
{
    const char *p = *pp;
    int sign = 1;
    if (*p == '+' || *p == '-') sign = *p++ == '-' ? -1 : 1;
    if (!isdigit((unsigned char)*p)) {
        *ok = false;
        return 0;
    }
    int32_t v = (int32_t)strtol(p, (char **)&p, 10) * 3600;
    if (*p == ':') v += (int32_t)strtol(p + 1, (char **)&p, 10) * 60;
    if (*p == ':') v += (int32_t)strtol(p + 1, (char **)&p, 10);
    *pp = p;
    *ok = true;
    return sign * v;
}

static bool parse_rule(const char **pp, rule_t *r)
{
    const char *p = *pp;
    if (*p++ != ',' || *p++ != 'M') return false;
    r->month = (int)strtol(p, (char **)&p, 10);
    if (*p++ != '.') return false;
    r->week = (int)strtol(p, (char **)&p, 10);
    if (*p++ != '.') return false;
    r->wday = (int)strtol(p, (char **)&p, 10);
    r->secs = 7200;
    if (*p == '/') {
        p++;
        bool ok;
        r->secs = parse_hms(&p, &ok);
    }
    *pp = p;
    return r->month >= 1 && r->month <= 12 && r->week >= 1 && r->week <= 5 && r->wday >= 0 && r->wday <= 6;
}

/* Local seconds since the epoch at which the rule fires in year y. */
static int64_t rule_local(int y, const rule_t *r)
{
    int64_t first = kg_days_from_civil(y, r->month, 1);
    int wd_first = (int)((first + 4) % 7); /* 1970-01-01 was a Thursday */
    if (wd_first < 0) wd_first += 7;
    int day = 1 + (r->wday - wd_first + 7) % 7 + (r->week - 1) * 7;
    static const int DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int dim = DAYS[r->month - 1] + (r->month == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)));
    while (day > dim) day -= 7; /* week 5 = last */
    return (first + day - 1) * 86400 + r->secs;
}

int32_t kg_tz_offset(const char *tz, time_t t, bool *dst)
{
    if (dst) *dst = false;
    const char *p = skip_name(tz);
    bool ok;
    int32_t std_east = -parse_hms(&p, &ok);
    if (!ok) return 0;
    if (!*p) return std_east;
    p = skip_name(p);
    int32_t dst_east = std_east + 3600;
    if (*p && *p != ',') {
        int32_t v = parse_hms(&p, &ok);
        if (ok) dst_east = -v;
    }
    rule_t start, end;
    if (!parse_rule(&p, &start) || !parse_rule(&p, &end)) return std_east;

    int y, m, d;
    kg_civil_from_days(((int64_t)t + std_east) / 86400, &y, &m, &d);
    int64_t s = rule_local(y, &start) - std_east, e = rule_local(y, &end) - dst_east;
    bool summer = s < e ? (t >= s && t < e) : !(t >= e && t < s); /* southern hemisphere: wraps */
    if (dst) *dst = summer;
    return summer ? dst_east : std_east;
}

void kg_tz_local(const char *tz, time_t t, struct tm *out, bool *dst)
{
    time_t local = t + kg_tz_offset(tz, t, dst);
    gmtime_r(&local, out);
}
