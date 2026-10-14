#include "kg_sky.h"
#include "kg_tz.h"
#include <math.h>

#define DEG (M_PI / 180.0)
#define JD_UNIX 2440587.5
#define SYNODIC 29.530588853

static time_t jd_to_unix(double jd) { return (time_t)llround((jd - JD_UNIX) * 86400.0); }

void kg_sun_day(int y, int m, int d, double lat, double lon, kg_sun_t *out)
{
    /* Days since J2000.0 (2000-01-01 12:00), corrected to the observer's mean solar noon. */
    double n = (double)(kg_days_from_civil(y, m, d) - kg_days_from_civil(2000, 1, 1));
    double jstar = n - lon / 360.0;
    double M = fmod(357.5291 + 0.98560028 * jstar, 360.0);
    double C = 1.9148 * sin(M * DEG) + 0.0200 * sin(2 * M * DEG) + 0.0003 * sin(3 * M * DEG);
    double lambda = fmod(M + C + 180.0 + 102.9372, 360.0);
    double transit = 2451545.0 + jstar + 0.0053 * sin(M * DEG) - 0.0069 * sin(2 * lambda * DEG);
    double sin_dec = sin(lambda * DEG) * sin(23.4397 * DEG), cos_dec = cos(asin(sin_dec));

    out->noon = jd_to_unix(transit);
    out->polar_day = out->polar_night = false;
    double cos_w = (sin(-0.833 * DEG) - sin(lat * DEG) * sin_dec) / (cos(lat * DEG) * cos_dec);
    if (cos_w < -1 || cos_w > 1) {
        out->polar_day = cos_w < -1;
        out->polar_night = cos_w > 1;
        out->rise = out->set = 0;
    } else {
        double w = acos(cos_w) / DEG;
        out->rise = jd_to_unix(transit - w / 360.0);
        out->set = jd_to_unix(transit + w / 360.0);
    }
    double cos_c = (sin(-6.0 * DEG) - sin(lat * DEG) * sin_dec) / (cos(lat * DEG) * cos_dec);
    if (cos_c < -1 || cos_c > 1) {
        out->dawn = out->dusk = 0;
    } else {
        double w = acos(cos_c) / DEG;
        out->dawn = jd_to_unix(transit - w / 360.0);
        out->dusk = jd_to_unix(transit + w / 360.0);
    }
}

/* Moon phase 0..1 (0 new, 0.5 full) from the Sun–Moon elongation, with the main periodic
 * terms (Meeus, Astronomical Algorithms ch. 48: about an hour on the phase times). */
static double phase_at(time_t t)
{
    double T = ((double)t / 86400.0 + JD_UNIX - 2451545.0) / 36525.0;
    double D = 297.8501921 + 445267.1114034 * T, M = 357.5291092 + 35999.0502909 * T,
           Mp = 134.9633964 + 477198.8675055 * T;
    double i = 180 - D - 6.289 * sin(Mp * DEG) + 2.100 * sin(M * DEG) - 1.274 * sin((2 * D - Mp) * DEG) -
               0.658 * sin(2 * D * DEG) - 0.214 * sin(2 * Mp * DEG) - 0.110 * sin(D * DEG);
    double psi = fmod(180 - i, 360.0); /* elongation */
    if (psi < 0) psi += 360;
    return psi / 360.0;
}

/* Next instant after t when the phase crosses `target` (hourly steps, then bisection). */
static time_t next_phase(time_t t, double target)
{
    double prev = fmod(phase_at(t) - target + 1, 1.0);
    for (time_t s = t + 3600; s < t + 31 * 86400; s += 3600) {
        double cur = fmod(phase_at(s) - target + 1, 1.0);
        if (cur < prev) { /* wrapped past the target in this hour */
            time_t lo = s - 3600, hi = s;
            while (hi - lo > 60) {
                time_t mid = lo + (hi - lo) / 2;
                if (fmod(phase_at(mid) - target + 1, 1.0) < 0.5) hi = mid;
                else lo = mid;
            }
            return hi;
        }
        prev = cur;
    }
    return 0;
}

void kg_moon(time_t t, kg_moon_t *out)
{
    double phase = phase_at(t);
    out->phase = phase;
    out->age = phase * SYNODIC;
    out->illum = (1 - cos(2 * M_PI * phase)) / 2;
    static const char *const NAMES[] = {"New moon", "Waxing crescent", "First quarter", "Waxing gibbous",
                                        "Full moon", "Waning gibbous", "Last quarter", "Waning crescent"};
    out->name = NAMES[(int)floor(phase * 8 + 0.5) % 8];
    out->next_full = next_phase(t, 0.5);
    out->next_new = next_phase(t, 0.0);
}
